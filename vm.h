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

#include <stdbool.h>
#include <stdint.h>
#include "system4.h"

// Non-heap values. Stored in pages and on the stack.
union vm_value {
	int32_t i;
	int64_t li;
	float f;
	// HLL only
	void *ref;
	int *iref;
	float *fref;
};

enum vm_pointer_type {
	VM_PAGE,
	VM_STRING
};

#define NR_VM_POINTER_TYPES (VM_STRING+1)

struct string;
struct page;
enum ain_data_type;

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
#endif
};

struct hll_function {
	char *name;
	union vm_value (*fun)(union vm_value *_args);
};

struct library {
	char *name;
	struct hll_function **functions;
};

struct ain;

struct vm_pointer *heap;
union vm_value *stack;
int32_t stack_ptr;
struct ain *ain;

int32_t heap_alloc_slot(enum vm_pointer_type type);
void heap_ref(int slot);
void heap_unref(int slot);

static inline union vm_value _vm_id(union vm_value v)
{
	return v;
}

static inline union vm_value vm_int(int32_t v)
{
	return (union vm_value) { .i = v };
}

static inline union vm_value vm_long(int64_t v)
{
	return (union vm_value) { .li = v };
}

static inline union vm_value vm_bool(bool b)
{
	return (union vm_value) { .i = !!b };
}

static inline union vm_value vm_float(float v)
{
	return (union vm_value) { .f = v };
}

#define vm_value_cast(v) _Generic((v),				\
				  union vm_value: _vm_id,	\
				  int32_t: vm_int,		\
				  int64_t: vm_long,		\
				  bool: vm_bool,		\
				  float: vm_float)(v)

static inline void stack_set_value(int n, union vm_value v)
{
	stack[stack_ptr - (1 + n)] = v;
}

static inline void stack_push_value(union vm_value v)
{
	stack[stack_ptr++] = v;
}

// Set the Nth value from the top of the stack to V.
#define stack_set(n, v) (stack_set_value((n), vm_value_cast(v)))
#define stack_push(v) (stack_push_value(vm_value_cast(v)))
union vm_value stack_pop(void);

void heap_set_page(int slot, struct page *page);

union vm_value global_get(int varno);
void global_set(int varno, union vm_value val);
struct page *vm_get_page(int index);

int vm_string_ref(struct string *s);
int vm_copy_page(struct page *page);
union vm_value vm_copy(union vm_value v, enum ain_data_type type);

void vm_call(int fno, int struct_page);
int vm_time(void);

void vm_stack_trace(void);
noreturn void _vm_error(const char *fmt, ...);
noreturn void vm_exit(int code);

#define VM_ERROR(fmt, ...) \
	_vm_error("*ERROR*(%s:%s:%d): " fmt "\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__)

#endif /* SYSTEM4_VM_H */
