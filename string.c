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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "system4.h"
#include "sys4_string.h"

struct string make_string(char *str, size_t len)
{
	return (struct string) {
		.str = str,
		.len = len,
		.mutable = false
	};
}

void free_string(struct string *str)
{
	if (str->mutable)
		free(str->str);
}

void string_add(struct string *a, struct string *b)
{
	if (a->mutable) {
		a->str = xrealloc(a->str, a->len + b->len + 1);
	} else {
		char *literal = a->str;
		a->str = xmalloc(a->len + b->len + 1);
		memcpy(a->str, literal, a->len + 1);
	}
	memcpy(a->str + a->len, b->str, b->len + 1);
	a->len = a->len + b->len;
}

struct string integer_to_string(int n)
{
	char buf[513];
	int len = snprintf(buf, 512, "%d" ,n);
	buf[512] = '\0';

	return (struct string) {
		.str = xstrdup(buf),
		.len = len,
		.mutable = true
	};
}
