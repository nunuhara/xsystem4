/* Copyright (C) 2021 kichikuou <KichikuouChrome@gmail.com>
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

static bool enabled = false;
static int state = 0;

HLL_WARN_UNIMPLEMENTED( , void, MenuMsg, Init, void *imainsystem);

static void MenuMsg_SetState(int flag)
{
	state = flag;
}

static void MenuMsg_SetEnable(int enable)
{
	enabled = !!enable;
}

static int MenuMsg_GetState(void)
{
	return state;
}

static int MenuMsg_GetEnable(void)
{
	return enabled;
}

HLL_LIBRARY(MenuMsg,
			HLL_EXPORT(Init, MenuMsg_Init),
			HLL_EXPORT(SetState, MenuMsg_SetState),
			HLL_EXPORT(SetEnable, MenuMsg_SetEnable),
			HLL_EXPORT(GetState, MenuMsg_GetState),
			HLL_EXPORT(GetEnable, MenuMsg_GetEnable));
