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
 *
 * Based on ANSI C grammar from http://www.quut.com/c/ANSI-C-grammar-l-2011.html
 */

%{
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <assert.h>
#include <errno.h>
#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "ainedit.h"
#include "jaf.h"
#include "jaf_parser.tab.h"
extern int yylex();
void yyerror(const char *s);
int yyparse(void);
FILE *yyin;

struct ain *jaf_ain_out;
struct jaf_block *jaf_toplevel;

static FILE *open_jaf_file(const char *file)
{
    if (!strcmp(file, "-"))
	return stdin;
    FILE *f = fopen(file, "rb");
    if (!f)
	ERROR("Opening input file '%s': %s", file, strerror(errno));
    return f;
}

static struct jaf_block *insert_eof(struct ain *ain, struct jaf_block *block, const char *_filename)
{
    assert(block);
    char *filename = xstrdup(_filename);
    int file_no = ain_add_file(ain, basename(filename));
    free(filename);

    struct jaf_block_item *eof = xmalloc(sizeof(struct jaf_block_item));
    eof->kind = JAF_EOF;
    eof->file_no = file_no;
    return jaf_block_append(block, eof);
}

struct jaf_block *jaf_parse(struct ain *ain, const char **files, unsigned nr_files)
{
    jaf_ain_out = ain;
    jaf_toplevel = NULL;

    for (unsigned i = 0; i < nr_files; i++) {
	// open file
	yyin = open_jaf_file(files[i]);
	if (yyparse())
	    ERROR("Failed to parse .jaf file");
	if (yyin != stdin)
	    fclose(yyin);

	jaf_toplevel = insert_eof(ain, jaf_toplevel, files[i]);
    }

    struct jaf_block *r = jaf_toplevel;
    jaf_toplevel = NULL;
    jaf_ain_out = NULL;
    return r;
}

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

static int parse_int(struct string *s)
{
    char *endptr;
    errno = 0;
    int i = strtol(s->text, &endptr, 0);
    if (errno || *endptr != '\0')
	ERROR("Invalid integer constant");
    free_string(s);
    return i;
}

%}

%union {
    int token;
    struct string *string;
    struct jaf_expression *expression;
    struct jaf_argument_list *args;
    struct jaf_type_specifier *type;
    struct jaf_declarator *declarator;
    struct jaf_declarator_list *declarators;
    struct jaf_block *block;
    struct jaf_block_item *statement;
    struct jaf_function_declarator *fundecl;
}

%token	<string>	I_CONSTANT F_CONSTANT STRING_LITERAL
%token	<string>	IDENTIFIER TYPEDEF_NAME ENUMERATION_CONSTANT

%token	<token>		INC_OP DEC_OP LEFT_OP RIGHT_OP LE_OP GE_OP EQ_OP NE_OP
%token	<token>		AND_OP OR_OP MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN ADD_ASSIGN
%token	<token>		SUB_ASSIGN LEFT_ASSIGN RIGHT_ASSIGN AND_ASSIGN
%token	<token>		XOR_ASSIGN OR_ASSIGN
%token	<token>		SYM_REF REF_ASSIGN FUNC_NAME SYSTEM ARRAY

%token	<token>		CONST
%token	<token>		BOOL CHAR INT LONG FLOAT VOID STRING
%token	<token>		STRUCT UNION ENUM ELLIPSIS

%token	CASE DEFAULT IF ELSE SYM_SWITCH WHILE DO FOR GOTO CONTINUE BREAK SYM_RETURN

%type	<token>		unary_operator assignment_operator type_qualifier atomic_type_specifier
%type	<string>	string param_identifer
%type	<expression>	initializer
%type	<expression>	postfix_expression unary_expression cast_expression
%type	<expression>	multiplicative_expression additive_expression shift_expression
%type	<expression>	relational_expression equality_expression and_expression
%type	<expression>	exclusive_or_expression inclusive_or_expression
%type	<expression>	logical_and_expression logical_or_expression
%type	<expression>	conditional_expression assignment_expression
%type	<expression>	expression constant_expression primary_expression constant
%type	<args>		argument_expression_list
%type	<type>		type_specifier struct_specifier declaration_specifiers
%type	<declarator>	init_declarator declarator array_allocation
%type	<declarators>	init_declarator_list struct_declarator_list
%type	<block>		translation_unit external_declaration declaration function_definition
%type	<block>		struct_declaration struct_declaration_list
%type	<block>		parameter_list parameter_declaration
%type	<block>		compound_statement block_item_list block_item
%type	<statement>	statement labeled_statement expression_statement selection_statement
%type	<statement>	iteration_statement jump_statement
%type	<fundecl>	function_declarator

/*
 * Shift-reduce conflicts:
 *     1. dangling else
 */
%expect 1

%start toplevel
%%

primary_expression
	: IDENTIFIER         { $$ = jaf_identifier($1); }
	| constant           { $$ = $1; }
	| string             { $$ = jaf_string($1); }
	| '(' expression ')' { $$ = $2; }
	;

constant
	: I_CONSTANT           { $$ = jaf_parse_integer($1); } /* includes character_constant */
	| F_CONSTANT           { $$ = jaf_parse_float($1); }
	| ENUMERATION_CONSTANT { ERROR("Enums not supported"); } /* after it has been defined as such */
	;

enumeration_constant		/* before it has been defined as such */
	: IDENTIFIER
	;

string
	: STRING_LITERAL { $$ = $1; }
	| FUNC_NAME      { ERROR("__func__ not supported"); }
//	| STRING '(' assignment_expression ')'
	;

postfix_expression
	: primary_expression                                     { $$ = $1; }
	| postfix_expression '[' expression ']'                  { $$ = jaf_subscript_expr($1, $3); }
	| postfix_expression '(' ')'                             { $$ = jaf_function_call($1, NULL); }
	| atomic_type_specifier '(' expression ')'               { $$ = jaf_cast_expression($1, $3); }
	| postfix_expression '(' argument_expression_list ')'    { $$ = jaf_function_call($1, $3); }
	| SYSTEM '.' IDENTIFIER '(' argument_expression_list ')' { $$ = jaf_system_call($3, $5); }
	| postfix_expression '.' IDENTIFIER                      { $$ = jaf_member_expr($1, $3); }
	| postfix_expression INC_OP                              { $$ = jaf_unary_expr(JAF_POST_INC, $1); }
	| postfix_expression DEC_OP                              { $$ = jaf_unary_expr(JAF_POST_DEC, $1); }
//	| '(' type_specifier ')' '{' initializer_list '}'
//	| '(' type_specifier ')' '{' initializer_list ',' '}'
	;

argument_expression_list
	: assignment_expression                              { $$ = jaf_args(NULL, $1); }
	| argument_expression_list ',' assignment_expression { $$ = jaf_args($1,   $3); }
	;

unary_expression
	: postfix_expression             { $$ = $1; }
	| INC_OP unary_expression        { $$ = jaf_unary_expr(JAF_PRE_INC, $2); }
	| DEC_OP unary_expression        { $$ = jaf_unary_expr(JAF_PRE_DEC, $2); }
	| unary_operator cast_expression { $$ = jaf_unary_expr($1, $2); }
	;

unary_operator
	: '&' { $$ = JAF_AMPERSAND; }
	| '+' { $$ = JAF_UNARY_PLUS; }
	| '-' { $$ = JAF_UNARY_MINUS; }
	| '~' { $$ = JAF_BIT_NOT; }
	| '!' { $$ = JAF_LOG_NOT; }
	;

cast_expression
	: unary_expression                              { $$ = $1; }
	| '(' atomic_type_specifier ')' cast_expression { $$ = jaf_cast_expression($2, $4); }
	;

multiplicative_expression
	: cast_expression                               { $$ = $1; }
	| multiplicative_expression '*' cast_expression { $$ = jaf_binary_expr(JAF_MULTIPLY, $1, $3); }
	| multiplicative_expression '/' cast_expression { $$ = jaf_binary_expr(JAF_DIVIDE, $1, $3); }
	| multiplicative_expression '%' cast_expression { $$ = jaf_binary_expr(JAF_REMAINDER, $1, $3); }
	;

additive_expression
	: multiplicative_expression                         { $$ = $1; }
	| additive_expression '+' multiplicative_expression { $$ = jaf_binary_expr(JAF_PLUS, $1, $3); }
	| additive_expression '-' multiplicative_expression { $$ = jaf_binary_expr(JAF_MINUS, $1, $3); }
	;

shift_expression
	: additive_expression                           { $$ = $1; }
	| shift_expression LEFT_OP additive_expression  { $$ = jaf_binary_expr(JAF_LSHIFT, $1, $3); }
	| shift_expression RIGHT_OP additive_expression { $$ = jaf_binary_expr(JAF_RSHIFT, $1, $3); }
	;

relational_expression
	: shift_expression                             { $$ = $1; }
	| relational_expression '<' shift_expression   { $$ = jaf_binary_expr(JAF_LT, $1, $3); }
	| relational_expression '>' shift_expression   { $$ = jaf_binary_expr(JAF_GT, $1, $3); }
	| relational_expression LE_OP shift_expression { $$ = jaf_binary_expr(JAF_LTE, $1, $3); }
	| relational_expression GE_OP shift_expression { $$ = jaf_binary_expr(JAF_GTE, $1, $3); }
	;

equality_expression
	: relational_expression                           { $$ = $1; }
	| equality_expression EQ_OP relational_expression { $$ = jaf_binary_expr(JAF_EQ,  $1, $3); }
	| equality_expression NE_OP relational_expression { $$ = jaf_binary_expr(JAF_NEQ, $1, $3); }
	;

and_expression
	: equality_expression                    { $$ = $1; }
	| and_expression '&' equality_expression { $$ = jaf_binary_expr(JAF_BIT_AND, $1, $3); }
	;

exclusive_or_expression
	: and_expression                             { $$ = $1; }
	| exclusive_or_expression '^' and_expression { $$ = jaf_binary_expr(JAF_BIT_XOR, $1, $3); }
	;

inclusive_or_expression
	: exclusive_or_expression                             { $$ = $1; }
	| inclusive_or_expression '|' exclusive_or_expression { $$ = jaf_binary_expr(JAF_BIT_IOR, $1, $3); }
	;

logical_and_expression
	: inclusive_or_expression                               { $$ = $1; }
	| logical_and_expression AND_OP inclusive_or_expression { $$ = jaf_binary_expr(JAF_LOG_AND, $1, $3); }
	;

logical_or_expression
	: logical_and_expression                             { $$ = $1; }
	| logical_or_expression OR_OP logical_and_expression { $$ = jaf_binary_expr(JAF_LOG_OR, $1, $3); }
	;

conditional_expression
	: logical_or_expression                                           { $$ = $1; }
	| logical_or_expression '?' expression ':' conditional_expression { $$ = jaf_ternary_expr($1, $3, $5); }
	;

assignment_expression
	: conditional_expression                                     { $$ = $1; }
	| unary_expression assignment_operator assignment_expression { $$ = jaf_binary_expr($2, $1, $3); }
	;

assignment_operator
	: '='          { $$ = JAF_ASSIGN; }
	| MUL_ASSIGN   { $$ = JAF_MUL_ASSIGN; }
	| DIV_ASSIGN   { $$ = JAF_DIV_ASSIGN; }
	| MOD_ASSIGN   { $$ = JAF_MOD_ASSIGN; }
	| ADD_ASSIGN   { $$ = JAF_ADD_ASSIGN; }
	| SUB_ASSIGN   { $$ = JAF_SUB_ASSIGN; }
	| LEFT_ASSIGN  { $$ = JAF_LSHIFT_ASSIGN; }
	| RIGHT_ASSIGN { $$ = JAF_RSHIFT_ASSIGN; }
	| AND_ASSIGN   { $$ = JAF_AND_ASSIGN; }
	| XOR_ASSIGN   { $$ = JAF_XOR_ASSIGN; }
	| OR_ASSIGN    { $$ = JAF_OR_ASSIGN; }
	| REF_ASSIGN   { $$ = JAF_REF_ASSIGN; }
	;

expression
	: assignment_expression                { $$ = $1; }
	| expression ',' assignment_expression { $$ = jaf_seq_expr($1, $3); }
	;

constant_expression
	: conditional_expression { $$ = $1; } /* with constraints */
	;

declaration
	: declaration_specifiers ';'                      { $$ = jaf_type_declaration($1); }
	| declaration_specifiers init_declarator_list ';' { $$ = jaf_declaration($1, $2); }
	;

declaration_specifiers
	: type_qualifier type_specifier { $$ = $2; $$->qualifiers |= $1; }
	| type_specifier                { $$ = $1; }
	;

init_declarator_list
	: init_declarator                          { $$ = jaf_declarators(NULL, $1); }
	| init_declarator_list ',' init_declarator { $$ = jaf_declarators($1, $3); }
	;

init_declarator
	: declarator '=' initializer { $$ = $1; $$->init = $3; }
	| declarator                 { $$ = $1; }
	;

atomic_type_specifier
	: VOID             { $$ = JAF_VOID; }
	| CHAR             { $$ = JAF_INT; }
	| INT              { $$ = JAF_INT; }
	| LONG             { $$ = JAF_INT; }
	| FLOAT            { $$ = JAF_FLOAT; }
	| BOOL             { $$ = JAF_INT; }
	| STRING           { $$ = JAF_STRING; }
	;

type_specifier
	: atomic_type_specifier                          { $$ = jaf_type($1); }
	| struct_specifier                               { $$ = $1; }
	| enum_specifier                                 { ERROR("Enums not supported"); }
	| ARRAY '@' atomic_type_specifier                { $$ = jaf_array_type(jaf_type($3), 1); }
	| ARRAY '@' atomic_type_specifier '@' I_CONSTANT { $$ = jaf_array_type(jaf_type($3), parse_int($5)); }
	| ARRAY '@' TYPEDEF_NAME                         { $$ = jaf_array_type(jaf_typedef($3), 1); }
	| ARRAY '@' TYPEDEF_NAME '@' I_CONSTANT          { $$ = jaf_array_type(jaf_typedef($3), parse_int($5)); }
	| TYPEDEF_NAME                                   { $$ = jaf_typedef($1); }
	;

struct_specifier
	: STRUCT param_identifer '{' struct_declaration_list '}' { $$ = jaf_struct($2, $4); jaf_define_struct(jaf_ain_out, $$); }
	;

param_identifer
	: IDENTIFIER                             { $$ = $1; }
	| IDENTIFIER '<' type_parameter_list '>' { ERROR("Type parameters not supported"); }
	;

type_parameter_list
	: type_specifier
	| type_parameter_list ',' type_specifier
	;

struct_declaration_list
	: struct_declaration                         { $$ = $1; }
	| struct_declaration_list struct_declaration { $$ = jaf_merge_blocks($1, $2); }
	;

struct_declaration
//	: type_specifier ';'	/* for anonymous struct/union */
	: type_specifier struct_declarator_list ';' { $$ = jaf_declaration($1, $2); }
	;

struct_declarator_list
	: declarator                            { $$ = jaf_declarators(NULL, $1); }
	| struct_declarator_list ',' declarator { $$ = jaf_declarators($1, $3); }
	;

enum_specifier
	: ENUM '{' enumerator_list '}'
	| ENUM '{' enumerator_list ',' '}'
	| ENUM IDENTIFIER '{' enumerator_list '}'
	| ENUM IDENTIFIER '{' enumerator_list ',' '}'
	;

enumerator_list
	: enumerator
	| enumerator_list ',' enumerator
	;

enumerator	/* identifiers must be flagged as ENUMERATION_CONSTANT */
	: enumeration_constant '=' constant_expression
	| enumeration_constant
	;

type_qualifier
	: CONST   { $$ = JAF_QUAL_CONST; }
	| SYM_REF { $$ = JAF_QUAL_REF; }
	;

declarator
	: IDENTIFIER                             { $$ = jaf_declarator($1); }
//	| '(' declarator ')' // XXX: causes shift-reduce conflict with function-like casts (e.g. "string(identifier)")
	| array_allocation                       { $$ = $1; }
	;

array_allocation
	: IDENTIFIER '[' constant ']'       { $$ = jaf_array_allocation($1, $3); }
	| array_allocation '[' constant ']' { ERROR("Multi-dimensional arrays not supported"); }
	;

initializer
	: '{' initializer_list '}'     { $$ = NULL; }
	| '{' initializer_list ',' '}' { $$ = NULL; }
	| assignment_expression        { $$ = $1; }
	;

initializer_list
	: designation initializer
	| initializer
	| initializer_list ',' designation initializer
	| initializer_list ',' initializer
	;

designation
	: designator_list '='
	;

designator_list
	: designator
	| designator_list designator
	;

designator
	: '[' constant_expression ']'
	| '.' IDENTIFIER
	;

statement
	: labeled_statement    { $$ = $1; }
	| compound_statement   { $$ = jaf_compound_statement($1); }
	| expression_statement { $$ = $1; }
	| selection_statement  { $$ = $1; }
	| iteration_statement  { $$ = $1; }
	| jump_statement       { $$ = $1; }
	;

labeled_statement
	: IDENTIFIER ':' statement               { $$ = jaf_label_statement($1, $3); }
	| CASE constant_expression ':' statement { $$ = jaf_case_statement($2, $4); }
	| DEFAULT ':' statement                  { $$ = jaf_case_statement(NULL, $3); }
	;

compound_statement
	: '{' '}'                  { $$ = jaf_block(NULL); }
	| '{'  block_item_list '}' { $$ = $2; }
	;

block_item_list
	: block_item                 { $$ = $1; }
	| block_item_list block_item { $$ = jaf_merge_blocks($1, $2); }
	;

block_item
	: declaration { $$ = $1; }
	| statement   { $$ = jaf_block($1); }
	;

expression_statement
	: ';'            { $$ = jaf_expression_statement(NULL); }
	| expression ';' { $$ = jaf_expression_statement($1); }
	;

selection_statement
	: IF '(' expression ')' statement ELSE statement   { $$ = jaf_if_statement($3, $5, $7); }
	| IF '(' expression ')' statement                  { $$ = jaf_if_statement($3, $5, NULL); }
	| SYM_SWITCH '(' expression ')' compound_statement { $$ = jaf_switch_statement($3, $5); }
	;

iteration_statement
	: WHILE '(' expression ')' statement                                         { $$ = jaf_while_loop($3, $5); }
	| DO statement WHILE '(' expression ')' ';'                                  { $$ = jaf_do_while_loop($5, $2); }
	| FOR '(' expression_statement expression_statement ')' statement            { $$ = jaf_for_loop(jaf_block($3), $4, NULL, $6); }
	| FOR '(' expression_statement expression_statement expression ')' statement { $$ = jaf_for_loop(jaf_block($3), $4, $5,   $7); }
	| FOR '(' declaration expression_statement ')' statement                     { $$ = jaf_for_loop($3, $4, NULL, $6); }
	| FOR '(' declaration expression_statement expression ')' statement          { $$ = jaf_for_loop($3, $4, $5,   $7); }
	;

jump_statement
	: GOTO IDENTIFIER ';'       { $$ = jaf_goto($2); }
	| CONTINUE ';'              { $$ = jaf_continue(); }
	| BREAK ';'                 { $$ = jaf_break(); }
	| SYM_RETURN ';'            { $$ = jaf_return(NULL); }
	| SYM_RETURN expression ';' { $$ = jaf_return($2); }
	;

toplevel
	: translation_unit { jaf_toplevel = jaf_merge_blocks(jaf_toplevel, $1); }
	;

translation_unit
	: external_declaration                  { $$ = jaf_merge_blocks(NULL, $1); }
	| translation_unit external_declaration { $$ = jaf_merge_blocks($1, $2); }
	;

external_declaration
	: function_definition { $$ = $1; }
	| declaration         { $$ = $1; }
	;

function_definition
	: declaration_specifiers function_declarator compound_statement { $$ = jaf_function($1, $2, $3); }
	;

function_declarator
	: IDENTIFIER '(' parameter_list ')' { $$ = jaf_function_declarator($1, $3); }
	| IDENTIFIER '(' ')'                { $$ = jaf_function_declarator($1, NULL); }
	| IDENTIFIER '(' VOID ')'           { $$ = jaf_function_declarator($1, NULL); }
	;

parameter_list
	: parameter_declaration                    { $$ = $1; }
	| parameter_list ',' parameter_declaration { $$ = jaf_merge_blocks($1, $3); }
	;

parameter_declaration
	: declaration_specifiers declarator { $$ = jaf_parameter($1, $2); }
	;

%%
#include <stdio.h>

void yyerror(const char *s)
{
	fflush(stdout);
	fprintf(stderr, "*** %s\n", s);
}
