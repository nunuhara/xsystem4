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

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "ainedit.h"
#include "system4.h"
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "asm_parser.tab.h"

extern FILE *yyin;

KHASH_MAP_INIT_STR(string_ht, size_t);

// TODO: better error messages
#define ASM_ERROR(state, ...) ERROR(__VA_ARGS__)

#define PSEUDO_OP(code, nargs, ...)	\
	[code - PSEUDO_OP_OFFSET] = {	\
		.opcode = code,		\
		.name = "." #code ,	\
		.nr_args = nargs,	\
		.ip_inc = 0,		\
		.implemented = false,	\
		.args = { __VA_ARGS__ } \
	}

const struct instruction asm_pseudo_ops[NR_PSEUDO_OPS - PSEUDO_OP_OFFSET] = {
	PSEUDO_OP(CASE, 2)
};

struct string_table {
	struct string **strings;
	size_t size;
	size_t allocated;
};

struct asm_state {
	struct ain *ain;
	uint32_t flags;
	uint8_t *buf;
	size_t buf_ptr;
	size_t buf_len;
	int32_t func;
	int32_t lib;
	struct string_table strings;
	struct string_table messages;
	khash_t(string_ht) *strings_index;
	khash_t(string_ht) *messages_index;
};

static int string_table_add(struct string_table *t, const char *s)
{
	if (!t->allocated) {
		t->allocated = 4096;
		t->strings = xmalloc(t->allocated * sizeof(struct string*));
	} else if (t->allocated <= t->size) {
		t->allocated *= 2;
		t->strings = xrealloc(t->strings, t->allocated * sizeof(struct string*));
	}

	t->strings[t->size++] = make_string(s, strlen(s));
	return t->size - 1;
}

static int asm_add_message(struct asm_state *state, const char *s)
{
	char *u = utf2sjis(s, strlen(s));
	int i = string_table_add(&state->messages, u);
	free(u);
	return i;
}

static int asm_add_string(struct asm_state *state, const char *s)
{
	int ret;
	char *u = utf2sjis(s, strlen(s));
	khiter_t k = kh_put(string_ht, state->strings_index, u, &ret);
	if (!ret) {
		// nothing
	} else if (ret == 1) {
		kh_value(state->strings_index, k) = string_table_add(&state->strings, u);
	} else {
		ERROR("Hash table lookup failed (%d)", ret);
	}
	return kh_value(state->strings_index, k);
}

static void init_asm_state(struct asm_state *state, struct ain *ain, uint32_t flags)
{
	memset(state, 0, sizeof(*state));
	state->ain = ain;
	state->flags = flags;
	state->func = -1;
	state->lib = -1;
	state->strings_index = kh_init(string_ht);
	state->messages_index = kh_init(string_ht);
}

static void fini_asm_state(struct asm_state *state)
{
	kh_destroy(string_ht, state->strings_index);
	kh_destroy(string_ht, state->messages_index);
}

static void asm_write_opcode(struct asm_state *state, uint16_t opcode)
{
//	if (state->buf_len - state->buf_ptr <= (size_t)instruction_width(opcode)) {
	if (state->buf_len - state->buf_ptr <= 18) {
		if (!state->buf) {
			state->buf_len = 4096;
			state->buf = xmalloc(state->buf_len);
		} else {
			state->buf_len = state->buf_len * 2;
			state->buf = xrealloc(state->buf, state->buf_len);
		}
	}

	state->buf[state->buf_ptr++] = opcode & 0xFF;
	state->buf[state->buf_ptr++] = (opcode & 0xFF00) >> 8;
}

static void asm_write_argument(struct asm_state *state, uint32_t arg)
{
	state->buf[state->buf_ptr++] = (arg & 0x000000FF);
	state->buf[state->buf_ptr++] = (arg & 0x0000FF00) >> 8;
	state->buf[state->buf_ptr++] = (arg & 0x00FF0000) >> 16;
	state->buf[state->buf_ptr++] = (arg & 0xFF000000) >> 24;
}

const struct instruction *asm_get_instruction(const char *name)
{
	if (name[0] == '.') {
		for (int i = 0; i < NR_PSEUDO_OPS; i++) {
			if (!strcmp(name, asm_pseudo_ops[i].name))
				return &asm_pseudo_ops[i];
		}
	}
	for (int i = 0; i < NR_OPCODES; i++) {
		if (!strcmp(name, instructions[i].name))
			return &instructions[i];
	}
	return NULL;
}

static char *parse_identifier(possibly_unused struct asm_state *state, char *s, int *n)
{
	char *delim = strchr(s, '#');
	if (!delim) {
		*n = 0;
		return s;
	}

	*delim = '\0';
	delim++;

	char *endptr;
	long nn = strtol(delim, &endptr, 10);
	if (*delim && !*endptr) {
		*n = nn;
		return s;
	}

	*delim = '#';
	ASM_ERROR(state, "Invalid identifier: '%s' (bad suffix)", s);
}

static int32_t parse_integer_constant(possibly_unused struct asm_state *state, const char *arg)
{
	char *endptr;
	errno = 0;
	long i = strtol(arg, &endptr, 0);
	if (errno || *endptr != '\0')
		ASM_ERROR(state, "Invalid integer constant: '%s'", arg);
	//if (i > INT32_MAX || i < INT32_MIN)
	//	ASM_ERROR(state, "Integer would be truncated: %ld -> %d", i, (int32_t)i);
	return i;
}

static uint32_t asm_resolve_arg(struct asm_state *state, enum instruction_argtype type, const char *arg)
{
	if (state->flags & ASM_RAW)
		type = T_INT;

	switch (type) {
	case T_INT:
		return parse_integer_constant(state, arg);
	case T_FLOAT: {
		char *endptr;
		union { int32_t i; float f; } v;
		errno = 0;
		v.f = strtof(arg, &endptr);
		if (errno || *endptr != '\0')
			ASM_ERROR(state, "Invalid float: %s", arg);
		return v.i;
	}
	case T_SWITCH: {
		int i = parse_integer_constant(state, arg);
		if (i < 0 || i >= state->ain->nr_switches)
			ASM_ERROR(state, "Invalid switch number: %d", i);
		return i;
	}
	case T_ADDR: {
		khiter_t k;
		k = kh_get(label_table, label_table, arg);
		if (k == kh_end(label_table))
			ASM_ERROR(state, "Unable to resolve label: '%s'", arg);
		return kh_value(label_table, k);
	}
	case T_FUNC: {
		char *u = utf2sjis(arg, strlen(arg));
		struct ain_function *f = ain_get_function(state->ain, u);
		free(u);
		if (!f)
			ASM_ERROR(state, "Unable to resolve function: '%s'", arg);
		return f - state->ain->functions;
	}
	case T_STRING: {
		if (state->flags & ASM_NO_STRINGS) {
			int32_t i = parse_integer_constant(state, arg);
			if (i < 0 || i >= state->ain->nr_strings)
				ASM_ERROR(state, "String index out of bounds: '%s'", arg);
			return i;
		}
		return asm_add_string(state, arg);
	}
	case T_MSG: {
		if (state->flags & ASM_NO_STRINGS) {
			int32_t i = parse_integer_constant(state, arg);
			if (i < 0 || i >= state->ain->nr_messages)
				ASM_ERROR(state, "Message index out of bounds: '%s'", arg);
			return i;
		}
		return asm_add_message(state, arg);
	}
	case T_LOCAL: {
		int n, count = 0;
		char *u = utf2sjis(arg, strlen(arg));
		u = parse_identifier(state, u, &n);
		struct ain_function *f = &state->ain->functions[state->func];
		for (int i = 0; i < f->nr_vars; i++) {
			if (!strcmp(u, f->vars[i].name)) {
				if (count < n) {
					count++;
					continue;
				}
				free(u);
				return i;
			}
		}
		ASM_ERROR(state, "Unable to resolve local variable: '%s'", arg);
	}
	case T_GLOBAL: {
		char *u = utf2sjis(arg, strlen(arg));
		for (int i = 0; i < state->ain->nr_globals; i++) {
			if (!strcmp(u, state->ain->globals[i].name)) {
				free(u);
				return i;
			}
		}
		ASM_ERROR(state, "Unable to resolve global variable: '%s'", arg);
	}
	case T_STRUCT: {
		char *u = utf2sjis(arg, strlen(arg));
		struct ain_struct *s = ain_get_struct(state->ain, u);
		free(u);
		if (!s)
			ASM_ERROR(state, "Unable to resolve struct: '%s'", arg);
		return s - state->ain->structures;
	}
	case T_SYSCALL: {
		for (int i = 0; i < NR_SYSCALLS; i++) {
			if (!strcmp(arg, syscalls[i].name))
				return i;
		}
		ASM_ERROR(state, "Unable to resolve system call: '%s'", arg);
	}
	case T_HLL: {
		for (int i = 0; i < state->ain->nr_libraries; i++) {
			if (!strcmp(arg, state->ain->libraries[i].name)) {
				state->lib = i;
				return i;
			}
		}
		ASM_ERROR(state, "Unable to resolve library: '%s'", arg);
	}
	case T_HLLFUNC: {
		if (state->lib < 0)
			ERROR("Tried to resolve library function without active library?");
		int n, count = 0;
		char *u = utf2sjis(arg, strlen(arg));
		u = parse_identifier(state, u, &n);
		for (int i = 0; i < state->ain->libraries[state->lib].nr_functions; i++) {
			if (strcmp(u, state->ain->libraries[state->lib].functions[i].name))
				continue;
			if (count < n) {
				count++;
				continue;
			}
			state->lib = -1;
			free(u);
			return i;
		}
		ASM_ERROR(state, "Unable to resolve library function: '%s.%s'",
			  state->ain->libraries[state->lib].name, arg);
	}
	case T_FILE: {
		if (!state->ain->nr_filenames)
			return atoi(arg);
		char *u = utf2sjis(arg, strlen(arg));
		for (int i = 0; i < state->ain->nr_filenames; i++) {
			if (!strcmp(u, state->ain->filenames[i])) {
				free(u);
				return i;
			}
		}
		ASM_ERROR(state, "Unable to resolve filename: '%s'", arg);
	}
	case T_DLG: {
		char *u = utf2sjis(arg, strlen(arg));
		for (int i = 0; i < state->ain->nr_delegates; i++) {
			if (!strcmp(u, state->ain->delegates[i].name)) {
				free(u);
				return i;
			}
		}
		ASM_ERROR(state, "Unable to resolve delegate: '%s'", arg);
	}
	default:
		ASM_ERROR(state, "Unhandled argument type: %d", type);
	}
}

void handle_pseudo_op(struct asm_state *state, struct parse_instruction *instr)
{
	switch (instr->opcode) {
	case CASE: {
		char *s_switch = kv_A(*instr->args, 0)->text;
		char *s_case = strchr(s_switch, ':');
		if (!s_case)
			ASM_ERROR(state, "Invalid switch/case index: '%s'", s_switch);

		*s_case++ = '\0';
		int n_switch = parse_integer_constant(state, s_switch);
		int n_case = parse_integer_constant(state, s_case);
		if (n_switch < 0 || n_switch >= state->ain->nr_switches)
			ASM_ERROR(state, "Invalid switch index: %d", n_switch);
		if (n_case < 0 || n_case >= state->ain->switches[n_switch].nr_cases)
			ASM_ERROR(state, "Invalid case index: %d", n_case);

		int c;
		struct ain_switch *swi = &state->ain->switches[n_switch];
		if (swi->case_type == AIN_SWITCH_STRING && !(state->flags & ASM_NO_STRINGS)) {
			c = asm_add_string(state, kv_A(*instr->args, 1)->text);
		} else {
			c = parse_integer_constant(state, kv_A(*instr->args, 1)->text);
		}
		if ((size_t)swi->cases[n_case].address != state->buf_ptr)
			ASM_ERROR(state, "ADDR NO MATCH");
		if (swi->cases[n_case].value != c)
			ASM_ERROR(state, "VALUE NO MATCH");
		swi->cases[n_case].address = state->buf_ptr;
		swi->cases[n_case].value = c;
		break;
	}
	}
}

void asm_assemble_jam(const char *filename, struct ain *ain, uint32_t flags)
{
	struct asm_state state;
	init_asm_state(&state, ain, flags);

	if (filename) {
		if (!strcmp(filename, "-"))
			yyin = stdin;
		else
			yyin = fopen(filename, "r");
		if (!yyin)
			ERROR("Opening input file '%s': %s", filename, strerror(errno));
	}
	label_table = kh_init(label_table);
	NOTICE("Parsing...");
	yyparse();

	NOTICE("Encoding...");
	for (size_t i = 0; i < kv_size(*parsed_code); i++) {
		struct parse_instruction *instr = kv_A(*parsed_code, i);
		if (instr->opcode >= PSEUDO_OP_OFFSET) {
			handle_pseudo_op(&state, instr);
			continue;
		}

		struct instruction *idef = &instructions[instr->opcode];

		// NOTE: special case: we need to record the new function address in the ain structure
		if (idef->opcode == FUNC) {
			state.func = asm_resolve_arg(&state, T_INT, kv_A(*instr->args, 0)->text);
			state.ain->functions[state.func].address = state.buf_ptr + 6;
			asm_write_opcode(&state, FUNC);
			asm_write_argument(&state, state.func);
			continue;
		}

		asm_write_opcode(&state, instr->opcode);
		for (int a = 0; a < idef->nr_args; a++) {
			asm_write_argument(&state, asm_resolve_arg(&state, idef->args[a], kv_A(*instr->args, a)->text));
		}
	}

	for (size_t i = 0; i < kv_size(*parsed_code); i++) {
		struct parse_instruction *instr = kv_A(*parsed_code, i);
		if (!instr->args)
			continue;
		for (size_t a = 0; a < kv_size(*instr->args); a++) {
			free_string(kv_A(*instr->args, a));
		}
	}

	/*
	if (state.buf_ptr != ain->code_size)
		WARNING("CODE SIZE CHANGED");
	if (state.strings.size != (size_t)ain->nr_strings)
		WARNING("NR STRINGS CHANGED (%d -> %lu)", ain->nr_strings, state.strings.size);
	if (state.messages.size != (size_t)ain->nr_messages)
		WARNING("NR MESSAGES CHANGED (%d -> %lu)", ain->nr_messages, state.messages.size);
	*/

	free(ain->code);
	ain->code = state.buf;
	ain->code_size = state.buf_ptr;

	if (!(state.flags & ASM_NO_STRINGS)) {
		ain_free_strings(ain);
		ain->strings = state.strings.strings;
		ain->nr_strings = state.strings.size;

		ain_free_messages(ain);
		ain->messages = state.messages.strings;
		ain->nr_messages = state.messages.size;
	}

	// TODO: verify integrity of ain file (e.g. does MAIN still point to a valid function? etc.)

	fini_asm_state(&state);
}
