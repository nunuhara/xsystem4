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

#ifndef SYSTEM4_HEAP_H
#define SYSTEM4_HEAP_H

#include <stdint.h>

struct string;
struct page;

enum vm_pointer_type {
	VM_PAGE,
	VM_STRING
};
#define NR_VM_POINTER_TYPES (VM_STRING+1)

// Heap-backed objects. Reference counted.
struct vm_pointer {
	int ref;
	enum vm_pointer_type type;
	union {
		struct string *s;
		struct page *page;
	};
#ifdef DEBUG_HEAP
	size_t alloc_addr;
	size_t ref_addr;
	size_t free_addr;
#endif
};

struct vm_pointer *heap;

void heap_init(void);
int32_t heap_alloc_slot(enum vm_pointer_type type);
void heap_ref(int slot);
void heap_unref(int slot);

struct page *heap_get_page(int index);
void heap_set_page(int slot, struct page *page);

#endif /* SYSTEM4_HEAP_H */
