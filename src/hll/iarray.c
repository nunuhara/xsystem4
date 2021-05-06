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
#include "system4.h"
#include "system4/string.h"
#include "vm/page.h"
#include "iarray.h"

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

void iarray_write_string(struct iarray_writer *w, struct string *s)
{
	for (char *p = s->text; *p ;p++) {
		iarray_write(w, *p);
	}
	iarray_write(w, 0);
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

void iarray_init_reader(struct iarray_reader *r, struct page *a)
{
	r->data = a->values;
	r->size = a->nr_vars;
	r->pos = 0;
	r->error = 0;
}

int iarray_read(struct iarray_reader *r)
{
	if (r->pos >= r->size) {
		r->error = 1;
		return 0;
	}
	return r->data[r->pos++].i;
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
