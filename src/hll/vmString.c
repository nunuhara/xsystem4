/* Copyright (C) 2024 kichikuou <KichikuouChrome@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/string.h"
#include "system4/utfsjis.h"

#include "hll.h"

static int vmString_GetLength(struct string *string)
{
	return sjis_count_char(string->text);
}

static int vmString_GetLengthA(struct string *string)
{
	return strlen(string->text);
}

static struct string *format_int(int v, int figures, bool zero_pad, bool zenkaku)
{
	char buf[32];
	int_to_cstr(buf, sizeof(buf), v, figures, zero_pad, zenkaku);
	return cstr_to_string(buf);
}

static struct string *vmString_IntToString(int num)
{
	return format_int(num, 0, false, true);
}

struct string *vmString_IntToStringA(int num)
{
	return format_int(num, 0, false, false);
}

static struct string *vmString_IntToStringEx(int num, int figure, int zero)
{
	return format_int(num, figure, zero, true);
}

static struct string *vmString_IntToStringExA(int num, int figure, int zero)
{
	return format_int(num, figure, zero, false);
}

static int vmString_StringToInt(struct string *string)
{
	return atoi(string->text);
}

//struct string *vmString_FloatToString(float fNum);
//struct string *vmString_FloatToStringA(float fNum);
//float vmString_StringToFloat(struct string *pIString);

static struct string *vmString_GetString(struct string *string, int index, int length)
{
	return string_copy(string, index, length);
}

//struct string *vmString_CutTag(struct string *pIString);
//void vmString_TransLower(struct string **pIString);
//void vmString_TransUpper(struct string **pIString);
//void vmString_Convert(struct string **pIDString, struct string *pISString, struct string *pICString);

HLL_LIBRARY(vmString,
	    HLL_EXPORT(GetLength, vmString_GetLength),
	    HLL_EXPORT(GetLengthA, vmString_GetLengthA),
	    HLL_EXPORT(IntToString, vmString_IntToString),
	    HLL_EXPORT(IntToStringA, vmString_IntToStringA),
	    HLL_EXPORT(IntToStringEx, vmString_IntToStringEx),
	    HLL_EXPORT(IntToStringExA, vmString_IntToStringExA),
	    HLL_EXPORT(StringToInt, vmString_StringToInt),
	    HLL_TODO_EXPORT(FloatToString, vmString_FloatToString),
	    HLL_TODO_EXPORT(FloatToStringA, vmString_FloatToStringA),
	    HLL_TODO_EXPORT(StringToFloat, vmString_StringToFloat),
	    HLL_EXPORT(GetString, vmString_GetString),
	    HLL_TODO_EXPORT(CutTag, vmString_CutTag),
	    HLL_TODO_EXPORT(TransLower, vmString_TransLower),
	    HLL_TODO_EXPORT(TransUpper, vmString_TransUpper),
	    HLL_TODO_EXPORT(Convert, vmString_Convert)
	    );
