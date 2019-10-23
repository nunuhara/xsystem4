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
	if (page->type == ARRAY_PAGE) {
		free(page->vars);
	}
	if (cache_no >= NR_CACHES || page_cache[cache_no].cached >= CACHE_SIZE) {
		free(page);
		return;
	}
	page_cache[cache_no].pages[page_cache[cache_no].cached++] = page;
}

struct page *alloc_page(enum page_type type, int nr_vars, struct ain_variable *vars)
{
	struct page *page = _alloc_page(nr_vars);
	page->type = type;
	page->nr_vars = nr_vars;
	page->vars = vars;
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
		return (union vm_value) { .i = -1 };
	case AIN_ARRAY_INT:
	case AIN_ARRAY_FLOAT:
	case AIN_ARRAY_STRING:
	case AIN_ARRAY_STRUCT:
	case AIN_ARRAY_FUNC_TYPE:
	case AIN_ARRAY_BOOL:
	case AIN_ARRAY_LONG_INT:
	case AIN_ARRAY_DELEGATE:
		slot = heap_alloc_slot(VM_PAGE);
		heap[slot].page = NULL;
		return (union vm_value) { .i = slot };
	default:
		return (union vm_value) { .i = 0 };
	}
}

struct array_type {
	struct ain_variable var;
	unsigned int size;
};

enum ain_data_type array_type(enum ain_data_type type)
{
	switch (type) {
	case AIN_ARRAY_INT: return AIN_INT;
	case AIN_ARRAY_FLOAT: return AIN_FLOAT;
	case AIN_ARRAY_STRING: return AIN_STRING;
	case AIN_ARRAY_STRUCT: return AIN_STRUCT;
	case AIN_ARRAY_FUNC_TYPE: return AIN_FUNC_TYPE;
	case AIN_ARRAY_BOOL: return AIN_BOOL;
	case AIN_ARRAY_LONG_INT: return AIN_LONG_INT;
	case AIN_ARRAY_DELEGATE: return AIN_DELEGATE;
	default: return AIN_VOID;
	}
}

struct page *alloc_array(int rank, union vm_value *dimensions, struct ain_variable *_var)
{
	struct array_type *var = xmalloc(sizeof(struct array_type));
	var->var.name = _var->name;
	var->var.data_type = rank == 1 ? (int32_t)array_type(_var->data_type) : _var->data_type;
	var->var.struct_type = _var->struct_type;
	var->var.array_dimensions = rank;
	var->size = dimensions->i;

	struct page *page = alloc_page(ARRAY_PAGE, dimensions->i, (struct ain_variable*)var);
	for (int i = 0; i < dimensions->i; i++) {
		if (rank == 1) {
			page->values[i] = variable_initval(array_type(_var->data_type));
		} else {
			struct page *child = alloc_array(rank - 1, dimensions + 1, _var);
			int slot = heap_alloc_slot(VM_PAGE);
			heap[slot].page = child;
			page->values[i].i = slot;
		}
	}
	return page;
}

void delete_page(struct page *page)
{
	for (int i = 0; i < page->nr_vars; i++) {
		enum ain_data_type type =
			page->type == ARRAY_PAGE
			? page->vars[0].data_type
			: page->vars[i].data_type;
		switch (type) {
		case AIN_STRING:
		case AIN_STRUCT:
		case AIN_ARRAY_INT:
		case AIN_ARRAY_FLOAT:
		case AIN_ARRAY_STRING:
		case AIN_ARRAY_STRUCT:
		case AIN_ARRAY_FUNC_TYPE:
		case AIN_ARRAY_BOOL:
		case AIN_ARRAY_LONG_INT:
		case AIN_ARRAY_DELEGATE:
			if (page->values[i].i == -1)
				break;
			heap_unref(page->values[i].i);
			break;
		default:
			break;
		}
	}
}

/*
 * Recursively copy a page.
 */
struct page *copy_page(struct page *src)
{
	struct page *dst = xmalloc(sizeof(struct page) + sizeof(union vm_value) * src->nr_vars);
	for (int i = 0; i < src->nr_vars; i++) {
		int slot;
		switch (src->vars[i].data_type) {
		case AIN_STRING:
			slot = heap_alloc_slot(VM_STRING);
			heap[slot].s = string_dup(heap[src->values[i].i].s);
			dst->values[i].i = slot;
			break;
		case AIN_STRUCT:
			slot = heap_alloc_slot(VM_PAGE);
			heap[slot].page = copy_page(heap[src->values[i].i].page);
			dst->values[i].i = slot;
			break;
		default:
			dst[i] = src[i];
			break;
		}
	}
	return dst;
}

enum ain_data_type variable_type(struct page *page, int varno)
{
	if (page->type == ARRAY_PAGE) {
		return page->vars[0].data_type;
	}
	return page->vars[varno].data_type;
}
