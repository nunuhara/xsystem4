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

%{

#pragma GCC diagnostic ignored "-Wunused-function"

#include <stdio.h>
#include "asm_parser.tab.h"
#include "system4.h"
#include "system4/string.h"

#define SAVE_TOKEN yylval.string = makes_string(yytext, yyleng)

char string_buf[65536];
char *string_buf_ptr;

%}

%option noyywrap

%x str

id_char  [^ \t\r\n:;\"]

%%

[ \t]                     ;
;[^\n]*\n                 return NEWLINE;
\n                        return NEWLINE;
[a-zA-Z0-9_-]+:           yylval.string = make_string(yytext, yyleng-1); return LABEL;
({id_char}|:)*{id_char}+  yylval.string = make_string(yytext, yyleng);   return IDENTIFIER;


\"      string_buf_ptr = string_buf; BEGIN(str);

<str>{
    \" {
        BEGIN(INITIAL);
        *string_buf_ptr = '\0';
        yylval.string = make_string(string_buf, strlen(string_buf));
        return IDENTIFIER;
    }

    \n ERROR("Unterminated string literal");

    \\n  *string_buf_ptr++ = '\n';
    \\t  *string_buf_ptr++ = '\t';
    \\r  *string_buf_ptr++ = '\r';
    \\b  *string_buf_ptr++ = '\b';
    \\f  *string_buf_ptr++ = '\f';

    \\(.|\n)  *string_buf_ptr++ = yytext[1];

    [^\\\n\"]+ {
        char *yptr = yytext;
        while (*yptr)
            *string_buf_ptr++ = *yptr++;
    }
}

%%
