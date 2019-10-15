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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "system4.h"
#include "vm_string.h"
#include "utfsjis.h"

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

struct string *string_dup(const struct string *in)
{
	struct string *out = xmalloc(sizeof(struct string) + in->size + 1);
	out->size = in->size;
	out->literal = false;
	memcpy(out->text, in->text, in->size + 1);
	return out;
}

void string_append(struct string **_a, const struct string *b)
{
	struct string *a = *_a;
	size_t a_size = a->size;

	if (a->literal) {
		a = xmalloc(sizeof(struct string) + a->size + b->size + 1);
		a->literal = false;
		memcpy(a->text, (*_a)->text, a_size+1);
	} else {
		a = xrealloc(a, sizeof(struct string) + a->size + b->size + 1);
	}
	a->size = a_size + b->size;
	memcpy(a->text + a_size, b->text, b->size + 1);
	*_a = a;
}

struct string *string_concatenate(const struct string *a, const struct string *b)
{
	struct string *s = xmalloc(sizeof(struct string) + a->size + b->size + 1);
	s->size = a->size + b->size;
	s->literal = false;
	memcpy(s->text, a->text, a->size);
	memcpy(s->text + a->size, b->text, b->size + 1);
	return s;
}

struct string *string_copy(const struct string *s, int index, int len)
{
	if (index < 0)
		index = 0;
	if (len <= 0)
		return make_string("", 0);
	if ((index = sjis_index(s->text, index)) < 0)
		return make_string("", 0);

	if ((len = sjis_index(s->text + index, len)) < 0)
		len = s->size - index;

	return make_string(s->text + index, len);
}

int string_find(const struct string *haystack, const struct string *needle)
{
	char *r = strstr(haystack->text, needle->text);
	if (!r)
		return -1;
	return r - haystack->text;
}

void string_push_back(struct string **s, int c)
{
	int bytes = SJIS_2BYTE(c) ? 2 : 1;
	*s = xrealloc(*s, sizeof(struct string) + (*s)->size + bytes + 1);
	(*s)->text[(*s)->size++] = c & 0xFF;
	if (bytes == 2) {
		(*s)->text[(*s)->size++] = c >> 8;
	}
	(*s)->text[(*s)->size] = '\0';
}

void string_pop_back(struct string *s)
{
	// get index of last character
	int c = 0;
	for (int i = 0; i < s->size; i++) {
		c = i;
		if (SJIS_2BYTE(s->text[i])) {
			i++;
		}
	}
	s->text[c] = '\0';
	s->size = c;
}

void string_erase(struct string *s, int index)
{
	int bytes;
	if (index < 0)
		index = 0;
	if (index >= s->size)
		return;
	if ((index = sjis_index(s->text, index)) < 0)
		return;
	bytes = SJIS_2BYTE(s->text[index]) ? 2 : 1;

	memcpy(s->text + index, s->text + index + bytes, s->size - index - bytes);
	s->size -= bytes;
	s->text[s->size] = '\0';
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
