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

#include <stdio.h>
#include "hll.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

static void OutputLog_Output(int handle, struct string *s)
{
	char *u = sjis2utf(s->text, s->size);
	printf("%s", u);
	free(u);
}

HLL_WARN_UNIMPLEMENTED(0, int,  OutputLog, Create, struct string *name);
HLL_WARN_UNIMPLEMENTED( , void, OutputLog, Clear, int handle);
HLL_WARN_UNIMPLEMENTED(0, int,  OutputLog, Save, int handle, struct string *filename);
HLL_WARN_UNIMPLEMENTED(0, bool, OutputLog, EnableAutoSave, int handle, struct string *filename);
HLL_WARN_UNIMPLEMENTED(0, bool, OutputLog, DisableAutoSave, int handle);

HLL_LIBRARY(OutputLog,
	    HLL_EXPORT(Create, OutputLog_Create),
	    HLL_EXPORT(Output, OutputLog_Output),
	    HLL_EXPORT(Clear, OutputLog_Clear),
	    HLL_EXPORT(Save, OutputLog_Save),
	    HLL_EXPORT(EnableAutoSave, OutputLog_EnableAutoSave),
	    HLL_EXPORT(DisableAutoSave, OutputLog_DisableAutoSave));
