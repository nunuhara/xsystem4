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
#include "hll.h"
#include "little_endian.h"
#include "system4/string.h"
#include "vm/page.h"

struct acx_file {
	int compressed_size;
	int size;
	int nr_columns;
	int nr_lines;
	uint8_t *data_raw;
	uint8_t *data;
};

static struct acx_file acx;

//bool Load(string pIFileName)
bool ACXLoader_Load(struct string *filename)
{
	FILE *fp;
	long len;
	uint8_t *buf = NULL;
	char *path = gamedir_path(filename->text);

	if (!(fp = fopen(path, "rb"))) {
		WARNING("ACXLoader.Load: Failed to open %s", path);
		free(path);
		return false;
	}
	free(path);

	// get size of ACX file
	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	// read ACX file into memory
	buf = xmalloc(len + 4);
	fread(buf, 1, len, fp);
	fclose(fp);

	// check magic
	if (strncmp((char*)buf, "ACX\0\0\0\0", 0)) {
		free(buf);
		return false;
	}

	acx.compressed_size = LittleEndian_getDW(buf, 8);
	acx.size = LittleEndian_getDW(buf, 12);
	acx.data_raw = xmalloc(acx.size);

	unsigned long size = acx.size;
	if (Z_OK != uncompress(acx.data_raw, &size, buf+16, len-16)) {
		WARNING("ACXLoader.Load: uncompress failed");
		free(buf);
		return false;
	}

	acx.nr_columns = LittleEndian_getDW(acx.data_raw, 0);
	acx.nr_lines = LittleEndian_getDW(acx.data_raw, 4 + acx.nr_columns * 4);
	acx.data = acx.data_raw + (8 + acx.nr_columns * 4);

	free(buf);
	return true;
}

void ACXLoader_Release(void)
{
	free(acx.data_raw);
	memset(&acx, 0, sizeof(struct acx_file));
}

int ACXLoader_GetNumofLine(void)
{
	return acx.nr_lines;
}

int ACXLoader_GetNumofColumn(void)
{
	return acx.nr_columns;
}

bool ACXLoader_GetDataInt(int line, int col, int *ptr)
{

	if (line < 0 || line >= acx.nr_lines || col < 0 || col >= acx.nr_columns)
		return false;

	uint8_t *data = acx.data + (line * acx.nr_columns * 4) + (col * 4);
	*ptr = LittleEndian_getDW(data, 0);

	return true;
}

HLL_UNIMPLEMENTED(bool, ACXLoader, GetDataString, int line, int col, struct string *data);

bool ACXLoader_GetDataStruct(int line, struct page **page)
{
	union vm_value *s = (*page)->values;

	if (line >= acx.nr_lines)
		return false;

	uint8_t *data = acx.data + (line * acx.nr_columns * 4);
	for (int i = 0; i < acx.nr_columns; i++) {
		s[i].i = LittleEndian_getDW(data, i*4);
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
