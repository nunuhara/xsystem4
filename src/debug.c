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

#include "system4/string.h"
#include "system4/utfsjis.h"

#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

#include "debugger.h"
#include "little_endian.h"
#include "xsystem4.h"

bool dbg_enabled = true;
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

static struct breakpoint *breakpoints = NULL;
static unsigned nr_breakpoints = 0;

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
	breakpoints = xrealloc_array(breakpoints, nr_breakpoints, nr_breakpoints+1, sizeof(struct breakpoint));
	breakpoints[nr_breakpoints].restore_op = LittleEndian_getW(ain->code, f->address);
	breakpoints[nr_breakpoints].cb = cb;
	breakpoints[nr_breakpoints].data = data;
	breakpoints[nr_breakpoints].message = xmalloc(512);
	snprintf(breakpoints[nr_breakpoints].message, 511, "Hit breakpoint at function '%s' (0x%08x)", display_sjis0(_name), f->address);
	LittleEndian_putW(ain->code, f->address, BREAKPOINT + nr_breakpoints);
	nr_breakpoints++;

	NOTICE("Set breakpoint at function '%s' (0x%08x)", display_sjis0(_name), f->address);
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

	breakpoints = xrealloc_array(breakpoints, nr_breakpoints, nr_breakpoints+1, sizeof(struct breakpoint));
	breakpoints[nr_breakpoints].restore_op = op;
	breakpoints[nr_breakpoints].cb = cb;
	breakpoints[nr_breakpoints].data = data;
	breakpoints[nr_breakpoints].message = xmalloc(512);
	snprintf(breakpoints[nr_breakpoints].message, 511, "Hit breakpoint at 0x%08x", address);
	LittleEndian_putW(ain->code, address, BREAKPOINT + nr_breakpoints);
	nr_breakpoints++;

	NOTICE("Set breakpoint at 0x%08x", address);
	return true;
}

static void _dbg_handle_breakpoint(void *data)
{
	unsigned bp_no = *((unsigned*)data);
	if (breakpoints[bp_no].cb) {
		breakpoints[bp_no].cb(&breakpoints[bp_no]);
	} else {
		NOTICE("%s", breakpoints[bp_no].message);
		dbg_cmd_repl();
	}
}

enum opcode dbg_handle_breakpoint(unsigned bp_no)
{
	assert(bp_no < nr_breakpoints);
	dbg_start(_dbg_handle_breakpoint, &bp_no);
	return breakpoints[bp_no].restore_op;
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
	NOTICE("%c #%d 0x%08x in %s", no == dbg_current_frame ? '*' : ' ',
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
	struct page *page = local_page();
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
		return integer_to_string(value.i);
	case AIN_FLOAT:
		return float_to_string(value.f, 6);
		break;
	case AIN_STRING: {
		struct string *out = cstr_to_string("\"");
		string_append(&out, heap_get_string(value.i));
		string_push_back(&out, '"');
		return out;
	}
	case AIN_STRUCT:
	case AIN_REF_STRUCT: {
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
