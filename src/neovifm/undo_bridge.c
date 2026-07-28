/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "undo_bridge.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../undo.h"

typedef struct nv_undo_mkdir_record_t nv_undo_mkdir_record_t;

struct nv_undo_mkdir_record_t
{
	char *path;
	nv_fs_identity_t parent_identity;
	nv_fs_identity_t child_identity;
	nv_undo_bridge_location_t location;
	nv_undo_mkdir_record_t *next;
};

static nv_undo_mkdir_record_t *records;
static int initialized;
static int undo_levels = 64;

static uint64_t
ctime_unix_ns(const struct stat *st)
{
#if defined(__APPLE__)
	return (uint64_t)st->st_ctimespec.tv_sec*1000000000ULL +
		(uint64_t)st->st_ctimespec.tv_nsec;
#elif defined(__linux__)
	return (uint64_t)st->st_ctim.tv_sec*1000000000ULL +
		(uint64_t)st->st_ctim.tv_nsec;
#else
	return (uint64_t)st->st_ctime*1000000000ULL;
#endif
}

static int
identity_equal(nv_fs_identity_t left, nv_fs_identity_t right)
{
	return left.device == right.device && left.inode == right.inode &&
		left.ctime_unix_ns == right.ctime_unix_ns;
}

static nv_undo_mkdir_record_t *
find_record(const char path[])
{
	for(nv_undo_mkdir_record_t *record = records; record != NULL;
			record = record->next)
	{
		if(strcmp(record->path, path) == 0) return record;
	}
	return NULL;
}

static OpsResult
perform_undo_operation(OPS op, void *data, const char src[], const char dst[])
{
	(void)data;
	(void)dst;
	if(src == NULL) return OPS_FAILED;
	nv_undo_mkdir_record_t *const record = find_record(src);
	if(record == NULL)
	{
		errno = ENOENT;
		return OPS_FAILED;
	}

	if(op == OP_RMDIR)
	{
		struct stat st = {};
		int is_symlink = 0;
		if(nv_lstat(src, &st, &is_symlink) != 0 || is_symlink != 0 ||
				!S_ISDIR(st.st_mode))
		{
			if(errno == 0) errno = ESTALE;
			return OPS_FAILED;
		}
		const nv_fs_identity_t current = {
			.device = (uint64_t)st.st_dev,
			.inode = (uint64_t)st.st_ino,
			.ctime_unix_ns = ctime_unix_ns(&st),
		};
		if(!identity_equal(current, record->child_identity))
		{
			errno = ESTALE;
			return OPS_FAILED;
		}
		return nv_fs_remove(src, record->parent_identity,
				record->child_identity, NULL, NULL) == 0 ? OPS_SUCCEEDED :
			OPS_FAILED;
	}

	if(op == OP_MKDIR)
	{
		if(nv_fs_mkdir(src, 0777, record->parent_identity) != 0)
			return OPS_FAILED;
		struct stat st = {};
		int is_symlink = 0;
		if(nv_lstat(src, &st, &is_symlink) != 0 || is_symlink != 0 ||
				!S_ISDIR(st.st_mode))
		{
			(void)nv_fs_remove(src, record->parent_identity,
					record->child_identity, NULL, NULL);
			errno = EIO;
			return OPS_FAILED;
		}
		record->child_identity = (nv_fs_identity_t){
			.device = (uint64_t)st.st_dev,
			.inode = (uint64_t)st.st_ino,
			.ctime_unix_ns = ctime_unix_ns(&st),
		};
		return OPS_SUCCEEDED;
	}

	errno = ENOTSUP;
	return OPS_FAILED;
}

static int
operation_available(OPS op)
{
	return op == OP_MKDIR || op == OP_RMDIR ? 0 : -1;
}

int
nv_undo_bridge_init(void)
{
	if(initialized) return 0;
	un_init(perform_undo_operation, operation_available, NULL, &undo_levels);
	initialized = 1;
	return 0;
}

void
nv_undo_bridge_reset(void)
{
	if(!initialized) return;
	un_reset();
	while(records != NULL)
	{
		nv_undo_mkdir_record_t *const next = records->next;
		free(records->path);
		free(records);
		records = next;
	}
	initialized = 0;
}

int
nv_undo_bridge_record_mkdir(const char path[], nv_fs_identity_t parent_identity,
		nv_undo_bridge_location_t location)
{
	if(!initialized || path == NULL || path[0] == '\0')
	{
		errno = EINVAL;
		return -1;
	}
	struct stat st = {};
	int is_symlink = 0;
	if(nv_lstat(path, &st, &is_symlink) != 0 || is_symlink != 0 ||
			!S_ISDIR(st.st_mode))
	{
		if(errno == 0) errno = EINVAL;
		return -1;
	}
	nv_undo_mkdir_record_t *const record = calloc(1U, sizeof(*record));
	if(record == NULL || (record->path = strdup(path)) == NULL)
	{
		free(record);
		errno = ENOMEM;
		return -1;
	}
	record->parent_identity = parent_identity;
	record->location = location;
	record->child_identity = (nv_fs_identity_t){
		.device = (uint64_t)st.st_dev,
		.inode = (uint64_t)st.st_ino,
		.ctime_unix_ns = ctime_unix_ns(&st),
	};
	un_group_open("mkdir");
	if(un_group_add_op(OP_MKDIR, NULL, NULL, path, "") != 0)
	{
		un_group_close();
		free(record->path);
		free(record);
		errno = ENOMEM;
		return -1;
	}
	un_group_close();
	record->next = records;
	records = record;
	return 0;
}

nv_undo_bridge_result_t
nv_undo_bridge_undo(nv_undo_bridge_location_t *location)
{
	if(location != NULL) *location = (nv_undo_bridge_location_t){};
	if(!initialized) return NV_UNDO_BRIDGE_FAILED;
	const UnErrCode result = un_group_undo();
	if(result == UN_ERR_SUCCESS && location != NULL && records != NULL)
		*location = records->location;
	if(result == UN_ERR_SUCCESS && records != NULL)
	{
		nv_undo_mkdir_record_t *const record = records;
		records = record->next;
		free(record->path);
		free(record);
	}
	switch(result)
	{
		case UN_ERR_SUCCESS: return NV_UNDO_BRIDGE_SUCCESS;
		case UN_ERR_NONE: return NV_UNDO_BRIDGE_NONE;
		default: return NV_UNDO_BRIDGE_FAILED;
	}
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
