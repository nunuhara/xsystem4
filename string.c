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
#include "vm_string.h"

struct string *make_string(const char *str, unsigned int len)
{
	struct string *s = xmalloc(sizeof(struct string) + len + 1);
	s->size = len;
	s->literal = false;
	memcpy(s->text, str, len);
	s->text[len] = '\0';
	return s;
}

void free_string(struct string *str)
{
	if (!str->literal)
		free(str);
}

struct string *string_append(struct string *a, struct string *b)
{
	if (!a->literal) {
		a = xrealloc(a, sizeof(struct string) + a->size + b->size + 1);
	} else {
		struct string *tmp = xmalloc(sizeof(struct string) + a->size + b->size + 1);
		memcpy(tmp, a, sizeof(struct string) + a->size);
		a = tmp;
	}
	memcpy(a->text + a->size, b->text, b->size + 1);
	a->size = a->size + b->size;
	return a;
}

struct string *integer_to_string(int n)
{
	char buf[513];
	int len = snprintf(buf, 512, "%d" ,n);
	buf[512] = '\0';
	return make_string(buf, len);
}
