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

#define NR_CACHES 8
#define CACHE_SIZE 64

static const char *pagetype_strtab[] = {
	[GLOBAL_PAGE] = "GLOBAL_PAGE",
	[LOCAL_PAGE] = "LOCAL_PAGE",
	[STRUCT_PAGE] = "STRUCT_PAGE",
	[ARRAY_PAGE] = "ARRAY_PAGE"
};

const char *pagetype_string(enum page_type type)
{
	if (type < NR_PAGE_TYPES)
		return pagetype_strtab[type];
	return "INVALID PAGE TYPE";
}

struct page_cache {
	unsigned int cached;
	struct page *pages[CACHE_SIZE];
};

struct page_cache page_cache[NR_CACHES];

struct page *_alloc_page(int nr_vars)
{
	int cache_nr = nr_vars - 1;
	if (cache_nr < NR_CACHES && page_cache[cache_nr].cached) {
		return page_cache[cache_nr].pages[--page_cache[cache_nr].cached];
	}
	return xmalloc(sizeof(struct page) + sizeof(union vm_value) * nr_vars);
}

void free_page(struct page *page)
{
	int cache_no = page->nr_vars - 1;
	if (cache_no < 0 || cache_no >= NR_CACHES || page_cache[cache_no].cached >= CACHE_SIZE) {
		free(page);
		return;
	}
	page_cache[cache_no].pages[page_cache[cache_no].cached++] = page;
}

struct page *alloc_page(enum page_type type, int type_index, int nr_vars)
{
	struct page *page = _alloc_page(nr_vars);
	page->type = type;
	page->index = type_index;
	page->nr_vars = nr_vars;
	return page;
}

union vm_value variable_initval(enum ain_data_type type)
{
	int slot;
	switch (type) {
	case AIN_STRING:
		slot = heap_alloc_slot(VM_STRING);
		heap[slot].s = string_ref(&EMPTY_STRING);
		return (union vm_value) { .i = slot };
	case AIN_STRUCT:
	case AIN_REF_TYPE:
		return (union vm_value) { .i = -1 };
	case AIN_ARRAY_TYPE:
		slot = heap_alloc_slot(VM_PAGE);
		heap_set_page(slot, NULL);
		return (union vm_value) { .i = slot };
	default:
		return (union vm_value) { .i = 0 };
	}
}

void variable_fini(union vm_value v, enum ain_data_type type)
{
	switch (type) {
	case AIN_STRING:
	case AIN_STRUCT:
	case AIN_ARRAY_TYPE:
	case AIN_REF_TYPE:
		if (v.i == -1)
			break;
		heap_unref(v.i);
		break;
	default:
		break;
	}
}

enum ain_data_type array_type(enum ain_data_type type)
{
	switch (type) {
	case AIN_ARRAY_INT:       return AIN_INT;
	case AIN_ARRAY_FLOAT:     return AIN_FLOAT;
	case AIN_ARRAY_STRING:    return AIN_STRING;
	case AIN_ARRAY_STRUCT:    return AIN_STRUCT;
	case AIN_ARRAY_FUNC_TYPE: return AIN_FUNC_TYPE;
	case AIN_ARRAY_BOOL:      return AIN_BOOL;
	case AIN_ARRAY_LONG_INT:  return AIN_LONG_INT;
	case AIN_ARRAY_DELEGATE:  return AIN_DELEGATE;
	default:                  return type;
	}
}

enum ain_data_type variable_type(struct page *page, int varno, int *struct_type)
{
	switch (page->type) {
	case GLOBAL_PAGE:
		if (struct_type)
			*struct_type = ain->globals[varno].struct_type;
		return ain->globals[varno].data_type;
	case LOCAL_PAGE:
		if (struct_type)
			*struct_type = ain->functions[page->index].vars[varno].struct_type;
		return ain->functions[page->index].vars[varno].data_type;
	case STRUCT_PAGE:
		if (struct_type)
			*struct_type = ain->structures[page->index].members[varno].struct_type;
		return ain->structures[page->index].members[varno].data_type;
	case ARRAY_PAGE:
		if (struct_type)
			*struct_type = page->struct_type;
		return page->rank > 1 ? page->a_type : array_type(page->a_type);
	}
	return AIN_VOID;
}

void delete_page(struct page *page)
{
	for (int i = 0; i < page->nr_vars; i++) {
		variable_fini(page->values[i], variable_type(page, i, NULL));
	}
}

/*
 * Recursively copy a page.
 */
struct page *copy_page(struct page *src)
{
	if (!src)
		return NULL;
	struct page *dst = alloc_page(src->type, src->index, src->nr_vars);
	dst->struct_type = src->struct_type;
	dst->rank = src->rank;
	for (int i = 0; i < src->nr_vars; i++) {
		dst->values[i] = vm_copy(src->values[i], variable_type(src, i, NULL));
	}
	return dst;
}

int alloc_struct(int no)
{
	struct ain_struct *s = &ain->structures[no];
	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, alloc_page(STRUCT_PAGE, no, s->nr_members));
	for (int i = 0; i < s->nr_members; i++) {
		if (s->members[i].data_type == AIN_STRUCT) {
			heap[slot].page->values[i].i = alloc_struct(s->members[i].struct_type);
		} else {
			heap[slot].page->values[i] = variable_initval(s->members[i].data_type);
		}
	}
	return slot;
}

void init_struct(int no, int slot)
{
	struct ain_struct *s = &ain->structures[no];
	for (int i = 0; i < s->nr_members; i++) {
		if (s->members[i].data_type == AIN_STRUCT) {
			init_struct(s->members[i].struct_type, heap[slot].page->values[i].i);
		}
	}
	if (s->constructor > 0) {
		vm_call(s->constructor, slot);
	}
}

void create_struct(int no, union vm_value *var)
{
	var->i = alloc_struct(no);
	init_struct(no, var->i);
}

struct page *alloc_array(int rank, union vm_value *dimensions, int data_type, int struct_type, bool init_structs)
{
	if (rank < 1)
		return NULL;

	enum ain_data_type type = array_type(data_type);
	struct page *page = alloc_page(ARRAY_PAGE, data_type, dimensions->i);
	page->struct_type = struct_type;
	page->rank = rank;

	for (int i = 0; i < dimensions->i; i++) {
		if (rank == 1) {
			if (type == AIN_STRUCT && init_structs)
				create_struct(struct_type, &page->values[i]);
			else
				page->values[i] = variable_initval(type);
		} else {
			struct page *child = alloc_array(rank - 1, dimensions + 1, data_type, struct_type, init_structs);
			int slot = heap_alloc_slot(VM_PAGE);
			heap_set_page(slot, child);
			page->values[i].i = slot;
		}
	}
	return page;
}

struct page *realloc_array(struct page *src, int rank, union vm_value *dimensions, int data_type, int struct_type, bool init_structs)
{
	if (rank < 1)
		ERROR("Tried to allocate 0-rank array");
	if (!src && !dimensions->i)
		return NULL;
	if (!src)
		return alloc_array(rank, dimensions, data_type, struct_type, init_structs);
	if (src->type != ARRAY_PAGE)
		ERROR("Not an array");
	if (src->rank != rank)
		ERROR("Attempt to reallocate array with different rank");
	if (!dimensions->i) {
		delete_page(src);
		return NULL;
	}

	// if shrinking array, unref orphaned children
	if (dimensions->i < src->nr_vars) {
		for (int i = dimensions->i; i < src->nr_vars; i++) {
			variable_fini(src->values[i], variable_type(src, i, NULL));
		}
	}

	src = xrealloc(src, sizeof(struct page) + sizeof(union vm_value) * dimensions->i);

	// if growing array, init new children
	enum ain_data_type type = array_type(data_type);
	if (dimensions->i > src->nr_vars) {
		for (int i = src->nr_vars; i < dimensions->i; i++) {
			if (rank == 1) {
				if (type == AIN_STRUCT && init_structs)
					create_struct(struct_type, &src->values[i]);
				else
					src->values[i] = variable_initval(type);
			} else {
				struct page *child = alloc_array(rank - 1, dimensions + 1, data_type, struct_type, init_structs);
				int slot = heap_alloc_slot(VM_PAGE);
				heap_set_page(slot, child);
				src->values[i].i = slot;
			}
		}
	}

	src->nr_vars = dimensions->i;
	return src;
}

int array_numof(struct page *page, int rank)
{
	if (!page)
		return 0;
	if (rank < 1 || rank > page->rank)
		return 0;
	if (rank == 1) {
		return page->nr_vars;
	}
	return array_numof(heap[page->values[0].i].page, rank - 1);
}

static bool array_index_ok(struct page *array, int i)
{
	return i >= 0 && i < array->nr_vars;
}

void array_copy(struct page *dst, int dst_i, struct page *src, int src_i, int n)
{
	if (!dst || !src)
		ERROR("Array is NULL");
	if (dst->type != ARRAY_PAGE || src->type != ARRAY_PAGE)
		ERROR("Not an array");
	if (!array_index_ok(dst, dst_i) || !array_index_ok(src, src_i))
		ERROR("Out of bounds array access");
	if (dst->rank != 1 || src->rank != 1)
		ERROR("Tried to copy to/from a multi-dimensional array");
	if (dst->a_type != src->a_type)
		ERROR("Array types do not match");

	for (int i = 0; i < n; i++) {
		dst->values[dst_i + i] = vm_copy(src->values[src_i + i], array_type(dst->a_type));
	}
}

int array_fill(struct page *dst, int dst_i, int n, union vm_value v)
{
	if (!dst)
		ERROR("Array is NULL");
	if (dst->type != ARRAY_PAGE)
		ERROR("Not an array");

	// clamp (dst_i, dst_i+n) to range of array
	if (dst_i < 0) {
		n += dst_i;
		dst_i = 0;
	}
	if (dst_i >= dst->nr_vars)
		return 0;
	if (dst_i + n >= dst->nr_vars)
		n = dst->nr_vars - dst_i;

	for (int i = 0; i < n; i++) {
		dst->values[dst_i + i] = vm_copy(v, array_type(dst->a_type));
	}
	return n;
}

void array_pushback(struct page **dst, union vm_value v, int data_type, int struct_type)
{
	if (*dst) {
		if ((*dst)->type != ARRAY_PAGE)
			ERROR("Not an array");
		if ((*dst)->rank != 1)
			ERROR("Tried pushing to a multi-dimensional array");

		int index = (*dst)->nr_vars;
		union vm_value dims[1] = { (union vm_value) { .i = index + 1 } };
		*dst = realloc_array(*dst, 1, dims, (*dst)->a_type, (*dst)->struct_type, false);
		(*dst)->values[index] = v;
	} else {
		union vm_value dims[1] = { (union vm_value) { .i = 1 } };
		*dst = alloc_array(1, dims, data_type, struct_type, false);
		(*dst)->values[0] = v;
	}
}

void array_popback(struct page **dst)
{
	if (!(*dst))
		return;
	if ((*dst)->type != ARRAY_PAGE)
		ERROR("Not an array");
	if ((*dst)->rank != 1)
		ERROR("Tried popping from a multi-dimensional array");

	union vm_value dims[1] = { (union vm_value) { .i = (*dst)->nr_vars - 1 } };
	*dst = realloc_array(*dst, 1, dims, (*dst)->a_type, (*dst)->struct_type, false);
}

bool array_erase(struct page **_page, int i)
{
	struct page *page = *_page;
	if (!page)
		return false;
	if (page->type != ARRAY_PAGE)
		ERROR("Not an array");
	if (page->rank != 1)
		ERROR("Tried erasing from a multi-dimensional array");
	if (!array_index_ok(page, i))
		return false;

	// if array will be empty...
	if (page->nr_vars == 1) {
		delete_page(page);
		*_page = NULL;
		return true;
	}

	// delete variable, shift subsequent variables, then realloc page
	variable_fini(page->values[i], array_type(page->a_type));
	for (int j = i + 1; j < page->nr_vars; j++) {
		page->values[j-1] = page->values[j];
	}
	page->nr_vars--;
	*_page = page = xrealloc(page, sizeof(struct page) + sizeof(union vm_value) * page->nr_vars);

	return true;
}

void array_insert(struct page **_page, int i, union vm_value v, int data_type, int struct_type)
{
	struct page *page = *_page;
	if (!page) {
		array_pushback(_page, v, data_type, struct_type);
		return;
	}
	if (page->type != ARRAY_PAGE)
		ERROR("Not an array");
	if (page->rank != 1)
		ERROR("Tried inserting into a multi-dimensional array");

	// NOTE: you cannot insert at the end of an array due to how i is clamped
	if (i >= page->nr_vars)
		i = page->nr_vars - 1;
	if (i < 0)
		i = 0;

	page->nr_vars++;
	*_page = page = xrealloc(page, sizeof(struct page) + sizeof(union vm_value) * page->nr_vars);
	for (int j = page->nr_vars - 1; j > i; j--) {
		page->values[j] = page->values[j-1];
	}
	page->values[i] = v;
}

static int current_sort_function;

static int array_compare(const void *_a, const void *_b)
{
	union vm_value a = *((union vm_value*)_a);
	union vm_value b = *((union vm_value*)_b);
	stack_push(a);
	stack_push(b);
	vm_call(current_sort_function, -1);
	return stack_pop().i;
}

void array_sort(struct page *page, int compare_fno)
{
	if (!page)
		return;

	current_sort_function = compare_fno;
	qsort(page->values, sizeof(union vm_value), page->nr_vars, array_compare);
}

int array_find(struct page *page, int start, int end, union vm_value v, int compare_fno)
{
	if (!page)
		return -1;

	start = max(start, 0);
	end = min(end, page->nr_vars);

	// if no compare function given, compare integer values
	if (!compare_fno) {
		for (int i = start; i < end; i++) {
			if (page->values[i].i == v.i)
				return i;
		}
		return -1;
	}

	for (int i = start; i < end; i++) {
		stack_push(v);
		stack_push(page->values[i]);
		vm_call(compare_fno, -1);
		if (stack_pop().i)
			return i;
	}
	return -1;
}
