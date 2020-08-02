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

#include "hll.h"
#include "little_endian.h"
#include "vm/page.h"
#include "xsystem4.h"

union acx_value {
	int32_t i;
	struct string *s;
};

struct acx_file {
	int compressed_size;
	int nr_columns;
	int nr_lines;
	int32_t *column_types;
	union acx_value *lines;
};

static struct acx_file acx;

static void acx_read(uint8_t *data_raw)
{
	acx.nr_columns = LittleEndian_getDW(data_raw, 0);
	acx.nr_lines = LittleEndian_getDW(data_raw, 4 + acx.nr_columns * 4);
	uint8_t *data = data_raw + (8 + acx.nr_columns * 4);

	acx.column_types = xcalloc(acx.nr_columns, sizeof(int32_t));
	for (int i = 0; i < acx.nr_columns; i++) {
		acx.column_types[i] = LittleEndian_getDW(data_raw, 4 + 4*i);
	}

	int ptr = 0;
	acx.lines = xcalloc(acx.nr_lines * acx.nr_columns, sizeof(union acx_value));
	for (int line = 0; line < acx.nr_lines; line++) {
		for (int col = 0; col < acx.nr_columns; col++) {
			if (acx.column_types[col] == 2) {
				int old_ptr = ptr;
				while (data[ptr])
					ptr++;
				acx.lines[line*acx.nr_columns + col].s = make_string((char*)data+old_ptr, ptr-old_ptr);
				ptr++;
			} else {
				acx.lines[line*acx.nr_columns + col].i = LittleEndian_getDW(data, ptr);
				ptr += 4;
			}
		}
	}
}

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

	int compressed_size = LittleEndian_getDW(buf, 8);
	unsigned long size = LittleEndian_getDW(buf, 12);
	uint8_t *data_raw = xmalloc(size);

	if (Z_OK != uncompress(data_raw, &size, buf+16, compressed_size)) {
		WARNING("ACXLoader.Load: uncompress failed");
		free(buf);
		free(data_raw);
		return false;
	}

	acx_read(data_raw);

	free(buf);
	free(data_raw);
	return true;
}

void ACXLoader_Release(void)
{
	for (int col = 0; col < acx.nr_columns; col++) {
		if (acx.column_types[col] != 2)
			continue;
		for (int line = 0; line < acx.nr_lines; line++) {
			free_string(acx.lines[line*acx.nr_columns+col].s);
		}
	}
	free(acx.lines);
	free(acx.column_types);
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

	*ptr = acx.lines[line*acx.nr_columns + col].i;
	return true;
}

bool ACXLoader_GetDataString(int line, int col, struct string **ptr)
{
	if (line < 0 || line >= acx.nr_lines || col < 0 || col >= acx.nr_columns)
		return false;

	*ptr = string_ref(acx.lines[line*acx.nr_columns + col].s);
	return true;
}

bool ACXLoader_GetDataStruct(int line, struct page **page)
{
	union vm_value *s = (*page)->values;

	if (line >= acx.nr_lines)
		return false;

	for (int i = 0; i < acx.nr_columns; i++) {
		if (acx.column_types[i] == 2) {
			s[i].i = vm_string_ref(acx.lines[line*acx.nr_columns + i].s);
		} else {
			s[i].i = acx.lines[line*acx.nr_columns + i].i;
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
