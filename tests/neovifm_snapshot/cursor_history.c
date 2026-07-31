#include <stic.h>

#include <string.h>

#include <test-utils.h>

#include "../../src/neovifm/workspace_session.h"

TEST(session_restores_cursor_when_returning_to_parent_directory)
{
	const char *const left = SANDBOX_PATH "/cursor-history-left";
	const char *const right = SANDBOX_PATH "/cursor-history-right";
	const char *const first = SANDBOX_PATH "/cursor-history-left/aaa-first";
	const char *const target = SANDBOX_PATH "/cursor-history-left/target-dir";
	create_dir(left);
	create_dir(right);
	create_dir(first);
	create_dir(target);
	make_file(SANDBOX_PATH "/cursor-history-left/zzz-file", "file");
	make_file(SANDBOX_PATH "/cursor-history-left/target-dir/inside", "inside");
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_workspace_session_init(left, right, &session, &error));
	size_t target_index = session.left.entry_count;
	for(size_t i = 0U; i < session.left.entry_count; ++i)
	{
		if(strcmp(session.left.entries[i].name_display, "target-dir") == 0)
		{
			target_index = i;
			break;
		}
	}
	assert_true(target_index < session.left.entry_count);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_SELECT_ENTRY, .pane = NV_SESSION_LEFT,
		.entry_index = target_index,
	}, &error));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_ENTER,
	}, &error));
	assert_string_equal(target, session.left.cwd_display);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_PARENT,
	}, &error));
	assert_string_equal(left, session.left.cwd_display);
	assert_true(session.left.cursor >= 0);
	assert_string_equal("target-dir", session.left.entries[session.left.cursor].name_display);

	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/cursor-history-left/target-dir/inside");
	remove_dir(target);
	remove_dir(first);
	remove_file(SANDBOX_PATH "/cursor-history-left/zzz-file");
	remove_dir(left);
	remove_dir(right);
}
