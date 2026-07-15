#include <stic.h>

#include <string.h>
#include <stdlib.h>

#include <test-utils.h>

#include "../../src/neovifm/workspace_session.h"
#include "../../src/neovifm/classic_pane_adapter.h"
#include "../../src/filelist.h"
#include "../../src/ui/ui.h"

TEST(session_keeps_panes_independent_while_moving_and_selecting)
{
	const char *const left = SANDBOX_PATH "/session-left";
	const char *const right = SANDBOX_PATH "/session-right";
	create_dir(left);
	create_dir(right);
	make_file(SANDBOX_PATH "/session-left/a", "a");
	make_file(SANDBOX_PATH "/session-left/b", "b");
	make_file(SANDBOX_PATH "/session-right/c", "c");
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_workspace_session_init(left, right, &session, &error));
	assert_int_equal(NV_SESSION_LEFT, session.active_pane);
	assert_int_equal(0, session.left.cursor);
	assert_int_equal(0, session.right.cursor);

	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_MOVE_CURSOR, .delta = 1,
	}, &error));
	assert_int_equal(1, session.left.cursor);
	assert_int_equal(0, session.right.cursor);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_TOGGLE_SELECTION,
	}, &error));
	assert_true(session.left.entries[1].selected);
	assert_int_equal(1, session.left.selection_count);
	assert_false(session.right.entries[0].selected);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_FOCUS, .pane = NV_SESSION_RIGHT,
	}, &error));
	assert_int_equal(NV_SESSION_RIGHT, session.active_pane);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_TOGGLE_SELECTION,
	}, &error));
	assert_true(session.right.entries[0].selected);
	assert_int_equal(1, session.right.selection_count);
	assert_true(session.left.entries[1].selected);

	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/session-left/a");
	remove_file(SANDBOX_PATH "/session-left/b");
	remove_file(SANDBOX_PATH "/session-right/c");
	remove_dir(left);
	remove_dir(right);
}

TEST(session_refreshes_an_inactive_pane_without_changing_focus)
{
	const char *const left = SANDBOX_PATH "/refresh-left";
	const char *const right = SANDBOX_PATH "/refresh-right";
	create_dir(left);
	create_dir(right);
	make_file(SANDBOX_PATH "/refresh-left/first", "first");
	make_file(SANDBOX_PATH "/refresh-right/only", "only");
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_workspace_session_init(left, right, &session, &error));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_FOCUS, .pane = NV_SESSION_RIGHT,
	}, &error));
	make_file(SANDBOX_PATH "/refresh-left/second", "second");
	assert_success(nv_workspace_session_refresh_pane(&session, NV_SESSION_LEFT,
			&error));
	assert_int_equal(NV_SESSION_RIGHT, session.active_pane);
	assert_int_equal(2, session.left.entry_count);
	assert_int_equal(1, session.right.entry_count);

	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/refresh-left/first");
	remove_file(SANDBOX_PATH "/refresh-left/second");
	remove_file(SANDBOX_PATH "/refresh-right/only");
	remove_dir(left);
	remove_dir(right);
}

TEST(session_focus_next_toggles_from_core_owned_active_pane)
{
	const char *const left = SANDBOX_PATH "/focus-next-left";
	const char *const right = SANDBOX_PATH "/focus-next-right";
	create_dir(left);
	create_dir(right);
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_workspace_session_init(left, right, &session, &error));
	assert_int_equal(NV_SESSION_LEFT, session.active_pane);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_FOCUS_NEXT,
	}, &error));
	assert_int_equal(NV_SESSION_RIGHT, session.active_pane);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_FOCUS_NEXT,
	}, &error));
	assert_int_equal(NV_SESSION_LEFT, session.active_pane);
	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	remove_dir(left);
	remove_dir(right);
}

TEST(session_moves_to_first_and_last_entries)
{
	const char *const left = SANDBOX_PATH "/move-to-left";
	const char *const right = SANDBOX_PATH "/move-to-right";
	create_dir(left);
	create_dir(right);
	make_file(SANDBOX_PATH "/move-to-left/a", "a");
	make_file(SANDBOX_PATH "/move-to-left/b", "b");
	make_file(SANDBOX_PATH "/move-to-left/c", "c");
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_workspace_session_init(left, right, &session, &error));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_MOVE_LAST,
	}, &error));
	assert_int_equal(2, session.left.cursor);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_MOVE_FIRST,
	}, &error));
	assert_int_equal(0, session.left.cursor);
	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/move-to-left/a");
	remove_file(SANDBOX_PATH "/move-to-left/b");
	remove_file(SANDBOX_PATH "/move-to-left/c");
	remove_dir(left);
	remove_dir(right);
}

TEST(classic_adapter_deep_copies_loaded_view_without_retaining_entries)
{
	const char *const dir = SANDBOX_PATH "/classic-adapter";
	view_t view = {};
	strcpy(view.curr_dir, dir);
	view.list_rows = 2;
	view.list_pos = 1;
	view.dir_entry = calloc(2U, sizeof(*view.dir_entry));
	assert_non_null(view.dir_entry);
	view.dir_entry[0].name = strdup("a");
	view.dir_entry[1].name = strdup("b");
	view.dir_entry[0].origin = view.curr_dir;
	view.dir_entry[1].origin = view.curr_dir;
	view.dir_entry[0].type = FT_REG;
	view.dir_entry[1].type = FT_REG;
	view.dir_entry[1].selected = 1;
	nv_pane_snapshot_t snapshot = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_pane_snapshot_from_classic_view(&view, &snapshot, &error));
	assert_string_equal(dir, snapshot.cwd_display);
	assert_int_equal(2, snapshot.entry_count);
	assert_int_equal(1, snapshot.cursor);
	assert_true(snapshot.entries[1].selected);
	assert_false(snapshot.entries[0].name_display == view.dir_entry[0].name);
	view.dir_entry[1].selected = 0;
	assert_true(snapshot.entries[1].selected);
	nv_pane_snapshot_free(&snapshot);
	nv_snapshot_error_free(&error);
	free(view.dir_entry[0].name);
	free(view.dir_entry[1].name);
	free(view.dir_entry);
}

TEST(classic_workspace_adapter_copies_both_panes_atomically)
{
	view_t left = {}, right = {};
	strcpy(left.curr_dir, "/left");
	strcpy(right.curr_dir, "/right");
	left.list_rows = right.list_rows = 1;
	left.list_pos = right.list_pos = 0;
	left.dir_entry = calloc(1U, sizeof(*left.dir_entry));
	right.dir_entry = calloc(1U, sizeof(*right.dir_entry));
	assert_non_null(left.dir_entry);
	assert_non_null(right.dir_entry);
	left.dir_entry[0].name = strdup("left-file");
	right.dir_entry[0].name = strdup("right-file");
	left.dir_entry[0].origin = left.curr_dir;
	right.dir_entry[0].origin = right.curr_dir;
	left.dir_entry[0].type = right.dir_entry[0].type = FT_REG;
	left.filtered = 3;
	left.sort[0] = -SK_BY_SIZE;
	left.local_filter.filter.raw = strdup("left-filter");
	right.dir_entry[0].selected = 1;
	nv_classic_workspace_snapshot_t workspace = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_classic_workspace_snapshot_from_views(&left, &right,
			NV_CLASSIC_PANE_RIGHT, &workspace, &error));
	assert_int_equal(NV_CLASSIC_PANE_RIGHT, workspace.active_pane);
	assert_string_equal("left-file", workspace.left.entries[0].name_display);
	assert_string_equal("right-file", workspace.right.entries[0].name_display);
	assert_true(workspace.right.entries[0].selected);
	assert_int_equal(1, workspace.right.selection_count);
	assert_int_equal(3, workspace.left.filtered_count);
	assert_int_equal(NV_SORT_SIZE, workspace.left.sort_key);
	assert_true(workspace.left.sort_descending);
	assert_true(workspace.left.filter_active);
	nv_workspace_session_t session = {};
	assert_success(nv_workspace_session_init_from_classic_views(&left, &right,
			NV_CLASSIC_PANE_RIGHT, &session, &error));
	assert_int_equal(NV_SESSION_RIGHT, session.active_pane);
	assert_string_equal("left-file", session.left.entries[0].name_display);
	left.dir_entry[0].name[0] = 'X';
	assert_string_equal("left-file", workspace.left.entries[0].name_display);

	right.list_pos = 1;
	assert_failure(nv_classic_workspace_snapshot_from_views(&left, &right,
			NV_CLASSIC_PANE_LEFT, &workspace, &error));
	assert_string_equal("left-file", workspace.left.entries[0].name_display);
	assert_int_equal(NV_CLASSIC_PANE_RIGHT, workspace.active_pane);

	nv_classic_workspace_snapshot_free(&workspace);
	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	free(left.dir_entry[0].name);
	free(right.dir_entry[0].name);
	free(left.local_filter.filter.raw);
	free(left.dir_entry);
	free(right.dir_entry);
}
