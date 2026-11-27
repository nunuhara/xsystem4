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

#include <string.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/utfsjis.h"
#include "system4/string.h"

#include "vm.h"
#include "gfx/types.h"
#include "gfx/gfx.h"
#include "hll.h"
#include "input.h"
#include "parts_widgets.h"

static int font_size;
static int font_weight;
static Point pos;
static bool has_editing_text;
static bool ime_open = false;
static bool get_result_string_clears;

static struct string *result;

static void InputString_ClearResultString(void)
{
	if (result)
		free_string(result);
	result = string_ref(&EMPTY_STRING);
}

static struct string *InputString_GetResultString(void)
{
	struct string *s = result ? string_ref(result) : string_ref(&EMPTY_STRING);
	if (get_result_string_clears)
		InputString_ClearResultString();
	return s;
}

static void InputString_SetResultString(struct string *s)
{
	if (result)
		free_string(result);
	result = s ? string_ref(s) : string_ref(&EMPTY_STRING);
}

static void InputString_SetFont(int nSize, possibly_unused struct string *pIName, int nWeight)
{
	font_size = nSize;
	font_weight = nWeight;
}

static void InputString_SetPos(int nX, int nY)
{
	pos.x = nX;
	pos.y = nY;
}

static void InputString_Begin(void)
{
	InputString_ClearResultString();
}

static void InputString_End(void)
{
	InputString_ClearResultString();
}

static void handle_input(const char *text)
{
	if (!result)
		result = string_ref(&EMPTY_STRING);
	char *u = mai_utf2sjis(text, 0);
	string_append_cstr(&result, u, strlen(u));
	free(u);
	has_editing_text = false;
}

// TODO: This function has to draw the text at the given coordinates/size/weight.
//       Need some kind of interface for creating textures that are always rendered
//       on top of the scene. Should ideally be independent of SACT.
static void handle_editing(const char *text, int start, int length)
{
	has_editing_text = *text != '\0';
}

static void handle_key(int code)
{
	if (code == VK_BACK && !has_editing_text && result && result->size > 0)
		string_pop_back(&result);
}

static void InputString_OpenIME(void)
{
	static const struct input_text_handlers handlers = { handle_input, handle_editing, handle_key };
	has_editing_text = false;
	ime_open = true;
	input_set_legacy_text_handlers(&handlers);
}

static void InputString_CloseIME(void)
{
	ime_open = false;
	has_editing_text = false;
	input_set_legacy_text_handlers(NULL);
}

static bool InputString_IsOpenIME(void) { return ime_open; }
static bool InputString_Inputs(void)
{
	return ime_open ? has_editing_text : PE_IsTextBoxComposing();
}

static bool InputString_Converts(void)
{
	return InputString_Inputs();
}

static void InputString_PreLink(void)
{
	get_result_string_clears = true;
	int libno = ain_get_library(ain, "InputString");
	if (libno < 0)
		return;
	if (ain_get_library_function(ain, libno, "SetResultString") >= 0 ||
	    ain_get_library_function(ain, libno, "Inputs") >= 0) {
		get_result_string_clears = false;
	}
}

static void InputString_ModuleFini(void)
{
	InputString_CloseIME();
	if (result) {
		free_string(result);
		result = NULL;
	}
	font_size = 0;
	font_weight = 0;
	pos = (Point){ 0, 0 };
	get_result_string_clears = false;
}

HLL_LIBRARY(InputString,
	    HLL_EXPORT(_ModuleFini, InputString_ModuleFini),
	    HLL_EXPORT(_PreLink, InputString_PreLink),
	    HLL_EXPORT(_ModuleFini, InputString_ModuleFini),
	    HLL_EXPORT(SetFont, InputString_SetFont),
	    HLL_EXPORT(SetPos, InputString_SetPos),
	    HLL_EXPORT(Begin, InputString_Begin),
	    HLL_EXPORT(End, InputString_End),
	    HLL_EXPORT(OpenIME, InputString_OpenIME),
	    HLL_EXPORT(CloseIME, InputString_CloseIME),
	    HLL_EXPORT(IsOpenIME, InputString_IsOpenIME),
	    HLL_EXPORT(GetResultString, InputString_GetResultString),
	    HLL_EXPORT(ClearResultString, InputString_ClearResultString),
	    HLL_EXPORT(SetResultString, InputString_SetResultString),
	    HLL_EXPORT(Inputs, InputString_Inputs),
	    HLL_EXPORT(Converts, InputString_Converts));
