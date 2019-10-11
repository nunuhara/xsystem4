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
#include "vm_string.h"
#include "ain.h"
#include "instructions.h"
#include "little_endian.h"

/*
 * NOTE: The current implementation is a simple bytecode interpreter.
 *       System40.exe uses a JIT compiler, and we should too.
 */

union vm_value {
	int32_t i;
	int64_t i64;
	float f;
	struct string *s;
};

static union vm_value *stack = NULL;
static size_t stack_size;
static int32_t stack_ptr = 0;

static union vm_value frame_stack[4096];
static int32_t frame_ptr = 0;

#define FRAME_FN_OFF  0 // offset to function number
#define FRAME_IP_OFF  1 // offset to return address
#define FRAME_FP_OFF  2 // offset to frame pointer
#define FRAME_VAR_OFF 3 // offset to variables

static struct ain *ain;
static size_t instr_ptr = 0;

static union vm_value _vm_id(union vm_value v)
{
	return v;
}

static union vm_value vm_int(int32_t v)
{
	return (union vm_value) { .i = v };
}

static union vm_value vm_long(int64_t v)
{
	return (union vm_value) { .i64 = v };
}

static union vm_value vm_float(float v)
{
	return (union vm_value) { .f = v };
}

static union vm_value vm_string(struct string *v)
{
	return (union vm_value) { .s = v };
}

#define vm_value_cast(v) _Generic((v),				\
				  union vm_value: _vm_id,	\
				  int32_t: vm_int,		\
				  int64_t: vm_long,		\
				  float: vm_float,		\
				  struct string*: vm_string)(v)

static int32_t local_get(int varno)
{
	return frame_stack[frame_ptr + FRAME_VAR_OFF + varno].i;
}

static int32_t local_ref(int varno)
{
	return frame_ptr + FRAME_VAR_OFF + varno;
}

static void local_set(int varno, int32_t value)
{
	frame_stack[frame_ptr + FRAME_VAR_OFF + varno].i = value;
}

static enum ain_data_type local_type(int varno)
{
	int32_t fno = frame_stack[frame_ptr + FRAME_FN_OFF].i;
	return ain->functions[fno].vars[varno].data_type;
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

static union vm_value stack_peek(int n)
{
	return stack[stack_ptr - (1 + n)];
}

// Set the Nth value from the top of the stack to V.
#define stack_set(n, v) (stack[stack_ptr - (1 + (n))] = vm_value_cast(v))

#define stack_push(v) (stack[stack_ptr++] = vm_value_cast(v))

static union vm_value stack_pop(void)
{
	stack_ptr--;
	return stack[stack_ptr];
}

// Pop a reference off the stack, returning the address of the referenced object.
static int32_t stack_pop_ref(void)
{
	int32_t index = stack_pop().i;
	int32_t frame = stack_pop().i;
	return frame + FRAME_VAR_OFF + index;
}

static void stack_push_string_literal(int32_t no)
{
	stack[stack_ptr++].s = ain->strings[no];
}

static void stack_push_string(struct string *str)
{
	stack[stack_ptr++].s = str;
}

static struct string *stack_pop_string(void)
{
	stack_ptr--;
	return stack[stack_ptr].s;
}

static void stack_toss_string(void)
{
	stack_ptr--;
	free_string(stack[stack_ptr].s);
}

static struct string *stack_peek_string(void)
{
	return stack[stack_ptr-1].s;
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
	struct ain_function *f = &ain->functions[no];
	int32_t cur_fno = frame_stack[frame_ptr + FRAME_FN_OFF].i;
	int32_t new_fp = frame_ptr + FRAME_VAR_OFF + ain->functions[cur_fno].nr_vars;

	// create new stack frame
	frame_stack[new_fp + FRAME_FN_OFF].i = no;
	frame_stack[new_fp + FRAME_IP_OFF].i = instr_ptr + instruction_width(CALLFUNC);
	frame_stack[new_fp + FRAME_FP_OFF].i = frame_ptr;
	for (int i = f->nr_args - 1; i >= 0; i--) {
		frame_stack[new_fp + FRAME_VAR_OFF + i] = stack_pop();
	}

	// update frame & instruction pointers
	frame_ptr = new_fp;
	instr_ptr = ain->functions[no].address;
}

static void function_return(void)
{
	instr_ptr  = frame_stack[frame_ptr + FRAME_IP_OFF].i;
	frame_ptr  = frame_stack[frame_ptr + FRAME_FP_OFF].i;
}

static void system_call(int32_t code)
{
	switch (code) {
	case 0x0: // system.Exit(int nResult)
		sys_exit(stack_pop().i);
		break;
	case 0x6: // system.Output(string szText)
		sys_message("%s", stack_peek_string()->text);
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
	int32_t index, a, b, c, v;
	union vm_value val;
	struct string *sa, *sb;
	const char *opcode_name = "UNKNOWN";
	switch (opcode) {
	//
	// --- Stack Management ---
	//
	case PUSH:
		stack_push(get_argument(0));
		break;
	case S_PUSH:
		stack_push_string_literal(get_argument(0));
		break;
	case POP:
		stack_pop();
		break;
	case S_POP:
		stack_toss_string();
		break;
	case REF:
		// Dereference a reference to a value.
		stack_push(frame_stack[stack_pop_ref()].i);
		break;
	case S_REF:
		// Dereference a reference to a string
		stack_push_string(frame_stack[stack_pop_ref()].s);
		break;
	case REFREF:
		// Dereference a reference to a reference.
		index = stack_pop_ref();
		stack_push(frame_stack[index].i);
		stack_push(frame_stack[index + 1].i);
		break;
	case DUP:
		// A -> AA
		stack_push(stack_peek(0).i);
		break;
	case DUP2:
		// AB -> ABAB
		a = stack_peek(1).i;
		b = stack_peek(0).i;
		stack_push(a);
		stack_push(b);
		break;
	case DUP_X2:
		// ABC -> CABC
		a = stack_peek(2).i;
		b = stack_peek(1).i;
		c = stack_peek(0).i;
		stack_set(2, c);
		stack_set(1, a);
		stack_set(0, b);
		stack_push(c);
		break;
	case DUP2_X1:
		// ABC -> BCABC
		a = stack_peek(2).i;
		b = stack_peek(1).i;
		c = stack_peek(0).i;
		stack_set(2, b);
		stack_set(1, c);
		stack_set(0, a);
		stack_push(b);
		stack_push(c);
		break;
	case PUSHLOCALPAGE:
		stack_push(frame_ptr);
		break;
	case ASSIGN:
		v = stack_pop().i;
		index = stack_pop_ref();
		frame_stack[index].i = v;
		break;
	case SH_LOCALREF: // VARNO
		// XXX: This instruction does different things depending on the
		//      type of the local variable.
		a = get_argument(0);
		switch (local_type(a)) {
		case AIN_STRING:
			// push a pointer to the stack (assignable)
			stack_push(local_ref(a));
			break;
		default:
			// push the value to the stack (immediate)
			stack_push(local_get(a));
			break;
		}
		break;
	case SH_LOCALASSIGN: // VARNO, VALUE
		// Assign VALUE to local VARNO
		local_set(get_argument(0), get_argument(1));
		break;
	case SH_LOCALINC: // VARNO
		index = get_argument(0);
		local_set(index, local_get(index)+1);
		break;
	case SH_LOCALDEC: // VARNO
		index = get_argument(0);
		local_set(index, local_get(index)-1);
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
		if (!stack_pop().i)
			instr_ptr = get_argument(0);
		else
			instr_ptr += instruction_width(IFZ);
		break;
	case IFNZ: // ADDR
		if (stack_pop().i)
			instr_ptr = get_argument(0);
		else
			instr_ptr += instruction_width(IFNZ);
		break;
	//
	// --- Arithmetic ---
	//
	case INV:
		stack[stack_ptr-1].i = -stack[stack_ptr-1].i;
		break;
	case NOT:
		stack[stack_ptr-1].i = !stack[stack_ptr-1].i;
		break;
	case COMPL:
		stack[stack_ptr-1].i = ~stack[stack_ptr-1].i;
		break;
	case ADD:
		stack[stack_ptr-2].i += stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	case SUB:
		stack[stack_ptr-2].i -= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	case MUL:
		stack[stack_ptr-2].i *= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	case DIV:
		stack[stack_ptr-2].i /= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	case MOD:
		stack[stack_ptr-2].i %= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	case AND:
		stack[stack_ptr-2].i &= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	case OR:
		stack[stack_ptr-2].i |= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	case XOR:
		stack[stack_ptr-2].i ^= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	case LSHIFT:
		stack[stack_ptr-2].i <<= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	case RSHIFT:
		stack[stack_ptr-2].i >>= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	// Numeric Comparisons
	case LT:
		b = stack_pop().i;
		a = stack_pop().i;
		stack_push(a < b ? 1 : 0);
		break;
	case GT:
		b = stack_pop().i;
		a = stack_pop().i;
		stack_push(a > b ? 1 : 0);
		break;
	case LTE:
		b = stack_pop().i;
		a = stack_pop().i;
		stack_push(a <= b ? 1 : 0);
		break;
	case GTE:
		b = stack_pop().i;
		a = stack_pop().i;
		stack_push(a >= b ? 1 : 0);
		break;
	case NOTE:
		b = stack_pop().i;
		a = stack_pop().i;
		stack_push(a != b ? 1 : 0);
		break;
	case EQUALE:
		b = stack_pop().i;
		a = stack_pop().i;
		stack_push(a == b ? 1 : 0);
		break;
	// +=, -=, etc.
	case PLUSA:
		v = stack_pop().i;
		index = stack_pop_ref();
		frame_stack[index].i += v;
		break;
	case MINUSA:
		v = stack_pop().i;
		index = stack_pop_ref();
		frame_stack[index].i -= v;
		break;
	case MULA:
		v = stack_pop().i;
		index = stack_pop_ref();
		frame_stack[index].i *= v;
		break;
	case DIVA:
		v = stack_pop().i;
		index = stack_pop_ref();
		frame_stack[index].i /= v;
		break;
	case MODA:
		v = stack_pop().i;
		index = stack_pop_ref();
		frame_stack[index].i %= v;
		break;
	case ANDA:
		v = stack_pop().i;
		index = stack_pop_ref();
		frame_stack[index].i &= v;
		break;
	case ORA:
		v = stack_pop().i;
		index = stack_pop_ref();
		frame_stack[index].i |= v;
		break;
	case XORA:
		v = stack_pop().i;
		index = stack_pop_ref();
		frame_stack[index].i ^= v;
		break;
	case LSHIFTA:
		v = stack_pop().i;
		index = stack_pop_ref();
		frame_stack[index].i <<= v;
		break;
	case RSHIFTA:
		v = stack_pop().i;
		index = stack_pop_ref();
		frame_stack[index].i >>= v;
		break;
	case INC:
		index = stack_pop_ref();
		frame_stack[index].i++;
		break;
	case DEC:
		index = stack_pop_ref();
		frame_stack[index].i--;
		break;
	//
	// --- Strings ---
	//
	case S_ASSIGN:
		sa = stack_pop_string();
		val = stack_pop();
		frame_stack[val.i].s = sa;
		stack_push_string(sa);
		break;
	case S_ADD:
		sb = stack_pop_string();
		sa = stack_pop_string();
		sa = string_append(sa, sb);
		stack_push_string(sa);
		break;
	case I_STRING:
		stack_push_string(integer_to_string(stack_pop().i));
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

	// Jump to main. We set up a stack frame so that when main returns,
	// the first instruction past the end of the code section is executed.
	// ()When we read the AIN file, CALLSYS 0x0 was placed there.)
	instr_ptr = ain->code_size - instruction_width(CALLFUNC);
	function_call(ain->main);

	// fetch/decode/execute loop
	for (;;)
	{
		if (instr_ptr >= ain->code_size + 6) {
			ERROR("Illegal instruction pointer: 0x%lX", instr_ptr);
		}
		int16_t opcode = get_opcode(instr_ptr);
		execute_instruction(opcode);
		instr_ptr += instructions[opcode].ip_inc;
	}
}
