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
#include <GL/glew.h>
#include "queue.h"
#include "gfx/gfx.h"
#include "gfx/types.h"

struct string;
struct page;
union vm_value;

struct sact_sprite {
	TAILQ_ENTRY(sact_sprite) entry;
	struct texture texture;
	SDL_Color color;
	Rectangle rect;
	struct {
		struct string *str;
		struct texture texture;
		Point home;
		Point pos;
		int char_space;
		int line_space;
	} text;
	int z;
	bool show;
	int no;
	int cg_no;
};

struct sact_sprite *sact_get_sprite(int sp);
struct texture *sact_get_texture(int sp_no);

void sact_render_scene(void);

int sact_Init(void *_, int cg_cache_size);
int sact_SetWP(int cg_no);
int sact_SetWP_Color(int r, int g, int b);
int sact_GetScreenWidth(void);
int sact_GetScreenHeight(void);
int sact_GetMainSurfaceNumber(void);
int sact_Update(void);
int sact_Effect(int type, int time, int key);
int sact_SP_GetUnuseNum(int min);
int sact_SP_Count(void);
int sact_SP_Enum(struct page **array);
int sact_SP_GetMaxZ(void);
int sact_SP_SetCG(int sp, int cg);
int sact_SP_Create(int sp, int width, int height, int r, int g, int b, int a);
int sact_SP_CreatePixelOnly(int sp, int width, int height);
int sact_SP_Delete(int sp);
int sact_SP_SetPos(int sp_no, int x, int y);
int sact_SP_SetX(int sp_no, int x);
int sact_SP_SetY(int sp_no, int y);
int sact_SP_SetZ(int sp, int z);
int sact_SP_GetBlendRate(int sp_no);
int sact_SP_SetBlendRate(int sp_no, int rate);
int sact_SP_SetShow(int sp_no, bool show);
int sact_SP_SetDrawMethod(int sp_no, int method);
int sact_SP_GetDrawMethod(int sp_no);
int sact_SP_IsUsing(int sp_no);
int sact_SP_ExistsAlpha(int sp_no);
int sact_SP_GetPosX(int sp_no);
int sact_SP_GetPosY(int sp_no);
int sact_SP_GetWidth(int sp_no);
int sact_SP_GetHeight(int sp_no);
int sact_SP_GetZ(int sp_no);
int sact_SP_GetShow(int sp_no);
int sact_SP_SetTextHome(int sp_no, int x, int y);
int sact_SP_SetTextLineSpace(int sp_no, int px);
int sact_SP_SetTextCharSpace(int sp_no, int px);
int sact_SP_SetTextPos(int sp_no, int x, int y);
int sact_SP_TextDraw(int sp_no, struct string *text, struct page *tm);
int sact_SP_TextClear(int sp_no);
int sact_SP_TextHome(int sp_no, int size);
int sact_SP_TextNewLine(int sp_no, int size);
int sact_SP_TextCopy(int dno, int sno);
int sact_SP_GetTextHomeX(int sp_no);
int sact_SP_GetTextHomeY(int sp_no);
int sact_SP_GetTextCharSpace(int sp_no);
int sact_SP_GetTextPosX(int sp_no);
int sact_SP_GetTextPosY(int sp_no);
int sact_SP_GetTextLineSpace(int sp_no);
int sact_SP_IsPtIn(int sp_no, int x, int y);
int sact_SP_IsPtInRect(int sp_no, int x, int y);
int sact_CG_GetMetrics(int cg_no, struct page **page);
int sact_SP_GetAMapValue(int sp_no, int x, int y);
int sact_SP_GetPixelValue(int sp_no, int x, int y, int *r, int *g, int *b);

#endif /* SYSTEM4_SACT_H */
