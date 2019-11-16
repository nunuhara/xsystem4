/* Copyright (C) 1997-1998 Masaki Chikama (Wren) <chikama@kasumi.ipl.mech.nagoya-u.ac.jp>
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

#ifndef SYSTEM4_CG_H
#define SYSTEM4_CG_H

#include <stdbool.h>
#include <stdint.h>
#include "graphics.h"

/*
 * Available CG formats
 */
enum cg_type {
	ALCG_UNKNOWN = 1,
	ALCG_VSP     = 2,
	ALCG_PMS8    = 3,
	ALCG_PMS16   = 4,
	ALCG_BMP8    = 5,
	ALCG_BMP24   = 6,
	ALCG_QNT     = 7,
	ALCG_AJP     = 8
};

/*
 * Information for displaying CG data
 */
struct cg {
	enum cg_type type; // cg format type
	int x;             // default display location x
	int y;             // default display location y
	int z;             // draw order
	int width;         // image width
	int height;        // image height

	uint8_t *pic;      // extracted pixel data
	uint8_t *alpha;    // extracted alpha data if exists
	Pallet256 *pal;    // extracted pallet data if exists

	int vsp_bank;      // pallet bank for vsp
	int pms_bank;      // pallet bank for pms

	int spritecolor;   // sprite color for vsp and pms8
	int alphalevel;    // alpha level of image

	int data_offset;   // pic offset for clipping

	int r, g, b, a;
	bool show;
};

bool cg_exists(int no);
struct cg *cg_load(int no);

#endif /* SYSTEM4_CG_H */
