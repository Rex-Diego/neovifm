/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef VIFM__NEOVIFM__ACTION_TASK_H__
#define VIFM__NEOVIFM__ACTION_TASK_H__

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint64_t */

#include "workspace_session.h"

typedef enum
{
	NV_ACTION_TASK_QUEUED,
	NV_ACTION_TASK_RUNNING,
	NV_ACTION_TASK_DONE,
	NV_ACTION_TASK_FAILED,
	NV_ACTION_TASK_CANCELLED,
} nv_action_task_state_t;

typedef struct
{
	uint64_t task_id;
	unsigned int command_sequence;
	nv_session_command_kind_t kind;
	nv_session_pane_t pane;
	nv_action_task_state_t state;
	size_t completed_count;
	size_t total_count;
	size_t failed_index;
	int has_failed_index;
	int partial;
	char *error_code;
	int os_error;
} nv_action_event_t;

typedef struct nv_action_queue_t nv_action_queue_t;

nv_action_queue_t *nv_action_queue_alloc(void);
nv_action_queue_t *nv_action_queue_alloc_paused(void);
int nv_action_queue_start(nv_action_queue_t *queue);

/*
 * Transfers the already deep-copied immutable action into the bounded
 * single-worker FIFO on success and clears action.  Queued actions retain
 * their immutable snapshot identities until they reach a terminal event.
 */
int nv_action_queue_submit(nv_action_queue_t *queue,
		nv_session_prepared_action_t *action, unsigned int command_sequence,
		uint64_t *task_id);
void nv_action_queue_cancel_all(nv_action_queue_t *queue);
/* Marks one queued/running task cancelled; terminal state is emitted by the worker. */
int nv_action_queue_cancel(nv_action_queue_t *queue, uint64_t task_id);
int nv_action_queue_busy(nv_action_queue_t *queue);
int nv_action_queue_failed(nv_action_queue_t *queue);
int nv_action_queue_pop(nv_action_queue_t *queue, nv_action_event_t *event);
void nv_action_queue_ack_terminal(nv_action_queue_t *queue, uint64_t task_id);
void nv_action_event_free(nv_action_event_t *event);
void nv_action_queue_free(nv_action_queue_t *queue);

#endif /* VIFM__NEOVIFM__ACTION_TASK_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
