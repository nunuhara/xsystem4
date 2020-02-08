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

#ifndef YY_YY_SRC_AINEDIT_48087CF_AINEDIT_EXE_ASM_PARSER_TAB_H_INCLUDED
# define YY_YY_SRC_AINEDIT_48087CF_AINEDIT_EXE_ASM_PARSER_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif
/* "%code requires" blocks.  */
#line 9 "../src/ainedit/asm_parser.y" /* yacc.c:1921  */

    #include <stdint.h>
    #include "khash.h"
    #include "kvec.h"

    kv_decl(parse_instruction_list, struct parse_instruction*);
    kv_decl(parse_argument_list, struct string*);
    kv_decl(pointer_list, uint32_t*);

    struct parse_instruction {
	uint16_t opcode;
        parse_argument_list *args;
    };

    parse_instruction_list *parsed_code;

    KHASH_MAP_INIT_STR(label_table, uint32_t);
    khash_t(label_table) *label_table;

#line 68 "src/ainedit/48087cf@@ainedit@exe/asm_parser.tab.h" /* yacc.c:1921  */

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    IDENTIFIER = 258,
    LABEL = 259,
    NEWLINE = 260
  };
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED

union YYSTYPE
{
#line 1 "../src/ainedit/asm_parser.y" /* yacc.c:1921  */

    int token;
    struct string *string;
    parse_argument_list *args;
    struct parse_instruction *instr;
    parse_instruction_list *program;

#line 94 "src/ainedit/48087cf@@ainedit@exe/asm_parser.tab.h" /* yacc.c:1921  */
};

typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;

int yyparse (void);

#endif /* !YY_YY_SRC_AINEDIT_48087CF_AINEDIT_EXE_ASM_PARSER_TAB_H_INCLUDED  */
