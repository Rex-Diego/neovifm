/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef VIFM__NEOVIFM__OPEN_RESOLVER_H__
#define VIFM__NEOVIFM__OPEN_RESOLVER_H__

#include <stddef.h> /* size_t */

#define NV_OPEN_MAX_ARGS 32U
#define NV_OPEN_MAX_ARG_BYTES (4U*1024U)
#define NV_OPEN_MAX_ASSOCIATIONS 256U
#define NV_OPEN_MAX_PATTERN_BYTES (4U*1024U)
#define NV_OPEN_CONFIG_MAX_BYTES (256U*1024U)
#define NV_OPEN_CONFIG_MAX_LINE_BYTES NV_OPEN_MAX_ARG_BYTES

typedef enum
{
	NV_OPEN_INTENT_OPEN,
	NV_OPEN_INTENT_EDIT,
	NV_OPEN_INTENT_PREVIEW,
} nv_open_intent_t;

typedef enum
{
	NV_OPEN_SOURCE_ASSOCIATION,
	NV_OPEN_SOURCE_PLATFORM,
} nv_open_source_t;

/* Vifm association namespaces accepted by the optional resolver.  The
 * caller supplies rules in configuration order; this module never maintains
 * a parallel extension table. */
typedef enum
{
	NV_OPEN_ASSOC_FILETYPE,
	NV_OPEN_ASSOC_FILEXTYPE,
	NV_OPEN_ASSOC_FILEVIEWER,
} nv_open_association_kind_t;

typedef struct
{
	nv_open_association_kind_t kind;
	const char *pattern;
	const char *command;
} nv_open_association_rule_t;

typedef struct
{
	nv_open_intent_t intent;
	nv_open_source_t source;
	char **argv;
	size_t argc;
} nv_open_resolution_t;

typedef struct
{
	char *code;
	char *message;
} nv_open_error_t;

/*
 * Resolves an open intent into an owned argv vector.  association_argv is an
 * optional argv prefix already resolved by the core configuration layer (for
 * example a Vifm filetype/filextype record); this module deliberately does
 * not parse extension or MIME tables.  The target path is appended as one
 * argument.  When no association is supplied, MYVIFMRC is loaded when set;
 * unmatched targets use the platform opener.
 */
int nv_open_resolve(nv_open_intent_t intent, const char path[],
		const char *const association_argv[], size_t association_argc,
		nv_open_resolution_t *resolution, nv_open_error_t *error);

/* Resolves the first matching, caller-supplied Vifm association rule.  Rules
 * must already come from an explicit configuration source and remain ordered
 * by Vifm precedence.  Patterns use a bounded glob subset (*, ?, []); command
 * strings are tokenized without a shell and support only %%/%%f/%%c macros.
 * open uses filetype/filextype and falls back to the platform opener when no
 * rule matches; preview uses fileviewer and reports no-association instead of
 * launching a desktop opener. */
int nv_open_resolve_rules(nv_open_intent_t intent, const char path[],
		const nv_open_association_rule_t rules[], size_t rule_count,
		nv_open_resolution_t *resolution, nv_open_error_t *error);

void nv_open_resolution_free(nv_open_resolution_t *resolution);
void nv_open_error_free(nv_open_error_t *error);
const char *nv_open_intent_name(nv_open_intent_t intent);
const char *nv_open_source_name(nv_open_source_t source);

#endif /* VIFM__NEOVIFM__OPEN_RESOLVER_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
