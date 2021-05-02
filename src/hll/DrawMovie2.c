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

HLL_WARN_UNIMPLEMENTED( , void, DrawMovie2, Release);
HLL_WARN_UNIMPLEMENTED(0, bool, DrawMovie2, Load, possibly_unused struct string *filename);
//bool DrawMovie2_Run(void);
//bool DrawMovie2_Draw(int nSprite);
//bool DrawMovie2_SetVolume(int nVolume);
//bool DrawMovie2_IsEnd(void);
//int DrawMovie2_GetCount(void);

HLL_LIBRARY(DrawMovie2,
	    HLL_EXPORT(Release, DrawMovie2_Release),
	    HLL_EXPORT(Load, DrawMovie2_Load),
	    HLL_TODO_EXPORT(Run, DrawMovie2_Run),
	    HLL_TODO_EXPORT(Draw, DrawMovie2_Draw),
	    HLL_TODO_EXPORT(SetVolume, DrawMovie2_SetVolume),
	    HLL_TODO_EXPORT(IsEnd, DrawMovie2_IsEnd),
	    HLL_TODO_EXPORT(GetCount, DrawMovie2_GetCount)
	);
