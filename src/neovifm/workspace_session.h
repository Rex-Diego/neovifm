/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef VIFM__NEOVIFM__WORKSPACE_SESSION_H__
#define VIFM__NEOVIFM__WORKSPACE_SESSION_H__

#include "pane_snapshot.h"
#include "../compat/neovifm_fs.h"

#define NV_SESSION_MAX_NAME_BYTES 255U
#define NV_SESSION_MAX_ACTION_PATHS 64U

typedef enum
{
	NV_SESSION_LEFT,
	NV_SESSION_RIGHT,
} nv_session_pane_t;

typedef enum
{
	NV_SESSION_FOCUS,
	NV_SESSION_FOCUS_NEXT,
	NV_SESSION_MOVE_CURSOR,
	NV_SESSION_MOVE_FIRST,
	NV_SESSION_MOVE_LAST,
	NV_SESSION_ENTER,
	NV_SESSION_PARENT,
	NV_SESSION_TOGGLE_SELECTION,
	NV_SESSION_REFRESH,
	NV_SESSION_SORT_CYCLE,
	NV_SESSION_SORT_BY,
	NV_SESSION_COPY,
	NV_SESSION_MOVE_FILES,
	NV_SESSION_MKDIR,
	NV_SESSION_DELETE,
} nv_session_command_kind_t;

typedef struct
{
	char *path_bytes_hex;
	uint64_t device;
	uint64_t inode;
	uint64_t ctime_unix_ns;
	nv_entry_kind_t kind;
} nv_session_action_target_t;

typedef struct
{
	nv_session_command_kind_t kind;
	nv_session_pane_t pane;
	int delta;
	nv_pane_sort_key_t sort_key;
	char name[NV_SESSION_MAX_NAME_BYTES + 1U];
	char *action_cwd_bytes_hex;
	uint64_t action_snapshot_revision;
	uint64_t action_cwd_device;
	uint64_t action_cwd_inode;
	uint64_t action_cwd_ctime_unix_ns;
	char *action_destination_cwd_bytes_hex;
	uint64_t action_destination_snapshot_revision;
	uint64_t action_destination_cwd_device;
	uint64_t action_destination_cwd_inode;
	uint64_t action_destination_cwd_ctime_unix_ns;
	nv_session_action_target_t *action_targets;
	size_t action_target_count;
	int owns_action_fields;
} nv_session_command_t;

typedef struct
{
	char *path;
	char *name;
	nv_fs_identity_t identity;
	nv_entry_kind_t kind;
} nv_session_prepared_target_t;

typedef struct
{
	nv_session_command_kind_t kind;
	nv_session_pane_t pane;
	char *source_directory;
	char *destination_directory;
	nv_fs_identity_t source_directory_identity;
	nv_fs_identity_t destination_directory_identity;
	char *name;
	nv_session_prepared_target_t *targets;
	size_t target_count;
} nv_session_prepared_action_t;

typedef struct
{
	nv_pane_snapshot_t left;
	nv_pane_snapshot_t right;
	nv_session_pane_t active_pane;
	uint64_t next_snapshot_revision;
} nv_workspace_session_t;

int nv_workspace_session_init(const char left_path[], const char right_path[],
		nv_workspace_session_t *session, nv_snapshot_error_t *error);
int nv_workspace_session_apply(nv_workspace_session_t *session,
		const nv_session_command_t *command, nv_snapshot_error_t *error);
int nv_workspace_session_prepare_action(const nv_workspace_session_t *session,
		const nv_session_command_t *command,
		nv_session_prepared_action_t *action, nv_snapshot_error_t *error);
void nv_session_prepared_action_free(nv_session_prepared_action_t *action);
int nv_workspace_session_refresh_pane(nv_workspace_session_t *session,
		nv_session_pane_t pane, nv_snapshot_error_t *error);
const nv_pane_snapshot_t *nv_workspace_session_active(
		const nv_workspace_session_t *session);
const char *nv_workspace_session_active_name(const nv_workspace_session_t *session);
void nv_session_command_free(nv_session_command_t *command);
void nv_workspace_session_free(nv_workspace_session_t *session);

#endif /* VIFM__NEOVIFM__WORKSPACE_SESSION_H__ */
