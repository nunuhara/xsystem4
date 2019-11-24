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

#ifndef SYSTEM4_SACT_H
#define SYSTEM4_SACT_H

#include <stdbool.h>
#include <SDL.h>
#include "queue.h"
#include "graphics.h"

struct cg;
struct string;
union vm_value;

struct sact_sprite {
	TAILQ_ENTRY(sact_sprite) entry;
	struct cg *cg;
	SDL_Color color;
	Rectangle rect;
	struct {
		struct string *str;
		SDL_Surface *surf;
		Point home;
		Point pos;
	} text;
	int z;
	bool show;
	int no;
};

struct sact_sprite *sact_get_sprite(int sp);

int sact_Init(void);
int sact_Update(void);
int sact_SP_GetUnuseNum(int min);
int sact_SP_GetMaxZ(void);
int sact_SP_SetCG(int sp, int cg);
int sact_SP_Create(int sp, int width, int height, int r, int g, int b, int a);
int sact_SP_Delete(int sp);
int sact_SP_SetZ(int sp, int z);
int sact_SP_TextDraw(int sp_no, struct string *text, union vm_value *tm);
int sact_SP_TextClear(int sp_no);

#endif /* SYSTEM4_SACT_H */
