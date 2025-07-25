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

// enable private VM interface
#define VM_PRIVATE

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "cJSON.h"

#include "system4.h"
#include "system4/file.h"
#include "system4/savefile.h"
#include "system4/string.h"

#include "savedata.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

/*
 * Save/load VM images as JSON.
 */

static const char * const page_type_strtab[] = {
	[GLOBAL_PAGE]   = "globals",
	[LOCAL_PAGE]    = "locals",
	[STRUCT_PAGE]   = "struct",
	[ARRAY_PAGE]    = "array",
	[DELEGATE_PAGE] = "delegate"
};

static enum page_type string_to_page_type(const char *str)
{
	if (!strcmp(str, "globals"))  return GLOBAL_PAGE;
	if (!strcmp(str, "locals"))   return LOCAL_PAGE;
	if (!strcmp(str, "struct"))   return STRUCT_PAGE;
	if (!strcmp(str, "array"))    return ARRAY_PAGE;
	if (!strcmp(str, "delegate")) return DELEGATE_PAGE;
	VM_ERROR("Invalid page type: %s", str);
}

static cJSON *value_to_json(union vm_value v)
{
	return cJSON_CreateNumber(v.i);
}

static int get_number(int i, void *data)
{
	return ((union vm_value*)data)[i].i;
}

static cJSON *resume_page_to_json(struct page *page)
{
	if (!page)
		return cJSON_CreateNull();

	cJSON *json = cJSON_CreateObject();
	cJSON_AddStringToObject(json, "type", page_type_strtab[page->type]);
	cJSON_AddNumberToObject(json, "subtype", page->index);
	if (page->type == ARRAY_PAGE) {
		cJSON_AddNumberToObject(json, "struct-type", page->array.struct_type);
		cJSON_AddNumberToObject(json, "rank", page->array.rank);
	}

	cJSON *values = cJSON_CreateIntArray_cb(page->nr_vars, get_number, page->values);

	cJSON_AddItemToObject(json, "values", values);
	return json;
}

static cJSON *heap_item_to_json(int i, possibly_unused void *_)
{
	if (!heap[i].ref)
		return NULL;

	cJSON *item = cJSON_CreateArray();
	cJSON_AddItemToArray(item, cJSON_CreateNumber(i));
	cJSON_AddItemToArray(item, cJSON_CreateNumber(heap[i].ref));
	switch (heap[i].type) {
	case VM_PAGE:
		cJSON_AddItemToArray(item, resume_page_to_json(heap[i].page));
		break;
	case VM_STRING:
		cJSON_AddItemToArray(item, cJSON_CreateString(heap[i].s->text));
		break;
	}
	if (ain->nr_delegates > 0) {
		cJSON_AddItemToArray(item, cJSON_CreateNumber(heap[i].seq));
	}
	return item;
}

static cJSON *heap_to_json(void)
{
	return cJSON_CreateArray_cb(heap_size, heap_item_to_json, NULL);
}

static cJSON *funcall_to_json(struct function_call *call)
{
	cJSON *json = cJSON_CreateObject();
	cJSON_AddNumberToObject(json, "function", call->fno);
	cJSON_AddNumberToObject(json, "return-address", call->return_address);
	cJSON_AddNumberToObject(json, "local-page", call->page_slot);
	cJSON_AddNumberToObject(json, "struct-page", call->struct_page);
	return json;
}

static cJSON *call_stack_to_json(void)
{
	cJSON *json = cJSON_CreateArray();
	for (int i = 0; i < call_stack_ptr; i++) {
		cJSON_AddItemToArray(json, funcall_to_json(&call_stack[i]));
	}
	return json;
}

static cJSON *stack_to_json(void)
{
	cJSON *json = cJSON_CreateArray();
	for (int i = 0; i < stack_ptr; i++) {
		cJSON_AddItemToArray(json, value_to_json(stack[i]));
	}
	return json;
}

static cJSON *vm_image_to_json(const char *key)
{
	cJSON *image = cJSON_CreateObject();
	cJSON_AddStringToObject(image, "key", key);
	cJSON_AddItemToObject(image, "heap", heap_to_json());
	cJSON_AddItemToObject(image, "call-stack", call_stack_to_json());
	cJSON_AddItemToObject(image, "stack", stack_to_json());
	cJSON_AddNumberToObject(image, "ip", instr_ptr);
	if (ain->nr_delegates > 0) {
		cJSON_AddNumberToObject(image, "next_seq", heap_next_seq);
	}
	return image;
}

static struct rsave_heap_frame *frame_page_to_rsave(struct page *page, int slot)
{
	struct rsave_heap_frame *o = xcalloc(1, sizeof(struct rsave_heap_frame) + page->nr_vars * sizeof(int32_t));
	o->ref = heap[slot].ref;
	o->seq = heap[slot].seq;
	struct ain_variable *vars;
	if (page->type == GLOBAL_PAGE) {
		o->tag = RSAVE_GLOBALS;
		o->func.id = -1;
		assert(page->nr_vars == ain->nr_globals);
		vars = ain->globals;
	} else {
		struct ain_function *f = &ain->functions[page->index];
		o->tag = RSAVE_LOCALS;
		o->func.name = strdup(f->name);
		assert(page->nr_vars == f->nr_vars);
		vars = f->vars;
		o->struct_ptr = page->local.struct_ptr;
	}
	o->nr_types = page->nr_vars;
	o->types = xcalloc(o->nr_types, sizeof(int32_t));
	for (int i = 0; i < o->nr_types; i++)
		o->types[i] = vars[i].type.data;
	o->nr_slots = page->nr_vars;
	for (int i = 0; i < o->nr_slots; i++)
		o->slots[i] = page->values[i].i;
	return o;
}

static struct rsave_heap_struct *struct_page_to_rsave(struct page *page, int slot)
{
	struct rsave_heap_struct *o = xcalloc(1, sizeof(struct rsave_heap_struct) + page->nr_vars * sizeof(int32_t));
	o->tag = RSAVE_STRUCT;
	o->ref = heap[slot].ref;
	o->seq = heap[slot].seq;
	struct ain_struct *s = &ain->structures[page->index];
	o->ctor.name = strdup(s->constructor >= 0 ? ain->functions[s->constructor].name : "");
	o->dtor.name = strdup(s->destructor >= 0 ? ain->functions[s->destructor].name : "");
	o->uk = 0;
	o->struct_type.name = strdup(s->name);
	o->nr_types = s->nr_members;
	o->types = xcalloc(o->nr_types, sizeof(int32_t));
	for (int i = 0; i < o->nr_types; i++)
		o->types[i] = s->members[i].type.data;
	o->nr_slots = page->nr_vars;
	for (int i = 0; i < o->nr_slots; i++)
		o->slots[i] = page->values[i].i;
	return o;
}

static struct rsave_heap_array *array_page_to_rsave(struct page *page, int slot)
{
	struct rsave_heap_array *o = xcalloc(1, sizeof(struct rsave_heap_array) + page->nr_vars * sizeof(int32_t));
	o->tag = RSAVE_ARRAY;
	o->ref = heap[slot].ref;
	o->seq = heap[slot].seq;
	o->rank_minus_1 = page->array.rank - 1;
	o->data_type = page->index;
	if (page->array.struct_type >= 0)
		o->struct_type.name = strdup(ain->structures[page->array.struct_type].name);
	else
		o->struct_type.name = strdup("");
	o->root_rank = page->array.rank;  // FIXME: this is incorrect for subarrays
	o->is_not_empty = page->nr_vars ? 1 : 0;
	o->nr_slots = page->nr_vars;
	for (int i = 0; i < o->nr_slots; i++)
		o->slots[i] = page->values[i].i;
	return o;
}

static struct rsave_heap_delegate *delegate_page_to_rsave(struct page *page, int slot)
{
	struct rsave_heap_delegate *o = xcalloc(1, sizeof(struct rsave_heap_delegate) + page->nr_vars * sizeof(int32_t));
	o->tag = RSAVE_DELEGATE;
	o->ref = heap[slot].ref;
	o->seq = heap[slot].seq;
	o->nr_slots = page->nr_vars;
	for (int i = 0; i < o->nr_slots; i++)
		o->slots[i] = page->values[i].i;
	return o;
}

static void *heap_item_to_rsave(int i)
{
	if (!heap[i].ref)
		return rsave_null;
	if (heap[i].type == VM_STRING) {
		int len = heap[i].s->size + 1;
		struct rsave_heap_string *s = xmalloc(sizeof(struct rsave_heap_string) + len);
		s->tag = RSAVE_STRING;
		s->ref = heap[i].ref;
		s->seq = heap[i].seq;
		s->uk = 0;
		s->len = len;
		memcpy(s->text, heap[i].s->text, len);
		return s;
	}
	struct page *page = heap[i].page;
	if (!page) {
		// Empty array.
		struct rsave_heap_array *o = xcalloc(1, sizeof(struct rsave_heap_array));
		o->tag = RSAVE_ARRAY;
		o->ref = heap[i].ref;
		o->seq = heap[i].seq;
		o->rank_minus_1 = -1;

		// FIXME: System40.exe populates them but we don't have the type
		// information of the array here.
		o->data_type = 0;
		o->struct_type.name = strdup("");

		o->root_rank = -1;
		o->is_not_empty = 0;
		o->nr_slots = 0;
		return o;
	}
	switch (page->type) {
	case GLOBAL_PAGE:
	case LOCAL_PAGE:
		return frame_page_to_rsave(page, i);
	case STRUCT_PAGE:
		return struct_page_to_rsave(page, i);
	case ARRAY_PAGE:
		return array_page_to_rsave(page, i);
	case DELEGATE_PAGE:
		return delegate_page_to_rsave(page, i);
	default:
		ERROR("unsupported type %d", page->type);
	}
}

static void save_heap_to_rsave(struct rsave *rs)
{
	rs->nr_heap_objs = heap_size;
	rs->heap = xcalloc(heap_size, sizeof(void*));
	for (size_t i = 0; i < heap_size; i++) {
		rs->heap[i] = heap_item_to_rsave(i);
	}
}

static void save_call_stack_to_rsave(struct rsave *rs)
{
	rs->nr_call_frames = call_stack_ptr + 1;
	rs->call_frames = xcalloc(rs->nr_call_frames, sizeof(struct rsave_call_frame));
	rs->call_frames[0].type = RSAVE_CALL_STACK_BOTTOM;
	rs->call_frames[0].local_ptr = -1;
	rs->nr_return_records = call_stack_ptr;
	rs->return_records = xcalloc(rs->nr_return_records + 1, sizeof(struct rsave_return_record));
	rs->return_records[0].return_addr = -1;

	struct ain_function *prev_func = NULL;
	for (int i = 0; i < call_stack_ptr; i++) {
		struct function_call *call = &call_stack[i];
		struct ain_function *func = &ain->functions[call->fno];
		if (call->struct_page >= 0) {
			rs->call_frames[i + 1].type = RSAVE_METHOD_CALL;
		} else if (!strcmp(func->name, "main")) {
			rs->call_frames[i + 1].type = RSAVE_ENTRY_POINT;
		} else {
			rs->call_frames[i + 1].type = RSAVE_FUNCTION_CALL;
		}
		rs->call_frames[i + 1].local_ptr = call->page_slot;
		rs->call_frames[i + 1].struct_ptr = call->struct_page;
		if (i > 0) {
			rs->return_records[i].return_addr = call->return_address;
			rs->return_records[i].local_addr = call->return_address - prev_func->address;
		}
		rs->return_records[i + 1].caller_func = strdup(func->name);
		rs->return_records[i + 1].crc = func->crc;
		prev_func = func;
	}
	rs->ip = rs->return_records[call_stack_ptr];
	rs->ip.return_addr = instr_ptr + 6;
	rs->ip.local_addr = instr_ptr - prev_func->address + 6;
}

static void save_stack_to_rsave(struct rsave *rs)
{
	// For compatibility with the AliceSoft's implementation, exclude two values
	// (the SYS_RESUME_SAVE arguments) at the top of the stack.
	rs->stack_size = stack_ptr - 2;

	rs->stack = xcalloc(rs->stack_size, sizeof(int32_t));
	for (int i = 0; i < rs->stack_size; i++) {
		rs->stack[i] = stack[i].i;
	}
}

static void save_func_names_to_rsave(struct rsave *rs)
{
	rs->nr_func_names = ain->nr_functions;
	rs->func_names = xcalloc(rs->nr_func_names, sizeof(char *));
	for (int i = 0; i < ain->nr_functions; i++) {
		rs->func_names[i] = strdup(ain->functions[i].name);
	}
}

static struct rsave *make_rsave(const char *key)
{
	struct rsave *save = xcalloc(1, sizeof(struct rsave));
	if (ain->nr_delegates > 0) {
		save->version = 9;
	} else {
		// FIXME: This is not always correct. Pastel Chime Continue is AIN v4
		//        but uses RSM v7.
		save->version = ain->version <= 4 ? 6 : 7;
	}
	save->key = strdup(key);
	return save;
}

static struct rsave *vm_image_to_rsave(const char *key)
{
	struct rsave *save = make_rsave(key);
	save->next_seq = heap_next_seq;
	save_heap_to_rsave(save);
	save_call_stack_to_rsave(save);
	save_stack_to_rsave(save);
	save_func_names_to_rsave(save);
	return save;
}

static int save_rsave_image(const char *key, const char *path)
{
	struct rsave *save = vm_image_to_rsave(key);
	if (!save)
		return 0;
	char *full_path = savedir_path(path);
	FILE *fp = file_open_utf8(full_path, "wb");
	if (!fp) {
		WARNING("Failed to open save file %s: %s", display_utf0(full_path), strerror(errno));
		free(full_path);
		rsave_free(save);
		return 0;
	}
	free(full_path);

	bool encrypt = true;
	int compression_level = 1;
	enum savefile_error error = rsave_write(save, fp, encrypt, compression_level);
	if (error != SAVEFILE_SUCCESS)
		WARNING("Failed to write save file: %s", savefile_strerror(error));
	fclose(fp);
	rsave_free(save);
	return 1;
}

static int save_json_image(const char *key, const char *path)
{
	cJSON *image = vm_image_to_json(key);
	int r = save_json(path, image);
	cJSON_Delete(image);
	return r;
}

int vm_save_image(const char *key, const char *path)
{
	switch (config.save_format) {
	case SAVE_FORMAT_RSM:
		return save_rsave_image(key, path);
	case SAVE_FORMAT_JSON:
		return save_json_image(key, path);
	}
	return 0;
}

#define _invalid_save_data(file, func, line, fmt, ...)	\
	_vm_error("*ERROR*(%s:%s:%d): " fmt "\n", file, func, line, ##__VA_ARGS__)
#define invalid_save_data(fmt, ...) \
	_invalid_save_data(__FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)

static const char *json_strtype(int type)
{
	switch (type & 0xFF) {
	case cJSON_False:
	case cJSON_True:
		return "boolean";
	case cJSON_NULL:   return "null";
	case cJSON_Number: return "number";
	case cJSON_String: return "string";
	case cJSON_Array:  return "array";
	case cJSON_Object: return "object";
	}
	return "unknown-type";
}

static cJSON *_type_check(const char *file, const char *func, int line, int type, cJSON *json)
{
	if (!json)
		_invalid_save_data(file, func, line, "Expected %s but got NULL", json_strtype(type));
	if (!(json->type & type))
		_invalid_save_data(file, func, line, "Expected %s but value is of type %s", json_strtype(type), json_strtype(json->type));
	return json;
}

#define type_check(type, json) _type_check(__FILE__, __func__, __LINE__, type, json)

static void load_json_page(int slot, cJSON *json)
{
	int struct_type = 0, rank = 0;

	// unpack
	cJSON *type    = type_check(cJSON_String, cJSON_GetObjectItem(json, "type"));
	cJSON *subtype = type_check(cJSON_Number, cJSON_GetObjectItem(json, "subtype"));
	cJSON *values  = type_check(cJSON_Array,  cJSON_GetObjectItem(json, "values"));

	enum page_type page_type = string_to_page_type(cJSON_GetStringValue(type));
	if (page_type == ARRAY_PAGE) {
		cJSON *_struct_type = type_check(cJSON_Number, cJSON_GetObjectItem(json, "struct-type"));
		cJSON *_rank        = type_check(cJSON_Number, cJSON_GetObjectItem(json, "rank"));
		struct_type = _struct_type->valueint;
		rank = _rank->valueint;
	}

	// allocate page
	struct page *page = alloc_page(page_type, subtype->valueint, cJSON_GetArraySize(values));
	page->array.struct_type = struct_type;
	page->array.rank = rank;

	// init page variables
	int i = 0;
	cJSON *item;
	cJSON_ArrayForEach(item, values) {
		page->values[i].i = item->valueint;
		i++;
	}

	heap[slot].page = page;
	heap[slot].type = VM_PAGE;
}

static void load_json_string(int slot, cJSON *json)
{
	const char *str = cJSON_GetStringValue(json);
	heap[slot].s = make_string(str, strlen(str));
	heap[slot].type = VM_STRING;
}

static void delete_heap(void)
{
	// free heap
	for (size_t i = 0; i < heap_size; i++) {
		if (!heap[i].ref)
			continue;
		switch (heap[i].type) {
		case VM_PAGE:
			if (heap[i].page)
				free_page(heap[i].page);
			break;
		case VM_STRING:
			free_string(heap[i].s);
			break;
		}
		heap[i].ref = 0;
		heap[i].seq = 0;
	}

	heap_free_ptr = 0;
	for (size_t i = 0; i < heap_size; i++) {
		heap_free_stack[i] = i;
	}
}

// Allocate a specific heap slot
// XXX: This only works while heap_free_stack[i]=i
//      After calling delete_heap, this can be called for INCREASING indices.
//      Out-of-order allocations would break the above assumption!
static void alloc_heap_slot(int slot)
{
	if ((size_t)slot >= heap_size) {
		size_t next_size = heap_size * 2;
		while ((size_t)slot >= next_size)
			next_size *= 2;
		heap_grow(next_size);
	}
	heap_free_stack[slot] = heap_free_stack[heap_free_ptr];
	heap_free_ptr++;
}

static void load_json_heap(cJSON *json)
{
	delete_heap();

	cJSON *item;
	cJSON_ArrayForEach(item, json) {
		type_check(cJSON_Array, item);
		if (cJSON_GetArraySize(item) < 3)
			invalid_save_data("Invalid heap data");

		int slot = type_check(cJSON_Number, cJSON_GetArrayItem(item, 0))->valueint;
		int ref  = type_check(cJSON_Number, cJSON_GetArrayItem(item, 1))->valueint;
		cJSON *value = cJSON_GetArrayItem(item, 2);
		int seq = ain->nr_delegates > 0
			? type_check(cJSON_Number, cJSON_GetArrayItem(item, 3))->valueint
			: slot;

		alloc_heap_slot(slot);
		heap[slot].ref = ref;
		heap[slot].seq = seq;

		if (cJSON_IsString(value)) {
			load_json_string(slot, value);
		} else if (cJSON_IsObject(value)) {
			load_json_page(slot, value);
		} else if (cJSON_IsNull(value)) {
			heap[slot].type = VM_PAGE;
			heap[slot].page = NULL;
		} else {
			invalid_save_data("Invalid heap data");
		}
	}
}

static int resolve_func_symbol(struct rsave_symbol *sym)
{
	if (!sym->name)
		return sym->id;
	return ain_get_function(ain, sym->name);
}

static int resolve_struct_symbol(struct rsave_symbol *sym)
{
	if (!sym->name)
		return sym->id;
	return ain_get_struct(ain, sym->name);
}

static void load_rsave_frame(int slot, struct rsave_heap_frame *f)
{
	struct page *page;
	int nr_vars;
	struct ain_variable *vars;
	if (f->tag == RSAVE_GLOBALS) {
		page = alloc_page(GLOBAL_PAGE, 0, f->nr_slots);
		nr_vars = ain->nr_globals;
		vars = ain->globals;
	} else {
		int func = resolve_func_symbol(&f->func);
		page = alloc_page(LOCAL_PAGE, func, f->nr_slots);
		nr_vars = ain->functions[func].nr_vars;
		vars = ain->functions[func].vars;
		page->local.struct_ptr = f->struct_ptr;
	}

	// type check
	if (nr_vars < f->nr_types) {
		invalid_save_data("call frame has too many variables");
	}
	for (int i = 0; i < f->nr_types; i++) {
		if (f->types[i] != vars[i].type.data) {
			invalid_save_data(
				"variable type mismatch. Expected %d, got %d",
				vars[i].type.data, f->types[i]);
		}
	}

	for (int i = 0; i < f->nr_slots; i++) {
		// TODO: update function pointers using rsave->func_names
		page->values[i].i = f->slots[i];
	}
	alloc_heap_slot(slot);
	heap[slot].ref = f->ref;
	heap[slot].seq = f->seq;
	heap[slot].type = VM_PAGE;
	heap[slot].page = page;
}

static void load_rsave_string(int slot, struct rsave_heap_string *s)
{
	alloc_heap_slot(slot);
	heap[slot].ref = s->ref;
	heap[slot].seq = s->seq;
	heap[slot].type = VM_STRING;
	heap[slot].s = make_string(s->text, s->len - 1);
}

static void load_rsave_array(int slot, struct rsave_heap_array *a)
{
	if (a->rank_minus_1 < 0) {
		alloc_heap_slot(slot);
		heap[slot].ref = a->ref;
		heap[slot].seq = a->seq;
		heap[slot].type = VM_PAGE;
		heap[slot].page = NULL;
		return;
	}
	struct page *page = alloc_page(ARRAY_PAGE, a->data_type, a->nr_slots);
	page->array.struct_type = resolve_struct_symbol(&a->struct_type);
	page->array.rank = a->rank_minus_1 + 1;
	for (int i = 0; i < a->nr_slots; i++)
		page->values[i].i = a->slots[i];

	alloc_heap_slot(slot);
	heap[slot].ref = a->ref;
	heap[slot].seq = a->seq;
	heap[slot].type = VM_PAGE;
	heap[slot].page = page;
}

static void load_rsave_struct(int slot, struct rsave_heap_struct *s)
{
	int struct_index = resolve_struct_symbol(&s->struct_type);
	struct page *page = alloc_page(STRUCT_PAGE, struct_index, s->nr_slots);

	// type check
	struct ain_struct *as = &ain->structures[struct_index];
	if (as->nr_members < s->nr_types) {
		invalid_save_data("Struct %s has too many members", as->name);
	}
	for (int i = 0; i < s->nr_types; i++) {
		if (s->types[i] != as->members[i].type.data) {
			invalid_save_data(
				"%s.%s: member type mismatch. Expected %d, got %d",
				as->name, as->members[i].name,
				as->members[i].type.data, s->types[i]);
		}
	}

	for (int i = 0; i < s->nr_slots; i++) {
		// TODO: update function pointers using rsave->func_names
		page->values[i].i = s->slots[i];
	}

	alloc_heap_slot(slot);
	heap[slot].ref = s->ref;
	heap[slot].seq = s->seq;
	heap[slot].type = VM_PAGE;
	heap[slot].page = page;
}

static void load_rsave_delegate(int slot, struct rsave_heap_delegate *d)
{
	struct page *page = alloc_page(DELEGATE_PAGE, 0, d->nr_slots);
	for (int i = 0; i < d->nr_slots; i++)
		page->values[i].i = d->slots[i];

	alloc_heap_slot(slot);
	heap[slot].ref = d->ref;
	heap[slot].seq = d->seq;
	heap[slot].type = VM_PAGE;
	heap[slot].page = page;
}

static void load_rsave_heap(struct rsave *save)
{
	delete_heap();

	for (int slot = 0; slot < save->nr_heap_objs; slot++) {
		enum rsave_heap_tag *tag = save->heap[slot];
		switch (*tag) {
		case RSAVE_GLOBALS:
		case RSAVE_LOCALS:   load_rsave_frame(slot, save->heap[slot]);    break;
		case RSAVE_STRING:   load_rsave_string(slot, save->heap[slot]);   break;
		case RSAVE_ARRAY:    load_rsave_array(slot, save->heap[slot]);    break;
		case RSAVE_STRUCT:   load_rsave_struct(slot, save->heap[slot]);   break;
		case RSAVE_DELEGATE: load_rsave_delegate(slot, save->heap[slot]); break;
		case RSAVE_NULL: break;
		}
	}
	heap_next_seq = save->next_seq;
}

static void load_json_call_stack(cJSON *json)
{
	call_stack_ptr = 0;
	cJSON *item;
	cJSON_ArrayForEach(item, json) {
		type_check(cJSON_Object, item);
		call_stack[call_stack_ptr++] = (struct function_call) {
			.fno            = type_check(cJSON_Number, cJSON_GetObjectItem(item, "function"))->valueint,
			.return_address = type_check(cJSON_Number, cJSON_GetObjectItem(item, "return-address"))->valueint,
			.page_slot      = type_check(cJSON_Number, cJSON_GetObjectItem(item, "local-page"))->valueint,
			.struct_page    = type_check(cJSON_Number, cJSON_GetObjectItem(item, "struct-page"))->valueint,
		};
	}
}

static void load_rsave_call_stack(struct rsave *save)
{
	call_stack_ptr = 0;

	if (save->nr_return_records == 0 || save->return_records[0].return_addr != -1)
		ERROR("invalid resume save");
	struct rsave_return_record *rr = save->return_records;
	if (save->nr_return_records == save->nr_call_frames)
		rr++;
	else if (save->nr_return_records != save->nr_call_frames - 1)
		ERROR("invalid resume save");

	int32_t return_address = -1;
	for (int i = 0; i < save->nr_call_frames; i++) {
		if (save->call_frames[i].type != RSAVE_CALL_STACK_BOTTOM) {
			int fno = ain_get_function(ain, rr->caller_func);
			if (fno < 0)
				VM_ERROR("Failed to load VM image: function '%s' not found", display_sjis0(rr->caller_func));
			if (ain->functions[fno].crc != rr->crc)
				VM_ERROR("Failed to load VM image: CRC mismatch for function '%s'", display_sjis0(rr->caller_func));
			call_stack[call_stack_ptr++] = (struct function_call) {
				.fno            = fno,
				.return_address = return_address,
				.page_slot      = save->call_frames[i].local_ptr,
				.struct_page    = save->call_frames[i].struct_ptr,
			};
			// Calculate return address from the function address and offset
			// to make it robust to ain changes.
			return_address = ain->functions[fno].address + rr->local_addr;
		}
		if (++rr == save->return_records + save->nr_return_records)
			rr = &save->ip;
	}
	// RSM records instruction pointer after the CALLSYS instruction, but
	// xsystem4 expects the address of the CALLSYS instruction, so -6.
	instr_ptr = return_address - 6;
}

static void load_json_stack(cJSON *json)
{
	stack_ptr = 0;

	cJSON *item;
	cJSON_ArrayForEach(item, json) {
		type_check(cJSON_Number, item);
		stack_push_value(vm_int(item->valueint));
	}
	// Pop the arguments of SYS_RESUME_SAVE.
	//heap_unref(stack_pop().i);
	//heap_unref(stack_pop().i);
	stack_pop();
	stack_pop();
}

static void load_rsave_stack(struct rsave *save)
{
	stack_ptr = 0;

	for (int i = 0; i < save->stack_size; i++) {
		stack_push_value(vm_int(save->stack[i]));
	}
}

static cJSON *read_json_image(const char *keyname, const char *path)
{
	char *full_path = savedir_path(path);
	char *save_file = file_read(full_path, NULL);
	if (!save_file) {
		free(save_file);
		free(full_path);
		return NULL;
	}

	cJSON *save = cJSON_Parse(save_file);
	cJSON *key = type_check(cJSON_String, cJSON_GetObjectItem(save, "key"));
	if (strcmp(keyname, cJSON_GetStringValue(key)))
		invalid_save_data("Key doesn't match");

	free(save_file);
	free(full_path);
	return type_check(cJSON_Object, save);
}

static void load_json_image(const char *key, const char *path)
{
	cJSON *save = read_json_image(key, path);
	if (!save) {
		VM_ERROR("Failed to read VM image: '%s'", display_sjis0(path));
	}
	cJSON *ip = type_check(cJSON_Number, cJSON_GetObjectItem(save, "ip"));
	load_json_heap(type_check(cJSON_Array, cJSON_GetObjectItem(save, "heap")));
	load_json_call_stack(type_check(cJSON_Array, cJSON_GetObjectItem(save, "call-stack")));
	load_json_stack(type_check(cJSON_Array, cJSON_GetObjectItem(save, "stack")));
	instr_ptr = ip->valueint;
	if (ain->nr_delegates > 0) {
		cJSON *next_seq = type_check(cJSON_Number, cJSON_GetObjectItem(save, "next_seq"));
		heap_next_seq = next_seq->valueint;
	} else {
		heap_next_seq = heap_size;
	}
	cJSON_Delete(save);
}

static enum savefile_error load_rsave_image(const char *key, const char *path)
{
	char *full_path = savedir_path(path);
	enum savefile_error error;
	struct rsave *save = rsave_read(full_path, RSAVE_READ_ALL, &error);
	free(full_path);
	if (error != SAVEFILE_SUCCESS)
		return error;
	if (strcmp(key, save->key))
		invalid_save_data("Key doesn't match");
	load_rsave_heap(save);
	load_rsave_call_stack(save);
	load_rsave_stack(save);
	rsave_free(save);
	return SAVEFILE_SUCCESS;
}

void vm_load_image(const char *key, const char *path)
{
	// First, try to read as a rsave.
	enum savefile_error error = load_rsave_image(key, path);
	switch (error) {
	case SAVEFILE_SUCCESS:
		return;
	case SAVEFILE_INVALID_SIGNATURE:
		// If not a System4 resume save file, read as a json.
		load_json_image(key, path);
		return;
	default:
		VM_ERROR("Failed to read VM image '%s': %s", display_sjis0(path), savefile_strerror(error));
	}
}

static struct page *load_json_image_comments(const char *key, const char *path, int *success)
{
	cJSON *save = read_json_image(key, path);
	if (!save) {
		*success = 0;
		return NULL;
	}
	cJSON *comments = cJSON_GetObjectItem(save, "comments");
	if (comments)
		type_check(cJSON_Array, comments);
	if (!comments || cJSON_GetArraySize(comments) == 0) {
		*success = 1;
		return NULL;
		//union vm_value dims = { .i = 0 };
		//return alloc_array(1, &dims, AIN_ARRAY_STRING, 0, false);
	}

	// read comments from JSON array into VM array
	union vm_value dims = { .i = cJSON_GetArraySize(comments) };
	struct page *array = alloc_array(1, &dims, AIN_ARRAY_STRING, 0, false);
	for (int i = 0; i < dims.i; i++) {
		int slot = heap_alloc_slot(VM_STRING);
		cJSON *jstr = type_check(cJSON_String, cJSON_GetArrayItem(comments, i));
		char *str = cJSON_GetStringValue(jstr);
		if (!str[0]) {
			heap[slot].s = string_ref(&EMPTY_STRING);
		} else {
			heap[slot].s = make_string(str, strlen(str));
		}
		array->values[i].i = slot;
	}

	*success = 1;
	cJSON_Delete(save);
	return array;
}

static struct page *load_rsave_image_comments(const char *key, const char *path, enum savefile_error *error)
{
	char *full_path = savedir_path(path);
	struct rsave *save = rsave_read(full_path, RSAVE_READ_COMMENTS, error);
	free(full_path);
	if (*error != SAVEFILE_SUCCESS)
		return NULL;
	if (strcmp(key, save->key))
		invalid_save_data("Key doesn't match");
	if (save->nr_comments == 0) {
		rsave_free(save);
		return NULL;
	}
	union vm_value dims = { .i = save->nr_comments };
	struct page *array = alloc_array(1, &dims, AIN_ARRAY_STRING, 0, false);
	for (int i = 0; i < dims.i; i++) {
		int slot = heap_alloc_slot(VM_STRING);
		if (!save->comments[i][0]) {
			heap[slot].s = string_ref(&EMPTY_STRING);
		} else {
			heap[slot].s = make_string(save->comments[i], strlen(save->comments[i]));
		}
		array->values[i].i = slot;
	}
	rsave_free(save);
	return array;
}

struct page *vm_load_image_comments(const char *key, const char *path, int *success)
{
	// First, try to read as a rsave.
	enum savefile_error error;
	struct page *page = load_rsave_image_comments(key, path, &error);
	if (error == SAVEFILE_SUCCESS) {
		*success = 1;
		return page;
	}

	if (error == SAVEFILE_INVALID_SIGNATURE) {
		// If not a System4 resume save file, read as a json.
		return load_json_image_comments(key, path, success);
	}
	*success = 0;
	return NULL;
}

static int write_rsave_image_comments(const char *key, const char *path, struct page *comments)
{
	char *full_path = savedir_path(path);
	enum savefile_error error;
	struct rsave *save = rsave_read(full_path, RSAVE_READ_ALL, &error);
	switch (error) {
	case SAVEFILE_SUCCESS:
		if (strcmp(key, save->key))
			invalid_save_data("Key doesn't match");
		break;
	case SAVEFILE_FILE_ERROR:
		save = make_rsave(key);
		save->comments_only = true;
		break;
	default:
		free(full_path);
		return 0;
	}

	// Comments were added in RSM v7, so if the save is older, upgrade it.
	if (save->version <= 6) {
		save->version = 7;
	}

	for (int i = 0; i < save->nr_comments; i++) {
		free(save->comments[i]);
	}
	free(save->comments);
	save->nr_comments = comments->nr_vars;
	save->comments = xcalloc(save->nr_comments, sizeof(char*));
	for (int i = 0; i < save->nr_comments; i++) {
		save->comments[i] = xstrdup(heap_get_string(comments->values[i].i)->text);
	}

	FILE *fp = file_open_utf8(full_path, "wb");
	if (!fp) {
		WARNING("Failed to open save file %s: %s", display_utf0(full_path), strerror(errno));
		free(full_path);
		rsave_free(save);
		return 0;
	}
	free(full_path);

	bool encrypt = true;
	int compression_level = 1;
	error = rsave_write(save, fp, encrypt, compression_level);
	if (error != SAVEFILE_SUCCESS)
		WARNING("Failed to write save file: %s", savefile_strerror(error));
	fclose(fp);
	rsave_free(save);
	return 1;
}

static int write_json_image_comments(const char *key, const char *path, struct page *comments)
{
	cJSON *save = read_json_image(key, path);
	if (!save) {
		// create blank save
		save = cJSON_CreateObject();
		cJSON_AddStringToObject(save, "key", key);
	}
	cJSON_DeleteItemFromObject(save, "comments");

	// TODO: check that comments is an array of strings
	cJSON *array = cJSON_CreateArray();
	for (int i = 0; i < comments->nr_vars; i++) {
		cJSON *s = cJSON_CreateString(heap_get_string(comments->values[i].i)->text);
		cJSON_AddItemToArray(array, s);
	}
	cJSON_AddItemToObject(save, "comments", array);

	save_json(path, save);
	cJSON_Delete(save);
	return 1;
}

int vm_write_image_comments(const char *key, const char *path, struct page *comments)
{
	switch (config.save_format) {
	case SAVE_FORMAT_RSM:
		return write_rsave_image_comments(key, path, comments);
	case SAVE_FORMAT_JSON:
		return write_json_image_comments(key, path, comments);
	}
	return 0;
}
