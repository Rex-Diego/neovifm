/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 or 3 of the License.
 */

#include "resource_task.h"

#ifndef _WIN32

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../compat/pthread.h"

extern char **environ;

#define NV_RESOURCE_EVENT_LIMIT 256U
#define NV_RESOURCE_QUEUE_LIMIT 64U
#define NV_RESOURCE_ERROR_BYTES 4096U

typedef struct nv_resource_task_t nv_resource_task_t;
typedef struct nv_resource_event_node_t nv_resource_event_node_t;

struct nv_resource_task_t
{
	uint64_t id;
	nv_resource_task_kind_t kind;
	unsigned int pane;
	uint64_t tab_id;
	unsigned int command_sequence;
	char *source_path;
	char *remote;
	char *mount_point;
	char *unmount_path;
	nv_resource_mount_options_t mount_options;
	char *owned_fuse_zip_path;
	char *owned_archivemount_path;
	char *owned_sshfs_path;
	char *owned_mount_unmount_path;
	int cancelled;
	nv_resource_task_t *next;
};

struct nv_resource_event_node_t
{
	nv_resource_task_event_t event;
	nv_resource_event_node_t *next;
};

struct nv_resource_task_queue_t
{
	pthread_mutex_t mutex;
	pthread_cond_t ready;
	pthread_t worker;
	int started;
	int stopping;
	int terminal_event_lost;
	uint64_t next_id;
	nv_resource_task_t *pending;
	nv_resource_task_t *pending_tail;
	size_t pending_count;
	nv_resource_task_t *running;
	size_t task_count;
	nv_resource_event_node_t *events_head;
	nv_resource_event_node_t *events_tail;
	size_t event_count;
};

static void *resource_worker(void *data);
static int queue_start_worker(nv_resource_task_queue_t *queue);
static nv_resource_task_queue_t *queue_alloc(int start);
static int request_valid(const nv_resource_task_request_t *request);
static int copy_optional(char **destination, const char source[]);
static int copy_request(nv_resource_task_t *task,
		const nv_resource_task_request_t *request);
static void task_free(nv_resource_task_t *task);
static int task_cancelled(nv_resource_task_queue_t *queue,
		const nv_resource_task_t *task);
static uint64_t monotonic_ms(void);
static int set_task_error(const char **code, int *os_error,
		const char value[], int error);
static int create_mount_point(char **mount_point, const char **error_code,
		int *os_error);
static int run_process(nv_resource_task_queue_t *queue,
		const nv_resource_task_t *task, const nv_resource_mount_spec_t *spec,
		const char **error_code, int *os_error);
static int run_unmount(nv_resource_task_queue_t *queue,
		const nv_resource_task_t *task, const char **error_code, int *os_error);
static int execute_task(nv_resource_task_queue_t *queue,
		nv_resource_task_t *task, char **mount_point, char **unmount_path,
		const char **error_code, int *os_error);
static int queue_event_locked(nv_resource_task_queue_t *queue,
		const nv_resource_task_t *task, nv_resource_task_state_t state,
		const char source_path[], const char mount_point[],
		const char unmount_path[], const char error_code[], int os_error);

const char *
nv_resource_task_kind_name(nv_resource_task_kind_t kind)
{
	switch(kind)
	{
		case NV_RESOURCE_TASK_MOUNT_ARCHIVE: return "mount-archive";
		case NV_RESOURCE_TASK_MOUNT_SSH: return "mount-ssh";
		case NV_RESOURCE_TASK_UNMOUNT: return "unmount";
	}
	return NULL;
}

const char *
nv_resource_task_state_name(nv_resource_task_state_t state)
{
	switch(state)
	{
		case NV_RESOURCE_TASK_QUEUED: return "queued";
		case NV_RESOURCE_TASK_RUNNING: return "running";
		case NV_RESOURCE_TASK_DONE: return "done";
		case NV_RESOURCE_TASK_FAILED: return "failed";
		case NV_RESOURCE_TASK_CANCELLED: return "cancelled";
	}
	return NULL;
}

void
nv_resource_task_event_free(nv_resource_task_event_t *event)
{
	if(event == NULL) return;
	free(event->source_path);
	free(event->mount_point);
	free(event->unmount_path);
	free(event->error_code);
	*event = (nv_resource_task_event_t){};
}

static int
request_valid(const nv_resource_task_request_t *request)
{
	if(request == NULL || nv_resource_task_kind_name(request->kind) == NULL ||
			request->command_sequence == 0U || request->tab_id == 0U ||
			request->pane > 1U)
		return 0;
	if(request->kind == NV_RESOURCE_TASK_MOUNT_ARCHIVE)
		return request->source_path != NULL && request->remote == NULL &&
				request->mount_point == NULL && request->unmount_path == NULL;
	if(request->kind == NV_RESOURCE_TASK_MOUNT_SSH)
		return request->remote != NULL && request->source_path == NULL &&
				request->mount_point == NULL && request->unmount_path == NULL;
	return request->mount_point != NULL && request->unmount_path != NULL &&
			request->source_path == NULL && request->remote == NULL;
}

static int
copy_optional(char **destination, const char source[])
{
	if(source == NULL) return 0;
	*destination = strdup(source);
	return *destination == NULL ? -1 : 0;
}

static int
copy_request(nv_resource_task_t *task,
		const nv_resource_task_request_t *request)
{
	task->kind = request->kind;
	task->pane = request->pane;
	task->tab_id = request->tab_id;
	task->command_sequence = request->command_sequence;
	if(copy_optional(&task->source_path, request->source_path) != 0 ||
			copy_optional(&task->remote, request->remote) != 0 ||
			copy_optional(&task->mount_point, request->mount_point) != 0 ||
			copy_optional(&task->unmount_path, request->unmount_path) != 0)
		return -1;
	if(request->mount_options != NULL)
	{
		const nv_resource_mount_options_t *const options = request->mount_options;
		if(copy_optional(&task->owned_fuse_zip_path, options->fuse_zip_path) != 0 ||
				copy_optional(&task->owned_archivemount_path,
					options->archivemount_path) != 0 ||
				copy_optional(&task->owned_sshfs_path, options->sshfs_path) != 0 ||
				copy_optional(&task->owned_mount_unmount_path,
					options->unmount_path) != 0)
			return -1;
	}
	task->mount_options = (nv_resource_mount_options_t){
		.fuse_zip_path = task->owned_fuse_zip_path,
		.archivemount_path = task->owned_archivemount_path,
		.sshfs_path = task->owned_sshfs_path,
		.unmount_path = task->owned_mount_unmount_path,
	};
	return 0;
}

static void
task_free(nv_resource_task_t *task)
{
	if(task == NULL) return;
	free(task->source_path);
	free(task->remote);
	free(task->mount_point);
	free(task->unmount_path);
	free(task->owned_fuse_zip_path);
	free(task->owned_archivemount_path);
	free(task->owned_sshfs_path);
	free(task->owned_mount_unmount_path);
	free(task);
}

static nv_resource_task_queue_t *
queue_alloc(int start)
{
	nv_resource_task_queue_t *const queue = calloc(1U, sizeof(*queue));
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
		nv_resource_task_queue_free(queue);
		return NULL;
	}
	return queue;
}

nv_resource_task_queue_t *
nv_resource_task_queue_alloc(void)
{
	return queue_alloc(1);
}

nv_resource_task_queue_t *
nv_resource_task_queue_alloc_paused(void)
{
	return queue_alloc(0);
}

static int
queue_start_worker(nv_resource_task_queue_t *queue)
{
	if(queue == NULL || queue->started || queue->stopping) return -1;
	if(pthread_create(&queue->worker, NULL, resource_worker, queue) != 0) return -1;
	queue->started = 1;
	return 0;
}

int
nv_resource_task_queue_start(nv_resource_task_queue_t *queue)
{
	if(queue == NULL) return -1;
	pthread_mutex_lock(&queue->mutex);
	const int result = queue_start_worker(queue);
	pthread_mutex_unlock(&queue->mutex);
	return result;
}

static int
queue_event_locked(nv_resource_task_queue_t *queue,
		const nv_resource_task_t *task, nv_resource_task_state_t state,
		const char source_path[], const char mount_point[],
		const char unmount_path[], const char error_code[], int os_error)
{
	if(queue->event_count >= NV_RESOURCE_EVENT_LIMIT)
	{
		queue->terminal_event_lost = 1;
		return -1;
	}
	nv_resource_event_node_t *const node = calloc(1U, sizeof(*node));
	if(node == NULL)
	{
		queue->terminal_event_lost = 1;
		return -1;
	}
	node->event = (nv_resource_task_event_t){
		.task_id = task->id,
		.command_sequence = task->command_sequence,
		.pane = task->pane,
		.tab_id = task->tab_id,
		.kind = task->kind,
		.state = state,
		.source_path = source_path == NULL ? NULL : strdup(source_path),
		.mount_point = mount_point == NULL ? NULL : strdup(mount_point),
		.unmount_path = unmount_path == NULL ? NULL : strdup(unmount_path),
		.error_code = error_code == NULL ? NULL : strdup(error_code),
		.os_error = os_error,
	};
	if((source_path != NULL && node->event.source_path == NULL) ||
			(mount_point != NULL && node->event.mount_point == NULL) ||
			(unmount_path != NULL && node->event.unmount_path == NULL) ||
			(error_code != NULL && node->event.error_code == NULL))
	{
		nv_resource_task_event_free(&node->event);
		free(node);
		queue->terminal_event_lost = 1;
		return -1;
	}
	if(queue->events_tail == NULL) queue->events_head = node;
	else queue->events_tail->next = node;
	queue->events_tail = node;
	++queue->event_count;
	return 0;
}

int
nv_resource_task_queue_submit(nv_resource_task_queue_t *queue,
		const nv_resource_task_request_t *request, uint64_t *task_id)
{
	if(queue == NULL || !request_valid(request))
	{
		errno = EINVAL;
		return -1;
	}
	nv_resource_task_t *const task = calloc(1U, sizeof(*task));
	if(task == NULL) { errno = ENOMEM; return -1; }
	if(copy_request(task, request) != 0)
	{
		task_free(task);
		errno = ENOMEM;
		return -1;
	}
	pthread_mutex_lock(&queue->mutex);
	if(queue->stopping || queue->task_count >= NV_RESOURCE_QUEUE_LIMIT)
	{
		pthread_mutex_unlock(&queue->mutex);
		task_free(task);
		errno = queue->stopping ? ECANCELED : EBUSY;
		return -1;
	}
	task->id = queue->next_id++;
	if(queue_event_locked(queue, task, NV_RESOURCE_TASK_QUEUED,
			task->source_path, task->mount_point, task->unmount_path, NULL, 0)
		!= 0)
	{
		pthread_mutex_unlock(&queue->mutex);
		task_free(task);
		errno = ENOMEM;
		return -1;
	}
	if(queue->pending_tail == NULL) queue->pending = task;
	else queue->pending_tail->next = task;
	queue->pending_tail = task;
	++queue->pending_count;
	++queue->task_count;
	if(task_id != NULL) *task_id = task->id;
	pthread_cond_signal(&queue->ready);
	pthread_mutex_unlock(&queue->mutex);
	return 0;
}

void
nv_resource_task_queue_cancel_all(nv_resource_task_queue_t *queue)
{
	if(queue == NULL) return;
	pthread_mutex_lock(&queue->mutex);
	for(nv_resource_task_t *task = queue->pending; task != NULL; task = task->next)
		task->cancelled = 1;
	if(queue->running != NULL) queue->running->cancelled = 1;
	pthread_cond_broadcast(&queue->ready);
	pthread_mutex_unlock(&queue->mutex);
}

int
nv_resource_task_queue_cancel(nv_resource_task_queue_t *queue, uint64_t task_id)
{
	if(queue == NULL || task_id == 0U)
	{
		errno = EINVAL;
		return -1;
	}
	pthread_mutex_lock(&queue->mutex);
	nv_resource_task_t *task = queue->pending;
	while(task != NULL && task->id != task_id) task = task->next;
	if(task == NULL && queue->running != NULL && queue->running->id == task_id)
		task = queue->running;
	if(task == NULL)
	{
		pthread_mutex_unlock(&queue->mutex);
		errno = ENOENT;
		return -1;
	}
	task->cancelled = 1;
	pthread_cond_broadcast(&queue->ready);
	pthread_mutex_unlock(&queue->mutex);
	return 0;
}

int
nv_resource_task_queue_busy(nv_resource_task_queue_t *queue)
{
	if(queue == NULL) return 0;
	pthread_mutex_lock(&queue->mutex);
	const int busy = queue->task_count != 0U;
	pthread_mutex_unlock(&queue->mutex);
	return busy;
}

int
nv_resource_task_queue_failed(nv_resource_task_queue_t *queue)
{
	if(queue == NULL) return 0;
	pthread_mutex_lock(&queue->mutex);
	const int failed = queue->terminal_event_lost;
	pthread_mutex_unlock(&queue->mutex);
	return failed;
}

int
nv_resource_task_queue_pop(nv_resource_task_queue_t *queue,
		nv_resource_task_event_t *event)
{
	if(queue == NULL || event == NULL) return -1;
	pthread_mutex_lock(&queue->mutex);
	nv_resource_event_node_t *const node = queue->events_head;
	if(node != NULL)
	{
		queue->events_head = node->next;
		if(queue->events_head == NULL) queue->events_tail = NULL;
		--queue->event_count;
		*event = node->event;
		free(node);
	}
	pthread_mutex_unlock(&queue->mutex);
	return node == NULL ? 0 : 1;
}

static int
task_cancelled(nv_resource_task_queue_t *queue,
		const nv_resource_task_t *task)
{
	pthread_mutex_lock(&queue->mutex);
	const int cancelled = queue->stopping || task->cancelled;
	pthread_mutex_unlock(&queue->mutex);
	return cancelled;
}

static uint64_t
monotonic_ms(void)
{
	struct timespec value;
	if(clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0U;
	return (uint64_t)value.tv_sec*1000U + (uint64_t)value.tv_nsec/1000000U;
}

static int
set_task_error(const char **code, int *os_error, const char value[], int error)
{
	*code = value;
	*os_error = error;
	return -1;
}

static int
create_mount_point(char **mount_point, const char **error_code, int *os_error)
{
	const char *base = getenv("TMPDIR");
	if(base == NULL || base[0] != '/' || strlen(base) > 4096U) base = "/tmp";
	const size_t length = strlen(base);
	const int separator = length != 0U && base[length - 1U] != '/';
	char *const template = malloc(length + (size_t)separator + 24U);
	if(template == NULL) return set_task_error(error_code, os_error,
			"resource-mount-out-of-memory", ENOMEM);
	(void)snprintf(template, length + (size_t)separator + 24U,
			"%s%sneovifm-mount-XXXXXX", base, separator ? "/" : "");
	if(mkdtemp(template) == NULL)
	{
		const int error = errno;
		free(template);
		return set_task_error(error_code, os_error,
				"resource-mount-point-failed", error);
	}
	*mount_point = template;
	return 0;
}

static int
run_process(nv_resource_task_queue_t *queue, const nv_resource_task_t *task,
		const nv_resource_mount_spec_t *spec, const char **error_code,
		int *os_error)
{
	int error_pipe[2];
	if(pipe(error_pipe) != 0)
		return set_task_error(error_code, os_error,
				"resource-helper-pipe-failed", errno);
	posix_spawn_file_actions_t actions;
	int setup_error = posix_spawn_file_actions_init(&actions);
	if(setup_error == 0)
		setup_error = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO,
				"/dev/null", O_RDONLY, 0);
	if(setup_error == 0)
		setup_error = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO,
				"/dev/null", O_WRONLY, 0);
	if(setup_error == 0)
		setup_error = posix_spawn_file_actions_adddup2(&actions, error_pipe[1],
				STDERR_FILENO);
	if(setup_error == 0)
		setup_error = posix_spawn_file_actions_addclose(&actions, error_pipe[0]);
	if(setup_error == 0)
		setup_error = posix_spawn_file_actions_addclose(&actions, error_pipe[1]);
	if(setup_error != 0)
	{
		(void)posix_spawn_file_actions_destroy(&actions);
		close(error_pipe[0]);
		close(error_pipe[1]);
		return set_task_error(error_code, os_error,
				"resource-helper-setup-failed", setup_error);
	}
	pid_t child = 0;
	const int spawned = posix_spawn(&child, spec->helper_path, &actions, NULL,
			spec->argv, environ);
	(void)posix_spawn_file_actions_destroy(&actions);
	close(error_pipe[1]);
	if(spawned != 0)
	{
		close(error_pipe[0]);
		return set_task_error(error_code, os_error,
				"resource-helper-spawn-failed", spawned);
	}
	const int flags = fcntl(error_pipe[0], F_GETFL, 0);
	if(flags < 0 || fcntl(error_pipe[0], F_SETFL, flags | O_NONBLOCK) != 0)
	{
		const int error = errno;
		(void)kill(child, SIGTERM);
		(void)waitpid(child, NULL, 0);
		close(error_pipe[0]);
		return set_task_error(error_code, os_error,
				"resource-helper-pipe-failed", error);
	}
	char diagnostics[NV_RESOURCE_ERROR_BYTES + 1U] = {};
	size_t diagnostic_length = 0U;
	int child_status = 0;
	int child_done = 0;
	const uint64_t deadline = monotonic_ms() + NV_RESOURCE_TASK_TIMEOUT_MS;
	while(!child_done)
	{
		if(task_cancelled(queue, task))
		{
			(void)kill(child, SIGTERM);
			(void)waitpid(child, &child_status, 0);
			close(error_pipe[0]);
			return set_task_error(error_code, os_error,
					"resource-cancelled", ECANCELED);
		}
		if(deadline != 0U && monotonic_ms() >= deadline)
		{
			(void)kill(child, SIGTERM);
			(void)waitpid(child, &child_status, 0);
			close(error_pipe[0]);
			return set_task_error(error_code, os_error,
					"resource-timeout", ETIMEDOUT);
		}
		struct pollfd descriptor = { .fd = error_pipe[0], .events = POLLIN };
		(void)poll(&descriptor, 1U, 50);
		if((descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0)
		{
			char buffer[512];
			for(;;)
			{
				const ssize_t count = read(error_pipe[0], buffer, sizeof(buffer));
				if(count > 0)
				{
					const size_t available = NV_RESOURCE_ERROR_BYTES - diagnostic_length;
					const size_t copied = (size_t)count < available ?
						(size_t)count : available;
					if(copied != 0U) memcpy(diagnostics + diagnostic_length, buffer, copied);
					diagnostic_length += copied;
					continue;
				}
				break;
			}
		}
		const pid_t waited = waitpid(child, &child_status, WNOHANG);
		if(waited == child) child_done = 1;
		else if(waited < 0)
		{
			const int error = errno;
			close(error_pipe[0]);
			return set_task_error(error_code, os_error,
					"resource-helper-wait-failed", error);
		}
	}
	close(error_pipe[0]);
	if(!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
	{
		(void)diagnostics;
		return set_task_error(error_code, os_error,
				"resource-helper-failed", WIFEXITED(child_status) ?
				WEXITSTATUS(child_status) : ECHILD);
	}
	return 0;
}

static int
run_unmount(nv_resource_task_queue_t *queue, const nv_resource_task_t *task,
		const char **error_code, int *os_error)
{
	char *argv[] = { task->unmount_path, task->mount_point, NULL };
	nv_resource_mount_spec_t spec = {
		.helper_path = task->unmount_path,
		.argv = argv,
		.argc = 2U,
	};
	return run_process(queue, task, &spec, error_code, os_error);
}

static int
execute_task(nv_resource_task_queue_t *queue, nv_resource_task_t *task,
		char **mount_point, char **unmount_path, const char **error_code,
		int *os_error)
{
	if(task->kind == NV_RESOURCE_TASK_UNMOUNT)
	{
		if(run_unmount(queue, task, error_code, os_error) != 0) return -1;
		return 0;
	}
	if(create_mount_point(mount_point, error_code, os_error) != 0) return -1;
	nv_resource_mount_spec_t spec = {};
	nv_resource_mount_error_t error = {};
	const int prepared = task->kind == NV_RESOURCE_TASK_MOUNT_ARCHIVE ?
		nv_resource_mount_prepare_archive(task->source_path, *mount_point,
				&task->mount_options, &spec, &error) :
		nv_resource_mount_prepare_ssh(task->remote, *mount_point,
				&task->mount_options, &spec, &error);
	if(prepared != 0)
	{
		if(error.code == NULL)
			*error_code = "resource-mount-prepare-failed";
		else if(strcmp(error.code, "resource-mounter-unavailable") == 0)
			*error_code = "resource-mounter-unavailable";
		else if(strcmp(error.code, "invalid-resource-path") == 0 ||
				strcmp(error.code, "invalid-remote") == 0)
			*error_code = "invalid-resource-path";
		else
			*error_code = "resource-mount-prepare-failed";
		*os_error = error.os_error;
		nv_resource_mount_error_free(&error);
		return -1;
	}
	if(run_process(queue, task, &spec, error_code, os_error) != 0)
	{
		nv_resource_mount_spec_free(&spec);
		return -1;
	}
	*unmount_path = strdup(spec.unmount_path);
	if(*unmount_path == NULL)
	{
		nv_resource_mount_spec_free(&spec);
		return set_task_error(error_code, os_error,
				"resource-mount-out-of-memory", ENOMEM);
	}
	nv_resource_mount_spec_free(&spec);
	return 0;
}

static void *
resource_worker(void *data)
{
	nv_resource_task_queue_t *const queue = data;
	for(;;)
	{
		pthread_mutex_lock(&queue->mutex);
		while(queue->pending == NULL && !queue->stopping)
			pthread_cond_wait(&queue->ready, &queue->mutex);
		if(queue->stopping)
		{
			pthread_mutex_unlock(&queue->mutex);
			break;
		}
		nv_resource_task_t *const task = queue->pending;
		queue->pending = task->next;
		if(queue->pending == NULL) queue->pending_tail = NULL;
		--queue->pending_count;
		task->next = NULL;
		queue->running = task;
		if(queue_event_locked(queue, task, NV_RESOURCE_TASK_RUNNING,
				task->source_path, task->mount_point, task->unmount_path, NULL, 0)
			!= 0)
			queue->terminal_event_lost = 1;
		pthread_mutex_unlock(&queue->mutex);

		char *mount_point = NULL;
		char *unmount_path = NULL;
		const char *error_code = NULL;
		int os_error = 0;
		const int outcome = task->cancelled ? -1 : execute_task(queue, task,
				&mount_point, &unmount_path, &error_code, &os_error);
		const int cancelled = task_cancelled(queue, task) || os_error == ECANCELED;
		if(outcome != 0 && mount_point != NULL)
		{
			(void)rmdir(mount_point);
			free(mount_point);
			mount_point = NULL;
		}
		pthread_mutex_lock(&queue->mutex);
		if(cancelled)
		{
			if(queue_event_locked(queue, task, NV_RESOURCE_TASK_CANCELLED,
					task->source_path, mount_point, unmount_path,
					"resource-cancelled", ECANCELED)
				!= 0)
				queue->terminal_event_lost = 1;
		}
		else if(outcome == 0)
		{
			if(queue_event_locked(queue, task, NV_RESOURCE_TASK_DONE,
					task->source_path, mount_point, unmount_path, NULL, 0)
				!= 0)
				queue->terminal_event_lost = 1;
		}
		else
		{
			if(queue_event_locked(queue, task, NV_RESOURCE_TASK_FAILED,
					task->source_path, mount_point, unmount_path,
					error_code == NULL ? "resource-failed" : error_code, os_error)
				!= 0)
				queue->terminal_event_lost = 1;
		}
		queue->running = NULL;
		if(queue->task_count != 0U) --queue->task_count;
		pthread_mutex_unlock(&queue->mutex);
		free(mount_point);
		free(unmount_path);
		task_free(task);
	}
	return NULL;
}

void
nv_resource_task_queue_free(nv_resource_task_queue_t *queue)
{
	if(queue == NULL) return;
	pthread_mutex_lock(&queue->mutex);
	queue->stopping = 1;
	for(nv_resource_task_t *task = queue->pending; task != NULL; task = task->next)
		task->cancelled = 1;
	if(queue->running != NULL) queue->running->cancelled = 1;
	pthread_cond_broadcast(&queue->ready);
	pthread_mutex_unlock(&queue->mutex);
	if(queue->started) pthread_join(queue->worker, NULL);
	while(queue->pending != NULL)
	{
		nv_resource_task_t *const next = queue->pending->next;
		task_free(queue->pending);
		queue->pending = next;
	}
	while(queue->events_head != NULL)
	{
		nv_resource_event_node_t *const next = queue->events_head->next;
		nv_resource_task_event_free(&queue->events_head->event);
		free(queue->events_head);
		queue->events_head = next;
	}
	pthread_cond_destroy(&queue->ready);
	pthread_mutex_destroy(&queue->mutex);
	free(queue);
}

#else

#include <errno.h>
#include <stdlib.h>

struct nv_resource_task_queue_t
{
	int unsupported;
};

const char *
nv_resource_task_kind_name(nv_resource_task_kind_t kind)
{
	switch(kind)
	{
		case NV_RESOURCE_TASK_MOUNT_ARCHIVE: return "mount-archive";
		case NV_RESOURCE_TASK_MOUNT_SSH: return "mount-ssh";
		case NV_RESOURCE_TASK_UNMOUNT: return "unmount";
	}
	return NULL;
}

const char *
nv_resource_task_state_name(nv_resource_task_state_t state)
{
	switch(state)
	{
		case NV_RESOURCE_TASK_QUEUED: return "queued";
		case NV_RESOURCE_TASK_RUNNING: return "running";
		case NV_RESOURCE_TASK_DONE: return "done";
		case NV_RESOURCE_TASK_FAILED: return "failed";
		case NV_RESOURCE_TASK_CANCELLED: return "cancelled";
	}
	return NULL;
}

void
nv_resource_task_event_free(nv_resource_task_event_t *event)
{
	if(event == NULL) return;
	free(event->source_path);
	free(event->mount_point);
	free(event->unmount_path);
	free(event->error_code);
	*event = (nv_resource_task_event_t){};
}

nv_resource_task_queue_t *
nv_resource_task_queue_alloc(void)
{
	return calloc(1U, sizeof(nv_resource_task_queue_t));
}

nv_resource_task_queue_t *
nv_resource_task_queue_alloc_paused(void)
{
	return nv_resource_task_queue_alloc();
}

int
nv_resource_task_queue_start(nv_resource_task_queue_t *queue)
{
	return queue == NULL ? -1 : 0;
}

int
nv_resource_task_queue_submit(nv_resource_task_queue_t *queue,
		const nv_resource_task_request_t *request, uint64_t *task_id)
{
	(void)request;
	(void)task_id;
	if(queue == NULL) { errno = EINVAL; return -1; }
	errno = ENOSYS;
	return -1;
}

int
nv_resource_task_queue_cancel(nv_resource_task_queue_t *queue,
		uint64_t task_id)
{
	(void)task_id;
	if(queue == NULL) { errno = EINVAL; return -1; }
	errno = ENOSYS;
	return -1;
}

void
nv_resource_task_queue_cancel_all(nv_resource_task_queue_t *queue)
{
	(void)queue;
}

int
nv_resource_task_queue_busy(nv_resource_task_queue_t *queue)
{
	(void)queue;
	return 0;
}

int
nv_resource_task_queue_failed(nv_resource_task_queue_t *queue)
{
	(void)queue;
	return 0;
}

int
nv_resource_task_queue_pop(nv_resource_task_queue_t *queue,
		nv_resource_task_event_t *event)
{
	(void)queue;
	(void)event;
	return 0;
}

void
nv_resource_task_queue_free(nv_resource_task_queue_t *queue)
{
	free(queue);
}

#endif
