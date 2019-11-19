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
#include <SDL.h>
#include "graphics.h"

/*
 * Available CG formats
 */
enum cg_type {
	ALCG_UNKNOWN = 1,
	ALCG_BMP24   = 2,
	ALCG_QNT     = 3,
	ALCG_AJP     = 4,
	ALCG_PNG     = 5
};

/*
 * Information for displaying CG data
 */
struct cg {
	enum cg_type type; // cg format type
	int no;
	int z;
	bool show;
	SDL_Color color;
	Rectangle rect;
	SDL_Surface *s;
};

bool cg_exists(int no);
bool cg_load(struct cg *cg, int no);
struct cg *cg_init(int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void cg_reinit(struct cg *cg, int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void cg_free(struct cg *cg);

#endif /* SYSTEM4_CG_H */
