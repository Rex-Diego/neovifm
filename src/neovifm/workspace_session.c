/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "workspace_session.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "../compat/neovifm_fs.h"

static int set_error(nv_snapshot_error_t *error, const char code[],
		const char message[]);
static nv_pane_snapshot_t *active_snapshot(nv_workspace_session_t *session);
static nv_pane_snapshot_t *pane_snapshot(nv_workspace_session_t *session,
		nv_session_pane_t pane);
static const nv_pane_snapshot_t *const_pane_snapshot(
		const nv_workspace_session_t *session, nv_session_pane_t pane);
static nv_session_tabs_t *pane_tabs(nv_workspace_session_t *session,
		nv_session_pane_t pane);
static const nv_session_tabs_t *const_pane_tabs(
		const nv_workspace_session_t *session, nv_session_pane_t pane);
static int ensure_tabs(nv_workspace_session_t *session,
		nv_snapshot_error_t *error);
static int clone_snapshot(const nv_pane_snapshot_t *source,
		nv_pane_snapshot_t *clone, nv_snapshot_error_t *error);
static int find_tab_index(const nv_session_tabs_t *tabs, uint64_t id,
		size_t *index);
static int replace_snapshot(nv_workspace_session_t *session,
		nv_pane_snapshot_t *current, const char path[],
		nv_snapshot_error_t *error);
static char *hex_decode(const char hex[]);
static char *parent_path(const char path[]);
static int refresh_snapshot(nv_workspace_session_t *session,
		nv_pane_snapshot_t *snapshot, nv_snapshot_error_t *error);

static int
set_error(nv_snapshot_error_t *error, const char code[], const char message[])
{
	if(error == NULL)
	{
		return -1;
	}
	nv_snapshot_error_free(error);
	error->code = strdup(code);
	error->message = strdup(message);
	if(error->code == NULL || error->message == NULL)
	{
		nv_snapshot_error_free(error);
		return -1;
	}
	return -1;
}

static nv_pane_snapshot_t *
active_snapshot(nv_workspace_session_t *session)
{
	return session->active_pane == NV_SESSION_LEFT ? &session->left : &session->right;
}

static int
valid_pane(nv_session_pane_t pane)
{
	return pane == NV_SESSION_LEFT || pane == NV_SESSION_RIGHT;
}

static nv_pane_snapshot_t *
pane_snapshot(nv_workspace_session_t *session, nv_session_pane_t pane)
{
	return pane == NV_SESSION_LEFT ? &session->left : &session->right;
}

static const nv_pane_snapshot_t *
const_pane_snapshot(const nv_workspace_session_t *session,
		nv_session_pane_t pane)
{
	return pane == NV_SESSION_LEFT ? &session->left : &session->right;
}

static nv_session_tabs_t *
pane_tabs(nv_workspace_session_t *session, nv_session_pane_t pane)
{
	return pane == NV_SESSION_LEFT ? &session->left_tabs : &session->right_tabs;
}

static const nv_session_tabs_t *
const_pane_tabs(const nv_workspace_session_t *session, nv_session_pane_t pane)
{
	return pane == NV_SESSION_LEFT ? &session->left_tabs : &session->right_tabs;
}

static uint64_t
next_tab_id(nv_workspace_session_t *session)
{
	if(session->next_tab_id == 0U) return 0U;
	return session->next_tab_id++;
}

static int
ensure_tabs(nv_workspace_session_t *session, nv_snapshot_error_t *error)
{
	if(session->left_tabs.count > NV_SESSION_MAX_TABS ||
			session->right_tabs.count > NV_SESSION_MAX_TABS)
	{
		return set_error(error, "invalid-tab", "session tab state is invalid");
	}
	if(session->left_tabs.count != 0U &&
			session->left_tabs.active >= session->left_tabs.count)
	{
		return set_error(error, "invalid-tab", "left tab state is invalid");
	}
	if(session->right_tabs.count != 0U &&
			session->right_tabs.active >= session->right_tabs.count)
	{
		return set_error(error, "invalid-tab", "right tab state is invalid");
	}
	if(session->next_tab_id == 0U && session->left_tabs.count == 0U &&
			session->right_tabs.count == 0U)
	{
		session->next_tab_id = 1U;
	}
	if(session->next_snapshot_revision == 0U)
	{
		const uint64_t latest = session->left.snapshot_revision >
				session->right.snapshot_revision ? session->left.snapshot_revision :
				session->right.snapshot_revision;
		session->next_snapshot_revision = latest == UINT64_MAX ? 1U : latest + 1U;
	}
	for(nv_session_pane_t pane = NV_SESSION_LEFT; pane <= NV_SESSION_RIGHT; ++pane)
	{
		nv_session_tabs_t *const tabs = pane_tabs(session, pane);
		if(tabs->count != 0U) continue;
		const uint64_t id = next_tab_id(session);
		if(id == 0U)
		{
			return set_error(error, "tab-id-exhausted",
					"no more tab identifiers are available");
		}
		tabs->items[0].id = id;
		tabs->count = 1U;
		tabs->active = 0U;
	}
	return 0;
}

const nv_pane_snapshot_t *
nv_workspace_session_active(const nv_workspace_session_t *session)
{
	if(session == NULL)
	{
		return NULL;
	}
	return session->active_pane == NV_SESSION_LEFT ? &session->left : &session->right;
}

const char *
nv_workspace_session_active_name(const nv_workspace_session_t *session)
{
	return session != NULL && session->active_pane == NV_SESSION_RIGHT ? "right" : "left";
}

size_t
nv_workspace_session_tab_count(const nv_workspace_session_t *session,
		nv_session_pane_t pane)
{
	if(session == NULL || !valid_pane(pane)) return 0U;
	const size_t count = const_pane_tabs(session, pane)->count;
	return count == 0U ? 1U : count;
}

size_t
nv_workspace_session_active_tab_index(const nv_workspace_session_t *session,
		nv_session_pane_t pane)
{
	if(session == NULL || !valid_pane(pane)) return 0U;
	const nv_session_tabs_t *const tabs = const_pane_tabs(session, pane);
	return tabs->count == 0U ? 0U : tabs->active;
}

uint64_t
nv_workspace_session_tab_id(const nv_workspace_session_t *session,
		nv_session_pane_t pane, size_t index)
{
	if(session == NULL || !valid_pane(pane) ||
			index >= nv_workspace_session_tab_count(session, pane)) return 0U;
	const nv_session_tabs_t *const tabs = const_pane_tabs(session, pane);
	if(tabs->count == 0U) return pane == NV_SESSION_LEFT ? 1U : 2U;
	return tabs->items[index].id;
}

const nv_pane_snapshot_t *
nv_workspace_session_tab_snapshot(const nv_workspace_session_t *session,
		nv_session_pane_t pane, size_t index)
{
	if(session == NULL || !valid_pane(pane) ||
			index >= nv_workspace_session_tab_count(session, pane)) return NULL;
	const nv_session_tabs_t *const tabs = const_pane_tabs(session, pane);
	if(tabs->count == 0U || index == tabs->active)
	{
		return const_pane_snapshot(session, pane);
	}
	return &tabs->items[index].snapshot;
}

int
nv_workspace_session_init(const char left_path[], const char right_path[],
		nv_workspace_session_t *session, nv_snapshot_error_t *error)
{
	if(session == NULL || error == NULL)
	{
		return -1;
	}
	nv_workspace_session_t next = {};
	nv_snapshot_error_t next_error = {};
	if(nv_pane_snapshot_build(left_path, &next.left, &next_error) != 0 ||
			nv_pane_snapshot_build(right_path, &next.right, &next_error) != 0)
	{
		nv_pane_snapshot_free(&next.left);
		nv_pane_snapshot_free(&next.right);
		nv_snapshot_error_free(error);
		*error = next_error;
		return -1;
	}
	next.next_snapshot_revision = 1U;
	next.left.snapshot_revision = next.next_snapshot_revision++;
	next.right.snapshot_revision = next.next_snapshot_revision++;
	next.left_tabs.items[0].id = 1U;
	next.left_tabs.count = 1U;
	next.right_tabs.items[0].id = 2U;
	next.right_tabs.count = 1U;
	next.next_tab_id = 3U;
	nv_workspace_session_free(session);
	nv_snapshot_error_free(error);
	*session = next;
	return 0;
}

static int
clone_string(char **destination, const char source[])
{
	*destination = source == NULL ? NULL : strdup(source);
	return source == NULL || *destination != NULL ? 0 : -1;
}

static int
clone_snapshot(const nv_pane_snapshot_t *source, nv_pane_snapshot_t *clone,
		nv_snapshot_error_t *error)
{
	nv_pane_snapshot_t next = *source;
	next.cwd_display = NULL;
	next.cwd_bytes_hex = NULL;
	next.entries = NULL;
	next.entry_count = 0U;
	if(clone_string(&next.cwd_display, source->cwd_display) != 0 ||
			clone_string(&next.cwd_bytes_hex, source->cwd_bytes_hex) != 0)
	{
		goto failed;
	}
	if(source->entry_count != 0U)
	{
		next.entries = calloc(source->entry_count, sizeof(*next.entries));
		if(next.entries == NULL) goto failed;
	}
	for(size_t i = 0U; i < source->entry_count; ++i)
	{
		next.entries[i] = source->entries[i];
			next.entries[i].name_display = NULL;
			next.entries[i].name_bytes_hex = NULL;
			next.entries[i].path_display = NULL;
			next.entries[i].path_bytes_hex = NULL;
			next.entries[i].owner_display = NULL;
			next.entries[i].group_display = NULL;
			if(clone_string(&next.entries[i].name_display,
						source->entries[i].name_display) != 0 ||
					clone_string(&next.entries[i].name_bytes_hex,
						source->entries[i].name_bytes_hex) != 0 ||
					clone_string(&next.entries[i].path_display,
						source->entries[i].path_display) != 0 ||
					clone_string(&next.entries[i].path_bytes_hex,
						source->entries[i].path_bytes_hex) != 0 ||
					clone_string(&next.entries[i].owner_display,
						source->entries[i].owner_display) != 0 ||
					clone_string(&next.entries[i].group_display,
						source->entries[i].group_display) != 0)
		{
			next.entry_count = i + 1U;
			goto failed;
		}
		next.entry_count = i + 1U;
	}
	nv_pane_snapshot_free(clone);
	*clone = next;
	return 0;

failed:
	nv_pane_snapshot_free(&next);
	return set_error(error, "out-of-memory", "failed to clone pane snapshot");
}

static int
replace_snapshot(nv_workspace_session_t *session, nv_pane_snapshot_t *current,
		const char path[], nv_snapshot_error_t *error)
{
	const nv_pane_sort_key_t sort_key = current->sort_key;
	const int sort_descending = current->sort_descending;
	char *const cursor_name = current->cursor < 0 ? NULL :
		strdup(current->entries[current->cursor].name_bytes_hex);
	nv_pane_snapshot_t next = {};
	if(nv_pane_snapshot_build(path, &next, error) != 0)
	{
		free(cursor_name);
		return -1;
	}
	for(size_t i = 0U; i < next.entry_count; ++i)
	{
		for(size_t j = 0U; j < current->entry_count; ++j)
		{
			if(strcmp(next.entries[i].name_bytes_hex,
					current->entries[j].name_bytes_hex) == 0)
			{
				next.entries[i].selected = current->entries[j].selected;
				next.selection_count += next.entries[i].selected != 0;
				if(cursor_name != NULL && strcmp(next.entries[i].name_bytes_hex,
						cursor_name) == 0) next.cursor = (int)i;
				break;
			}
		}
	}
	nv_pane_snapshot_sort(&next, sort_key, sort_descending);
	next.snapshot_revision = session->next_snapshot_revision++;
	free(cursor_name);
	nv_pane_snapshot_free(current);
	*current = next;
	return 0;
}

static int
refresh_snapshot(nv_workspace_session_t *session, nv_pane_snapshot_t *snapshot,
		nv_snapshot_error_t *error)
{
	char *const path = hex_decode(snapshot->cwd_bytes_hex);
	if(path == NULL)
	{
		return set_error(error, "invalid-path", "directory identity is invalid");
	}
	const int result = replace_snapshot(session, snapshot, path, error);
	free(path);
	return result;
}

int
nv_workspace_session_refresh_pane(nv_workspace_session_t *session,
		nv_session_pane_t pane, nv_snapshot_error_t *error)
{
	if(session == NULL || error == NULL ||
			(pane != NV_SESSION_LEFT && pane != NV_SESSION_RIGHT))
	{
		return -1;
	}
	nv_snapshot_error_free(error);
	nv_pane_snapshot_t *const snapshot = pane == NV_SESSION_LEFT ?
		&session->left : &session->right;
	return refresh_snapshot(session, snapshot, error);
}

int
nv_workspace_session_refresh_tab(nv_workspace_session_t *session,
		nv_session_pane_t pane, uint64_t tab_id, nv_snapshot_error_t *error)
{
	if(session == NULL || error == NULL || !valid_pane(pane)) return -1;
	nv_snapshot_error_free(error);
	if(ensure_tabs(session, error) != 0) return -1;
	nv_session_tabs_t *const tabs = pane_tabs(session, pane);
	size_t index = 0U;
	if(find_tab_index(tabs, tab_id, &index) != 0)
		return set_error(error, "invalid-tab", "tab does not exist");
	nv_pane_snapshot_t *const snapshot = index == tabs->active ?
		pane_snapshot(session, pane) : &tabs->items[index].snapshot;
	return refresh_snapshot(session, snapshot, error);
}

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
	if(hex == NULL)
	{
		return NULL;
	}
	const size_t length = strlen(hex);
	if(length % 2U != 0U || length/2U > NV_PANE_SNAPSHOT_MAX_HEX_BYTES/2U)
	{
		return NULL;
	}
	char *const decoded = malloc(length/2U + 1U);
	if(decoded == NULL)
	{
		return NULL;
	}
	for(size_t i = 0U; i < length; i += 2U)
	{
		const int high = hex_digit(hex[i]);
		const int low = hex_digit(hex[i + 1U]);
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

static char *
parent_path(const char path[])
{
	char *const result = strdup(path);
	if(result == NULL)
	{
		return NULL;
	}
	char *end = result + strlen(result);
	while(end > result + 1 && end[-1] == '/') --end;
	*end = '\0';
	char *const slash = strrchr(result, '/');
	if(slash == NULL)
	{
		free(result);
		return strdup("..");
	}
	if(slash == result)
	{
		result[1] = '\0';
	}
	else
	{
		*slash = '\0';
	}
	return result;
}

static const nv_pane_entry_t *
find_entry_by_path(const nv_pane_snapshot_t *snapshot,
		const char path_bytes_hex[])
{
	if(path_bytes_hex == NULL) return NULL;
	for(size_t i = 0U; i < snapshot->entry_count; ++i)
	{
		if(strcmp(snapshot->entries[i].path_bytes_hex, path_bytes_hex) == 0)
		{
			return &snapshot->entries[i];
		}
	}
	return NULL;
}

static int
valid_name(const char name[])
{
	if(name == NULL) return 0;
	const char *const end = memchr(name, '\0', NV_SESSION_MAX_NAME_BYTES + 1U);
	if(end == NULL) return 0;
	const size_t length = (size_t)(end - name);
	return length != 0U && length <= NV_SESSION_MAX_NAME_BYTES &&
			strcmp(name, ".") != 0 && strcmp(name, "..") != 0 &&
			strchr(name, '/') == NULL && strchr(name, '\\') == NULL;
}

void
nv_session_prepared_action_free(nv_session_prepared_action_t *action)
{
	if(action == NULL) return;
	free(action->source_directory);
	free(action->destination_directory);
	free(action->name);
	for(size_t i = 0U; i < action->target_count; ++i)
	{
		free(action->targets[i].path);
		free(action->targets[i].name);
	}
	free(action->targets);
	*action = (nv_session_prepared_action_t){};
}

static int
snapshot_identity_matches(const nv_pane_snapshot_t *snapshot,
		const nv_session_command_t *command, int destination)
{
	return snapshot->has_cwd_stat &&
			(destination ? command->action_destination_snapshot_revision :
				command->action_snapshot_revision) == snapshot->snapshot_revision &&
			(destination ? command->action_destination_cwd_device :
				command->action_cwd_device) == snapshot->cwd_device &&
			(destination ? command->action_destination_cwd_inode :
				command->action_cwd_inode) == snapshot->cwd_inode &&
			(destination ? command->action_destination_cwd_ctime_unix_ns :
				command->action_cwd_ctime_unix_ns) == snapshot->cwd_ctime_unix_ns;
}

static int
find_tab_index(const nv_session_tabs_t *tabs, uint64_t id, size_t *index)
{
	if(id == 0U) return -1;
	for(size_t i = 0U; i < tabs->count; ++i)
	{
		if(tabs->items[i].id == id)
		{
			*index = i;
			return 0;
		}
	}
	return -1;
}

static int
activate_tab_at(nv_workspace_session_t *session, nv_session_pane_t pane,
		size_t index, int focus_pane, nv_snapshot_error_t *error)
{
	nv_session_tabs_t *const tabs = pane_tabs(session, pane);
	if(index >= tabs->count)
	{
		return set_error(error, "invalid-tab", "tab does not exist");
	}
	if(index != tabs->active)
	{
		nv_pane_snapshot_t *const current = pane_snapshot(session, pane);
		tabs->items[tabs->active].snapshot = *current;
		*current = tabs->items[index].snapshot;
		tabs->items[index].snapshot = (nv_pane_snapshot_t){};
		tabs->active = index;
	}
	if(focus_pane) session->active_pane = pane;
	return 0;
}

static int
new_tab(nv_workspace_session_t *session, nv_session_pane_t pane,
		nv_snapshot_error_t *error)
{
	if(!valid_pane(pane))
		return set_error(error, "invalid-command", "invalid pane");
	if(ensure_tabs(session, error) != 0) return -1;
	nv_session_tabs_t *const tabs = pane_tabs(session, pane);
	if(tabs->count == NV_SESSION_MAX_TABS)
	{
		return set_error(error, "tab-limit", "pane tab limit reached");
	}
	nv_pane_snapshot_t clone = {};
	if(clone_snapshot(pane_snapshot(session, pane), &clone, error) != 0) return -1;
	const uint64_t id = next_tab_id(session);
	if(id == 0U)
	{
		nv_pane_snapshot_free(&clone);
		return set_error(error, "tab-id-exhausted",
				"no more tab identifiers are available");
	}
	clone.snapshot_revision = session->next_snapshot_revision++;
	const size_t inserted = tabs->active + 1U;
	for(size_t i = tabs->count; i > inserted; --i)
	{
		tabs->items[i] = tabs->items[i - 1U];
	}
	tabs->items[tabs->active].snapshot = *pane_snapshot(session, pane);
	*pane_snapshot(session, pane) = clone;
	tabs->items[inserted] = (nv_session_tab_t){ .id = id };
	++tabs->count;
	tabs->active = inserted;
	session->active_pane = pane;
	return 0;
}

static void
remove_tab_slot(nv_session_tabs_t *tabs, size_t index)
{
	for(size_t i = index; i + 1U < tabs->count; ++i)
	{
		tabs->items[i] = tabs->items[i + 1U];
	}
	--tabs->count;
	tabs->items[tabs->count] = (nv_session_tab_t){};
}

static int
close_tab(nv_workspace_session_t *session, nv_session_pane_t pane,
		uint64_t id, nv_snapshot_error_t *error)
{
	if(!valid_pane(pane))
		return set_error(error, "invalid-command", "invalid pane");
	if(ensure_tabs(session, error) != 0) return -1;
	nv_session_tabs_t *const tabs = pane_tabs(session, pane);
	size_t index = 0U;
	if(find_tab_index(tabs, id, &index) != 0)
		return set_error(error, "invalid-tab", "tab does not exist");
	if(tabs->count == 1U)
		return set_error(error, "last-tab", "cannot close the last pane tab");
	if(index != tabs->active)
	{
		nv_pane_snapshot_free(&tabs->items[index].snapshot);
		remove_tab_slot(tabs, index);
		if(index < tabs->active) --tabs->active;
		return 0;
	}

	const size_t replacement = index + 1U < tabs->count ? index + 1U : index - 1U;
	nv_pane_snapshot_t next = tabs->items[replacement].snapshot;
	tabs->items[replacement].snapshot = (nv_pane_snapshot_t){};
	nv_pane_snapshot_free(pane_snapshot(session, pane));
	*pane_snapshot(session, pane) = next;
	remove_tab_slot(tabs, index);
	tabs->active = replacement > index ? index : replacement;
	return 0;
}

int
nv_workspace_session_prepare_action(const nv_workspace_session_t *session,
		const nv_session_command_t *command,
		nv_session_prepared_action_t *action, nv_snapshot_error_t *error)
{
	if(session == NULL || command == NULL || action == NULL || error == NULL)
		return -1;
	nv_snapshot_error_free(error);
	nv_session_prepared_action_t next = {};
	if(command->pane != NV_SESSION_LEFT && command->pane != NV_SESSION_RIGHT)
		return set_error(error, "invalid-command", "invalid action pane");
	if(command->kind != NV_SESSION_COPY && command->kind != NV_SESSION_MOVE_FILES &&
			command->kind != NV_SESSION_MKDIR && command->kind != NV_SESSION_DELETE)
		return set_error(error, "invalid-command", "command is not a file action");
	const nv_pane_snapshot_t *const source = command->pane == NV_SESSION_LEFT ?
		&session->left : &session->right;
	if(command->action_cwd_bytes_hex == NULL ||
			strcmp(command->action_cwd_bytes_hex, source->cwd_bytes_hex) != 0 ||
			!snapshot_identity_matches(source, command, 0))
	{
		return set_error(error, "stale-action", "file action target is stale");
	}
	next.kind = command->kind;
	next.pane = command->pane;
	next.source_directory = hex_decode(source->cwd_bytes_hex);
	next.source_directory_identity = (nv_fs_identity_t){
		.device = source->cwd_device, .inode = source->cwd_inode,
		.ctime_unix_ns = source->cwd_ctime_unix_ns,
	};
	if(next.source_directory == NULL)
		goto invalid_path;
	if(command->kind == NV_SESSION_MKDIR)
	{
		if(!valid_name(command->name))
		{
			nv_session_prepared_action_free(&next);
			return set_error(error, "invalid-name", "directory name is invalid");
		}
		next.name = strdup(command->name);
		if(next.name == NULL) goto out_of_memory;
	}
	if(command->kind == NV_SESSION_COPY || command->kind == NV_SESSION_MOVE_FILES)
	{
		const nv_pane_snapshot_t *const destination = command->pane == NV_SESSION_LEFT ?
			&session->right : &session->left;
		if(command->action_destination_cwd_bytes_hex == NULL ||
				strcmp(command->action_destination_cwd_bytes_hex,
					destination->cwd_bytes_hex) != 0 ||
				!snapshot_identity_matches(destination, command, 1))
		{
			nv_session_prepared_action_free(&next);
			return set_error(error, "stale-action",
					"file action destination is stale");
		}
		next.destination_directory = hex_decode(destination->cwd_bytes_hex);
		next.destination_directory_identity = (nv_fs_identity_t){
			.device = destination->cwd_device, .inode = destination->cwd_inode,
			.ctime_unix_ns = destination->cwd_ctime_unix_ns,
		};
		if(next.destination_directory == NULL) goto invalid_path;
	}
	if(command->kind != NV_SESSION_MKDIR)
	{
		if(command->action_targets == NULL || command->action_target_count == 0U ||
				command->action_target_count > NV_SESSION_MAX_ACTION_PATHS)
		{
			nv_session_prepared_action_free(&next);
			return set_error(error, "stale-action", "file action target is stale");
		}
		next.targets = calloc(command->action_target_count, sizeof(*next.targets));
		if(next.targets == NULL) goto out_of_memory;
		next.target_count = command->action_target_count;
		for(size_t i = 0U; i < next.target_count; ++i)
		{
			const nv_pane_entry_t *const entry = find_entry_by_path(source,
				command->action_targets[i].path_bytes_hex);
			if(entry == NULL || !entry->has_stat ||
					entry->device != command->action_targets[i].device ||
					entry->inode != command->action_targets[i].inode ||
					entry->ctime_unix_ns != command->action_targets[i].ctime_unix_ns ||
					entry->kind != command->action_targets[i].kind)
			{
				nv_session_prepared_action_free(&next);
				return set_error(error, "stale-action", "file action entry is stale");
			}
			next.targets[i].path = hex_decode(entry->path_bytes_hex);
			next.targets[i].name = hex_decode(entry->name_bytes_hex);
			next.targets[i].identity = (nv_fs_identity_t){
				.device = entry->device, .inode = entry->inode,
				.ctime_unix_ns = entry->ctime_unix_ns,
			};
			next.targets[i].kind = entry->kind;
			if(next.targets[i].path == NULL || next.targets[i].name == NULL)
				goto invalid_path;
		}
	}
	nv_session_prepared_action_free(action);
	*action = next;
	return 0;

invalid_path:
	nv_session_prepared_action_free(&next);
	return set_error(error, "invalid-path", "file action identity is invalid");
out_of_memory:
	nv_session_prepared_action_free(&next);
	return set_error(error, "out-of-memory", "failed to prepare file action");
}

int
nv_workspace_session_apply(nv_workspace_session_t *session,
		const nv_session_command_t *command, nv_snapshot_error_t *error)
{
	if(session == NULL || command == NULL || error == NULL)
	{
		return -1;
	}
	nv_snapshot_error_free(error);
	if(command->kind == NV_SESSION_FOCUS)
	{
		if(command->pane != NV_SESSION_LEFT && command->pane != NV_SESSION_RIGHT)
		{
			return set_error(error, "invalid-command", "invalid pane");
		}
		session->active_pane = command->pane;
		return 0;
	}
	if(command->kind == NV_SESSION_FOCUS_NEXT)
	{
		session->active_pane = session->active_pane == NV_SESSION_LEFT ?
			NV_SESSION_RIGHT : NV_SESSION_LEFT;
		return 0;
	}
	if(command->kind == NV_SESSION_SORT_BY)
	{
		if(command->pane != NV_SESSION_LEFT && command->pane != NV_SESSION_RIGHT)
		{
			return set_error(error, "invalid-command", "invalid pane");
		}
		if(command->sort_key < NV_SORT_NAME || command->sort_key > NV_SORT_OTHER)
		{
			return set_error(error, "invalid-command", "invalid sort key");
		}
		session->active_pane = command->pane;
		nv_pane_snapshot_t *const target = active_snapshot(session);
		const int descending = target->sort_key == command->sort_key ?
			!target->sort_descending : 0;
		nv_pane_snapshot_sort(target, command->sort_key, descending);
		return 0;
	}
	if(command->kind == NV_SESSION_SELECT_ENTRY)
	{
		if(!valid_pane(command->pane))
			return set_error(error, "invalid-command", "invalid pane");
		if(command->toggle_selection != 0 && command->toggle_selection != 1)
			return set_error(error, "invalid-command", "invalid selection mode");
		nv_pane_snapshot_t *const target = pane_snapshot(session, command->pane);
		if(command->entry_index >= target->entry_count)
			return set_error(error, "invalid-entry", "entry does not exist");
		session->active_pane = command->pane;
		target->cursor = (int)command->entry_index;
		if(command->toggle_selection)
		{
			target->entries[command->entry_index].selected =
				!target->entries[command->entry_index].selected;
			if(target->entries[command->entry_index].selected)
				++target->selection_count;
			else
				--target->selection_count;
		}
		return 0;
	}
	if(command->kind == NV_SESSION_NEW_TAB)
		return new_tab(session, command->pane, error);
	if(command->kind == NV_SESSION_ACTIVATE_TAB)
	{
		if(!valid_pane(command->pane))
			return set_error(error, "invalid-command", "invalid pane");
		if(ensure_tabs(session, error) != 0) return -1;
		size_t index = 0U;
		if(find_tab_index(pane_tabs(session, command->pane), command->tab_id,
				&index) != 0)
			return set_error(error, "invalid-tab", "tab does not exist");
		return activate_tab_at(session, command->pane, index, 1, error);
	}
	if(command->kind == NV_SESSION_CLOSE_TAB)
		return close_tab(session, command->pane, command->tab_id, error);
	if(command->kind == NV_SESSION_TAB_CYCLE)
	{
		if(command->delta == 0 || command->delta < -NV_SESSION_MAX_TAB_CYCLE ||
				command->delta > NV_SESSION_MAX_TAB_CYCLE)
			return set_error(error, "invalid-command", "invalid tab direction");
		if(ensure_tabs(session, error) != 0) return -1;
		nv_session_tabs_t *const tabs = pane_tabs(session, session->active_pane);
		const int count = (int)tabs->count;
		int index = ((int)tabs->active + command->delta)%count;
		if(index < 0) index += count;
		return activate_tab_at(session, session->active_pane, (size_t)index, 1,
				error);
	}
	if(command->kind == NV_SESSION_SORT_CYCLE && command->has_pane)
	{
		if(!valid_pane(command->pane))
			return set_error(error, "invalid-command", "invalid pane");
		if(command->delta != -1 && command->delta != 1)
			return set_error(error, "invalid-command", "invalid sort direction");
		session->active_pane = command->pane;
	}

	nv_pane_snapshot_t *const snapshot = active_snapshot(session);
	switch(command->kind)
	{
		case NV_SESSION_MOVE_FIRST:
			if(snapshot->entry_count != 0U) snapshot->cursor = 0;
			return 0;
		case NV_SESSION_MOVE_LAST:
			if(snapshot->entry_count != 0U) snapshot->cursor = (int)snapshot->entry_count - 1;
			return 0;
		case NV_SESSION_MOVE_CURSOR:
			if(snapshot->entry_count == 0U) return 0;
			if(command->delta < 0 && snapshot->cursor > 0) --snapshot->cursor;
			if(command->delta > 0 && (size_t)snapshot->cursor + 1U < snapshot->entry_count) ++snapshot->cursor;
			return 0;
		case NV_SESSION_TOGGLE_SELECTION:
			if(snapshot->cursor < 0) return set_error(error, "empty-pane", "cannot select in an empty pane");
			snapshot->entries[snapshot->cursor].selected = !snapshot->entries[snapshot->cursor].selected;
			if(snapshot->entries[snapshot->cursor].selected)
			{
				++snapshot->selection_count;
			}
			else
			{
				--snapshot->selection_count;
			}
			return 0;
		case NV_SESSION_ENTER:
			if(snapshot->cursor < 0 || snapshot->entries[snapshot->cursor].kind != NV_ENTRY_DIRECTORY)
			{
				return set_error(error, "not-directory", "current entry is not a directory");
			}
			{
				char *const path = hex_decode(snapshot->entries[snapshot->cursor].path_bytes_hex);
				if(path == NULL) return set_error(error, "invalid-path", "directory identity is invalid");
				const int result = replace_snapshot(session, snapshot, path, error);
				free(path);
				return result;
			}
		case NV_SESSION_PARENT:
			{
				char *const path = hex_decode(snapshot->cwd_bytes_hex);
				if(path == NULL) return set_error(error, "invalid-path", "directory identity is invalid");
				char *const parent = parent_path(path);
				free(path);
				if(parent == NULL) return set_error(error, "out-of-memory", "failed to build parent path");
				const int result = replace_snapshot(session, snapshot, parent, error);
				free(parent);
				return result;
			}
		case NV_SESSION_REFRESH:
			return refresh_snapshot(session, snapshot, error);
		case NV_SESSION_SORT_CYCLE:
			{
				static const nv_pane_sort_key_t keys[] = {
					NV_SORT_NAME, NV_SORT_SIZE, NV_SORT_CTIME, NV_SORT_MTIME,
					NV_SORT_MODE,
				};
				size_t index = 0U;
				while(index + 1U < sizeof(keys)/sizeof(keys[0]) &&
						keys[index] != snapshot->sort_key) ++index;
				const size_t count = sizeof(keys)/sizeof(keys[0]);
				if(command->delta != -1 && command->delta != 1)
				{
					return set_error(error, "invalid-command", "invalid sort direction");
				}
				const size_t next = command->delta < 0 ?
					(index + count - 1U)%count : (index + 1U)%count;
				nv_pane_snapshot_sort(snapshot, keys[next], 0);
				return 0;
			}
		case NV_SESSION_COPY:
		case NV_SESSION_MOVE_FILES:
		case NV_SESSION_DELETE:
		case NV_SESSION_MKDIR:
			return set_error(error, "async-action-required",
					"file actions must run through the action queue");
		case NV_SESSION_UNDO:
			return set_error(error, "core-command-required",
					"undo must run through the core session command path");
		case NV_SESSION_FOCUS:
		case NV_SESSION_FOCUS_NEXT:
		case NV_SESSION_SORT_BY:
		case NV_SESSION_SELECT_ENTRY:
		case NV_SESSION_NEW_TAB:
		case NV_SESSION_ACTIVATE_TAB:
		case NV_SESSION_CLOSE_TAB:
		case NV_SESSION_TAB_CYCLE:
		case NV_SESSION_PREVIEW:
		case NV_SESSION_OPEN:
		case NV_SESSION_CANCEL_ACTION:
		case NV_SESSION_RETRY_ACTION:
			break;
	}
	return set_error(error, "invalid-command", "unsupported session command");
}

void
nv_session_command_free(nv_session_command_t *command)
{
	if(command == NULL) return;
	if(command->owns_action_fields)
	{
		free(command->action_cwd_bytes_hex);
		free(command->action_destination_cwd_bytes_hex);
		for(size_t i = 0U; i < command->action_target_count; ++i)
		{
			free(command->action_targets[i].path_bytes_hex);
		}
		free(command->action_targets);
	}
	if(command->owns_preview_fields)
	{
		free(command->preview_cwd_bytes_hex);
		free(command->preview_path_bytes_hex);
	}
	if(command->owns_open_fields)
	{
		free(command->open_cwd_bytes_hex);
		free(command->open_path_bytes_hex);
		for(size_t i = 0U; i < command->open_association_argc; ++i)
			free(command->open_association_argv[i]);
		free(command->open_association_argv);
	}
	memset(command, 0, sizeof(*command));
}

void
nv_workspace_session_free(nv_workspace_session_t *session)
{
	if(session == NULL)
	{
		return;
	}
	nv_pane_snapshot_free(&session->left);
	nv_pane_snapshot_free(&session->right);
	for(size_t i = 0U; i < session->left_tabs.count; ++i)
	{
		if(i != session->left_tabs.active)
			nv_pane_snapshot_free(&session->left_tabs.items[i].snapshot);
	}
	for(size_t i = 0U; i < session->right_tabs.count; ++i)
	{
		if(i != session->right_tabs.active)
			nv_pane_snapshot_free(&session->right_tabs.items[i].snapshot);
	}
	memset(session, 0, sizeof(*session));
}
