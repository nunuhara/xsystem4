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
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "system4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "ainedit.h"
#include "jaf.h"
#include "jaf_parser.tab.h"

FILE *yyin;
int yyparse(void);

struct ain *jaf_ain_out;
struct jaf_block *jaf_toplevel;

int sym_type(char *name)
{
	char *u = encode_text_to_input_format(name);
	struct ain_struct *s = ain_get_struct(jaf_ain_out, u);
	free(u);

	if (s) {
		return TYPEDEF_NAME;
	}
	return IDENTIFIER;
}

static void write_opcode(struct buffer *out, uint16_t opcode)
{
	buffer_write_int16(out, opcode);
}

static void write_argument(struct buffer *out, uint32_t arg)
{
	buffer_write_int32(out, arg);
}

static void write_instruction0(struct buffer *out, uint16_t opcode)
{
	write_opcode(out, opcode);
}

static void write_instruction1(struct buffer *out, uint16_t opcode, uint32_t arg0)
{
	write_opcode(out, opcode);
	write_argument(out, arg0);
}

static void write_instruction2(struct buffer *out, uint16_t opcode, uint32_t arg0, uint32_t arg1)
{
	write_opcode(out, opcode);
	write_argument(out, arg0);
	write_argument(out, arg1);
}

static uint32_t flo2int(float f)
{
	union { uint32_t i; float f; } v = { .f = f};
	return v.i;
}

static enum opcode jaf_op_to_opcode(enum jaf_operator op, enum jaf_type type)
{
	if (type == JAF_FLOAT) {
		switch (op) {
		case JAF_MULTIPLY:      return F_MUL;
		case JAF_DIVIDE:        return F_DIV;
		case JAF_PLUS:          return F_ADD;
		case JAF_MINUS:         return F_SUB;
		case JAF_LT:            return F_LT;
		case JAF_GT:            return F_GT;
		case JAF_LTE:           return F_LTE;
		case JAF_GTE:           return F_GTE;
		case JAF_EQ:            return F_EQUALE;
		case JAF_ASSIGN:        return F_ASSIGN;
		case JAF_MUL_ASSIGN:    return F_MULA;
		case JAF_DIV_ASSIGN:    return F_DIVA;
		case JAF_ADD_ASSIGN:    return F_PLUSA;
		case JAF_SUB_ASSIGN:    return F_MINUSA;
		case JAF_REF_ASSIGN:    // TODO
		default: ERROR("Invalid floating point operator");
		}
	} else if (type == JAF_INT) {
		switch (op) {
		case JAF_MULTIPLY:      return MUL;
		case JAF_DIVIDE:        return DIV;
		case JAF_REMAINDER:     return MOD;
		case JAF_PLUS:          return ADD;
		case JAF_MINUS:         return SUB;
		case JAF_LSHIFT:        return LSHIFT;
		case JAF_RSHIFT:        return RSHIFT;
		case JAF_LT:            return LT;
		case JAF_GT:            return GT;
		case JAF_LTE:           return LTE;
		case JAF_GTE:           return GTE;
		case JAF_EQ:            return EQUALE;
		case JAF_BIT_AND:       return AND;
		case JAF_BIT_XOR:       return XOR;
		case JAF_BIT_IOR:       return OR;
		//case JAF_LOG_AND:       return AND;
		//case JAF_LOG_OR:        return OR;
		case JAF_ASSIGN:        return ASSIGN;
		case JAF_MUL_ASSIGN:    return MULA;
		case JAF_DIV_ASSIGN:    return DIVA;
		case JAF_MOD_ASSIGN:    return MODA;
		case JAF_ADD_ASSIGN:    return PLUSA;
		case JAF_SUB_ASSIGN:    return MINUSA;
		case JAF_LSHIFT_ASSIGN: return LSHIFTA;
		case JAF_RSHIFT_ASSIGN: return RSHIFTA;
		case JAF_AND_ASSIGN:    return ANDA;
		case JAF_XOR_ASSIGN:    return XORA;
		case JAF_OR_ASSIGN:     return ORA;
		case JAF_REF_ASSIGN:    // TODO
		default: ERROR("Invalid integer operator");
		}
	} else {
		ERROR("Invalid operator type");
	}
}

static void compile_block(struct ain *ain, struct buffer *out, struct jaf_block *block);
static void compile_expression(struct ain *ain, struct buffer *out, struct jaf_expression *expr);

static void compile_identifier(possibly_unused struct ain *ain, struct buffer *out, struct jaf_expression *expr)
{
	if (expr->ident.var_type == AIN_VAR_LOCAL) {
		write_instruction0(out, PUSHLOCALPAGE);
	} else if (expr->ident.var_type == AIN_VAR_GLOBAL) {
		write_instruction0(out, PUSHGLOBALPAGE);
	} else {
		ERROR("Invalid variable type");
	}
	write_instruction1(out, PUSH, expr->ident.var_no);
	write_instruction0(out, REF);
}

static void compile_unary(struct ain *ain, struct buffer *out, struct jaf_expression *expr)
{
	switch (expr->op) {
	case JAF_AMPERSAND:
		ERROR("Function types not supported");
	case JAF_UNARY_PLUS:
		compile_expression(ain, out, expr->expr);
		break;
	case JAF_UNARY_MINUS:
		write_instruction0(out, INV);
		break;
	case JAF_BIT_NOT:
		write_instruction0(out, COMPL);
		break;
	case JAF_LOG_NOT:
		write_instruction0(out, NOT);
		break;
	case JAF_PRE_INC:
	case JAF_PRE_DEC:
	case JAF_POST_INC:
	case JAF_POST_DEC:
		ERROR("Increment/decrement not supported"); // TODO: need location type
	default:
		ERROR("Invalid unary operator");
	}
}

static void compile_lvalue(possibly_unused struct ain *ain, struct buffer *out, struct jaf_expression *expr)
{
	if (expr->type == JAF_EXP_IDENTIFIER) {
		if (expr->ident.var_type == AIN_VAR_GLOBAL) {
			write_instruction0(out, PUSHGLOBALPAGE);
		} else if (expr->ident.var_type == AIN_VAR_LOCAL) {
			write_instruction0(out, PUSHLOCALPAGE);
		} else {
			ERROR("Invalid variable type as lvalue");
		}
		write_instruction1(out, PUSH, expr->ident.var_no);
	} else if (expr->type == JAF_EXP_MEMBER) {
		ERROR("struct member lvalues not supported");
	} else {
		ERROR("Invalid lvalue");
	}
}

static void compile_binary(struct ain *ain, struct buffer *out, struct jaf_expression *expr)
{
	size_t addr[3];
	switch (expr->op) {
	case JAF_MULTIPLY:
	case JAF_DIVIDE:
	case JAF_REMAINDER:
	case JAF_PLUS:
	case JAF_MINUS:
	case JAF_LSHIFT:
	case JAF_RSHIFT:
	case JAF_LT:
	case JAF_GT:
	case JAF_LTE:
	case JAF_GTE:
	case JAF_EQ:
	case JAF_BIT_AND:
	case JAF_BIT_XOR:
	case JAF_BIT_IOR:
		assert(expr->lhs->value_type.type == expr->rhs->value_type.type);
		compile_expression(ain, out, expr->lhs);
		compile_expression(ain, out, expr->rhs);
		write_instruction0(out, jaf_op_to_opcode(expr->op, expr->value_type.type));
		break;
	case JAF_LOG_AND:
		compile_expression(ain, out, expr->lhs);
		addr[0] = out->index + 2;
		write_instruction1(out, IFZ, 0);
		compile_expression(ain, out, expr->rhs);
		addr[1] = out->index + 2;
		write_instruction1(out, IFZ, 0);
		write_instruction1(out, PUSH, 1);
		addr[2] = out->index + 2;
		write_instruction1(out, JUMP, 0);
		buffer_write_int32_at(out, addr[0], out->index);
		buffer_write_int32_at(out, addr[1], out->index);
		write_instruction1(out, PUSH, 0);
		buffer_write_int32_at(out, addr[2], out->index);
		break;
	case JAF_LOG_OR:
		compile_expression(ain, out, expr->lhs);
		addr[0] = out->index + 2;
		write_instruction1(out, IFNZ, 0);
		compile_expression(ain, out, expr->rhs);
		addr[1] = out->index + 2;
		write_instruction1(out, IFNZ, 0);
		write_instruction1(out, PUSH, 0);
		addr[2] = out->index + 2;
		write_instruction1(out, JUMP, 0);
		buffer_write_int32_at(out, addr[0], out->index);
		buffer_write_int32_at(out, addr[1], out->index);
		write_instruction1(out, PUSH, 1);
		buffer_write_int32_at(out, addr[2], out->index);
		break;
	case JAF_ASSIGN:
	case JAF_MUL_ASSIGN:
	case JAF_DIV_ASSIGN:
	case JAF_MOD_ASSIGN:
	case JAF_ADD_ASSIGN:
	case JAF_SUB_ASSIGN:
	case JAF_LSHIFT_ASSIGN:
	case JAF_RSHIFT_ASSIGN:
	case JAF_AND_ASSIGN:
	case JAF_XOR_ASSIGN:
	case JAF_OR_ASSIGN:
	case JAF_REF_ASSIGN:
		compile_lvalue(ain, out, expr->lhs);
		compile_expression(ain, out, expr->rhs);
		write_instruction0(out, jaf_op_to_opcode(expr->op, expr->value_type.type));
		break;
	default:
		ERROR("Invalid binary operator");
	}
}

static void compile_ternary(struct ain *ain, struct buffer *out, struct jaf_expression *expr)
{
	ERROR("Ternary operator not supported");
}

static void compile_funcall(struct ain *ain, struct buffer *out, struct jaf_expression *expr)
{
	for (size_t i = 0; i < expr->call.args->nr_items; i++) {
		compile_expression(ain, out, expr->call.args->items[i]);
	}
	write_instruction1(out, CALLFUNC, expr->call.func_no);
}

static void compile_cast(struct ain *ain, struct buffer *out, struct jaf_expression *expr)
{
	enum jaf_type src_type = expr->cast.expr->value_type.type;
	enum jaf_type dst_type = expr->cast.type;
	compile_expression(ain, out, expr->cast.expr);

	if (src_type == dst_type)
		return;
	if (src_type == JAF_INT) {
		if (dst_type == JAF_FLOAT) {
			write_instruction0(out, ITOF);
		} else if (dst_type == JAF_STRING) {
			write_instruction0(out, I_STRING);
		} else {
			goto invalid_cast;
		}
	} else if (src_type == JAF_FLOAT) {
		if (dst_type == JAF_INT) {
			write_instruction0(out, FTOI);
		} else if (dst_type == JAF_STRING) {
			write_instruction0(out, FTOS);
		} else {
			goto invalid_cast;
		}
	} else if (src_type == JAF_STRING) {
		if (dst_type == JAF_INT) {
			write_instruction0(out, STOI);
		} else {
			goto invalid_cast;
		}
	}
	return;
invalid_cast:
	ERROR("Unsupported cast: %s to %s", jaf_typestr(src_type), jaf_typestr(dst_type));
}

static void compile_member(struct ain *ain, struct buffer *out, struct jaf_expression *expr)
{
	ERROR("struct member access not supported");
}

static void compile_expression(struct ain *ain, struct buffer *out, struct jaf_expression *expr)
{
	switch (expr->type) {
	case JAF_EXP_VOID:
		break;
	case JAF_EXP_INT:
		write_instruction1(out, PUSH, expr->i);
		break;
	case JAF_EXP_FLOAT:
		write_instruction1(out, F_PUSH, flo2int(expr->f));
		break;
	case JAF_EXP_STRING:
		ERROR("strings not supported");
	case JAF_EXP_IDENTIFIER:
		compile_identifier(ain, out, expr);
		break;
	case JAF_EXP_UNARY:
		compile_unary(ain, out, expr);
		break;
	case JAF_EXP_BINARY:
		compile_binary(ain, out, expr);
		break;
	case JAF_EXP_TERNARY:
		compile_ternary(ain, out, expr);
		break;
	//case JAF_EXP_SUBSCRIPT:
		//compile_subscript(ain, out, expr);
		//break;
	case JAF_EXP_FUNCALL:
		compile_funcall(ain, out, expr);
		break;
	case JAF_EXP_CAST:
		compile_cast(ain, out, expr);
		break;
	case JAF_EXP_MEMBER:
		compile_member(ain, out, expr);
		break;
	//case JAF_EXP_SEQ:
		//compile_seq(ain, out, expr);
		//break;
	}
}

static void compile_vardecl(struct ain *ain, struct buffer *out, struct jaf_declaration *decl)
{
	switch (decl->type->type) {
	case JAF_VOID:
		ERROR("void variable declaration");
	case JAF_INT:
		write_instruction0(out, PUSHLOCALPAGE);
		write_instruction1(out, PUSH, decl->var_no);
		if (decl->init) {
			compile_expression(ain, out, decl->init);
		} else {
			write_instruction1(out, PUSH, 0);
		}
		write_instruction0(out, ASSIGN);
		write_instruction0(out, POP);
		break;
	case JAF_FLOAT:
		write_instruction0(out, PUSHLOCALPAGE);
		write_instruction1(out, PUSH, decl->var_no);
		if (decl->init) {
			compile_expression(ain, out, decl->init);
		} else {
			write_instruction1(out, F_PUSH, 0);
		}
		write_instruction0(out, F_ASSIGN);
		write_instruction0(out, POP);
		break;
	case JAF_STRING:
		ERROR("strings not supported");
	case JAF_STRUCT:
		ERROR("structs not supported");
	case JAF_ENUM:
		ERROR("enums not supported");
	case JAF_TYPEDEF:
		ERROR("Unresolved typedef");
	case JAF_FUNCTION:
		ERROR("Function types not supported");
		break;
	}
}

static void compile_statement(struct ain *ain, struct buffer *out, struct jaf_block_item *item)
{
	int addr[8];
	switch (item->kind) {
	case JAF_DECLARATION:
		compile_vardecl(ain, out, &item->decl);
		break;
	case JAF_FUNDECL:
		ERROR("Nested functions not supported");
	case JAF_STMT_LABELED:
		ERROR("Labels not supported");
		break;
	case JAF_STMT_COMPOUND:
		compile_block(ain, out, item->block);
		break;
	case JAF_STMT_EXPRESSION:
		compile_expression(ain, out, item->expr);
		switch (item->expr->value_type.type) {
		case JAF_VOID:
			break;
		case JAF_INT:
		case JAF_FLOAT:
		case JAF_STRING:
		case JAF_STRUCT: // FIXME: this also needs a DELETE I think...
		case JAF_ENUM:
		case JAF_FUNCTION:
			write_instruction0(out, POP);
			break;
		// TODO: immediate ref types will need additional POP
		case JAF_TYPEDEF:
			ERROR("Unresolved typedef");
		}
		break;
	case JAF_STMT_IF:
		compile_expression(ain, out, item->cond.test);
		addr[0] = out->index + 2;
		write_instruction1(out, IFNZ, 0);
		addr[1] = out->index + 2;
		write_instruction1(out, JUMP, 0);
		buffer_write_int32_at(out, addr[0], out->index);
		compile_statement(ain, out, item->cond.consequent);
		if (item->cond.alternative) {
			addr[2] = out->index + 2;
			write_instruction1(out, JUMP, 0);
			buffer_write_int32_at(out, addr[1], out->index);
			compile_statement(ain, out, item->cond.alternative);
			buffer_write_int32_at(out, addr[2], out->index);
		} else {
			buffer_write_int32_at(out, addr[1], out->index);
		}
		break;
	case JAF_STMT_SWITCH:
		ERROR("switch not supported");
		break;
	case JAF_STMT_WHILE:
		ERROR("while loops not supported");
		break;
	case JAF_STMT_DO_WHILE:
		ERROR("do-while loops not supported");
		break;
	case JAF_STMT_FOR:
		ERROR("for loops not supported");
		break;
	case JAF_STMT_GOTO:
		ERROR("goto not supported");
	case JAF_STMT_CONTINUE:
		break;
	case JAF_STMT_BREAK:
		ERROR("break not supported");
		break;
	case JAF_STMT_RETURN:
		if (item->expr)
			compile_expression(ain, out, item->expr);
		write_instruction0(out, RETURN);
		break;
	case JAF_STMT_CASE:
		ERROR("switch not supported");
		break;
	case JAF_STMT_DEFAULT:
		ERROR("switch not supported");
		break;
	}
}

static void compile_block(struct ain *ain, struct buffer *out, struct jaf_block *block)
{
	for (size_t i = 0; i < block->nr_items; i++) {
		compile_statement(ain, out, block->items[i]);
	}
}

static void compile_function(struct ain *ain, struct buffer *out, struct jaf_declaration *decl)
{
	assert(decl->func_no >= 0 && decl->func_no < ain->nr_functions);
	ain->functions[decl->func_no].address = out->index;
	write_instruction1(out, FUNC, decl->func_no);
	compile_block(ain, out, decl->body);
	write_instruction1(out, ENDFUNC, decl->func_no);
}

static void compile_declaration(struct ain *ain, struct buffer *code, struct jaf_block_item *decl)
{
	if (decl->kind == JAF_FUNDECL) {
		compile_function(ain, code, &decl->decl);
		return;
	}

	// TODO: global constructors/array allocations
}

void jaf_compile(struct ain *out, const char *path)
{
	if (path) {
		if (!strcmp(path, "-"))
			yyin = stdin;
		else
			yyin = fopen(path, "rb");
		if (!yyin)
			ERROR("Opening input file '%s': %s", path, strerror(errno));
	}
	jaf_ain_out = out;
	yyparse();

	if (!jaf_toplevel)
		ERROR("Failed to parse .jaf file");

	jaf_toplevel = jaf_static_analyze(out, jaf_toplevel);

	struct buffer buf = {
		.buf = out->code,
		.size = out->code_size,
		.index = out->code_size
	};
	for (size_t i = 0; i < jaf_toplevel->nr_items; i++) {
		compile_declaration(out, &buf, jaf_toplevel->items[i]);
	}
	out->code = buf.buf;
	out->code_size = buf.index;

	// XXX: ain_add_initval adds initval to GSET section of the ain file.
	//      If the original ain file did not have a GSET section, init
	//      code needs to be added to the "0" function instead.
	if (out->nr_initvals && !out->GSET.present) {
		WARNING("%d global initvals ignored", out->nr_initvals);
	}

	jaf_free_block(jaf_toplevel);
}
