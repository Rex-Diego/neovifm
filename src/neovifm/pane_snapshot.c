/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "pane_snapshot.h"

#include <sys/stat.h> /* S_* struct stat */

#include <errno.h> /* E* errno */
#include <limits.h> /* INT64_MAX INT64_MIN */
#include <stdint.h> /* SIZE_MAX int64_t */
#include <stdlib.h> /* free() malloc() qsort() realloc() */
#include <string.h> /* memcpy() memset() strcmp() strdup() strlen() strerror() */
#include <time.h> /* time_t time() */
#ifndef _WIN32
#include <sys/time.h> /* gettimeofday() struct timeval */
#endif

#include "../compat/neovifm_fs.h"
#include "../utils/utf8proc.h"

static char *bytes_to_hex(const char bytes[]);
static char *display_string(const char bytes[]);
static int raw_string_may_fit_protocol_fields(const char bytes[]);
static int protocol_field_pair_fits(const char display[], const char hex[]);
static int add_protocol_bytes(size_t *total, size_t bytes);
static int snapshot_protocol_budget(const nv_pane_snapshot_t *snapshot,
		size_t *budget);
static int entry_protocol_budget(const nv_pane_entry_t *entry, size_t *budget);
static char *join_path(const char dir[], const char name[]);
static int build_entry(const char dir[], const char name[],
		nv_pane_entry_t *entry);
static int append_entry(nv_pane_snapshot_t *snapshot,
		nv_pane_entry_t *entry, size_t *capacity, size_t *protocol_bytes);
static void entry_free(nv_pane_entry_t *entry);
static int set_error(nv_snapshot_error_t *error, const char code[],
		int os_error, const char path[]);

enum
{
	BUILD_ENTRY_NO_MEMORY = -1,
	BUILD_ENTRY_LIMIT_REACHED = -2,
	APPEND_ENTRY_NO_MEMORY = -1,
	APPEND_ENTRY_LIMIT_REACHED = -2,
};

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
raw_string_may_fit_protocol_fields(const char bytes[])
{
	const size_t maximum_bytes = NV_PANE_SNAPSHOT_MAX_HEX_BYTES/2U;
	for(size_t i = 0U; i <= maximum_bytes; ++i)
	{
		if(bytes[i] == '\0')
		{
			return 1;
		}
	}
	return 0;
}

static int
protocol_field_pair_fits(const char display[], const char hex[])
{
	return strlen(display) <= NV_PANE_SNAPSHOT_MAX_DISPLAY_BYTES &&
			strlen(hex) <= NV_PANE_SNAPSHOT_MAX_HEX_BYTES;
}

static int
add_protocol_bytes(size_t *total, size_t bytes)
{
	if(*total > SIZE_MAX - bytes)
	{
		return -1;
	}
	*total += bytes;
	return 0;
}

static int
snapshot_protocol_budget(const nv_pane_snapshot_t *snapshot, size_t *budget)
{
	/* Includes envelope, payload keys, decimal fields, separators and quotes. */
	*budget = 512U;
	const size_t cwd_display_bytes = strlen(snapshot->cwd_display);
	if(cwd_display_bytes > SIZE_MAX/2U ||
			add_protocol_bytes(budget, cwd_display_bytes*2U) != 0 ||
			add_protocol_bytes(budget, strlen(snapshot->cwd_bytes_hex)) != 0)
	{
		return -1;
	}
	return *budget <= NV_PROTOCOL_MAX_RECORD_BYTES ? 0 : -1;
}

static int
entry_protocol_budget(const nv_pane_entry_t *entry, size_t *budget)
{
	/* Display fields can expand to two JSON bytes per stored byte. */
	*budget = 512U;
	const size_t name_display_bytes = strlen(entry->name_display);
	const size_t path_display_bytes = strlen(entry->path_display);
	if(name_display_bytes > SIZE_MAX/2U || path_display_bytes > SIZE_MAX/2U ||
			add_protocol_bytes(budget, name_display_bytes*2U) != 0 ||
			add_protocol_bytes(budget, strlen(entry->name_bytes_hex)) != 0 ||
			add_protocol_bytes(budget, path_display_bytes*2U) != 0 ||
			add_protocol_bytes(budget, strlen(entry->path_bytes_hex)) != 0)
	{
		return -1;
	}
	return 0;
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
#elif defined(HAVE_STRUCT_STAT_ST_MTIM)
	return milliseconds_from_parts(st->st_mtim.tv_sec, st->st_mtim.tv_nsec);
#else
	return milliseconds_from_parts(st->st_mtime, 0L);
#endif
}

static nv_entry_kind_t
entry_kind(const struct stat *st)
{
	if(S_ISDIR(st->st_mode))
		return NV_ENTRY_DIRECTORY;
#ifdef S_ISLNK
	if(S_ISLNK(st->st_mode))
		return NV_ENTRY_SYMLINK;
#endif
	if(S_ISREG(st->st_mode))
		return (st->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
			? NV_ENTRY_EXECUTABLE
			: NV_ENTRY_FILE;
#ifdef S_ISFIFO
	if(S_ISFIFO(st->st_mode))
		return NV_ENTRY_FIFO;
#endif
#ifdef S_ISSOCK
	if(S_ISSOCK(st->st_mode))
		return NV_ENTRY_SOCKET;
#endif
#ifdef S_ISCHR
	if(S_ISCHR(st->st_mode))
		return NV_ENTRY_CHAR_DEVICE;
#endif
#ifdef S_ISBLK
	if(S_ISBLK(st->st_mode))
		return NV_ENTRY_BLOCK_DEVICE;
#endif
	return NV_ENTRY_UNKNOWN;
}

static void
set_stat(nv_pane_entry_t *entry, const struct stat *st, int is_symlink)
{
	entry->kind = is_symlink ? NV_ENTRY_SYMLINK : entry_kind(st);
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
		return BUILD_ENTRY_NO_MEMORY;
	}
	if(!raw_string_may_fit_protocol_fields(name) ||
			!raw_string_may_fit_protocol_fields(raw_path))
	{
		free(raw_path);
		return BUILD_ENTRY_LIMIT_REACHED;
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
		return BUILD_ENTRY_NO_MEMORY;
	}
	if(!protocol_field_pair_fits(entry->name_display, entry->name_bytes_hex) ||
			!protocol_field_pair_fits(entry->path_display,
					entry->path_bytes_hex))
	{
		free(raw_path);
		entry_free(entry);
		return BUILD_ENTRY_LIMIT_REACHED;
	}

	struct stat st;
	int is_symlink = 0;
	if(nv_lstat(raw_path, &st, &is_symlink) == 0)
	{
		set_stat(entry, &st, is_symlink);
	}
	else
	{
		entry->stat_error = errno;
		if(is_symlink)
		{
			entry->kind = NV_ENTRY_SYMLINK;
		}
	}
	free(raw_path);
	return 0;
}

static int
append_entry(nv_pane_snapshot_t *snapshot, nv_pane_entry_t *entry,
		size_t *capacity, size_t *protocol_bytes)
{
	if(snapshot->entry_count >= NV_PANE_SNAPSHOT_MAX_ENTRIES)
	{
		return APPEND_ENTRY_LIMIT_REACHED;
	}
	size_t entry_bytes;
	if(entry_protocol_budget(entry, &entry_bytes) != 0 ||
			entry_bytes > NV_PROTOCOL_MAX_RECORD_BYTES - *protocol_bytes)
	{
		return APPEND_ENTRY_LIMIT_REACHED;
	}
	if(snapshot->entry_count == *capacity)
	{
		const size_t next_capacity = (*capacity == 0U)
			? 16U
			: *capacity + *capacity/2U;
		if(next_capacity <= *capacity ||
				next_capacity > SIZE_MAX/sizeof(*snapshot->entries))
		{
			return APPEND_ENTRY_NO_MEMORY;
		}

		nv_pane_entry_t *const entries = realloc(snapshot->entries,
				next_capacity*sizeof(*snapshot->entries));
		if(entries == NULL)
		{
			return APPEND_ENTRY_NO_MEMORY;
		}
		snapshot->entries = entries;
		*capacity = next_capacity;
	}

	snapshot->entries[snapshot->entry_count] = *entry;
	++snapshot->entry_count;
	*protocol_bytes += entry_bytes;
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
	char *path_display = NULL;
	char *path_bytes_hex = NULL;
	if(path != NULL && raw_string_may_fit_protocol_fields(path))
	{
		path_display = display_string(path);
		path_bytes_hex = bytes_to_hex(path);
		if(path_display == NULL || path_bytes_hex == NULL)
		{
			free(error_code);
			free(message);
			free(path_display);
			free(path_bytes_hex);
			return -1;
		}
		if(!protocol_field_pair_fits(path_display, path_bytes_hex))
		{
			free(path_display);
			free(path_bytes_hex);
			path_display = NULL;
			path_bytes_hex = NULL;
		}
	}
	if(error_code == NULL || message == NULL)
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
	if(!raw_string_may_fit_protocol_fields(path))
	{
		return set_error(error, "snapshot-too-large", E2BIG, path);
	}
	snapshot->cwd_display = display_string(path);
	snapshot->cwd_bytes_hex = bytes_to_hex(path);
	if(snapshot->cwd_display == NULL || snapshot->cwd_bytes_hex == NULL)
	{
		return set_error(error, "out-of-memory", ENOMEM, path);
	}
	if(!protocol_field_pair_fits(snapshot->cwd_display,
			snapshot->cwd_bytes_hex))
	{
		free(snapshot->cwd_display);
		free(snapshot->cwd_bytes_hex);
		snapshot->cwd_display = NULL;
		snapshot->cwd_bytes_hex = NULL;
		return set_error(error, "snapshot-too-large", E2BIG, path);
	}
	return 0;
}

static int
scan_directory(nv_dir_t *dir, const char path[], nv_pane_snapshot_t *snapshot,
		nv_snapshot_error_t *error, size_t *capacity, size_t *protocol_bytes)
{
	for(;;)
	{
		errno = 0;
		const char *const name = nv_dir_read(dir);
		if(name == NULL)
		{
			return (errno == 0) ? 0 : set_error(error, "read-directory", errno,
					path);
		}
		if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		{
			continue;
		}

		nv_pane_entry_t entry;
		const int build_result = build_entry(path, name, &entry);
		if(build_result != 0)
		{
			entry_free(&entry);
			return set_error(error,
					build_result == BUILD_ENTRY_LIMIT_REACHED
						? "snapshot-too-large"
						: "out-of-memory",
					build_result == BUILD_ENTRY_LIMIT_REACHED ? E2BIG : ENOMEM, path);
		}
		const int append_result = append_entry(snapshot, &entry, capacity,
				protocol_bytes);
		if(append_result != 0)
		{
			entry_free(&entry);
			return set_error(error,
					append_result == APPEND_ENTRY_LIMIT_REACHED
						? "snapshot-too-large"
						: "out-of-memory",
					append_result == APPEND_ENTRY_LIMIT_REACHED ? E2BIG : ENOMEM,
					path);
		}
	}
}

static int64_t
current_time_ms(void)
{
#ifndef _WIN32
	struct timeval now;
	if(gettimeofday(&now, NULL) != 0)
	{
		return 0;
	}
	return milliseconds_from_parts(now.tv_sec, now.tv_usec*1000L);
#else
	const time_t now = time(NULL);
	if(now == (time_t)-1)
	{
		return 0;
	}
	return milliseconds_from_parts(now, 0L);
#endif
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
	size_t protocol_bytes = 0U;
	if(path == NULL || path[0] == '\0')
	{
		set_error(&next_error, "invalid-path", EINVAL, path);
		goto failed;
	}
	if(!raw_string_may_fit_protocol_fields(path))
	{
		set_error(&next_error, "snapshot-too-large", E2BIG, path);
		goto failed;
	}

	nv_dir_t *const dir = nv_dir_open(path);
	if(dir == NULL)
	{
		set_error(&next_error, "open-directory", errno, path);
		goto failed;
	}
	if(initialize_snapshot(path, &next_snapshot, &next_error) != 0 ||
			snapshot_protocol_budget(&next_snapshot, &protocol_bytes) != 0 ||
			scan_directory(dir, path, &next_snapshot, &next_error, &capacity,
					&protocol_bytes) != 0)
	{
		if(next_error.code == NULL)
		{
			set_error(&next_error, "snapshot-too-large", E2BIG, path);
		}
		nv_dir_close(dir);
		goto failed;
	}
	if(nv_dir_close(dir) != 0)
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
