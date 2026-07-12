/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "snapshot_json.h"

#include <inttypes.h> /* PRId64 PRIu64 */
#include <stddef.h> /* NULL size_t */
#include <stdio.h> /* snprintf() */

#include "../utils/parson.h"

static char *serialize_payload(const char type[], unsigned int sequence,
		JSON_Value *payload_value);
static JSON_Value *snapshot_payload(const nv_pane_snapshot_t *snapshot);
static JSON_Value *entry_value(const nv_pane_entry_t *entry);

static int
set_i64_string(JSON_Object *object, const char name[], int64_t value)
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%" PRId64, value);
	return json_object_set_string(object, name, buffer);
}

static int
set_u64_string(JSON_Object *object, const char name[], uint64_t value)
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
	return json_object_set_string(object, name, buffer);
}

static const char *
entry_kind_name(nv_entry_kind_t kind)
{
	switch(kind)
	{
		case NV_ENTRY_DIRECTORY: return "directory";
		case NV_ENTRY_FILE: return "file";
		case NV_ENTRY_SYMLINK: return "symlink";
		case NV_ENTRY_EXECUTABLE: return "executable";
		case NV_ENTRY_FIFO: return "fifo";
		case NV_ENTRY_SOCKET: return "socket";
		case NV_ENTRY_CHAR_DEVICE: return "char-device";
		case NV_ENTRY_BLOCK_DEVICE: return "block-device";
		case NV_ENTRY_UNKNOWN: return "unknown";
	}
	return "unknown";
}

static char *
serialize_payload(const char type[], unsigned int sequence,
		JSON_Value *payload_value)
{
	JSON_Value *const root_value = json_value_init_object();
	if(root_value == NULL)
	{
		json_value_free(payload_value);
		return NULL;
	}

	JSON_Object *const root = json_value_get_object(root_value);
	if(json_object_set_string(root, "protocol", "neovifm-core") != JSONSuccess ||
			json_object_set_number(root, "version", 0) != JSONSuccess ||
			json_object_set_string(root, "type", type) != JSONSuccess ||
			json_object_set_number(root, "sequence", sequence) != JSONSuccess ||
			json_object_set_value(root, "payload", payload_value) != JSONSuccess)
	{
		if(json_value_get_parent(payload_value) == NULL)
		{
			json_value_free(payload_value);
		}
		json_value_free(root_value);
		return NULL;
	}

	char *const serialized = json_serialize_to_string(root_value);
	json_value_free(root_value);
	return serialized;
}

char *
nv_protocol_hello_json(unsigned int sequence)
{
	JSON_Value *const payload_value = json_value_init_object();
	JSON_Value *const capabilities_value = json_value_init_array();
	if(payload_value == NULL || capabilities_value == NULL)
	{
		json_value_free(payload_value);
		json_value_free(capabilities_value);
		return NULL;
	}

	JSON_Object *const payload = json_value_get_object(payload_value);
	JSON_Array *const capabilities = json_value_get_array(capabilities_value);
	if(json_object_set_string(payload, "implementation",
				"neovifm-core-probe") != JSONSuccess ||
			json_array_append_string(capabilities, "snapshot-v0") != JSONSuccess ||
			json_object_set_value(payload, "capabilities",
					capabilities_value) != JSONSuccess)
	{
		if(json_value_get_parent(capabilities_value) == NULL)
		{
			json_value_free(capabilities_value);
		}
		json_value_free(payload_value);
		return NULL;
	}

	return serialize_payload("hello", sequence, payload_value);
}

static int
set_stat_error(JSON_Object *object, int error_number)
{
	JSON_Value *const value = json_value_init_object();
	if(value == NULL)
	{
		return JSONFailure;
	}

	JSON_Object *const stat_error = json_value_get_object(value);
	if(json_object_set_number(stat_error, "code", error_number) != JSONSuccess ||
			json_object_set_string(stat_error, "message",
					"metadata unavailable") != JSONSuccess ||
			json_object_set_value(object, "stat_error", value) != JSONSuccess)
	{
		if(json_value_get_parent(value) == NULL)
		{
			json_value_free(value);
		}
		return JSONFailure;
	}
	return JSONSuccess;
}

static int
set_optional_stat(JSON_Object *object, const nv_pane_entry_t *entry)
{
	if(!entry->has_stat)
	{
		return (entry->stat_error == 0)
			? JSONSuccess
			: set_stat_error(object, entry->stat_error);
	}

	char mode[16];
	snprintf(mode, sizeof(mode), "%o", (unsigned int)entry->mode);
	if(set_u64_string(object, "inode", entry->inode) != JSONSuccess ||
			json_object_set_string(object, "mode_octal", mode) != JSONSuccess)
	{
		return JSONFailure;
	}
	return JSONSuccess;
}

static JSON_Value *
entry_value(const nv_pane_entry_t *entry)
{
	JSON_Value *const value = json_value_init_object();
	if(value == NULL)
	{
		return NULL;
	}

	JSON_Object *const object = json_value_get_object(value);
	if(json_object_set_string(object, "name_display", entry->name_display) !=
			JSONSuccess ||
			json_object_set_string(object, "name_bytes_hex",
					entry->name_bytes_hex) != JSONSuccess ||
			json_object_set_string(object, "path_display", entry->path_display) !=
			JSONSuccess ||
			json_object_set_string(object, "path_bytes_hex",
					entry->path_bytes_hex) != JSONSuccess ||
			json_object_set_string(object, "kind", entry_kind_name(entry->kind)) !=
			JSONSuccess ||
			set_u64_string(object, "size_bytes", entry->size_bytes) != JSONSuccess ||
			set_i64_string(object, "mtime_unix_ms", entry->mtime_unix_ms) !=
			JSONSuccess ||
			json_object_set_boolean(object, "selected", entry->selected) !=
			JSONSuccess ||
			json_object_set_boolean(object, "hidden", entry->hidden) != JSONSuccess ||
			set_optional_stat(object, entry) != JSONSuccess)
	{
		json_value_free(value);
		return NULL;
	}
	return value;
}

static JSON_Value *
snapshot_payload(const nv_pane_snapshot_t *snapshot)
{
	JSON_Value *const payload_value = json_value_init_object();
	JSON_Value *const entries_value = json_value_init_array();
	if(payload_value == NULL || entries_value == NULL)
	{
		json_value_free(payload_value);
		json_value_free(entries_value);
		return NULL;
	}

	JSON_Object *const payload = json_value_get_object(payload_value);
	JSON_Array *const entries = json_value_get_array(entries_value);
	for(size_t i = 0U; i < snapshot->entry_count; ++i)
	{
		JSON_Value *const value = entry_value(&snapshot->entries[i]);
		if(value == NULL || json_array_append_value(entries, value) != JSONSuccess)
		{
			json_value_free(value);
			json_value_free(entries_value);
			json_value_free(payload_value);
			return NULL;
		}
	}

	if(json_object_set_string(payload, "cwd_display", snapshot->cwd_display) !=
			JSONSuccess ||
			json_object_set_string(payload, "cwd_bytes_hex",
					snapshot->cwd_bytes_hex) != JSONSuccess ||
			set_i64_string(payload, "generated_at_unix_ms",
					snapshot->generated_at_unix_ms) != JSONSuccess ||
			json_object_set_number(payload, "cursor", snapshot->cursor) != JSONSuccess ||
			json_object_set_number(payload, "entry_count", snapshot->entry_count) !=
			JSONSuccess ||
			json_object_set_value(payload, "entries", entries_value) != JSONSuccess)
	{
		if(json_value_get_parent(entries_value) == NULL)
		{
			json_value_free(entries_value);
		}
		json_value_free(payload_value);
		return NULL;
	}
	return payload_value;
}

char *
nv_protocol_snapshot_json(const nv_pane_snapshot_t *snapshot,
		unsigned int sequence)
{
	if(snapshot == NULL || snapshot->cwd_display == NULL ||
			snapshot->cwd_bytes_hex == NULL ||
			(snapshot->entry_count != 0U && snapshot->entries == NULL))
	{
		return NULL;
	}

	JSON_Value *const payload = snapshot_payload(snapshot);
	return (payload == NULL) ? NULL : serialize_payload("snapshot", sequence,
			payload);
}

char *
nv_protocol_error_json(const nv_snapshot_error_t *error,
		unsigned int sequence)
{
	if(error == NULL || error->code == NULL || error->message == NULL)
	{
		return NULL;
	}

	JSON_Value *const payload_value = json_value_init_object();
	if(payload_value == NULL)
	{
		return NULL;
	}
	JSON_Object *const payload = json_value_get_object(payload_value);
	if(json_object_set_string(payload, "code", error->code) != JSONSuccess ||
			json_object_set_string(payload, "message", error->message) != JSONSuccess ||
			json_object_set_boolean(payload, "retryable", error->retryable) !=
			JSONSuccess ||
			(error->os_error != 0 &&
			 json_object_set_number(payload, "os_error", error->os_error) !=
			 JSONSuccess) ||
			(error->path_display != NULL &&
			 json_object_set_string(payload, "path_display", error->path_display) !=
			 JSONSuccess) ||
			(error->path_bytes_hex != NULL &&
			 json_object_set_string(payload, "path_bytes_hex",
					 error->path_bytes_hex) != JSONSuccess))
	{
		json_value_free(payload_value);
		return NULL;
	}
	return serialize_payload("error", sequence, payload_value);
}

void
nv_protocol_json_free(char *json)
{
	json_free_serialized_string(json);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
