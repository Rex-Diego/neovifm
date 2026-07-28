#include <stic.h>

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
