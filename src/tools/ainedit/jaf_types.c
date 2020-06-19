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
#include <assert.h>
#include "system4.h"
#include "system4/string.h"
#include "ainedit.h"
#include "jaf.h"

// TODO: better error messages
#define TYPE_ERROR(expr, expected) ERROR("Type error (expected %s; got %s)", jaf_typestr(expected), jaf_typestr(expr->value_type.type))
#define TYPE_CHECK(expr, expected) { if (expr->value_type.type != expected) TYPE_ERROR(expr, expected); }
#define TYPE_CHECK_NUMERIC(expr) { if (expr->derived_type != JAF_INT && expr->derived_type != JAF_FLOAT) TYPE_ERROR(expr, JAF_INT); }

const char *jaf_typestr(enum jaf_type type)
{
	switch (type) {
	case JAF_VOID:     return "void";
	case JAF_INT:      return "int";
	case JAF_FLOAT:    return "float";
	case JAF_STRING:   return "string";
	case JAF_STRUCT:   return "struct";
	case JAF_ENUM:     return "enum";
	case JAF_TYPEDEF:  return "typedef";
	case JAF_FUNCTION: return "functype";
	}
	return "unknown";
}

static bool jaf_type_equal(struct jaf_type_specifier *a, struct jaf_type_specifier *b)
{
	if (a->type != b->type)
		return false;
	if (a->type == JAF_STRUCT && a->struct_no != b->struct_no)
		return false;
	if (a->type == JAF_FUNCTION && a->func_no != b->func_no)
		return false;
	return true;
}

static enum jaf_type jaf_type_check_numeric(struct jaf_expression *expr)
{
	if (expr->value_type.type != JAF_INT && expr->value_type.type != JAF_FLOAT)
		TYPE_ERROR(expr, JAF_INT);
	return expr->value_type.type;
}

static enum jaf_type jaf_type_check_int(struct jaf_expression *expr)
{
	if (expr->value_type.type != JAF_INT)
		TYPE_ERROR(expr, JAF_INT);
	return JAF_INT;
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

static void jaf_check_types_lvalue(struct jaf_env *env, struct jaf_expression *e)
{
	// TODO: array subscripts
	if (e->type != JAF_EXP_IDENTIFIER && e->type != JAF_EXP_MEMBER)
		ERROR("Invalid expression as lvalue");
	switch (e->value_type.type) {
	case JAF_INT:
	case JAF_FLOAT:
	case JAF_STRING:
		break;
	default:
		ERROR("Invalid type as lvalue");
	}
}

static void jaf_check_types_unary(struct jaf_env *env, struct jaf_expression *expr)
{
	jaf_derive_types(env, expr->expr);
	switch (expr->op) {
	case JAF_UNARY_PLUS:
	case JAF_UNARY_MINUS:
	case JAF_PRE_INC:
	case JAF_PRE_DEC:
	case JAF_POST_INC:
	case JAF_POST_DEC:
		expr->value_type.type = jaf_type_check_numeric(expr->expr);
		break;
	case JAF_BIT_NOT:
	case JAF_LOG_NOT:
		expr->value_type.type = jaf_type_check_int(expr->expr);
		break;
	case JAF_AMPERSAND:
		ERROR("Function types not supported");
	default:
		ERROR("Unhandled unary operator");
	}
}

static void jaf_check_types_binary(struct jaf_env *env, struct jaf_expression *expr)
{
	jaf_derive_types(env, expr->lhs);
	jaf_derive_types(env, expr->rhs);

	switch (expr->op) {
	// real ops
	case JAF_MULTIPLY:
	case JAF_DIVIDE:
	case JAF_PLUS:
	case JAF_MINUS:
		expr->value_type.type = jaf_merge_types(jaf_type_check_numeric(expr->lhs), jaf_type_check_numeric(expr->rhs));
		break;
	// integer ops
	case JAF_REMAINDER:
	case JAF_LSHIFT:
	case JAF_RSHIFT:
	case JAF_BIT_AND:
	case JAF_BIT_XOR:
	case JAF_BIT_IOR:
		jaf_type_check_int(expr->lhs);
		jaf_type_check_int(expr->rhs);
		expr->value_type.type = JAF_INT;
		break;
	// comparison ops
	case JAF_LT:
	case JAF_GT:
	case JAF_LTE:
	case JAF_GTE:
	case JAF_EQ:
		jaf_type_check_numeric(expr->lhs);
		jaf_type_check_numeric(expr->rhs);
		expr->value_type.type = JAF_INT;
		break;
	// boolean ops
	case JAF_LOG_AND:
	case JAF_LOG_OR:
		jaf_type_check_int(expr->lhs);
		jaf_type_check_int(expr->rhs);
		expr->value_type.type = JAF_INT;
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
		jaf_check_types_lvalue(env, expr->lhs);
		// FIXME: need to coerce types (?)
		jaf_check_type(expr->rhs, &expr->lhs->value_type);
		expr->value_type.type = expr->lhs->value_type.type;
		break;
	default:
		ERROR("Unhandled binary operator");
	}
}

static void jaf_check_types_ternary(struct jaf_env *env, struct jaf_expression *expr)
{
	jaf_derive_types(env, expr->condition);
	jaf_derive_types(env, expr->consequent);
	jaf_derive_types(env, expr->alternative);

	TYPE_CHECK(expr->condition, JAF_INT);
	if (!jaf_type_equal(&expr->consequent->value_type, &expr->alternative->value_type)) {
		// TODO: better error message
		ERROR("Mismatched types in conditional expression");
	}

	jaf_copy_type(&expr->value_type, &expr->consequent->value_type);
}

static void ain_to_jaf_type(struct jaf_type_specifier *dst, struct ain_type *src)
{
	switch (src->data) {
	case AIN_VOID:
		dst->type = JAF_VOID;
		break;
	case AIN_INT:
		dst->type = JAF_INT;
		break;
	case AIN_FLOAT:
		dst->type = JAF_FLOAT;
		break;
	case AIN_STRING:
		dst->type = JAF_STRING;
		break;
	case AIN_STRUCT:
		dst->type = JAF_STRUCT;
		dst->struct_no = src->struc;
		break;
	default:
		ERROR("Unsupported variable type");
	}
}

static struct ain_variable *jaf_scope_lookup(struct jaf_env *env, char *name, int *var_no)
{
	for (size_t i = 0; i < env->nr_locals; i++) {
		if (!strcmp(env->locals[i]->name, name)) {
			*var_no = i;
			return env->locals[i];
		}
	}
	return NULL;
}

struct ain_variable *jaf_env_lookup(struct jaf_env *env, struct string *name, int *var_no)
{
	char *u = encode_text_to_input_format(name->text);
	struct jaf_env *scope = env;
        while (scope) {
		struct ain_variable *v = jaf_scope_lookup(scope, u, var_no);
		if (v) {
			free(u);
			return v;
		}
		scope = scope->parent;
	}

	int no = ain_get_global_no(env->ain, u);
	free(u);

	if (no >= 0) {
		*var_no = no;
		return &env->ain->globals[no];
	}

	return NULL;
}

static void jaf_check_types_identifier(struct jaf_env *env, struct jaf_expression *expr)
{
	int var_no;
	struct ain_variable *v = jaf_env_lookup(env, expr->ident.name, &var_no);
	if (v) {
		ain_to_jaf_type(&expr->value_type, &v->type);
		expr->ident.var_type = v->var_type;
		expr->ident.var_no = var_no;
		return;
	}

	char *u = encode_text_to_input_format(expr->s->text);
	int func_no = ain_get_function_no(env->ain, u);
	free(u);
	if (func_no < 0)
		ERROR("Undefined variable: %s", expr->s->text);

	expr->value_type.type = JAF_FUNCTION;
	expr->value_type.func_no = func_no;
}

static void jaf_check_types_funcall(struct jaf_env *env, struct jaf_expression *expr)
{
	jaf_derive_types(env, expr->call.fun);
	TYPE_CHECK(expr->call.fun, JAF_FUNCTION);

	int func_no = expr->call.fun->value_type.func_no;
	assert(func_no >= 0 && func_no < env->ain->nr_functions);
	ain_to_jaf_type(&expr->value_type, &env->ain->functions[func_no].return_type);
}

static void jaf_check_types_member(struct jaf_env *env, struct jaf_expression *expr)
{
	jaf_derive_types(env, expr->member.struc);
	TYPE_CHECK(expr->member.struc, JAF_STRUCT);

	char *u = encode_text_to_input_format(expr->member.name->text);
	struct ain_struct *s = &env->ain->structures[expr->member.struc->value_type.struct_no];
	for (int i = 0; i < s->nr_members; i++) {
		if (!strcmp(s->members[i].name, u)) {
			ain_to_jaf_type(&expr->value_type, &s->members[i].type);
			break;
		}
	}
	free(u);
}

void jaf_derive_types(struct jaf_env *env, struct jaf_expression *expr)
{
	switch (expr->type) {
	case JAF_EXP_VOID:
		expr->value_type.type = JAF_VOID;
		break;
	case JAF_EXP_INT:
		expr->value_type.type = JAF_INT;
		break;
	case JAF_EXP_FLOAT:
		expr->value_type.type = JAF_FLOAT;
		break;
	case JAF_EXP_STRING:
		expr->value_type.type = JAF_STRING;
		break;
	case JAF_EXP_IDENTIFIER:
		jaf_check_types_identifier(env, expr);
		break;
	case JAF_EXP_UNARY:
		jaf_check_types_unary(env, expr);
		break;
	case JAF_EXP_BINARY:
		jaf_check_types_binary(env, expr);
		break;
	case JAF_EXP_TERNARY:
		jaf_check_types_ternary(env, expr);
		break;
	case JAF_EXP_FUNCALL:
		jaf_check_types_funcall(env, expr);
		break;
	case JAF_EXP_CAST:
		expr->value_type.type = expr->cast.type;
		break;
	case JAF_EXP_MEMBER:
		jaf_check_types_member(env, expr);
		break;
	}
}

void jaf_check_type(struct jaf_expression *expr, struct jaf_type_specifier *type)
{
	if (!jaf_type_equal(&expr->value_type, type)) {
		// treat ints and floats as interchangable
		if (expr->value_type.type == JAF_INT && type->type == JAF_FLOAT)
			return;
		if (expr->value_type.type == JAF_FLOAT && type->type == JAF_INT)
			return;
		TYPE_ERROR(expr, type->type);
	}
}