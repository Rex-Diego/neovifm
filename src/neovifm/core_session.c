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
# include <unistd.h>
#endif

#include "snapshot_json.h"
#include "workspace_session.h"
#include "../utils/parson.h"

#define NV_SESSION_MAX_COMMAND_BYTES (16U*1024U)

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
		nv_preview_queue_t *preview_queue, uint64_t *preview_generation);
static int submit_active_preview(const nv_workspace_session_t *session,
		nv_preview_queue_t *queue, uint64_t *generation);
static int drain_preview_events(nv_preview_queue_t *queue,
		unsigned int *output_sequence);

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
		unsigned int command_sequence, int *stdin_ready);
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
	const nv_protocol_json_result_t result = nv_protocol_preview_session_snapshot_json(
			&session->left, &session->right, nv_workspace_session_active_name(session),
			output_sequence, command_sequence, trigger, &json);
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
	else
	{
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
process_command_line(nv_workspace_session_t *session, char line[],
		size_t line_capacity, unsigned int *output_sequence,
		unsigned int *command_sequence, int *directory_changed,
		nv_preview_queue_t *preview_queue, uint64_t *preview_generation)
{
	const size_t length = strlen(line);
	nv_session_command_t command = {};
	unsigned int next_sequence = *command_sequence + 1U;
	nv_snapshot_error_t error = {};
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
		if(nv_workspace_session_apply(session, &command, &error) == 0)
		{
			*command_sequence = next_sequence;
			if(directory_changed != NULL &&
					(command.kind == NV_SESSION_ENTER || command.kind == NV_SESSION_PARENT))
			{
				*directory_changed = 1;
			}
			const int result = write_workspace(session, (*output_sequence)++,
					*command_sequence, "command");
			if(result == 0 && submit_active_preview(session, preview_queue,
					preview_generation) != 0)
			{
				fputs("neovifm-core-session: failed to queue preview\n", stderr);
			}
			nv_snapshot_error_free(&error);
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
		unsigned int command_sequence, int *stdin_ready)
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
#endif

int
main(int argc, char *argv[])
{
	if(argc != 3)
	{
		fputs("neovifm-core-session: expected left and right directory arguments\n", stderr);
		return 2;
	}
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	if(nv_workspace_session_init(argv[1], argv[2], &session, &error) != 0)
	{
		fputs(error.message == NULL ? "neovifm-core-session: failed to initialize\n" : error.message, stderr);
		fputc('\n', stderr);
		nv_snapshot_error_free(&error);
		return 1;
	}
	nv_preview_queue_t *const preview_queue = nv_preview_queue_alloc();
	if(preview_queue == NULL)
	{
		fputs("neovifm-core-session: failed to initialize preview queue\n", stderr);
		nv_workspace_session_free(&session);
		return 1;
	}
	uint64_t preview_generation = 0U;
	char *const hello = nv_protocol_preview_session_hello_json(0U);
	if(write_line(hello) != 0 || write_workspace(&session, 1U, 0U, "initial") != 0)
	{
		nv_protocol_json_free(hello);
		nv_preview_queue_free(preview_queue);
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
	int result = 0;
#ifdef __APPLE__
	nv_session_watcher_t watcher = {};
	if(watcher_init(&watcher, &session) == 0)
	{
		for(;;)
		{
			int stdin_ready = 0;
			if(watcher_handle_events(&watcher, &session, &output_sequence,
					command_sequence, &stdin_ready) != 0)
			{
				result = 1;
				break;
			}
			if(drain_preview_events(preview_queue, &output_sequence) != 0)
			{
				result = 1;
				break;
			}
			if(!stdin_ready) continue;
			if(fgets(line, sizeof(line), stdin) == NULL) break;
			int directory_changed = 0;
			if(process_command_line(&session, line, sizeof(line), &output_sequence,
					&command_sequence, &directory_changed, preview_queue,
					&preview_generation) != 0)
			{
				result = 1;
				break;
			}
			if(directory_changed && watcher_open_pane(&watcher, &session,
					session.active_pane) != 0)
			{
				fprintf(stderr, "neovifm-core-session: %s pane watcher disabled: %s\n",
						pane_name(session.active_pane), strerror(errno));
				watcher_stop_pane(&watcher, session.active_pane);
			}
		}
		watcher_free(&watcher);
	}
	else
#endif
	while(fgets(line, sizeof(line), stdin) != NULL)
	{
		if(process_command_line(&session, line, sizeof(line), &output_sequence,
				&command_sequence, NULL, preview_queue, &preview_generation) != 0)
		{
			result = 1;
			break;
		}
	}
	if(drain_preview_events(preview_queue, &output_sequence) != 0) result = 1;
	nv_snapshot_error_free(&error);
	nv_preview_queue_free(preview_queue);
	nv_workspace_session_free(&session);
	return result != 0 || ferror(stdin) ? 1 : 0;
}
