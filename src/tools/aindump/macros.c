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
#include "system4/ain.h"
#include "system4/instructions.h"
#include "aindump.h"
#include "dasm.h"

// Returns true if the current instruction can be elided.
// We need to prevent eliding instructions that are jump targets.
static bool can_elide(struct dasm_state *dasm)
{
	return !dasm_eof(dasm) && !dasm_is_jump_target(dasm);
}

static bool check_opcode(struct dasm_state *dasm, enum opcode opcode)
{
	return dasm->instr->opcode == opcode && can_elide(dasm);
}

static bool check_next_opcode(struct dasm_state *dasm, enum opcode opcode)
{
	dasm_next(dasm);
	return check_opcode(dasm, opcode);
}

/* Macro List

.GLOBALREF <varname>:
    PUSHGLOBALPAGE
    PUSH <varno>
    REF

.GLOBALREFREF <varname>:
    PUSHGLOBALPAGE
    PUSH <varno>
    REFREF

.LOCALREF <varname>:
    PUSHLOCALPAGE
    PUSH <varno>
    REF

.LOCALREFREF <varname>:
    PUSHLOCALPAGE
    PUSH <varno>
    REFREF

.LOCALINC:
    PUSHLOCALPAGE
    PUSH <varno>
    INC

.LOCALDEC:
    PUSHLOCALPAGE
    PUSH <varno>
    DEC

.LOCALASSIGN <varname> <value>:
    PUSHLOCALPAGE
    PUSH <varno>
    PUSH <value>
    ASSIGN

.LOCALDELETE:
    PUSHLOCALPAGE
    PUSH <varno>
    DUP2
    REF
    DELETE
    PUSH -1
    ASSIGN
    POP

.LOCALCREATE:
    PUSHLOCALPAGE
    PUSH <varno>
    DUP2
    REF
    DELETE
    DUP2
    NEW <structno> -1
    ASSIGN
    POP
    POP
    POP

 */

static void print_local(struct dasm_state *dasm, int32_t no)
{
	dasm_print_local_variable(dasm, &dasm->ain->functions[dasm->func], no);
}

static bool local_create_or_delete_macro(struct dasm_state *dasm, int32_t varno)
{
	if (!check_next_opcode(dasm, REF))
		return false;

	if (!check_next_opcode(dasm, DELETE))
		return false;

	dasm_next(dasm);
	if (check_opcode(dasm, PUSH)) {
		// localdelete
		if (dasm_arg(dasm, 0) != -1)
			return false;

		if (!check_next_opcode(dasm, ASSIGN))
			return false;

		if (!check_next_opcode(dasm, POP))
			return false;

		fprintf(dasm->out, ".LOCALDELETE ");
		dasm_print_local_variable(dasm, &dasm->ain->functions[dasm->func], varno);
		fputc('\n', dasm->out);
		return true;
	} else if (check_opcode(dasm, DUP2)) {
		// localcreate
		if (!check_next_opcode(dasm, NEW))
			return false;

		int32_t structno = dasm_arg(dasm, 0);
		if (structno < 0 || structno >= dasm->ain->nr_structures)
			return false;
		if (dasm_arg(dasm, 1) != -1)
			return false;

		if (!check_next_opcode(dasm, ASSIGN))
			return false;
		if (!check_next_opcode(dasm, POP))
			return false;
		if (!check_next_opcode(dasm, POP))
			return false;
		if (!check_next_opcode(dasm, POP))
			return false;

		fprintf(dasm->out, ".LOCALCREATE ");
		print_local(dasm, varno);
		fputc(' ', dasm->out);
		dasm_print_identifier(dasm, dasm->ain->structures[structno].name);
		fputc('\n', dasm->out);
		return true;
	}

	return false;
}

static bool localassign_macro(struct dasm_state *dasm, int32_t no)
{
	int32_t val = dasm_arg(dasm, 0);
	if (!check_next_opcode(dasm, ASSIGN))
		return false;

	fprintf(dasm->out, ".LOCALASSIGN ");
	print_local(dasm, no);
	fputc(' ', dasm->out);
	fprintf(dasm->out, "%d\n", val);
	return true;
}

static bool localpage_macro(struct dasm_state *dasm)
{
	dasm_next(dasm);
	if (!check_opcode(dasm, PUSH))
		return false;

	int32_t no = dasm_arg(dasm, 0);
	if (no >= dasm->ain->functions[dasm->func].nr_vars)
		return false;

	dasm_next(dasm);
	if (check_opcode(dasm, REF)) {
		fprintf(dasm->out, ".LOCALREF ");
	} else if (check_opcode(dasm, REFREF)) {
		fprintf(dasm->out, ".LOCALREFREF ");
	} else if (check_opcode(dasm, INC)) {
		fprintf(dasm->out, ".LOCALINC ");
	} else if (check_opcode(dasm, DEC)) {
		fprintf(dasm->out, ".LOCALDEC ");
	} else if (check_opcode(dasm, PUSH)) {
		return localassign_macro(dasm, no);
	} else if (check_opcode(dasm, DUP2)) {
		return local_create_or_delete_macro(dasm, no);
	} else {
		return false;
	}

	print_local(dasm, no);
	fputc('\n', dasm->out);

	return true;
}

static bool globalassign_macro(struct dasm_state *dasm, int32_t no)
{
	int32_t val = dasm_arg(dasm, 0);
	if (!check_next_opcode(dasm, ASSIGN))
		return false;

	fprintf(dasm->out, ".GLOBALASSIGN ");
	dasm_print_identifier(dasm, dasm->ain->globals[no].name);
	fputc(' ', dasm->out);
	fprintf(dasm->out, "%d\n", val);
	return true;

}

static bool globalpage_macro(struct dasm_state *dasm)
{
	dasm_next(dasm);
	if (!check_opcode(dasm, PUSH))
		return false;

	int32_t no = dasm_arg(dasm, 0);
	if (no >= dasm->ain->nr_globals)
		return false;

	dasm_next(dasm);
	if (check_opcode(dasm, REF)) {
		fprintf(dasm->out, ".GLOBALREF ");
	} else if (check_opcode(dasm, REFREF)) {
		fprintf(dasm->out, ".GLOBALREFREF ");
	} else if (check_opcode(dasm, INC)) {
		fprintf(dasm->out, ".GLOBALINC ");
	} else if (check_opcode(dasm, DEC)) {
		fprintf(dasm->out, ".GLOBALDEC ");
	} else if (check_opcode(dasm, PUSH)) {
		return globalassign_macro(dasm, no);
	} else {
		return false;
	}

	dasm_print_identifier(dasm, dasm->ain->globals[no].name);
	fputc('\n', dasm->out);

	return true;
}

static bool structassign_macro(struct dasm_state *dasm, struct ain_struct *s, struct ain_variable *m)
{
	int32_t val = dasm_arg(dasm, 0);
	if (!check_next_opcode(dasm, ASSIGN))
		return false;

	fprintf(dasm->out, ".STRUCTASSIGN ");
	dasm_print_identifier(dasm, s->name);
	fputc(' ', dasm->out);
	dasm_print_identifier(dasm, m->name);
	fputc(' ', dasm->out);
	fprintf(dasm->out, "%d\n", val);
	return true;
}

static bool structpage_macro(struct dasm_state *dasm)
{
	struct ain_function *f = &dasm->ain->functions[dasm->func];
	if (f->struct_type < 0)
		return false;
	if (!check_next_opcode(dasm, PUSH))
		return false;

	int32_t no = dasm_arg(dasm, 0);
	struct ain_struct *s = &dasm->ain->structures[f->struct_type];
	if (no < 0 || no >= s->nr_members)
		return false;

	struct ain_variable *m = &s->members[no];
	dasm_next(dasm);
	if (check_opcode(dasm, REF)) {
		fprintf(dasm->out, ".STRUCTREF ");
	} else if (check_opcode(dasm, REFREF)) {
		fprintf(dasm->out, ".STRUCTREFREF ");
	} else if (check_opcode(dasm, INC)) {
		fprintf(dasm->out, ".STRUCTINC ");
	} else if (check_opcode(dasm, DEC)) {
		fprintf(dasm->out, ".STRUCTDEC ");
	} else if (check_opcode(dasm, PUSH)) {
		return structassign_macro(dasm, s, m);
	} else {
		return false;
	}

	dasm_print_identifier(dasm, s->name);
	fputc(' ', dasm->out);
	dasm_print_identifier(dasm, m->name);
	fputc('\n', dasm->out);

	return true;
}

static bool _dasm_print_macro(struct dasm_state *dasm)
{
	switch (dasm->instr->opcode) {
	case PUSHLOCALPAGE:
		return localpage_macro(dasm);
	case PUSHGLOBALPAGE:
		return globalpage_macro(dasm);
	case PUSHSTRUCTPAGE:
		return structpage_macro(dasm);
	default:
		return false;
	}
}

bool dasm_print_macro(struct dasm_state *dasm)
{
	dasm_save_t save = dasm_save(dasm);
	bool success = _dasm_print_macro(dasm);
	if (!success)
		dasm_restore(dasm, save);
	return success;
}
