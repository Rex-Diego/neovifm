/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "neovifm_fs.h"

#include <errno.h> /* E* errno */
#include <stdint.h> /* SIZE_MAX */
#include <stdlib.h> /* free() malloc() */
#include <string.h> /* memcpy() strlen() */

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
	char *path;
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
		case ERROR_ALREADY_EXISTS:
		case ERROR_FILE_EXISTS:
			errno = EEXIST;
			break;
		case ERROR_DIR_NOT_EMPTY:
			errno = ENOTEMPTY;
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

	*dir = (nv_dir_t){ .handle = handle, .entry = entry, .path = strdup(path),
		.has_first_entry = 1 };
	if(dir->path == NULL)
	{
		(void)FindClose(handle);
		free(dir);
		errno = ENOMEM;
		return NULL;
	}
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
	free(dir->path);
	free(dir);
	return result;
}

int
nv_dir_fstat(nv_dir_t *dir, struct stat *st)
{
	if(dir == NULL || st == NULL) { errno = EINVAL; return -1; }
	return nv_lstat(dir->path, st, NULL);
}

int
nv_dir_lstat(nv_dir_t *dir, const char name[], struct stat *st,
		int *is_symlink)
{
	if(dir == NULL || name == NULL || st == NULL) { errno = EINVAL; return -1; }
	const size_t length = strlen(dir->path), name_length = strlen(name);
	const int slash = length != 0U && dir->path[length - 1U] != '/' &&
		dir->path[length - 1U] != '\\';
	char *const path = malloc(length + (size_t)slash + name_length + 1U);
	if(path == NULL) { errno = ENOMEM; return -1; }
	memcpy(path, dir->path, length);
	if(slash) path[length] = '/';
	memcpy(path + length + (size_t)slash, name, name_length + 1U);
	const int result = nv_lstat(path, st, is_symlink);
	free(path);
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
		/* Treat every reparse point as a no-follow entry.  Junctions and mount
		 * points must never be traversed by recursive file actions. */
		*is_symlink = (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
	}
	stat_from_find_data(st, &entry);
	if(!FindClose(handle))
	{
		set_errno_from_windows_error(GetLastError());
		return -1;
	}
	return 0;
}

int
nv_fs_mkdir(const char path[], int mode,
		nv_fs_identity_t destination_directory)
{
	(void)path;
	(void)mode;
	(void)destination_directory;
	errno = ENOTSUP;
	return -1;
}

int
nv_fs_remove(const char path[], nv_fs_identity_t source_directory,
		nv_fs_identity_t source_entry, nv_fs_cancel_hook cancelled,
		void *cancel_arg)
{
	(void)path;
	(void)source_directory;
	(void)source_entry;
	(void)cancelled;
	(void)cancel_arg;
	errno = ENOTSUP;
	return -1;
}

int
nv_fs_copy(const char source[], const char destination[],
		nv_fs_identity_t source_directory,
		nv_fs_identity_t destination_directory, nv_fs_identity_t source_entry,
		nv_fs_cancel_hook cancelled, void *cancel_arg)
{
	(void)source;
	(void)destination;
	(void)source_directory;
	(void)destination_directory;
	(void)source_entry;
	(void)cancelled;
	(void)cancel_arg;
	errno = ENOTSUP;
	return -1;
}

int
nv_fs_move(const char source[], const char destination[],
		nv_fs_identity_t source_directory,
		nv_fs_identity_t destination_directory, nv_fs_identity_t source_entry,
		nv_fs_cancel_hook cancelled, void *cancel_arg)
{
	(void)source;
	(void)destination;
	(void)source_directory;
	(void)destination_directory;
	(void)source_entry;
	(void)cancelled;
	(void)cancel_arg;
	errno = ENOTSUP;
	return -1;
}

#else

#include <dirent.h> /* DIR struct dirent */

#include <fcntl.h> /* O_* open() */
#include <signal.h> /* SIGTERM kill() */
#include <spawn.h> /* posix_spawn() */
#include <stdio.h> /* rename() */
#ifdef __linux__
#include <linux/fs.h> /* RENAME_NOREPLACE */
#include <sys/syscall.h> /* SYS_renameat2 */
#endif
#include <sys/wait.h> /* waitpid() */
#include <time.h> /* nanosleep() */
#include <unistd.h> /* close() link() read() readlink() symlink() unlink() write() */

#include "os.h"

extern char **environ;

static nv_fs_test_before_atomic_hook test_before_atomic_hook;
static int test_cross_device_move;

void
nv_fs_test_set_before_atomic_hook(nv_fs_test_before_atomic_hook hook)
{
	test_before_atomic_hook = hook;
}

void
nv_fs_test_force_cross_device_move(int enabled)
{
	test_cross_device_move = enabled;
}

#if defined(__APPLE__) || defined(__linux__)
static void
run_test_before_atomic_hook(const char path[])
{
	if(test_before_atomic_hook != NULL) test_before_atomic_hook(path);
}
#endif

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
nv_dir_fstat(nv_dir_t *dir, struct stat *st)
{
	if(dir == NULL || st == NULL) { errno = EINVAL; return -1; }
	return fstat(dirfd(dir->dir), st);
}

int
nv_dir_lstat(nv_dir_t *dir, const char name[], struct stat *st,
		int *is_symlink)
{
	if(dir == NULL || name == NULL || st == NULL) { errno = EINVAL; return -1; }
	if(is_symlink != NULL) *is_symlink = 0;
	const int result = fstatat(dirfd(dir->dir), name, st, AT_SYMLINK_NOFOLLOW);
	if(result == 0 && is_symlink != NULL) *is_symlink = S_ISLNK(st->st_mode);
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

typedef struct
{
	int fd;
	char *path;
	char *name;
} nv_parent_entry_t;

static void
parent_entry_free(nv_parent_entry_t *entry)
{
	if(entry->fd >= 0) close(entry->fd);
	free(entry->path);
	free(entry->name);
	entry->fd = -1;
	entry->path = NULL;
	entry->name = NULL;
}

static int
open_parent_entry(const char path[], nv_parent_entry_t *entry)
{
	*entry = (nv_parent_entry_t){ .fd = -1 };
	if(path == NULL || path[0] == '\0') { errno = EINVAL; return -1; }
	char *const copy = strdup(path);
	if(copy == NULL) { errno = ENOMEM; return -1; }
	size_t length = strlen(copy);
	while(length > 1U && copy[length - 1U] == '/') copy[--length] = '\0';
	char *const slash = strrchr(copy, '/');
	const char *parent = ".";
	const char *name = copy;
	if(slash != NULL)
	{
		name = slash + 1;
		if(slash == copy)
		{
			parent = "/";
		}
		else
		{
			*slash = '\0';
			parent = copy;
		}
	}
	if(name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
	{
		free(copy);
		errno = EINVAL;
		return -1;
	}
	entry->path = strdup(parent);
	entry->name = strdup(name);
	entry->fd = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	free(copy);
	if(entry->path == NULL || entry->name == NULL || entry->fd < 0)
	{
		const int saved = entry->path == NULL || entry->name == NULL ? ENOMEM : errno;
		parent_entry_free(entry);
		errno = saved;
		return -1;
	}
	return 0;
}

static int
same_object(const struct stat *st, nv_fs_identity_t identity)
{
	return (uint64_t)st->st_dev == identity.device &&
			(uint64_t)st->st_ino == identity.inode;
}

static uint64_t
stat_ctime_ns(const struct stat *st)
{
#if defined(__APPLE__)
	const time_t seconds = st->st_ctimespec.tv_sec;
	const long nanoseconds = st->st_ctimespec.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_CTIM)
	const time_t seconds = st->st_ctim.tv_sec;
	const long nanoseconds = st->st_ctim.tv_nsec;
#else
	const time_t seconds = st->st_ctime;
	const long nanoseconds = 0L;
#endif
	if(seconds < 0) return 0U;
	const uint64_t value = (uint64_t)seconds;
	if(value > (UINT64_MAX - (uint64_t)nanoseconds)/1000000000U)
		return UINT64_MAX;
	return value*1000000000U + (uint64_t)nanoseconds;
}

static int
identity_matches(const struct stat *st, nv_fs_identity_t identity)
{
	return same_object(st, identity) && stat_ctime_ns(st) == identity.ctime_unix_ns;
}

static int
parent_matches(const nv_parent_entry_t *entry, nv_fs_identity_t identity)
{
	struct stat st;
	if(fstat(entry->fd, &st) != 0) return 0;
	/* The snapshot ctime guards command admission.  File actions themselves
	 * change a directory's ctime, so subsequent targets must validate the
	 * held directory object without treating their own prior work as stale. */
	if(same_object(&st, identity)) return 1;
	errno = ESTALE;
	return 0;
}

static int
is_cancelled(nv_fs_cancel_hook cancelled, void *cancel_arg)
{
	if(cancelled != NULL && cancelled(cancel_arg))
	{
		errno = ECANCELED;
		return 1;
	}
	return 0;
}

static int
copy_file_data(int input, int output, nv_fs_cancel_hook cancelled,
		void *cancel_arg)
{
	char buffer[64U*1024U];
	for(;;)
	{
		if(is_cancelled(cancelled, cancel_arg)) return -1;
		ssize_t count;
		do { count = read(input, buffer, sizeof(buffer)); }
		while(count < 0 && errno == EINTR);
		if(count == 0) return 0;
		if(count < 0) return -1;
		ssize_t written = 0;
		while(written < count)
		{
			ssize_t result;
			do { result = write(output, buffer + written,
					(size_t)(count - written)); }
			while(result < 0 && errno == EINTR);
			if(result <= 0)
			{
				if(result == 0) errno = EIO;
				return -1;
			}
			written += result;
		}
	}
}

static int
copy_symbolic_link_at(int source_fd, const char source_name[], int destination_fd,
		const char destination_name[], off_t size)
{
	size_t capacity = size > 0 && (uintmax_t)size < SIZE_MAX - 2U ?
		(size_t)size + 2U : 4096U;
	for(;;)
	{
		char *const target = malloc(capacity);
		if(target == NULL) { errno = ENOMEM; return -1; }
		const ssize_t length = readlinkat(source_fd, source_name, target,
				capacity - 1U);
		if(length >= 0 && (size_t)length < capacity - 1U)
		{
			target[length] = '\0';
			const int result = symlinkat(target, destination_fd,
					destination_name);
			free(target);
			return result;
		}
		const int saved = length < 0 ? errno : ENAMETOOLONG;
		free(target);
		if(length < 0 || capacity > SIZE_MAX/2U)
		{
			errno = saved;
			return -1;
		}
		capacity *= 2U;
	}
}

static int
directory_is_ancestor(int ancestor_fd, int directory_fd)
{
	struct stat ancestor;
	if(fstat(ancestor_fd, &ancestor) != 0) return -1;
	int current = dup(directory_fd);
	if(current < 0) return -1;
	for(;;)
	{
		struct stat here;
		if(fstat(current, &here) != 0)
		{
			const int saved = errno;
			close(current);
			errno = saved;
			return -1;
		}
		if(here.st_dev == ancestor.st_dev && here.st_ino == ancestor.st_ino)
		{
			close(current);
			return 1;
		}
		const int parent = openat(current, "..",
				O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if(parent < 0)
		{
			const int saved = errno;
			close(current);
			errno = saved;
			return -1;
		}
		struct stat above;
		if(fstat(parent, &above) != 0)
		{
			const int saved = errno;
			close(parent);
			close(current);
			errno = saved;
			return -1;
		}
		if(above.st_dev == here.st_dev && above.st_ino == here.st_ino)
		{
			close(parent);
			close(current);
			return 0;
		}
		close(current);
		current = parent;
	}
}

static int
stat_content_same(const struct stat *before, const struct stat *after)
{
	if(before->st_size != after->st_size || before->st_mtime != after->st_mtime ||
			stat_ctime_ns(before) != stat_ctime_ns(after))
		return 0;
#ifdef __APPLE__
	return before->st_mtimespec.tv_nsec == after->st_mtimespec.tv_nsec;
#else
	return 1;
#endif
}

static int
copy_entry_at(int source_fd, const char source_name[], int destination_fd,
		const char destination_name[], const nv_fs_identity_t *expected,
		nv_fs_cancel_hook cancelled, void *cancel_arg)
{
	if(is_cancelled(cancelled, cancel_arg)) return -1;
	struct stat st;
	if(fstatat(source_fd, source_name, &st, AT_SYMLINK_NOFOLLOW) != 0) return -1;
	if(expected != NULL && !identity_matches(&st, *expected))
	{
		errno = ESTALE;
		return -1;
	}
	if(S_ISLNK(st.st_mode))
	{
		if(copy_symbolic_link_at(source_fd, source_name, destination_fd,
				destination_name, st.st_size) != 0) return -1;
		struct stat current;
		const int current_result = fstatat(source_fd, source_name, &current,
				AT_SYMLINK_NOFOLLOW);
		if(current_result == 0 &&
				same_object(&current, (nv_fs_identity_t){
					.device = (uint64_t)st.st_dev, .inode = (uint64_t)st.st_ino }))
		{
			return 0;
		}
		const int saved = current_result == 0 ? ESTALE : errno;
		errno = saved;
		return -1;
	}
	if(S_ISREG(st.st_mode))
	{
		const int input = openat(source_fd, source_name,
				O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
		if(input < 0) return -1;
		struct stat opened;
		if(fstat(input, &opened) != 0)
		{
			const int saved = errno;
			close(input);
			errno = saved;
			return -1;
		}
		if(!S_ISREG(opened.st_mode) || opened.st_dev != st.st_dev ||
				opened.st_ino != st.st_ino)
		{
			close(input);
			errno = ESTALE;
			return -1;
		}
		const int output = openat(destination_fd, destination_name,
				O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
				opened.st_mode & 0777);
		if(output < 0)
		{
			const int saved = errno;
			close(input);
			errno = saved;
			return -1;
		}
		int result = copy_file_data(input, output, cancelled, cancel_arg);
		int saved = errno;
		struct stat finished;
		if(result == 0 && (fstat(input, &finished) != 0 ||
				!stat_content_same(&opened, &finished)))
		{
			result = -1;
			saved = errno == 0 ? ESTALE : errno;
		}
		if(close(input) != 0 && result == 0) { result = -1; saved = errno; }
		if(close(output) != 0 && result == 0) { result = -1; saved = errno; }
		if(result != 0)
		{
			errno = saved == 0 ? EIO : saved;
		}
		return result;
	}
	if(!S_ISDIR(st.st_mode)) { errno = ENOTSUP; return -1; }

	const int input = openat(source_fd, source_name,
			O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(input < 0) return -1;
	struct stat opened;
	if(fstat(input, &opened) != 0)
	{
		const int saved = errno;
		close(input);
		errno = saved;
		return -1;
	}
	if(opened.st_dev != st.st_dev || opened.st_ino != st.st_ino)
	{
		close(input);
		errno = ESTALE;
		return -1;
	}
	if(mkdirat(destination_fd, destination_name, opened.st_mode & 0777) != 0)
	{
		const int saved = errno;
		close(input);
		errno = saved;
		return -1;
	}
	const int output = openat(destination_fd, destination_name,
			O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(output < 0)
	{
		const int saved = errno;
		close(input);
		errno = saved;
		return -1;
	}
	const int traversal_fd = dup(input);
	DIR *const directory = traversal_fd < 0 ? NULL : fdopendir(traversal_fd);
	if(directory == NULL)
	{
		const int saved = errno;
		if(traversal_fd >= 0) close(traversal_fd);
		close(output);
		close(input);
		errno = saved;
		return -1;
	}
	int result = 0;
	int saved = 0;
	for(;;)
	{
		if(is_cancelled(cancelled, cancel_arg))
		{
			result = -1;
			saved = errno;
			break;
		}
		errno = 0;
		struct dirent *const entry = readdir(directory);
		if(entry == NULL)
		{
			if(errno != 0) { result = -1; saved = errno; }
			break;
		}
		if(strcmp(entry->d_name, ".") == 0 ||
				strcmp(entry->d_name, "..") == 0) continue;
		if(copy_entry_at(input, entry->d_name, output, entry->d_name, NULL,
				cancelled, cancel_arg) != 0)
		{
			result = -1;
			saved = errno;
			break;
		}
	}
	struct stat finished;
	if(result == 0 && (fstat(input, &finished) != 0 ||
			!stat_content_same(&opened, &finished)))
	{
		result = -1;
		saved = errno == 0 ? ESTALE : errno;
	}
	if(closedir(directory) != 0 && result == 0) { result = -1; saved = errno; }
	if(close(output) != 0 && result == 0) { result = -1; saved = errno; }
	if(close(input) != 0 && result == 0) { result = -1; saved = errno; }
	if(result != 0) errno = saved == 0 ? EIO : saved;
	return result;
}

#ifdef __linux__
static int
rename_no_replace(int source_fd, const char source_name[], int destination_fd,
		const char destination_name[])
{
#ifdef SYS_renameat2
	return (int)syscall(SYS_renameat2, source_fd, source_name, destination_fd,
			destination_name, RENAME_NOREPLACE);
#else
	(void)source_fd;
	(void)source_name;
	(void)destination_fd;
	(void)destination_name;
	errno = ENOTSUP;
	return -1;
#endif
}
#endif

#if defined(__APPLE__) || defined(__linux__)
static char *
join_parent_name(const char parent[], const char name[])
{
	const size_t parent_length = strlen(parent), name_length = strlen(name);
	const int separator = parent_length != 0U && parent[parent_length - 1U] != '/';
	if(parent_length > SIZE_MAX - name_length - (size_t)separator - 1U)
	{
		errno = ENOMEM;
		return NULL;
	}
	char *const path = malloc(parent_length + (size_t)separator + name_length + 1U);
	if(path == NULL) { errno = ENOMEM; return NULL; }
	memcpy(path, parent, parent_length);
	if(separator) path[parent_length] = '/';
	memcpy(path + parent_length + (size_t)separator, name, name_length + 1U);
	return path;
}

static int
random_quarantine_name(char name[], size_t size)
{
	unsigned char bytes[16];
	const int random_fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if(random_fd < 0) return -1;
	size_t used = 0U;
	while(used < sizeof(bytes))
	{
		const ssize_t count = read(random_fd, bytes + used, sizeof(bytes) - used);
		if(count < 0 && errno == EINTR) continue;
		if(count <= 0)
		{
			const int saved = count < 0 ? errno : EIO;
			close(random_fd);
			errno = saved;
			return -1;
		}
		used += (size_t)count;
	}
	if(close(random_fd) != 0) return -1;
	static const char hex[] = "0123456789abcdef";
	if(size < sizeof(".neovifm-trash-") + 2U*sizeof(bytes))
	{
		errno = ENAMETOOLONG;
		return -1;
	}
	memcpy(name, ".neovifm-trash-", sizeof(".neovifm-trash-") - 1U);
	size_t offset = sizeof(".neovifm-trash-") - 1U;
	for(size_t i = 0U; i < sizeof(bytes); ++i)
	{
		name[offset++] = hex[bytes[i] >> 4U];
		name[offset++] = hex[bytes[i] & 0x0fU];
	}
	name[offset] = '\0';
	return 0;
}

static int
create_quarantine_directory(int parent_fd, char name[], size_t size,
		int *directory_fd)
{
	for(int attempt = 0; attempt < 8; ++attempt)
	{
		if(random_quarantine_name(name, size) != 0) return -1;
		if(mkdirat(parent_fd, name, 0700) == 0)
		{
			*directory_fd = openat(parent_fd, name,
					O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
			if(*directory_fd >= 0) return 0;
			const int saved = errno;
			(void)unlinkat(parent_fd, name, AT_REMOVEDIR);
			errno = saved;
			return -1;
		}
		if(errno != EEXIST) return -1;
	}
	errno = EEXIST;
	return -1;
}

static void
restore_no_replace(int source_fd, const char source_name[], int parent_fd,
		const char original_name[], const struct stat *moved)
{
	struct stat current;
	if(fstatat(source_fd, source_name, &current, AT_SYMLINK_NOFOLLOW) == 0 &&
		current.st_dev == moved->st_dev && current.st_ino == moved->st_ino)
	{
#ifdef __APPLE__
		(void)renameatx_np(source_fd, source_name, parent_fd, original_name,
				RENAME_EXCL);
#else
		(void)rename_no_replace(source_fd, source_name, parent_fd, original_name);
#endif
	}
}

static int
run_trash(const char path[], nv_fs_cancel_hook cancelled, void *cancel_arg)
{
	pid_t child;
	const char *const configured = getenv("NEOVIFM_TRASH_EXECUTABLE");
	const char *const executable = configured == NULL || configured[0] != '/' ?
#ifdef __APPLE__
		"/usr/bin/trash" : configured;
	char *argv[] = { (char *)executable, (char *)path, NULL };
#else
		"/usr/bin/gio" : configured;
	char *argv[] = { (char *)executable, NULL, (char *)path, NULL };
	if(configured == NULL || configured[0] != '/') argv[1] = (char *)"trash";
#endif
	posix_spawn_file_actions_t actions;
	int spawn_error = posix_spawn_file_actions_init(&actions);
	const int actions_initialized = spawn_error == 0;
	if(spawn_error == 0)
		spawn_error = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO,
				"/dev/null", O_RDONLY, 0);
	if(spawn_error == 0)
		spawn_error = posix_spawn_file_actions_adddup2(&actions, STDERR_FILENO,
				STDOUT_FILENO);
	if(spawn_error == 0)
		spawn_error = posix_spawn(&child, argv[0], &actions, NULL, argv, environ);
	if(actions_initialized) (void)posix_spawn_file_actions_destroy(&actions);
	if(spawn_error != 0) { errno = spawn_error; return -1; }
	for(;;)
	{
		int status;
		const pid_t result = waitpid(child, &status, WNOHANG);
		if(result == child)
		{
			if(WIFEXITED(status) && WEXITSTATUS(status) == 0) return 0;
			errno = EIO;
			return -1;
		}
		if(result < 0 && errno != EINTR) return -1;
		if(is_cancelled(cancelled, cancel_arg))
		{
			(void)kill(child, SIGTERM);
			for(int attempt = 0; attempt < 50; ++attempt)
			{
				const pid_t waited = waitpid(child, &status, WNOHANG);
				if(waited == child) { errno = ECANCELED; return -1; }
				if(waited < 0 && errno != EINTR) return -1;
				const struct timespec delay = { .tv_sec = 0,
					.tv_nsec = 10L*1000L*1000L };
				(void)nanosleep(&delay, NULL);
			}
			(void)kill(child, SIGKILL);
			while(waitpid(child, &status, 0) < 0 && errno == EINTR) { }
			errno = ECANCELED;
			return -1;
		}
		const struct timespec delay = { .tv_sec = 0, .tv_nsec = 10L*1000L*1000L };
		(void)nanosleep(&delay, NULL);
	}
}
#endif

static int
copy_between_entries(const nv_parent_entry_t *from,
		const nv_parent_entry_t *to, nv_fs_identity_t source_directory,
		nv_fs_identity_t destination_directory, nv_fs_identity_t source_entry,
		nv_fs_cancel_hook cancelled, void *cancel_arg)
{
	if(!parent_matches(from, source_directory) ||
			!parent_matches(to, destination_directory)) return -1;
	struct stat source_stat;
	if(fstatat(from->fd, from->name, &source_stat, AT_SYMLINK_NOFOLLOW) != 0)
	{
		return -1;
	}
	if(!identity_matches(&source_stat, source_entry))
	{
		errno = ESTALE;
		return -1;
	}
	if(S_ISDIR(source_stat.st_mode))
	{
		const int source_fd = openat(from->fd, from->name,
				O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if(source_fd < 0) return -1;
		const int contained = directory_is_ancestor(source_fd, to->fd);
		const int saved = errno;
		close(source_fd);
		if(contained != 0)
		{
			errno = contained > 0 ? EINVAL : saved;
			return -1;
		}
	}
	return copy_entry_at(from->fd, from->name, to->fd, to->name,
			&source_entry, cancelled, cancel_arg);
}

int
nv_fs_mkdir(const char path[], int mode,
		nv_fs_identity_t destination_directory)
{
	nv_parent_entry_t entry;
	if(open_parent_entry(path, &entry) != 0) return -1;
	const int result = parent_matches(&entry, destination_directory) ?
		mkdirat(entry.fd, entry.name, (mode_t)mode) : -1;
	const int saved = errno;
	parent_entry_free(&entry);
	if(result != 0) errno = saved;
	return result;
}

int
nv_fs_copy(const char source[], const char destination[],
		nv_fs_identity_t source_directory,
		nv_fs_identity_t destination_directory, nv_fs_identity_t source_entry,
		nv_fs_cancel_hook cancelled, void *cancel_arg)
{
	nv_parent_entry_t from, to;
	if(open_parent_entry(source, &from) != 0) return -1;
	if(open_parent_entry(destination, &to) != 0)
	{
		const int saved = errno;
		parent_entry_free(&from);
		errno = saved;
		return -1;
	}
	const int result = copy_between_entries(&from, &to, source_directory,
			destination_directory, source_entry, cancelled, cancel_arg);
	const int saved = errno;
	parent_entry_free(&to);
	parent_entry_free(&from);
	if(result != 0) errno = saved;
	return result;
}

int
nv_fs_remove(const char path[], nv_fs_identity_t source_directory,
		nv_fs_identity_t source_entry, nv_fs_cancel_hook cancelled,
		void *cancel_arg)
{
	nv_parent_entry_t entry;
	if(open_parent_entry(path, &entry) != 0) return -1;
	int result = -1;
	int saved = 0;
	struct stat current;
	if(!parent_matches(&entry, source_directory) ||
			fstatat(entry.fd, entry.name, &current, AT_SYMLINK_NOFOLLOW) != 0)
	{
		saved = errno;
		goto done;
	}
	if(!identity_matches(&current, source_entry))
	{
		saved = ESTALE;
		goto done;
	}
	if(is_cancelled(cancelled, cancel_arg))
	{
		saved = errno;
		goto done;
	}
#if defined(__APPLE__) || defined(__linux__)
	char quarantine[64];
	int quarantine_fd = -1;
	if(create_quarantine_directory(entry.fd, quarantine, sizeof(quarantine),
			&quarantine_fd) != 0)
	{
		saved = errno;
		goto done;
	}
	run_test_before_atomic_hook(path);
#ifdef __APPLE__
	if(renameatx_np(entry.fd, entry.name, quarantine_fd, entry.name,
			RENAME_EXCL) != 0)
#else
	if(rename_no_replace(entry.fd, entry.name, quarantine_fd, entry.name) != 0)
#endif
	{
		saved = errno;
		close(quarantine_fd);
		(void)unlinkat(entry.fd, quarantine, AT_REMOVEDIR);
		goto done;
	}
	struct stat quarantined;
	const int quarantined_result = fstatat(quarantine_fd, entry.name,
			&quarantined, AT_SYMLINK_NOFOLLOW);
	if(quarantined_result != 0 ||
			!same_object(&quarantined, source_entry))
	{
		saved = errno == 0 ? ESTALE : errno;
		if(quarantined_result == 0)
			restore_no_replace(quarantine_fd, entry.name, entry.fd, entry.name,
					&quarantined);
		close(quarantine_fd);
		(void)unlinkat(entry.fd, quarantine, AT_REMOVEDIR);
		goto done;
	}
	char *const quarantine_path = join_parent_name(entry.path, quarantine);
	char *const trash_path = quarantine_path == NULL ? NULL :
		join_parent_name(quarantine_path, entry.name);
	free(quarantine_path);
	if(trash_path == NULL)
	{
		saved = errno;
		restore_no_replace(quarantine_fd, entry.name, entry.fd, entry.name,
				&quarantined);
		close(quarantine_fd);
		(void)unlinkat(entry.fd, quarantine, AT_REMOVEDIR);
		goto done;
	}
	result = run_trash(trash_path, cancelled, cancel_arg);
	saved = errno;
	free(trash_path);
	if(result != 0)
	{
		struct stat remaining;
		if(fstatat(quarantine_fd, entry.name, &remaining, AT_SYMLINK_NOFOLLOW) != 0 &&
				errno == ENOENT)
		{
			result = 0;
		}
		else
		{
			restore_no_replace(quarantine_fd, entry.name, entry.fd, entry.name,
					&quarantined);
		}
	}
	close(quarantine_fd);
	(void)unlinkat(entry.fd, quarantine, AT_REMOVEDIR);
#else
	(void)cancelled;
	(void)cancel_arg;
	saved = ENOTSUP;
#endif

done:
	parent_entry_free(&entry);
	if(result != 0) errno = saved == 0 ? EIO : saved;
	return result;
}

int
nv_fs_move(const char source[], const char destination[],
		nv_fs_identity_t source_directory,
		nv_fs_identity_t destination_directory, nv_fs_identity_t source_entry,
		nv_fs_cancel_hook cancelled, void *cancel_arg)
{
	nv_parent_entry_t from, to;
	if(open_parent_entry(source, &from) != 0) return -1;
	if(open_parent_entry(destination, &to) != 0)
	{
		const int saved = errno;
		parent_entry_free(&from);
		errno = saved;
		return -1;
	}
	int result = -1;
	int saved = 0;
	struct stat current;
	if(!parent_matches(&from, source_directory) ||
			!parent_matches(&to, destination_directory) ||
			fstatat(from.fd, from.name, &current, AT_SYMLINK_NOFOLLOW) != 0)
	{
		saved = errno;
		goto done;
	}
	if(!identity_matches(&current, source_entry))
	{
		saved = ESTALE;
		goto done;
	}
	if(is_cancelled(cancelled, cancel_arg))
	{
		saved = errno;
		goto done;
	}
	if(S_ISDIR(current.st_mode))
	{
		const int source_fd = openat(from.fd, from.name,
				O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		const int contained = source_fd < 0 ? -1 :
			directory_is_ancestor(source_fd, to.fd);
		saved = errno;
		if(source_fd >= 0) close(source_fd);
		if(contained != 0)
		{
			if(contained > 0) saved = EINVAL;
			goto done;
		}
	}
#if defined(__APPLE__) || defined(__linux__)
	run_test_before_atomic_hook(source);
#ifdef __APPLE__
	if(test_cross_device_move)
	{
		saved = EXDEV;
		goto done;
	}
	if(renameatx_np(from.fd, from.name, to.fd, to.name, RENAME_EXCL) != 0)
#else
	if(test_cross_device_move)
	{
		saved = EXDEV;
		goto done;
	}
	if(rename_no_replace(from.fd, from.name, to.fd, to.name) != 0)
#endif
	{
		saved = errno;
		goto done;
	}
	struct stat moved;
	const int moved_result = fstatat(to.fd, to.name, &moved,
			AT_SYMLINK_NOFOLLOW);
	if(moved_result == 0 &&
			same_object(&moved, source_entry))
	{
		result = 0;
	}
	else
	{
		saved = moved_result == 0 ? ESTALE : errno;
		if(moved_result == 0)
			restore_no_replace(to.fd, to.name, from.fd, from.name, &moved);
	}
	#else
	saved = ENOTSUP;
	#endif

done:
	parent_entry_free(&to);
	parent_entry_free(&from);
	if(result != 0) errno = saved == 0 ? EIO : saved;
	return result;
}

#endif

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
