#include <stic.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <test-utils.h>

#include "../../src/neovifm/pane_snapshot.h"
#include "../../src/neovifm/preview_task.h"
#include "../../src/neovifm/snapshot_json.h"
#include "../../src/utils/parson.h"

TEST(hello_record_declares_protocol_version)
{
	char *line = nv_protocol_hello_json(0U);
	assert_non_null(line);

	JSON_Value *value = json_parse_string(line);
	assert_non_null(value);
	JSON_Object *object = json_object(value);
	assert_string_equal("neovifm-core", json_object_get_string(object, "protocol"));
	assert_int_equal(0, json_object_get_number(object, "version"));
	assert_string_equal("hello", json_object_get_string(object, "type"));

	json_value_free(value);
	nv_protocol_json_free(line);
}

TEST(preview_session_records_are_versioned_and_keep_task_identity)
{
	char *hello = nv_protocol_preview_session_hello_json(0U);
	assert_non_null(hello);
	JSON_Value *value = json_parse_string(hello);
	assert_non_null(value);
	JSON_Object *root = json_object(value);
	assert_int_equal(3, json_object_get_number(root, "version"));
	JSON_Array *const capabilities = json_object_get_array(
			json_object_get_object(root, "payload"), "capabilities");
	assert_string_equal("preview-session-v3", json_array_get_string(
			capabilities, 0U));
	assert_string_equal("workspace-sort-v1", json_array_get_string(capabilities,
			1U));
	#ifdef __APPLE__
	assert_string_equal("file-actions-v1", json_array_get_string(capabilities,
			2U));
	#else
	assert_int_equal(2, json_array_get_count(capabilities));
	#endif
	json_value_free(value);
	nv_protocol_json_free(hello);

	nv_preview_event_t event = {
		.task_id = 42U, .generation = 7U, .pane = NV_PREVIEW_PANE_RIGHT,
		.kind = NV_PREVIEW_KIND_TEXT, .state = NV_PREVIEW_TASK_DONE,
		.cwd_bytes_hex = "2f746d70", .path_bytes_hex = "2f746d702f6e6f7465",
		.content = "note", .truncated = 0,
	};
	char *line = nv_protocol_preview_task_json(&event, 2U);
	assert_non_null(line);
	value = json_parse_string(line);
	assert_non_null(value);
	root = json_object(value);
	assert_int_equal(3, json_object_get_number(root, "version"));
	assert_string_equal("task", json_object_get_string(root, "type"));
	JSON_Object *payload = json_object_get_object(root, "payload");
	assert_string_equal("42", json_object_get_string(payload, "task_id"));
	assert_string_equal("7", json_object_get_string(payload, "generation"));
	assert_string_equal("right", json_object_get_string(payload, "pane"));
	assert_string_equal("done", json_object_get_string(payload, "state"));
	json_value_free(value);
	nv_protocol_json_free(line);

	nv_action_event_t action = {
		.task_id = 9U, .command_sequence = 3U,
		.kind = NV_SESSION_COPY, .pane = NV_SESSION_LEFT,
		.state = NV_ACTION_TASK_FAILED, .completed_count = 1U,
		.total_count = 2U, .failed_index = 1U, .has_failed_index = 1,
		.partial = 1, .error_code = "destination-exists", .os_error = EEXIST,
	};
	line = nv_protocol_action_task_json(&action, 3U);
	assert_non_null(line);
	value = json_parse_string(line);
	assert_non_null(value);
	root = json_object(value);
	assert_string_equal("action-task", json_object_get_string(root, "type"));
	payload = json_object_get_object(root, "payload");
	assert_string_equal("copy", json_object_get_string(payload, "action"));
	assert_string_equal("failed", json_object_get_string(payload, "state"));
	assert_int_equal(1, json_object_get_number(payload, "completed_count"));
	assert_true(json_object_get_boolean(payload, "partial"));
	json_value_free(value);
	nv_protocol_json_free(line);
}

TEST(workspace_record_is_atomic_and_uses_v1)
{
	const char *const left_dir = SANDBOX_PATH "/workspace-left";
	const char *const right_dir = SANDBOX_PATH "/workspace-right";
	nv_pane_snapshot_t left = {};
	nv_pane_snapshot_t right = {};
	nv_snapshot_error_t error = {};
	create_dir(left_dir);
	create_dir(right_dir);
	make_file(SANDBOX_PATH "/workspace-left/left", "left");
	make_file(SANDBOX_PATH "/workspace-right/right", "right");

	assert_success(nv_pane_snapshot_build(left_dir, &left, &error));
	assert_success(nv_pane_snapshot_build(right_dir, &right, &error));
	char *line = NULL;
	assert_success(nv_protocol_workspace_snapshot_json(&left, &right, "right",
			1U, &line));
	JSON_Value *value = json_parse_string(line);
	assert_non_null(value);
	JSON_Object *const root = json_object(value);
	assert_int_equal(1, json_object_get_number(root, "version"));
	assert_string_equal("workspace-snapshot", json_object_get_string(root, "type"));
	JSON_Object *const payload = json_object_get_object(root, "payload");
	assert_string_equal("right", json_object_get_string(payload, "active_pane"));
	assert_string_equal(left_dir, json_object_get_string(
			json_object_get_object(payload, "left"), "cwd_display"));
	assert_string_equal(right_dir, json_object_get_string(
			json_object_get_object(payload, "right"), "cwd_display"));

	json_value_free(value);
	nv_protocol_json_free(line);
	nv_pane_snapshot_free(&left);
	nv_pane_snapshot_free(&right);
	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/workspace-left/left");
	remove_file(SANDBOX_PATH "/workspace-right/right");
	remove_dir(left_dir);
	remove_dir(right_dir);
}

TEST(workspace_record_rejects_combined_entry_limit_as_too_large)
{
	nv_pane_snapshot_t left = {
		.cwd_display = "/left",
		.cwd_bytes_hex = "2f6c656674",
		.entry_count = 2049U,
	};
	nv_pane_snapshot_t right = {
		.cwd_display = "/right",
		.cwd_bytes_hex = "2f7269676874",
		.entry_count = 2048U,
	};
	char *line = NULL;
	assert_int_equal(NV_PROTOCOL_JSON_TOO_LARGE,
			nv_protocol_workspace_snapshot_json(&left, &right, "left", 1U, &line));
	assert_null(line);
}

TEST(snapshot_record_rejects_invalid_cursor_before_serialization)
{
	nv_pane_snapshot_t snapshot = {
		.cwd_display = "/tmp",
		.cwd_bytes_hex = "2f746d70",
		.cursor = 0,
		.entry_count = 0U,
	};
	char *line = NULL;
	assert_int_equal(NV_PROTOCOL_JSON_ERROR,
			nv_protocol_snapshot_json(&snapshot, 1U, &line));
	assert_null(line);
}

TEST(snapshot_record_uses_strings_for_wide_numbers)
{
	const char *const dir = SANDBOX_PATH "/protocol";
	nv_pane_snapshot_t snapshot = {};
	nv_snapshot_error_t error = {};
	create_dir(dir);
	make_file(SANDBOX_PATH "/protocol/file", "abc");

	assert_success(nv_pane_snapshot_build(dir, &snapshot, &error));
	char *line = NULL;
	assert_success(nv_protocol_snapshot_json(&snapshot, 1U, &line));
	assert_non_null(line);

	JSON_Value *value = json_parse_string(line);
	assert_non_null(value);
	JSON_Object *object = json_object(value);
	assert_string_equal("snapshot", json_object_get_string(object, "type"));
	JSON_Object *payload = json_object_get_object(object, "payload");
	JSON_Array *entries = json_object_get_array(payload, "entries");
	JSON_Object *entry = json_array_get_object(entries, 0U);
	assert_string_equal("3", json_object_get_string(entry, "size_bytes"));
	assert_non_null(json_object_get_string(entry, "mtime_unix_ms"));
	assert_non_null(json_object_get_string(entry, "path_bytes_hex"));
	assert_int_equal(0, json_object_get_number(payload, "selection_count"));
	assert_int_equal(0, json_object_get_number(payload, "filtered_count"));
	assert_string_equal("name", json_object_get_string(payload, "sort_key"));
	assert_false(json_object_get_boolean(payload, "sort_descending"));
	assert_false(json_object_get_boolean(payload, "filter_active"));

	json_value_free(value);
	nv_protocol_json_free(line);
	nv_pane_snapshot_free(&snapshot);
	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/protocol/file");
	remove_dir(dir);
}

TEST(error_record_omits_absent_os_error)
{
	nv_snapshot_error_t error = {
		.code = "usage",
		.message = "expected one path",
	};
	char *line = nv_protocol_error_json(&error, 1U);
	assert_non_null(line);

	JSON_Value *value = json_parse_string(line);
	assert_non_null(value);
	JSON_Object *payload = json_object_get_object(json_object(value), "payload");
	assert_string_equal("usage", json_object_get_string(payload, "code"));
	assert_false(json_object_has_value(payload, "os_error"));

	json_value_free(value);
	nv_protocol_json_free(line);
}

TEST(snapshot_record_serializes_all_entry_kinds_and_stat_errors)
{
	static const nv_entry_kind_t kinds[] = {
		NV_ENTRY_DIRECTORY,
		NV_ENTRY_FILE,
		NV_ENTRY_SYMLINK,
		NV_ENTRY_EXECUTABLE,
		NV_ENTRY_FIFO,
		NV_ENTRY_SOCKET,
		NV_ENTRY_CHAR_DEVICE,
		NV_ENTRY_BLOCK_DEVICE,
		NV_ENTRY_UNKNOWN,
	};
	nv_pane_entry_t entries[sizeof(kinds)/sizeof(kinds[0])] = {};
	for(size_t i = 0U; i < sizeof(kinds)/sizeof(kinds[0]); ++i)
	{
		entries[i].name_display = "entry";
		entries[i].name_bytes_hex = "656e747279";
		entries[i].path_display = "/tmp/entry";
		entries[i].path_bytes_hex = "2f746d702f656e747279";
		entries[i].kind = kinds[i];
		entries[i].has_stat = 1;
	}
	entries[2].has_stat = 0;
	entries[2].stat_error = EACCES;
	entries[8].has_stat = 0;

	nv_pane_snapshot_t snapshot = {
		.cwd_display = "/tmp",
		.cwd_bytes_hex = "2f746d70",
		.cursor = 0,
		.entry_count = sizeof(entries)/sizeof(entries[0]),
		.entries = entries,
	};
	char *line = NULL;
	assert_success(nv_protocol_snapshot_json(&snapshot, 1U, &line));
	assert_non_null(line);

	JSON_Value *value = json_parse_string(line);
	assert_non_null(value);
	JSON_Array *serialized = json_object_get_array(
			json_object_get_object(json_object(value), "payload"), "entries");
	assert_int_equal(sizeof(entries)/sizeof(entries[0]),
			json_array_get_count(serialized));
	assert_string_equal("directory",
			json_object_get_string(json_array_get_object(serialized, 0U), "kind"));
	assert_string_equal("unknown",
			json_object_get_string(json_array_get_object(serialized, 8U), "kind"));
	assert_non_null(json_object_get_object(json_array_get_object(serialized, 2U),
			"stat_error"));
	assert_false(json_object_has_value(json_array_get_object(serialized, 8U),
			"stat_error"));

	json_value_free(value);
	nv_protocol_json_free(line);
}

TEST(snapshot_record_larger_than_m0_byte_limit_is_rejected_before_serialization)
{
	char *const display = malloc(NV_PANE_SNAPSHOT_MAX_DISPLAY_BYTES + 1U);
	char *const hex = malloc(NV_PANE_SNAPSHOT_MAX_HEX_BYTES + 1U);
	const size_t entry_count = 44U;
	nv_pane_entry_t *const entries = calloc(entry_count, sizeof(*entries));
	assert_non_null(display);
	assert_non_null(hex);
	assert_non_null(entries);

	memset(display, 'd', NV_PANE_SNAPSHOT_MAX_DISPLAY_BYTES);
	display[NV_PANE_SNAPSHOT_MAX_DISPLAY_BYTES] = '\0';
	memset(hex, 'a', NV_PANE_SNAPSHOT_MAX_HEX_BYTES);
	hex[NV_PANE_SNAPSHOT_MAX_HEX_BYTES] = '\0';
	for(size_t i = 0U; i < entry_count; ++i)
	{
		entries[i] = (nv_pane_entry_t){
			.name_display = display,
			.name_bytes_hex = hex,
			.path_display = display,
			.path_bytes_hex = hex,
			.kind = NV_ENTRY_FILE,
		};
	}

	nv_pane_snapshot_t snapshot = {
		.cwd_display = display,
		.cwd_bytes_hex = hex,
		.cursor = 0,
		.entry_count = entry_count,
		.entries = entries,
	};
	char *line = NULL;
	assert_int_equal(NV_PROTOCOL_JSON_TOO_LARGE,
			nv_protocol_snapshot_json(&snapshot, 1U, &line));
	assert_null(line);

	free(entries);
	free(hex);
	free(display);
}

TEST(error_record_includes_os_error_and_path_identity)
{
	nv_snapshot_error_t error = {
		.code = "open-directory",
		.message = "permission denied",
		.path_display = "/private",
		.path_bytes_hex = "2f70726976617465",
		.os_error = EACCES,
	};
	char *line = nv_protocol_error_json(&error, 1U);
	assert_non_null(line);

	JSON_Value *value = json_parse_string(line);
	assert_non_null(value);
	JSON_Object *payload = json_object_get_object(json_object(value), "payload");
	assert_int_equal(EACCES, json_object_get_number(payload, "os_error"));
	assert_string_equal("/private",
			json_object_get_string(payload, "path_display"));
	assert_string_equal("2f70726976617465",
			json_object_get_string(payload, "path_bytes_hex"));

	json_value_free(value);
	nv_protocol_json_free(line);
}

TEST(null_protocol_models_are_rejected)
{
	char *line = NULL;
	assert_int_equal(NV_PROTOCOL_JSON_ERROR,
			nv_protocol_snapshot_json(NULL, 1U, &line));
	assert_null(line);
	assert_null(nv_protocol_error_json(NULL, 1U));
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
