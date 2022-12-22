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

#include "hll.h"

HLL_WARN_UNIMPLEMENTED( , void, DrawMovie, Release);
HLL_WARN_UNIMPLEMENTED(0, bool, DrawMovie, Load, possibly_unused struct string *filename);
//bool DrawMovie_Run(void);
//bool DrawMovie_Draw(int nSprite);
//bool DrawMovie_SetVolume(int nVolume);
//bool DrawMovie_IsEnd(void);
//int DrawMovie_GetCount(void);

HLL_LIBRARY(DrawMovie,
	    HLL_EXPORT(Release, DrawMovie_Release),
	    HLL_EXPORT(Load, DrawMovie_Load),
	    HLL_TODO_EXPORT(Run, DrawMovie_Run),
	    HLL_TODO_EXPORT(Draw, DrawMovie_Draw),
	    HLL_TODO_EXPORT(SetVolume, DrawMovie_SetVolume),
	    HLL_TODO_EXPORT(IsEnd, DrawMovie_IsEnd),
	    HLL_TODO_EXPORT(GetCount, DrawMovie_GetCount)
	);
