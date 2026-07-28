#include <stic.h>

#include <stdlib.h>
#include <string.h>

#include <test-utils.h>

#include "../../src/neovifm/workspace_session.h"

TEST(session_refreshes_an_inactive_tab_by_stable_id)
{
	const char *const left = SANDBOX_PATH "/refresh-tab-left";
	const char *const right = SANDBOX_PATH "/refresh-tab-right";
	create_dir(left);
	create_dir(right);
	make_file(SANDBOX_PATH "/refresh-tab-left/first", "first");
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_workspace_session_init(left, right, &session, &error));
	const uint64_t first_id = nv_workspace_session_tab_id(&session,
			NV_SESSION_LEFT, 0U);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_NEW_TAB, .pane = NV_SESSION_LEFT,
	}, &error));
	const uint64_t second_id = nv_workspace_session_tab_id(&session,
			NV_SESSION_LEFT, 1U);
	make_file(SANDBOX_PATH "/refresh-tab-left/second", "second");

	assert_success(nv_workspace_session_refresh_tab(&session, NV_SESSION_LEFT,
			first_id, &error));
	assert_int_equal(1, nv_workspace_session_active_tab_index(&session,
			NV_SESSION_LEFT));
	assert_int_equal(second_id, nv_workspace_session_tab_id(&session,
			NV_SESSION_LEFT, nv_workspace_session_active_tab_index(&session,
				NV_SESSION_LEFT)));
	assert_int_equal(1, session.left.entry_count);
	assert_int_equal(2, nv_workspace_session_tab_snapshot(&session,
			NV_SESSION_LEFT, 0U)->entry_count);

	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/refresh-tab-left/first");
	remove_file(SANDBOX_PATH "/refresh-tab-left/second");
	remove_dir(left);
	remove_dir(right);
}

TEST(session_keeps_resource_ownership_with_a_tab_and_restores_origin)
{
	const char *const left = SANDBOX_PATH "/resource-session-left";
	const char *const right = SANDBOX_PATH "/resource-session-right";
	const char *const mount = SANDBOX_PATH "/resource-session-mount";
	create_dir(left);
	create_dir(right);
	create_dir(mount);
	make_file(SANDBOX_PATH "/resource-session-left/bundle.zip", "zip");
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_workspace_session_init(left, right, &session, &error));
	const nv_pane_snapshot_t *const origin = &session.left;
	assert_true(origin->cursor >= 0);
	const char *const origin_cwd = origin->cwd_bytes_hex;
	char *const origin_entry = strdup(origin->entries[origin->cursor].path_bytes_hex);
	assert_non_null(origin_entry);
	const uint64_t tab_id = nv_workspace_session_tab_id(&session, NV_SESSION_LEFT, 0U);
	assert_success(nv_workspace_session_attach_resource(&session, NV_SESSION_LEFT,
			tab_id, NV_SESSION_RESOURCE_ARCHIVE, left, origin_cwd, origin_entry,
			NULL, mount, SANDBOX_PATH "/resource-session-umount", &error));
	assert_true(nv_workspace_session_tab_resource(&session, NV_SESSION_LEFT,
			0U)->active);
	assert_string_equal(mount, session.left.cwd_display);
	assert_success(nv_workspace_session_detach_resource(&session, NV_SESSION_LEFT,
		tab_id, &error));
	assert_false(nv_workspace_session_tab_resource(&session, NV_SESSION_LEFT,
			0U)->active);
	assert_string_equal(left, session.left.cwd_display);
	assert_string_equal(origin_entry, session.left.entries[session.left.cursor].path_bytes_hex);

	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	free(origin_entry);
	remove_file(SANDBOX_PATH "/resource-session-left/bundle.zip");
	remove_dir(mount);
	remove_dir(left);
	remove_dir(right);
}
