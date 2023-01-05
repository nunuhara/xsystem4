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

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "system4/string.h"
#include "system4/utfsjis.h"
#include "hll.h"
#include "xsystem4.h"

/*
 * OutputLog is HLL for manipulating log files.
 * For example, Pastel Chime C++ uses OutputLog HLL to write logs to
 * Dungeon.log and SACT.log, etc.
 * Currently, logs are written to standard output only, without creating a log file.
 */

static int OutputLog_Create(struct string *szName)
{
	return 0;
}

static void OutputLog_Output(int nHandle, struct string *szText)
{
	sys_message("%s", display_sjis0(szText->text));
}

static bool OutputLog_EnableAutoSave(int nHandle, struct string *szFileName)
{
	return true;
}

static bool OutputLog_DisableAutoSave(int nHandle)
{
	return true;
}

static void OutputLog_Clear(int nHandle)
{
	return;
}

static void OutputLog_Save(int nHandle, struct string *szFileName)
{
	return;
}

HLL_LIBRARY(OutputLog,
	    HLL_EXPORT(Create, OutputLog_Create),
	    HLL_EXPORT(Output, OutputLog_Output),
	    HLL_EXPORT(Clear, OutputLog_Clear),
	    HLL_EXPORT(Save, OutputLog_Save),
	    HLL_EXPORT(EnableAutoSave, OutputLog_EnableAutoSave),
	    HLL_EXPORT(DisableAutoSave, OutputLog_DisableAutoSave));
