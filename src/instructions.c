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

#define SYS(syscode, sysname)			\
	[syscode] = {				\
		.code = syscode,		\
		.name = "system." #sysname ,	\
		.implemented = true		\
	}

#define TODO(syscode, sysname)			\
	[syscode] = {				\
		.code = syscode,		\
		.name = "system." #sysname ,	\
		.implemented = false		\
	}

const struct syscall syscalls[NR_SYSCALLS] = {
	SYS  ( SYS_EXIT,                 Exit ),
	SYS  ( SYS_GLOBAL_SAVE,          GlobalSave ),
	SYS  ( SYS_GLOBAL_LOAD,          GlobalLoad ),
	SYS  ( SYS_LOCK_PEEK,            LockPeek ),
	SYS  ( SYS_UNLOCK_PEEK,          UnlockPeek ),
	TODO ( SYS_RESET,                Reset ),
	SYS  ( SYS_OUTPUT,               Output ),
	SYS  ( SYS_MSGBOX,               MsgBox ),
	SYS  ( SYS_RESUME_SAVE,          ResumeSave ),
	SYS  ( SYS_RESUME_LOAD,          ResumeLoad ),
	SYS  ( SYS_EXISTS_FILE,          ExistsFile ),
	TODO ( SYS_OPEN_WEB,             OpenWeb ),
	SYS  ( SYS_GET_SAVE_FOLDER_NAME, GetSaveFolderName ),
	SYS  ( SYS_GET_TIME,             GetTime ),
	TODO ( SYS_GET_GAME_NAME,        GetGameName ),
	SYS  ( SYS_ERROR,                Error ),
	SYS  ( SYS_EXISTS_SAVE_FILE,     ExistsSaveFile ),
	SYS  ( SYS_IS_DEBUG_MODE,        IsDebugMode ),
	TODO ( SYS_MSGBOX_OK_CANCEL,     MsgBoxOkCancel ),
	SYS  ( SYS_GET_FUNC_STACK_NAME,  GetFuncStackName ),
	SYS  ( SYS_PEEK,                 Peek ),
	SYS  ( SYS_SLEEP,                Sleep ),
	SYS  ( SYS_GROUP_SAVE,           GroupSave ),
	SYS  ( SYS_GROUP_LOAD,           GroupLoad ),
	TODO ( SYS_RESUME_WRITE_COMMENT, ResumeWriteComment ),
	TODO ( SYS_RESUME_READ_COMMENT,  ResumeReadComment ),
	TODO ( SYS_DELETE_SAVE_FILE,     DeleteSaveFile ),
	TODO ( SYS_EXIST_FUNC,           ExistFunc ),
	TODO ( SYS_COPY_SAVE_FILE,       CopySaveFile ),
};

// JMP = instruction that modifies the instruction pointer
// OP  = everything else
#define OP(code, nargs, ...)			\
	[code] = {				\
		.opcode = code,			\
		.name = #code ,			\
		.nr_args = nargs,		\
		.ip_inc = 2 + nargs * 4,	\
		.implemented = true,		\
		.args = { __VA_ARGS__ }		\
	}
#define JMP(code, nargs, ...)			\
	[code] = {				\
		.opcode = code,			\
		.name = #code ,			\
		.nr_args = nargs,		\
		.ip_inc = 0,			\
		.implemented = true,		\
		.args = { __VA_ARGS__ }		\
	}

#undef TODO
#define TODO(code, nargs, ...)			\
	[code] = {				\
		.opcode = code,			\
		.name = #code,			\
		.nr_args = nargs,		\
		.ip_inc = 2 + nargs * 4,	\
		.implemented = false,		\
		.args = { __VA_ARGS__ }		\
	}

struct instruction instructions[NR_OPCODES] = {
//      TYPE   OPCODE          NR_ARGS, ARG_TYPES...
	OP   ( PUSH,           1, T_INT ),
        OP   ( POP,            0 ),
        OP   ( REF,            0 ),
        OP   ( REFREF,         0 ),
        OP   ( PUSHGLOBALPAGE, 0 ),
        OP   ( PUSHLOCALPAGE,  0 ),
        OP   ( INV,            0 ),
        OP   ( NOT,            0 ),
        OP   ( COMPL,          0 ),
        OP   ( ADD,            0 ),
        OP   ( SUB,            0 ),
        OP   ( MUL,            0 ),
        OP   ( DIV,            0 ),
        OP   ( MOD,            0 ),
        OP   ( AND,            0 ),
        OP   ( OR,             0 ),
        OP   ( XOR,            0 ),
        OP   ( LSHIFT,         0 ),
        OP   ( RSHIFT,         0 ),
        OP   ( LT,             0 ),
        OP   ( GT,             0 ),
        OP   ( LTE,            0 ),
        OP   ( GTE,            0 ),
        OP   ( NOTE,           0 ),
        OP   ( EQUALE,         0 ),
        OP   ( ASSIGN,         0 ),
        OP   ( PLUSA,          0 ),
        OP   ( MINUSA,         0 ),
        OP   ( MULA,           0 ),
        OP   ( DIVA,           0 ),
        OP   ( MODA,           0 ),
        OP   ( ANDA,           0 ),
        OP   ( ORA,            0 ),
        OP   ( XORA,           0 ),
        OP   ( LSHIFTA,        0 ),
        OP   ( RSHIFTA,        0 ),
        OP   ( F_ASSIGN,       0 ),
        OP   ( F_PLUSA,        0 ),
        OP   ( F_MINUSA,       0 ),
        OP   ( F_MULA,         0 ),
        OP   ( F_DIVA,         0 ),
        OP   ( DUP2,           0 ),
        OP   ( DUP_X2,         0 ),
        TODO ( CMP,            0 ),
        JMP  ( JUMP,           1, T_ADDR ),
        JMP  ( IFZ,            1, T_ADDR ),
        JMP  ( IFNZ,           1, T_ADDR ),
        JMP  ( RETURN,         0 ),
        JMP  ( CALLFUNC,       1, T_FUNC ),
        OP   ( INC,            0 ),
        OP   ( DEC,            0 ),
        OP   ( FTOI,           0 ),
        OP   ( ITOF,           0 ),
        OP   ( F_INV,          0 ),
        OP   ( F_ADD,          0 ),
        OP   ( F_SUB,          0 ),
        OP   ( F_MUL,          0 ),
        OP   ( F_DIV,          0 ),
        OP   ( F_LT,           0 ),
        OP   ( F_GT,           0 ),
        OP   ( F_LTE,          0 ),
        OP   ( F_GTE,          0 ),
        OP   ( F_NOTE,         0 ),
        OP   ( F_EQUALE,       0 ),
        OP   ( F_PUSH,         1, T_FLOAT ),
        OP   ( S_PUSH,         1, T_STRING ),
        OP   ( S_POP,          0 ),
        OP   ( S_ADD,          0 ),
        OP   ( S_ASSIGN,       0 ),
        TODO ( S_PLUSA,        0 ),
        OP   ( S_REF,          0 ),
        TODO ( S_REFREF,       0 ),
        OP   ( S_NOTE,         0 ),
        OP   ( S_EQUALE,       0 ),
        TODO ( SF_CREATE,      0 ),
        TODO ( SF_CREATEPIXEL, 0 ),
        TODO ( SF_CREATEALPHA, 0 ),

        OP   ( SR_POP,         0 ),
        OP   ( SR_ASSIGN,      0 ),
        OP   ( SR_REF,         1, T_STRUCT ),
        TODO ( SR_REFREF,      0 ),
        OP   ( A_ALLOC,        0 ),
        OP   ( A_REALLOC,      0 ),
        OP   ( A_FREE,         0 ),
        OP   ( A_NUMOF,        0 ),
        OP   ( A_COPY,         0 ),
        OP   ( A_FILL,         0 ),
        OP   ( C_REF,          0 ),
        OP   ( C_ASSIGN,       0 ),
        JMP  ( MSG,            1, T_MSG ),
        OP   ( CALLHLL,        2, T_HLL, T_HLLFUNC, T_INT ), // XXX: changed in ain version > 8
        OP   ( PUSHSTRUCTPAGE, 0 ),
        JMP  ( CALLMETHOD,     1, T_FUNC ),
        OP   ( SH_GLOBALREF,   1, T_GLOBAL ),
        OP   ( SH_LOCALREF,    1, T_LOCAL ),
        JMP  ( SWITCH,         1, T_INT ),
        JMP  ( STRSWITCH,      1, T_INT ),
        OP   ( FUNC,           1, T_FUNC ),
        OP   ( _EOF,           1, T_FILE ),
        OP   ( CALLSYS,        1, T_SYSCALL ),
        JMP  ( SJUMP,          0 ),
        OP   ( CALLONJUMP,     0 ),
        OP   ( SWAP,           0 ),
        OP   ( SH_STRUCTREF,   1, T_INT ),
        OP   ( S_LENGTH,       0 ),
        OP   ( S_LENGTHBYTE,   0 ),
        OP   ( I_STRING,       0 ),
        JMP  ( CALLFUNC2,      0 ),
        OP   ( DUP2_X1,        0 ),
        OP   ( R_ASSIGN,       0 ),
        OP   ( FT_ASSIGNS,     0 ),
        OP   ( ASSERT,         0 ),
        OP   ( S_LT,           0 ),
        OP   ( S_GT,           0 ),
        OP   ( S_LTE,          0 ),
        OP   ( S_GTE,          0 ),
        OP   ( S_LENGTH2,      0 ),
        TODO ( S_LENGTHBYTE2,  0 ),
        TODO ( NEW,            2, T_INT, T_INT ),
        OP   ( DELETE,         0 ),
        TODO ( CHECKUDO,       0 ),
        OP   ( A_REF,          0 ),
        OP   ( DUP,            0 ),
        OP   ( DUP_U2,         0 ),
        OP   ( SP_INC,         0 ),
        TODO ( SP_DEC,         0 ),
        OP   ( ENDFUNC,        1, T_FUNC ),
        TODO ( R_EQUALE,       0 ),
        TODO ( R_NOTE,         0 ),
        OP   ( SH_LOCALCREATE, 2, T_LOCAL, T_STRUCT ),
        OP   ( SH_LOCALDELETE, 1, T_LOCAL ),
        TODO ( STOI,           0 ),
        OP   ( A_PUSHBACK,     0 ),
        OP   ( A_POPBACK,      0 ),
        OP   ( S_EMPTY,        0 ),
        OP   ( A_EMPTY,        0 ),
        OP   ( A_ERASE,        0 ),
        OP   ( A_INSERT,       0 ),
        OP   ( SH_LOCALINC,    1, T_LOCAL ),
        OP   ( SH_LOCALDEC,    1, T_LOCAL ),
        OP   ( SH_LOCALASSIGN, 2, T_LOCAL, T_INT ),
        OP   ( ITOB,           0 ),
        OP   ( S_FIND,         0 ),
        OP   ( S_GETPART,      0 ),
        OP   ( A_SORT,         0 ),
        TODO ( S_PUSHBACK,     0 ),
        TODO ( S_POPBACK,      0 ),
        OP   ( FTOS,           0 ),
        OP   ( S_MOD,          0, T_INT ), // XXX: changed in ain version > 8
        OP   ( S_PLUSA2,       0 ),
        OP   ( OBJSWAP,        0, T_INT ), // XXX: changed in ain version > 8
        TODO ( S_ERASE,        0 ),
        TODO ( SR_REF2,        1, T_INT ),
        OP   ( S_ERASE2,       0 ),
        OP   ( S_PUSHBACK2,    0 ),
        OP   ( S_POPBACK2,     0 ),
        OP   ( ITOLI,          0 ),
        OP   ( LI_ADD,         0 ),
        OP   ( LI_SUB,         0 ),
        OP   ( LI_MUL,         0 ),
        OP   ( LI_DIV,         0 ),
        OP   ( LI_MOD,         0 ),
        OP   ( LI_ASSIGN,      0 ),
        OP   ( LI_PLUSA,       0 ),
        OP   ( LI_MINUSA,      0 ),
        OP   ( LI_MULA,        0 ),
        OP   ( LI_DIVA,        0 ),
        OP   ( LI_MODA,        0 ),
        OP   ( LI_ANDA,        0 ),
        OP   ( LI_ORA,         0 ),
        OP   ( LI_XORA,        0 ),
        OP   ( LI_LSHIFTA,     0 ),
        OP   ( LI_RSHIFTA,     0 ),
        OP   ( LI_INC,         0 ),
        OP   ( LI_DEC,         0 ),
        OP   ( A_FIND,         0 ),
        OP   ( A_REVERSE,      0 ),

	// FIXME: all types are guesses
        TODO ( SH_SR_ASSIGN, 0 ),
        TODO ( SH_MEM_ASSIGN_LOCAL, 2, T_MEMB, T_LOCAL ),
        TODO ( A_NUMOF_GLOB_1, 1, T_GLOBAL ),
        TODO ( A_NUMOF_STRUCT_1, 1, T_MEMB ),
        TODO ( SH_MEM_ASSIGN_IMM, 2, T_MEMB, T_INT ),
        TODO ( SH_LOCALREFREF, 1, T_LOCAL ),
        TODO ( SH_LOCALASSIGN_SUB_IMM, 2, T_LOCAL, T_INT ),
        TODO ( SH_IF_LOC_LT_IMM, 3, T_LOCAL, T_INT, T_ADDR ),
        TODO ( SH_IF_LOC_GE_IMM, 3, T_LOCAL, T_INT, T_ADDR ),
        TODO ( SH_LOCREF_ASSIGN_MEM, 2, T_LOCAL, T_MEMB ),
        TODO ( PAGE_REF, 1, T_INT ),
        TODO ( SH_GLOBAL_ASSIGN_LOCAL, 2, T_GLOBAL, T_LOCAL ),
        TODO ( SH_STRUCTREF_GT_IMM, 2, T_MEMB, T_INT ),
        TODO ( SH_STRUCT_ASSIGN_LOCALREF_ITOB, 2, T_MEMB, T_LOCAL ),
        TODO ( SH_LOCAL_ASSIGN_STRUCTREF, 2, T_LOCAL, T_MEMB ),
        TODO ( SH_IF_STRUCTREF_NE_LOCALREF, 3, T_MEMB, T_LOCAL, T_ADDR ),
        TODO ( SH_IF_STRUCTREF_GT_IMM, 3, T_MEMB, T_INT, T_ADDR ),
        TODO ( SH_STRUCTREF_CALLMETHOD_NO_PARAM, 2, T_MEMB, T_FUNC ),
        TODO ( SH_STRUCTREF2, 2, T_MEMB, T_MEMB2 ),
        TODO ( SH_REF_STRUCTREF2, 2, T_MEMB, T_MEMB2 ),
        TODO ( SH_STRUCTREF3, 3, T_MEMB, T_MEMB2, T_MEMB3 ),
        TODO ( SH_STRUCTREF2_CALLMETHOD_NO_PARAM, 3, T_MEMB, T_MEMB2, T_FUNC ),
        TODO ( SH_IF_STRUCTREF_Z, 2, T_MEMB, T_ADDR ),
        TODO ( SH_IF_STRUCT_A_NOT_EMPTY, 2, T_MEMB, T_ADDR ),
        TODO ( SH_IF_LOC_GT_IMM, 3, T_LOCAL, T_INT, T_ADDR ),
        TODO ( SH_IF_STRUCTREF_NE_IMM, 3, T_MEMB, T_INT, T_ADDR ),
        TODO ( THISCALLMETHOD_NOPARAM, 1, T_FUNC ),
        TODO ( SH_IF_LOC_NE_IMM, 3, T_LOCAL, T_INT, T_ADDR ),
        TODO ( SH_IF_STRUCTREF_EQ_IMM, 3, T_MEMB, T_INT, T_ADDR ),
        TODO ( SH_GLOBAL_ASSIGN_IMM, 2, T_GLOBAL, T_INT ),
        TODO ( SH_LOCALSTRUCT_ASSIGN_IMM, 3, T_LOCAL, T_LOCMEMB, T_INT ),
        TODO ( SH_STRUCT_A_PUSHBACK_LOCAL_STRUCT, 2, T_MEMB, T_LOCAL ),
        TODO ( SH_GLOBAL_A_PUSHBACK_LOCAL_STRUCT, 2, T_GLOBAL, T_LOCAL ),
        TODO ( SH_LOCAL_A_PUSHBACK_LOCAL_STRUCT, 2, T_LOCAL, T_LOCAL ),
        TODO ( SH_IF_SREF_NE_STR0, 2, T_STRING, T_ADDR ),
        TODO ( SH_S_ASSIGN_REF, 0 ),
        TODO ( SH_A_FIND_SREF, 0 ),
        TODO ( SH_SREF_EMPTY, 0 ),
        TODO ( SH_STRUCTSREF_EQ_LOCALSREF, 2 , T_MEMB, T_LOCAL ),
        TODO ( SH_LOCALSREF_EQ_STR0, 2, T_LOCAL, T_STRING ),
        TODO ( SH_STRUCTSREF_NE_LOCALSREF, 2, T_MEMB, T_LOCAL ),
        TODO ( SH_LOCALSREF_NE_STR0, 2, T_LOCAL, T_STRING ),
        TODO ( SH_STRUCT_SR_REF, 2, T_MEMB, T_STRUCT ),
        TODO ( SH_STRUCT_S_REF, 1, T_MEMB ),
        TODO ( S_REF2, 1, T_MEMB ),
        TODO ( SH_REF_LOCAL_ASSIGN_STRUCTREF2, 3, T_INT, T_INT, T_INT ), // FIXME: arguments types unknown
        TODO ( SH_GLOBAL_S_REF, 1, T_GLOBAL ),
        TODO ( SH_LOCAL_S_REF, 1, T_LOCAL ),
        TODO ( SH_LOCALREF_SASSIGN_LOCALSREF, 2, T_LOCAL, T_LOCAL ),
        TODO ( SH_LOCAL_APUSHBACK_LOCALSREF, 2, T_LOCAL, T_LOCAL ),
        TODO ( SH_S_ASSIGN_CALLSYS19, 0 ),
        TODO ( SH_S_ASSIGN_STR0, 1, T_STRING ),
        TODO ( SH_SASSIGN_LOCALSREF, 1, T_LOCAL ),
        TODO ( SH_STRUCTREF_SASSIGN_LOCALSREF, 2, T_MEMB, T_LOCAL ),
        TODO ( SH_LOCALSREF_EMPTY, 1, T_LOCAL ),
        TODO ( SH_GLOBAL_APUSHBACK_LOCALSREF, 2, T_GLOBAL, T_LOCAL ),
        TODO ( SH_STRUCT_APUSHBACK_LOCALSREF, 2, T_MEMB, T_LOCAL ),
        TODO ( SH_STRUCTSREF_EMPTY, 1, T_MEMB ),
        TODO ( SH_GLOBALSREF_EMPTY, 1, T_GLOBAL ),
        TODO ( SH_SASSIGN_STRUCTSREF, 1, T_MEMB ),
        TODO ( SH_SASSIGN_GLOBALSREF, 1, T_GLOBAL ),
        TODO ( SH_STRUCTSREF_NE_STR0, 2, T_MEMB, T_STRING ),
        TODO ( SH_GLOBALSREF_NE_STR0, 2, T_GLOBAL, T_STRING ),
        TODO ( SH_LOC_LT_IMM_OR_LOC_GE_IMM, 3, T_LOCAL, T_INT, T_INT ),

        TODO ( A_SORT_MEM, 0 ),
        TODO ( DG_ADD, 0 ),
        TODO ( DG_SET, 0 ),
        TODO ( DG_CALL, 2, T_DLG, T_ADDR ),
        TODO ( DG_NUMOF, 0 ),
        TODO ( DG_EXIST, 0 ),
        TODO ( DG_ERASE, 0 ),
        TODO ( DG_CLEAR, 0 ),
        TODO ( DG_COPY, 0 ),
        TODO ( DG_ASSIGN, 0 ),
        TODO ( DG_PLUSA, 0 ),
        TODO ( DG_POP, 0 ),
        TODO ( DG_NEW_FROM_METHOD, 0 ),
        TODO ( DG_MINUSA, 0 ),
        TODO ( DG_CALLBEGIN, 1, T_DLG ),
        TODO ( DG_NEW, 0 ),
        TODO ( DG_STR_TO_METHOD, 0, T_DLG ), // XXX: changed in ain version > 8

	TODO ( OP_0x102, 0 ),
	TODO ( OP_0x103, 2, T_INT, T_INT ),
	TODO ( OP_0x104, 0 ),
	TODO ( OP_0x105, 1, T_INT, T_INT ),
};
