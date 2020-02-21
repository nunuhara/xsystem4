/* A Bison parser, made by GNU Bison 3.3.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2019 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* Undocumented macros, especially those whose name start with YY_,
   are private implementation details.  Do not rely on them.  */

#ifndef YY_YEX_SRC_TOOLS_EX_CF5F17F_EXBUILD_EXE_EX_PARSER_TAB_H_INCLUDED
# define YY_YEX_SRC_TOOLS_EX_CF5F17F_EXBUILD_EXE_EX_PARSER_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YEX_DEBUG
# if defined YYDEBUG
#if YYDEBUG
#   define YEX_DEBUG 1
#  else
#   define YEX_DEBUG 0
#  endif
# else /* ! defined YYDEBUG */
#  define YEX_DEBUG 0
# endif /* ! defined YYDEBUG */
#endif  /* ! defined YEX_DEBUG */
#if YEX_DEBUG
extern int yex_debug;
#endif
/* "%code requires" blocks.  */
#line 22 "../src/tools/ex/ex_parser.y" /* yacc.c:1921  */

    #include "kvec.h"
    #include "system4/ex.h"

    kv_decl(block_list, struct ex_block*);
    kv_decl(field_list, struct ex_field*);
    kv_decl(value_list, struct ex_value*);
    kv_decl(row_list,   value_list*);
    kv_decl(node_list,  struct ex_tree*);

    struct ex *ex_data;


#line 70 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.h" /* yacc.c:1921  */

/* Token type.  */
#ifndef YEX_TOKENTYPE
# define YEX_TOKENTYPE
  enum yex_tokentype
  {
    INT = 258,
    FLOAT = 259,
    STRING = 260,
    TABLE = 261,
    LIST = 262,
    TREE = 263,
    CONST_INT = 264,
    CONST_FLOAT = 265,
    CONST_STRING = 266
  };
#endif

/* Value type.  */
#if ! defined YEX_STYPE && ! defined YEX_STYPE_IS_DECLARED

union YEX_STYPE
{
#line 3 "../src/tools/ex/ex_parser.y" /* yacc.c:1921  */

    int token;
    int i;
    float f;
    struct string *s;
    struct ex *ex;
    struct ex_block *block;
    struct ex_table *table;
    struct ex_field *field;
    struct ex_list *list;
    struct ex_tree *tree;
    struct ex_value *value;
    block_list *blocks;
    field_list *fields;
    row_list *rows;
    value_list *values;
    node_list *nodes;

#line 113 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.h" /* yacc.c:1921  */
};

typedef union YEX_STYPE YEX_STYPE;
# define YEX_STYPE_IS_TRIVIAL 1
# define YEX_STYPE_IS_DECLARED 1
#endif


extern YEX_STYPE yex_lval;

int yex_parse (void);

#endif /* !YY_YEX_SRC_TOOLS_EX_CF5F17F_EXBUILD_EXE_EX_PARSER_TAB_H_INCLUDED  */
