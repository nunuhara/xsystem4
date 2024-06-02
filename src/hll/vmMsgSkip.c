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

#include "hll.h"

HLL_WARN_UNIMPLEMENTED(, void, vmMsgSkip, Init, void *imainsystem);
//int vmMsgSkip_EnumHandle(void);
//int vmMsgSkip_Open(struct string *pIString, int nOption);
//void vmMsgSkip_Close(int hMsgSkip);
//int vmMsgSkip_Query(int hMsgSkip, int nMsgNum);
//void vmMsgSkip_SetEnable(int nEnable);
HLL_QUIET_UNIMPLEMENTED(, void, vmMsgSkip, SetState, int state);
//int vmMsgSkip_GetEnable(void);
//int vmMsgSkip_GetState(void);

HLL_LIBRARY(vmMsgSkip,
	    HLL_EXPORT(Init, vmMsgSkip_Init),
	    HLL_TODO_EXPORT(EnumHandle, vmMsgSkip_EnumHandle),
	    HLL_TODO_EXPORT(Open, vmMsgSkip_Open),
	    HLL_TODO_EXPORT(Close, vmMsgSkip_Close),
	    HLL_TODO_EXPORT(Query, vmMsgSkip_Query),
	    HLL_TODO_EXPORT(SetEnable, vmMsgSkip_SetEnable),
	    HLL_EXPORT(SetState, vmMsgSkip_SetState),
	    HLL_TODO_EXPORT(GetEnable, vmMsgSkip_GetEnable),
	    HLL_TODO_EXPORT(GetState, vmMsgSkip_GetState)
	    );
