/* Copyright (C) 2021 kichikuou <KichikuouChrome@gmail.com>
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

#include <string.h>
#include <stdio.h>
#include <zlib.h>

#include "system4/archive.h"
#include "system4/buffer.h"
#include "system4/mt19937int.h"
#include "system4/string.h"

#include "hll.h"
#include "asset_manager.h"
#include "xsystem4.h"

enum value_type {
	GDAT_INT = 0,
	GDAT_STRING = 2,
	GDAT_FLOAT = 3
};

struct gdat_col {
	enum value_type type;
	uint32_t index;
};

struct gdat_row {
	struct gdat_col *cols;
	uint32_t nr_cols;
};

struct gdat_table {
	struct gdat_row *rows;
	uint32_t nr_rows;
};

struct gdat {
	char **labels;
	uint32_t nr_labels;
	struct gdat_table *tables;
	uint32_t nr_tables;
	int32_t *ints;
	uint32_t nr_ints;
	float *floats;
	uint32_t nr_floats;
	struct string **strings;
	uint32_t nr_strings;
};

static struct gdat *current_gdat;

static void gdat_free(struct gdat *dat)
{
	if (!dat)
		return;
	if (dat->labels) {
		for (uint32_t i = 0; i < dat->nr_labels; i++)
			free(dat->labels[i]);
		free(dat->labels);
	}
	if (dat->tables) {
		for (uint32_t i = 0; i < dat->nr_tables; i++) {
			for (uint32_t j = 0; j < dat->tables[i].nr_rows; j++) {
				free(dat->tables[i].rows[j].cols);
			}
			free(dat->tables[i].rows);
		}
		free(dat->tables);
	}
	if (dat->ints)
		free(dat->ints);
	if (dat->floats)
		free(dat->floats);
	if (dat->strings) {
		for (uint32_t i = 0; i < dat->nr_strings; i++) {
			if (dat->strings[i])
				free_string(dat->strings[i]);
		}
		free(dat->strings);
	}
	free(dat);
}

static struct gdat *gdat_read(struct buffer *r)
{
	if (strncmp(buffer_strdata(r), "GDAT", 4)) {
		WARNING("Missing GDAT section marker");
		return NULL;
	}
	buffer_skip(r, 4);

	int32_t version = buffer_read_int32(r);
	if (version != 1) {
		WARNING("Unknown GDAT version %d", version);
		return NULL;
	}

	struct gdat *dat = xcalloc(1, sizeof(struct gdat));

	if (strncmp(buffer_strdata(r), "LABL", 4)) {
		WARNING("Missing LABL section marker");
		gdat_free(dat);
		return NULL;
	}
	buffer_skip(r, 4);

	dat->nr_labels = buffer_read_int32(r);
	dat->labels = xcalloc(dat->nr_labels, sizeof(const char *));
	for (uint32_t i = 0; i < dat->nr_labels; i++) {
		dat->labels[i] = xstrdup(buffer_strdata(r));
		buffer_skip(r, strlen(dat->labels[i]) + 1);
	}

	if (strncmp(buffer_strdata(r), "DATA", 4)) {
		WARNING("Missing DATA section marker");
		gdat_free(dat);
		return NULL;
	}
	buffer_skip(r, 4);

	dat->nr_tables = buffer_read_int32(r);
	dat->tables = xcalloc(dat->nr_tables, sizeof(struct gdat_table));
	for (uint32_t i = 0; i < dat->nr_tables; i++) {
		struct gdat_table *tbl = &dat->tables[i];
		tbl->nr_rows = buffer_read_int32(r);
		tbl->rows = xcalloc(tbl->nr_rows, sizeof(struct gdat_row));
		for (uint32_t j = 0; j < tbl->nr_rows; j++) {
			struct gdat_row *row = &tbl->rows[j];
			row->nr_cols = buffer_read_int32(r);
			row->cols = xcalloc(row->nr_cols, sizeof(struct gdat_col));
			for (uint32_t k = 0; k < row->nr_cols; k++) {
				row->cols[k].type = buffer_read_int32(r);
				row->cols[k].index = buffer_read_int32(r);
			}
		}
	}

	if (strncmp(buffer_strdata(r), "INT_", 4) == 0) {
		buffer_skip(r, 4);
		dat->nr_ints = buffer_read_int32(r);
		dat->ints = xmalloc(dat->nr_ints * sizeof(int32_t));
		for (uint32_t i = 0; i < dat->nr_ints; i++)
			dat->ints[i] = buffer_read_int32(r);
	}

	if (strncmp(buffer_strdata(r), "FLOT", 4) == 0) {
		buffer_skip(r, 4);
		dat->nr_floats = buffer_read_int32(r);
		dat->floats = xmalloc(dat->nr_floats * sizeof(float));
		for (uint32_t i = 0; i < dat->nr_floats; i++)
			dat->floats[i] = buffer_read_float(r);
	}

	if (strncmp(buffer_strdata(r), "STRG", 4) == 0) {
		buffer_skip(r, 4);
		dat->nr_strings = buffer_read_int32(r);
		dat->strings = xcalloc(dat->nr_strings, sizeof(struct string*));
		for (uint32_t i = 0; i < dat->nr_strings; i++) {
			dat->strings[i] = cstr_to_string(buffer_strdata(r));
			buffer_skip(r, dat->strings[i]->size + 1);
		}
	}

	if (buffer_remaining(r)) {
		WARNING("Unknown GDAT section header");
		gdat_free(dat);
		return NULL;
	}
	return dat;
}

static struct gdat_table *gdat_get_table(struct gdat *dat, int table)
{
	if (!dat || table < 0 || (uint32_t)table >= dat->nr_tables) {
		WARNING("Table index out of bounds: %d", table);
		return NULL;
	}
	return &dat->tables[table];
}

static struct gdat_row *gdat_get_row(struct gdat *dat, int table, int row)
{
	struct gdat_table *tbl = gdat_get_table(dat, table);
	if (!tbl)
		return NULL;
	if (row < 0 || (uint32_t)row >= tbl->nr_rows) {
		WARNING("Row index out of bounds: %d", row);
		return NULL;
	}
	return &tbl->rows[row];
}

static struct gdat_col *gdat_get_column(struct gdat *dat, int table, int row, int col)
{
	struct gdat_row *r = gdat_get_row(dat, table, row);
	if (!r)
		return NULL;
	if (col < 0 || (uint32_t)col >= r->nr_cols) {
		WARNING("Column index out of bounds: %d", col);
		return NULL;
	}
	return &r->cols[col];
}

// for debug
possibly_unused static void gdat_dump_as_json(struct gdat *dat, FILE *fp)
{
	fputs("{\n", fp);
	for (uint32_t i = 0; i < dat->nr_tables; i++) {
		fprintf(fp, "  \"%s\": [\n", dat->labels[i]);
		struct gdat_table *tbl = &dat->tables[i];
		for (uint32_t j = 0; j < tbl->nr_rows; j++) {
			fputs("    [", fp);
			struct gdat_row *row = &tbl->rows[j];
			for (uint32_t k = 0; k < row->nr_cols; k++) {
				struct gdat_col *col = &row->cols[k];
				switch (col->type) {
				case GDAT_INT:
					fprintf(fp, "%d", dat->ints[col->index]);
					break;
				case GDAT_FLOAT:
					fprintf(fp, "%f", dat->floats[col->index]);
					break;
				case GDAT_STRING:
					fprintf(fp, "\"%s\"", dat->strings[col->index]->text);
					break;
				}
				if (k < row->nr_cols - 1)
					fputs(", ", fp);
			}
			fprintf(fp, "]%s\n", j < tbl->nr_rows - 1 ? "," : "");
		}
		fprintf(fp, "  ]%s\n", i < dat->nr_labels - 1 ? "," : "");
	}
	fputs("}\n", fp);
}

static int DataFile_Open(int link)
{
	struct archive_data *dfile = asset_get(ASSET_DATA, link);
	if (!dfile) {
		WARNING("Failed to load data file %d", link);
		return 0;
	}

	struct buffer r;
	buffer_init(&r, dfile->data, dfile->size);

	if (strncmp(buffer_strdata(&r), "DD\x01\x01", 4)) {
		WARNING("Invalid data file");
		archive_free_data(dfile);
		return 0;
	}
	buffer_skip(&r, 4);

	unsigned long uncompressed_size = buffer_read_int32(&r);
	uint32_t compressed_size = buffer_remaining(&r);

	// decode compressed data
	uint8_t *compressed_data = xmalloc(compressed_size);
	buffer_read_bytes(&r, compressed_data, compressed_size);
	archive_free_data(dfile);
	mt19937_xorcode(compressed_data, compressed_size, 4588163);

	uint8_t *raw = xmalloc(uncompressed_size);
	int rv = uncompress(raw, &uncompressed_size, compressed_data, compressed_size);
	free(compressed_data);
	if (rv != Z_OK) {
		WARNING("Uncompress failed: %d", rv);
		free(raw);
		return 0;
	}

	buffer_init(&r, raw, uncompressed_size);
	gdat_free(current_gdat);
	current_gdat = gdat_read(&r);

	free(raw);
	return current_gdat ? 1 : 0;
}

static int DataFile_SarchLabel(struct string *label)
{
	if (!current_gdat)
		return -1;
	for (uint32_t i = 0; i < current_gdat->nr_labels; i++) {
		if (!strcmp(current_gdat->labels[i], label->text))
			return i;
	}
	WARNING("Label '%s' not found", label->text);
	return -1;
}

static int DataFile_GetNumofVertical(int label)
{
	struct gdat_table *tbl = gdat_get_table(current_gdat, label);
	return tbl ? (int)tbl->nr_rows : -1;
}

static int DataFile_GetNumofHorizontal(int label, int vartical)
{
	struct gdat_row *row = gdat_get_row(current_gdat, label, vartical);
	return row ? (int)row->nr_cols : -1;
}

static int DataFile_SarchString(int label, int horizontal, struct string *data)
{
	struct gdat_table *tbl = gdat_get_table(current_gdat, label);
	if (!tbl)
		return -1;
	if (horizontal < 0)
		return -1;
	for (uint32_t i = 0; i < tbl->nr_rows; i++) {
		struct gdat_row *row = &tbl->rows[i];
		if ((uint32_t)horizontal < row->nr_cols &&
			row->cols[horizontal].type == GDAT_STRING &&
			!strcmp(current_gdat->strings[row->cols[horizontal].index]->text, data->text))
			return i;
	}
	return -1;
}

static int DataFile_SarchInt(int label, int horizontal, int data)
{
	struct gdat_table *tbl = gdat_get_table(current_gdat, label);
	if (!tbl)
		return -1;
	if (horizontal < 0)
		return -1;
	for (uint32_t i = 0; i < tbl->nr_rows; i++) {
		struct gdat_row *row = &tbl->rows[i];
		if ((uint32_t)horizontal < row->nr_cols &&
			row->cols[horizontal].type == GDAT_INT &&
			current_gdat->ints[row->cols[horizontal].index] == data)
			return i;
	}
	return -1;
}

static int DataFile_GetInt(int label, int vartical, int horizontal)
{
	struct gdat_col *col = gdat_get_column(current_gdat, label, vartical, horizontal);
	if (!col)
		return -1;

	if (col->type != GDAT_INT) {
		WARNING("Value is not an int");
		return -1;
	}
	if (col->index >= current_gdat->nr_ints) {
		WARNING("Broken GDAT table; integer index %d out of bounds", col->index);
		return -1;
	}
	return current_gdat->ints[col->index];
}

static float DataFile_GetFloat(int label, int vartical, int horizontal)
{
	struct gdat_col *col = gdat_get_column(current_gdat, label, vartical, horizontal);
	if (!col)
		return -1.0;

	if (col->type != GDAT_FLOAT) {
		WARNING("Value is not a float");
		return -1.0;
	}
	if (col->index >= current_gdat->nr_floats) {
		WARNING("Broken GDAT table; float index %d out of bounds", col->index);
		return -1.0;
	}
	return current_gdat->floats[col->index];
}

static struct string *DataFile_GetString(int label, int vartical, int horizontal)
{
	struct gdat_col *col = gdat_get_column(current_gdat, label, vartical, horizontal);
	if (!col)
		return string_ref(&EMPTY_STRING);

	if (col->type != GDAT_STRING) {
		WARNING("Value is not a string");
		return string_ref(&EMPTY_STRING);
	}
	if (col->index >= current_gdat->nr_strings) {
		WARNING("Broken GDAT table; string index %d out of bounds", col->index);
		return string_ref(&EMPTY_STRING);
	}
	return string_ref(current_gdat->strings[col->index]);
}

HLL_LIBRARY(DataFile,
			HLL_EXPORT(Open, DataFile_Open),
			HLL_EXPORT(SarchLabel, DataFile_SarchLabel),
			HLL_EXPORT(GetNumofVertical, DataFile_GetNumofVertical),
			HLL_EXPORT(GetNumofHorizontal, DataFile_GetNumofHorizontal),
			HLL_EXPORT(SarchString, DataFile_SarchString),
			HLL_EXPORT(SarchInt, DataFile_SarchInt),
			HLL_EXPORT(GetInt, DataFile_GetInt),
			HLL_EXPORT(GetFloat, DataFile_GetFloat),
			HLL_EXPORT(GetString, DataFile_GetString));
