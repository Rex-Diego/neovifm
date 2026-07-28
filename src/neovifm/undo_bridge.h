/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef VIFM__NEOVIFM__UNDO_BRIDGE_H__
#define VIFM__NEOVIFM__UNDO_BRIDGE_H__

#include "../compat/neovifm_fs.h"

typedef enum
{
	NV_UNDO_BRIDGE_SUCCESS,
	NV_UNDO_BRIDGE_NONE,
	NV_UNDO_BRIDGE_FAILED,
} nv_undo_bridge_result_t;

typedef struct
{
	unsigned int pane;
	uint64_t tab_id;
} nv_undo_bridge_location_t;

/* Initializes the process-local classic Vifm undo stack. */
int nv_undo_bridge_init(void);

/* Discards the process-local undo stack and all bridge metadata. */
void nv_undo_bridge_reset(void);

/* Records one identity-checked mkdir action after it has completed. */
int nv_undo_bridge_record_mkdir(const char path[],
		nv_fs_identity_t parent_identity, nv_undo_bridge_location_t location);

/* Undoes the most recent supported action on the session's main thread. */
nv_undo_bridge_result_t nv_undo_bridge_undo(nv_undo_bridge_location_t *location);

#endif /* VIFM__NEOVIFM__UNDO_BRIDGE_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
