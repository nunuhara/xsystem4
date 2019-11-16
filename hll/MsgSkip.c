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
//int Init(string szName)
hll_warn_unimplemented(MsgSkip, Init, 1);
//void UseFlag(int nUse)
hll_warn_unimplemented(MsgSkip, UseFlag, 0);

//void SetEnable(int nEnable)
hll_defun(SetEnable, args)
{
	enabled = !!args[0].i;
	hll_return(0);
}

//void SetState(int nState)
hll_warn_unimplemented(MsgSkip, SetState, 0);
//void SetFlag(int nMsgNum)
hll_warn_unimplemented(MsgSkip, SetFlag, 0);

//int GetEnable(void)
hll_defun(GetEnable, args)
{
	hll_return(enabled);
}

//int GetState(void)
hll_warn_unimplemented(MsgSkip, GetState, 0);
//int GetFlag(int nMsgNum)
hll_warn_unimplemented(MsgSkip, GetFlag, 0);
//int GetNumofMsg(void)
hll_warn_unimplemented(MsgSkip, GetNumofMsg, 0);
//int GetNumofFlag(void)
hll_warn_unimplemented(MsgSkip, GetNumofFlag, 0);

hll_deflib(MsgSkip) {
	hll_export(Init),
	hll_export(UseFlag),
	hll_export(SetEnable),
	hll_export(SetState),
	hll_export(SetFlag),
	hll_export(GetEnable),
	hll_export(GetState),
	hll_export(GetFlag),
	hll_export(GetNumofMsg),
	hll_export(GetNumofFlag),
	NULL
};
