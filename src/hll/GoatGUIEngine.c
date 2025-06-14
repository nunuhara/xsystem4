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

#include <assert.h>

#include "system4/ain.h"

#include "asset_manager.h"
#include "parts.h"
#include "xsystem4.h"
#include "hll.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

static inline float rad2deg(float rad)
{
	return rad * (180.0 / M_PI);
}

static void GoatGUIEngine_PreLink(void);

static void GoatGUIEngine_AddMotionHGaugeRate(int parts_no, int begin_numerator, int begin_denominator, int end_numerator, int end_denominator, int begin_t, int end_t)
{
	PE_AddMotionHGaugeRate(parts_no, (float)begin_numerator, (float)begin_denominator, (float)end_numerator, (float)end_denominator, begin_t, end_t);
}

static void GoatGUIEngine_AddMotionVGaugeRate(int parts_no, int begin_numerator, int begin_denominator, int end_numerator, int end_denominator, int begin_t, int end_t)
{
	PE_AddMotionVGaugeRate(parts_no, (float)begin_numerator, (float)begin_denominator, (float)end_numerator, (float)end_denominator, begin_t, end_t);
}

static void GoatGUIEngine_AddMotionRotateX(int parts_no, float begin, float end,
		int begin_t, int end_t)
{
	PE_AddMotionRotateX(parts_no, rad2deg(begin), rad2deg(end), begin_t, end_t);
}

static void GoatGUIEngine_AddMotionRotateY(int parts_no, float begin, float end,
		int begin_t, int end_t)
{
	PE_AddMotionRotateY(parts_no, rad2deg(begin), rad2deg(end), begin_t, end_t);
}

static void GoatGUIEngine_AddMotionRotateZ(int parts_no, float begin, float end,
		int begin_t, int end_t)
{
	PE_AddMotionRotateZ(parts_no, rad2deg(begin), rad2deg(end), begin_t, end_t);
}

static void GoatGUIEngine_SetPartsRotateX(int parts_no, float rot)
{
	PE_SetPartsRotateX(parts_no, rot);
}

static void GoatGUIEngine_SetPartsRotateY(int parts_no, float rot)
{
	PE_SetPartsRotateY(parts_no, rot);
}

static void GoatGUIEngine_SetPartsRotateZ(int parts_no, float rot)
{
	PE_SetPartsRotateZ(parts_no, rot);
}

HLL_LIBRARY(GoatGUIEngine,
	    HLL_EXPORT(_PreLink, GoatGUIEngine_PreLink),
	    HLL_EXPORT(_ModuleFini, PE_Reset),
	    HLL_EXPORT(Init, PE_Init),
	    HLL_EXPORT(Update, PE_Update),
	    HLL_EXPORT(SetPartsCG, PE_SetPartsCG),
	    HLL_EXPORT(GetPartsCGNumber, PE_GetPartsCGNumber),
	    HLL_EXPORT(GetPartsCGName, PE_GetPartsCGName),
	    HLL_EXPORT(SetLoopCG, PE_SetLoopCG),
	    HLL_EXPORT(SetText, PE_SetText),
	    HLL_EXPORT(AddPartsText, PE_AddPartsText),
	    HLL_TODO_EXPORT(DeletePartsTopTextLine, PE_DeletePartsTopTextLine),
	    HLL_EXPORT(SetFont, PE_SetFont),
	    HLL_EXPORT(SetPartsFontType, PE_SetPartsFontType),
	    HLL_EXPORT(SetPartsFontSize, PE_SetPartsFontSize),
	    HLL_EXPORT(SetPartsFontColor, PE_SetPartsFontColor),
	    HLL_EXPORT(SetPartsFontBoldWeight, PE_SetPartsFontBoldWeight),
	    HLL_EXPORT(SetPartsFontEdgeColor, PE_SetPartsFontEdgeColor),
	    HLL_EXPORT(SetPartsFontEdgeWeight, PE_SetPartsFontEdgeWeight),
	    HLL_EXPORT(SetTextCharSpace, PE_SetTextCharSpace),
	    HLL_EXPORT(SetTextLineSpace, PE_SetTextLineSpace),
	    HLL_EXPORT(SetHGaugeCG, PE_SetHGaugeCG),
	    HLL_EXPORT(SetHGaugeRate, PE_SetHGaugeRate_int),
	    HLL_EXPORT(SetVGaugeCG, PE_SetVGaugeCG),
	    HLL_EXPORT(SetVGaugeRate, PE_SetVGaugeRate_int),
	    HLL_TODO_EXPORT(SetNumeralCG, PE_SetNumeralCG),
	    HLL_EXPORT(SetNumeralLinkedCGNumberWidthWidthList, PE_SetNumeralLinkedCGNumberWidthWidthList),
	    HLL_EXPORT(SetNumeralNumber, PE_SetNumeralNumber),
	    HLL_EXPORT(SetNumeralShowComma, PE_SetNumeralShowComma),
	    HLL_EXPORT(SetNumeralSpace, PE_SetNumeralSpace),
	    HLL_TODO_EXPORT(SetPartsRectangleDetectionSize, PE_SetPartsRectangleDetectionSize),
	    HLL_EXPORT(SetPartsFlash, PE_SetPartsFlash),
	    HLL_EXPORT(IsPartsFlashEnd, PE_IsPartsFlashEnd),
	    HLL_EXPORT(GetPartsFlashCurrentFrameNumber, PE_GetPartsFlashCurrentFrameNumber),
	    HLL_EXPORT(BackPartsFlashBeginFrame, PE_BackPartsFlashBeginFrame),
	    HLL_EXPORT(StepPartsFlashFinalFrame, PE_StepPartsFlashFinalFrame),
	    HLL_EXPORT(ReleaseParts, PE_ReleaseParts),
	    HLL_EXPORT(ReleaseAllParts, PE_ReleaseAllParts),
	    HLL_EXPORT(ReleaseAllPartsWithoutSystem, PE_ReleaseAllPartsWithoutSystem),
	    HLL_EXPORT(SetPos, PE_SetPos),
	    HLL_EXPORT(SetZ, PE_SetZ),
	    HLL_EXPORT(SetShow, PE_SetShow),
	    HLL_EXPORT(SetAlpha, PE_SetAlpha),
	    HLL_EXPORT(SetPartsDrawFilter, PE_SetPartsDrawFilter),
	    HLL_EXPORT(SetAddColor, PE_SetAddColor),
	    HLL_EXPORT(SetMultiplyColor, PE_SetMultiplyColor),
	    HLL_EXPORT(SetClickable, PE_SetClickable),
	    HLL_EXPORT(GetPartsX, PE_GetPartsX),
	    HLL_EXPORT(GetPartsY, PE_GetPartsY),
	    HLL_EXPORT(GetPartsZ, PE_GetPartsZ),
	    HLL_EXPORT(GetPartsShow, PE_GetPartsShow),
	    HLL_EXPORT(GetPartsAlpha, PE_GetPartsAlpha),
	    HLL_EXPORT(GetAddColor, PE_GetAddColor),
	    HLL_EXPORT(GetMultiplyColor, PE_GetMultiplyColor),
	    HLL_EXPORT(GetPartsClickable, PE_GetPartsClickable),
	    HLL_EXPORT(SetPartsOriginPosMode, PE_SetPartsOriginPosMode),
	    HLL_EXPORT(GetPartsOriginPosMode, PE_GetPartsOriginPosMode),
	    HLL_EXPORT(SetParentPartsNumber, PE_SetParentPartsNumber),
	    HLL_EXPORT(SetPartsGroupNumber, PE_SetPartsGroupNumber),
	    HLL_EXPORT(SetPartsGroupDecideOnCursor, PE_SetPartsGroupDecideOnCursor),
	    HLL_EXPORT(SetPartsGroupDecideClick, PE_SetPartsGroupDecideClick),
	    HLL_EXPORT(SetOnCursorShowLinkPartsNumber, PE_SetOnCursorShowLinkPartsNumber),
	    HLL_EXPORT(SetPartsMessageWindowShowLink, PE_SetPartsMessageWindowShowLink),
	    HLL_EXPORT(GetPartsMessageWindowShowLink, PE_GetPartsMessageWindowShowLink),
	    HLL_EXPORT(SetPartsOnCursorSoundNumber, PE_SetPartsOnCursorSoundNumber),
	    HLL_EXPORT(SetPartsClickSoundNumber, PE_SetPartsClickSoundNumber),
	    HLL_EXPORT(SetClickMissSoundNumber, PE_SetClickMissSoundNumber),
	    HLL_EXPORT(BeginClick, PE_BeginInput),
	    HLL_EXPORT(EndClick, PE_EndInput),
	    HLL_EXPORT(GetClickPartsNumber, PE_GetClickPartsNumber),
	    HLL_EXPORT(AddMotionPos, PE_AddMotionPos),
	    HLL_EXPORT(AddMotionAlpha, PE_AddMotionAlpha),
	    HLL_TODO_EXPORT(AddMotionCG, PE_AddMotionCG),
	    HLL_EXPORT(AddMotionHGaugeRate, GoatGUIEngine_AddMotionHGaugeRate),
	    HLL_EXPORT(AddMotionVGaugeRate, GoatGUIEngine_AddMotionVGaugeRate),
	    HLL_EXPORT(AddMotionNumeralNumber, PE_AddMotionNumeralNumber),
	    HLL_EXPORT(AddMotionMagX, PE_AddMotionMagX),
	    HLL_EXPORT(AddMotionMagY, PE_AddMotionMagY),
	    HLL_EXPORT(AddMotionRotateX, GoatGUIEngine_AddMotionRotateX),
	    HLL_EXPORT(AddMotionRotateY, GoatGUIEngine_AddMotionRotateY),
	    HLL_EXPORT(AddMotionRotateZ, GoatGUIEngine_AddMotionRotateZ),
	    HLL_EXPORT(AddMotionVibrationSize, PE_AddMotionVibrationSize),
	    HLL_EXPORT(AddWholeMotionVibrationSize, PE_AddWholeMotionVibrationSize),
	    HLL_EXPORT(AddMotionSound, PE_AddMotionSound),
	    HLL_EXPORT(BeginMotion, PE_BeginMotion),
	    HLL_EXPORT(EndMotion, PE_EndMotion),
	    HLL_EXPORT(SetMotionTime, PE_SetMotionTime),
	    HLL_EXPORT(IsMotion, PE_IsMotion),
	    HLL_EXPORT(GetMotionEndTime, PE_GetMotionEndTime),
	    HLL_EXPORT(SetPartsMagX, PE_SetPartsMagX),
	    HLL_EXPORT(SetPartsMagY, PE_SetPartsMagY),
	    HLL_EXPORT(SetPartsRotateX, GoatGUIEngine_SetPartsRotateX),
	    HLL_EXPORT(SetPartsRotateY, GoatGUIEngine_SetPartsRotateY),
	    HLL_EXPORT(SetPartsRotateZ, GoatGUIEngine_SetPartsRotateZ),
	    HLL_TODO_EXPORT(SetPartsAlphaClipperPartsNumber, PE_SetPartsAlphaClipperPartsNumber),
	    HLL_EXPORT(SetPartsPixelDecide, PE_SetPartsPixelDecide),
	    HLL_EXPORT(SetThumbnailReductionSize, PE_SetThumbnailReductionSize),
	    HLL_EXPORT(SetThumbnailMode, PE_SetThumbnailMode),
	    HLL_EXPORT(Save, PE_Save),
	    HLL_EXPORT(Load, PE_Load),

	    HLL_EXPORT(SetPartsFlashAndStop, PE_SetPartsFlashAndStop),
	    HLL_EXPORT(StopPartsFlash, PE_StopPartsFlash),
	    HLL_EXPORT(StartPartsFlash, PE_StartPartsFlash),
	    HLL_EXPORT(GoFramePartsFlash, PE_GoFramePartsFlash),
	    HLL_EXPORT(GetPartsFlashEndFrame, PE_GetPartsFlashEndFrame),
	    HLL_TODO_EXPORT(ClearPartsConstructionProcess, PE_ClearPartsConstructionProcess),
	    HLL_EXPORT(AddCreateToPartsConstructionProcess, PE_AddCreateToPartsConstructionProcess),
	    HLL_EXPORT(AddCreatePixelOnlyToPartsConstructionProcess, PE_AddCreatePixelOnlyToPartsConstructionProcess),
	    HLL_EXPORT(AddFillToPartsConstructionProcess, PE_AddFillToPartsConstructionProcess),
	    HLL_EXPORT(AddFillAlphaColorToPartsConstructionProcess, PE_AddFillAlphaColorToPartsConstructionProcess),
	    HLL_EXPORT(AddFillAMapToPartsConstructionProcess, PE_AddFillAMapToPartsConstructionProcess),
	    HLL_TODO_EXPORT(AddFillWithAlphaToPartsConstructionProcess, PE_AddFillWithAlphaToPartsConstructionProcess),
	    HLL_TODO_EXPORT(AddFillGradationHorizonToPartsConstructionProcess, PE_AddFillGradationHorizonToPartsConstructionProcess),
	    HLL_EXPORT(AddDrawRectToPartsConstructionProcess, PE_AddDrawRectToPartsConstructionProcess),
	    HLL_EXPORT(BuildPartsConstructionProcess, PE_BuildPartsConstructionProcess),
	    HLL_EXPORT(AddDrawTextToPartsConstructionProcess, PE_AddDrawTextToPartsConstructionProcess));

static struct ain_hll_function *get_fun(int libno, const char *name)
{
	int fno = ain_get_library_function(ain, libno, name);
	return fno >= 0 ? &ain->libraries[libno].functions[fno] : NULL;
}

// Daiteikoku and Shaman's Sanctuary have different interface than later versions
static void GoatGUIEngine_PreLink(void)
{
	struct ain_hll_function *fun;
	int libno = ain_get_library(ain, "GoatGUIEngine");
	assert(libno >= 0);

	fun = get_fun(libno, "SetPartsCG");
	if (fun && game_rance8) {
		static_library_replace(&lib_GoatGUIEngine, "SetPartsCG", PE_SetPartsCG_by_string_index);
	}
	else if (fun && fun->arguments[1].type.data == AIN_INT) {
		static_library_replace(&lib_GoatGUIEngine, "SetPartsCG", PE_SetPartsCG_by_index);
	}

	fun = get_fun(libno, "SetLoopCG");
	if (fun && fun->arguments[1].type.data == AIN_INT) {
		static_library_replace(&lib_GoatGUIEngine, "SetLoopCG", PE_SetLoopCG_by_index);
	}

	fun = get_fun(libno, "SetHGaugeCG");
	if (fun && fun->arguments[1].type.data == AIN_INT) {
		static_library_replace(&lib_GoatGUIEngine, "SetHGaugeCG", PE_SetHGaugeCG_by_index);
	}

	fun = get_fun(libno, "SetVGaugeCG");
	if (fun && fun->arguments[1].type.data == AIN_INT) {
		static_library_replace(&lib_GoatGUIEngine, "SetVGaugeCG", PE_SetVGaugeCG_by_index);
	}

	fun = get_fun(libno, "SetNumeralCG");
	if (fun && fun->arguments[1].type.data == AIN_INT) {
		static_library_replace(&lib_GoatGUIEngine, "SetNumeralCG", PE_SetNumeralCG_by_index);
	}

	fun = get_fun(libno, "SetNumeralLinkedCGNumberWidthWidthList");
	if (fun && fun->arguments[1].type.data == AIN_INT) {
		static_library_replace(&lib_GoatGUIEngine, "SetNumeralLinkedCGNumberWidthWidthList",
				PE_SetNumeralLinkedCGNumberWidthWidthList_by_index);
	}

	fun = get_fun(libno, "AddMotionCG");
	if (fun && fun->arguments[1].type.data == AIN_INT) {
		static_library_replace(&lib_GoatGUIEngine, "AddMotionCG", PE_AddMotionCG_by_index);
	}

}
