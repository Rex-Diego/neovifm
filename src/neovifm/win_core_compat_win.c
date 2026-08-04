/* vifm
 * Copyright (C) 2026 NeoVifm contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdint.h>
#include <wchar.h>

#include "../utils/utf8proc.h"
#include "../utils/utils.h"

int
vifm_wcwidth(wchar_t character)
{
	const int width = utf8proc_charwidth((utf8proc_int32_t)character);
	if(width < 0) return (uint32_t)character < (uint32_t)L' ' ? 2 : 1;
	return width;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
