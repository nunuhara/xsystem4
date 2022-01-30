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

#include "system4/string.h"

#include "hll.h"

static void CommonSystemData_SetFullPathSaveFileName(struct string *filename)
{
	NOTICE("SetFullPathSaveFileName(%s)", filename->text);
}

HLL_WARN_UNIMPLEMENTED(false, bool, CommonSystemData, LoadAtStartup);
//static bool CommonSystemData_LoadAtStartup(void);
//static bool CommonSystemData_SetDataInt(struct string *name, int value);
//static bool CommonSystemData_SetDataFloat(struct string *name, float value);
//static bool CommonSystemData_SetDataString(struct string *name, struct string *value);
//static bool CommonSystemData_SetDataBool(struct string *name, bool value);
//static bool CommonSystemData_GetDataInt(struct string *name, int *value);
//static bool CommonSystemData_GetDataFloat(struct string *name, float *value);
//static bool CommonSystemData_GetDataString(struct string *name, struct string **value);
//static bool CommonSystemData_GetDataBool(struct string *name, bool *value);

HLL_LIBRARY(CommonSystemData,
	    HLL_EXPORT(SetFullPathSaveFileName, CommonSystemData_SetFullPathSaveFileName),
	    HLL_EXPORT(LoadAtStartup, CommonSystemData_LoadAtStartup),
	    HLL_TODO_EXPORT(SetDataInt, CommonSystemData_SetDataInt),
	    HLL_TODO_EXPORT(SetDataFloat, CommonSystemData_SetDataFloat),
	    HLL_TODO_EXPORT(SetDataString, CommonSystemData_SetDataString),
	    HLL_TODO_EXPORT(SetDataBool, CommonSystemData_SetDataBool),
	    HLL_TODO_EXPORT(GetDataInt, CommonSystemData_GetDataInt),
	    HLL_TODO_EXPORT(GetDataFloat, CommonSystemData_GetDataFloat),
	    HLL_TODO_EXPORT(GetDataString, CommonSystemData_GetDataString),
	    HLL_TODO_EXPORT(GetDataBool, CommonSystemData_GetDataBool));
