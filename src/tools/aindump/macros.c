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

static bool print_localref(struct dasm_state *dasm)
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
	} else {
		return false;
	}

	//dasm_print_identifier(dasm, dasm->ain->functions[dasm->func].vars[no].name);
	dasm_print_local_variable(dasm, &dasm->ain->functions[dasm->func], no);
	fputc('\n', dasm->out);

	return true;
}

static bool print_globalref(struct dasm_state *dasm)
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
	} else {
		return false;
	}

	dasm_print_identifier(dasm, dasm->ain->globals[no].name);
	fputc('\n', dasm->out);

	return true;
}

static bool _dasm_print_macro(struct dasm_state *dasm)
{
	switch (dasm->instr->opcode) {
	case PUSHLOCALPAGE:
		return print_localref(dasm);
	case PUSHGLOBALPAGE:
		return print_globalref(dasm);
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
