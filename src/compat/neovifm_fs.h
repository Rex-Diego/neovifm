/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef VIFM__COMPAT__NEOVIFM_FS_H__
#define VIFM__COMPAT__NEOVIFM_FS_H__

#include <sys/stat.h> /* struct stat */

typedef struct nv_dir_t nv_dir_t;

nv_dir_t * nv_dir_open(const char path[]);
const char * nv_dir_read(nv_dir_t *dir);
int nv_dir_close(nv_dir_t *dir);

/*
 * Gets no-follow metadata and reports symbolic-link identity separately.
 * On Windows this uses FindFirstFileW() so dangling links retain their
 * identity even though struct stat has no portable S_IFLNK representation.
 */
int nv_lstat(const char path[], struct stat *st, int *is_symlink);

#endif /* VIFM__COMPAT__NEOVIFM_FS_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
