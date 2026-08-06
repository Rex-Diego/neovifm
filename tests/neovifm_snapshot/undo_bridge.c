#include <stic.h>

#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#include <test-utils.h>

#include "../../src/compat/neovifm_fs.h"
#include "../../src/neovifm/undo_bridge.h"

#ifdef __APPLE__

static nv_fs_identity_t
identity_for(const char path[])
{
	struct stat st = {};
	int is_symlink = 0;
	assert_success(nv_lstat(path, &st, &is_symlink));
	assert_false(is_symlink);
	uint64_t ctime_unix_ns = (uint64_t)st.st_ctime*1000000000ULL;
#if defined(__APPLE__)
	ctime_unix_ns = (uint64_t)st.st_ctimespec.tv_sec*1000000000ULL +
		(uint64_t)st.st_ctimespec.tv_nsec;
#elif defined(__linux__)
	ctime_unix_ns = (uint64_t)st.st_ctim.tv_sec*1000000000ULL +
		(uint64_t)st.st_ctim.tv_nsec;
#endif
	return (nv_fs_identity_t){
		.device = (uint64_t)st.st_dev,
		.inode = (uint64_t)st.st_ino,
		.ctime_unix_ns = ctime_unix_ns,
	};
}

TEST(undo_bridge_removes_the_last_created_directory)
{
	const char *const parent = SANDBOX_PATH "/undo-parent";
	const char *const path = SANDBOX_PATH "/undo-parent/created";
	const nv_undo_bridge_location_t location = { .pane = 1U, .tab_id = 42U };
	create_dir(parent);
	assert_success(nv_undo_bridge_init());
	assert_int_equal(NV_UNDO_BRIDGE_NONE, nv_undo_bridge_undo(NULL));
	assert_success(nv_fs_mkdir(path, 0777, identity_for(parent)));
	assert_success(nv_undo_bridge_record_mkdir(path, identity_for(parent), location));
	nv_undo_bridge_location_t undone_location = {};
	assert_int_equal(NV_UNDO_BRIDGE_SUCCESS, nv_undo_bridge_undo(&undone_location));
	assert_int_equal(location.pane, undone_location.pane);
	assert_int_equal(location.tab_id, undone_location.tab_id);
	assert_failure(access(path, F_OK));
	nv_undo_bridge_reset();
	remove_dir(parent);
}

TEST(undo_bridge_refuses_to_remove_a_replaced_directory)
{
	const char *const parent = SANDBOX_PATH "/undo-replaced-parent";
	const char *const path = SANDBOX_PATH "/undo-replaced-parent/created";
	create_dir(parent);
	assert_success(nv_undo_bridge_init());
	assert_success(nv_fs_mkdir(path, 0777, identity_for(parent)));
	assert_success(nv_undo_bridge_record_mkdir(path, identity_for(parent),
			(nv_undo_bridge_location_t){ .pane = 0U, .tab_id = 7U }));
	remove_dir(path);
	create_dir(path);
	assert_int_equal(NV_UNDO_BRIDGE_FAILED, nv_undo_bridge_undo(NULL));
	assert_success(access(path, F_OK));
	nv_undo_bridge_reset();
	remove_dir(path);
	remove_dir(parent);
}

TEST(undo_bridge_removes_a_completed_copy_as_one_group)
{
	const char *const source_parent = SANDBOX_PATH "/undo-copy-source";
	const char *const destination_parent = SANDBOX_PATH "/undo-copy-destination";
	const char *const source = SANDBOX_PATH "/undo-copy-source/file";
	const char *const destination = SANDBOX_PATH "/undo-copy-destination/file";
	create_dir(source_parent);
	create_dir(destination_parent);
	make_file(source, "content");
	assert_success(nv_undo_bridge_init());
	const nv_fs_identity_t source_identity = identity_for(source);
	assert_success(nv_fs_copy(source, destination, identity_for(source_parent),
			identity_for(destination_parent), source_identity, NULL, NULL));
	const nv_undo_bridge_transfer_t transfer = {
		.source_path = source, .destination_path = destination,
		.source_identity = source_identity,
	};
	assert_success(nv_undo_bridge_record_copy_group(&transfer, 1U,
			identity_for(destination_parent), (nv_undo_bridge_location_t){
				.pane = 0U, .tab_id = 2U }));
	assert_int_equal(NV_UNDO_BRIDGE_SUCCESS, nv_undo_bridge_undo(NULL));
	assert_success(access(source, F_OK));
	assert_failure(access(destination, F_OK));
	nv_undo_bridge_reset();
	remove_file(source);
	remove_dir(source_parent);
	remove_dir(destination_parent);
}

TEST(undo_bridge_moves_a_completed_move_back_to_its_source)
{
	const char *const source_parent = SANDBOX_PATH "/undo-move-source";
	const char *const destination_parent = SANDBOX_PATH "/undo-move-destination";
	const char *const source = SANDBOX_PATH "/undo-move-source/file";
	const char *const destination = SANDBOX_PATH "/undo-move-destination/file";
	create_dir(source_parent);
	create_dir(destination_parent);
	make_file(source, "content");
	assert_success(nv_undo_bridge_init());
	const nv_fs_identity_t source_identity = identity_for(source);
	const nv_fs_identity_t source_parent_identity = identity_for(source_parent);
	const nv_fs_identity_t destination_parent_identity = identity_for(destination_parent);
	assert_success(nv_fs_move(source, destination, source_parent_identity,
			destination_parent_identity, source_identity, NULL, NULL));
	const nv_undo_bridge_transfer_t transfer = {
		.source_path = source, .destination_path = destination,
		.source_identity = source_identity,
	};
	assert_success(nv_undo_bridge_record_move_group(&transfer, 1U,
			source_parent_identity, destination_parent_identity,
			(nv_undo_bridge_location_t){ .pane = 1U, .tab_id = 3U }));
	assert_int_equal(NV_UNDO_BRIDGE_SUCCESS, nv_undo_bridge_undo(NULL));
	assert_success(access(source, F_OK));
	assert_failure(access(destination, F_OK));
	nv_undo_bridge_reset();
	remove_file(source);
	remove_dir(source_parent);
	remove_dir(destination_parent);
}

#endif /* __APPLE__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
