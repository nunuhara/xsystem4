/* 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include <stdbool.h>
#include <SDL.h>
#include "system4.h"
#include "graphics.h"
#include "sdl_core.h"
#include "sdl_private.h"
#include "cg.h"

// FIXME: these can be constants (but depend on endianness)
#define R_MASK (sdl.format->Rmask)
#define G_MASK (sdl.format->Gmask)
#define B_MASK (sdl.format->Bmask)
#define A_MASK (sdl.format->Amask)

#define R_SHIFT (sdl.format->Rshift)
#define G_SHIFT (sdl.format->Gshift)
#define B_SHIFT (sdl.format->Bshift)
#define A_SHIFT (sdl.format->Ashift)

#define PIXEL_R(p) ((p & R_MASK) >> R_SHIFT)
#define PIXEL_G(p) ((p & G_MASK) >> G_SHIFT)
#define PIXEL_B(p) ((p & B_MASK) >> B_SHIFT)
#define PIXEL_A(p) ((p & A_MASK) >> A_SHIFT)

static inline void PIXEL_SET_R(uint32_t *p, uint8_t r)
{
	*p = (*p & ~R_MASK) | (r << R_SHIFT);
}

static inline void PIXEL_SET_G(uint32_t *p, uint8_t g)
{
	*p = (*p & ~G_MASK) | (g << G_SHIFT);
}

static inline void PIXEL_SET_B(uint32_t *p, uint8_t b)
{
	*p = (*p & ~B_MASK) | (b << B_SHIFT);
}

static inline void PIXEL_SET_A(uint32_t *p, uint8_t a)
{
	*p = (*p & ~A_MASK) | (a << A_SHIFT);
}

static inline void PIXEL_SET_RGB(uint32_t *p, uint8_t r, uint8_t g, uint8_t b)
{
	*p = (*p & A_MASK) | (r << R_SHIFT) | (g << G_SHIFT) | (b << B_SHIFT);
}

static inline void PIXEL_SET_RGBA(uint32_t *p, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	*p = (r << R_SHIFT) | (g << G_SHIFT) | (b << B_SHIFT) | (a << A_SHIFT);
}

// Clip src/dst rectangles in preparation for a copy.
static void copy_clip_rects(int dw, int dh, int *dx, int *dy, int sw, int sh, int *sx, int *sy, int *w, int *h)
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
	*w = min(*w, min(dw - *dx, sw - *sx));
	*h = min(*h, min(dh - *dy, sh - *sy));
}

static void fill_clip_rect(int *dx, int *dy, int dw, int dh, int *w, int *h)
{
	if (*dx > dw || *dy > dh) {
		*dx = *dy = *w = *h = 0;
		return;
	}

	if (*dx < 0) {
		*w += *dx;
		*dx = 0;
	}
	if (*dy < 0) {
		*h += *dy;
		*dy = 0;
	}
	*w = min(*w, dw - *dx);
	*h = min(*h, dh - *dy);
}

static inline uint32_t *get_pixel(SDL_Surface *s, int x, int y)
{
	return (uint32_t*)(((uint8_t*)s->pixels) + s->pitch*y + s->format->BytesPerPixel*x);
}

// iterator for copy operations
// FIXME: texture should be marked as dirty and updated later to avoid unnecessary copying
#define copy_for_each_pixel(dcg, dx, dy, scg, sx, sy, w, h, dp, sp, expr) \
	do {								\
		copy_clip_rects(dcg->s->w, dcg->s->h, &dx, &dy, scg->s->w, scg->s->h, &sx, &sy, &w, &h); \
		SDL_LockSurface(dcg->s);				\
		SDL_LockSurface(scg->s);				\
		for (int _row = 0; _row < h; _row++) {			\
			uint32_t *dp = get_pixel(dcg->s, dx, _row+dy);	\
			uint32_t *sp = get_pixel(scg->s, sx, _row+sy);	\
			for (int _col = 0; _col < w; _col++, dp++, sp++) \
				expr;					\
		}							\
		SDL_UpdateTexture(dcg->t, NULL, dcg->s->pixels, dcg->s->pitch); \
		SDL_UnlockSurface(scg->s);				\
		SDL_UnlockSurface(dcg->s);				\
	} while (0)

// iterator for fill operations
#define fill_for_each_pixel(cg, x, y, w, h, p, expr)			\
	do {								\
		fill_clip_rect(&x, &y, cg->s->w, cg->s->h, &w, &h);	\
		SDL_LockSurface(cg->s);					\
		for (int _row = 0; _row < h; _row++) {			\
			uint32_t *p = get_pixel(cg->s, x, _row+y);	\
			for (int _col = 0; _col < w; _col++, p++)	\
				expr;					\
		}							\
		SDL_UpdateTexture(cg->t, NULL, cg->s->pixels, cg->s->pitch); \
		SDL_UnlockSurface(cg->s);				\
	} while(0);

static void sdl_blit(SDL_Surface *ds, int dx, int dy, SDL_Surface *ss, int sx, int sy, int w, int h)
{
	Rectangle src_rect = RECT(sx, sy, w, h);
	Rectangle dst_rect = RECT(dx, dy, w, h);
	SDL_BlitSurface(ss, &src_rect, ds, &dst_rect);
}

void sdl_copy(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h)
{
	SDL_BlendMode mode;
	SDL_GetSurfaceBlendMode(src->s, &mode);
	SDL_SetSurfaceBlendMode(src->s, SDL_BLENDMODE_NONE);
	sdl_blit(dst->s, dx, dy, src->s, sx, sy, w, h);

	SDL_SetSurfaceBlendMode(src->s, mode);
}

void sdl_copy_bright(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h, int rate)
{
	SDL_BlendMode mode;
	SDL_GetSurfaceBlendMode(src->s, &mode);
	SDL_SetSurfaceBlendMode(src->s, SDL_BLENDMODE_NONE);
	SDL_SetSurfaceColorMod(src->s, rate, rate, rate);

	sdl_blit(dst->s, dx, dy, src->s, sx, sy, w, h);

	SDL_SetSurfaceColorMod(src->s, 255, 255, 255);
	SDL_SetSurfaceBlendMode(src->s, mode);
}

void sdl_copy_amap(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h)
{
	if (!dst->has_alpha || !src->has_alpha)
		return;

	copy_for_each_pixel(dst, dx, dy, src, sx, sy, w, h, dp, sp, {
		PIXEL_SET_A(dp, PIXEL_A(*sp));
	});
}

void sdl_copy_sprite(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h, int r, int g, int b)
{
	SDL_BlendMode mode;

	SDL_GetSurfaceBlendMode(src->s, &mode);
	SDL_SetSurfaceBlendMode(src->s, SDL_BLENDMODE_NONE);
	SDL_SetColorKey(src->s, SDL_TRUE, SDL_MapRGB(sdl.format, r, g, b));

	sdl_blit(dst->s, dx, dy, src->s, sx, sy, w, h);

	SDL_SetColorKey(src->s, SDL_FALSE, 0);
	SDL_SetSurfaceBlendMode(src->s, mode);
}

void sdl_copy_use_amap_under(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h, int a_threshold)
{
	if (!src->has_alpha)
		return;

	uint8_t r, g, b, a;
	copy_for_each_pixel(dst, dx, dy, src, sx, sy, w, h, dp, sp, {
		SDL_GetRGBA(*sp, sdl.format, &r, &g, &b, &a);
		if (a <= a_threshold) {
			PIXEL_SET_RGB(dp, r, g, b);
		}
	});
}

void sdl_copy_use_amap_border(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h, int a_threshold)
{
	if (!src->has_alpha)
		return;

	uint8_t r, g, b, a;
	copy_for_each_pixel(dst, dx, dy, src, sx, sy, w, h, dp, sp, {
		SDL_GetRGBA(*sp, sdl.format, &r, &g, &b, &a);
		if (a >= a_threshold) {
			PIXEL_SET_RGB(dp, r, g, b);
		}
	});
}

void sdl_copy_amap_max(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h)
{
	if (!src->has_alpha || !dst->has_alpha)
		return;

	copy_for_each_pixel(dst, dx, dy, src, sx, sy, w, h, dp, sp, {
		uint32_t d_a = (*dp & sdl.format->Amask) >> sdl.format->Ashift;
		uint32_t s_a = (*sp & sdl.format->Amask) >> sdl.format->Ashift;
		if (s_a > d_a) {
			PIXEL_SET_A(dp, s_a);
		}
	});
}

void sdl_copy_amap_min(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h)
{
	if (!src->has_alpha || !dst->has_alpha)
		return;

	copy_for_each_pixel(dst, dx, dy, src, sx, sy, w, h, dp, sp, {
		uint32_t d_a = (*dp & A_MASK) >> A_SHIFT;
		uint32_t s_a = (*sp & A_MASK) >> A_SHIFT;
		if (s_a < d_a) {
			PIXEL_SET_A(dp, s_a);
		}
	});
}

static void blend_pixels(SDL_Color *dst, SDL_Color src)
{
	uint32_t alpha = src.a + 1;
	uint32_t inv_alpha = 256 - src.a;
	dst->r = (uint8_t)((alpha * src.r + inv_alpha * dst->r) >> 8);
	dst->g = (uint8_t)((alpha * src.g + inv_alpha * dst->g) >> 8);
	dst->b = (uint8_t)((alpha * src.b + inv_alpha * dst->b) >> 8);
}

void sdl_fill(struct cg *dst, int x, int y, int w, int h, int r, int g, int b)
{
	fill_for_each_pixel(dst, x, y, w, h, p, {
		PIXEL_SET_RGBA(p, r, g, b, PIXEL_A(*p));
	});
}

// XXX: Despite being called a fill operation, this function actually performs blending.
void sdl_fill_alpha_color(struct cg *cg, int x, int y, int w, int h, int r, int g, int b, int a)
{
	SDL_Color dst;
	SDL_Color src = { .r = r, .g = g, .b = b, .a = a };
	fill_for_each_pixel(cg, x, y, w, h, p, {
		SDL_GetRGBA(*p, sdl.format, &dst.r, &dst.g, &dst.b, &dst.a);
		blend_pixels(&dst, src);
		PIXEL_SET_RGBA(p, dst.r, dst.g, dst.b, dst.a);
	});
}

void sdl_fill_amap(struct cg *dst, int x, int y, int w, int h, int a)
{
	fill_for_each_pixel(dst, x, y, w, h, p, {
		PIXEL_SET_A(p, a);
	});
}
