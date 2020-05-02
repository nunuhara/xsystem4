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

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <zlib.h>
#include "little_endian.h"
#include "system4.h"
#include "system4/buffer.h"
#include "system4/ex.h"
#include "system4/string.h"

const char *ex_strtype(enum ex_value_type type)
{
	switch (type) {
	case EX_INT: return "int";
	case EX_FLOAT: return "float";
	case EX_STRING: return "string";
	case EX_TABLE: return "table";
	case EX_LIST: return "list";
	case EX_TREE: return "tree";
	default: return "unknown_type";
	}
}

uint8_t ex_decode_table[256];
uint8_t ex_decode_table_inv[256];

static struct string *ex_read_string(struct buffer *r)
{
	struct string *s = buffer_read_pascal_string(r);
	// TODO: at most 3 bytes padding, no need for full strlen
	s->size = strlen(s->text);
	// TODO: validate?
	return s;
}

static bool ex_initialized = false;

static void ex_init(void)
{
	// initialize table
	for (int i = 0; i < 256; i++) {
		int tmpfor = i;
		int tmp = tmpfor;
		tmp = (tmp & 0x55) + ((tmp >> 1) & 0x55);
		tmp = (tmp & 0x33) + ((tmp >> 2) & 0x33);
		tmp = (tmp & 0x0F) + ((tmp >> 4) & 0x0F);
		if ((tmp & 0x01) == 0) {
			tmpfor = ((tmpfor << (8 - tmp)) | (tmpfor >> tmp)) & 0xFF;
		} else {
			tmpfor = ((tmpfor >> (8 - tmp)) | (tmpfor << tmp)) & 0xFF;
		}
		ex_decode_table[i] = tmpfor;
	}
	// initialize inverse table
	for (int i = 0; i < 256; i++) {
		ex_decode_table_inv[ex_decode_table[i]] = i;
	}

	ex_initialized = true;
}

void ex_encode(uint8_t *buf, size_t size)
{
	if (!ex_initialized)
		ex_init();

	for (size_t i = 0; i < size; i++) {
		buf[i] = ex_decode_table_inv[buf[i]];
	}
}

static uint8_t *ex_decode(uint8_t *data, size_t *len, uint32_t *nr_blocks)
{
	struct buffer r;
	uint32_t compressed_size;
	unsigned long uncompressed_size;

	if (!ex_initialized)
		ex_init();

	buffer_init(&r, data, *len);
	if (strncmp(buffer_strdata(&r), "HEAD", 4))
		ERROR("Missing HEAD section marker");
	buffer_skip(&r, 8);

	if (strncmp(buffer_strdata(&r), "EXTF", 4)) {
		NOTICE("index = %zu", r.index);
		ERROR("Missing EXTF section marker");
	}
	buffer_skip(&r, 8);

	if (nr_blocks)
		*nr_blocks = buffer_read_int32(&r);
	else
		buffer_read_int32(&r);

	if (strncmp(buffer_strdata(&r), "DATA", 4)) {
		ERROR("Missing DATA section marker");
	}
	buffer_skip(&r, 4);

	compressed_size = buffer_read_int32(&r);
	uncompressed_size = buffer_read_int32(&r);

	// decode compressed data
	for (size_t i = 0; i < compressed_size; i++) {
		data[r.index+i] = ex_decode_table[data[r.index+i]];
	}

	uint8_t *out = xmalloc(uncompressed_size);
	int rv = uncompress(out, &uncompressed_size, (uint8_t*)buffer_strdata(&r), compressed_size);
	switch (rv) {
	case Z_BUF_ERROR:  ERROR("Uncompress failed: Z_BUF_ERROR");
	case Z_MEM_ERROR:  ERROR("Uncompress failed: Z_MEM_ERROR");
	case Z_DATA_ERROR: ERROR("Uncompress failed: Z_DATA_ERROR");
	case Z_OK:         break;
	default:           ERROR("Uncompress failed: %d", rv);
	}

	// decompress
	*len = uncompressed_size;
	return out;
}

static void ex_read_fields(struct buffer *r, struct ex_table *table);
static void ex_read_table(struct buffer *r, struct ex_table *table, struct ex_field *fields, uint32_t nr_fields);
static void ex_read_list(struct buffer *r, struct ex_list *list);

static void _ex_read_value(struct buffer *r, struct ex_value *value, struct ex_field *fields, uint32_t nr_fields)
{
	switch (value->type) {
	case EX_INT:
		value->i = buffer_read_int32(r);
		break;
	case EX_FLOAT:
		value->f = buffer_read_float(r);
		break;
	case EX_STRING:
		value->s = buffer_read_pascal_string(r);
		break;
	case EX_TABLE:
		value->t = xcalloc(1, sizeof(struct ex_table));
		// XXX: if nr_fields is zero, we are NOT a sub-table and therefore need to read fields
		if (!nr_fields) {
			ex_read_fields(r, value->t);
			ex_read_table(r, value->t, value->t->fields, value->t->nr_fields);
		} else {
			ex_read_table(r, value->t, fields, nr_fields);
		}
		break;
	case EX_LIST:
		value->list = xcalloc(1, sizeof(struct ex_list));
		ex_read_list(r, value->list);
		break;
	default:
		ERROR("Unhandled value type: %d", value->type);
	}
}

static void ex_read_value(struct buffer *r, struct ex_value *value, struct ex_field *fields, uint32_t nr_fields)
{
	value->type = buffer_read_int32(r);
	_ex_read_value(r, value, fields, nr_fields);
}

static void ex_read_field(struct buffer *r, struct ex_field *field)
{
	field->type = buffer_read_int32(r);
	if (field->type < EX_INT || field->type > EX_TABLE)
		ERROR("Unknown/invalid field type: %d", field->type);

	field->name = ex_read_string(r);
	field->uk0 = buffer_read_int32(r);
	field->uk1 = buffer_read_int32(r);
	if (field->uk0)
		field->uk2 = buffer_read_int32(r);

	if (field->type == EX_TABLE) {
		field->nr_subfields = buffer_read_int32(r);
		if (field->nr_subfields > 255)
			ERROR("Too many subfields: %u", field->nr_subfields);

		field->subfields = xcalloc(field->nr_subfields, sizeof(struct ex_field));
		for (uint32_t i = 0; i < field->nr_subfields; i++) {
			ex_read_field(r, &field->subfields[i]);
		}
	}
}

static void ex_read_fields(struct buffer *r, struct ex_table *table)
{
	table->nr_fields = buffer_read_int32(r);
	table->fields = xcalloc(table->nr_fields, sizeof(struct ex_field));
	for (uint32_t i = 0; i < table->nr_fields; i++) {
		ex_read_field(r, &table->fields[i]);
	}
}

enum table_layout {
	TABLE_DEFAULT,
	TABLE_COLUMNS_FIRST,
	TABLE_ROWS_FIRST
};

static enum table_layout table_layout = TABLE_DEFAULT;

static void ex_read_table(struct buffer *r, struct ex_table *table, struct ex_field *fields, uint32_t nr_fields)
{
	// NOTE: Starting in Evenicle, the rows/columns quantities are reversed
	if (table_layout == TABLE_ROWS_FIRST) {
		table->nr_rows = buffer_read_int32(r);
		table->nr_columns = buffer_read_int32(r);
	} else {
		table->nr_columns = buffer_read_int32(r);
		table->nr_rows = buffer_read_int32(r);
	}

	// try switching to column-major order if fields don't make sense
	if (table_layout == TABLE_DEFAULT && table->nr_columns != nr_fields) {
		if (table->nr_rows == nr_fields) {
			uint32_t tmp = table->nr_columns;
			table->nr_columns = table->nr_rows;
			table->nr_rows = tmp;
			table_layout = TABLE_ROWS_FIRST;
		} else {
			ERROR("Number of fields doesn't match number of columns: %u, %u", table->nr_columns, nr_fields);
		}
	} else if (table->nr_columns != nr_fields) {
		ERROR("Number of fields doesn't match number of columns: %u, %u", table->nr_columns, nr_fields);
	}

	table->rows = xcalloc(table->nr_rows, sizeof(struct ex_value*));
	for (uint32_t i = 0; i < table->nr_rows; i++) {
		table->rows[i] = xcalloc(table->nr_columns, sizeof(struct ex_value));
		for (uint32_t j = 0; j < table->nr_columns; j++) {
			ex_read_value(r, &table->rows[i][j], fields[j].subfields, fields[j].nr_subfields);
			if (table->rows[i][j].type != fields[j].type) {
				// broken table in Rance 03?
				WARNING("Column type doesn't match field type: expected %s; got %s",
					ex_strtype(fields[j].type), ex_strtype(table->rows[i][j].type));
			}
		}
	}
}

static void ex_read_list(struct buffer *r, struct ex_list *list)
{
	list->nr_items = buffer_read_int32(r);
	list->items = xcalloc(list->nr_items, sizeof(struct ex_list_item));
	for (uint32_t i = 0; i < list->nr_items; i++) {
		list->items[i].value.type = buffer_read_int32(r);
		list->items[i].size = buffer_read_int32(r);
		size_t data_loc = r->index;
		_ex_read_value(r, &list->items[i].value, NULL, 0);
		if (r->index - data_loc != list->items[i].size) {
			ERROR("Incorrect size for list item: %" SIZE_T_FMT "u / %" SIZE_T_FMT "u",
			      list->items[i].size, r->index - data_loc);
		}
	}
}

static void ex_read_tree(struct buffer *r, struct ex_tree *tree)
{
	tree->name = ex_read_string(r);
	uint32_t is_leaf = buffer_read_int32(r);
	if (is_leaf > 1)
		ERROR("tree->is_leaf is not a boolean: %u", is_leaf);
	tree->is_leaf = is_leaf;

	if (!tree->is_leaf) {
		tree->nr_children = buffer_read_int32(r);
		tree->children = xcalloc(tree->nr_children, sizeof(struct ex_tree));
		for (uint32_t i = 0; i < tree->nr_children; i++) {
			ex_read_tree(r, &tree->children[i]);
		}
	} else {
		tree->leaf.value.type = buffer_read_int32(r);
		tree->leaf.size = buffer_read_int32(r);
		size_t data_loc = r->index;
		tree->leaf.name = ex_read_string(r);
		_ex_read_value(r, &tree->leaf.value, NULL, 0);

		if (r->index - data_loc != tree->leaf.size) {
			ERROR("Incorrect size for leaf node: %" SIZE_T_FMT "u / %" SIZE_T_FMT "u",
			      tree->leaf.size, r->index - data_loc);
		}

		int32_t zero = buffer_read_int32(r);
		if (zero) {
			ERROR("Expected 0 after leaf node: 0x%x at 0x%zx", zero, r->index);
		}
	}
}

static void ex_read_block(struct buffer *r, struct ex_block *block)
{
	block->val.type = buffer_read_int32(r);
	if (block->val.type < EX_INT || block->val.type > EX_TREE)
		ERROR("Unknown/invalid block type: %d", block->val.type);

	block->size = buffer_read_int32(r);
	if (block->size > buffer_remaining(r))
		ERROR("Block size extends past end of file: %" SIZE_T_FMT "u", block->size);

	size_t data_loc = r->index;
	block->name = ex_read_string(r);

	switch (block->val.type) {
	case EX_INT:
		block->val.i = buffer_read_int32(r);
		break;
	case EX_FLOAT:
		block->val.f = buffer_read_float(r);
		break;
	case EX_STRING:
		block->val.s = buffer_read_pascal_string(r);
		break;
	case EX_TABLE:
		block->val.t = xcalloc(1, sizeof(struct ex_table));
		ex_read_fields(r, block->val.t);
		ex_read_table(r, block->val.t, block->val.t->fields, block->val.t->nr_fields);
		break;
	case EX_LIST:
		block->val.list = xcalloc(1, sizeof(struct ex_list));
		ex_read_list(r, block->val.list);
		break;
	case EX_TREE:
		block->val.tree = xcalloc(1, sizeof(struct ex_tree));
		ex_read_tree(r, block->val.tree);
		break;
	}

	if (r->index - data_loc != block->size) {
		ERROR("Incorrect block size: %" SIZE_T_FMT "u / %" SIZE_T_FMT "u",
		      r->index - data_loc, block->size);
	}
}

uint8_t *ex_decrypt(const char *path, size_t *size, uint32_t *nr_blocks)
{
	FILE *fp;
	long len;
	uint8_t *buf = NULL;

	if (!(fp = fopen(path, "rb")))
		ERROR("fopen failed: %s", strerror(errno));

	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	fseek(fp, 0, SEEK_SET);


	buf = xmalloc(len+1);
	if (fread(buf, len, 1, fp) != 1)
		ERROR("fread failed: %s", strerror(errno));
	if (fclose(fp))
		ERROR("fclose failed: %s", strerror(errno));
	buf[len] = '\0';

	*size = len;
	uint8_t *decoded = ex_decode(buf, size, nr_blocks);
	free(buf);
	return decoded;
}

struct ex *ex_read(const char *path)
{
	size_t decoded_len;
	uint32_t nr_blocks;
	uint8_t *decoded = ex_decrypt(path, &decoded_len, &nr_blocks);

	struct buffer r;
	buffer_init(&r, decoded, decoded_len);

	struct ex *ex = xmalloc(sizeof(struct ex));
	ex->nr_blocks = nr_blocks;
	ex->blocks = xcalloc(nr_blocks, sizeof(struct ex_block));
	for (size_t i = 0; i < nr_blocks; i++) {
		ex_read_block(&r, &ex->blocks[i]);
	}

	free(decoded);
	return ex;
}

static void ex_free_table(struct ex_table *table);
static void ex_free_list(struct ex_list *list);
static void ex_free_tree(struct ex_tree *tree);

static void ex_free_value(struct ex_value *value)
{
	switch (value->type) {
	case EX_STRING:
		free_string(value->s);
		break;
	case EX_TABLE:
		ex_free_table(value->t);
		break;
	case EX_LIST:
		ex_free_list(value->list);
		break;
	case EX_TREE:
		ex_free_tree(value->tree);
		free(value->tree);
		break;
	default:
		break;
	}
}

static void ex_free_values(struct ex_value *values, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		ex_free_value(&values[i]);
	}
	free(values);
}

static void ex_free_fields(struct ex_field *fields, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		free_string(fields[i].name);
		ex_free_fields(fields[i].subfields, fields[i].nr_subfields);
	}
	free(fields);
}

static void ex_free_table(struct ex_table *table)
{
	ex_free_fields(table->fields, table->nr_fields);
	for (uint32_t i = 0; i < table->nr_rows; i++) {
		ex_free_values(table->rows[i], table->nr_columns);
	}
	free(table->rows);
	free(table);
}

static void ex_free_list(struct ex_list *list)
{
	for (uint32_t i = 0; i < list->nr_items; i++) {
		ex_free_value(&list->items[i].value);
	}
	free(list->items);
	free(list);
}

static void ex_free_tree(struct ex_tree *tree)
{
	free_string(tree->name);

	if (tree->is_leaf) {
		free_string(tree->leaf.name);
		ex_free_value(&tree->leaf.value);
		return;
	}

	for (uint32_t i = 0; i < tree->nr_children; i++) {
		ex_free_tree(&tree->children[i]);
	}
	free(tree->children);
}

void ex_free(struct ex *ex)
{
	for (uint32_t i = 0; i < ex->nr_blocks; i++) {
		struct ex_block *block = &ex->blocks[i];
		free_string(block->name);
		switch (block->val.type) {
		case EX_STRING:
			free_string(block->val.s);
			break;
		case EX_TABLE:
			ex_free_table(block->val.t);
			break;
		case EX_LIST:
			ex_free_list(block->val.list);
			break;
		case EX_TREE:
			ex_free_tree(block->val.tree);
			free(block->val.tree);
			break;
		default:
			break;
		}
	}
	free(ex->blocks);
	free(ex);
}

