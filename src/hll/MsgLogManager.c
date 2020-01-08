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

#include "hll.h"

HLL_WARN_UNIMPLEMENTED(0, int,  MsgLogManager, Numof, void);
HLL_WARN_UNIMPLEMENTED( , void, MsgLogManager, AddInt, int type, int value);
HLL_WARN_UNIMPLEMENTED( , void, MsgLogManager, AddString, int type, struct string *str);
HLL_WARN_UNIMPLEMENTED(0, int,  MsgLogManager, GetType, int index);
HLL_WARN_UNIMPLEMENTED(0, int,  MsgLogManager, GetInt, int index);
HLL_WARN_UNIMPLEMENTED( , void, MsgLogManager, GetString, int index, struct string **str);
HLL_WARN_UNIMPLEMENTED(0, int,  MsgLogManager, Save, struct string *filename);
HLL_WARN_UNIMPLEMENTED(0, int,  MsgLogManager, Load, struct string *filename);
HLL_WARN_UNIMPLEMENTED( , void, MsgLogManager, SetLineMax, int line_max);
HLL_WARN_UNIMPLEMENTED(0, int,  MsgLogManager, GetInterface, void);

HLL_LIBRARY(MsgLogManager,
	    HLL_EXPORT(Numof, MsgLogManager_Numof),
	    HLL_EXPORT(AddInt, MsgLogManager_AddInt),
	    HLL_EXPORT(AddString, MsgLogManager_AddString),
	    HLL_EXPORT(GetType, MsgLogManager_GetType),
	    HLL_EXPORT(GetInt, MsgLogManager_GetInt),
	    HLL_EXPORT(GetString, MsgLogManager_GetString),
	    HLL_EXPORT(Save, MsgLogManager_Save),
	    HLL_EXPORT(Load, MsgLogManager_Load),
	    HLL_EXPORT(SetLineMax, MsgLogManager_SetLineMax),
	    HLL_EXPORT(GetInterface, MsgLogManager_GetInterface));
