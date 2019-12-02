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

static inline uint32_t PIXEL_MAP(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	return (r << R_SHIFT) | (g << G_SHIFT) | (b << B_SHIFT) | (a << A_SHIFT);
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

typedef void(*pixel_op)(uint32_t *p, uint32_t data);
typedef void(*pixel_op2)(uint32_t *dp, uint32_t *sp, uint32_t data);

// iterator for copy operations
// FIXME: texture should be marked as dirty and updated later to avoid unnecessary copying
static void for_each_pixel_pair(struct cg *dst, int dx, int dy, SDL_Surface *src, int sx, int sy, int w, int h, pixel_op2 op, uint32_t data)
{
	copy_clip_rects(dst->s->w, dst->s->h, &dx, &dy, src->w, src->h, &sx, &sy, &w, &h);
	SDL_LockSurface(dst->s);
	SDL_LockSurface(src);
	for (int row = 0; row < h; row++) {
		uint32_t *dp = get_pixel(dst->s, dx, row+dy);
		uint32_t *sp = get_pixel(src, sx, row+sy);
		for (int col = 0; col < w; col++, dp++, sp++) {
			op(dp, sp, data);
		}
	}
	SDL_UpdateTexture(dst->t, NULL, dst->s->pixels, dst->s->pitch);
	SDL_UnlockSurface(src);
	SDL_UnlockSurface(dst->s);
}

// iterator for fill operations
static void for_each_pixel(struct cg *dst, int x, int y, int w, int h, pixel_op op, uint32_t data)
{
	fill_clip_rect(&x, &y, dst->s->w, dst->s->h, &w, &h);
	SDL_LockSurface(dst->s);
	for (int row = 0; row < h; row++) {
		uint32_t *p = get_pixel(dst->s, x, row+y);
		for (int col = 0; col < w; col++, p++) {
			op(p, data);
		}
	}
	SDL_UpdateTexture(dst->t, NULL, dst->s->pixels, dst->s->pitch);
	SDL_UnlockSurface(dst->s);
}

static void sdl_pixop_copy_color(uint32_t *dp, uint32_t *sp, possibly_unused uint32_t data)
{
	uint8_t r, g, b, a;
	SDL_GetRGBA(*sp, sdl.format, &r, &g, &b, &a);
	PIXEL_SET_RGB(dp, r, g, b);
}

static void sdl_pixop_copy_alpha(uint32_t *dp, uint32_t *sp, possibly_unused uint32_t data)
{
	PIXEL_SET_A(dp, PIXEL_A(*sp));
}

static void sdl_cg_blit(struct cg *dst, int dx, int dy, SDL_Surface *src, int sx, int sy, int w, int h)
{
	Rectangle src_rect = RECT(sx, sy, w, h);
	Rectangle dst_rect = RECT(dx, dy, w, h);
	SDL_BlitSurface(src, &src_rect, dst->s, &dst_rect);
	sdl_update_texture(dst->t, dst->s);
}

void sdl_copy(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h)
{
	SDL_BlendMode mode;
	SDL_GetSurfaceBlendMode(src->s, &mode);
	SDL_SetSurfaceBlendMode(src->s, SDL_BLENDMODE_NONE);
	sdl_cg_blit(dst, dx, dy, src->s, sx, sy, w, h);

	SDL_SetSurfaceBlendMode(src->s, mode);
}

void sdl_copy_bright(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h, int rate)
{
	SDL_BlendMode mode;
	SDL_GetSurfaceBlendMode(src->s, &mode);
	SDL_SetSurfaceBlendMode(src->s, SDL_BLENDMODE_NONE);
	SDL_SetSurfaceColorMod(src->s, rate, rate, rate);

	sdl_cg_blit(dst, dx, dy, src->s, sx, sy, w, h);

	SDL_SetSurfaceColorMod(src->s, 255, 255, 255);
	SDL_SetSurfaceBlendMode(src->s, mode);
}

void sdl_copy_amap(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h)
{
	if (!dst->has_alpha || !src->has_alpha)
		return;
	for_each_pixel_pair(dst, dx, dy, src->s, sx, sy, w, h, sdl_pixop_copy_alpha, 0);
}

void sdl_copy_sprite(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h, int r, int g, int b)
{
	SDL_BlendMode mode;

	SDL_GetSurfaceBlendMode(src->s, &mode);
	SDL_SetSurfaceBlendMode(src->s, SDL_BLENDMODE_NONE);
	SDL_SetColorKey(src->s, SDL_TRUE, SDL_MapRGB(sdl.format, r, g, b));

	sdl_cg_blit(dst, dx, dy, src->s, sx, sy, w, h);

	SDL_SetColorKey(src->s, SDL_FALSE, 0);
	SDL_SetSurfaceBlendMode(src->s, mode);
}

static void sdl_pixop_copy_use_amap_under(uint32_t *dp, uint32_t *sp, uint32_t threshold)
{
	uint8_t r, g, b, a;
	SDL_GetRGBA(*sp, sdl.format, &r, &g, &b, &a);
	if (a <= threshold) {
		PIXEL_SET_RGB(dp, r, g, b);
	}
}

void sdl_copy_use_amap_under(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h, int a_threshold)
{
	if (!src->has_alpha)
		return;
	for_each_pixel_pair(dst, dx, dy, src->s, sx, sy, w, h, sdl_pixop_copy_use_amap_under, a_threshold);
}

static void sdl_pixop_copy_use_amap_border(uint32_t *dp, uint32_t *sp, uint32_t threshold)
{
	uint8_t r, g, b, a;
	SDL_GetRGBA(*sp, sdl.format, &r, &g, &b, &a);
	if (a >= threshold) {
		PIXEL_SET_RGB(dp, r, g, b);
	}
}

void sdl_copy_use_amap_border(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h, int a_threshold)
{
	if (!src->has_alpha)
		return;
	for_each_pixel_pair(dst, dx, dy, src->s, sx, sy, w, h, sdl_pixop_copy_use_amap_border, a_threshold);
}

static void sdl_pixop_copy_amap_max(uint32_t *dp, uint32_t *sp, possibly_unused uint32_t data)
{
	uint32_t d_a = PIXEL_A(*dp);
	uint32_t s_a = PIXEL_A(*sp);
	if (s_a > d_a) {
		PIXEL_SET_A(dp, s_a);
	}
}

void sdl_copy_amap_max(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h)
{
	if (!src->has_alpha || !dst->has_alpha)
		return;
	for_each_pixel_pair(dst, dx, dy, src->s, sx, sy, w, h, sdl_pixop_copy_amap_max, 0);
}

static void sdl_pixop_copy_amap_min(uint32_t *dp, uint32_t *sp, possibly_unused uint32_t data)
{
	uint32_t d_a = PIXEL_A(*dp);
	uint32_t s_a = PIXEL_A(*sp);
	if (s_a < d_a) {
		PIXEL_SET_A(dp, s_a);
	}
}

void sdl_copy_amap_min(struct cg *dst, int dx, int dy, struct cg *src, int sx, int sy, int w, int h)
{
	if (!src->has_alpha || !dst->has_alpha)
		return;
	for_each_pixel_pair(dst, dx, dy, src->s, sx, sy, w, h, sdl_pixop_copy_amap_min, 0);
}

static uint32_t blend_pixels(uint32_t dst, uint32_t src)
{
	uint32_t alpha = PIXEL_A(src) + 1;
	uint32_t inv_alpha = 256 - PIXEL_A(src);
	uint8_t r = (uint8_t)((alpha * PIXEL_R(src) + inv_alpha * PIXEL_R(dst)) >> 8);
	uint8_t g = (uint8_t)((alpha * PIXEL_G(src) + inv_alpha * PIXEL_G(dst)) >> 8);
	uint8_t b = (uint8_t)((alpha * PIXEL_B(src) + inv_alpha * PIXEL_B(dst)) >> 8);
	return PIXEL_MAP(r, g, b, PIXEL_A(dst));
}

static void sdl_pixop_fill_rgb(uint32_t *p, uint32_t c)
{
	PIXEL_SET_A(&c, PIXEL_A(*p));
	*p = c;
}

void sdl_fill(struct cg *dst, int x, int y, int w, int h, int r, int g, int b)
{
	uint32_t c = SDL_MapRGB(sdl.format, r, g, b);
	for_each_pixel(dst, x, y, w, h, sdl_pixop_fill_rgb, c);
}

static void sdl_pixop_fill_alpha_color(uint32_t *p, uint32_t c)
{
	*p = blend_pixels(*p, c);
}

// XXX: Despite being called a fill operation, this function actually performs blending.
void sdl_fill_alpha_color(struct cg *cg, int x, int y, int w, int h, int r, int g, int b, int a)
{
	uint32_t c = SDL_MapRGBA(sdl.format, r, g, b, a);
	for_each_pixel(cg, x, y, w, h, sdl_pixop_fill_alpha_color, c);
}

static void sdl_pixop_fill_alpha(uint32_t *p, uint32_t a)
{
	PIXEL_SET_A(p, a);
}

void sdl_fill_amap(struct cg *dst, int x, int y, int w, int h, int a)
{
	for_each_pixel(dst, x, y, w, h, sdl_pixop_fill_alpha, a);
}

static SDL_Surface *sdl_stretch_surface(SDL_Surface *src, Rectangle *src_rect, int w, int h)
{
	Rectangle dst_rect = RECT(0, 0, w, h);
	SDL_Surface *dst = sdl_create_surface(w, h, NULL);
	SDL_BlitScaled(src, src_rect, dst, &dst_rect);
	return dst;
}

void sdl_copy_stretch(struct cg *dst, int dx, int dy, int dw, int dh, struct cg *src, int sx, int sy, int sw, int sh)
{
	Rectangle s_rect = RECT(sx, sy, sw, sh);
	SDL_Surface *stretched = sdl_stretch_surface(src->s, &s_rect, dw, dh);
	for_each_pixel_pair(dst, dx, dy, stretched, 0, 0, dw, dh, sdl_pixop_copy_color, 0);
	SDL_FreeSurface(stretched);
}

void sdl_copy_stretch_amap(struct cg *dst, int dx, int dy, int dw, int dh, struct cg *src, int sx, int sy, int sw, int sh)
{
	Rectangle s_rect = RECT(sx, sy, sw, sh);
	SDL_Surface *stretched = sdl_stretch_surface(src->s, &s_rect, dw, dh);
	for_each_pixel_pair(dst, dx, dy, stretched, 0, 0, dw, dh, sdl_pixop_copy_alpha, 0);
	SDL_FreeSurface(stretched);
}
