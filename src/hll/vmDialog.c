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
	char *title = sjis2utf(t_string->text, 0);
	char *message = sjis2utf(string->text, 0);
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
