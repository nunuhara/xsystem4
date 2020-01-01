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
#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

#define INITIAL_HEAP_SIZE  4096
#define HEAP_ALLOC_STEP    4096

struct vm_pointer *heap;
size_t heap_size;

// Heap free list
// This is a list of unused indices into the 'heap' array.
int32_t *heap_free_stack;
size_t heap_free_ptr = 0;

static const char *vm_ptrtype_strtab[] = {
	[VM_PAGE] = "VM_PAGE",
	[VM_STRING] = "VM_STRING",
};

static const char *vm_ptrtype_string(enum vm_pointer_type type) {
	if (type < NR_VM_POINTER_TYPES)
		return vm_ptrtype_strtab[type];
	return "INVALID POINTER TYPE";
}

void heap_init(void)
{
	heap_size = INITIAL_HEAP_SIZE;
	heap = xcalloc(1, INITIAL_HEAP_SIZE * sizeof(struct vm_pointer));

	heap_free_stack = xmalloc(INITIAL_HEAP_SIZE * sizeof(int32_t));
	for (size_t i = 0; i < INITIAL_HEAP_SIZE; i++) {
		heap_free_stack[i] = i;
	}
	heap_free_ptr = 1; // global page at index 0
}

int32_t heap_alloc_slot(enum vm_pointer_type type)
{
	// grow heap if needed
	if (heap_free_ptr >= heap_size) {
		heap = xrealloc(heap, sizeof(struct vm_pointer) * (heap_size+HEAP_ALLOC_STEP));
		heap_free_stack = xrealloc(heap_free_stack, sizeof(int32_t) * (heap_size+HEAP_ALLOC_STEP));
		for (size_t i = heap_size; i < heap_size+HEAP_ALLOC_STEP; i++) {
			heap[i].ref = 0;
			heap_free_stack[i] = i;
		}
		heap_size += HEAP_ALLOC_STEP;
	}

	int32_t slot = heap_free_stack[heap_free_ptr++];
	heap[slot].ref = 1;
	heap[slot].type = type;
#ifdef DEBUG_HEAP
	heap[slot].alloc_addr = instr_ptr;
	heap[slot].ref_addr = 0;
#endif
	return slot;
}

static void heap_free_slot(int32_t slot)
{
	heap_free_stack[--heap_free_ptr] = slot;
}

void heap_ref(int32_t slot)
{
	if (slot == -1)
		return;
	heap[slot].ref++;
#ifdef DEBUG_HEAP
	heap[slot].ref_addr = instr_ptr;
#endif
}

void heap_unref(int slot)
{
	if (heap[slot].ref <= 0) {
#ifdef DEBUG_HEAP
		VM_ERROR("double free of slot %d (%s)\nOriginally allocated at %X\nOriginally freed at %X",
			 slot, vm_ptrtype_string(heap[slot].type), heap[slot].alloc_addr, heap[slot].free_addr);
#endif
		VM_ERROR("double free of slot %d (%s)", slot, vm_ptrtype_string(heap[slot].type));
	}
	if (heap[slot].ref > 1) {
		heap[slot].ref--;
		return;
	}
#ifdef DEBUG_HEAP
	heap[slot].free_addr = instr_ptr;
#endif
	switch (heap[slot].type) {
	case VM_PAGE:
		if (heap[slot].page) {
			delete_page(slot);
		}
		break;
	case VM_STRING:
		free_string(heap[slot].s);
		break;
	}
	heap[slot].ref = 0;
	heap_free_slot(slot);
}

// XXX: special version of heap_unref which avoids calling destructors
void exit_unref(int slot)
{
	if (slot < 0 || (size_t)slot >= heap_size) {
		WARNING("out of bounds heap index: %d", slot);
		return;
	}
	if (heap[slot].ref <= 0) {
		WARNING("double free of slot %d", slot);
		return;
	}
	if (heap[slot].ref > 1) {
		heap[slot].ref--;
		return;
	}
	switch (heap[slot].type) {
	case VM_PAGE:
		if (heap[slot].page) {
			struct page *page = heap[slot].page;
			for (int i = 0; i < page->nr_vars; i++) {
				switch (variable_type(page, i, NULL, NULL)) {
				case AIN_STRING:
				case AIN_STRUCT:
				case AIN_ARRAY_TYPE:
				case AIN_REF_TYPE:
					if (page->values[i].i == -1)
						break;
					exit_unref(page->values[i].i);
					break;
				default:
					break;
				}
			}
		}
		break;
	case VM_STRING:
		free_string(heap[slot].s);
		break;
	}
	heap[slot].ref = 0;
	heap_free_slot(slot);
}

bool heap_index_valid(int index)
{
	return index >= 0 && (size_t)index < heap_size && heap[index].ref > 0;
}

bool page_index_valid(int index)
{
	return heap_index_valid(index) && heap[index].type == VM_PAGE;
}

struct page *heap_get_page(int index)
{
	if (!page_index_valid(index))
		VM_ERROR("Invalid page index: %d", index);
	return heap[index].page;
}

void heap_set_page(int slot, struct page *page)
{
	heap[slot].page = page;
}
