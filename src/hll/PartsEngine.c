/* Copyright (C) 2023 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include "vm/page.h"
#include "parts.h"
#include "hll.h"

static void PartsEngine_ModuleInit(void)
{
	PE_Init();
}

static void PartsEngine_ModuleFini(void)
{
	PE_Reset();
}

static void PartsEngine_Update(int passed_time, bool is_skip, bool message_window_show)
{
	PE_Update(passed_time, message_window_show);
}

static void PartsEngine_Update_Pascha3PC(struct string *xxx1, struct string *xxx2, int passed_time, bool is_skip, bool message_window_show)
{
	PE_Update(passed_time, message_window_show);
}

// Oyako Rankan
static bool PartsEngine_AddDrawCutCGToPartsConstructionProcess_old(int parts_no,
		struct string *cg_name, int dx, int dy, int sx, int sy, int w, int h,
		int state)
{
	return PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name, dx, dy, w, h,
			sx, sy, w, h, 0, state);
}

// Oyako Rankan
static bool PartsEngine_AddCopyCutCGToPartsConstructionProcess_old(int parts_no,
		struct string *cg_name, int dx, int dy, int sx, int sy, int w, int h,
		int state)
{
	return PE_AddCopyCutCGToPartsConstructionProcess(parts_no, cg_name, dx, dy, w, h,
			sx, sy, w, h, 0, state);
}

// Rance 9: argument types changed from int to float
static void PartsEngine_SetComponentPos(int parts_no, float x, float y)
{
	PE_SetPos(parts_no, x, y);
}

// Rance 9: return type changed from int to float
static float PartsEngine_Parts_GetPartsUpperLeftPosX(int parts_no, int state)
{
	return PE_GetPartsUpperLeftPosX(parts_no, state);
}

static float PartsEngine_Parts_GetPartsUpperLeftPosY(int parts_no, int state)
{
	return PE_GetPartsUpperLeftPosY(parts_no, state);
}

// Rance 9: return type changed from int to float
static float PartsEngine_Parts_GetComponentPosX(int parts_no)
{
	return PE_GetPartsX(parts_no);
}

static float PartsEngine_GetComponentPosY(int parts_no)
{
	return PE_GetPartsY(parts_no);
}

// Rance 9: X/Y coordinate types changed from int to float
static void PartsEngine_AddComponentMotionPos(int parts_no, float begin_x, float begin_y,
		float end_x, float end_y, int begin_t, int end_t,
		struct string *curve_name)
{
	PE_AddMotionPos_curve(parts_no, begin_x, begin_y, end_x, end_y,
			begin_t, end_t, curve_name);
}

// Generic dispatch function for PartsEngine operations.
// func_id selects the operation; arguments and return values are passed
// through three typed arrays (int/bool, float, string).
//
// Calling convention (from game script):
//   1. Push input values into the appropriate arrays.
//   2. Push a placeholder (0 or "") for each output slot.
//   3. Call PartsFunc; outputs are written back into the placeholder slots.
static int PartsEngine_PartsFunc(int func_id, struct page **array_int,
		struct page **array_float, struct page **array_string)
{
	int nr_ints = (array_int && *array_int) ? (*array_int)->nr_vars : 0;
	union vm_value *ints = nr_ints ? (*array_int)->values : NULL;

#define REQUIRE_INTS(n) \
	if (nr_ints != (n)) VM_ERROR("Invalid arguments for PartsFunc %d: expected %d ints, got %d", func_id, (n), nr_ints)

	switch (func_id) {
	case 0:  // void SetActiveLayer(int layer)
		REQUIRE_INTS(1);
		PE_set_active_controller(ints[0].i);
		return 1;
	case 1:  // int GetActiveLayer()
		REQUIRE_INTS(1);
		ints[0].i = PE_get_active_controller();
		return 1;
	case 2:  // int GetSystemOverlayLayer()
		REQUIRE_INTS(1);
		ints[0].i = PE_get_system_controller();
		return 1;
	case 162:  // bool InitPartsMovie(int parts_no, int width, int height, int bg_r, int bg_g, int bg_b, int state)
		REQUIRE_INTS(8);
		ints[7].i = PE_init_parts_movie(ints[0].i, ints[1].i, ints[2].i, ints[3].i, ints[4].i, ints[5].i, ints[6].i);
		return 1;
	case 163:  // int GetMovieSprite(int parts_no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PE_get_movie_sprite(ints[0].i, ints[1].i);
		return 1;
	default:
		WARNING("Unknown func_id: %d", func_id);
		return 0;
	}
#undef REQUIRE_INTS
}

HLL_WARN_UNIMPLEMENTED(, void, PartsEngine, StopSoundWithoutSystemSound);
HLL_WARN_UNIMPLEMENTED(, void, PartsEngine, Parts_SetPassCursor, int Number, bool Pass);

static void PartsEngine_PreLink(void);

HLL_LIBRARY(PartsEngine,
	    HLL_EXPORT(_PreLink, PartsEngine_PreLink),
	    // for versions without PartsEngine.Init
	    HLL_EXPORT(_ModuleInit, PartsEngine_ModuleInit),
	    HLL_EXPORT(_ModuleFini, PartsEngine_ModuleFini),
	    // Oyako Rankan
	    HLL_EXPORT(Init, PE_Init),
	    HLL_EXPORT(Update, PartsEngine_Update),
	    HLL_TODO_EXPORT(UpdateGUIController, PartsEngine_UpdateGUIController),
	    HLL_EXPORT(GetFreeSystemPartsNumber, PE_GetFreeNumber),
	    // FIXME: what is the difference?
	    HLL_EXPORT(GetFreeSystemPartsNumberNotSaved, PE_GetFreeNumber),
	    HLL_EXPORT(IsExistParts, PE_IsExist),
	    HLL_EXPORT(SetPartsCG, PE_SetPartsCG),
	    HLL_EXPORT(GetPartsCGName, PE_GetPartsCGName),
	    HLL_EXPORT(SetPartsCGSurfaceArea, PE_SetPartsCGSurfaceArea),
	    HLL_EXPORT(SetLoopCG, PE_SetLoopCG),
	    HLL_EXPORT(SetLoopCGSurfaceArea, PE_SetLoopCGSurfaceArea),
	    HLL_EXPORT(SetText, PE_SetText),
	    HLL_EXPORT(AddPartsText, PE_AddPartsText),
	    HLL_TODO_EXPORT(DeletePartsTopTextLine, PartsEngine_DeletePartsTopTextLine),
	    HLL_EXPORT(SetPartsTextSurfaceArea, PE_SetPartsTextSurfaceArea),
	    HLL_TODO_EXPORT(SetPartsTextHighlight, PartsEngine_SetPartsTextHighlight),
	    HLL_TODO_EXPORT(AddPartsTextHighlight, PartsEngine_AddPartsTextHighlight),
	    HLL_TODO_EXPORT(ClearPartsTextHighlight, PartsEngine_ClearPartsTextHighlight),
	    HLL_TODO_EXPORT(SetPartsTextCountReturn, PartsEngine_SetPartsTextCountReturn),
	    HLL_TODO_EXPORT(GetPartsTextCountReturn, PartsEngine_GetPartsTextCountReturn),
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
	    HLL_EXPORT(SetHGaugeSurfaceArea, PE_SetHGaugeSurfaceArea),
	    HLL_EXPORT(SetVGaugeSurfaceArea, PE_SetVGaugeSurfaceArea),
	    HLL_EXPORT(SetNumeralCG, PE_SetNumeralCG),
	    HLL_EXPORT(SetNumeralLinkedCGNumberWidthWidthList, PE_SetNumeralLinkedCGNumberWidthWidthList),
	    HLL_TODO_EXPORT(SetNumeralFont, PartsEngine_SetNumeralFont),
	    HLL_EXPORT(SetNumeralNumber, PE_SetNumeralNumber),
	    HLL_EXPORT(SetNumeralShowComma, PE_SetNumeralShowComma),
	    HLL_EXPORT(SetNumeralSpace, PE_SetNumeralSpace),
	    HLL_EXPORT(SetNumeralLength, PE_SetNumeralLength),
	    HLL_EXPORT(SetNumeralSurfaceArea, PE_SetNumeralSurfaceArea),
	    HLL_EXPORT(SetPartsRectangleDetectionSize, PE_SetPartsRectangleDetectionSize),
	    HLL_TODO_EXPORT(SetPartsRectangleDetectionSurfaceArea, PartsEngine_SetPartsRectangleDetectionSurfaceArea),
	    HLL_EXPORT(SetPartsCGDetectionSize, PE_SetPartsCGDetectionSize),
	    HLL_TODO_EXPORT(SetPartsCGDetectionSurfaceArea, PartsEngine_SetPartsCGDetectionSurfaceArea),
	    HLL_EXPORT(SetPartsFlash, PE_SetPartsFlash),
	    HLL_EXPORT(IsPartsFlashEnd, PE_IsPartsFlashEnd),
	    HLL_EXPORT(GetPartsFlashCurrentFrameNumber, PE_GetPartsFlashCurrentFrameNumber),
	    HLL_EXPORT(BackPartsFlashBeginFrame, PE_BackPartsFlashBeginFrame),
	    HLL_EXPORT(StepPartsFlashFinalFrame, PE_StepPartsFlashFinalFrame),
	    HLL_TODO_EXPORT(SetPartsFlashSurfaceArea, PE_SetPartsFlashSurfaceArea),
	    HLL_EXPORT(SetPartsFlashAndStop, PE_SetPartsFlashAndStop),
	    HLL_EXPORT(StopPartsFlash, PE_StopPartsFlash),
	    HLL_EXPORT(StartPartsFlash, PE_StartPartsFlash),
	    HLL_EXPORT(GoFramePartsFlash, PE_GoFramePartsFlash),
	    HLL_EXPORT(GetPartsFlashEndFrame, PE_GetPartsFlashEndFrame),
	    HLL_EXPORT(ExistsFlashFile, PE_ExistsFlashFile),
	    HLL_EXPORT(ClearPartsConstructionProcess, PE_ClearPartsConstructionProcess),
	    HLL_EXPORT(AddCreateToPartsConstructionProcess, PE_AddCreateToPartsConstructionProcess),
	    HLL_EXPORT(AddCreatePixelOnlyToPartsConstructionProcess, PE_AddCreatePixelOnlyToPartsConstructionProcess),
	    HLL_EXPORT(AddCreateCGToProcess, PE_AddCreateCGToProcess),
	    HLL_EXPORT(AddFillToPartsConstructionProcess, PE_AddFillToPartsConstructionProcess),
	    HLL_EXPORT(AddFillAlphaColorToPartsConstructionProcess, PE_AddFillAlphaColorToPartsConstructionProcess),
	    HLL_EXPORT(AddFillAMapToPartsConstructionProcess, PE_AddFillAMapToPartsConstructionProcess),
	    HLL_EXPORT(AddFillWithAlphaToPartsConstructionProcess, PE_AddFillWithAlphaToPartsConstructionProcess),
	    HLL_TODO_EXPORT(AddFillGradationHorizonToPartsConstructionProcess, PartsEngine_AddFillGradationHorizonToPartsConstructionProcess),
	    HLL_EXPORT(AddDrawRectToPartsConstructionProcess, PE_AddDrawRectToPartsConstructionProcess),
	    HLL_EXPORT(AddDrawCutCGToPartsConstructionProcess, PartsEngine_AddDrawCutCGToPartsConstructionProcess_old),
	    HLL_EXPORT(AddCopyCutCGToPartsConstructionProcess, PartsEngine_AddCopyCutCGToPartsConstructionProcess_old),
	    HLL_EXPORT(AddGrayFilterToPartsConstructionProcess, PE_AddGrayFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(AddAddFilterToPartsConstructionProcess, PartsEngine_AddAddFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(AddMulFilterToPartsConstructionProcess, PartsEngine_AddMulFilterToPartsConstructionProcess),
	    HLL_EXPORT(BuildPartsConstructionProcess, PE_BuildPartsConstructionProcess),
	    HLL_EXPORT(AddDrawTextToPartsConstructionProcess, PE_AddDrawTextToPartsConstructionProcess),
	    HLL_EXPORT(AddCopyTextToPartsConstructionProcess, PE_AddCopyTextToPartsConstructionProcess),
	    HLL_EXPORT(SetPartsConstructionSurfaceArea, PE_SetPartsConstructionSurfaceArea),
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
	    HLL_EXPORT(SetSpeedupRateByMessageSkip, PE_SetSpeedupRateByMessageSkip),
	    HLL_TODO_EXPORT(SetResetTimerByChangeInputStatus, PartsEngine_SetResetTimerByChangeInputStatus),
	    HLL_EXPORT(GetPartsX, PE_GetPartsX),
	    HLL_EXPORT(GetPartsY, PE_GetPartsY),
	    HLL_EXPORT(GetPartsZ, PE_GetPartsZ),
	    HLL_EXPORT(GetPartsShow, PE_GetPartsShow),
	    HLL_EXPORT(GetPartsAlpha, PE_GetPartsAlpha),
	    HLL_TODO_EXPORT(GetAddColor, PartsEngine_GetAddColor),
	    HLL_TODO_EXPORT(GetMultiplyColor, PartsEngine_GetMultiplyColor),
	    HLL_EXPORT(GetPartsClickable, PE_GetPartsClickable),
	    HLL_TODO_EXPORT(GetPartsSpeedupRateByMessageSkip, PartsEngine_GetPartsSpeedupRateByMessageSkip),
	    HLL_TODO_EXPORT(GetResetTimerByChangeInputStatus, PartsEngine_GetResetTimerByChangeInputStatus),
	    HLL_EXPORT(GetPartsUpperLeftPosX, PE_GetPartsUpperLeftPosX),
	    HLL_EXPORT(GetPartsUpperLeftPosY, PE_GetPartsUpperLeftPosY),
	    HLL_EXPORT(GetPartsWidth, PE_GetPartsWidth),
	    HLL_EXPORT(GetPartsHeight, PE_GetPartsHeight),
	    HLL_EXPORT(SetInputState, PE_SetInputState),
	    HLL_EXPORT(GetInputState, PE_GetInputState),
	    HLL_EXPORT(SetPartsOriginPosMode, PE_SetPartsOriginPosMode),
	    HLL_EXPORT(GetPartsOriginPosMode, PE_GetPartsOriginPosMode),
	    HLL_EXPORT(SetParentPartsNumber, PE_SetParentPartsNumber),
	    HLL_EXPORT(SetPartsGroupNumber, PE_SetPartsGroupNumber),
	    HLL_EXPORT(SetPartsGroupDecideOnCursor, PE_SetPartsGroupDecideOnCursor),
	    HLL_EXPORT(SetPartsGroupDecideClick, PE_SetPartsGroupDecideClick),
	    HLL_EXPORT(SetOnCursorShowLinkPartsNumber, PE_SetOnCursorShowLinkPartsNumber),
	    HLL_EXPORT(SetPartsMessageWindowShowLink, PE_SetPartsMessageWindowShowLink),
	    HLL_EXPORT(GetPartsMessageWindowShowLink, PE_GetPartsMessageWindowShowLink),
	    HLL_EXPORT(AddMotionPos, PE_AddMotionPos_curve),
	    HLL_EXPORT(AddMotionAlpha, PE_AddMotionAlpha_curve),
	    HLL_TODO_EXPORT(AddMotionCG, PartsEngine_AddMotionCG),
	    HLL_EXPORT(AddMotionHGaugeRate, PE_AddMotionHGaugeRate_curve),
	    HLL_EXPORT(AddMotionVGaugeRate, PE_AddMotionVGaugeRate_curve),
	    HLL_EXPORT(AddMotionNumeralNumber, PE_AddMotionNumeralNumber_curve),
	    HLL_EXPORT(AddMotionMagX, PE_AddMotionMagX_curve),
	    HLL_EXPORT(AddMotionMagY, PE_AddMotionMagY_curve),
	    HLL_EXPORT(AddMotionRotateX, PE_AddMotionRotateX_curve),
	    HLL_EXPORT(AddMotionRotateY, PE_AddMotionRotateY_curve),
	    HLL_EXPORT(AddMotionRotateZ, PE_AddMotionRotateZ_curve),
	    HLL_EXPORT(AddMotionVibrationSize, PE_AddMotionVibrationSize),
	    HLL_EXPORT(AddWholeMotionVibrationSize, PE_AddWholeMotionVibrationSize),
	    HLL_EXPORT(AddMotionSound, PE_AddMotionSound),
	    HLL_TODO_EXPORT(SetSoundNumber, PartsEngine_SetSoundNumber),
	    HLL_TODO_EXPORT(GetSoundNumber, PartsEngine_GetSoundNumber),
	    HLL_EXPORT(SetClickMissSoundNumber, PE_SetClickMissSoundNumber),
	    HLL_TODO_EXPORT(GetClickMissSoundNumber, PartsEngine_GetClickMissSoundNumber),
	    HLL_EXPORT(BeginMotion, PE_BeginMotion),
	    HLL_EXPORT(EndMotion, PE_EndMotion),
	    HLL_EXPORT(IsMotion, PE_IsMotion),
	    HLL_EXPORT(SeekEndMotion, PE_SeekEndMotion),
	    HLL_EXPORT(UpdateMotionTime, PE_UpdateMotionTime),
	    HLL_EXPORT(BeginInput, PE_BeginInput),
	    HLL_EXPORT(EndInput, PE_EndInput),
	    HLL_EXPORT(GetClickPartsNumber, PE_GetClickPartsNumber),
	    HLL_TODO_EXPORT(GetFocusPartsNumber, PartsEngine_GetFocusPartsNumber),
	    HLL_TODO_EXPORT(SetFocusPartsNumber, PartsEngine_SetFocusPartsNumber),
	    HLL_TODO_EXPORT(PushGUIController, PartsEngine_PushGUIController),
	    HLL_TODO_EXPORT(PopGUIController, PartsEngine_PopGUIController),
	    HLL_EXPORT(SetPartsMagX, PE_SetPartsMagX),
	    HLL_EXPORT(SetPartsMagY, PE_SetPartsMagY),
	    HLL_EXPORT(SetPartsRotateX, PE_SetPartsRotateX),
	    HLL_EXPORT(SetPartsRotateY, PE_SetPartsRotateY),
	    HLL_EXPORT(SetPartsRotateZ, PE_SetPartsRotateZ),
	    HLL_EXPORT(SetPartsAlphaClipperPartsNumber, PE_SetPartsAlphaClipperPartsNumber),
	    HLL_EXPORT(SetPartsPixelDecide, PE_SetPartsPixelDecide),
	    HLL_EXPORT(IsCursorIn, PE_IsCursorIn),
	    HLL_EXPORT(SetThumbnailReductionSize, PE_SetThumbnailReductionSize),
	    HLL_EXPORT(SetThumbnailMode, PE_SetThumbnailMode),
	    HLL_EXPORT(Save, PE_Save),
	    HLL_EXPORT(SaveWithoutHideParts, PE_SaveWithoutHideParts),
	    HLL_EXPORT(Load, PE_Load),
	    // Rance 9
	    HLL_EXPORT(PartsFunc, PartsEngine_PartsFunc),
	    HLL_EXPORT(Release, PE_ReleaseParts),
	    HLL_TODO_EXPORT(ReleaseAll, PartsEngine_ReleaseAll),
	    HLL_EXPORT(ReleaseAllWithoutSystem, PE_ReleaseAllWithoutSystem),
	    HLL_EXPORT(GetFreeNumber, PE_GetFreeNumber),
	    HLL_EXPORT(IsExist, PE_IsExist),
	    HLL_EXPORT(AddController, PE_AddController),
	    HLL_EXPORT(RemoveController, PE_RemoveController),
	    HLL_EXPORT(UpdateComponent, PartsEngine_Update),
	    HLL_EXPORT(Parts_SetThumbnailReductionSize, PE_SetThumbnailReductionSize),
	    HLL_EXPORT(Parts_SetThumbnailMode, PE_SetThumbnailMode),
	    HLL_EXPORT(GetClickNumber, PE_GetClickPartsNumber),
	    HLL_EXPORT(StopSoundWithoutSystemSound, PartsEngine_StopSoundWithoutSystemSound),
	    HLL_TODO_EXPORT(ReleaseActivity, PartsEngine_ReleaseActivity),
	    HLL_TODO_EXPORT(CrateActivityBinary, PartsEngine_CrateActivityBinary),
	    HLL_TODO_EXPORT(ReadActivityBinary, PartsEngine_ReadActivityBinary),
	    HLL_EXPORT(ReleaseMessage, PE_ReleaseMessage),
	    HLL_EXPORT(PopMessage, PE_PopMessage),
	    HLL_EXPORT(GetMessagePartsNumber, PE_GetMessagePartsNumber),
	    HLL_EXPORT(GetMessageDelegateIndex, PE_GetMessageDelegateIndex),
	    HLL_EXPORT(GetDelegateIndex, PE_GetDelegateIndex),
	    HLL_EXPORT(GetMessageType, PE_GetMessageType),
	    HLL_EXPORT(GetMessageVariableCount, PE_GetMessageVariableCount),
	    HLL_EXPORT(GetMessageVariableType, PE_GetMessageVariableType),
	    HLL_EXPORT(GetMessageVariableInt, PE_GetMessageVariableInt),
	    HLL_EXPORT(GetMessageVariableFloat, PE_GetMessageVariableFloat),
	    HLL_EXPORT(GetMessageVariableBool, PE_GetMessageVariableBool),
	    HLL_EXPORT(GetMessageVariableString, PE_GetMessageVariableString),
	    HLL_EXPORT(SetDelegateIndex, PE_SetDelegateIndex),
	    HLL_TODO_EXPORT(SetFocus, PartsEngine_SetFocus),
	    HLL_TODO_EXPORT(IsFocus, PartsEngine_IsFocus),
	    HLL_EXPORT(SetComponentType, PE_SetComponentType),
	    HLL_EXPORT(GetComponentType, PE_GetComponentType),
	    HLL_EXPORT(SetComponentPos, PartsEngine_SetComponentPos),
	    HLL_EXPORT(SetComponentPosZ, PE_SetZ),
	    HLL_EXPORT(GetComponentPosX, PartsEngine_Parts_GetComponentPosX),
	    HLL_EXPORT(GetComponentPosY, PartsEngine_GetComponentPosY),
	    HLL_EXPORT(GetComponentPosZ, PE_GetPartsZ),
	    HLL_EXPORT(Parts_GetPartsUpperLeftPosX, PartsEngine_Parts_GetPartsUpperLeftPosX),
	    HLL_EXPORT(Parts_GetPartsUpperLeftPosY, PartsEngine_Parts_GetPartsUpperLeftPosY),
	    HLL_EXPORT(SetComponentOriginPosMode, PE_SetPartsOriginPosMode),
	    HLL_EXPORT(GetComponentOriginPosMode, PE_GetPartsOriginPosMode),
	    HLL_TODO_EXPORT(GetComponentWidth, PartsEngine_GetComponentWidth),
	    HLL_TODO_EXPORT(GetComponentHeight, PartsEngine_GetComponentHeight),
	    HLL_EXPORT(Parts_GetPartsWidth, PE_GetPartsWidth),
	    HLL_EXPORT(Parts_GetPartsHeight, PE_GetPartsHeight),
	    HLL_EXPORT(SetComponentShow, PE_SetShow),
	    HLL_EXPORT(IsComponentShow, PE_GetPartsShow),
	    HLL_EXPORT(SetComponentMessageWindowShowLink, PE_SetPartsMessageWindowShowLink),
	    HLL_EXPORT(IsComponentMessageWindowShowLink, PE_GetPartsMessageWindowShowLink),
	    HLL_EXPORT(SetComponentAlpha, PE_SetAlpha),
	    HLL_EXPORT(GetComponentAlpha, PE_GetPartsAlpha),
	    HLL_EXPORT(SetComponentAddColor, PE_SetAddColor),
	    HLL_TODO_EXPORT(GetComponentAddColorR, PartsEngine_GetComponentAddColorR),
	    HLL_TODO_EXPORT(GetComponentAddColorG, PartsEngine_GetComponentAddColorG),
	    HLL_TODO_EXPORT(GetComponentAddColorB, PartsEngine_GetComponentAddColorB),
	    HLL_EXPORT(SetComponentMulColor, PE_SetMultiplyColor),
	    HLL_TODO_EXPORT(GetComponentMulColorR, PartsEngine_GetComponentMulColorR),
	    HLL_TODO_EXPORT(GetComponentMulColorG, PartsEngine_GetComponentMulColorG),
	    HLL_TODO_EXPORT(GetComponentMulColorB, PartsEngine_GetComponentMulColorB),
	    HLL_EXPORT(SetComponentDrawFilter, PE_SetPartsDrawFilter),
	    HLL_TODO_EXPORT(GetComponentDrawFilter, PartsEngine_GetComponentDrawFilter),
	    HLL_EXPORT(SetComponentMagX, PE_SetPartsMagX),
	    HLL_EXPORT(SetComponentMagY, PE_SetPartsMagY),
	    HLL_TODO_EXPORT(GetComponentMagX, PartsEngine_GetComponentMagX),
	    HLL_TODO_EXPORT(GetComponentMagY, PartsEngine_GetComponentMagY),
	    HLL_EXPORT(SetComponentRotateX, PE_SetPartsRotateX),
	    HLL_EXPORT(SetComponentRotateY, PE_SetPartsRotateY),
	    HLL_EXPORT(SetComponentRotateZ, PE_SetPartsRotateZ),
	    HLL_TODO_EXPORT(GetComponentRotateX, PartsEngine_GetComponentRotateX),
	    HLL_TODO_EXPORT(GetComponentRotateY, PartsEngine_GetComponentRotateY),
	    HLL_EXPORT(GetComponentRotateZ, PE_GetPartsRotateZ),
	    HLL_TODO_EXPORT(SetComponentMargin, PartsEngine_SetComponentMargin),
	    HLL_TODO_EXPORT(GetComponentMarginTop, PartsEngine_GetComponentMarginTop),
	    HLL_TODO_EXPORT(GetComponentMarginBottom, PartsEngine_GetComponentMarginBottom),
	    HLL_TODO_EXPORT(GetComponentMarginLeft, PartsEngine_GetComponentMarginLeft),
	    HLL_TODO_EXPORT(GetComponentMarginRight, PartsEngine_GetComponentMarginRight),
	    HLL_EXPORT(SetComponentAlphaClipper, PE_SetPartsAlphaClipperPartsNumber),
	    HLL_TODO_EXPORT(GetComponentAlphaClipper, PartsEngine_GetComponentAlphaClipper),
	    HLL_TODO_EXPORT(SetComponentTextureFilterType, PartsEngine_SetComponentTextureFilterType),
	    HLL_TODO_EXPORT(GetComponentTextureFilterType, PartsEngine_GetComponentTextureFilterType),
	    HLL_TODO_EXPORT(SetComponentMipmap, PartsEngine_SetComponentMipmap),
	    HLL_TODO_EXPORT(IsComponentMipmap, PartsEngine_IsComponentMipmap),
	    HLL_EXPORT(SetComponentSpeedupRateByMessageSkip, PE_SetSpeedupRateByMessageSkip),
	    HLL_TODO_EXPORT(GetComponentSpeedupRateByMessageSkip, PartsEngine_GetComponentSpeedupRateByMessageSkip),
	    HLL_EXPORT(AddComponentMotionPos, PartsEngine_AddComponentMotionPos),
	    HLL_EXPORT(AddComponentMotionAlpha, PE_AddMotionAlpha_curve),
	    HLL_TODO_EXPORT(AddComponentMotionCG, PartsEngine_AddComponentMotionCG),
	    HLL_TODO_EXPORT(AddComponentMotionCGTermination, PartsEngine_AddComponentMotionCGTermination),
	    HLL_EXPORT(AddComponentMotionHGaugeRate, PE_AddMotionHGaugeRate_curve),
	    HLL_EXPORT(AddComponentMotionVGaugeRate, PE_AddMotionVGaugeRate_curve),
	    HLL_EXPORT(AddComponentMotionNumeralNumber, PE_AddMotionNumeralNumber_curve),
	    HLL_EXPORT(AddComponentMotionMagX, PE_AddMotionMagX_curve),
	    HLL_EXPORT(AddComponentMotionMagY, PE_AddMotionMagY_curve),
	    HLL_EXPORT(AddComponentMotionRotateX, PE_AddMotionRotateX_curve),
	    HLL_EXPORT(AddComponentMotionRotateY, PE_AddMotionRotateY_curve),
	    HLL_EXPORT(AddComponentMotionRotateZ, PE_AddMotionRotateZ_curve),
	    HLL_EXPORT(AddComponentMotionVibrationSize, PE_AddMotionVibrationSize),
	    HLL_TODO_EXPORT(SuspendBuildView, PartsEngine_SuspendBuildView),
	    HLL_TODO_EXPORT(SuspendBuildViewAt, PartsEngine_SuspendBuildViewAt),
	    HLL_TODO_EXPORT(ResumeBuildView, PartsEngine_ResumeBuildView),
	    HLL_TODO_EXPORT(SetButtonSize, PartsEngine_SetButtonSize),
	    HLL_TODO_EXPORT(SetButtonDrag, PartsEngine_SetButtonDrag),
	    HLL_TODO_EXPORT(IsButtonDrag, PartsEngine_IsButtonDrag),
	    HLL_TODO_EXPORT(SetButtonEnable, PartsEngine_SetButtonEnable),
	    HLL_TODO_EXPORT(IsButtonEnable, PartsEngine_IsButtonEnable),
	    HLL_TODO_EXPORT(SetButtonPixelDecide, PartsEngine_SetButtonPixelDecide),
	    HLL_TODO_EXPORT(IsButtonPixelDecide, PartsEngine_IsButtonPixelDecide),
	    HLL_TODO_EXPORT(SetButtonColor, PartsEngine_SetButtonColor),
	    HLL_TODO_EXPORT(GetButtonR, PartsEngine_GetButtonR),
	    HLL_TODO_EXPORT(GetButtonG, PartsEngine_GetButtonG),
	    HLL_TODO_EXPORT(GetButtonB, PartsEngine_GetButtonB),
	    HLL_TODO_EXPORT(SetButtonFontProperty, PartsEngine_SetButtonFontProperty),
	    HLL_TODO_EXPORT(GetButtonFontProperty, PartsEngine_GetButtonFontProperty),
	    HLL_TODO_EXPORT(SetButtonOnCursorSoundNumber, PartsEngine_SetButtonOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetButtonClickSoundNumber, PartsEngine_SetButtonClickSoundNumber),
	    HLL_TODO_EXPORT(GetButtonOnCursorSoundNumber, PartsEngine_GetButtonOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetButtonClickSoundNumber, PartsEngine_GetButtonClickSoundNumber),
	    HLL_TODO_EXPORT(SetButtonCGName, PartsEngine_SetButtonCGName),
	    HLL_TODO_EXPORT(GetButtonCGName, PartsEngine_GetButtonCGName),
	    HLL_TODO_EXPORT(SetButtonText, PartsEngine_SetButtonText),
	    HLL_TODO_EXPORT(GetButtonText, PartsEngine_GetButtonText),
	    HLL_TODO_EXPORT(SetCheckBoxSize, PartsEngine_SetCheckBoxSize),
	    HLL_TODO_EXPORT(SetCheckBoxDrag, PartsEngine_SetCheckBoxDrag),
	    HLL_TODO_EXPORT(IsCheckBoxDrag, PartsEngine_IsCheckBoxDrag),
	    HLL_TODO_EXPORT(CheckBoxChecked, PartsEngine_CheckBoxChecked),
	    HLL_TODO_EXPORT(IsCheckBoxChecked, PartsEngine_IsCheckBoxChecked),
	    HLL_TODO_EXPORT(SetCheckBoxColor, PartsEngine_SetCheckBoxColor),
	    HLL_TODO_EXPORT(GetCheckBoxR, PartsEngine_GetCheckBoxR),
	    HLL_TODO_EXPORT(GetCheckBoxG, PartsEngine_GetCheckBoxG),
	    HLL_TODO_EXPORT(GetCheckBoxB, PartsEngine_GetCheckBoxB),
	    HLL_TODO_EXPORT(SetCheckBoxFontProperty, PartsEngine_SetCheckBoxFontProperty),
	    HLL_TODO_EXPORT(GetCheckBoxFontProperty, PartsEngine_GetCheckBoxFontProperty),
	    HLL_TODO_EXPORT(SetCheckBoxOnCursorSoundNumber, PartsEngine_SetCheckBoxOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetCheckBoxClickSoundNumber, PartsEngine_SetCheckBoxClickSoundNumber),
	    HLL_TODO_EXPORT(GetCheckBoxOnCursorSoundNumber, PartsEngine_GetCheckBoxOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetCheckBoxClickSoundNumber, PartsEngine_GetCheckBoxClickSoundNumber),
	    HLL_TODO_EXPORT(SetCheckBoxCGName, PartsEngine_SetCheckBoxCGName),
	    HLL_TODO_EXPORT(GetCheckBoxCGName, PartsEngine_GetCheckBoxCGName),
	    HLL_TODO_EXPORT(SetCheckBoxText, PartsEngine_SetCheckBoxText),
	    HLL_TODO_EXPORT(GetCheckBoxText, PartsEngine_GetCheckBoxText),
	    HLL_TODO_EXPORT(SetVScrollbarOnCursorSoundNumber, PartsEngine_SetVScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetVScrollbarClickSoundNumber, PartsEngine_SetVScrollbarClickSoundNumber),
	    HLL_TODO_EXPORT(GetVScrollbarOnCursorSoundNumber, PartsEngine_GetVScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetVScrollbarClickSoundNumber, PartsEngine_GetVScrollbarClickSoundNumber),
	    HLL_TODO_EXPORT(SetVScrollbarSize, PartsEngine_SetVScrollbarSize),
	    HLL_TODO_EXPORT(SetVScrollbarUpHeight, PartsEngine_SetVScrollbarUpHeight),
	    HLL_TODO_EXPORT(SetVScrollbarDownHeight, PartsEngine_SetVScrollbarDownHeight),
	    HLL_TODO_EXPORT(GetVScrollbarUpHeight, PartsEngine_GetVScrollbarUpHeight),
	    HLL_TODO_EXPORT(GetVScrollbarDownHeight, PartsEngine_GetVScrollbarDownHeight),
	    HLL_TODO_EXPORT(SetVScrollbarTotalSize, PartsEngine_SetVScrollbarTotalSize),
	    HLL_TODO_EXPORT(SetVScrollbarViewSize, PartsEngine_SetVScrollbarViewSize),
	    HLL_TODO_EXPORT(SetVScrollbarScrollPos, PartsEngine_SetVScrollbarScrollPos),
	    HLL_TODO_EXPORT(SetVScrollbarScrollRate, PartsEngine_SetVScrollbarScrollRate),
	    HLL_TODO_EXPORT(SetVScrollbarMoveSizeByButton, PartsEngine_SetVScrollbarMoveSizeByButton),
	    HLL_TODO_EXPORT(GetVScrollbarTotalSize, PartsEngine_GetVScrollbarTotalSize),
	    HLL_TODO_EXPORT(GetVScrollbarViewSize, PartsEngine_GetVScrollbarViewSize),
	    HLL_TODO_EXPORT(GetVScrollbarScrollPos, PartsEngine_GetVScrollbarScrollPos),
	    HLL_TODO_EXPORT(GetVScrollbarScrollRate, PartsEngine_GetVScrollbarScrollRate),
	    HLL_TODO_EXPORT(GetVScrollbarMoveSizeByButton, PartsEngine_GetVScrollbarMoveSizeByButton),
	    HLL_TODO_EXPORT(SetVScrollbarCGName, PartsEngine_SetVScrollbarCGName),
	    HLL_TODO_EXPORT(GetVScrollbarCGName, PartsEngine_GetVScrollbarCGName),
	    HLL_TODO_EXPORT(SetHScrollbarOnCursorSoundNumber, PartsEngine_SetHScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetHScrollbarClickSoundNumber, PartsEngine_SetHScrollbarClickSoundNumber),
	    HLL_TODO_EXPORT(GetHScrollbarOnCursorSoundNumber, PartsEngine_GetHScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetHScrollbarClickSoundNumber, PartsEngine_GetHScrollbarClickSoundNumber),
	    HLL_TODO_EXPORT(SetHScrollbarSize, PartsEngine_SetHScrollbarSize),
	    HLL_TODO_EXPORT(SetHScrollbarLeftWidth, PartsEngine_SetHScrollbarLeftWidth),
	    HLL_TODO_EXPORT(SetHScrollbarRightWidth, PartsEngine_SetHScrollbarRightWidth),
	    HLL_TODO_EXPORT(GetHScrollbarLeftWidth, PartsEngine_GetHScrollbarLeftWidth),
	    HLL_TODO_EXPORT(GetHScrollbarRightWidth, PartsEngine_GetHScrollbarRightWidth),
	    HLL_TODO_EXPORT(SetHScrollbarTotalSize, PartsEngine_SetHScrollbarTotalSize),
	    HLL_TODO_EXPORT(SetHScrollbarViewSize, PartsEngine_SetHScrollbarViewSize),
	    HLL_TODO_EXPORT(SetHScrollbarScrollPos, PartsEngine_SetHScrollbarScrollPos),
	    HLL_TODO_EXPORT(SetHScrollbarScrollRate, PartsEngine_SetHScrollbarScrollRate),
	    HLL_TODO_EXPORT(SetHScrollbarMoveSizeByButton, PartsEngine_SetHScrollbarMoveSizeByButton),
	    HLL_TODO_EXPORT(GetHScrollbarTotalSize, PartsEngine_GetHScrollbarTotalSize),
	    HLL_TODO_EXPORT(GetHScrollbarViewSize, PartsEngine_GetHScrollbarViewSize),
	    HLL_TODO_EXPORT(GetHScrollbarScrollPos, PartsEngine_GetHScrollbarScrollPos),
	    HLL_TODO_EXPORT(GetHScrollbarScrollRate, PartsEngine_GetHScrollbarScrollRate),
	    HLL_TODO_EXPORT(GetHScrollbarMoveSizeByButton, PartsEngine_GetHScrollbarMoveSizeByButton),
	    HLL_TODO_EXPORT(SetHScrollbarCGName, PartsEngine_SetHScrollbarCGName),
	    HLL_TODO_EXPORT(GetHScrollbarCGName, PartsEngine_GetHScrollbarCGName),
	    HLL_TODO_EXPORT(SetTextBoxSize, PartsEngine_SetTextBoxSize),
	    HLL_TODO_EXPORT(SetTextBoxFontProperty, PartsEngine_SetTextBoxFontProperty),
	    HLL_TODO_EXPORT(GetTextBoxFontProperty, PartsEngine_GetTextBoxFontProperty),
	    HLL_TODO_EXPORT(SetTextBoxText, PartsEngine_SetTextBoxText),
	    HLL_TODO_EXPORT(GetTextBoxText, PartsEngine_GetTextBoxText),
	    HLL_TODO_EXPORT(SetTextBoxMaxTextLength, PartsEngine_SetTextBoxMaxTextLength),
	    HLL_TODO_EXPORT(GetTextBoxMaxTextLength, PartsEngine_GetTextBoxMaxTextLength),
	    HLL_TODO_EXPORT(SetTextBoxSelectColor, PartsEngine_SetTextBoxSelectColor),
	    HLL_TODO_EXPORT(GetTextBoxSelectR, PartsEngine_GetTextBoxSelectR),
	    HLL_TODO_EXPORT(GetTextBoxSelectG, PartsEngine_GetTextBoxSelectG),
	    HLL_TODO_EXPORT(GetTextBoxSelectB, PartsEngine_GetTextBoxSelectB),
	    HLL_TODO_EXPORT(SetTextBoxCGName, PartsEngine_SetTextBoxCGName),
	    HLL_TODO_EXPORT(GetTextBoxCGName, PartsEngine_GetTextBoxCGName),
	    HLL_TODO_EXPORT(OpenTextBoxIME, PartsEngine_OpenTextBoxIME),
	    HLL_TODO_EXPORT(CloseTextBoxIME, PartsEngine_CloseTextBoxIME),
	    HLL_TODO_EXPORT(SetListBoxSize, PartsEngine_SetListBoxSize),
	    HLL_TODO_EXPORT(SetListBoxLineHeight, PartsEngine_SetListBoxLineHeight),
	    HLL_TODO_EXPORT(GetListBoxLineHeight, PartsEngine_GetListBoxLineHeight),
	    HLL_TODO_EXPORT(SetListBoxMargin, PartsEngine_SetListBoxMargin),
	    HLL_TODO_EXPORT(GetListBoxWidthMargin, PartsEngine_GetListBoxWidthMargin),
	    HLL_TODO_EXPORT(GetListBoxHeightMargin, PartsEngine_GetListBoxHeightMargin),
	    HLL_TODO_EXPORT(SetListBoxCGName, PartsEngine_SetListBoxCGName),
	    HLL_TODO_EXPORT(GetListBoxCGName, PartsEngine_GetListBoxCGName),
	    HLL_TODO_EXPORT(SetListBoxScrollPos, PartsEngine_SetListBoxScrollPos),
	    HLL_TODO_EXPORT(GetListBoxScrollPos, PartsEngine_GetListBoxScrollPos),
	    HLL_TODO_EXPORT(AddListBoxItem, PartsEngine_AddListBoxItem),
	    HLL_TODO_EXPORT(InsertListBoxItem, PartsEngine_InsertListBoxItem),
	    HLL_TODO_EXPORT(SetListBoxItem, PartsEngine_SetListBoxItem),
	    HLL_TODO_EXPORT(GetListBoxItemCount, PartsEngine_GetListBoxItemCount),
	    HLL_TODO_EXPORT(GetListBoxItem, PartsEngine_GetListBoxItem),
	    HLL_TODO_EXPORT(EraseListBoxItem, PartsEngine_EraseListBoxItem),
	    HLL_TODO_EXPORT(ClearListBoxItem, PartsEngine_ClearListBoxItem),
	    HLL_TODO_EXPORT(GetListBoxOnCursorItemIndex, PartsEngine_GetListBoxOnCursorItemIndex),
	    HLL_TODO_EXPORT(GetListBoxOnCursorItem, PartsEngine_GetListBoxOnCursorItem),
	    HLL_TODO_EXPORT(SetListBoxFontProperty, PartsEngine_SetListBoxFontProperty),
	    HLL_TODO_EXPORT(GetListBoxFontProperty, PartsEngine_GetListBoxFontProperty),
	    HLL_TODO_EXPORT(SetListBoxSelectIndex, PartsEngine_SetListBoxSelectIndex),
	    HLL_TODO_EXPORT(GetListBoxSelectIndex, PartsEngine_GetListBoxSelectIndex),
	    HLL_TODO_EXPORT(SetComboBoxSize, PartsEngine_SetComboBoxSize),
	    HLL_TODO_EXPORT(SetComboBoxTextMargin, PartsEngine_SetComboBoxTextMargin),
	    HLL_TODO_EXPORT(GetComboBoxTextWidthMargin, PartsEngine_GetComboBoxTextWidthMargin),
	    HLL_TODO_EXPORT(GetComboBoxTextHeightMargin, PartsEngine_GetComboBoxTextHeightMargin),
	    HLL_TODO_EXPORT(SetComboBoxCGName, PartsEngine_SetComboBoxCGName),
	    HLL_TODO_EXPORT(GetComboBoxCGName, PartsEngine_GetComboBoxCGName),
	    HLL_TODO_EXPORT(SetComboBoxText, PartsEngine_SetComboBoxText),
	    HLL_TODO_EXPORT(GetComboBoxText, PartsEngine_GetComboBoxText),
	    HLL_TODO_EXPORT(SetComboBoxFontProperty, PartsEngine_SetComboBoxFontProperty),
	    HLL_TODO_EXPORT(GetComboBoxFontProperty, PartsEngine_GetComboBoxFontProperty),
	    HLL_TODO_EXPORT(SetMultiTextBoxSize, PartsEngine_SetMultiTextBoxSize),
	    HLL_TODO_EXPORT(SetMultiTextBoxFontProperty, PartsEngine_SetMultiTextBoxFontProperty),
	    HLL_TODO_EXPORT(GetMultiTextBoxFontProperty, PartsEngine_GetMultiTextBoxFontProperty),
	    HLL_TODO_EXPORT(SetMultiTextBoxText, PartsEngine_SetMultiTextBoxText),
	    HLL_TODO_EXPORT(GetMultiTextBoxText, PartsEngine_GetMultiTextBoxText),
	    HLL_TODO_EXPORT(SetMultiTextBoxMaxTextLength, PartsEngine_SetMultiTextBoxMaxTextLength),
	    HLL_TODO_EXPORT(GetMultiTextBoxMaxTextLength, PartsEngine_GetMultiTextBoxMaxTextLength),
	    HLL_TODO_EXPORT(SetMultiTextBoxSelectColor, PartsEngine_SetMultiTextBoxSelectColor),
	    HLL_TODO_EXPORT(GetMultiTextBoxSelectR, PartsEngine_GetMultiTextBoxSelectR),
	    HLL_TODO_EXPORT(GetMultiTextBoxSelectG, PartsEngine_GetMultiTextBoxSelectG),
	    HLL_TODO_EXPORT(GetMultiTextBoxSelectB, PartsEngine_GetMultiTextBoxSelectB),
	    HLL_TODO_EXPORT(SetMultiTextBoxCGName, PartsEngine_SetMultiTextBoxCGName),
	    HLL_TODO_EXPORT(GetMultiTextBoxCGName, PartsEngine_GetMultiTextBoxCGName),
	    HLL_TODO_EXPORT(SetLayoutBoxLayoutType, PartsEngine_SetLayoutBoxLayoutType),
	    HLL_TODO_EXPORT(GetLayoutBoxLayoutType, PartsEngine_GetLayoutBoxLayoutType),
	    HLL_TODO_EXPORT(SetLayoutBoxReturn, PartsEngine_SetLayoutBoxReturn),
	    HLL_TODO_EXPORT(IsLayoutBoxReturn, PartsEngine_IsLayoutBoxReturn),
	    HLL_TODO_EXPORT(GetLayoutBoxReturnSize, PartsEngine_GetLayoutBoxReturnSize),
	    HLL_TODO_EXPORT(SetLayoutBoxAlign, PartsEngine_SetLayoutBoxAlign),
	    HLL_TODO_EXPORT(GetLayoutBoxAlign, PartsEngine_GetLayoutBoxAlign),
	    HLL_EXPORT(Parts_SetPartsCG, PE_SetPartsCG),
	    HLL_EXPORT(Parts_GetPartsCGName, PE_GetPartsCGName),
	    HLL_EXPORT(Parts_SetPartsCGSurfaceArea, PE_SetPartsCGSurfaceArea),
	    HLL_EXPORT(Parts_SetLoopCG, PE_SetLoopCG),
	    HLL_EXPORT(Parts_SetLoopCGSurfaceArea, PE_SetLoopCGSurfaceArea),
	    HLL_EXPORT(Parts_SetText, PE_SetText),
	    HLL_EXPORT(Parts_AddPartsText, PE_AddPartsText),
	    HLL_TODO_EXPORT(Parts_DeletePartsTopTextLine, PartsEngine_Parts_DeletePartsTopTextLine),
	    HLL_EXPORT(Parts_SetPartsTextSurfaceArea, PE_SetPartsTextSurfaceArea),
	    HLL_TODO_EXPORT(Parts_SetPartsTextHighlight, PartsEngine_Parts_SetPartsTextHighlight),
	    HLL_TODO_EXPORT(Parts_AddPartsTextHighlight, PartsEngine_Parts_AddPartsTextHighlight),
	    HLL_TODO_EXPORT(Parts_ClearPartsTextHighlight, PartsEngine_Parts_ClearPartsTextHighlight),
	    HLL_TODO_EXPORT(Parts_SetPartsTextCountReturn, PartsEngine_Parts_SetPartsTextCountReturn),
	    HLL_TODO_EXPORT(Parts_GetPartsTextCountReturn, PartsEngine_Parts_GetPartsTextCountReturn),
	    HLL_EXPORT(Parts_SetFont, PE_SetFont),
	    HLL_EXPORT(Parts_SetPartsFontType, PE_SetPartsFontType),
	    HLL_EXPORT(Parts_SetPartsFontSize, PE_SetPartsFontSize),
	    HLL_EXPORT(Parts_SetPartsFontColor, PE_SetPartsFontColor),
	    HLL_EXPORT(Parts_SetPartsFontBoldWeight, PE_SetPartsFontBoldWeight),
	    HLL_EXPORT(Parts_SetPartsFontEdgeColor, PE_SetPartsFontEdgeColor),
	    HLL_EXPORT(Parts_SetPartsFontEdgeWeight, PE_SetPartsFontEdgeWeight),
	    HLL_EXPORT(Parts_SetTextCharSpace, PE_SetTextCharSpace),
	    HLL_EXPORT(Parts_SetTextLineSpace, PE_SetTextLineSpace),
	    HLL_EXPORT(Parts_SetHGaugeCG, PE_SetHGaugeCG),
	    HLL_EXPORT(Parts_SetHGaugeRate, PE_SetHGaugeRate),
	    HLL_EXPORT(Parts_SetVGaugeCG, PE_SetVGaugeCG),
	    HLL_EXPORT(Parts_SetVGaugeRate, PE_SetVGaugeRate),
	    HLL_EXPORT(Parts_SetHGaugeSurfaceArea, PE_SetHGaugeSurfaceArea),
	    HLL_EXPORT(Parts_SetVGaugeSurfaceArea, PE_SetVGaugeSurfaceArea),
	    HLL_EXPORT(Parts_SetNumeralCG, PE_SetNumeralCG),
	    HLL_EXPORT(Parts_SetNumeralLinkedCGNumberWidthWidthList, PE_SetNumeralLinkedCGNumberWidthWidthList),
	    HLL_TODO_EXPORT(Parts_SetNumeralFont, PartsEngine_Parts_SetNumeralFont),
	    HLL_EXPORT(Parts_SetNumeralNumber, PE_SetNumeralNumber),
	    HLL_EXPORT(Parts_SetNumeralShowComma, PE_SetNumeralShowComma),
	    HLL_EXPORT(Parts_SetNumeralSpace, PE_SetNumeralSpace),
	    HLL_EXPORT(Parts_SetNumeralLength, PE_SetNumeralLength),
	    HLL_EXPORT(Parts_SetNumeralSurfaceArea, PE_SetNumeralSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsRectangleDetectionSize, PE_SetPartsRectangleDetectionSize),
	    HLL_TODO_EXPORT(Parts_SetPartsRectangleDetectionSurfaceArea, PartsEngine_Parts_SetPartsRectangleDetectionSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsCGDetectionSize, PE_SetPartsCGDetectionSize),
	    HLL_TODO_EXPORT(Parts_SetPartsCGDetectionSurfaceArea, PartsEngine_Parts_SetPartsCGDetectionSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsFlat, PE_SetPartsFlat),
	    HLL_EXPORT(Parts_IsPartsFlatEnd, PE_IsPartsFlatEnd),
	    HLL_EXPORT(Parts_GetPartsFlatCurrentFrameNumber, PE_GetPartsFlatCurrentFrameNumber),
	    HLL_EXPORT(Parts_BackPartsFlatBeginFrame, PE_BackPartsFlatBeginFrame),
	    HLL_EXPORT(Parts_StepPartsFlatFinalFrame, PE_StepPartsFlatFinalFrame),
	    HLL_EXPORT(Parts_SetPartsFlatSurfaceArea, PE_SetPartsFlatSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsFlatAndStop, PE_SetPartsFlatAndStop),
	    HLL_EXPORT(Parts_StopPartsFlat, PE_StopPartsFlat),
	    HLL_EXPORT(Parts_StartPartsFlat, PE_StartPartsFlat),
	    HLL_EXPORT(Parts_GoFramePartsFlat, PE_GoFramePartsFlat),
	    HLL_EXPORT(Parts_GetPartsFlatEndFrame, PE_GetPartsFlatEndFrame),
	    HLL_EXPORT(Parts_ExistsFlatFile, PE_ExistsFlatFile),
	    HLL_TODO_EXPORT(Parts_GetPartsFlatDataInfo, PartsEngine_Parts_GetPartsFlatDataInfo),
	    HLL_TODO_EXPORT(Parts_ChangeFlatCG, PartsEngine_Parts_ChangeFlatCG),
	    HLL_TODO_EXPORT(Parts_ChangeFlatSound, PartsEngine_Parts_ChangeFlatSound),
	    HLL_EXPORT(Parts_ClearPartsConstructionProcess, PE_ClearPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreateToPartsConstructionProcess, PE_AddCreateToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreatePixelOnlyToPartsConstructionProcess, PE_AddCreatePixelOnlyToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreateCGToProcess, PE_AddCreateCGToProcess),
	    HLL_EXPORT(Parts_AddFillToPartsConstructionProcess, PE_AddFillToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillAlphaColorToPartsConstructionProcess, PE_AddFillAlphaColorToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillAMapToPartsConstructionProcess, PE_AddFillAMapToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillWithAlphaToPartsConstructionProcess, PE_AddFillWithAlphaToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddFillGradationHorizonToPartsConstructionProcess, PartsEngine_Parts_AddFillGradationHorizonToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawRectToPartsConstructionProcess, PE_AddDrawRectToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawCutCGToPartsConstructionProcess, PE_AddDrawCutCGToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCopyCutCGToPartsConstructionProcess, PE_AddCopyCutCGToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddGrayFilterToPartsConstructionProcess, PE_AddGrayFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddAddFilterToPartsConstructionProcess, PartsEngine_Parts_AddAddFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddMulFilterToPartsConstructionProcess, PartsEngine_Parts_AddMulFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddDrawLineToPartsConstructionProcess, PartsEngine_Parts_AddDrawLineToPartsConstructionProcess),
	    HLL_EXPORT(Parts_BuildPartsConstructionProcess, PE_BuildPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawTextToPartsConstructionProcess, PE_AddDrawTextToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCopyTextToPartsConstructionProcess, PE_AddCopyTextToPartsConstructionProcess),
	    HLL_EXPORT(Parts_SetPartsConstructionSurfaceArea, PE_SetPartsConstructionSurfaceArea),
	    HLL_TODO_EXPORT(Parts_CreateParts3DLayerPluginID, PartsEngine_Parts_CreateParts3DLayerPluginID),
	    HLL_TODO_EXPORT(Parts_GetParts3DLayerPluginID, PartsEngine_Parts_GetParts3DLayerPluginID),
	    HLL_TODO_EXPORT(Parts_ReleaseParts3DLayerPluginID, PartsEngine_Parts_ReleaseParts3DLayerPluginID),
	    HLL_EXPORT(Parts_SetPassCursor, PartsEngine_Parts_SetPassCursor),
	    HLL_EXPORT(Parts_SetClickable, PE_SetClickable),
	    HLL_TODO_EXPORT(Parts_SetResetTimerByChangeInputStatus, PartsEngine_Parts_SetResetTimerByChangeInputStatus),
	    HLL_TODO_EXPORT(Parts_SetDrag, PartsEngine_Parts_SetDrag),
	    HLL_EXPORT(Parts_SetParentPartsNumber, PE_SetParentPartsNumber),
	    HLL_EXPORT(Parts_SetInputState, PE_SetInputState),
	    HLL_EXPORT(Parts_SetOnCursorShowLinkPartsNumber, PE_SetOnCursorShowLinkPartsNumber),
	    HLL_TODO_EXPORT(Parts_SetSoundNumber, PartsEngine_Parts_SetSoundNumber),
	    HLL_EXPORT(Parts_SetPartsPixelDecide, PE_SetPartsPixelDecide),
	    HLL_TODO_EXPORT(Parts_GetPartsPassCursor, PartsEngine_Parts_GetPartsPassCursor),
	    HLL_EXPORT(Parts_GetPartsClickable, PE_GetPartsClickable),
	    HLL_TODO_EXPORT(Parts_GetResetTimerByChangeInputStatus, PartsEngine_Parts_GetResetTimerByChangeInputStatus),
	    HLL_TODO_EXPORT(Parts_GetPartsDrag, PartsEngine_Parts_GetPartsDrag),
	    HLL_TODO_EXPORT(Parts_GetParentPartsNumber, PartsEngine_Parts_GetParentPartsNumber),
	    HLL_EXPORT(Parts_GetInputState, PE_GetInputState),
	    HLL_TODO_EXPORT(Parts_GetOnCursorShowLinkPartsNumber, PartsEngine_Parts_GetOnCursorShowLinkPartsNumber),
	    HLL_TODO_EXPORT(Parts_GetSoundNumber, PartsEngine_Parts_GetSoundNumber),
	    HLL_TODO_EXPORT(Parts_IsPartsPixelDecide, PartsEngine_Parts_IsPartsPixelDecide),
	    HLL_EXPORT(Parts_IsCursorIn, PE_IsCursorIn),
	    HLL_TODO_EXPORT(SaveParts, PartsEngine_SaveParts),
	    HLL_TODO_EXPORT(LoadParts, PartsEngine_LoadParts)
	    );

static struct ain_hll_function *get_fun(int libno, const char *name)
{
	int fno = ain_get_library_function(ain, libno, name);
	return fno >= 0 ? &ain->libraries[libno].functions[fno] : NULL;
}

static void PartsEngine_PreLink(void)
{
	struct ain_hll_function *fun;
	int libno = ain_get_library(ain, "PartsEngine");
	assert(libno >= 0);

	fun = get_fun(libno, "AddDrawCutCGToPartsConstructionProcess");
	if (fun && fun->nr_arguments == 12) {
		static_library_replace(&lib_PartsEngine, "AddDrawCutCGToPartsConstructionProcess",
				PE_AddDrawCutCGToPartsConstructionProcess);
	}
	fun = get_fun(libno, "AddCopyCutCGToPartsConstructionProcess");
	if (fun && fun->nr_arguments == 12) {
		static_library_replace(&lib_PartsEngine, "AddCopyCutCGToPartsConstructionProcess",
				PE_AddCopyCutCGToPartsConstructionProcess);
	}
	fun = get_fun(libno, "Update");
	if (fun && fun->nr_arguments == 5) {
		static_library_replace(&lib_PartsEngine, "Update",
				PartsEngine_Update_Pascha3PC);
	}
	if (get_fun(libno, "AddController")) {
		PE_enable_multi_controller();
	}
}
