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
#include <errno.h>
#include <zlib.h>

#include "system4.h"
#include "system4/string.h"
#include "system4/acx.h"
#include "system4/file.h"
#include "system4/little_endian.h"

#include "hll.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

static struct acx *acx = NULL;

static bool ACXLoader_Load(struct string *filename)
{
	int error = ACX_SUCCESS;
	char *path = gamedir_path(filename->text);
	acx = acx_load(path, &error);

	// XXX: Fix for games (e.g. Tsuma Shibori) that shipped with incorrectly
	//      cased file names
	if (error == ACX_ERROR_FILE) {
		char *ipath = path_get_icase(path);
		if (ipath) {
			free(path);
			path = ipath;
			acx = acx_load(path, &error);
		}
	}

	if (error == ACX_ERROR_FILE) {
		WARNING("acx_load(\"%s\"): %s", display_utf0(path), strerror(errno));
	} else if (error == ACX_ERROR_INVALID) {
		WARNING("acx_load(\"%s\"): invalid .acx file", display_utf0(path));
	}
	free(path);
	return !!acx;
}

static void ACXLoader_Release(void)
{
	acx_free(acx);
	acx = NULL;
}

static int ACXLoader_GetNumofLine(void)
{
	return acx ? acx->nr_lines : 0;
}

static int ACXLoader_GetNumofColumn(void)
{
	return acx ? acx->nr_columns : 0;
}

static bool ACXLoader_GetDataInt(int line, int col, int *ptr)
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

static bool ACXLoader_GetDataString(int line, int col, struct string **ptr)
{
	if (!acx)
		return false;
	if (line < 0 || line >= acx->nr_lines)
		return false;
	if (col < 0 || col >= acx->nr_columns)
		return false;
	if (*ptr)
		free_string(*ptr);
	*ptr = string_ref(acx_get_string(acx, line, col));
	return true;
}

static bool ACXLoader_GetDataStruct(int line, struct page **page)
{
	if (!acx)
		return false;
	if (line < 0 || line >= acx->nr_lines)
		return false;

	union vm_value *s = (*page)->values;
	int nr_columns = acx->nr_columns;
	for (int i = 0; i < nr_columns; i++) {
		if (acx->column_types[i] == ACX_STRING) {
			if (heap[s[i].i].s) {
				free_string(heap[s[i].i].s);
			}
			heap[s[i].i].s = string_ref(acx_get_string(acx, line, i));
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

HLL_WARN_UNIMPLEMENTED(, void, ACXLoaderP2, GetError, int *nError, struct string **szErrorString);

static bool ACXLoaderP2_GetDataIntByKey(int key, int col, int *ptr)
{
	if (!acx)
		return false;
	if (col < 0 || col >= acx->nr_columns)
		return false;
	for (int line = 0; line < acx->nr_lines; line++) {
		if (key == acx_get_int(acx, line, 0)) {
			*ptr = acx_get_int(acx, line, col);
			return true;
		}
	}
	return false;
}

static bool ACXLoaderP2_GetDataStringByKey(int key, int col, struct string **ptr)
{
	if (!acx)
		return false;
	if (col < 0 || col >= acx->nr_columns)
		return false;
	for (int line = 0; line < acx->nr_lines; line++) {
		if (key == acx_get_int(acx, line, 0)) {
			if (*ptr)
				free_string(*ptr);
			*ptr = string_ref(acx_get_string(acx, line, col));
			return true;
		}
	}
	return false;
}

HLL_LIBRARY(ACXLoaderP2,
	    HLL_EXPORT(Load, ACXLoader_Load),
	    HLL_EXPORT(Release, ACXLoader_Release),
	    HLL_EXPORT(GetNumofLine, ACXLoader_GetNumofLine),
	    HLL_EXPORT(GetNumofColumn, ACXLoader_GetNumofColumn),
	    HLL_EXPORT(GetDataInt, ACXLoader_GetDataInt),
	    HLL_EXPORT(GetDataString, ACXLoader_GetDataString),
	    HLL_EXPORT(GetDataStruct, ACXLoader_GetDataStruct),
	    HLL_EXPORT(GetError, ACXLoaderP2_GetError),
	    HLL_EXPORT(GetDataIntByKey, ACXLoaderP2_GetDataIntByKey),
	    HLL_EXPORT(GetDataStringByKey, ACXLoaderP2_GetDataStringByKey)
	    );
