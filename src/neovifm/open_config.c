/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 or 3 of the License.
 */

#include "open_config.h"

#include <ctype.h> /* isspace() */
#include <stdio.h> /* FILE fclose fopen fgets ferror */
#include <stdlib.h> /* free getenv realloc */
#include <string.h> /* memcpy strchr strlen strncmp strdup */

typedef struct
{
	const char *name;
	nv_open_association_kind_t kind;
} association_keyword_t;

static const association_keyword_t association_keywords[] = {
	{ "filetype", NV_OPEN_ASSOC_FILETYPE },
	{ "filextype", NV_OPEN_ASSOC_FILEXTYPE },
	{ "fileviewer", NV_OPEN_ASSOC_FILEVIEWER },
};

static int set_error(nv_open_error_t *error, const char code[],
		const char message[]);
static int bounded_path(const char path[]);
static char *skip_space(char value[]);
static void trim_right(char value[]);
static int append_logical(char logical[], size_t *length, const char value[],
		nv_open_error_t *error);
static int collect_pattern(char ***patterns, size_t *pattern_count,
		const char value[], size_t length, nv_open_error_t *error);
static int collect_pattern_token(char ***patterns, size_t *pattern_count,
		char token[], nv_open_error_t *error);
static int add_rule(nv_open_config_t *config, nv_open_association_kind_t kind,
		const char pattern[], const char command[], nv_open_error_t *error);
static int parse_statement(char statement[], nv_open_config_t *config,
		nv_open_error_t *error);
static int flush_statement(char logical[], size_t *logical_length,
		int *have_statement, nv_open_config_t *config, nv_open_error_t *error);

static int
set_error(nv_open_error_t *error, const char code[], const char message[])
{
	if(error == NULL) return -1;
	nv_open_error_free(error);
	error->code = strdup(code);
	error->message = strdup(message);
	if(error->code == NULL || error->message == NULL)
	{
		nv_open_error_free(error);
	}
	return -1;
}

static int
bounded_path(const char path[])
{
	if(path == NULL || path[0] == '\0') return 0;
	for(size_t i = 0U; i <= NV_OPEN_MAX_ARG_BYTES; ++i)
	{
		if(path[i] == '\0') return 1;
	}
	return 0;
}

static char *
skip_space(char value[])
{
	while(*value != '\0' && isspace((unsigned char)*value)) ++value;
	return value;
}

static void
trim_right(char value[])
{
	size_t length = strlen(value);
	while(length > 0U && isspace((unsigned char)value[length - 1U]))
	{
		value[--length] = '\0';
	}
}

static int
append_logical(char logical[], size_t *length, const char value[],
		nv_open_error_t *error)
{
	const size_t value_length = strlen(value);
	if(value_length > NV_OPEN_CONFIG_MAX_LINE_BYTES ||
			*length > NV_OPEN_CONFIG_MAX_LINE_BYTES - value_length)
	{
		return set_error(error, "config-line-too-large",
				"MYVIFMRC logical line is too long");
	}
	memcpy(logical + *length, value, value_length + 1U);
	*length += value_length;
	return 0;
}

static int
collect_pattern(char ***patterns, size_t *pattern_count, const char value[],
		size_t length, nv_open_error_t *error)
{
	while(length > 0U && isspace((unsigned char)value[0]))
	{
		++value;
		--length;
	}
	while(length > 0U && isspace((unsigned char)value[length - 1U])) --length;
	if(length == 0U || (value[0] == '<' && value[length - 1U] == '>'))
	{
		return 0;
	}
	if(length > NV_OPEN_MAX_PATTERN_BYTES)
	{
		return set_error(error, "config-invalid",
				"MYVIFMRC pattern is too long");
	}
	for(size_t i = 0U; i < length; ++i)
	{
		const unsigned char character = (unsigned char)value[i];
		if(character < 0x20U || character == 0x7fU)
		{
			return set_error(error, "config-invalid",
					"MYVIFMRC pattern contains a control character");
		}
	}
	if(*pattern_count >= NV_OPEN_MAX_ASSOCIATIONS)
	{
		return set_error(error, "association-too-large",
				"MYVIFMRC has too many association patterns");
	}
	char *const copy = malloc(length + 1U);
	if(copy == NULL)
	{
		return set_error(error, "out-of-memory",
				"failed to copy MYVIFMRC pattern");
	}
	memcpy(copy, value, length);
	copy[length] = '\0';
	char **const resized = realloc(*patterns,
			(*pattern_count + 1U)*sizeof(**patterns));
	if(resized == NULL)
	{
		free(copy);
		return set_error(error, "out-of-memory",
				"failed to allocate MYVIFMRC patterns");
	}
	*patterns = resized;
	(*patterns)[(*pattern_count)++] = copy;
	return 0;
}

static int
collect_pattern_token(char ***patterns, size_t *pattern_count, char token[],
		nv_open_error_t *error)
{
	char *part = token;
	for(;;)
	{
		char *const comma = strchr(part, ',');
		if(comma != NULL) *comma = '\0';
		if(collect_pattern(patterns, pattern_count, part, strlen(part), error) != 0)
		{
			return -1;
		}
		if(comma == NULL) break;
		part = comma + 1U;
	}
	return 0;
}

static int
add_rule(nv_open_config_t *config, nv_open_association_kind_t kind,
		const char pattern[], const char command[], nv_open_error_t *error)
{
	if(config->rule_count >= NV_OPEN_MAX_ASSOCIATIONS)
	{
		return set_error(error, "association-too-large",
				"MYVIFMRC has too many association rules");
	}
	char *const pattern_copy = strdup(pattern);
	char *const command_copy = strdup(command);
	if(pattern_copy == NULL || command_copy == NULL)
	{
		free(pattern_copy);
		free(command_copy);
		return set_error(error, "out-of-memory",
				"failed to copy MYVIFMRC association");
	}
	nv_open_association_rule_t *const resized_rules = realloc(config->rules,
			(config->rule_count + 1U)*sizeof(*config->rules));
	if(resized_rules == NULL)
	{
		free(pattern_copy);
		free(command_copy);
		return set_error(error, "out-of-memory",
				"failed to allocate MYVIFMRC rules");
	}
	config->rules = resized_rules;
	char **const resized_strings = realloc(config->owned_strings,
			(config->owned_string_count + 2U)*sizeof(*config->owned_strings));
	if(resized_strings == NULL)
	{
		free(pattern_copy);
		free(command_copy);
		return set_error(error, "out-of-memory",
				"failed to allocate MYVIFMRC rule storage");
	}
	config->owned_strings = resized_strings;
	config->owned_strings[config->owned_string_count++] = pattern_copy;
	config->owned_strings[config->owned_string_count++] = command_copy;
	config->rules[config->rule_count++] = (nv_open_association_rule_t){
		.kind = kind,
		.pattern = pattern_copy,
		.command = command_copy,
	};
	return 0;
}

static int
parse_statement(char statement[], nv_open_config_t *config,
		nv_open_error_t *error)
{
	char *cursor = skip_space(statement);
	if(*cursor == '\0' || *cursor == '"' || *cursor == '#') return 0;
	nv_open_association_kind_t kind;
	const size_t keyword_count = sizeof(association_keywords)/
		sizeof(association_keywords[0]);
	for(size_t i = 0U; i < keyword_count; ++i)
	{
		const size_t keyword_length = strlen(association_keywords[i].name);
		if(strncmp(cursor, association_keywords[i].name, keyword_length) == 0 &&
				(cursor[keyword_length] == '\0' ||
				 isspace((unsigned char)cursor[keyword_length])))
		{
			kind = association_keywords[i].kind;
			cursor += keyword_length;
			break;
		}
		if(i + 1U == keyword_count) return 0;
	}
	cursor = skip_space(cursor);
	char **patterns = NULL;
	size_t pattern_count = 0U;
	if(*cursor == '{')
	{
		char *const content = cursor + 1U;
		char *close = content;
		int escaped = 0;
		while(*close != '\0')
		{
			if(!escaped && *close == '}') break;
			escaped = !escaped && *close == '\\';
			if(*close != '\\') escaped = 0;
			++close;
		}
		if(*close != '}')
		{
			return set_error(error, "config-invalid",
					"MYVIFMRC pattern set is not closed");
		}
		char saved = *close;
		*close = '\0';
		if(collect_pattern_token(&patterns, &pattern_count, content, error) != 0)
		{
			*close = saved;
			goto free_patterns;
		}
		*close = saved;
		cursor = close + 1U;
		while(*cursor == ',')
		{
			char *selector = cursor + 1U;
			selector = skip_space(selector);
			if(*selector != '<') break;
			char *const selector_end = strchr(selector + 1U, '>');
			if(selector_end == NULL)
			{
				set_error(error, "config-invalid",
						"MYVIFMRC MIME selector is not closed");
				goto free_patterns;
			}
			cursor = selector_end + 1U;
		}
	}
	else
	{
		char *const token_start = cursor;
		while(*cursor != '\0' && !isspace((unsigned char)*cursor)) ++cursor;
		const char saved = *cursor;
		*cursor = '\0';
		if(collect_pattern_token(&patterns, &pattern_count, token_start, error) != 0)
		{
			*cursor = saved;
			goto free_patterns;
		}
		*cursor = saved;
	}
	cursor = skip_space(cursor);
	if(*cursor == '{')
	{
		char *close = cursor + 1U;
		int escaped = 0;
		while(*close != '\0')
		{
			if(!escaped && *close == '}') break;
			escaped = !escaped && *close == '\\';
			if(*close != '\\') escaped = 0;
			++close;
		}
		if(*close != '}')
		{
			set_error(error, "config-invalid",
					"MYVIFMRC command description is not closed");
			goto free_patterns;
		}
		cursor = skip_space(close + 1U);
	}
	if(pattern_count == 0U || *cursor == '\0') goto free_patterns;
	char quote = '\0';
	int escaped = 0;
	char *command_end = cursor;
	for(; *command_end != '\0'; ++command_end)
	{
		if(escaped)
		{
			escaped = 0;
			continue;
		}
		if(*command_end == '\\')
		{
			escaped = 1;
			continue;
		}
		if(quote != '\0')
		{
			if(*command_end == quote) quote = '\0';
			continue;
		}
		if(*command_end == '\'' || *command_end == '"')
		{
			quote = *command_end;
			continue;
		}
		if(*command_end == ',') break;
	}
	char saved = *command_end;
	*command_end = '\0';
	trim_right(cursor);
	if(*cursor != '\0')
	{
		for(size_t i = 0U; i < pattern_count; ++i)
		{
			if(add_rule(config, kind, patterns[i], cursor, error) != 0)
			{
				*command_end = saved;
				goto free_patterns;
			}
		}
	}
	*command_end = saved;

free_patterns:
	for(size_t i = 0U; i < pattern_count; ++i) free(patterns[i]);
	free(patterns);
	return error->code == NULL ? 0 : -1;
}

static int
flush_statement(char logical[], size_t *logical_length, int *have_statement,
		nv_open_config_t *config, nv_open_error_t *error)
{
	if(!*have_statement) return 0;
	logical[*logical_length] = '\0';
	const int result = parse_statement(logical, config, error);
	*logical_length = 0U;
	logical[0] = '\0';
	*have_statement = 0;
	return result;
}

int
nv_open_config_load(const char path[], nv_open_config_t *config,
		nv_open_error_t *error)
{
	if(config == NULL || error == NULL) return -1;
	nv_open_config_free(config);
	nv_open_error_free(error);
	if(!bounded_path(path))
	{
		return set_error(error, "invalid-config-path",
				"MYVIFMRC path is empty");
	}
	FILE *const fp = fopen(path, "rb");
	if(fp == NULL)
	{
		return set_error(error, "config-open-failed",
				"failed to open MYVIFMRC");
	}
	char raw[NV_OPEN_CONFIG_MAX_LINE_BYTES + 2U];
	char logical[NV_OPEN_CONFIG_MAX_LINE_BYTES + 1U] = "";
	size_t logical_length = 0U;
	size_t total_bytes = 0U;
	int have_statement = 0;
	int result = 0;
	while(fgets(raw, sizeof(raw), fp) != NULL)
	{
		const size_t raw_length = strlen(raw);
		const int has_newline = raw_length > 0U && raw[raw_length - 1U] == '\n';
		const size_t content_length = has_newline ? raw_length - 1U : raw_length;
		total_bytes += raw_length;
		if(total_bytes > NV_OPEN_CONFIG_MAX_BYTES)
		{
			set_error(error, "config-too-large", "MYVIFMRC exceeds the size limit");
			result = -1;
			break;
		}
		if(content_length > NV_OPEN_CONFIG_MAX_LINE_BYTES ||
				(!has_newline && !feof(fp)))
		{
			set_error(error, "config-line-too-large",
					"MYVIFMRC line exceeds the size limit");
			result = -1;
			break;
		}
		if(raw[raw_length - 1U] == '\n') raw[raw_length - 1U] = '\0';
		trim_right(raw);
		char *line = skip_space(raw);
		if(*line == '"' || *line == '#') continue;
		if(*line == '\0')
		{
			if(flush_statement(logical, &logical_length, &have_statement,
					config, error) != 0)
			{
				result = -1;
				break;
			}
			continue;
		}
		if(*line == '\\')
		{
			if(!have_statement)
			{
				set_error(error, "config-invalid",
						"MYVIFMRC continuation has no command");
				result = -1;
				break;
			}
			if(append_logical(logical, &logical_length, line + 1U, error) != 0)
			{
				result = -1;
				break;
			}
			continue;
		}
		if(flush_statement(logical, &logical_length, &have_statement,
				config, error) != 0 ||
				append_logical(logical, &logical_length, line, error) != 0)
		{
			result = -1;
			break;
		}
		have_statement = 1;
	}
	if(result == 0 && ferror(fp))
	{
		set_error(error, "config-read-failed", "failed to read MYVIFMRC");
		result = -1;
	}
	if(result == 0 && flush_statement(logical, &logical_length, &have_statement,
			config, error) != 0)
	{
		result = -1;
	}
	fclose(fp);
	if(result != 0)
	{
		nv_open_config_free(config);
	}
	return result;
}

int
nv_open_config_load_env(nv_open_config_t *config, nv_open_error_t *error)
{
	if(config == NULL || error == NULL) return -1;
	nv_open_config_free(config);
	nv_open_error_free(error);
	const char *const path = getenv("MYVIFMRC");
	if(path == NULL || path[0] == '\0') return 0;
	return nv_open_config_load(path, config, error);
}

void
nv_open_config_free(nv_open_config_t *config)
{
	if(config == NULL) return;
	for(size_t i = 0U; i < config->owned_string_count; ++i)
	{
		free(config->owned_strings[i]);
	}
	free(config->owned_strings);
	free(config->rules);
	memset(config, 0, sizeof(*config));
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
