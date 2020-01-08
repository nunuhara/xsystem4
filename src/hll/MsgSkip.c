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

#include <stdbool.h>
#include "hll.h"

static bool enabled = false;
static int state = 0;

void MsgSkip_SetEnable(int enable)
{
	enabled = !!enable;
}

int MsgSkip_GetEnable(void)
{
	return enabled;
}

void MsgSkip_SetState(int _state)
{
	state = _state;
}

int MsgSkip_GetState(void)
{
	return state;
}

HLL_WARN_UNIMPLEMENTED(1, int,  MsgSkip, Init, struct string *name);
HLL_WARN_UNIMPLEMENTED( , void, MsgSkip, UseFlag, int use);
HLL_WARN_UNIMPLEMENTED( , void, MsgSkip, SetFlag, int msgnum);
HLL_WARN_UNIMPLEMENTED(0, int,  MsgSkip, GetFlag, int msgnum);
HLL_WARN_UNIMPLEMENTED(0, int,  MsgSkip, GetNumofMsg, void);
HLL_WARN_UNIMPLEMENTED(0, int,  MsgSkip, GetNumofFlag, void);

HLL_LIBRARY(MsgSkip,
	    HLL_EXPORT(Init, MsgSkip_Init),
	    HLL_EXPORT(UseFlag, MsgSkip_UseFlag),
	    HLL_EXPORT(SetEnable, MsgSkip_SetEnable),
	    HLL_EXPORT(SetState, MsgSkip_SetState),
	    HLL_EXPORT(SetFlag, MsgSkip_SetFlag),
	    HLL_EXPORT(GetEnable, MsgSkip_GetEnable),
	    HLL_EXPORT(GetState, MsgSkip_GetState),
	    HLL_EXPORT(GetFlag, MsgSkip_GetFlag),
	    HLL_EXPORT(GetNumofMsg, MsgSkip_GetNumofMsg),
	    HLL_EXPORT(GetNumofFlag, MsgSkip_GetNumofFlag));
