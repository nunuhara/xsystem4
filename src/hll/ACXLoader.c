/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include "system4.h"
#include "system4/string.h"
#include "system4/acx.h"

#include "hll.h"
#include "little_endian.h"
#include "vm/page.h"
#include "xsystem4.h"

struct acx *acx = NULL;

bool ACXLoader_Load(struct string *filename)
{
	char *path = gamedir_path(filename->text);
	acx = acx_load(path);
	free(path);
	return !!acx;
}

void ACXLoader_Release(void)
{
	acx_free(acx);
	acx = NULL;
}

int ACXLoader_GetNumofLine(void)
{
	return acx ? acx->nr_lines : 0;
}

int ACXLoader_GetNumofColumn(void)
{
	return acx ? acx->nr_columns : 0;
}

bool ACXLoader_GetDataInt(int line, int col, int *ptr)
{
	if (!acx)
		return false;
	if (line < 0 || line >= acx->nr_lines)
		return false;
	if (col < 0 || col >= acx->nr_columns)
		return false;
	*ptr = acx_get_int(acx, line, col);
	return true;
}

bool ACXLoader_GetDataString(int line, int col, struct string **ptr)
{
	if (!acx)
		return false;
	if (line < 0 || line >= acx->nr_lines)
		return false;
	if (col < 0 || col >= acx->nr_columns)
		return false;
	*ptr = string_ref(acx_get_string(acx, line, col));
	return true;
}

bool ACXLoader_GetDataStruct(int line, struct page **page)
{
	if (!acx)
		return false;
	if (line < 0 || line >= acx->nr_lines)
		return false;

	union vm_value *s = (*page)->values;
	int nr_columns = acx->nr_columns;
	for (int i = 0; i < nr_columns; i++) {
		if (acx->column_types[i] == ACX_STRING) {
			s[i].i = vm_string_ref(acx_get_string(acx, line, i));
		} else {
			s[i].i = acx_get_int(acx, line, i);
		}
	}

	return true;
}

HLL_LIBRARY(ACXLoader,
	    HLL_EXPORT(Load, ACXLoader_Load),
	    HLL_EXPORT(Release, ACXLoader_Release),
	    HLL_EXPORT(GetNumofLine, ACXLoader_GetNumofLine),
	    HLL_EXPORT(GetNumofColumn, ACXLoader_GetNumofColumn),
	    HLL_EXPORT(GetDataInt, ACXLoader_GetDataInt),
	    HLL_EXPORT(GetDataString, ACXLoader_GetDataString),
	    HLL_EXPORT(GetDataStruct, ACXLoader_GetDataStruct));
