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

#include <SDL.h>

#include "system4/string.h"
#include "system4/utfsjis.h"

#include "hll.h"

HLL_QUIET_UNIMPLEMENTED( , void, vmDialog, Init, void *imainsystem);

static int vmDialog_MsgBox(struct string *t_string, struct string *string, int type, possibly_unused int default_button)
{
	uint32_t flags = (type == 4) ? SDL_MESSAGEBOX_ERROR : 0;
	char *title = cjk_dialog_text_to_utf8(t_string ? t_string->text : "", t_string ? t_string->size : 0);
	char *message = cjk_dialog_text_to_utf8(string ? string->text : "", string ? string->size : 0);
	SDL_ShowSimpleMessageBox(flags, title, message, NULL);
	free(message);
	free(title);
	return 1;
}

//int vmDialog_InputNum(struct string *pITString, int nMin, int nMax, int *pnNum);
//int vmDialog_InputString(struct string *pITString, int nLength, struct string **pIString);
//int vmDialog_SelectNum(struct string *pITString, int nMin, int nMax, int *pnNum);
//int vmDialog_SelectString(struct string *pITString, struct page **pIVMArray, int nNum, struct string **pIString);
//int vmDialog_InputOpenFile(struct string *pITString, struct string **pIPString, struct string **pIString, struct string *pIDString, struct string *pIFString);
//int vmDialog_InputSaveFile(struct string *pITString, struct string **pIPString, struct string **pIString, struct string *pIDString, struct string *pIFString);

HLL_LIBRARY(vmDialog,
	    HLL_EXPORT(Init, vmDialog_Init),
	    HLL_EXPORT(MsgBox, vmDialog_MsgBox),
	    HLL_TODO_EXPORT(InputNum, vmDialog_InputNum),
	    HLL_TODO_EXPORT(InputString, vmDialog_InputString),
	    HLL_TODO_EXPORT(SelectNum, vmDialog_SelectNum),
	    HLL_TODO_EXPORT(SelectString, vmDialog_SelectString),
	    HLL_TODO_EXPORT(InputOpenFile, vmDialog_InputOpenFile),
	    HLL_TODO_EXPORT(InputSaveFile, vmDialog_InputSaveFile)
	    );

static int Dialog_MsgBox(struct string *title, struct string *msg, possibly_unused int button,
		int icon, int default_button)
{
	return vmDialog_MsgBox(title, msg, icon, default_button);
}

/*
 * xsystem4 has no blocking modal numeric/menu/text input dialogs (only
 * SDL_ShowMessageBox for alerts and non-blocking IME for InputString/Parts).
 */
static int Dialog_InputNum(possibly_unused struct string *title, int min_val,
		int max_val, int *num)
{
	if (num && min_val <= max_val) {
		if (*num < min_val)
			*num = min_val;
		else if (*num > max_val)
			*num = max_val;
	}
	return 0;
}

/*
 * Preserve the caller's pre-filled default name (guaranteeing *msg is non-NULL
 * and bounded by max_len on a multibyte character boundary) and return 1:
 * returning 0 causes a 100% CPU infinite hang in Dungeons & Dolls
 * (`while (!Dialog.InputMsg("あなたの名前は？", 8, name)) {}`).
 */
static int Dialog_InputMsg(possibly_unused struct string *title, int max_len,
		struct string **msg)
{
	if (msg) {
		if (!*msg) {
			*msg = make_string("", 0);
		} else if (max_len > 0 && (*msg)->size > max_len) {
			const char *text = (*msg)->text;
			int size = (*msg)->size;
			int len = 0;
			while (len < size) {
				int step = (SJIS_2BYTE((uint8_t)text[len]) && len + 1 < size) ? 2 : 1;
				if (len + step > max_len)
					break;
				len += step;
			}
			struct string *truncated = make_string(text, len);
			free_string(*msg);
			*msg = truncated;
		}
	}
	return 1;
}

static int Dialog_MenuMsg(possibly_unused struct string *title, possibly_unused struct page **items,
		int *num)
{
	if (num && *num < 0)
		*num = 0;
	return 0;
}

static int Dialog_LoadFile(possibly_unused struct string *title, possibly_unused struct string **file,
		possibly_unused struct string *default_dir, possibly_unused struct string *filter)
{
	return 0;
}

static int Dialog_SaveFile(possibly_unused struct string *title, possibly_unused struct string **file,
		possibly_unused struct string *default_dir, possibly_unused struct string *filter)
{
	return 0;
}

HLL_LIBRARY(Dialog,
	    HLL_EXPORT(MsgBox, Dialog_MsgBox),
	    HLL_EXPORT(InputNum, Dialog_InputNum),
	    HLL_EXPORT(InputMsg, Dialog_InputMsg),
	    HLL_EXPORT(MenuMsg, Dialog_MenuMsg),
	    HLL_EXPORT(LoadFile, Dialog_LoadFile),
	    HLL_EXPORT(SaveFile, Dialog_SaveFile)
	    );
