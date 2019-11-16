/*
 * vsp.h  extract VSP cg
 *
 * Copyright (C) 1997-1998 Masaki Chikama (Wren) <chikama@kasumi.ipl.mech.nagoya-u.ac.jp>
 *               1998-                           <masaki-c@is.aist-nara.ac.jp>
 *               2019 Nunuhara Cabbage           <nunuhara@haniwa.technology>
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

#ifndef SYSTEM4_VSP_H
#define SYSTEM4_VSP_H

#include <stdbool.h>
#include <stdint.h>
#include "cg.h"

struct vsp_header {
	int vspX0;  // display location x
	int vspY0;  // display location y
	int vspXW;  // image width
	int vspYW;  // image height
	int vspPb;  // default pallet bank
	int vspPp;  // pointer to pallet
	int vspDp;  // pointer to pixel data
};
/*
 * vspPb:
 *   VSP has only 16 pallets. When 256 pallets mode, this parameter determine
 *  where to copy these 16 pallets in 256 pallets.
 *  The parameter is from 0 to 15, and for example, if it is 1 then copy
 *  16 palltes into pal[16] ~ pal[31].
 */

bool vsp_checkfmt(uint8_t *data);
struct cg *vsp_extract(uint8_t *data);
struct cg *vsp_getpal(uint8_t *data);

#endif /* SYSTEM4_VSP_H */
