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

HLL_WARN_UNIMPLEMENTED(false, bool, PixelRestore, Init, int dest_sprite, int dest_x, int dest_y, int src_sprite, int total_time, int get_time);
HLL_WARN_UNIMPLEMENTED(false, bool, PixelRestore, Draw, int get_time);
HLL_WARN_UNIMPLEMENTED(, void, PixelRestore, Set, int now_time);
HLL_WARN_UNIMPLEMENTED(, void, PixelRestore, Delete, void);

HLL_LIBRARY(PixelRestore,
	    HLL_EXPORT(Init, PixelRestore_Init),
	    HLL_EXPORT(Draw, PixelRestore_Draw),
	    HLL_EXPORT(Set, PixelRestore_Set),
	    HLL_EXPORT(Delete, PixelRestore_Delete)
	    );
