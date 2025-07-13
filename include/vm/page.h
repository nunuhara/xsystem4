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

#ifndef SYSTEM4_PAGE_H
#define SYSTEM4_PAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "system4/ain.h"
#include "vm.h"

struct ain_variable;
enum ain_data_type;

enum page_type {
	GLOBAL_PAGE,
	LOCAL_PAGE,
	STRUCT_PAGE,
	ARRAY_PAGE,
	DELEGATE_PAGE,
};

#define NR_PAGE_TYPES (ARRAY_PAGE+1)

/*
 * A page is an ordered collection of variables. Pages are used to implement
 * global and local variables, structures, arrays and delegates as follows:
 *
 * Global variables: all global variables are stored in a single global page.
 *
 * Local variables: each function invocation is backed by a page storing its
 * local variables.
 *
 * Structures: each struct object is backed by a page storing its members.
 *
 * Arrays: each array object is backed by a page storing its members.
 * Multi-dimensional arrays are implemented as a tree of pages (meaning the
 * whole array is NOT contiguous in memory).
 *
 * Delegates: each delegate object is backed by a page storing (object,
 * function, seq) triples.
 */
struct page {
	enum page_type type;
	// Value of `index`:
	// ----------------
	// GLOBAL_PAGE:   unused
	// LOCAL_PAGE:    function index
	// STRUCT_PAGE:   struct index
	// ARRAY_PAGE:    array data type (e.g. for AIN_ARRAY_STRING, AIN_STRING)
	// DELEGATE_PAGE: unused
	union {
		int index;
		enum ain_data_type a_type;
	};
	union {
		// local-specific metadata
		struct {
			int struct_ptr;
		} local;
		// array-specific metadata
		struct {
			int struct_type;
			int rank;
		} array;
	};
	int nr_vars;
	union vm_value values[];
};

static inline union vm_value page_get_var(struct page *page, int i)
{
#ifdef DEBUG_HEAP
	if (i < 0 || i >= page->nr_vars)
		VM_ERROR("Invalid page index: %d", i);
#endif
	return page->values[i];
}

static inline void _page_set_var(struct page *page, int i, union vm_value v)
{
#ifdef DEBUG_HEAP
	if (i < 0 || i >= page->nr_vars)
		VM_ERROR("Invalid page index: %d", i);
#endif
	page->values[i] = v;
}

#define page_set_var(page, i, v) _page_set_var(page, i, vm_value_cast(v))

// variables
union vm_value variable_initval(enum ain_data_type type);
void variable_fini(union vm_value v, enum ain_data_type type, bool call_dtor);
enum ain_data_type variable_type(struct page *page, int varno, int *struct_type, int *array_rank);
void variable_set(struct page *page, int varno, enum ain_data_type type, union vm_value val);

// pages
struct page *alloc_page(enum page_type type, int type_index, int nr_vars);
void free_page(struct page *page);
struct page *copy_page(struct page *page);
void delete_page_vars(struct page *page);
void delete_page(int slot);

// structs
int alloc_struct(int no);
void init_struct(int no, int slot);
void delete_struct(int no, int slot);
void create_struct(int no, union vm_value *var);

// arrays
enum ain_data_type array_type(enum ain_data_type type);
struct page *alloc_array(int rank, union vm_value *dimensions, enum ain_data_type data_type, int struct_type, bool init_structs);
struct page *realloc_array(struct page *src, int rank, union vm_value *dimensions, enum ain_data_type data_type, int struct_type, bool init_structs);
int array_numof(struct page *page, int rank);
void array_copy(struct page *dst, int dst_i, struct page *src, int src_i, int n);
int array_fill(struct page *dst, int dst_i, int n, union vm_value v);
struct page *array_pushback(struct page *dst, union vm_value v, enum ain_data_type data_type, int struct_type);
struct page *array_popback(struct page *dst);
struct page *array_erase(struct page *page, int i, bool *success);
struct page *array_insert(struct page *page, int i, union vm_value v, enum ain_data_type data_type, int struct_type);
void array_sort(struct page *page, int compare_fno);
void array_sort_mem(struct page *page, int member_no);
int array_find(struct page *page, int start, int end, union vm_value v, int compare_fno);
void array_reverse(struct page *page);

// delegates
struct page *delegate_new_from_method(int obj, int fun);
int delegate_numof(struct page *page);
bool delegate_contains(struct page *dst, int obj, int fun);
void delegate_erase(struct page *page, int obj, int fun);
struct page *delegate_append(struct page *dst, int obj, int fun);
struct page *delegate_plusa(struct page *dst, struct page *add);
struct page *delegate_minusa(struct page *dst, struct page *minus);
struct page *delegate_clear(struct page *page);
bool delegate_get(struct page *page, int i, int *obj_out, int *fun_out);

#endif /* SYSTEM4_PAGE_H */
