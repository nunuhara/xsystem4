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

#ifndef YY_TEXT_SRC_AINEDIT_48087CF_AINEDIT_EXE_TEXT_PARSER_TAB_H_INCLUDED
# define YY_TEXT_SRC_AINEDIT_48087CF_AINEDIT_EXE_TEXT_PARSER_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef TEXT_DEBUG
# if defined YYDEBUG
#if YYDEBUG
#   define TEXT_DEBUG 1
#  else
#   define TEXT_DEBUG 0
#  endif
# else /* ! defined YYDEBUG */
#  define TEXT_DEBUG 0
# endif /* ! defined YYDEBUG */
#endif  /* ! defined TEXT_DEBUG */
#if TEXT_DEBUG
extern int text_debug;
#endif
/* "%code requires" blocks.  */
#line 11 "../src/ainedit/text_parser.y" /* yacc.c:1921  */

    #include "kvec.h"

    kv_decl(assignment_list, struct text_assignment*);

    struct text_assignment {
	int type;
	int index;
	struct string *string;
    };

    assignment_list *statements;


#line 71 "src/ainedit/48087cf@@ainedit@exe/text_parser.tab.h" /* yacc.c:1921  */

/* Token type.  */
#ifndef TEXT_TOKENTYPE
# define TEXT_TOKENTYPE
  enum text_tokentype
  {
    NEWLINE = 258,
    LBRACKET = 259,
    RBRACKET = 260,
    EQUAL = 261,
    MESSAGES = 262,
    STRINGS = 263,
    NUMBER = 264,
    STRING = 265
  };
#endif

/* Value type.  */
#if ! defined TEXT_STYPE && ! defined TEXT_STYPE_IS_DECLARED

union TEXT_STYPE
{
#line 3 "../src/ainedit/text_parser.y" /* yacc.c:1921  */

    int token;
    int integer;
    struct string *string;
    struct text_assignment *assign;
    assignment_list *program;

#line 102 "src/ainedit/48087cf@@ainedit@exe/text_parser.tab.h" /* yacc.c:1921  */
};

typedef union TEXT_STYPE TEXT_STYPE;
# define TEXT_STYPE_IS_TRIVIAL 1
# define TEXT_STYPE_IS_DECLARED 1
#endif


extern TEXT_STYPE text_lval;

int text_parse (void);

#endif /* !YY_TEXT_SRC_AINEDIT_48087CF_AINEDIT_EXE_TEXT_PARSER_TAB_H_INCLUDED  */
