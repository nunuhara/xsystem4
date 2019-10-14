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

#ifndef SYSTEM4_STRING_H
#define SYSTEM4_STRING_H

#include <stdbool.h>
#include <stddef.h>

struct string {
	unsigned int size;
	bool literal;
	char text[];
};

struct string *make_string(const char *str, unsigned int len);
void free_string(struct string *str);
struct string *string_dup(struct string *in);
struct string *string_append(struct string *a, struct string *b);
struct string *integer_to_string(int n);
struct string *float_to_string(float f, int precision);

#endif
