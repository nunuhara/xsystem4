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

#include "system4.h"
#include "sact.h"
#include "cg.h"
#include "input.h"
#include "sdl_core.h"
#include "vm.h"
#include "vm_string.h"
#include "utfsjis.h"

static struct sact_sprite **sprites = NULL;
static int nr_sprites = 0;

TAILQ_HEAD(listhead, sact_sprite) sprite_list =
	TAILQ_HEAD_INITIALIZER(sprite_list);

struct sact_sprite *sact_get_sprite(int sp)
{
	if (sp < 0 || sp >= nr_sprites)
		return NULL;
	return sprites[sp];
}

static struct sact_sprite *alloc_sprite(int sp)
{
	sprites[sp] = xcalloc(1, sizeof(struct sact_sprite));
	sprites[sp]->no = sp;
	return sprites[sp];
}

static void sprite_register(struct sact_sprite *sp)
{
	struct sact_sprite *p;
	TAILQ_FOREACH(p, &sprite_list, entry) {
		if (p->z > sp->z) {
			TAILQ_INSERT_BEFORE(p, sp, entry);
			return;
		}
	}
	TAILQ_INSERT_TAIL(&sprite_list, sp, entry);
}

static void sprite_unregister(struct sact_sprite *sp)
{
	TAILQ_REMOVE(&sprite_list, sp, entry);
}

int sact_Init(void)
{
	sdl_initialize();
	sdl_text_init();
	return 1;
}

int sact_Update(void)
{
	handle_events();
	struct sact_sprite *p;
	TAILQ_FOREACH(p, &sprite_list, entry) {
		if (!p->show)
			continue;
		if (!p->cg->s)
			p->cg->s = sdl_make_rectangle(p->rect.w, p->rect.h, &p->color);
		sdl_draw_surface(p->cg->s, &p->rect);
		if (p->text.surf)
			sdl_draw_surface(p->text.surf, &p->rect);
	}
	sdl_update_screen();
	return 1;
}

int sact_SP_GetUnuseNum(int min)
{
	for (int i = min; i < nr_sprites; i++) {
		if (!sprites[i])
			return i;
	}

	int old_nr_sprites = nr_sprites;
	int n = max(min, nr_sprites);
	nr_sprites = n + 256;
	sprites = xrealloc(sprites, sizeof(struct sact_sprite*) * nr_sprites);
	memset(sprites + old_nr_sprites, 0, sizeof(struct sact_sprite*) * (nr_sprites - old_nr_sprites));
	return n;
}

int sact_SP_GetMaxZ(void)
{
	if (TAILQ_EMPTY(&sprite_list))
		return 0;
	return TAILQ_LAST(&sprite_list, listhead)->z;
}

int sact_SP_SetCG(int sp_no, int cg_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp)
		sact_SP_Create(sp_no, 1, 1, 0, 0, 0, 0);
	if (!(sp = sact_get_sprite(sp_no))) {
		WARNING("Failed to create sprite");
		return 0;
	}
	if (!cg_load(sp->cg, cg_no - 1))
		return 0;
	sp->rect.w = sp->cg->s->w;
	sp->rect.h = sp->cg->s->h;
	return 1;

}

int sact_SP_Create(int sp_no, int width, int height, int r, int g, int b, int a)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);

	if (!sp) {
		sp = alloc_sprite(sp_no);
	}

	sp->color = (SDL_Color) { .r = r, .g = g, .b = b, .a = a };
	sp->rect.w = width;
	sp->rect.h = height;
	if (sp->cg) {
		cg_reinit(sp->cg);
	} else {
		sp->cg = cg_init();
		sprite_register(sp);
	}
	return 1;
}

int sact_SP_Delete(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_unregister(sp);
	cg_free(sp->cg);
	free(sp);
	sprites[sp_no] = NULL;
	return 1;
}

int sact_SP_SetZ(int sp_no, int z)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_unregister(sp);
	sp->z = z;
	sprite_register(sp);
	return 1;

}

int sact_SP_ExistsAlpha(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	if (!sp->cg->s) return 1;
	return !!sp->cg->s->format->Amask;
}

#define tm_ColorR(tm)       (tm[0].i)
#define tm_ColorG(tm)       (tm[1].i)
#define tm_ColorB(tm)       (tm[2].i)
#define tm_Size(tm)         (tm[3].i)
#define tm_Weight(tm)       (tm[4].i)
#define tm_Face(tm)         (tm[5].i)
#define tm_ShadowPixelL(tm) (tm[6].i)
#define tm_ShadowPixelU(tm) (tm[7].i)
#define tm_ShadowPixelR(tm) (tm[8].i)
#define tm_ShadowPixelD(tm) (tm[9].i)
#define tm_ShadowColorR(tm) (tm[10].i)
#define tm_ShadowColorG(tm) (tm[11].i)
#define tm_ShadowColorB(tm) (tm[12].i)

static void init_text_metrics(struct text_metrics *tm, union vm_value *_tm)
{
	*tm = (struct text_metrics) {
		.color = {
			.r = tm_ColorR(_tm),
			.g = tm_ColorG(_tm),
			.b = tm_ColorB(_tm),
			.a = 255
		},
		.outline_color = {
			.r = tm_ShadowColorR(_tm),
			.g = tm_ShadowColorG(_tm),
			.b = tm_ShadowColorB(_tm),
			.a = 255
		},
		.size          = tm_Size(_tm),
		.weight        = tm_Weight(_tm),
		.face          = tm_Face(_tm),
		.outline_left  = tm_ShadowPixelL(_tm),
		.outline_up    = tm_ShadowPixelU(_tm),
		.outline_right = tm_ShadowPixelR(_tm),
		.outline_down  = tm_ShadowPixelD(_tm),
	};
}

int sact_SP_TextDraw(int sp_no, struct string *text, union vm_value *_tm)
{
	struct text_metrics tm;
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;

	if (!sp->text.surf) {
		SDL_Color c = { 0, 0, 0, 0 };
		sp->text.surf = sdl_make_rectangle(sp->rect.w, sp->rect.h, &c);
	}

	init_text_metrics(&tm, _tm);
	sp->text.pos.x += sdl_render_text(sp->text.surf, sp->text.pos, text->text, &tm);
	return 1;
}

int sact_SP_TextClear(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;

	if (sp->text.surf) {
		SDL_FreeSurface(sp->text.surf);
		sp->text.surf = NULL;
	}
	return 1;
}

//
// --- DrawGraph.dll ---
//

struct sact_sprite *DrawGraph_get_sprite(int no)
{
	struct sact_sprite *sp = sact_get_sprite(no);
	if (!sp)
		ERROR("Invalid sprite number: %d", no);
	if (!sp->cg->s)
		sp->cg->s = sdl_make_rectangle(sp->rect.w, sp->rect.h, &sp->color);
	return sp;
}

static void DrawGraph_Blit(SDL_Surface *ds, int dx, int dy, SDL_Surface *ss, int sx, int sy, int w, int h)
{
	Rectangle src_rect = { .x = sx, .y = sy, .w = w, .h = h };
	Rectangle dst_rect = { .x = dx, .y = dy, .w = w, .h = h };
	SDL_BlitSurface(ss, &src_rect, ds, &dst_rect);
}

void DrawGraph_Copy(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h)
{
	SDL_BlendMode mode;
	SDL_Surface *ds = DrawGraph_get_sprite(dno)->cg->s;
	SDL_Surface *ss = DrawGraph_get_sprite(sno)->cg->s;

	SDL_GetSurfaceBlendMode(ss, &mode);
	SDL_SetSurfaceBlendMode(ss, SDL_BLENDMODE_NONE);

	DrawGraph_Blit(ds, dx, dy, ss, sx, sy, w, h);

	SDL_SetSurfaceBlendMode(ss, mode);
}

void DrawGraph_CopyBright(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h, int rate)
{
	SDL_BlendMode mode;
	SDL_Surface *ds = DrawGraph_get_sprite(dno)->cg->s;
	SDL_Surface *ss = DrawGraph_get_sprite(sno)->cg->s;

	SDL_GetSurfaceBlendMode(ss, &mode);
	SDL_SetSurfaceBlendMode(ss, SDL_BLENDMODE_NONE);
	SDL_SetSurfaceColorMod(ss, rate, rate, rate);

	DrawGraph_Blit(ds, dx, dy, ss, sx, sy, w, h);

	SDL_SetSurfaceColorMod(ss, 255, 255, 255);
	SDL_SetSurfaceBlendMode(ss, mode);
}

// Clip src/dst rectangles in preparation for a copy.
static void clip_rects(SDL_Surface *ds, int *dx, int *dy, SDL_Surface *ss, int *sx, int *sy, int *w, int *h)
{
	if (*sx < 0) {
		*dx -= *sx;
		*w += *sx;
		*sx = 0;
	}
	if (*sy < 0) {
		*dy -= *sy;
		*h += *sy;
		*sy = 0;
	}
	if (*dx < 0) {
		*sx -= *dx;
		*w += *dx;
		*dx = 0;
	}
	if (*dy < 0) {
		*sy -= *dy;
		*w += *dy;
		*dy = 0;
	}
	*w = min(*w, min(ds->w - *dx, ss->w - *sx));
	*h = min(*h, min(ds->h - *dy, ss->h - *sy));
}

static inline uint32_t *get_pixel(SDL_Surface *s, int x, int y)
{
	return (uint32_t*)(((uint8_t*)s->pixels) + s->pitch*y + s->format->BytesPerPixel*x);
}

static inline void next_pixel(SDL_Surface *s, uint32_t **p)
{
	*p = (uint32_t*)((uint8_t*)*p + s->format->BytesPerPixel);
}

// iterator for copy operations
#define copy_for_each_pixel(ds, dx, dy, ss, sx, sy, w, h, dp, sp, expr)	\
	do {								\
		clip_rects(ds, &dx, &dy, ss, &sx, &sy, &w, &h);		\
		SDL_LockSurface(ds);					\
		SDL_LockSurface(ss);					\
		for (int _row = 0; _row < h; _row++) {			\
			uint32_t *dp = get_pixel(ds, dx, _row+dy);	\
			uint32_t *sp = get_pixel(ss, sx, _row+sy);	\
			for (int _col = 0; _col < w; _col++, next_pixel(ds, &dp), next_pixel(ss, &sp)) \
				expr;					\
		}							\
		SDL_UnlockSurface(ss);					\
		SDL_UnlockSurface(ds);					\
	} while (0)

void DrawGraph_CopyAMap(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h)
{
	SDL_Surface *ds = DrawGraph_get_sprite(dno)->cg->s;
	SDL_Surface *ss = DrawGraph_get_sprite(sno)->cg->s;

	if (!ds->format->Amask || !ss->format->Amask)
		return;

	copy_for_each_pixel(ds, dx, dy, ss, sx, sy, w, h, dp, sp, {
		uint32_t a = (*sp & ss->format->Amask) >> ss->format->Ashift;
		*dp &= ~ds->format->Amask;
		*dp |= (a << ds->format->Ashift);
	});
}

void DrawGraph_CopySprite(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h, int r, int g, int b)
{
	SDL_BlendMode mode;
	SDL_Surface *ds = DrawGraph_get_sprite(dno)->cg->s;
	SDL_Surface *ss = DrawGraph_get_sprite(sno)->cg->s;

	SDL_GetSurfaceBlendMode(ss, &mode);
	SDL_SetSurfaceBlendMode(ss, SDL_BLENDMODE_NONE);
	SDL_SetColorKey(ss, SDL_TRUE, SDL_MapRGB(ss->format, r, g, b));

	DrawGraph_Blit(ds, dx, dy, ss, sx, sy, w, h);

	SDL_SetColorKey(ss, SDL_FALSE, 0);
	SDL_SetSurfaceBlendMode(ss, mode);
}

void DrawGraph_CopyUseAMapUnder(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h, int a_threshold)
{
	SDL_Surface *ds = DrawGraph_get_sprite(dno)->cg->s;
	SDL_Surface *ss = DrawGraph_get_sprite(sno)->cg->s;

	if (!ss->format->Amask)
		return;

	uint8_t r, g, b, a;
	copy_for_each_pixel(ds, dx, dy, ss, sx, sy, w, h, dp, sp, {
		SDL_GetRGBA(*sp, ss->format, &r, &g, &b, &a);
		if (a <= a_threshold) {
			*dp &= ~(ds->format->Rmask | ds->format->Gmask | ds->format->Bmask);
			*dp |= (uint32_t)r << ds->format->Rshift;
			*dp |= (uint32_t)g << ds->format->Gshift;
			*dp |= (uint32_t)b << ds->format->Bshift;
		}
	});
}

void DrawGraph_CopyUseAMapBorder(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h, int a_threshold)
{
	SDL_Surface *ds = DrawGraph_get_sprite(dno)->cg->s;
	SDL_Surface *ss = DrawGraph_get_sprite(sno)->cg->s;

	if (!ss->format->Amask)
		return;

	uint8_t r, g, b, a;
	copy_for_each_pixel(ds, dx, dy, ss, sx, sy, w, h, dp, sp, {
			SDL_GetRGBA(*sp, ss->format, &r, &g, &b, &a);
			if (a >= a_threshold) {
				*dp &= ~(ds->format->Rmask | ds->format->Gmask | ds->format->Bmask);
				*dp |= (uint32_t)r << ds->format->Rshift;
				*dp |= (uint32_t)g << ds->format->Gshift;
				*dp |= (uint32_t)b << ds->format->Bshift;
			}
		});
}

void DrawGraph_CopyAMapMax(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h)
{
	SDL_Surface *ds = DrawGraph_get_sprite(dno)->cg->s;
	SDL_Surface *ss = DrawGraph_get_sprite(sno)->cg->s;

	if (!ss->format->Amask || !ds->format->Amask)
		return;

	copy_for_each_pixel(ds, dx, dy, ss, sx, sy, w, h, dp, sp, {
		uint32_t d_a = (*dp & ds->format->Amask) >> ds->format->Ashift;
		uint32_t s_a = (*sp & ss->format->Amask) >> ss->format->Ashift;
		if (s_a > d_a) {
			*dp &= ~ds->format->Amask;
			*dp |= s_a << ds->format->Ashift;
		}
	});
}

void DrawGraph_CopyAMapMin(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h)
{
	SDL_Surface *ds = DrawGraph_get_sprite(dno)->cg->s;
	SDL_Surface *ss = DrawGraph_get_sprite(sno)->cg->s;

	if (!ss->format->Amask || !ds->format->Amask)
		return;

	copy_for_each_pixel(ds, dx, dy, ss, sx, sy, w, h, dp, sp, {
		uint32_t d_a = (*dp & ds->format->Amask) >> ds->format->Ashift;
		uint32_t s_a = (*sp & ss->format->Amask) >> ss->format->Ashift;
		if (s_a < d_a) {
			*dp &= ~ds->format->Amask;
			*dp |= s_a << ds->format->Ashift;
		}
	});
}
