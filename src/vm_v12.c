/* AIN v12/v14 native runtime extensions, GPL-2.0-or-later. */
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "system4.h"
#include "system4/ain.h"
#include "system4/file.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "savedata.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "vm/v12.h"
#include "xsystem4.h"

static int type_stride(const struct ain_type *type)
{
	return ain_type_slot_width(type);
}

// Sys43's Shuffle uses its 521-word XOR generator, not the platform C RNG.
// Fixed seeds must produce the same permutations on Windows and ARM64.
struct shuffle_random { uint32_t state[521]; int index; };

static void shuffle_random_advance(struct shuffle_random *r)
{
	for (int i = 0; i < 32; i++) r->state[i] ^= r->state[i + 489];
	for (int i = 32; i < 521; i++) r->state[i] ^= r->state[i - 32];
	r->index = 0;
}

static void shuffle_random_init(struct shuffle_random *r, uint32_t seed)
{
	uint32_t word = 0;
	for (int i = 0; i < 17; i++) {
		for (int bit = 0; bit < 32; bit++) {
			seed = seed * UINT32_C(0x5d588b65) + 1;
			word = (word >> 1) | (seed & UINT32_C(0x80000000));
		}
		r->state[i] = word;
	}
	r->state[16] = (r->state[16] << 23) ^ (r->state[0] >> 9) ^ r->state[15];
	for (int i = 17; i < 521; i++)
		r->state[i] = (r->state[i - 17] << 23) ^ (r->state[i - 16] >> 9) ^ r->state[i - 1];
	for (int i = 0; i < 4; i++) shuffle_random_advance(r);
}

static uint32_t shuffle_random_next(struct shuffle_random *r)
{
	if (r->index == 521) shuffle_random_advance(r);
	return r->state[r->index++];
}

static struct ain_type element_type(struct page *p)
{
	if (!p || p->type != ARRAY_PAGE)
		VM_ERROR("AIN v12 array operation on a non-array");
	if (p->a_type == AIN_ARRAY)
		return p->array.element_type;
	return (struct ain_type) { .data = array_type(p->a_type), .struc = p->array.struct_type };
}

int v12_array_length(struct page *p)
{
	if (!p) return 0;
	struct ain_type type = element_type(p);
	return p->nr_vars / type_stride(&type);
}

struct page *v12_array_new(const struct ain_type *element, int count, bool construct)
{
	if (!element || count < 0 || count > INT_MAX / (3 * (int)sizeof(union vm_value)))
		VM_ERROR("Invalid AIN v12 array allocation: %d elements", count);
	int width = type_stride(element);
	struct page *page = alloc_page(ARRAY_PAGE, AIN_ARRAY, count * width);
	page->array.rank = 1;
	page->array.struct_type = element->struc;
	page->array.element_type = *element;
	for (int i = 0; i < count; i++) {
		if (element->data == AIN_STRUCT) {
			if (construct) {
				page->values[i * width].i = alloc_struct(element->struc);
				init_struct(element->struc, page->values[i * width].i);
			} else if (ain->version < 14) {
				page->values[i * width].i = alloc_struct(element->struc);
			} else {
				page->values[i * width].i = -1;
			}
		} else {
			variable_init_typed(&page->values[i * width], element, false, false);
		}
	}
	return page;
}

static void resize_array_mode(int slot, int count, bool construct)
{
	struct page *old = heap_get_page(slot);
	struct ain_type type = element_type(old);
	int width = type_stride(&type), old_count = v12_array_length(old);
	if (count < 0 || count > INT_MAX / (3 * (int)sizeof(union vm_value)))
		VM_ERROR("Invalid AIN v12 array size: %d", count);
	struct page *page = alloc_page(ARRAY_PAGE, AIN_ARRAY, count * width);
	page->array.rank = 1;
	page->array.struct_type = type.struc;
	page->array.element_type = type;
	int keep_elems = min(count, old_count);
	int keep = keep_elems * width;
	memcpy(page->values, old->values, keep * sizeof(*page->values));
	for (int i = keep_elems; i < old_count; i++)
		elem_value_fini(&old->values[i * width], &type, true);
	free_page(old);
	heap_set_page(slot, page);
	for (int i = keep_elems; i < count; i++) {
		if (type.data == AIN_STRUCT) {
			if (construct) {
				int object = alloc_struct(type.struc);
				page->values[i * width].i = object;
				init_struct(type.struc, object);
			} else if (ain->version < 14) {
				page->values[i * width].i = alloc_struct(type.struc);
			} else {
				page->values[i * width].i = -1;
			}
		} else {
			variable_init_typed(&page->values[i * width], &type, false, false);
		}
	}
}

static void resize_array(int slot, int count)
{
	resize_array_mode(slot, count, true);
}

static void alloc_dimensions(int slot, const int *dimensions, int remaining)
{
	int dim0 = max(dimensions[0], 0);
	resize_array(slot, 0);
	resize_array(slot, dim0);
	if (remaining < 2 || dimensions[1] < 0) return;
	if (element_type(heap_get_page(slot)).data != AIN_ARRAY)
		VM_ERROR("Too many dimensions in Array.Alloc");
	for (int i = 0; i < dim0; i++)
		alloc_dimensions(heap_get_page(slot)->values[i].i, dimensions + 1, remaining - 1);
}

static void set_element(int slot, int index, const union vm_value *value)
{
	struct page *p = heap_get_page(slot);
	struct ain_type type = element_type(p);
	int width = type_stride(&type);
	if (index < 0 || index >= v12_array_length(p))
		VM_ERROR("Array index out of bounds: %d", index);
	union vm_value copy[3] = {0};
	elem_value_copy(copy, value, &type);
	elem_value_fini(&p->values[index * width], &type, true);
	p = heap_get_page(slot);
	memcpy(p->values + index * width, copy, width * sizeof(*copy));
}

static void erase_elements_range(int slot, int index, int count)
{
	struct page *p = heap_get_page(slot);
	struct ain_type type = element_type(p);
	int width = type_stride(&type), size = v12_array_length(p);
	if (index < 0 || index >= size || count <= 0)
		return;
	if (count > size - index)
		count = size - index;
	for (int k = 0; k < count; k++) {
		p = heap_get_page(slot);
		elem_value_fini(&p->values[(index + k) * width], &type, true);
	}
	p = heap_get_page(slot);
	size = v12_array_length(p);
	if (index + count > size)
		count = max(0, size - index);
	memmove(p->values + index * width, p->values + (index + count) * width,
		(size - index - count) * width * sizeof(*p->values));
	p->nr_vars -= count * width;
}

static void erase_element(int slot, int index)
{
	erase_elements_range(slot, index, 1);
}

static void push_element(int slot, int index, const struct ain_type *formal)
{
	struct page *p = heap_get_page(slot);
	struct ain_type type = element_type(p);
	int pos = index * type_stride(&type);
	if (index < 0 || index >= v12_array_length(p))
		VM_ERROR("Array element index out of bounds: %d", index);
	if (formal->data == AIN_WRAP) {
		if (!formal->array_type) VM_ERROR("Missing borrowed element type");
		switch (formal->array_type->data) {
		case AIN_INT: case AIN_FLOAT: case AIN_BOOL: case AIN_LONG_INT:
		case AIN_ENUM: case AIN_ENUM2: case AIN_OPTION:
			stack_push(slot); stack_push(pos); return;
		case AIN_IFACE: case AIN_IFACE_WRAP:
			stack_push(p->values[pos]); stack_push(p->values[pos + 1]); return;
		default:
			stack_push(p->values[pos]); return;
		}
	}
	enum ain_data_type formal_type = formal->data;
	switch (formal_type) {
	case AIN_REF_INT: case AIN_REF_FLOAT: case AIN_REF_BOOL:
	case AIN_REF_LONG_INT: case AIN_REF_ENUM: case AIN_REF_OPTION:
		stack_push(slot);
		stack_push(pos);
		break;
	case AIN_IFACE: case AIN_IFACE_WRAP:
		stack_push(p->values[pos]);
		stack_push(p->values[pos + 1]);
		break;
	case AIN_OPTION: {
		int w = ain_type_slot_width(formal);
		union vm_value vals[3] = {0};
		for (int k = 0; k < w; k++)
			vals[k] = p->values[pos + k];
		if (vals[w - 1].i == 0 && formal->array_type && ain_option_inner_is_value_heap(formal->array_type))
			vals[0] = vm_copy(vals[0], formal->array_type->data);
		for (int k = 0; k < w; k++)
			stack_push(vals[k]);
		break;
	}
	case AIN_STRUCT: case AIN_STRING: case AIN_ARRAY: case AIN_DELEGATE:
		stack_push(vm_copy(p->values[pos], formal_type));
		break;
	default:
		stack_push(p->values[pos]);
		break;
	}
}

static int predicate(int slot, int index, union vm_value *callback)
{
	int fn = callback[1].i;
	if (fn < 0 || fn >= ain->nr_functions || !ain->functions[fn].nr_args)
		VM_ERROR("Invalid array predicate: %d", fn);
	push_element(slot, index, &ain->functions[fn].vars[0].type);
	vm_call(fn, callback[0].i);
	return stack_pop().i;
}

static bool binary_predicate2(int left_slot, int left, int right_slot, int right, union vm_value *callback)
{
	int fn = callback[1].i;
	if (fn < 0 || fn >= ain->nr_functions || ain->functions[fn].nr_args < 2)
		VM_ERROR("Invalid two-argument array predicate: %d", fn);
	struct ain_function *f = &ain->functions[fn];
	int second = 1;
	while (second < f->nr_args && f->vars[second].type.data == AIN_VOID) second++;
	if (second >= f->nr_args) VM_ERROR("Array predicate has no second argument");
	push_element(left_slot, left, &f->vars[0].type);
	push_element(right_slot, right, &f->vars[second].type);
	vm_call(fn, callback[0].i);
	return !!stack_pop().i;
}

static bool binary_predicate(int slot, int left, int right, union vm_value *callback)
{
	return binary_predicate2(slot, left, slot, right, callback);
}

static bool is_string_elem_type(const struct ain_type *type)
{
	return type->data == AIN_STRING
		|| (type->data == AIN_WRAP && type->array_type && type->array_type->data == AIN_STRING);
}

static int compare_element(int slot, int index, union vm_value *value)
{
	struct page *p = heap_get_page(slot);
	struct ain_type type = element_type(p);
	int w = type_stride(&type);
	union vm_value v = p->values[index * w];
	if (type.data == AIN_OPTION) {
		bool lhs_none = (p->values[index * w + w - 1].i != 0);
		bool rhs_none = (value[w - 1].i != 0);
		if (lhs_none != rhs_none)
			return lhs_none ? -1 : 1;
		if (lhs_none || !type.array_type)
			return 0;
		if (is_string_elem_type(type.array_type)) {
			const char *lhs = string_index_valid(v.i) ? heap_get_string(v.i)->text : "";
			const char *rhs = string_index_valid(value[0].i) ? heap_get_string(value[0].i)->text : "";
			return strcmp(lhs, rhs);
		}
		if (type.array_type->data == AIN_FLOAT)
			return (v.f > value[0].f) - (v.f < value[0].f);
		return (v.i > value[0].i) - (v.i < value[0].i);
	}
	if (is_string_elem_type(&type)) {
		const char *lhs = string_index_valid(v.i) ? heap_get_string(v.i)->text : "";
		const char *rhs = string_index_valid(value[0].i) ? heap_get_string(value[0].i)->text : "";
		return strcmp(lhs, rhs);
	}
	if (type.data == AIN_FLOAT)
		return (v.f > value[0].f) - (v.f < value[0].f);
	return (v.i > value[0].i) - (v.i < value[0].i);
}

static void insert_element(int slot, int index, union vm_value *value)
{
	struct page *p = heap_get_page(slot);
	struct ain_type type = element_type(p);
	int width = type_stride(&type), size = v12_array_length(p);
	if (unlikely(index < 0 || index > size)) {
		static unsigned int warn_count = 0;
		if (warn_count++ < 64)
			WARNING("Array.Insert index out of bounds: %d (size=%d)", index, size);
		index = index < 0 ? 0 : size;
	}
	union vm_value copy[3] = {0};
	elem_value_copy(copy, value, &type);
	resize_array_mode(slot, size + 1, false);
	p = heap_get_page(slot);
	elem_value_fini(&p->values[size * width], &type, true);
	p = heap_get_page(slot);
	memmove(p->values + (index + 1) * width, p->values + index * width,
		(size - index) * width * sizeof(*p->values));
	memcpy(p->values + index * width, copy, width * sizeof(*copy));
}

static bool equal_element(int slot, int index, union vm_value *value)
{
	struct page *p = heap_get_page(slot);
	struct ain_type type = element_type(p);
	int w = type_stride(&type);
	int pos = index * w;
	if (type.data == AIN_OPTION) {
		bool lhs_none = (p->values[pos + w - 1].i != 0);
		bool rhs_none = (value[w - 1].i != 0);
		if (lhs_none != rhs_none)
			return false;
		if (lhs_none || !type.array_type)
			return true;
		if (is_string_elem_type(type.array_type)) {
			const char *lhs = string_index_valid(p->values[pos].i) ? heap_get_string(p->values[pos].i)->text : "";
			const char *rhs = string_index_valid(value[0].i) ? heap_get_string(value[0].i)->text : "";
			return !strcmp(lhs, rhs);
		}
		if (type.array_type->data == AIN_FLOAT)
			return p->values[pos].f == value[0].f;
		return p->values[pos].i == value[0].i;
	}
	if (is_string_elem_type(&type)) {
		const char *lhs = string_index_valid(p->values[pos].i) ? heap_get_string(p->values[pos].i)->text : "";
		const char *rhs = string_index_valid(value[0].i) ? heap_get_string(value[0].i)->text : "";
		return !strcmp(lhs, rhs);
	}
	if (type.data == AIN_FLOAT)
		return p->values[pos].f == value[0].f;
	return p->values[pos].i == value[0].i;
}

static void sort_array(int slot, union vm_value *callback, bool descending)
{
	struct page *p = heap_get_page(slot);
	struct ain_type type = element_type(p);
	int count = v12_array_length(p), width = type_stride(&type);
	int *order = xmalloc(max(count, 1) * sizeof(int));
	int *scratch = xmalloc(max(count, 1) * sizeof(int));
	for (int i = 0; i < count; i++) order[i] = i;
	// Sort indices so callback arguments continue to refer to stable elements.
	for (int run = 1; run < count; run *= 2) {
		for (int start = 0; start < count; start += 2 * run) {
			int middle = min(start + run, count), end = min(start + 2 * run, count);
			int left = start, right = middle;
			for (int dest = start; dest < end; dest++) {
				bool use_right = left == middle;
				if (left < middle && right < end) {
					p = heap_get_page(slot);
					use_right = callback
						? binary_predicate(slot, order[right], order[left], callback)
						: (descending
							? compare_element(slot, order[right], p->values + order[left] * width) > 0
							: compare_element(slot, order[right], p->values + order[left] * width) < 0);
				}
				scratch[dest] = use_right ? order[right++] : order[left++];
			}
		}
		int *tmp = order; order = scratch; scratch = tmp;
	}
	p = heap_get_page(slot);
	if (v12_array_length(p) != count) VM_ERROR("Array size changed inside sort comparator");
	union vm_value *values = xmalloc(max(p->nr_vars, 1) * sizeof(*values));
	for (int i = 0; i < count; i++)
		memcpy(values + i * width, p->values + order[i] * width, width * sizeof(*values));
	memcpy(p->values, values, p->nr_vars * sizeof(*values));
	free(values);
	free(order);
	free(scratch);
}

static bool elem_ref_is_two_slot(const struct ain_type *type)
{
	switch (type->data) {
	case AIN_INT: case AIN_FLOAT: case AIN_BOOL: case AIN_LONG_INT:
	case AIN_ENUM: case AIN_ENUM2:
	case AIN_IFACE: case AIN_IFACE_WRAP:
	case AIN_OPTION: case AIN_REF_OPTION:
		return true;
	case AIN_WRAP:
		return ain_type_slot_width(type) == 2;
	default:
		return false;
	}
}

static void return_element(int slot, int index)
{
	struct page *p = heap_get_page(slot);
	struct ain_type type = element_type(p);
	if (index < 0 || index >= v12_array_length(p)) {
		// At/First/Last/Min/Max return an optional reference on a missing item.
		stack_push(-1);
		if (elem_ref_is_two_slot(&type))
			stack_push(0);
		return;
	}
	int pos = index * type_stride(&type);
	switch (type.data) {
	case AIN_INT: case AIN_FLOAT: case AIN_BOOL: case AIN_LONG_INT:
	case AIN_ENUM: case AIN_ENUM2:
	case AIN_OPTION:
		heap_ref(slot);
		stack_push(slot);
		stack_push(pos);
		break;
	case AIN_IFACE: case AIN_IFACE_WRAP:
		heap_ref(p->values[pos].i);
		stack_push(p->values[pos]);
		stack_push(p->values[pos + 1]);
		break;
	case AIN_WRAP:
		if (type.array_type && (type.array_type->data == AIN_IFACE || type.array_type->data == AIN_IFACE_WRAP)) {
			heap_ref(p->values[pos].i);
			stack_push(p->values[pos]);
			stack_push(p->values[pos + 1]);
		} else if (ain_type_slot_width(&type) == 2) {
			heap_ref(p->values[pos].i);
			stack_push(p->values[pos]);
			stack_push(p->values[pos + 1]);
		} else {
			heap_ref(p->values[pos].i);
			stack_push(p->values[pos]);
		}
		break;
	default:
		heap_ref(p->values[pos].i);
		stack_push(p->values[pos]);
		break;
	}
}

static int array_hll_param_width(int parameter)
{
	if (ain->version >= 14) {
		int hi = (parameter >> 16) & 0xffff;
		int lo = parameter & 0xffff;
		if (hi >= 2)
			return lo == 3 ? 3 : 2;
		return lo == 3 || (hi == 1 && lo == 1) ? 2 : 1;
	}
	return parameter == AIN_IFACE ? 2 : 1;
}

static bool array_hll_ref_ret_is_two_slot(int parameter)
{
	if (ain->version >= 14) {
		int hi = (parameter >> 16) & 0xffff;
		int lo = parameter & 0xffff;
		return (hi >= 2) || (lo == 1) || (lo == 3);
	}
	return parameter == AIN_INT || parameter == AIN_FLOAT || parameter == AIN_BOOL
		|| parameter == AIN_ENUM || parameter == AIN_IFACE;
}

static void cleanup_array_value_args(struct ain_hll_function *f, int parameter, union vm_value args[8][3])
{
	// Value arguments are compiler-created temporaries. Reference arguments
	// and interface pairs are borrowed for the duration of the call.
	for (int i = 1; i < f->nr_arguments; i++) {
		if (f->arguments[i].type.data != AIN_HLL_PARAM) continue;
		if (ain->version >= 14) {
			int hi = (parameter >> 16) & 0xffff;
			int lo = parameter & 0xffff;
			if (hi == 0 && lo == 2) {
				if (args[i][0].i >= 0)
					heap_unref(args[i][0].i);
			} else if (hi == 2 && lo == 2) {
				if (args[i][1].i == 0 && args[i][0].i >= 0)
					heap_unref(args[i][0].i);
			}
		} else {
			if (parameter == AIN_STRING || parameter == AIN_STRUCT
					|| parameter == AIN_ARRAY || parameter == AIN_DELEGATE)
				variable_fini(args[i][0], parameter, true);
		}
	}
}

static bool array_call_invalid_slot(struct ain_hll_function *f, int parameter, int slot, union vm_value args[8][3])
{
	static unsigned int warn_count = 0;
	if (!(ain->version >= 14 && slot == -1) && warn_count++ < 64)
		WARNING("Array.%s on invalid array slot: %d", f->name, slot);
	const char *name = f->name;
	if (!strcmp(name, "Numof") || !strcmp(name, "Count") || !strcmp(name, "Any")
			|| !strcmp(name, "IsExist") || !strcmp(name, "Equals")
			|| !strcmp(name, "Erase") || !strcmp(name, "EraseAll")
			|| !strcmp(name, "Copy") || !strcmp(name, "Fill")
			|| !strcmp(name, "LowerBound") || !strcmp(name, "UpperBound")) {
		stack_push(0);
	} else if (!strcmp(name, "Empty") || !strcmp(name, "All")) {
		stack_push(1);
	} else if (!strcmp(name, "Find") || !strcmp(name, "FindLast") || !strcmp(name, "BinarySearch")
			|| !strcmp(name, "Sort") || !strcmp(name, "QuickSort")
			|| !strcmp(name, "AscSort") || !strcmp(name, "DescSort")
			|| !strcmp(name, "Remain")) {
		stack_push(-1);
	} else if (!strcmp(name, "ShallowCopy") || !strcmp(name, "Where")
			|| !strcmp(name, "SYSTEMONLY_GetStructPageList")) {
		struct page *empty = alloc_page(ARRAY_PAGE, AIN_ARRAY, 0);
		empty->array.rank = 1;
		empty->array.struct_type = -1;
		enum ain_data_type edata = AIN_INT;
		if (ain->version < 14 && strcmp(name, "SYSTEMONLY_GetStructPageList") != 0) {
			edata = (parameter == AIN_STRUCT || parameter == AIN_REF_STRUCT)
				? AIN_REF_STRUCT : (parameter >= 0 ? parameter : AIN_INT);
		}
		empty->array.element_type = (struct ain_type){ .data = edata, .struc = -1 };
		stack_push(heap_alloc_page(empty));
	} else if (!strcmp(name, "At") || !strcmp(name, "First") || !strcmp(name, "Last")
			|| !strcmp(name, "Min") || !strcmp(name, "Max") || !strcmp(name, "EmplaceBack")) {
		stack_push(-1);
		if (array_hll_ref_ret_is_two_slot(parameter))
			stack_push(0);
	}
	cleanup_array_value_args(f, parameter, args);
	return true;
}

static inline bool is_hll_func_type(enum ain_data_type type)
{
	return type == AIN_HLL_FUNC || type == AIN_HLL_FUNC_71;
}

static bool array_call(struct ain_hll_function *f, int parameter)
{
	union vm_value args[8][3] = {0};
	if (f->nr_arguments > 8) VM_ERROR("Too many Array arguments");
	for (int i = f->nr_arguments - 1; i >= 0; i--) {
		int type = f->arguments[i].type.data;
		int width = 1;
		if (is_hll_func_type(type))
			width = 2;
		else if (type == AIN_HLL_PARAM)
			width = array_hll_param_width(parameter);
		else
			width = ain_type_slot_width(&f->arguments[i].type);
		for (int j = width - 1; j >= 0; j--) args[i][j] = stack_pop();
	}
	int slot = args[0][0].i;
	if (unlikely(!page_index_valid(slot) || (heap[slot].page && heap[slot].page->type != ARRAY_PAGE)))
		return array_call_invalid_slot(f, parameter, slot, args);
	struct page *p = heap_get_page(slot);
	if (unlikely(!p))
		return array_call_invalid_slot(f, parameter, slot, args);
	int size = v12_array_length(p);
	const char *name = f->name;
	if (!strcmp(name, "Alloc")) {
		if (f->nr_arguments < 2 || f->nr_arguments > 8)
			VM_ERROR("Invalid Array.Alloc argument count: %d", f->nr_arguments);
		int dimensions[7];
		for (int i = 1; i < f->nr_arguments; i++) dimensions[i - 1] = args[i][0].i;
		alloc_dimensions(slot, dimensions, f->nr_arguments - 1);
	} else if (!strcmp(name, "Realloc")) {
		int new_size = max(args[1][0].i, 0);
		resize_array(slot, new_size);
		if (f->nr_arguments == 3)
			for (int i = size; i < new_size; i++) set_element(slot, i, args[2]);
	} else if (!strcmp(name, "Free") || !strcmp(name, "Clear")) {
		resize_array(slot, 0);
	} else if (!strcmp(name, "Numof") || !strcmp(name, "Count")
			|| !strcmp(name, "Empty") || !strcmp(name, "Any") || !strcmp(name, "All")) {
		int count = size;
		if (f->nr_arguments == 2) {
			count = 0;
			for (int i = 0; i < size; i++) count += !!predicate(slot, i, args[1]);
		}
		if (!strcmp(name, "Numof") || !strcmp(name, "Count")) stack_push(count);
		else if (!strcmp(name, "Empty")) stack_push(count == 0);
		else if (!strcmp(name, "Any")) stack_push(count > 0);
		else stack_push(count == size);
	} else if (!strcmp(name, "PushBack") || !strcmp(name, "Add") || !strcmp(name, "EmplaceBack")) {
		bool is_emplace = !strcmp(name, "EmplaceBack");
		resize_array_mode(slot, size + 1, is_emplace);
		if (!is_emplace) {
			set_element(slot, size, args[1]);
		} else {
			p = heap_get_page(slot);
			struct ain_type type = element_type(p);
			int width = type_stride(&type);
			if (type.data == AIN_WRAP && type.array_type && type.array_type->data == AIN_STRUCT) {
				create_struct(type.array_type->struc, &p->values[size * width]);
			}
			return_element(slot, size);
		}
	} else if (!strcmp(name, "Insert")) {
		insert_element(slot, args[1][0].i, args[2]);
	} else if (!strcmp(name, "PopBack")) {
		if (size) resize_array(slot, size - 1);
	} else if (!strcmp(name, "Duplicate")) {
		heap_struct_assign(slot, args[1][0].i);
	} else if (!strcmp(name, "Concat") || !strcmp(name, "AddRange")) {
		int source = args[1][0].i;
		if (unlikely(!page_index_valid(source) || !heap[source].page || heap[source].page->type != ARRAY_PAGE)) {
			WARNING("Array.%s from invalid array slot: %d", name, source);
			cleanup_array_value_args(f, parameter, args);
			return true;
		}
		struct page *src = heap_get_page(source);
		struct ain_type type = element_type(p), src_type = element_type(src);
		int count = v12_array_length(src), width = type_stride(&type);
		if (count == 0) {
			cleanup_array_value_args(f, parameter, args);
			return true;
		}
		if (type_stride(&src_type) != width)
			VM_ERROR("Array.%s element widths differ", name);
		if (count > INT_MAX / (3 * (int)sizeof(union vm_value)) - size)
			VM_ERROR("Array.%s result is too large", name);
		struct page *joined = alloc_page(ARRAY_PAGE, AIN_ARRAY, (size + count) * width);
		joined->array.rank = 1;
		joined->array.struct_type = type.struc;
		joined->array.element_type = type;
		// Copy before replacing the destination page, including for self-concat.
		for (int i = 0; i < count; i++) {
			src = heap_get_page(source);
			elem_value_copy(&joined->values[(size + i) * width], &src->values[i * width], &type);
		}
		p = heap_get_page(slot);
		memcpy(joined->values, p->values, size * width * sizeof(*p->values));
		free_page(p);
		heap_set_page(slot, joined);
	} else if (!strcmp(name, "Copy")) {
		int dst_at = 0, source = -1, src_at = 0, count = -1;
		if (f->nr_arguments == 2) {
			source = args[1][0].i;
		} else if (f->nr_arguments == 3) {
			dst_at = args[1][0].i;
			source = args[2][0].i;
		} else if (f->nr_arguments == 4) {
			source = args[1][0].i;
			src_at = args[2][0].i;
			count = args[3][0].i;
		} else if (f->nr_arguments == 5) {
			dst_at = args[1][0].i;
			source = args[2][0].i;
			src_at = args[3][0].i;
			count = args[4][0].i;
		} else {
			VM_ERROR("Unimplemented Array.Copy overload with %d arguments", f->nr_arguments);
		}
		if (unlikely(!page_index_valid(source) || !heap[source].page || heap[source].page->type != ARRAY_PAGE)) {
			WARNING("Array.Copy from invalid array slot: %d", source);
			stack_push(0);
			cleanup_array_value_args(f, parameter, args);
			return true;
		}
		struct page *src = heap_get_page(source);
		struct ain_type type = element_type(p), src_type = element_type(src);
		int src_size = v12_array_length(src), width = type_stride(&type);
		if (f->nr_arguments <= 3) {
			if (dst_at < 0 || dst_at > size) {
				stack_push(0);
				cleanup_array_value_args(f, parameter, args);
				return true;
			}
			count = min(size - dst_at, src_size);
		} else if (ain->version >= 14 && dst_at >= 0 && dst_at <= size && src_at >= 0 && src_at <= src_size && count > 0) {
			count = min(count, min(size - dst_at, src_size - src_at));
		}
		if (count <= 0 || dst_at < 0 || dst_at > size || count > size - dst_at
				|| src_at < 0 || src_at > src_size || count > src_size - src_at) {
			if (count != 0)
				WARNING("Array.Copy range out of bounds: dst=%d/%d src=%d/%d count=%d",
					dst_at, size, src_at, src_size, count);
			stack_push(0);
			cleanup_array_value_args(f, parameter, args);
			return true;
		}
		if (type_stride(&src_type) != width)
			VM_ERROR("Array.Copy element types differ");
		union vm_value *copy = xcalloc(count * width, sizeof(*copy));
		for (int i = 0; i < count; i++) {
			src = heap_get_page(source);
			elem_value_copy(&copy[i * width], &src->values[(src_at + i) * width], &type);
		}
		for (int i = 0; i < count; i++) {
			p = heap_get_page(slot);
			elem_value_fini(&p->values[(dst_at + i) * width], &type, true);
			p = heap_get_page(slot);
			if (dst_at + i >= v12_array_length(p))
				VM_ERROR("Array.Copy destination changed during assignment");
			memcpy(p->values + (dst_at + i) * width, copy + i * width, width * sizeof(*copy));
		}
		free(copy);
		stack_push(count);
	} else if (!strcmp(name, "ShallowCopy")) {
		struct ain_type src_type = element_type(p);
		struct ain_type dst_type = p->array.element_type;
		if (ain->version >= 14) {
			if (dst_type.data == AIN_STRUCT)
				dst_type.data = AIN_REF_STRUCT;
		} else {
			if (dst_type.data == AIN_STRUCT
					|| parameter == AIN_STRUCT || parameter == AIN_REF_STRUCT)
				dst_type.data = AIN_REF_STRUCT;
			else if (parameter >= 0)
				dst_type.data = parameter;
		}
		int src_width = type_stride(&src_type);
		int dst_width = type_stride(&dst_type);
		struct page *copy = alloc_page(ARRAY_PAGE, AIN_ARRAY, size * dst_width);
		copy->array = p->array;
		copy->array.element_type = dst_type;
		for (int i = 0; i < size; i++) {
			p = heap_get_page(slot);
			elem_value_copy(&copy->values[i * dst_width], &p->values[i * src_width], &dst_type);
		}
		stack_push(heap_alloc_page(copy));
	} else if (!strcmp(name, "Where")) {
		struct ain_type type = element_type(p);
		int width = type_stride(&type), count = 0;
		struct page *result = alloc_page(ARRAY_PAGE, AIN_ARRAY, size * width);
		result->array = p->array;
		for (int i = 0; i < size; i++) {
			if (!predicate(slot, i, args[1])) continue;
			p = heap_get_page(slot);
			elem_value_copy(&result->values[count * width], &p->values[i * width], &type);
			count++;
		}
		result->nr_vars = count * width;
		stack_push(heap_alloc_page(result));
	} else if (!strcmp(name, "Remain")) {
		for (int i = 0; i < size; ) {
			if (!predicate(slot, i, args[1])) {
				erase_element(slot, i);
				size--;
			} else {
				i++;
			}
		}
		heap_ref(slot);
		stack_push(slot);
	} else if (!strcmp(name, "Equals")) {
		int src_slot = args[1][0].i;
		if (!page_index_valid(src_slot) || !heap[src_slot].page || heap[src_slot].page->type != ARRAY_PAGE) {
			stack_push(0);
		} else {
			struct page *src = heap_get_page(src_slot);
			int src_size = v12_array_length(src);
			struct ain_type type = element_type(p), src_type = element_type(src);
			int width = type_stride(&type);
			if (src_size != size || type_stride(&src_type) != width) {
				stack_push(0);
			} else {
				bool eq = true;
				for (int i = 0; i < size; i++) {
					bool match = f->nr_arguments == 3
						? binary_predicate2(slot, i, src_slot, i, args[2])
						: equal_element(slot, i, heap_get_page(src_slot)->values + i * width);
					if (!match) {
						eq = false;
						break;
					}
				}
				stack_push(eq ? 1 : 0);
			}
		}
	} else if (!strcmp(name, "At") || !strcmp(name, "First") || !strcmp(name, "Last")) {
		int index = !strcmp(name, "At") ? args[1][0].i : !strcmp(name, "Last") ? size - 1 : 0;
		if (f->nr_arguments == 2 && is_hll_func_type(f->arguments[1].type.data)) {
			int step = !strcmp(name, "Last") ? -1 : 1;
			for (; index >= 0 && index < size; index += step)
				if (predicate(slot, index, args[1])) break;
		}
		return_element(slot, index);
	} else if (!strcmp(name, "Min") || !strcmp(name, "Max")) {
		struct ain_type type = element_type(p);
		if (!size) {
			return_element(slot, -1);
		} else {
			int best = 0;
			int width = type_stride(&type);
			bool maximum = !strcmp(name, "Max");
			for (int i = 1; i < size; i++) {
				p = heap_get_page(slot);
				bool less = f->nr_arguments == 2
					? binary_predicate(slot, i, best, args[1])
					: compare_element(slot, i, p->values + best * width) < 0;
				// The original chooses the first minimum and the last maximum.
				if (maximum ? !less : less) best = i;
			}
			return_element(slot, best);
		}
	} else if (!strcmp(name, "Find") || !strcmp(name, "FindLast") || !strcmp(name, "IsExist")) {
		int arg = f->nr_arguments - 1, found = -1;
		int begin = f->nr_arguments == 4 ? max(args[1][0].i, 0) : 0;
		int end = f->nr_arguments == 4 ? min(args[2][0].i, size) : size;
		bool backwards = !strcmp(name, "FindLast");
		for (int i = backwards ? end - 1 : begin; i >= begin && i < end; i += backwards ? -1 : 1) {
			bool match = is_hll_func_type(f->arguments[arg].type.data)
				? predicate(slot, i, args[arg]) : equal_element(slot, i, args[arg]);
			if (match) { found = i; break; }
		}
		if (!strcmp(name, "IsExist")) stack_push(found >= 0); else stack_push(found);
	} else if (!strcmp(name, "BinarySearch") || !strcmp(name, "LowerBound") || !strcmp(name, "UpperBound")) {
		int first = 0, last = size;
		bool callback = is_hll_func_type(f->arguments[1].type.data);
		bool upper = !strcmp(name, "UpperBound");
		while (first < last) {
			int mid = first + (last - first) / 2;
			int cmp = callback ? predicate(slot, mid, args[1]) : compare_element(slot, mid, args[1]);
			if (cmp < 0 || (upper && cmp == 0)) first = mid + 1;
			else last = mid;
		}
		if (!strcmp(name, "BinarySearch")) {
			if (first == size || (callback ? predicate(slot, first, args[1]) : compare_element(slot, first, args[1])))
				first = -1;
		}
		stack_push(first);
	} else if (!strcmp(name, "Erase") || !strcmp(name, "EraseAll")) {
		bool erased = false;
		if (f->arguments[1].type.data == AIN_INT) {
			int index = args[1][0].i;
			int len = f->nr_arguments >= 3 ? args[2][0].i : 1;
			if (index >= 0 && index < size && len > 0) {
				erase_elements_range(slot, index, len);
				erased = true;
			}
		} else if (is_hll_func_type(f->arguments[1].type.data)) {
			for (int i = 0; i < size; ) {
				if (predicate(slot, i, args[1])) {
					erase_element(slot, i); size--; erased = true;
					if (!strcmp(name, "Erase")) break;
				} else i++;
			}
		} else if (f->arguments[1].type.data == AIN_REF_ARRAY || f->arguments[1].type.data == AIN_WRAP) {
			int src_slot = args[1][0].i;
			if (page_index_valid(src_slot) && heap[src_slot].page && heap[src_slot].page->type == ARRAY_PAGE) {
				struct page *src = heap_get_page(src_slot);
				struct ain_type type = element_type(p), src_type = element_type(src);
				int src_size = v12_array_length(src), width = type_stride(&type);
				if (src_size > 0 && type_stride(&src_type) != width)
					VM_ERROR("Array.Erase element types differ");
				for (int j = 0; j < src_size; j++) {
					src = heap_get_page(src_slot);
					for (int i = 0; i < size; i++) {
						if (equal_element(slot, i, src->values + j * width)) {
							erase_element(slot, i);
							size--;
							erased = true;
							break;
						}
					}
				}
			}
		} else VM_ERROR("Array.Erase overload is not implemented");
		stack_push(erased);
	} else if (!strcmp(name, "Fill")) {
		int first = f->nr_arguments == 4 ? max(args[1][0].i, 0) : 0;
		int count = f->nr_arguments == 4 ? max(args[2][0].i, 0) : size;
		if (first > size) first = size;
		if (count > size - first) count = size - first;
		for (int i = first; i < first + count; i++) set_element(slot, i, args[f->nr_arguments - 1]);
		stack_push(count);
	} else if (!strcmp(name, "Reverse")) {
		struct ain_type type = element_type(p);
		int width = type_stride(&type);
		for (int i = 0; i < size / 2; i++) {
			for (int j = 0; j < width; j++) {
				union vm_value tmp = p->values[i * width + j];
				p->values[i * width + j] = p->values[(size - 1 - i) * width + j];
				p->values[(size - 1 - i) * width + j] = tmp;
			}
		}
	} else if (!strcmp(name, "Shuffle")) {
		if (size > 1) {
			struct shuffle_random random;
			struct ain_type type = element_type(p);
			int seed = args[1][0].i, width = type_stride(&type);
			shuffle_random_init(&random, seed < 0 ? SDL_GetTicks() : (uint32_t)seed);
			for (int i = 1; i < size; i++) {
				int at = shuffle_random_next(&random) % (uint32_t)(i + 1);
				for (int j = 0; j < width; j++) {
					union vm_value tmp = p->values[i * width + j];
					p->values[i * width + j] = p->values[at * width + j];
					p->values[at * width + j] = tmp;
				}
			}
		}
	} else if (!strcmp(name, "Sort") || !strcmp(name, "QuickSort")
			|| !strcmp(name, "AscSort") || !strcmp(name, "DescSort")) {
		sort_array(slot, f->nr_arguments == 2 ? args[1] : NULL, !strcmp(name, "DescSort"));
		heap_ref(slot);
		stack_push(slot);
	} else if (!strcmp(name, "Unique") || !strcmp(name, "UniqueSorted")) {
		// Keep the first occurrence, including when equal values are separated.
		struct ain_type type = element_type(p);
		int width = type_stride(&type);
		for (int i = 0; i < size; i++) {
			for (int j = i + 1; j < size;) {
				p = heap_get_page(slot);
				bool equal = f->nr_arguments == 2 ? binary_predicate(slot, i, j, args[1])
					: equal_element(slot, j, p->values + i * width);
				if (equal) {
					erase_element(slot, j);
					size--;
				} else j++;
			}
		}
	} else if (!strcmp(name, "SYSTEMONLY_GetStructPageList")) {
		struct ain_type int_type = { .data = AIN_INT, .struc = -1 };
		struct page *res = v12_array_new(&int_type, size, false);
		struct ain_type type = element_type(p);
		int width = type_stride(&type);
		for (int i = 0; i < size; i++)
			res->values[i].i = p->values[i * width].i;
		stack_push(heap_alloc_page(res));
	} else {
		VM_ERROR("Unimplemented AIN v12 Array.%s overload (%d arguments)", name, f->nr_arguments);
	}
	cleanup_array_value_args(f, parameter, args);
	return true;
}

static int encoded_char_size(const char *text)
{
	return SJIS_2BYTE((unsigned char)*text) && text[1] ? 2 : 1;
}

static int string_find_last(const struct string *s, const struct string *match)
{
	int found = -1, index = 0;
	for (int offset = 0; offset < s->size; offset += encoded_char_size(s->text + offset), index++) {
		if (match->size <= s->size - offset && !memcmp(s->text + offset, match->text, match->size))
			found = index;
	}
	return found;
}

static struct page *split_string(struct string *s, struct string *separators, int mode)
{
	bool keep_sep = (mode & 1) != 0;
	bool keep_empty = (mode & 2) != 0;
	struct page *list = alloc_page(ARRAY_PAGE, AIN_ARRAY, s->size * 2 + 1);
	list->array.rank = 1;
	list->array.struct_type = -1;
	list->array.element_type = (struct ain_type){.data = AIN_STRING, .struc = -1};
	int count = 0;
	const char *begin = s->text, *at = begin;
	while (*at) {
		int width = encoded_char_size(at);
		bool separator = false;
		for (const char *sep = separators->text; *sep; sep += encoded_char_size(sep)) {
			if (encoded_char_size(sep) == width && !memcmp(at, sep, width)) {
				separator = true;
				break;
			}
		}
		if (separator) {
			if (at > begin || keep_empty) list->values[count++].i = heap_alloc_string(make_string(begin, at - begin));
			if (keep_sep) list->values[count++].i = heap_alloc_string(make_string(at, width));
			begin = at + width;
		}
		at += width;
	}
	if (at > begin || (keep_empty && s->size > 0)) list->values[count++].i = heap_alloc_string(make_string(begin, at - begin));
	list->nr_vars = count;
	return list;
}

static struct string *replace_string(struct string *s, struct string *match, struct string *replacement)
{
	if (!match->size) return string_dup(s);
	struct string *out = string_ref(&EMPTY_STRING);
	int begin = 0, at = 0;
	while (at < s->size) {
		if (match->size <= s->size - at && !memcmp(s->text + at, match->text, match->size)) {
			string_append_cstr(&out, s->text + begin, at - begin);
			string_append(&out, replacement);
			at += match->size;
			begin = at;
		} else {
			at += encoded_char_size(s->text + at);
		}
	}
	string_append_cstr(&out, s->text + begin, s->size - begin);
	return out;
}

static int utf8_char_step(const char *u)
{
	unsigned char c = (unsigned char)*u;
	if (!c) return 0;
	if ((c & 0x80) == 0) return 1;
	if ((c & 0xe0) == 0xc0) return u[1] ? 2 : 1;
	if ((c & 0xf0) == 0xe0) return (u[1] && u[2]) ? 3 : 1;
	return (u[1] && u[2] && u[3]) ? 4 : 1;
}

static int re_utf8_decode(const char *s, size_t len, size_t off, uint32_t *cp_out)
{
	if (off >= len) { *cp_out = 0; return 0; }
	unsigned char c0 = (unsigned char)s[off];
	if (c0 < 0x80) { *cp_out = c0; return 1; }
	if ((c0 & 0xe0) == 0xc0 && off + 1 < len) {
		*cp_out = ((uint32_t)(c0 & 0x1f) << 6) | ((unsigned char)s[off + 1] & 0x3f);
		return 2;
	}
	if ((c0 & 0xf0) == 0xe0 && off + 2 < len) {
		*cp_out = ((uint32_t)(c0 & 0x0f) << 12) |
		          ((uint32_t)((unsigned char)s[off + 1] & 0x3f) << 6) |
		          ((unsigned char)s[off + 2] & 0x3f);
		return 3;
	}
	if ((c0 & 0xf8) == 0xf0 && off + 3 < len) {
		*cp_out = ((uint32_t)(c0 & 0x07) << 18) |
		          ((uint32_t)((unsigned char)s[off + 1] & 0x3f) << 12) |
		          ((uint32_t)((unsigned char)s[off + 2] & 0x3f) << 6) |
		          ((unsigned char)s[off + 3] & 0x3f);
		return 4;
	}
	*cp_out = c0;
	return 1;
}

enum {
	RE_SH_D     = 1u << 0,
	RE_SH_NOT_D = 1u << 1,
	RE_SH_S     = 1u << 2,
	RE_SH_NOT_S = 1u << 3,
	RE_SH_W     = 1u << 4,
	RE_SH_NOT_W = 1u << 5,
};

struct re_range { uint32_t lo, hi; };

struct re_class {
	bool negated;
	unsigned shorthands;
	struct re_range *ranges;
	int nr_ranges;
	int cap_ranges;
};

enum re_opcode {
	RE_OP_CHAR,
	RE_OP_DOT,
	RE_OP_SHORTHAND,
	RE_OP_CLASS,
	RE_OP_BOL,
	RE_OP_EOL,
	RE_OP_WB,
	RE_OP_NWB,
	RE_OP_SAVE,
	RE_OP_JMP,
	RE_OP_SPLIT,
	RE_OP_MATCH,
};

struct re_inst {
	enum re_opcode op;
	int arg1;
	int arg2;
};

#define RE_MAX_GROUPS 64

struct re_prog {
	struct re_inst *insts;
	int nr_insts;
	int cap_insts;
	struct re_class *classes;
	int nr_classes;
	int cap_classes;
	int nr_groups;
	bool error;
};

static inline bool re_is_digit(uint32_t cp) { return cp >= '0' && cp <= '9'; }
static inline bool re_is_space(uint32_t cp)
{
	return cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n' ||
	       cp == '\v' || cp == '\f' || cp == 0x00a0 || cp == 0x3000;
}
static inline bool re_is_word(uint32_t cp)
{
	return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
	       (cp >= '0' && cp <= '9') || cp == '_';
}

static bool re_shorthand_matches(unsigned sh, uint32_t cp)
{
	if ((sh & RE_SH_D) && re_is_digit(cp)) return true;
	if ((sh & RE_SH_NOT_D) && !re_is_digit(cp)) return true;
	if ((sh & RE_SH_S) && re_is_space(cp)) return true;
	if ((sh & RE_SH_NOT_S) && !re_is_space(cp)) return true;
	if ((sh & RE_SH_W) && re_is_word(cp)) return true;
	if ((sh & RE_SH_NOT_W) && !re_is_word(cp)) return true;
	return false;
}

static bool re_class_matches(const struct re_class *cls, uint32_t cp)
{
	bool hit = cls->shorthands ? re_shorthand_matches(cls->shorthands, cp) : false;
	for (int i = 0; !hit && i < cls->nr_ranges; i++) {
		if (cp >= cls->ranges[i].lo && cp <= cls->ranges[i].hi)
			hit = true;
	}
	return cls->negated ? !hit : hit;
}

static void re_class_add_range(struct re_class *cls, uint32_t lo, uint32_t hi)
{
	if (lo > hi) { uint32_t t = lo; lo = hi; hi = t; }
	if (cls->nr_ranges >= cls->cap_ranges) {
		cls->cap_ranges = cls->cap_ranges ? cls->cap_ranges * 2 : 8;
		cls->ranges = xrealloc(cls->ranges, (size_t)cls->cap_ranges * sizeof(*cls->ranges));
	}
	cls->ranges[cls->nr_ranges++] = (struct re_range){ lo, hi };
}

#define RE_MAX_INSTS 65536

static int re_emit(struct re_prog *p, enum re_opcode op, int arg1, int arg2)
{
	if (p->error || p->nr_insts >= RE_MAX_INSTS) {
		p->error = true;
		return 0;
	}
	if (p->nr_insts >= p->cap_insts) {
		int next_cap = p->cap_insts ? p->cap_insts * 2 : 32;
		if (next_cap > RE_MAX_INSTS)
			next_cap = RE_MAX_INSTS;
		p->cap_insts = next_cap;
		p->insts = xrealloc(p->insts, (size_t)p->cap_insts * sizeof(*p->insts));
	}
	int idx = p->nr_insts++;
	p->insts[idx] = (struct re_inst){ op, arg1, arg2 };
	return idx;
}

static void re_insert_at(struct re_prog *p, int pos, enum re_opcode op, int arg1, int arg2)
{
	re_emit(p, RE_OP_MATCH, 0, 0);
	if (p->error)
		return;
	memmove(&p->insts[pos + 1], &p->insts[pos], (size_t)(p->nr_insts - 1 - pos) * sizeof(*p->insts));
	p->insts[pos] = (struct re_inst){ op, arg1, arg2 };
	for (int i = pos + 1; i < p->nr_insts; i++) {
		if (p->insts[i].op == RE_OP_JMP) {
			if (p->insts[i].arg1 >= pos) p->insts[i].arg1++;
		} else if (p->insts[i].op == RE_OP_SPLIT) {
			if (p->insts[i].arg1 >= pos) p->insts[i].arg1++;
			if (p->insts[i].arg2 >= pos) p->insts[i].arg2++;
		}
	}
}

static void re_append_relocated(struct re_prog *p, const struct re_inst *tpl, int tpl_len, int orig_base)
{
	int dst_base = p->nr_insts;
	int delta = dst_base - orig_base;
	for (int i = 0; i < tpl_len && !p->error; i++) {
		struct re_inst in = tpl[i];
		if (in.op == RE_OP_JMP && in.arg1 >= orig_base)
			in.arg1 += delta;
		else if (in.op == RE_OP_SPLIT) {
			if (in.arg1 >= orig_base) in.arg1 += delta;
			if (in.arg2 >= orig_base) in.arg2 += delta;
		}
		re_emit(p, in.op, in.arg1, in.arg2);
	}
}

static int re_hex_digit(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static bool re_parse_escaped(const char *pat, size_t len, size_t *off, uint32_t *cp_out, unsigned *sh_out)
{
	*sh_out = 0;
	if (*off >= len) { *cp_out = '\\'; return true; }
	uint32_t cp = 0;
	int step = re_utf8_decode(pat, len, *off, &cp);
	*off += (size_t)step;
	switch (cp) {
	case 'd': *sh_out = RE_SH_D; return true;
	case 'D': *sh_out = RE_SH_NOT_D; return true;
	case 's': *sh_out = RE_SH_S; return true;
	case 'S': *sh_out = RE_SH_NOT_S; return true;
	case 'w': *sh_out = RE_SH_W; return true;
	case 'W': *sh_out = RE_SH_NOT_W; return true;
	case 't': *cp_out = '\t'; return true;
	case 'r': *cp_out = '\r'; return true;
	case 'n': *cp_out = '\n'; return true;
	case 'v': *cp_out = '\v'; return true;
	case 'f': *cp_out = '\f'; return true;
	case '0': *cp_out = 0; return true;
	case 'x':
		if (*off + 2 <= len) {
			int h1 = re_hex_digit(pat[*off]), h2 = re_hex_digit(pat[*off + 1]);
			if (h1 >= 0 && h2 >= 0) { *off += 2; *cp_out = (uint32_t)((h1 << 4) | h2); return true; }
		}
		*cp_out = 'x';
		return true;
	case 'u':
		if (*off + 4 <= len) {
			int h1 = re_hex_digit(pat[*off]), h2 = re_hex_digit(pat[*off + 1]);
			int h3 = re_hex_digit(pat[*off + 2]), h4 = re_hex_digit(pat[*off + 3]);
			if (h1 >= 0 && h2 >= 0 && h3 >= 0 && h4 >= 0) {
				*off += 4;
				*cp_out = (uint32_t)((h1 << 12) | (h2 << 8) | (h3 << 4) | h4);
				return true;
			}
		}
		*cp_out = 'u';
		return true;
	default:
		*cp_out = cp;
		return true;
	}
}

static void re_compile_class(struct re_prog *p, const char *pat, size_t len, size_t *off)
{
	if (p->nr_classes >= p->cap_classes) {
		p->cap_classes = p->cap_classes ? p->cap_classes * 2 : 8;
		p->classes = xrealloc(p->classes, (size_t)p->cap_classes * sizeof(*p->classes));
	}
	int cidx = p->nr_classes++;
	struct re_class *cls = &p->classes[cidx];
	memset(cls, 0, sizeof(*cls));

	if (*off < len && pat[*off] == '^') {
		cls->negated = true;
		(*off)++;
	}
	bool closed = false;
	while (*off < len) {
		if (pat[*off] == ']') {
			(*off)++;
			closed = true;
			break;
		}
		uint32_t cp1 = 0;
		unsigned sh1 = 0;
		if (pat[*off] == '\\') {
			(*off)++;
			re_parse_escaped(pat, len, off, &cp1, &sh1);
		} else {
			*off += (size_t)re_utf8_decode(pat, len, *off, &cp1);
		}
		if (sh1) {
			cls->shorthands |= sh1;
			continue;
		}
		if (*off + 1 < len && pat[*off] == '-' && pat[*off + 1] != ']') {
			size_t save_off = *off;
			(*off)++; // skip '-'
			uint32_t cp2 = 0;
			unsigned sh2 = 0;
			if (pat[*off] == '\\') {
				(*off)++;
				re_parse_escaped(pat, len, off, &cp2, &sh2);
			} else {
				*off += (size_t)re_utf8_decode(pat, len, *off, &cp2);
			}
			if (sh2) {
				*off = save_off;
				re_class_add_range(cls, cp1, cp1);
			} else {
				re_class_add_range(cls, cp1, cp2);
			}
		} else {
			re_class_add_range(cls, cp1, cp1);
		}
	}
	if (!closed) {
		p->error = true;
		return;
	}
	re_emit(p, RE_OP_CLASS, cidx, 0);
}

static void re_compile_alt(struct re_prog *p, const char *pat, size_t len, size_t *off, bool in_group);

static void re_compile_atom(struct re_prog *p, const char *pat, size_t len, size_t *off)
{
	if (*off >= len) return;
	char c = pat[*off];
	if (c == '(') {
		(*off)++;
		bool non_cap = false;
		if (*off + 1 < len && pat[*off] == '?' && pat[*off + 1] == ':') {
			non_cap = true;
			*off += 2;
		}
		int g = -1;
		if (!non_cap && p->nr_groups < RE_MAX_GROUPS) {
			g = p->nr_groups++;
			re_emit(p, RE_OP_SAVE, g * 2, 0);
		}
		re_compile_alt(p, pat, len, off, true);
		if (*off >= len || pat[*off] != ')') {
			p->error = true;
			return;
		}
		(*off)++;
		if (g >= 0)
			re_emit(p, RE_OP_SAVE, g * 2 + 1, 0);
	} else if (c == '[') {
		(*off)++;
		re_compile_class(p, pat, len, off);
	} else if (c == '.') {
		(*off)++;
		re_emit(p, RE_OP_DOT, 0, 0);
	} else if (c == '^') {
		(*off)++;
		re_emit(p, RE_OP_BOL, 0, 0);
	} else if (c == '$') {
		(*off)++;
		re_emit(p, RE_OP_EOL, 0, 0);
	} else if (c == '\\') {
		(*off)++;
		if (*off < len && (pat[*off] == 'b' || pat[*off] == 'B')) {
			re_emit(p, pat[*off] == 'b' ? RE_OP_WB : RE_OP_NWB, 0, 0);
			(*off)++;
			return;
		}
		uint32_t cp = 0;
		unsigned sh = 0;
		re_parse_escaped(pat, len, off, &cp, &sh);
		if (sh) re_emit(p, RE_OP_SHORTHAND, (int)sh, 0);
		else re_emit(p, RE_OP_CHAR, (int)cp, 0);
	} else {
		uint32_t cp = 0;
		*off += (size_t)re_utf8_decode(pat, len, *off, &cp);
		re_emit(p, RE_OP_CHAR, (int)cp, 0);
	}
}

static bool re_try_parse_braces(const char *pat, size_t len, size_t *off, int *min_r, int *max_r)
{
	size_t i = *off + 1;
	if (i >= len || pat[i] < '0' || pat[i] > '9') return false;
	int m = 0;
	while (i < len && pat[i] >= '0' && pat[i] <= '9') {
		m = min(1024, m * 10 + (pat[i] - '0'));
		i++;
	}
	int n = m;
	if (i < len && pat[i] == ',') {
		i++;
		if (i < len && pat[i] == '}') {
			n = -1;
		} else if (i < len && pat[i] >= '0' && pat[i] <= '9') {
			n = 0;
			while (i < len && pat[i] >= '0' && pat[i] <= '9') {
				n = min(1024, n * 10 + (pat[i] - '0'));
				i++;
			}
		} else {
			return false;
		}
	}
	if (i >= len || pat[i] != '}') return false;
	if (n >= 0 && n < m) return false;
	*off = i + 1;
	*min_r = m;
	*max_r = n;
	return true;
}

static void re_compile_seq(struct re_prog *p, const char *pat, size_t len, size_t *off, bool in_group)
{
	while (*off < len && !p->error) {
		char c = pat[*off];
		if (c == '|' || (in_group && c == ')')) break;
		if (c == ')' || c == '*' || c == '+' || c == '?') {
			p->error = true;
			return;
		}
		int atom_start = p->nr_insts;
		re_compile_atom(p, pat, len, off);
		if (p->error) return;
		int atom_end = p->nr_insts;

		int min_r = -1, max_r = -1;
		bool has_q = false;
		if (*off < len) {
			char q = pat[*off];
			if (q == '*') { (*off)++; min_r = 0; max_r = -1; has_q = true; }
			else if (q == '+') { (*off)++; min_r = 1; max_r = -1; has_q = true; }
			else if (q == '?') { (*off)++; min_r = 0; max_r = 1; has_q = true; }
			else if (q == '{') { has_q = re_try_parse_braces(pat, len, off, &min_r, &max_r); }
		}
		if (!has_q || atom_end == atom_start) continue;

		bool greedy = true;
		if (*off < len && pat[*off] == '?') {
			greedy = false;
			(*off)++;
		}

		if (min_r == 0 && max_r == -1) {
			re_insert_at(p, atom_start, RE_OP_SPLIT, 0, 0);
			int body = atom_start + 1;
			re_emit(p, RE_OP_JMP, atom_start, 0);
			if (p->error) return;
			int after = p->nr_insts;
			p->insts[atom_start].arg1 = greedy ? body : after;
			p->insts[atom_start].arg2 = greedy ? after : body;
		} else if (min_r == 1 && max_r == -1) {
			int after = p->nr_insts + 1;
			re_emit(p, RE_OP_SPLIT, greedy ? atom_start : after, greedy ? after : atom_start);
			if (p->error) return;
		} else if (min_r == 0 && max_r == 1) {
			re_insert_at(p, atom_start, RE_OP_SPLIT, 0, 0);
			if (p->error) return;
			int body = atom_start + 1;
			int after = p->nr_insts;
			p->insts[atom_start].arg1 = greedy ? body : after;
			p->insts[atom_start].arg2 = greedy ? after : body;
		} else {
			int tpl_len = atom_end - atom_start;
			struct re_inst *tpl = xmalloc((size_t)tpl_len * sizeof(*tpl));
			memcpy(tpl, &p->insts[atom_start], (size_t)tpl_len * sizeof(*tpl));
			p->nr_insts = atom_start;
			for (int k = 0; k < min_r && !p->error; k++)
				re_append_relocated(p, tpl, tpl_len, atom_start);
			if (!p->error && max_r == -1) {
				int sp = re_emit(p, RE_OP_SPLIT, 0, 0);
				int body = p->nr_insts;
				re_append_relocated(p, tpl, tpl_len, atom_start);
				re_emit(p, RE_OP_JMP, sp, 0);
				if (!p->error) {
					int after = p->nr_insts;
					p->insts[sp].arg1 = greedy ? body : after;
					p->insts[sp].arg2 = greedy ? after : body;
				}
			} else if (!p->error) {
				for (int k = min_r; k < max_r && !p->error; k++) {
					int sp = re_emit(p, RE_OP_SPLIT, 0, 0);
					int body = p->nr_insts;
					re_append_relocated(p, tpl, tpl_len, atom_start);
					if (!p->error) {
						int after = p->nr_insts;
						p->insts[sp].arg1 = greedy ? body : after;
						p->insts[sp].arg2 = greedy ? after : body;
					}
				}
			}
			free(tpl);
		}
	}
}

static void re_compile_alt(struct re_prog *p, const char *pat, size_t len, size_t *off, bool in_group)
{
	int alt_start = p->nr_insts;
	re_compile_seq(p, pat, len, off, in_group);
	if (p->error || *off >= len || pat[*off] != '|')
		return;
	(*off)++; // consume '|'
	re_insert_at(p, alt_start, RE_OP_SPLIT, alt_start + 1, 0);
	int jmp_end = re_emit(p, RE_OP_JMP, 0, 0);
	if (p->error)
		return;
	int rhs_start = p->nr_insts;
	p->insts[alt_start].arg2 = rhs_start;
	re_compile_alt(p, pat, len, off, in_group);
	if (!p->error)
		p->insts[jmp_end].arg1 = p->nr_insts;
}

static bool re_compile(struct re_prog *p, const char *pat_utf)
{
	memset(p, 0, sizeof(*p));
	p->nr_groups = 1;
	size_t len = strlen(pat_utf), off = 0;
	re_emit(p, RE_OP_SAVE, 0, 0);
	re_compile_alt(p, pat_utf, len, &off, false);
	if (off != len) p->error = true;
	re_emit(p, RE_OP_SAVE, 1, 0);
	re_emit(p, RE_OP_MATCH, 0, 0);
	return !p->error;
}

static void re_free(struct re_prog *p)
{
	for (int i = 0; i < p->nr_classes; i++)
		free(p->classes[i].ranges);
	free(p->classes);
	free(p->insts);
}

struct re_bt_entry {
	bool is_save_undo;
	int slot_or_pc;
	int old_val;
	size_t off;
};

struct re_exec_ctx {
	const struct re_prog *prog;
	const char *text;
	size_t text_len;
	bool full_match;
	int *saved;
	uint32_t *visited;
	uint32_t gen;
	struct re_bt_entry *bt_stack;
	size_t bt_len;
	size_t bt_cap;
};

static inline void re_bt_push(struct re_exec_ctx *ctx, struct re_bt_entry e)
{
	if (ctx->bt_len >= ctx->bt_cap) {
		ctx->bt_cap = ctx->bt_cap ? ctx->bt_cap * 2 : 256;
		ctx->bt_stack = xrealloc(ctx->bt_stack, ctx->bt_cap * sizeof(*ctx->bt_stack));
	}
	ctx->bt_stack[ctx->bt_len++] = e;
}

static bool re_at_word_boundary(const char *s, size_t len, size_t off)
{
	bool prev_w = false, next_w = false;
	if (off > 0) {
		size_t p = off - 1;
		while (p > 0 && ((unsigned char)s[p] & 0xc0) == 0x80) p--;
		uint32_t cp = 0;
		re_utf8_decode(s, len, p, &cp);
		prev_w = re_is_word(cp);
	}
	if (off < len) {
		uint32_t cp = 0;
		re_utf8_decode(s, len, off, &cp);
		next_w = re_is_word(cp);
	}
	return prev_w != next_w;
}

static bool re_exec_dfs(struct re_exec_ctx *ctx, int pc, size_t off)
{
	ctx->bt_len = 0;
	for (;;) {
		size_t vidx = (size_t)pc * (ctx->text_len + 1) + off;
		if (ctx->visited[vidx] == ctx->gen)
			goto backtrack;
		ctx->visited[vidx] = ctx->gen;

		const struct re_inst *in = &ctx->prog->insts[pc];
		switch (in->op) {
		case RE_OP_CHAR: {
			uint32_t cp = 0;
			int step = re_utf8_decode(ctx->text, ctx->text_len, off, &cp);
			if (!step || cp != (uint32_t)in->arg1) goto backtrack;
			off += (size_t)step;
			pc++;
			break;
		}
		case RE_OP_DOT: {
			uint32_t cp = 0;
			int step = re_utf8_decode(ctx->text, ctx->text_len, off, &cp);
			if (!step || cp == '\n' || cp == '\r') goto backtrack;
			off += (size_t)step;
			pc++;
			break;
		}
		case RE_OP_SHORTHAND: {
			uint32_t cp = 0;
			int step = re_utf8_decode(ctx->text, ctx->text_len, off, &cp);
			if (!step || !re_shorthand_matches((unsigned)in->arg1, cp)) goto backtrack;
			off += (size_t)step;
			pc++;
			break;
		}
		case RE_OP_CLASS: {
			uint32_t cp = 0;
			int step = re_utf8_decode(ctx->text, ctx->text_len, off, &cp);
			if (!step || !re_class_matches(&ctx->prog->classes[in->arg1], cp)) goto backtrack;
			off += (size_t)step;
			pc++;
			break;
		}
		case RE_OP_BOL:
			if (off != 0 && ctx->text[off - 1] != '\n') goto backtrack;
			pc++;
			break;
		case RE_OP_EOL:
			if (off != ctx->text_len && ctx->text[off] != '\n' && ctx->text[off] != '\r') goto backtrack;
			pc++;
			break;
		case RE_OP_WB:
			if (!re_at_word_boundary(ctx->text, ctx->text_len, off)) goto backtrack;
			pc++;
			break;
		case RE_OP_NWB:
			if (re_at_word_boundary(ctx->text, ctx->text_len, off)) goto backtrack;
			pc++;
			break;
		case RE_OP_SAVE: {
			re_bt_push(ctx, (struct re_bt_entry){
				.is_save_undo = true,
				.slot_or_pc = in->arg1,
				.old_val = ctx->saved[in->arg1],
				.off = 0,
			});
			ctx->saved[in->arg1] = (int)off;
			pc++;
			break;
		}
		case RE_OP_JMP:
			pc = in->arg1;
			break;
		case RE_OP_SPLIT:
			re_bt_push(ctx, (struct re_bt_entry){
				.is_save_undo = false,
				.slot_or_pc = in->arg2,
				.old_val = 0,
				.off = off,
			});
			pc = in->arg1;
			break;
		case RE_OP_MATCH:
			if (!ctx->full_match || off == ctx->text_len)
				return true;
			goto backtrack;
		}
		continue;

backtrack:
		for (;;) {
			if (ctx->bt_len == 0)
				return false;
			struct re_bt_entry e = ctx->bt_stack[--ctx->bt_len];
			if (e.is_save_undo) {
				ctx->saved[e.slot_or_pc] = e.old_val;
			} else {
				pc = e.slot_or_pc;
				off = e.off;
				break;
			}
		}
	}
}

static bool re_match_at(struct re_exec_ctx *ctx, size_t start_off)
{
	for (int i = 0; i < ctx->prog->nr_groups * 2; i++)
		ctx->saved[i] = -1;
	ctx->gen++;
	return re_exec_dfs(ctx, 0, start_off);
}

static struct string *replace_regex_string(struct string *s, struct string *pattern, struct string *replacement)
{
	if (!pattern->size) return string_dup(s);
	char *pat_utf = mai_sjis2utf(pattern->text, pattern->size);
	struct re_prog prog;
	if (!re_compile(&prog, pat_utf)) {
		free(pat_utf);
		re_free(&prog);
		return string_dup(s);
	}
	free(pat_utf);

	char *text_utf = mai_sjis2utf(s->text, s->size);
	char *rep_utf = mai_sjis2utf(replacement->text, replacement->size);
	size_t text_len = strlen(text_utf);
	size_t rep_len = strlen(rep_utf);

	size_t n_states = 0;
	if (__builtin_mul_overflow((size_t)prog.nr_insts, text_len + 1, &n_states)
			|| n_states > 64u * 1024u * 1024u) {
		WARNING("Regex state space too large (%d insts, %zu bytes)", prog.nr_insts, text_len);
		free(rep_utf);
		free(text_utf);
		re_free(&prog);
		return string_dup(s);
	}

	int saved[RE_MAX_GROUPS * 2];
	uint32_t *visited = xcalloc(n_states, sizeof(*visited));
	struct re_exec_ctx ctx = {
		.prog = &prog, .text = text_utf, .text_len = text_len,
		.full_match = false, .saved = saved, .visited = visited, .gen = 0
	};

	size_t out_cap = text_len + rep_len + 64, out_len = 0;
	char *out_utf = xmalloc(out_cap);
	size_t last_emit = 0, off = 0;

	while (off <= text_len) {
		if (re_match_at(&ctx, off)) {
			size_t so = (size_t)saved[0], eo = (size_t)saved[1];
			if (eo == so && off == last_emit && off > 0) {
				int step = utf8_char_step(text_utf + off);
				if (!step) break;
				off += (size_t)step;
				continue;
			}
			size_t prefix = so - last_emit;
			if (out_len + prefix + rep_len + text_len + 16 > out_cap) {
				out_cap = (out_len + prefix + rep_len + text_len + 64) * 2;
				out_utf = xrealloc(out_utf, out_cap);
			}
			memcpy(out_utf + out_len, text_utf + last_emit, prefix);
			out_len += prefix;

			for (size_t r = 0; r < rep_len; r++) {
				if (rep_utf[r] == '$' && r + 1 < rep_len) {
					char nc = rep_utf[r + 1];
					if (nc == '$') {
						out_utf[out_len++] = '$';
						r++;
						continue;
					} else if (nc == '&' || (nc >= '0' && nc <= '9')) {
						int g = (nc == '&') ? 0 : (nc - '0');
						if (g < prog.nr_groups) {
							r++;
							if (saved[g * 2] >= 0 && saved[g * 2 + 1] >= saved[g * 2]) {
								size_t gl = (size_t)(saved[g * 2 + 1] - saved[g * 2]);
								if (out_len + gl + 16 > out_cap) {
									out_cap = (out_len + gl + 64) * 2;
									out_utf = xrealloc(out_utf, out_cap);
								}
								memcpy(out_utf + out_len, text_utf + saved[g * 2], gl);
								out_len += gl;
							}
							continue;
						}
					}
				}
				if (out_len + 4 > out_cap) {
					out_cap *= 2;
					out_utf = xrealloc(out_utf, out_cap);
				}
				out_utf[out_len++] = rep_utf[r];
			}
			last_emit = eo;
			if (eo > off) {
				off = eo;
			} else {
				int step = utf8_char_step(text_utf + off);
				if (!step) break;
				off += (size_t)step;
			}
		} else {
			int step = utf8_char_step(text_utf + off);
			if (!step) break;
			off += (size_t)step;
		}
	}
	size_t tail = text_len - last_emit;
	if (out_len + tail + 1 > out_cap) {
		out_cap = out_len + tail + 1;
		out_utf = xrealloc(out_utf, out_cap);
	}
	memcpy(out_utf + out_len, text_utf + last_emit, tail);
	out_len += tail;
	out_utf[out_len] = '\0';

	char *out_sjis = mai_utf2sjis(out_utf, out_len);
	struct string *res = make_string(out_sjis, strlen(out_sjis));
	free(out_sjis);
	free(out_utf);
	free(ctx.bt_stack);
	free(visited);
	free(rep_utf);
	free(text_utf);
	re_free(&prog);
	return res;
}

static bool string_regex_op(const char *op_name, const struct string *s, const struct string *pattern, int match_list_slot)
{
	if (match_list_slot >= 0 && page_index_valid(match_list_slot) &&
	    heap[match_list_slot].page && heap[match_list_slot].page->type == ARRAY_PAGE) {
		resize_array(match_list_slot, 0);
	} else {
		match_list_slot = -1;
	}

	char *pat_utf = mai_sjis2utf(pattern->text, pattern->size);
	struct re_prog prog;
	if (!re_compile(&prog, pat_utf)) {
		free(pat_utf);
		re_free(&prog);
		return false;
	}
	free(pat_utf);

	char *text_utf = mai_sjis2utf(s->text, s->size);
	size_t text_len = strlen(text_utf);
	bool full_match = !strcmp(op_name, "Match");
	bool search_all = !strcmp(op_name, "SearchAll");

	size_t n_states = 0;
	if (__builtin_mul_overflow((size_t)prog.nr_insts, text_len + 1, &n_states)
			|| n_states > 64u * 1024u * 1024u) {
		WARNING("Regex state space too large (%d insts, %zu bytes)", prog.nr_insts, text_len);
		free(text_utf);
		re_free(&prog);
		return false;
	}

	int saved[RE_MAX_GROUPS * 2];
	uint32_t *visited = xcalloc(n_states, sizeof(*visited));
	struct re_exec_ctx ctx = {
		.prog = &prog, .text = text_utf, .text_len = text_len,
		.full_match = full_match, .saved = saved, .visited = visited, .gen = 0
	};

	bool matched = false;
	if (search_all) {
		size_t off = 0;
		while (off <= text_len) {
			if (re_match_at(&ctx, off)) {
				matched = true;
				size_t so = (size_t)saved[0], eo = (size_t)saved[1];
				if (match_list_slot >= 0) {
					char *sub_utf = xmalloc(eo - so + 1);
					memcpy(sub_utf, text_utf + so, eo - so);
					sub_utf[eo - so] = '\0';
					char *sub_sjis = mai_utf2sjis(sub_utf, eo - so);
					free(sub_utf);
					int cur_len = v12_array_length(heap_get_page(match_list_slot));
					resize_array(match_list_slot, cur_len + 1);
					union vm_value val = { .i = heap_alloc_string(make_string(sub_sjis, strlen(sub_sjis))) };
					set_element(match_list_slot, cur_len, &val);
					heap_unref(val.i);
					free(sub_sjis);
				}
				if (eo > off) {
					off = eo;
				} else {
					int step = utf8_char_step(text_utf + off);
					if (!step) break;
					off += (size_t)step;
				}
			} else {
				int step = utf8_char_step(text_utf + off);
				if (!step) break;
				off += (size_t)step;
			}
		}
	} else if (full_match) {
		if (re_match_at(&ctx, 0)) {
			matched = true;
			if (match_list_slot >= 0) {
				resize_array(match_list_slot, prog.nr_groups);
				for (int g = 0; g < prog.nr_groups; g++) {
					struct string *gs;
					if (saved[g * 2] >= 0 && saved[g * 2 + 1] >= saved[g * 2]) {
						size_t glen = (size_t)(saved[g * 2 + 1] - saved[g * 2]);
						char *sub_utf = xmalloc(glen + 1);
						memcpy(sub_utf, text_utf + saved[g * 2], glen);
						sub_utf[glen] = '\0';
						char *sub_sjis = mai_utf2sjis(sub_utf, glen);
						free(sub_utf);
						gs = make_string(sub_sjis, strlen(sub_sjis));
						free(sub_sjis);
					} else {
						gs = string_ref(&EMPTY_STRING);
					}
					union vm_value val = { .i = heap_alloc_string(gs) };
					set_element(match_list_slot, g, &val);
					heap_unref(val.i);
				}
			}
		}
	} else {
		size_t off = 0;
		while (off <= text_len) {
			if (re_match_at(&ctx, off)) {
				matched = true;
				if (match_list_slot >= 0) {
					resize_array(match_list_slot, prog.nr_groups);
					for (int g = 0; g < prog.nr_groups; g++) {
						struct string *gs;
						if (saved[g * 2] >= 0 && saved[g * 2 + 1] >= saved[g * 2]) {
							size_t glen = (size_t)(saved[g * 2 + 1] - saved[g * 2]);
							char *sub_utf = xmalloc(glen + 1);
							memcpy(sub_utf, text_utf + saved[g * 2], glen);
							sub_utf[glen] = '\0';
							char *sub_sjis = mai_utf2sjis(sub_utf, glen);
							free(sub_utf);
							gs = make_string(sub_sjis, strlen(sub_sjis));
							free(sub_sjis);
						} else {
							gs = string_ref(&EMPTY_STRING);
						}
						union vm_value val = { .i = heap_alloc_string(gs) };
						set_element(match_list_slot, g, &val);
						heap_unref(val.i);
					}
				}
				break;
			}
			int step = utf8_char_step(text_utf + off);
			if (!step) break;
			off += (size_t)step;
		}
	}

	free(ctx.bt_stack);
	free(visited);
	free(text_utf);
	re_free(&prog);
	return matched;
}

static bool char_in_set(const char *ch, int ch_w, const char *set_bytes, int set_len)
{
	for (int off = 0; off < set_len; ) {
		int sw = encoded_char_size(set_bytes + off);
		if (sw == ch_w && !memcmp(ch, set_bytes + off, ch_w))
			return true;
		off += sw;
	}
	return false;
}

static struct string *trim_string_impl(const struct string *s, const struct string *char_list, bool trim_start, bool trim_end)
{
	const char *default_gbk = " \t\r\n\v\f\xa1\xa1";
	const char *default_sjis = " \t\r\n\v\f\x81\x40";
	const char *set_bytes = char_list ? char_list->text : (sys4_text_is_gbk ? default_gbk : default_sjis);
	int set_len = char_list ? char_list->size : 8;

	int start = 0;
	if (trim_start) {
		while (start < s->size) {
			int w = encoded_char_size(s->text + start);
			if (!char_in_set(s->text + start, w, set_bytes, set_len))
				break;
			start += w;
		}
	}
	int end = s->size;
	if (trim_end && end > start) {
		int last_non_ws_end = start;
		for (int off = start; off < s->size; ) {
			int w = encoded_char_size(s->text + off);
			if (!char_in_set(s->text + off, w, set_bytes, set_len))
				last_non_ws_end = off + w;
			off += w;
		}
		end = last_non_ws_end;
	}
	return make_string(s->text + start, end - start);
}

static struct string *pad_string_impl(const struct string *s, int target_bytes, int pad_ch, bool pad_left)
{
	if (target_bytes <= s->size)
		return string_dup(s);
	char pad_unit[2];
	int pad_w = 1;
	if ((pad_ch & 0xff00) != 0) {
		pad_unit[0] = (char)((pad_ch >> 8) & 0xff);
		pad_unit[1] = (char)(pad_ch & 0xff);
		pad_w = 2;
	} else {
		pad_unit[0] = pad_ch ? (char)(pad_ch & 0xff) : ' ';
	}
	int needed = target_bytes - s->size;
	struct string *out = string_ref(&EMPTY_STRING);
	if (!pad_left)
		string_append(&out, s);
	while (needed >= pad_w) {
		string_append_cstr(&out, pad_unit, pad_w);
		needed -= pad_w;
	}
	if (needed > 0)
		string_append_cstr(&out, " ", 1);
	if (pad_left)
		string_append(&out, s);
	return out;
}

static struct string *ascii_to_wide_string(const char *ascii)
{
	size_t len = strlen(ascii);
	char *buf = xmalloc(len * 2 + 1);
	size_t o = 0;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)ascii[i];
		if (sys4_text_is_gbk) {
			if (c == ' ') {
				buf[o++] = (char)0xa1; buf[o++] = (char)0xa1;
			} else if (c >= 0x21 && c <= 0x7e) {
				buf[o++] = (char)0xa3; buf[o++] = (char)(0x80 + c);
			} else {
				buf[o++] = (char)c;
			}
		} else {
			if (c >= '0' && c <= '9') {
				buf[o++] = (char)0x82; buf[o++] = (char)(0x4f + (c - '0'));
			} else if (c >= 'A' && c <= 'Z') {
				buf[o++] = (char)0x82; buf[o++] = (char)(0x60 + (c - 'A'));
			} else if (c >= 'a' && c <= 'z') {
				buf[o++] = (char)0x82; buf[o++] = (char)(0x81 + (c - 'a'));
			} else if (c == '-') {
				buf[o++] = (char)0x81; buf[o++] = (char)0x7c;
			} else if (c == '+') {
				buf[o++] = (char)0x81; buf[o++] = (char)0x7b;
			} else if (c == '.') {
				buf[o++] = (char)0x81; buf[o++] = (char)0x44;
			} else if (c == ' ') {
				buf[o++] = (char)0x81; buf[o++] = (char)0x40;
			} else {
				buf[o++] = (char)c;
			}
		}
	}
	buf[o] = '\0';
	struct string *res = make_string(buf, (unsigned int)o);
	free(buf);
	return res;
}

static bool string_call(struct ain_hll_function *f)
{
	union vm_value args[5] = {0};
	if (f->nr_arguments > 5) VM_ERROR("Too many String arguments");
	for (int i = f->nr_arguments - 1; i >= 0; i--) args[i] = stack_pop();
	int slot = args[0].i;
	bool valid_slot = string_index_valid(slot);
	if (unlikely(!valid_slot)) {
		static unsigned int warn_count = 0;
		if (warn_count++ < 64)
			WARNING("String.%s on invalid string slot: %d", f->name, slot);
	}
	struct string *s = valid_slot ? heap_get_string(slot) : &EMPTY_STRING, *result = NULL;
	const char *name = f->name;
	if (!strcmp(name, "Int") || !strcmp(name, "ToInt")) stack_push(string_to_integer(s));
	else if (!strcmp(name, "Float") || !strcmp(name, "ToFloat")) stack_push((float)strtod(s->text, NULL));
	else if (!strcmp(name, "LengthByte")) stack_push(s->size);
	else if (!strcmp(name, "Length")) {
		stack_push(sjis_count_char(s->text));
	} else if (!strcmp(name, "Empty")) stack_push(s->size == 0);
	else if (!strcmp(name, "GetPart")) result = string_copy(s, args[1].i, f->nr_arguments == 3 ? args[2].i : s->size);
	else if (!strcmp(name, "Replace")) result = replace_string(s, string_index_valid(args[1].i) ? heap_get_string(args[1].i) : &EMPTY_STRING, string_index_valid(args[2].i) ? heap_get_string(args[2].i) : &EMPTY_STRING);
	else if (!strcmp(name, "ReplaceRegex")) result = replace_regex_string(s, string_index_valid(args[1].i) ? heap_get_string(args[1].i) : &EMPTY_STRING, string_index_valid(args[2].i) ? heap_get_string(args[2].i) : &EMPTY_STRING);
	else if (!strcmp(name, "Find") || !strcmp(name, "FindLast") || !strcmp(name, "Contains")) {
		struct string *match = string_index_valid(args[1].i) ? heap_get_string(args[1].i) : &EMPTY_STRING;
		int index = !strcmp(name, "FindLast") ? string_find_last(s, match) : string_find(s, match);
		if (!strcmp(name, "Contains")) stack_push(index >= 0); else stack_push(index);
	} else if (!strcmp(name, "StartsWith") || !strcmp(name, "EndsWith")) {
		struct string *match = string_index_valid(args[1].i) ? heap_get_string(args[1].i) : &EMPTY_STRING;
		int offset = !strcmp(name, "EndsWith") ? s->size - match->size : 0;
		stack_push(offset >= 0 && match->size <= s->size && !memcmp(s->text + offset, match->text, match->size));
	} else if (!strcmp(name, "PushBack")) {
		if (valid_slot) string_push_back(&heap[slot].s, args[1].i);
	} else if (!strcmp(name, "Insert")) {
		if (valid_slot) {
			struct string *ins = string_index_valid(args[2].i) ? heap_get_string(args[2].i) : &EMPTY_STRING;
			int char_idx = max(args[1].i, 0);
			int byte_off = 0;
			for (int c = 0; c < char_idx && byte_off < s->size; c++)
				byte_off += encoded_char_size(s->text + byte_off);
			struct string *ns = make_string(s->text, byte_off);
			string_append(&ns, ins);
			string_append_cstr(&ns, s->text + byte_off, s->size - byte_off);
			free_string(heap[slot].s);
			heap[slot].s = ns;
		}
	} else if (!strcmp(name, "Split")) {
		stack_push(heap_alloc_page(split_string(s, string_index_valid(args[1].i) ? heap_get_string(args[1].i) : &EMPTY_STRING, args[2].i)));
	} else if (!strcmp(name, "PopBack")) {
		if (valid_slot) string_pop_back(&heap[slot].s);
	} else if (!strcmp(name, "Erase")) {
		if (valid_slot) {
			int count = f->nr_arguments >= 3 ? args[2].i : 1;
			for (int i = 0; i < count; i++) string_erase(&heap[slot].s, args[1].i);
		}
	} else if (!strcmp(name, "ToUpper") || !strcmp(name, "ToLower")) {
		result = string_dup(s);
		for (int i = 0; i < result->size; i++) {
			unsigned char c = result->text[i];
			if (c < 0x80) result->text[i] = !strcmp(name, "ToUpper") ? toupper(c) : tolower(c);
			else if (SJIS_2BYTE(c) && i + 1 < result->size) i++;
		}
	} else if (!strcmp(name, "PadLeft") || !strcmp(name, "PadRight")) {
		int pad_ch = f->nr_arguments >= 3 ? args[2].i : ' ';
		result = pad_string_impl(s, args[1].i, pad_ch, !strcmp(name, "PadLeft"));
	} else if (!strcmp(name, "Trim") || !strcmp(name, "TrimStart") || !strcmp(name, "TrimEnd")) {
		struct string *clist = (f->nr_arguments >= 2 && string_index_valid(args[1].i))
			? heap_get_string(args[1].i) : NULL;
		result = trim_string_impl(s, clist, strcmp(name, "TrimEnd") != 0, strcmp(name, "TrimStart") != 0);
	} else if (!strcmp(name, "Search") || !strcmp(name, "SearchAll") || !strcmp(name, "Match")) {
		int ml_slot = f->nr_arguments == 3 ? args[1].i : -1;
		int pat_slot = f->nr_arguments == 3 ? args[2].i : args[1].i;
		struct string *pat = string_index_valid(pat_slot) ? heap_get_string(pat_slot) : &EMPTY_STRING;
		stack_push(string_regex_op(name, s, pat, ml_slot) ? 1 : 0);
	} else VM_ERROR("Unimplemented AIN v12 String.%s overload (%d arguments)", name, f->nr_arguments);
	for (int i = 1; i < f->nr_arguments; i++)
		if (f->arguments[i].type.data == AIN_STRING) heap_unref(args[i].i);
	if (result) stack_push(heap_alloc_string(result));
	return true;
}

static bool delegate_call_hll(struct ain_hll_function *f)
{
	int fn = -1, object = -1, other_slot = -1;
	bool unref_other = false;
	if (f->nr_arguments == 2) {
		if (is_hll_func_type(f->arguments[1].type.data)) {
			fn = stack_pop().i;
			object = stack_pop().i;
		} else {
			other_slot = stack_pop().i;
			unref_other = (f->arguments[1].type.data == AIN_DELEGATE);
		}
	}
	int slot = stack_pop().i;
	if (unlikely(!page_index_valid(slot))) {
		static unsigned int warn_count = 0;
		if (!(ain->version >= 14 && slot == -1) && warn_count++ < 64)
			WARNING("Delegate.%s on invalid delegate slot: %d", f->name, slot);
		if (!strcmp(f->name, "Numof") || !strcmp(f->name, "Exist")
				|| !strcmp(f->name, "IsExist") || !strcmp(f->name, "Equals"))
			stack_push(0);
		else if (!strcmp(f->name, "Empty"))
			stack_push(1);
		else if (!strcmp(f->name, "ToString"))
			stack_push(heap_alloc_string(string_ref(&EMPTY_STRING)));
		if (unref_other && other_slot > 0 && page_index_valid(other_slot))
			heap_unref(other_slot);
		return true;
	}
	struct page *page = heap_get_page(slot);
	if (!strcmp(f->name, "Set")) {
		page = delegate_clear(page);
		heap_set_page(slot, delegate_append(page, object, fn));
	} else if (!strcmp(f->name, "Add")) heap_set_page(slot, delegate_append(page, object, fn));
	else if (!strcmp(f->name, "Numof")) stack_push(delegate_numof(page));
	else if (!strcmp(f->name, "Empty")) stack_push(delegate_numof(page) == 0);
	else if (!strcmp(f->name, "Exist") || !strcmp(f->name, "IsExist")) stack_push(delegate_contains(page, object, fn));
	else if (!strcmp(f->name, "Erase")) delegate_erase(page, object, fn);
	else if (!strcmp(f->name, "Clear")) heap_set_page(slot, delegate_clear(page));
	else if (!strcmp(f->name, "Equals")) {
		struct page *other = page_index_valid(other_slot) ? heap_get_page(other_slot) : NULL;
		int n1 = delegate_numof(page), n2 = delegate_numof(other);
		bool eq = (n1 == n2);
		for (int i = 0; eq && i < n1; i++) {
			if (page->values[i * 4].i != other->values[i * 4].i ||
			    page->values[i * 4 + 1].i != other->values[i * 4 + 1].i)
				eq = false;
		}
		stack_push(eq ? 1 : 0);
	} else if (!strcmp(f->name, "ToString")) {
		struct string *out = string_ref(&EMPTY_STRING);
		int n = delegate_numof(page);
		for (int i = 0; i < n; i++) {
			int dfn = page->values[i * 4 + 1].i;
			if (i > 0)
				string_append_cstr(&out, ", ", 2);
			if (dfn >= 0 && dfn < ain->nr_functions && ain->functions[dfn].name)
				string_append_cstr(&out, ain->functions[dfn].name, strlen(ain->functions[dfn].name));
		}
		stack_push(heap_alloc_string(out));
	} else VM_ERROR("Unimplemented AIN v12 Delegate.%s", f->name);
	if (unref_other && other_slot > 0 && page_index_valid(other_slot))
		heap_unref(other_slot);
	return true;
}

static bool int_call_hll(struct ain_hll_function *f)
{
	int pad = 0;
	if (f->nr_arguments == 2)
		pad = stack_pop().i;
	int index = stack_pop().i, page = stack_pop().i;
	int32_t val = (page_index_valid(page) && heap[page].page && index >= 0 && index < heap[page].page->nr_vars)
		? heap[page].page->values[index].i : 0;

	if (!strcmp(f->name, "String")) {
		stack_push(heap_alloc_string(integer_to_string(val)));
		return true;
	}
	if (!strcmp(f->name, "ToString") || !strcmp(f->name, "ToWideString")) {
		char buf[64];
		if (pad > 0 && pad < 60)
			snprintf(buf, sizeof(buf), "%0*d", pad, val);
		else
			snprintf(buf, sizeof(buf), "%d", val);
		struct string *res = !strcmp(f->name, "ToWideString")
			? ascii_to_wide_string(buf) : make_string(buf, strlen(buf));
		stack_push(heap_alloc_string(res));
		return true;
	}
	if (!strcmp(f->name, "ToHexString") || !strcmp(f->name, "ToLowerHexString")) {
		char buf[64];
		const char *fmt = !strcmp(f->name, "ToHexString")
			? (pad > 0 && pad < 60 ? "%0*X" : "%X")
			: (pad > 0 && pad < 60 ? "%0*x" : "%x");
		if (pad > 0 && pad < 60)
			snprintf(buf, sizeof(buf), fmt, pad, (uint32_t)val);
		else
			snprintf(buf, sizeof(buf), fmt, (uint32_t)val);
		stack_push(heap_alloc_string(make_string(buf, strlen(buf))));
		return true;
	}
	if (!strcmp(f->name, "ToCharacter")) {
		char ch[2];
		int len = 0;
		if ((val & 0xff00) != 0) {
			ch[0] = (char)((val >> 8) & 0xff);
			ch[1] = (char)(val & 0xff);
			len = 2;
		} else if ((val & 0xff) != 0) {
			ch[0] = (char)(val & 0xff);
			len = 1;
		}
		stack_push(heap_alloc_string(make_string(ch, len)));
		return true;
	}
	return false;
}

static bool float_call_hll(struct ain_hll_function *f)
{
	if (!strcmp(f->name, "String")) {
		int precision = f->nr_arguments == 2 ? stack_pop().i : 6;
		int index = stack_pop().i, page = stack_pop().i;
		float val = (page_index_valid(page) && heap[page].page && index >= 0 && index < heap[page].page->nr_vars)
			? heap[page].page->values[index].f : 0.0f;
		stack_push(heap_alloc_string(float_to_string(val, precision)));
		return true;
	}
	if (!strcmp(f->name, "ToString") || !strcmp(f->name, "ToWideString")) {
		int pad = f->nr_arguments >= 3 ? stack_pop().i : 0;
		int decimal = f->nr_arguments >= 2 ? stack_pop().i : 6;
		int index = stack_pop().i, page = stack_pop().i;
		float val = (page_index_valid(page) && heap[page].page && index >= 0 && index < heap[page].page->nr_vars)
			? heap[page].page->values[index].f : 0.0f;
		if (decimal < 0) decimal = 6;
		if (decimal > 32) decimal = 32;
		char buf[128];
		if (pad > 0 && pad < 100)
			snprintf(buf, sizeof(buf), "%0*.*f", pad, decimal, (double)val);
		else
			snprintf(buf, sizeof(buf), "%.*f", decimal, (double)val);
		struct string *res = !strcmp(f->name, "ToWideString")
			? ascii_to_wide_string(buf) : make_string(buf, strlen(buf));
		stack_push(heap_alloc_string(res));
		return true;
	}
	return false;
}

static bool system_call_hll(struct ain_hll_function *f)
{
	const char *name = f->name;
	if (!strcmp(name, "ExistSaveFile")) {
		vm_system_call(SYS_EXISTS_SAVE_FILE);
		return true;
	}
	if (!strcmp(name, "PeekAll")) {
		vm_system_call(SYS_PEEK);
		return true;
	}
	if (!strcmp(name, "OutputLine")) {
		int s_slot = stack_peek(0).i;
		struct string *str = string_index_valid(s_slot) ? heap_get_string(s_slot) : &EMPTY_STRING;
		log_message("stdout", "%s\n", display_sjis0(str->text));
		if (f->return_type.data == AIN_VOID)
			heap_unref(stack_pop().i);
		return true;
	}
	if (!strcmp(name, "CopySaveFile") || !strcmp(name, "BackupSaveFile")) {
		int src = stack_pop().i;
		int dst = stack_pop().i;
		char *u_src = savedir_path(heap_get_string(src)->text);
		char *u_dst = savedir_path(heap_get_string(dst)->text);
		char *u_dst_copy = xstrdup(u_dst);
		char *dir = path_dirname(u_dst_copy);
		if (dir && dir[0])
			mkdir_p(dir);
		free(u_dst_copy);
		stack_push(file_copy(u_src, u_dst));
		free(u_src);
		free(u_dst);
		heap_unref(src);
		heap_unref(dst);
		return true;
	}
	if (!strcmp(name, "WriteGroupSaveComment")) {
		int log_slot = stack_pop().i;
		int file_slot = stack_pop().i;
		int key_slot = stack_pop().i;
		int ok = write_group_save_comment(
			string_index_valid(key_slot) ? heap_get_string(key_slot)->text : "",
			string_index_valid(file_slot) ? heap_get_string(file_slot)->text : "",
			string_index_valid(log_slot) ? heap_get_string(log_slot) : &EMPTY_STRING);
		heap_unref(log_slot);
		heap_unref(file_slot);
		heap_unref(key_slot);
		stack_push(ok);
		return true;
	}
	if (!strcmp(name, "ReadGroupSaveComment")) {
		int log_slot = stack_pop().i;
		int file_slot = stack_pop().i;
		int key_slot = stack_pop().i;
		struct string *loaded = NULL;
		int ok = read_group_save_comment(
			string_index_valid(key_slot) ? heap_get_string(key_slot)->text : "",
			string_index_valid(file_slot) ? heap_get_string(file_slot)->text : "",
			&loaded);
		if (ok && loaded) {
			if (string_index_valid(log_slot)) {
				free_string(heap[log_slot].s);
				heap[log_slot].s = loaded;
			} else {
				free_string(loaded);
			}
		}
		heap_unref(file_slot);
		heap_unref(key_slot);
		stack_push(ok);
		return true;
	}
	if (!strcmp(name, "SerializeStruct") || !strcmp(name, "DeserializeStruct")) {
		bool save_folder = !!stack_pop().i;
		int list_slot = stack_pop().i;
		int file_slot = stack_pop().i;
		struct page *list_page = page_index_valid(list_slot) ? heap_get_page(list_slot) : NULL;
		const char *fname = string_index_valid(file_slot) ? heap_get_string(file_slot)->text : "";
		int ok = !strcmp(name, "SerializeStruct")
			? serialize_structs(fname, list_page, save_folder)
			: deserialize_structs(fname, list_page, save_folder);
		if (page_index_valid(list_slot))
			heap_unref(list_slot);
		if (string_index_valid(file_slot))
			heap_unref(file_slot);
		stack_push(ok);
		return true;
	}
	if (!strcmp(name, "WriteSerializeStructComment")) {
		bool save_folder = !!stack_pop().i;
		int comment_slot = stack_pop().i;
		int file_slot = stack_pop().i;
		int ok = write_serialize_struct_comment(
			string_index_valid(file_slot) ? heap_get_string(file_slot)->text : "",
			string_index_valid(comment_slot) ? heap_get_string(comment_slot) : &EMPTY_STRING,
			save_folder);
		if (string_index_valid(comment_slot))
			heap_unref(comment_slot);
		if (string_index_valid(file_slot))
			heap_unref(file_slot);
		stack_push(ok);
		return true;
	}
	if (!strcmp(name, "ReadSerializeStructComment")) {
		bool save_folder = !!stack_pop().i;
		int comment_slot = stack_pop().i;
		int file_slot = stack_pop().i;
		struct string *loaded = NULL;
		int ok = read_serialize_struct_comment(
			string_index_valid(file_slot) ? heap_get_string(file_slot)->text : "",
			&loaded,
			save_folder);
		if (ok && loaded) {
			if (string_index_valid(comment_slot)) {
				free_string(heap[comment_slot].s);
				heap[comment_slot].s = loaded;
			} else {
				free_string(loaded);
			}
		}
		if (string_index_valid(file_slot))
			heap_unref(file_slot);
		stack_push(ok);
		return true;
	}
	for (int i = 0; i < NR_SYSCALLS; i++) {
		if (syscalls[i].name && !strcmp(syscalls[i].name + 7, name)) {
			vm_system_call(i);
			return true;
		}
	}
	VM_ERROR("Unimplemented AIN v12 system.%s", name);
}

bool v12_builtin_library(const char *name)
{
	if (!ain || !name)
		return false;
	if (ain->version >= 11) {
		return !strcmp(name, "system") || !strcmp(name, "Array") || !strcmp(name, "String")
			|| !strcmp(name, "Delegate") || !strcmp(name, "Int") || !strcmp(name, "Float");
	}
	if (ain->version >= 8) {
		return !strcmp(name, "String") || !strcmp(name, "Int") || !strcmp(name, "Float");
	}
	return false;
}

bool v12_hll_call(int library, int function, int type_parameter)
{
	if (!ain || ain->version < 8)
		return false;
	if (library < 0 || library >= ain->nr_libraries || function < 0
			|| function >= ain->libraries[library].nr_functions)
		VM_ERROR("Invalid AIN v12 HLL call: %d.%d", library, function);
	struct ain_library *lib = &ain->libraries[library];
	struct ain_hll_function *f = &lib->functions[function];
	if (ain->version >= 11) {
		if (!strcmp(lib->name, "system")) return system_call_hll(f);
		if (!strcmp(lib->name, "Array")) return array_call(f, type_parameter);
		if (!strcmp(lib->name, "Delegate")) return delegate_call_hll(f);
	}
	if (!strcmp(lib->name, "String")) return string_call(f);
	if (!strcmp(lib->name, "Int")) return int_call_hll(f);
	if (!strcmp(lib->name, "Float")) return float_call_hll(f);
	return false;
}
