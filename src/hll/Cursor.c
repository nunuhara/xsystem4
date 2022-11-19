/* Copyright (C) 2022 kichikuou <KichikuouChrome@gmail.com>
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

#include "input.h"
#include "hll.h"

#define TT3_CURSOR_TRANSPARENT 11

HLL_WARN_UNIMPLEMENTED(1, int, Cursor, Load, int nNum, int nLinkNum);

static void Cursor_Set(int num)
{
	switch (num) {
	case 0:
		mouse_show_cursor(true);
		break;
	case TT3_CURSOR_TRANSPARENT:
		mouse_show_cursor(false);
		break;
	default:
		WARNING("Cursor.Set: unexpected argument %d", num);
		break;
	}
}

HLL_LIBRARY(Cursor,
	    HLL_EXPORT(Load, Cursor_Load),
	    HLL_EXPORT(Set, Cursor_Set)
	    );
