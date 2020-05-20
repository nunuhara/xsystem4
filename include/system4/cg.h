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

struct archive;

/*
 * Available CG formats
 */
enum cg_type {
	ALCG_UNKNOWN = 1,
	ALCG_QNT     = 2,
	ALCG_AJP     = 3,
	ALCG_PNG     = 4,
	ALCG_PMS8    = 5,
	ALCG_PMS16   = 6,
	ALCG_WEBP    = 7,
	ALCG_DCF     = 8,
};

struct cg_palette {
	uint8_t red[256];
	uint8_t green[256];
	uint8_t blue[256];
};

struct cg_metrics {
	int x;
	int y;
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
	struct cg_palette *pal;
	void *pixels;
};

struct archive_data;

enum cg_type cg_check_format(uint8_t *data);
bool cg_get_metrics(struct archive *ar, int no, struct cg_metrics *dst);
struct cg *cg_load_data(struct archive_data *dfile);
struct cg *cg_load(struct archive *ar, int no);
void cg_free(struct cg *cg);

#endif /* SYSTEM4_CG_H */
