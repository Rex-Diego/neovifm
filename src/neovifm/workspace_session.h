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

typedef enum
{
	NV_SESSION_LEFT,
	NV_SESSION_RIGHT,
} nv_session_pane_t;

typedef enum
{
	NV_SESSION_FOCUS,
	NV_SESSION_MOVE_CURSOR,
	NV_SESSION_ENTER,
	NV_SESSION_PARENT,
	NV_SESSION_TOGGLE_SELECTION,
	NV_SESSION_REFRESH,
} nv_session_command_kind_t;

typedef struct
{
	nv_session_command_kind_t kind;
	nv_session_pane_t pane;
	int delta;
} nv_session_command_t;

typedef struct
{
	nv_pane_snapshot_t left;
	nv_pane_snapshot_t right;
	nv_session_pane_t active_pane;
} nv_workspace_session_t;

int nv_workspace_session_init(const char left_path[], const char right_path[],
		nv_workspace_session_t *session, nv_snapshot_error_t *error);
int nv_workspace_session_apply(nv_workspace_session_t *session,
		const nv_session_command_t *command, nv_snapshot_error_t *error);
int nv_workspace_session_refresh_pane(nv_workspace_session_t *session,
		nv_session_pane_t pane, nv_snapshot_error_t *error);
const nv_pane_snapshot_t *nv_workspace_session_active(
		const nv_workspace_session_t *session);
const char *nv_workspace_session_active_name(const nv_workspace_session_t *session);
void nv_workspace_session_free(nv_workspace_session_t *session);

#endif /* VIFM__NEOVIFM__WORKSPACE_SESSION_H__ */
