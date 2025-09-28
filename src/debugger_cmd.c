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
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

#include "scene.h"
#include "debugger.h"
#include "input.h"
#include "xsystem4.h"

struct dbg_cmd_node;
static enum {
	NOT_STEPPING = 0,
	STEPPING_INTO,
	STEPPING_OVER,
	STEPPING_FINISH
} stepping = NOT_STEPPING;
static int stepping_file = 0;
static int stepping_line = 0;

struct dbg_cmd_list {
	unsigned nr_commands;
	struct dbg_cmd_node *commands;
};

struct dbg_cmd_node {
	enum { CMD_NODE_CMD, CMD_NODE_MOD } type;
	union {
		struct dbg_cmd cmd;
		struct {
			const char *name;
			struct dbg_cmd_list list;
		} mod;
	};
};

static void dbg_cmd_help(unsigned nr_args, char **args);

static void dbg_cmd_backtrace(unsigned nr_args, char **args)
{
	dbg_print_stack_trace();
}

static void dbg_cmd_breakpoint(unsigned nr_args, char **args)
{
	if (nr_args == 1) {
		int addr = strtol(args[0], NULL, 0);
		if (addr > 0) {
			dbg_set_address_breakpoint(addr, NULL, NULL);
		} else {
			dbg_set_function_breakpoint(args[0], NULL, NULL);
		}
	} else if (nr_args == 2 && dbg_info) {
		int file = dbg_info_find_file(dbg_info, args[0]);
		if (file < 0) {
			DBG_ERROR("No such file: %s", args[0]);
			return;
		}
		int line = atoi(args[1]);
		int addr = dbg_info_line2addr(dbg_info, file, line);
		if (addr < 0) {
			DBG_ERROR("No such line: %s:%d", args[0], line);
			return;
		}
		dbg_set_address_breakpoint(addr, NULL, NULL);
	} else {
		DBG_ERROR("Invalid breakpoint command");
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
		if (f->vars[i].type.data == AIN_VOID)
			continue;
		struct string *value = dbg_variable_to_string(&f->vars[i].type, page, i, 1);
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
		if (f->vars[i].type.data == AIN_VOID)
			continue;
		struct string *value = dbg_variable_to_string(&f->vars[i].type, locals, i, 1);
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
		if (s->members[i].type.data == AIN_VOID)
			continue;
		struct string *value = dbg_variable_to_string(&s->members[i].type, page, i, 1);
		printf("[%d] %s: %s\n", i, display_sjis0(s->members[i].name), display_sjis1(value->text));
		free_string(value);
	}
}

static void dbg_cmd_print(unsigned nr_args, char **args)
{
	int recursive = nr_args == 2 ? atoi(args[1]) : 2;

	struct ain_type type;
	union vm_value value = dbg_eval_string(args[0], &type);
	if (type.data == AIN_VOID)
		return;

	struct string *str = dbg_value_to_string(&type, value, recursive);
	printf("%s\n", display_sjis0(str->text));
	free_string(str);
}

static void dbg_cmd_quit(unsigned nr_args, char **args)
{
	dbg_quit();
}

static void dbg_cmd_scene(unsigned nr_args, char **args)
{
	scene_print();
}

static void dbg_cmd_next(unsigned nr_args, char **args)
{
	stepping_file = stepping_line = 0;
	if (dbg_info)
		dbg_info_addr2line(dbg_info, instr_ptr, &stepping_file, &stepping_line);

	if (!dbg_set_step_over_breakpoint()) {
		DBG_ERROR("Can't step over this instruction");
		return;
	}
	stepping = STEPPING_OVER;
	dbg_continue();
}

static void dbg_cmd_step(unsigned nr_args, char **args)
{
	stepping_file = stepping_line = 0;
	if (dbg_info)
		dbg_info_addr2line(dbg_info, instr_ptr, &stepping_file, &stepping_line);

	if (!dbg_set_step_into_breakpoint()) {
		DBG_ERROR("Can't step into this instruction");
		return;
	}
	stepping = STEPPING_INTO;
	dbg_continue();
}

static void dbg_cmd_finish(unsigned nr_args, char **args)
{
	if (!dbg_set_finish_breakpoint()) {
		DBG_ERROR("Can't set finish breakpoint from this location");
		return;
	}
	stepping = STEPPING_FINISH;
	dbg_continue();
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

static struct dbg_cmd dbg_default_commands[] = {
	{ "backtrace", "bt", NULL, "Display stack trace", 0, 0, dbg_cmd_backtrace },
	{ "breakpoint", "bp", "<function> | <address> | <file> <line>", "Set breakpoint", 1, 2, dbg_cmd_breakpoint },
	{ "continue", "c", NULL, "Resume execution", 0, 0, dbg_cmd_continue },
	{ "finish", "fin", NULL, "Execute until the current function returns", 0, 0, dbg_cmd_finish },
	{ "frame", "f", "<frame-number>", "Set the current frame", 1, 1, dbg_cmd_frame },
	{ "help", "h", "[command-name]", "Get help about a command", 0, 2, dbg_cmd_help },
	{ "locals", "l", "[frame-number]", "Print local variables", 0, 1, dbg_cmd_locals },
	{ "log", NULL, "<function-name>", "Log function calls", 1, 1, dbg_cmd_log },
	{ "members", "m", "[frame-number]", "Print struct members", 0, 1, dbg_cmd_members },
	{ "next", "n", NULL, "Step to the next instruction within the current function", 0, 0, dbg_cmd_next },
	{ "print", "p", "<variable-name> [recursion-depth]", "Print a variable", 1, 2, dbg_cmd_print },
	{ "quit", "q", NULL, "Quit xsystem4", 0, 0, dbg_cmd_quit },
	{ "scene", NULL, NULL, "Display scene graph", 0, 0, dbg_cmd_scene },
	{ "step", "s", NULL, "Step to the next instruction", 0, 0, dbg_cmd_step },
#ifdef HAVE_SCHEME
	{ "scheme", "scm", NULL, "Drop into the Scheme REPL", 0, 0, dbg_cmd_scheme },
#endif
	{ "vm-state", "vm", NULL, "Display current VM state", 0, 0, dbg_cmd_vm_state },
};

static struct dbg_cmd_list dbg_commands = {0};

/*
 * Get a command by name.
 */
static struct dbg_cmd_node *dbg_get_node(struct dbg_cmd_list *list, const char *name)
{
	for (unsigned i = 0; i < list->nr_commands; i++) {
		switch (list->commands[i].type) {
		case CMD_NODE_CMD: {
			struct dbg_cmd *cmd = &list->commands[i].cmd;
			if (!strcmp(name, cmd->fullname) || (cmd->shortname && !strcmp(name, cmd->shortname)))
				return &list->commands[i];
			break;
		}
		case CMD_NODE_MOD:
			if (!strcmp(name, list->commands[i].mod.name))
				return &list->commands[i];
			break;
		}
	}
	return NULL;
}

static void dbg_help_cmd(struct dbg_cmd *cmd)
{
	printf("%s", cmd->fullname);
	if (cmd->shortname)
		printf(", %s", cmd->shortname);

	if (cmd->arg_description)
		printf(" %s", cmd->arg_description);

	printf("\n\t%s\n", cmd->description);
}

static int cmd_name_max(struct dbg_cmd_list *list)
{
	int m = 0;
	for (unsigned i = 0; i < list->nr_commands; i++) {
		if (list->commands[i].type == CMD_NODE_MOD) {
			m = max(m, strlen(list->commands[i].mod.name));
		} else if (list->commands[i].type == CMD_NODE_CMD) {
			m = max(m, strlen(list->commands[i].cmd.fullname));
		}
	}
	return m;
}

static void dbg_help_cmd_short(struct dbg_cmd *cmd, int name_len)
{
	printf("%*s", name_len, cmd->fullname);
	if (cmd->shortname)
		printf(" (%s)%*s", cmd->shortname, 3 - (int)strlen(cmd->shortname), "");
	else
		printf("      ");
	printf(" -- %s\n", cmd->description);
}

static void dbg_help_list(struct dbg_cmd_list *list)
{
	int name_len = cmd_name_max(list);

	puts("");
	puts("Available Commands");
	puts("==================");
	for (unsigned i = 0; i < list->nr_commands; i++) {
		if (list->commands[i].type == CMD_NODE_MOD) {
			printf("%s - command module; run to see additional commands\n",
					list->commands[i].mod.name);
		} else if (list->commands[i].type == CMD_NODE_CMD) {
			dbg_help_cmd_short(&list->commands[i].cmd, name_len);
		}
	}
	puts("");
	puts("Type 'help <command>' to get help about a specific command.");
}

static void dbg_cmd_help(unsigned nr_args, char **args)
{
	if (!nr_args) {
		dbg_help_list(&dbg_commands);
		return;
	}

	struct dbg_cmd_node *node = dbg_get_node(&dbg_commands, args[0]);
	if (!node) {
		DBG_ERROR("'%s' is not a command or module", args[0]);
		return;
	}
	if (nr_args > 1) {
		if (node->type != CMD_NODE_MOD) {
			DBG_ERROR("'%s' is not a command module", args[0]);
			return;
		}
		node = dbg_get_node(&node->mod.list, args[1]);
		if (!node) {
			DBG_ERROR("'%s %s' is not a command or module", args[0], args[1]);
			return;
		}
	}

	switch (node->type) {
	case CMD_NODE_CMD:
		dbg_help_cmd(&node->cmd);
		break;
	case CMD_NODE_MOD:
		dbg_help_list(&node->mod.list);
		break;
	}
}

void dbg_cmd_add_module(const char *name, unsigned nr_commands, struct dbg_cmd *commands)
{
	dbg_commands.commands = xrealloc_array(dbg_commands.commands, dbg_commands.nr_commands,
			dbg_commands.nr_commands+1, sizeof(struct dbg_cmd_node));
	struct dbg_cmd_node *node = &dbg_commands.commands[dbg_commands.nr_commands++];
	node->type = CMD_NODE_MOD;
	node->mod.name = name;
	node->mod.list.nr_commands = nr_commands;
	node->mod.list.commands = xcalloc(nr_commands, sizeof(struct dbg_cmd_node));
	for (unsigned i = 0; i < nr_commands; i++) {
		node->mod.list.commands[i].type = CMD_NODE_CMD;
		node->mod.list.commands[i].cmd = commands[i];
	}
}

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>

static int cmd_rl_event_hook(void)
{
	handle_window_events();
	return 1;
}

/*
 * Get a string from the user. The result can be modified but shouldn't be free'd.
 */
static char *cmd_gets(void)
{
	rl_event_hook = cmd_rl_event_hook;
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

	handle_window_events();
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

static void execute_command(struct dbg_cmd *cmd, unsigned nr_args, char **args)
{
	if (nr_args < cmd->min_args || nr_args > cmd->max_args) {
		printf("Wrong number of arguments to '%s' command\n", cmd->fullname);
		return;
	}
	cmd->run(nr_args, args);
}

static void execute_words(struct dbg_cmd_list *list, unsigned nr_words, char **words)
{
	struct dbg_cmd_node *node = dbg_get_node(list, words[0]);
	if (!node) {
		printf("Invalid command: %s (type 'help' for a list of commands)\n", words[0]);
		return;
	}

	switch (node->type) {
	case CMD_NODE_CMD:
		execute_command(&node->cmd, nr_words-1, words+1);
		break;
	case CMD_NODE_MOD:
		if (nr_words == 1) {
			printf("TODO: module help");
			return;
		}
		execute_words(&node->mod.list, nr_words-1, words+1);
		break;
	}
}

static void execute_line(char *line)
{
	unsigned nr_words;
	char **words = cmd_parse(line, &nr_words);
	if (!nr_words)
		return;

	execute_words(&dbg_commands, nr_words, words);
}

void dbg_cmd_repl(void)
{
	if (stepping) {
		if (stepping_line) {
			int file, line;
			dbg_info_addr2line(dbg_info, instr_ptr, &file, &line);
			if (file == stepping_file && line == stepping_line) {
				if (stepping == STEPPING_INTO)
					dbg_set_step_into_breakpoint();
				else if (stepping == STEPPING_OVER)
					dbg_set_step_over_breakpoint();
				dbg_continue();
			}
		}
		stepping = NOT_STEPPING;
		stepping_file = stepping_line = 0;
		if (dbg_info)
			dbg_print_current_line();
		else
			dbg_print_vm_state();
	} else {
		puts("Entering the debugger REPL. Type 'help' for a list of commands.");
	}
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
	dbg_commands.nr_commands = sizeof(dbg_default_commands)/sizeof(*dbg_default_commands);
	dbg_commands.commands = xcalloc(dbg_commands.nr_commands, sizeof(struct dbg_cmd_node));
	for (unsigned i = 0; i < dbg_commands.nr_commands; i++) {
		dbg_commands.commands[i].type = CMD_NODE_CMD;
		dbg_commands.commands[i].cmd = dbg_default_commands[i];
	}

	char *path = xmalloc(PATH_MAX);
	snprintf(path, PATH_MAX, "%s/.xsys4-debugrc", config.home_dir);
	read_config(path);
	free(path);

	path = gamedir_path(".xsys4-debugrc");
	read_config(path);
	free(path);
}
