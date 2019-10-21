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

#include "system4.h"
#include "ain.h"
#include "vm.h"
#include "vm_string.h"
#include "page.h"

int32_t alloc_page(int nr_vars)
{
	union vm_value *page = xmalloc(sizeof(union vm_value) * nr_vars);
	int32_t slot = heap_alloc_slot(VM_PAGE);
	heap[slot].page = page;
	return slot;
}

void delete_page(union vm_value *page, struct ain_variable *vars, int nr_vars)
{
	for (int i = 0; i < nr_vars; i++) {
		struct ain_struct *s;
		switch (vars[i].data_type) {
		case AIN_STRING:
			heap_unref(page[i].i);
			break;
		case AIN_STRUCT:
			if (page[i].i == -1)
				break;
			s = &ain->structures[vars[i].struct_type];
			delete_page(heap[page[i].i].page, s->members, s->nr_members);
			heap_unref(page[i].i);
			break;
		default:
			break;
		}
	}
}

/*
 * Recursively copy a page.
 */
union vm_value *copy_page(union vm_value *src, struct ain_variable *vars, int nr_vars)
{
	union vm_value *dst = xmalloc(sizeof(union vm_value) * nr_vars);
	for (int i = 0; i < nr_vars; i++) {
		int slot;
		struct ain_struct *s;
		union vm_value *page;
		switch (vars[i].data_type) {
		case AIN_STRING:
			slot = heap_alloc_slot(VM_STRING);
			heap[slot].s = string_dup(heap[src[i].i].s);
			dst[i].i = slot;
			break;
		case AIN_STRUCT:
			s = &ain->structures[vars[i].struct_type];
			page = copy_page(heap[src[i].i].page, s->members, s->nr_members);
			slot = heap_alloc_slot(VM_PAGE);
			heap[slot].page = page;
			dst[i].i = slot;
			break;
		default:
			dst[i] = src[i];
			break;
		}
	}
	return dst;
}
