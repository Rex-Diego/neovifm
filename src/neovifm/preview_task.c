/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef _WIN32
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#endif

#include "preview_task.h"
#include "../compat/pthread.h"

typedef struct nv_preview_task_t nv_preview_task_t;
typedef struct nv_preview_event_node_t nv_preview_event_node_t;

struct nv_preview_task_t
{
	uint64_t id;
	nv_preview_request_t request;
	char *cwd;
	char *path;
	char *cwd_hex;
	char *path_hex;
	int cancelled;
	int terminal_emitted;
	uint64_t deadline_ms;
	nv_preview_task_t *next;
};

struct nv_preview_event_node_t
{
	nv_preview_event_t event;
	nv_preview_event_node_t *next;
};

struct nv_preview_queue_t
{
	pthread_mutex_t mutex;
	pthread_cond_t ready;
	pthread_t worker;
	int started;
	int stopping;
	uint64_t next_id;
	nv_preview_task_t *tasks_head;
	nv_preview_task_t *tasks_tail;
	nv_preview_task_t *running_task;
	nv_preview_event_node_t *events_head;
	nv_preview_event_node_t *events_tail;
};

static int queue_start_worker(nv_preview_queue_t *queue);
static void *preview_worker(void *data);
static int request_valid(const nv_preview_request_t *request);
static int hex_decode(const char hex[], char **decoded);
static int hex_digit(char character);
static void task_free(nv_preview_task_t *task);
static void queue_event_locked(nv_preview_queue_t *queue,
		const nv_preview_task_t *task, nv_preview_task_state_t state,
		const char content[], int truncated, const char error_code[], int os_error);
static void cancel_task_locked(nv_preview_queue_t *queue, nv_preview_task_t *task);
static int task_cancelled(nv_preview_queue_t *queue, const nv_preview_task_t *task);
static int task_expired(const nv_preview_task_t *task);
static uint64_t now_ms(void);
static int preview_text(nv_preview_queue_t *queue, nv_preview_task_t *task,
		char **content, int *truncated, const char **error_code, int *os_error);
static int preview_pdf(nv_preview_queue_t *queue, nv_preview_task_t *task,
		char **content, int *truncated, const char **error_code, int *os_error);
static int preview_archive(nv_preview_queue_t *queue, nv_preview_task_t *task,
		char **content, int *truncated, const char **error_code, int *os_error);
static int preview_binary(nv_preview_queue_t *queue, nv_preview_task_t *task,
		char **content, int *truncated, const char **error_code, int *os_error);
static int preview_directory(nv_preview_queue_t *queue, nv_preview_task_t *task,
		char **content, int *truncated, const char **error_code, int *os_error);
static void sanitize_preview_text(char content[], size_t length);

int
nv_preview_hex_encode(const char input[], char output[], size_t output_size)
{
	static const char digits[] = "0123456789abcdef";
	if(input == NULL || output == NULL) return -1;
	const size_t length = strlen(input);
	if(length > NV_PREVIEW_MAX_PATH_BYTES || output_size < length*2U + 1U) return -1;
	for(size_t i = 0U; i < length; ++i)
	{
		const unsigned char value = (unsigned char)input[i];
		output[i*2U] = digits[value >> 4U];
		output[i*2U + 1U] = digits[value & 0x0fU];
	}
	output[length*2U] = '\0';
	return 0;
}

static int
hex_digit(char character)
{
	if(character >= '0' && character <= '9') return character - '0';
	if(character >= 'a' && character <= 'f') return character - 'a' + 10;
	if(character >= 'A' && character <= 'F') return character - 'A' + 10;
	return -1;
}

static int
hex_decode(const char hex[], char **decoded)
{
	if(hex == NULL || decoded == NULL) return -1;
	const size_t length = strlen(hex);
	if(length == 0U || length % 2U != 0U || length/2U > NV_PREVIEW_MAX_PATH_BYTES)
	{
		return -1;
	}
	char *const result = malloc(length/2U + 1U);
	if(result == NULL) return -1;
	for(size_t i = 0U; i < length; i += 2U)
	{
		const int high = hex_digit(hex[i]), low = hex_digit(hex[i + 1U]);
		if(high < 0 || low < 0 || (high == 0 && low == 0))
		{
			free(result);
			return -1;
		}
		result[i/2U] = (char)((high << 4U) | low);
	}
	result[length/2U] = '\0';
	*decoded = result;
	return 0;
}

static int
request_valid(const nv_preview_request_t *request)
{
	char *cwd = NULL, *path = NULL;
	const int valid = request != NULL && request->generation != 0U &&
			(request->pane == NV_PREVIEW_PANE_LEFT || request->pane == NV_PREVIEW_PANE_RIGHT) &&
			(request->has_target_pane == 0 || request->has_target_pane == 1) &&
			(!request->has_target_pane ||
			 (request->target_pane == NV_PREVIEW_PANE_LEFT ||
			  request->target_pane == NV_PREVIEW_PANE_RIGHT)) &&
			(request->kind == NV_PREVIEW_KIND_TEXT ||
			 request->kind == NV_PREVIEW_KIND_MARKDOWN ||
			 request->kind == NV_PREVIEW_KIND_PDF ||
			 request->kind == NV_PREVIEW_KIND_DIRECTORY ||
			 request->kind == NV_PREVIEW_KIND_ARCHIVE ||
			 request->kind == NV_PREVIEW_KIND_BINARY) &&
			request->max_bytes != 0U && request->max_bytes <= NV_PREVIEW_MAX_BYTES &&
			request->timeout_ms != 0U && request->timeout_ms <= NV_PREVIEW_MAX_TIMEOUT_MS &&
			hex_decode(request->cwd_bytes_hex, &cwd) == 0 &&
			hex_decode(request->path_bytes_hex, &path) == 0;
	free(cwd);
	free(path);
	return valid;
}

static uint64_t
now_ms(void)
{
	struct timeval value;
	return gettimeofday(&value, NULL) == 0 ? (uint64_t)value.tv_sec*1000U +
		(uint64_t)value.tv_usec/1000U : 0U;
}

static int
task_expired(const nv_preview_task_t *task)
{
	return task->deadline_ms != 0U && now_ms() >= task->deadline_ms;
}

static void
sanitize_preview_text(char content[], size_t length)
{
	for(size_t i = 0U; i < length; ++i)
	{
		const unsigned char value = (unsigned char)content[i];
		if(value == '\n' || value == '\r' || value == '\t' ||
			(value >= 0x20U && value <= 0x7eU)) continue;
		content[i] = '?';
	}
}

static nv_preview_queue_t *
queue_alloc(int start)
{
	nv_preview_queue_t *const queue = calloc(1U, sizeof(*queue));
	if(queue == NULL) return NULL;
	if(pthread_mutex_init(&queue->mutex, NULL) != 0 ||
			pthread_cond_init(&queue->ready, NULL) != 0)
	{
		pthread_mutex_destroy(&queue->mutex);
		free(queue);
		return NULL;
	}
	queue->next_id = 1U;
	if(start && queue_start_worker(queue) != 0)
	{
		nv_preview_queue_free(queue);
		return NULL;
	}
	return queue;
}

nv_preview_queue_t *
nv_preview_queue_alloc(void)
{
	return queue_alloc(1);
}

nv_preview_queue_t *
nv_preview_queue_alloc_paused(void)
{
	return queue_alloc(0);
}

static int
queue_start_worker(nv_preview_queue_t *queue)
{
	if(queue == NULL || queue->started || queue->stopping) return -1;
	if(pthread_create(&queue->worker, NULL, preview_worker, queue) != 0) return -1;
	queue->started = 1;
	return 0;
}

int
nv_preview_queue_start(nv_preview_queue_t *queue)
{
	if(queue == NULL) return -1;
	pthread_mutex_lock(&queue->mutex);
	const int result = queue_start_worker(queue);
	pthread_mutex_unlock(&queue->mutex);
	return result;
}

static void
task_free(nv_preview_task_t *task)
{
	if(task == NULL) return;
	free(task->cwd);
	free(task->path);
	free(task->cwd_hex);
	free(task->path_hex);
	free(task);
}

void
nv_preview_event_free(nv_preview_event_t *event)
{
	if(event == NULL) return;
	free(event->cwd_bytes_hex);
	free(event->path_bytes_hex);
	free(event->content);
	free(event->error_code);
	*event = (nv_preview_event_t){};
}

static void
queue_event_locked(nv_preview_queue_t *queue, const nv_preview_task_t *task,
		nv_preview_task_state_t state, const char content[], int truncated,
		const char error_code[], int os_error)
{
	nv_preview_event_node_t *const node = calloc(1U, sizeof(*node));
	if(node == NULL) return;
	node->event = (nv_preview_event_t){
		.task_id = task->id,
		.generation = task->request.generation,
		.pane = task->request.pane,
		.target_pane = task->request.target_pane,
		.has_target_pane = 1,
		.kind = task->request.kind,
		.state = state,
		.cwd_bytes_hex = strdup(task->cwd_hex),
		.path_bytes_hex = strdup(task->path_hex),
		.content = content == NULL ? NULL : strdup(content),
		.error_code = error_code == NULL ? NULL : strdup(error_code),
		.os_error = os_error,
		.truncated = truncated,
	};
	if(node->event.cwd_bytes_hex == NULL || node->event.path_bytes_hex == NULL ||
			(content != NULL && node->event.content == NULL) ||
			(error_code != NULL && node->event.error_code == NULL))
	{
		nv_preview_event_free(&node->event);
		free(node);
		return;
	}
	if(queue->events_tail == NULL) queue->events_head = node;
	else queue->events_tail->next = node;
	queue->events_tail = node;
}

static void
cancel_task_locked(nv_preview_queue_t *queue, nv_preview_task_t *task)
{
	if(task->cancelled || task->terminal_emitted) return;
	task->cancelled = 1;
	task->terminal_emitted = 1;
	queue_event_locked(queue, task, NV_PREVIEW_TASK_CANCELLED, NULL, 0,
			"preview-cancelled", 0);
}

int
nv_preview_queue_submit(nv_preview_queue_t *queue,
		const nv_preview_request_t *request, uint64_t *task_id)
{
	if(queue == NULL || !request_valid(request)) return -1;
	nv_preview_task_t *const task = calloc(1U, sizeof(*task));
	if(task == NULL) return -1;
	task->request = *request;
	if(!task->request.has_target_pane)
	{
		task->request.target_pane = task->request.pane;
		task->request.has_target_pane = 1;
	}
	task->deadline_ms = now_ms() + request->timeout_ms;
	if(hex_decode(request->cwd_bytes_hex, &task->cwd) != 0 ||
			hex_decode(request->path_bytes_hex, &task->path) != 0 ||
			(task->cwd_hex = strdup(request->cwd_bytes_hex)) == NULL ||
			(task->path_hex = strdup(request->path_bytes_hex)) == NULL)
	{
		task_free(task);
		return -1;
	}
	pthread_mutex_lock(&queue->mutex);
	if(queue->stopping)
	{
		pthread_mutex_unlock(&queue->mutex);
		task_free(task);
		return -1;
	}
	for(nv_preview_task_t *older = queue->tasks_head; older != NULL; older = older->next)
	{
		if(older->request.target_pane == task->request.target_pane &&
				older->request.generation < request->generation)
		{
			cancel_task_locked(queue, older);
		}
	}
	if(queue->running_task != NULL &&
			queue->running_task->request.target_pane == task->request.target_pane &&
			queue->running_task->request.generation < request->generation)
	{
		cancel_task_locked(queue, queue->running_task);
	}
	task->id = queue->next_id++;
	if(queue->tasks_tail == NULL) queue->tasks_head = task;
	else queue->tasks_tail->next = task;
	queue->tasks_tail = task;
	queue_event_locked(queue, task, NV_PREVIEW_TASK_QUEUED, NULL, 0, NULL, 0);
	pthread_cond_signal(&queue->ready);
	pthread_mutex_unlock(&queue->mutex);
	if(task_id != NULL) *task_id = task->id;
	return 0;
}

int
nv_preview_queue_pop(nv_preview_queue_t *queue, nv_preview_event_t *event)
{
	if(queue == NULL || event == NULL) return -1;
	pthread_mutex_lock(&queue->mutex);
	nv_preview_event_node_t *const node = queue->events_head;
	if(node != NULL)
	{
		queue->events_head = node->next;
		if(queue->events_head == NULL) queue->events_tail = NULL;
		*event = node->event;
		free(node);
	}
	pthread_mutex_unlock(&queue->mutex);
	return node == NULL ? 0 : 1;
}

static int
task_cancelled(nv_preview_queue_t *queue, const nv_preview_task_t *task)
{
	pthread_mutex_lock(&queue->mutex);
	const int cancelled = task->cancelled || queue->stopping;
	pthread_mutex_unlock(&queue->mutex);
	return cancelled;
}

static int
preview_text(nv_preview_queue_t *queue, nv_preview_task_t *task, char **content,
		int *truncated, const char **error_code, int *os_error)
{
	/* Do not let a FIFO/device selected by a stale snapshot block the only
	 * worker before fstat() rejects its non-regular type. */
	const int fd = open(task->path, O_RDONLY | O_NONBLOCK);
	if(fd < 0)
	{
		*error_code = "preview-open-failed";
		*os_error = errno;
		return -1;
	}
	struct stat st;
	if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
	{
		*error_code = "preview-not-regular-file";
		*os_error = errno;
		close(fd);
		return -1;
	}
	char *const result = malloc(task->request.max_bytes + 1U);
	if(result == NULL)
	{
		*error_code = "preview-out-of-memory";
		close(fd);
		return -1;
	}
	size_t used = 0U;
	while(used < task->request.max_bytes)
	{
		if(task_cancelled(queue, task))
		{
			free(result);
			close(fd);
			return 1;
		}
		if(task_expired(task))
		{
			*error_code = "preview-timeout";
			free(result);
			close(fd);
			return -1;
		}
		const ssize_t read_count = read(fd, result + used, task->request.max_bytes - used);
		if(read_count < 0)
		{
			if(errno == EINTR) continue;
			*error_code = "preview-read-failed";
			*os_error = errno;
			free(result);
			close(fd);
			return -1;
		}
		if(read_count == 0) break;
		used += (size_t)read_count;
	}
	if(used == task->request.max_bytes)
	{
		char extra;
		const ssize_t read_count = read(fd, &extra, 1U);
		*truncated = read_count > 0;
	}
	sanitize_preview_text(result, used);
	result[used] = '\0';
	close(fd);
	*content = result;
	return 0;
}

static int
preview_directory(nv_preview_queue_t *queue, nv_preview_task_t *task,
		char **content, int *truncated, const char **error_code, int *os_error)
{
	DIR *const directory = opendir(task->path);
	if(directory == NULL)
	{
		*error_code = "preview-open-failed";
		*os_error = errno;
		return -1;
	}
	char *const result = calloc(task->request.max_bytes + 1U, 1U);
	if(result == NULL)
	{
		*error_code = "preview-out-of-memory";
		closedir(directory);
		return -1;
	}
	size_t used = 0U;
	for(;;)
	{
		if(task_cancelled(queue, task))
		{
			free(result);
			closedir(directory);
			return 1;
		}
		if(task_expired(task))
		{
			*error_code = "preview-timeout";
			free(result);
			closedir(directory);
			return -1;
		}
		errno = 0;
		struct dirent *const entry = readdir(directory);
		if(entry == NULL)
		{
			if(errno != 0)
			{
				*error_code = "preview-read-failed";
				*os_error = errno;
				free(result);
				closedir(directory);
				return -1;
			}
			break;
		}
		if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
		const size_t name_length = strlen(entry->d_name);
		if(name_length + 1U > task->request.max_bytes - used)
		{
			*truncated = 1;
			break;
		}
		memcpy(result + used, entry->d_name, name_length);
		used += name_length;
		result[used++] = '\n';
	}
	closedir(directory);
	sanitize_preview_text(result, used);
	*content = result;
	return 0;
}

static int
preview_binary(nv_preview_queue_t *queue, nv_preview_task_t *task, char **content,
		int *truncated, const char **error_code, int *os_error)
{
	const int fd = open(task->path, O_RDONLY | O_NONBLOCK);
	if(fd < 0)
	{
		*error_code = "preview-open-failed";
		*os_error = errno;
		return -1;
	}
	struct stat st;
	if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
	{
		*error_code = "preview-not-regular-file";
		*os_error = errno;
		close(fd);
		return -1;
	}
	char *const result = malloc(task->request.max_bytes + 1U);
	if(result == NULL)
	{
		*error_code = "preview-out-of-memory";
		close(fd);
		return -1;
	}
	size_t used = 0U;
	size_t offset = 0U;
	for(;;)
	{
		if(task_cancelled(queue, task))
		{
			free(result);
			close(fd);
			return 1;
		}
		if(task_expired(task))
		{
			*error_code = "preview-timeout";
			free(result);
			close(fd);
			return -1;
		}
		unsigned char bytes[16];
		const ssize_t read_count = read(fd, bytes, sizeof(bytes));
		if(read_count < 0)
		{
			if(errno == EINTR) continue;
			*error_code = "preview-read-failed";
			*os_error = errno;
			free(result);
			close(fd);
			return -1;
		}
		if(read_count == 0) break;
		char line[128];
		size_t line_length = (size_t)snprintf(line, sizeof(line), "%08zx  ", offset);
		if(line_length >= sizeof(line))
		{
			*error_code = "preview-format-failed";
			free(result);
			close(fd);
			return -1;
		}
		for(size_t i = 0U; i < 16U; ++i)
		{
			const int written = snprintf(line + line_length, sizeof(line) - line_length,
					i < (size_t)read_count ? "%02x " : "   ",
				i < (size_t)read_count ? bytes[i] : 0U);
			if(written < 0 || (size_t)written >= sizeof(line) - line_length)
			{
				*error_code = "preview-format-failed";
				free(result);
				close(fd);
				return -1;
			}
			line_length += (size_t)written;
		}
		if(line_length + 2U >= sizeof(line))
		{
			*error_code = "preview-format-failed";
			free(result);
			close(fd);
			return -1;
		}
		line[line_length++] = ' ';
		line[line_length++] = '|';
		for(size_t i = 0U; i < (size_t)read_count; ++i)
		{
			const unsigned char value = bytes[i];
			line[line_length++] = value >= 0x20U && value <= 0x7eU ? (char)value : '.';
		}
		line[line_length++] = '|';
		line[line_length++] = '\n';
		if(line_length > task->request.max_bytes - used)
		{
			*truncated = 1;
			break;
		}
		memcpy(result + used, line, line_length);
		used += line_length;
		offset += (size_t)read_count;
	}
	close(fd);
	result[used] = '\0';
	*content = result;
	return 0;
}

#ifndef _WIN32
extern char **environ;

static int
preview_external(nv_preview_queue_t *queue, nv_preview_task_t *task,
		const char helper[], char *const argv[], char **content,
		int *truncated, const char **error_code, int *os_error)
{
	int output_pipe[2];
	if(pipe(output_pipe) != 0)
	{
		*error_code = "preview-helper-pipe-failed";
		*os_error = errno;
		return -1;
	}
	posix_spawn_file_actions_t actions;
	if(posix_spawn_file_actions_init(&actions) != 0 ||
			posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDOUT_FILENO) != 0 ||
			posix_spawn_file_actions_addclose(&actions, output_pipe[0]) != 0)
	{
		close(output_pipe[0]);
		close(output_pipe[1]);
		*error_code = "preview-helper-setup-failed";
		*os_error = errno;
		return -1;
	}
	pid_t child = 0;
	const int spawned = posix_spawn(&child, helper, &actions, NULL, argv, environ);
	(void)posix_spawn_file_actions_destroy(&actions);
	close(output_pipe[1]);
	if(spawned != 0)
	{
		close(output_pipe[0]);
		*error_code = "preview-helper-spawn-failed";
		*os_error = spawned;
		return -1;
	}
	const int flags = fcntl(output_pipe[0], F_GETFL, 0);
	if(flags < 0 || fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK) != 0)
	{
		(void)kill(child, SIGTERM);
		(void)waitpid(child, NULL, 0);
		close(output_pipe[0]);
		*error_code = "preview-helper-pipe-failed";
		*os_error = errno;
		return -1;
	}
	char *const result = malloc(task->request.max_bytes + 1U);
	if(result == NULL)
	{
		(void)kill(child, SIGTERM);
		(void)waitpid(child, NULL, 0);
		close(output_pipe[0]);
		*error_code = "preview-out-of-memory";
		return -1;
	}
	size_t used = 0U;
	int child_status = 0, child_done = 0, pipe_done = 0;
	while(!child_done || !pipe_done)
	{
		if(task_cancelled(queue, task))
		{
			(void)kill(child, SIGTERM);
			(void)waitpid(child, &child_status, 0);
			free(result);
			close(output_pipe[0]);
			return 1;
		}
		if(task_expired(task))
		{
			(void)kill(child, SIGTERM);
			(void)waitpid(child, &child_status, 0);
			free(result);
			close(output_pipe[0]);
			*error_code = "preview-timeout";
			return -1;
		}
		struct pollfd descriptor = { .fd = output_pipe[0], .events = POLLIN };
		(void)poll(&descriptor, 1U, 50);
		if(!pipe_done && (descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0)
		{
			char buffer[4096];
			for(;;)
			{
				const ssize_t count = read(output_pipe[0], buffer, sizeof(buffer));
				if(count > 0)
				{
					const size_t available = task->request.max_bytes - used;
					const size_t copied = (size_t)count < available ? (size_t)count : available;
					if(copied != 0U) memcpy(result + used, buffer, copied);
					used += copied;
					if((size_t)count > copied) *truncated = 1;
					if(used == task->request.max_bytes) *truncated = 1;
					continue;
				}
				if(count == 0) pipe_done = 1;
				if(count < 0 && errno != EAGAIN && errno != EINTR) pipe_done = 1;
				break;
			}
		}
		if(!child_done)
		{
			const pid_t waited = waitpid(child, &child_status, WNOHANG);
			if(waited == child) child_done = 1;
			else if(waited < 0)
			{
				pipe_done = 1;
				*error_code = "preview-helper-wait-failed";
				*os_error = errno;
			}
		}
	}
	close(output_pipe[0]);
	if(!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
	{
		free(result);
		*error_code = "preview-helper-failed";
		*os_error = WIFEXITED(child_status) ? WEXITSTATUS(child_status) : ECHILD;
		return -1;
	}
	sanitize_preview_text(result, used);
	result[used] = '\0';
	*content = result;
	return 0;
}

static int
preview_pdf(nv_preview_queue_t *queue, nv_preview_task_t *task, char **content,
		int *truncated, const char **error_code, int *os_error)
{
	const char *helper = NULL;
	static const char *const candidates[] = {
		"/usr/local/bin/pdftotext", "/opt/homebrew/bin/pdftotext",
		"/usr/bin/pdftotext",
	};
	for(size_t i = 0U; i < sizeof(candidates)/sizeof(candidates[0]); ++i)
	{
		if(access(candidates[i], X_OK) == 0) { helper = candidates[i]; break; }
	}
	if(helper == NULL)
	{
		*error_code = "preview-helper-unavailable";
		*os_error = ENOENT;
		return -1;
	}
	char *const argv[] = {
		(char *)helper, "-f", "1", "-l", "1", task->path, "-", NULL,
	};
	return preview_external(queue, task, helper, argv, content, truncated,
			error_code, os_error);
}

static int
archive_path_is_zip(const char path[])
{
	const size_t length = path == NULL ? 0U : strlen(path);
	if(length < 4U) return 0;
	const char *const suffix = path + length - 4U;
	return tolower((unsigned char)suffix[0]) == '.' &&
		tolower((unsigned char)suffix[1]) == 'z' &&
		tolower((unsigned char)suffix[2]) == 'i' &&
		tolower((unsigned char)suffix[3]) == 'p';
}

static int
preview_archive(nv_preview_queue_t *queue, nv_preview_task_t *task, char **content,
		int *truncated, const char **error_code, int *os_error)
{
	const char *helper = NULL;
	const char *first = NULL;
	const char *second = NULL;
	const char *const *common = NULL;
	if(archive_path_is_zip(task->path))
	{
		first = "/usr/local/bin/unzip";
		second = "/opt/homebrew/bin/unzip";
		static const char *const zip_common[] = {
			"/usr/bin/unzip", "/bin/unzip", "/usr/bin/bsdtar", "/bin/bsdtar",
		};
		common = zip_common;
	}
	else
	{
		first = "/usr/local/bin/bsdtar";
		second = "/opt/homebrew/bin/bsdtar";
		static const char *const tar_common[] = {
			"/usr/bin/bsdtar", "/usr/bin/tar", "/bin/bsdtar", "/bin/tar",
		};
		common = tar_common;
	}
	const char *const candidates[] = { first, second, common[0], common[1], common[2], common[3] };
	for(size_t i = 0U; i < sizeof(candidates)/sizeof(candidates[0]); ++i)
	{
		if(access(candidates[i], X_OK) == 0)
		{
			helper = candidates[i];
			break;
		}
	}
	if(helper == NULL)
	{
		*error_code = "preview-helper-unavailable";
		*os_error = ENOENT;
		return -1;
	}
	char *argv[4] = { (char *)helper, NULL, task->path, NULL };
	argv[1] = archive_path_is_zip(task->path) && strstr(helper, "unzip") != NULL
		? "-Z1" : "-tf";
	return preview_external(queue, task, helper, argv, content, truncated,
			error_code, os_error);
}
#else
static int
preview_pdf(nv_preview_queue_t *queue, nv_preview_task_t *task, char **content,
		int *truncated, const char **error_code, int *os_error)
{
	(void)queue; (void)task; (void)content; (void)truncated;
	*error_code = "preview-helper-unavailable";
	*os_error = ENOSYS;
	return -1;
}

static int
preview_archive(nv_preview_queue_t *queue, nv_preview_task_t *task, char **content,
		int *truncated, const char **error_code, int *os_error)
{
	(void)queue; (void)task; (void)content; (void)truncated;
	*error_code = "preview-helper-unavailable";
	*os_error = ENOSYS;
	return -1;
}
#endif

static void *
preview_worker(void *data)
{
	nv_preview_queue_t *const queue = data;
	for(;;)
	{
		pthread_mutex_lock(&queue->mutex);
		while(queue->tasks_head == NULL && !queue->stopping)
		{
			pthread_cond_wait(&queue->ready, &queue->mutex);
		}
		if(queue->stopping)
		{
			pthread_mutex_unlock(&queue->mutex);
			break;
		}
		nv_preview_task_t *const task = queue->tasks_head;
		queue->tasks_head = task->next;
		if(queue->tasks_head == NULL) queue->tasks_tail = NULL;
		task->next = NULL;
		queue->running_task = task;
		if(!task->cancelled) queue_event_locked(queue, task,
				NV_PREVIEW_TASK_RUNNING, NULL, 0, NULL, 0);
		pthread_mutex_unlock(&queue->mutex);

		char *content = NULL;
		int truncated = 0, os_error = 0;
		const char *error_code = NULL;
		int outcome = task->cancelled ? 1 :
			(task->request.kind == NV_PREVIEW_KIND_DIRECTORY ?
				preview_directory(queue, task, &content, &truncated, &error_code, &os_error) :
			 task->request.kind == NV_PREVIEW_KIND_ARCHIVE ?
				preview_archive(queue, task, &content, &truncated, &error_code, &os_error) :
			 task->request.kind == NV_PREVIEW_KIND_BINARY ?
				preview_binary(queue, task, &content, &truncated, &error_code, &os_error) :
			 task->request.kind == NV_PREVIEW_KIND_PDF ?
				preview_pdf(queue, task, &content, &truncated, &error_code, &os_error) :
				preview_text(queue, task, &content, &truncated, &error_code, &os_error));
		if(outcome == 0 && task_expired(task))
		{
			free(content);
			content = NULL;
			outcome = -1;
			error_code = "preview-timeout";
		}
		pthread_mutex_lock(&queue->mutex);
		if(!task->terminal_emitted)
		{
			task->terminal_emitted = 1;
			if(task->cancelled || outcome > 0)
			{
				queue_event_locked(queue, task, NV_PREVIEW_TASK_CANCELLED, NULL, 0,
						"preview-cancelled", 0);
			}
			else if(outcome == 0)
			{
				queue_event_locked(queue, task, NV_PREVIEW_TASK_DONE, content, truncated,
						NULL, 0);
			}
			else
			{
				queue_event_locked(queue, task, NV_PREVIEW_TASK_FAILED, NULL, 0,
						error_code == NULL ? "preview-failed" : error_code, os_error);
			}
		}
		queue->running_task = NULL;
		pthread_mutex_unlock(&queue->mutex);
		free(content);
		task_free(task);
	}
	return NULL;
}

void
nv_preview_queue_free(nv_preview_queue_t *queue)
{
	if(queue == NULL) return;
	pthread_mutex_lock(&queue->mutex);
	queue->stopping = 1;
	pthread_cond_broadcast(&queue->ready);
	pthread_mutex_unlock(&queue->mutex);
	if(queue->started) pthread_join(queue->worker, NULL);
	while(queue->tasks_head != NULL)
	{
		nv_preview_task_t *const next = queue->tasks_head->next;
		task_free(queue->tasks_head);
		queue->tasks_head = next;
	}
	while(queue->events_head != NULL)
	{
		nv_preview_event_node_t *const next = queue->events_head->next;
		nv_preview_event_free(&queue->events_head->event);
		free(queue->events_head);
		queue->events_head = next;
	}
	pthread_cond_destroy(&queue->ready);
	pthread_mutex_destroy(&queue->mutex);
	free(queue);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
