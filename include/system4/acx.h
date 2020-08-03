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

#ifndef SYSTEM4_ACX_H
#define SYSTEM4_ACX_H

#include <stdint.h>

struct string;
struct acx;

enum acx_column_type {
	ACX_INT    = 0,
	ACX_STRING = 2
};

union acx_value {
	int32_t i;
	struct string *s;
};

struct acx {
	int nr_columns;
	int nr_lines;
	enum acx_column_type *column_types;
	union acx_value *lines;
};

struct acx *acx_load(const char *path);
void acx_free(struct acx *acx);
int acx_get_int(struct acx *acx, int line, int col);
struct string *acx_get_string(struct acx *acx, int line, int col);

#endif /* SYSTEM4_ACX_H */
