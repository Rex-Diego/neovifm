#include <stic.h>

#include <errno.h>
#include <stdlib.h>

#include <test-utils.h>

#include "../../src/neovifm/pane_snapshot.h"
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

TEST(snapshot_record_uses_strings_for_wide_numbers)
{
	const char *const dir = SANDBOX_PATH "/protocol";
	nv_pane_snapshot_t snapshot = {};
	nv_snapshot_error_t error = {};
	create_dir(dir);
	make_file(SANDBOX_PATH "/protocol/file", "abc");

	assert_success(nv_pane_snapshot_build(dir, &snapshot, &error));
	char *line = nv_protocol_snapshot_json(&snapshot, 1U);
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
	char *line = nv_protocol_snapshot_json(&snapshot, 1U);
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
	assert_null(nv_protocol_snapshot_json(NULL, 1U));
	assert_null(nv_protocol_error_json(NULL, 1U));
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
