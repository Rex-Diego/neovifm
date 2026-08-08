#include <stic.h>

#include <errno.h>
#include <sys/stat.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <test-utils.h>

#include "../../src/neovifm/pane_snapshot.h"

static const char *const EMPTY_DIR = SANDBOX_PATH "/empty";
static const char *const CONTENT_DIR = SANDBOX_PATH "/content";

static const nv_pane_entry_t *
find_entry(const nv_pane_snapshot_t *snapshot, const char name[])
{
	for(size_t i = 0U; i < snapshot->entry_count; ++i)
	{
		if(strcmp(snapshot->entries[i].name_display, name) == 0)
		{
			return &snapshot->entries[i];
		}
	}
	return NULL;
}

static const nv_pane_entry_t *
find_entry_by_bytes(const nv_pane_snapshot_t *snapshot, const char bytes_hex[])
{
	for(size_t i = 0U; i < snapshot->entry_count; ++i)
	{
		if(strcmp(snapshot->entries[i].name_bytes_hex, bytes_hex) == 0)
		{
			return &snapshot->entries[i];
		}
	}
	return NULL;
}

TEST(empty_directory_produces_empty_snapshot)
{
	nv_pane_snapshot_t snapshot = {};
	nv_snapshot_error_t error = {};

	create_dir(EMPTY_DIR);

	assert_success(nv_pane_snapshot_build(EMPTY_DIR, &snapshot, &error));
	assert_int_equal(0, snapshot.entry_count);
	assert_int_equal(-1, snapshot.cursor);
	assert_string_equal(EMPTY_DIR, snapshot.cwd_display);

	nv_pane_snapshot_free(&snapshot);
	nv_snapshot_error_free(&error);
	remove_dir(EMPTY_DIR);
}

TEST(snapshot_owns_basic_file_and_directory_metadata)
{
	nv_pane_snapshot_t snapshot = {};
	nv_snapshot_error_t error = {};

	create_dir(CONTENT_DIR);
	make_file(SANDBOX_PATH "/content/file.txt", "abc");
	create_dir(SANDBOX_PATH "/content/subdir");

	assert_success(nv_pane_snapshot_build(CONTENT_DIR, &snapshot, &error));
	assert_int_equal(2, snapshot.entry_count);
	assert_int_equal(0, snapshot.cursor);
	assert_int_equal(0, snapshot.selection_count);
	assert_int_equal(0, snapshot.filtered_count);
	assert_int_equal(NV_SORT_NAME, snapshot.sort_key);
	assert_false(snapshot.sort_descending);
	assert_false(snapshot.filter_active);

	const nv_pane_entry_t *file = find_entry(&snapshot, "file.txt");
	assert_non_null(file);
	assert_int_equal(NV_ENTRY_FILE, file->kind);
	assert_int_equal(3, file->size_bytes);
	assert_false(file->hidden);
	assert_false(file->selected);
#ifndef _WIN32
	assert_non_null(file->owner_display);
	assert_non_null(file->group_display);
	assert_true(strlen(file->owner_display) <= NV_PANE_SNAPSHOT_MAX_OWNER_BYTES);
	assert_true(strlen(file->group_display) <= NV_PANE_SNAPSHOT_MAX_OWNER_BYTES);
#endif

	const nv_pane_entry_t *dir = find_entry(&snapshot, "subdir");
	assert_non_null(dir);
	assert_int_equal(NV_ENTRY_DIRECTORY, dir->kind);

	char *owned_name = file->name_display;
	nv_pane_snapshot_free(&snapshot);
	assert_null(snapshot.entries);
	assert_int_equal(0, snapshot.entry_count);
	assert_true(owned_name != NULL);

	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/content/file.txt");
	remove_dir(SANDBOX_PATH "/content/subdir");
	remove_dir(CONTENT_DIR);
}

TEST(initial_name_sort_places_cursor_on_first_sorted_entry)
{
	nv_pane_snapshot_t snapshot = {};
	nv_snapshot_error_t error = {};

	create_dir(CONTENT_DIR);
	create_file(SANDBOX_PATH "/content/z-file");
	create_file(SANDBOX_PATH "/content/a-file");

	assert_success(nv_pane_snapshot_build(CONTENT_DIR, &snapshot, &error));
	assert_int_equal(0, snapshot.cursor);
	assert_string_equal("a-file", snapshot.entries[snapshot.cursor].name_display);

	nv_pane_snapshot_free(&snapshot);
	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/content/z-file");
	remove_file(SANDBOX_PATH "/content/a-file");
	remove_dir(CONTENT_DIR);
}

TEST(directory_entries_stay_before_files_in_both_name_directions)
{
	nv_pane_snapshot_t snapshot = {};
	nv_snapshot_error_t error = {};

	create_dir(CONTENT_DIR);
	create_dir(SANDBOX_PATH "/content/a-dir");
	create_dir(SANDBOX_PATH "/content/z-dir");
	create_file(SANDBOX_PATH "/content/0-file");
	create_file(SANDBOX_PATH "/content/z-file");

	assert_success(nv_pane_snapshot_build(CONTENT_DIR, &snapshot, &error));
	nv_pane_snapshot_sort(&snapshot, NV_SORT_NAME, 0);
	assert_int_equal(NV_ENTRY_DIRECTORY, snapshot.entries[0].kind);
	assert_int_equal(NV_ENTRY_DIRECTORY, snapshot.entries[1].kind);
	assert_string_equal("a-dir", snapshot.entries[0].name_display);
	assert_string_equal("z-dir", snapshot.entries[1].name_display);
	assert_string_equal("0-file", snapshot.entries[2].name_display);

	nv_pane_snapshot_sort(&snapshot, NV_SORT_NAME, 1);
	assert_int_equal(NV_ENTRY_DIRECTORY, snapshot.entries[0].kind);
	assert_int_equal(NV_ENTRY_DIRECTORY, snapshot.entries[1].kind);
	assert_string_equal("z-dir", snapshot.entries[0].name_display);
	assert_string_equal("a-dir", snapshot.entries[1].name_display);
	assert_string_equal("z-file", snapshot.entries[2].name_display);
	assert_string_equal("0-file", snapshot.entries[3].name_display);

	static const nv_pane_sort_key_t keys[] = {
		NV_SORT_NAME, NV_SORT_EXTENSION, NV_SORT_SIZE, NV_SORT_CTIME,
		NV_SORT_MTIME, NV_SORT_MODE, NV_SORT_TYPE, NV_SORT_OTHER,
	};
	for(size_t i = 0U; i < sizeof(keys)/sizeof(keys[0]); ++i)
	{
		nv_pane_snapshot_sort(&snapshot, keys[i], 0);
		assert_int_equal(NV_ENTRY_DIRECTORY, snapshot.entries[0].kind);
		assert_int_equal(NV_ENTRY_DIRECTORY, snapshot.entries[1].kind);
		assert_true(snapshot.entries[2].kind != NV_ENTRY_DIRECTORY);
		assert_true(snapshot.entries[3].kind != NV_ENTRY_DIRECTORY);
		nv_pane_snapshot_sort(&snapshot, keys[i], 1);
		assert_int_equal(NV_ENTRY_DIRECTORY, snapshot.entries[0].kind);
		assert_int_equal(NV_ENTRY_DIRECTORY, snapshot.entries[1].kind);
		assert_true(snapshot.entries[2].kind != NV_ENTRY_DIRECTORY);
		assert_true(snapshot.entries[3].kind != NV_ENTRY_DIRECTORY);
	}

	nv_pane_snapshot_free(&snapshot);
	nv_snapshot_error_free(&error);
	remove_dir(SANDBOX_PATH "/content/a-dir");
	remove_dir(SANDBOX_PATH "/content/z-dir");
	remove_file(SANDBOX_PATH "/content/0-file");
	remove_file(SANDBOX_PATH "/content/z-file");
	remove_dir(CONTENT_DIR);
}

TEST(hidden_entry_is_marked)
{
	nv_pane_snapshot_t snapshot = {};
	nv_snapshot_error_t error = {};

	create_dir(CONTENT_DIR);
	create_file(SANDBOX_PATH "/content/.hidden");

	assert_success(nv_pane_snapshot_build(CONTENT_DIR, &snapshot, &error));
	const nv_pane_entry_t *entry = find_entry(&snapshot, ".hidden");
	assert_non_null(entry);
	assert_true(entry->hidden);

	nv_pane_snapshot_free(&snapshot);
	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/content/.hidden");
	remove_dir(CONTENT_DIR);
}

TEST(control_characters_are_safe_for_display_and_lossless_in_bytes)
{
	nv_pane_snapshot_t snapshot = {};
	nv_snapshot_error_t error = {};
	const char *const raw_path = SANDBOX_PATH "/content/unsafe\033name";

	create_dir(CONTENT_DIR);
	create_file(raw_path);

	assert_success(nv_pane_snapshot_build(CONTENT_DIR, &snapshot, &error));
	const nv_pane_entry_t *entry = find_entry_by_bytes(&snapshot,
			"756e736166651b6e616d65");
	assert_non_null(entry);
	assert_string_equal("unsafe\xef\xbf\xbd" "name", entry->name_display);
	assert_null(strchr(entry->name_display, '\033'));

	nv_pane_snapshot_free(&snapshot);
	nv_snapshot_error_free(&error);
	remove_file(raw_path);
	remove_dir(CONTENT_DIR);
}

TEST(bidirectional_controls_are_replaced_in_display_names)
{
	nv_pane_snapshot_t snapshot = {};
	nv_snapshot_error_t error = {};
	const char *const raw_path = SANDBOX_PATH "/content/report\xe2\x80\xae"
			"gnp.exe";

	create_dir(CONTENT_DIR);
	create_file(raw_path);

	assert_success(nv_pane_snapshot_build(CONTENT_DIR, &snapshot, &error));
	const nv_pane_entry_t *entry = find_entry_by_bytes(&snapshot,
			"7265706f7274e280ae676e702e657865");
	assert_non_null(entry);
	assert_string_equal("report\xef\xbf\xbd" "gnp.exe", entry->name_display);

	nv_pane_snapshot_free(&snapshot);
	nv_snapshot_error_free(&error);
	remove_file(raw_path);
	remove_dir(CONTENT_DIR);
}

TEST(rebuilding_replaces_snapshot_and_preserves_it_on_failure)
{
	nv_pane_snapshot_t snapshot = {};
	nv_snapshot_error_t error = {};
	const char *const replacement_dir = SANDBOX_PATH "/replacement";

	create_dir(CONTENT_DIR);
	create_file(SANDBOX_PATH "/content/file");
	create_dir(replacement_dir);

	assert_success(nv_pane_snapshot_build(CONTENT_DIR, &snapshot, &error));
	assert_int_equal(1, snapshot.entry_count);
	assert_success(nv_pane_snapshot_build(replacement_dir, &snapshot, &error));
	assert_int_equal(0, snapshot.entry_count);
	assert_string_equal(replacement_dir, snapshot.cwd_display);

	assert_failure(nv_pane_snapshot_build(SANDBOX_PATH "/missing", &snapshot,
			&error));
	assert_string_equal(replacement_dir, snapshot.cwd_display);
	assert_string_equal("open-directory", error.code);

	nv_pane_snapshot_free(&snapshot);
	nv_snapshot_error_free(&error);
	remove_file(SANDBOX_PATH "/content/file");
	remove_dir(CONTENT_DIR);
	remove_dir(replacement_dir);
}

#ifndef _WIN32
TEST(executable_symlink_and_fifo_kinds_are_classified)
{
	nv_pane_snapshot_t snapshot = {};
	nv_snapshot_error_t error = {};
	const char *const executable = SANDBOX_PATH "/content/run";
	const char *const symlink_path = SANDBOX_PATH "/content/link";
	const char *const fifo = SANDBOX_PATH "/content/pipe";

	create_dir(CONTENT_DIR);
	create_file(executable);
	assert_success(chmod(executable, 0700));
	assert_success(symlink("missing-target", symlink_path));
	assert_success(mkfifo(fifo, 0600));

	assert_success(nv_pane_snapshot_build(CONTENT_DIR, &snapshot, &error));
	assert_int_equal(NV_ENTRY_EXECUTABLE,
			find_entry(&snapshot, "run")->kind);
	assert_int_equal(NV_ENTRY_SYMLINK,
			find_entry(&snapshot, "link")->kind);
	assert_int_equal(NV_ENTRY_FIFO,
			find_entry(&snapshot, "pipe")->kind);

	nv_pane_snapshot_free(&snapshot);
	nv_snapshot_error_free(&error);
	remove_file(executable);
	remove_file(symlink_path);
	remove_file(fifo);
	remove_dir(CONTENT_DIR);
}
#endif

TEST(growing_entry_storage_keeps_all_entries)
{
	nv_pane_snapshot_t snapshot = {};
	nv_snapshot_error_t error = {};
	char path[256];

	create_dir(CONTENT_DIR);
	for(int i = 0; i < 20; ++i)
	{
		snprintf(path, sizeof(path), SANDBOX_PATH "/content/file-%02d", i);
		create_file(path);
	}

	assert_success(nv_pane_snapshot_build(CONTENT_DIR, &snapshot, &error));
	assert_int_equal(20, snapshot.entry_count);

	nv_pane_snapshot_free(&snapshot);
	nv_snapshot_error_free(&error);
	for(int i = 0; i < 20; ++i)
	{
		snprintf(path, sizeof(path), SANDBOX_PATH "/content/file-%02d", i);
		remove_file(path);
	}
	remove_dir(CONTENT_DIR);
}

TEST(directory_above_m0_entry_limit_returns_structured_error)
{
	nv_pane_snapshot_t snapshot = {};
	nv_snapshot_error_t error = {};
	char path[256];

	create_dir(CONTENT_DIR);
	for(size_t i = 0U; i <= NV_PANE_SNAPSHOT_MAX_ENTRIES; ++i)
	{
		snprintf(path, sizeof(path), SANDBOX_PATH "/content/entry-%04zu", i);
		create_file(path);
	}

	assert_failure(nv_pane_snapshot_build(CONTENT_DIR, &snapshot, &error));
	assert_string_equal("snapshot-too-large", error.code);
	assert_int_equal(0, snapshot.entry_count);

	nv_pane_snapshot_free(&snapshot);
	nv_snapshot_error_free(&error);
	for(size_t i = 0U; i <= NV_PANE_SNAPSHOT_MAX_ENTRIES; ++i)
	{
		snprintf(path, sizeof(path), SANDBOX_PATH "/content/entry-%04zu", i);
		remove_file(path);
	}
	remove_dir(CONTENT_DIR);
}

TEST(path_outside_protocol_field_limits_returns_structured_error)
{
	nv_pane_snapshot_t snapshot = {};
	nv_snapshot_error_t error = {};
	char path[NV_PANE_SNAPSHOT_MAX_HEX_BYTES/2U + 2U];

	memset(path, 'a', sizeof(path) - 1U);
	path[sizeof(path) - 1U] = '\0';

	assert_failure(nv_pane_snapshot_build(path, &snapshot, &error));
	assert_string_equal("snapshot-too-large", error.code);
	assert_int_equal(E2BIG, error.os_error);
	assert_false(error.retryable);
	assert_null(error.path_display);
	assert_null(error.path_bytes_hex);

	nv_pane_snapshot_free(&snapshot);
	nv_snapshot_error_free(&error);
}

TEST(empty_path_and_null_outputs_are_rejected)
{
	nv_pane_snapshot_t snapshot = {};
	nv_snapshot_error_t error = {};

	assert_failure(nv_pane_snapshot_build("", &snapshot, &error));
	assert_string_equal("invalid-path", error.code);
	assert_failure(nv_pane_snapshot_build(CONTENT_DIR, NULL, &error));
	assert_failure(nv_pane_snapshot_build(CONTENT_DIR, &snapshot, NULL));

	nv_pane_snapshot_free(&snapshot);
	nv_snapshot_error_free(&error);
	nv_pane_snapshot_free(NULL);
	nv_snapshot_error_free(NULL);
}

TEST(missing_directory_returns_structured_error)
{
	nv_pane_snapshot_t snapshot = {};
	nv_snapshot_error_t error = {};

	assert_failure(nv_pane_snapshot_build(SANDBOX_PATH "/missing", &snapshot,
			&error));
	assert_string_equal("open-directory", error.code);
	assert_non_null(error.message);
	assert_string_equal(SANDBOX_PATH "/missing", error.path_display);

	nv_pane_snapshot_free(&snapshot);
	nv_snapshot_error_free(&error);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
