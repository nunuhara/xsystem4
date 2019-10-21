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

#ifndef SYSTEM4_VM_H
#define SYSTEM4_VM_H

#include <stdint.h>

// Non-heap values. Stored in pages and on the stack.
union vm_value {
	int32_t i;
	int64_t i64;
	float f;
};

enum vm_pointer_type {
	VM_FRAME,
	VM_PAGE,
	VM_STRING
};

// Heap-backed objects. Reference counted.
struct vm_pointer {
	int ref;
	enum vm_pointer_type type;
	union {
		struct string *s;
		union vm_value *page;
	};
};

struct ain;

struct vm_pointer *heap;
struct ain *ain;

int32_t heap_alloc_slot(enum vm_pointer_type type);
void heap_ref(int slot);
void heap_unref(int slot);

#endif /* SYSTEM4_VM_H */
