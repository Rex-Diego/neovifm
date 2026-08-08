/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef VIFM__NEOVIFM__SESSION_PLATFORM_H__
#define VIFM__NEOVIFM__SESSION_PLATFORM_H__

#include <stdio.h> /* FILE */

char * nv_session_state_path(void);
int nv_session_ensure_parent_directory(const char path[]);
int nv_session_directory_exists(const char path[]);

FILE * nv_session_fopen(const char path[], const char mode[]);
int nv_session_open_temporary(const char path[], char **temporary);
int nv_session_replace_file(const char temporary[], const char path[]);
int nv_session_remove_file(const char path[]);

int nv_session_poll_stdin(int *ready);

#endif /* VIFM__NEOVIFM__SESSION_PLATFORM_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
