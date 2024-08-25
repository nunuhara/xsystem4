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

struct ain;
enum ain_data_type;
struct page;
struct string;

// xsystem4-specific system calls (used for hacks)
enum vm_extra_syscall {
	VM_XSYS_KEY_IS_DOWN = 0x80000000,
};

// Non-heap values. Stored in pages and on the stack.
union vm_value {
	int32_t i;
	float f;
	void *ref; // for casting HLL return value
};

struct static_hll_function {
	char *name;
	void *fun;
};

struct static_library {
	char *name;
	struct static_hll_function functions[];
};

extern struct ain *ain;

extern union vm_value *stack;
extern int32_t stack_ptr;

static inline union vm_value _vm_id(union vm_value v)
{
	return v;
}

static inline union vm_value vm_int(int32_t v)
{
	return (union vm_value) { .i = v };
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
union vm_value stack_peek(int n);
union vm_value *stack_peek_var(void);

union vm_value local_get(int varno);
union vm_value global_get(int varno);
union vm_value member_get(int varno);
void global_set(int varno, union vm_value val, bool call_dtors);
struct page *local_page(void);
struct page *get_local_page(int frame_no);
struct page *get_struct_page(int frame_no);

int vm_string_ref(struct string *s);
int vm_copy_page(struct page *page);
union vm_value vm_copy(union vm_value v, enum ain_data_type type);

int vm_execute_ain(struct ain *program);
void vm_call(int fno, int struct_page);
int vm_time(void);
void vm_sleep(int ms);

void hll_call(int libno, int fno);
bool library_exists(int libno);
bool library_function_exists(int libno, int fno);
void init_libraries(void);
void exit_libraries(void);

// NOTE: This can probably be merged with ain_data_type, since the values are
// disjoint.
enum string_format_type {
	STRFMT_INT = 2,
	STRFMT_FLOAT = 3,
	STRFMT_STRING = 4,
	STRFMT_BOOL = 48,
	STRFMT_LONG_INT = 56,
};
struct string *string_format(struct string *fmt, union vm_value arg, enum string_format_type type);

void vm_stack_trace(void);
_Noreturn void _vm_error(const char *fmt, ...);
_Noreturn void vm_exit(int code);
_Noreturn void vm_reset(void);

extern bool vm_reset_once;

#if defined(__ANDROID__) || defined(RELEASE)
// Report the error with a message box and exit.
#define VM_ERROR ERROR
#else
#define VM_ERROR(fmt, ...) \
	_vm_error("*ERROR*(%s:%s:%d): " fmt "\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__)
#endif

#ifdef VM_PRIVATE
#include "system4/little_endian.h"
#include "system4/ain.h"

struct function_call {
	int32_t fno;
	uint32_t call_address;
	uint32_t return_address;
	int32_t page_slot;
	int32_t struct_page;
};

extern struct function_call call_stack[4096];
extern int32_t call_stack_ptr;

extern size_t instr_ptr;

// Read argument N for the current instruction.
static inline int32_t get_argument(int n)
{
	return LittleEndian_getDW(ain->code, instr_ptr + 2 + n*4);
}

// XXX: not strictly portable
static inline float get_argument_float(int n)
{
	union vm_value v;
	v.i = LittleEndian_getDW(ain->code, instr_ptr + 2 + n*4);
	return v.f;
}

uint32_t get_switch_address(int no, int val);
uint32_t get_strswitch_address(int no, struct string *str);

int vm_save_image(const char *key, const char *path);
void vm_load_image(const char *key, const char *path);
struct page *vm_load_image_comments(const char *key, const char *path, int *success);
int vm_write_image_comments(const char *key, const char *path, struct page *comments);

#endif /* VM_PRIVATE */
#endif /* SYSTEM4_VM_H */
