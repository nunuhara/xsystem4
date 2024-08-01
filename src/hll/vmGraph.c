/* Copyright (C) 2024 kichikuou <KichikuouChrome@gmail.com>
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

#include "effect.h"
#include "gfx/gfx.h"
#include "vmSurface.h"
#include "hll.h"

HLL_QUIET_UNIMPLEMENTED( , void, vmGraph, Init, void *imainsystem);
//void vmGraph_SetUseCPUEx(int nUse);

static void vmGraph_Copy(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height)
{
	struct vm_surface *dst = vm_surface_get(dst_surface);
	struct vm_surface *src = vm_surface_get(src_surface);
	if (!dst || !src)
		return;
	gfx_copy_with_alpha_map(&dst->texture, dx, dy, &src->texture, sx, sy, width, height);
}

static void vmGraph_CopyPixelMap(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height)
{
	struct vm_surface *dst = vm_surface_get(dst_surface);
	struct vm_surface *src = vm_surface_get(src_surface);
	if (!dst || !src)
		return;
	gfx_copy(&dst->texture, dx, dy, &src->texture, sx, sy, width, height);
}

static void vmGraph_CopyAlphaMap(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height)
{
	struct vm_surface *dst = vm_surface_get(dst_surface);
	struct vm_surface *src = vm_surface_get(src_surface);
	if (!dst || !src)
		return;
	gfx_copy_amap(&dst->texture, dx, dy, &src->texture, sx, sy, width, height);
}

//void vmGraph_CopyBright(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height, int nRate);

static void vmGraph_CopySprite(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height, int r, int g, int b)
{
	struct vm_surface *dst = vm_surface_get(dst_surface);
	struct vm_surface *src = vm_surface_get(src_surface);
	if (!dst || !src)
		return;
	gfx_copy_sprite(&dst->texture, dx, dy, &src->texture, sx, sy, width, height, COLOR(r, g, b, 0));
}

//void vmGraph_Blend(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height, int alpha);
//void vmGraph_BlendSrcBright(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height, int alpha, int nRate);

static void vmGraph_BlendAddSatur(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height)
{
	struct vm_surface *dst = vm_surface_get(dst_surface);
	struct vm_surface *src = vm_surface_get(src_surface);
	if (!dst || !src)
		return;
	gfx_blend_add_satur(&dst->texture, dx, dy, &src->texture, sx, sy, width, height);
}

static void vmGraph_BlendAlphaMap(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height)
{
	struct vm_surface *dst = vm_surface_get(dst_surface);
	struct vm_surface *src = vm_surface_get(src_surface);
	if (!dst || !src)
		return;
	gfx_blend_amap(&dst->texture, dx, dy, &src->texture, sx, sy, width, height);
}

//void vmGraph_BlendAlphaMapSrcOnly(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height);
//void vmGraph_BlendAlphaMapColor(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height, int r, int g, int b);
//void vmGraph_BlendAlphaMapColorAlpha(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height, int r, int g, int b, int alpha);
//void vmGraph_BlendAlphaMapAlpha(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height, int alpha);
//void vmGraph_BlendAlphaMapBright(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height, int nRate);
//void vmGraph_BlendAlphaMapAlphaSrcBright(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height, int alpha, int nRate);
//void vmGraph_BlendUseAMapColor(int dst_surface, int dx, int dy, int width, int height, int hAlphaSurface, int nAx, int nAy, int r, int g, int b, int alpha);

static void vmGraph_BlendScreen(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height)
{
	struct vm_surface *dst = vm_surface_get(dst_surface);
	struct vm_surface *src = vm_surface_get(src_surface);
	if (!dst || !src)
		return;
	gfx_blend_screen(&dst->texture, dx, dy, &src->texture, sx, sy, width, height);
}

//void vmGraph_BlendMultiply(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height);
//void vmGraph_BlendScreenAlpha(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height, int alpha);

static void vmGraph_Fill(int surface, int x, int y, int width, int height, int r, int g, int b, int alpha)
{
	struct vm_surface *dst = vm_surface_get(surface);
	if (!dst)
		return;
	gfx_fill_with_alpha(&dst->texture, x, y, width, height, r, g, b, alpha);
}

static void vmGraph_FillPixelMap(int surface, int x, int y, int width, int height, int r, int g, int b)
{
	struct vm_surface *dst = vm_surface_get(surface);
	if (!dst)
		return;
	gfx_fill(&dst->texture, x, y, width, height, r, g, b);
}

static void vmGraph_FillAlphaMap(int surface, int x, int y, int width, int height, int alpha)
{
	struct vm_surface *dst = vm_surface_get(surface);
	if (!dst)
		return;
	gfx_fill_amap(&dst->texture, x, y, width, height, alpha);
}

static void vmGraph_FillAlphaColor(int surface, int x, int y, int width, int height, int r, int g, int b, int rate)
{
	struct vm_surface *dst = vm_surface_get(surface);
	if (!dst)
		return;
	gfx_fill_alpha_color(&dst->texture, x, y, width, height, r, g, b, rate);
}

//void vmGraph_SaturDP_DPxSA(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height);
//void vmGraph_ScreenDA_DAxSA(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height);

static void vmGraph_AddDA_DAxSA(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height)
{
	struct vm_surface *dst = vm_surface_get(dst_surface);
	struct vm_surface *src = vm_surface_get(src_surface);
	if (!dst || !src)
		return;
	gfx_add_da_daxsa(&dst->texture, dx, dy, &src->texture, sx, sy, width, height);
}

static void vmGraph_SubDA_DAxSA(int dst_surface, int dx, int dy, int src_surface, int sx, int sy, int width, int height)
{
	struct vm_surface *dst = vm_surface_get(dst_surface);
	struct vm_surface *src = vm_surface_get(src_surface);
	if (!dst || !src)
		return;
	gfx_sub_da_daxsa(&dst->texture, dx, dy, &src->texture, sx, sy, width, height);
}

//void vmGraph_BrightDestOnly(int surface, int x, int y, int width, int height, int nRate);
//void vmGraph_CopyTextureWrap(int dst_surface, int dx, int dy, int d_width, int d_height, int src_surface, int sx, int sy, int s_width, int s_height, int nU, int nV);
//void vmGraph_CopyTextureWrapAlpha(int dst_surface, int dx, int dy, int d_width, int d_height, int src_surface, int sx, int sy, int s_width, int s_height, int nU, int nV, int alpha);

static void vmGraph_CopyStretch(int surface, int dx, int dy, int d_width, int d_height, int src_surface, float sx, float sy, float s_width, float s_height)
{
	struct vm_surface *dst = vm_surface_get(surface);
	struct vm_surface *src = vm_surface_get(src_surface);
	if (!dst || !src)
		return;

	// Use nearest neighbor interpolation to avoid magenta edges.
	glBindTexture(GL_TEXTURE_2D, src->texture.handle);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);

	gfx_copy_stretch(
		&dst->texture, dx, dy, d_width, d_height,
		&src->texture, sx, sy, s_width, s_height);

	glBindTexture(GL_TEXTURE_2D, src->texture.handle);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);
}

//void vmGraph_CopyStretchBlend(int surface, int dx, int dy, int d_width, int d_height, int src_surface, float sx, float sy, float s_width, float s_height, int alpha);
//void vmGraph_CopyStretchBlendAlphaMap(int surface, int dx, int dy, int d_width, int d_height, int src_surface, float sx, float sy, float s_width, float s_height);
HLL_WARN_UNIMPLEMENTED( , void, vmGraph, SetClickCancel, int click);

static int vmGraph_EffectCopy(int dx, int dy, int src_surface, int sx, int sy, int width, int height, int effect, int total_time)
{
	uint32_t start = SDL_GetTicks();
	if (!effect_init(effect))
		return 0;
	vmGraph_Copy(vm_surface_get_main_surface(), dx, dy, src_surface, sx, sy, width, height);

	uint32_t t = SDL_GetTicks() - start;
	while (t < total_time) {
		effect_update((float)t / (float)total_time);
		uint32_t t2 = SDL_GetTicks() - start;
		if (t2 < t + 16) {
			SDL_Delay(t + 16 - t2);
			t += 16;
		} else {
			t = t2;
		}
	}
	effect_update(1.0);

	effect_fini();
	return 1;
}

//int vmGraph_GetMainWidth(void);
//int vmGraph_GetMainHeight(void);
//int vmGraph_GetMainBpp(void);

void vmGraph_Update(int x, int y, int width, int height)
{
	gfx_swap();
}

HLL_LIBRARY(vmGraph,
	    HLL_EXPORT(Init, vmGraph_Init),
	    HLL_TODO_EXPORT(SetUseCPUEx, vmGraph_SetUseCPUEx),
	    HLL_EXPORT(Copy, vmGraph_Copy),
	    HLL_EXPORT(CopyPixelMap, vmGraph_CopyPixelMap),
	    HLL_EXPORT(CopyAlphaMap, vmGraph_CopyAlphaMap),
	    HLL_TODO_EXPORT(CopyBright, vmGraph_CopyBright),
	    HLL_EXPORT(CopySprite, vmGraph_CopySprite),
	    HLL_TODO_EXPORT(Blend, vmGraph_Blend),
	    HLL_TODO_EXPORT(BlendSrcBright, vmGraph_BlendSrcBright),
	    HLL_EXPORT(BlendAddSatur, vmGraph_BlendAddSatur),
	    HLL_EXPORT(BlendAlphaMap, vmGraph_BlendAlphaMap),
	    HLL_TODO_EXPORT(BlendAlphaMapSrcOnly, vmGraph_BlendAlphaMapSrcOnly),
	    HLL_TODO_EXPORT(BlendAlphaMapColor, vmGraph_BlendAlphaMapColor),
	    HLL_TODO_EXPORT(BlendAlphaMapColorAlpha, vmGraph_BlendAlphaMapColorAlpha),
	    HLL_TODO_EXPORT(BlendAlphaMapAlpha, vmGraph_BlendAlphaMapAlpha),
	    HLL_TODO_EXPORT(BlendAlphaMapBright, vmGraph_BlendAlphaMapBright),
	    HLL_TODO_EXPORT(BlendAlphaMapAlphaSrcBright, vmGraph_BlendAlphaMapAlphaSrcBright),
	    HLL_TODO_EXPORT(BlendUseAMapColor, vmGraph_BlendUseAMapColor),
	    HLL_EXPORT(BlendScreen, vmGraph_BlendScreen),
	    HLL_TODO_EXPORT(BlendMultiply, vmGraph_BlendMultiply),
	    HLL_TODO_EXPORT(BlendScreenAlpha, vmGraph_BlendScreenAlpha),
	    HLL_EXPORT(Fill, vmGraph_Fill),
	    HLL_EXPORT(FillPixelMap, vmGraph_FillPixelMap),
	    HLL_EXPORT(FillAlphaMap, vmGraph_FillAlphaMap),
	    HLL_EXPORT(FillAlphaColor, vmGraph_FillAlphaColor),
	    HLL_TODO_EXPORT(SaturDP_DPxSA, vmGraph_SaturDP_DPxSA),
	    HLL_TODO_EXPORT(ScreenDA_DAxSA, vmGraph_ScreenDA_DAxSA),
	    HLL_EXPORT(AddDA_DAxSA, vmGraph_AddDA_DAxSA),
	    HLL_EXPORT(SubDA_DAxSA, vmGraph_SubDA_DAxSA),
	    HLL_TODO_EXPORT(BrightDestOnly, vmGraph_BrightDestOnly),
	    HLL_TODO_EXPORT(CopyTextureWrap, vmGraph_CopyTextureWrap),
	    HLL_TODO_EXPORT(CopyTextureWrapAlpha, vmGraph_CopyTextureWrapAlpha),
	    HLL_EXPORT(CopyStretch, vmGraph_CopyStretch),
	    HLL_TODO_EXPORT(CopyStretchBlend, vmGraph_CopyStretchBlend),
	    HLL_TODO_EXPORT(CopyStretchBlendAlphaMap, vmGraph_CopyStretchBlendAlphaMap),
	    HLL_EXPORT(SetClickCancel, vmGraph_SetClickCancel),
	    HLL_EXPORT(EffectCopy, vmGraph_EffectCopy),
	    HLL_EXPORT(GetMainSurface, vm_surface_get_main_surface),
	    HLL_TODO_EXPORT(GetMainWidth, vmGraph_GetMainWidth),
	    HLL_TODO_EXPORT(GetMainHeight, vmGraph_GetMainHeight),
	    HLL_TODO_EXPORT(GetMainBpp, vmGraph_GetMainBpp),
	    HLL_EXPORT(Update, vmGraph_Update)
	    );
