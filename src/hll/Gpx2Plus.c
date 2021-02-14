/* Copyright (C) 2021 kichikuou <KichikuouChrome@gmail.com>
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

#include <assert.h>

#include "system4.h"
#include "system4/ald.h"
#include "system4/cg.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "hll.h"
#include "audio.h"
#include "gfx/gfx.h"
#include "input.h"
#include "vm.h"
#include "xsystem4.h"

struct gpx_surface {
	struct texture texture;
	int w;
	int h;
	bool has_pixel;
	bool has_alpha;
	int no;
};

static struct gpx_surface **surfaces = NULL;
static int nr_surfaces = 0;

static struct {
	struct string **strs;
	int len;
	int cap;
} messages;

static struct gpx_surface *create_surface(int width, int height)
{
	int sf_no;
	for (sf_no = 0; surfaces[sf_no]; sf_no++) {
		if (sf_no + 1 == nr_surfaces) {
			int new_nr_surfaces = nr_surfaces + 256;
			surfaces = xrealloc(surfaces, sizeof(struct gpx_surface *) * new_nr_surfaces);
			memset(surfaces + nr_surfaces, 0, sizeof(struct gpx_surface *) * (new_nr_surfaces - nr_surfaces));
			nr_surfaces = new_nr_surfaces;
		}
	}
	struct gpx_surface *sf = xcalloc(1, sizeof(struct gpx_surface));
	surfaces[sf_no] = sf;
	sf->w = width;
	sf->h = height;
	sf->has_pixel = true;
	sf->has_alpha = true;
	sf->no = sf_no;
	return sf;
}

static void free_surface(struct gpx_surface *sf)
{
	if (surfaces[sf->no] == NULL)
		VM_ERROR("Double free of gpx_surface");
	surfaces[sf->no] = NULL;
	gfx_delete_texture(&sf->texture);
	free(sf);
}

static struct texture *get_texture(int sf_no)
{
	if (sf_no < 0 || sf_no >= nr_surfaces || !surfaces[sf_no])
		return NULL;
	struct gpx_surface *sf = surfaces[sf_no];
	if (!sf->texture.handle)
		gfx_init_texture_with_color(&sf->texture, sf->w, sf->h, (SDL_Color){0, 0, 0, 255});
	return &sf->texture;
}

static void Gpx2Plus_Init(possibly_unused void *imainsystem, possibly_unused struct string *link_file_name)
{
	// already initialized
	if (surfaces)
		return;

	gfx_init();
	gfx_font_init();
	audio_init();

	nr_surfaces = 256;
	surfaces = xmalloc(sizeof(struct gpx_surface*) * 256);
	memset(surfaces, 0, sizeof(struct gpx_surface*) * 256);

	// create main surface
	Texture *t = gfx_main_surface();
	struct gpx_surface *sf = create_surface(t->w, t->h);
	assert(sf->no == 0);

	messages.cap = 16;
	messages.len = 0;
	messages.strs = xmalloc(messages.cap * sizeof(struct string *));

	gfx_set_font_weight(FW_NORMAL);
}

static int Gpx2Plus_Create(int width, int height, possibly_unused int bpp)
{
	struct gpx_surface *sf = create_surface(width, height);
	return sf->no;
}

static int Gpx2Plus_CreatePixelOnly(int width, int height, possibly_unused int bpp)
{
	struct gpx_surface *sf = create_surface(width, height);
	sf->has_alpha = false;
	return sf->no;
}

// int CreateAMapOnly(int nWidth, int nHeight);
// int IsSurface(int nSurface);

static int Gpx2Plus_IsPixel(int surface)
{
	if (surface < 0 || surface >= nr_surfaces || !surfaces[surface])
		return 0;
	return surfaces[surface]->has_pixel ? 1 : 0;
}

static int Gpx2Plus_IsAlpha(int surface)
{
	if (surface < 0 || surface >= nr_surfaces || !surfaces[surface])
		return 0;
	return surfaces[surface]->has_alpha ? 1 : 0;
}

static int Gpx2Plus_GetWidth(int surface)
{
	if (surface < 0 || surface >= nr_surfaces || !surfaces[surface])
		return 0;
	return surfaces[surface]->w;
}

static int Gpx2Plus_GetHeight(int surface)
{
	if (surface < 0 || surface >= nr_surfaces || !surfaces[surface])
		return 0;
	return surfaces[surface]->h;
}

// int GetCreatedSurface(void);
// int GetSurfacePointer(int nSurface);

static int Gpx2Plus_GetPixsel(int surface, int x, int y)
{
	struct texture *t = get_texture(surface);
	if (!t)
		return 0;
	SDL_Color c = gfx_get_pixel(t, x, y);
	return c.r << 16 | c.g << 8 | c.b;
}

static int Gpx2Plus_LoadCG(int cg_num)
{
	if (!cg_num)
		return -1;
	struct cg *cg = cg_load(ald[ALDFILE_CG], cg_num - 1);
	if (!cg)
		return -1;
	struct gpx_surface *sf = create_surface(1, 1);
	gfx_init_texture_with_cg(&sf->texture, cg);
	sf->w = cg->metrics.w;
	sf->h = cg->metrics.h;
	sf->has_pixel = cg->metrics.has_pixel;
	sf->has_alpha = cg->metrics.has_alpha;
	cg_free(cg);
	return sf->no;
}

// int GetCGPosX(int nCGNum);
// int GetCGPosY(int nCGNum);

static void Gpx2Plus_Free(int surface)
{
	// surface 0 (main surface) cannot be freed
	if (surface <= 0 || surface >= nr_surfaces || !surfaces[surface])
		return;
	free_surface(surfaces[surface]);
}

static void Gpx2Plus_FreeAll(void)
{
	for (int i = 1; i < nr_surfaces; i++) {
		if (surfaces[i])
			free_surface(surfaces[i]);
	}
}

static void Gpx2Plus_Copy(int destSurface, int dx, int dy, int srcSurface, int sx, int sy, int width, int height)
{
	struct texture *dst = get_texture(destSurface);
	struct texture *src = get_texture(srcSurface);
	if (!dst || !src)
		return;
	gfx_copy(dst, dx, dy, src, sx, sy, width, height);
}

static void Gpx2Plus_CopyBright(int destSurface, int dx, int dy, int srcSurface, int sx, int sy, int width, int height, int rate)
{
	struct texture *dst = get_texture(destSurface);
	struct texture *src = get_texture(srcSurface);
	if (!dst || !src)
		return;
	gfx_copy_bright(dst, dx, dy, src, sx, sy, width, height, rate);
}

static void Gpx2Plus_CopyAMap(int destSurface, int dx, int dy, int srcSurface, int sx, int sy, int width, int height)
{
	struct texture *dst = get_texture(destSurface);
	struct texture *src = get_texture(srcSurface);
	if (!dst || !src)
		return;
	gfx_copy_amap(dst, dx, dy, src, sx, sy, width, height);
}

static void Gpx2Plus_CopySprite(int destSurface, int dx, int dy, int srcSurface, int sx, int sy, int width, int height, int r, int g, int b)
{
	struct texture *dst = get_texture(destSurface);
	struct texture *src = get_texture(srcSurface);
	if (!dst || !src)
		return;
	SDL_Color color = { .r = r, .g = g, .b = b };
	gfx_copy_sprite(dst, dx, dy, src, sx, sy, width, height, color);
}

static void Gpx2Plus_Blend(int destSurface, int dx, int dy, int srcSurface, int sx, int sy, int width, int height, int alpha)
{
	struct texture *dst = get_texture(destSurface);
	struct texture *src = get_texture(srcSurface);
	if (!dst || !src)
		return;
	gfx_blend(dst, dx, dy, src, sx, sy, width, height, alpha);
}

// void BlendSrcBright(int nDestSurface, int nDx, int nDy, int nSrcSurface, int nSx, int nSy, int nWidth, int nHeight, int nAlpha, int nRate);
// void BlendAddSatur(int nDestSurface, int nDx, int nDy, int nSrcSurface, int nSx, int nSy, int nWidth, int nHeight);

static void Gpx2Plus_BlendAMap(int destSurface, int dx, int dy, int srcSurface, int sx, int sy, int width, int height)
{
	struct texture *dst = get_texture(destSurface);
	struct texture *src = get_texture(srcSurface);
	if (!dst || !src)
		return;
	gfx_blend_amap(dst, dx, dy, src, sx, sy, width, height);
}

// void BlendAMapSrcOnly(int nDestSurface, int nDx, int nDy, int nSrcSurface, int nSx, int nSy, int nWidth, int nHeight);
// void BlendAMapColor(int nDestSurface, int nDx, int nDy, int nSrcSurface, int nSx, int nSy, int nWidth, int nHeight, int nR, int nG, int nB);
HLL_WARN_UNIMPLEMENTED( , void, Gpx2Plus, BlendAMapColorAlpha, int destSurface, int dx, int dy, int srcSurface, int sx, int sy, int width, int height, int r, int g, int b, int alpha);

static void Gpx2Plus_BlendAMapAlpha(int destSurface, int dx, int dy, int srcSurface, int sx, int sy, int width, int height, int alpha)
{
	struct texture *dst = get_texture(destSurface);
	struct texture *src = get_texture(srcSurface);
	if (!dst || !src)
		return;
	gfx_blend_amap_alpha(dst, dx, dy, src, sx, sy, width, height, alpha);
}

// void BlendAMapBright(int nDestSurface, int nDx, int nDy, int nSrcSurface, int nSx, int nSy, int nWidth, int nHeight, int nRate);
// void BlendAMapAlphaSrcBright(int nDestSurface, int nDx, int nDy, int nSrcSurface, int nSx, int nSy, int nWidth, int nHeight, int nAlpha, int nRate);
// void BlendUseAMapColor(int nDestSurface, int nDx, int nDy, int nAlphaSurface, int nAx, int nAy, int nWidth, int nHeight, int nR, int nG, int nB, int nAlpha);
// void BlendScreen(int nDestSurface, int nDx, int nDy, int nSrcSurface, int nSx, int nSy, int nWidth, int nHeight);
// void BlendMultiply(int nDestSurface, int nDx, int nDy, int nSrcSurface, int nSx, int nSy, int nWidth, int nHeight);
// void BlendScreenAlpha(int nDestSurface, int nDx, int nDy, int nSrcSurface, int nSx, int nSy, int nWidth, int nHeight, int nAlpha);

static void Gpx2Plus_Fill(int surface, int x, int y, int width, int height, int r, int g, int b)
{
	struct texture *dst = get_texture(surface);
	if (!dst)
		return;
	gfx_fill(dst, x, y, width, height, r, g, b);
}

static void Gpx2Plus_FillAlphaColor(int surface, int x, int y, int width, int height, int r, int g, int b, int rate)
{
	struct texture *dst = get_texture(surface);
	if (!dst)
		return;
	gfx_fill_alpha_color(dst, x, y, width, height, r, g, b, rate);
}

static void Gpx2Plus_FillAMap(int surface, int x, int y, int width, int height, int alpha)
{
	struct texture *dst = get_texture(surface);
	if (!dst)
		return;
	gfx_fill_amap(dst, x, y, width, height, alpha);
}

// void FillAMapOverBorder(int nSurface, int nX, int nY, int nWidth, int nHeight, int nAlpha, int nBorder);
// void FillAMapUnderBorder(int nSurface, int nX, int nY, int nWidth, int nHeight, int nAlpha, int nBorder);
// void SaturDP_DPxSA(int nDestSurface, int nDx, int nDy, int nSrcSurface, int nSx, int nSy, int nWidth, int nHeight);
// void ScreenDA_DAxSA(int nDestSurface, int nDx, int nDy, int nSrcSurface, int nSx, int nSy, int nWidth, int nHeight);
// void AddDA_DAxSA(int nDestSurface, int nDx, int nDy, int nSrcSurface, int nSx, int nSy, int nWidth, int nHeight);
// void SpriteCopyAMap(int nDestSurface, int nDx, int nDy, int nSrcSurface, int nSx, int nSy, int nWidth, int nHeight, int nColorKey);
// void BrightDestOnly(int nSurface, int nX, int nY, int nWidth, int nHeight, int nRate);
// void CopyTextureWrap(int nDestSurface, int nDx, int nDy, int nDWidth, int nDHeight, int nSrcSurface, int nSx, int nSy, int nSWidth, int nSHeight, int nU, int nV);
// void CopyTextureWrapAlpha(int nDestSurface, int nDx, int nDy, int nDWidth, int nDHeight, int nSrcSurface, int nSx, int nSy, int nSWidth, int nSHeight, int nU, int nV, int nAlpha);

static void Gpx2Plus_CopyStretch(int surface, int dx, int dy, int dWidth, int dHeight, int srcSurface, int sx, int sy, int sWidth, int sHeight)
{
	struct texture *dst = get_texture(surface);
	struct texture *src = get_texture(srcSurface);
	if (!dst || !src)
		return;
	gfx_copy_stretch(dst, dx, dy, dWidth, dHeight, src, sx, sy, sWidth, sHeight);
}

// void CopyStretchBlend(int nSurface, int nDx, int nDy, int nDWidth, int nDHeight, int nSrcSurface, int nSx, int nSy, int nSWidth, int nSHeight, int nAlpha);

static void Gpx2Plus_CopyStretchBlendAMap(int surface, int dx, int dy, int dWidth, int dHeight, int srcSurface, int sx, int sy, int sWidth, int sHeight)
{
	struct texture *dst = get_texture(surface);
	struct texture *src = get_texture(srcSurface);
	if (!dst || !src)
		return;
	gfx_copy_stretch_amap(dst, dx, dy, dWidth, dHeight, src, sx, sy, sWidth, sHeight);
}

// void StretchBlendScreen2x2(int nDestSurface, int nDx, int nDy, int nSrcSurface, int nSx, int nSy, int nWidth, int nHeight);
// void StretchBlendScreen2x2WDS(int nWriteSurface, int nWx, int nWy, int nDestSurface, int nDx, int nDy, int nSrcSurface, int nSx, int nSy, int nWidth, int nHeight);

static void Gpx2Plus_CopyStretchReduce(int destSurface, int dx, int dy, int dWidth, int dHeight, int srcSurface, int sx, int sy, int sWidth, int sHeight)
{
	struct texture *dst = get_texture(destSurface);
	struct texture *src = get_texture(srcSurface);
	if (!dst || !src)
		return;
	gfx_copy_stretch(dst, dx, dy, dWidth, dHeight, src, sx, sy, sWidth, sHeight);
}

static void Gpx2Plus_CopyStretchReduceAMap(int destSurface, int dx, int dy, int dWidth, int dHeight, int srcSurface, int sx, int sy, int sWidth, int sHeight)
{
	struct texture *dst = get_texture(destSurface);
	struct texture *src = get_texture(srcSurface);
	if (!dst || !src)
		return;
	gfx_copy_stretch_amap(dst, dx, dy, dWidth, dHeight, src, sx, sy, sWidth, sHeight);
}

static void Gpx2Plus_CopyReverseLR(int destSurface, int dx, int dy, int srcSurface, int sx, int sy, int width, int height)
{
	struct texture *dst = get_texture(destSurface);
	struct texture *src = get_texture(srcSurface);
	if (!dst || !src)
		return;
	gfx_copy_reverse_LR(dst, dx, dy, src, sx, sy, width, height);
}

// void CopyReverseUD(int nDestSurface, int nDx, int nDy, int nSrcSurface, int nSx, int nSy, int nWidth, int nHeight);

static void Gpx2Plus_CopyReverseAMapLR(int destSurface, int dx, int dy, int srcSurface, int sx, int sy, int width, int height)
{
	struct texture *dst = get_texture(destSurface);
	struct texture *src = get_texture(srcSurface);
	if (!dst || !src)
		return;
	gfx_copy_reverse_amap_LR(dst, dx, dy, src, sx, sy, width, height);
}

// void CopyReverseAMapUD(int nDestSurface, int nDx, int nDy, int nSrcSurface, int nSx, int nSy, int nWidth, int nHeight);
// void BlendScreenWDS(int nWriteSurface, int nWx, int nWy, int nDestSurface, int nDx, int nDy, int nSrcSurface, int nSx, int nSy, int nWidth, int nHeight);

static void Gpx2Plus_Update(int x, int y, int width, int height);
static int Gpx2Plus_EffectCopy(int effect, int wx, int wy,
							   int destSurface, int dx, int dy,
							   int srcSurface, int sx, int sy,
							   int width, int height, int totalTime)
{
	// TODO: Apply effect
	WARNING("gpx_EffectCopy %d (%d, %d) %d (%d, %d) <- %d (%d, %d) %dx%d %dms",
			effect, wx, wy, destSurface, dx, dy, srcSurface, sx, sy, width, height, totalTime);
	struct texture *dst = get_texture(0);
	struct texture *src = get_texture(srcSurface);
	if (!dst || !src)
		return 0;
	gfx_copy(dst, wx, wy, src, sx, sy, width, height);
	Gpx2Plus_Update(0, 0, 640, 480);
	return 1;
}

// void SetClickCancelFlag(int nFlag);

static void Gpx2Plus_Update(int x, int y, int width, int height)
{
	handle_events();

	gfx_clear();
	Rectangle r = {0, 0, surfaces[0]->w, surfaces[0]->h};
	gfx_render_texture(&surfaces[0]->texture, &r);

	gfx_swap();
}

static void Gpx2Plus_DrawText(int surface, int x, int y, struct string *text)
{
	struct texture *dst = get_texture(surface);
	if (!dst)
		return;
	gfx_draw_text_to_pmap(dst, x, y, text->text);
}

static void Gpx2Plus_DrawTextToAMap(int surface, int x, int y, struct string *text)
{
	struct texture *dst = get_texture(surface);
	if (!dst)
		return;
	gfx_draw_text_to_amap(dst, x, y, text->text);
}

static void Gpx2Plus_SetFontName(struct string *name)
{
	char *u = sjis2utf(name->text, name->size);
	if (!strcmp(u, "ＭＳ ゴシック"))
		gfx_set_font_face(FONT_GOTHIC);
	else if (!strcmp(u, "ＭＳ 明朝"))
		gfx_set_font_face(FONT_MINCHO);
	else
		WARNING("Unknown font name '%s'", u);
	free(u);
}

static void Gpx2Plus_SetFontColor(int r, int g, int b)
{
	gfx_set_font_color((SDL_Color) { .r = r, .g = g, .b = b, .a = 255 });
}

// string GetFontName(void);

static void Gpx2Plus_GetFontColor(int *r, int *g, int *b)
{
	SDL_Color col = gfx_get_font_color();
	*r = col.r;
	*g = col.g;
	*b = col.b;
}

static void Gpx2Plus_MsgAddText(struct string *text)
{
	if (messages.len >= messages.cap) {
		messages.cap *= 2;
		messages.strs = xrealloc(messages.strs, messages.cap * sizeof(struct string *));
	}
	messages.strs[messages.len++] = string_ref(text);
}

static void Gpx2Plus_MsgClearText(void)
{
	for (int i = 0; i < messages.len; i++)
		free_string(messages.strs[i]);
	messages.len = 0;
}

static void Gpx2Plus_MsgDraw(int surface, int x, int y)
{
	struct texture *dst = get_texture(surface);
	if (!dst)
		return;
	struct text_metrics tm = { .weight = FW_NORMAL };
	Point pos = POINT(x, y);
	for (int i = 0; i < messages.len; i++) {
		char *s = messages.strs[i]->text;
		while (*s) {
			int len = 0, a1, a2, a3;
			if (*s != '<') {
				char *lb = strchr(s, '<');
				len = lb ? lb - s : strlen(s);
				char *ss = strndup(s, len);
				pos.x += gfx_render_text(dst, pos, ss, &tm, 0 /* char_space */);
				free(ss);
			} else if (sscanf(s, "<F%d>%n", &a1, &len) == 1) {
				tm.face = a1;
			} else if (sscanf(s, "<S%d>%n", &a1, &len) == 1) {
				tm.size = a1;
			} else if (sscanf(s, "<C%d,%d,%d>%n", &a1, &a2, &a3, &len) == 3) {
				tm.color = (SDL_Color) { .r = a1, .g = a2, .b = a3, .a = 255 };
			} else if (sscanf(s, "<KC%d,%d,%d>%n", &a1, &a2, &a3, &len) == 3) {
				tm.outline_color = (SDL_Color) { .r = a1, .g = a2, .b = a3, .a = 255 };
			} else if (!strncmp(s, "<K>", 3)) {
				len = 3;
				tm.outline_right = tm.outline_down = 2;
			} else if (!strncmp(s, "</K>", 3)) {
				len = 4;
				tm.outline_right = tm.outline_down = 0;
			} else if (!strncmp(s, "<R>", 3)) {
				len = 3;
				pos.x = x;
				pos.y += tm.size;
			} else {
				char *rb = strchr(s, '>');
				if (rb) {
					WARNING("Unknown command %.*s", ++rb - s, s);
					len = rb - s;
				} else {
					WARNING("Unfinished command in message '%s'", messages.strs[i]->text);
					break;
				}
			}
			s += len;
		}
	}
}

HLL_LIBRARY(Gpx2Plus,
			HLL_EXPORT(Init, Gpx2Plus_Init),
			HLL_EXPORT(Create, Gpx2Plus_Create),
			HLL_EXPORT(CreatePixelOnly, Gpx2Plus_CreatePixelOnly),
			// HLL_EXPORT(CreateAMapOnly, Gpx2Plus_CreateAMapOnly),
			// HLL_EXPORT(IsSurface, Gpx2Plus_IsSurface),
			HLL_EXPORT(IsPixel, Gpx2Plus_IsPixel),
			HLL_EXPORT(IsAlpha, Gpx2Plus_IsAlpha),
			HLL_EXPORT(GetWidth, Gpx2Plus_GetWidth),
			HLL_EXPORT(GetHeight, Gpx2Plus_GetHeight),
			// HLL_EXPORT(GetCreatedSurface, Gpx2Plus_GetCreatedSurface),
			// HLL_EXPORT(GetSurfacePointer, Gpx2Plus_GetSurfacePointer),
			HLL_EXPORT(GetPixsel, Gpx2Plus_GetPixsel),
			HLL_EXPORT(LoadCG, Gpx2Plus_LoadCG),
			// HLL_EXPORT(GetCGPosX, Gpx2Plus_GetCGPosX),
			// HLL_EXPORT(GetCGPosY, Gpx2Plus_GetCGPosY),
			HLL_EXPORT(Free, Gpx2Plus_Free),
			HLL_EXPORT(FreeAll, Gpx2Plus_FreeAll),
			HLL_EXPORT(Copy, Gpx2Plus_Copy),
			HLL_EXPORT(CopyBright, Gpx2Plus_CopyBright),
			HLL_EXPORT(CopyAMap, Gpx2Plus_CopyAMap),
			HLL_EXPORT(CopySprite, Gpx2Plus_CopySprite),
			HLL_EXPORT(Blend, Gpx2Plus_Blend),
			// HLL_EXPORT(BlendSrcBright, Gpx2Plus_BlendSrcBright),
			// HLL_EXPORT(BlendAddSatur, Gpx2Plus_BlendAddSatur),
			HLL_EXPORT(BlendAMap, Gpx2Plus_BlendAMap),
			// HLL_EXPORT(BlendAMapSrcOnly, Gpx2Plus_BlendAMapSrcOnly),
			// HLL_EXPORT(BlendAMapColor, Gpx2Plus_BlendAMapColor),
			HLL_EXPORT(BlendAMapColorAlpha, Gpx2Plus_BlendAMapColorAlpha),
			HLL_EXPORT(BlendAMapAlpha, Gpx2Plus_BlendAMapAlpha),
			// HLL_EXPORT(BlendAMapBright, Gpx2Plus_BlendAMapBright),
			// HLL_EXPORT(BlendAMapAlphaSrcBright, Gpx2Plus_BlendAMapAlphaSrcBright),
			// HLL_EXPORT(BlendUseAMapColor, Gpx2Plus_BlendUseAMapColor),
			// HLL_EXPORT(BlendScreen, Gpx2Plus_BlendScreen),
			// HLL_EXPORT(BlendMultiply, Gpx2Plus_BlendMultiply),
			// HLL_EXPORT(BlendScreenAlpha, Gpx2Plus_BlendScreenAlpha),
			HLL_EXPORT(Fill, Gpx2Plus_Fill),
			HLL_EXPORT(FillAlphaColor, Gpx2Plus_FillAlphaColor),
			HLL_EXPORT(FillAMap, Gpx2Plus_FillAMap),
			// HLL_EXPORT(FillAMapOverBorder, Gpx2Plus_FillAMapOverBorder),
			// HLL_EXPORT(FillAMapUnderBorder, Gpx2Plus_FillAMapUnderBorder),
			// HLL_EXPORT(SaturDP_DPxSA, Gpx2Plus_SaturDP_DPxSA),
			// HLL_EXPORT(ScreenDA_DAxSA, Gpx2Plus_ScreenDA_DAxSA),
			// HLL_EXPORT(AddDA_DAxSA, Gpx2Plus_AddDA_DAxSA),
			// HLL_EXPORT(SpriteCopyAMap, Gpx2Plus_SpriteCopyAMap),
			// HLL_EXPORT(BrightDestOnly, Gpx2Plus_BrightDestOnly),
			// HLL_EXPORT(CopyTextureWrap, Gpx2Plus_CopyTextureWrap),
			// HLL_EXPORT(CopyTextureWrapAlpha, Gpx2Plus_CopyTextureWrapAlpha),
			HLL_EXPORT(CopyStretch, Gpx2Plus_CopyStretch),
			// HLL_EXPORT(CopyStretchBlend, Gpx2Plus_CopyStretchBlend),
			HLL_EXPORT(CopyStretchBlendAMap, Gpx2Plus_CopyStretchBlendAMap),
			// HLL_EXPORT(StretchBlendScreen2x2, Gpx2Plus_StretchBlendScreen2x2),
			// HLL_EXPORT(StretchBlendScreen2x2WDS, Gpx2Plus_StretchBlendScreen2x2WDS),
			HLL_EXPORT(CopyStretchReduce, Gpx2Plus_CopyStretchReduce),
			HLL_EXPORT(CopyStretchReduceAMap, Gpx2Plus_CopyStretchReduceAMap),
			HLL_EXPORT(CopyReverseLR, Gpx2Plus_CopyReverseLR),
			// HLL_EXPORT(CopyReverseUD, Gpx2Plus_CopyReverseUD),
			HLL_EXPORT(CopyReverseAMapLR, Gpx2Plus_CopyReverseAMapLR),
			// HLL_EXPORT(CopyReverseAMapUD, Gpx2Plus_CopyReverseAMapUD),
			// HLL_EXPORT(BlendScreenWDS, Gpx2Plus_BlendScreenWDS),
			HLL_EXPORT(EffectCopy, Gpx2Plus_EffectCopy),
			// HLL_EXPORT(SetClickCancelFlag, Gpx2Plus_SetClickCancelFlag),
			HLL_EXPORT(Update, Gpx2Plus_Update),
			HLL_EXPORT(DrawText, Gpx2Plus_DrawText),
			HLL_EXPORT(DrawTextToAMap, Gpx2Plus_DrawTextToAMap),
			HLL_EXPORT(SetFontSize, gfx_set_font_size),
			HLL_EXPORT(SetFontName, Gpx2Plus_SetFontName),
			HLL_EXPORT(SetFontWeight, gfx_set_font_weight),
			HLL_EXPORT(SetFontUnderline, gfx_set_font_underline),
			HLL_EXPORT(SetFontStrikeOut, gfx_set_font_strikeout),
			HLL_EXPORT(SetFontSpace, gfx_set_font_space),
			HLL_EXPORT(SetFontColor, Gpx2Plus_SetFontColor),
			HLL_EXPORT(GetFontSize, gfx_get_font_size),
			// HLL_EXPORT(GetFontName, Gpx2Plus_GetFontName),
			HLL_EXPORT(GetFontWeight, gfx_get_font_weight),
			HLL_EXPORT(GetFontUnderline, gfx_get_font_underline),
			HLL_EXPORT(GetFontStrikeOut, gfx_get_font_strikeout),
			HLL_EXPORT(GetFontSpace, gfx_get_font_space),
			HLL_EXPORT(GetFontColor, Gpx2Plus_GetFontColor),
			HLL_EXPORT(MsgAddText, Gpx2Plus_MsgAddText),
			HLL_EXPORT(MsgClearText, Gpx2Plus_MsgClearText),
			HLL_EXPORT(MsgDraw, Gpx2Plus_MsgDraw));
