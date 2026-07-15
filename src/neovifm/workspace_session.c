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

static int set_error(nv_snapshot_error_t *error, const char code[],
		const char message[]);
static nv_pane_snapshot_t *active_snapshot(nv_workspace_session_t *session);
static int replace_active_snapshot(nv_workspace_session_t *session,
		const char path[], nv_snapshot_error_t *error);
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
	nv_workspace_session_free(session);
	nv_snapshot_error_free(error);
	*session = next;
	return 0;
}

static int
replace_active_snapshot(nv_workspace_session_t *session, const char path[],
		nv_snapshot_error_t *error)
{
	nv_pane_snapshot_t *const current = active_snapshot(session);
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
	const int result = replace_active_snapshot(session, path, error);
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
	const nv_session_pane_t previous_pane = session->active_pane;
	session->active_pane = pane;
	const int result = refresh_snapshot(session, snapshot, error);
	session->active_pane = previous_pane;
	return result;
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
				const int result = replace_active_snapshot(session, path, error);
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
				const int result = replace_active_snapshot(session, parent, error);
				free(parent);
				return result;
			}
		case NV_SESSION_REFRESH:
			return refresh_snapshot(session, snapshot, error);
		case NV_SESSION_FOCUS:
		case NV_SESSION_FOCUS_NEXT:
			break;
	}
	return set_error(error, "invalid-command", "unsupported session command");
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
	memset(session, 0, sizeof(*session));
}
