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
	if (cache_no >= NR_CACHES || page_cache[cache_no].cached >= CACHE_SIZE) {
		free(page);
		return;
	}
	page_cache[cache_no].pages[page_cache[cache_no].cached++] = page;
}

struct page *alloc_page(int nr_vars, struct ain_variable *vars)
{
	struct page *page = _alloc_page(nr_vars);
	page->nr_vars = nr_vars;
	page->vars = vars;
	return page;
}

void delete_page(struct page *page)
{
	for (int i = 0; i < page->nr_vars; i++) {
		switch (page->vars[i].data_type) {
		case AIN_STRING:
		case AIN_STRUCT:
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
