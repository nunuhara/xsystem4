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
#include <errno.h>
#include "system4.h"
#include "system4/string.h"
#include "jaf.h"

static struct jaf_expression *jaf_expr(enum jaf_expression_type type, enum jaf_operator op)
{
	struct jaf_expression *e = xcalloc(1, sizeof(struct jaf_expression));
	e->type = type;
	e->op = op;
	return e;
}

struct jaf_expression *jaf_integer(struct string *text)
{
	struct jaf_expression *e = jaf_expr(JAF_EXP_INT, 0);
	char *endptr;
	errno = 0;
	e->i = strtol(text->text, &endptr, 0);
	if (errno || *endptr != '\0')
		ERROR("Invalid integer constant");
	free_string(text);
	return e;
}

struct jaf_expression *jaf_float(struct string *text)
{
	struct jaf_expression *e = jaf_expr(JAF_EXP_FLOAT, 0);
	char *endptr;
	errno = 0;
	e->f = strtof(text->text, &endptr);
	if (errno || *endptr != '\0')
		ERROR("Invalid floating point constant");
	free_string(text);
	return e;
}

struct jaf_expression *jaf_string(struct string *text)
{
	struct jaf_expression *e = jaf_expr(JAF_EXP_STRING, 0);
	e->s = text;
	return e;
}

struct jaf_expression *jaf_identifier(struct string *name)
{
	struct jaf_expression *e = jaf_expr(JAF_EXP_IDENTIFIER, 0);
	e->s = name;
	return e;
}

struct jaf_expression *jaf_unary_expr(enum jaf_operator op, struct jaf_expression *expr)
{
	struct jaf_expression *e = jaf_expr(JAF_EXP_UNARY, op);
	e->expr = expr;
	return e;
}

struct jaf_expression *jaf_binary_expr(enum jaf_operator op, struct jaf_expression *lhs, struct jaf_expression *rhs)
{
	struct jaf_expression *e = jaf_expr(JAF_EXP_BINARY, op);
	e->lhs = lhs;
	e->rhs = rhs;
	return e;
}

struct jaf_expression *jaf_ternary_expr(struct jaf_expression *test, struct jaf_expression *cons, struct jaf_expression *alt)
{
	struct jaf_expression *e = jaf_expr(JAF_EXP_TERNARY, 0);
	e->condition = test;
	e->consequent = cons;
	e->alternative = alt;
	return e;
}

struct jaf_expression *jaf_seq_expr(struct jaf_expression *head, struct jaf_expression *tail)
{
	ERROR("Sequence expressions not supported");
}

struct jaf_type_specifier *jaf_type(enum jaf_type type)
{
	struct jaf_type_specifier *p = xcalloc(1, sizeof(struct jaf_type_specifier));
	p->type = type;
	p->struct_no = -1;
	return p;
}

struct jaf_type_specifier *jaf_struct(struct string *name, struct jaf_declaration_list *fields)
{
	struct jaf_type_specifier *p = jaf_type(JAF_STRUCT);
	p->name = name;
	p->def = fields;
	return p;
}

struct jaf_type_specifier *jaf_typedef(struct string *name)
{
	struct jaf_type_specifier *p = jaf_type(JAF_TYPEDEF);
	p->name = name;
	return p;
}

struct jaf_declarator *jaf_declarator(struct string *name)
{
	struct jaf_declarator *d = xcalloc(1, sizeof(struct jaf_declarator));
	d->name = name;
	return d;
}

struct jaf_declarator_list *jaf_declarators(struct jaf_declarator_list *head, struct jaf_declarator *tail)
{
	if (!head) {
		head = xcalloc(1, sizeof(struct jaf_declarator_list));
	}

	head->decls = xrealloc_array(head->decls, head->nr_decls, head->nr_decls+1, sizeof(struct jaf_declarator*));
	head->decls[head->nr_decls++] = tail;
	return head;
}

struct jaf_declaration_list *jaf_declaration(struct jaf_type_specifier *type, struct jaf_declarator_list *declarators)
{
	struct jaf_declaration_list *decls = xcalloc(1, sizeof(struct jaf_declaration_list));
	decls->nr_decls = declarators->nr_decls;
	decls->decls = xcalloc(declarators->nr_decls, sizeof(struct jaf_declaration*));
	for (size_t i = 0; i < declarators->nr_decls; i++) {
		struct jaf_declaration *decl = xcalloc(1, sizeof(struct jaf_declaration));
		decl->function = false;
		decl->name = declarators->decls[i]->name;
		decl->type = type;
		decl->init = declarators->decls[i]->init;
		decls->decls[i] = decl;

		free(declarators->decls[i]);
	}
	free(declarators);
	return decls;
}

struct jaf_declaration_list *jaf_type_declaration(struct jaf_type_specifier *type)
{
	struct jaf_declaration_list *decls = xcalloc(1, sizeof(struct jaf_declaration_list));
	decls->nr_decls = 1;
	decls->decls = xcalloc(1, sizeof(struct jaf_declaration*));
	decls->decls[0] = xcalloc(1, sizeof(struct jaf_declaration));
	decls->decls[0]->type = type;
	return decls;
}


struct jaf_declaration_list *jaf_declarations(struct jaf_declaration_list *head, struct jaf_declaration_list *tail)
{
	if (!head)
		return tail;

	size_t nr_decls = head->nr_decls + tail->nr_decls;
	head->decls = xrealloc_array(head->decls, head->nr_decls, nr_decls, sizeof(struct jaf_toplevel_decl*));

	for (size_t i = 0; i < tail->nr_decls; i++) {
		head->decls[head->nr_decls+i] = tail->decls[i];
	}
	head->nr_decls = nr_decls;

	free(tail);
	return head;
}

void jaf_free_expr(struct jaf_expression *expr)
{
	if (expr->type == JAF_EXP_UNARY) {
		jaf_free_expr(expr->expr);
	} else if (expr->type == JAF_EXP_BINARY) {
		jaf_free_expr(expr->lhs);
		jaf_free_expr(expr->rhs);
	} else if (expr->type == JAF_EXP_TERNARY) {
		jaf_free_expr(expr->condition);
		jaf_free_expr(expr->consequent);
		jaf_free_expr(expr->alternative);
	}
	free(expr);
}
