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

#include <stdio.h>
#include <string.h>
#include "aindump.h"
#include "khash.h"
#include "kvec.h"
#include "little_endian.h"
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#define DASM_ERROR(dasm, fmt, ...) ERROR("At 0x%x: " fmt, dasm->addr, ##__VA_ARGS__)

#define DASM_FUNC_STACK_SIZE 16

struct dasm_state {
	struct ain *ain;
	uint32_t flags;
	FILE *out;
	size_t addr;
	int func;
	int func_stack[DASM_FUNC_STACK_SIZE];
};

enum jump_target_type {
	JMP_LABEL,
	JMP_CASE,
	JMP_DEFAULT
};

struct jump_target {
	enum jump_target_type type;
	union {
		char *label;
		struct ain_switch_case *switch_case;
		struct ain_switch *switch_default;
	};
};

kv_decl(jump_list, struct jump_target*);

KHASH_MAP_INIT_INT(jump_table, jump_list*);
khash_t(jump_table) *jump_table;

static void jump_table_init(void)
{
	jump_table = kh_init(jump_table);
}

static void free_jump_targets(jump_list *list)
{
	if (!list)
		return;
	for (size_t i = 0; i < kv_size(*list); i++) {
		struct jump_target *t = kv_A(*list, i);
		if (t->type == JMP_LABEL)
			free(t->label);
		free(t);
	}
}

static void jump_table_fini(void)
{
	jump_list *list;
	kh_foreach_value(jump_table, list, free_jump_targets(list));
}

static jump_list *get_jump_targets(ain_addr_t addr)
{
	khiter_t k = kh_get(jump_table, jump_table, addr);
	if (k == kh_end(jump_table))
		return NULL;
	return kh_value(jump_table, k);
}

static char *get_label(ain_addr_t addr)
{
	jump_list *list = get_jump_targets(addr);
	for (size_t i = 0; i < kv_size(*list); i++) {
		struct jump_target *t = kv_A(*list, i);
		if (t->type == JMP_LABEL)
			return t->label;
	}
	return NULL;
}

static void add_jump_target(struct jump_target *target, ain_addr_t addr)
{
	int ret;
	khiter_t k = kh_put(jump_table, jump_table, addr, &ret);
	if (!ret) {
		// add to list
		jump_list *list = kh_value(jump_table, k);
		kv_push(struct jump_target*, *list, target);
	} else if (ret == 1) {
		// create list
		jump_list *list = xmalloc(sizeof(jump_list));
		kv_init(*list);
		kv_push(struct jump_target*, *list, target);
		kh_value(jump_table, k) = list;
	} else {
		ERROR("Failed to insert target into jump table (%d)", ret);
	}
}

static void add_label(char *name, ain_addr_t addr)
{
	// check for duplicate label
	jump_list *list = get_jump_targets(addr);
	if (list) {
		for (size_t i = 0; i < kv_size(*list); i++) {
			struct jump_target *t = kv_A(*list, i);
			if (t->type == JMP_LABEL) {
				free(name);
				return;
			}
		}
	}

	struct jump_target *t = xmalloc(sizeof(struct jump_target));
	t->type = JMP_LABEL;
	t->label = name;
	add_jump_target(t, addr);
}

static void add_switch_case(struct ain_switch_case *c)
{
	struct jump_target *t = xmalloc(sizeof(struct jump_target));
	t->type = JMP_CASE;
	t->switch_case = c;
	add_jump_target(t, c->address);
}

static void add_switch_default(struct ain_switch *s)
{
	if (s->default_address == -1)
		return;
	struct jump_target *t = xmalloc(sizeof(struct jump_target));
	t->type = JMP_DEFAULT;
	t->switch_default = s;
	add_jump_target(t, s->default_address);
}

union float_cast {
	int32_t i;
	float f;
};

static float arg_to_float(int32_t arg)
{
	union float_cast v;
	v.i = arg;
	return v.f;
}

static void print_sjis(struct dasm_state *dasm, const char *s)
{
	char *u = sjis2utf(s, strlen(s));
	fprintf(dasm->out, "%s", u);
	free(u);
}

static char *prepare_string(const char *str, const char *escape_chars, const char *replace_chars)
{
	int escapes = 0;
	char *u = sjis2utf(str, strlen(str));

	// count number of required escapes
	for (int i = 0; u[i]; i++) {
		if (u[i] & 0x80)
			continue;
		for (int j = 0; escape_chars[j]; j++) {
			if (u[i] == escape_chars[j]) {
				escapes++;
				break;
			}
		}
	}

	// add backslash escapes
	if (escapes > 0) {
		int dst = 0;
		int len = strlen(u);
		char *tmp = xmalloc(len + escapes + 1);
		for (int i = 0; u[i]; i++) {
			bool escaped = false;
			for (int j = 0; escape_chars[j]; j++) {
				if (u[i] == escape_chars[j]) {
					tmp[dst++] = '\\';
					tmp[dst++] = replace_chars[j];
					escaped = true;
					break;
				}
			}
			if (!escaped)
				tmp[dst++] = u[i];
		}
		tmp[len+escapes] = '\0';
		free(u);
		u = tmp;
	}

	return u;
}

static void print_string(struct dasm_state *dasm, const char *str)
{
	const char escape_chars[]  = { '\\', '\"', '\n', 0 };
	const char replace_chars[] = { '\\', '\"', 'n',  0  };
	char *u = prepare_string(str, escape_chars, replace_chars);
	fprintf(dasm->out, "\"%s\"", u);
	free(u);
}

static void print_identifier(struct dasm_state *dasm, const char *str)
{
	// if the identifier contains spaces, it must be quoted
	for (int i = 0; str[i]; i++) {
		if (SJIS_2BYTE(str[i])) {
			i++;
			continue;
		}
		if (str[i] == ' ') {
			print_string(dasm, str);
			return;
		}
	}

	print_sjis(dasm, str);
}

static void print_local_variable(struct dasm_state *dasm, struct ain_function *func, int varno)
{
	int dup_no = 0; // nr of duplicate-named variables preceding varno
	for (int i = 0; i < func->nr_vars; i++) {
		if (i == varno)
			break;
		if (!strcmp(func->vars[i].name, func->vars[varno].name))
			dup_no++;
	}

	// if variable name is ambiguous, add #n suffix
	char *name;
	char buf[512];
	if (dup_no) {
		snprintf(buf, 512, "%s#%d", func->vars[varno].name, dup_no);
		name = buf;
	} else {
		name = func->vars[varno].name;
	}

	print_identifier(dasm, name);
}

static void print_function_name(struct dasm_state *dasm, struct ain_function *func)
{
	int i = ain_get_function_index(dasm->ain, func);

	char *name = func->name;
	char buf[512];
	if (i > 0) {
		snprintf(buf, 512, "%s#%d", func->name, i);
		name = buf;
	}

	print_identifier(dasm, name);
}

static void print_hll_function_name(struct dasm_state *dasm, struct ain_library *lib, int fno)
{
	int dup_no = 0;
	for (int i = 0; i < lib->nr_functions; i++) {
		if (i == fno)
			break;
		if (!strcmp(lib->functions[i].name, lib->functions[fno].name))
			dup_no++;
	}

	char *name = lib->functions[fno].name;
	char buf[512];
	if (dup_no) {
		snprintf(buf, 512, "%s#%d", lib->functions[fno].name, dup_no);
		name = buf;
	}

	print_identifier(dasm, name);
}

static void print_argument(struct dasm_state *dasm, int32_t arg, enum instruction_argtype type, possibly_unused const char **comment)
{
	if (dasm->flags & DASM_RAW) {
		fprintf(dasm->out, "0x%x", arg);
		return;
	}
	char *label;
	struct ain *ain = dasm->ain;
	switch (type) {
	case T_INT:
	case T_SWITCH:
		fprintf(dasm->out, "0x%x", arg);
		break;
	case T_FLOAT:
		fprintf(dasm->out, "%f", arg_to_float(arg));
		break;
	case T_ADDR:
		label = get_label(arg);
		if (!label) {
			WARNING("No label generated for address: 0x%x", arg);
			fprintf(dasm->out, "0x%x", arg);
		} else {
			fprintf(dasm->out, "%s", label);
		}
		break;
	case T_FUNC:
		if (arg < 0 || arg >= ain->nr_functions)
			DASM_ERROR(dasm, "Invalid function number: %d", arg);
		print_function_name(dasm, &ain->functions[arg]);
		break;
	case T_DLG:
		if (arg < 0 || arg >= ain->nr_delegates)
			DASM_ERROR(dasm, "Invalid delegate number: %d", arg);
		print_identifier(dasm, ain->delegates[arg].name);
		break;
	case T_STRING:
		if (arg < 0 || arg >= ain->nr_strings)
			DASM_ERROR(dasm, "Invalid string number: %d", arg);
		if (dasm->flags & DASM_NO_STRINGS) {
			fprintf(dasm->out, "0x%x ", arg);
			*comment = ain->strings[arg]->text;
		} else {
			print_string(dasm, ain->strings[arg]->text);
		}
		break;
	case T_MSG:
		if (arg < 0 || arg >= ain->nr_messages)
			DASM_ERROR(dasm, "Invalid message number: %d", arg);
		if (dasm->flags & DASM_NO_STRINGS) {
			fprintf(dasm->out, "0x%x ", arg);
			*comment = ain->messages[arg]->text;
		} else {
			print_string(dasm, ain->messages[arg]->text);
		}
		break;
	case T_LOCAL:
		if (dasm->func < 0) {
			DASM_ERROR(dasm, "Attempt to access local variable outside of function");
			//WARNING("Attempt to access local variable outside of function");
			//print_string(dasm, "???");
			break;
		}
		if (arg < 0 || arg >= ain->functions[dasm->func].nr_vars)
			DASM_ERROR(dasm, "Invalid variable number: %d", arg);
		print_local_variable(dasm, &ain->functions[dasm->func], arg);
		break;
	case T_GLOBAL:
		if (arg < 0 || arg >= ain->nr_globals)
			DASM_ERROR(dasm, "Invalid global number: %d", arg);
		print_identifier(dasm, ain->globals[arg].name);
		break;
	case T_STRUCT:
		if (arg < 0 || arg >= ain->nr_structures)
			DASM_ERROR(dasm, "Invalid struct number: %d", arg);
		print_identifier(dasm, ain->structures[arg].name);
		break;
	case T_SYSCALL:
		if (arg < 0 || arg >= NR_SYSCALLS || !syscalls[arg].name)
			DASM_ERROR(dasm, "Invalid/unknown syscall number: %d", arg);
		fprintf(dasm->out, "%s", syscalls[arg].name);
		break;
	case T_HLL:
		if (arg < 0 || arg >= ain->nr_libraries)
			DASM_ERROR(dasm, "Invalid HLL library number: %d", arg);
		print_identifier(dasm, ain->libraries[arg].name);
		break;
	case T_HLLFUNC:
		fprintf(dasm->out, "0x%x", arg);
		break;
	case T_FILE:
		if (!ain->nr_filenames) {
			fprintf(dasm->out, "%d", arg);
			break;
		}
		if (arg < 0 || arg >= ain->nr_filenames)
			DASM_ERROR(dasm, "Invalid file number: %d", arg);
		print_identifier(dasm, ain->filenames[arg]);
		break;
	default:
		fprintf(dasm->out, "<UNKNOWN ARG TYPE: %d>", type);
		break;
	}
}

static void print_arguments(struct dasm_state *dasm, const struct instruction *instr)
{
	if (instr->opcode == CALLHLL) {
		int32_t lib = LittleEndian_getDW(dasm->ain->code, dasm->addr + 2);
		int32_t fun = LittleEndian_getDW(dasm->ain->code, dasm->addr + 6);
		fprintf(dasm->out, " %s ", dasm->ain->libraries[lib].name);
		print_hll_function_name(dasm, &dasm->ain->libraries[lib], fun);
		if (dasm->ain->version >= 11) {
			fprintf(dasm->out, " %d", LittleEndian_getDW(dasm->ain->code, dasm->addr + 10));
		}
		return;
	}
	if (instr->opcode == FUNC) {
		fputc(' ', dasm->out);
		fprintf(dasm->out, "0x%x", dasm->func);
		//ain_dump_function(dasm->out, dasm->ain, &dasm->ain->functions[dasm->func]);
		return;
	}

	const char *comment = NULL;
	for (int i = 0; i < instr->nr_args; i++) {
		fputc(' ', dasm->out);
		print_argument(dasm, LittleEndian_getDW(dasm->ain->code, dasm->addr + 2 + i*4), instr->args[i], &comment);
	}
	if (comment) {
		fprintf(dasm->out, "; ");
		print_string(dasm, comment);
	}
}

static void dasm_enter_function(struct dasm_state *dasm, int fno)
{
	if (fno < 0 || fno >= dasm->ain->nr_functions)
		DASM_ERROR(dasm, "Invalid function number: %d", fno);

	for (int i = 1; i < DASM_FUNC_STACK_SIZE; i++) {
		dasm->func_stack[i] = dasm->func_stack[i-1];
	}
	dasm->func_stack[0] = dasm->func;
	dasm->func = fno;

	fprintf(dasm->out, "; ");
	ain_dump_function(dasm->out, dasm->ain, &dasm->ain->functions[fno]);
	fputc('\n', dasm->out);
	//fprintf(dasm->out, "; FUNC 0x%x\n", fno);
}

static void dasm_leave_function(struct dasm_state *dasm)
{
	dasm->func = dasm->func_stack[0];
	for (int i = 1; i < DASM_FUNC_STACK_SIZE; i++) {
		dasm->func_stack[i-1] = dasm->func_stack[i];
	}
}

static void print_instruction(struct dasm_state *dasm, const struct instruction *instr)
{
	if (dasm->flags & DASM_RAW)
		fprintf(dasm->out, "0x%08lX:\t", dasm->addr);

	switch (instr->opcode) {
	case FUNC:
		dasm_enter_function(dasm, LittleEndian_getDW(dasm->ain->code, dasm->addr + 2));
		break;
	case ENDFUNC:
		dasm_leave_function(dasm);
		break;
	case _EOF:
		break;
	default:
		fputc('\t', dasm->out);
		break;
	}

	fprintf(dasm->out, "%s", instr->name);
	print_arguments(dasm, instr);
	fputc('\n', dasm->out);
}

static const struct instruction *get_instruction(struct dasm_state *dasm)
{
	uint16_t opcode = LittleEndian_getW(dasm->ain->code, dasm->addr);
	const struct instruction *instr = &instructions[opcode];
	if (opcode >= NR_OPCODES)
		DASM_ERROR(dasm, "Unknown/invalid opcode: %u", opcode);
	if (dasm->addr + instr->nr_args * 4 >= dasm->ain->code_size)
		DASM_ERROR(dasm, "CODE section truncated?");
	return instr;
}

static void print_switch_case(struct dasm_state *dasm, struct ain_switch_case *c)
{
	fprintf(dasm->out, ".CASE %ld:%ld ", c->parent - dasm->ain->switches, c - c->parent->cases);
	switch (c->parent->case_type) {
	case AIN_SWITCH_INT:
		fprintf(dasm->out, "%d", c->value);
		break;
	case AIN_SWITCH_STRING:
		if (dasm->flags & DASM_NO_STRINGS) {
			fprintf(dasm->out, "%d ; ", c->value);
			print_string(dasm, dasm->ain->strings[c->value]->text);
		} else {
			print_string(dasm, dasm->ain->strings[c->value]->text);
		}
		break;
	default:
		WARNING("Unknown switch case type: %d", c->parent->case_type);
		fprintf(dasm->out, "0x%x", c->value);
		break;
	}
	fputc('\n', dasm->out);
}

static char *genlabel(size_t addr)
{
	char name[64];
	snprintf(name, 64, "0x%lx", addr);
	return strdup(name);
}

static void generate_labels(struct dasm_state *dasm)
{
	jump_table_init();

	if (!(dasm->flags & DASM_RAW)) {
		for (dasm->addr = 0; dasm->addr < dasm->ain->code_size;) {
			const struct instruction *instr = get_instruction(dasm);
			for (int i = 0; i < instr->nr_args; i++) {
				if (instr->args[i] != T_ADDR)
					continue;
				int32_t arg = LittleEndian_getDW(dasm->ain->code, dasm->addr + 2 + i*4);
				add_label(genlabel(arg), arg);
			}
			dasm->addr += instruction_width(instr->opcode);
		}
	}
	for (int i = 0; i < dasm->ain->nr_switches; i++) {
		add_switch_default(&dasm->ain->switches[i]);
		for (int j = 0; j < dasm->ain->switches[i].nr_cases; j++) {
			add_switch_case(&dasm->ain->switches[i].cases[j]);
		}
	}
}

void dasm_init(struct dasm_state *dasm, FILE *out, struct ain *ain, uint32_t flags)
{
	dasm->out = out;
	dasm->ain = ain;
	dasm->flags = flags;
	dasm->addr = 0;
	dasm->func = -1;

	for (int i = 0; i < DASM_FUNC_STACK_SIZE; i++) {
		dasm->func_stack[i] = -1;
	}
}

void disassemble_ain(FILE *out, struct ain *ain, unsigned int flags)
{
	struct dasm_state dasm;
	dasm_init(&dasm, out, ain, flags);

	generate_labels(&dasm);

	for (dasm.addr = 0; dasm.addr < dasm.ain->code_size;) {
		const struct instruction *instr = get_instruction(&dasm);
		jump_list *targets = get_jump_targets(dasm.addr);
		if (targets) {
			for (size_t i = 0; i < kv_size(*targets); i++) {
				struct jump_target *t = kv_A(*targets, i);
				switch (t->type) {
				case JMP_LABEL:
					fprintf(dasm.out, "%s:\n", t->label);
					break;
				case JMP_CASE:
					print_switch_case(&dasm, t->switch_case);
					break;
				case JMP_DEFAULT:
					fprintf(dasm.out, ".DEFAULT %ld\n", t->switch_default - dasm.ain->switches);
					break;
				}
			}
		}
		print_instruction(&dasm, instr);
		dasm.addr += instruction_width(instr->opcode);
	}
	fflush(dasm.out);

	jump_table_fini();
}
