/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 or 3 of the License.
 */

#include "resource_mount.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
# include <unistd.h>
#endif

static int set_error(nv_resource_mount_error_t *error, const char code[],
		const char message[], int os_error);
static int path_valid(const char value[], size_t maximum, int absolute);
static int helper_valid(const char path[]);
static int append_argument(nv_resource_mount_spec_t *spec, const char value[]);
static int select_helper(const char explicit_path[], const char *const candidates[],
		nv_resource_mounter_kind_t kind, char **path,
		nv_resource_mount_error_t *error);
static int select_unmount(const char explicit_path[], char **path,
		nv_resource_mount_error_t *error);
static int prepare_common(nv_resource_mount_kind_t resource_kind,
		nv_resource_mounter_kind_t mounter_kind, const char source[],
		const char mount_point[], const char remote[],
		const nv_resource_mount_options_t *options,
		nv_resource_mount_spec_t *spec, nv_resource_mount_error_t *error);

static const char *const fuse_zip_candidates[] = {
	"/usr/local/bin/fuse-zip", "/opt/homebrew/bin/fuse-zip",
	"/usr/bin/fuse-zip", NULL,
};

static const char *const archivemount_candidates[] = {
	"/usr/local/bin/archivemount", "/opt/homebrew/bin/archivemount",
	"/usr/bin/archivemount", NULL,
};

static const char *const sshfs_candidates[] = {
	"/usr/local/bin/sshfs", "/opt/homebrew/bin/sshfs", "/usr/bin/sshfs",
	NULL,
};

static const char *const unmount_candidates[] = {
	"/sbin/umount", "/usr/sbin/umount", "/usr/bin/umount",
	"/usr/local/bin/fusermount3", "/usr/bin/fusermount3",
	"/usr/local/bin/fusermount", "/usr/bin/fusermount", NULL,
};

const char *
nv_resource_mount_kind_name(nv_resource_mount_kind_t kind)
{
	switch(kind)
	{
		case NV_RESOURCE_MOUNT_ARCHIVE: return "archive";
		case NV_RESOURCE_MOUNT_SSH: return "ssh";
	}
	return NULL;
}

const char *
nv_resource_mounter_name(nv_resource_mounter_kind_t kind)
{
	switch(kind)
	{
		case NV_RESOURCE_MOUNTER_FUSE_ZIP: return "fuse-zip";
		case NV_RESOURCE_MOUNTER_ARCHIVEMOUNT: return "archivemount";
		case NV_RESOURCE_MOUNTER_SSHFS: return "sshfs";
	}
	return NULL;
}

void
nv_resource_mount_spec_free(nv_resource_mount_spec_t *spec)
{
	if(spec == NULL) return;
	free(spec->helper_path);
	free(spec->unmount_path);
	if(spec->argv != NULL)
	{
		for(size_t i = 0U; i < spec->argc; ++i) free(spec->argv[i]);
		free(spec->argv);
	}
	*spec = (nv_resource_mount_spec_t){};
}

void
nv_resource_mount_error_free(nv_resource_mount_error_t *error)
{
	if(error == NULL) return;
	free(error->code);
	free(error->message);
	*error = (nv_resource_mount_error_t){};
}

static int
set_error(nv_resource_mount_error_t *error, const char code[],
		const char message[], int os_error)
{
	if(error == NULL) return -1;
	nv_resource_mount_error_free(error);
	error->code = strdup(code);
	error->message = strdup(message);
	error->os_error = os_error;
	if(error->code == NULL || error->message == NULL)
	{
		nv_resource_mount_error_free(error);
	}
	return -1;
}

static int
path_valid(const char value[], size_t maximum, int absolute)
{
	if(value == NULL || value[0] == '\0' || (absolute && value[0] != '/'))
		return 0;
	for(size_t i = 0U; i <= maximum; ++i)
	{
		const unsigned char character = (unsigned char)value[i];
		if(character == '\0') return i != 0U;
		if(character < 0x20U || character == 0x7fU) return 0;
	}
	return 0;
}

static int
helper_valid(const char path[])
{
#ifdef _WIN32
	(void)path;
	return 0;
#else
	return path_valid(path, NV_RESOURCE_MAX_PATH_BYTES, 1) &&
			access(path, X_OK) == 0;
#endif
}

static int
append_argument(nv_resource_mount_spec_t *spec, const char value[])
{
	if(spec == NULL || value == NULL || spec->argc >= NV_RESOURCE_MAX_ARGS)
		return -1;
	char *const copy = strdup(value);
	if(copy == NULL) return -1;
	char **const resized = realloc(spec->argv,
			(spec->argc + 2U)*sizeof(*resized));
	if(resized == NULL)
	{
		free(copy);
		return -1;
	}
	spec->argv = resized;
	spec->argv[spec->argc++] = copy;
	spec->argv[spec->argc] = NULL;
	return 0;
}

static int
select_helper(const char explicit_path[], const char *const candidates[],
		nv_resource_mounter_kind_t kind, char **path,
		nv_resource_mount_error_t *error)
{
	if(path == NULL) return set_error(error, "invalid-output",
			"mount helper output is invalid", EINVAL);
	*path = NULL;
	if(explicit_path != NULL && explicit_path[0] != '\0')
	{
		if(!helper_valid(explicit_path))
			return set_error(error, "resource-mounter-unavailable",
					"configured resource mounter is unavailable", errno);
		*path = strdup(explicit_path);
		return *path == NULL ? set_error(error, "out-of-memory",
				"failed to copy resource mounter path", ENOMEM) : 0;
	}
	for(size_t i = 0U; candidates != NULL && candidates[i] != NULL; ++i)
	{
		if(helper_valid(candidates[i]))
		{
			*path = strdup(candidates[i]);
			return *path == NULL ? set_error(error, "out-of-memory",
					"failed to copy resource mounter path", ENOMEM) : 0;
		}
	}
	(void)kind;
	return set_error(error, "resource-mounter-unavailable",
			"no compatible resource mounter is installed", ENOENT);
}

static int
select_unmount(const char explicit_path[], char **path,
		nv_resource_mount_error_t *error)
{
	return select_helper(explicit_path, unmount_candidates,
			NV_RESOURCE_MOUNTER_SSHFS, path, error);
}

static int
prepare_common(nv_resource_mount_kind_t resource_kind,
		nv_resource_mounter_kind_t mounter_kind, const char source[],
		const char mount_point[], const char remote[],
		const nv_resource_mount_options_t *options,
		nv_resource_mount_spec_t *spec, nv_resource_mount_error_t *error)
{
	if(spec == NULL || error == NULL)
		return -1;
	nv_resource_mount_spec_free(spec);
	nv_resource_mount_error_free(error);
	if(!path_valid(mount_point, NV_RESOURCE_MAX_PATH_BYTES, 1) ||
			(resource_kind == NV_RESOURCE_MOUNT_ARCHIVE &&
				!path_valid(source, NV_RESOURCE_MAX_PATH_BYTES, 1)) ||
			(resource_kind == NV_RESOURCE_MOUNT_SSH &&
				!path_valid(remote, NV_RESOURCE_MAX_REMOTE_BYTES, 0)))
	{
		return set_error(error, "invalid-resource-path",
				"resource source or mount point is invalid", EINVAL);
	}
	if(resource_kind == NV_RESOURCE_MOUNT_SSH && remote[0] == '-')
	{
		return set_error(error, "invalid-remote",
			"remote resource must not begin with an option", EINVAL);
	}
	const char *explicit_helper = NULL;
	const char *const *candidates = NULL;
	if(mounter_kind == NV_RESOURCE_MOUNTER_FUSE_ZIP)
	{
		explicit_helper = options == NULL ? NULL : options->fuse_zip_path;
		candidates = fuse_zip_candidates;
	}
	else if(mounter_kind == NV_RESOURCE_MOUNTER_ARCHIVEMOUNT)
	{
		explicit_helper = options == NULL ? NULL : options->archivemount_path;
		candidates = archivemount_candidates;
	}
	else
	{
		explicit_helper = options == NULL ? NULL : options->sshfs_path;
		candidates = sshfs_candidates;
	}
	if(select_helper(explicit_helper, candidates, mounter_kind,
			&spec->helper_path, error) != 0)
	{
		return -1;
	}
	const char *const explicit_unmount = options == NULL ? NULL : options->unmount_path;
	if(select_unmount(explicit_unmount, &spec->unmount_path, error) != 0)
	{
		nv_resource_mount_spec_free(spec);
		return -1;
	}
	if(append_argument(spec, spec->helper_path) != 0)
		goto allocation_failed;
	if(resource_kind == NV_RESOURCE_MOUNT_SSH)
	{
		if(append_argument(spec, "-o") != 0 ||
				append_argument(spec, "ro") != 0 ||
				append_argument(spec, remote) != 0)
			goto allocation_failed;
	}
	else if(mounter_kind == NV_RESOURCE_MOUNTER_FUSE_ZIP)
	{
		if(append_argument(spec, "-r") != 0 ||
				append_argument(spec, source) != 0)
			goto allocation_failed;
	}
	else if(append_argument(spec, "-o") != 0 ||
			append_argument(spec, "readonly") != 0 ||
			append_argument(spec, source) != 0)
		goto allocation_failed;
	if(append_argument(spec, mount_point) != 0)
		goto allocation_failed;
	spec->resource_kind = resource_kind;
	spec->mounter_kind = mounter_kind;
	spec->read_only = 1;
	return 0;

allocation_failed:
	nv_resource_mount_spec_free(spec);
	return set_error(error, "out-of-memory",
			"failed to build resource mounter argv", ENOMEM);
}

int
nv_resource_mount_prepare_archive(const char source_path[],
		const char mount_point[], const nv_resource_mount_options_t *options,
		nv_resource_mount_spec_t *spec, nv_resource_mount_error_t *error)
{
	if(options != NULL && options->fuse_zip_path != NULL &&
			options->fuse_zip_path[0] != '\0')
	{
		return prepare_common(NV_RESOURCE_MOUNT_ARCHIVE,
				NV_RESOURCE_MOUNTER_FUSE_ZIP, source_path, mount_point, NULL,
				options, spec, error);
	}
	if(options != NULL && options->archivemount_path != NULL &&
			options->archivemount_path[0] != '\0')
	{
		return prepare_common(NV_RESOURCE_MOUNT_ARCHIVE,
				NV_RESOURCE_MOUNTER_ARCHIVEMOUNT, source_path, mount_point, NULL,
				options, spec, error);
	}
	if(spec == NULL || error == NULL) return -1;
	return prepare_common(NV_RESOURCE_MOUNT_ARCHIVE,
			NV_RESOURCE_MOUNTER_FUSE_ZIP, source_path, mount_point, NULL,
			options, spec, error) == 0 ? 0 :
		prepare_common(NV_RESOURCE_MOUNT_ARCHIVE,
				NV_RESOURCE_MOUNTER_ARCHIVEMOUNT, source_path, mount_point, NULL,
				options, spec, error);
}

int
nv_resource_mount_prepare_ssh(const char remote[], const char mount_point[],
		const nv_resource_mount_options_t *options,
		nv_resource_mount_spec_t *spec, nv_resource_mount_error_t *error)
{
	return prepare_common(NV_RESOURCE_MOUNT_SSH, NV_RESOURCE_MOUNTER_SSHFS,
			NULL, mount_point, remote, options, spec, error);
}
