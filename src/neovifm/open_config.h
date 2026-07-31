/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 or 3 of the License.
 */

#ifndef VIFM__NEOVIFM__OPEN_CONFIG_H__
#define VIFM__NEOVIFM__OPEN_CONFIG_H__

#include <stddef.h> /* size_t */

#include "open_resolver.h"

/* A loaded config owns every string referenced by rules.  The struct remains
 * deliberately plain so callers can pass rules directly to the resolver. */
typedef struct
{
	nv_open_association_rule_t *rules;
	size_t rule_count;
	/* Last bounded previewprg option.  The core prepends it at preview
	 * resolution time so it retains priority over fileviewer rules. */
	char *previewprg;
	char **owned_strings;
	size_t owned_string_count;
} nv_open_config_t;

/* Loads a bounded subset of Vifm's filetype configuration.  One rule is
 * emitted for each glob pattern; MIME selectors and later comma-separated
 * command alternatives are ignored because the resolver is path/argv based. */
int nv_open_config_load(const char path[], nv_open_config_t *config,
		nv_open_error_t *error);

/* Loads the file named by MYVIFMRC.  An unset or empty variable is treated as
 * an empty configuration so platform fallback remains available. */
int nv_open_config_load_env(nv_open_config_t *config, nv_open_error_t *error);

void nv_open_config_free(nv_open_config_t *config);

#endif /* VIFM__NEOVIFM__OPEN_CONFIG_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
