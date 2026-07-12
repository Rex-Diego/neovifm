/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h> /* EOF fflush() fputc() fputs() fprintf() */

#include "pane_snapshot.h"
#include "snapshot_json.h"

static int
write_json_line(const char json[])
{
	if(json == NULL || fputs(json, stdout) == EOF || fputc('\n', stdout) == EOF ||
			fflush(stdout) == EOF)
	{
		fputs("neovifm-core-probe: failed to write protocol output\n", stderr);
		return -1;
	}
	return 0;
}

static int
write_error(const nv_snapshot_error_t *error, unsigned int sequence)
{
	char *const json = nv_protocol_error_json(error, sequence);
	if(json == NULL)
	{
		fputs("neovifm-core-probe: failed to serialize error record\n", stderr);
		return -1;
	}
	const int result = write_json_line(json);
	nv_protocol_json_free(json);
	return result;
}

static int
write_hello(void)
{
	char *const json = nv_protocol_hello_json(0U);
	if(json == NULL)
	{
		fputs("neovifm-core-probe: failed to serialize hello record\n", stderr);
		return -1;
	}
	const int result = write_json_line(json);
	nv_protocol_json_free(json);
	return result;
}

int
main(int argc, char *argv[])
{
	if(write_hello() != 0)
	{
		return 1;
	}
	if(argc != 2)
	{
		nv_snapshot_error_t error = {
			.code = "usage",
			.message = "expected exactly one directory argument",
		};
		return (write_error(&error, 1U) == 0) ? 2 : 1;
	}

	nv_pane_snapshot_t snapshot = {};
	nv_snapshot_error_t error = {};
	if(nv_pane_snapshot_build(argv[1], &snapshot, &error) != 0)
	{
		write_error(&error, 1U);
		nv_snapshot_error_free(&error);
		return 1;
	}

	char *const json = nv_protocol_snapshot_json(&snapshot, 1U);
	nv_pane_snapshot_free(&snapshot);
	if(json == NULL)
	{
		nv_snapshot_error_t serialization_error = {
			.code = "serialization",
			.message = "failed to serialize directory snapshot",
		};
		write_error(&serialization_error, 1U);
		return 1;
	}

	const int result = write_json_line(json);
	nv_protocol_json_free(json);
	return (result == 0) ? 0 : 1;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
