#include <stic.h>

#include <string.h>

#include <test-utils.h>

#include "../../src/neovifm/workspace_session.h"

static const char *
cursor_name(const nv_pane_snapshot_t *snapshot)
{
	return snapshot->cursor < 0 ? NULL : snapshot->entries[snapshot->cursor].name_display;
}

TEST(session_search_moves_forward_backward_and_wraps)
{
	const char *const left = SANDBOX_PATH "/search-left";
	const char *const right = SANDBOX_PATH "/search-right";
	create_dir(left);
	create_dir(right);
	make_file(SANDBOX_PATH "/search-left/alpha.txt", "a");
	make_file(SANDBOX_PATH "/search-left/beta.txt", "b");
	make_file(SANDBOX_PATH "/search-left/ALPHA-two.txt", "c");
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_workspace_session_init(left, right, &session, &error));
	const char query[] = "alpha";
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_SEARCH, .search_query = (char *)query,
		.search_direction = 1,
	}, &error));
	assert_string_equal("alpha.txt", cursor_name(&session.left));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_SEARCH_NEXT, .search_direction = 1,
	}, &error));
	assert_string_equal("ALPHA-two.txt", cursor_name(&session.left));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_SEARCH_NEXT, .search_direction = -1,
	}, &error));
	assert_string_equal("alpha.txt", cursor_name(&session.left));

	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/search-left/alpha.txt");
	remove_file(SANDBOX_PATH "/search-left/beta.txt");
	remove_file(SANDBOX_PATH "/search-left/ALPHA-two.txt");
	remove_dir(left);
	remove_dir(right);
}
