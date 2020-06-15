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
#include <assert.h>
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

struct jaf_expression *jaf_integer(int i)
{
	struct jaf_expression *e = jaf_expr(JAF_EXP_INT, 0);
	e->value_type.type = JAF_INT;
	e->i = i;
	return e;
}

struct jaf_expression *jaf_parse_integer(struct string *text)
{
	char *endptr;
	errno = 0;
	int i = strtol(text->text, &endptr, 0);
	if (errno || *endptr != '\0')
		ERROR("Invalid integer constant");
	free_string(text);
	return jaf_integer(i);
}

struct jaf_expression *jaf_float(float f)
{
	struct jaf_expression *e = jaf_expr(JAF_EXP_FLOAT, 0);
	e->value_type.type = JAF_FLOAT;
	e->f = f;
	return e;
}

struct jaf_expression *jaf_parse_float(struct string *text)
{
	char *endptr;
	errno = 0;
	float f = strtof(text->text, &endptr);
	if (errno || *endptr != '\0')
		ERROR("Invalid floating point constant");
	free_string(text);
	return jaf_float(f);
}

struct jaf_expression *jaf_string(struct string *text)
{
	struct jaf_expression *e = jaf_expr(JAF_EXP_STRING, 0);
	e->value_type.type = JAF_STRING;
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

struct jaf_expression *jaf_function_call(struct jaf_expression *fun, struct jaf_expression_list *args)
{
	struct jaf_expression *e = jaf_expr(JAF_EXP_FUNCALL, 0);
	e->call.fun = fun;
	e->call.args = args;
	return e;
}

struct jaf_expression_list *jaf_args(struct jaf_expression_list *head, struct jaf_expression *tail)
{
	if (!head) {
		head = xcalloc(1, sizeof(struct jaf_expression_list));
	}
	head->items = xrealloc_array(head->items, head->nr_items, head->nr_items+1, sizeof(struct jaf_expression*));
	head->items[head->nr_items++] = tail;
	return head;
}

struct jaf_expression *jaf_cast_expression(enum jaf_type type, struct jaf_expression *expr)
{
	struct jaf_expression *e = jaf_expr(JAF_EXP_CAST, 0);
	e->cast.type = type;
	e->cast.expr = expr;
	return e;
}

struct jaf_expression *jaf_member_expr(struct jaf_expression *struc, struct string *name)
{
	struct jaf_expression *e = jaf_expr(JAF_EXP_MEMBER, 0);
	e->member.struc = struc;
	e->member.name = name;
	return e;
}

struct jaf_type_specifier *jaf_type(enum jaf_type type)
{
	struct jaf_type_specifier *p = xcalloc(1, sizeof(struct jaf_type_specifier));
	p->type = type;
	p->struct_no = -1;
	return p;
}

struct jaf_type_specifier *jaf_struct(struct string *name, struct jaf_block *fields)
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

void jaf_copy_type(struct jaf_type_specifier *dst, struct jaf_type_specifier *src)
{
	assert(!src->def);
	*dst = *src;
	if (src->name)
		dst->name = string_dup(src->name);
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

static void init_declaration(struct jaf_type_specifier *type, struct jaf_block_item *dst, struct jaf_declarator *src)
{
	dst->kind = JAF_DECLARATION;
	dst->decl.name = src->name;
	dst->decl.type = type;
	dst->decl.init = src->init;
	free(src);
}

struct jaf_block *jaf_parameter(struct jaf_type_specifier *type, struct jaf_declarator *declarator)
{
	struct jaf_block *p = xmalloc(sizeof(struct jaf_block));
	p->nr_items = 1;
	p->items = xmalloc(sizeof(struct jaf_block_item*));
	p->items[0] = xcalloc(1, sizeof(struct jaf_block_item));
	init_declaration(type, p->items[0], declarator);
	return p;
}

struct jaf_function_declarator *jaf_function_declarator(struct string *name, struct jaf_block *params)
{
	struct jaf_function_declarator *decl = xmalloc(sizeof(struct jaf_function_declarator));
	decl->name = name;
	decl->params = params;
	return decl;
}

struct jaf_block *jaf_function(struct jaf_type_specifier *type, struct jaf_function_declarator *decl, struct jaf_block *body)
{
	struct jaf_block *p = xmalloc(sizeof(struct jaf_block));
	p->nr_items = 1;
	p->items = xmalloc(sizeof(struct jaf_block_item*));
	p->items[0] = xcalloc(1, sizeof(struct jaf_block_item));
	p->items[0]->kind = JAF_FUNDECL;
	p->items[0]->decl.type = type;
	p->items[0]->decl.name = decl->name;
	p->items[0]->decl.params = decl->params;
	p->items[0]->decl.body = body;
	free(decl);
	return p;
}

struct jaf_block *jaf_declaration(struct jaf_type_specifier *type, struct jaf_declarator_list *declarators)
{
	struct jaf_block *decls = xcalloc(1, sizeof(struct jaf_block));
	decls->nr_items = declarators->nr_decls;
	decls->items = xcalloc(declarators->nr_decls, sizeof(struct jaf_block_item*));
	for (size_t i = 0; i < declarators->nr_decls; i++) {
		decls->items[i] = xcalloc(1, sizeof(struct jaf_block_item));
		init_declaration(type, decls->items[i], declarators->decls[i]);
	}
	free(declarators->decls);
	free(declarators);
	return decls;
}

struct jaf_block *jaf_type_declaration(struct jaf_type_specifier *type)
{
	struct jaf_block *decls = xcalloc(1, sizeof(struct jaf_block));
	decls->nr_items = 1;
	decls->items = xcalloc(1, sizeof(struct jaf_block_item*));
	decls->items[0] = xcalloc(1, sizeof(struct jaf_block_item));
	decls->items[0]->kind = JAF_DECLARATION;
	decls->items[0]->decl.type = type;
	return decls;
}

struct jaf_block *jaf_merge_blocks(struct jaf_block *head, struct jaf_block *tail)
{
	if (!head)
		return tail;

	size_t nr_decls = head->nr_items + tail->nr_items;
	head->items = xrealloc_array(head->items, head->nr_items, nr_decls, sizeof(struct jaf_toplevel_decl*));

	for (size_t i = 0; i < tail->nr_items; i++) {
		head->items[head->nr_items+i] = tail->items[i];
	}
	head->nr_items = nr_decls;

	free(tail->items);
	free(tail);
	return head;
}

struct jaf_block *jaf_block(struct jaf_block_item *item)
{
	struct jaf_block *block = xcalloc(1, sizeof(struct jaf_block));
	if (!item)
		return block;
	block->items = xmalloc(sizeof(struct jaf_block_item*));
	block->items[0] = item;
	block->nr_items = 1;
	return block;
}

static struct jaf_block_item *block_item(enum block_item_kind kind)
{
	struct jaf_block_item *item = xcalloc(1, sizeof(struct jaf_block_item));
	item->kind = kind;
	return item;
}

struct jaf_block_item *jaf_compound_statement(struct jaf_block *block)
{
	struct jaf_block_item *item = block_item(JAF_STMT_COMPOUND);
	item->block = block;
	return item;
}

struct jaf_block_item *jaf_label_statement(struct string *label, struct jaf_block_item *stmt)
{
	struct jaf_block_item *item = block_item(JAF_STMT_LABELED);
	item->label.name = label;
	item->label.stmt = stmt;
	return item;
}

struct jaf_block_item *jaf_case_statement(struct jaf_expression *expr, struct jaf_block_item *stmt)
{
	struct jaf_block_item *item = block_item(expr ? JAF_STMT_CASE : JAF_STMT_DEFAULT);
	item->swi_case.expr = expr;
	item->swi_case.stmt = stmt;
	return item;
}

struct jaf_block_item *jaf_expression_statement(struct jaf_expression *expr)
{
	struct jaf_block_item *item = block_item(JAF_STMT_EXPRESSION);
	item->expr = expr;
	return item;
}

struct jaf_block_item *jaf_if_statement(struct jaf_expression *test, struct jaf_block_item *cons, struct jaf_block_item *alt)
{
	struct jaf_block_item *item = block_item(JAF_STMT_IF);
	item->cond.test = test;
	item->cond.consequent = cons;
	item->cond.alternative = alt;
	return item;
}

struct jaf_block_item *jaf_switch_statement(struct jaf_expression *expr, struct jaf_block *body)
{
	struct jaf_block_item *item = block_item(JAF_STMT_SWITCH);
	item->swi.expr = expr;
	item->swi.body = body;
	return item;
}

struct jaf_block_item *jaf_while_loop(struct jaf_expression *test, struct jaf_block_item *body)
{
	struct jaf_block_item *item = block_item(JAF_STMT_WHILE);
	item->while_loop.test = test;
	item->while_loop.body = body;
	return item;
}

struct jaf_block_item *jaf_do_while_loop(struct jaf_expression *test, struct jaf_block_item *body)
{
	struct jaf_block_item *item = block_item(JAF_STMT_DO_WHILE);
	item->while_loop.test = test;
	item->while_loop.body = body;
	return item;
}

struct jaf_block_item *jaf_for_loop(struct jaf_block *init, struct jaf_block_item *test, struct jaf_expression *after, struct jaf_block_item *body)
{
	assert(test->kind == JAF_STMT_EXPRESSION);
	struct jaf_block_item *item = block_item(JAF_STMT_FOR);
	item->for_loop.init = init;
	item->for_loop.test = test->expr;
	item->for_loop.after = after;
	item->for_loop.body = body;
	return item;
}

struct jaf_block_item *jaf_goto(struct string *target)
{
	struct jaf_block_item *item = block_item(JAF_STMT_GOTO);
	item->target = target;
	return item;
}

struct jaf_block_item *jaf_continue(void)
{
	return block_item(JAF_STMT_CONTINUE);
}

struct jaf_block_item *jaf_break(void)
{
	return block_item(JAF_STMT_BREAK);
}

struct jaf_block_item *jaf_return(struct jaf_expression *expr)
{
	struct jaf_block_item *item = block_item(JAF_STMT_RETURN);
	item->expr = expr;
	return item;
}

void jaf_free_expr(struct jaf_expression *expr)
{
	if (!expr)
		return;
	switch (expr->type) {
	case JAF_EXP_VOID:
	case JAF_EXP_INT:
	case JAF_EXP_FLOAT:
		break;
	case JAF_EXP_STRING:
	case JAF_EXP_IDENTIFIER:
		free_string(expr->s);
		break;
	case JAF_EXP_UNARY:
		jaf_free_expr(expr->expr);
		break;
	case JAF_EXP_BINARY:
		jaf_free_expr(expr->lhs);
		jaf_free_expr(expr->rhs);
		break;
	case JAF_EXP_TERNARY:
		jaf_free_expr(expr->condition);
		jaf_free_expr(expr->consequent);
		jaf_free_expr(expr->alternative);
		break;
	case JAF_EXP_FUNCALL:
		jaf_free_expr(expr->call.fun);
		for (size_t i = 0; i < expr->call.args->nr_items; i++) {
			jaf_free_expr(expr->call.args->items[i]);
		}
		free(expr->call.args->items);
		free(expr->call.args);
		break;
	case JAF_EXP_CAST:
		jaf_free_expr(expr->cast.expr);
		break;
	case JAF_EXP_MEMBER:
		jaf_free_expr(expr->member.struc);
		free_string(expr->member.name);
		break;
	}
	free(expr);
}

void jaf_free_type_specifier(struct jaf_type_specifier *type)
{
	if (!type)
		return;
	if (type->name)
		free_string(type->name);
	jaf_free_block(type->def);
	free(type);
}

void jaf_free_block_item(struct jaf_block_item *item)
{
	if (!item)
		return;

	switch (item->kind) {
	case JAF_DECLARATION:
		if (item->decl.name)
			free_string(item->decl.name);
		jaf_free_type_specifier(item->decl.type);
		jaf_free_expr(item->decl.init);
		break;
	case JAF_FUNDECL:
		if (item->decl.name)
			free_string(item->decl.name);
		jaf_free_type_specifier(item->decl.type);
		jaf_free_block(item->decl.params);
		jaf_free_block(item->decl.body);
		break;
	case JAF_STMT_LABELED:
		free_string(item->label.name);
		jaf_free_block_item(item->label.stmt);
		break;
	case JAF_STMT_COMPOUND:
		jaf_free_block(item->block);
		break;
	case JAF_STMT_EXPRESSION:
		jaf_free_expr(item->expr);
		break;
	case JAF_STMT_IF:
		jaf_free_expr(item->cond.test);
		jaf_free_block_item(item->cond.consequent);
		jaf_free_block_item(item->cond.alternative);
		break;
	case JAF_STMT_SWITCH:
		ERROR("switch not supported");
		break;
	case JAF_STMT_WHILE:
	case JAF_STMT_DO_WHILE:
		jaf_free_expr(item->while_loop.test);
		jaf_free_block_item(item->while_loop.body);
		break;
	case JAF_STMT_FOR:
		jaf_free_block(item->for_loop.init);
		jaf_free_expr(item->for_loop.test);
		jaf_free_expr(item->for_loop.after);
		jaf_free_block_item(item->for_loop.body);
		break;
	case JAF_STMT_GOTO:
		free_string(item->target);
	case JAF_STMT_CONTINUE:
	case JAF_STMT_BREAK:
		break;
	case JAF_STMT_RETURN:
		jaf_free_expr(item->expr);
		break;
	case JAF_STMT_CASE:
	case JAF_STMT_DEFAULT:
		jaf_free_expr(item->swi_case.expr);
		jaf_free_block_item(item->swi_case.stmt);
		break;
	}
	free(item);
}

void jaf_free_block(struct jaf_block *block)
{
	if (!block)
		return;
	for (size_t i = 0; i < block->nr_items; i++) {
		jaf_free_block_item(block->items[i]);
	}
	free(block->items);
	free(block);
}
