/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef VIFM__NEOVIFM__SNAPSHOT_JSON_H__
#define VIFM__NEOVIFM__SNAPSHOT_JSON_H__

#include "pane_snapshot.h"

char *nv_protocol_hello_json(unsigned int sequence);
char *nv_protocol_snapshot_json(const nv_pane_snapshot_t *snapshot,
		unsigned int sequence);
char *nv_protocol_error_json(const nv_snapshot_error_t *error,
		unsigned int sequence);

void nv_protocol_json_free(char *json);

#endif /* VIFM__NEOVIFM__SNAPSHOT_JSON_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
