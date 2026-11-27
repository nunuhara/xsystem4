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

#define VM_PRIVATE

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

#define INITIAL_HEAP_SIZE  4096
#define HEAP_ALLOC_STEP    4096

struct vm_pointer *heap = NULL;
size_t heap_size = 0;
uint32_t heap_next_seq;

// Heap free list
// This is a list of unused indices into the 'heap' array.
int32_t *heap_free_stack = NULL;
size_t heap_free_ptr = 0;

static int *delegate_slots;
static size_t nr_delegate_slots, delegate_slots_capacity;

static void unregister_delegate_page(int slot)
{
	int index = heap[slot].delegate_index;
	if (index < 0) return;
	int last = delegate_slots[--nr_delegate_slots];
	delegate_slots[index] = last;
	heap[last].delegate_index = index;
	heap[slot].delegate_index = -1;
}

static const char *vm_ptrtype_strtab[] = {
	[VM_PAGE] = "VM_PAGE",
	[VM_STRING] = "VM_STRING",
};

static const char *vm_ptrtype_string(enum vm_pointer_type type) {
	if (type < NR_VM_POINTER_TYPES)
		return vm_ptrtype_strtab[type];
	return "INVALID POINTER TYPE";
}

void heap_grow(size_t new_size)
{
	assert(new_size > heap_size);
	heap = xrealloc(heap, sizeof(struct vm_pointer) * new_size);
	heap_free_stack = xrealloc(heap_free_stack, sizeof(int32_t) * new_size);
	for (size_t i = heap_size; i < new_size; i++) {
		heap[i].ref = 0;
		heap[i].has_delegate_target = false;
		heap[i].exiting = false;
		heap[i].delegate_index = -1;
		heap_free_stack[i] = i;
	}
	heap_size = new_size;
}

void heap_init(void)
{
	if (!heap) {
		heap_size = INITIAL_HEAP_SIZE;
		heap = xcalloc(1, INITIAL_HEAP_SIZE * sizeof(struct vm_pointer));
		heap_free_stack = xmalloc(INITIAL_HEAP_SIZE * sizeof(int32_t));
	} else {
		for (size_t i = 0; i < heap_size; i++) {
			if (heap[i].ref <= 0)
				continue;
			switch (heap[i].type) {
			case VM_PAGE:
				if (heap[i].page)
					free_page(heap[i].page);
				break;
			case VM_STRING:
				if (heap[i].s)
					free_string(heap[i].s);
				break;
			}
		}
		memset(heap, 0, heap_size * sizeof(*heap));
	}

	for (size_t i = 0; i < heap_size; i++) {
		heap_free_stack[i] = i;
		heap[i].delegate_index = -1;
	}
	nr_delegate_slots = 0;
	heap_free_ptr = 1; // global page at index 0
	heap_next_seq = 1;
}

int32_t heap_alloc_slot(enum vm_pointer_type type)
{
	if (heap_free_ptr >= heap_size) {
		heap_grow(heap_size+HEAP_ALLOC_STEP);
	}

	int32_t slot = heap_free_stack[heap_free_ptr++];
	heap[slot].ref = 1;
	heap[slot].seq = heap_next_seq++;
	heap[slot].type = type;
	heap[slot].has_delegate_target = false;
	heap[slot].exiting = false;
	heap[slot].page = NULL;
	heap[slot].delegate_index = -1;
#ifdef DEBUG_HEAP
	heap[slot].alloc_addr = instr_ptr;
	memset(heap[slot].ref_addr, 0, sizeof(heap[slot].ref_addr));
	heap[slot].ref_nr = 0;
	memset(heap[slot].deref_addr, 0, sizeof(heap[slot].deref_addr));
	heap[slot].deref_nr = 0;
	heap[slot].free_addr = 0;
#endif
	return slot;
}

static void heap_free_slot(int32_t slot)
{
	unregister_delegate_page(slot);
	heap[slot].has_delegate_target = false;
	heap[slot].exiting = false;
	heap[slot].seq = 0;
	heap_free_stack[--heap_free_ptr] = slot;
}

static void heap_double_free(int32_t slot)
{
#ifdef DEBUG_HEAP
		WARNING("double free of slot %d (%s)\nOriginally allocated at %X\nOriginally freed at %X",
			 slot, vm_ptrtype_string(heap[slot].type),
			 heap[slot].alloc_addr, heap[slot].free_addr);
#else
		WARNING("double free of slot %d (%s)", slot, vm_ptrtype_string(heap[slot].type));
#endif
}

void heap_ref(int32_t slot)
{
	if (slot < 0)
		return;
	if (unlikely((size_t)slot >= heap_size)) {
		WARNING("out of bounds heap index in heap_ref: %d", slot);
		return;
	}
	heap[slot].ref++;
#ifdef DEBUG_HEAP
	heap[slot].ref_addr[heap[slot].ref_nr++ % 16] = instr_ptr;
#endif
}

void heap_unref(int slot)
{
	if (slot < 0)
		return;
	if (unlikely((size_t)slot >= heap_size)) {
		WARNING("out of bounds heap index in heap_unref: %d", slot);
		return;
	}
	if (unlikely(slot == 0 && heap[0].ref <= 1)) {
		VM_ERROR("Attempted to free global page (slot 0)");
	}
	if (heap[slot].exiting) {
		if (heap[slot].ref > 1)
			heap[slot].ref--;
		return;
	}
	if (unlikely(heap[slot].ref <= 0)) {
		heap_double_free(slot);
		VM_ERROR("double free");
	}
	if (heap[slot].ref > 1) {
#ifdef DEBUG_HEAP
		heap[slot].deref_addr[heap[slot].deref_nr++ % 16] = instr_ptr;
#endif
		heap[slot].ref--;
		return;
	}
#ifdef DEBUG_HEAP
	heap[slot].free_addr = instr_ptr;
#endif
	heap[slot].exiting = true;
	switch (heap[slot].type) {
	case VM_PAGE:
		if (heap[slot].page) {
			delete_page(slot);
		}
		if (heap[slot].ref > 1) {
			heap[slot].ref--;
			heap[slot].exiting = false;
			return;
		}
		break;
	case VM_STRING:
		if (heap[slot].s) {
			struct string *s = heap[slot].s;
			heap[slot].s = NULL;
			free_string(s);
		}
		break;
	}
	heap[slot].ref = 0;
	heap[slot].exiting = false;
	heap_free_slot(slot);
}

// XXX: special version of heap_unref which avoids calling destructors
void exit_unref(int slot)
{
	if (slot < 0 || (size_t)slot >= heap_size) {
		WARNING("out of bounds heap index: %d", slot);
		return;
	}
	if (heap[slot].exiting)
		return;
	if (heap[slot].ref <= 0) {
		heap_double_free(slot);
		return;
	}
	if (heap[slot].ref > 1) {
#ifdef DEBUG_HEAP
		heap[slot].deref_addr[heap[slot].deref_nr++ % 16] = 0xDEADC0DE;
#endif
		heap[slot].ref--;
		return;
	}
	// Mark exiting before recursing into children so cyclic page references
	// cannot loop infinitely or re-enter heap_double_free during shutdown.
	heap[slot].exiting = true;
	heap[slot].ref = 0;
	switch (heap[slot].type) {
	case VM_PAGE:
		if (heap[slot].page) {
			struct page *page = heap[slot].page;
			heap[slot].page = NULL;
			for (int i = 0; i < page->nr_vars; i++) {
				page_var_fini(page, i, false);
			}
			if (page->type == LOCAL_PAGE && page->local.parent_page >= 0)
				exit_unref(page->local.parent_page);
			free_page(page);
		}
		break;
	case VM_STRING:
		if (heap[slot].s) {
			struct string *s = heap[slot].s;
			heap[slot].s = NULL;
			free_string(s);
		}
		break;
	}
	heap[slot].exiting = false;
	heap_free_slot(slot);
}

uint32_t heap_get_seq(int slot)
{
	return heap_index_valid(slot) ? heap[slot].seq : 0;
}

bool heap_index_valid(int index)
{
	return index >= 0 && (size_t)index < heap_size && heap[index].ref > 0;
}

bool page_index_valid(int index)
{
	return heap_index_valid(index) && heap[index].type == VM_PAGE;
}

bool string_index_valid(int index)
{
	return heap_index_valid(index) && heap[index].type == VM_STRING;
}

struct page *heap_get_page(int index)
{
	if (unlikely(!page_index_valid(index)))
		VM_ERROR("Invalid page index: %d", index);
	return heap[index].page;
}

struct string *heap_get_string(int index)
{
	if (unlikely(!string_index_valid(index)))
		VM_ERROR("Invalid string index: %d", index);
	return heap[index].s;
}

struct page *heap_get_delegate_page(int index)
{
	struct page *page = heap_get_page(index);
	if (unlikely(page && page->type != DELEGATE_PAGE))
		VM_ERROR("Not a delegate page: %d", index);
	return page;
}

void heap_mark_delegate_target(int slot)
{
	if (heap_index_valid(slot))
		heap[slot].has_delegate_target = true;
}

void heap_refresh_delegate_targets(void)
{
	int width = delegate_stride();
	for (size_t d = 0; d < nr_delegate_slots; d++) {
		struct page *page = heap[delegate_slots[d]].page;
		if (!page || page->type != DELEGATE_PAGE)
			continue;
		for (int i = 0; i < page->nr_vars; i += width) {
			int target = page->values[i].i;
			if (target >= 0 && (uint32_t)page->values[i + 2].i == heap_get_seq(target))
				heap_mark_delegate_target(target);
		}
	}
}

void heap_set_page(int slot, struct page *page)
{
#ifdef DEBUG_HEAP
	if (unlikely(!page_index_valid(slot)))
		VM_ERROR("Invalid page index: %d", slot);
#endif
	heap[slot].page = page;
	if (page && page->type == DELEGATE_PAGE) {
		if (heap[slot].delegate_index < 0) {
			if (nr_delegate_slots == delegate_slots_capacity) {
				delegate_slots_capacity = max(delegate_slots_capacity * 2, 64);
				delegate_slots = xrealloc(delegate_slots, delegate_slots_capacity * sizeof(*delegate_slots));
			}
			heap[slot].delegate_index = nr_delegate_slots;
			delegate_slots[nr_delegate_slots++] = slot;
		}
		int width = delegate_stride();
		for (int i = 0; i < page->nr_vars; i += width) {
			int target = page->values[i].i;
			if (target >= 0 && (uint32_t)page->values[i + 2].i == heap_get_seq(target))
				heap_mark_delegate_target(target);
		}
	} else {
		unregister_delegate_page(slot);
	}
}

void heap_copy_delegate_targets(int source, int destination)
{
	if (ain->version < 11 || source == destination
			|| !heap_index_valid(source) || !heap[source].has_delegate_target)
		return;
	struct page *object = heap_get_page(source);
	if (!object || object->type != STRUCT_PAGE)
		return;
	uint32_t seq = heap_get_seq(source);
	// v12 value copies inherit the source object's event subscriptions. The
	// bindings remain weak: deleting either object removes only its own entry.
	// Iterate live delegate pages rather than the entire (mostly local) heap.
	for (size_t d = 0; d < nr_delegate_slots; d++) {
		int slot = delegate_slots[d];
		struct page *page = heap_get_delegate_page(slot);
		int count = page->nr_vars;
		int width = delegate_stride();
		for (int i = 0; i < count; i += width) {
			if (page->values[i].i != source || (uint32_t)page->values[i + 2].i != seq)
				continue;
			page = delegate_append_captured(page, destination, page->values[i + 1].i,
				width == 4 ? page->values[i + 3].i : -1);
			heap_set_page(slot, page);
		}
	}
}

void heap_string_assign(int slot, struct string *string)
{
	if (unlikely(!string_index_valid(slot))) {
#ifdef DEBUG_HEAP
		VM_ERROR("Tried to assign string to non-string slot: %d", slot);
#else
		WARNING("Tried to assign string to non-string slot: %d", slot);
		return;
#endif
	}
	if (heap[slot].s) {
		free_string(heap[slot].s);
	}
	heap[slot].s = string_ref(string);
}

void heap_struct_assign(int lval, int rval)
{
	if (unlikely(lval < 0 || !page_index_valid(lval))) {
#ifdef DEBUG_HEAP
		if (lval == -1)
			VM_ERROR("Assignment to null-pointer");
		VM_ERROR("Invalid page index: %d", lval);
#else
		WARNING("heap_struct_assign to invalid page index: %d", lval);
		return;
#endif
	}
	if (lval == rval)
		return;
	if (unlikely(rval < 0 || !page_index_valid(rval))) {
#ifdef DEBUG_HEAP
		VM_ERROR("Invalid page index: %d", rval);
#else
		WARNING("heap_struct_assign from invalid page index: %d", rval);
		return;
#endif
	}
#ifdef DEBUG_HEAP
	if (unlikely(heap[lval].page && heap[lval].page->type != STRUCT_PAGE
			&& !(ain->version >= 11 && heap[lval].page->type == ARRAY_PAGE)))
		VM_ERROR("SR_ASSIGN to non-struct page");
	if (unlikely(heap[rval].page && heap[rval].page->type != STRUCT_PAGE
			&& !(ain->version >= 11 && heap[rval].page->type == ARRAY_PAGE)))
		VM_ERROR("SR_ASSIGN from non-struct page");
	if (unlikely(heap[lval].page && heap[rval].page && heap[lval].page->index != heap[rval].page->index))
		VM_ERROR("SR_ASSIGN with different struct types");
#endif
	heap_ref(rval);
	if (heap[lval].page) {
		delete_page(lval);
	}
	struct page *copied = page_index_valid(rval) ? copy_page(heap[rval].page) : NULL;
	if (page_index_valid(lval)) {
		heap_set_page(lval, copied);
	} else if (copied) {
		delete_page_vars(copied);
		free_page(copied);
	}
	heap_unref(rval);
}

int32_t heap_alloc_string(struct string *s)
{
	int slot = heap_alloc_slot(VM_STRING);
	heap[slot].s = s;
	return slot;
}

int32_t heap_alloc_page(struct page *page)
{
	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, page);
	return slot;
}

static void describe_page(struct page *page)
{
	if (!page) {
		sys_message("NULL_PAGE\n");
		return;
	}

	switch (page->type) {
	case GLOBAL_PAGE:
		sys_message("GLOBAL_PAGE\n");
		break;
	case LOCAL_PAGE:
		sys_message("LOCAL_PAGE: %s\n", display_sjis0(ain->functions[page->index].name));
		break;
	case STRUCT_PAGE:
		sys_message("STRUCT_PAGE: %s\n", display_sjis0(ain->structures[page->index].name));
		break;
	case ARRAY_PAGE:
		sys_message("ARRAY_PAGE: %s\n", display_sjis0(ain_strtype(ain, page->a_type, page->array.struct_type)));
		break;
	case DELEGATE_PAGE:
		// TODO: list function names
		sys_message("DELEGATE_PAGE\n");
		break;
	}
}

void heap_describe_slot(int slot)
{
	if (heap[slot].type == VM_STRING && heap[slot].s == &EMPTY_STRING)
		return;
#ifdef DEBUG_HEAP
	sys_message("[%d](%d)(%08X)[", slot, heap[slot].ref, heap[slot].alloc_addr);
	for (int i = 0; i < heap[slot].ref_nr && i < 16; i++) {
		if (i > 0)
			sys_message(",");
		sys_message("%08X", heap[slot].ref_addr[i]);
	}
	sys_message("][");
	for (int i = 0; i < heap[slot].deref_nr && i < 16; i++) {
		if (i > 0)
			sys_message(",");
		sys_message("%08X", heap[slot].deref_addr[i]);
	}
	sys_message("] = ");
#else
	sys_message("[%d](%d) = ", slot, heap[slot].ref);
#endif
	switch (heap[slot].type) {
	case VM_PAGE:
		describe_page(heap[slot].page);
		break;
	case VM_STRING:
		if (heap[slot].s) {
			sys_message("STRING: %s\n", display_sjis0(heap[slot].s->text));
		} else {
			sys_message("STRING: NULL\n");
		}
		break;
	default:
		sys_message("???\n");
		break;
	}
}
