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
#include "system4.h"
#include "jaf.h"

// TODO: better error messages
#define TYPE_ERROR(expr, expected) ERROR("Type error (expected %s; got %s)", jaf_typestr(expected), jaf_typestr(expr->derived_type))
#define TYPE_CHECK(expr, expected) { if (expr->derived_type != expected) TYPE_ERROR(expr, expected); }
#define TYPE_CHECK_NUMERIC(expr) { if (expr->derived_type != JAF_INT && expr->derived_type != JAF_FLOAT) TYPE_ERROR(expr, JAF_INT); }

static const char *jaf_typestr(enum jaf_type type)
{
	switch (type) {
	case JAF_VOID:    return "void";
	case JAF_INT:     return "int";
	case JAF_FLOAT:   return "float";
	case JAF_STRING:  return "string";
	case JAF_STRUCT:  return "struct";
	case JAF_ENUM:    return "enum";
	case JAF_TYPEDEF: return "typedef";
	}
	return "unknown";
}

static enum jaf_type jaf_type_check_numeric(struct jaf_expression *expr)
{
	if (expr->derived_type != JAF_INT && expr->derived_type != JAF_FLOAT)
		TYPE_ERROR(expr, JAF_INT);
	return expr->derived_type;
}

static enum jaf_type jaf_type_check_int(struct jaf_expression *expr)
{
	if (expr->derived_type != JAF_INT)
		TYPE_ERROR(expr, JAF_INT);
	return expr->derived_type;
}

static enum jaf_type jaf_merge_types(enum jaf_type a, enum jaf_type b)
{
	if (a == b)
		return a;
	if (a == JAF_INT && b == JAF_FLOAT)
		return JAF_FLOAT;
	if (a == JAF_FLOAT && b == JAF_INT)
		return JAF_FLOAT;
	ERROR("Incompatible types");
}

static enum jaf_type jaf_check_types_unary(struct jaf_expression *expr)
{
	jaf_derive_types(expr->expr);
	switch (expr->op) {
	case JAF_UNARY_PLUS:
	case JAF_UNARY_MINUS:
	case JAF_PRE_INC:
	case JAF_PRE_DEC:
	case JAF_POST_INC:
	case JAF_POST_DEC:
		return jaf_type_check_numeric(expr->expr);
	case JAF_BIT_NOT:
	case JAF_LOG_NOT:
		return jaf_type_check_int(expr->expr);
	case JAF_AMPERSAND:
		ERROR("Function types not supported");
	default:
		break;
	}
	ERROR("Unhandled unary operator");
}

static enum jaf_type jaf_check_types_binary(struct jaf_expression *expr)
{
	jaf_derive_types(expr->lhs);
	jaf_derive_types(expr->rhs);
	switch (expr->op) {
	// real ops
	case JAF_MULTIPLY:
	case JAF_DIVIDE:
	case JAF_PLUS:
	case JAF_MINUS:
		return jaf_merge_types(jaf_type_check_numeric(expr->lhs), jaf_type_check_numeric(expr->rhs));
	// integer ops
	case JAF_REMAINDER:
	case JAF_LSHIFT:
	case JAF_RSHIFT:
	case JAF_BIT_AND:
	case JAF_BIT_XOR:
	case JAF_BIT_IOR:
		jaf_type_check_int(expr->lhs);
		jaf_type_check_int(expr->rhs);
		return JAF_INT;
	// comparison ops
	case JAF_LT:
	case JAF_GT:
	case JAF_LTE:
	case JAF_GTE:
	case JAF_EQ:
		jaf_type_check_numeric(expr->lhs);
		jaf_type_check_numeric(expr->rhs);
		return JAF_INT;
	// boolean ops
	case JAF_LOG_AND:
	case JAF_LOG_OR:
		jaf_type_check_int(expr->lhs);
		jaf_type_check_int(expr->rhs);
		return JAF_INT;
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
		ERROR("Assignment expressions not supported");
	default:
		break;
	}
	ERROR("Unhandled binary operator");
}

static enum jaf_type jaf_check_types_ternary(struct jaf_expression *expr)
{
	jaf_derive_types(expr->condition);
	jaf_derive_types(expr->consequent);
	jaf_derive_types(expr->alternative);

	TYPE_CHECK(expr->condition, JAF_INT);
	if (expr->consequent->derived_type != expr->alternative->derived_type) {
		// TODO: better error message
		ERROR("Mismatched types in conditional expression");
	}

	return expr->consequent->derived_type;
}

static enum jaf_type _jaf_check_types(struct jaf_expression *expr)
{
	switch (expr->type) {
	case JAF_EXP_VOID:
		return JAF_VOID;
	case JAF_EXP_INT:
		return JAF_INT;
	case JAF_EXP_FLOAT:
		return JAF_FLOAT;
	case JAF_EXP_STRING:
		return JAF_STRING;
	case JAF_EXP_IDENTIFIER:
		// TODO: need environment
		ERROR("Identifiers not supported");
	case JAF_EXP_UNARY:
		return jaf_check_types_unary(expr);
	case JAF_EXP_BINARY:
		return jaf_check_types_binary(expr);
	case JAF_EXP_TERNARY:
		return jaf_check_types_ternary(expr);
	case JAF_EXP_CAST:
		return expr->cast.type;
	}
	ERROR("Unhandled expression type");
}

void jaf_derive_types(struct jaf_expression *expr)
{
	expr->derived_type = _jaf_check_types(expr);
}

void jaf_check_type(struct jaf_expression *expr, struct jaf_type_specifier *type)
{
	if (expr->derived_type != type->type) {
		TYPE_ERROR(expr, type->type);
	}
}
