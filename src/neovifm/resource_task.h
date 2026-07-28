/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 or 3 of the License.
 */

#ifndef VIFM__NEOVIFM__RESOURCE_TASK_H__
#define VIFM__NEOVIFM__RESOURCE_TASK_H__

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint64_t */

#include "resource_mount.h"

#define NV_RESOURCE_TASK_TIMEOUT_MS 30000U

typedef enum
{
	NV_RESOURCE_TASK_MOUNT_ARCHIVE,
	NV_RESOURCE_TASK_MOUNT_SSH,
	NV_RESOURCE_TASK_UNMOUNT,
} nv_resource_task_kind_t;

typedef enum
{
	NV_RESOURCE_TASK_QUEUED,
	NV_RESOURCE_TASK_RUNNING,
	NV_RESOURCE_TASK_DONE,
	NV_RESOURCE_TASK_FAILED,
	NV_RESOURCE_TASK_CANCELLED,
} nv_resource_task_state_t;

typedef struct
{
	nv_resource_task_kind_t kind;
	unsigned int pane;
	uint64_t tab_id;
	unsigned int command_sequence;
	const char *source_path;
	const char *remote;
	const char *mount_point;
	const char *unmount_path;
	const nv_resource_mount_options_t *mount_options;
} nv_resource_task_request_t;

typedef struct
{
	uint64_t task_id;
	unsigned int command_sequence;
	unsigned int pane;
	uint64_t tab_id;
	nv_resource_task_kind_t kind;
	nv_resource_task_state_t state;
	char *source_path;
	char *mount_point;
	char *unmount_path;
	char *error_code;
	int os_error;
} nv_resource_task_event_t;

typedef struct nv_resource_task_queue_t nv_resource_task_queue_t;

nv_resource_task_queue_t *nv_resource_task_queue_alloc(void);
nv_resource_task_queue_t *nv_resource_task_queue_alloc_paused(void);
int nv_resource_task_queue_start(nv_resource_task_queue_t *queue);
int nv_resource_task_queue_submit(nv_resource_task_queue_t *queue,
		const nv_resource_task_request_t *request, uint64_t *task_id);
int nv_resource_task_queue_cancel(nv_resource_task_queue_t *queue,
		uint64_t task_id);
void nv_resource_task_queue_cancel_all(nv_resource_task_queue_t *queue);
int nv_resource_task_queue_busy(nv_resource_task_queue_t *queue);
int nv_resource_task_queue_failed(nv_resource_task_queue_t *queue);
int nv_resource_task_queue_pop(nv_resource_task_queue_t *queue,
		nv_resource_task_event_t *event);
void nv_resource_task_event_free(nv_resource_task_event_t *event);
void nv_resource_task_queue_free(nv_resource_task_queue_t *queue);

const char *nv_resource_task_kind_name(nv_resource_task_kind_t kind);
const char *nv_resource_task_state_name(nv_resource_task_state_t state);

#endif /* VIFM__NEOVIFM__RESOURCE_TASK_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
