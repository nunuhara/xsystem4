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
void iarray_write_float(struct iarray_writer *w, float data);
void iarray_write_string(struct iarray_writer *w, struct string *s);
void iarray_write_struct(struct iarray_writer *w, struct page *page);
void iarray_write_array(struct iarray_writer *w, struct page *page);

struct page *iarray_to_page(struct iarray_writer *w);

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
struct page *iarray_read_struct(struct iarray_reader *r, int struct_type);
struct page *iarray_read_array(struct iarray_reader *r, struct ain_type *t);

#endif /* SYSTEM4_HLL_IARRAY_H */
