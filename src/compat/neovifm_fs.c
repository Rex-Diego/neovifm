/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "neovifm_fs.h"

#ifdef _WIN32

#include <windows.h> /* FindClose() FindFirstFileW() FindNextFileW() */
#include <winioctl.h> /* IO_REPARSE_TAG_SYMLINK */

#include <errno.h> /* E* errno */
#include <stddef.h> /* size_t */
#include <stdint.h> /* INT64_MAX */
#include <stdlib.h> /* free() malloc() */
#include <string.h> /* memset() */
#include <sys/stat.h> /* S_IF* S_I* struct stat */
#include <time.h> /* time_t */
#include <wchar.h> /* wchar_t wcslen() */

#include "../utils/utf8.h"

struct nv_dir_t
{
	HANDLE handle;
	WIN32_FIND_DATAW entry;
	char *name;
	int has_first_entry;
};

static void set_errno_from_windows_error(DWORD error);
static wchar_t * search_pattern(const char path[]);
static time_t filetime_to_time_t(FILETIME file_time);
static void stat_from_find_data(struct stat *st,
		const WIN32_FIND_DATAW *entry);

static void
set_errno_from_windows_error(DWORD error)
{
	switch(error)
	{
		case ERROR_ACCESS_DENIED:
			errno = EACCES;
			break;
		case ERROR_FILE_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND:
		case ERROR_DIRECTORY:
			errno = ENOENT;
			break;
		case ERROR_INVALID_NAME:
			errno = EINVAL;
			break;
		case ERROR_NOT_ENOUGH_MEMORY:
			errno = ENOMEM;
			break;
		default:
			errno = EIO;
			break;
	}
}

static wchar_t *
search_pattern(const char path[])
{
	wchar_t *const wide_path = utf8_to_utf16(path);
	if(wide_path == NULL)
	{
		errno = ENOMEM;
		return NULL;
	}

	const size_t length = wcslen(wide_path);
	const int needs_separator = length != 0U && wide_path[length - 1U] != L'/' &&
			wide_path[length - 1U] != L'\\';
	wchar_t *const pattern = malloc((length + (size_t)needs_separator + 2U)*
			sizeof(*pattern));
	if(pattern == NULL)
	{
		free(wide_path);
		errno = ENOMEM;
		return NULL;
	}

	memcpy(pattern, wide_path, length*sizeof(*pattern));
	if(needs_separator)
	{
		pattern[length] = L'\\';
	}
	pattern[length + (size_t)needs_separator] = L'*';
	pattern[length + (size_t)needs_separator + 1U] = L'\0';
	free(wide_path);
	return pattern;
}

nv_dir_t *
nv_dir_open(const char path[])
{
	wchar_t *const pattern = search_pattern(path);
	if(pattern == NULL)
	{
		return NULL;
	}

	WIN32_FIND_DATAW entry;
	const HANDLE handle = FindFirstFileW(pattern, &entry);
	free(pattern);
	if(handle == INVALID_HANDLE_VALUE)
	{
		set_errno_from_windows_error(GetLastError());
		return NULL;
	}

	nv_dir_t *const dir = malloc(sizeof(*dir));
	if(dir == NULL)
	{
		(void)FindClose(handle);
		errno = ENOMEM;
		return NULL;
	}

	*dir = (nv_dir_t){ .handle = handle, .entry = entry, .has_first_entry = 1 };
	return dir;
}

const char *
nv_dir_read(nv_dir_t *dir)
{
	if(dir->has_first_entry)
	{
		dir->has_first_entry = 0;
	}
	else if(!FindNextFileW(dir->handle, &dir->entry))
	{
		const DWORD error = GetLastError();
		if(error == ERROR_NO_MORE_FILES)
		{
			errno = 0;
		}
		else
		{
			set_errno_from_windows_error(error);
		}
		return NULL;
	}

	char *const name = utf8_from_utf16(dir->entry.cFileName);
	if(name == NULL)
	{
		errno = ENOMEM;
		return NULL;
	}
	free(dir->name);
	dir->name = name;
	return dir->name;
}

int
nv_dir_close(nv_dir_t *dir)
{
	const int result = FindClose(dir->handle) ? 0 : -1;
	if(result != 0)
	{
		set_errno_from_windows_error(GetLastError());
	}
	free(dir->name);
	free(dir);
	return result;
}

static time_t
filetime_to_time_t(FILETIME file_time)
{
	ULARGE_INTEGER ticks = {
		.LowPart = file_time.dwLowDateTime,
		.HighPart = file_time.dwHighDateTime,
	};
	static const ULONGLONG WINDOWS_EPOCH_OFFSET = 116444736000000000ULL;
	static const ULONGLONG TICKS_PER_SECOND = 10000000ULL;
	if(ticks.QuadPart <= WINDOWS_EPOCH_OFFSET)
	{
		return 0;
	}

	const ULONGLONG seconds = (ticks.QuadPart - WINDOWS_EPOCH_OFFSET)/
		TICKS_PER_SECOND;
	return seconds > (ULONGLONG)INT64_MAX ? (time_t)INT64_MAX :
		(time_t)seconds;
}

static void
stat_from_find_data(struct stat *st, const WIN32_FIND_DATAW *entry)
{
	ULARGE_INTEGER size = {
		.LowPart = entry->nFileSizeLow,
		.HighPart = entry->nFileSizeHigh,
	};
	memset(st, 0, sizeof(*st));
	st->st_mode = (entry->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
		? S_IFDIR
		: S_IFREG;
	st->st_mode |= S_IREAD;
	if((entry->dwFileAttributes & FILE_ATTRIBUTE_READONLY) == 0)
	{
		st->st_mode |= S_IWRITE;
	}
	st->st_size = size.QuadPart > (ULONGLONG)INT64_MAX
		? (off_t)INT64_MAX
		: (off_t)size.QuadPart;
	st->st_atime = filetime_to_time_t(entry->ftLastAccessTime);
	st->st_mtime = filetime_to_time_t(entry->ftLastWriteTime);
	st->st_ctime = filetime_to_time_t(entry->ftCreationTime);
}

int
nv_lstat(const char path[], struct stat *st, int *is_symlink)
{
	if(is_symlink != NULL)
	{
		*is_symlink = 0;
	}
	wchar_t *const wide_path = utf8_to_utf16(path);
	if(wide_path == NULL)
	{
		errno = ENOMEM;
		return -1;
	}

	WIN32_FIND_DATAW entry;
	const HANDLE handle = FindFirstFileW(wide_path, &entry);
	free(wide_path);
	if(handle == INVALID_HANDLE_VALUE)
	{
		set_errno_from_windows_error(GetLastError());
		return -1;
	}
	if(is_symlink != NULL)
	{
		*is_symlink = (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) !=
				0 && entry.dwReserved0 == IO_REPARSE_TAG_SYMLINK;
	}
	stat_from_find_data(st, &entry);
	if(!FindClose(handle))
	{
		set_errno_from_windows_error(GetLastError());
		return -1;
	}
	return 0;
}

#else

#include <dirent.h> /* DIR struct dirent */

#include <errno.h> /* ENOMEM errno */
#include <stdlib.h> /* free() malloc() */

#include "os.h"

struct nv_dir_t
{
	DIR *dir;
};

nv_dir_t *
nv_dir_open(const char path[])
{
	DIR *const native_dir = os_opendir(path);
	if(native_dir == NULL)
	{
		return NULL;
	}

	nv_dir_t *const dir = malloc(sizeof(*dir));
	if(dir == NULL)
	{
		(void)os_closedir(native_dir);
		errno = ENOMEM;
		return NULL;
	}
	dir->dir = native_dir;
	return dir;
}

const char *
nv_dir_read(nv_dir_t *dir)
{
	struct dirent *const entry = os_readdir(dir->dir);
	return entry == NULL ? NULL : entry->d_name;
}

int
nv_dir_close(nv_dir_t *dir)
{
	const int result = os_closedir(dir->dir);
	free(dir);
	return result;
}

int
nv_lstat(const char path[], struct stat *st, int *is_symlink)
{
	if(is_symlink != NULL)
	{
		*is_symlink = 0;
	}
	const int result = os_lstat(path, st);
#ifdef S_ISLNK
	if(result == 0 && is_symlink != NULL)
	{
		*is_symlink = S_ISLNK(st->st_mode);
	}
#endif
	return result;
}

#endif

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
