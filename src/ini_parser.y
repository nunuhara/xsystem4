%define api.prefix {yini_}

%union {
    int token;
    int i;
    struct string *s;
    struct ini_entry *entry;
    struct ini_value value;
    entry_list *entries;
    value_list *list;
}

%code requires {
    #include "kvec.h"
    #include "system4/ini.h"

    kv_decl(entry_list, struct ini_entry*);
    kv_decl(value_list, struct ini_value);
    entry_list *yini_entries;
}

%{

#include <stdio.h>
#include "system4.h"
#include "system4/string.h"

extern unsigned long yini_line;
extern int yini_lex();
void yini_error(const char *s) { ERROR("at line %lu: %s", yini_line, s); }

static entry_list *make_ini(void)
{
    entry_list *ini = xmalloc(sizeof(entry_list));
    kv_init(*ini);
    return ini;
}

static void push_entry(entry_list *ini, struct ini_entry *entry)
{
    kv_push(struct ini_entry*, *ini, entry);
}

static struct ini_value make_list(value_list *values)
{
    struct ini_value *vals = kv_data(*values);
    size_t nr_vals = kv_size(*values);
    free(values);
    return ini_make_list(vals, nr_vals);
}

static value_list *make_value_list(void)
{
    value_list *values = xmalloc(sizeof(value_list));
    kv_init(*values);
    return values;
}

static void push_value(value_list *values, struct ini_value value)
{
    kv_push(struct ini_value, *values, value);
}

static struct ini_entry *make_list_assign(struct string *name, int i, struct ini_value value)
{
    struct ini_value *val = xmalloc(sizeof(struct ini_value));
    *val = value;

    struct ini_value wrapper = {
	.type = _INI_LIST_ENTRY,
	._list_pos = i,
	._list_value = val
    };
    return ini_make_entry(name, wrapper);
}

%}

%token	<token>		TRUE FALSE
%token	<i>		INTEGER
%token	<s>		STRING IDENTIFIER

%type	<entries>	inifile
%type	<entries>	entries
%type	<entry>		entry
%type	<value>		value
%type	<list>		list

%start inifile

%%

inifile	: 	entries { yini_entries = $1; }
	;

entries	:	entry         { $$ = make_ini(); push_entry($$, $1); }
	|	entries entry { push_entry($1, $2); }
	;

entry	:	IDENTIFIER                 '=' value { $$ = ini_make_entry($1, $3); }
	|	IDENTIFIER '[' INTEGER ']' '=' value { $$ = make_list_assign($1, $3, $6); }
	;

value	:	INTEGER          { $$ = ini_make_integer($1); }
	|	TRUE             { $$ = ini_make_boolean(true); }
	|	FALSE            { $$ = ini_make_boolean(false); }
	|	STRING           { $$ = ini_make_string($1); }
	|	'{' list '}'     { $$ = make_list($2); }
	|	'{' list ',' '}' { $$ = make_list($2); }
	;

list	:	value          { $$ = make_value_list(); push_value($$, $1); }
	|	list ',' value { push_value($1, $3); }
	;

%%
