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
#include <string.h>
#include "little_endian.h"
#include "system4.h"
#include "system4/buffer.h"
#include "system4/string.h"

void buffer_init(struct buffer *r, uint8_t *buf, size_t size)
{
	r->buf = buf;
	r->size = size;
	r->index = 0;
}

int32_t buffer_read_int32(struct buffer *r)
{
	if (r->index + 4 > r->size)
		ERROR("Out of bounds buffer read");
	int32_t v = LittleEndian_getDW(r->buf, r->index);
	r->index += 4;
	return v;
}

uint8_t buffer_read_u8(struct buffer *r)
{
	if (r->index + 1 > r->size)
		ERROR("Out of bounds buffer read");
	return r->buf[r->index++];
}

uint16_t buffer_read_u16(struct buffer *r)
{
	if (r->index + 2 > r->size)
		ERROR("Out of bounds buffer read");
	uint16_t v = LittleEndian_getW(r->buf, r->index);
	r->index += 2;
	return v;
}

float buffer_read_float(struct buffer *r)
{
	union { int32_t i; float f; } v;
	v.i = buffer_read_int32(r);
	return v.f;
}

struct string *buffer_read_pascal_string(struct buffer *r)
{
	int32_t len = buffer_read_int32(r);
	if (len < 0)
		ERROR("Invalid string length: %d", len);
	struct string *s = make_string((char*)r->buf+r->index, len);
	r->index += len;
	return s;
}

void buffer_skip(struct buffer *r, size_t off)
{
	r->index = min(r->index + off, r->size);
}

size_t buffer_remaining(struct buffer *r)
{
	return r->size - r->index;
}

char *buffer_strdata(struct buffer *r)
{
	return (char*)r->buf + r->index;
}

static void alloc_buffer(struct buffer *b, size_t size)
{
	if (b->index + size < b->size)
		return;

	size_t new_size = b->index + size;
	if (!b->size)
		b->size = 64;
	while (b->size <= new_size)
		b->size *= 2;
	b->buf = xrealloc(b->buf, b->size);
}

void buffer_write_int32(struct buffer *b, uint32_t v)
{
	alloc_buffer(b, 4);
	b->buf[b->index++] = (v & 0x000000FF);
	b->buf[b->index++] = (v & 0x0000FF00) >> 8;
	b->buf[b->index++] = (v & 0x00FF0000) >> 16;
	b->buf[b->index++] = (v & 0xFF000000) >> 24;
}

void buffer_write_int32_at(struct buffer *buf, size_t index, uint32_t v)
{
	size_t tmp = buf->index;
	buf->index = index;
	buffer_write_int32(buf, v);
	buf->index = tmp;
}

void buffer_write_int16(struct buffer *b, uint16_t v)
{
	alloc_buffer(b, 2);
	b->buf[b->index++] = (v & 0x00FF);
	b->buf[b->index++] = (v & 0xFF00) >> 8;
}

void buffer_write_float(struct buffer *b, float f)
{
	union { float f; uint32_t u; } v;
	v.f = f;
	buffer_write_int32(b, v.u);
}

void buffer_write_bytes(struct buffer *b, const uint8_t *bytes, size_t len)
{
	alloc_buffer(b, len);
	memcpy(b->buf + b->index, bytes, len);
	b->index += len;
}

void buffer_write_string(struct buffer *b, struct string *s)
{
	buffer_write_bytes(b, (uint8_t*)s->text, s->size);
}

void buffer_write_cstring(struct buffer *b, const char *s)
{
	buffer_write_bytes(b, (uint8_t*)s, strlen(s));
}

void buffer_write_pascal_string(struct buffer *b, struct string *s)
{
	buffer_write_int32(b, s->size);
	buffer_write_string(b, s);
}
