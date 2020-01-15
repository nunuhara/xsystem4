/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * Credit to SLC for reverse engineering the opcodes.
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

#ifndef SYSTEM4_INSTRUCTIONS_H
#define SYSTEM4_INSTRUCTIONS_H

#include <stdbool.h>
#include <stdint.h>
#include "system4.h"

#define INSTRUCTION_MAX_ARGS 4 // FIXME: determine the real maximum

enum syscall_code {
	SYS_EXIT                 = 0x00,
	SYS_GLOBAL_SAVE          = 0x01,
	SYS_GLOBAL_LOAD          = 0x02,
	SYS_LOCK_PEEK            = 0x03,
	SYS_UNLOCK_PEEK          = 0x04,
	SYS_RESET                = 0x05,
	SYS_OUTPUT               = 0x06,
	SYS_MSGBOX               = 0x07,
	SYS_RESUME_SAVE          = 0x08,
	SYS_RESUME_LOAD          = 0x09,
	SYS_EXISTS_FILE          = 0x0A,
	SYS_OPEN_WEB             = 0x0B,
	SYS_GET_SAVE_FOLDER_NAME = 0x0C,
	SYS_GET_TIME             = 0x0D,
	SYS_GET_GAME_NAME        = 0x0E,
	SYS_ERROR                = 0x0F,
	SYS_EXISTS_SAVE_FILE     = 0x10,
	SYS_IS_DEBUG_MODE        = 0x11,
	SYS_MSGBOX_OK_CANCEL     = 0x12,
	SYS_GET_FUNC_STACK_NAME  = 0x13,
	SYS_PEEK                 = 0x14,
	SYS_SLEEP                = 0x15,
	SYS_RESUME_WRITE_COMMENT = 0x16,
	SYS_RESUME_READ_COMMENT  = 0x17,
	SYS_GROUP_SAVE           = 0x18,
	SYS_GROUP_LOAD           = 0x19,
	SYS_DELETE_SAVE_FILE     = 0x1A,
	SYS_EXIST_FUNC           = 0x1B,
	SYS_COPY_SAVE_FILE       = 0x1C,
	NR_SYSCALLS
};

struct syscall {
	const enum syscall_code code;
	const char * const name;
	const bool implemented;
};

const struct syscall syscalls[NR_SYSCALLS];

enum opcode
{
        PUSH           = 0x00,
        POP            = 0x01,
        REF            = 0x02,
        REFREF         = 0x03,
        PUSHGLOBALPAGE = 0x04,
        PUSHLOCALPAGE  = 0x05,
        INV            = 0x06,
        NOT            = 0x07,
        COMPL          = 0x08,
        ADD            = 0x09,
        SUB            = 0x0a,
        MUL            = 0x0b,
        DIV            = 0x0c,
        MOD            = 0x0d,
        AND            = 0x0e,
        OR             = 0x0f,
        XOR            = 0x10,
        LSHIFT         = 0x11,
        RSHIFT         = 0x12,
        LT             = 0x13,
        GT             = 0x14,
        LTE            = 0x15,
        GTE            = 0x16,
        NOTE           = 0x17,
        EQUALE         = 0x18,
        ASSIGN         = 0x19,
        PLUSA          = 0x1a,
        MINUSA         = 0x1b,
        MULA           = 0x1c,
        DIVA           = 0x1d,
        MODA           = 0x1e,
        ANDA           = 0x1f,
        ORA            = 0x20,
        XORA           = 0x21,
        LSHIFTA        = 0x22,
        RSHIFTA        = 0x23,
        F_ASSIGN       = 0x24,
        F_PLUSA        = 0x25,
        F_MINUSA       = 0x26,
        F_MULA         = 0x27,
        F_DIVA         = 0x28,
        DUP2           = 0x29,
        DUP_X2         = 0x2a,
        CMP            = 0x2b,
        JUMP           = 0x2c,
        IFZ            = 0x2d,
        IFNZ           = 0x2e,
        RETURN         = 0x2f,
        CALLFUNC       = 0x30,
        INC            = 0x31,
        DEC            = 0x32,
        FTOI           = 0x33,
        ITOF           = 0x34,
        F_INV          = 0x35,
        F_ADD          = 0x36,
        F_SUB          = 0x37,
        F_MUL          = 0x38,
        F_DIV          = 0x39,
        F_LT           = 0x3a,
        F_GT           = 0x3b,
        F_LTE          = 0x3c,
        F_GTE          = 0x3D,
        F_NOTE         = 0x3E,
        F_EQUALE       = 0x3f,
        F_PUSH         = 0x40,
        S_PUSH         = 0x41,
        S_POP          = 0x42,
        S_ADD          = 0x43,
        S_ASSIGN       = 0x44,
        S_PLUSA        = 0x45,
        S_REF          = 0x46,
        S_REFREF       = 0x47,
        S_NOTE         = 0x48,
        S_EQUALE       = 0x49,
        SF_CREATE      = 0x4A,
        SF_CREATEPIXEL = 0x4B,
        SF_CREATEALPHA = 0x4C,
        SR_POP         = 0x4d,
        SR_ASSIGN      = 0x4e,
        SR_REF         = 0x4f,
        SR_REFREF      = 0x50,
        A_ALLOC        = 0x51,
        A_REALLOC      = 0x52,
        A_FREE         = 0x53,
        A_NUMOF        = 0x54,
        A_COPY         = 0x55,
        A_FILL         = 0x56,
        C_REF          = 0x57,
        C_ASSIGN       = 0x58,
        MSG            = 0x59,
        CALLHLL        = 0x5a,
        PUSHSTRUCTPAGE = 0x5b,
        CALLMETHOD     = 0x5c,
        SH_GLOBALREF   = 0x5d,
        SH_LOCALREF    = 0x5e,
        SWITCH         = 0x5f,
        STRSWITCH      = 0x60,
        FUNC           = 0x61,
        _EOF           = 0x62,
        CALLSYS        = 0x63,
        SJUMP          = 0x64,
        CALLONJUMP     = 0x65,
        SWAP           = 0x66,
        SH_STRUCTREF   = 0x67,
        S_LENGTH       = 0x68,
        S_LENGTHBYTE   = 0x69,
        I_STRING       = 0x6a,
        CALLFUNC2      = 0x6b,
        DUP2_X1        = 0x6c,
        R_ASSIGN       = 0x6d,
        FT_ASSIGNS     = 0x6e,
        ASSERT         = 0x6f,
        S_LT           = 0x70,
        S_GT           = 0x71,
        S_LTE          = 0x72,
        S_GTE          = 0x73,
        S_LENGTH2      = 0x74,
        S_LENGTHBYTE2  = 0x75,
        NEW            = 0x76,
        DELETE         = 0x77,
        CHECKUDO       = 0x78,
        A_REF          = 0x79,
        DUP            = 0x7a,
        DUP_U2         = 0x7b,
        SP_INC         = 0x7c,
        SP_DEC         = 0x7d,
        ENDFUNC        = 0x7e,
        R_EQUALE       = 0x7f,
        R_NOTE         = 0x80,
        SH_LOCALCREATE = 0x81,
        SH_LOCALDELETE = 0x82,
        STOI           = 0x83,
        A_PUSHBACK     = 0x84,
        A_POPBACK      = 0x85,
        S_EMPTY        = 0x86,
        A_EMPTY        = 0x87,
        A_ERASE        = 0x88,
        A_INSERT       = 0x89,
        SH_LOCALINC    = 0x8a,
        SH_LOCALDEC    = 0x8b,
        SH_LOCALASSIGN = 0x8c,
        ITOB           = 0x8d,
        S_FIND         = 0x8e,
        S_GETPART      = 0x8f,
        A_SORT         = 0x90,
        S_PUSHBACK     = 0x91,
        S_POPBACK      = 0x92,
        FTOS           = 0x93,
        S_MOD          = 0x94,
        S_PLUSA2       = 0x95,
        OBJSWAP        = 0x96,
        S_ERASE        = 0x97,
        SR_REF2        = 0x98,
        S_ERASE2       = 0x99,
        S_PUSHBACK2    = 0x9A,
        S_POPBACK2     = 0x9B,
        ITOLI          = 0x9c,
        LI_ADD         = 0x9d,
        LI_SUB         = 0x9e,
        LI_MUL         = 0x9f,
        LI_DIV         = 0xa0,
        LI_MOD         = 0xa1,
        LI_ASSIGN      = 0xa2,
        LI_PLUSA       = 0xa3,
        LI_MINUSA      = 0xa4,
        LI_MULA        = 0xa5,
        LI_DIVA        = 0xa6,
        LI_MODA        = 0xa7,
        LI_ANDA        = 0xa8,
        LI_ORA         = 0xa9,
        LI_XORA        = 0xaa,
        LI_LSHIFTA     = 0xab,
        LI_RSHIFTA     = 0xac,
        LI_INC         = 0xad,
        LI_DEC         = 0xae,
        A_FIND         = 0xaf,
        A_REVERSE      = 0xb0,
        SH_SR_ASSIGN,
        SH_MEM_ASSIGN_LOCAL,
        A_NUMOF_GLOB_1,
        A_NUMOF_STRUCT_1,
        SH_MEM_ASSIGN_IMM,
        SH_LOCALREFREF,
        SH_LOCALASSIGN_SUB_IMM,
        SH_IF_LOC_LT_IMM,
        SH_IF_LOC_GE_IMM,
        SH_LOCREF_ASSIGN_MEM,
        PAGE_REF,
        SH_GLOBAL_ASSIGN_LOCAL,
        SH_STRUCTREF_GT_IMM,
        SH_STRUCT_ASSIGN_LOCALREF_ITOB,
        SH_LOCAL_ASSIGN_STRUCTREF,
        SH_IF_STRUCTREF_NE_LOCALREF,
        SH_IF_STRUCTREF_GT_IMM,
        SH_STRUCTREF_CALLMETHOD_NO_PARAM,
        SH_STRUCTREF2,
        SH_REF_STRUCTREF2,
        SH_STRUCTREF3,
        SH_STRUCTREF2_CALLMETHOD_NO_PARAM,
        SH_IF_STRUCTREF_Z,
        SH_IF_STRUCT_A_NOT_EMPTY,
        SH_IF_LOC_GT_IMM,
        SH_IF_STRUCTREF_NE_IMM,
        THISCALLMETHOD_NOPARAM,
        SH_IF_LOC_NE_IMM,
        SH_IF_STRUCTREF_EQ_IMM,
        SH_GLOBAL_ASSIGN_IMM,
        SH_LOCALSTRUCT_ASSIGN_IMM,
        SH_STRUCT_A_PUSHBACK_LOCAL_STRUCT,
        SH_GLOBAL_A_PUSHBACK_LOCAL_STRUCT,
        SH_LOCAL_A_PUSHBACK_LOCAL_STRUCT,
        SH_IF_SREF_NE_STR0,
        SH_S_ASSIGN_REF,
        SH_A_FIND_SREF,
        SH_SREF_EMPTY,
        SH_STRUCTSREF_EQ_LOCALSREF,
        SH_LOCALSREF_EQ_STR0,
        SH_STRUCTSREF_NE_LOCALSREF,
        SH_LOCALSREF_NE_STR0,
        SH_STRUCT_SR_REF,
        SH_STRUCT_S_REF,
        S_REF2,
        SH_REF_LOCAL_ASSIGN_STRUCTREF2,
        SH_GLOBAL_S_REF,
        SH_LOCAL_S_REF,
        SH_LOCALREF_SASSIGN_LOCALSREF,
        SH_LOCAL_APUSHBACK_LOCALSREF,
        SH_S_ASSIGN_CALLSYS19,
        SH_S_ASSIGN_STR0,
        SH_SASSIGN_LOCALSREF,
        SH_STRUCTREF_SASSIGN_LOCALSREF,
        SH_LOCALSREF_EMPTY,
        SH_GLOBAL_APUSHBACK_LOCALSREF,
        SH_STRUCT_APUSHBACK_LOCALSREF,
        SH_STRUCTSREF_EMPTY,
        SH_GLOBALSREF_EMPTY,
        SH_SASSIGN_STRUCTSREF,
        SH_SASSIGN_GLOBALSREF,
        SH_STRUCTSREF_NE_STR0,
        SH_GLOBALSREF_NE_STR0,
        SH_LOC_LT_IMM_OR_LOC_GE_IMM,
        // instruction names are known, exact order of them are guesses after this point
        A_SORT_MEM,
        DG_ADD,
        DG_SET,
        DG_CALL,
        DG_NUMOF,
        DG_EXIST,
        DG_ERASE,
        DG_CLEAR,
        DG_COPY,
        DG_ASSIGN,
        DG_PLUSA,
        DG_POP,
        DG_NEW_FROM_METHOD,
        DG_MINUSA,
        DG_CALLBEGIN,
        DG_NEW,
        DG_STR_TO_METHOD,

	OP_0x102 = 0x102,
	OP_0x103 = 0x103,
	OP_0x104 = 0x104,
	OP_0x105 = 0x105,

	NR_OPCODES
};

enum instruction_argtype {
	T_INT,
	T_FLOAT,
	T_ADDR,
	T_FUNC,
	T_STRING,
	T_MSG,
	T_LOCAL,
	T_GLOBAL,
	T_STRUCT,
	T_SYSCALL,
	T_HLL,
	T_HLLFUNC,
	T_FILE,
	T_DLG,
};

// TODO: Need to know which class a method belongs to
//       in order to use this. Should be replaced with
//       "this.<member-name>" in disassembled output.
#define T_MEMB T_INT

// TODO: As above, but for member of a member, etc.
#define T_MEMB2 T_INT
#define T_MEMB3 T_INT

// TODO: Similar to above, but for member of local variable.
//       Only used in one instruction--maybe special case?
#define T_LOCMEMB T_INT

struct instruction {
	const enum opcode opcode; // the opcode
	const char * const name;  // assembler name
	int nr_args;              // number of arguments (???: always 1 or 0?)
	const int ip_inc;         // amount to increment instruction pointer after instruction
	const bool implemented;   // implemented in xsystem4
	int args[INSTRUCTION_MAX_ARGS]; // argument data types
};

struct instruction instructions[NR_OPCODES];

static const_pure inline int32_t instruction_width(enum opcode opcode)
{
	return 2 + instructions[opcode].nr_args * 4;
}

#endif /* SYSTEM4_INSTRUCTIONS_H */
