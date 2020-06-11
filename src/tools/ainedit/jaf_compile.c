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
#include "system4/string.h"
#include "ainedit.h"
#include "jaf.h"
#include "jaf_parser.tab.h"

FILE *yyin;
int yyparse(void);

struct ain *jaf_ain_out;
struct jaf_declaration_list *jaf_toplevel;

int sym_type(char *name)
{
	char *u = encode_text_to_input_format(name);
	struct ain_struct *s = ain_get_struct(jaf_ain_out, u);
	free(u);

	if (s)
		return TYPEDEF_NAME;
	return IDENTIFIER;
}

static void define_types(struct ain *ain, struct jaf_declaration *decl);

void jaf_define_struct(struct ain *ain, struct jaf_type_specifier *type)
{
	if (!type->name)
		ERROR("Anonymous structs not supported");

	char *u = encode_text_to_input_format(type->name->text);
	if (ain_get_struct(ain, u)) {
		ERROR("Redefining structs not supported");
	}

	type->struct_no = ain_add_struct(ain, u);
	free(u);

	for (size_t i = 0; i < type->def->nr_decls; i++) {
		define_types(ain, type->def->decls[i]);
	}
}

static void define_types(struct ain *ain, struct jaf_declaration *decl)
{
	if (!decl)
		ERROR("DECL IS NULL");
	if (!decl->type)
		ERROR("TYPE IS NULL");
	if (decl->type->type != JAF_STRUCT)
		return;

	jaf_define_struct(ain, decl->type);
}

static enum ain_data_type jaf_to_ain_data_type(enum jaf_type type)
{
	switch (type) {
	case JAF_VOID:   return AIN_VOID;
	case JAF_INT:    return AIN_INT;
	case JAF_FLOAT:  return AIN_FLOAT;
	case JAF_STRING: return AIN_STRING;
	case JAF_STRUCT: return AIN_STRUCT;
	case JAF_ENUM:   ERROR("Enums not supported");
	default:         ERROR("Unknown type: %d", type);
	}
}

static void jaf_to_ain_type(possibly_unused struct ain *ain, struct ain_type *out, struct jaf_type_specifier *in)
{
	out->data = jaf_to_ain_data_type(in->type);
	if (in->type == JAF_STRUCT) {
		out->struc = in->struct_no;
	}
}

static void resolve_typedef(struct ain *ain, struct jaf_type_specifier *type)
{
	char *u = encode_text_to_input_format(type->name->text);
	int sno = ain_get_struct_no(ain, u);
	if (sno) {
		type->type = JAF_STRUCT;
		type->struct_no = sno;
	} else {
		ERROR("Failed to resolve typedef \"%s\"", type->name->text);
	}

	free(u);
}

static void jaf_to_initval(struct ain_initval *dst, struct jaf_expression *expr)
{
	switch (expr->type) {
	case JAF_EXP_INT:
		dst->data_type = AIN_INT;
		dst->int_value = expr->i;
		break;
	case JAF_EXP_FLOAT:
		dst->data_type = AIN_FLOAT;
		dst->float_value = expr->f;
		break;
	case JAF_EXP_STRING:
		dst->data_type = AIN_STRING;
		dst->string_value = strdup(expr->s->text);
		break;
	default:
		ERROR("Initval is not constant");
	}
}

static void analyze_expression(possibly_unused struct ain *ain, struct jaf_expression **expr, struct jaf_type_specifier *type)
{
	jaf_check_types(*expr, type);
	*expr = jaf_simplify(*expr);
}

static void resolve_types(struct ain *ain, struct jaf_declaration *decl)
{
	if (decl->function)
		return;
	if (decl->type->type == JAF_TYPEDEF)
		resolve_typedef(ain, decl->type);
	if (decl->type->type != JAF_STRUCT || !decl->type->def)
		return;

	assert(decl->type->struct_no >= 0); // set in parse phase
	assert(decl->type->struct_no < ain->nr_structures);

	// create struct definition in ain object
	struct jaf_declaration_list *def = decl->type->def;
	struct ain_variable *members = xcalloc(def->nr_decls, sizeof(struct ain_variable));
	for (size_t i = 0; i < def->nr_decls; i++) {
		members[i].name = encode_text_to_input_format(def->decls[i]->name->text);
		if (ain->version >= 12)
			members[i].name2 = strdup("");
		jaf_to_ain_type(ain, &members[i].type, def->decls[i]->type);
	}

	struct ain_struct *s = &ain->structures[decl->type->struct_no];
	s->nr_members = def->nr_decls;
	s->members = members;
}

static void add_global(struct ain *ain, struct jaf_declaration *decl)
{
	char *u = encode_text_to_input_format(decl->name->text);
	struct ain_variable *v = ain_add_global(ain, u);
	jaf_to_ain_type(ain, &v->type, decl->type);
	decl->var_no = v - ain->globals;
	free(u);
}

static void compile_declaration(possibly_unused struct ain *ain, struct jaf_declaration *decl)
{
	if (decl->function) {
		WARNING("Functions not supported");
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

	// pass 1: typedefs && struct definitions
	for (size_t i = 0; i < jaf_toplevel->nr_decls; i++) {
		resolve_types(out, jaf_toplevel->decls[i]);
	}
	// pass 2: register globals (names, types)
	for (size_t i = 0; i < jaf_toplevel->nr_decls; i++) {
		if (jaf_toplevel->decls[i]->name) {
			if (jaf_toplevel->decls[i]->function)
				ERROR("Functions not supported");
			else
				add_global(out, jaf_toplevel->decls[i]);
		}
	}
	// pass 3: type analysis & simplification & global initvals (static analysis)
	for (size_t i = 0; i < jaf_toplevel->nr_decls; i++) {
		if (jaf_toplevel->decls[i]->init) {
			analyze_expression(out, &jaf_toplevel->decls[i]->init, jaf_toplevel->decls[i]->type);

			// add initval to ain object
			struct ain_initval init = { .global_index = jaf_toplevel->decls[i]->var_no };
			jaf_to_initval(&init, jaf_toplevel->decls[i]->init);
			ain_add_initval(out, &init);
		}
	}
	// pass 4: generate bytecode
	for (size_t i = 0; i < jaf_toplevel->nr_decls; i++) {
		compile_declaration(out, jaf_toplevel->decls[i]);
	}

	// XXX: ain_add_initval adds initval to GSET section of the ain file.
	//      If the original ain file did not have a GSET section, init
	//      code needs to be added to the "0" function instead.
	if (out->nr_initvals && !out->GSET.present) {
		WARNING("%d global initvals ignored", out->nr_initvals);
	}
}
