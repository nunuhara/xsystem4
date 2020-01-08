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

HLL_WARN_UNIMPLEMENTED(0, int, PlayMovie, Init, void);
HLL_UNIMPLEMENTED(int, PlayMovie, Load, struct string *filename);
HLL_UNIMPLEMENTED(int, PlayMovie, Play, void);
HLL_UNIMPLEMENTED(void, PlayMovie, Stop, void);
HLL_UNIMPLEMENTED(int, PlayMovie, IsPlay, void);
HLL_UNIMPLEMENTED(void, PlayMovie, Release, void);

HLL_LIBRARY(PlayMovie,
	    HLL_EXPORT(Init, PlayMovie_Init),
	    HLL_EXPORT(Load, PlayMovie_Load),
	    HLL_EXPORT(Play, PlayMovie_Play),
	    HLL_EXPORT(Stop, PlayMovie_Stop),
	    HLL_EXPORT(IsPlay, PlayMovie_IsPlay),
	    HLL_EXPORT(Release, PlayMovie_Release));
