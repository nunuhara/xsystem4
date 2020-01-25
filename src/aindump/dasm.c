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
#include "little_endian.h"
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#define DASM_ERROR(dasm, fmt, ...) ERROR("At 0x%x: " fmt, dasm->addr, ##__VA_ARGS__)

#define DASM_FUNC_STACK_SIZE 16

struct dasm_state {
	struct ain *ain;
	FILE *out;
	size_t addr;
	int func;
	bool raw;
	int func_stack[DASM_FUNC_STACK_SIZE];
};

KHASH_MAP_INIT_INT(label_table, char*);
khash_t(label_table) *label_table;

static void label_table_init(void)
{
	label_table = kh_init(label_table);
}
static char *get_label(unsigned int addr)
{
	khiter_t k;
	k = kh_get(label_table, label_table, addr);
	if (k == kh_end(label_table))
		return NULL;
	return kh_value(label_table, k);
}

static void add_label(char *name, unsigned int addr)
{
	int ret;
	khiter_t k = kh_put(label_table, label_table, addr, &ret);
	if (!ret) {
		// key already present: first label takes precedence
		free(name);
		return;
	}
	kh_value(label_table, k) = name;
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
	for (int i = 0; str[i]; i++) {
		for (int j = 0; escape_chars[j]; j++) {
			if (str[i] == escape_chars[j]) {
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

static void print_argument(struct dasm_state *dasm, int32_t arg, enum instruction_argtype type)
{
	if (dasm->raw) {
		fprintf(dasm->out, "0x%x", arg);
		return;
	}
	char *label;
	struct ain *ain = dasm->ain;
	switch (type) {
	case T_INT:
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
		print_identifier(dasm, ain->functions[arg].name);
		break;
	case T_DLG:
		if (arg < 0 || arg >= ain->nr_delegates)
			DASM_ERROR(dasm, "Invalid delegate number: %d", arg);
		print_identifier(dasm, ain->delegates[arg].name);
		break;
	case T_STRING:
		if (arg < 0 || arg >= ain->nr_strings)
			DASM_ERROR(dasm, "Invalid string number: %d", arg);
		print_string(dasm, ain->strings[arg]->text);
		break;
	case T_MSG:
		if (arg < 0 || arg >= ain->nr_messages)
			DASM_ERROR(dasm, "Invalid message number: %d", arg);
		print_string(dasm, ain->messages[arg]->text);
		break;
	case T_LOCAL:
		if (dasm->func < 0) {
			//DASM_ERROR(dasm, "Attempt to access local variable outside of function");
			WARNING("Attempt to access local variable outside of function");
			print_string(dasm, "???");
			break;
		}
		if (arg < 0 || arg >= ain->functions[dasm->func].nr_vars)
			DASM_ERROR(dasm, "Invalid variable number: %d", arg);
		print_identifier(dasm, ain->functions[dasm->func].vars[arg].name);
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
		fprintf(dasm->out, " %s.%s", dasm->ain->libraries[lib].name, dasm->ain->libraries[lib].functions[fun].name);
		return;
	}
	if (instr->opcode == FUNC) {
		fputc(' ', dasm->out);
		ain_dump_function(dasm->out, dasm->ain, &dasm->ain->functions[dasm->func]);
		return;
	}

	for (int i = 0; i < instr->nr_args; i++) {
		fputc(' ', dasm->out);
		print_argument(dasm, LittleEndian_getDW(dasm->ain->code, dasm->addr + 2 + i*4), instr->args[i]);
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

	fprintf(dasm->out, "; FUNC 0x%x\n", fno);
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
	if (dasm->raw)
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

static char *genlabel(size_t addr)
{
	char name[64];
	snprintf(name, 64, "0x%lx", addr);
	return strdup(name);
}

static void generate_labels(struct dasm_state *dasm)
{
	label_table_init();
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

void dasm_init(struct dasm_state *dasm, FILE *out, struct ain *ain, bool raw)
{
	dasm->out = out;
	dasm->ain = ain;
	dasm->addr = 0;
	dasm->raw = raw;
	dasm->func = -1;

	for (int i = 0; i < DASM_FUNC_STACK_SIZE; i++) {
		dasm->func_stack[i] = -1;
	}
}

void disassemble_ain(FILE *out, struct ain *ain, bool raw)
{
	struct dasm_state dasm;
	dasm_init(&dasm, out, ain, raw);

	if (!raw)
		generate_labels(&dasm);

	for (dasm.addr = 0; dasm.addr < dasm.ain->code_size;) {
		const struct instruction *instr = get_instruction(&dasm);
		if (!raw) {
			char *label = get_label(dasm.addr);
			if (label)
				fprintf(dasm.out, "%s:\n", label);
		}
		print_instruction(&dasm, instr);
		dasm.addr += instruction_width(instr->opcode);
	}
	fflush(dasm.out);

	if (!raw) {
		char *label;
		kh_foreach_value(label_table, label, free(label));
	}
}
