/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef VIFM__NEOVIFM__CLASSIC_PANE_ADAPTER_H__
#define VIFM__NEOVIFM__CLASSIC_PANE_ADAPTER_H__

#include "pane_snapshot.h"
#include "workspace_session.h"

typedef struct view_t view_t;

typedef enum
{
	NV_CLASSIC_PANE_LEFT,
	NV_CLASSIC_PANE_RIGHT,
} nv_classic_pane_t;

typedef struct
{
	nv_pane_snapshot_t left;
	nv_pane_snapshot_t right;
	nv_classic_pane_t active_pane;
} nv_classic_workspace_snapshot_t;

/*
 * Copies an already-loaded classic view into an owned snapshot.  This never
 * loads a directory, changes process cwd, touches ncurses, or retains view
 * pointers.  Call only from the classic UI thread.
 */
int nv_pane_snapshot_from_classic_view(const view_t *view,
		nv_pane_snapshot_t *snapshot, nv_snapshot_error_t *error);

/*
 * Atomically snapshots two already-loaded classic panes.  Like the one-pane
 * adapter, this is UI-thread-only and retains no view or entry pointers.
 */
int nv_classic_workspace_snapshot_from_views(const view_t *left,
		const view_t *right, nv_classic_pane_t active_pane,
		nv_classic_workspace_snapshot_t *workspace,
		nv_snapshot_error_t *error);
void nv_classic_workspace_snapshot_free(
		nv_classic_workspace_snapshot_t *workspace);

/* Builds the same owned session model from classic panes without exposing
 * view_t to the headless session process.  Classic callers use it only on
 * their UI thread after filelist has produced both loaded views. */
int nv_workspace_session_init_from_classic_views(const view_t *left,
		const view_t *right, nv_classic_pane_t active_pane,
		nv_workspace_session_t *session, nv_snapshot_error_t *error);

#endif /* VIFM__NEOVIFM__CLASSIC_PANE_ADAPTER_H__ */
