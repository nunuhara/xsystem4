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
#include "../little_endian.h"

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
hll_defun(Load, args)
{
	FILE *fp;
	long len;
	uint8_t *buf = NULL;
	char *path = gamedir_path(heap[args[0].i].s->text);

	if (!(fp = fopen(path, "rb"))) {
		WARNING("ACXLoader.Load: Failed to open %s", path);
		hll_return(false);
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
	if (strncmp((char*)buf, "ACX\0\0\0\0", 0)) {
		free(buf);
		hll_return(false);
	}

	acx.compressed_size = LittleEndian_getDW(buf, 8);
	acx.size = LittleEndian_getDW(buf, 12);
	acx.data_raw = xmalloc(acx.size);

	unsigned long size = acx.size;
	if (Z_OK != uncompress(acx.data_raw, &size, buf+16, len-16)) {
		WARNING("ACXLoader.Load: uncompress failed");
		hll_return(false);
	}

	acx.nr_columns = LittleEndian_getDW(acx.data_raw, 0);
	acx.nr_lines = LittleEndian_getDW(acx.data_raw, 4 + acx.nr_columns * 4);
	acx.data = acx.data_raw + (8 + acx.nr_columns * 4);

	hll_return(true);
}

//void Release()
hll_defun(Release, args)
{
	free(acx.data_raw);
	memset(&acx, 0, sizeof(struct acx_file));
	hll_return(0);
}

//int GetNumofLine()
hll_defun(GetNumofLine, args)
{
	hll_return(acx.nr_lines);
}

//int GetNumofColumn()
hll_defun(GetNumofColumn, args)
{
	hll_return(acx.nr_columns);
}

//bool GetDataInt(int nLine, int nColumn, ref int pnData)
hll_defun(GetDataInt, args)
{
	int line = args[0].i;
	int col = args[1].i;

	if (line < 0 || line >= acx.nr_lines || col < 0 || col >= acx.nr_columns)
		hll_return(false);

	uint8_t *data = acx.data + (line * acx.nr_columns * 4) + (col * 4);
	*args[2].iref = LittleEndian_getDW(data, 0);

	hll_return(true);
}

//bool GetDataString(int nLine, int nColumn, ref string pIData)
hll_unimplemented(ACXLoader, GetDataString);

//bool GetDataStruct(int nLine, ref struct pIVMStruct2)
hll_defun(GetDataStruct, args)
{
	int line = args[0].i;
	union vm_value *s = hll_struct_ref(args[1].i);

	if (line >= acx.nr_lines)
		hll_return(false);

	uint8_t *data = acx.data + (line * acx.nr_columns * 4);
	for (int i = 0; i < acx.nr_columns; i++) {
		s[i].i = LittleEndian_getDW(data, i*4);
	}

	hll_return(true);
}

hll_deflib(ACXLoader) {
	hll_export(Load),
	hll_export(Release),
	hll_export(GetNumofLine),
	hll_export(GetNumofColumn),
	hll_export(GetDataInt),
	hll_export(GetDataString),
	hll_export(GetDataStruct)
};
