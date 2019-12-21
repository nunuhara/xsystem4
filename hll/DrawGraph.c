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
#include "../sact.h"
#include "../graphics.h"

#define TEX(sp_no) sact_get_texture(sp_no)

//void Copy(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_defun_inline(Copy, (gfx_copy(TEX(a[0].i), a[1].i, a[2].i, TEX(a[3].i), a[4].i, a[5].i, a[6].i, a[7].i), 0));
//void CopyBright(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight, int nRate)
hll_defun_inline(CopyBright, (gfx_copy_bright(TEX(a[0].i), a[1].i, a[2].i, TEX(a[3].i), a[4].i, a[5].i, a[6].i, a[7].i, a[8].i), 0));
//void CopyAMap(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_defun_inline(CopyAMap, (gfx_copy_amap(TEX(a[0].i), a[1].i, a[2].i, TEX(a[3].i), a[4].i, a[5].i, a[6].i, a[7].i), 0));
//void CopySprite(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight, int nR, int nG, int nB)
hll_defun_inline(CopySprite, (gfx_copy_sprite(TEX(a[0].i), a[1].i, a[2].i, TEX(a[3].i), a[4].i, a[5].i, a[6].i, a[7].i, COLOR(a[8].i, a[9].i, a[10].i, 0)), 0));
//void CopyColorReverse(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_unimplemented(DrawGraph, CopyColorReverse);
//void CopyUseAMapUnder(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight, int nAlpha)
hll_defun_inline(CopyUseAMapUnder, (gfx_copy_use_amap_under(TEX(a[0].i), a[1].i, a[2].i, TEX(a[3].i), a[4].i, a[5].i, a[6].i, a[7].i, a[8].i), 0));
//void CopyUseAMapBorder(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight, int nAlpha)
hll_defun_inline(CopyUseAMapBorder, (gfx_copy_use_amap_border(TEX(a[0].i), a[1].i, a[2].i, TEX(a[3].i), a[4].i, a[5].i, a[6].i, a[7].i, a[8].i), 0));
//void CopyAMapMax(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_defun_inline(CopyAMapMax, (gfx_copy_amap_max(TEX(a[0].i), a[1].i, a[2].i, TEX(a[3].i), a[4].i, a[5].i, a[6].i, a[7].i), 0));
//void CopyAMapMin(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_defun_inline(CopyAMapMin, (gfx_copy_amap_min(TEX(a[0].i), a[1].i, a[2].i, TEX(a[3].i), a[4].i, a[5].i, a[6].i, a[7].i), 0));
//void Blend(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight, int nAlpha)
hll_unimplemented(DrawGraph, Blend);
//void BlendSrcBright(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight, int nAlpha, int nRate)
hll_unimplemented(DrawGraph, BlendSrcBright);
//void BlendAddSatur(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_unimplemented(DrawGraph, BlendAddSatur);
//void BlendAMap(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_defun_inline(BlendAMap, (gfx_blend_amap(TEX(a[0].i), a[1].i, a[2].i, TEX(a[3].i), a[4].i, a[5].i, a[6].i, a[7].i), 0));
//void BlendAMapSrcOnly(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_unimplemented(DrawGraph, BlendAMapSrcOnly);
//void BlendAMapColor(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight, int nR, int nG, int nB)
hll_unimplemented(DrawGraph, BlendAMapColor);
//void BlendAMapColorAlpha(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight, int nR, int nG, int nB, int nAlpha)
hll_unimplemented(DrawGraph, BlendAMapColorAlpha);
//void BlendAMapAlpha(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight, int nAlpha)
hll_defun_inline(BlendAMapAlpha, (gfx_blend_amap_alpha(TEX(a[0].i), a[1].i, a[2].i, TEX(a[3].i), a[4].i, a[5].i, a[6].i, a[7].i, a[8].i), 0));
//void BlendAMapBright(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight, int nRate)
hll_unimplemented(DrawGraph, BlendAMapBright);
//void BlendAMapAlphaSrcBright(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight, int nAlpha, int nRate)
hll_unimplemented(DrawGraph, BlendAMapAlphaSrcBright);
//void BlendUseAMapColor(int nDest, int nDx, int nDy, int nAlpha, int nAx, int nAy, int nWidth, int nHeight, int nR, int nG, int nB, int nRate)
hll_unimplemented(DrawGraph, BlendUseAMapColor);
//void BlendScreen(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_unimplemented(DrawGraph, BlendScreen);
//void BlendMultiply(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_unimplemented(DrawGraph, BlendMultiply);
//void BlendScreenAlpha(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight, int nAlpha)
hll_unimplemented(DrawGraph, BlendScreenAlpha);
//void Fill(int nDest, int nX, int nY, int nWidth, int nHeight, int nR, int nG, int nB)
hll_defun_inline(Fill, (gfx_fill(TEX(a[0].i), a[1].i, a[2].i, a[3].i, a[4].i, a[5].i, a[6].i, a[7].i), 0));
//void FillAlphaColor(int nDest, int nX, int nY, int nWidth, int nHeight, int nR, int nG, int nB, int nRate)
hll_defun_inline(FillAlphaColor, (gfx_fill_alpha_color(TEX(a[0].i), a[1].i, a[2].i, a[3].i, a[4].i, a[5].i, a[6].i, a[7].i, a[8].i), 0));
//void FillAMap(int nDest, int nX, int nY, int nWidth, int nHeight, int nAlpha)
hll_defun_inline(FillAMap, (gfx_fill_amap(TEX(a[0].i), a[1].i, a[2].i, a[3].i, a[4].i, a[5].i), 0));
//void FillAMapOverBorder(int nDest, int nX, int nY, int nWidth, int nHeight, int nAlpha, int nBorder)
hll_unimplemented(DrawGraph, FillAMapOverBorder);
//void FillAMapUnderBorder(int nDest, int nX, int nY, int nWidth, int nHeight, int nAlpha, int nBorder)
hll_unimplemented(DrawGraph, FillAMapUnderBorder);
//void FillAMapGradationUD(int nDest, int nX, int nY, int nWidth, int nHeight, int nUpA, int nDownA)
hll_unimplemented(DrawGraph, FillAMapGradationUD);
//void FillScreen(int nDest, int nX, int nY, int nWidth, int nHeight, int nR, int nG, int nB)
hll_unimplemented(DrawGraph, FillScreen);
//void FillMultiply(int nDest, int nX, int nY, int nWidth, int nHeight, int nR, int nG, int nB)
hll_unimplemented(DrawGraph, FillMultiply);
//void SaturDP_DPxSA(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_unimplemented(DrawGraph, SaturDP);
//void ScreenDA_DAxSA(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_unimplemented(DrawGraph, ScreenDA_DAxSA);
//void AddDA_DAxSA(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_defun_inline(AddDA_DAxSA, (gfx_add_da_daxsa(TEX(a[0].i), a[1].i, a[2].i, TEX(a[3].i), a[4].i, a[5].i, a[6].i, a[7].i), 0));
//void SpriteCopyAMap(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight, int nColorKey)
hll_unimplemented(DrawGraph, SpriteCopyAMap);
//void BlendDA_DAxSA(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_unimplemented(DrawGraph, BlendDA);
//void SubDA_DAxSA(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_unimplemented(DrawGraph, SubDA);
//void BrightDestOnly(int nDest, int nX, int nY, int nWidth, int nHeight, int nRate)
hll_unimplemented(DrawGraph, BrightDestOnly);
//void CopyTextureWrap(int nDest, int nDx, int nDy, int nDWidth, int nDHeight, int nSrc, int nSx, int nSy, int nSWidth, int nSHeight, int nU, int nV)
hll_unimplemented(DrawGraph, CopyTextureWrap);
//void CopyTextureWrapAlpha(int nDest, int nDx, int nDy, int nDWidth, int nDHeight, int nSrc, int nSx, int nSy, int nSWidth, int nSHeight, int nU, int nV, int nAlpha)
hll_unimplemented(DrawGraph, CopyTextureWrapAlpha);
//void CopyStretch(int nDest, int nDx, int nDy, int nDWidth, int nDHeight, int nSrc, int nSx, int nSy, int nSWidth, int nSHeight)
hll_defun_inline(CopyStretch, (gfx_copy_stretch(TEX(a[0].i), a[1].i, a[2].i, a[3].i, a[4].i, TEX(a[5].i), a[6].i, a[7].i, a[8].i, a[9].i), 0));
//void CopyStretchBlend(int nDest, int nDx, int nDy, int nDWidth, int nDHeight, int nSrc, int nSx, int nSy, int nSWidth, int nSHeight, int nAlpha)
hll_unimplemented(DrawGraph, CopyStretchBlend);
//void CopyStretchBlendAMap(int nDest, int nDx, int nDy, int nDWidth, int nDHeight, int nSrc, int nSx, int nSy, int nSWidth, int nSHeight)
hll_unimplemented(DrawGraph, CopyStretchBlendAMap);
//void CopyStretchAMap(int nDest, int nDx, int nDy, int nDWidth, int nDHeight, int nSrc, int nSx, int nSy, int nSWidth, int nSHeight)
hll_defun_inline(CopyStretchAMap, (gfx_copy_stretch_amap(TEX(a[0].i), a[1].i, a[2].i, a[3].i, a[4].i, TEX(a[5].i), a[6].i, a[7].i, a[8].i, a[9].i), 0));
//void CopyStretchInterp(int nDest, int nDx, int nDy, int nDWidth, int nDHeight, int nSrc, int nSx, int nSy, int nSWidth, int nSHeight)
hll_unimplemented(DrawGraph, CopyStretchInterp);
//void CopyStretchAMapInterp(int nDest, int nDx, int nDy, int nDWidth, int nDHeight, int nSrc, int nSx, int nSy, int nSWidth, int nSHeight)
hll_unimplemented(DrawGraph, CopyStretchAMapInterp);
//void CopyReduce(int nDest, int nDx, int nDy, int nDWidth, int nDHeight, int nSrc, int nSx, int nSy, int nSWidth, int nSHeight)
hll_defun_inline(CopyReduce, (gfx_copy_stretch(TEX(a[0].i), a[1].i, a[2].i, a[3].i, a[4].i, TEX(a[5].i), a[6].i, a[7].i, a[8].i, a[9].i), 0));
//void CopyReduceAMap(int nDest, int nDx, int nDy, int nDWidth, int nDHeight, int nSrc, int nSx, int nSy, int nSWidth, int nSHeight)
hll_defun_inline(CopyReduceAMap, (gfx_copy_stretch_amap(TEX(a[0].i), a[1].i, a[2].i, a[3].i, a[4].i, TEX(a[5].i), a[6].i, a[7].i, a[8].i, a[9].i), 0));
//void DrawTextToPMap(int nDest, int nX, int nY, string szText)
hll_defun_inline(DrawTextToPMap, (gfx_draw_text_to_pmap(TEX(a[0].i), a[1].i, a[2].i, hll_string_ref(a[3].i)->text), 0));
//void DrawTextToAMap(int nDest, int nX, int nY, string szText)
hll_defun_inline(DrawTextToAMap, (gfx_draw_text_to_amap(TEX(a[0].i), a[1].i, a[2].i, hll_string_ref(a[3].i)->text), 0));
//void SetFontSize(int nSize)
hll_defun_inline(SetFontSize, gfx_set_font_size(a[0].i));
//void SetFontName(string pIText)
hll_warn_unimplemented(DrawGraph, SetFontName, 0);
//void SetFontWeight(int nWeight)
hll_defun_inline(SetFontWeight, gfx_set_font_weight(a[0].i));
//void SetFontUnderline(int nFlag)
hll_defun_inline(SetFontUnderline, gfx_set_font_underline(a[0].i));
//void SetFontStrikeOut(int nFlag)
hll_defun_inline(SetFontStrikeOut, gfx_set_font_strikeout(a[0].i));
//void SetFontSpace(int nSpace)
hll_defun_inline(SetFontSpace, gfx_set_font_space(a[0].i));
//void SetFontColor(int nR, int nG, int nB)
hll_defun_inline(SetFontColor, gfx_set_font_color(COLOR(a[0].i, a[1].i, a[2].i, 255)));
//int GetFontSize()
hll_defun_inline(GetFontSize, gfx_get_font_size());
//string GetFontName()
hll_warn_unimplemented(DrawGraph, GetFontName, vm_string_ref(&EMPTY_STRING)); // FIXME
//int GetFontWeight()
hll_defun_inline(GetFontWeight, (int)gfx_get_font_weight());
//int GetFontUnderline()
hll_defun_inline(GetFontUnderline, gfx_get_font_underline());
//int GetFontStrikeOut()
hll_defun_inline(GetFontStrikeOut, gfx_get_font_strikeout());
//int GetFontSpace()
hll_defun_inline(GetFontSpace, gfx_get_font_space());

//void GetFontColor(ref int pnR, ref int pnG, ref int pnB)
hll_defun(GetFontColor, args)
{
	SDL_Color c = gfx_get_font_color();
	*args[0].iref = c.r;
	*args[1].iref = c.g;
	*args[2].iref = c.b;
	hll_return(0);
}

//void CopyRotZoom(int nDest, int nSrc, int nSx, int nSy, int nWidth, int nHeight, float fRotate, float fMag)
hll_defun_inline(CopyRotZoom, (gfx_copy_rot_zoom(TEX(a[0].i), TEX(a[1].i), a[2].i, a[3].i, a[4].i, a[5].i, a[6].f, a[7].f), 0));
//void CopyRotZoomAMap(int nDest, int nSrc, int nSx, int nSy, int nWidth, int nHeight, float fRotate, float fMag)
hll_defun_inline(CopyRotZoomAMap, (gfx_copy_rot_zoom_amap(TEX(a[0].i), TEX(a[1].i), a[2].i, a[3].i, a[4].i, a[5].i, a[6].f, a[7].f), 0));
//void CopyRotZoomUseAMap(int nDest, int nSrc, int nSx, int nSy, int nWidth, int nHeight, float fRotate, float fMag)
hll_defun_inline(CopyRotZoomUseAMap, (gfx_copy_rot_zoom_use_amap(TEX(a[0].i), TEX(a[1].i), a[2].i, a[3].i, a[4].i, a[5].i, a[6].f, a[7].f), 0));
//void CopyRotZoom2Bilinear(int nDest, float fCx, float fCy, int nSrc, float fSrcCx, float fSrcCy, float fRotate, float fMag)
hll_unimplemented(DrawGraph, CopyRotZoom2Bilinear);
//void CopyRotateY(int nWrite, int nDest, int nSrc, int nSx, int nSy, int nWidth, int nHeight, float fRotate, float fMag)
hll_unimplemented(DrawGraph, CopyRotateY);
//void CopyRotateYUseAMap(int nWrite, int nDest, int nSrc, int nSx, int nSy, int nWidth, int nHeight, float fRotate, float fMag)
hll_unimplemented(DrawGraph, CopyRotateYUseAMap);
//void CopyRotateYFixL(int nWrite, int nDest, int nSrc, int nSx, int nSy, int nWidth, int nHeight, float fRotate, float fMag)
hll_unimplemented(DrawGraph, CopyRotateYFixL);
//void CopyRotateYFixR(int nWrite, int nDest, int nSrc, int nSx, int nSy, int nWidth, int nHeight, float fRotate, float fMag)
hll_unimplemented(DrawGraph, CopyRotateYFixR);
//void CopyRotateYFixLUseAMap(int nWrite, int nDest, int nSrc, int nSx, int nSy, int nWidth, int nHeight, float fRotate, float fMag)
hll_unimplemented(DrawGraph, CopyRotateYFixLUseAMap);
//void CopyRotateYFixRUseAMap(int nWrite, int nDest, int nSrc, int nSx, int nSy, int nWidth, int nHeight, float fRotate, float fMag)
hll_unimplemented(DrawGraph, CopyRotateYFixRUseAMap);
//void CopyRotatex(int nWrite, int nDest, int nSrc, int nSx, int nSy, int nWidth, int nHeight, float fRotate, float fMag)
hll_unimplemented(DrawGraph, CopyRotateX);
//void CopyRotateXUseAMap(int nWrite, int nDest, int nSrc, int nSx, int nSy, int nWidth, int nHeight, float fRotate, float fMag)
hll_unimplemented(DrawGraph, CopyRotateXUseAMap);
//void CopyRotateXFixU(int nWrite, int nDest, int nSrc, int nSx, int nSy, int nWidth, int nHeight, float fRotate, float fMag)
hll_unimplemented(DrawGraph, CopyRotateXFixU);
//void CopyRotateXFixD(int nWrite, int nDest, int nSrc, int nSx, int nSy, int nWidth, int nHeight, float fRotate, float fMag)
hll_unimplemented(DrawGraph, CopyRotateXFixD);
//void CopyRotateXFixUUseAMap(int nWrite, int nDest, int nSrc, int nSx, int nSy, int nWidth, int nHeight, float fRotate, float fMag)
hll_unimplemented(DrawGraph, CopyRotateXFixUUseAMap);
//void CopyRotateXFixDUseAMap(int nWrite, int nDest, int nSrc, int nSx, int nSy, int nWidth, int nHeight, float fRotate, float fMag)
hll_unimplemented(DrawGraph, CopyRotateXFixDUseAMap);
//void CopyReverseLR(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_defun_inline(CopyReverseLR, (gfx_copy_reverse_LR(TEX(a[0].i), a[1].i, a[2].i, TEX(a[3].i), a[4].i, a[5].i, a[6].i, a[7].i), 0));
//void CopyReverseUD(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_unimplemented(DrawGraph, CopyReverseUD);
//void CopyReverseAMapLR(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_defun_inline(CopyReverseAMapLR, (gfx_copy_reverse_amap_LR(TEX(a[0].i), a[1].i, a[2].i, TEX(a[3].i), a[4].i, a[5].i, a[6].i, a[7].i), 0));
//void CopyReverseAMapUD(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight)
hll_unimplemented(DrawGraph, CopyReverseAMapUD);
//void CopyWidthBlur(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight, int nBlur)
hll_unimplemented(DrawGraph, CopyWidthBlur);
//void CopyHeightBlur(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight, int nBlur)
hll_unimplemented(DrawGraph, CopyHeightBlur);
//void CopyAMapWidthBlur(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight, int nBlur)
hll_unimplemented(DrawGraph, CopyAMapWidthBlur);
//void CopyAMapHeightBlur(int nDest, int nDx, int nDy, int nSrc, int nSx, int nSy, int nWidth, int nHeight, int nBlur)
hll_unimplemented(DrawGraph, CopyAMapHeightBlur);
//void DrawLine(int nDest, int nX0, int nY0, int nX1, int nY1, int nR, int nG, int nB)
hll_unimplemented(DrawGraph, DrawLine);
//void DrawLineToAMap(int nDest, int nX0, int nY0, int nX1, int nY1, int nAlpha)
hll_unimplemented(DrawGraph, DrawLineToAMap);
//bool GetPixelColor(int nSurface, int nX, int nY, ref int nR, ref int nG, ref int nB)
hll_defun_inline(GetPixelColor, sact_SP_GetPixelValue(a[0].i, a[1].i, a[2].i, a[3].iref, a[4].iref, a[5].iref));
//bool GetAlphaColor(int nSurface, int nX, int nY, ref int nA)
hll_defun_inline(GetAlphaColor, (*a[3].iref = sact_SP_GetAMapValue(a[0].i, a[1].i, a[2].i), 1));
//void DrawPolygon(int nDest, int nTex, float fX0, float fY0, float fZ0, float fU0, float fV0, float fX1, float fY1, float fZ1, float fU1, float fV1, float fX2, float fY2, float fZ2, float fU2, float fV2)
hll_unimplemented(DrawGraph, DrawPolygon);
//void DrawColorPolygon(int nDest, float fX0, float fY0, float fZ0, int nR0, int nG0, int nB0, int nA0, float fX1, float fY1, float fZ1, int nR1, int nG1, int nB1, int nA1, float fX2, float fY2, float fZ2, int nR2, int nG2, int nB2, int nA2)
hll_unimplemented(DrawGraph, DrawColorPolygon);

hll_deflib(DrawGraph) {
	hll_export(Copy),
	hll_export(CopyBright),
	hll_export(CopyAMap),
	hll_export(CopySprite),
	hll_export(CopyColorReverse),
	hll_export(CopyUseAMapUnder),
	hll_export(CopyUseAMapBorder),
	hll_export(CopyAMapMax),
	hll_export(CopyAMapMin),
	hll_export(Blend),
	hll_export(BlendSrcBright),
	hll_export(BlendAddSatur),
	hll_export(BlendAMap),
	hll_export(BlendAMapSrcOnly),
	hll_export(BlendAMapColor),
	hll_export(BlendAMapColorAlpha),
	hll_export(BlendAMapAlpha),
	hll_export(BlendAMapBright),
	hll_export(BlendAMapAlphaSrcBright),
	hll_export(BlendUseAMapColor),
	hll_export(BlendScreen),
	hll_export(BlendMultiply),
	hll_export(BlendScreenAlpha),
	hll_export(Fill),
	hll_export(FillAlphaColor),
	hll_export(FillAMap),
	hll_export(FillAMapOverBorder),
	hll_export(FillAMapUnderBorder),
	hll_export(FillAMapGradationUD),
	hll_export(FillScreen),
	hll_export(FillMultiply),
	hll_export(SaturDP),
	hll_export(ScreenDA_DAxSA),
	hll_export(AddDA_DAxSA),
	hll_export(SpriteCopyAMap),
	hll_export(BlendDA),
	hll_export(SubDA),
	hll_export(BrightDestOnly),
	hll_export(CopyTextureWrap),
	hll_export(CopyTextureWrapAlpha),
	hll_export(CopyStretch),
	hll_export(CopyStretchBlend),
	hll_export(CopyStretchBlendAMap),
	hll_export(CopyStretchAMap),
	hll_export(CopyStretchInterp),
	hll_export(CopyStretchAMapInterp),
	hll_export(CopyReduce),
	hll_export(CopyReduceAMap),
	hll_export(DrawTextToPMap),
	hll_export(DrawTextToAMap),
	hll_export(SetFontSize),
	hll_export(SetFontName),
	hll_export(SetFontWeight),
	hll_export(SetFontUnderline),
	hll_export(SetFontStrikeOut),
	hll_export(SetFontSpace),
	hll_export(SetFontColor),
	hll_export(GetFontSize),
	hll_export(GetFontName),
	hll_export(GetFontWeight),
	hll_export(GetFontUnderline),
	hll_export(GetFontStrikeOut),
	hll_export(GetFontSpace),
	hll_export(GetFontColor),
	hll_export(CopyRotZoom),
	hll_export(CopyRotZoomAMap),
	hll_export(CopyRotZoomUseAMap),
	hll_export(CopyRotZoom2Bilinear),
	hll_export(CopyRotateY),
	hll_export(CopyRotateYUseAMap),
	hll_export(CopyRotateYFixL),
	hll_export(CopyRotateYFixR),
	hll_export(CopyRotateYFixLUseAMap),
	hll_export(CopyRotateYFixRUseAMap),
	hll_export(CopyRotateX),
	hll_export(CopyRotateXUseAMap),
	hll_export(CopyRotateXFixU),
	hll_export(CopyRotateXFixD),
	hll_export(CopyRotateXFixUUseAMap),
	hll_export(CopyRotateXFixDUseAMap),
	hll_export(CopyReverseLR),
	hll_export(CopyReverseUD),
	hll_export(CopyReverseAMapLR),
	hll_export(CopyReverseAMapUD),
	hll_export(CopyWidthBlur),
	hll_export(CopyHeightBlur),
	hll_export(CopyAMapWidthBlur),
	hll_export(CopyAMapHeightBlur),
	hll_export(DrawLine),
	hll_export(DrawLineToAMap),
	hll_export(GetPixelColor),
	hll_export(GetAlphaColor),
	hll_export(DrawPolygon),
	hll_export(DrawColorPolygon),
	NULL
};
