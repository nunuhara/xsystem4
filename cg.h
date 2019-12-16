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

/*
 * Available CG formats
 */
enum cg_type {
	ALCG_UNKNOWN = 1,
	ALCG_QNT     = 2,
	ALCG_AJP     = 3,
	ALCG_PNG     = 4
};

struct cg_metrics {
	int w;
	int h;
	int bpp;
	bool has_pixel;
	bool has_alpha;
	int pixel_pitch;
	int alpha_pitch;
};

/*
 * Information for displaying CG data
 */
struct cg {
	enum cg_type type; // cg format type
	struct cg_metrics metrics;
	void *pixels;
};

bool cg_exists(int no);
bool cg_get_metrics(int no, struct cg_metrics *dst);
struct cg *cg_load(int no);
void cg_free(struct cg *cg);

#endif /* SYSTEM4_CG_H */
