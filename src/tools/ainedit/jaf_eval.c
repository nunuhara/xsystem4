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
#include "system4/string.h"
#include "jaf.h"

struct jaf_expression *jaf_simplify(struct jaf_expression *in);

static struct jaf_expression *jaf_simplify_negation(struct jaf_expression *expr)
{
	if (expr->type == JAF_EXP_INT) {
		expr->i = -expr->i;
		return expr;
	}
	if (expr->type == JAF_EXP_FLOAT) {
		expr->f = -expr->f;
		return expr;
	}

	return jaf_unary_expr(JAF_UNARY_MINUS, expr);
}

static struct jaf_expression *jaf_simplify_bitnot(struct jaf_expression *expr)
{
	if (expr->type == JAF_EXP_INT) {
		expr->i = ~expr->i;
		return expr;
	}

	return jaf_unary_expr(JAF_BIT_NOT, expr);
}

static struct jaf_expression *jaf_simplify_lognot(struct jaf_expression *expr)
{
	if (expr->type == JAF_EXP_INT) {
		expr->i = !expr->i;
		return expr;
	}

	return jaf_unary_expr(JAF_LOG_NOT, expr);
}

static struct jaf_expression *jaf_simplify_unary(struct jaf_expression *in)
{
	struct jaf_expression *out = jaf_simplify(in->expr);
	switch (in->op) {
	case JAF_UNARY_PLUS:
		return out;
	case JAF_UNARY_MINUS:
		return jaf_simplify_negation(out);
	case JAF_BIT_NOT:
		return jaf_simplify_bitnot(out);
	case JAF_LOG_NOT:
		return jaf_simplify_lognot(out);
	case JAF_AMPERSAND:
	case JAF_PRE_INC:
	case JAF_PRE_DEC:
	case JAF_POST_INC:
	case JAF_POST_DEC:
		return jaf_unary_expr(in->op, out);
	default:
		break;
	}
	ERROR("Invalid unary operator");
}

static void jaf_normalize_for_arithmetic(struct jaf_expression *lhs, struct jaf_expression *rhs)
{
	if (lhs->type == JAF_EXP_FLOAT && rhs->type == JAF_EXP_INT) {
		rhs->type = JAF_EXP_FLOAT;
		rhs->f = rhs->i;
	} else if (lhs->type == JAF_EXP_INT && rhs->type == JAF_EXP_FLOAT) {
		lhs->type = JAF_EXP_FLOAT;
		lhs->f = lhs->i;
	}
}

#define SIMPLIFY_ARITHMETIC_FUN(name, op_name, op)			\
	static struct jaf_expression *name(struct jaf_expression *lhs, struct jaf_expression *rhs) \
	{								\
		jaf_normalize_for_arithmetic(lhs, rhs);			\
		if (lhs->type == JAF_EXP_INT && rhs->type == JAF_EXP_INT) { \
			lhs->i = lhs->i op rhs->i;			\
			free(rhs);					\
			return lhs;					\
		}							\
		if (lhs->type == JAF_EXP_FLOAT && rhs->type == JAF_EXP_FLOAT) { \
			lhs->f = lhs->f op rhs->f;			\
			free(rhs);					\
			return lhs;					\
		}							\
		return jaf_binary_expr(op_name, lhs, rhs);		\
	}

#define SIMPLIFY_INTEGER_FUN(name, op_name, op)				\
	static struct jaf_expression *name(struct jaf_expression *lhs, struct jaf_expression *rhs) \
	{								\
		jaf_normalize_for_arithmetic(lhs, rhs);			\
		if (lhs->type == JAF_EXP_INT && rhs->type == JAF_EXP_INT) { \
			lhs->i = lhs->i op rhs->i;			\
			free(rhs);					\
			return lhs;					\
		}							\
		return jaf_binary_expr(op_name, lhs, rhs);		\
	}

SIMPLIFY_ARITHMETIC_FUN(jaf_simplify_multiply,  JAF_MULTIPLY,  *)
SIMPLIFY_ARITHMETIC_FUN(jaf_simplify_divide,    JAF_DIVIDE,    /)
SIMPLIFY_INTEGER_FUN   (jaf_simplify_remainder, JAF_REMAINDER, %)
SIMPLIFY_ARITHMETIC_FUN(jaf_simplify_plus,      JAF_PLUS,      +) // TODO: string concatentation
SIMPLIFY_ARITHMETIC_FUN(jaf_simplify_minus,     JAF_MINUS,     -)
SIMPLIFY_INTEGER_FUN   (jaf_simplify_lshift,    JAF_LSHIFT,    <<)
SIMPLIFY_INTEGER_FUN   (jaf_simplify_rshift,    JAF_RSHIFT,    >>)
SIMPLIFY_ARITHMETIC_FUN(jaf_simplify_lt,        JAF_LT,        <)
SIMPLIFY_ARITHMETIC_FUN(jaf_simplify_gt,        JAF_GT,        >)
SIMPLIFY_ARITHMETIC_FUN(jaf_simplify_lte,       JAF_LTE,       <=)
SIMPLIFY_ARITHMETIC_FUN(jaf_simplify_gte,       JAF_GTE,       >=)
SIMPLIFY_ARITHMETIC_FUN(jaf_simplify_eq,        JAF_EQ,        ==)
SIMPLIFY_INTEGER_FUN   (jaf_simplify_bitand,    JAF_BIT_AND,   &)
SIMPLIFY_INTEGER_FUN   (jaf_simplify_bitxor,    JAF_BIT_XOR,   ^)
SIMPLIFY_INTEGER_FUN   (jaf_simplify_bitior,    JAF_BIT_IOR,   |)
SIMPLIFY_INTEGER_FUN   (jaf_simplify_logand,    JAF_LOG_AND,   &&)
SIMPLIFY_INTEGER_FUN   (jaf_simplify_logor,     JAF_LOG_OR,    ||)

static struct jaf_expression *jaf_simplify_binary(struct jaf_expression *in)
{
	enum jaf_operator op = in->op;
	struct jaf_expression *lhs = jaf_simplify(in->lhs);
	struct jaf_expression *rhs = jaf_simplify(in->rhs);
	free(in);

	switch (op) {
	case JAF_MULTIPLY:
		return jaf_simplify_multiply(lhs, rhs);
	case JAF_DIVIDE:
		return jaf_simplify_divide(lhs, rhs);
	case JAF_REMAINDER:
		return jaf_simplify_remainder(lhs, rhs);
	case JAF_PLUS:
		return jaf_simplify_plus(lhs, rhs);
	case JAF_MINUS:
		return jaf_simplify_minus(lhs, rhs);
	case JAF_LSHIFT:
		return jaf_simplify_lshift(lhs, rhs);
	case JAF_RSHIFT:
		return jaf_simplify_rshift(lhs, rhs);
	case JAF_LT:
		return jaf_simplify_lt(lhs, rhs);
	case JAF_GT:
		return jaf_simplify_gt(lhs, rhs);
	case JAF_LTE:
		return jaf_simplify_lte(lhs, rhs);
	case JAF_GTE:
		return jaf_simplify_gte(lhs, rhs);
	case JAF_EQ:
		return jaf_simplify_eq(lhs, rhs);
	case JAF_BIT_AND:
		return jaf_simplify_bitand(lhs, rhs);
	case JAF_BIT_XOR:
		return jaf_simplify_bitxor(lhs, rhs);
	case JAF_BIT_IOR:
		return jaf_simplify_bitior(lhs, rhs);
	case JAF_LOG_AND:
		return jaf_simplify_logand(lhs, rhs);
	case JAF_LOG_OR:
		return jaf_simplify_logor(lhs, rhs);
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
		return jaf_binary_expr(op, lhs, rhs);
	default:
		ERROR("Invalid binary operator");
	}
}

struct jaf_expression *jaf_simplify_ternary(struct jaf_expression *in)
{
	in->condition = jaf_simplify(in->condition);
	in->consequent = jaf_simplify(in->consequent);
	in->alternative = jaf_simplify(in->alternative);

	if (in->condition->type == JAF_EXP_INT) {
		if (in->condition->i) {
			jaf_free_expr(in->condition);
			jaf_free_expr(in->alternative);
			free(in);
			return in->consequent;
		} else {
			jaf_free_expr(in->condition);
			jaf_free_expr(in->consequent);
			free(in);
			return in->alternative;
		}
	}

	return in;
}

/*
 * Simplify an expression by evaluating the constant parts.
 */
struct jaf_expression *jaf_simplify(struct jaf_expression *in)
{
	switch (in->type) {
	case JAF_EXP_VOID:
	case JAF_EXP_INT:
	case JAF_EXP_FLOAT:
	case JAF_EXP_STRING:
	case JAF_EXP_IDENTIFIER:
		return in;
	case JAF_EXP_UNARY:
		return jaf_simplify_unary(in);
	case JAF_EXP_BINARY:
		return jaf_simplify_binary(in);
	case JAF_EXP_TERNARY:
		return jaf_simplify_ternary(in);
	}
	ERROR("Invalid expression type");
}

struct jaf_expression *jaf_compute_constexpr(struct jaf_expression *in)
{
	struct jaf_expression *out = jaf_simplify(in);
	switch (out->type) {
	case JAF_EXP_INT:
	case JAF_EXP_FLOAT:
	case JAF_EXP_STRING:
		return out;
	default:
		return NULL;
	}
}

