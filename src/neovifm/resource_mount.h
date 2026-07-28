/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 or 3 of the License.
 */

#ifndef VIFM__NEOVIFM__RESOURCE_MOUNT_H__
#define VIFM__NEOVIFM__RESOURCE_MOUNT_H__

#include <stddef.h> /* size_t */

#define NV_RESOURCE_MAX_PATH_BYTES (16U*1024U)
#define NV_RESOURCE_MAX_REMOTE_BYTES (16U*1024U)
#define NV_RESOURCE_MAX_ARGS 8U

typedef enum
{
	NV_RESOURCE_MOUNT_ARCHIVE,
	NV_RESOURCE_MOUNT_SSH,
} nv_resource_mount_kind_t;

typedef enum
{
	NV_RESOURCE_MOUNTER_FUSE_ZIP,
	NV_RESOURCE_MOUNTER_ARCHIVEMOUNT,
	NV_RESOURCE_MOUNTER_SSHFS,
} nv_resource_mounter_kind_t;

/* Optional absolute helper paths used by tests and controlled deployments.
 * Empty values use the platform candidate list.  No value is interpreted as
 * a shell command or searched through PATH. */
typedef struct
{
	const char *fuse_zip_path;
	const char *archivemount_path;
	const char *sshfs_path;
	const char *unmount_path;
} nv_resource_mount_options_t;

typedef struct
{
	char *helper_path;
	char *unmount_path;
	char **argv;
	size_t argc;
	nv_resource_mount_kind_t resource_kind;
	nv_resource_mounter_kind_t mounter_kind;
	int read_only;
} nv_resource_mount_spec_t;

typedef struct
{
	char *code;
	char *message;
	int os_error;
} nv_resource_mount_error_t;

/* Prepares a safe, read-only archive mount invocation.  The caller still
 * needs to run it asynchronously and own the mount lifecycle. */
int nv_resource_mount_prepare_archive(const char source_path[],
		const char mount_point[], const nv_resource_mount_options_t *options,
		nv_resource_mount_spec_t *spec, nv_resource_mount_error_t *error);

/* Prepares a safe, read-only sshfs invocation.  The remote string is passed as
 * one argv item and is never logged or interpolated into shell syntax. */
int nv_resource_mount_prepare_ssh(const char remote[],
		const char mount_point[], const nv_resource_mount_options_t *options,
		nv_resource_mount_spec_t *spec, nv_resource_mount_error_t *error);

void nv_resource_mount_spec_free(nv_resource_mount_spec_t *spec);
void nv_resource_mount_error_free(nv_resource_mount_error_t *error);
const char *nv_resource_mount_kind_name(nv_resource_mount_kind_t kind);
const char *nv_resource_mounter_name(nv_resource_mounter_kind_t kind);

#endif /* VIFM__NEOVIFM__RESOURCE_MOUNT_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
