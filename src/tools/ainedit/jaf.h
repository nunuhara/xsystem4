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

#ifndef AINEDIT_JAF_H
#define AINEDIT_JAF_H

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
	JAF_FUNCTION,
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
	//JAF_EXP_SUBSCRIPT,
	JAF_EXP_FUNCALL,
	JAF_EXP_CAST,
	//JAF_EXP_MEMBER,
	//JAF_EXP_SEQ,
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

struct jaf_expression_list {
	size_t nr_items;
	struct jaf_expression **items;
};

struct jaf_type_specifier {
	enum jaf_type type;
	unsigned qualifiers;
	struct string *name;
	struct jaf_block *def;
	union {
		int struct_no;
		int func_no;
	};
};

struct jaf_expression {
	enum jaf_expression_type type;
	enum jaf_operator op;
	struct jaf_type_specifier value_type;
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
		// cast
		struct {
			enum jaf_type type;
			struct jaf_expression *expr;
		} cast;
		// function call
		struct {
			struct jaf_expression *fun;
			struct jaf_expression_list *args;
			int func_no;
		} call;
	};
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

struct jaf_function_declarator {
	struct string *name;
	struct jaf_block *params;
};

enum block_item_kind {
	JAF_DECLARATION,
	JAF_FUNDECL,
	JAF_STMT_LABELED,
	JAF_STMT_COMPOUND,
	JAF_STMT_EXPRESSION,
	JAF_STMT_IF,
	JAF_STMT_SWITCH,
	JAF_STMT_WHILE,
	JAF_STMT_DO_WHILE,
	JAF_STMT_FOR,
	JAF_STMT_GOTO,
	JAF_STMT_CONTINUE,
	JAF_STMT_BREAK,
	JAF_STMT_RETURN,
	JAF_STMT_CASE,
	JAF_STMT_DEFAULT
};

struct jaf_declaration {
	struct string *name;
	struct jaf_type_specifier *type;
	size_t array_rank;
	size_t *array_dims;
	union {
		struct {
			struct jaf_expression *init;
			int var_no;
		};
		struct {
			struct jaf_block *params;
			struct jaf_block *body;
			int func_no;
		};
	};
};

// declaration or statement
struct jaf_block_item {
	enum block_item_kind kind;
	union {
		struct jaf_declaration decl;
		struct {
			struct string *name;
			struct jaf_block_item *stmt;
		} label;
		struct {
			struct jaf_expression *expr;
			struct jaf_block_item *stmt;
		} swi_case;
		struct jaf_block *block;
		struct jaf_expression *expr;
		struct {
			struct jaf_expression *test;
			struct jaf_block_item *consequent;
			struct jaf_block_item *alternative;
		} cond;
		struct {
			struct jaf_expression *expr;
			struct jaf_block *body;
		} swi;
		struct {
			struct jaf_expression *test;
			struct jaf_block_item *body;
		} while_loop;
		struct {
			struct jaf_block *init;
			struct jaf_expression *test;
			struct jaf_expression *after;
			struct jaf_block_item *body;
		} for_loop;
		struct string *target; // goto
	};
};

struct jaf_block {
	size_t nr_items;
	struct jaf_block_item **items;
};

struct jaf_env {
	struct ain *ain;
	struct jaf_env *parent;
	int func_no;
	size_t nr_locals;
	struct ain_variable **locals;
};

struct jaf_expression *jaf_integer(int i);
struct jaf_expression *jaf_parse_integer(struct string *text);
struct jaf_expression *jaf_float(float f);
struct jaf_expression *jaf_parse_float(struct string *text);
struct jaf_expression *jaf_string(struct string *text);
struct jaf_expression *jaf_identifier(struct string *name);
struct jaf_expression *jaf_unary_expr(enum jaf_operator op, struct jaf_expression *expr);
struct jaf_expression *jaf_binary_expr(enum jaf_operator op, struct jaf_expression *lhs, struct jaf_expression *rhs);
struct jaf_expression *jaf_ternary_expr(struct jaf_expression *test, struct jaf_expression *cons, struct jaf_expression *alt);
struct jaf_expression *jaf_seq_expr(struct jaf_expression *head, struct jaf_expression *tail);
struct jaf_expression *jaf_cast_expression(enum jaf_type type, struct jaf_expression *expr);
struct jaf_expression *jaf_function_call(struct jaf_expression *fun, struct jaf_expression_list *args);

struct jaf_expression_list *jaf_args(struct jaf_expression_list *head, struct jaf_expression *tail);

struct jaf_type_specifier *jaf_type(enum jaf_type type);
struct jaf_type_specifier *jaf_struct(struct string *name, struct jaf_block *fields);
struct jaf_type_specifier *jaf_typedef(struct string *name);
void jaf_copy_type(struct jaf_type_specifier *dst, struct jaf_type_specifier *src);

struct jaf_declarator *jaf_declarator(struct string *name);
struct jaf_declarator_list *jaf_declarators(struct jaf_declarator_list *head, struct jaf_declarator *tail);

struct jaf_block *jaf_parameter(struct jaf_type_specifier *type, struct jaf_declarator *declarator);
struct jaf_function_declarator *jaf_function_declarator(struct string *name, struct jaf_block *params);
struct jaf_block *jaf_function(struct jaf_type_specifier *type, struct jaf_function_declarator *decl, struct jaf_block *body);

struct jaf_block *jaf_declaration(struct jaf_type_specifier *type, struct jaf_declarator_list *declarators);
struct jaf_block *jaf_type_declaration(struct jaf_type_specifier *type);
struct jaf_block *jaf_merge_blocks(struct jaf_block *head, struct jaf_block *tail);

struct jaf_block *jaf_block(struct jaf_block_item *item);
struct jaf_block_item *jaf_compound_statement(struct jaf_block *block);
struct jaf_block_item *jaf_label_statement(struct string *label, struct jaf_block_item *stmt);
struct jaf_block_item *jaf_case_statement(struct jaf_expression *expr, struct jaf_block_item *stmt);
struct jaf_block_item *jaf_expression_statement(struct jaf_expression *expr);
struct jaf_block_item *jaf_if_statement(struct jaf_expression *test, struct jaf_block_item *cons, struct jaf_block_item *alt);
struct jaf_block_item *jaf_switch_statement(struct jaf_expression *expr, struct jaf_block *body);
struct jaf_block_item *jaf_while_loop(struct jaf_expression *test, struct jaf_block_item *body);
struct jaf_block_item *jaf_do_while_loop(struct jaf_expression *test, struct jaf_block_item *body);
struct jaf_block_item *jaf_for_loop(struct jaf_block *init, struct jaf_block_item *test, struct jaf_expression *after, struct jaf_block_item *body);
struct jaf_block_item *jaf_goto(struct string *target);
struct jaf_block_item *jaf_continue(void);
struct jaf_block_item *jaf_break(void);
struct jaf_block_item *jaf_return(struct jaf_expression *expr);

void jaf_free_expr(struct jaf_expression *expr);
void jaf_free_block(struct jaf_block *block);

// jaf_compile.c
struct ain *jaf_ain_out;
struct jaf_block *jaf_toplevel;
void jaf_compile(struct ain *out, const char *path);
void jaf_define_struct(struct ain *ain, struct jaf_type_specifier *type);
struct ain_variable *jaf_env_lookup(struct jaf_env *env, struct string *name);

// jaf_eval.c
struct jaf_expression *jaf_simplify(struct jaf_expression *in);

// jaf_types.c
void jaf_derive_types(struct jaf_env *env, struct jaf_expression *expr);
void jaf_check_type(struct jaf_expression *expr, struct jaf_type_specifier *type);

#endif /* AINEDIT_JAF_H */
