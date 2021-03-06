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

#include <stdio.h>
#include <assert.h>
#include <chibi/eval.h>

#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

#include "debugger.h"
#include "little_endian.h"

#define SCM_ERROR(fmt, ...) sys_warning("ERROR: " fmt "\n", ##__VA_ARGS__)

bool dbg_enabled = true;

struct variable {
	enum ain_data_type data_type;
	int struct_type;
	char *name;
	int varno;
	union vm_value *value;
};

static void free_variable(struct variable *v)
{
	free(v->name);
	free(v);
}

static char *to_utf(const char *sjis)
{
	return sjis2utf(sjis, strlen(sjis));
}

static const char *vm_value_string(union vm_value *v)
{
	return to_utf(heap[v->i].s->text);
}

static struct page *get_page(int pageno)
{
	if (!page_index_valid(pageno))
		return NULL;
	return heap_get_page(pageno);
}

static struct variable *get_global_by_name(const char *name)
{
	for (int i = 0; i < ain->nr_globals; i++) {
		if (!strcmp(ain->globals[i].name, name)) {
			struct variable *v = malloc(sizeof(struct variable));
			v->data_type = ain->globals[i].type.data;
			v->struct_type = ain->globals[i].type.struc;
			v->name = to_utf(ain->globals[i].name);
			v->varno = i;
			v->value = &heap[0].page->values[i];
			return v;
		}
	}
	return NULL;
}

static struct variable *get_local_by_name(const char *name)
{
	struct page *page = local_page();
	struct ain_function *f = &ain->functions[page->index];

	for (int i = 0; i < f->nr_vars; i++) {
		if (!strcmp(f->vars[i].name, name)) {
			struct variable *v = malloc(sizeof(struct variable));
			v->data_type = f->vars[i].type.data;
			v->struct_type = f->vars[i].type.struc;
			v->name = to_utf(ain->globals[i].name);
			v->varno = i;
			v->value = &page->values[i];
			return v;
		}
	}
	return NULL;
}

static const char *variable_name(struct page *page, int varno)
{
	switch (page->type) {
	case GLOBAL_PAGE:
		return ain->globals[varno].name;
	case LOCAL_PAGE:
		return ain->functions[page->index].vars[varno].name;
	case STRUCT_PAGE:
		return ain->structures[page->index].members[varno].name;
	case ARRAY_PAGE:
		return "array member";
	}
	return "unknown";
}

static struct variable *page_ref(struct page *page, int i)
{
	if (i < 0 || i >= page->nr_vars)
		return NULL;

	struct variable *v = malloc(sizeof(struct variable));
	v->data_type = variable_type(page, i, &v->struct_type, NULL);
	v->name = to_utf(variable_name(page, i));
	v->varno = i;
	v->value = &page->values[i];
	return v;
}

struct breakpoint {
	enum opcode restore_op;
	sexp handler;
	char *message;
	int count;
};

static struct breakpoint *breakpoints = NULL;
static unsigned nr_breakpoints = 0;

static sexp dbg_ctx;

static void set_function_breakpoint(const char *_name, sexp handler)
{
	if (!sexp_procedurep(handler) && handler != SEXP_FALSE) {
		SCM_ERROR("Breakpoint handler must be a procedure or #f");
		return;
	}

	char *name = utf2sjis(_name, 0);
	int fno = ain_get_function(ain, name);
	free(name);

	if (fno < 0) {
		SCM_ERROR("No function with name '%s'", _name);
		return;
	}

	struct ain_function *f = &ain->functions[fno];
	breakpoints = xrealloc_array(breakpoints, nr_breakpoints, nr_breakpoints+1, sizeof(struct breakpoint));
	breakpoints[nr_breakpoints].restore_op = LittleEndian_getW(ain->code, f->address);
	breakpoints[nr_breakpoints].handler = handler;
	if (handler != SEXP_FALSE)
		sexp_preserve_object(dbg_ctx, handler);
	breakpoints[nr_breakpoints].message = xmalloc(512);
	snprintf(breakpoints[nr_breakpoints].message, 511, "Hit breakpoint at function '%s' (0x%08x)", _name, f->address);
	LittleEndian_putW(ain->code, f->address, BREAKPOINT + nr_breakpoints);
	nr_breakpoints++;
}

static void set_address_breakpoint(uint32_t address, sexp handler)
{
	if (!sexp_procedurep(handler) && handler != SEXP_FALSE) {
		SCM_ERROR("Breakpoint handler must be a procedure or #f");
		return;
	}

	if (address & 1 || address >= ain->code_size - 2) {
		SCM_ERROR("Invalid instruction address: 0x%08x", address);
		return;
	}

	// XXX: this is not very robust; some illegal addresses will slip through
	//      if the data looks like a valid opcode
	enum opcode op = LittleEndian_getW(ain->code, address);
	if (op < 0 || op >= NR_OPCODES) {
		SCM_ERROR("Invalid instruction address: 0x%08x", address);
		return;
	}

	breakpoints = xrealloc_array(breakpoints, nr_breakpoints, nr_breakpoints+1, sizeof(struct breakpoint));
	breakpoints[nr_breakpoints].restore_op = op;
	breakpoints[nr_breakpoints].handler = handler;
	if (handler != SEXP_FALSE)
		sexp_preserve_object(dbg_ctx, handler);
	breakpoints[nr_breakpoints].message = xmalloc(512);
	snprintf(breakpoints[nr_breakpoints].message, 511, "Hit breakpoint at 0x%08x", address);
	LittleEndian_putW(ain->code, address, BREAKPOINT + nr_breakpoints);
	nr_breakpoints++;
}

static void print_stack_trace(void)
{
	for (int i = call_stack_ptr - 1, j = 0; i >= 0; i--, j++) {
		struct ain_function *f = &ain->functions[call_stack[i].fno];
		char *u = sjis2utf(f->name, strlen(f->name));
		uint32_t addr = (i == call_stack_ptr - 1) ? instr_ptr : call_stack[i+1].call_address;
		NOTICE("#%d 0x%08x in %s", j, addr, u);
		free(u);
	}
}

static struct page *frame_page(int i)
{
	if (i < 0 || i >= call_stack_ptr)
		return NULL;
	return get_page(call_stack[call_stack_ptr - (i+1)].page_slot);
}

// autogenerated with `chibi-ffi src/debug-ffi.stub`
#include "debug-ffi.c"

void dbg_init(void)
{
	if (!dbg_enabled)
		return;
	dbg_ctx = sexp_make_eval_context(NULL, NULL, NULL, 0, 0);
	sexp_load_standard_env(dbg_ctx, NULL, SEXP_SEVEN);
	sexp_load_standard_ports(dbg_ctx, NULL, stdin, stdout, stderr, 1);
	sexp_init_library(dbg_ctx, NULL, 0, sexp_context_env(dbg_ctx), sexp_version, SEXP_ABI_IDENTIFIER);
	sexp_eval_string(dbg_ctx, "(import (scheme base))", -1, NULL);
	sexp_eval_string(dbg_ctx, "(import (chibi repl))", -1, NULL);
	sexp_eval_string(dbg_ctx, "(define *prompt* (make-parameter \"dbg> \"))", -1, NULL);
	sexp_eval_string(dbg_ctx, "(define breakpoint-count (make-parameter 0))", -1, NULL);
	sexp_eval_string(dbg_ctx, "(define (repl-make-prompt m) (*prompt*))", -1, NULL);
	sexp_eval_string(dbg_ctx, "(load \"./debugger.scm\")", -1, NULL);
}

void dbg_fini(void)
{
	if (!dbg_enabled)
		return;
	sexp_destroy_context(dbg_ctx);
}

void dbg_repl(void)
{
	if (!dbg_enabled)
		return;
	sexp_eval_string(dbg_ctx, "(repl 'escape: #\\\\ 'make-prompt: repl-make-prompt)", -1, NULL);
}

static void set_parameter(const char *param, sexp value)
{
	sexp_eval(dbg_ctx, sexp_list2(dbg_ctx, sexp_intern(dbg_ctx, param, -1), value), NULL);
}

enum opcode dbg_handle_breakpoint(unsigned bp_no)
{
	assert(bp_no < nr_breakpoints);

	set_parameter("breakpoint-count", sexp_make_integer(dbg_ctx, breakpoints[bp_no].count++));
	// TODO: breakpoint-function parameter with function name, argument count, etc.

	if (breakpoints[bp_no].handler == SEXP_FALSE) {
		NOTICE("%s", breakpoints[bp_no].message);
		dbg_repl();
	} else {
		sexp_apply(dbg_ctx, breakpoints[bp_no].handler, SEXP_NULL);
	}

	return breakpoints[bp_no].restore_op;
}
