/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef VIFM__NEOVIFM__PANE_SNAPSHOT_H__
#define VIFM__NEOVIFM__PANE_SNAPSHOT_H__

#include <stddef.h> /* size_t */
#include <stdint.h> /* int64_t uint32_t uint64_t */

#define NV_PROTOCOL_MAX_RECORD_BYTES (4U*1024U*1024U)
#define NV_PROTOCOL_MAX_STREAM_BYTES (8U*1024U*1024U)
#define NV_PANE_SNAPSHOT_MAX_ENTRIES 4096U
#define NV_PANE_SNAPSHOT_MAX_DISPLAY_BYTES (16U*1024U)
#define NV_PANE_SNAPSHOT_MAX_HEX_BYTES (32U*1024U)
#define NV_PANE_SNAPSHOT_MAX_OWNER_BYTES 256U

typedef enum
{
	NV_ENTRY_DIRECTORY,
	NV_ENTRY_FILE,
	NV_ENTRY_SYMLINK,
	NV_ENTRY_EXECUTABLE,
	NV_ENTRY_FIFO,
	NV_ENTRY_SOCKET,
	NV_ENTRY_CHAR_DEVICE,
	NV_ENTRY_BLOCK_DEVICE,
	NV_ENTRY_UNKNOWN,
} nv_entry_kind_t;

typedef enum
{
	NV_ENTRY_RESOURCE_NONE,
	NV_ENTRY_RESOURCE_ARCHIVE,
} nv_entry_resource_kind_t;

typedef enum
{
	NV_SORT_NAME,
	NV_SORT_EXTENSION,
	NV_SORT_SIZE,
	NV_SORT_CTIME,
	NV_SORT_MTIME,
	NV_SORT_MODE,
	NV_SORT_TYPE,
	NV_SORT_OTHER,
} nv_pane_sort_key_t;

typedef struct
{
	char *name_display;
	char *name_bytes_hex;
	char *path_display;
	char *path_bytes_hex;
	char *owner_display;
	char *group_display;
	nv_entry_kind_t kind;
	nv_entry_resource_kind_t resource_kind;
	uint64_t size_bytes;
	int64_t mtime_unix_ms;
	uint64_t device;
	uint64_t inode;
	uint64_t ctime_unix_ns;
	uint32_t mode;
	int has_stat;
	int stat_error;
	int selected;
	int hidden;
} nv_pane_entry_t;

typedef struct
{
	char *cwd_display;
	char *cwd_bytes_hex;
	int64_t generated_at_unix_ms;
	uint64_t snapshot_revision;
	uint64_t cwd_device;
	uint64_t cwd_inode;
	uint64_t cwd_ctime_unix_ns;
	int has_cwd_stat;
	int cursor;
	size_t entry_count;
	size_t selection_count;
	size_t filtered_count;
	nv_pane_sort_key_t sort_key;
	int sort_descending;
	int filter_active;
	nv_pane_entry_t *entries;
} nv_pane_snapshot_t;

typedef struct
{
	char *code;
	char *message;
	char *path_display;
	char *path_bytes_hex;
	int os_error;
	int retryable;
} nv_snapshot_error_t;

/*
 * Builds an owned, immutable directory snapshot.  The outputs must be
 * zero-initialized before their first use.  A successful call replaces both
 * outputs; a failed call preserves the previous snapshot and replaces error.
 * Returns zero on success.
 */
int nv_pane_snapshot_build(const char path[], nv_pane_snapshot_t *snapshot,
		nv_snapshot_error_t *error);

void nv_pane_snapshot_sort(nv_pane_snapshot_t *snapshot,
		nv_pane_sort_key_t key, int descending);

void nv_pane_snapshot_free(nv_pane_snapshot_t *snapshot);
void nv_snapshot_error_free(nv_snapshot_error_t *error);

#endif /* VIFM__NEOVIFM__PANE_SNAPSHOT_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
