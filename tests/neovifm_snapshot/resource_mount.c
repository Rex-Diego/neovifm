#include <stic.h>

#include <sys/stat.h>

#include <string.h>
#include <unistd.h>

#include <test-utils.h>

#include "../../src/neovifm/resource_mount.h"

static const char *const MOUNT = SANDBOX_PATH "/mount";

#ifndef _WIN32
static const char *const FUSE_ZIP = SANDBOX_PATH "/fuse-zip";
static const char *const ARCHIVEMOUNT = SANDBOX_PATH "/archivemount";
static const char *const SSHFS = SANDBOX_PATH "/sshfs";
static const char *const UNMOUNT = SANDBOX_PATH "/umount";
static const char *const ARCHIVE = SANDBOX_PATH "/bundle.zip";

static void
install_helper(const char path[])
{
	make_file(path, "#!/bin/sh\nexit 0\n");
	assert_success(chmod(path, 0700));
}

static void
remove_helpers(void)
{
	const char *const paths[] = { FUSE_ZIP, ARCHIVEMOUNT, SSHFS, UNMOUNT };
	for(size_t i = 0U; i < sizeof(paths)/sizeof(paths[0]); ++i)
	{
		if(access(paths[i], F_OK) == 0) remove_file(paths[i]);
	}
}

TEST(archive_mount_prefers_fuse_zip_and_builds_read_only_argv)
{
	install_helper(FUSE_ZIP);
	install_helper(UNMOUNT);
	const nv_resource_mount_options_t options = {
		.fuse_zip_path = FUSE_ZIP,
		.unmount_path = UNMOUNT,
	};
	nv_resource_mount_spec_t spec = {};
	nv_resource_mount_error_t error = {};
	assert_success(nv_resource_mount_prepare_archive(ARCHIVE, MOUNT, &options,
			&spec, &error));
	assert_string_equal(FUSE_ZIP, spec.helper_path);
	assert_string_equal(UNMOUNT, spec.unmount_path);
	assert_int_equal(NV_RESOURCE_MOUNTER_FUSE_ZIP, spec.mounter_kind);
	assert_true(spec.read_only);
	assert_int_equal(4, spec.argc);
	assert_string_equal(FUSE_ZIP, spec.argv[0]);
	assert_string_equal("-r", spec.argv[1]);
	assert_string_equal(ARCHIVE, spec.argv[2]);
	assert_string_equal(MOUNT, spec.argv[3]);
	assert_null(spec.argv[4]);
	nv_resource_mount_spec_free(&spec);
	nv_resource_mount_error_free(&error);
	remove_helpers();
}

TEST(archive_mount_falls_back_to_archivemount)
{
	install_helper(ARCHIVEMOUNT);
	install_helper(UNMOUNT);
	const nv_resource_mount_options_t options = {
		.archivemount_path = ARCHIVEMOUNT,
		.unmount_path = UNMOUNT,
	};
	nv_resource_mount_spec_t spec = {};
	nv_resource_mount_error_t error = {};
	assert_success(nv_resource_mount_prepare_archive(ARCHIVE, MOUNT, &options,
			&spec, &error));
	assert_string_equal("archivemount", nv_resource_mounter_name(spec.mounter_kind));
	assert_int_equal(5, spec.argc);
	assert_string_equal(ARCHIVEMOUNT, spec.argv[0]);
	assert_string_equal("-o", spec.argv[1]);
	assert_string_equal("readonly", spec.argv[2]);
	assert_string_equal(ARCHIVE, spec.argv[3]);
	assert_string_equal(MOUNT, spec.argv[4]);
	nv_resource_mount_spec_free(&spec);
	nv_resource_mount_error_free(&error);
	remove_helpers();
}

TEST(archive_mount_reports_missing_configured_helper)
{
	install_helper(UNMOUNT);
	const nv_resource_mount_options_t options = {
		.fuse_zip_path = SANDBOX_PATH "/missing-fuse-zip",
		.unmount_path = UNMOUNT,
	};
	nv_resource_mount_spec_t spec = {};
	nv_resource_mount_error_t error = {};
	assert_failure(nv_resource_mount_prepare_archive(ARCHIVE, MOUNT, &options,
			&spec, &error));
	assert_string_equal("resource-mounter-unavailable", error.code);
	assert_null(spec.helper_path);
	nv_resource_mount_spec_free(&spec);
	nv_resource_mount_error_free(&error);
	remove_helpers();
}

TEST(ssh_mount_uses_one_argument_for_remote_and_read_only_option)
{
	install_helper(SSHFS);
	install_helper(UNMOUNT);
	const nv_resource_mount_options_t options = {
		.sshfs_path = SSHFS,
		.unmount_path = UNMOUNT,
	};
	nv_resource_mount_spec_t spec = {};
	nv_resource_mount_error_t error = {};
	assert_success(nv_resource_mount_prepare_ssh("alice@example.test:/data",
			MOUNT, &options, &spec, &error));
	assert_int_equal(NV_RESOURCE_MOUNT_SSH, spec.resource_kind);
	assert_string_equal("sshfs", nv_resource_mounter_name(spec.mounter_kind));
	assert_int_equal(5, spec.argc);
	assert_string_equal(SSHFS, spec.argv[0]);
	assert_string_equal("-o", spec.argv[1]);
	assert_string_equal("ro", spec.argv[2]);
	assert_string_equal("alice@example.test:/data", spec.argv[3]);
	assert_string_equal(MOUNT, spec.argv[4]);
	nv_resource_mount_spec_free(&spec);
	nv_resource_mount_error_free(&error);
	remove_helpers();
}
#endif

TEST(ssh_mount_rejects_option_like_remote_before_helper_lookup)
{
	nv_resource_mount_spec_t spec = {};
	nv_resource_mount_error_t error = {};
	assert_failure(nv_resource_mount_prepare_ssh("-oIdentityFile=x", MOUNT,
			NULL, &spec, &error));
	assert_string_equal("invalid-remote", error.code);
	nv_resource_mount_spec_free(&spec);
	nv_resource_mount_error_free(&error);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
