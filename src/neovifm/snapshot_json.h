/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef VIFM__NEOVIFM__SNAPSHOT_JSON_H__
#define VIFM__NEOVIFM__SNAPSHOT_JSON_H__

#include "pane_snapshot.h"
#include "preview_task.h"
#include "action_task.h"
#include "open_resolver.h"
#include "resource_task.h"

typedef enum
{
	NV_PROTOCOL_JSON_OK,
	NV_PROTOCOL_JSON_ERROR = -1,
	NV_PROTOCOL_JSON_TOO_LARGE = -2,
} nv_protocol_json_result_t;

char *nv_protocol_hello_json(unsigned int sequence);
char *nv_protocol_workspace_hello_json(unsigned int sequence);
char *nv_protocol_session_hello_json(unsigned int sequence);
char *nv_protocol_preview_session_hello_json(unsigned int sequence);
nv_protocol_json_result_t nv_protocol_snapshot_json(
		const nv_pane_snapshot_t *snapshot, unsigned int sequence, char **json);
nv_protocol_json_result_t nv_protocol_workspace_snapshot_json(
		const nv_pane_snapshot_t *left, const nv_pane_snapshot_t *right,
		const char active_pane[], unsigned int sequence, char **json);
char *nv_protocol_error_json(const nv_snapshot_error_t *error,
		unsigned int sequence);
char *nv_protocol_workspace_error_json(const nv_snapshot_error_t *error,
		unsigned int sequence);
	nv_protocol_json_result_t nv_protocol_session_snapshot_json(
		const nv_pane_snapshot_t *left, const nv_pane_snapshot_t *right,
		const char active_pane[], unsigned int output_sequence,
		unsigned int request_sequence, const char trigger[], char **json);
nv_protocol_json_result_t nv_protocol_preview_session_snapshot_json(
		const nv_pane_snapshot_t *left, const nv_pane_snapshot_t *right,
		const char active_pane[], unsigned int output_sequence,
		unsigned int request_sequence, const char trigger[], char **json);
nv_protocol_json_result_t
	nv_protocol_preview_workspace_session_snapshot_json(
			const nv_workspace_session_t *session, unsigned int output_sequence,
			unsigned int request_sequence, const char trigger[], char **json);
char *nv_protocol_session_command_error_json(const nv_snapshot_error_t *error,
		unsigned int output_sequence, unsigned int request_sequence);
char *nv_protocol_preview_session_command_error_json(
		const nv_snapshot_error_t *error, unsigned int output_sequence,
		unsigned int request_sequence);
char *nv_protocol_preview_task_json(const nv_preview_event_t *event,
		unsigned int output_sequence);
char *nv_protocol_preview_json(const nv_preview_event_t *event,
		unsigned int output_sequence);
char *nv_protocol_action_task_json(const nv_action_event_t *event,
		unsigned int output_sequence);
char *nv_protocol_resource_task_json(const nv_resource_task_event_t *event,
		unsigned int output_sequence);
char *nv_protocol_open_json(const nv_open_resolution_t *resolution,
		const char path_bytes_hex[], unsigned int output_sequence,
		unsigned int command_sequence);

void nv_protocol_json_free(char *json);

#endif /* VIFM__NEOVIFM__SNAPSHOT_JSON_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
