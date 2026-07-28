/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef VIFM__NEOVIFM__PREVIEW_TASK_H__
#define VIFM__NEOVIFM__PREVIEW_TASK_H__

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint64_t */

#define NV_PREVIEW_MAX_BYTES (64U*1024U)
#define NV_PREVIEW_MAX_PATH_BYTES (16U*1024U)
#define NV_PREVIEW_MAX_TIMEOUT_MS 30000U

typedef enum
{
	NV_PREVIEW_PANE_LEFT,
	NV_PREVIEW_PANE_RIGHT,
} nv_preview_pane_t;

typedef enum
{
	NV_PREVIEW_KIND_TEXT,
	NV_PREVIEW_KIND_MARKDOWN,
	NV_PREVIEW_KIND_PDF,
	NV_PREVIEW_KIND_DIRECTORY,
	NV_PREVIEW_KIND_ARCHIVE,
	NV_PREVIEW_KIND_BINARY,
} nv_preview_kind_t;

typedef enum
{
	NV_PREVIEW_TASK_QUEUED,
	NV_PREVIEW_TASK_RUNNING,
	NV_PREVIEW_TASK_DONE,
	NV_PREVIEW_TASK_FAILED,
	NV_PREVIEW_TASK_CANCELLED,
} nv_preview_task_state_t;

typedef struct
{
	nv_preview_pane_t pane;
	/* `pane` identifies the source snapshot.  The target is the render lane;
	 * old callers may omit it and get source-pane rendering. */
	nv_preview_pane_t target_pane;
	int has_target_pane;
	uint64_t generation;
	const char *cwd_bytes_hex;
	const char *path_bytes_hex;
	nv_preview_kind_t kind;
	size_t max_bytes;
	unsigned int timeout_ms;
} nv_preview_request_t;

typedef struct
{
	uint64_t task_id;
	uint64_t generation;
	nv_preview_pane_t pane;
	nv_preview_pane_t target_pane;
	int has_target_pane;
	nv_preview_kind_t kind;
	nv_preview_task_state_t state;
	char *cwd_bytes_hex;
	char *path_bytes_hex;
	char *content;
	char *error_code;
	int os_error;
	int truncated;
} nv_preview_event_t;

typedef struct nv_preview_queue_t nv_preview_queue_t;

/* Allocates a single-worker queue and starts its worker immediately. */
nv_preview_queue_t *nv_preview_queue_alloc(void);

/* Allocates a queue whose worker is started only by nv_preview_queue_start(). */
nv_preview_queue_t *nv_preview_queue_alloc_paused(void);

/* Starts a paused queue.  Calling this more than once fails. */
int nv_preview_queue_start(nv_preview_queue_t *queue);

/*
 * Copies a fully explicit, raw-byte-addressed request into the queue.  A newer
 * generation for one render target cancels every older unfinished generation
 * for that target.  Requests without target_pane retain source-pane behavior.
 */
int nv_preview_queue_submit(nv_preview_queue_t *queue,
		const nv_preview_request_t *request, uint64_t *task_id);

/* Transfers one immutable lifecycle event to event and returns 1, or 0 if none. */
int nv_preview_queue_pop(nv_preview_queue_t *queue, nv_preview_event_t *event);
void nv_preview_event_free(nv_preview_event_t *event);
void nv_preview_queue_free(nv_preview_queue_t *queue);

/* Encodes a NUL-terminated path as lowercase hex for the protocol boundary. */
int nv_preview_hex_encode(const char input[], char output[], size_t output_size);

#endif /* VIFM__NEOVIFM__PREVIEW_TASK_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
