#include <stic.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <test-utils.h>

#include "../../src/neovifm/workspace_session.h"
#include "../../src/neovifm/action_task.h"
#include "../../src/neovifm/classic_pane_adapter.h"
#include "../../src/filelist.h"
#include "../../src/ui/ui.h"
#include "../../src/utils/matchers.h"

#ifdef __APPLE__

static nv_pane_snapshot_t *
test_pane(nv_workspace_session_t *session, nv_session_pane_t pane)
{
	return pane == NV_SESSION_LEFT ? &session->left : &session->right;
}

static int
run_action(nv_workspace_session_t *session,
		const nv_session_command_t *command, nv_snapshot_error_t *error)
{
	nv_session_prepared_action_t action = {};
	if(nv_workspace_session_prepare_action(session, command, &action, error) != 0)
		return -1;
	nv_action_queue_t *const queue = nv_action_queue_alloc();
	assert_non_null(queue);
	if(nv_action_queue_submit(queue, &action, 1U, NULL) != 0)
	{
		nv_session_prepared_action_free(&action);
		nv_action_queue_free(queue);
		return -1;
	}
	nv_action_event_t event = {};
	for(int attempt = 0; attempt < 400; ++attempt)
	{
		if(nv_action_queue_pop(queue, &event) == 1)
		{
			if(event.state == NV_ACTION_TASK_DONE ||
					event.state == NV_ACTION_TASK_FAILED ||
					event.state == NV_ACTION_TASK_CANCELLED) break;
			nv_action_event_free(&event);
		}
		else usleep(5000);
	}
	nv_action_queue_free(queue);
	nv_snapshot_error_t refresh_error = {};
	assert_success(nv_workspace_session_refresh_pane(session, NV_SESSION_LEFT,
			&refresh_error));
	assert_success(nv_workspace_session_refresh_pane(session, NV_SESSION_RIGHT,
			&refresh_error));
	nv_snapshot_error_free(&refresh_error);
	if(event.state == NV_ACTION_TASK_DONE)
	{
		nv_action_event_free(&event);
		return 0;
	}
	nv_snapshot_error_free(error);
	error->code = strdup(event.error_code == NULL ? "action-failed" :
			event.error_code);
	error->message = strdup("file action failed");
	error->os_error = event.os_error;
	nv_action_event_free(&event);
	return -1;
}

static int
apply_action(nv_workspace_session_t *session, nv_session_pane_t pane,
		nv_session_command_kind_t kind, nv_snapshot_error_t *error)
{
	nv_pane_snapshot_t *const source = test_pane(session, pane);
	nv_session_action_target_t targets[NV_SESSION_MAX_ACTION_PATHS];
	size_t count = 0U;
	if(source->selection_count != 0U)
	{
		for(size_t i = 0U; i < source->entry_count; ++i)
		{
			if(source->entries[i].selected)
			{
				assert_true(count < NV_SESSION_MAX_ACTION_PATHS);
				targets[count++] = (nv_session_action_target_t){
					.path_bytes_hex = source->entries[i].path_bytes_hex,
					.device = source->entries[i].device,
					.inode = source->entries[i].inode,
					.ctime_unix_ns = source->entries[i].ctime_unix_ns,
					.kind = source->entries[i].kind,
				};
			}
		}
	}
	else if(source->cursor >= 0)
	{
		const nv_pane_entry_t *const entry = &source->entries[source->cursor];
		targets[count++] = (nv_session_action_target_t){
			.path_bytes_hex = entry->path_bytes_hex,
			.device = entry->device,
			.inode = entry->inode,
			.ctime_unix_ns = entry->ctime_unix_ns,
			.kind = entry->kind,
		};
	}
	nv_pane_snapshot_t *const destination = test_pane(session,
			pane == NV_SESSION_LEFT ? NV_SESSION_RIGHT : NV_SESSION_LEFT);
	const nv_session_command_t command = {
		.kind = kind,
		.pane = pane,
		.action_cwd_bytes_hex = source->cwd_bytes_hex,
		.action_snapshot_revision = source->snapshot_revision,
		.action_cwd_device = source->cwd_device,
		.action_cwd_inode = source->cwd_inode,
		.action_cwd_ctime_unix_ns = source->cwd_ctime_unix_ns,
		.action_destination_cwd_bytes_hex = destination->cwd_bytes_hex,
		.action_destination_snapshot_revision = destination->snapshot_revision,
		.action_destination_cwd_device = destination->cwd_device,
		.action_destination_cwd_inode = destination->cwd_inode,
		.action_destination_cwd_ctime_unix_ns =
			destination->cwd_ctime_unix_ns,
		.action_targets = targets,
		.action_target_count = count,
	};
	return run_action(session, &command, error);
}

static int
apply_mkdir(nv_workspace_session_t *session, nv_session_pane_t pane,
		const char name[], nv_snapshot_error_t *error)
{
	nv_session_command_t command = {
		.kind = NV_SESSION_MKDIR,
		.pane = pane,
		.action_cwd_bytes_hex = test_pane(session, pane)->cwd_bytes_hex,
		.action_snapshot_revision = test_pane(session, pane)->snapshot_revision,
		.action_cwd_device = test_pane(session, pane)->cwd_device,
		.action_cwd_inode = test_pane(session, pane)->cwd_inode,
	};
	strcpy(command.name, name);
	command.action_cwd_ctime_unix_ns =
		test_pane(session, pane)->cwd_ctime_unix_ns;
	return run_action(session, &command, error);
}

static const char *
install_test_trash(void)
{
	const char *const helper = SANDBOX_PATH "/neovifm-test-trash";
	make_file(helper, "#!/bin/sh\nexec /bin/rm -rf -- \"$1\"\n");
	assert_success(chmod(helper, 0700));
	assert_success(setenv("NEOVIFM_TRASH_EXECUTABLE", helper, 1));
	return helper;
}

static void
remove_test_trash(const char helper[])
{
	assert_success(unsetenv("NEOVIFM_TRASH_EXECUTABLE"));
	remove_file(helper);
}

#endif /* __APPLE__ */

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

TEST(session_select_entry_focuses_pane_and_toggles_exact_row_atomically)
{
	const char *const left = SANDBOX_PATH "/select-entry-left";
	const char *const right = SANDBOX_PATH "/select-entry-right";
	create_dir(left);
	create_dir(right);
	make_file(SANDBOX_PATH "/select-entry-left/a", "a");
	make_file(SANDBOX_PATH "/select-entry-right/a", "a");
	make_file(SANDBOX_PATH "/select-entry-right/b", "b");
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_workspace_session_init(left, right, &session, &error));

	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_SELECT_ENTRY, .pane = NV_SESSION_RIGHT,
		.entry_index = 1U,
	}, &error));
	assert_int_equal(NV_SESSION_RIGHT, session.active_pane);
	assert_int_equal(1, session.right.cursor);
	assert_int_equal(0, session.right.selection_count);

	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_SELECT_ENTRY, .pane = NV_SESSION_RIGHT,
		.entry_index = 0U, .toggle_selection = 1,
	}, &error));
	assert_int_equal(0, session.right.cursor);
	assert_true(session.right.entries[0].selected);
	assert_int_equal(1, session.right.selection_count);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_SELECT_ENTRY, .pane = NV_SESSION_RIGHT,
		.entry_index = 0U, .toggle_selection = 1,
	}, &error));
	assert_false(session.right.entries[0].selected);
	assert_int_equal(0, session.right.selection_count);

	assert_failure(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_SELECT_ENTRY, .pane = NV_SESSION_LEFT,
		.entry_index = 9U,
	}, &error));
	assert_string_equal("invalid-entry", error.code);
	assert_int_equal(NV_SESSION_RIGHT, session.active_pane);

	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/select-entry-left/a");
	remove_file(SANDBOX_PATH "/select-entry-right/a");
	remove_file(SANDBOX_PATH "/select-entry-right/b");
	remove_dir(left);
	remove_dir(right);
}

TEST(session_tabs_clone_activate_cycle_and_preserve_independent_state)
{
	const char *const left = SANDBOX_PATH "/tabs-left";
	const char *const right = SANDBOX_PATH "/tabs-right";
	create_dir(left);
	create_dir(right);
	create_dir(SANDBOX_PATH "/tabs-left/directory");
	make_file(SANDBOX_PATH "/tabs-left/z-file", "z");
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_workspace_session_init(left, right, &session, &error));
	const uint64_t first_id = nv_workspace_session_tab_id(&session,
			NV_SESSION_LEFT, 0U);
	assert_true(first_id != 0U);
	assert_int_equal(1, nv_workspace_session_tab_count(&session,
			NV_SESSION_LEFT));

	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_TOGGLE_SELECTION,
	}, &error));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_NEW_TAB, .pane = NV_SESSION_LEFT,
	}, &error));
	assert_int_equal(2, nv_workspace_session_tab_count(&session,
			NV_SESSION_LEFT));
	assert_int_equal(1, nv_workspace_session_active_tab_index(&session,
			NV_SESSION_LEFT));
	const uint64_t second_id = nv_workspace_session_tab_id(&session,
			NV_SESSION_LEFT, 1U);
	assert_true(second_id != 0U);
	assert_true(second_id != first_id);
	assert_true(session.left.entries[session.left.cursor].selected);
	assert_int_equal(1, session.left.selection_count);

	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_ENTER,
	}, &error));
	assert_string_equal(SANDBOX_PATH "/tabs-left/directory",
			session.left.cwd_display);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_ACTIVATE_TAB, .pane = NV_SESSION_LEFT,
		.tab_id = first_id,
	}, &error));
	assert_string_equal(left, session.left.cwd_display);
	assert_int_equal(1, session.left.selection_count);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_TAB_CYCLE, .delta = -1,
	}, &error));
	assert_string_equal(SANDBOX_PATH "/tabs-left/directory",
			session.left.cwd_display);
	assert_int_equal(1, nv_workspace_session_active_tab_index(&session,
			NV_SESSION_LEFT));
	assert_int_equal(1, nv_workspace_session_tab_count(&session,
			NV_SESSION_RIGHT));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_TAB_CYCLE, .delta = -999,
	}, &error));
	assert_int_equal(first_id, nv_workspace_session_tab_id(&session,
			NV_SESSION_LEFT, nv_workspace_session_active_tab_index(&session,
				NV_SESSION_LEFT)));

	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/tabs-left/z-file");
	remove_dir(SANDBOX_PATH "/tabs-left/directory");
	remove_dir(left);
	remove_dir(right);
}

TEST(session_tabs_close_prefers_right_then_left_and_rejects_last_tab)
{
	const char *const left = SANDBOX_PATH "/close-tabs-left";
	const char *const right = SANDBOX_PATH "/close-tabs-right";
	create_dir(left);
	create_dir(right);
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
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_NEW_TAB, .pane = NV_SESSION_LEFT,
	}, &error));
	const uint64_t third_id = nv_workspace_session_tab_id(&session,
			NV_SESSION_LEFT, 2U);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_TAB_CYCLE, .delta = -2,
	}, &error));
	assert_int_equal(first_id, nv_workspace_session_tab_id(&session,
			NV_SESSION_LEFT, nv_workspace_session_active_tab_index(&session,
				NV_SESSION_LEFT)));

	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_ACTIVATE_TAB, .pane = NV_SESSION_LEFT,
		.tab_id = second_id,
	}, &error));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_CLOSE_TAB, .pane = NV_SESSION_LEFT,
		.tab_id = first_id,
	}, &error));
	assert_int_equal(second_id, nv_workspace_session_tab_id(&session,
			NV_SESSION_LEFT, nv_workspace_session_active_tab_index(&session,
				NV_SESSION_LEFT)));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_CLOSE_TAB, .pane = NV_SESSION_LEFT,
		.tab_id = second_id,
	}, &error));
	assert_int_equal(third_id, nv_workspace_session_tab_id(&session,
			NV_SESSION_LEFT, 0U));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_NEW_TAB, .pane = NV_SESSION_LEFT,
	}, &error));
	const uint64_t fourth_id = nv_workspace_session_tab_id(&session,
			NV_SESSION_LEFT, 1U);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_CLOSE_TAB, .pane = NV_SESSION_LEFT,
		.tab_id = fourth_id,
	}, &error));
	assert_int_equal(third_id, nv_workspace_session_tab_id(&session,
			NV_SESSION_LEFT, 0U));
	assert_failure(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_CLOSE_TAB, .pane = NV_SESSION_LEFT,
		.tab_id = third_id,
	}, &error));
	assert_string_equal("last-tab", error.code);

	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	remove_dir(left);
	remove_dir(right);
}

TEST(session_tabs_enforce_per_pane_limit)
{
	const char *const left = SANDBOX_PATH "/limit-tabs-left";
	const char *const right = SANDBOX_PATH "/limit-tabs-right";
	create_dir(left);
	create_dir(right);
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_workspace_session_init(left, right, &session, &error));
	for(size_t i = 1U; i < NV_SESSION_MAX_TABS; ++i)
	{
		assert_success(nv_workspace_session_apply(&session,
				&(nv_session_command_t){
					.kind = NV_SESSION_NEW_TAB, .pane = NV_SESSION_LEFT,
				}, &error));
	}
	assert_int_equal(NV_SESSION_MAX_TABS, nv_workspace_session_tab_count(&session,
			NV_SESSION_LEFT));
	assert_failure(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_NEW_TAB, .pane = NV_SESSION_LEFT,
	}, &error));
	assert_string_equal("tab-limit", error.code);

	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
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

TEST(session_sorts_by_core_owned_column_and_preserves_cursor_identity)
{
	const char *const left = SANDBOX_PATH "/sort-left";
	const char *const right = SANDBOX_PATH "/sort-right";
	create_dir(left);
	create_dir(right);
	make_file(SANDBOX_PATH "/sort-left/a-large", "large");
	make_file(SANDBOX_PATH "/sort-left/b-small", "s");
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_workspace_session_init(left, right, &session, &error));
	assert_string_equal("a-large", session.left.entries[session.left.cursor].name_display);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_SORT_BY, .pane = NV_SESSION_LEFT,
		.sort_key = NV_SORT_SIZE,
	}, &error));
	assert_int_equal(NV_SORT_SIZE, session.left.sort_key);
	assert_false(session.left.sort_descending);
	assert_string_equal("b-small", session.left.entries[0].name_display);
	assert_string_equal("a-large", session.left.entries[session.left.cursor].name_display);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_SORT_BY, .pane = NV_SESSION_LEFT,
		.sort_key = NV_SORT_SIZE,
	}, &error));
	assert_true(session.left.sort_descending);
	assert_string_equal("a-large", session.left.entries[0].name_display);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_SORT_CYCLE, .delta = 1,
	}, &error));
	assert_int_equal(NV_SORT_CTIME, session.left.sort_key);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_SORT_CYCLE, .delta = 1,
	}, &error));
	assert_int_equal(NV_SORT_MTIME, session.left.sort_key);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_SORT_CYCLE, .pane = NV_SESSION_RIGHT,
		.has_pane = 1, .delta = 1,
	}, &error));
	assert_int_equal(NV_SESSION_RIGHT, session.active_pane);
	assert_int_equal(NV_SORT_SIZE, session.right.sort_key);
	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/sort-left/a-large");
	remove_file(SANDBOX_PATH "/sort-left/b-small");
	remove_dir(left);
	remove_dir(right);
}

#ifdef __APPLE__

TEST(session_function_actions_copy_move_mkdir_and_delete_real_files)
{
	const char *const trash_helper = install_test_trash();
	const char *const left = SANDBOX_PATH "/actions-left";
	const char *const right = SANDBOX_PATH "/actions-right";
	create_dir(left);
	create_dir(right);
	make_file(SANDBOX_PATH "/actions-left/a", "a");
	make_file(SANDBOX_PATH "/actions-left/b", "b");
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_workspace_session_init(left, right, &session, &error));

	assert_success(apply_action(&session, NV_SESSION_LEFT, NV_SESSION_COPY,
			&error));
	assert_success(access(SANDBOX_PATH "/actions-right/a", F_OK));
	assert_int_equal(1, session.right.entry_count);
	assert_failure(apply_action(&session, NV_SESSION_LEFT, NV_SESSION_COPY,
			&error));
	assert_string_equal("destination-exists", error.code);

	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_MOVE_CURSOR, .delta = 1,
	}, &error));
	assert_success(apply_action(&session, NV_SESSION_LEFT,
			NV_SESSION_MOVE_FILES, &error));
	assert_failure(access(SANDBOX_PATH "/actions-left/b", F_OK));
	assert_success(access(SANDBOX_PATH "/actions-right/b", F_OK));

	assert_success(apply_mkdir(&session, NV_SESSION_LEFT, "new-dir", &error));
	assert_success(access(SANDBOX_PATH "/actions-left/new-dir", F_OK));

	assert_success(apply_action(&session, NV_SESSION_LEFT, NV_SESSION_DELETE,
			&error));
	assert_failure(access(SANDBOX_PATH "/actions-left/a", F_OK));

	assert_failure(apply_mkdir(&session, NV_SESSION_LEFT, "../escape", &error));
	assert_string_equal("invalid-name", error.code);

	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	remove_dir(SANDBOX_PATH "/actions-left/new-dir");
	remove_file(SANDBOX_PATH "/actions-right/a");
	remove_file(SANDBOX_PATH "/actions-right/b");
	remove_dir(left);
	remove_dir(right);
	remove_test_trash(trash_helper);
}

TEST(session_actions_copy_directories_and_symlinks_without_following_them)
{
	const char *const trash_helper = install_test_trash();
	const char *const left = SANDBOX_PATH "/tree-actions-left";
	const char *const right = SANDBOX_PATH "/tree-actions-right";
	create_dir(left);
	create_dir(right);
	create_dir(SANDBOX_PATH "/tree-actions-left/tree");
	create_dir(SANDBOX_PATH "/tree-actions-left/tree/nested");
	make_file(SANDBOX_PATH "/tree-actions-left/tree/nested/file", "content");
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_workspace_session_init(left, right, &session, &error));
	assert_success(apply_action(&session, NV_SESSION_LEFT, NV_SESSION_COPY,
			&error));
	assert_success(access(SANDBOX_PATH "/tree-actions-right/tree/nested/file",
			F_OK));
	assert_success(apply_action(&session, NV_SESSION_LEFT, NV_SESSION_DELETE,
			&error));
	assert_failure(access(SANDBOX_PATH "/tree-actions-left/tree", F_OK));

	assert_success(symlink("missing-target",
			SANDBOX_PATH "/tree-actions-left/link"));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_REFRESH,
	}, &error));
	assert_success(apply_action(&session, NV_SESSION_LEFT, NV_SESSION_COPY,
			&error));
	char target[64];
	const ssize_t target_length = readlink(
			SANDBOX_PATH "/tree-actions-right/link", target, sizeof(target) - 1U);
	assert_int_equal((int)strlen("missing-target"), target_length);
	if(target_length >= 0)
	{
		target[target_length] = '\0';
		assert_string_equal("missing-target", target);
	}

	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/tree-actions-left/link");
	remove_file(SANDBOX_PATH "/tree-actions-right/link");
	remove_file(SANDBOX_PATH "/tree-actions-right/tree/nested/file");
	remove_dir(SANDBOX_PATH "/tree-actions-right/tree/nested");
	remove_dir(SANDBOX_PATH "/tree-actions-right/tree");
	remove_dir(left);
	remove_dir(right);
	remove_test_trash(trash_helper);
}

TEST(session_actions_complete_multiple_targets_without_self_staling)
{
	const char *const trash_helper = install_test_trash();
	const char *const left = SANDBOX_PATH "/multi-actions-left";
	const char *const right = SANDBOX_PATH "/multi-actions-right";
	create_dir(left);
	create_dir(right);
	make_file(SANDBOX_PATH "/multi-actions-left/a", "a");
	make_file(SANDBOX_PATH "/multi-actions-left/b", "b");
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_workspace_session_init(left, right, &session, &error));
	session.left.entries[0].selected = 1;
	session.left.entries[1].selected = 1;
	session.left.selection_count = 2U;
	assert_success(apply_action(&session, NV_SESSION_LEFT, NV_SESSION_COPY,
			&error));
	assert_success(access(SANDBOX_PATH "/multi-actions-right/a", F_OK));
	assert_success(access(SANDBOX_PATH "/multi-actions-right/b", F_OK));
	assert_success(apply_action(&session, NV_SESSION_LEFT, NV_SESSION_DELETE,
			&error));
	assert_failure(access(SANDBOX_PATH "/multi-actions-left/a", F_OK));
	assert_failure(access(SANDBOX_PATH "/multi-actions-left/b", F_OK));

	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/multi-actions-right/a");
	remove_file(SANDBOX_PATH "/multi-actions-right/b");
	remove_dir(left);
	remove_dir(right);
	remove_test_trash(trash_helper);
}

TEST(session_file_action_uses_captured_identity_after_cursor_moves)
{
	const char *const left = SANDBOX_PATH "/captured-action-left";
	const char *const right = SANDBOX_PATH "/captured-action-right";
	create_dir(left);
	create_dir(right);
	make_file(SANDBOX_PATH "/captured-action-left/a", "a");
	make_file(SANDBOX_PATH "/captured-action-left/b", "b");
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_workspace_session_init(left, right, &session, &error));
	nv_session_action_target_t targets[] = { {
		.path_bytes_hex = session.left.entries[0].path_bytes_hex,
		.device = session.left.entries[0].device,
		.inode = session.left.entries[0].inode,
		.ctime_unix_ns = session.left.entries[0].ctime_unix_ns,
		.kind = session.left.entries[0].kind,
	} };
	const nv_session_command_t command = {
		.kind = NV_SESSION_DELETE,
		.pane = NV_SESSION_LEFT,
		.action_cwd_bytes_hex = session.left.cwd_bytes_hex,
		.action_snapshot_revision = session.left.snapshot_revision,
		.action_cwd_device = session.left.cwd_device,
		.action_cwd_inode = session.left.cwd_inode,
		.action_cwd_ctime_unix_ns = session.left.cwd_ctime_unix_ns,
		.action_targets = targets,
		.action_target_count = 1U,
	};
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_MOVE_CURSOR, .delta = 1,
	}, &error));
	const char *const trash_helper = install_test_trash();
	assert_success(run_action(&session, &command, &error));
	assert_failure(access(SANDBOX_PATH "/captured-action-left/a", F_OK));
	assert_success(access(SANDBOX_PATH "/captured-action-left/b", F_OK));

	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/captured-action-left/b");
	remove_dir(left);
	remove_dir(right);
	remove_test_trash(trash_helper);
}

TEST(session_rejects_copying_a_directory_into_its_own_subtree)
{
	const char *const left = SANDBOX_PATH "/subtree-action";
	const char *const right = SANDBOX_PATH "/subtree-action/tree/nested";
	create_dir(left);
	create_dir(SANDBOX_PATH "/subtree-action/tree");
	create_dir(right);
	make_file(SANDBOX_PATH "/subtree-action/tree/file", "content");
	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_workspace_session_init(left, right, &session, &error));
	assert_failure(apply_action(&session, NV_SESSION_LEFT, NV_SESSION_COPY,
			&error));
	assert_string_equal("copy-failed", error.code);
	assert_failure(access(SANDBOX_PATH "/subtree-action/tree/nested/tree",
			F_OK));
	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/subtree-action/tree/file");
	remove_dir(right);
	remove_dir(SANDBOX_PATH "/subtree-action/tree");
	remove_dir(left);
}

#endif /* __APPLE__ */

#ifndef _WIN32
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
	set_local_filter(&left, "left-filter");
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
	matchers_free(left.local_filter.matchers);
	free(left.dir_entry);
	free(right.dir_entry);
}
#endif
