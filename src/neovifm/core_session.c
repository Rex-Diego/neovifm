/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
# include <fcntl.h>
# include <sys/event.h>
# include <sys/select.h>
# include <unistd.h>
#endif

#include "snapshot_json.h"
#include "undo_bridge.h"
#include "workspace_session.h"
#include "../utils/parson.h"

#define NV_SESSION_MAX_COMMAND_BYTES (16U*1024U)

typedef struct nv_pending_action_context_t
{
	uint64_t task_id;
	int active;
	nv_session_pane_t source_pane;
	uint64_t source_tab_id;
	int has_destination;
	nv_session_pane_t destination_pane;
	uint64_t destination_tab_id;
	char *undo_path;
	nv_fs_identity_t undo_parent_identity;
	struct nv_pending_action_context_t *next;
} nv_pending_action_context_t;

static int write_line(const char json[]);
static int write_workspace(const nv_workspace_session_t *session,
		unsigned int output_sequence, unsigned int command_sequence,
		const char trigger[]);
static int write_command_error(const nv_snapshot_error_t *error,
		unsigned int output_sequence, unsigned int command_sequence);
static int parse_command(const char line[], unsigned int previous_sequence,
		unsigned int *sequence, nv_session_command_t *command);
static int discard_command_tail(void);
static int set_error(nv_snapshot_error_t *error, const char code[],
		const char message[]);
static int process_command_line(nv_workspace_session_t *session, char line[],
		size_t line_capacity, unsigned int *output_sequence,
		unsigned int *command_sequence, int *directory_changed,
		nv_preview_queue_t *preview_queue, uint64_t *preview_generation,
		nv_action_queue_t *action_queue,
		nv_pending_action_context_t **pending_actions);
static int submit_active_preview(const nv_workspace_session_t *session,
		nv_preview_queue_t *queue, uint64_t *generation);
static int drain_preview_events(nv_preview_queue_t *queue,
		unsigned int *output_sequence);
static int drain_action_events(nv_action_queue_t *queue,
		nv_workspace_session_t *session, unsigned int *output_sequence,
		unsigned int command_sequence, nv_preview_queue_t *preview_queue,
		uint64_t *preview_generation,
		nv_pending_action_context_t **pending_actions);
static int command_is_action(nv_session_command_kind_t kind);
static int refresh_action_tab(nv_workspace_session_t *session,
		nv_session_pane_t pane, uint64_t tab_id, nv_snapshot_error_t *error);

#ifdef __APPLE__
typedef struct
{
	int queue;
	int left_fd;
	int right_fd;
} nv_session_watcher_t;

static int hex_digit(char character);
static char *hex_decode(const char hex[]);
static int watcher_open_pane(nv_session_watcher_t *watcher,
		const nv_workspace_session_t *session, nv_session_pane_t pane);
static void watcher_stop_pane(nv_session_watcher_t *watcher,
		nv_session_pane_t pane);
static int watcher_init(nv_session_watcher_t *watcher,
		const nv_workspace_session_t *session);
static void watcher_free(nv_session_watcher_t *watcher);
static int watcher_handle_events(nv_session_watcher_t *watcher,
		nv_workspace_session_t *session, unsigned int *output_sequence,
		unsigned int command_sequence, int *stdin_ready,
		nv_action_queue_t *action_queue);
static int poll_stdin(int *stdin_ready);
#endif

static int
set_error(nv_snapshot_error_t *error, const char code[], const char message[])
{
	nv_snapshot_error_free(error);
	error->code = strdup(code);
	error->message = strdup(message);
	if(error->code == NULL || error->message == NULL)
	{
		nv_snapshot_error_free(error);
	}
	return -1;
}

static int
write_line(const char json[])
{
	if(json == NULL || fputs(json, stdout) == EOF || fputc('\n', stdout) == EOF ||
			fflush(stdout) == EOF)
	{
		fputs("neovifm-core-session: failed to write protocol output\n", stderr);
		return -1;
	}
	return 0;
}

static int
write_workspace(const nv_workspace_session_t *session, unsigned int output_sequence,
		unsigned int command_sequence, const char trigger[])
{
	char *json = NULL;
	const nv_protocol_json_result_t result =
		nv_protocol_preview_workspace_session_snapshot_json(session, output_sequence,
				command_sequence, trigger, &json);
	if(result != NV_PROTOCOL_JSON_OK)
	{
		fputs("neovifm-core-session: failed to serialize workspace\n", stderr);
		return -1;
	}
	const int write_result = write_line(json);
	nv_protocol_json_free(json);
	return write_result;
}

static int
write_command_error(const nv_snapshot_error_t *error, unsigned int output_sequence,
		unsigned int command_sequence)
{
	char *const json = nv_protocol_preview_session_command_error_json(error,
			output_sequence, command_sequence);
	if(json == NULL)
	{
		fputs("neovifm-core-session: failed to serialize command error\n", stderr);
		return -1;
	}
	const int result = write_line(json);
	nv_protocol_json_free(json);
	return result;
}

static int
pane_from_string(const char pane[], nv_session_pane_t *result)
{
	if(pane == NULL || result == NULL) return -1;
	if(strcmp(pane, "left") == 0) { *result = NV_SESSION_LEFT; return 0; }
	if(strcmp(pane, "right") == 0) { *result = NV_SESSION_RIGHT; return 0; }
	return -1;
}

static int
sort_key_from_string(const char key[], nv_pane_sort_key_t *result)
{
	if(key == NULL || result == NULL) return -1;
	if(strcmp(key, "name") == 0) { *result = NV_SORT_NAME; return 0; }
	if(strcmp(key, "extension") == 0) { *result = NV_SORT_EXTENSION; return 0; }
	if(strcmp(key, "size") == 0) { *result = NV_SORT_SIZE; return 0; }
	if(strcmp(key, "ctime") == 0) { *result = NV_SORT_CTIME; return 0; }
	if(strcmp(key, "mtime") == 0) { *result = NV_SORT_MTIME; return 0; }
	if(strcmp(key, "mode") == 0) { *result = NV_SORT_MODE; return 0; }
	if(strcmp(key, "type") == 0) { *result = NV_SORT_TYPE; return 0; }
	if(strcmp(key, "other") == 0) { *result = NV_SORT_OTHER; return 0; }
	return -1;
}

static int
copy_action_string(JSON_Object *payload, const char field[], char **result)
{
	const char *const value = json_object_get_string(payload, field);
	if(value == NULL || strlen(value) > 32U*1024U) return -1;
	*result = strdup(value);
	return *result == NULL ? -1 : 0;
}

static int
parse_u64_field(JSON_Object *object, const char field[], uint64_t *result)
{
	const char *const value = json_object_get_string(object, field);
	if(value == NULL || value[0] == '\0' || strlen(value) > 32U) return -1;
	for(const char *character = value; *character != '\0'; ++character)
	{
		if(*character < '0' || *character > '9') return -1;
	}
	char *end = NULL;
	errno = 0;
	const unsigned long long parsed = strtoull(value, &end, 10);
	if(errno != 0 || end == value || *end != '\0') return -1;
	*result = (uint64_t)parsed;
	return (unsigned long long)*result == parsed ? 0 : -1;
}

static int
entry_kind_from_string(const char kind[], nv_entry_kind_t *result)
{
	if(kind == NULL || result == NULL) return -1;
	static const struct { const char *name; nv_entry_kind_t kind; } kinds[] = {
		{ "directory", NV_ENTRY_DIRECTORY }, { "file", NV_ENTRY_FILE },
		{ "symlink", NV_ENTRY_SYMLINK }, { "executable", NV_ENTRY_EXECUTABLE },
		{ "fifo", NV_ENTRY_FIFO }, { "socket", NV_ENTRY_SOCKET },
		{ "char-device", NV_ENTRY_CHAR_DEVICE },
		{ "block-device", NV_ENTRY_BLOCK_DEVICE }, { "unknown", NV_ENTRY_UNKNOWN },
	};
	for(size_t i = 0U; i < sizeof(kinds)/sizeof(kinds[0]); ++i)
	{
		if(strcmp(kind, kinds[i].name) == 0) { *result = kinds[i].kind; return 0; }
	}
	return -1;
}

static int
parse_action_identity(JSON_Object *payload, nv_session_command_t *command,
		int needs_destination, int needs_paths)
{
	command->owns_action_fields = 1;
	if(pane_from_string(json_object_get_string(payload, "pane"),
			&command->pane) != 0 ||
			copy_action_string(payload, "cwd_bytes_hex",
				&command->action_cwd_bytes_hex) != 0 ||
			parse_u64_field(payload, "snapshot_revision",
				&command->action_snapshot_revision) != 0 ||
			parse_u64_field(payload, "cwd_device",
				&command->action_cwd_device) != 0 ||
			parse_u64_field(payload, "cwd_inode",
				&command->action_cwd_inode) != 0 ||
			parse_u64_field(payload, "cwd_ctime_unix_ns",
				&command->action_cwd_ctime_unix_ns) != 0)
	{
		return -1;
	}
	if(needs_destination && copy_action_string(payload,
			"destination_cwd_bytes_hex",
			&command->action_destination_cwd_bytes_hex) != 0)
	{
		return -1;
	}
	if(needs_destination &&
			(parse_u64_field(payload, "destination_snapshot_revision",
				&command->action_destination_snapshot_revision) != 0 ||
			 parse_u64_field(payload, "destination_cwd_device",
				&command->action_destination_cwd_device) != 0 ||
			 parse_u64_field(payload, "destination_cwd_inode",
				&command->action_destination_cwd_inode) != 0 ||
			 parse_u64_field(payload, "destination_cwd_ctime_unix_ns",
				&command->action_destination_cwd_ctime_unix_ns) != 0)) return -1;
	if(!needs_paths) return 0;

	JSON_Array *const targets = json_object_get_array(payload, "targets");
	const size_t count = targets == NULL ? 0U : json_array_get_count(targets);
	if(count == 0U || count > NV_SESSION_MAX_ACTION_PATHS) return -1;
	command->action_targets = calloc(count, sizeof(*command->action_targets));
	if(command->action_targets == NULL) return -1;
	command->action_target_count = count;
	for(size_t i = 0U; i < count; ++i)
	{
		JSON_Object *const target = json_array_get_object(targets, i);
		const char *const path = target == NULL ? NULL :
			json_object_get_string(target, "path_bytes_hex");
		if(path == NULL || strlen(path) > 32U*1024U ||
				(command->action_targets[i].path_bytes_hex = strdup(path)) == NULL ||
				parse_u64_field(target, "device",
					&command->action_targets[i].device) != 0 ||
				parse_u64_field(target, "inode",
					&command->action_targets[i].inode) != 0 ||
				parse_u64_field(target, "ctime_unix_ns",
					&command->action_targets[i].ctime_unix_ns) != 0 ||
				entry_kind_from_string(json_object_get_string(target, "kind"),
					&command->action_targets[i].kind) != 0)
		{
			return -1;
		}
		for(size_t j = 0U; j < i; ++j)
		{
			if(strcmp(command->action_targets[j].path_bytes_hex, path) == 0)
				return -1;
		}
	}
	return 0;
}

static int
parse_command(const char line[], unsigned int previous_sequence,
		unsigned int *sequence, nv_session_command_t *command)
{
	JSON_Value *const value = json_parse_string(line);
	if(value == NULL || json_value_get_type(value) != JSONObject)
	{
		json_value_free(value);
		return -1;
	}
	JSON_Object *const root = json_value_get_object(value);
	JSON_Object *const payload = json_object_get_object(root, "payload");
	const char *const protocol = json_object_get_string(root, "protocol");
	const char *const type = json_object_get_string(root, "type");
	const double version = json_object_get_number(root, "version");
	const double next_sequence = json_object_get_number(root, "sequence");
	const char *const action = payload == NULL ? NULL : json_object_get_string(payload, "action");
	if(protocol == NULL || strcmp(protocol, "neovifm-core") != 0 || type == NULL ||
			strcmp(type, "command") != 0 || version != 3.0 || next_sequence < 1.0 ||
			next_sequence > 4294967295.0 || next_sequence != (double)(unsigned int)next_sequence ||
			(unsigned int)next_sequence <= previous_sequence || action == NULL)
	{
		json_value_free(value);
		return -1;
	}
	memset(command, 0, sizeof(*command));
	if(strcmp(action, "focus") == 0)
	{
		command->kind = NV_SESSION_FOCUS;
		if(pane_from_string(json_object_get_string(payload, "pane"), &command->pane) != 0)
		{
			json_value_free(value);
			return -1;
		}
	}
	else if(strcmp(action, "focus-next") == 0) command->kind = NV_SESSION_FOCUS_NEXT;
	else if(strcmp(action, "move") == 0)
	{
		const double delta = json_object_get_number(payload, "delta");
		if(delta != -1.0 && delta != 1.0)
		{
			json_value_free(value);
			return -1;
		}
		command->kind = NV_SESSION_MOVE_CURSOR;
		command->delta = (int)delta;
	}
	else if(strcmp(action, "move-to") == 0)
	{
		const char *const target = json_object_get_string(payload, "target");
		if(target == NULL || (strcmp(target, "first") != 0 && strcmp(target, "last") != 0))
		{
			json_value_free(value);
			return -1;
		}
		command->kind = strcmp(target, "first") == 0 ?
			NV_SESSION_MOVE_FIRST : NV_SESSION_MOVE_LAST;
	}
	else if(strcmp(action, "enter") == 0) command->kind = NV_SESSION_ENTER;
	else if(strcmp(action, "parent") == 0) command->kind = NV_SESSION_PARENT;
	else if(strcmp(action, "toggle-selection") == 0) command->kind = NV_SESSION_TOGGLE_SELECTION;
	else if(strcmp(action, "refresh") == 0) command->kind = NV_SESSION_REFRESH;
	else if(strcmp(action, "undo") == 0) command->kind = NV_SESSION_UNDO;
	else if(strcmp(action, "sort-cycle") == 0)
	{
		const double delta = json_object_get_number(payload, "delta");
		if(delta != -1.0 && delta != 1.0)
		{
			json_value_free(value);
			return -1;
		}
		command->kind = NV_SESSION_SORT_CYCLE;
		command->delta = (int)delta;
		JSON_Value *const pane_value = json_object_get_value(payload, "pane");
		if(pane_value != NULL)
		{
			if(json_value_get_type(pane_value) != JSONString || pane_from_string(
					json_value_get_string(pane_value), &command->pane) != 0)
			{
				json_value_free(value);
				return -1;
			}
			command->has_pane = 1;
		}
	}
	else if(strcmp(action, "sort-by") == 0)
	{
		command->kind = NV_SESSION_SORT_BY;
		if(pane_from_string(json_object_get_string(payload, "pane"),
				&command->pane) != 0 ||
				sort_key_from_string(json_object_get_string(payload, "key"),
					&command->sort_key) != 0)
		{
			json_value_free(value);
			return -1;
		}
	}
	else if(strcmp(action, "select-entry") == 0)
	{
		JSON_Value *const index_value = json_object_get_value(payload, "index");
		const double index = json_value_get_number(index_value);
		JSON_Value *const toggle = json_object_get_value(payload, "toggle");
		if(pane_from_string(json_object_get_string(payload, "pane"),
				&command->pane) != 0 ||
				json_value_get_type(index_value) != JSONNumber || index < 0.0 ||
				index > (double)(NV_PANE_SNAPSHOT_MAX_ENTRIES - 1U) ||
				index != (double)(size_t)index ||
				json_value_get_type(toggle) != JSONBoolean)
		{
			json_value_free(value);
			return -1;
		}
		command->kind = NV_SESSION_SELECT_ENTRY;
		command->entry_index = (size_t)index;
		command->toggle_selection = json_value_get_boolean(toggle);
	}
	else if(strcmp(action, "new-tab") == 0)
	{
		command->kind = NV_SESSION_NEW_TAB;
		if(pane_from_string(json_object_get_string(payload, "pane"),
				&command->pane) != 0)
		{
			json_value_free(value);
			return -1;
		}
	}
	else if(strcmp(action, "activate-tab") == 0 ||
			strcmp(action, "close-tab") == 0)
	{
		command->kind = strcmp(action, "activate-tab") == 0 ?
			NV_SESSION_ACTIVATE_TAB : NV_SESSION_CLOSE_TAB;
		if(pane_from_string(json_object_get_string(payload, "pane"),
				&command->pane) != 0 || parse_u64_field(payload, "tab_id",
					&command->tab_id) != 0 || command->tab_id == 0U)
		{
			json_value_free(value);
			return -1;
		}
	}
	else if(strcmp(action, "tab-cycle") == 0)
	{
		const double delta = json_object_get_number(payload, "delta");
		if(delta == 0.0 || delta < -(double)NV_SESSION_MAX_TAB_CYCLE ||
				delta > (double)NV_SESSION_MAX_TAB_CYCLE ||
				delta != (double)(int)delta)
		{
			json_value_free(value);
			return -1;
		}
		command->kind = NV_SESSION_TAB_CYCLE;
		command->delta = (int)delta;
	}
	else if(strcmp(action, "copy") == 0 || strcmp(action, "move-files") == 0 ||
			strcmp(action, "delete") == 0)
	{
		command->kind = strcmp(action, "copy") == 0 ? NV_SESSION_COPY :
			strcmp(action, "move-files") == 0 ? NV_SESSION_MOVE_FILES :
			NV_SESSION_DELETE;
		if(parse_action_identity(payload, command,
				command->kind != NV_SESSION_DELETE, 1) != 0)
		{
			nv_session_command_free(command);
			json_value_free(value);
			return -1;
		}
	}
	else if(strcmp(action, "mkdir") == 0)
	{
		const char *const name = json_object_get_string(payload, "name");
		command->kind = NV_SESSION_MKDIR;
		if(name == NULL || strlen(name) > NV_SESSION_MAX_NAME_BYTES ||
				parse_action_identity(payload, command, 0, 0) != 0)
		{
			nv_session_command_free(command);
			json_value_free(value);
			return -1;
		}
		strcpy(command->name, name);
	}
	else
	{
		nv_session_command_free(command);
		json_value_free(value);
		return -1;
	}
	*sequence = (unsigned int)next_sequence;
	json_value_free(value);
	return 0;
}

static int
discard_command_tail(void)
{
	int character;
	do { character = fgetc(stdin); } while(character != '\n' && character != EOF);
	return character == EOF ? -1 : 0;
}

static int
command_is_action(nv_session_command_kind_t kind)
{
	return kind == NV_SESSION_COPY || kind == NV_SESSION_MOVE_FILES ||
		kind == NV_SESSION_MKDIR || kind == NV_SESSION_DELETE;
}

static char *
join_action_path(const char directory[], const char name[])
{
	const size_t directory_length = strlen(directory);
	const size_t name_length = strlen(name);
	const int separator = directory_length != 0U &&
		directory[directory_length - 1U] != '/';
	if(directory_length > SIZE_MAX - name_length - (size_t)separator - 1U)
	{
		errno = ENOMEM;
		return NULL;
	}
	char *const path = malloc(directory_length + (size_t)separator +
		name_length + 1U);
	if(path == NULL)
	{
		errno = ENOMEM;
		return NULL;
	}
	memcpy(path, directory, directory_length);
	if(separator) path[directory_length] = '/';
	memcpy(path + directory_length + (size_t)separator, name, name_length + 1U);
	return path;
}

static nv_pending_action_context_t
pending_action_context(const nv_workspace_session_t *session,
		const nv_session_command_t *command,
		const nv_session_prepared_action_t *action)
{
	const size_t source_index = nv_workspace_session_active_tab_index(session,
			command->pane);
	nv_pending_action_context_t context = {
		.active = 1,
		.source_pane = command->pane,
		.source_tab_id = nv_workspace_session_tab_id(session, command->pane,
				source_index),
	};
	if(command->kind == NV_SESSION_COPY || command->kind == NV_SESSION_MOVE_FILES)
	{
		context.has_destination = 1;
		context.destination_pane = command->pane == NV_SESSION_LEFT ?
			NV_SESSION_RIGHT : NV_SESSION_LEFT;
		const size_t destination_index = nv_workspace_session_active_tab_index(session,
				context.destination_pane);
		context.destination_tab_id = nv_workspace_session_tab_id(session,
				context.destination_pane, destination_index);
	}
	if(action != NULL && action->kind == NV_SESSION_MKDIR)
	{
		context.undo_path = join_action_path(action->source_directory,
				action->name);
		context.undo_parent_identity = action->source_directory_identity;
	}
	return context;
}

static void
pending_action_context_free(nv_pending_action_context_t *context)
{
	if(context == NULL) return;
	free(context->undo_path);
	free(context);
}

static int
apply_undo_command(nv_workspace_session_t *session, nv_action_queue_t *queue,
		nv_snapshot_error_t *error, nv_undo_bridge_location_t *location)
{
	if(queue == NULL)
		return set_error(error, "undo-unavailable",
				"file undo is unavailable on this platform");
	if(nv_action_queue_busy(queue) != 0)
		return set_error(error, "undo-busy",
				"wait for queued file actions to finish");
	switch(nv_undo_bridge_undo(location))
	{
		case NV_UNDO_BRIDGE_SUCCESS:
			break;
		case NV_UNDO_BRIDGE_NONE:
			return set_error(error, "undo-empty",
					"no supported file action can be undone");
		case NV_UNDO_BRIDGE_FAILED:
		default:
			return set_error(error, "undo-failed",
					"the last supported file action could not be undone");
	}
	if(location == NULL || location->tab_id == 0U || location->pane > NV_SESSION_RIGHT)
		return set_error(error, "undo-location-invalid",
				"undo target location is invalid");
	return refresh_action_tab(session, (nv_session_pane_t)location->pane,
			location->tab_id, error);
}

static int
process_command_line(nv_workspace_session_t *session, char line[],
		size_t line_capacity, unsigned int *output_sequence,
		unsigned int *command_sequence, int *directory_changed,
		nv_preview_queue_t *preview_queue, uint64_t *preview_generation,
		nv_action_queue_t *action_queue,
		nv_pending_action_context_t **pending_actions)
{
	const size_t length = strlen(line);
	nv_session_command_t command = {};
	unsigned int next_sequence = *command_sequence + 1U;
	nv_snapshot_error_t error = {};
	const int had_cwd_stat[] = {
		session->left.has_cwd_stat, session->right.has_cwd_stat,
	};
	const uint64_t cwd_device[] = {
		session->left.cwd_device, session->right.cwd_device,
	};
	const uint64_t cwd_inode[] = {
		session->left.cwd_inode, session->right.cwd_inode,
	};
	if(directory_changed != NULL) *directory_changed = 0;
	if(length == line_capacity - 1U && line[length - 1U] != '\n')
	{
		discard_command_tail();
		set_error(&error, "command-too-large", "command exceeds input limit");
	}
	else if(parse_command(line, *command_sequence, &next_sequence, &command) != 0)
	{
		set_error(&error, "invalid-command", "invalid session command");
	}
	else
	{
		if(command_is_action(command.kind))
		{
			nv_session_prepared_action_t action = {};
			if(action_queue == NULL)
			{
				set_error(&error, "unsupported-action",
						"file actions are unavailable on this platform");
			}
			else if(nv_workspace_session_prepare_action(session, &command, &action,
					&error) == 0)
			{
				nv_pending_action_context_t *const context =
					calloc(1U, sizeof(*context));
				uint64_t task_id = 0U;
				if(context == NULL)
				{
					set_error(&error, "action-queue-failed",
							"failed to allocate action context");
					error.os_error = ENOMEM;
					error.retryable = 1;
				}
				else if((*context = pending_action_context(session, &command,
						&action)).undo_path == NULL && command.kind == NV_SESSION_MKDIR)
				{
					pending_action_context_free(context);
					set_error(&error, "action-queue-failed",
							"failed to prepare mkdir undo context");
					error.os_error = ENOMEM;
					error.retryable = 1;
				}
				else if(nv_action_queue_submit(action_queue, &action, next_sequence,
						&task_id) == 0)
				{
					context->task_id = task_id;
					context->next = *pending_actions;
					*pending_actions = context;
					*command_sequence = next_sequence;
					const int result = write_workspace(session,
							(*output_sequence)++, *command_sequence, "command");
					nv_session_prepared_action_free(&action);
					nv_snapshot_error_free(&error);
					nv_session_command_free(&command);
					return result;
				}
				else
				{
					const int submit_error = errno;
					pending_action_context_free(context);
					set_error(&error, submit_error == EBUSY ? "action-queue-full" :
							"action-queue-failed", submit_error == EBUSY ?
							"file action queue is full" :
							"failed to queue file action");
					error.os_error = submit_error;
					error.retryable = submit_error == EBUSY;
				}
			}
			nv_session_prepared_action_free(&action);
		}
		else if(command.kind == NV_SESSION_UNDO)
		{
			nv_undo_bridge_location_t location = {};
			if(apply_undo_command(session, action_queue, &error, &location) == 0)
			{
				*command_sequence = next_sequence;
				const int result = write_workspace(session, (*output_sequence)++,
						*command_sequence, "command");
				if(result == 0 && submit_active_preview(session, preview_queue,
						preview_generation) != 0)
				{
					fputs("neovifm-core-session: failed to queue undo preview\n", stderr);
				}
				nv_snapshot_error_free(&error);
				nv_session_command_free(&command);
				return result;
			}
			*command_sequence = next_sequence;
		}
		else if(nv_workspace_session_apply(session, &command, &error) == 0)
		{
			*command_sequence = next_sequence;
			if(directory_changed != NULL)
			{
				for(nv_session_pane_t pane = NV_SESSION_LEFT;
						pane <= NV_SESSION_RIGHT; ++pane)
				{
					const nv_pane_snapshot_t *const snapshot = pane == NV_SESSION_LEFT ?
						&session->left : &session->right;
					if(snapshot->has_cwd_stat != had_cwd_stat[pane] ||
							(snapshot->has_cwd_stat &&
							 (snapshot->cwd_device != cwd_device[pane] ||
							  snapshot->cwd_inode != cwd_inode[pane])))
					{
						*directory_changed |= 1 << pane;
					}
				}
			}
			const int result = write_workspace(session, (*output_sequence)++,
					*command_sequence, "command");
			if(result == 0 && submit_active_preview(session, preview_queue,
					preview_generation) != 0)
			{
				fputs("neovifm-core-session: failed to queue preview\n", stderr);
			}
			nv_snapshot_error_free(&error);
			nv_session_command_free(&command);
			return result;
		}
		/* A recoverable command error is still acknowledged, so subsequent watch
		 * snapshots must use this command sequence instead of becoming stale. */
		*command_sequence = next_sequence;
	}
	/* command-error is an acknowledgement too: keep later watch snapshots at
	 * the record sequence visible to the client, even for malformed input. */
	*command_sequence = next_sequence;
	const int result = write_command_error(&error, (*output_sequence)++,
			next_sequence);
	nv_snapshot_error_free(&error);
	nv_session_command_free(&command);
	return result;
}

static int
submit_active_preview(const nv_workspace_session_t *session,
		nv_preview_queue_t *queue, uint64_t *generation)
{
	if(session == NULL || queue == NULL || generation == NULL) return -1;
	const nv_pane_snapshot_t *const snapshot = nv_workspace_session_active(session);
	if(snapshot == NULL || snapshot->cursor < 0) return 0;
	const nv_pane_entry_t *const entry = &snapshot->entries[snapshot->cursor];
	const nv_preview_request_t request = {
		.pane = session->active_pane == NV_SESSION_LEFT ? NV_PREVIEW_PANE_LEFT :
			NV_PREVIEW_PANE_RIGHT,
		.generation = ++*generation,
		.cwd_bytes_hex = snapshot->cwd_bytes_hex,
		.path_bytes_hex = entry->path_bytes_hex,
		.kind = entry->kind == NV_ENTRY_DIRECTORY ? NV_PREVIEW_KIND_DIRECTORY :
			NV_PREVIEW_KIND_TEXT,
		.max_bytes = NV_PREVIEW_MAX_BYTES,
		.timeout_ms = 2000U,
	};
	return nv_preview_queue_submit(queue, &request, NULL);
}

static int
drain_preview_events(nv_preview_queue_t *queue, unsigned int *output_sequence)
{
	for(;;)
	{
		nv_preview_event_t event = {};
		const int popped = nv_preview_queue_pop(queue, &event);
		if(popped < 0) return -1;
		if(popped == 0) return 0;
		char *const task = nv_protocol_preview_task_json(&event,
				(*output_sequence)++);
		char *const preview = (event.state == NV_PREVIEW_TASK_DONE ||
			event.state == NV_PREVIEW_TASK_FAILED ||
			event.state == NV_PREVIEW_TASK_CANCELLED) ?
			nv_protocol_preview_json(&event, (*output_sequence)++) : NULL;
		const int result = task == NULL || write_line(task) != 0 ||
			(preview != NULL && write_line(preview) != 0) ? -1 : 0;
		nv_protocol_json_free(task);
		nv_protocol_json_free(preview);
		nv_preview_event_free(&event);
		if(result != 0) return result;
	}
}

static int
action_terminal(nv_action_task_state_t state)
{
	return state == NV_ACTION_TASK_DONE || state == NV_ACTION_TASK_FAILED ||
		state == NV_ACTION_TASK_CANCELLED;
}

static int
refresh_action_tab(nv_workspace_session_t *session, nv_session_pane_t pane,
		uint64_t tab_id, nv_snapshot_error_t *error)
{
	if(nv_workspace_session_refresh_tab(session, pane, tab_id, error) == 0)
		return 0;
	if(error->code != NULL && strcmp(error->code, "invalid-tab") == 0)
	{
		nv_snapshot_error_free(error);
		return 0;
	}
	return -1;
}

static int
drain_action_events(nv_action_queue_t *queue,
		nv_workspace_session_t *session, unsigned int *output_sequence,
		unsigned int command_sequence, nv_preview_queue_t *preview_queue,
		uint64_t *preview_generation,
		nv_pending_action_context_t **pending_actions)
{
	if(queue == NULL) return 0;
	if(nv_action_queue_failed(queue))
	{
		fputs("neovifm-core-session: failed to publish action terminal event\n",
				stderr);
		return -1;
	}
	for(;;)
	{
		nv_action_event_t event = {};
		const int popped = nv_action_queue_pop(queue, &event);
		if(popped < 0) return -1;
		if(popped == 0) return 0;
		int refresh_failed = 0;
		if(action_terminal(event.state))
		{
			nv_snapshot_error_t error = {};
			nv_pending_action_context_t *previous = NULL;
			nv_pending_action_context_t *context = pending_actions == NULL ? NULL :
				*pending_actions;
			while(context != NULL && context->task_id != event.task_id)
			{
				previous = context;
				context = context->next;
			}
			if(context != NULL && context->active)
			{
				if(event.state == NV_ACTION_TASK_DONE &&
					 event.kind == NV_SESSION_MKDIR && context->undo_path != NULL &&
					 nv_undo_bridge_record_mkdir(context->undo_path,
							context->undo_parent_identity, (nv_undo_bridge_location_t){
								.pane = (unsigned int)context->source_pane,
								.tab_id = context->source_tab_id,
							}) != 0)
				{
					fprintf(stderr,
							"neovifm-core-session: mkdir undo record failed: %s\n",
							strerror(errno));
				}
				refresh_failed = refresh_action_tab(session,
						context->source_pane, context->source_tab_id,
						&error) != 0;
				if(!refresh_failed && context->has_destination)
				{
					refresh_failed = refresh_action_tab(session,
							context->destination_pane, context->destination_tab_id,
							&error) != 0;
				}
				if(previous == NULL) *pending_actions = context->next;
				else previous->next = context->next;
				pending_action_context_free(context);
			}
			else
			{
				refresh_failed = nv_workspace_session_refresh_pane(session,
						NV_SESSION_LEFT, &error) != 0 ||
					nv_workspace_session_refresh_pane(session, NV_SESSION_RIGHT,
							&error) != 0;
			}
			if(!refresh_failed && write_workspace(session, (*output_sequence)++,
					command_sequence, "action") != 0) refresh_failed = 1;
			if(refresh_failed)
			{
				fprintf(stderr, "neovifm-core-session: action refresh failed: %s\n",
						error.message == NULL ? "unknown error" : error.message);
			}
			nv_snapshot_error_free(&error);
			if(!refresh_failed && submit_active_preview(session, preview_queue,
					preview_generation) != 0)
			{
				fputs("neovifm-core-session: failed to queue action preview\n",
						stderr);
			}
		}
		char *const json = nv_protocol_action_task_json(&event,
				(*output_sequence)++);
		const int result = json == NULL || write_line(json) != 0 ? -1 : 0;
		nv_protocol_json_free(json);
		if(action_terminal(event.state))
			nv_action_queue_ack_terminal(queue, event.task_id);
		nv_action_event_free(&event);
		if(result != 0 || refresh_failed) return -1;
	}
}

#ifdef __APPLE__
static int
hex_digit(char character)
{
	if(character >= '0' && character <= '9') return character - '0';
	if(character >= 'a' && character <= 'f') return character - 'a' + 10;
	return -1;
}

static char *
hex_decode(const char hex[])
{
	if(hex == NULL) return NULL;
	const size_t length = strlen(hex);
	if(length % 2U != 0U || length/2U > NV_PANE_SNAPSHOT_MAX_HEX_BYTES/2U)
	{
		return NULL;
	}
	char *const decoded = malloc(length/2U + 1U);
	if(decoded == NULL) return NULL;
	for(size_t i = 0U; i < length; i += 2U)
	{
		const int high = hex_digit(hex[i]), low = hex_digit(hex[i + 1U]);
		if(high < 0 || low < 0 || (high == 0 && low == 0))
		{
			free(decoded);
			return NULL;
		}
		decoded[i/2U] = (char)((high << 4U) | low);
	}
	decoded[length/2U] = '\0';
	return decoded;
}

static int
watcher_fd(const nv_session_watcher_t *watcher, nv_session_pane_t pane)
{
	return pane == NV_SESSION_LEFT ? watcher->left_fd : watcher->right_fd;
}

static int *
watcher_fd_slot(nv_session_watcher_t *watcher, nv_session_pane_t pane)
{
	return pane == NV_SESSION_LEFT ? &watcher->left_fd : &watcher->right_fd;
}

static const char *
pane_name(nv_session_pane_t pane)
{
	return pane == NV_SESSION_LEFT ? "left" : "right";
}

static void
watcher_stop_pane(nv_session_watcher_t *watcher, nv_session_pane_t pane)
{
	int *const fd = watcher_fd_slot(watcher, pane);
	if(*fd >= 0) close(*fd);
	*fd = -1;
}

static int
watcher_open_pane(nv_session_watcher_t *watcher,
		const nv_workspace_session_t *session, nv_session_pane_t pane)
{
	const nv_pane_snapshot_t *const snapshot = pane == NV_SESSION_LEFT ?
		&session->left : &session->right;
	char *const path = hex_decode(snapshot->cwd_bytes_hex);
	if(path == NULL) return -1;
	const int fd = open(path, O_EVTONLY);
	free(path);
	if(fd < 0) return -1;
	struct kevent change;
	EV_SET(&change, (uintptr_t)fd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
			NOTE_WRITE | NOTE_EXTEND | NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB, 0, NULL);
	if(kevent(watcher->queue, &change, 1, NULL, 0, NULL) == -1)
	{
		close(fd);
		return -1;
	}
	watcher_stop_pane(watcher, pane);
	*watcher_fd_slot(watcher, pane) = fd;
	return 0;
}

static int
watcher_init(nv_session_watcher_t *watcher, const nv_workspace_session_t *session)
{
	*watcher = (nv_session_watcher_t){ .queue = -1, .left_fd = -1, .right_fd = -1 };
	watcher->queue = kqueue();
	if(watcher->queue < 0)
	{
		fputs("neovifm-core-session: kqueue unavailable; watcher disabled\n", stderr);
		return -1;
	}
	struct kevent change;
	EV_SET(&change, STDIN_FILENO, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
	if(kevent(watcher->queue, &change, 1, NULL, 0, NULL) == -1)
	{
		fputs("neovifm-core-session: stdin kqueue registration failed; watcher disabled\n", stderr);
		watcher_free(watcher);
		return -1;
	}
	for(nv_session_pane_t pane = NV_SESSION_LEFT; pane <= NV_SESSION_RIGHT; ++pane)
	{
		if(watcher_open_pane(watcher, session, pane) != 0)
		{
			fprintf(stderr, "neovifm-core-session: %s pane watcher disabled: %s\n",
					pane_name(pane), strerror(errno));
		}
	}
	return 0;
}

static void
watcher_free(nv_session_watcher_t *watcher)
{
	watcher_stop_pane(watcher, NV_SESSION_LEFT);
	watcher_stop_pane(watcher, NV_SESSION_RIGHT);
	if(watcher->queue >= 0) close(watcher->queue);
	*watcher = (nv_session_watcher_t){ .queue = -1, .left_fd = -1, .right_fd = -1 };
}

static int
watcher_handle_events(nv_session_watcher_t *watcher,
		nv_workspace_session_t *session, unsigned int *output_sequence,
		unsigned int command_sequence, int *stdin_ready,
		nv_action_queue_t *action_queue)
{
	struct kevent events[3];
	const struct timespec timeout = { .tv_sec = 0, .tv_nsec = 12L*1000L*1000L };
	const int count = kevent(watcher->queue, NULL, 0, events,
			sizeof(events)/sizeof(events[0]), &timeout);
	if(count < 0)
	{
		if(errno == EINTR) return 0;
		fputs("neovifm-core-session: kqueue wait failed; watcher disabled\n", stderr);
		return -1;
	}
	for(int i = 0; i < count; ++i)
	{
		if(events[i].filter == EVFILT_READ && events[i].ident == STDIN_FILENO)
		{
			*stdin_ready = 1;
			continue;
		}
		const nv_session_pane_t pane = events[i].ident == (uintptr_t)watcher_fd(watcher,
				NV_SESSION_LEFT) ? NV_SESSION_LEFT : NV_SESSION_RIGHT;
		if(events[i].filter != EVFILT_VNODE || watcher_fd(watcher, pane) < 0 ||
				events[i].ident != (uintptr_t)watcher_fd(watcher, pane)) continue;
		if(nv_action_queue_busy(action_queue)) continue;
		nv_snapshot_error_t error = {};
		if(nv_workspace_session_refresh_pane(session, pane, &error) != 0)
		{
			fprintf(stderr, "neovifm-core-session: %s pane watch refresh stopped: %s\n",
					pane_name(pane), error.message == NULL ? "unknown error" : error.message);
			nv_snapshot_error_free(&error);
			watcher_stop_pane(watcher, pane);
			continue;
		}
		nv_snapshot_error_free(&error);
		if(write_workspace(session, (*output_sequence)++, command_sequence, "watch") != 0)
		{
			return -1;
		}
	}
	return 0;
}

static int
poll_stdin(int *stdin_ready)
{
	fd_set read_fds;
	FD_ZERO(&read_fds);
	FD_SET(STDIN_FILENO, &read_fds);
	struct timeval timeout = { .tv_sec = 0, .tv_usec = 12000 };
	const int result = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout);
	if(result < 0) return errno == EINTR ? 0 : -1;
	*stdin_ready = result != 0;
	return 0;
}
#endif

int
main(int argc, char *argv[])
{
	if(argc != 3)
	{
		fputs("neovifm-core-session: expected left and right directory arguments\n", stderr);
		return 2;
	}
	/* The macOS watcher loop uses select(2) before fgets().  Keep stdin
	 * unbuffered so a second command already present in the pipe is not hidden
	 * inside stdio after select reports the descriptor as drained. */
	(void)setvbuf(stdin, NULL, _IONBF, 0);
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	if(nv_workspace_session_init(argv[1], argv[2], &session, &error) != 0)
	{
		fputs(error.message == NULL ? "neovifm-core-session: failed to initialize\n" : error.message, stderr);
		fputc('\n', stderr);
		nv_snapshot_error_free(&error);
		return 1;
	}
	if(nv_undo_bridge_init() != 0)
	{
		fputs("neovifm-core-session: failed to initialize undo bridge\n", stderr);
		nv_workspace_session_free(&session);
		return 1;
	}
	nv_preview_queue_t *const preview_queue = nv_preview_queue_alloc();
	if(preview_queue == NULL)
	{
		fputs("neovifm-core-session: failed to initialize preview queue\n", stderr);
		nv_undo_bridge_reset();
		nv_workspace_session_free(&session);
		return 1;
	}
	nv_action_queue_t *action_queue = NULL;
#ifdef __APPLE__
	action_queue = nv_action_queue_alloc();
	if(action_queue == NULL)
	{
		fputs("neovifm-core-session: failed to initialize action queue\n", stderr);
		nv_preview_queue_free(preview_queue);
		nv_undo_bridge_reset();
		nv_workspace_session_free(&session);
		return 1;
	}
#endif
	uint64_t preview_generation = 0U;
	char *const hello = nv_protocol_preview_session_hello_json(0U);
	if(write_line(hello) != 0 || write_workspace(&session, 1U, 0U, "initial") != 0)
	{
		nv_protocol_json_free(hello);
		nv_action_queue_free(action_queue);
		nv_preview_queue_free(preview_queue);
		nv_undo_bridge_reset();
		nv_workspace_session_free(&session);
		return 1;
	}
	nv_protocol_json_free(hello);
	if(submit_active_preview(&session, preview_queue, &preview_generation) != 0)
	{
		fputs("neovifm-core-session: failed to queue initial preview\n", stderr);
	}

	char line[NV_SESSION_MAX_COMMAND_BYTES + 2U];
	unsigned int output_sequence = 2U;
	unsigned int command_sequence = 0U;
	nv_pending_action_context_t *pending_actions = NULL;
	int result = 0;
#ifdef __APPLE__
	nv_session_watcher_t watcher = {};
	if(watcher_init(&watcher, &session) == 0)
	{
		for(;;)
		{
			int stdin_ready = 0;
			if(watcher_handle_events(&watcher, &session, &output_sequence,
					command_sequence, &stdin_ready, action_queue) != 0)
			{
				result = 1;
				break;
			}
			if(drain_preview_events(preview_queue, &output_sequence) != 0)
			{
				result = 1;
				break;
			}
			if(drain_action_events(action_queue, &session, &output_sequence,
					command_sequence, preview_queue, &preview_generation,
					&pending_actions) != 0)
			{
				result = 1;
				break;
			}
			if(!stdin_ready) continue;
			if(fgets(line, sizeof(line), stdin) == NULL) break;
			int directory_changed = 0;
			if(process_command_line(&session, line, sizeof(line), &output_sequence,
					&command_sequence, &directory_changed, preview_queue,
					&preview_generation, action_queue, &pending_actions) != 0)
			{
				result = 1;
				break;
			}
			for(nv_session_pane_t pane = NV_SESSION_LEFT; directory_changed != 0 &&
					pane <= NV_SESSION_RIGHT; ++pane)
			{
				if((directory_changed & (1 << pane)) == 0) continue;
				if(watcher_open_pane(&watcher, &session, pane) != 0)
				{
					fprintf(stderr,
							"neovifm-core-session: %s pane watcher disabled: %s\n",
							pane_name(pane), strerror(errno));
					watcher_stop_pane(&watcher, pane);
				}
			}
		}
		watcher_free(&watcher);
	}
	else
#endif
	for(;;)
	{
		int stdin_ready = 0;
#ifdef __APPLE__
		if(poll_stdin(&stdin_ready) != 0)
		{
			result = 1;
			break;
		}
#else
		stdin_ready = 1;
#endif
		if(drain_preview_events(preview_queue, &output_sequence) != 0 ||
				drain_action_events(action_queue, &session, &output_sequence,
						command_sequence, preview_queue, &preview_generation,
						&pending_actions) != 0)
		{
			result = 1;
			break;
		}
		if(!stdin_ready) continue;
		if(fgets(line, sizeof(line), stdin) == NULL) break;
		if(process_command_line(&session, line, sizeof(line), &output_sequence,
					&command_sequence, NULL, preview_queue, &preview_generation,
					action_queue, &pending_actions) != 0)
		{
			result = 1;
			break;
		}
	}
	nv_action_queue_cancel_all(action_queue);
	if(drain_action_events(action_queue, &session, &output_sequence,
				command_sequence, preview_queue, &preview_generation,
				&pending_actions) != 0) result = 1;
	if(drain_preview_events(preview_queue, &output_sequence) != 0) result = 1;
	nv_snapshot_error_free(&error);
	while(pending_actions != NULL)
	{
		nv_pending_action_context_t *const next = pending_actions->next;
		pending_action_context_free(pending_actions);
		pending_actions = next;
	}
	nv_action_queue_free(action_queue);
	nv_preview_queue_free(preview_queue);
	nv_undo_bridge_reset();
	nv_workspace_session_free(&session);
	return result != 0 || ferror(stdin) ? 1 : 0;
}
