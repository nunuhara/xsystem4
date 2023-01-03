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

#ifndef SYSTEM4_DEBUGGER_H
#define SYSTEM4_DEBUGGER_H
#ifdef DEBUGGER_ENABLED

#include <stdbool.h>
#include <stdint.h>
#include "system4/instructions.h"
#include "vm.h"
#include "xsystem4.h"

#define DBG_ERROR(fmt, ...) sys_warning("ERROR: " fmt "\n", ##__VA_ARGS__)

struct ain_variable;

struct breakpoint {
	enum opcode restore_op;
	void *data;
	void (*cb)(struct breakpoint*);
	char *message;
	int count;
};

struct dbg_cmd {
	const char *fullname;
	const char *shortname;
	const char *description;
	unsigned min_args;
	unsigned max_args;
	void (*run)(unsigned nr_args, char **args);
};

extern bool dbg_enabled;
extern bool dbg_start_in_debugger;
extern unsigned dbg_current_frame;

void dbg_init(void);
void dbg_fini(void);
void dbg_repl(void);

void dbg_continue(void);
void dbg_quit(void);
void dbg_start(void(*fun)(void*), void *data);
void dbg_cmd_init(void);
void dbg_cmd_repl(void);
void dbg_cmd_add_module(const char *name, unsigned nr_commands, struct dbg_cmd *commands);
void dbg_handle_breakpoint(void);
bool dbg_set_function_breakpoint(const char *_name, void(*cb)(struct breakpoint*), void *data);
bool dbg_set_address_breakpoint(uint32_t address, void(*cb)(struct breakpoint*), void *data);
void dbg_print_frame(unsigned no);
void dbg_print_stack_trace(void);
void dbg_print_dasm(void);
void dbg_print_stack(void);
void dbg_print_vm_state(void);
struct ain_variable *dbg_get_member(const char *name, union vm_value *val_out);
struct ain_variable *dbg_get_local(const char *name, union vm_value *val_out);
struct ain_variable *dbg_get_global(const char *name, union vm_value *val_out);
struct ain_variable *dbg_get_variable(const char *name, union vm_value *val_out);
struct string *dbg_value_to_string(struct ain_type *type, union vm_value value, int recursive);

#ifdef HAVE_SCHEME
void dbg_scm_init(void);
void dbg_scm_fini(void);
void dbg_scm_repl(void);
#endif /* HAVE_SCHEME */
#endif /* DEBUGGER_ENABLED */
#endif /* SYSTEM4_DEBUGGER_H */
