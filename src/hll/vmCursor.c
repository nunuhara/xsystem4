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
#include "input.h"

HLL_QUIET_UNIMPLEMENTED( , void, vmCursor, Init, void *imainsystem);

static void vmCursor_SetPos(int x, int y)
{
	mouse_set_pos(x, y);
}

static int vmCursor_GetPos(int *x, int *y)
{
	handle_events();
	mouse_get_pos(x, y);
	return mouse_focus && keyboard_focus;
}

//void vmCursor_SetShow(int nShow);
//int vmCursor_GetShow(void);

HLL_LIBRARY(vmCursor,
	    HLL_EXPORT(Init, vmCursor_Init),
	    HLL_EXPORT(SetPos, vmCursor_SetPos),
	    HLL_EXPORT(GetPos, vmCursor_GetPos),
	    HLL_TODO_EXPORT(SetShow, vmCursor_SetShow),
	    HLL_TODO_EXPORT(GetShow, vmCursor_GetShow)
	    );
