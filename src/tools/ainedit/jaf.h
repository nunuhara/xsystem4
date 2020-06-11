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

#ifndef AINEDIT_JAF_AST_H
#define AINEDIT_JAF_AST_H

#include <stdbool.h>
#include <stdio.h>
#include "system4/ain.h"

struct string;

enum jaf_type {
	JAF_VOID,
	JAF_INT,
	JAF_FLOAT,
	JAF_STRING,
	JAF_STRUCT,
	JAF_ENUM,
	JAF_TYPEDEF,
};

enum jaf_type_qualifier {
	JAF_QUAL_CONST = 1,
	JAF_QUAL_REF   = 2
};

enum jaf_expression_type {
	JAF_EXP_VOID = 0,
	JAF_EXP_INT,
	JAF_EXP_FLOAT,
	JAF_EXP_STRING,
	JAF_EXP_IDENTIFIER,
	JAF_EXP_UNARY,
	JAF_EXP_BINARY,
	JAF_EXP_TERNARY,
};

enum jaf_operator {
	JAF_NO_OPERATOR = 0,
	JAF_AMPERSAND,
	JAF_UNARY_PLUS,
	JAF_UNARY_MINUS,
	JAF_BIT_NOT,
	JAF_LOG_NOT,
	JAF_PRE_INC,
	JAF_PRE_DEC,
	JAF_POST_INC,
	JAF_POST_DEC,

	JAF_MULTIPLY,
	JAF_DIVIDE,
	JAF_REMAINDER,
	JAF_PLUS,
	JAF_MINUS,
	JAF_LSHIFT,
	JAF_RSHIFT,
	JAF_LT,
	JAF_GT,
	JAF_LTE,
	JAF_GTE,
	JAF_EQ,
	JAF_BIT_AND,
	JAF_BIT_XOR,
	JAF_BIT_IOR,
	JAF_LOG_AND,
	JAF_LOG_OR,
	JAF_ASSIGN,
	JAF_MUL_ASSIGN,
	JAF_DIV_ASSIGN,
	JAF_MOD_ASSIGN,
	JAF_ADD_ASSIGN,
	JAF_SUB_ASSIGN,
	JAF_LSHIFT_ASSIGN,
	JAF_RSHIFT_ASSIGN,
	JAF_AND_ASSIGN,
	JAF_XOR_ASSIGN,
	JAF_OR_ASSIGN,
	JAF_REF_ASSIGN,
};

struct jaf_expression {
	enum jaf_expression_type type;
	enum jaf_operator op;
	enum jaf_type derived_type;
	union {
		int i;
		float f;
		struct string *s;
		// unary operators
		struct jaf_expression *expr;
		// binary operators
		struct {
			struct jaf_expression *lhs;
			struct jaf_expression *rhs;
		};
		// ternary operator
		struct {
			struct jaf_expression *condition;
			struct jaf_expression *consequent;
			struct jaf_expression *alternative;
		};
	};
};

struct jaf_type_specifier {
	enum jaf_type type;
	unsigned qualifiers;
	struct string *name;
	struct jaf_declaration_list *def;
	int struct_no;
};

struct jaf_declarator {
	struct string *name;
	struct jaf_expression *init;
	size_t array_rank;
	size_t *array_dims;
};

struct jaf_declarator_list {
	size_t nr_decls;
	struct jaf_declarator **decls;
};

struct jaf_function {
	// TODO
};

struct jaf_declaration {
	bool function;
	struct string *name;
	struct jaf_type_specifier *type;
	size_t array_rank;
	size_t *array_dims;
	struct jaf_expression *init;
	struct jaf_function *fun;
	int var_no;
};

struct jaf_declaration_list {
	size_t nr_decls;
	struct jaf_declaration **decls;
};

struct jaf_expression *jaf_integer(struct string *text);
struct jaf_expression *jaf_float(struct string *text);
struct jaf_expression *jaf_string(struct string *text);
struct jaf_expression *jaf_identifier(struct string *name);
struct jaf_expression *jaf_unary_expr(enum jaf_operator op, struct jaf_expression *expr);
struct jaf_expression *jaf_binary_expr(enum jaf_operator op, struct jaf_expression *lhs, struct jaf_expression *rhs);
struct jaf_expression *jaf_ternary_expr(struct jaf_expression *test, struct jaf_expression *cons, struct jaf_expression *alt);
struct jaf_expression *jaf_seq_expr(struct jaf_expression *head, struct jaf_expression *tail);

struct jaf_type_specifier *jaf_type(enum jaf_type type);
struct jaf_type_specifier *jaf_struct(struct string *name, struct jaf_declaration_list *fields);
struct jaf_type_specifier *jaf_typedef(struct string *name);

struct jaf_declarator *jaf_declarator(struct string *name);
struct jaf_declarator_list *jaf_declarators(struct jaf_declarator_list *head, struct jaf_declarator *tail);

struct jaf_declaration_list *jaf_declaration(struct jaf_type_specifier *type, struct jaf_declarator_list *declarators);
struct jaf_declaration_list *jaf_type_declaration(struct jaf_type_specifier *type);
struct jaf_declaration_list *jaf_declarations(struct jaf_declaration_list *head, struct jaf_declaration_list *tail);

void jaf_free_expr(struct jaf_expression *expr);

// jaf_compile.c
struct ain *jaf_ain_out;
struct jaf_declaration_list *jaf_toplevel;
void jaf_compile(struct ain *out, const char *path);
void jaf_define_struct(struct ain *ain, struct jaf_type_specifier *type);

// jaf_eval.c
struct jaf_expression *jaf_simplify(struct jaf_expression *in);
struct jaf_expression *jaf_compute_constexpr(struct jaf_expression *in);

// jaf_types.c
void jaf_check_types(struct jaf_expression *expr, struct jaf_type_specifier *type);

#endif /* AINEDIT_JAF_AST_H */
