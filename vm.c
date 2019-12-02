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
#include <string.h>
#include <math.h>
#include <time.h>

#include "system4.h"
#include "vm.h"
#include "vm_string.h"
#include "page.h"
#include "ain.h"
#include "instructions.h"
#include "little_endian.h"
#include "utfsjis.h"

#define INITIAL_STACK_SIZE 4096
#define INITIAL_HEAP_SIZE  4096
#define HEAP_ALLOC_STEP    4096
#define HLL_MAX_ARGS 64

// When the IP is set to VM_RETURN, the VM halts
#define VM_RETURN 0xFFFFFFFF

/*
 * NOTE: The current implementation is a simple bytecode interpreter.
 *       System40.exe uses a JIT compiler, and we should too.
 */

struct function_call {
	int32_t fno;
	uint32_t return_address;
	int32_t page_slot;
	int32_t struct_page;
};

// The stack
union vm_value *stack = NULL; // the stack
int32_t stack_ptr = 0;        // pointer to the top of the stack
static size_t stack_size;     // current size of the stack

// The heap
// An array of pointers to heap-allocated objects, plus reference counts.
struct vm_pointer *heap;
static size_t heap_size;

// Heap free list
// This is a list of unused indices into the 'heap' array.
static int32_t *heap_free_stack;
static size_t heap_free_ptr = 0;

// Stack of function call frames
static struct function_call call_stack[4096];
static int32_t call_stack_ptr = 0; // 0 = imaginary frame before main()

struct ain *ain;
static size_t instr_ptr = 0;

// Read the opcode at ADDR.
static int16_t get_opcode(size_t addr)
{
	return LittleEndian_getW(ain->code, addr);
}

// Read argument N for the current instruction.
static int32_t get_argument(int n)
{
	return LittleEndian_getDW(ain->code, instr_ptr + 2 + n*4);
}

// XXX: not strictly portable
static float get_argument_float(int n)
{
	union vm_value v;
	v.i = LittleEndian_getDW(ain->code, instr_ptr + 2 + n*4);
	return v.f;
}

static const char *current_instruction_name(void)
{
	int16_t opcode = get_opcode(instr_ptr);
	if (opcode >= 0 && opcode < NR_OPCODES)
		return instructions[opcode].name;
	return "UNKNOWN OPCODE";
}

int32_t heap_alloc_slot(enum vm_pointer_type type)
{
	// grow heap if needed
	if (heap_free_ptr >= heap_size) {
		heap = xrealloc(heap, sizeof(struct vm_pointer) * (heap_size+HEAP_ALLOC_STEP));
		heap_free_stack = xrealloc(heap_free_stack, sizeof(int32_t) * (heap_size+HEAP_ALLOC_STEP));
		for (size_t i = heap_size; i < heap_size+HEAP_ALLOC_STEP; i++) {
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
	heap[slot].ref++;
#ifdef DEBUG_HEAP
	heap[slot].ref_addr = instr_ptr;
#endif
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

void heap_unref(int slot)
{
	if (heap[slot].ref <= 0) {
		VM_ERROR("double free of slot %d (%s)", slot, vm_ptrtype_string(heap[slot].type));
	}
	if (--heap[slot].ref <= 0) {
		switch (heap[slot].type) {
		case VM_PAGE:
			if (heap[slot].page) {
				delete_page(heap[slot].page);
				free_page(heap[slot].page);
			}
			break;
		case VM_STRING:
			free_string(heap[slot].s);
			break;
		}
		heap_free_slot(slot);
	}
}

static int local_page_slot(void)
{
	return call_stack[call_stack_ptr-1].page_slot;
}

static union vm_value *local_page(void)
{
	return heap[local_page_slot()].page->values;
}

static union vm_value local_get(int varno)
{
	return local_page()[varno];
}

static void local_set(int varno, int32_t value)
{
	local_page()[varno].i = value;
}

static union vm_value *local_ptr(int varno)
{
	return local_page() + varno;
}

static union vm_value global_get(int varno)
{
	return heap[0].page->values[varno];
}

static int32_t struct_page_slot(void)
{
	return call_stack[call_stack_ptr-1].struct_page;
}

static union vm_value *struct_page(void)
{
	return heap[struct_page_slot()].page->values;
}

static union vm_value stack_peek(int n)
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
	int32_t page_index = stack_pop().i;
	int32_t heap_index = stack_pop().i;
	if (!heap[heap_index].page || page_index >= heap[heap_index].page->nr_vars)
		VM_ERROR("Out of bounds page index: %d/%d", heap_index, page_index);
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
	return heap[stack_peek(n).i].s;
}

int vm_string_ref(struct string *s)
{
	int slot = heap_alloc_slot(VM_STRING);
	heap[slot].s = string_ref(s);
	return slot;
}

int vm_copy_page(struct page *page)
{
	int slot = heap_alloc_slot(VM_PAGE);
	heap[slot].page = copy_page(page);
	return slot;
}

union vm_value vm_copy(union vm_value v, enum ain_data_type type)
{
	switch (type) {
	case AIN_STRING:
		return (union vm_value) { .i = vm_string_ref(heap[v.i].s) };
	case AIN_STRUCT:
	case AIN_ARRAY_TYPE:
		return (union vm_value) { .i = vm_copy_page(heap[v.i].page) };
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

static int alloc_scenario_page(const char *fname)
{
	int fno, slot;
	struct ain_function *f;

	if ((fno = get_function_by_name(fname)) < 0)
		VM_ERROR("Invalid scenario function: %s", fname);
	f = &ain->functions[fno];

	slot = heap_alloc_slot(VM_PAGE);
	heap[slot].page = alloc_page(LOCAL_PAGE, fno, f->nr_vars);
	for (int i = 0; i < f->nr_vars; i++) {
		heap[slot].page->values[i] = variable_initval(f->vars[i].data_type);
	}
	return slot;
}

static void scenario_call(int slot)
{
	int fno = heap[slot].page->index;
	// flush call stack
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		heap_unref(call_stack[i].page_slot);
	}
	call_stack[0] = (struct function_call) {
		.fno = fno,
		.return_address = VM_RETURN,
		.page_slot = slot,
		.struct_page = -1
	};
	call_stack_ptr = 1;
	instr_ptr = ain->functions[fno].address;
}

/*
 * System 4 calling convention:
 *   - caller pushes arguments, in order
 *   - CALLFUNC creates stack frame, pops arguments into local page
 *   - callee pushes return value on the stack
 *   - RETURN jumps to return address (saved in stack frame)
 */
static void function_call(int fno, int return_address)
{
	struct ain_function *f = &ain->functions[fno];
	int slot = heap_alloc_slot(VM_PAGE);
	heap[slot].page = alloc_page(LOCAL_PAGE, fno, f->nr_vars);

	call_stack[call_stack_ptr++] = (struct function_call) {
		.fno = fno,
		.return_address = return_address,
		.page_slot = slot,
		.struct_page = -1
	};
	// pop arguments, store in local page
	for (int i = f->nr_args - 1; i >= 0; i--) {
		heap[slot].page->values[i] = stack_pop();
		switch (f->vars[i].data_type) {
		case AIN_REF_TYPE:
			heap_ref(heap[slot].page->values[i].i);
			break;
		}
	}
	// initialize local variables
	for (int i = f->nr_args; i < f->nr_vars; i++) {
		heap[slot].page->values[i] = variable_initval(f->vars[i].data_type);
	}
	// jump to function start
	instr_ptr = ain->functions[fno].address;
}

static void method_call(int fno, int return_address)
{
	function_call(fno, return_address);
	call_stack[call_stack_ptr-1].struct_page = stack_pop().i;
}

static void vm_execute(void);

void vm_call(int fno, int struct_page)
{
	size_t saved_ip = instr_ptr;
	if (struct_page < 0) {
		function_call(fno, VM_RETURN);
	} else {
		stack_push(struct_page);
		method_call(fno, VM_RETURN);
	}
	vm_execute();
	instr_ptr = saved_ip;
}

static void hll_call(int libno, int fno)
{
	struct ain_hll_function *f = &ain->libraries[libno].functions[fno];
	if (!f->fun)
		VM_ERROR("Unimplemented HLL function: %s.%s", ain->libraries[libno].name, f->name);

	union vm_value args[HLL_MAX_ARGS];
	for (int i = f->nr_arguments - 1; i >= 0; i--) {
		int pageno, varno;
		switch(f->arguments[i].data_type) {
		case AIN_REF_INT:
		case AIN_REF_BOOL:
		case AIN_REF_FLOAT:
			stack_ptr -= 2;
			pageno = stack[stack_ptr].i;
			varno = stack[stack_ptr+1].i;
			args[i].ref = &heap[pageno].page->values[varno];
			break;
		default:
			stack_ptr--;
			args[i] = stack[stack_ptr];
		}
	}
	union vm_value r = f->fun(args);
	for (int i = 0; i < f->nr_arguments; i++) {
		// XXX: We don't increase the ref count when passing ref arguments to HLL
		//      functions, so we need to avoid decreasing it via variable_fini
		switch (f->arguments[i].data_type) {
		case AIN_REF_TYPE:
			break;
		default:
			variable_fini(stack[stack_ptr + i], f->arguments[i].data_type);
			break;
		}
	}
	if (f->data_type != AIN_VOID)
		stack_push(r);
}

static void function_return(void)
{
	call_stack_ptr--;
	heap_unref(call_stack[call_stack_ptr].page_slot);
	instr_ptr = call_stack[call_stack_ptr].return_address;
}

static void system_call(int32_t code)
{
	char *utf;
	struct string *str;
	switch (code) {
	case 0x0: // system.Exit(int nResult)
		vm_exit(stack_pop().i);
		break;
	case 0x3: // system.LockPeek()
	case 0x4: // system.UnlockPeek()
		stack_push(1);
		break;
	case 0x6: // system.Output(string szText)
		str = stack_peek_string(0);
		utf = sjis2utf(str->text, str->size);
		sys_message("%s", utf);
		free(utf);
		// XXX: caller S_POPs
		break;
	case 0xC:
		if (config.save_dir)
			stack_push_string(make_string(config.save_dir, strlen(config.save_dir)));
		else
			stack_push_string(string_ref(&EMPTY_STRING));
		break;
	case 0xD:
		stack_push(vm_time());
		break;
	case 0x14: // system.Peek()
		break;
	case 0x15: // system.Sleep(int nSleep)
		stack_pop();
		break;
	default:
		WARNING("Unimplemented syscall: 0x%X", code);
		vm_stack_trace();
	}
}

void exec_switch(int no, int val)
{
	struct ain_switch *s = &ain->switches[no];
	for (int i = 0; i < s->nr_cases; i++) {
		if (s->cases[i].value == val) {
			instr_ptr = s->cases[i].address;
			return;
		}
	}
	if (s->default_address > 0)
		instr_ptr = s->default_address;
	else
		instr_ptr += instruction_width(SWITCH);
}

void exec_strswitch(int no, struct string *str)
{
	struct ain_switch *s = &ain->switches[no];
	for (int i = 0; i < s->nr_cases; i++) {
		if (!strcmp(str->text, ain->strings[s->cases[i].value]->text)) {
			instr_ptr = s->cases[i].address;
			return;
		}
	}
	if (s->default_address > 0)
		instr_ptr = s->default_address;
	else
		instr_ptr += instruction_width(STRSWITCH);
}

static void execute_instruction(enum opcode opcode)
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
	case SH_STRUCTREF: { // VARNO
		stack_push(struct_page()[get_argument(0)]);
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
		int dst_page = stack_pop().i;
		heap[dst_page].page->values[dst_var].i = src_page;
		heap[dst_page].page->values[dst_var+1].i = src_var;
		stack_push(src_page);
		stack_push(src_var);
		break;
	}
	case DELETE: {
		int slot = stack_pop().i;
		if (slot != -1)
			heap_unref(slot);
		break;
	}
	case SP_INC: {
		heap_ref(stack_pop().i);
		break;
	}
	//
	// --- Control Flow ---
	//
	case CALLFUNC: {
		function_call(get_argument(0), instr_ptr + instruction_width(CALLFUNC));
		break;
	}
	case CALLFUNC2: {
		stack_pop(); // function-type index (only needed for compilation)
		function_call(stack_pop().i, instr_ptr + instruction_width(CALLFUNC2));
		break;
	}
	case CALLMETHOD: {
		method_call(get_argument(0), instr_ptr + instruction_width(CALLMETHOD));
		break;
	}
	case CALLHLL: {
		hll_call(get_argument(0), get_argument(1));
		break;
	}
	case RETURN: {
		function_return();
		break;
	}
	case CALLSYS: {
		system_call(get_argument(0));
		break;
	}
	case CALLONJUMP: {
		int str = stack_pop().i;
		// XXX: I am GUESSING that the VM pre-allocates the scenario function's
		//      local page here. It certainly pushes what appears to be a page
		//      index to the stack.
		stack_push(alloc_scenario_page(heap[str].s->text));
		heap_unref(str);
		break;
	}
	case SJUMP: {
		scenario_call(stack_pop().i);
		break;
	}
	case MSG: {
		if (ain->msgf < 0)
			break;
		stack_push(get_argument(0));
		stack_push(ain->nr_messages);
		stack_push_string(string_ref(ain->messages[get_argument(0)]));
		function_call(ain->msgf, instr_ptr + instruction_width(MSG));
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
		exec_switch(get_argument(0), stack_pop().i);
		break;
	}
	case STRSWITCH: {
		int str = stack_pop().i;
		exec_strswitch(get_argument(0), heap[str].s);
		heap_unref(str);
		break;
	}
	case ASSERT: {
		int line = stack_pop().i; // line number
		int file = stack_pop().i; // filename
		int expr = stack_pop().i; // expression
		if (!stack_pop().i) {
			char *filename = sjis2utf(heap[file].s->text, heap[file].s->size);
			char *value = sjis2utf(heap[expr].s->text, heap[expr].s->size);
			sys_message("Assertion failed at %s:%d: %s\n", filename, line, value);
			free(filename);
			free(value);
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
		stack[stack_ptr-1].i = -stack[stack_ptr-1].i;
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
		stack[stack_ptr-2].i += stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case SUB: {
		stack[stack_ptr-2].i -= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case MUL: {
		stack[stack_ptr-2].i *= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case DIV: {
		stack[stack_ptr-2].i /= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case MOD: {
		stack[stack_ptr-2].i %= stack[stack_ptr-1].i;
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
		stack[stack_ptr-2].i <<= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case RSHIFT: {
		stack[stack_ptr-2].i >>= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	// Numeric Comparisons
	case LT: {
		int b = stack_pop().i;
		int a = stack_pop().i;
		stack_push(a < b ? 1 : 0);
		break;
	}
	case GT: {
		int b = stack_pop().i;
		int a = stack_pop().i;
		stack_push(a > b ? 1 : 0);
		break;
	}
	case LTE: {
		int b = stack_pop().i;
		int a = stack_pop().i;
		stack_push(a <= b ? 1 : 0);
		break;
	}
	case GTE: {
		int b = stack_pop().i;
		int a = stack_pop().i;
		stack_push(a >= b ? 1 : 0);
		break;
	}
	case NOTE: {
		int b = stack_pop().i;
		int a = stack_pop().i;
		stack_push(a != b ? 1 : 0);
		break;
	}
	case EQUALE: {
		int b = stack_pop().i;
		int a = stack_pop().i;
		stack_push(a == b ? 1 : 0);
		break;
	}
	// +=, -=, etc.
	case PLUSA: {
		int n = stack_pop().i;
		stack_push(stack_pop_var()[0].i += n);
		break;
	}
	case MINUSA: {
		int n = stack_pop().i;
		stack_push(stack_pop_var()[0].i -= n);
		break;
	}
	case MULA: {
		int n = stack_pop().i;
		stack_push(stack_pop_var()[0].i *= n);
		break;
	}
	case DIVA: {
		int n = stack_pop().i;
		stack_push(stack_pop_var()[0].i /= n);
		break;
	}
	case MODA: {
		int n = stack_pop().i;
		stack_push(stack_pop_var()[0].i %= n);
		break;
	}
	case ANDA: {
		int n = stack_pop().i;
		stack_push(stack_pop_var()[0].i &= n);
		break;
	}
	case ORA: {
		int n = stack_pop().i;
		stack_push(stack_pop_var()[0].i |= n);
		break;
	}
	case XORA: {
		int n = stack_pop().i;
		stack_push(stack_pop_var()[0].i ^= n);
		break;
	}
	case LSHIFTA: {
		int n = stack_pop().i;
		stack_push(stack_pop_var()[0].i <<= n);
		break;
	}
	case RSHIFTA: {
		int n = stack_pop().i;
		stack_push(stack_pop_var()[0].i >>= n);
		break;
	}
	case INC: {
		stack_pop_var()[0].i++;
		break;
	}
	case DEC: {
		stack_pop_var()[0].i--;
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
		stack_set(0, (int64_t)stack_peek(0).i);
		break;
	}
	//
	// --- Floating Point Arithmetic ---
	//
	case FTOI: {
		stack_set(0, (int32_t)stack_peek(0).f);
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
		stack_push(stack_pop_var()->f /= n);
		break;
	}
	//
	// --- Strings ---
	//
	case S_PUSH: {
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
		stack_push_string(string_ref(heap[str].s));
		break;
	}
	//case S_REFREF: // ???: why/how is this different from regular REFREF?
	case S_ASSIGN: { // A = B
		int rval = stack_peek(0).i;
		int lval = stack_peek(1).i;
		if (heap[lval].s) {
			free_string(heap[lval].s);
		}
		heap[lval].s = string_ref(heap[rval].s);
		// remove A from the stack, but leave B
		stack_set(1, rval);
		stack_pop();
		break;
	}
	case S_PLUSA2: {
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
		stack_push_string(string_concatenate(heap[a].s, heap[b].s));
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
		bool noteq = !!strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text);
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(noteq);
		break;
	}
	case S_EQUALE: {
		bool eq = !strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text);
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(eq);
		break;
	}
	case S_LENGTH: {
		int str = stack_pop_var()->i;
		stack_push(sjis_count_char(heap[str].s->text));
		break;
	}
	case S_LENGTH2: {
		int str = stack_pop().i;
		stack_push(sjis_count_char(heap[str].s->text));
		heap_unref(str);
		break;
	}
	case S_LENGTHBYTE: {
		int str = stack_pop_var()->i;
		stack_push(heap[str].s->size);
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
		stack_pop(); // ???
		union vm_value val = stack_pop();
		int fmt = stack_pop().i;
		int dst = heap_alloc_slot(VM_STRING);
		heap[dst].s = string_format(heap[fmt].s, val);
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
	// --- Characters ---
	case C_REF: {
		int i = stack_pop().i;
		int str = stack_pop().i;
		stack_push(string_get_char(heap[str].s, i));
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
		stack_push(vm_copy_page(heap[stack_pop_var()->i].page));
		break;
	}
	case SR_POP: {
		heap_unref(stack_pop().i);
		break;
	}
	case SR_ASSIGN: {
		stack_pop(); // struct type
		int rval = stack_pop().i;
		int lval = stack_pop().i;
		if (lval == -1)
			VM_ERROR("Assignment to null-pointer");
		if (heap[lval].page) {
			delete_page(heap[lval].page);
			free_page(heap[lval].page);
		}
		heap[lval].page = copy_page(heap[rval].page);
		stack_push(rval);
		break;
	}
	//
	// -- Arrays --
	//
	case A_ALLOC: {
		enum ain_data_type struct_type;
		int rank = stack_pop().i;
		int varno = stack_peek(rank).i;
		int pageno = stack_peek(rank+1).i;
		int array = heap[pageno].page->values[varno].i;
		enum ain_data_type data_type = variable_type(heap[pageno].page, varno, &struct_type);
		heap[array].page = alloc_array(rank, stack_peek_ptr(rank-1), data_type, struct_type, true);
		stack_ptr -= rank + 2;
		break;
	}
	case A_REALLOC: {
		enum ain_data_type struct_type;
		int rank = stack_pop().i; // rank
		int varno = stack_peek(rank).i;
		int pageno = stack_peek(rank+1).i;
		int array = heap[pageno].page->values[varno].i;
		enum ain_data_type data_type = variable_type(heap[pageno].page, varno, &struct_type);
		heap[array].page = realloc_array(heap[array].page, rank, stack_peek_ptr(rank-1), data_type, struct_type, true);
		stack_ptr -= rank + 2;
		break;
	}
	case A_FREE: {
		int array = stack_pop_var()->i;
		if (heap[array].page) {
			delete_page(heap[array].page);
			free_page(heap[array].page);
			heap[array].page = NULL;
		}
		break;
	}
	case A_REF: {
		int array = stack_pop().i;
		int slot = heap_alloc_slot(VM_PAGE);
		heap[slot].page = copy_page(heap[array].page);
		stack_push(slot);
		break;
	}
	case A_NUMOF: {
		int rank = stack_pop().i; // rank
		int array = stack_pop_var()->i;
		stack_push(array_numof(heap[array].page, rank));
		break;
	}
	case A_COPY: {
		int n = stack_pop().i;
		int src_i = stack_pop().i;
		int src = stack_pop().i;
		int dst_i = stack_pop().i;
		int dst = stack_pop_var()->i;
		array_copy(heap[dst].page, dst_i, heap[src].page, src_i, n);
		stack_push(n);
		break;
	}
	case A_FILL: {
		union vm_value val = stack_pop();
		int n = stack_pop().i;
		int i = stack_pop().i;
		int array = stack_pop_var()->i;
		stack_push(array_fill(heap[array].page, i, n, val));
		break;
	}
	case A_PUSHBACK: {
		enum ain_data_type struct_type;
		union vm_value val = stack_pop();
		int varno = stack_pop().i;
		int pageno = stack_pop().i;
		int array = heap[pageno].page->values[varno].i;
		enum ain_data_type data_type = variable_type(heap[pageno].page, varno, &struct_type);
		array_pushback(&heap[array].page, val, data_type, struct_type);
		break;
	}
	case A_POPBACK: {
		array_popback(&heap[stack_pop_var()->i].page);
		break;
	}
	case A_EMPTY: {
		int array = stack_pop_var()->i;
		stack_push(!heap[array].page);
		break;
	}
	case A_ERASE: {
		int i = stack_pop().i;
		int array = stack_pop_var()->i;
		stack_push(array_erase(&heap[array].page, i));
		break;
	}
	case A_INSERT: {
		enum ain_data_type struct_type;
		union vm_value val = stack_pop();
		int i = stack_pop().i;
		int varno = stack_pop().i;
		int pageno = stack_pop().i;
		int array = heap[pageno].page->values[varno].i;
		enum ain_data_type data_type = variable_type(heap[pageno].page, varno, &struct_type);
		array_insert(&heap[array].page, i, val, data_type, struct_type);
		break;
	}
	case A_SORT: {
		int fno = stack_pop().i;
		int array = stack_pop_var()->i;
		array_sort(heap[array].page, fno);
		break;
	}
	case A_FIND: {
		int fno = stack_pop().i;
		union vm_value v = stack_pop();
		int end = stack_pop().i;
		int start = stack_pop().i;
		int array = stack_pop_var()->i;
		stack_push(array_find(heap[array].page, start, end, v, fno));
	}
	// -- NOOPs ---
	case FUNC:
		break;
	default:
		VM_ERROR("Unimplemented instruction");
	}
}

static void vm_execute(void)
{
	for (;;) {
		uint16_t opcode;
		if (instr_ptr == VM_RETURN)
			return;
		if (instr_ptr >= ain->code_size) {
			VM_ERROR("Illegal instruction pointer: 0x%08lX", instr_ptr);
		}
		opcode = get_opcode(instr_ptr);
		if (opcode >= NR_OPCODES) {
			VM_ERROR("Illegal opcode: 0x%04X", opcode);
		}
		execute_instruction(opcode);
		instr_ptr += instructions[opcode].ip_inc;
	}
}

extern struct library lib_ACXLoader;
extern struct library lib_AliceLogo;
extern struct library lib_AliceLogo2;
extern struct library lib_AliceLogo3;
extern struct library lib_Confirm2;
extern struct library lib_DrawGraph;
extern struct library lib_DrawPluginManager;
extern struct library lib_Math;
extern struct library lib_MsgLogManager;
extern struct library lib_MsgSkip;
extern struct library lib_OutputLog;
extern struct library lib_PlayMovie;
extern struct library lib_SACT2;
extern struct library lib_SystemServiceEx;

struct library *libraries[] = {
	&lib_ACXLoader,
	&lib_AliceLogo,
	&lib_AliceLogo2,
	&lib_AliceLogo3,
	&lib_Confirm2,
	&lib_DrawGraph,
	&lib_DrawPluginManager,
	&lib_Math,
	&lib_MsgLogManager,
	&lib_MsgSkip,
	&lib_OutputLog,
	&lib_PlayMovie,
	&lib_SACT2,
	&lib_SystemServiceEx,
	NULL
};

static void link_library(struct ain_library *ainlib, struct library *lib)
{
	for (int i = 0; i < ainlib->nr_functions; i++) {
		bool linked = false;
		for (int j = 0; lib->functions[j]; j++) {
			if (!strcmp(ainlib->functions[i].name, lib->functions[j]->name)) {
				ainlib->functions[i].fun = lib->functions[j]->fun;
				linked = true;
				break;
			}
		}
		if (!linked)
			WARNING("Unimplemented library function: %s", ainlib->functions[i].name);
		else if (ainlib->functions[i].nr_arguments >= HLL_MAX_ARGS)
			ERROR("Too many arguments to library function: %s", ainlib->functions[i].name);
	}
}

static void link_libraries(void)
{
	for (int i = 0; i < ain->nr_libraries; i++) {
		bool linked = false;
		for (int j = 0; libraries[j]; j++) {
			if (!strcmp(ain->libraries[i].name, libraries[j]->name)) {
				link_library(&ain->libraries[i], libraries[j]);
				linked = true;
				break;
			}
		}
		if (!linked)
			WARNING("Unimplemented library: %s", ain->libraries[i].name);
	}
}

void vm_execute_ain(struct ain *program)
{
	// initialize VM state
	stack_size = INITIAL_STACK_SIZE;
	stack = xmalloc(INITIAL_STACK_SIZE * sizeof(union vm_value));
	stack_ptr = 0;

	heap_size = INITIAL_HEAP_SIZE;
	heap = xmalloc(INITIAL_HEAP_SIZE * sizeof(struct vm_pointer));

	heap_free_stack = xmalloc(INITIAL_HEAP_SIZE * sizeof(int32_t));
	for (size_t i = 0; i < INITIAL_HEAP_SIZE; i++) {
		heap_free_stack[i] = i;
	}
	heap_free_ptr = 1; // global page at index 0

	ain = program;

	link_libraries();

	// Initialize globals
	heap[0].ref = 1;
	heap[0].page = alloc_page(GLOBAL_PAGE, 0, ain->nr_globals);
	for (int i = 0; i < ain->nr_globals; i++) {
		if (ain->globals[i].data_type == AIN_STRUCT) {
			// XXX: need to allocate storage for global structs BEFORE calling
			//      constructors.
			alloc_struct(ain->globals[i].struct_type, &heap[0].page->values[i]);
		} else {
			heap[0].page->values[i] = variable_initval(ain->globals[i].data_type);
		}
	}
	for (int i = 0; i < ain->nr_initvals; i++) {
		int32_t index;
		struct ain_initval *v = &ain->global_initvals[i];
		switch (v->data_type) {
		case AIN_STRING:
			index = heap_alloc_slot(VM_STRING);
			heap[0].page->values[v->global_index].i = index;
			heap[index].s = make_string(v->string_value, strlen(v->string_value));
			break;
		default:
			heap[0].page->values[v->global_index].i = v->int_value;
			break;
		}
	}

	vm_call(ain->alloc, -1); // function "0": allocate global arrays

	// XXX: global constructors must be called AFTER initializing non-struct variables
	//      otherwise a global set in a constructor will be clobbered by its initval
	for (int i = 0; i < ain->nr_globals; i++) {
		if (ain->globals[i].data_type == AIN_STRUCT)
			init_struct(ain->globals[i].struct_type, heap[0].page->values[i].i);
	}

	vm_call(ain->main, -1);
}

void vm_stack_trace(void)
{
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		struct ain_function *f = &ain->functions[call_stack[i].fno];
		char *u = sjis2utf(f->name, strlen(f->name));
		sys_warning("\t%s\n", u);
		free(u);
	}
}

noreturn void _vm_error(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	sys_vwarning(fmt, ap);
	va_end(ap);
	sys_warning("at %s (0x%X) in:\n", current_instruction_name(), instr_ptr);
	vm_stack_trace();
	sys_exit(1);
}

int vm_time(void)
{
	return clock() / (CLOCKS_PER_SEC / 1000);
}

#ifdef DEBUG_HEAP
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
		sys_message("LOCAL_PAGE: %s\n", ain->functions[page->index].name);
		break;
	case STRUCT_PAGE:
		sys_message("STRUCT_PAGE: %s\n", ain->structures[page->index].name);
		break;
	case ARRAY_PAGE:
		sys_message("ARRAY_PAGE: %s\n", ain_strtype(ain, page->a_type, page->struct_type));
		break;
	}
}

static void describe_slot(size_t slot)
{
	sys_message("[%d](%d)(%08X)(%08X) = ", slot, heap[slot].ref, heap[slot].alloc_addr, heap[slot].ref_addr);
	switch (heap[slot].type) {
	case VM_PAGE:
		describe_page(heap[slot].page);
		break;
	case VM_STRING:
		if (heap[slot].s) {
			char *u = sjis2utf(heap[slot].s->text, heap[slot].s->size);
			sys_message("STRING: %s\n", u);
			free(u);
		} else {
			sys_message("STRING: NULL\n");
		}
		break;
	default:
		sys_message("???\n");
		break;
	}
}
#endif

noreturn void vm_exit(int code)
{
	// flush call stack
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		heap_unref(call_stack[i].page_slot);
	}
	// free globals
	heap_unref(0);
#ifdef DEBUG_HEAP
	for (size_t i = 0; i < heap_size; i++) {
		if (heap[i].ref > 0)
			describe_slot(i);
	}
	sys_message("Number of leaked objects: %d\n", heap_free_ptr);
#endif
	sys_exit(code);
}
