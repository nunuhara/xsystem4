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
#include <math.h>
#include <time.h>
#include <setjmp.h>
#include <assert.h>
#include <SDL.h> // for system.MsgBox

#include "system4.h"
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/file.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "debugger.h"
#include "gfx/gfx.h"
#include "input.h"
#include "message_layout.h"
#include "modifier.h"
#include "savedata.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "vm/v12.h"
#include "xsystem4.h"

static inline int32_t lint_clamp(int64_t n)
{
	if (n < 0)
		return 0;
	if (n > INT32_MAX)
		return INT32_MAX;
	return (int32_t)n;
}

#define INITIAL_STACK_SIZE 65536

// When the IP is set to VM_RETURN, the VM halts
#define VM_RETURN 0xFFFFFFFF

/*
 * NOTE: The current implementation is a simple bytecode interpreter.
 *       System40.exe uses a JIT compiler, and we should too.
 */

// The stack
union vm_value *stack = NULL; // the stack
int32_t stack_ptr = 0;        // pointer to the top of the stack
static size_t stack_size;     // current size of the stack

static inline void stack_ensure(int extra)
{
	if (unlikely((size_t)(stack_ptr + extra) >= stack_size)) {
		while ((size_t)(stack_ptr + extra) >= stack_size)
			stack_size *= 2;
		stack = xrealloc(stack, stack_size * sizeof(union vm_value));
	}
}

void vm_stack_ensure(size_t needed)
{
	if (stack && needed < stack_size)
		return;
	size_t new_size = stack_size ? stack_size : INITIAL_STACK_SIZE;
	while (new_size <= needed) {
		if (new_size > SIZE_MAX / 2 / sizeof(union vm_value))
			VM_ERROR("VM stack size overflow");
		new_size *= 2;
	}
	stack = xrealloc(stack, new_size * sizeof(union vm_value));
	stack_size = new_size;
}

// Stack of function call frames
struct function_call call_stack[4096];
int32_t call_stack_ptr = 0;

struct ain *ain;
size_t instr_ptr = 0;
bool dg_callbegin_pending = false;

bool vm_reset_once = false;

static bool vm_zero_vector_div_guard(void)
{
	if (call_stack_ptr <= 0 || !ain)
		return false;
	int fno = call_stack[call_stack_ptr - 1].fno;
	if (fno < 0 || fno >= ain->nr_functions || !ain->functions[fno].name)
		return false;
	const char *name = ain->functions[fno].name;
	return !strcmp(name, "VECTOR3D@Normalize")
		|| !strcmp(name, "VECTOR2D@Normalize")
		|| !strcmp(name, "VECTOR2D@Rotate")
		|| !strcmp(name, "VECTOR3D@SetLength")
		|| !strcmp(name, "VECTOR2D@SetLength")
		|| !strcmp(name, "CASVector3D@Normalize")
		|| !strcmp(name, "CASVector2D@Normalize")
		|| !strcmp(name, "CASVector3D@SetLength")
		|| !strcmp(name, "CASVector2D@SetLength")
		|| !strcmp(name, "fxyz_t@Normalize")
		|| !strcmp(name, "CParticleObject@SetDirection")
		|| !strcmp(name, "CPolygonUnitObject@GetDirectionAngle")
		|| !strcmp(name, "CBillBoardUnitObject@GetDirectionAngle");
}

static const char *current_instruction_name(void)
{
	int16_t opcode = get_opcode(instr_ptr);
	if (opcode >= 0 && opcode < NR_OPCODES)
		return instructions[opcode].name;
	return "UNKNOWN OPCODE";
}

static int local_page_slot(void)
{
	return call_stack[call_stack_ptr-1].page_slot;
}

struct page *local_page(void)
{
	return heap[local_page_slot()].page;
}

struct page *get_local_page(int frame_no)
{
	if (frame_no < 0 || frame_no >= call_stack_ptr)
		return NULL;
	int slot = call_stack[call_stack_ptr - (frame_no + 1)].page_slot;
	return slot < 1 ? NULL : heap[slot].page;
}

union vm_value local_get(int varno)
{
	return local_page()->values[varno];
}

static void local_set(int varno, int32_t value)
{
	local_page()->values[varno].i = value;
}

static union vm_value *local_ptr(int varno)
{
	return local_page()->values + varno;
}

struct page *global_page(void)
{
	return heap[0].page;
}

union vm_value global_get(int varno)
{
	return heap[0].page->values[varno];
}

void global_set(int varno, union vm_value val, bool call_dtors)
{
	switch (ain->globals[varno].type.data) {
	case AIN_WRAP:
		if (!ain || ain->version < 14)
			break;
		// fallthrough
	case AIN_STRING:
	case AIN_STRUCT:
	case AIN_DELEGATE:
	case AIN_ARRAY_TYPE:
	case AIN_ARRAY:
	case AIN_IFACE:
	case AIN_IFACE_WRAP:
	case AIN_REF_OPTION:
	case AIN_REF_TYPE:
		if (heap[0].page->values[varno].i > 0) {
			if (call_dtors)
				heap_unref(heap[0].page->values[varno].i);
			else
				exit_unref(heap[0].page->values[varno].i);
		}
		break;
	default:
		break;
	}
	heap[0].page->values[varno] = val;
}

static int32_t struct_page_slot(void)
{
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		if (call_stack[i].has_struct_page)
			return call_stack[i].struct_page;
	}
	return -1;
}

static struct page *struct_page(void)
{
	int32_t slot = struct_page_slot();
	if (unlikely(!page_index_valid(slot)))
		return NULL;
	return heap[slot].page;
}

struct page *get_struct_page(int frame_no)
{
	if (frame_no < 0 || frame_no >= call_stack_ptr)
		return NULL;
	int slot = -1;
	for (int i = call_stack_ptr - (frame_no + 1); i >= 0; i--) {
		if (call_stack[i].has_struct_page) {
			slot = call_stack[i].struct_page;
			break;
		}
	}
	return page_index_valid(slot) && slot >= 1 ? heap[slot].page : NULL;
}

static const struct ain_type *last_dummy_type = NULL;
static int32_t last_var_heap = -1;
static int32_t last_var_page = -1;
static unsigned int invalid_var_warn_count = 0;
#define INVALID_VAR_WARN_LIMIT 64

static bool ain_type_is_null_handle(enum ain_data_type dt)
{
	switch (dt) {
	case AIN_INT:
	case AIN_FLOAT:
	case AIN_BOOL:
	case AIN_LONG_INT:
	case AIN_FUNC_TYPE:
	case AIN_ENUM:
	case AIN_ENUM2:
	case AIN_VOID:
		return false;
	default:
		return true;
	}
}

static const struct ain_type *lookup_expected_var_type(int32_t heap_index, int32_t page_index)
{
	if (!ain)
		return NULL;
	if (heap_index_valid(heap_index) && heap[heap_index].type == VM_PAGE && heap[heap_index].page) {
		struct page *p = heap[heap_index].page;
		switch (p->type) {
		case STRUCT_PAGE:
			if (p->index >= 0 && p->index < ain->nr_structures &&
			    page_index >= 0 && page_index < ain->structures[p->index].nr_members)
				return &ain->structures[p->index].members[page_index].type;
			break;
		case LOCAL_PAGE:
			if (p->index >= 0 && p->index < ain->nr_functions &&
			    page_index >= 0 && page_index < ain->functions[p->index].nr_vars)
				return &ain->functions[p->index].vars[page_index].type;
			break;
		case GLOBAL_PAGE:
			if (page_index >= 0 && page_index < ain->nr_globals)
				return &ain->globals[page_index].type;
			break;
		case ARRAY_PAGE:
			if (p->a_type == AIN_ARRAY)
				return &p->array.element_type;
			break;
		default:
			break;
		}
	} else if (heap_index < 0 && call_stack_ptr > 0) {
		int fno = call_stack[call_stack_ptr - 1].fno;
		if (fno >= 0 && fno < ain->nr_functions) {
			int st = ain->functions[fno].struct_type;
			if (st >= 0 && st < ain->nr_structures &&
			    struct_page_slot() == heap_index &&
			    page_index >= 0 && page_index < ain->structures[st].nr_members) {
				return &ain->structures[st].members[page_index].type;
			}
		}
	}
	return NULL;
}

static union vm_value *prepare_dummy_var(union vm_value dummy[4], int32_t heap_index, int32_t page_index)
{
	const struct ain_type *t = lookup_expected_var_type(heap_index, page_index);
	last_dummy_type = t;
	dummy[1].i = 0;
	dummy[2].i = 0;
	dummy[3].i = 0;
	if (t) {
		if (t->data == AIN_OPTION) {
			int w = ain_type_slot_width(t);
			dummy[0].i = -1;
			dummy[w - 1].i = 1;
			return &dummy[0];
		}
		dummy[0].i = ain_type_is_null_handle(t->data) ? -1 : 0;
	} else if (ain && ain->code && instr_ptr + 2 <= ain->code_size &&
		   (get_opcode(instr_ptr) & ~OPTYPE_MASK) == REFREF) {
		dummy[0].i = -1;
	} else {
		dummy[0].i = 0;
	}
	return &dummy[0];
}

union vm_value member_get(int varno)
{
	int32_t slot = struct_page_slot();
	struct page *page = page_index_valid(slot) ? heap[slot].page : NULL;
	if (unlikely(!page || varno < 0 || varno >= page->nr_vars)) {
		union vm_value dummy[4];
		if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT)
			WARNING("Invalid struct member read: %d/%d", slot, varno);
		return *prepare_dummy_var(dummy, slot, varno);
	}
	return page->values[varno];
}

static void member_set(int varno, int32_t value)
{
	int32_t slot = struct_page_slot();
	struct page *page = page_index_valid(slot) ? heap[slot].page : NULL;
	if (unlikely(!page || varno < 0 || varno >= page->nr_vars)) {
		if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT)
			WARNING("Invalid struct member write: %d/%d", slot, varno);
		return;
	}
	page->values[varno].i = value;
}

union vm_value stack_peek(int n)
{
	return stack[stack_ptr - (1 + n)];
}

union vm_value stack_pop(void)
{
	stack_ptr--;
	return stack[stack_ptr];
}

static union vm_value *stack_peek_ptr(int n)
{
	return &stack[stack_ptr - (1 + n)];
}

// Pop a reference off the stack, returning the address of the referenced object.
static union vm_value *stack_pop_var(void)
{
	static union vm_value dummy_var[4];
	int32_t page_index = stack_pop().i;
	int32_t heap_index = stack_pop().i;
	last_var_heap = heap_index;
	last_var_page = page_index;
	if (unlikely(!heap_index_valid(heap_index))) {
		if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT)
			WARNING("Out of bounds heap index: %d/%d", heap_index, page_index);
		return prepare_dummy_var(dummy_var, heap_index, page_index);
	}
	if (unlikely(heap[heap_index].type != VM_PAGE)) {
		if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT)
			WARNING("Heap index is not a page: %d/%d (type=%d, str=%s)", heap_index, page_index,
				heap[heap_index].type, (heap[heap_index].type == VM_STRING && heap[heap_index].s) ? heap[heap_index].s->text : "");
		return prepare_dummy_var(dummy_var, heap_index, page_index);
	}
	if (unlikely(!heap[heap_index].page || page_index < 0 || page_index >= heap[heap_index].page->nr_vars)) {
		if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT)
			WARNING("Out of bounds page index: %d/%d", heap_index, page_index);
		return prepare_dummy_var(dummy_var, heap_index, page_index);
	}
	last_dummy_type = NULL;
	return &heap[heap_index].page->values[page_index];
}

union vm_value *stack_peek_var(void)
{
	static union vm_value dummy_var[4];
	int32_t page_index = stack_peek(0).i;
	int32_t heap_index = stack_peek(1).i;
	if (unlikely(!heap_index_valid(heap_index))) {
		if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT)
			WARNING("Out of bounds heap index: %d/%d", heap_index, page_index);
		return prepare_dummy_var(dummy_var, heap_index, page_index);
	}
	if (unlikely(heap[heap_index].type != VM_PAGE)) {
		if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT)
			WARNING("Heap index is not a page: %d/%d (type=%d, str=%s)", heap_index, page_index,
				heap[heap_index].type, (heap[heap_index].type == VM_STRING && heap[heap_index].s) ? heap[heap_index].s->text : "");
		return prepare_dummy_var(dummy_var, heap_index, page_index);
	}
	if (unlikely(!heap[heap_index].page || page_index < 0 || page_index >= heap[heap_index].page->nr_vars)) {
		if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT)
			WARNING("Out of bounds page index: %d/%d", heap_index, page_index);
		return prepare_dummy_var(dummy_var, heap_index, page_index);
	}
	last_dummy_type = NULL;
	return &heap[heap_index].page->values[page_index];
}

static void stack_push_string(struct string *s)
{
	int32_t heap_slot = heap_alloc_slot(VM_STRING);
	heap[heap_slot].s = s;
	stack[stack_ptr++].i = heap_slot;
}

static struct string *stack_peek_string(int n)
{
	int slot = stack_peek(n).i;
	return string_index_valid(slot) ? heap[slot].s : &EMPTY_STRING;
}

// GBK-translated games may compare names built with %D/%F (GBK fullwidth
// digits) against Shift-JIS names from untranslated data files.
static bool vm_string_equal_gbk(const struct string *a, const struct string *b)
{
	if (a->size != b->size)
		return false;
	char a_buf[256], b_buf[256];
	bool a_changed = false, b_changed = false;
	char *a_norm = cjk_gbk_zenkaku_to_sjis_buf(a->text, a->size, a_buf, sizeof(a_buf), &a_changed);
	char *b_norm = cjk_gbk_zenkaku_to_sjis_buf(b->text, b->size, b_buf, sizeof(b_buf), &b_changed);
	bool eq = (a_changed || b_changed) && !memcmp(a_norm, b_norm, a->size);
	if (a_norm != a_buf)
		free(a_norm);
	if (b_norm != b_buf)
		free(b_norm);
	return eq;
}

static bool vm_string_equal(const struct string *a, const struct string *b)
{
	return !strcmp(a->text, b->text)
		|| (sys4_text_is_gbk && vm_string_equal_gbk(a, b));
}

int vm_string_ref(struct string *s)
{
	int slot = heap_alloc_slot(VM_STRING);
	heap[slot].s = string_ref(s);
	return slot;
}

int vm_copy_page(int source)
{
	if (unlikely(!page_index_valid(source))) {
		WARNING("vm_copy_page on invalid page slot: %d", source);
		return -1;
	}
	struct page *page = heap_get_page(source);
	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, copy_page(page));
	heap_copy_delegate_targets(source, slot);
	return slot;
}

union vm_value vm_copy(union vm_value v, enum ain_data_type type)
{
	switch (type) {
	case AIN_STRING:
		return (union vm_value) { .i = vm_string_ref(heap_get_string(v.i)) };
	case AIN_STRUCT:
	case AIN_DELEGATE:
	case AIN_ARRAY_TYPE:
	case AIN_ARRAY:
		return (union vm_value) { .i = vm_copy_page(v.i) };
	case AIN_WRAP:
		if (!ain || ain->version < 14)
			return v;
		// fallthrough
	case AIN_REF_TYPE:
	case AIN_IFACE:
	case AIN_IFACE_WRAP:
	case AIN_REF_OPTION:
		heap_ref(v.i);
		return v;
	default:
		return v;
	}
}

static int get_function_by_name(const char *name)
{
	for (int i = 0; i < ain->nr_functions; i++) {
		if (!strcmp(name, ain->functions[i].name))
			return i;
	}
	return -1;
}

// Check whether a function's signature is compatible with a delegate type.
static bool function_matches_delegate(int dg_no, int fno)
{
	if (dg_no < 0 || dg_no >= ain->nr_delegates)
		return false;
	if (fno < 0 || fno >= ain->nr_functions)
		return false;
	struct ain_function_type *dg = &ain->delegates[dg_no];
	struct ain_function *f = &ain->functions[fno];

	if (dg->return_type.data != f->return_type.data ||
	    dg->return_type.struc != f->return_type.struc)
		return false;
	if (dg->nr_arguments != f->nr_args)
		return false;
	for (int i = 0; i < dg->nr_arguments; i++) {
		struct ain_type *a = &dg->variables[i].type;
		struct ain_type *b = &f->vars[i].type;
		if (a->data != b->data || a->struc != b->struc || a->rank != b->rank)
			return false;
	}
	return true;
}

static int scenario_label_addr(const char *lname)
{
	for (int i = 0; i < ain->nr_scenario_labels; i++) {
		if (!strcmp(ain->scenario_labels[i].name, lname)) {
			return ain->scenario_labels[i].address;
		}
	}
	VM_ERROR("Invalid scenario label: %s", display_sjis0(lname));
}

static int alloc_scenario_page(const char *fname)
{
	int fno, slot;
	struct ain_function *f;

	if ((fno = get_function_by_name(fname)) < 0)
		VM_ERROR("Invalid scenario function: %s", display_sjis0(fname));
	f = &ain->functions[fno];

	slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, alloc_page(LOCAL_PAGE, fno, f->nr_vars));
	for (int i = 0; i < f->nr_vars; i++) {
		int w = variable_init_typed(&heap[slot].page->values[i], &f->vars[i].type, false, false);
		i += w - 1;
	}
	return slot;
}

static void set_struct_page(int slot)
{
	call_stack[call_stack_ptr-1].struct_page = slot;
	call_stack[call_stack_ptr-1].has_struct_page = true;
	// Keep `this` alive during the call (from Rance9 onwards).
	if (slot >= 0 && AIN_VERSION_GTE(ain, 6, 1))
		heap_ref(slot);
}

static void unref_call_frame(struct function_call *frame)
{
	if (frame->struct_page >= 0 && AIN_VERSION_GTE(ain, 6, 1))
		heap_unref(frame->struct_page);
	heap_unref(frame->page_slot);
}

static void scenario_jump(int address)
{
	// flush call stack
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		unref_call_frame(&call_stack[i]);
	}
	call_stack_ptr = 0;
	instr_ptr = address;
	dg_callbegin_pending = false;
}

static void scenario_call(int slot)
{
	int fno = heap[slot].page->index;
	// flush call stack
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		unref_call_frame(&call_stack[i]);
	}
	call_stack[0] = (struct function_call) {
		.fno = fno,
		.call_address = instr_ptr,
		.return_address = VM_RETURN,
		.page_slot = slot,
		.struct_page = -1,
		.has_struct_page = true,
	};
	call_stack_ptr = 1;
	instr_ptr = ain->functions[fno].address;
	dg_callbegin_pending = false;
}

static int *lambda_parents;

static bool is_callback_binding_op(unsigned op, size_t address)
{
	if (op == DG_NEW_FROM_METHOD || op == DG_SET || op == DG_ADD ||
	    op == DG_EXIST || op == DG_ERASE || op == CALLFUNC2)
		return true;
	if (op == CALLHLL) {
		int lib = LittleEndian_getDW(ain->code, address + 2);
		int fn = LittleEndian_getDW(ain->code, address + 6);
		if (lib >= 0 && lib < ain->nr_libraries &&
		    fn >= 0 && fn < ain->libraries[lib].nr_functions) {
			struct ain_hll_function *hf = &ain->libraries[lib].functions[fn];
			if (hf->nr_arguments > 0) {
				enum ain_data_type t = hf->arguments[hf->nr_arguments - 1].type.data;
				return t == AIN_HLL_FUNC || t == AIN_HLL_FUNC_71;
			}
			return false;
		}
	}
	return false;
}

static void init_lambda_parents(void)
{
	free(lambda_parents);
	lambda_parents = NULL;
	if (ain->version < 11) return;
	lambda_parents = xmalloc(ain->nr_functions * sizeof(int));
	for (int i = 0; i < ain->nr_functions; i++) lambda_parents[i] = -1;
	int *nesting = xmalloc(ain->nr_functions * sizeof(int));
	int depth = 0;
	unsigned prev_op = 0;
	int prev_arg = -1;
	for (size_t address = 0; address < ain->code_size;) {
		unsigned op = LittleEndian_getW(ain->code, address) & ~OPTYPE_MASK;
		if (op >= NR_OPCODES || address + instruction_width(op) > ain->code_size)
			VM_ERROR("Invalid instruction while reading lexical scopes at 0x%zx", address);
		if (op == FUNC) {
			int fno = LittleEndian_getDW(ain->code, address + 2);
			if (fno < 0 || fno >= ain->nr_functions || depth >= ain->nr_functions)
				VM_ERROR("Invalid function scope at 0x%zx", address);
			if (ain->functions[fno].is_lambda && depth && lambda_parents[fno] < 0)
				lambda_parents[fno] = nesting[depth - 1];
			nesting[depth++] = fno;
		} else if (op == ENDFUNC && depth) {
			depth--;
		} else if (prev_op == PUSH && depth > 0 &&
			   prev_arg >= 0 && prev_arg < ain->nr_functions &&
			   ain->functions[prev_arg].is_lambda &&
			   is_callback_binding_op(op, address)) {
			// Nested expression lambdas may be emitted before their enclosing
			// outer lambda's FUNC opcode; bind them to the scope that creates
			// the delegate/callback reference.
			lambda_parents[prev_arg] = nesting[depth - 1];
		}
		prev_op = op;
		prev_arg = (op == PUSH) ? LittleEndian_getDW(ain->code, address + 2) : -1;
		address += instruction_width(op);
	}
	free(nesting);
}

int vm_capture_environment(int fno)
{
	if (ain->version < 11 || !ain->functions[fno].is_lambda || !call_stack_ptr)
		return -1;
	int parent = lambda_parents[fno];
	if (parent < 0) return call_stack[call_stack_ptr - 1].page_slot;
	// Sibling lambdas (including delegate calls made inside an Array callback)
	// capture the enclosing function, not the callback's argument page.
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		int slot = call_stack[i].page_slot;
		while (slot >= 0) {
			struct page *page = heap_get_page(slot);
			if (!page || page->type != LOCAL_PAGE) break;
			if (page->index == parent) return slot;
			slot = page->local.parent_page;
		}
	}
	VM_ERROR("Closure environment is no longer active: %s", display_sjis0(ain->functions[fno].name));
}

/*
 * System 4 calling convention:
 *   - caller pushes arguments, in order
 *   - CALLFUNC creates stack frame, pops arguments into local page
 *   - callee pushes return value on the stack
 *   - RETURN jumps to return address (saved in stack frame)
 */
static int _function_call(int fno, int return_address, int environment)
{
	if (fno < 0 || fno >= ain->nr_functions)
		VM_ERROR("Invalid function index: %d", fno);
	if (call_stack_ptr >= 4096)
		VM_ERROR("Function call stack overflow");
	struct ain_function *f = &ain->functions[fno];
	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, alloc_page(LOCAL_PAGE, fno, f->nr_vars));
	heap[slot].page->local.struct_ptr = -1;
	// A callback's object is its `this` binding, separate from lexical locals.
	heap[slot].page->local.parent_page = environment;
	heap_ref(environment);

	call_stack[call_stack_ptr++] = (struct function_call) {
		.fno = fno,
		.call_address = instr_ptr,
		.return_address = return_address,
		.page_slot = slot,
		.struct_page = -1,
		.has_struct_page = false,
	};
	// initialize local variables
	for (int i = f->nr_args; i < f->nr_vars; i++) {
		if (ain->version <= 1 && f->vars[i].type.data == AIN_STRUCT) {
			create_struct(f->vars[i].type.struc, &heap[slot].page->values[i]);
			continue;
		}
		int w = variable_init_typed(&heap[slot].page->values[i], &f->vars[i].type, false, false);
		i += w - 1;
	}
	// jump to function start
	instr_ptr = ain->functions[fno].address;

	return slot;
}

static void function_call(int fno, int return_address)
{
	int slot = _function_call(fno, return_address, vm_capture_environment(fno));

	// pop arguments, store in local page
	struct ain_function *f = &ain->functions[fno];
	for (int i = f->nr_args - 1; i >= 0; i--) {
		heap[slot].page->values[i] = stack_pop();
		switch (f->vars[i].type.data) {
		case AIN_WRAP:
			if (!ain || ain->version < 14)
				break;
			// fallthrough
		case AIN_REF_TYPE:
		case AIN_IFACE:
		case AIN_IFACE_WRAP:
		case AIN_REF_OPTION:
			heap_ref(heap[slot].page->values[i].i);
			break;
		case AIN_OPTION: {
			int w = ain_type_slot_width(&f->vars[i].type);
			if (i + w - 1 < f->nr_vars && heap[slot].page->values[i + w - 1].i == 0
					&& ain_option_inner_is_ref_heap(f->vars[i].type.array_type)) {
				heap_ref(heap[slot].page->values[i].i);
			}
			break;
		}
		default:
			break;
		}
	}
}

static void method_call(int fno, int return_address)
{
	function_call(fno, return_address);
	int struct_page = stack_pop().i;
	set_struct_page(struct_page);
	heap[call_stack[call_stack_ptr-1].page_slot].page->local.struct_ptr = struct_page;
	if (game_rance01 || game_rance02 || game_rance03 || game_rance8 || game_rance9)
		modifier_before_method();
}

static void vm_execute(void);

static int delegate_return_width(const struct ain_function_type *dg)
{
	return ain_return_type_width(&dg->return_type);
}

static void delegate_call(int dg_no, int return_address)
{
	if (dg_no < 0 || dg_no >= ain->nr_delegates)
		VM_ERROR("Invalid delegate index");

	bool first_call = dg_callbegin_pending;
	dg_callbegin_pending = false;

	// stack: [arg0, ..., dg_page] on first call, or [arg0, ..., dg_page, return_value] after RETURN
	struct ain_function_type *dg_type = &ain->delegates[dg_no];
	int return_values = delegate_return_width(dg_type);
	int dg_page = stack_peek(first_call ? 0 : return_values).i;
	struct page *page = page_index_valid(dg_page) ? heap_get_delegate_page(dg_page) : NULL;
	int dg_index = page ? page->delegate.cursor : 0;
	int obj, fun, environment;
	if (page && dg_index < page->delegate.end && delegate_get(page, dg_index, &obj, &fun, &environment)) {
		if (!first_call) {
			union vm_value previous[4] = {0};
			for (int i = return_values - 1; i >= 0; i--) previous[i] = stack_pop();
			// The original VM does not run destructors for replaced multicast
			// results. Release their storage without invoking script callbacks.
			if (return_values)
				elem_value_fini(previous, &dg_type->return_type, false);
		}
		page->delegate.cursor = dg_index + 1;

		int slot = _function_call(fun, instr_ptr + instruction_width(DG_CALL), environment);

		// copy arguments into local page
		struct ain_function_type *dg = &ain->delegates[dg_no];
		int args_base = stack_ptr - 1 - dg->nr_arguments;
		for (int i = 0; i < dg->nr_arguments; i++) {
			if (dg->variables[i].type.data == AIN_VOID) {
				heap[slot].page->values[i] = stack[args_base + i];
				continue;
			}
			int w = elem_value_copy(&heap[slot].page->values[i], &stack[args_base + i], &dg->variables[i].type);
			i += w - 1;
		}

		set_struct_page(obj);
		heap[slot].page->local.struct_ptr = obj;
	} else {
		// call finished: clean up stack and jump to return address
		union vm_value r[4] = {0};
		if (first_call) {
			if (dg_type->return_type.data == AIN_OPTION) {
				r[0] = vm_int(-1);
				if (return_values == 3) r[1] = vm_int(0);
				r[return_values - 1] = vm_int(1);
			} else {
				for (int i = 0; i < return_values; i++) {
					bool is_null = !i && ain_type_is_null_handle(dg_type->return_type.data);
					r[i] = vm_int(is_null ? -1 : 0);
				}
			}
		} else {
			for (int i = return_values - 1; i >= 0; i--) r[i] = stack_pop();
		}
		stack_pop(); // dg_page
		int nvars = dg_type->nr_variables;
		int args_base = stack_ptr - nvars;
		for (int i = 0; i < nvars; i++) {
			const struct ain_type *vt = &dg_type->variables[i].type;
			switch (vt->data) {
			case AIN_VOID:
			case AIN_REF_TYPE:
			case AIN_IFACE:
			case AIN_IFACE_WRAP:
			case AIN_WRAP:
			case AIN_REF_OPTION:
				// References, wraps, and interface arguments are borrowed at the
				// call site. Each invoked function owns its copied reference only.
				break;
			case AIN_OPTION: {
				int w = ain_type_slot_width(vt);
				if (!ain_option_inner_is_ref_heap(vt->array_type))
					elem_value_fini(&stack[args_base + i], vt, true);
				i += w - 1;
				break;
			}
			default:
				variable_fini(stack[args_base + i], vt->data, true);
				break;
			}
		}
		stack_ptr -= nvars;
		for (int i = 0; i < return_values; i++) stack_push(r[i]);
		instr_ptr = return_address;
	}
}

void vm_call(int fno, int struct_page)
{
	size_t saved_ip = instr_ptr;
	function_call(fno, VM_RETURN);
	set_struct_page(struct_page);
	local_page()->local.struct_ptr = struct_page;
	vm_execute();
	instr_ptr = saved_ip;
}

void vm_call_method_1(int fno, int struct_page, int value)
{
	size_t saved_ip = instr_ptr;
	stack_push(struct_page);
	stack_push(value);
	method_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = saved_ip;
}

static void function_return(void)
{
	if (game_rance01 || game_rance02 || game_rance03 || game_rance8 || game_rance9)
		modifier_after_method_return();
	unref_call_frame(&call_stack[call_stack_ptr-1]);
	instr_ptr = call_stack[call_stack_ptr-1].return_address;
	call_stack_ptr--;
}

static const SDL_MessageBoxButtonData ok_cancel_buttons[] = {
	{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "OK" },
	{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel" },
};

static const SDL_MessageBoxButtonData stop_continue_buttons[] = {
	{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Stop" },
	{ 0, 0, "Continue" },
};

static struct string *get_func_stack_name(int index)
{
	int i = call_stack_ptr - (1 + index);
	if (i < 0 || i >= call_stack_ptr) {
		return string_ref(&EMPTY_STRING);
	}
	struct function_call *call = &call_stack[i];
	struct ain_function *fun = &ain->functions[call->fno];
	return cstr_to_string(fun->name);
}

void vm_system_call(int code)
{
	switch (code) {
	case SYS_EXIT: {// system.Exit(int nResult)
		int code = stack_pop().i;
		vm_exit(code);
		break;
	}
	case SYS_GLOBAL_SAVE: { // system.GlobalSave(string szKeyName, string szFileName)
		int filename = stack_pop().i;
		int keyname = stack_pop().i;
		stack_push(save_globals(heap_get_string(keyname)->text, heap_get_string(filename)->text, NULL, NULL));
		heap_unref(filename);
		heap_unref(keyname);
		break;
	}
	case SYS_GLOBAL_LOAD: { // system.GlobalLoad(string szKeyName, string szFileName)
		int filename = stack_pop().i;
		int keyname = stack_pop().i;
		stack_push(load_globals(heap_get_string(keyname)->text, heap_get_string(filename)->text, NULL, NULL));
		heap_unref(filename);
		heap_unref(keyname);
		break;
	}
	case SYS_LOCK_PEEK: // system.LockPeek(void)
	case SYS_UNLOCK_PEEK: {// system.UnlockPeek(void)
		stack_push(1);
		break;
	}
	case SYS_RESET: {
		vm_reset();
		break;
	}
	case SYS_OUTPUT: {// system.Output(string szText)
		struct string *str = stack_peek_string(0);
		log_message("stdout", "%s", display_sjis0(str->text));
		// XXX: caller S_POPs
		break;
	}
	case SYS_MSGBOX: {
		struct string *str = stack_peek_string(0);
		char *utf = cjk_dialog_text_to_utf8(str->text, str->size);
		SDL_ShowSimpleMessageBox(0, "xsystem4", utf, NULL);
		free(utf);
		// XXX: caller S_POPs
		break;
	}
	case SYS_MSGBOX_OK_CANCEL: {
		int result = 0;
		struct string *str = stack_peek_string(0);
		char *utf = cjk_dialog_text_to_utf8(str->text, str->size);

		const SDL_MessageBoxData mbox = {
			SDL_MESSAGEBOX_INFORMATION,
			NULL,
			"xsystem4",
			utf,
			SDL_arraysize(ok_cancel_buttons),
			ok_cancel_buttons,
			NULL
		};
		if (SDL_ShowMessageBox(&mbox, &result)) {
			WARNING("Error displaying message box");
		}
		free(utf);
		heap_unref(stack_pop().i);
		stack_push(result);
		break;
	}
	case SYS_RESUME_SAVE: {
		union vm_value *success = stack_pop_var();
		int filename_slot = stack_pop().i;
		int keyname_slot = stack_pop().i;
		struct string *filename = string_ref(heap_get_string(filename_slot));
		struct string *keyname = string_ref(heap_get_string(keyname_slot));
		heap_unref(filename_slot);
		heap_unref(keyname_slot);
		success->i = vm_save_image(keyname->text, filename->text);
		free_string(filename);
		free_string(keyname);
		stack_push(1);
		break;
	}
	case SYS_RESUME_LOAD: {
		int filename_slot = stack_pop().i;
		int key_slot = stack_pop().i;
		struct string *filename = string_ref(heap_get_string(filename_slot));
		struct string *key = string_ref(heap_get_string(key_slot));
		vm_load_image(key->text, filename->text);
		free_string(filename);
		free_string(key);
		stack_push(0);
		break;
	}
	case SYS_EXISTS_FILE: { // system.ExistsFile(string szFileName)
		int str = stack_pop().i;
		char *path = gamedir_path(heap_get_string(str)->text);
		int len = strlen(path);
		// Return true for directories, but false if the path ends with a directory separator.
		if (len > 0 && (path[len-1] == '/' || path[len-1] == '\\')) {
			stack_push(0);
		} else {
			stack_push(file_exists(path));
		}
		heap_unref(str);
		free(path);
		break;
	}
	case SYS_OPEN_WEB: {
#if !SDL_VERSION_ATLEAST(2, 0, 14)
		WARNING("SDL_OpenURL not available");
#else
		struct string *url = stack_peek_string(0);
		if (SDL_OpenURL(url->text) < 0) {
			WARNING("SDL_OpenURL failed: %s", SDL_GetError());
		}
#endif
		heap_unref(stack_pop().i);
		break;
	};
	case SYS_GET_SAVE_FOLDER_NAME: {// system.GetSaveFolderName(void)
		if (config.save_dir) {
			char *sjis = utf2filename_cjk(config.save_dir, strlen(config.save_dir));
			stack_push_string(make_string(sjis, strlen(sjis)));
			free(sjis);
		} else {
			stack_push_string(string_ref(&EMPTY_STRING));
		}
		break;
	}
	case SYS_GET_TIME: {// system.GetTime(void)
		stack_push(vm_time());
		break;
	}
	case SYS_GET_GAME_NAME: {// system.GetGameName(void)
		stack_push_string(make_string(config.game_name, strlen(config.game_name)));
		break;
	}
	case SYS_ERROR: {// system.Error(string szText)
		int result = 0;
		struct string *str = stack_peek_string(0);
		char *utf = cjk_dialog_text_to_utf8(str->text, str->size);
		sys_warning("*GAME ERROR*: %s\n", utf);
		const SDL_MessageBoxData mbox = {
			SDL_MESSAGEBOX_ERROR,
			NULL,
			"Game Error - xsystem4",
			utf,
			SDL_arraysize(stop_continue_buttons),
			stop_continue_buttons,
			NULL
		};
		if (SDL_ShowMessageBox(&mbox, &result)) {
			WARNING("Error displaying message box");
		}
		free(utf);
		if (result == 1) {
			// stop execution
			vm_exit(1);
		}
		// XXX: caller S_POPs
		break;
	}
	case SYS_EXISTS_SAVE_FILE: {
		int slot = stack_pop().i;
		stack_push(savedir_file_exists(heap_get_string(slot)->text));
		heap_unref(slot);
		break;
	}
	case SYS_IS_DEBUG_MODE: {// system.IsDebugMode(void)
		stack_push(0);
		break;
	}
	case SYS_GET_FUNC_STACK_NAME: { // system.GetFuncStackName(int nIndex)
		stack_push_string(get_func_stack_name(stack_pop().i));
		break;
	}
	case SYS_PEEK: {// system.Peek(void)
		handle_events();
		break;
	}
	case SYS_SLEEP: {// system.Sleep(int nSleep)
		int ms = stack_pop().i;
		vm_sleep(ms);
		break;
	}
	case SYS_RESUME_READ_COMMENT: {// system.ResumeReadComment(string szKeyName, string szFileName, ref array@string aszComment)
		int success = 0;
		int comment = stack_pop().i;
		int filename = stack_pop().i;
		int keyname = stack_pop().i;
		struct page *loaded = vm_load_image_comments(heap_get_string(keyname)->text,
							     heap_get_string(filename)->text,
							     &success);
		if (page_index_valid(comment)) {
			if (heap[comment].page)
				delete_page(comment);
			heap_set_page(comment, loaded);
		} else if (loaded) {
			delete_page_vars(loaded);
			free_page(loaded);
		}
		heap_unref(filename);
		heap_unref(keyname);
		stack_push(success);
		break;
	}
	case SYS_RESUME_WRITE_COMMENT: { // system.ResumeWriteComment(string szKeyName, string szFileName, ref array@string aszComment)
		int comment = stack_pop().i;
		int filename = stack_pop().i;
		int keyname = stack_pop().i;
		stack_push(vm_write_image_comments(heap_get_string(keyname)->text,
						   heap_get_string(filename)->text,
						   page_index_valid(comment) ? heap_get_page(comment) : NULL));
		heap_unref(filename);
		heap_unref(keyname);
		break;
	}
	case SYS_GROUP_SAVE: { // system.GroupSave(string szKeyName, string szFileName, string szGroupName, ref int nNumofLoad)
		union vm_value *n = stack_pop_var();
		int groupname = stack_pop().i;
		int filename = stack_pop().i;
		int keyname = stack_pop().i;
		stack_push(save_globals(heap_get_string(keyname)->text,
				      heap_get_string(filename)->text,
				      heap_get_string(groupname)->text,
				      &n->i));
		heap_unref(groupname);
		heap_unref(filename);
		heap_unref(keyname);
		break;
	}
	case SYS_GROUP_LOAD: { // system.GroupLoad(string szKeyName, string szFileName, string szGroupName, ref int nNumofLoad)
		union vm_value *n = stack_pop_var();
		int groupname = stack_pop().i;
		int filename = stack_pop().i;
		int keyname = stack_pop().i;
		stack_push(load_globals(heap_get_string(keyname)->text,
					heap_get_string(filename)->text,
					heap_get_string(groupname)->text,
					&n->i));
		heap_unref(groupname);
		heap_unref(filename);
		heap_unref(keyname);
		break;
	}
	case SYS_DELETE_SAVE_FILE: { // system.DeleteSaveFile(string szFileName)
		int filename = stack_pop().i;
		stack_push(delete_save_file(heap_get_string(filename)->text));
		heap_unref(filename);
		break;
	}
	case SYS_EXIST_FUNC: { // system.ExistFunc(string szFuncName)
		int funcname = stack_pop().i;
		stack_push(ain_get_function(ain, heap_get_string(funcname)->text) > 0);
		heap_unref(funcname);
		break;
	}
	case SYS_COPY_SAVE_FILE: { // system.CopySaveFile(string szDestFileName, string szSourceFileName)
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
		break;
	}
	default:
		// xsystem4-specific system calls (used for hacks)
		switch ((enum vm_extra_syscall)code) {
		case VM_XSYS_KEY_IS_DOWN:
			stack_push(key_is_down(stack_pop().i));
			break;
		case VM_XSYS_FIT_RANCE03_MESSAGE:
			message_layout_fit_rance03(stack_pop().i);
			break;
		default:
			VM_ERROR("Unimplemented syscall: 0x%X", code);
		}
	}
}

uint32_t get_switch_address(int no, int val)
{
	struct ain_switch *s = &ain->switches[no];
	for (int i = 0; i < s->nr_cases; i++) {
		if (s->cases[i].value == val) {
			return s->cases[i].address;
		}
	}
	if (s->default_address > 0)
		return s->default_address;
	else
		return instr_ptr + instruction_width(SWITCH);
}

uint32_t get_strswitch_address(int no, struct string *str)
{
	struct ain_switch *s = &ain->switches[no];
	for (int i = 0; i < s->nr_cases; i++) {
		if (!strcmp(str->text, ain->strings[s->cases[i].value]->text)) {
			return s->cases[i].address;
		}
	}
	if (sys4_text_is_gbk) {
		for (int i = 0; i < s->nr_cases; i++) {
			if (vm_string_equal_gbk(str, ain->strings[s->cases[i].value]))
				return s->cases[i].address;
		}
	}
	if (s->default_address > 0)
		return s->default_address;
	else
		return instr_ptr + instruction_width(STRSWITCH);
}

static void echo_message(int i)
{
	NOTICE("MSG %d: %s", i, display_sjis0(ain->messages[i]->text));
}

static enum opcode execute_instruction(enum opcode opcode)
{
	switch (opcode) {
	//
	// --- Stack Management ---
	//
	case PUSH: {
		stack_push(get_argument(0));
		break;
	}
	case POP: {
		stack_pop();
		break;
	}
	case F_PUSH: {
		stack_push(get_argument_float(0));
		break;
	}
	case REF: {
		// Dereference a reference to a value.
		stack_push(stack_pop_var()->i);
		break;
	}
	case REFREF: {
		// Dereference a reference to a reference.
		union vm_value *ref = stack_pop_var();
		stack_push(ref[0].i);
		stack_push(ref[1].i);
		break;
	}
	case DUP: {
		// A -> AA
		stack_push(stack_peek(0).i);
		break;
	}
	case DUP2: {
		// AB -> ABAB
		int a = stack_peek(1).i;
		int b = stack_peek(0).i;
		stack_push(a);
		stack_push(b);
		break;
	}
	case DUP_X2: {
		// ABC -> CABC
		int a = stack_peek(2).i;
		int b = stack_peek(1).i;
		int c = stack_peek(0).i;
		stack_set(2, c);
		stack_set(1, a);
		stack_set(0, b);
		stack_push(c);
		break;
	}
	case DUP2_X1: {
		// ABC -> BCABC
		int a = stack_peek(2).i;
		int b = stack_peek(1).i;
		int c = stack_peek(0).i;
		stack_set(2, b);
		stack_set(1, c);
		stack_set(0, a);
		stack_push(b);
		stack_push(c);
		break;
	}
	case DUP_U2: {
		// AB -> ABA
		stack_push(stack_peek(1).i);
		break;
	}
	case SWAP: {
		int a = stack_peek(1).i;
		stack_set(1, stack_peek(0));
		stack_set(0, a);
		break;
	}
	//
	// --- Variables ---
	//
	case PUSHGLOBALPAGE: {
		stack_push(0);
		break;
	}
	case PUSHLOCALPAGE: {
		stack_push(local_page_slot());
		break;
	}
	case PUSHSTRUCTPAGE: {
		stack_push(struct_page_slot());
		break;
	}
	case ASSIGN:
	case F_ASSIGN: {
		union vm_value val = stack_pop();
		stack_pop_var()[0] = val;
		stack_push(val);
		break;
	}
	case SH_GLOBALREF: { // VARNO
		stack_push(global_get(get_argument(0)).i);
		break;
	}
	case SH_LOCALREF: { // VARNO
		stack_push(local_get(get_argument(0)).i);
		break;
	}
	case _EOF: // In Ain v0, opcode 0x62 is not EOF but SH_STRUCTREF
		if (ain->version != 0)
			VM_ERROR("Illegal opcode: 0x%04x", opcode);
		// fallthrough
	case SH_STRUCTREF: { // VARNO
		stack_push(member_get(get_argument(0)));
		break;
	}
	case SH_LOCALASSIGN: { // VARNO, VALUE
		local_set(get_argument(0), get_argument(1));
		break;
	}
	case SH_LOCALINC: { // VARNO
		int varno = get_argument(0);
		local_set(varno, local_get(varno).i+1);
		break;
	}
	case SH_LOCALDEC: { // VARNO
		int varno = get_argument(0);
		local_set(varno, local_get(varno).i-1);
		break;
	}
	case SH_LOCALDELETE: {
		int slot = local_get(get_argument(0)).i;
		if (slot != -1) {
			heap_unref(slot);
			local_set(get_argument(0), -1);
		}
		break;
	}
	case SH_LOCALCREATE: { // VARNO, STRUCTNO
		create_struct(get_argument(1), local_ptr(get_argument(0)));
		break;
	}
	case R_ASSIGN: {
		int src_var = stack_pop().i;
		int src_page = stack_pop().i;
		int dst_var = stack_pop().i;
		struct page *dst = heap_get_page(stack_pop().i);
		page_set_var(dst, dst_var, src_page);
		page_set_var(dst, dst_var+1, src_var);
		stack_push(src_page);
		stack_push(src_var);
		break;
	}
	case R_EQUALE: {
		int rhs_var = stack_pop().i;
		int rhs_page = stack_pop().i;
		int lhs_var = stack_pop().i;
		int lhs_page = stack_pop().i;
		stack_push(lhs_page == rhs_page && lhs_var == rhs_var ? 1 : 0);
		break;
	}
	case R_NOTE: {
		int rhs_var = stack_pop().i;
		int rhs_page = stack_pop().i;
		int lhs_var = stack_pop().i;
		int lhs_page = stack_pop().i;
		stack_push(lhs_page == rhs_page && lhs_var == rhs_var ? 0 : 1);
		break;
	}
	case NEW: {
		union vm_value v;
		if (ain->version >= 11) {
			int type = get_argument(0);
			int constructor = get_argument(1);
			if (type < 0 || type >= ain->nr_structures)
				VM_ERROR("Invalid NEW structure: %d", type);
			v.i = alloc_struct(type);
			if (constructor < 0) {
				init_struct(type, v.i);
			} else {
				if (ain->version < 14) {
					struct ain_struct *s = &ain->structures[type];
					for (int i = 0; i < s->nr_members; i++) {
						if (s->members[i].type.data == AIN_STRUCT)
							init_struct(s->members[i].type.struc, heap_get_page(v.i)->values[i].i);
					}
				}
				// Run the constructor in the current VM loop so that its frame
				// can be saved and restored by resume saves.
				function_call(constructor, instr_ptr + instruction_width(NEW));
				set_struct_page(v.i);
				local_page()->local.struct_ptr = v.i;
				stack_push(v);
				return CALLFUNC;
			}
		} else {
			create_struct(stack_pop().i, &v);
		}
		stack_push(v);
		break;
	}
	case DELETE:
	case CHECKUDO: {
		int slot = stack_pop().i;
		if (slot != -1)
			heap_unref(slot);
		break;
	}
	case SP_INC: {
		heap_ref(stack_pop().i);
		break;
	}
	case OBJSWAP: {
		if (ain->version < 11)
			stack_pop(); // type is immediate in v11+
		union vm_value *b = stack_pop_var();
		union vm_value *a = stack_pop_var();
		union vm_value tmp = *a;
		*a = *b;
		*b = tmp;
		break;
	}
	//
	// --- Control Flow ---
	//
	case CALLFUNC: {
		function_call(get_argument(0), instr_ptr + instruction_width(CALLFUNC));
		if (game_rance02 || game_rance8 || game_rance9)
			modifier_before_method();
		break;
	}
	case CALLFUNC2: {
		stack_pop(); // function-type index (only needed for compilation)
		function_call(stack_pop().i, instr_ptr + instruction_width(CALLFUNC2));
		break;
	}
	case CALLMETHOD: {
		int fno = get_argument(0);
		if (ain->version >= 11) {
			int argc = get_argument(0);
			if (argc < 0 || argc + 2 > stack_ptr)
				VM_ERROR("Invalid CALLMETHOD argument count: %d", argc);
			int pos = stack_ptr - argc - 1;
			fno = stack[pos].i;
			memmove(stack + pos, stack + pos + 1, argc * sizeof(*stack));
			stack_ptr--;
		}
		method_call(fno, instr_ptr + instruction_width(CALLMETHOD));
		break;
	}
	case CALLHLL: {
		int lib = get_argument(0);
		int fn = get_argument(1);
		int type_param = ain->version >= 11 ? get_argument(2) : 0;
		if (!v12_hll_call(lib, fn, type_param))
			hll_call(lib, fn);
		break;
	}
	case RETURN: {
		function_return();
		break;
	}
	case CALLSYS: {
		vm_system_call(get_argument(0));
		break;
	}
	case CALLONJUMP: {
		int str = stack_pop().i;
		if (ain->scenario_labels) {
			stack_push(scenario_label_addr(heap_get_string(str)->text));
		} else {
			// XXX: I am GUESSING that the VM pre-allocates the scenario function's
			//      local page here. It certainly pushes what appears to be a page
			//      index to the stack.
			stack_push(alloc_scenario_page(heap_get_string(str)->text));
		}
		heap_unref(str);
		break;
	}
	case SJUMP: {
		if (ain->scenario_labels) {
			scenario_jump(stack_pop().i);
		} else {
			scenario_call(stack_pop().i);
		}
		break;
	}
	case _MSG: {
		if ((unsigned)get_argument(0) >= ain->nr_messages)
			VM_ERROR("Invalid message index: %d", get_argument(0));
		if (config.echo)
			echo_message(get_argument(0));
		if (ain->msgf < 0)
			break;
		if (ain->msgf >= ain->nr_functions || !ain->functions[ain->msgf].address)
			VM_ERROR("Invalid message handler: %d", ain->msgf);
		stack_push(get_argument(0));
		stack_push(ain->nr_messages);
		stack_push_string(string_ref(ain->messages[get_argument(0)]));
		function_call(ain->msgf, instr_ptr + instruction_width(_MSG));
		break;
	}
	case JUMP: { // ADDR
		instr_ptr = get_argument(0);
		break;
	}
	case IFZ: { // ADDR
		if (!stack_pop().i)
			instr_ptr = get_argument(0);
		else
			instr_ptr += instruction_width(IFZ);
		break;
	}
	case IFNZ: { // ADDR
		if (stack_pop().i)
			instr_ptr = get_argument(0);
		else
			instr_ptr += instruction_width(IFNZ);
		break;
	}
	case SWITCH: {
		instr_ptr = get_switch_address(get_argument(0), stack_pop().i);
		break;
	}
	case STRSWITCH: {
		int str = stack_pop().i;
		instr_ptr = get_strswitch_address(get_argument(0), heap_get_string(str));
		heap_unref(str);
		break;
	}
	case ASSERT: {
		int line = stack_pop().i; // line number
		int file = stack_pop().i; // filename
		int expr = stack_pop().i; // expression
		if (!stack_pop().i) {
			sys_message("Assertion failed at %s:%d: %s\n",
					display_sjis0(heap_get_string(file)->text),
					line,
					display_sjis1(heap_get_string(expr)->text));
			vm_exit(1);
		}
		heap_unref(file);
		heap_unref(expr);
		break;
	}
	//
	// --- Arithmetic ---
	//
	case INV: {
		stack[stack_ptr-1].i = (int32_t)-(uint32_t)stack[stack_ptr-1].i;
		break;
	}
	case NOT: {
		stack[stack_ptr-1].i = !stack[stack_ptr-1].i;
		break;
	}
	case COMPL: {
		stack[stack_ptr-1].i = ~stack[stack_ptr-1].i;
		break;
	}
	case ADD: {
		stack[stack_ptr-2].i = (int32_t)((uint32_t)stack[stack_ptr-2].i + (uint32_t)stack[stack_ptr-1].i);
		stack_ptr--;
		break;
	}
	case SUB: {
		stack[stack_ptr-2].i = (int32_t)((uint32_t)stack[stack_ptr-2].i - (uint32_t)stack[stack_ptr-1].i);
		stack_ptr--;
		break;
	}
	case MUL: {
		stack[stack_ptr-2].i = (int32_t)((uint32_t)stack[stack_ptr-2].i * (uint32_t)stack[stack_ptr-1].i);
		stack_ptr--;
		break;
	}
	case DIV: {
		int32_t a = stack[stack_ptr-2].i;
		int32_t b = stack[stack_ptr-1].i;
		if (!b) {
			stack[stack_ptr-2].i = 0;
		} else if (a == INT32_MIN && b == -1) {
			stack[stack_ptr-2].i = INT32_MIN;
		} else {
			stack[stack_ptr-2].i = a / b;
		}
		stack_ptr--;
		break;
	}
	case MOD: {
		int32_t a = stack[stack_ptr-2].i;
		int32_t b = stack[stack_ptr-1].i;
		if (!b || (a == INT32_MIN && b == -1)) {
			stack[stack_ptr-2].i = 0;
		} else {
			stack[stack_ptr-2].i = a % b;
		}
		stack_ptr--;
		break;
	}
	case AND: {
		stack[stack_ptr-2].i &= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case OR: {
		stack[stack_ptr-2].i |= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case XOR: {
		stack[stack_ptr-2].i ^= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case LSHIFT: {
		int32_t sh = stack[stack_ptr-1].i;
		if (sh < 0 || sh >= 32) {
			stack[stack_ptr-2].i = 0;
		} else {
			stack[stack_ptr-2].i = (int32_t)((uint32_t)stack[stack_ptr-2].i << sh);
		}
		stack_ptr--;
		break;
	}
	case RSHIFT: {
		int32_t sh = stack[stack_ptr-1].i;
		if (sh < 0 || sh >= 32) {
			stack[stack_ptr-2].i = (stack[stack_ptr-2].i < 0 && sh >= 32) ? -1 : 0;
		} else {
			stack[stack_ptr-2].i >>= sh;
		}
		stack_ptr--;
		break;
	}
	// Numeric Comparisons
	case LT: {
		int32_t b = stack_pop().i;
		int32_t a = stack_pop().i;
		stack_push(a < b ? 1 : 0);
		break;
	}
	case GT: {
		int32_t b = stack_pop().i;
		int32_t a = stack_pop().i;
		stack_push(a > b ? 1 : 0);
		break;
	}
	case LTE: {
		int32_t b = stack_pop().i;
		int32_t a = stack_pop().i;
		stack_push(a <= b ? 1 : 0);
		break;
	}
	case GTE: {
		int32_t b = stack_pop().i;
		int32_t a = stack_pop().i;
		stack_push(a >= b ? 1 : 0);
		break;
	}
	case NOTE: {
		int32_t b = stack_pop().i;
		int32_t a = stack_pop().i;
		stack_push(a != b ? 1 : 0);
		break;
	}
	case EQUALE: {
		int32_t b = stack_pop().i;
		int32_t a = stack_pop().i;
		stack_push(a == b ? 1 : 0);
		break;
	}
	// +=, -=, etc.
	case PLUSA: {
		int32_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		v->i = (int32_t)((uint32_t)v->i + (uint32_t)n);
		stack_push(v->i);
		break;
	}
	case MINUSA: {
		int32_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		v->i = (int32_t)((uint32_t)v->i - (uint32_t)n);
		stack_push(v->i);
		break;
	}
	case MULA: {
		int32_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		v->i = (int32_t)((uint32_t)v->i * (uint32_t)n);
		stack_push(v->i);
		break;
	}
	case DIVA: {
		int32_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		if (!n) {
			stack_push(0);
		} else if (v->i == INT32_MIN && n == -1) {
			v->i = INT32_MIN;
			stack_push(v->i);
		} else {
			stack_push(v->i /= n);
		}
		break;
	}
	case MODA: {
		int32_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		if (!n) {
			stack_push(0);
		} else if (v->i == INT32_MIN && n == -1) {
			v->i = 0;
			stack_push(0);
		} else {
			stack_push(v->i %= n);
		}
		break;
	}
	case ANDA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i &= n);
		break;
	}
	case ORA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i |= n);
		break;
	}
	case XORA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i ^= n);
		break;
	}
	case LSHIFTA: {
		int32_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		if (n < 0 || n >= 32) {
			v->i = 0;
		} else {
			v->i = (int32_t)((uint32_t)v->i << n);
		}
		stack_push(v->i);
		break;
	}
	case RSHIFTA: {
		int32_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		if (n < 0 || n >= 32) {
			v->i = (v->i < 0 && n >= 32) ? -1 : 0;
		} else {
			v->i >>= n;
		}
		stack_push(v->i);
		break;
	}
	case INC: {
		union vm_value *v = stack_pop_var();
		v->i = (int32_t)((uint32_t)v->i + 1u);
		break;
	}
	case DEC: {
		union vm_value *value = stack_pop_var();
		if (!game_rance03 || !modifier_keep_dec_item(value))
			value->i = (int32_t)((uint32_t)value->i - 1u);
		break;
	}
	case ITOB: {
		stack_set(0, !!stack_peek(0).i);
		break;
	}
	//
	// --- 64-bit integers ---
	//
	case ITOLI: {
		stack_set(0, lint_clamp(stack_peek(0).i));
		break;
	}
	case LI_ADD: {
		int64_t a = stack[stack_ptr-2].i;
		int64_t b = stack[stack_ptr-1].i;
		stack[stack_ptr-2].i = lint_clamp(a + b);
		stack_ptr--;
		break;
	}
	case LI_SUB: {
		int64_t a = stack[stack_ptr-2].i;
		int64_t b = stack[stack_ptr-1].i;
		stack[stack_ptr-2].i = lint_clamp(a - b);
		stack_ptr--;
		break;
	}
	case LI_MUL: {
		int64_t a = stack[stack_ptr-2].i;
		int64_t b = stack[stack_ptr-1].i;
		stack[stack_ptr-2].i = lint_clamp(a * b);
		stack_ptr--;
		break;
	}
	case LI_DIV: {
		int64_t a = stack[stack_ptr-2].i;
		int64_t b = stack[stack_ptr-1].i;
		stack[stack_ptr-2].i = b ? lint_clamp(a / b) : 0;
		stack_ptr--;
		break;
	}
	case LI_MOD: {
		int64_t a = stack[stack_ptr-2].i;
		int64_t b = stack[stack_ptr-1].i;
		stack[stack_ptr-2].i = b ? lint_clamp(a % b) : 0;
		stack_ptr--;
		break;
	}
	case LI_ASSIGN: {
		int64_t v = stack_pop().i;
		stack_push(stack_pop_var()->i = lint_clamp(v));
		break;
	}
	case LI_PLUSA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i + n));
		break;
	}
	case LI_MINUSA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i - n));
		break;
	}
	case LI_MULA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i * n));
		break;
	}
	case LI_DIVA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = (n ? lint_clamp((int64_t)v->i / n) : 0));
		break;
	}
	case LI_MODA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = (n ? lint_clamp((int64_t)v->i % n) : 0));
		break;
	}
	case LI_ANDA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i & n));
		break;
	}
	case LI_ORA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i | n));
		break;
	}
	case LI_XORA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i ^ n));
		break;
	}
	case LI_LSHIFTA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		int64_t res = (n < 0 || n >= 64) ? 0 : (int64_t)((uint64_t)(int64_t)v->i << n);
		stack_push(v->i = lint_clamp(res));
		break;
	}
	case LI_RSHIFTA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		int64_t res = (n < 0 || n >= 64) ? ((v->i < 0 && n >= 64) ? -1 : 0) : ((int64_t)v->i >> n);
		stack_push(v->i = lint_clamp(res));
		break;
	}
	case LI_INC: {
		union vm_value *v = stack_pop_var();
		v->i = lint_clamp((int64_t)v->i + (int64_t)1);
		break;
	}
	case LI_DEC: {
		union vm_value *v = stack_pop_var();
		v->i = lint_clamp((int64_t)v->i - (int64_t)1);
		break;
	}
	//
	// --- Floating Point Arithmetic ---
	//
	case FTOI: {
		float f = stack_peek(0).f;
		int32_t res;
		if (f != f) {
			res = 0;
		} else if (f >= 2147483648.0f) {
			res = INT32_MAX;
		} else if (f < -2147483648.0f) {
			res = INT32_MIN;
		} else {
			res = (int32_t)f;
		}
		stack_set(0, res);
		break;
	}
	case ITOF: {
		stack_set(0, (float)stack_peek(0).i);
		break;
	}
	case F_INV: {
		stack_set(0, -stack_peek(0).f);
		break;
	}
	case F_ADD: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f + f);
		break;
	}
	case F_SUB: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f - f);
		break;
	}
	case F_MUL: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f * f);
		break;
	}
	case F_DIV: {
		float f = stack_pop().f;
		if (unlikely(f == 0.0f && stack_peek(0).f == 0.0f) && vm_zero_vector_div_guard())
			f = 1.0f;
		stack_set(0, stack_peek(0).f / f);
		break;
	}
	// floating point comparison
	case F_LT: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f < f ? 1 : 0);
		break;
	}
	case F_GT: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f > f ? 1 : 0);
		break;
	}
	case F_LTE: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f <= f ? 1 : 0);
		break;
	}
	case F_GTE: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f >= f ? 1 : 0);
		break;
	}
	case F_NOTE: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f != f ? 1 : 0);
		break;
	}
	case F_EQUALE: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f == f ? 1 : 0);
		break;
	}
	case F_PLUSA: {
		float n = stack_pop().f;
		stack_push(stack_pop_var()->f += n);
		break;
	}
	case F_MINUSA: {
		float n = stack_pop().f;
		stack_push(stack_pop_var()->f -= n);
		break;
	}
	case F_MULA: {
		float n = stack_pop().f;
		stack_push(stack_pop_var()->f *= n);
		break;
	}
	case F_DIVA: {
		float n = stack_pop().f;
		union vm_value *var = stack_pop_var();
		if (unlikely(n == 0.0f && var->f == 0.0f) && vm_zero_vector_div_guard())
			n = 1.0f;
		stack_push(var->f /= n);
		break;
	}
	//
	// --- Strings ---
	//
	case S_PUSH: {
		if (ain->version == 0)
			stack_push_string(string_ref(ain->messages[get_argument(0)]));
		else
			stack_push_string(string_ref(ain->strings[get_argument(0)]));
		break;
	}
	case S_POP: {
		heap_unref(stack_pop().i);
		break;
	}
	case S_REF: {
		// Dereference a reference to a string
		int str = stack_pop_var()->i;
		if (unlikely(!string_index_valid(str))) {
			WARNING("S_REF on invalid string slot: %d", str);
			stack_push_string(string_ref(&EMPTY_STRING));
			break;
		}
		stack_push_string(string_ref(heap_get_string(str)));
		break;
	}
	//case S_REFREF: // ???: why/how is this different from regular REFREF?
	case S_ASSIGN: { // A = B
		if (ain->version >= 14) {
			int rval = stack_pop().i;
			int32_t varno = stack_pop().i;
			int32_t pageno = stack_pop().i;
			struct string *rstr = string_index_valid(rval) ? heap_get_string(rval) : &EMPTY_STRING;
			if (page_index_valid(pageno) && heap[pageno].page
					&& varno >= 0 && varno < heap[pageno].page->nr_vars) {
				int lval = heap[pageno].page->values[varno].i;
				if (string_index_valid(lval)) {
					heap_string_assign(lval, rstr);
				} else {
					int slot = heap_alloc_slot(VM_STRING);
					heap[slot].s = string_ref(rstr);
					heap[pageno].page->values[varno].i = slot;
				}
			} else if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT) {
				WARNING("Out of bounds S_ASSIGN: %d/%d", pageno, varno);
			}
			stack_push(rval);
			break;
		}
		int rval = stack_peek(0).i;
		int lval = stack_peek(1).i;
		heap_string_assign(lval, string_index_valid(rval) ? heap_get_string(rval) : &EMPTY_STRING);
		// remove A from the stack, but leave B
		stack_set(1, rval);
		stack_pop();
		break;
	}
	case S_PLUSA:
	case S_PLUSA2: {
		if (ain->version >= 14) {
			int b = stack_pop().i;
			int32_t varno = stack_pop().i;
			int32_t pageno = stack_pop().i;
			struct string *bstr = string_index_valid(b) ? heap_get_string(b) : &EMPTY_STRING;
			struct string *res = string_ref(&EMPTY_STRING);
			if (page_index_valid(pageno) && heap[pageno].page
					&& varno >= 0 && varno < heap[pageno].page->nr_vars) {
				int a = heap[pageno].page->values[varno].i;
				if (string_index_valid(a)) {
					string_append(&heap[a].s, bstr);
				} else {
					a = heap_alloc_slot(VM_STRING);
					heap[a].s = string_dup(bstr);
					heap[pageno].page->values[varno].i = a;
				}
				free_string(res);
				res = string_ref(heap[a].s);
			} else if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT) {
				WARNING("Out of bounds S_PLUSA2: %d/%d", pageno, varno);
			}
			if (string_index_valid(b))
				heap_unref(b);
			stack_push_string(res);
			break;
		}
		int a = stack_peek(1).i;
		int b = stack_peek(0).i;
		string_append(&heap[a].s, heap[b].s);
		heap_unref(b);
		stack_pop();
		stack_pop();
		stack_push_string(string_ref(heap[a].s));
		break;
	}
	case S_ADD: {
		int b = stack_pop().i;
		int a = stack_pop().i;
		// TODO: can use string_append here?
		stack_push_string(string_concatenate(heap_get_string(a), heap_get_string(b)));
		heap_unref(a);
		heap_unref(b);
		break;
	}
	case S_LT: {
		bool lt = strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text) < 0;
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(lt);
		break;
	}
	case S_GT: {
		bool gt = strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text) > 0;
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(gt);
		break;
	}
	case S_LTE: {
		bool lte = strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text) <= 0;
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(lte);
		break;
	}
	case S_GTE: {
		bool gte = strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text) >= 0;
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(gte);
		break;
	}
	case S_NOTE: {
		bool noteq = !vm_string_equal(stack_peek_string(1), stack_peek_string(0));
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(noteq);
		break;
	}
	case S_EQUALE: {
		bool eq = vm_string_equal(stack_peek_string(1), stack_peek_string(0));
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(eq);
		break;
	}
	case S_LENGTH: {
		int str = stack_pop_var()->i;
		stack_push(string_index_valid(str) ? sjis_count_char(heap_get_string(str)->text) : 0);
		break;
	}
	case S_LENGTH2: {
		int str = stack_pop().i;
		stack_push(string_index_valid(str) ? sjis_count_char(heap_get_string(str)->text) : 0);
		heap_unref(str);
		break;
	}
	case S_LENGTHBYTE: {
		int str = stack_pop_var()->i;
		stack_push(string_index_valid(str) ? heap_get_string(str)->size : 0);
		break;
	}
	case S_EMPTY: {
		bool empty = !stack_peek_string(0)->size;
		heap_unref(stack_pop().i);
		stack_push(empty);
		break;
	}
	case S_FIND: {
		int i = string_find(stack_peek_string(1), stack_peek_string(0));
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(i);
		break;
	}
	case S_GETPART: {
		int len = stack_pop().i; // length
		int i = stack_pop().i; // index
		// Since Sys43VM, a negative end position means copying through the end.
		if (AIN_VERSION_GTE(ain, 6, 1) && len < -max(i, 0))
			len = INT32_MAX;
		struct string *s = string_copy(stack_peek_string(0), i, len);
		heap_unref(stack_pop().i);
		stack_push_string(s);
		break;
	}
	//case S_PUSHBACK: // ???
	case S_PUSHBACK2: {
		int c = stack_pop().i;
		int str = stack_pop().i;
		string_push_back(&heap[str].s, c);
		break;
	}
	//case S_POPBACK: // ???
	case S_POPBACK2: {
		int str = stack_pop().i;
		string_pop_back(&heap[str].s);
		break;
	}
	//case S_ERASE: // ???
	case S_ERASE2: {
		stack_pop(); // ???
		int i = stack_pop().i; // index
		int str = stack_pop().i;
		string_erase(&heap[str].s, i);
		break;
	}
	case S_MOD: {
		int type = ain->version >= 11 ? get_argument(0) : stack_pop().i;
		union vm_value val = stack_pop();
		int fmt = stack_pop().i;
		int dst = heap_alloc_slot(VM_STRING);
		heap[dst].s = string_format(heap[fmt].s, val, type);
		heap_unref(fmt);
		stack_push(dst);
		break;
	}
	case I_STRING: {
		stack_push_string(integer_to_string(stack_pop().i));
		break;
	}
	case FTOS: {
		int precision = stack_pop().i;
		stack_push_string(float_to_string(stack_pop().f, precision));
		break;
	}
	case STOI: {
		int str = stack_pop().i;
		stack_push(string_to_integer(heap[str].s));
		heap_unref(str);
		break;
	}
	case FT_ASSIGNS: {
		//int functype = stack_pop().i;
		stack_pop();
		int str = stack_pop().i;
		int fno = get_function_by_name(heap_get_string(str)->text);
		stack_pop_var()->i = fno > 0 ? fno : 0;
		stack_push(str);
		break;
	}
	// --- Characters ---
	case C_REF: {
		int i = stack_pop().i;
		int str = stack_pop().i;
		int32_t ch = string_get_char(heap_get_string(str), i);
		// 'é' for EN Rance Quest
		if (game_rance8_mg && ch == -91)
			stack_push(165);
		else
			stack_push(ch);
		break;
	}
	case C_ASSIGN: {
		int c = stack_pop().i;
		int i = stack_pop().i;
		int str = stack_pop().i;
		string_set_char(&heap[str].s, i, c);
		stack_push(c);
		break;
	}
	//
	// --- Structs/Classes ---
	//
	case SR_REF: {
		stack_push(vm_copy_page(stack_pop_var()->i));
		break;
	}
	case SR_REF2: {
		stack_push(vm_copy_page(stack_pop().i));
		break;
	}
	case SR_POP: {
		heap_unref(stack_pop().i);
		break;
	}
	case SR_ASSIGN: {
		if (ain->version >= 14) {
			int rval = stack_pop().i;
			int32_t varno = stack_pop().i;
			int32_t pageno = stack_pop().i;
			if (page_index_valid(pageno) && heap[pageno].page
					&& varno >= 0 && varno < heap[pageno].page->nr_vars) {
				int lval = heap[pageno].page->values[varno].i;
				if (page_index_valid(rval)) {
					if (page_index_valid(lval)) {
						heap_struct_assign(lval, rval);
					} else {
						heap[pageno].page->values[varno].i = vm_copy_page(rval);
					}
				} else if (page_index_valid(lval)) {
					heap_unref(lval);
					heap[pageno].page->values[varno].i = -1;
				}
			} else if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT) {
				WARNING("Out of bounds SR_ASSIGN: %d/%d", pageno, varno);
			}
			stack_push(rval);
			break;
		}
		if (ain->version > 1 && ain->version < 11)
			stack_pop(); // struct type
		int rval = stack_pop().i;
		int lval = stack_pop().i;
		heap_struct_assign(lval, rval);
		stack_push(rval);
		break;
	}
	//
	// -- Arrays --
	//
	case A_ALLOC: {
		int struct_type;
		int rank = stack_pop().i;
		int varno = stack_peek(rank).i;
		int pageno = stack_peek(rank+1).i;
		if (unlikely(!page_index_valid(pageno) || !heap[pageno].page
				|| varno < 0 || varno >= heap[pageno].page->nr_vars))
			VM_ERROR("A_ALLOC: invalid parent page %d var %d", pageno, varno);
		int array = heap[pageno].page->values[varno].i;
		if (unlikely(!page_index_valid(array)))
			VM_ERROR("A_ALLOC: invalid array page index %d", array);
		enum ain_data_type data_type = variable_type(heap[pageno].page, varno, &struct_type, NULL);
		if (heap[array].page) {
			delete_page_vars(heap[array].page);
			free_page(heap[array].page);
		}
		heap_set_page(array, alloc_array(rank, stack_peek_ptr(rank-1), data_type, struct_type, true));
		stack_ptr -= rank + 2;
		break;
	}
	case A_REALLOC: {
		int struct_type;
		int rank = stack_pop().i; // rank
		int varno = stack_peek(rank).i;
		int pageno = stack_peek(rank+1).i;
		if (unlikely(!page_index_valid(pageno) || !heap[pageno].page
				|| varno < 0 || varno >= heap[pageno].page->nr_vars))
			VM_ERROR("A_REALLOC: invalid parent page %d var %d", pageno, varno);
		int array = heap[pageno].page->values[varno].i;
		if (unlikely(!page_index_valid(array)))
			VM_ERROR("A_REALLOC: invalid array page index %d", array);
		enum ain_data_type data_type = variable_type(heap[pageno].page, varno, &struct_type, NULL);
		heap_set_page(array, realloc_array(heap[array].page, rank, stack_peek_ptr(rank-1), data_type, struct_type, true));
		stack_ptr -= rank + 2;
		break;
	}
	case A_FREE: {
		int array = stack_pop_var()->i;
		if (page_index_valid(array) && heap[array].page) {
			delete_page_vars(heap[array].page);
			free_page(heap[array].page);
			heap_set_page(array, NULL);
		}
		break;
	}
	case A_REF: {
		int array = stack_pop().i;
		if (ain->version >= 14 && array == -1) {
			stack_push(-1);
			break;
		}
		if (ain->version >= 11 && string_index_valid(array)) {
			stack_push(vm_string_ref(heap_get_string(array)));
			break;
		}
		if (unlikely(array <= 0 || !page_index_valid(array))) {
			static unsigned int warn_count = 0;
			if (warn_count++ < INVALID_VAR_WARN_LIMIT)
				WARNING("A_REF on invalid page/string slot: %d", array);
			const struct ain_type *t = last_dummy_type ? last_dummy_type
				: lookup_expected_var_type(last_var_heap, last_var_page);
			if (t && t->data == AIN_WRAP)
				t = t->array_type;
			if (ain->version >= 11 && t) {
				if ((t->data == AIN_ARRAY || t->data == AIN_REF_ARRAY) && t->array_type) {
					stack_push(heap_alloc_page(v12_array_new(t->array_type, 0, false)));
					break;
				}
				if ((t->data == AIN_STRUCT || t->data == AIN_REF_STRUCT) &&
				    t->struc >= 0 && t->struc < ain->nr_structures) {
					int s = alloc_struct(t->struc);
					init_struct(t->struc, s);
					stack_push(s);
					break;
				}
				if (t->data == AIN_DELEGATE) {
					stack_push(heap_alloc_page(alloc_page(DELEGATE_PAGE, 0, 0)));
					break;
				}
			}
			stack_push(vm_string_ref(&EMPTY_STRING));
			break;
		}
		stack_push(vm_copy_page(array));
		break;
	}
	case A_NUMOF: {
		int rank = stack_pop().i; // rank
		int array = stack_pop_var()->i;
		stack_push(page_index_valid(array) ? array_numof(heap[array].page, rank) : 0);
		break;
	}
	case A_COPY: {
		int n = stack_pop().i;
		int src_i = stack_pop().i;
		int src = stack_pop().i;
		int dst_i = stack_pop().i;
		int dst = stack_pop_var()->i;
		array_copy(page_index_valid(dst) ? heap[dst].page : NULL, dst_i,
				page_index_valid(src) ? heap[src].page : NULL, src_i, n);
		stack_push(n);
		break;
	}
	case A_FILL: {
		union vm_value val = stack_pop();
		int n = stack_pop().i;
		int i = stack_pop().i;
		int array = stack_pop_var()->i;
		stack_push(page_index_valid(array) ? array_fill(heap[array].page, i, n, val) : 0);
		break;
	}
	case A_PUSHBACK: {
		int struct_type;
		union vm_value val = stack_pop();
		int varno = stack_pop().i;
		int pageno = stack_pop().i;
		if (unlikely(!page_index_valid(pageno) || !heap[pageno].page
				|| varno < 0 || varno >= heap[pageno].page->nr_vars))
			VM_ERROR("A_PUSHBACK: invalid parent page %d var %d", pageno, varno);
		int array = heap[pageno].page->values[varno].i;
		if (unlikely(!page_index_valid(array)))
			VM_ERROR("A_PUSHBACK: invalid array page index %d", array);
		enum ain_data_type data_type = variable_type(heap[pageno].page, varno, &struct_type, NULL);
		heap_set_page(array, array_pushback(heap[array].page, val, data_type, struct_type));
		break;
	}
	case A_POPBACK: {
		int array = stack_pop_var()->i;
		if (page_index_valid(array))
			heap_set_page(array, array_popback(heap[array].page));
		break;
	}
	case A_EMPTY: {
		struct page *array = heap_get_page(stack_pop_var()->i);
		stack_push(!array || !array->nr_vars);
		break;
	}
	case A_ERASE: {
		int i = stack_pop().i;
		int array = stack_pop_var()->i;
		bool success = false;
		if (page_index_valid(array))
			heap_set_page(array, array_erase(heap[array].page, i, &success));
		stack_push(success);
		break;
	}
	case A_INSERT: {
		int struct_type;
		union vm_value val = stack_pop();
		int i = stack_pop().i;
		int varno = stack_pop().i;
		int pageno = stack_pop().i;
		if (unlikely(!page_index_valid(pageno) || !heap[pageno].page
				|| varno < 0 || varno >= heap[pageno].page->nr_vars))
			VM_ERROR("A_INSERT: invalid parent page %d var %d", pageno, varno);
		int array = heap[pageno].page->values[varno].i;
		if (unlikely(!page_index_valid(array)))
			VM_ERROR("A_INSERT: invalid array page index %d", array);
		enum ain_data_type data_type = variable_type(heap[pageno].page, varno, &struct_type, NULL);
		heap_set_page(array, array_insert(heap[array].page, i, val, data_type, struct_type));
		break;
	}
	case A_SORT: {
		int fno = stack_pop().i;
		int obj = (ain->version > 8 || (ain->version == 8 && !game_tsumamigui3_jun && !game_tsumamigui3_ntr))
			? stack_pop().i : -1;
		int array = stack_pop_var()->i;
		if (page_index_valid(array))
			array_sort(heap[array].page, fno, obj);
		break;
	}
	case A_FIND: {
		int fno = stack_pop().i;
		int obj = (ain->version > 8 || (ain->version == 8 && !game_tsumamigui3_jun && !game_tsumamigui3_ntr))
			? stack_pop().i : -1;
		union vm_value v = stack_pop();
		int end = stack_pop().i;
		int start = stack_pop().i;
		int32_t arr_heap = stack_peek(1).i;
		int32_t arr_var = stack_peek(0).i;
		struct page *array = heap_get_page(stack_pop_var()->i);
		bool is_str_array = false;
		if (array) {
			enum ain_data_type et = (array->a_type == AIN_ARRAY)
				? array->array.element_type.data
				: array_type(array->a_type);
			is_str_array = (et == AIN_STRING);
		} else {
			const struct ain_type *vt = lookup_expected_var_type(arr_heap, arr_var);
			if (vt && vt->data == AIN_WRAP)
				vt = vt->array_type;
			if (vt) {
				if (vt->data == AIN_ARRAY_STRING || vt->data == AIN_REF_ARRAY_STRING)
					is_str_array = true;
				else if ((vt->data == AIN_ARRAY || vt->data == AIN_REF_ARRAY)
						&& vt->array_type && vt->array_type->data == AIN_STRING)
					is_str_array = true;
			} else if (page_index_valid(arr_heap) && heap[arr_heap].page
					&& heap[arr_heap].page->type == ARRAY_PAGE
					&& array_type(heap[arr_heap].page->a_type) == AIN_STRING) {
				is_str_array = true;
			}
		}
		stack_push(array_find(array, start, end, v, fno, obj));
		if (is_str_array) {
			heap_unref(v.i);
		}
		break;
	}
	case A_REVERSE: {
		int array = stack_pop_var()->i;
		if (page_index_valid(array))
			array_reverse(heap[array].page);
		break;
	}
	//
	// -- Shorthand Instructions (added in Alice 2010) ---
	//
	case SH_SR_ASSIGN: {
		int rval = stack_pop_var()->i;
		int lval = stack_pop().i;
		heap_struct_assign(lval, rval);
		break;
	}
	case SH_MEM_ASSIGN_LOCAL: {
		member_set(get_argument(0), local_get(get_argument(1)).i);
		break;
	}
	case A_NUMOF_GLOB_1: {
		int array = global_get(get_argument(0)).i;
		stack_push(array_numof(heap_get_page(array), 1));
		break;
	}
	case A_NUMOF_STRUCT_1: {
		int array = member_get(get_argument(0)).i;
		stack_push(array_numof(heap_get_page(array), 1));
		break;
	}
	case SH_MEM_ASSIGN_IMM: {
		member_set(get_argument(0), get_argument(1));
		break;
	}
	case SH_LOCALREFREF: {
		stack_push(local_get(get_argument(0)));
		stack_push(local_get(get_argument(0)+1));
		break;
	}
	case SH_LOCALASSIGN_SUB_IMM: {
		int n = get_argument(0);
		local_set(n, (int32_t)((uint32_t)local_get(n).i - (uint32_t)get_argument(1)));
		break;
	}
	case SH_IF_LOC_LT_IMM: {
		if (local_get(get_argument(0)).i < get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_LOC_LT_IMM);
		break;
	}
	case SH_IF_LOC_GE_IMM: {
		if (local_get(get_argument(0)).i >= get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_LOC_GE_IMM);
		break;
	}
	case SH_LOCREF_ASSIGN_MEM: {
		struct page *page = heap_get_page(local_get(get_argument(0)).i);
		int index = local_get(get_argument(0)+1).i;
		page_set_var(page, index, member_get(get_argument(1)));
		break;
	}
	case PAGE_REF: {
		struct page *page = heap_get_page(stack_pop().i);
		stack_push(page_get_var(page, get_argument(0)));
		break;
	}
	case SH_GLOBAL_ASSIGN_LOCAL: {
		global_set(get_argument(0), local_get(get_argument(1)), true);
		break;
	}
	case SH_STRUCTREF_GT_IMM: {
		stack_push(member_get(get_argument(0)).i > get_argument(1) ? 1 : 0);
		break;
	}
	case SH_STRUCT_ASSIGN_LOCALREF_ITOB: {
		member_set(get_argument(0), !!local_get(get_argument(1)).i);
		break;
	}
	case SH_LOCAL_ASSIGN_STRUCTREF: {
		local_set(get_argument(0), member_get(get_argument(1)).i);
		break;
	}
	case SH_IF_STRUCTREF_NE_LOCALREF: {
		if (member_get(get_argument(0)).i != local_get(get_argument(1)).i)
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_STRUCTREF_NE_LOCALREF);
		break;
	}
	case SH_IF_STRUCTREF_GT_IMM: {
		if (member_get(get_argument(0)).i > get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_STRUCTREF_GT_IMM);
		break;
	}
	case SH_STRUCTREF_CALLMETHOD_NO_PARAM: {
		int memb_page = member_get(get_argument(0)).i;
		function_call(get_argument(1), instr_ptr + instruction_width(SH_STRUCTREF_CALLMETHOD_NO_PARAM));
		set_struct_page(memb_page);
		local_page()->local.struct_ptr = memb_page;
		break;
	}
	case SH_STRUCTREF2: {
		int memb = member_get(get_argument(0)).i;
		stack_push(page_get_var(heap_get_page(memb), get_argument(1)));
		break;
	}
	case SH_REF_STRUCTREF2: {
		int page = stack_pop().i;
		int memb = page_get_var(heap_get_page(page), get_argument(0)).i;
		stack_push(page_get_var(heap_get_page(memb), get_argument(1)));
		break;
	}
	case SH_STRUCTREF3: {
		int memb0 = member_get(get_argument(0)).i;
		int memb1 = page_get_var(heap_get_page(memb0), get_argument(1)).i;
		stack_push(page_get_var(heap_get_page(memb1), get_argument(2)));
		break;
	}
	case SH_STRUCTREF2_CALLMETHOD_NO_PARAM: {
		int memb1 = member_get(get_argument(0)).i;
		int memb2 = page_get_var(heap_get_page(memb1), get_argument(1)).i;
		function_call(get_argument(2), instr_ptr + instruction_width(SH_STRUCTREF2_CALLMETHOD_NO_PARAM));
		set_struct_page(memb2);
		local_page()->local.struct_ptr = memb2;
		break;
	}
	case SH_IF_STRUCTREF_Z: {
		if (!member_get(get_argument(0)).i)
			instr_ptr = get_argument(1);
		else
			instr_ptr += instruction_width(SH_IF_STRUCTREF_Z);
		break;
	}
	case SH_IF_STRUCT_A_NOT_EMPTY: {
		struct page *array = heap_get_page(member_get(get_argument(0)).i);
		if (array && array->nr_vars)
			instr_ptr = get_argument(1);
		else
			instr_ptr += instruction_width(SH_IF_STRUCT_A_NOT_EMPTY);
		break;
	}
	case SH_IF_LOC_GT_IMM: {
		if (local_get(get_argument(0)).i > get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_LOC_GT_IMM);
		break;
	}
	case SH_IF_STRUCTREF_NE_IMM: {
		if (member_get(get_argument(0)).i != get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_STRUCTREF_NE_IMM);
		break;
	}
	case THISCALLMETHOD_NOPARAM: {
		int this_page = struct_page_slot();
		function_call(get_argument(0), instr_ptr + instruction_width(THISCALLMETHOD_NOPARAM));
		set_struct_page(this_page);
		local_page()->local.struct_ptr = this_page;
		break;
	}
	case SH_IF_LOC_NE_IMM: {
		if (local_get(get_argument(0)).i != get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_LOC_NE_IMM);
		break;
	}
	case SH_IF_STRUCTREF_EQ_IMM: {
		if (member_get(get_argument(0)).i == get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_STRUCTREF_EQ_IMM);
		break;
	}
	case SH_GLOBAL_ASSIGN_IMM: {
		global_set(get_argument(0), (union vm_value) { .i = get_argument(1) }, false);
		break;
	}
	case SH_LOCALSTRUCT_ASSIGN_IMM: {
		struct page *page = heap_get_page(local_get(get_argument(0)).i);
		page_set_var(page, get_argument(1), get_argument(2));
		break;
	}
	case SH_STRUCT_A_PUSHBACK_LOCAL_STRUCT: {
		int struct_type;
		int array = member_get(get_argument(0)).i;
		union vm_value val = vm_copy(local_get(get_argument(1)), AIN_STRUCT);
		enum ain_data_type data_type = variable_type(struct_page(), get_argument(0), &struct_type, NULL);
		heap_set_page(array, array_pushback(heap_get_page(array), val, data_type, struct_type));
		break;
	}
	case SH_GLOBAL_A_PUSHBACK_LOCAL_STRUCT: {
		int struct_type;
		int array = global_get(get_argument(0)).i;
		union vm_value val = vm_copy(local_get(get_argument(1)), AIN_STRUCT);
		enum ain_data_type data_type = variable_type(global_page(), get_argument(0), &struct_type, NULL);
		heap_set_page(array, array_pushback(heap_get_page(array), val, data_type, struct_type));
		break;
	}
	case SH_LOCAL_A_PUSHBACK_LOCAL_STRUCT: {
		int struct_type;
		int array = local_get(get_argument(0)).i;
		union vm_value val = vm_copy(local_get(get_argument(1)), AIN_STRUCT);
		enum ain_data_type data_type = variable_type(local_page(), get_argument(0), &struct_type, NULL);
		heap_set_page(array, array_pushback(heap_get_page(array), val, data_type, struct_type));
		break;
	}
	case SH_IF_SREF_NE_STR0: {
		struct string *a = heap_get_string(stack_pop_var()->i);
		struct string *b = ain->strings[get_argument(0)];
		if (!vm_string_equal(a, b))
			instr_ptr = get_argument(1);
		else
			instr_ptr += instruction_width(SH_IF_SREF_NE_STR0);
		break;
	}
	case SH_S_ASSIGN_REF: {
		int rval = stack_pop_var()->i;
		int lval = stack_pop().i;
		heap_string_assign(lval, heap_get_string(rval));
		break;
	}
	case SH_A_FIND_SREF: {
		union vm_value *v = stack_pop_var();
		int end = stack_pop().i;
		int start = stack_pop().i;
		int array = stack_pop_var()->i;
		stack_push(array_find(heap_get_page(array), start, end, *v, 0, -1));
		break;
	}
	case SH_SREF_EMPTY: {
		stack_push(!heap_get_string(stack_pop_var()->i)->size);
		break;
	}
	case SH_STRUCTSREF_EQ_LOCALSREF: {
		struct string *a = heap_get_string(member_get(get_argument(0)).i);
		struct string *b = heap_get_string(local_get(get_argument(1)).i);
		stack_push(vm_string_equal(a, b));
		break;
	}
	case SH_LOCALSREF_EQ_STR0: {
		struct string *a = heap_get_string(local_get(get_argument(0)).i);
		struct string *b = ain->strings[get_argument(1)];
		stack_push(vm_string_equal(a, b));
		break;
	}
	case SH_STRUCTSREF_NE_LOCALSREF: {
		struct string *a = heap_get_string(member_get(get_argument(0)).i);
		struct string *b = heap_get_string(local_get(get_argument(1)).i);
		stack_push(!vm_string_equal(a, b));
		break;
	}
	case SH_LOCALSREF_NE_STR0: {
		struct string *a = heap_get_string(local_get(get_argument(0)).i);
		struct string *b = ain->strings[get_argument(1)];
		stack_push(!vm_string_equal(a, b));
		break;
	}
	case SH_STRUCT_SR_REF: {
		int sr = member_get(get_argument(0)).i;
		stack_push(vm_copy_page(sr));
		// NOTE: argument 1 (struct type) not used
		break;
	}
	case SH_STRUCT_S_REF: {
		int str = member_get(get_argument(0)).i;
		stack_push_string(string_ref(heap_get_string(str)));
		break;
	}
	case S_REF2: {
		struct page *page = heap_get_page(stack_pop().i);
		struct string *s = heap_get_string(page_get_var(page, get_argument(0)).i);
		stack_push_string(string_ref(s));
		break;
	}
	case SH_REF_LOCAL_ASSIGN_STRUCTREF2: {
		struct page *memb = heap_get_page(member_get(get_argument(0)).i);
		int page = local_get(get_argument(1)).i;
		int var = local_get(get_argument(1) + 1).i;
		page_set_var(heap_get_page(page), var, page_get_var(memb, get_argument(2)));
		break;
	}
	case SH_GLOBAL_S_REF: {
		int str = global_get(get_argument(0)).i;
		stack_push_string(string_ref(heap_get_string(str)));
		break;
	}
	case SH_LOCAL_S_REF: {
		int str = local_get(get_argument(0)).i;
		stack_push_string(string_ref(heap_get_string(str)));
		break;
	}
	case SH_LOCALREF_SASSIGN_LOCALSREF: {
		int lval = local_get(get_argument(0)).i;
		int rval = local_get(get_argument(1)).i;
		heap_string_assign(lval, heap_get_string(rval));
		break;
	}
	case SH_LOCAL_APUSHBACK_LOCALSREF: {
		int array = local_get(get_argument(0)).i;
		union vm_value val = vm_copy(local_get(get_argument(1)), AIN_STRING);
		heap_set_page(array, array_pushback(heap_get_page(array), val, AIN_ARRAY_STRING, -1));
		break;
	}
	case SH_S_ASSIGN_CALLSYS19: {
		struct string *name = get_func_stack_name(stack_pop().i);
		heap_string_assign(stack_pop().i, name);
		free_string(name);
		break;
	}
	case SH_S_ASSIGN_STR0: {
		int lval = stack_pop().i;
		heap_string_assign(lval, ain->strings[get_argument(0)]);
		break;
	}
	case SH_SASSIGN_LOCALSREF: {
		int lval = stack_pop().i;
		struct string *rval = heap_get_string(local_get(get_argument(0)).i);
		heap_string_assign(lval, rval);
		break;
	}
	case SH_STRUCTREF_SASSIGN_LOCALSREF: {
		int lval = member_get(get_argument(0)).i;
		int rval = local_get(get_argument(1)).i;
		heap_string_assign(lval, heap_get_string(rval));
		break;
	}
	case SH_LOCALSREF_EMPTY: {
		stack_push(!heap_get_string(local_get(get_argument(0)).i)->size);
		break;
	}
	case SH_GLOBAL_APUSHBACK_LOCALSREF: {
		int array = global_get(get_argument(0)).i;
		union vm_value val = vm_copy(local_get(get_argument(1)), AIN_STRING);
		heap_set_page(array, array_pushback(heap_get_page(array), val, AIN_ARRAY_STRING, -1));
		break;
	}
	case SH_STRUCT_APUSHBACK_LOCALSREF: {
		int array = member_get(get_argument(0)).i;
		union vm_value val = vm_copy(local_get(get_argument(1)), AIN_STRING);
		heap_set_page(array, array_pushback(heap_get_page(array), val, AIN_ARRAY_STRING, -1));
		break;
	}
	case SH_STRUCTSREF_EMPTY: {
		stack_push(!heap_get_string(member_get(get_argument(0)).i)->size);
		break;
	}
	case SH_GLOBALSREF_EMPTY: {
		stack_push(!heap_get_string(global_get(get_argument(0)).i)->size);
		break;
	}
	case SH_SASSIGN_STRUCTSREF: {
		int lval = stack_pop().i;
		int rval = member_get(get_argument(0)).i;
		heap_string_assign(lval, heap_get_string(rval));
		break;
	}
	case SH_SASSIGN_GLOBALSREF: {
		int lval = stack_pop().i;
		int rval = global_get(get_argument(0)).i;
		heap_string_assign(lval, heap_get_string(rval));
		break;
	}
	case SH_STRUCTSREF_NE_STR0: {
		struct string *a = heap_get_string(member_get(get_argument(0)).i);
		struct string *b = ain->strings[get_argument(1)];
		stack_push(!vm_string_equal(a, b));
		break;
	}
	case SH_GLOBALSREF_NE_STR0: {
		struct string *a = heap_get_string(global_get(get_argument(0)).i);
		struct string *b = ain->strings[get_argument(1)];
		stack_push(!vm_string_equal(a, b));
		break;
	}
	case SH_LOC_LT_IMM_OR_LOC_GE_IMM: {
		int i = local_get(get_argument(0)).i;
		stack_push(i < get_argument(1) || i >= get_argument(2));
		break;
	}
	case A_SORT_MEM: {
		int mno = stack_pop().i;
		int array = stack_pop_var()->i;
		if (page_index_valid(array))
			array_sort_mem(heap[array].page, mno);
		break;
	}
	case DG_SET: {
		int fun = stack_pop().i;
		int obj = stack_pop().i;
		int dg_i = stack_pop().i;
		delete_page(dg_i);
		heap_set_page(dg_i, delegate_new_from_method(obj, fun));
		break;
	}
	case DG_ADD: {
		int fun = stack_pop().i;
		int obj = stack_pop().i;
		int dg_i = stack_pop().i;
		struct page *dg = heap_get_delegate_page(dg_i);
		heap_set_page(dg_i, delegate_append(dg, obj, fun));
		break;
	}
	case DG_CALL: { // DG_TYPE, ADDR
		delegate_call(get_argument(0), get_argument(1));
		break;
	}
	case DG_NUMOF: {
		int dg = stack_pop().i;
		stack_push(delegate_numof(heap_get_delegate_page(dg)));
		break;
	}
	case DG_EXIST: {
		int fun = stack_pop().i;
		int obj = stack_pop().i;
		int dg_i = stack_pop().i;
		stack_push(delegate_contains(heap_get_delegate_page(dg_i), obj, fun));
		break;
	}
	case DG_ERASE: {
		int fun = stack_pop().i;
		int obj = stack_pop().i;
		int dg_i = stack_pop().i;
		delegate_erase(heap_get_delegate_page(dg_i), obj, fun);
		break;
	}
	case DG_CLEAR: {
		int slot = stack_pop().i;
		if (slot <= 0 || !page_index_valid(slot))
			break;
		heap_set_page(slot, delegate_clear(heap_get_delegate_page(slot)));
		break;
	}
	case DG_COPY: {
		stack_push(vm_copy_page(stack_pop().i));
		break;
	}
	case DG_ASSIGN: {
		int set_i = stack_pop().i;
		if (ain->version >= 14) {
			int32_t varno = stack_pop().i;
			int32_t pageno = stack_pop().i;
			struct page *set = page_index_valid(set_i) ? heap_get_delegate_page(set_i) : NULL;
			if (page_index_valid(pageno) && heap[pageno].page
					&& varno >= 0 && varno < heap[pageno].page->nr_vars) {
				int dst_i = heap[pageno].page->values[varno].i;
				struct page *new_dg = set ? copy_page(set) : alloc_page(DELEGATE_PAGE, 0, 0);
				if (page_index_valid(dst_i)) {
					delete_page(dst_i);
					heap_set_page(dst_i, new_dg);
				} else {
					heap[pageno].page->values[varno].i = heap_alloc_page(new_dg);
				}
			} else if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT) {
				WARNING("Out of bounds DG_ASSIGN: %d/%d", pageno, varno);
			}
			stack_push(set_i);
			break;
		}
		int dst_i = stack_pop().i;
		struct page *set = heap_get_delegate_page(set_i);
		struct page *new_dg = copy_page(set);
		delete_page(dst_i);
		heap_set_page(dst_i, new_dg);
		stack_push(set_i);
		break;
	}
	case DG_PLUSA: {
		int add_i = stack_pop().i;
		if (ain->version >= 14) {
			int32_t varno = stack_pop().i;
			int32_t pageno = stack_pop().i;
			struct page *add = page_index_valid(add_i) ? heap_get_delegate_page(add_i) : NULL;
			if (page_index_valid(pageno) && heap[pageno].page
					&& varno >= 0 && varno < heap[pageno].page->nr_vars) {
				int dst_i = heap[pageno].page->values[varno].i;
				if (page_index_valid(dst_i)) {
					struct page *dst = heap_get_delegate_page(dst_i);
					heap_set_page(dst_i, delegate_plusa(dst, add));
				} else {
					struct page *dst = delegate_plusa(alloc_page(DELEGATE_PAGE, 0, 0), add);
					heap[pageno].page->values[varno].i = heap_alloc_page(dst);
				}
			} else if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT) {
				WARNING("Out of bounds DG_PLUSA: %d/%d", pageno, varno);
			}
			stack_push(add_i);
			break;
		}
		int dst_i = stack_pop().i;
		struct page *add = heap_get_delegate_page(add_i);
		struct page *dst = heap_get_delegate_page(dst_i);
		heap_set_page(dst_i, delegate_plusa(dst, add));
		stack_push(add_i);
		break;
	}
	case DG_MINUSA: {
		int minus_i = stack_pop().i;
		if (ain->version >= 14) {
			int32_t varno = stack_pop().i;
			int32_t pageno = stack_pop().i;
			struct page *minus = page_index_valid(minus_i) ? heap_get_delegate_page(minus_i) : NULL;
			if (page_index_valid(pageno) && heap[pageno].page
					&& varno >= 0 && varno < heap[pageno].page->nr_vars) {
				int dst_i = heap[pageno].page->values[varno].i;
				if (page_index_valid(dst_i)) {
					struct page *dst = heap_get_delegate_page(dst_i);
					heap_set_page(dst_i, delegate_minusa(dst, minus));
				}
			} else if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT) {
				WARNING("Out of bounds DG_MINUSA: %d/%d", pageno, varno);
			}
			stack_push(minus_i);
			break;
		}
		int dst_i = stack_pop().i;
		struct page *minus = heap_get_delegate_page(minus_i);
		struct page *dst = heap_get_delegate_page(dst_i);
		heap_set_page(dst_i, delegate_minusa(dst, minus));
		stack_push(minus_i);
		break;
	}
	case DG_POP: {
		heap_unref(stack_pop().i);
		break;
	}
	case DG_NEW_FROM_METHOD: {
		int fun = stack_pop().i;
		int obj = stack_pop().i;
		stack_push(heap_alloc_page(delegate_new_from_method(obj, fun)));
		break;
	}
	case DG_CALLBEGIN: { // DG_TYPE
		int dg_no = get_argument(0);
		if (dg_no < 0 || dg_no >= ain->nr_delegates)
			VM_ERROR("Invalid delegate index");
		struct ain_function_type *dg = &ain->delegates[dg_no];

		// Stack before: [dg_page, arg0, ...]
		// Stack after:  [arg0, ..., dg_page]
		int dg_page = stack_peek(dg->nr_arguments).i;
		for (int i = 0; i < dg->nr_arguments; i++) {
			int pos = (stack_ptr - dg->nr_arguments) + i;
			stack[pos-1] = stack[pos];
		}
		stack[stack_ptr-1].i = dg_page;
		struct page *page = page_index_valid(dg_page) ? heap_get_delegate_page(dg_page) : NULL;
		if (page) {
			page->delegate.cursor = 0;
			page->delegate.end = delegate_numof(page);
		}
		dg_callbegin_pending = true;
		break;
	}
	case DG_NEW: {
		stack_push(heap_alloc_page(alloc_page(DELEGATE_PAGE, 0, 0)));
		break;
	}
	case DG_STR_TO_METHOD: {
		int dg_no = ain->version >= 11 ? get_argument(0) : stack_pop().i;
		int str = stack_pop().i;
		int fno = get_function_by_name(heap_get_string(str)->text);
		stack_push(function_matches_delegate(dg_no, fno) ? fno : 0);
		heap_unref(str);
		break;
	}
	case X_GETENV: {
		int slot = stack_pop().i;
		struct page *page = heap_get_page(slot);
		if (!page || page->type != LOCAL_PAGE || page->local.parent_page < 0)
			VM_ERROR("Missing closure environment for page %d", slot);
		stack_push(page->local.parent_page);
		break;
	}
	case X_SET: {
		int source = stack_pop().i;
		if (ain->version >= 14) {
			int32_t varno = stack_pop().i;
			int32_t pageno = stack_pop().i;
			if (page_index_valid(pageno) && heap[pageno].page
					&& varno >= 0 && varno < heap[pageno].page->nr_vars) {
				int dst = heap[pageno].page->values[varno].i;
				if (page_index_valid(source)) {
					if (page_index_valid(dst)) {
						heap_struct_assign(dst, source);
					} else {
						heap[pageno].page->values[varno].i = vm_copy_page(source);
					}
				} else if (page_index_valid(dst)) {
					heap_unref(dst);
					heap[pageno].page->values[varno].i = -1;
				}
			} else if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT) {
				WARNING("Out of bounds X_SET: %d/%d", pageno, varno);
			}
			stack_push(source);
			break;
		}
		int destination = stack_pop().i;
		heap_struct_assign(destination, source);
		stack_push(source);
		break;
	}
	case X_ICAST: {
		int object = stack_pop().i;
		int target = get_argument(0);
		int offset = 0;
		bool found = false;
		if (object >= 0) {
			struct page *page = heap_get_page(object);
			if (!page || page->type != STRUCT_PAGE)
				VM_ERROR("Interface cast on non-structure page");
			found = page->index == target;
			struct ain_struct *type = &ain->structures[page->index];
			for (int i = 0; !found && i < type->nr_interfaces; i++) {
				if (type->interfaces[i].struct_type == target) {
					offset = type->interfaces[i].vtable_offset;
					found = true;
				}
			}
		}
		stack_push(found ? object : -1);
		stack_push(offset);
		// AIN v11-12 tests the third result against -1 for failure;
		// AIN v14+ returns option<Target> (3rd slot = is_empty: 0=some, 1=none).
		stack_push(ain->version >= 14 ? (found ? 0 : 1) : (found ? offset : -1));
		break;
	}
	case X_OP_SET: {
		int mode = get_argument(0);
		int w = (mode == 1) ? 2 : (mode & 0xffff);
		if (w < 2 || w > 3 || stack_ptr < w + 2)
			VM_ERROR("Invalid X_OP_SET mode=0x%x (stack_ptr=%d)", mode, stack_ptr);
		union vm_value v[3];
		for (int i = w - 1; i >= 0; i--)
			v[i] = stack_pop();
		int32_t varno = stack_pop().i;
		int32_t pageno = stack_pop().i;
		if (page_index_valid(pageno) && heap[pageno].page
				&& varno >= 0 && varno + w <= heap[pageno].page->nr_vars) {
			struct page *p = heap[pageno].page;
			const struct ain_type *dst_t = variable_full_type(p, varno);
			const struct ain_type *inner = dst_t ? dst_t->array_type : NULL;
			bool new_empty = (v[w - 1].i != 0);
			bool old_empty = (p->values[varno + w - 1].i != 0);
			int32_t old_v0 = p->values[varno].i;
			if ((mode & 0x10000) || ain_option_inner_is_ref_heap(inner)) {
				if (!new_empty && v[0].i >= 0)
					heap_ref(v[0].i);
				for (int i = 0; i < w; i++)
					p->values[varno + i] = v[i];
				if (!old_empty && old_v0 >= 0)
					heap_unref(old_v0);
			} else if (ain_option_inner_is_value_heap(inner)) {
				union vm_value copied = (!new_empty && v[0].i >= 0) ? vm_copy(v[0], inner->data) : vm_int(-1);
				p = heap_get_page(pageno);
				p->values[varno] = copied;
				for (int i = 1; i < w; i++)
					p->values[varno + i] = v[i];
				if (!old_empty && old_v0 >= 0)
					variable_fini(vm_int(old_v0), inner->data, true);
			} else {
				for (int i = 0; i < w; i++)
					p->values[varno + i] = v[i];
			}
		} else if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT) {
			WARNING("Out of bounds X_OP_SET(0x%x): %d/%d at 0x%zx in %s", mode, pageno, varno,
				instr_ptr, call_stack_ptr > 0 ? ain->functions[call_stack[call_stack_ptr-1].fno].name : "?");
		}
		for (int i = 0; i < w; i++)
			stack_push(v[i]);
		break;
	}
	case X_DUP: {
		int count = get_argument(0);
		if (count < 0 || count > stack_ptr)
			VM_ERROR("Invalid X_DUP count: %d (stack_ptr=%d)", count, stack_ptr);
		stack_ensure(count);
		memcpy(&stack[stack_ptr], &stack[stack_ptr - count], count * sizeof(union vm_value));
		stack_ptr += count;
		break;
	}
	case X_MOV: {
		int depth = get_argument(0);
		int count = get_argument(1);
		if (depth < 0 || count < 0 || count > depth || depth > stack_ptr)
			VM_ERROR("Invalid X_MOV %d %d (stack_ptr=%d)", depth, count, stack_ptr);
		if (count > 0 && count < depth) {
			union vm_value small_buf[16];
			union vm_value *tmp = (count <= 16) ? small_buf : xmalloc(count * sizeof(union vm_value));
			memcpy(tmp, &stack[stack_ptr - count], count * sizeof(union vm_value));
			memmove(&stack[stack_ptr - depth + count], &stack[stack_ptr - depth],
				(depth - count) * sizeof(union vm_value));
			memcpy(&stack[stack_ptr - depth], tmp, count * sizeof(union vm_value));
			if (tmp != small_buf)
				free(tmp);
		}
		break;
	}
	case X_REF: {
		int count = get_argument(0);
		if (count < 0 || count > 4)
			VM_ERROR("Invalid X_REF count: %d", count);
		stack_ensure(count);
		int32_t page_index = stack_pop().i;
		int32_t heap_index = stack_pop().i;
		last_var_heap = heap_index;
		last_var_page = page_index;
		if (page_index_valid(heap_index) && heap[heap_index].page
				&& page_index >= 0 && page_index + count <= heap[heap_index].page->nr_vars) {
			last_dummy_type = NULL;
			union vm_value *vals = &heap[heap_index].page->values[page_index];
			for (int i = 0; i < count; i++)
				stack_push(vals[i]);
		} else {
			union vm_value dummy[4];
			if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT)
				WARNING("Out of bounds X_REF(%d): %d/%d at 0x%zx in %s", count, heap_index, page_index,
					instr_ptr, call_stack_ptr > 0 ? ain->functions[call_stack[call_stack_ptr-1].fno].name : "?");
			prepare_dummy_var(dummy, heap_index, page_index);
			for (int i = 0; i < count; i++)
				stack_push(dummy[i]);
		}
		break;
	}
	case X_ASSIGN: {
		int count = get_argument(0);
		if (count < 0 || stack_ptr < count + 2)
			VM_ERROR("Invalid X_ASSIGN count: %d (stack_ptr=%d)", count, stack_ptr);
		int pos = stack_ptr - count - 2;
		int32_t heap_index = stack[pos].i;
		int32_t page_index = stack[pos + 1].i;
		if (page_index_valid(heap_index) && heap[heap_index].page
				&& page_index >= 0 && page_index + count <= heap[heap_index].page->nr_vars) {
			if (count > 0)
				memcpy(&heap[heap_index].page->values[page_index], &stack[pos + 2], count * sizeof(union vm_value));
		} else if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT) {
			WARNING("Out of bounds X_ASSIGN(%d): %d/%d at 0x%zx in %s", count, heap_index, page_index,
				instr_ptr, call_stack_ptr > 0 ? ain->functions[call_stack[call_stack_ptr-1].fno].name : "?");
		}
		if (count > 0)
			memmove(&stack[pos], &stack[pos + 2], count * sizeof(union vm_value));
		stack_ptr -= 2;
		break;
	}
	case X_A_INIT: {
		int init_elems = get_argument(0);
		int count = stack_pop().i;
		int32_t varno = stack_pop().i;
		int32_t pageno = stack_pop().i;
		if (count < 0)
			count = 0;
		const struct ain_type *var_t = lookup_expected_var_type(pageno, varno);
		if (var_t && var_t->data == AIN_WRAP)
			var_t = var_t->array_type;
		const struct ain_type *elem_t = (var_t && var_t->array_type) ? var_t->array_type : NULL;
		struct page *arr;
		if (init_elems) {
			arr = v12_array_new(elem_t, count, true);
		} else {
			int width = elem_t ? ain_type_slot_width(elem_t) : 1;
			arr = alloc_page(ARRAY_PAGE, AIN_ARRAY, count * width);
			arr->array.rank = 1;
			arr->array.struct_type = elem_t ? elem_t->struc : -1;
			arr->array.element_type = elem_t ? *elem_t : (struct ain_type){ .data = AIN_INT, .struc = -1 };
			for (int i = 0; i < count; i++) {
				if (elem_t && elem_t->data == AIN_OPTION) {
					arr->values[i * width].i = -1;
					if (width == 3)
						arr->values[i * width + 1].i = 0;
					arr->values[i * width + width - 1].i = 1;
				} else if (elem_t && ain_type_is_null_handle(elem_t->data)) {
					arr->values[i * width].i = -1;
				}
			}
		}
		int arr_slot = heap_alloc_page(arr);
		if (page_index_valid(pageno) && heap[pageno].page && varno >= 0 && varno < heap[pageno].page->nr_vars) {
			heap[pageno].page->values[varno].i = arr_slot;
		} else if (invalid_var_warn_count++ < INVALID_VAR_WARN_LIMIT) {
			WARNING("Out of bounds X_A_INIT(%d): %d/%d", init_elems, pageno, varno);
		}
		stack_push(arr_slot);
		break;
	}
	case X_A_SIZE: {
		int arr_slot = stack_pop().i;
		if (page_index_valid(arr_slot) && heap[arr_slot].page && heap[arr_slot].page->type == ARRAY_PAGE) {
			stack_push(v12_array_length(heap[arr_slot].page));
		} else {
			stack_push(0);
		}
		break;
	}
	case X_TO_STR: {
		int type = get_argument(0);
		switch (type) {
		case AIN_INT:
		case AIN_LONG_INT:
		case AIN_ENUM:
			stack_push_string(integer_to_string(stack_pop().i));
			break;
		case AIN_FLOAT:
			stack_push_string(float_to_string(stack_pop().f, -1));
			break;
		case AIN_BOOL: {
			int b = stack_pop().i;
			stack_push_string(b ? make_string("true", 4) : make_string("false", 5));
			break;
		}
		default:
			VM_ERROR("Unsupported X_TO_STR type: %d", type);
		}
		break;
	}
	// -- NOOPs ---
	case FUNC:
		break;
	case ENDFUNC:
		function_return();
		return RETURN;
	default:
#ifdef DEBUGGER_ENABLED
		if ((opcode & OPTYPE_MASK) == BREAKPOINT) {
			dbg_handle_breakpoint();
			return execute_instruction(opcode & ~OPTYPE_MASK);
		}
#endif
		VM_ERROR("Illegal opcode: 0x%04x", opcode);
	}
	return opcode;
}

static void vm_execute(void)
{
	unsigned checkpoint_counter = 0;
	for (;;) {
		uint16_t opcode;
		if (instr_ptr == VM_RETURN)
			return;
		if (unlikely(instr_ptr + 2 > ain->code_size)) {
			VM_ERROR("Illegal instruction pointer: 0x%08lX", instr_ptr);
		}
		stack_ensure(64);
		opcode = get_opcode(instr_ptr);
		uint16_t raw_op = opcode & ~OPTYPE_MASK;
		if (unlikely(raw_op < NR_OPCODES && instr_ptr + (size_t)instruction_width(raw_op) > ain->code_size)) {
			VM_ERROR("Truncated instruction 0x%04x at 0x%08lX", opcode, instr_ptr);
		}
		opcode = execute_instruction(opcode);
		instr_ptr += instructions[opcode].ip_inc;
	}
}

static void call_global_destructors(void)
{
	if (heap_size <= 0 || heap[0].ref <= 0)
		return;
	struct page *global_page = heap_get_page(0);
	// Call global variable destructors, but do not unref them because the
	// destructors may reference other global variables.
	for (int i = global_page->nr_vars - 1; i >= 0; i--) {
		if (variable_type(global_page, i, NULL, NULL) != AIN_STRUCT)
			continue;
		int slot = global_page->values[i].i;
		if (slot >= 0 && page_index_valid(slot) && heap[slot].page)
			delete_struct(heap[slot].page->index, slot);
	}
}

static void vm_free(void)
{
	if (game_dungeons_and_dolls) {
		// Dungeons & Dolls saves the game state in destructors of global variables
		call_global_destructors();
	}

	// call library exit routines
	exit_libraries();
	// flush call stack
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		if (call_stack[i].struct_page >= 0 && AIN_VERSION_GTE(ain, 6, 1))
			exit_unref(call_stack[i].struct_page);
		exit_unref(call_stack[i].page_slot);
	}
	// free globals
	if (heap_size > 0 && heap[0].ref > 0)
		exit_unref(0);

	savedata_reset();
	vm_reset_once = true;
}

static jmp_buf reset_buf;

#ifdef ALICEHUB_IOS
static bool session_active;
void unlink_libraries(void);

void vm_end_session(void)
{
	if (session_active) {
		session_active = false;
		savedata_flush_on_background();
		vm_free();
	}
	unlink_libraries();
	ain = NULL;
	page_end_session();
	modifier_end_session();
	call_stack_ptr = 0;
	vm_reset_once = false;
}
#endif

_Noreturn void vm_reset(void)
{
	vm_free();
	longjmp(reset_buf, 1);
}

int vm_execute_ain(struct ain *program)
{
	ain = program;
	init_lambda_parents();
	setjmp(reset_buf);
	modifier_on_ain_init();

	// initialize VM state
	if (!stack) {
		stack_size = INITIAL_STACK_SIZE;
		stack = xmalloc(INITIAL_STACK_SIZE * sizeof(union vm_value));
	}
	stack_ptr = 0;
	call_stack_ptr = 0;
	dg_callbegin_pending = false;
	last_dummy_type = NULL;
	invalid_var_warn_count = 0;

	heap_init();
	init_libraries();
#ifdef ALICEHUB_IOS
	session_active = true;
#endif

	// Initialize globals
	heap[0].ref = 1;
	heap[0].seq = heap_next_seq++;
	heap_set_page(0, alloc_page(GLOBAL_PAGE, 0, ain->nr_globals));
	if (ain->version >= 14) {
		for (int i = 0; i < ain->nr_globals; i++) {
			int w = variable_init_typed(&heap[0].page->values[i], &ain->globals[i].type, false, false);
			i += w - 1;
		}
	} else {
		for (int i = 0; i < ain->nr_globals; i++) {
			if (ain->globals[i].type.data == AIN_STRUCT) {
				// XXX: need to allocate storage for global structs BEFORE calling
				//      constructors.
				heap[0].page->values[i].i = alloc_struct(ain->globals[i].type.struc);
			} else {
				heap[0].page->values[i] = variable_initval_typed(&ain->globals[i].type);
			}
		}
	}
	for (int i = 0; i < ain->nr_initvals; i++) {
		int32_t index;
		struct ain_initval *v = &ain->global_initvals[i];
		if (v->global_index < 0 || v->global_index >= ain->nr_globals) {
			WARNING("Out of bounds global initval index: %d", v->global_index);
			continue;
		}
		switch (v->data_type) {
		case AIN_STRING: {
			int32_t old_slot = heap[0].page->values[v->global_index].i;
			index = heap_alloc_slot(VM_STRING);
			heap[0].page->values[v->global_index].i = index;
			heap[index].s = v->string_value
				? make_string(v->string_value, strlen(v->string_value))
				: string_ref(&EMPTY_STRING);
			if (old_slot > 0 && heap_index_valid(old_slot))
				exit_unref(old_slot);
			break;
		}
		case AIN_REF_STRING:
		case AIN_REF_STRUCT:
		case AIN_REF_DELEGATE:
		case AIN_REF_ARRAY_TYPE:
		case AIN_REF_ARRAY:
			// Resolved in the second pass once target globals have valid heap slots.
			break;
		default: {
			enum ain_data_type gdt = ain->globals[v->global_index].type.data;
			switch (gdt) {
			case AIN_STRING:
			case AIN_STRUCT:
			case AIN_DELEGATE:
			case AIN_ARRAY_TYPE:
			case AIN_ARRAY: {
				int32_t old_slot = heap[0].page->values[v->global_index].i;
				if (old_slot > 0 && heap_index_valid(old_slot))
					exit_unref(old_slot);
				break;
			}
			default:
				break;
			}
			heap[0].page->values[v->global_index].i = v->int_value;
			break;
		}
		}
	}
	for (int i = 0; i < ain->nr_initvals; i++) {
		struct ain_initval *v = &ain->global_initvals[i];
		if (v->global_index < 0 || v->global_index >= ain->nr_globals)
			continue;
		switch (v->data_type) {
		case AIN_REF_STRING:
		case AIN_REF_STRUCT:
		case AIN_REF_DELEGATE:
		case AIN_REF_ARRAY_TYPE:
		case AIN_REF_ARRAY: {
			int32_t old_slot = heap[0].page->values[v->global_index].i;
			if (v->int_value >= 0 && v->int_value < ain->nr_globals) {
				int32_t slot = heap[0].page->values[v->int_value].i;
				heap[0].page->values[v->global_index].i = slot;
				if (slot > 0 && heap_index_valid(slot))
					heap_ref(slot);
			} else {
				heap[0].page->values[v->global_index].i = -1;
			}
			if (old_slot > 0 && heap_index_valid(old_slot))
				exit_unref(old_slot);
			break;
		}
		default:
			break;
		}
	}

	// Reference initializers contain global variable indices, not heap slots.
	// Resolve them after applying ordinary initializers.
	for (int i = 0; i < ain->nr_initvals; i++) {
		struct ain_initval *v = &ain->global_initvals[i];
		switch (ain->globals[v->global_index].type.data) {
		case AIN_REF_STRING:
		case AIN_REF_STRUCT:
		case AIN_REF_ARRAY_TYPE:
		case AIN_REF_ARRAY:
		case AIN_REF_DELEGATE:
			if (v->int_value != -1) {
				int slot = global_get(v->int_value).i;
				heap[0].page->values[v->global_index].i = slot;
				heap_ref(slot);
			}
			break;
		case AIN_REF_INT:
		case AIN_REF_FLOAT:
		case AIN_REF_BOOL:
		case AIN_REF_LONG_INT:
		case AIN_REF_FUNC_TYPE:
			if (v->int_value != -1) {
				heap[0].page->values[v->global_index].i = 0;
				heap[0].page->values[v->global_index + 1].i = v->int_value;
				heap_ref(0);
			}
			break;
		default:
			break;
		}
	}

	if (ain->alloc >= 0)
		vm_call(ain->alloc, -1); // function "0": allocate global arrays (and all globals in v14)

	if (ain->version < 14) {
		// XXX: global constructors must be called AFTER initializing non-struct variables
		//      otherwise a global set in a constructor will be clobbered by its initval
		for (int i = 0; i < ain->nr_globals; i++) {
			if (ain->globals[i].type.data == AIN_STRUCT)
				init_struct(ain->globals[i].type.struc, heap[0].page->values[i].i);
		}
	}

	vm_call(ain->main, -1);
	return stack_pop().i;
}

void vm_stack_trace(void)
{
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		struct ain_function *f = &ain->functions[call_stack[i].fno];
		uint32_t addr = (i == call_stack_ptr - 1) ? instr_ptr : call_stack[i+1].call_address;
		sys_warning("\t0x%08x in %s\n", addr, display_sjis0(f->name));
	}
}

static void sbuf_puts(char *buf, size_t size, size_t *off, const char *s)
{
	if (!s || *off + 1 >= size)
		return;
	while (*s && *off + 1 < size) {
		buf[(*off)++] = *s++;
	}
	buf[*off] = '\0';
}

static void sbuf_put_hex(char *buf, size_t size, size_t *off, uint64_t val, int width)
{
	static const char hex[] = "0123456789abcdef";
	char tmp[16];
	int i = 0;
	do {
		tmp[i++] = hex[val & 0xf];
		val >>= 4;
	} while (val && i < 16);
	while (i < width && i < 16)
		tmp[i++] = '0';
	while (i > 0 && *off + 1 < size)
		buf[(*off)++] = tmp[--i];
	if (size > 0)
		buf[*off] = '\0';
}

static void sbuf_put_i64(char *buf, size_t size, size_t *off, int64_t val)
{
	char tmp[24];
	uint64_t u;
	if (val < 0) {
		sbuf_puts(buf, size, off, "-");
		u = (uint64_t)(-(val + 1)) + 1;
	} else {
		u = (uint64_t)val;
	}
	int i = 0;
	do {
		tmp[i++] = (char)('0' + (u % 10));
		u /= 10;
	} while (u && i < 24);
	while (i > 0 && *off + 1 < size)
		buf[(*off)++] = tmp[--i];
	if (size > 0)
		buf[*off] = '\0';
}

size_t vm_format_stack_trace(char *buf, size_t size, bool safe_signal)
{
	if (!buf || size == 0)
		return 0;
	size_t off = 0;
	buf[0] = '\0';

	if (!ain) {
		sbuf_puts(buf, size, &off, "  (AIN VM not initialized)\n");
		return off;
	}

	int16_t opcode = -1;
	const char *op_name = "INVALID_IP";
	if (ain->code && instr_ptr + 2 <= ain->code_size) {
		opcode = get_opcode(instr_ptr);
		op_name = current_instruction_name();
	}
	const char *hll_lib = NULL;
	const char *hll_func = NULL;
	vm_get_last_hll_call(&hll_lib, &hll_func);

	sbuf_puts(buf, size, &off, "  instr_ptr      : 0x");
	sbuf_put_hex(buf, size, &off, (uint64_t)instr_ptr, 8);
	sbuf_puts(buf, size, &off, " (opcode=0x");
	sbuf_put_hex(buf, size, &off, (uint64_t)(uint16_t)opcode, 4);
	sbuf_puts(buf, size, &off, " ");
	sbuf_puts(buf, size, &off, op_name ? op_name : "?");
	sbuf_puts(buf, size, &off, ")\n  stack_ptr      : ");
	sbuf_put_i64(buf, size, &off, stack_ptr);
	sbuf_puts(buf, size, &off, "\n  call_stack_ptr : ");
	sbuf_put_i64(buf, size, &off, call_stack_ptr);
	sbuf_puts(buf, size, &off, "\n  last_hll_call  : ");
	sbuf_puts(buf, size, &off, hll_lib ? hll_lib : "(none)");
	sbuf_puts(buf, size, &off, ".");
	sbuf_puts(buf, size, &off, hll_func ? hll_func : "(none)");
	sbuf_puts(buf, size, &off, "\n  call_stack     :\n");

	int max_frames = call_stack_ptr;
	if (max_frames > 64)
		max_frames = 64;
	for (int idx = 0; idx < max_frames && off + 1 < size; idx++) {
		int i = call_stack_ptr - 1 - idx;
		if (i < 0 || i >= 4096)
			break;
		int fno = call_stack[i].fno;
		uint32_t addr = (i == call_stack_ptr - 1) ? (uint32_t)instr_ptr : call_stack[i + 1].call_address;
		const char *fname = "<invalid fno>";
		if (ain->functions && fno >= 0 && fno < ain->nr_functions && ain->functions[fno].name) {
			fname = safe_signal ? ain->functions[fno].name : display_sjis0(ain->functions[fno].name);
		}
		sbuf_puts(buf, size, &off, "    #");
		if (idx < 10)
			sbuf_puts(buf, size, &off, "0");
		sbuf_put_i64(buf, size, &off, idx);
		sbuf_puts(buf, size, &off, " 0x");
		sbuf_put_hex(buf, size, &off, addr, 8);
		sbuf_puts(buf, size, &off, " in ");
		sbuf_puts(buf, size, &off, fname);
		sbuf_puts(buf, size, &off, " (fno=");
		sbuf_put_i64(buf, size, &off, fno);
		sbuf_puts(buf, size, &off, ", page=");
		sbuf_put_i64(buf, size, &off, call_stack[i].page_slot);
		sbuf_puts(buf, size, &off, ", struct_page=");
		sbuf_put_i64(buf, size, &off, call_stack[i].struct_page);
		sbuf_puts(buf, size, &off, ")\n");
	}
	return off;
}

_Noreturn void _vm_error(const char *fmt, ...)
{
	// Format before stack tracing: display_sjis0() reuses its string buffer.
	char msg[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	sys_warning("%s", msg);
	sys_warning("at %s (0x%X) in:\n", current_instruction_name(), instr_ptr);
	vm_stack_trace();



	dbg_repl(DBG_STOP_ERROR, msg);
	sys_error("%s", msg);
}

int vm_time(void)
{
	return SDL_GetTicks();
}

void vm_sleep(int ms)
{
	gfx_android_delay(ms);
}

_Noreturn void vm_exit(int code)
{
	sys_warning("vm_exit(%d) at %s (0x%X) in:\n", code, current_instruction_name(), instr_ptr);
	vm_stack_trace();
	if (code == 0)
		savedata_flush_on_background();
	vm_free();
#ifdef ALICEHUB_IOS
	session_active = false;
#endif
#ifdef DEBUG_HEAP
	for (size_t i = 0; i < heap_size; i++) {
		if (heap[i].ref > 0)
			heap_describe_slot(i);
	}
	sys_message("Number of leaked objects: %d\n", heap_free_ptr);
#endif
	sys_exit(code);
}
