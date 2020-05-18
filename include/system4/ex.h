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

#ifndef SYSTEM4_EX_H
#define SYSTEM4_EX_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

enum ex_value_type {
	EX_INT = 1,
	EX_FLOAT = 2,
	EX_STRING = 3,
	EX_TABLE = 4,
	EX_LIST = 5,
	EX_TREE = 6
};

struct ex_value {
	enum ex_value_type type;
	union {
		int32_t i;
		float f;
		struct string *s;
		struct ex_table *t;
		struct ex_list *list;
		struct ex_tree *tree;
	};
};

struct ex_field {
	enum ex_value_type type;
	struct string *name;
	int32_t has_value;
	struct ex_value value;
	int32_t is_index;
	uint32_t nr_subfields;
	struct ex_field *subfields;
};

struct ex_table {
	uint32_t nr_fields;
	struct ex_field *fields;
	uint32_t nr_columns;
	uint32_t nr_rows;
	struct ex_value **rows;
};

struct ex_list_item {
	uint32_t size;
	struct ex_value value;
};

struct ex_list {
	uint32_t nr_items;
	struct ex_list_item *items;
};

struct ex_leaf {
	uint32_t size;
	struct string *name;
	struct ex_value value;
};

struct ex_tree {
	struct string *name;
	bool is_leaf;
	union {
		struct {
			uint32_t nr_children;
			struct ex_tree *children;
		};
		struct ex_leaf leaf;
	};
};

struct ex_block {
	size_t size;
	struct string *name;
	struct ex_value val;
};

struct ex {
	uint32_t nr_blocks;
	struct ex_block *blocks;
};

uint8_t *ex_decrypt(const char *path, size_t *size, uint32_t *nr_blocks);
struct ex *ex_read(const char *path);
void ex_free(struct ex *ex);

void ex_encode(uint8_t *buf, size_t size);
const char *ex_strtype(enum ex_value_type type);

#endif /* SYSTE4_EX_H */
