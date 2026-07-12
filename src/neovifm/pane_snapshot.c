/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "pane_snapshot.h"

#include <sys/stat.h> /* S_* lstat() stat */
#include <sys/time.h> /* gettimeofday() timeval */

#include <dirent.h> /* DIR dirent closedir() opendir() readdir() */
#include <errno.h> /* E* errno */
#include <limits.h> /* INT64_MAX INT64_MIN */
#include <stdint.h> /* SIZE_MAX int64_t */
#include <stdlib.h> /* free() malloc() qsort() realloc() */
#include <string.h> /* memcpy() memset() strcmp() strdup() strlen() strerror() */

#include "../utils/utf8proc.h"

static char *bytes_to_hex(const char bytes[]);
static char *display_string(const char bytes[]);
static char *join_path(const char dir[], const char name[]);
static int build_entry(const char dir[], const char name[],
		nv_pane_entry_t *entry);
static int append_entry(nv_pane_snapshot_t *snapshot,
		nv_pane_entry_t *entry, size_t *capacity);
static void entry_free(nv_pane_entry_t *entry);
static int set_error(nv_snapshot_error_t *error, const char code[],
		int os_error, const char path[]);

static char *
bytes_to_hex(const char bytes[])
{
	static const char hex[] = "0123456789abcdef";
	const size_t len = strlen(bytes);
	if(len > (SIZE_MAX - 1U)/2U)
	{
		return NULL;
	}

	char *const encoded = malloc(len*2U + 1U);
	if(encoded == NULL)
	{
		return NULL;
	}

	for(size_t i = 0U; i < len; ++i)
	{
		const unsigned char byte = (unsigned char)bytes[i];
		encoded[i*2U] = hex[byte >> 4U];
		encoded[i*2U + 1U] = hex[byte & 0x0fU];
	}
	encoded[len*2U] = '\0';
	return encoded;
}

static int
is_unsafe_codepoint(utf8proc_int32_t codepoint)
{
	return codepoint < 0x20 || codepoint == 0x7f ||
			(codepoint >= 0x80 && codepoint <= 0x9f) ||
			codepoint == 0x061c || codepoint == 0x200e || codepoint == 0x200f ||
			(codepoint >= 0x2028 && codepoint <= 0x202e) ||
			(codepoint >= 0x2066 && codepoint <= 0x2069);
}

static char *
display_string(const char bytes[])
{
	static const unsigned char replacement[] = { 0xefU, 0xbfU, 0xbdU };
	const size_t len = strlen(bytes);
	if(len > (SIZE_MAX - 1U)/3U || len > (size_t)PTRDIFF_MAX)
	{
		return NULL;
	}

	char *const display = malloc(len*3U + 1U);
	if(display == NULL)
	{
		return NULL;
	}

	size_t input_pos = 0U;
	size_t output_pos = 0U;
	while(input_pos < len)
	{
		utf8proc_int32_t codepoint;
		const utf8proc_ssize_t width = utf8proc_iterate(
				(const utf8proc_uint8_t *)bytes + input_pos,
				(utf8proc_ssize_t)(len - input_pos), &codepoint);
		if(width > 0 && !is_unsafe_codepoint(codepoint))
		{
			memcpy(display + output_pos, bytes + input_pos, (size_t)width);
			input_pos += (size_t)width;
			output_pos += (size_t)width;
		}
		else
		{
			memcpy(display + output_pos, replacement, sizeof(replacement));
			input_pos += (width > 0) ? (size_t)width : 1U;
			output_pos += sizeof(replacement);
		}
	}
	display[output_pos] = '\0';
	return display;
}

static char *
join_path(const char dir[], const char name[])
{
	const size_t dir_len = strlen(dir);
	const size_t name_len = strlen(name);
	const int needs_separator = dir_len != 0U && dir[dir_len - 1U] != '/';
	if(dir_len > SIZE_MAX - name_len - (size_t)needs_separator - 1U)
	{
		return NULL;
	}

	char *const path = malloc(dir_len + (size_t)needs_separator + name_len + 1U);
	if(path == NULL)
	{
		return NULL;
	}

	memcpy(path, dir, dir_len);
	if(needs_separator)
	{
		path[dir_len] = '/';
	}
	memcpy(path + dir_len + (size_t)needs_separator, name, name_len + 1U);
	return path;
}

static int64_t
milliseconds_from_parts(long double seconds, long nanoseconds)
{
	const long double value = seconds*1000.0L + nanoseconds/1000000.0L;
	if(value > INT64_MAX)
	{
		return INT64_MAX;
	}
	if(value < INT64_MIN)
	{
		return INT64_MIN;
	}
	return (int64_t)value;
}

static int64_t
stat_mtime_ms(const struct stat *st)
{
#if defined(__APPLE__)
	return milliseconds_from_parts(st->st_mtimespec.tv_sec,
			st->st_mtimespec.tv_nsec);
#else
	return milliseconds_from_parts(st->st_mtim.tv_sec, st->st_mtim.tv_nsec);
#endif
}

static nv_entry_kind_t
entry_kind(const struct stat *st)
{
	if(S_ISDIR(st->st_mode))
		return NV_ENTRY_DIRECTORY;
	if(S_ISLNK(st->st_mode))
		return NV_ENTRY_SYMLINK;
	if(S_ISREG(st->st_mode))
		return (st->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
			? NV_ENTRY_EXECUTABLE
			: NV_ENTRY_FILE;
	if(S_ISFIFO(st->st_mode))
		return NV_ENTRY_FIFO;
	if(S_ISSOCK(st->st_mode))
		return NV_ENTRY_SOCKET;
	if(S_ISCHR(st->st_mode))
		return NV_ENTRY_CHAR_DEVICE;
	if(S_ISBLK(st->st_mode))
		return NV_ENTRY_BLOCK_DEVICE;
	return NV_ENTRY_UNKNOWN;
}

static void
set_stat(nv_pane_entry_t *entry, const struct stat *st)
{
	entry->kind = entry_kind(st);
	entry->size_bytes = (st->st_size > 0) ? (uint64_t)st->st_size : 0U;
	entry->mtime_unix_ms = stat_mtime_ms(st);
	entry->inode = (uint64_t)st->st_ino;
	entry->mode = (uint32_t)st->st_mode;
	entry->has_stat = 1;
}

static int
build_entry(const char dir[], const char name[], nv_pane_entry_t *entry)
{
	memset(entry, 0, sizeof(*entry));
	entry->kind = NV_ENTRY_UNKNOWN;
	entry->hidden = name[0] == '.';

	char *const raw_path = join_path(dir, name);
	if(raw_path == NULL)
	{
		return -1;
	}

	entry->name_display = display_string(name);
	entry->name_bytes_hex = bytes_to_hex(name);
	entry->path_display = display_string(raw_path);
	entry->path_bytes_hex = bytes_to_hex(raw_path);
	if(entry->name_display == NULL || entry->name_bytes_hex == NULL ||
			entry->path_display == NULL || entry->path_bytes_hex == NULL)
	{
		free(raw_path);
		entry_free(entry);
		return -1;
	}

	struct stat st;
	if(lstat(raw_path, &st) == 0)
	{
		set_stat(entry, &st);
	}
	else
	{
		entry->stat_error = errno;
	}
	free(raw_path);
	return 0;
}

static int
append_entry(nv_pane_snapshot_t *snapshot, nv_pane_entry_t *entry,
		size_t *capacity)
{
	if(snapshot->entry_count == *capacity)
	{
		const size_t next_capacity = (*capacity == 0U)
			? 16U
			: *capacity + *capacity/2U;
		if(next_capacity <= *capacity ||
				next_capacity > SIZE_MAX/sizeof(*snapshot->entries))
		{
			return -1;
		}

		nv_pane_entry_t *const entries = realloc(snapshot->entries,
				next_capacity*sizeof(*snapshot->entries));
		if(entries == NULL)
		{
			return -1;
		}
		snapshot->entries = entries;
		*capacity = next_capacity;
	}

	snapshot->entries[snapshot->entry_count] = *entry;
	++snapshot->entry_count;
	memset(entry, 0, sizeof(*entry));
	return 0;
}

static void
entry_free(nv_pane_entry_t *entry)
{
	free(entry->name_display);
	free(entry->name_bytes_hex);
	free(entry->path_display);
	free(entry->path_bytes_hex);
	memset(entry, 0, sizeof(*entry));
}

static int
entry_compare(const void *left, const void *right)
{
	const nv_pane_entry_t *const lhs = left;
	const nv_pane_entry_t *const rhs = right;
	return strcmp(lhs->name_bytes_hex, rhs->name_bytes_hex);
}

static int
retryable_error(int error_number)
{
	return error_number == EINTR || error_number == EAGAIN ||
			error_number == EMFILE || error_number == ENFILE;
}

static int
set_error(nv_snapshot_error_t *error, const char code[], int os_error,
		const char path[])
{
	char *const error_code = strdup(code);
	char *const message = display_string(strerror(os_error));
	char *const path_display = (path == NULL) ? NULL : display_string(path);
	char *const path_bytes_hex = (path == NULL) ? NULL : bytes_to_hex(path);
	if(error_code == NULL || message == NULL ||
			(path != NULL && (path_display == NULL || path_bytes_hex == NULL)))
	{
		free(error_code);
		free(message);
		free(path_display);
		free(path_bytes_hex);
		return -1;
	}

	error->code = error_code;
	error->message = message;
	error->path_display = path_display;
	error->path_bytes_hex = path_bytes_hex;
	error->os_error = os_error;
	error->retryable = retryable_error(os_error);
	return -1;
}

static int
initialize_snapshot(const char path[], nv_pane_snapshot_t *snapshot,
		nv_snapshot_error_t *error)
{
	snapshot->cwd_display = display_string(path);
	snapshot->cwd_bytes_hex = bytes_to_hex(path);
	if(snapshot->cwd_display == NULL || snapshot->cwd_bytes_hex == NULL)
	{
		return set_error(error, "out-of-memory", ENOMEM, path);
	}
	return 0;
}

static int
scan_directory(DIR *dir, const char path[], nv_pane_snapshot_t *snapshot,
		nv_snapshot_error_t *error, size_t *capacity)
{
	for(;;)
	{
		errno = 0;
		struct dirent *const dir_entry = readdir(dir);
		if(dir_entry == NULL)
		{
			return (errno == 0) ? 0 : set_error(error, "read-directory", errno,
					path);
		}
		if(strcmp(dir_entry->d_name, ".") == 0 ||
				strcmp(dir_entry->d_name, "..") == 0)
		{
			continue;
		}

		nv_pane_entry_t entry;
		if(build_entry(path, dir_entry->d_name, &entry) != 0 ||
				append_entry(snapshot, &entry, capacity) != 0)
		{
			entry_free(&entry);
			return set_error(error, "out-of-memory", ENOMEM, path);
		}
	}
}

static int64_t
current_time_ms(void)
{
	struct timeval now;
	if(gettimeofday(&now, NULL) != 0)
	{
		return 0;
	}
	return milliseconds_from_parts(now.tv_sec, now.tv_usec*1000L);
}

int
nv_pane_snapshot_build(const char path[], nv_pane_snapshot_t *snapshot,
		nv_snapshot_error_t *error)
{
	if(snapshot == NULL || error == NULL)
	{
		return -1;
	}

	nv_pane_snapshot_t next_snapshot = {};
	nv_snapshot_error_t next_error = {};
	size_t capacity = 0U;
	if(path == NULL || path[0] == '\0')
	{
		set_error(&next_error, "invalid-path", EINVAL, path);
		goto failed;
	}

	DIR *const dir = opendir(path);
	if(dir == NULL)
	{
		set_error(&next_error, "open-directory", errno, path);
		goto failed;
	}
	if(initialize_snapshot(path, &next_snapshot, &next_error) != 0 ||
			scan_directory(dir, path, &next_snapshot, &next_error, &capacity) != 0)
	{
		closedir(dir);
		goto failed;
	}
	if(closedir(dir) != 0)
	{
		const int close_error = errno;
		set_error(&next_error, "close-directory", close_error, path);
		goto failed;
	}

	if(next_snapshot.entry_count > 1U)
	{
		qsort(next_snapshot.entries, next_snapshot.entry_count,
				sizeof(*next_snapshot.entries), entry_compare);
	}
	next_snapshot.generated_at_unix_ms = current_time_ms();
	next_snapshot.cursor = (next_snapshot.entry_count == 0U) ? -1 : 0;

	nv_pane_snapshot_free(snapshot);
	nv_snapshot_error_free(error);
	*snapshot = next_snapshot;
	return 0;

failed:
	nv_pane_snapshot_free(&next_snapshot);
	nv_snapshot_error_free(error);
	*error = next_error;
	return -1;
}

void
nv_pane_snapshot_free(nv_pane_snapshot_t *snapshot)
{
	if(snapshot == NULL)
	{
		return;
	}
	for(size_t i = 0U; i < snapshot->entry_count; ++i)
	{
		entry_free(&snapshot->entries[i]);
	}
	free(snapshot->entries);
	free(snapshot->cwd_display);
	free(snapshot->cwd_bytes_hex);
	memset(snapshot, 0, sizeof(*snapshot));
}

void
nv_snapshot_error_free(nv_snapshot_error_t *error)
{
	if(error == NULL)
	{
		return;
	}
	free(error->code);
	free(error->message);
	free(error->path_display);
	free(error->path_bytes_hex);
	memset(error, 0, sizeof(*error));
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
