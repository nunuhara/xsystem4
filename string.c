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

struct string *string_dup(struct string *in)
{
	struct string *out = xmalloc(sizeof(struct string) + in->size + 1);
	out->size = in->size;
	out->literal = false;
	memcpy(out->text, in->text, in->size + 1);
	return out;
}

struct string *string_append(struct string *a, struct string *b)
{
	struct string *s = xmalloc(sizeof(struct string) + a->size + b->size + 1);
	s->size = a->size + b->size;
	s->literal = false;
	memcpy(s->text, a->text, a->size);
	memcpy(s->text + a->size, b->text, b->size + 1);
	return s;
}

struct string *integer_to_string(int n)
{
	char buf[512];
	int len = snprintf(buf, 512, "%d", n);
	return make_string(buf, len);
}

struct string *float_to_string(float f, int precision)
{
	char buf[512];
	int len;

	// System40.exe pushes -1, defaults to 6
	if (precision < 0) {
		precision = 6;
	}

	len = snprintf(buf, 512, "%.*f", precision, f);
	return make_string(buf, len);
}
