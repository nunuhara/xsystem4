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

#ifndef SYSTEM4_HLL_IARRAY_H
#define SYSTEM4_HLL_IARRAY_H

#include <stddef.h>
#include <stdbool.h>
#include "gfx/types.h"
#include "gfx/font.h"

struct string;
struct page;
struct ain_type;

// TODO: implement using array page directly?
struct iarray_writer {
	unsigned allocated;
	unsigned size;
	int *data;
};

void iarray_init_writer(struct iarray_writer *w, const char *header);
void iarray_free_writer(struct iarray_writer *w);
void iarray_write(struct iarray_writer *w, int data);
void iarray_write_at(struct iarray_writer *w, unsigned pos, int data);
void iarray_write_float(struct iarray_writer *w, float data);
void iarray_write_string(struct iarray_writer *w, struct string *s);
void iarray_write_string_or_null(struct iarray_writer *w, struct string *s);
void iarray_write_struct(struct iarray_writer *w, struct page *page, bool with_type);
void iarray_write_array(struct iarray_writer *w, struct page *page);
void iarray_write_point(struct iarray_writer *w, Point *p);
void iarray_write_rectangle(struct iarray_writer *w, Rectangle *r);
void iarray_write_color(struct iarray_writer *w, SDL_Color *color);
void iarray_write_text_style(struct iarray_writer *w, struct text_style *style);

static inline unsigned iarray_writer_pos(struct iarray_writer *w)
{
	return w->size;
}

struct page *iarray_to_page(struct iarray_writer *w);
uint8_t *iarray_to_buffer(struct iarray_writer *w, size_t *size_out);

struct iarray_reader {
	union vm_value *data;
	unsigned size;
	unsigned pos;
	int error;
};

static inline bool iarray_end(struct iarray_reader *r)
{
	return r->pos >= r->size;
}

bool iarray_init_reader(struct iarray_reader *r, struct page *a, const char *header);
int iarray_read(struct iarray_reader *r);
float iarray_read_float(struct iarray_reader *r);
struct string *iarray_read_string(struct iarray_reader *r);
struct string *iarray_read_string_or_null(struct iarray_reader *r);
struct page *iarray_read_struct(struct iarray_reader *r, int struct_type, bool with_type);
struct page *iarray_read_array(struct iarray_reader *r, struct ain_type *t);
void iarray_read_point(struct iarray_reader *r, Point *p);
void iarray_read_rectangle(struct iarray_reader *r, Rectangle *rect);
void iarray_read_color(struct iarray_reader *r, SDL_Color *color);
void iarray_read_text_style(struct iarray_reader *r, struct text_style *style);

#endif /* SYSTEM4_HLL_IARRAY_H */
