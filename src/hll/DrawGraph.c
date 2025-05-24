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

#include "hll.h"
#include "sact.h"
#include "gfx/gfx.h"
#include "gfx/font.h"
#include "xsystem4.h"
#include "system4/string.h"

// Get texture for source sprite
static struct texture *STEX(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return NULL;
	return sprite_get_texture(sp);
}

// Get texture for destination sprite
static struct texture *DTEX(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return NULL;
	sprite_dirty(sp);
	return sprite_get_texture(sp);
}

static void DrawGraph_Copy(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_copy(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

static void DrawGraph_CopyBright(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int rate)
{
	gfx_copy_bright(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h, rate);
}

static void DrawGraph_CopyAMap(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_copy_amap(STEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

static void DrawGraph_CopySprite(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int r, int g, int b)
{
	gfx_copy_sprite(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h, COLOR(r, g, b, 0));
}

static void DrawGraph_CopyColorReverse(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_copy_color_reverse(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

static void DrawGraph_CopyUseAMapUnder(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int alpha)
{
	gfx_copy_use_amap_under(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h, alpha);
}

static void DrawGraph_CopyUseAMapBorder(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int alpha)
{
	gfx_copy_use_amap_border(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h, alpha);
}

static void DrawGraph_CopyAMapMax(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_copy_amap_max(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

static void DrawGraph_CopyAMapMin(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_copy_amap_min(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

static void DrawGraph_Blend(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int alpha)
{
	gfx_blend(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h, alpha);
}

static void DrawGraph_BlendSrcBright(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int alpha, int rate)
{
	gfx_blend_src_bright(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h, alpha, rate);
}

static void DrawGraph_BlendAddSatur(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_blend_add_satur(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

static void DrawGraph_BlendAMap(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_blend_amap(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

static void DrawGraph_BlendAMapSrcOnly(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_blend_amap_src_only(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

static void DrawGraph_BlendAMapColor(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int r, int g, int b)
{
	gfx_blend_amap_color(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h, r, g, b);
}

static void DrawGraph_BlendAMapColorAlpha(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int r, int g, int b, int a)
{
	gfx_blend_amap_color_alpha(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h, r, g, b, a);
}

static void DrawGraph_BlendAMapAlpha(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int alpha)
{
	gfx_blend_amap_alpha(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h, alpha);
}

static void DrawGraph_BlendAMapBright(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int rate)
{
	gfx_blend_amap_bright(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h, rate);
}

static void DrawGraph_BlendAMapAlphaSrcBright(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int alpha, int rate)
{
	gfx_blend_amap_alpha_src_bright(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h, alpha, rate);
}

static void DrawGraph_BlendUseAMapColor(int dst, int dx, int dy, int alpha, int ax, int ay, int w, int h, int r, int g, int b, int rate)
{
	gfx_blend_use_amap_color(DTEX(dst), dx, dy, STEX(alpha), ax, ay, w, h, r, g, b, rate);
}

static void DrawGraph_BlendScreen(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_blend_screen(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

static void DrawGraph_BlendMultiply(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_blend_multiply(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

static void DrawGraph_BlendScreenAlpha(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int alpha)
{
	gfx_blend_screen_alpha(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h, alpha);
}

static void DrawGraph_Fill(int dst, int x, int y, int w, int h, int r, int g, int b)
{
	gfx_fill(DTEX(dst), x, y, w, h, r, g, b);
}

static void DrawGraph_FillAlphaColor(int dst, int x, int y, int w, int h, int r, int g, int b, int rate)
{
	gfx_fill_alpha_color(DTEX(dst), x, y, w, h, r, g, b, rate);
}

static void DrawGraph_FillAMap(int dst, int x, int y, int w, int h, int alpha)
{
	gfx_fill_amap(DTEX(dst), x, y, w, h, alpha);
	if (game_rance7_mg) {
		// Short-term fix for https://github.com/nunuhara/xsystem4/issues/237
		sprite_text_clear(sact_get_sprite(dst));
	}
}

static void DrawGraph_FillAMapOverBorder(int dst, int x, int y, int w, int h, int alpha, int border)
{
	gfx_fill_amap_over_border(DTEX(dst), x, y, w, h, alpha, border);
}

static void DrawGraph_FillAMapUnderBorder(int dst, int x, int y, int w, int h, int alpha, int border)
{
	gfx_fill_amap_under_border(DTEX(dst), x, y, w, h, alpha, border);
}

static void DrawGraph_FillAMapGradationUD(int dst, int x, int y, int w, int h, int up_a, int down_a)
{
	gfx_fill_amap_gradation_ud(DTEX(dst), x, y, w, h, up_a, down_a);
}

static void DrawGraph_FillScreen(int dst, int x, int y, int w, int h, int r, int g, int b)
{
	gfx_fill_screen(DTEX(dst), x, y, w, h, r, g, b);
}

static void DrawGraph_FillMultiply(int dst, int x, int y, int w, int h, int r, int g, int b)
{
	gfx_fill_multiply(DTEX(dst), x, y, w, h, r, g, b);
}

static void DrawGraph_SaturDP_DPxSA(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_satur_dp_dpxsa(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

static void DrawGraph_ScreenDA_DAxSA(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_screen_da_daxsa(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

static void DrawGraph_AddDA_DAxSA(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_add_da_daxsa(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

static void DrawGraph_SpriteCopyAMap(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int key)
{
	gfx_sprite_copy_amap(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h, key);
}

static void DrawGraph_BlendDA_DAxSA(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_blend_da_daxsa(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

static void DrawGraph_SubDA_DAxSA(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_sub_da_daxsa(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

static void DrawGraph_BrightDestOnly(int dst, int x, int y, int w, int h, int rate)
{
	gfx_bright_dest_only(DTEX(dst), x, y, w, h, rate);
}

//void DrawGraph_CopyTextureWrap(int dst, int dx, int dy, int dw, int dh, int src, int sx, int sy, int sw, int sh, int u, int v);
//void DrawGraph_CopyTextureWrapAlpha(int dst, int dx, int dy, int dw, int dh, int src, int sx, int sy, int sw, int sh, int u, int v, int alpha);

static void DrawGraph_CopyStretch(int dst, int dx, int dy, int dw, int dh, int src, int sx, int sy, int sw, int sh)
{
	gfx_copy_stretch(DTEX(dst), dx, dy, dw, dh, STEX(src), sx, sy, sw, sh);
}

static void DrawGraph_CopyStretchBlend(int dst, int dx, int dy, int dw, int dh, int src, int sx, int sy, int sw, int sh, int alpha)
{
	gfx_copy_stretch_blend(DTEX(dst), dx, dy, dw, dh, STEX(src), sx, sy, sw, sh, alpha);
}

static void DrawGraph_CopyStretchBlendAMap(int dst, int dx, int dy, int dw, int dh, int src, int sx, int sy, int sw, int sh)
{
	gfx_copy_stretch_blend_amap(DTEX(dst), dx, dy, dw, dh, STEX(src), sx, sy, sw, sh);
}

static void DrawGraph_CopyStretchAMap(int dst, int dx, int dy, int dw, int dh, int src, int sx, int sy, int sw, int sh)
{
	gfx_copy_stretch_amap(DTEX(dst), dx, dy, dw, dh, STEX(src), sx, sy, sw, sh);
}

//void DrawGraph_CopyStretchInterp(int dst, int dx, int dy, int dw, int dh, int src, int sx, int sy, int sw, int sh);
//void DrawGraph_CopyStretchAMapInterp(int dst, int dx, int dy, int dw, int dh, int src, int sx, int sy, int sw, int sh);

static void DrawGraph_DrawTextToPMap(int dst, int x, int y, struct string *s)
{
	gfx_draw_text_to_pmap(DTEX(dst), x, y, s->text);
}

static void DrawGraph_DrawTextToAMap(int dst, int x, int y, struct string *s)
{
	gfx_draw_text_to_amap(DTEX(dst), x, y, s->text);
}

static void DrawGraph_SetFontSize(int size)
{
	gfx_set_font_size(size);
}

static void DrawGraph_SetFontName(struct string *text)
{
	gfx_set_font_name(text->text);
}

static void DrawGraph_SetFontWeight(int weight)
{
	gfx_set_font_weight(weight);
}

static void DrawGraph_SetFontUnderline(int flag)
{
	gfx_set_font_underline(flag);
}

static void DrawGraph_SetFontStrikeOut(int flag)
{
	gfx_set_font_strikeout(flag);
}

static void DrawGraph_SetFontSpace(int space)
{
	gfx_set_font_space(space);
}

HLL_WARN_UNIMPLEMENTED(string_ref(&EMPTY_STRING), struct string*, DrawGraph, GetFontName, void);

static void DrawGraph_SetFontColor(int r, int g, int b)
{
	gfx_set_font_color(COLOR(r, g, b, 255));
}

static void DrawGraph_GetFontColor(int *r, int *g, int *b)
{
	SDL_Color c = gfx_get_font_color();
	*r = c.r;
	*g = c.g;
	*b = c.b;
}

static void DrawGraph_CopyRotZoom(int dst, int src, int sx, int sy, int w, int h, float rotate, float mag)
{
	gfx_copy_rot_zoom(DTEX(dst), STEX(src), sx, sy, w, h, rotate, mag);
}

static void DrawGraph_CopyRotZoomAMap(int dst, int src, int sx, int sy, int w, int h, float rotate, float mag)
{
	gfx_copy_rot_zoom_amap(DTEX(dst), STEX(src), sx, sy, w, h, rotate, mag);
}

static void DrawGraph_CopyRotZoomUseAMap(int dst, int src, int sx, int sy, int w, int h, float rotate, float mag)
{
	gfx_copy_rot_zoom_use_amap(DTEX(dst), STEX(src), sx, sy, w, h, rotate, mag);
}

static void DrawGraph_CopyRotZoom2Bilinear(int dst, float cx, float cy, int src, float scx, float scy, float rot, float mag)
{
	gfx_copy_rot_zoom2(DTEX(dst), cx, cy, STEX(src), scx, scy, rot, mag);
}

static void DrawGraph_CopyRotateY(int write, int dst, int src, int sx, int sy, int w, int h, float rot, float mag)
{
	gfx_copy_rotate_y(DTEX(write), STEX(dst), STEX(src), sx, sy, w, h, rot, mag);
}

static void DrawGraph_CopyRotateYUseAMap(int write, int dst, int src, int sx, int sy, int w, int h, float rot, float mag)
{
	gfx_copy_rotate_y_use_amap(DTEX(write), STEX(dst), STEX(src), sx, sy, w, h, rot, mag);
}

//void DrawGraph_CopyRotateYFixL(int write, int dst, int src, int sx, int sy, int w, int h, float rot, float mag);
//void DrawGraph_CopyRotateYFixR(int write, int dst, int src, int sx, int sy, int w, int h, float rot, float mag);
//void DrawGraph_CopyRotateYFixLUseAMap(int write, int dst, int src, int sx, int sy, int w, int h, float rot, float mag);
//void DrawGraph_CopyRotateYFixRUseAMap(int write, int dst, int src, int sx, int sy, int w, int h, float rot, float mag);

static void DrawGraph_CopyRotateX(int write, int dst, int src, int sx, int sy, int w, int h, float rot, float mag)
{
	gfx_copy_rotate_x(DTEX(write), STEX(dst), STEX(src), sx, sy, w, h, rot, mag);
}

static void DrawGraph_CopyRotateXUseAMap(int write, int dst, int src, int sx, int sy, int w, int h, float rot, float mag)
{
	gfx_copy_rotate_x_use_amap(DTEX(write), STEX(dst), STEX(src), sx, sy, w, h, rot, mag);
}

//void DrawGraph_CopyRotateXFixU(int write, int dst, int src, int sx, int sy, int w, int h, float rot, float mag);
//void DrawGraph_CopyRotateXFixD(int write, int dst, int src, int sx, int sy, int w, int h, float rot, float mag);
//void DrawGraph_CopyRotateXFixUUseAMap(int write, int dst, int src, int sx, int sy, int w, int h, float rot, float mag);
//void DrawGraph_CopyRotateXFixDUseAMap(int write, int dst, int src, int sx, int sy, int w, int h, float rot, float mag);

static void DrawGraph_CopyReverseLR(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_copy_reverse_LR(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

static void DrawGraph_CopyReverseUD(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_copy_reverse_UD(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

static void DrawGraph_CopyReverseAMapLR(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_copy_reverse_amap_LR(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

//void DrawGraph_CopyReverseAMapUD(int dst, int dx, int dy, int src, int sx, int sy, int w, int h);

static void DrawGraph_CopyReverseLRWithAMap(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_copy_reverse_LR_with_alpha_map(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

//void DrawGraph_CopyReverseUDWithAMap(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight);

static void DrawGraph_CopyWidthBlur(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int blur)
{
	gfx_copy_width_blur(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h, blur);
}

static void DrawGraph_CopyHeightBlur(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int blur)
{
	gfx_copy_height_blur(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h, blur);
}

static void DrawGraph_CopyAMapWidthBlur(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int blur)
{
	gfx_copy_amap_width_blur(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h, blur);
}

static void DrawGraph_CopyAMapHeightBlur(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int blur)
{
	gfx_copy_amap_height_blur(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h, blur);
}

static void DrawGraph_DrawLine(int dst, int x0, int y0, int x1, int y1, int r, int g, int b)
{
	gfx_draw_line(DTEX(dst), x0, y0, x1, y1, r, g, b);
}

static void DrawGraph_DrawLineToAMap(int dst, int x0, int y0, int x1, int y1, int alpha)
{
	gfx_draw_line_to_amap(DTEX(dst), x0, y0, x1, y1, alpha);
}

static bool DrawGraph_GetAlphaColor(int surface, int x, int y, int *a)
{
	struct sact_sprite *sp = sact_get_sprite(surface);
	// NOTE: System40.exe errors here
	if (!sp)
		VM_ERROR("Invalid sprite index: %d", surface);
	if (x < 0 || x >= sp->rect.w || y < 0 || y >= sp->rect.h)
		return false;
	*a = sact_SP_GetAMapValue(surface, x, y);
	return true;
}

//void DrawGraph_DrawPolygon(int dst, int tex, float x0, float y0, float z0, float u0, float v0, float x1, float y1, float z1, float u1, float v1, float x2, float y2, float z2, float u2, float v2);
//void DrawGraph_DrawColorPolygon(int dst, int tex, float x0, float y0, float z0, int r0, int g0, int b0, int a0, float x1, float y1, float z1, int r1, int g1, int b1, int a1, float x2, float y2, float z2, int r2, int g2, int b2, int a2);
//void DrawGraph_DrawDeformedSprite(int dst, int tex, float x0, float y0, float z0, float u0, float v0, float x1, float y1, float z1, float u1, float v1, float x2, float y2, float z2, float u2, float v2, float x3, float y3, float z3, float u3, float v3);
//void DrawGraph_BlendDeformedSprite(int dst, int tex, float x0, float y0, float z0, float u0, float v0, float x1, float y1, float z1, float u1, float v1, float x2, float y2, float z2, float u2, float v2, float x3, float y3, float z3, float u3, float v3, int rate);
//void DrawGraph_BlendAMapDeformedSprite(int dst, int tex, float x0, float y0, float z0, float u0, float v0, float x1, float y1, float z1, float u1, float v1, float x2, float y2, float z2, float u2, float v2, float x3, float y3, float z3, float u3, float v3);
//void DrawGraph_BlendAMapAlphaDeformedSprite(int dst, int tex, float x0, float y0, float z0, float u0, float v0, float x1, float y1, float z1, float u1, float v1, float x2, float y2, float z2, float u2, float v2, float x3, float y3, float z3, float u3, float v3, int rate);

static void DrawGraph_DrawDeformedSpriteBilinear(int dst, int tex,
	float x0, float y0, float z0, float u0, float v0,
	float x1, float y1, float z1, float u1, float v1,
	float x2, float y2, float z2, float u2, float v2,
	float x3, float y3, float z3, float u3, float v3)
{
	struct gfx_vertex vertices[4] = {
		{x0, y0, z0, 1.f, u0, v0},
		{x1, y1, z1, 1.f, u1, v1},
		{x2, y2, z2, 1.f, u2, v2},
		{x3, y3, z3, 1.f, u3, v3}
	};
	gfx_draw_quadrilateral(DTEX(dst), STEX(tex), vertices);
}

//void DrawGraph_BlendAMapDeformedSpriteBilinear(int dst, int tex, float x0, float y0, float z0, float u0, float v0, float x1, float y1, float z1, float u1, float v1, float x2, float y2, float z2, float u2, float v2, float x3, float y3, float z3, float u3, float v3);

static void DrawGraph_CopyWithAlphaMap(int dst, int dx, int dy, int src, int sx, int sy, int w, int h)
{
	gfx_copy_with_alpha_map(DTEX(dst), dx, dy, STEX(src), sx, sy, w, h);
}

static void DrawGraph_FillWithAlpha(int dst, int x, int y, int w, int h, int r, int g, int b, int a)
{
	gfx_fill_with_alpha(DTEX(dst), x, y, w, h, r, g, b, a);
}

static void DrawGraph_CopyStretchWithAlphaMap(int dst, int dx, int dy, int dw, int dh, int src, int sx, int sy, int sw, int sh)
{
	gfx_copy_stretch_with_alpha_map(DTEX(dst), dx, dy, dw, dh, STEX(src), sx, sy, sw, sh);
}

static void DrawGraph_CopyGrayscale(int dst, int dx, int dy, int src, int sx, int sy, int width, int height)
{
	gfx_copy_grayscale(DTEX(dst), dx, dy, STEX(src), sx, sy, width, height);
}

HLL_LIBRARY(DrawGraph,
	    HLL_EXPORT(Copy, DrawGraph_Copy),
	    HLL_EXPORT(CopyBright, DrawGraph_CopyBright),
	    HLL_EXPORT(CopyAMap, DrawGraph_CopyAMap),
	    HLL_EXPORT(CopySprite, DrawGraph_CopySprite),
	    HLL_EXPORT(CopyColorReverse, DrawGraph_CopyColorReverse),
	    HLL_EXPORT(CopyUseAMapUnder, DrawGraph_CopyUseAMapUnder),
	    HLL_EXPORT(CopyUseAMapBorder, DrawGraph_CopyUseAMapBorder),
	    HLL_EXPORT(CopyAMapMax, DrawGraph_CopyAMapMax),
	    HLL_EXPORT(CopyAMapMin, DrawGraph_CopyAMapMin),
	    HLL_EXPORT(Blend, DrawGraph_Blend),
	    HLL_EXPORT(BlendSrcBright, DrawGraph_BlendSrcBright),
	    HLL_EXPORT(BlendAddSatur, DrawGraph_BlendAddSatur),
	    HLL_EXPORT(BlendAMap, DrawGraph_BlendAMap),
	    HLL_EXPORT(BlendAMapSrcOnly, DrawGraph_BlendAMapSrcOnly),
	    HLL_EXPORT(BlendAMapColor, DrawGraph_BlendAMapColor),
	    HLL_EXPORT(BlendAMapColorAlpha, DrawGraph_BlendAMapColorAlpha),
	    HLL_EXPORT(BlendAMapAlpha, DrawGraph_BlendAMapAlpha),
	    HLL_EXPORT(BlendAMapBright, DrawGraph_BlendAMapBright),
	    HLL_EXPORT(BlendAMapAlphaSrcBright, DrawGraph_BlendAMapAlphaSrcBright),
	    HLL_EXPORT(BlendUseAMapColor, DrawGraph_BlendUseAMapColor),
	    HLL_EXPORT(BlendScreen, DrawGraph_BlendScreen),
	    HLL_EXPORT(BlendMultiply, DrawGraph_BlendMultiply),
	    HLL_EXPORT(BlendScreenAlpha, DrawGraph_BlendScreenAlpha),
	    HLL_EXPORT(Fill, DrawGraph_Fill),
	    HLL_EXPORT(FillAlphaColor, DrawGraph_FillAlphaColor),
	    HLL_EXPORT(FillAMap, DrawGraph_FillAMap),
	    HLL_EXPORT(FillAMapOverBorder, DrawGraph_FillAMapOverBorder),
	    HLL_EXPORT(FillAMapUnderBorder, DrawGraph_FillAMapUnderBorder),
	    HLL_EXPORT(FillAMapGradationUD, DrawGraph_FillAMapGradationUD),
	    HLL_EXPORT(FillScreen, DrawGraph_FillScreen),
	    HLL_EXPORT(FillMultiply, DrawGraph_FillMultiply),
	    HLL_EXPORT(SaturDP_DPxSA, DrawGraph_SaturDP_DPxSA),
	    HLL_EXPORT(ScreenDA_DAxSA, DrawGraph_ScreenDA_DAxSA),
	    HLL_EXPORT(AddDA_DAxSA, DrawGraph_AddDA_DAxSA),
	    HLL_EXPORT(SpriteCopyAMap, DrawGraph_SpriteCopyAMap),
	    HLL_EXPORT(BlendDA_DAxSA, DrawGraph_BlendDA_DAxSA),
	    HLL_EXPORT(SubDA_DAxSA, DrawGraph_SubDA_DAxSA),
	    HLL_EXPORT(BrightDestOnly, DrawGraph_BrightDestOnly),
	    //HLL_EXPORT(CopyTextureWrap, DrawGraph_CopyTextureWrap),
	    //HLL_EXPORT(CopyTextureWrapAlpha, DrawGraph_CopyTextureWrapAlpha),
	    HLL_EXPORT(CopyStretch, DrawGraph_CopyStretch),
	    HLL_EXPORT(CopyStretchBlend, DrawGraph_CopyStretchBlend),
	    HLL_EXPORT(CopyStretchBlendAMap, DrawGraph_CopyStretchBlendAMap),
	    HLL_EXPORT(CopyStretchAMap, DrawGraph_CopyStretchAMap),
	    // NOTE: xsystem4 uses bilinear interpolation by default
	    HLL_EXPORT(CopyStretchInterp, DrawGraph_CopyStretch),
	    HLL_EXPORT(CopyStretchAMapInterp, DrawGraph_CopyStretchAMap),
	    HLL_EXPORT(CopyReduce, DrawGraph_CopyStretch),
	    HLL_EXPORT(CopyReduceAMap, DrawGraph_CopyStretchAMap),
	    HLL_EXPORT(DrawTextToPMap, DrawGraph_DrawTextToPMap),
	    HLL_EXPORT(DrawTextToAMap, DrawGraph_DrawTextToAMap),
	    HLL_EXPORT(SetFontSize, DrawGraph_SetFontSize),
	    HLL_EXPORT(SetFontName, DrawGraph_SetFontName),
	    HLL_EXPORT(SetFontWeight, DrawGraph_SetFontWeight),
	    HLL_EXPORT(SetFontUnderline, DrawGraph_SetFontUnderline),
	    HLL_EXPORT(SetFontStrikeOut, DrawGraph_SetFontStrikeOut),
	    HLL_EXPORT(SetFontSpace, DrawGraph_SetFontSpace),
	    HLL_EXPORT(SetFontColor, DrawGraph_SetFontColor),
	    HLL_EXPORT(GetFontSize, gfx_get_font_size),
	    HLL_EXPORT(GetFontName, DrawGraph_GetFontName),
	    HLL_EXPORT(GetFontWeight, gfx_get_font_weight),
	    HLL_EXPORT(GetFontUnderline, gfx_get_font_underline),
	    HLL_EXPORT(GetFontStrikeOut, gfx_get_font_strikeout),
	    HLL_EXPORT(GetFontSpace, gfx_get_font_space),
	    HLL_EXPORT(GetFontColor, DrawGraph_GetFontColor),
	    HLL_EXPORT(CopyRotZoom, DrawGraph_CopyRotZoom),
	    HLL_EXPORT(CopyRotZoomAMap, DrawGraph_CopyRotZoomAMap),
	    HLL_EXPORT(CopyRotZoomUseAMap, DrawGraph_CopyRotZoomUseAMap),
	    HLL_EXPORT(CopyRotZoom2Bilinear, DrawGraph_CopyRotZoom2Bilinear),
	    HLL_EXPORT(CopyRotateY, DrawGraph_CopyRotateY),
	    HLL_EXPORT(CopyRotateYUseAMap, DrawGraph_CopyRotateYUseAMap),
	    //HLL_EXPORT(CopyRotateYFixL, DrawGraph_CopyRotateYFixL),
	    //HLL_EXPORT(CopyRotateYFixR, DrawGraph_CopyRotateYFixR),
	    //HLL_EXPORT(CopyRotateYFixLUseAMap, DrawGraph_CopyRotateYFixLUseAMap),
	    //HLL_EXPORT(CopyRotateYFixRUseAMap, DrawGraph_CopyRotateYFixRUseAMap),
	    HLL_EXPORT(CopyRotateX, DrawGraph_CopyRotateX),
	    HLL_EXPORT(CopyRotateXUseAMap, DrawGraph_CopyRotateXUseAMap),
	    //HLL_EXPORT(CopyRotateXFixU, DrawGraph_CopyRotateXFixU),
	    //HLL_EXPORT(CopyRotateXFixD, DrawGraph_CopyRotateXFixD),
	    //HLL_EXPORT(CopyRotateXFixUUseAMap, DrawGraph_CopyRotateXFixUUseAMap),
	    //HLL_EXPORT(CopyRotateXFixDUseAMap, DrawGraph_CopyRotateXFixDUseAMap),
	    HLL_EXPORT(CopyReverseLR, DrawGraph_CopyReverseLR),
	    HLL_EXPORT(CopyReverseUD, DrawGraph_CopyReverseUD),
	    HLL_EXPORT(CopyReverseAMapLR, DrawGraph_CopyReverseAMapLR),
	    //HLL_EXPORT(CopyReverseAMapUD, DrawGraph_CopyReverseAMapUD),
	    HLL_EXPORT(CopyReverseLRWithAMap, DrawGraph_CopyReverseLRWithAMap),
	    HLL_TODO_EXPORT(CopyReverseUDWithAMap, DrawGraph_CopyReverseUDWithAMap),
	    HLL_EXPORT(CopyWidthBlur, DrawGraph_CopyWidthBlur),
	    HLL_EXPORT(CopyHeightBlur, DrawGraph_CopyHeightBlur),
	    HLL_EXPORT(CopyAMapWidthBlur, DrawGraph_CopyAMapWidthBlur),
	    HLL_EXPORT(CopyAMapHeightBlur, DrawGraph_CopyAMapHeightBlur),
	    HLL_EXPORT(DrawLine, DrawGraph_DrawLine),
	    HLL_EXPORT(DrawLineToAMap, DrawGraph_DrawLineToAMap),
	    HLL_EXPORT(GetPixelColor, sact_SP_GetPixelValue),
	    HLL_EXPORT(GetAlphaColor, DrawGraph_GetAlphaColor),
	    //HLL_EXPORT(DrawPolygon, DrawGraph_DrawPolygon),
	    //HLL_EXPORT(DrawColorPolygon, DrawGraph_DrawColorPolygon)
	    HLL_TODO_EXPORT(DrawDeformedSprite, DrawGraph_DrawDeformedSprite),
	    HLL_TODO_EXPORT(BlendDeformedSprite, DrawGraph_BlendDeformedSprite),
	    HLL_TODO_EXPORT(BlendAMapDeformedSprite, DrawGraph_BlendAMapDeformedSprite),
	    HLL_TODO_EXPORT(BlendAMapAlphaDeformedSprite, DrawGraph_BlendAMapAlphaDeformedSprite),
	    HLL_EXPORT(DrawDeformedSpriteBilinear, DrawGraph_DrawDeformedSpriteBilinear),
	    HLL_TODO_EXPORT(BlendAMapDeformedSpriteBilinear, DrawGraph_BlendAMapDeformedSpriteBilinear),
	    HLL_EXPORT(CopyWithAlphaMap, DrawGraph_CopyWithAlphaMap),
	    HLL_EXPORT(FillWithAlpha, DrawGraph_FillWithAlpha),
	    HLL_EXPORT(CopyStretchWithAlphaMap, DrawGraph_CopyStretchWithAlphaMap),
	    // NOTE: xsystem4 uses bilinear interpolation by default
	    HLL_EXPORT(CopyStretchBilinearWithAlphaMap, DrawGraph_CopyStretchWithAlphaMap),
	    HLL_EXPORT(CopyGrayscale, DrawGraph_CopyGrayscale)
	);
