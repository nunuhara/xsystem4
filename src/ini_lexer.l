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
#include <stdlib.h>
#include "ini_parser.tab.h"
#include "system4.h"
#include "system4/string.h"

char string_buf[65536];
char *string_buf_ptr;

unsigned long yini_line = 1;

%}

%option noyywrap
%option prefix="yini_"

%x str

%%

\/\/[^\n]*\n              yini_line++;
[ \t\r]                   ;
\n                        yini_line++;
;[^\n]*\n                 yini_line++;
\{                        return '{';
\}                        return '}';
\[                        return '[';
\]                        return ']';
=                         return '=';
,                         return ',';
true                      return TRUE;
false                     return FALSE;
Formation                 return FORMATION;
[0-9]+                    yini_lval.i = atoi(yini_text); return INTEGER;
[a-zA-Z]+[a-zA-Z0-9]*     yini_lval.s = make_string(yini_text, yini_leng); return IDENTIFIER;

\"      string_buf_ptr = string_buf; BEGIN(str);

<str>{
    \" {
        BEGIN(INITIAL);
        *string_buf_ptr = '\0';
        yini_lval.s = make_string(string_buf, string_buf_ptr - string_buf);
        return STRING;
    }

    \n   ERROR("Unterminated string literal at line %lu", yini_line);

    [^\n\"]+ {
        char *yptr = yini_text;
        while (*yptr)
            *string_buf_ptr++ = *yptr++;
    }
}

%%
