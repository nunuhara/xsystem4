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

#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "sys4_string.h"
#include "ain.h"
#include "instructions.h"
#include "little_endian.h"

/*
 * NOTE: The current implementation is a simple bytecode interpreter.
 *       System40.exe uses a JIT compiler, and we should too.
 */

static int32_t *stack = NULL;
static size_t stack_size;
static int stack_ptr = 0;

// Separate stack for strings
// Not sure exactly how System40.exe handles strings.
// Further testing required to ensure this approach doesn't break anything.
static struct string string_stack[1024];
static int string_stack_ptr = 0;

//static struct frame *frame_stack[1024];
static int32_t frame_stack[4096];
static int frame_ptr = 0;

#define FRAME_FN_OFF  0 // offset to function number
#define FRAME_IP_OFF  1 // offset to return address
#define FRAME_FP_OFF  2 // offset to frame pointer
#define FRAME_VAR_OFF 3 // offset to variables

static struct ain *ain;
static size_t instr_ptr = 0;

static int32_t local_ref(int varno)
{
	return frame_stack[frame_ptr + FRAME_VAR_OFF + varno];
}

static void local_set(int varno, int32_t value)
{
	frame_stack[frame_ptr + FRAME_VAR_OFF + varno] = value;
}

// Read the opcode at ADDR.
static int16_t get_opcode(size_t addr)
{
	return LittleEndian_getW(ain->code, addr);
}

// Read argument N for the current instruction.
static int32_t get_argument(int n)
{
	return LittleEndian_getDW(ain->code, instr_ptr + 2 + n*4);
}

static void stack_push(int32_t v)
{
	stack[stack_ptr++] = v;
}

static int32_t stack_pop(void)
{
	stack_ptr--;
	return stack[stack_ptr];
}

// Pop a reference off the stack, returning the address of the referenced object.
static int32_t stack_pop_ref(void)
{
	int32_t index = stack_pop();
	int32_t frame = stack_pop();
	return frame + FRAME_VAR_OFF + index;
}

static void stack_push_string_literal(int32_t no)
{
	// FIXME: should store string length when reading AIN
	string_stack[string_stack_ptr++] = make_string(ain->strings[no], strlen(ain->strings[no]));
}

static void stack_push_string(struct string *str)
{
	string_stack[string_stack_ptr++] = *str;
}

static struct string stack_pop_string(void)
{
	string_stack_ptr--;
	return string_stack[string_stack_ptr];
}

static void stack_toss_string(void)
{
	string_stack_ptr--;
	free_string(&string_stack[string_stack_ptr]);
}

static struct string *stack_peek_string(void)
{
	return &string_stack[string_stack_ptr-1];
}

/*
 * System 4 calling convention:
 *   - caller pushes arguments, in order
 *   - CALLFUNC creates stack frame ("page", on separate stack), pops arguments
 *   - callee pushes return value on the stack
 *   - RETURN jumps to return address (saved in stack frame)
 */

static void function_call(int32_t no)
{
	int32_t cur_fno = frame_stack[frame_ptr + FRAME_FN_OFF];
	int32_t new_fp = frame_ptr + FRAME_VAR_OFF + ain->functions[cur_fno].nr_vars;

	// create new stack frame
	frame_stack[new_fp + FRAME_FN_OFF] = no;
	frame_stack[new_fp + FRAME_IP_OFF] = instr_ptr + instruction_width(CALLFUNC);
	frame_stack[new_fp + FRAME_FP_OFF] = frame_ptr;
	for (int i = ain->functions[no].nr_args - 1; i >= 0; i--) {
		frame_stack[new_fp + FRAME_VAR_OFF + i] = stack_pop();
	}

	// update frame & instruction pointers
	frame_ptr = new_fp;
	instr_ptr = ain->functions[no].address;
}

static void function_return(void)
{
	instr_ptr  = frame_stack[frame_ptr + FRAME_IP_OFF];
	frame_ptr  = frame_stack[frame_ptr + FRAME_FP_OFF];
}

static void system_call(int32_t code)
{
	switch (code) {
	case 0x6: // system.Output(string szText)
		sys_message("%s", stack_peek_string()->str);
		// XXX: caller S_POPs
		break;
	case 0x14: // system.Peek()
		break;
	case 0x15: // system.Sleep(int nSleep)
		stack_pop();
		break;
	default:
		WARNING("Unimplemented syscall: 0x%X", code);
	}
}

static void execute_instruction(int16_t opcode)
{
	int32_t index, a, b;
	struct string sa, sb;
	const char *opcode_name = "UNKNOWN";
	switch (opcode) {
	//
	// --- Stack Management ---
	//
	case PUSH:
		stack_push(get_argument(0));
		break;
	case POP:
		stack_pop();
		break;
	case REF:
		// Dereference a reference to a value.
		stack_push(frame_stack[stack_pop_ref()]);
		break;
	case REFREF:
		// Dereference a reference to a reference.
		index = stack_pop_ref();
		stack_push(frame_stack[index]);
		stack_push(frame_stack[index + 1]);
		break;
	case PUSHLOCALPAGE:
		stack_push(frame_ptr);
		break;
	case SH_LOCALREF: // VARNO
		// Push the value of VARNO to the stack
		stack_push(local_ref(get_argument(0)));
		break;
	case SH_LOCALASSIGN: // VARNO, VALUE
		// Assign VALUE to local VARNO
		local_set(get_argument(0), get_argument(1));
		break;
	case SH_LOCALINC: // VARNO
		index = get_argument(0);
		local_set(index, local_ref(index)+1);
		break;
	case SH_LOCALDEC: // VARNO
		index = get_argument(0);
		local_set(index, local_ref(index)-1);
		break;
	//
	// --- Function Calls ---
	//
	case CALLFUNC:
		function_call(get_argument(0));
		break;
	case RETURN:
		function_return();
		break;
	case CALLSYS:
		system_call(get_argument(0));
		break;
	//
	// --- Control Flow ---
	//
	case JUMP: // ADDR
		instr_ptr = get_argument(0);
		break;
	case IFZ: // ADDR
		if (!stack_pop())
			instr_ptr = get_argument(0);
		else
			instr_ptr += instruction_width(IFZ);
		break;
	case IFNZ: // ADDR
		if (stack_pop())
			instr_ptr = get_argument(0);
		else
			instr_ptr += instruction_width(IFNZ);
		break;
	//
	// --- Arithmetic ---
	//
	case LT:
		b = stack_pop();
		a = stack_pop();
		stack_push(a < b ? 1 : 0);
		break;
	case GT:
		b = stack_pop();
		a = stack_pop();
		stack_push(a > b ? 1 : 0);
		break;
	case LTE:
		b = stack_pop();
		a = stack_pop();
		stack_push(a <= b ? 1 : 0);
		break;
	case GTE:
		b = stack_pop();
		a = stack_pop();
		stack_push(a >= b ? 1 : 0);
		break;
	case NOTE:
		b = stack_pop();
		a = stack_pop();
		stack_push(a != b ? 1 : 0);
		break;
	case EQUALE:
		b = stack_pop();
		a = stack_pop();
		stack_push(a == b ? 1 : 0);
		break;
	//
	// --- Strings ---
	//
	case S_PUSH:
		stack_push_string_literal(get_argument(0));
		break;
	case S_POP:
		stack_toss_string();
		break;
	case S_ADD:
		sb = stack_pop_string();
		sa = stack_pop_string();
		string_add(&sa, &sb);
		stack_push_string(&sa);
		break;
	case I_STRING:
		sa = integer_to_string(stack_pop());
		stack_push_string(&sa);
		break;
	// -- NOOPs ---
	case FUNC:
		break;
	default:
		if (opcode >= 0 && opcode < NR_OPCODES && instructions[opcode].name) {
			opcode_name = instructions[opcode].name;
		}
		WARNING("Unimplemented instruction: 0x%X(%s)", opcode, opcode_name);
	}
}

void vm_execute(struct ain *program)
{
	// initialize machine state
	stack_ptr = 0;
	frame_ptr = 0;
	stack_size = 1024;
	stack = xmalloc(stack_size);
	ain = program;

	// jump to main and begin fetch-decode-execute cycle
	//instr_ptr = ain->functions[ain->main].address;
	function_call(ain->main);
	for ( ; instr_ptr < ain->code_size; )
	{
		if (instr_ptr >= ain->code_size) {
			ERROR("Illegal instruction pointer: 0x%lX", instr_ptr);
		}
		int16_t opcode = get_opcode(instr_ptr);
		execute_instruction(opcode);
		instr_ptr += instructions[opcode].ip_inc;
	}
}
