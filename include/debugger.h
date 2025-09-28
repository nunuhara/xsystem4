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

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include "system4/instructions.h"
#include "vm.h"
#include "xsystem4.h"

#define DBG_ERROR(fmt, ...) sys_warning("ERROR: " fmt "\n", ##__VA_ARGS__)

struct ain_variable;

enum dbg_stop_type {
	DBG_STOP_PAUSE,
	DBG_STOP_ERROR,
	DBG_STOP_BREAKPOINT,
	DBG_STOP_STEP,
};

struct dbg_stop {
	enum dbg_stop_type type;
	const char *message;
};

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
	const char *arg_description;
	const char *description;
	unsigned min_args;
	unsigned max_args;
	void (*run)(unsigned nr_args, char **args);
};

struct dbg_info;

extern bool dbg_dap;
extern bool dbg_enabled;
extern bool dbg_start_in_debugger;
extern unsigned dbg_current_frame;
extern struct dbg_info *dbg_info;

void dbg_init(const char *debug_info_path);
void dbg_fini(void);
void dbg_repl(enum dbg_stop_type, const char *msg);

void dbg_log(const char *log, const char *fmt, va_list ap);

void dbg_continue(void);
void dbg_quit(void);
void dbg_start(void(*fun)(void*), void *data);
void dbg_cmd_init(void);
void dbg_cmd_repl(void);
void dbg_cmd_add_module(const char *name, unsigned nr_commands, struct dbg_cmd *commands);
void dbg_handle_breakpoint(void);
bool dbg_clear_breakpoint(uint32_t addr, void(*free_data)(void*));
void dbg_foreach_breakpoint(void (*fun)(int addr, struct breakpoint*, void *data), void *data);
bool dbg_set_function_breakpoint(const char *_name, void(*cb)(struct breakpoint*), void *data);
bool dbg_set_address_breakpoint(uint32_t address, void(*cb)(struct breakpoint*), void *data);
bool dbg_set_step_over_breakpoint(void);
bool dbg_set_step_into_breakpoint(void);
bool dbg_set_finish_breakpoint(void);
void dbg_print_frame(unsigned no);
void dbg_print_stack_trace(void);
void dbg_print_stack(void);
void dbg_print_vm_state(void);
void dbg_print_current_line(void);
union vm_value dbg_eval_string(const char *str, struct ain_type *type_out);
struct string *dbg_value_to_string(struct ain_type *type, union vm_value value, int recursive);
struct string *dbg_variable_to_string(struct ain_type *type, struct page *page, int slot, int recursive);

void dbg_dap_init(void);
void dbg_dap_quit(void);
void dbg_dap_repl(struct dbg_stop *stop);
void dbg_dap_handle_messages(void);
void dbg_dap_log(const char *log, const char *fmt, va_list ap);

struct dbg_info *dbg_info_load(const char *path);
const char *dbg_info_source_name(const struct dbg_info *info, int file);
char *dbg_info_source_path(const struct dbg_info *info, int file);
const char *dbg_info_source_line(const struct dbg_info *info, int file, int line);
int dbg_info_find_file(const struct dbg_info *info, const char *filename);
bool dbg_info_addr2line(const struct dbg_info *info, int addr, int *file, int *line);
int dbg_info_line2addr(const struct dbg_info *info, int file, int line);

#ifdef HAVE_SCHEME
void dbg_scm_init(void);
void dbg_scm_fini(void);
void dbg_scm_repl(void);
#endif /* HAVE_SCHEME */
#else  /* DEBUGGER ENABLED */
#define dbg_init(debug_info_path)
#define dbg_repl(type, msg)
#define dbg_dap 0
#define dbg_dap_handle_messages()
#define dbg_dap_log(log, fmt, ap)
#endif /* DEBUGGER_ENABLED */
#endif /* SYSTEM4_DEBUGGER_H */
