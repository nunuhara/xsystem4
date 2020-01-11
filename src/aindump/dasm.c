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
#include "little_endian.h"
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#define DASM_ERROR(dasm, fmt, ...) ERROR("At 0x%x: " fmt, dasm->addr, ##__VA_ARGS__)

struct dasm_state {
	struct ain *ain;
	FILE *out;
	size_t addr;
	int func;
	bool raw;
};

#define NR_LABEL_BUCKETS 128

struct label {
	char *name;
	size_t addr;
};

struct label_bucket {
	int nr_labels;
	struct label *labels;
};

struct label_bucket label_table[NR_LABEL_BUCKETS];

static struct label_bucket *get_bucket(size_t addr)
{
	return &label_table[(addr>>4) % NR_LABEL_BUCKETS];
}

static char *get_label(size_t addr)
{
	struct label_bucket *bucket = get_bucket(addr);
	for (int i = 0; i < bucket->nr_labels; i++) {
		if (bucket->labels[i].addr == addr)
			return bucket->labels[i].name;
	}
	return NULL;
}

static void add_label(char *name, size_t addr)
{
	// NOTE: first registered label takes precedence
	if (get_label(addr)) {
		free(name);
		return;
	}

	struct label_bucket *bucket = get_bucket(addr);
	int n = bucket->nr_labels;

	bucket->labels = xrealloc(bucket->labels, sizeof(struct label) * (n + 1));
	bucket->nr_labels = n+1;

	bucket->labels[n].name = name;
	bucket->labels[n].addr = addr;
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

static void print_string(struct dasm_state *dasm, struct string *s)
{
	int i, escapes = 0;
	char *u = sjis2utf(s->text, s->size);

	// check for characters that need escaping
	for (i = 0; u[i]; i++) {
		if (u[i] == '"' || u[i] == '\n') {
			escapes++;
		}
	}

	// add backslash escapes
	if (escapes > 0) {
		int j = 0;
		char *tmp = xmalloc(i + escapes + 1);
		for (int i = 0; u[i];) {
			if (u[i] == '"') {
				tmp[j++] = '\\';
				tmp[j++] = u[i++];
			} else if (u[i] == '\n') {
				tmp[j++] = '\\';
				tmp[j++] = 'n';
				i++;
			} else {
				tmp[j++] = u[i++];
			}
		}
		tmp[i+escapes] = '\0';
		free(u);
		u = tmp;
	}

	fprintf(dasm->out, "\"%s\"", u);
	free(u);
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
		print_sjis(dasm, ain->functions[arg].name);
		break;
	case T_DLG:
		if (arg < 0 || arg >= ain->nr_delegates)
			DASM_ERROR(dasm, "Invalid delegate number: %d", arg);
		print_sjis(dasm, ain->delegates[arg].name);
		break;
	case T_STRING:
		if (arg < 0 || arg >= ain->nr_strings)
			DASM_ERROR(dasm, "Invalid string number: %d", arg);
		print_string(dasm, ain->strings[arg]);
		break;
	case T_MSG:
		if (arg < 0 || arg >= ain->nr_messages)
			DASM_ERROR(dasm, "Invalid message number: %d", arg);
		print_string(dasm, ain->messages[arg]);
		break;
	case T_LOCAL:
		if (dasm->func < 0)
			DASM_ERROR(dasm, "Attempt to access local variable outside of function");
		if (arg < 0 || arg >= ain->functions[dasm->func].nr_vars)
			DASM_ERROR(dasm, "Invalid variable number: %d", arg);
		print_sjis(dasm, ain->functions[dasm->func].vars[arg].name);
		break;
	case T_GLOBAL:
		if (arg < 0 || arg >= ain->nr_globals)
			DASM_ERROR(dasm, "Invalid global number: %d", arg);
		print_sjis(dasm, ain->globals[arg].name);
		break;
	case T_STRUCT:
		if (arg < 0 || arg >= ain->nr_structures)
			DASM_ERROR(dasm, "Invalid struct number: %d", arg);
		print_sjis(dasm, ain->structures[arg].name);
		break;
	case T_SYSCALL:
		if (arg < 0 || arg >= NR_SYSCALLS || !syscalls[arg].name)
			DASM_ERROR(dasm, "Invalid/unknown syscall number: %d", arg);
		fprintf(dasm->out, "%s", syscalls[arg].name);
		break;
	case T_HLL:
		if (arg < 0 || arg >= ain->nr_libraries)
			DASM_ERROR(dasm, "Invalid HLL library number: %d", arg);
		print_sjis(dasm, ain->libraries[arg].name);
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
		print_sjis(dasm, ain->filenames[arg]);
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

	for (int i = 0; i < instr->nr_args; i++) {
		fputc(' ', dasm->out);
		print_argument(dasm, LittleEndian_getDW(dasm->ain->code, dasm->addr + 2 + i*4), instr->args[i]);
	}
}

static void print_instruction(struct dasm_state *dasm, const struct instruction *instr)
{
	if (dasm->raw) {
		fprintf(dasm->out, "0x%08lX:\t", dasm->addr);
	}
	switch (instr->opcode) {
	case FUNC:
		dasm->func = LittleEndian_getDW(dasm->ain->code, dasm->addr + 2);
		if (dasm->func < 0 || dasm->func >= dasm->ain->nr_functions)
			DASM_ERROR(dasm, "Invalid function number: %d", dasm->func);
		break;
	case ENDFUNC:
		dasm->func = -1;
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

void disassemble_ain(FILE *out, struct ain *ain, bool raw)
{
	struct dasm_state dasm = {
		.ain  = ain,
		.out  = out,
		.addr = 0,
		.func = -1,
		.raw  = raw
	};

	if (!raw)
		generate_labels(&dasm);

	for (dasm.addr = 0; dasm.addr < dasm.ain->code_size;) {
		const struct instruction *instr = get_instruction(&dasm);
		char *label = get_label(dasm.addr);
		if (label)
			fprintf(dasm.out, "%s:\n", label);
		print_instruction(&dasm, instr);
		dasm.addr += instruction_width(instr->opcode);
	}
	fflush(dasm.out);

	for (int i = 0; i < NR_LABEL_BUCKETS; i++) {
		for (int j = 0; j < label_table[i].nr_labels; j++) {
			free(label_table[i].labels[j].name);
		}
		free(label_table[i].labels);
	}
}
