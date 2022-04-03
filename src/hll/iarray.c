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
#include <assert.h>
#include "system4.h"
#include "system4/string.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "iarray.h"

/*
 * Serialization interface for various Save/Load HLL functions that write/read
 * integer arrays.
 */

void iarray_init_writer(struct iarray_writer *w, const char *header)
{
	w->data = xmalloc(1024 * sizeof(int));
	w->allocated = 1024;
	w->size = 0;
	if (header) {
		while (*header) {
			w->data[w->size++] = *header;
			header++;
		}
		w->data[w->size++] = 0;
	}
}

void iarray_free_writer(struct iarray_writer *w)
{
	free(w->data);
	w->size = 0;
	w->allocated = 0;
	w->data = NULL;
}

void iarray_write(struct iarray_writer *w, int data)
{
	if (!w->allocated) {
		w->data = xmalloc(1024 * sizeof(int));
		w->allocated = 1024;
	} else if (w->size >= w->allocated) {
		w->data = xrealloc(w->data, w->allocated * 2 * sizeof(int));
		w->allocated *= 2;
	}
	w->data[w->size++] = data;
}


void iarray_write_at(struct iarray_writer *w, unsigned pos, int data)
{
	if (pos >= w->size)
		VM_ERROR("iarray_write_at: invalid index");
	w->data[pos] = data;
}

void iarray_write_float(struct iarray_writer *w, float data)
{
	union { float f; int i; } cast = { .f = data };
	iarray_write(w, cast.i);
}

void iarray_write_string(struct iarray_writer *w, struct string *s)
{
	for (char *p = s->text; *p ;p++) {
		iarray_write(w, *p);
	}
	iarray_write(w, 0);
}

static void iarray_write_value(struct iarray_writer *w, struct ain_type *t, int value)
{
	switch (t->data) {
	case AIN_VOID:
	case AIN_INT:
	case AIN_FLOAT:
	case AIN_BOOL:
		iarray_write(w, value);
		break;
	case AIN_STRING:
		iarray_write_string(w, heap_get_string(value));
		break;
	case AIN_STRUCT:
		iarray_write_struct(w, heap_get_page(value));
		break;
	case AIN_ARRAY_INT:
	case AIN_ARRAY_FLOAT:
	case AIN_ARRAY_STRING:
	case AIN_ARRAY_STRUCT:
	case AIN_ARRAY_BOOL:
		iarray_write_array(w, heap_get_page(value));
		break;
	default:
		VM_ERROR("Unsupported data type for serialization: %d", t->data);
	}
}

void iarray_write_struct(struct iarray_writer *w, struct page *page)
{
	assert(page->type == STRUCT_PAGE);
	struct ain_struct *s = &ain->structures[page->index];
	assert(s->nr_members == page->nr_vars);
	for (int i = 0; i < s->nr_members; i++) {
		iarray_write_value(w, &s->members[i].type, page->values[i].i);
	}
}

void iarray_write_array(struct iarray_writer *w, struct page *page)
{
	if (!page) {
		iarray_write(w, 0);
		return;
	}

	assert(page->type == ARRAY_PAGE);

	struct ain_type t = {
		.data = page->array.rank > 1 ? page->a_type : array_type(page->a_type),
		.struc = page->array.struct_type,
		.rank = page->array.rank - 1
	};

	iarray_write(w, page->nr_vars);
	for (int i = 0; i < page->nr_vars; i++) {
		iarray_write_value(w, &t, page->values[i].i);
	}
}

struct page *iarray_to_page(struct iarray_writer *w)
{
	union vm_value dim = { .i = w->size };
	struct page *page = alloc_array(1, &dim, AIN_ARRAY_INT, 0, false);
	for (unsigned i = 0; i < w->size; i++) {
		page->values[i].i = w->data[i];
	}
	return page;
}

bool iarray_init_reader(struct iarray_reader *r, struct page *a, const char *header)
{
	r->data = a->values;
	r->size = a->nr_vars;
	r->pos = 0;
	r->error = 0;
	if (header) {
		for (const char *p = header; *p; p++) {
			if (iarray_read(r) != *p)
				goto error;
		}
		if (iarray_read(r) != '\0')
			goto error;
	}
	return true;
error:
	r->error = 1;
	return false;
}

int iarray_read(struct iarray_reader *r)
{
	if (r->pos >= r->size) {
		r->error = 1;
		return 0;
	}
	return r->data[r->pos++].i;
}

float iarray_read_float(struct iarray_reader *r)
{
	union { float f; int i; } cast = { .i = iarray_read(r) };
	return cast.f;
}

struct string *iarray_read_string(struct iarray_reader *r)
{
	int c;
	struct string *s = make_string("", 0);
	while ((c = iarray_read(r))) {
		string_push_back(&s, c);
	}
	return s;
}

static int32_t iarray_read_member(struct iarray_reader *r, struct ain_type *t)
{
	switch (t->data) {
	case AIN_INT:
	case AIN_FLOAT:
	case AIN_BOOL:
		return iarray_read(r);
	case AIN_STRING:
		return heap_alloc_string(iarray_read_string(r));
	case AIN_STRUCT:
		return heap_alloc_page(iarray_read_struct(r, t->struc));
	case AIN_ARRAY_INT:
	case AIN_ARRAY_FLOAT:
	case AIN_ARRAY_STRING:
	case AIN_ARRAY_STRUCT:
	case AIN_ARRAY_BOOL:
		return heap_alloc_page(iarray_read_array(r, t));
	default:
		VM_ERROR("Unsupported data type for (de)serialization: %d", t->data);
	}
}

struct page *iarray_read_struct(struct iarray_reader *r, int struct_type)
{
	struct ain_struct *s = &ain->structures[struct_type];
	struct page *page = alloc_page(STRUCT_PAGE, struct_type, s->nr_members);
	for (int i = 0; i < s->nr_members; i++) {
		page->values[i].i = iarray_read_member(r, &s->members[i].type);
	}
	return page;
}

struct page *iarray_read_array(struct iarray_reader *r, struct ain_type *t)
{
	struct ain_type next_t = {
		.data = t->rank > 1 ? t->data : array_type(t->data),
		.struc = t->struc,
		.rank = t->rank - 1
	};

	int nr_vars = iarray_read(r);
	struct page *page = alloc_page(ARRAY_PAGE, t->data, nr_vars);
	page->array.struct_type = t->struc;
	page->array.rank = t->rank;

	for (int i = 0; i < nr_vars; i++) {
		page->values[i].i = iarray_read_member(r, &next_t);
	}
	return page;
}
