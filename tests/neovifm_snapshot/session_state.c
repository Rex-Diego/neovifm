#include <stic.h>

#include <stdlib.h>
#include <string.h>

#include <test-utils.h>

#include "../../src/neovifm/workspace_session.h"

static const nv_pane_snapshot_t *
tab(nv_workspace_session_t *session, nv_session_pane_t pane, size_t index)
{
	return nv_workspace_session_tab_snapshot(session, pane, index);
}

TEST(session_state_round_trip_restores_tabs_panes_cursors_and_sorting)
{
	const char *const left = SANDBOX_PATH "/state-left";
	const char *const right = SANDBOX_PATH "/state-right";
	const char *const child = SANDBOX_PATH "/state-left/child";
	const char *const state = SANDBOX_PATH "/session-state.json";
	const char *const invalid_state = SANDBOX_PATH "/invalid-session-state.json";
	create_dir(left);
	create_dir(right);
	create_dir(child);
	make_file(SANDBOX_PATH "/state-left/alpha", "alpha");
	make_file(SANDBOX_PATH "/state-left/zulu", "zulu");
	make_file(SANDBOX_PATH "/state-left/child/inside", "inside");
	make_file(SANDBOX_PATH "/state-right/right-file", "right");

	nv_workspace_session_t session = {};
	nv_snapshot_error_t error = {};
	assert_success(nv_workspace_session_init(left, right, &session, &error));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_SORT_BY,
		.pane = NV_SESSION_LEFT,
		.sort_key = NV_SORT_SIZE,
	}, &error));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_NEW_TAB,
		.pane = NV_SESSION_LEFT,
	}, &error));
	size_t child_index = session.left.entry_count;
	for(size_t i = 0U; i < session.left.entry_count; ++i)
	{
		if(strcmp(session.left.entries[i].name_display, "child") == 0)
		{
			child_index = i;
			break;
		}
	}
	assert_true(child_index < session.left.entry_count);
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_SELECT_ENTRY,
		.pane = NV_SESSION_LEFT,
		.entry_index = child_index,
	}, &error));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_ENTER,
	}, &error));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_FOCUS,
		.pane = NV_SESSION_RIGHT,
	}, &error));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_NEW_TAB,
		.pane = NV_SESSION_RIGHT,
	}, &error));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_FOCUS,
		.pane = NV_SESSION_LEFT,
	}, &error));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_ACTIVATE_TAB,
		.pane = NV_SESSION_LEFT,
		.tab_id = nv_workspace_session_tab_id(&session, NV_SESSION_LEFT, 1U),
	}, &error));
	assert_int_equal(1, nv_workspace_session_active_tab_index(&session,
		NV_SESSION_LEFT));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_FOCUS,
		.pane = NV_SESSION_RIGHT,
	}, &error));
	assert_success(nv_workspace_session_save_state(&session, state, &error));
	assert_success(nv_workspace_session_apply(&session, &(nv_session_command_t){
		.kind = NV_SESSION_FOCUS,
		.pane = NV_SESSION_LEFT,
	}, &error));
	assert_success(nv_workspace_session_save_state(&session, state, &error));

	nv_workspace_session_t restored = {};
	assert_success(nv_workspace_session_init(right, left, &restored, &error));
	assert_int_equal(0, nv_workspace_session_load_state(&restored, state, &error));
	assert_int_equal(NV_SESSION_LEFT, restored.active_pane);
	assert_int_equal(2, nv_workspace_session_tab_count(&restored,
		NV_SESSION_LEFT));
	assert_int_equal(2, nv_workspace_session_tab_count(&restored,
		NV_SESSION_RIGHT));
	assert_int_equal(1, nv_workspace_session_active_tab_index(&restored,
		NV_SESSION_LEFT));
	assert_int_equal(1, nv_workspace_session_active_tab_index(&restored,
		NV_SESSION_RIGHT));
	assert_string_equal(left, tab(&restored, NV_SESSION_LEFT, 0U)->cwd_display);
	assert_string_equal(child, tab(&restored, NV_SESSION_LEFT, 1U)->cwd_display);
	assert_int_equal(NV_SORT_SIZE, tab(&restored, NV_SESSION_LEFT, 0U)->sort_key);
	assert_string_equal(child, restored.left.cwd_display);
	assert_string_equal("inside",
		restored.left.entries[restored.left.cursor].name_display);
	make_file(invalid_state,
			"{\"version\":1,\"active_pane\":\"left\","
			"\"left_tabs\":[{\"cwd_bytes_hex\":\"2f746d70\","
			"\"sort_key\":\"name\",\"sort_descending\":false}],"
			"\"right_tabs\":[{\"cwd_bytes_hex\":\"2f746d70\","
			"\"sort_key\":\"name\",\"sort_descending\":false}],"
			"\"left_active\":1e300,\"right_active\":0}");
	assert_int_equal(1, nv_workspace_session_load_state(&restored, invalid_state,
			&error));
	assert_int_equal(1, nv_workspace_session_load_state(&restored,
			SANDBOX_PATH "/missing-session-state.json", &error));
	const size_t oversized_length = 256U*1024U + 1U;
	char *const oversized = malloc(oversized_length + 1U);
	assert_non_null(oversized);
	memset(oversized, 'x', oversized_length);
	oversized[oversized_length] = '\0';
	const char *const oversized_state = SANDBOX_PATH "/oversized-session-state.json";
	make_file(oversized_state, oversized);
	free(oversized);
	assert_int_equal(1, nv_workspace_session_load_state(&restored,
			oversized_state, &error));

	nv_workspace_session_free(&restored);
	nv_workspace_session_free(&session);
	nv_snapshot_error_free(&error);
	remove_file(state);
	remove_file(invalid_state);
	remove_file(oversized_state);
	remove_file(SANDBOX_PATH "/state-left/alpha");
	remove_file(SANDBOX_PATH "/state-left/zulu");
	remove_file(SANDBOX_PATH "/state-left/child/inside");
	remove_file(SANDBOX_PATH "/state-right/right-file");
	remove_dir(child);
	remove_dir(left);
	remove_dir(right);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
