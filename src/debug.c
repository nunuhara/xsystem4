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
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <ctype.h>
#include <assert.h>

#include "system4/dasm.h"
#include "system4/hashtable.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

#include "debugger.h"
#include "little_endian.h"
#include "xsystem4.h"

bool dbg_enabled = true;
bool dbg_start_in_debugger = false;
unsigned dbg_current_frame = 0;
static jmp_buf dbg_continuation;

enum {
	DBG_CONTINUE = 1,
	DBG_QUIT = 2,
};

void dbg_continue(void)
{
	longjmp(dbg_continuation, DBG_CONTINUE);
}

void dbg_quit(void)
{
	longjmp(dbg_continuation, DBG_QUIT);
}

void dbg_start(void(*fun)(void*), void *data)
{
	dbg_current_frame = 0;
	switch (setjmp(dbg_continuation)) {
	case 0:
		break;
	case DBG_CONTINUE:
		return;
	case DBG_QUIT:
		vm_exit(0);
	default:
		ERROR("setjmp returned unexpected value");
	}
	fun(data);
}

static void _dbg_repl(void *_)
{
	dbg_cmd_repl();
}

void dbg_repl(void)
{
	if (!dbg_enabled)
		return;
	dbg_start(_dbg_repl, NULL);
}

void dbg_init(void)
{
	dbg_cmd_init();
#ifdef HAVE_SCHEME
	dbg_scm_init();
#endif
}

void dbg_fini(void)
{
#ifdef HAVE_SCHEME
	dbg_scm_fini();
#endif
}

static struct hash_table *bp_table = NULL;

static void add_breakpoint(uint32_t addr, struct breakpoint *bp)
{
	if (!bp_table)
		bp_table = ht_create(64);

	struct ht_slot *slot = ht_put_int(bp_table, addr, NULL);
	if (slot->value) {
		WARNING("Overwriting breakpoint at %0x%08x", addr);
		free(slot->value);
	}
	slot->value = bp;
}

static void delete_breakpoint(uint32_t addr, struct breakpoint *bp)
{
	// restore opcode
	LittleEndian_putW(ain->code, addr, bp->restore_op);

	// remove from hash table
	struct ht_slot *slot = ht_put_int(bp_table, addr, NULL);
	assert(slot->value == bp);
	slot->value = NULL;

	free(bp->message);
	free(bp);
}

static struct breakpoint *get_breakpoint(uint32_t addr)
{
	if (!bp_table)
		return NULL;
	return ht_get_int(bp_table, addr, NULL);
}

bool dbg_set_function_breakpoint(const char *_name, void(*cb)(struct breakpoint*), void *data)
{
	char *name = utf2sjis(_name, 0);
	int fno = ain_get_function(ain, name);
	free(name);

	if (fno < 0) {
		DBG_ERROR("No function with name '%s'", display_sjis0(_name));
		return false;
	}

	struct ain_function *f = &ain->functions[fno];
	struct breakpoint *bp = xcalloc(1, sizeof(struct breakpoint));
	bp->restore_op = LittleEndian_getW(ain->code, f->address);
	bp->cb = cb;
	bp->data = data;
	bp->message = xmalloc(512);
	snprintf(bp->message, 511, "Hit breakpoint at function '%s' (0x%08x)",
			display_utf0(_name), f->address);
	LittleEndian_putW(ain->code, f->address, BREAKPOINT | bp->restore_op);
	add_breakpoint(f->address, bp);

	printf("Set breakpoint at function '%s' (0x%08x)\n", display_utf0(_name), f->address);
	return true;
}

bool dbg_set_address_breakpoint(uint32_t address, void(*cb)(struct breakpoint*), void *data)
{
	if (address & 1 || address >= ain->code_size - 2) {
		DBG_ERROR("Invalid address: 0x%08x", address);
		return false;
	}

	// XXX: this is not very robust; some illegal addresses will slip through
	//      if the data looks like a valid opcode
	enum opcode op = LittleEndian_getW(ain->code, address);
	if (op < 0 || op >= NR_OPCODES) {
		DBG_ERROR("Invalid address: 0x%08x", address);
		return false;
	}

	struct breakpoint *bp = xcalloc(1, sizeof(struct breakpoint));
	bp->restore_op = op;
	bp->cb = cb;
	bp->data = data;
	bp->message = xmalloc(512);
	snprintf(bp->message, 511, "Hit breakpoint at 0x%08x", address);
	LittleEndian_putW(ain->code, address, BREAKPOINT | bp->restore_op);
	add_breakpoint(address, bp);

	printf("Set breakpoint at 0x%08x\n", address);
	return true;
}

static void dbg_step_breakpoint_cb(struct breakpoint *bp)
{
	// XXX: mutually recursive functions could trigger breakpoint early.
	//      use size of call stack to check for this case
	if ((intptr_t)bp->data != call_stack_ptr)
		return;
	delete_breakpoint(instr_ptr, bp);
	dbg_cmd_repl();
}

static void dbg_set_step_breakpoint(int32_t address, int call_index)
{
	// if a breakpoint is already set on the next address, leave it
	enum opcode op = LittleEndian_getW(ain->code, address);
	if ((op & OPTYPE_MASK) == BREAKPOINT)
		return;

	struct breakpoint *bp = xcalloc(1, sizeof(struct breakpoint));
	bp->restore_op = op & ~OPTYPE_MASK;
	assert(bp->restore_op >= 0 && bp->restore_op < NR_OPCODES);
	bp->cb = dbg_step_breakpoint_cb;
	bp->data = (void*)(intptr_t)call_index;
	bp->message = NULL;
	LittleEndian_putW(ain->code, address, BREAKPOINT | bp->restore_op);
	add_breakpoint(address, bp);
}

static int32_t get_function_address(int fno)
{
	assert(fno > 0 && fno < ain->nr_functions);
	return ain->functions[fno].address;
}

/* Determine the next address at the current instruction. */
static int32_t dbg_next_address(bool into, int *call_index)
{
	*call_index = call_stack_ptr;
	enum opcode current = LittleEndian_getW(ain->code, instr_ptr) & ~OPTYPE_MASK;
	assert(current >= 0 && current < NR_OPCODES);
	switch (current) {
	case JUMP:
		return get_argument(0);
	case IFZ:
		if (stack_peek(0).i == 0)
			return get_argument(0);
		break;
	case IFNZ:
		if (stack_peek(0).i)
			return get_argument(0);
		break;
	case RETURN: {
		*call_index = call_stack_ptr-1;
		return call_stack[call_stack_ptr-1].return_address;
	}
	case _MSG:
		if (!into)
			break;
		// TODO: step into message function
		return -1;
	case SWITCH:
		return get_switch_address(get_argument(0), stack_peek(0).i);
	case STRSWITCH:
		return get_strswitch_address(get_argument(0), heap_get_string(stack_peek(0).i));
	case SJUMP: {
		if (!into)
			break;
		// XXX: can't determine RETURN address of scenario call (VM_RETURN)
		return -1;
		/*
		int fno = heap[stack_peek(0).i].page->index;
		return ain->functions[fno].address;
		*/
	}
	case CALLFUNC:
	case CALLMETHOD:
	case THISCALLMETHOD_NOPARAM:
		if (!into)
			break;
		*call_index = call_stack_ptr + 1;
		return get_function_address(get_argument(0));
	case CALLFUNC2:
		if (!into)
			break;
		*call_index = call_stack_ptr + 1;
		return get_function_address(stack_peek(1).i);
	case SH_IF_LOC_LT_IMM:
		if (local_get(get_argument(0)).i < get_argument(1))
			return get_argument(2);
		break;
	case SH_IF_LOC_GE_IMM:
		if (local_get(get_argument(0)).i >= get_argument(1))
			return get_argument(2);
		break;
	case SH_IF_STRUCTREF_NE_LOCALREF:
		if (member_get(get_argument(0)).i != local_get(get_argument(1)).i)
			return get_argument(2);
		break;
	case SH_IF_STRUCTREF_GT_IMM:
		if (member_get(get_argument(0)).i > get_argument(1))
			return get_argument(2);
		break;
	case SH_STRUCTREF2_CALLMETHOD_NO_PARAM:
		if (!into)
			break;
		*call_index = call_stack_ptr + 1;
		return get_function_address(get_argument(2));
	case SH_IF_STRUCTREF_Z:
		if (!member_get(get_argument(0)).i)
			return get_argument(1);
		break;
	case SH_IF_STRUCT_A_NOT_EMPTY: {
		struct page *array = heap_get_page(member_get(get_argument(0)).i);
		if (array && array->nr_vars)
			return get_argument(1);
		break;
	}
	case SH_IF_LOC_GT_IMM:
		if (local_get(get_argument(0)).i > get_argument(1))
			return get_argument(2);
		break;
	case SH_IF_STRUCTREF_NE_IMM:
		if (member_get(get_argument(0)).i != get_argument(1))
			return get_argument(2);
		break;
	case SH_IF_STRUCTREF_EQ_IMM:
		if (member_get(get_argument(0)).i == get_argument(1))
			return get_argument(2);
		break;
	case SH_IF_SREF_NE_STR0: {
		struct string *a = heap_get_string(stack_peek_var()->i);
		struct string *b = ain->strings[get_argument(0)];
		if (strcmp(a->text, b->text))
			return get_argument(1);
		break;
	}
	case DG_CALL: {
		if (!into)
			return get_argument(1);
		// XXX: can't determine RETURN address of delegate call (VM_RETURN)
		return -1;
	}
	default:
		// XXX: catch any unhandled control-flow instructions
		if (instructions[current].ip_inc == 0)
			return -1;
		break;
	}
	return instr_ptr + instruction_width(current);
}

bool dbg_set_step_over_breakpoint(void)
{
	int call_index;
	int32_t address = dbg_next_address(false, &call_index);
	if (address < 0)
		return false;
	dbg_set_step_breakpoint(address, call_index);
	return true;
}

bool dbg_set_step_into_breakpoint(void)
{
	int call_index;
	int32_t address = dbg_next_address(true, &call_index);
	if (address < 0)
		return false;
	dbg_set_step_breakpoint(address, call_index);
	return true;
}

static void _dbg_handle_breakpoint(void *data)
{
	struct breakpoint *bp = data;
	if (bp->cb) {
		bp->cb(bp);
	} else {
		printf("%s\n", bp->message);
		dbg_cmd_repl();
	}
}

void dbg_handle_breakpoint(void)
{
	struct breakpoint *bp = get_breakpoint(instr_ptr);
	if (!bp) {
		WARNING("Unregistered breakpoint");
		return;
	}
	dbg_start(_dbg_handle_breakpoint, bp);
}

void dbg_print_frame(unsigned no)
{
	if (no >= call_stack_ptr) {
		DBG_ERROR("Invalid frame number: %d", no);
		return;
	}
	unsigned cs_no = call_stack_ptr - (1 + no);
	struct ain_function *f = &ain->functions[call_stack[cs_no].fno];
	uint32_t addr = no ? call_stack[cs_no+1].call_address : instr_ptr;
	printf("%c #%d 0x%08x in %s\n", no == dbg_current_frame ? '*' : ' ',
			no, addr, display_sjis0(f->name));
}

void dbg_print_stack_trace(void)
{
	for (int i = 0; i < call_stack_ptr; i++) {
		dbg_print_frame(i);
	}
}

struct ain_variable *dbg_get_member(const char *name, union vm_value *val_out)
{
	struct page *page = get_struct_page(dbg_current_frame);
	if (!page)
		return NULL;
	assert(page->type == STRUCT_PAGE);
	assert(page->index >= 0 && page->index < ain->nr_structures);
	struct ain_struct *s = &ain->structures[page->index];
	assert(page->nr_vars == s->nr_members);
	for (int i = 0; i < s->nr_members; i++) {
		if (!strcmp(s->members[i].name, name)) {
			*val_out = page->values[i];
			return &s->members[i];
		}
	}
	return NULL;
}

struct ain_variable *dbg_get_local(const char *name, union vm_value *val_out)
{
	struct page *page = get_local_page(dbg_current_frame);
	if (!page)
		return NULL;
	assert(page->type == LOCAL_PAGE);
	assert(page->index >= 0 && page->index < ain->nr_functions);
	struct ain_function *f = &ain->functions[page->index];
	for (int i = 0; i < f->nr_vars; i++) {
		if (!strcmp(f->vars[i].name, name)) {
			*val_out = page->values[i];
			return &f->vars[i];
		}
	}
	return NULL;
}

struct ain_variable *dbg_get_global(const char *name, union vm_value *val_out)
{
	for (int i = 0; i < ain->nr_globals; i++) {
		if (!strcmp(ain->globals[i].name, name)) {
			*val_out = global_get(i);
			return &ain->globals[i];
		}
	}
	return NULL;
}

struct ain_variable *dbg_get_variable(const char *name, union vm_value *val_out)
{
	struct ain_variable *var;
	if (!strncmp(name, "this.", 5) && (var = dbg_get_member(name+5, val_out)))
		return var;
	if ((var = dbg_get_local(name, val_out)))
		return var;
	if ((var = dbg_get_global(name, val_out)))
		return var;
	return NULL;
}

struct string *dbg_value_to_string(struct ain_type *type, union vm_value value, int recursive)
{
	switch (type->data) {
	case AIN_INT:
	case AIN_LONG_INT:
		return integer_to_string(value.i);
	case AIN_FLOAT:
		return float_to_string(value.f, 6);
	case AIN_BOOL:
		return cstr_to_string(value.i ? "true" : "false");
	case AIN_STRING: {
		struct string *out = cstr_to_string("\"");
		string_append(&out, heap_get_string(value.i));
		string_push_back(&out, '"');
		return out;
	}
	case AIN_STRUCT:
	case AIN_REF_STRUCT: {
		if (value.i < 0)
			return cstr_to_string("NULL");
		struct page *page = heap_get_page(value.i);
		if (page->nr_vars == 0) {
			return cstr_to_string("{}");
		}
		if (!recursive) {
			return cstr_to_string("{ <...> }");
		}
		struct string *out = cstr_to_string("{ ");
		for (int i = 0; i < page->nr_vars; i++) {
			struct ain_variable *m = &ain->structures[type->struc].members[i];
			if (i) {
				string_append_cstr(&out, "; ", 2);
			}
			string_append_cstr(&out, m->name, strlen(m->name));
			string_append_cstr(&out, " = ", 3);
			struct string *tmp = dbg_value_to_string(&m->type, page->values[i], recursive-1);
			string_append(&out, tmp);
			free_string(tmp);
		}
		string_append_cstr(&out, " }", 2);
		return out;
	}
	case AIN_ARRAY_TYPE:
	case AIN_REF_ARRAY_TYPE: {
		if (value.i < 0)
			return cstr_to_string("[]");
		struct page *page = heap_get_page(value.i);
		if (!page || page->nr_vars == 0) {
			return cstr_to_string("[]");
		}
		// get member type
		struct ain_type t;
		t.data = variable_type(page, 0, &t.struc, &t.rank);
		t.array_type = NULL;
		if (!recursive) {
			switch (t.data) {
			case AIN_STRUCT:
			case AIN_REF_STRUCT:
			case AIN_ARRAY_TYPE:
			case AIN_REF_ARRAY_TYPE:
				return cstr_to_string("[ <...> ]");
			default:
				break;
			}
		}
		struct string *out = cstr_to_string("[ ");
		for (int i = 0; i < page->nr_vars; i++) {
			if (i) {
				string_append_cstr(&out, "; ", 2);
			}
			struct string *tmp = dbg_value_to_string(&t, page->values[i], recursive-1);
			string_append(&out, tmp);
			free_string(tmp);
		}
		string_append_cstr(&out, " ]", 2);
		return out;
	}
	default:
		return cstr_to_string("<unsupported-data-type>");
	}
	return string_ref(&EMPTY_STRING);
}

// the maximum number of instructions preceeding the instruction pointer to be displayed
#define DASM_REWIND 16
// the number of instructions following the instruction pointer to be displayed
#define DASM_FWD 8

// Rewind to DASM_REWIND instructions before the current instruction pointer.
static bool dbg_init_dasm(struct dasm *dasm)
{
	unsigned addr_i = 0;
	size_t addr[DASM_REWIND] = {0};
	int fno = call_stack[call_stack_ptr-1].fno;

	dasm_init(dasm, ain);
	dasm_jump(dasm, ain->functions[fno].address - 6);

	for (; dasm_addr(dasm) < instr_ptr && !dasm_eof(dasm); dasm_next(dasm)) {
		addr[addr_i++ % DASM_REWIND] = dasm_addr(dasm);
	}

	if (dasm_addr(dasm) != instr_ptr) {
		goto error;
	}

	addr_i = (addr_i+1) % DASM_REWIND;
	if (addr[addr_i]) {
		dasm_jump(dasm, addr[addr_i]);
	} else if (addr[0]) {
		dasm_jump(dasm, addr[0]);
	}

	return true;
error:
	DBG_ERROR("Couldn't locate instruction pointer from current function");
	return false;
}

static void dbg_print_string(const char *str)
{
	// TODO: escape
	printf("\"%s\"", display_sjis0(str));
}

static void dbg_print_identifier(const char *str)
{
	if (strchr(str, ' '))
		dbg_print_string(str);
	else
		printf("%s", display_sjis0(str));
}

static void dbg_print_function_name(struct ain_function *fun)
{
	int i = ain_get_function_index(ain, fun);
	char *name = fun->name;
	char buf[512];
	if (i > 0) {
		snprintf(buf, 512, "%s#%d", fun->name, i);
		name = buf;
	}
	dbg_print_identifier(name);
}

static void dbg_print_local(struct dasm *dasm, int32_t n)
{
	int fno = dasm_function(dasm);
	if (fno < 0 || fno >= ain->nr_functions) {
		printf("%d", n);
		return;
	}

	struct ain_function *f = &ain->functions[fno];
	if (n < 0 || n >= f->nr_vars) {
		printf("<invalid local variable number: %d>", n);
		return;
	}

	int dup_no = 0;
	for (int i = 0; i < f->nr_vars; i++) {
		if (i == n)
			break;
		if (!strcmp(f->vars[i].name, f->vars[n].name))
			dup_no++;
	}

	char *name;
	char buf[512];
	if (dup_no) {
		snprintf(buf, 512, "%s#%d", f->vars[n].name, dup_no);
		name = buf;
	} else {
		name = f->vars[n].name;
	}

	dbg_print_identifier(name);
}

static void dbg_print_arg(struct dasm *dasm, int n)
{
	static int hll = 0;
	int32_t value = dasm_arg(dasm, n);
	switch (dasm_arg_type(dasm, n)) {
	case T_INT:
	case T_SWITCH:
		printf("%d", value);
		break;
	case T_FLOAT: {
		union { int32_t i; float f; } cast = { .i = value };
		printf("%f", cast.f);
		break;
	}
	case T_ADDR:
		printf("0x%08x", value);
		break;
	case T_FUNC:
		if (value < 0 || value >= ain->nr_functions)
			printf("<invalid function number: %d>", value);
		else
			dbg_print_function_name(&ain->functions[value]);
		break;
	case T_DLG:
		if (value < 0 || value >= ain->nr_delegates)
			printf("<invalid delegate number: %d>", value);
		else
			dbg_print_identifier(ain->delegates[value].name);
		break;
	case T_STRING:
		if (value < 0 || value >= ain->nr_strings)
			printf("<invalid string number: %d>", value);
		else
			dbg_print_string(ain->strings[value]->text);
		break;
	case T_MSG:
		if (value < 0 || value >= ain->nr_messages)
			printf("<invalid message number: %d>", value);
		else
			printf("%d ; %s", value, display_sjis0(ain->messages[value]->text));
		break;
	case T_LOCAL:
		dbg_print_local(dasm, value);
		break;
	case T_GLOBAL:
		if (value < 0 || value >= ain->nr_globals)
			printf("<invalid global number: %d>", value);
		else
			dbg_print_identifier(ain->globals[value].name);
		break;
	case T_STRUCT:
		if (value < 0 || value >= ain->nr_structures)
			printf("<invalid struct number: %d>", value);
		else
			dbg_print_identifier(ain->structures[value].name);
		break;
	case T_SYSCALL:
		if (value < 0 || value >= NR_SYSCALLS || !syscalls[value].name)
			printf("<invalid syscall number: %d>", value);
		else
			printf("%s", syscalls[value].name);
		break;
	case T_HLL:
		if (value < 0 || value >= ain->nr_libraries) {
			printf("<invalid library number: %d>", value);
		} else {
			dbg_print_identifier(ain->libraries[value].name);
			hll = value;
		}
		break;
	case T_HLLFUNC:
		if (hll < 0 || hll >= ain->nr_libraries)
			printf("%d", value);
		else if (value < 0 || value >= ain->libraries[hll].nr_functions)
			printf("<invalid library function number: %d>", value);
		else
			dbg_print_identifier(ain->libraries[hll].functions[value].name);
		break;
	case T_FILE:
		if (!ain->nr_filenames)
			printf("%d", value);
		else if (value < 0 || value >= ain->nr_filenames)
			printf("<invalid file number: %d>", value);
		else
			dbg_print_identifier(ain->filenames[value]);
		break;
	default:
		printf("<invalid arg type: %d>", dasm_arg_type(dasm, n));
		break;
	}
}

static void dbg_print_instruction(struct dasm *dasm)
{
	char c = dasm_addr(dasm) == instr_ptr ? '*' : ' ';
	printf("%c 0x%08x: %s", c, (unsigned)dasm_addr(dasm), dasm_instruction(dasm)->name);
	for (int i = 0; i < dasm_nr_args(dasm); i++) {
		putchar(' ');
		dbg_print_arg(dasm, i);
	}
}

static bool stack_print_finished(int stack_i)
{
	return stack_i >= stack_ptr;
}

static bool dasm_print_finished(struct dasm *dasm, int dasm_i)
{
	if (dasm_eof(dasm))
		return true;
	if (dasm_addr(dasm) < instr_ptr)
		return false;
	if (dasm_i < DASM_FWD)
		return false;
	return true;
}

#define STACK_MAX (DASM_REWIND + DASM_FWD)

void dbg_print_vm_state(void)
{
	puts("");
	puts("     Stack           Disassembly");
	puts("-----------------    -----------");

	struct dasm dasm;
	if (!dbg_init_dasm(&dasm))
		return;

	int stack_i = 0;
	if (stack_ptr > STACK_MAX)
		stack_i = stack_ptr - STACK_MAX;

	int dasm_i = 0;
	while (!stack_print_finished(stack_i) || !dasm_print_finished(&dasm, dasm_i)) {
		// print stack line
		if (stack_i < stack_ptr) {
			printf(" [%02d]: 0x%08x  ", stack_i, stack[stack_i].i);
			stack_i++;
		} else {
			printf("                   ");
		}

		// print disassembly line
		if (!dasm_print_finished(&dasm, dasm_i)) {
			if (dasm_addr(&dasm) >= instr_ptr)
				dasm_i++;
			dbg_print_instruction(&dasm);
			// XXX: stop printing at ENDFUNC
			if (dasm_instruction(&dasm)->opcode == ENDFUNC) {
				dasm.addr = dasm.ain->code_size;
			}
			dasm_next(&dasm);
		}

		putchar('\n');
	}
}
