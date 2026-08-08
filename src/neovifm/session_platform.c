/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "session_platform.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
# include <sys/select.h>
# include <sys/stat.h>
# include <unistd.h>
#else
# include <io.h>
# include <sys/stat.h>
# include <wchar.h>
# include <windows.h>

# include "../utils/utf8.h"
#endif

static char * environment_value(const char name[]);
static char * join_path(const char base[], const char suffix[]);

static char *
environment_value(const char name[])
{
#ifndef _WIN32
	const char *const value = getenv(name);
	return value == NULL || value[0] == '\0' ? NULL : strdup(value);
#else
	wchar_t *const wide_name = utf8_to_utf16(name);
	if(wide_name == NULL) return NULL;
	const wchar_t *const wide_value = _wgetenv(wide_name);
	free(wide_name);
	if(wide_value == NULL || wide_value[0] == L'\0') return NULL;
	return utf8_from_utf16(wide_value);
#endif
}

static char *
join_path(const char base[], const char suffix[])
{
	const size_t base_length = strlen(base);
	const size_t suffix_length = strlen(suffix);
	const int needs_separator = base_length != 0U && base[base_length - 1U] != '/' &&
		base[base_length - 1U] != '\\';
	char *const result = malloc(base_length + (size_t)needs_separator +
			suffix_length + 1U);
	if(result == NULL) return NULL;
	if(snprintf(result, base_length + (size_t)needs_separator + suffix_length + 1U,
			"%s%s%s", base, needs_separator ? "/" : "", suffix) < 0)
	{
		free(result);
		return NULL;
	}
	return result;
}

char *
nv_session_state_path(void)
{
	char *const configured = environment_value("NEOVIFM_SESSION_STATE");
	if(configured != NULL) return configured;

#ifdef _WIN32
	char *base = environment_value("LOCALAPPDATA");
	if(base != NULL)
	{
		char *const result = join_path(base, "neovifm/session.json");
		free(base);
		return result;
	}
	base = environment_value("USERPROFILE");
	if(base == NULL) return NULL;
	char *const result = join_path(base, "AppData/Local/neovifm/session.json");
	free(base);
	return result;
#else
	char *base = environment_value("XDG_STATE_HOME");
	if(base != NULL)
	{
		char *const result = join_path(base, "neovifm/session.json");
		free(base);
		return result;
	}
	base = environment_value("HOME");
	if(base == NULL) return NULL;
	char *const result = join_path(base, ".local/state/neovifm/session.json");
	free(base);
	return result;
#endif
}

#ifdef _WIN32
static int
wide_directory_exists(const wchar_t path[])
{
	const DWORD attributes = GetFileAttributesW(path);
	return attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
}

static int
create_wide_directory(const wchar_t path[])
{
	if(path[0] == L'\0' || (path[1] == L':' && path[2] == L'\0')) return 0;
	if(CreateDirectoryW(path, NULL) != 0) return 0;
	if(GetLastError() == ERROR_ALREADY_EXISTS && wide_directory_exists(path)) return 0;
	errno = EIO;
	return -1;
}

static size_t
wide_root_length(const wchar_t path[])
{
	if(path[0] != L'\0' && path[1] == L':')
		return path[2] == L'\\' ? 3U : 2U;
	if(path[0] != L'\\' || path[1] != L'\\') return 0U;
	const wchar_t *separator = wcschr(path + 2, L'\\');
	if(separator == NULL) return wcslen(path);
	separator = wcschr(separator + 1, L'\\');
	return separator == NULL ? wcslen(path) : (size_t)(separator - path + 1);
}
#endif

int
nv_session_ensure_parent_directory(const char path[])
{
	if(path == NULL || path[0] == '\0') return -1;
	char *const directory = strdup(path);
	if(directory == NULL) return -1;
	char *slash = strrchr(directory, '/');
	char *const backslash = strrchr(directory, '\\');
	if(backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
	if(slash == NULL)
	{
		free(directory);
		return 0;
	}
	*slash = '\0';
	if(directory[0] == '\0')
	{
		free(directory);
		return 0;
	}

#ifndef _WIN32
	char *cursor = directory + (directory[0] == '/' ? 1 : 0);
	for(; *cursor != '\0'; ++cursor)
	{
		if(*cursor != '/') continue;
		*cursor = '\0';
		if(directory[0] != '\0' && mkdir(directory, 0700) != 0 && errno != EEXIST)
		{
			free(directory);
			return -1;
		}
		*cursor = '/';
	}
	const int result = mkdir(directory, 0700) == 0 || errno == EEXIST ? 0 : -1;
	free(directory);
	return result;
#else
	wchar_t *const wide = utf8_to_utf16(directory);
	free(directory);
	if(wide == NULL) return -1;
	for(wchar_t *cursor = wide; *cursor != L'\0'; ++cursor)
		if(*cursor == L'/') *cursor = L'\\';
	const size_t root_length = wide_root_length(wide);
	int result = 0;
	for(wchar_t *cursor = wide + root_length; *cursor != L'\0'; ++cursor)
	{
		if(*cursor != L'\\') continue;
		*cursor = L'\0';
		if(create_wide_directory(wide) != 0) { result = -1; break; }
		*cursor = L'\\';
	}
	if(result == 0) result = create_wide_directory(wide);
	free(wide);
	return result;
#endif
}

FILE *
nv_session_fopen(const char path[], const char mode[])
{
#ifndef _WIN32
	return fopen(path, mode);
#else
	wchar_t *const wide_path = utf8_to_utf16(path);
	wchar_t *const wide_mode = utf8_to_utf16(mode);
	FILE *const result = wide_path == NULL || wide_mode == NULL ? NULL :
		_wfopen(wide_path, wide_mode);
	free(wide_mode);
	free(wide_path);
	return result;
#endif
}

int
nv_session_open_temporary(const char path[], char **temporary)
{
	if(path == NULL || temporary == NULL) return -1;
	*temporary = NULL;
	const size_t path_length = strlen(path);
#ifndef _WIN32
	const char suffix[] = ".tmp-XXXXXX";
	char *const candidate = malloc(path_length + sizeof(suffix));
	if(candidate == NULL) return -1;
	if(snprintf(candidate, path_length + sizeof(suffix), "%s%s", path, suffix) < 0)
	{
		free(candidate);
		return -1;
	}
	const int descriptor = mkstemp(candidate);
	if(descriptor < 0) { free(candidate); return -1; }
	*temporary = candidate;
	return descriptor;
#else
	const size_t capacity = path_length + 48U;
	char *const candidate = malloc(capacity);
	if(candidate == NULL) return -1;
	for(unsigned int attempt = 0U; attempt != 128U; ++attempt)
	{
		if(snprintf(candidate, capacity, "%s.tmp-%08lx-%08lx-%03u", path,
				(unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount(),
				attempt) < 0) break;
		wchar_t *const wide = utf8_to_utf16(candidate);
		if(wide == NULL) break;
		const int descriptor = _wopen(wide,
				_O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
				_S_IREAD | _S_IWRITE);
		free(wide);
		if(descriptor >= 0)
		{
			*temporary = candidate;
			return descriptor;
		}
		if(errno != EEXIST) break;
	}
	free(candidate);
	return -1;
#endif
}

int
nv_session_replace_file(const char temporary[], const char path[])
{
#ifndef _WIN32
	return rename(temporary, path);
#else
	wchar_t *const wide_temporary = utf8_to_utf16(temporary);
	wchar_t *const wide_path = utf8_to_utf16(path);
	const int result = wide_temporary != NULL && wide_path != NULL &&
		MoveFileExW(wide_temporary, wide_path,
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0 ? 0 : -1;
	free(wide_path);
	free(wide_temporary);
	if(result != 0) errno = EIO;
	return result;
#endif
}

int
nv_session_remove_file(const char path[])
{
#ifndef _WIN32
	return remove(path);
#else
	wchar_t *const wide = utf8_to_utf16(path);
	const int result = wide == NULL ? -1 : _wremove(wide);
	free(wide);
	return result;
#endif
}

int
nv_session_poll_stdin(int *ready)
{
	if(ready == NULL) return -1;
#ifndef _WIN32
	fd_set read_fds;
	FD_ZERO(&read_fds);
	FD_SET(STDIN_FILENO, &read_fds);
	struct timeval timeout = { .tv_sec = 0, .tv_usec = 12000 };
	const int result = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout);
	if(result < 0) return errno == EINTR ? 0 : -1;
	*ready = result != 0;
	return 0;
#else
	const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
	if(input == NULL || input == INVALID_HANDLE_VALUE) return -1;
	if(GetFileType(input) != FILE_TYPE_PIPE)
	{
		*ready = 1;
		return 0;
	}
	DWORD available = 0U;
	if(PeekNamedPipe(input, NULL, 0U, NULL, &available, NULL) == 0)
	{
		const DWORD error = GetLastError();
		if(error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED)
		{
			*ready = 1;
			return 0;
		}
		errno = EIO;
		return -1;
	}
	*ready = available != 0U;
	if(!*ready) Sleep(12U);
	return 0;
#endif
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
