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
#include <assert.h>
#include <zlib.h>

#include "system4.h"
#include "system4/acx.h"
#include "system4/string.h"
#include "little_endian.h"

static struct acx *acx_read(uint8_t *data_raw)
{
	struct acx *acx = xcalloc(1, sizeof(struct acx));
	acx->nr_columns = LittleEndian_getDW(data_raw, 0);
	acx->nr_lines = LittleEndian_getDW(data_raw, 4 + acx->nr_columns * 4);
	uint8_t *data = data_raw + (8 + acx->nr_columns * 4);

	acx->column_types = xcalloc(acx->nr_columns, sizeof(int32_t));
	for (int i = 0; i < acx->nr_columns; i++) {
		acx->column_types[i] = LittleEndian_getDW(data_raw, 4 + 4*i);
		if (acx->column_types[i] != ACX_INT && acx->column_types[i] != ACX_STRING)
			WARNING("Column %d has unknown type (%d)", i, acx->column_types[i]);
	}

	int ptr = 0;
	acx->lines = xcalloc(acx->nr_lines * acx->nr_columns, sizeof(union acx_value));
	for (int line = 0; line < acx->nr_lines; line++) {
		for (int col = 0; col < acx->nr_columns; col++) {
			if (acx->column_types[col] == 2) {
				int old_ptr = ptr;
				while (data[ptr])
					ptr++;
				acx->lines[line*acx->nr_columns + col].s = make_string((char*)data+old_ptr, ptr-old_ptr);
				ptr++;
			} else {
				acx->lines[line*acx->nr_columns + col].i = LittleEndian_getDW(data, ptr);
				ptr += 4;
			}
		}
	}

	return acx;
}

struct acx *acx_load(const char *path)
{
	FILE *fp;
	long len;
	uint8_t *buf = NULL;

	if (!(fp = fopen(path, "rb"))) {
		WARNING("ACXLoader.Load: Failed to open %s", path);
		return NULL;
	}

	// get size of ACX file
	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	// read ACX file into memory
	buf = xmalloc(len + 4);
	fread(buf, 1, len, fp);
	fclose(fp);

	// check magic
	if (strncmp((char*)buf, "ACX\0\0\0\0", 8)) {
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
		return NULL;
	}

	struct acx *acx = acx_read(data_raw);
	free(buf);
	free(data_raw);
	return acx;
}

void acx_free(struct acx *acx)
{
	if (!acx)
		return;

	for (int col = 0; col < acx->nr_columns; col++) {
		if (acx->column_types[col] != 2)
			continue;
		for (int line = 0; line < acx->nr_lines; line++) {
			free_string(acx->lines[line*acx->nr_columns+col].s);
		}
	}
	free(acx->lines);
	free(acx->column_types);
	free(acx);
}

int acx_nr_lines(struct acx *acx)
{
	return acx->nr_lines;
}

int acx_nr_columns(struct acx *acx)
{
	return acx->nr_columns;
}

enum acx_column_type acx_column_type(struct acx *acx, int col)
{
	assert(col >= 0);
	assert(col < acx->nr_columns);
	return acx->column_types[col];
}

int acx_get_int(struct acx *acx, int line, int col)
{
	assert(line >= 0);
	assert(line < acx->nr_lines);
	assert(col >= 0);
	assert(col < acx->nr_columns);
	return acx->lines[line*acx->nr_columns + col].i;
}

struct string *acx_get_string(struct acx *acx, int line, int col)
{
	assert(line >= 0);
	assert(line < acx->nr_lines);
	assert(col >= 0);
	assert(col < acx->nr_columns);
	return acx->lines[line*acx->nr_columns + col].s;
}


