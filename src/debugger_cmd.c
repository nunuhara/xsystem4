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
#include <limits.h>
#include <assert.h>

#include "system4.h"
#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

#include "scene.h"
#include "debugger.h"
#include "little_endian.h"
#include "xsystem4.h"

struct dbg_cmd {
	const char *fullname;
	const char *shortname;
	const char *description;
	unsigned min_args;
	unsigned max_args;
	void (*run)(unsigned nr_args, char **args);
};

static void dbg_cmd_help(unsigned nr_args, char **args);

static void dbg_cmd_backtrace(unsigned nr_args, char **args)
{
	dbg_print_stack_trace();
}

static void dbg_cmd_breakpoint(unsigned nr_args, char **args)
{
	int addr = strtol(args[0], NULL, 0);
	if (addr > 0) {
		dbg_set_address_breakpoint(addr, NULL, NULL);
	} else {
		dbg_set_function_breakpoint(args[0], NULL, NULL);
	}
}

static void dbg_cmd_continue(unsigned nr_args, char **args)
{
	dbg_continue();
}

static void dbg_cmd_frame(unsigned nr_args, char **args)
{
	int frame_no = atoi(args[0]);
	if (frame_no < 0 || frame_no >= call_stack_ptr) {
		DBG_ERROR("Invalid frame number: %d", frame_no);
		return;
	}
	dbg_current_frame = frame_no;
	dbg_print_frame(frame_no);
}

static void dbg_cmd_locals(unsigned nr_args, char **args)
{
	int frame_no = nr_args > 0 ? atoi(args[0]) : dbg_current_frame;
	if (frame_no < 0 || frame_no >= call_stack_ptr) {
		DBG_ERROR("Invalid frame number: %d", frame_no);
		return;
	}

	struct page *page = heap_get_page(call_stack[call_stack_ptr - (frame_no+1)].page_slot);
	struct ain_function *f = &ain->functions[page->index];
	for (int i = 0; i < f->nr_vars; i++) {
		struct string *value = dbg_value_to_string(&f->vars[i].type, page->values[i], 1);
		printf("[%d] %s: %s\n", i, display_sjis0(f->vars[i].name), display_sjis1(value->text));
		free_string(value);
	}
}

static void log_handler(struct breakpoint *bp)
{
	struct ain_function *f = bp->data;
	struct page *locals = local_page();
	printf("%s(", display_sjis0(f->name));
	for (int i = 0; i < f->nr_args; i++) {
		struct string *value = dbg_value_to_string(&f->vars[i].type, locals->values[i], 1);
		printf(i > 0 ? ", %s" : "%s", display_sjis0(value->text));
		free_string(value);
	}
	printf(")\n");
}

static void dbg_cmd_log(unsigned nr_args, char **args)
{
	int fno = ain_get_function(ain, args[0]);
	if (fno < 0) {
		DBG_ERROR("No function with name '%s'", args[0]);
		return;
	}
	dbg_set_function_breakpoint(args[0], log_handler, &ain->functions[fno]);
}

static void dbg_cmd_members(unsigned nr_args, char **args)
{
	int frame_no = nr_args > 0 ? atoi(args[0]) : dbg_current_frame;
	if (frame_no < 0 || frame_no >= call_stack_ptr) {
		DBG_ERROR("Invalid frame number: %d", frame_no);
		return;
	}

	struct page *page = get_struct_page(frame_no);
	if (!page) {
		DBG_ERROR("Frame #%d is not a method", frame_no);
		return;
	}

	assert(page->type == STRUCT_PAGE);
	assert(page->index >= 0 && page->index < ain->nr_structures);
	struct ain_struct *s = &ain->structures[page->index];
	assert(page->nr_vars == s->nr_members);

	for (int i = 0; i < s->nr_members; i++) {
		struct string *value = dbg_value_to_string(&s->members[i].type, page->values[i], 1);
		printf("[%d] %s: %s\n", i, display_sjis0(s->members[i].name), display_sjis1(value->text));
		free_string(value);
	}
}

static void dbg_cmd_print(unsigned nr_args, char **args)
{
	union vm_value value;
	struct ain_variable *var = dbg_get_variable(args[0], &value);
	if (var) {
		struct string *v = dbg_value_to_string(&var->type, value, 1);
		printf("%s\n", display_sjis0(v->text));
		free_string(v);
		return;
	}
	// TODO: print functions, etc.
	DBG_ERROR("Undefined variable: %s", args[0]);
}

static void dbg_cmd_quit(unsigned nr_args, char **args)
{
	dbg_quit();
}

static void dbg_cmd_scene(unsigned nr_args, char **args)
{
	scene_print();
}

#ifdef HAVE_SCHEME
static void dbg_cmd_scheme(unsigned nr_args, char **args)
{
	dbg_scm_repl();
}
#endif

static void dbg_cmd_vm_state(unsigned nr_args, char **args)
{
	dbg_print_vm_state();
}

static struct dbg_cmd dbg_commands[] = {
	{ "help",       "h",   "[command-name] - Display this message",  0, 1, dbg_cmd_help },
	{ "backtrace",  "bt",  "- Display stack trace",                  0, 0, dbg_cmd_backtrace },
	{ "breakpoint", "bp",  "<function-or-address> - Set breakpoint", 1, 1, dbg_cmd_breakpoint },
	{ "continue",   "c",   "- Resume execution",                     0, 0, dbg_cmd_continue },
	{ "frame",      "f",   "<frame-number> - Set the current frame", 1, 1, dbg_cmd_frame },
	{ "locals",     "l",   "[frame-number] - Print local variables", 0, 1, dbg_cmd_locals },
	{ "log",        NULL,  "<function-name> - Log function calls",   1, 1, dbg_cmd_log },
	{ "members",    "m",   "[frame-number] - Print struct members",  0, 1, dbg_cmd_members },
	{ "print",      "p",   "<variable-name> - Print a variable",     1, 1, dbg_cmd_print },
	{ "quit",       "q",   "- Quit xsystem4",                        0, 0, dbg_cmd_quit },
	{ "scene",      NULL,  "- Display scene graph",                  0, 0, dbg_cmd_scene },
#ifdef HAVE_SCHEME
	{ "scheme",     "scm", "- Drop into Scheme REPL",                0, 0, dbg_cmd_scheme },
#endif
	{ "vm-state",   "vm",  "- Display current VM state",             0, 0, dbg_cmd_vm_state },
};

static void dbg_cmd_help(unsigned nr_args, char **args)
{
	puts("Available Commands");
	puts("------------------");
	for (unsigned i = 0; i < sizeof(dbg_commands)/sizeof(*dbg_commands); i++) {
		struct dbg_cmd *cmd = &dbg_commands[i];
		if (!nr_args || !strcmp(args[0], cmd->fullname) || !strcmp(args[0], cmd->shortname)) {
			if (cmd->shortname)
				printf("%s (%s) %s\n", cmd->fullname, cmd->shortname, cmd->description);
			else
				printf("%s %s\n", cmd->fullname, cmd->description);
		}
	}
}

/*
 * Get a command by name.
 */
static struct dbg_cmd *dbg_get_command(const char *name)
{
	for (unsigned i = 0; i < sizeof(dbg_commands)/sizeof(*dbg_commands); i++) {
		struct dbg_cmd *cmd = &dbg_commands[i];
		if (!strcmp(name, cmd->fullname) || (cmd->shortname && !strcmp(name, cmd->shortname)))
			return cmd;
	}
	return NULL;
}

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
/*
 * Get a string from the user. The result can be modified but shouldn't be free'd.
 */
static char *cmd_gets(void)
{
	static char *line_read = NULL;
	free(line_read);
	line_read = readline("dbg(cmd)> ");
	if (line_read && *line_read)
		add_history(line_read);
	return line_read;
}
#else
/*
 * Get a string from the user. The result can be modified but shouldn't be free'd.
 */
static char *cmd_gets(void)
{
	static char line[1024];

	printf("dbg(cmd)> ");
	fflush(stdout);
	fgets(line, 1024, stdin);
	return line;
}
#endif

/*
 * Parse a line into an array of words. Modified the input string.
 */
static char **cmd_parse(char *line, unsigned *nr_words)
{
	static char *words[32];

	*nr_words = 0;
	while (*line) {
		// skip whitespace
		while (*line && isspace(*line)) line++;
		if (!*line) break;
		// save word ptr
		words[(*nr_words)++] = line;
		// skip word
		while (*line && !isspace(*line)) line++;
		if (!*line) break;
		*line = '\0';
		line++;
	}
	return words;
}

static void execute_line(char *line)
{
	unsigned nr_words;
	char **words = cmd_parse(line, &nr_words);
	if (!nr_words)
		return;

	struct dbg_cmd *cmd = dbg_get_command(words[0]);
	if (!cmd) {
		printf("Invalid command: %s (type 'help' for a list of commands)\n", words[0]);
		return;
	}

	if ((nr_words-1) < cmd->min_args || (nr_words-1) > cmd->max_args) {
		printf("Wrong number of arguments to '%s' command\n", cmd->fullname);
		return;
	}

	cmd->run(nr_words-1, words+1);
}

void dbg_cmd_repl(void)
{
	puts("Entering the debugger REPL. Type 'help' for a list of commands.");
	while (1) {
		char *line = cmd_gets();
		if (line)
			execute_line(line);
	}
}

static void execute_config(void *_f)
{
	FILE *f = _f;
	char line[1024];
	while (fgets(line, 1024, f)) {
		execute_line(line);
	}
}

static void read_config(const char *path)
{
	FILE *f = file_open_utf8(path, "rb");
	if (!f)
		return;
	dbg_start(execute_config, f);
	fclose(f);
}

void dbg_cmd_init(void)
{
	char *path = xmalloc(PATH_MAX);
	snprintf(path, PATH_MAX, "%s/.xsys4-debugrc", config.home_dir);
	read_config(path);
	free(path);

	path = gamedir_path(".xsys4-debugrc");
	read_config(path);
	free(path);
}
