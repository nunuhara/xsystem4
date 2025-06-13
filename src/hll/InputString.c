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

#include <SDL.h>

#include "system4.h"
#include "system4/utfsjis.h"
#include "system4/string.h"

#include "gfx/types.h"
#include "gfx/gfx.h"
#include "hll.h"
#include "input.h"

static int font_size;
static int font_weight;
static Point pos;
static bool has_editing_text;

static struct string *result;

static void InputString_ClearResultString(void)
{
	if (result)
		free_string(result);
	result = string_ref(&EMPTY_STRING);
}

struct string *InputString_GetResultString(void)
{
	struct string *s = string_ref(result);
	InputString_ClearResultString();
	return s;
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
	char *u = utf2sjis(text, 0);
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

static void InputString_OpenIME(void)
{
	register_input_handler(handle_input);
	register_editing_handler(handle_editing);
	has_editing_text = false;
	SDL_StartTextInput();
}

static void InputString_CloseIME(void)
{
	SDL_StopTextInput();
	has_editing_text = false;
	clear_input_handler();
	clear_editing_handler();
}

static bool InputString_Inputs(void)
{
	return has_editing_text;
}

static bool InputString_Converts(void)
{
	return has_editing_text;
}

HLL_LIBRARY(InputString,
	    HLL_EXPORT(SetFont, InputString_SetFont),
	    HLL_EXPORT(SetPos, InputString_SetPos),
	    HLL_EXPORT(Begin, InputString_Begin),
	    HLL_EXPORT(End, InputString_End),
	    HLL_EXPORT(OpenIME, InputString_OpenIME),
	    HLL_EXPORT(CloseIME, InputString_CloseIME),
	    HLL_EXPORT(GetResultString, InputString_GetResultString),
	    HLL_EXPORT(ClearResultString, InputString_ClearResultString),
	    HLL_EXPORT(Inputs, InputString_Inputs),
	    HLL_EXPORT(Converts, InputString_Converts));
