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

#ifndef SYSTEM4_INI_H
#define SYSTEM4_INI_H

#include <stddef.h>

struct string;

enum ini_value_type {
	INI_NULL,
	INI_INTEGER,
	INI_BOOLEAN,
	INI_STRING,
	INI_LIST,
	_INI_LIST_ENTRY,
	INI_FORMATION,

	INI_NR_TYPES
};

struct ini_value {
	enum ini_value_type type;
	union {
		int i;
		struct string *s;
		// list
		struct {
			size_t list_size;
			struct ini_value *list;
		};
		// list item
		struct {
			size_t _list_pos;
			struct ini_value *_list_value;
		};
		// formation
		struct {
			size_t nr_entries;
			struct ini_entry *entries;
		};
	};
};

struct ini_entry {
	struct string *name;
	struct ini_value value;
};

struct ini_entry *ini_parse(const char *path, size_t *nr_entries);
struct ini_entry *ini_make_entry(struct string *name, struct ini_value value);
void ini_free_entry(struct ini_entry *entry);

static inline struct ini_value ini_make_integer(int n)
{
	return (struct ini_value) { .type = INI_INTEGER, .i = n };
}

static inline struct ini_value ini_make_boolean(int b)
{
	return (struct ini_value) { .type = INI_BOOLEAN, .i = !!b };
}

static inline struct ini_value ini_make_string(struct string *s)
{
	return (struct ini_value) { .type = INI_STRING, .s = s };
}

static inline struct ini_value ini_make_list(struct ini_value *values, size_t nr_values)
{
	return (struct ini_value) { .type = INI_LIST, .list = values, .list_size = nr_values };
}

#endif /* SYSTEM4_INI_H */
