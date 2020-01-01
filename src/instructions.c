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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "system4.h"
#include "system4/instructions.h"

// JMP = instruction that modifies the instruction pointer
// OP  = everything else
#define OP(code, nargs, ...)			\
	[code] = {				\
		.opcode = code,			\
		.name = #code ,			\
		.nr_args = nargs,		\
		.ip_inc = 2 + nargs * 4,	\
		.args = { __VA_ARGS__ }		\
	}
#define JMP(code, nargs, ...)			\
	[code] = {				\
		.opcode = code,			\
		.name = #code ,			\
		.nr_args = nargs,		\
		.ip_inc = 0,			\
		.args = { __VA_ARGS__ }		\
	}

const struct instruction instructions[NR_OPCODES] = {
	OP(PUSH, 1, INSTR_INT),
        OP(POP, 0),
        OP(REF, 0),
        OP(REFREF, 0),
        OP(PUSHGLOBALPAGE, 0),
        OP(PUSHLOCALPAGE, 0),
        OP(INV, 0),
        OP(NOT, 0),
        OP(COMPL, 0),
        OP(ADD, 0),
        OP(SUB, 0),
        OP(MUL, 0),
        OP(DIV, 0),
        OP(MOD, 0),
        OP(AND, 0),
        OP(OR, 0),
        OP(XOR, 0),
        OP(LSHIFT, 0),
        OP(RSHIFT, 0),
        OP(LT, 0),
        OP(GT, 0),
        OP(LTE, 0),
        OP(GTE, 0),
        OP(NOTE, 0),
        OP(EQUALE, 0),
        OP(ASSIGN, 0),
        OP(PLUSA, 0),
        OP(MINUSA, 0),
        OP(MULA, 0),
        OP(DIVA, 0),
        OP(MODA, 0),
        OP(ANDA, 0),
        OP(ORA, 0),
        OP(XORA, 0),

        OP(LSHIFTA, 0),
        OP(RSHIFTA, 0),

        OP(F_ASSIGN, 0),
        OP(F_PLUSA, 0),
        OP(F_MINUSA, 0),
        OP(F_MULA, 0),
        OP(F_DIVA, 0),
        OP(DUP2, 0),
        OP(DUP_X2, 0),

        OP(CMP, 0), // TODO

        JMP(JUMP, 1, INSTR_PTR),
        JMP(IFZ, 1, INSTR_PTR),
        JMP(IFNZ, 1, INSTR_PTR),
        JMP(RETURN, 0),
        JMP(CALLFUNC, 1, INSTR_FUN),
        OP(INC, 0),
        OP(DEC, 0),
        OP(FTOI, 0),
        OP(ITOF, 0),
        OP(F_INV, 0),
        OP(F_ADD, 0),
        OP(F_SUB, 0),
        OP(F_MUL, 0),
        OP(F_DIV, 0),
        OP(F_LT, 0),
        OP(F_GT, 0),
        OP(F_LTE, 0),
        OP(F_GTE, 0),
        OP(F_NOTE, 0),
        OP(F_EQUALE, 0),
        OP(F_PUSH, 1, INSTR_FLO),
        OP(S_PUSH, 1, INSTR_STR),
        OP(S_POP, 0),
        OP(S_ADD, 0),
        OP(S_ASSIGN, 0),
        OP(S_PLUSA, 0), // TODO
        OP(S_REF, 0),
        OP(S_REFREF, 0), // TODO
        OP(S_NOTE, 0),
        OP(S_EQUALE, 0),
        OP(SF_CREATE, 0), // TODO
        OP(SF_CREATEPIXEL, 0), // TODO
        OP(SF_CREATEALPHA, 0), // TODO

        OP(SR_POP, 0),
        OP(SR_ASSIGN, 0),
        OP(SR_REF, 1, INSTR_INT),
        OP(SR_REFREF, 0), // TODO
        OP(A_ALLOC, 0),
        OP(A_REALLOC, 0),
        OP(A_FREE, 0),
        OP(A_NUMOF, 0),
        OP(A_COPY, 0),
        OP(A_FILL, 0),
        OP(C_REF, 0),
        OP(C_ASSIGN, 0),
        JMP(MSG, 1, INSTR_INT),
        OP(CALLHLL, 2, INSTR_INT, INSTR_INT),
        OP(PUSHSTRUCTPAGE, 0),
        JMP(CALLMETHOD, 1, INSTR_FUN),
        OP(SH_GLOBALREF, 1, INSTR_INT),
        OP(SH_LOCALREF, 1, INSTR_INT),
        JMP(SWITCH, 1),
        JMP(STRSWITCH, 1),
        OP(FUNC, 1, INSTR_FUN),
        OP(_EOF, 1, INSTR_FILE),
        OP(CALLSYS, 1, INSTR_INT),
        JMP(SJUMP, 0),
        OP(CALLONJUMP, 0),
        OP(SWAP, 0),
        OP(SH_STRUCTREF, 1, INSTR_INT),
        OP(S_LENGTH, 0),
        OP(S_LENGTHBYTE, 0),
        OP(I_STRING, 0),
        JMP(CALLFUNC2, 0),
        OP(DUP2_X1, 0),
        OP(R_ASSIGN, 0),
        OP(FT_ASSIGNS, 0), // TODO
        OP(ASSERT, 0),
        OP(S_LT, 0),
        OP(S_GT, 0),
        OP(S_LTE, 0),
        OP(S_GTE, 0),
        OP(S_LENGTH2, 0),
        OP(S_LENGTHBYTE2, 0), // TODO
        OP(NEW, 0), // TODO
        OP(DELETE, 0),
        OP(CHECKUDO, 0), // TODO
        OP(A_REF, 0),
        OP(DUP, 0),
        OP(DUP_U2, 0),
        OP(SP_INC, 0),
        OP(SP_DEC, 0), // TODO
        OP(ENDFUNC, 1, INSTR_FUN),
        OP(R_EQUALE, 0), // TODO
        OP(R_NOTE, 0), // TODO
        OP(SH_LOCALCREATE, 2),
        OP(SH_LOCALDELETE, 1),
        OP(STOI, 0), // TODO
        OP(A_PUSHBACK, 0),
        OP(A_POPBACK, 0),
        OP(S_EMPTY, 0),
        OP(A_EMPTY, 0),
        OP(A_ERASE, 0),
        OP(A_INSERT, 0),
        OP(SH_LOCALINC, 1, INSTR_INT),
        OP(SH_LOCALDEC, 1, INSTR_INT),
        OP(SH_LOCALASSIGN, 2, INSTR_INT, INSTR_INT),
        OP(ITOB, 0),
        OP(S_FIND, 0),
        OP(S_GETPART, 0),
        OP(A_SORT, 0),
        OP(S_PUSHBACK, 0), // TODO
        OP(S_POPBACK, 0), // TODO
        OP(FTOS, 0),
        OP(S_MOD, 0),
        OP(S_PLUSA2, 0),
        OP(OBJSWAP, 0),
        OP(S_ERASE, 0), // TODO
        OP(SR_REF2, 0), // TODO
        OP(S_ERASE2, 0),
        OP(S_PUSHBACK2, 0),
        OP(S_POPBACK2, 0),
        OP(ITOLI, 0),
        OP(LI_ADD, 0),
        OP(LI_SUB, 0),
        OP(LI_MUL, 0),
        OP(LI_DIV, 0),
        OP(LI_MOD, 0),
        OP(LI_ASSIGN, 0),
        OP(LI_PLUSA, 0),
        OP(LI_MINUSA, 0),
        OP(LI_MULA, 0),
        OP(LI_DIVA, 0),
        OP(LI_MODA, 0),
        OP(LI_ANDA, 0),
        OP(LI_ORA, 0),
        OP(LI_XORA, 0),
        OP(LI_LSHIFTA, 0),
        OP(LI_RSHIFTA, 0),
        OP(LI_INC, 0),
        OP(LI_DEC, 0),
        OP(A_FIND, 0),
        OP(A_REVERSE, 0),
        OP(SH_SR_ASSIGN, 0), // TODO
        OP(SH_MEM_ASSIGN_LOCAL, 0), // TODO
        OP(A_NUMOF_GLOB_1, 0), // TODO
        OP(A_NUMOF_STRUCT_1, 0), // TODO
        OP(SH_MEM_ASSIGN_IMM, 0), // TODO
        OP(SH_LOCALREFREF, 0), // TODO
        OP(SH_LOCALASSIGN_SUB_IMM, 0), // TODO
        OP(SH_IF_LOC_LT_IMM, 0), // TODO
        OP(SH_IF_LOC_GE_IMM, 0), // TODO
        OP(SH_LOCREF_ASSIGN_MEM, 0), // TODO
        OP(PAGE_REF, 0), // TODO
        OP(SH_GLOBAL_ASSIGN_LOCAL, 0), // TODO
        OP(SH_STRUCTREF_GT_IMM, 0), // TODO
        OP(SH_STRUCT_ASSIGN_LOCALREF_ITOB, 0), // TODO
        OP(SH_LOCAL_ASSIGN_STRUCTREF, 0), // TODO
        OP(SH_IF_STRUCTREF_NE_LOCALREF, 0), // TODO
        OP(SH_IF_STRUCTREF_GT_IMM, 0), // TODO
        OP(SH_STRUCTREF_CALLMETHOD_NO_PARAM, 0), // TODO
        OP(SH_STRUCTREF2, 0), // TODO
        OP(SH_REF_STRUCTREF2, 0), // TODO
        OP(SH_STRUCTREF3, 0), // TODO
        OP(SH_STRUCTREF2_CALLMETHOD_NO_PARAM, 0), // TODO
        OP(SH_IF_STRUCTREF_Z, 0), // TODO
        OP(SH_IF_STRUCT_A_NOT_EMPTY, 0), // TODO
        OP(SH_IF_LOC_GT_IMM, 0), // TODO
        OP(SH_IF_STRUCTREF_NE_IMM, 0), // TODO
        OP(THISCALLMETHOD_NOPARAM, 0), // TODO
        OP(SH_IF_LOC_NE_IMM, 0), // TODO
        OP(SH_IF_STRUCTREF_EQ_IMM, 0), // TODO
        OP(SH_GLOBAL_ASSIGN_IMM, 0), // TODO
        OP(SH_LOCALSTRUCT_ASSIGN_IMM, 0), // TODO
        OP(SH_STRUCT_A_PUSHBACK_LOCAL_STRUCT, 0), // TODO
        OP(SH_GLOBAL_A_PUSHBACK_LOCAL_STRUCT, 0), // TODO
        OP(SH_LOCAL_A_PUSHBACK_LOCAL_STRUCT, 0), // TODO
        OP(SH_IF_SREF_NE_STR0, 0), // TODO
        OP(SH_S_ASSIGN_REF, 0), // TODO
        OP(SH_A_FIND_SREF, 0), // TODO
        OP(SH_SREF_EMPTY, 0), // TODO
        OP(SH_STRUCTSREF_EQ_LOCALSREF, 0), // TODO
        OP(SH_LOCALSREF_EQ_STR0, 0), // TODO
        OP(SH_STRUCTSREF_NE_LOCALSREF, 0), // TODO
        OP(SH_LOCALSREF_NE_STR0, 0), // TODO
        OP(SH_STRUCT_SR_REF, 0), // TODO
        OP(SH_STRUCT_S_REF, 0), // TODO
        OP(S_REF2, 0), // TODO
        OP(SH_REF_LOCAL_ASSIGN_STRUCTREF2, 0), // TODO
        OP(SH_GLOBAL_S_REF, 0), // TODO
        OP(SH_LOCAL_S_REF, 0), // TODO
        OP(SH_LOCALREF_SASSIGN_LOCALSREF, 0), // TODO
        OP(SH_LOCAL_APUSHBACK_LOCALSREF, 0), // TODO
        OP(SH_S_ASSIGN_CALLSYS19, 0), // TODO
        OP(SH_S_ASSIGN_STR0, 0), // TODO
        OP(SH_SASSIGN_LOCALSREF, 0), // TODO
        OP(SH_STRUCTREF_SASSIGN_LOCALSREF, 0), // TODO
        OP(SH_LOCALSREF_EMPTY, 0), // TODO
        OP(SH_GLOBAL_APUSHBACK_LOCALSREF, 0), // TODO
        OP(SH_STRUCT_APUSHBACK_LOCALSREF, 0), // TODO
        OP(SH_STRUCTSREF_EMPTY, 0), // TODO
        OP(SH_GLOBALSREF_EMPTY, 0), // TODO
        OP(SH_SASSIGN_STRUCTSREF, 0), // TODO
        OP(SH_SASSIGN_GLOBALSREF, 0), // TODO
        OP(SH_STRUCTSREF_NE_STR0, 0), // TODO
        OP(SH_GLOBALSREF_NE_STR0, 0), // TODO
        OP(SH_LOC_LT_IMM_OR_LOC_GE_IMM, 0), // TODO
        OP(A_SORT_MEM, 0), // TODO
        OP(DG_ADD, 0), // TODO
        OP(DG_SET, 0), // TODO
        OP(DG_CALL, 0), // TODO
        OP(DG_NUMOF, 0), // TODO
        OP(DG_EXIST, 0), // TODO
        OP(DG_ERASE, 0), // TODO
        OP(DG_CLEAR, 0), // TODO
        OP(DG_COPY, 0), // TODO
        OP(DG_ASSIGN, 0), // TODO
        OP(DG_PLUSA, 0), // TODO
        OP(DG_POP, 0), // TODO
        OP(DG_NEW_FROM_METHOD, 0), // TODO
        OP(DG_MINUSA, 0), // TODO
        OP(DG_CALLBEGIN, 0), // TODO
        OP(DG_NEW, 0), // TODO
        OP(DG_STR_TO_METHOD, 0), // TODO
};
