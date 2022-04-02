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
		DBG_ERROR("No function with name '%s'", _name);
		return false;
	}

	struct ain_function *f = &ain->functions[fno];
	breakpoints = xrealloc_array(breakpoints, nr_breakpoints, nr_breakpoints+1, sizeof(struct breakpoint));
	breakpoints[nr_breakpoints].restore_op = LittleEndian_getW(ain->code, f->address);
	breakpoints[nr_breakpoints].cb = cb;
	breakpoints[nr_breakpoints].data = data;
	breakpoints[nr_breakpoints].message = xmalloc(512);
	snprintf(breakpoints[nr_breakpoints].message, 511, "Hit breakpoint at function '%s' (0x%08x)", _name, f->address);
	LittleEndian_putW(ain->code, f->address, BREAKPOINT + nr_breakpoints);
	nr_breakpoints++;

	NOTICE("Set breakpoint at function '%s' (0x%08x)", _name, f->address);
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

void dbg_print_stack_trace(void)
{
	for (int i = call_stack_ptr - 1, j = 0; i >= 0; i--, j++) {
		struct ain_function *f = &ain->functions[call_stack[i].fno];
		char *u = sjis2utf(f->name, strlen(f->name));
		uint32_t addr = (i == call_stack_ptr - 1) ? instr_ptr : call_stack[i+1].call_address;
		NOTICE("#%d 0x%08x in %s", j, addr, u);
		free(u);
	}
}
