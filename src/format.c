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

#include <stdbool.h>
#include <string.h>
#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"

#define DIGIT_MAX 512

enum fmt_type {
	FMT_VOID,
	FMT_INT,
	FMT_FLOAT,
	FMT_STRING,
	FMT_CHAR,
	FMT_BOOL
};

struct fmt_spec {
	enum fmt_type type;
	int precision;
	int padding;
	bool zero_pad;
	bool zenkaku;
};

static inline bool is_integer_type(enum string_format_type t)
{
	return t == STRFMT_INT
		|| t == STRFMT_BOOL
		|| t == STRFMT_LONG_INT;
}

static int read_number(const char **_fmt)
{
	int n = 0;
	const char *fmt = *_fmt;
	while (*fmt && *fmt >= '0' && *fmt <= '9') {
		n *= 10;
		n += *fmt - '0';
		fmt++;
	}
	*_fmt = fmt;
	return n;
}

static bool parse_fmt_spec(const char **_fmt, struct fmt_spec *spec, enum string_format_type target)
{
	bool r = false;
	const char *fmt = (*_fmt) + 1;
	memset(spec, 0, sizeof(struct fmt_spec));
	spec->precision = 6;
	while (*fmt) {
		switch (*fmt) {
		case 'd':
			spec->type = FMT_INT;
			r = is_integer_type(target);
			goto end;
		case 'D':
			spec->type = FMT_INT;
			spec->zenkaku = true;
			r = is_integer_type(target);
			goto end;
		case 'f':
			spec->type = FMT_FLOAT;
			r = target == STRFMT_FLOAT;
			goto end;
		case 'F':
			spec->type = FMT_FLOAT;
			spec->zenkaku = true;
			r = target == STRFMT_FLOAT;
			goto end;
		case 's':
			spec->type = FMT_STRING;
			r = target == STRFMT_STRING;
			goto end;
		case 'c':
			spec->type = FMT_CHAR;
			r = is_integer_type(target);
			goto end;
		case 'b':
			spec->type = FMT_BOOL;
			r = is_integer_type(target);
			goto end;
		case '0':
			spec->zero_pad = true;
			fmt++;
			break;
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			spec->padding = read_number(&fmt);
			break;
		case '.':
			fmt++;
			spec->precision = read_number(&fmt);
			break;
		default:
			goto warn;
		}
	}
warn:
	WARNING("Invalid format specifier: %s", *_fmt);
end:
	if (r)
		*_fmt = ++fmt;
	return r;
}

static void append_fmt(struct string **s, struct fmt_spec *spec, union vm_value arg)
{
	int len;
	char buf[DIGIT_MAX] = { [DIGIT_MAX-1] = '\0' };
	switch (spec->type) {
	case FMT_VOID:
		return;
	case FMT_INT:
		len = int_to_cstr(buf, DIGIT_MAX, arg.i, spec->padding, spec->zero_pad, spec->zenkaku);
		string_append_cstr(s, buf, len);
		break;
	case FMT_FLOAT:
		len = float_to_cstr(buf, DIGIT_MAX, arg.f, spec->padding, spec->zero_pad, spec->precision, spec->zenkaku);
		string_append_cstr(s, buf, len);
		break;
	case FMT_STRING: {
		struct string *val = heap_get_string(arg.i);
		int pad_len = spec->padding - val->size;
		if (pad_len > 0) {
			for (int i = 0; i < pad_len; i++) {
				string_push_back(s, ' ');
			}
		}
		string_append(s, val);
		heap_unref(arg.i);
		break;
	}
	case FMT_CHAR:
		string_push_back(s, arg.i);
		break;
	case FMT_BOOL:
		if (arg.i)
			string_append_cstr(s, "true", 4);
		else
			string_append_cstr(s, "false", 5);
		break;
	}
}

struct string *string_format(struct string *fmt, union vm_value arg, enum string_format_type type)
{
	for (const char *s = fmt->text; *s; s++) {
		if (*s != '%')
			continue;

		size_t size = s - fmt->text;
		struct fmt_spec spec;
		if (parse_fmt_spec(&s, &spec, type)) {
			struct string *out = string_ref(&EMPTY_STRING);
			string_append_cstr(&out, fmt->text, size);
			append_fmt(&out, &spec, arg);
			string_append_cstr(&out, s, strlen(s));
			return out;
		}
	}
	return string_ref(fmt);
}
