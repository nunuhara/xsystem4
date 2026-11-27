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

#ifndef SYSTEM4_PARTS_H
#define SYSTEM4_PARTS_H

#include <stdbool.h>

struct page;
struct string;

void PE_SuspendTexture(int no);
void PE_ResumeTexture(int no);
void PE_SetComponentPos(int no, float x, float y);
float PE_GetComponentPosX(int no);
float PE_GetComponentPosY(int no);
void PE_GetComponentAbsolutePos(int no, float *x, float *y);
float PE_GetComponentAbsolutePosX(int no);
float PE_GetComponentAbsolutePosY(int no);
void PE_SetFocus(int no);
bool PE_IsFocus(int no);
int PE_GetActiveParts(void);

struct string *PE_GetPartsFlatName(int no, int state);
bool PE_IsPartsFlatStop(int no, int state);

int PE_GetTextCharSpace(int no, int state);
int PE_GetTextLineSpace(int no, int state);
struct string *PE_GetTextPartsText(int no, int state);
void PE_GetPartsTextSurfaceArea(int no, int *x, int *y, int *w, int *h, int state);
void PE_GetPartsTextFontProperty(int no, int *face, int *size, int *r, int *g, int *b,
		float *bold, int *er, int *eg, int *eb, float *edge, int state);

// parts.c
void PE_enable_multi_controller(void);
bool PE_Init(void);
void PE_Reset(void);
void PE_Update(int passed_time, bool message_window_show);
void PE_UpdateWithTimeScale(int passed_time, int scaled_time, bool message_window_show);
void PE_UpdateComponent(int passed_time);
void PE_UpdateParts(int passed_time, bool is_skip, bool message_window_show);
void PE_SetMessageWindowShow(bool show);
void PE_SetMessageWindowEffectRates(float multiply_rate, float alpha_rate);
void PE_SetDelegateIndex(int parts_no, int delegate_index);
int PE_GetDelegateIndex(int parts_no);
void PE_SetComment(int parts_no, struct string *comment);
struct string *PE_GetComment(int parts_no);
bool PE_SetPartsCG(int parts_no, struct string *cg_name, int sprite_deform, int state);
bool PE_SetPartsCG_by_index(int parts_no, int cg_no, int sprite_deform, int state);
bool PE_SetPartsCG_by_string_index(int parts_no, struct string *cg_no,
		int sprite_deform, int state);
void PE_GetPartsCGName(int parts_no, struct string **cg_name, int state);
bool PE_SetPartsCGSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
void PE_GetPartsCGSurfaceArea(int parts_no, int *x, int *y, int *w, int *h, int state);
int PE_GetPartsCGNumber(int parts_no, int state);
bool PE_SetLoopCG_by_index(int parts_no, int cg_no, int nr_frames, int frame_time, int state);
bool PE_SetLoopCG(int parts_no, struct string *cg_name, int start_no, int nr_frames,
		int frame_time, int state);
bool PE_SetLoopCGSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
bool PE_SetHGaugeCG(int parts_no, struct string *cg_name, int state);
bool PE_SetHGaugeCG_by_index(int parts_no, int cg_no, int state);
bool PE_SetHGaugeRate(int parts_no, float numerator, float denominator, int state);
bool PE_SetHGaugeRate_int(int parts_no, int numerator, int denominator, int state);
void PE_SetHGaugeReverse(int parts_no, bool reverse, int state);
bool PE_SetHGaugeSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
bool PE_SetVGaugeCG(int parts_no, struct string *cg_name, int state);
bool PE_SetVGaugeCG_by_index(int parts_no, int cg_no, int state);
bool PE_SetVGaugeRate(int parts_no, float numerator, float denominator, int state);
bool PE_SetVGaugeRate_int(int parts_no, int numerator, int denominator, int state);
void PE_SetVGaugeReverse(int parts_no, bool reverse, int state);
bool PE_SetVGaugeSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
float PE_GetHGaugeNumerator(int parts_no, int state);
float PE_GetHGaugeDenominator(int parts_no, int state);
float PE_GetVGaugeNumerator(int parts_no, int state);
float PE_GetVGaugeDenominator(int parts_no, int state);
bool PE_SetNumeralCG(int parts_no, struct string *cg_name, int state);
bool PE_SetNumeralCG_by_index(int parts_no, int cg_no, int state);
bool PE_SetNumeralLinkedCGNumberWidthWidthList_by_index(int parts_no, int cg_no,
		int w0, int w1, int w2, int w3, int w4, int w5, int w6, int w7, int w8,
		int w9, int w_minus, int w_comma, int state);
bool PE_SetNumeralLinkedCGNumberWidthWidthList(int parts_no, struct string *cg_name,
		int w0, int w1, int w2, int w3, int w4, int w5, int w6, int w7, int w8,
		int w9, int w_minus, int w_comma, int state);
bool PE_SetNumeralNumber(int parts_no, int n, int state);
bool PE_SetNumeralShowComma(int parts_no, bool show_comma, int state);
bool PE_SetNumeralSpace(int parts_no, int space, int state);
bool PE_SetNumeralLength(int parts_no, int length, int state);
bool PE_SetNumeralSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
void PE_SetNumeralFont(int no, int face, int size, int r, int g, int b, float bold,
		int er, int eg, int eb, float edge, int state);
bool PE_SetNumeralFontWithPitch(int no, int face, int size, int r, int g, int b, float bold,
		int er, int eg, int eb, float edge, bool full_pitch, int state);
void PE_GetNumeralFont(int no, int *face, int *size, int *r, int *g, int *b, float *bold,
		int *er, int *eg, int *eb, float *edge, int state);
void PE_SetNumeralFullPitch(int no, bool full_pitch, int state);
void PE_SetNumeralShowPadding(int no, bool show, int state);
bool PE_IsNumeralShowPadding(int no, int state);
void PE_SetNumeralShowType(int no, int type, int state);
int PE_GetNumeralCGType(int no, int state);
struct string *PE_GetNumeralCGName(int no, int state);
int PE_GetNumeralNumber(int no, int state);
bool PE_IsNumeralShowComma(int no, int state);
int PE_GetNumeralSpace(int no, int state);
int PE_GetNumeralLength(int no, int state);
void PE_GetNumeralCGNumberWidthList(int no, int *w0, int *w1, int *w2, int *w3,
		int *w4, int *w5, int *w6, int *w7, int *w8, int *w9, int *minus, int *comma, int state);
void PE_GetNumeralSurfaceArea(int no, int *x, int *y, int *w, int *h, int state);
void PE_ReleaseParts(int parts_no);
void PE_ReleaseAllParts(void);
void PE_ReleaseAll_list(struct page **erase_number_list);
void PE_ReleaseAllPartsWithoutSystem(void);
void PE_ReleaseAllWithoutSystem(struct page **erase_number_list);
void PE_SetPos(int parts_no, int x, int y);
int PE_GetPartsX(int parts_no);
int PE_GetPartsY(int parts_no);
void PE_SetZ(int parts_no, int z);
int PE_GetPartsZ(int parts_no);
void PE_SetShow(int parts_no, bool show);
bool PE_GetPartsShow(int parts_no);
void PE_SetAlpha(int parts_no, int alpha);
int PE_GetPartsAlpha(int parts_no);
void PE_SetPartsDrawFilter(int parts_no, int draw_filter);
int PE_GetPartsDrawFilter(int parts_no);
void PE_SetAddColor(int parts_no, int r, int g, int b);
void PE_GetAddColor(int parts_no, int *r, int *g, int *b);
void PE_SetMultiplyColor(int parts_no, int r, int g, int b);
void PE_GetMultiplyColor(int parts_no, int *r, int *g, int *b);
int PE_GetPartsWidth(int parts_no, int state);
int PE_GetPartsHeight(int parts_no, int state);
void PE_GetComponentAbsoluteSquarePos(int no, float *x0, float *y0,
		float *x1, float *y1, float *x2, float *y2, float *x3, float *y3, int state);
int PE_GetPartsUpperLeftPosX(int parts_no, int state);
int PE_GetPartsUpperLeftPosY(int parts_no, int state);
void PE_SetPartsOriginPosMode(int parts_no, int origin_pos_mode);
int PE_GetPartsOriginPosMode(int parts_no);
void PE_SetParentPartsNumber(int parts_no, int parent_parts_no);
int PE_GetParentPartsNumber(int parts_no);
bool PE_IsExistChild(int no, int child);
void PE_ClearChild(int no);
void PE_AddChild(int no, int child);
void PE_InsertChild(int no, int index, int child);
void PE_RemoveChild(int no, int child);
int PE_NumofChild(int no);
int PE_GetChildIndex(int no, int child);
int PE_GetChild(int no, int index);
int PE_GetControllerIndex(int id);
int PE_GetControllerID(int index);
int PE_GetControllerLength(void);
bool PE_SetPartsGroupNumber(int parts_no, int group_no);
void PE_SetPartsMessageWindowShowLink(int parts_no, bool message_window_show_link);
bool PE_GetPartsMessageWindowShowLink(int parts_no);
void PE_SetEnableSpeedUp(int parts_no, bool enable);
bool PE_IsEnableSpeedUp(int parts_no);
void PE_SetSpeedupRateByMessageSkip(int parts_no, int rate);
int PE_GetSpeedupRateByMessageSkip(int parts_no);
void PE_SetPartsMagX(int parts_no, float scale_x);
float PE_GetPartsMagX(int parts_no);
void PE_SetPartsMagY(int parts_no, float scale_y);
float PE_GetPartsMagY(int parts_no);
void PE_SetPartsRotateX(int parts_no, float rot_x);
void PE_SetPartsRotateY(int parts_no, float rot_y);
void PE_SetPartsRotateZ(int parts_no, float rot_z);
float PE_GetPartsRotateZ(int parts_no);
void PE_SetPartsAlphaClipperPartsNumber(int parts_no, int alpha_clipper_parts_no);
void PE_SetPartsPixelDecide(int PartsNumber, bool pixel_decide);
bool PE_SetThumbnailReductionSize(int reduction_size);
bool PE_SetThumbnailMode(bool Mode);
void PE_SetComponentType(int parts_no, int type, int state);
int PE_GetComponentType(int parts_no, int state);
void PE_SetInputState(int parts_no, int state);
int PE_GetInputState(int parts_no);
bool PE_SetPartsRectangleDetectionSize(int parts_no, int w, int h, int state);
void PE_SetRectangleDetectionPoint(int no, int x0, int y0, int x1, int y1,
		int x2, int y2, int x3, int y3, int state);
void PE_GetRectangleDetectionPoint(int no, int *x0, int *y0, int *x1, int *y1,
		int *x2, int *y2, int *x3, int *y3, int state);
bool PE_SetPartsCGDetectionSize(int parts_no, struct string *cg_name, int state);
bool PE_Save(struct page **buffer);
bool PE_SaveParts(int parts_no, struct page **buffer);
bool PE_LoadParts(int parts_no, struct page **buffer);
bool PE_SaveWithoutHideParts(struct page **buffer);
bool PE_Load(struct page **buffer);
bool PE_SaveBackScene(struct page **buffer);
bool PE_LoadBackScene(struct page **buffer);
void PE_ClearBackScene(void);
void PE_SetShowForBackScene(int no, bool show);
void PE_SetMulColorForBackScene(int no, int r, int g, int b);
void PE_SetAlphaForBackScene(int no, int a);
void PE_SetFontColorForBackScene(int no, int r, int g, int b, int state);
int PE_AddController(int index);
void PE_RemoveController(struct page **erase_number_list, int index);
void PE_MoveController(int id, int index);
bool PE_CreateParts3DLayerPluginID(int parts_no, int state);
int PE_GetParts3DLayerPluginID(int parts_no, int state);
bool PE_ReleaseParts3DLayerPluginID(int parts_no, int state);
// GUIEngine
int PE_GetFreeNumber(void);
bool PE_IsExist(int parts_no);
// PartsFunc interface
void PE_set_active_controller(int controller_no);
int PE_get_active_controller(void);
int PE_get_system_controller(void);
void PE_parts_set_want_save(int parts_no, bool want_save);
bool PE_parts_get_want_save(int parts_no);
bool PE_save_thumbnail(struct string *filename, int reduction_factor);
bool PE_init_parts_movie(int parts_no, int width, int height, int bg_r, int bg_g, int bg_b, int state);
int PE_get_movie_sprite(int parts_no, int state);
float PE_parts_get_absolute_x(int parts_no);
float PE_parts_get_absolute_y(int parts_no);
int PE_parts_get_absolute_z(int parts_no);
void PE_parts_set_lock_input_state(int parts_no, bool lock);

// construction.c
bool PE_AddCreateToPartsConstructionProcess(int parts_no, int w, int h, int state);
bool PE_AddCreatePixelOnlyToPartsConstructionProcess(int parts_no, int w, int h, int state);
bool PE_AddCreateCGToProcess(int parts_no, struct string *cg_name, int state);
bool PE_AddFillToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int state);
bool PE_AddFillAlphaColorToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int a, int state);
bool PE_AddFillAMapToPartsConstructionProcess(int parts_no, int x, int y, int w, int h, int a, int state);
bool PE_AddFillWithAlphaToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int a, int state);
bool PE_AddFillGradationHorizonToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int top_r, int top_g, int top_b, int bot_r, int bot_g, int bot_b, int state);
bool PE_AddDrawRectToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int state);
bool PE_AddDrawLineToPartsConstructionProcess(int parts_no, int x0, int y0, int x1, int y1,
		int r, int g, int b, int a, int state);
bool PE_AddCutCGAlphaBlendToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, bool scale, int state);
bool PE_AddCutCGOnlyAlphaToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, bool scale, int state);
bool PE_AddMulAMapGradationHorizonToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int top_a, int bot_a, int state);
bool PE_AddMulAMapGradationVerticalToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int left_a, int right_a, int state);
bool PE_AddCGBlendToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int state);
bool PE_AddDrawCutCGToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, int state);
bool PE_AddCopyCutCGToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, int state);
bool PE_AddGrayFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		bool full_size, int state);
bool PE_AddAddFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, bool full_size, int state);
bool PE_AddMulFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, bool full_size, int state);
bool PE_AddHBlurFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int blur, bool full_size, int state);
bool PE_AddVBlurFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int blur, bool full_size, int state);
bool PE_AddCopyTextToPartsConstructionProcess(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state);
bool PE_AddDrawTextToPartsConstructionProcess(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state);
bool PE_AddAlphaBlendTextToPartsConstructionProcess(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state);
bool PE_AddOnlyAlphaTextToPartsConstructionProcess(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state);
bool PE_BuildPartsConstructionProcess(int parts_no, int state);
bool PE_BuildPartsConstructionProcessThread(int parts_no, int state);
bool PE_ClearPartsConstructionProcess(int parts_no, int state);
bool PE_SetPartsConstructionSurfaceArea(int parts_no, int x, int y, int w, int h, int state);

// input.c
void PE_UpdateInputState(int passed_time);
void PE_SetEnableInput(bool enable);
bool PE_IsEnableInput(void);
void PE_SetPassCursor(int parts_no, bool pass);
bool PE_GetPartsPassCursor(int parts_no);
void PE_SetClickable(int parts_no, bool clickable);
void PE_SetWheelable(int parts_no, bool enable);
bool PE_GetPartsClickable(int parts_no);
void PE_SetEnableInputProcess(int parts_no, bool enable);
bool PE_IsEnableInputProcess(int parts_no);
void PE_Parts_SetSoundName(int parts_no, struct string *name, int state);
struct string *PE_Parts_GetSoundName(int parts_no, int state);
void PE_Parts_PlaySound(int parts_no, int state);
void PE_SetDrag(int parts_no, bool enable);
void PE_Parts_SetSwipeType(int parts_no, int swipe_type);
int PE_Parts_GetSwipeType(int parts_no);
void PE_Parts_StopSwipe(void);
void PE_SetPartsGroupDecideOnCursor(int group_no, bool decide_on_cursor);
void PE_SetPartsGroupDecideClick(int group_no, bool decide_click);
void PE_SetOnCursorShowLinkPartsNumber(int parts_no, int link_parts_no);
int PE_GetOnCursorShowLinkPartsNumber(int parts_no);
bool PE_SetPartsOnCursorSoundNumber(int parts_no, int sound_no);
bool PE_SetPartsClickSoundNumber(int parts_no, int sound_no);
bool PE_SetClickMissSoundNumber(int sound_no);
void PE_SetClickMissSoundName(struct string *name);
struct string *PE_GetClickMissSoundName(void);
void PE_BeginInput(void);
void PE_EndInput(void);
int PE_GetClickPartsNumber(void);
bool PE_IsCursorIn(int parts_no, int mouse_x, int mouse_y, int state);
void PE_FlushCGCache(void);

// message.c
void PE_ReleaseMessage(void);
void PE_PopMessage(void);
void PE_SeekMessage(int target_parts_no);
int PE_GetMessageType(void);
int PE_GetMessagePartsNumber(void);
int PE_GetMessageDelegateIndex(void);
int PE_GetMessageUniqueID(void);
void PE_SetEventID(int parts_no, int delegate_index, int unique_id);
void PE_SetComponentSubColorMode(int parts_no, bool enable);
bool PE_IsComponentSubColorMode(int parts_no);
int PE_GetMessageVariableCount(void);
int PE_GetMessageVariableType(int index);
int PE_GetMessageVariableInt(int index);
float PE_GetMessageVariableFloat(int index);
bool PE_GetMessageVariableBool(int index);
void PE_GetMessageVariableString(int index, struct string **out);

// motion.c
void PE_AddMotionPos(int parts_no, int begin_x, int begin_y, int end_x, int end_y, int begin_t, int end_t);
void PE_AddMotionPos_curve(int parts_no, int begin_x, int begin_y, int end_x, int end_y,
		int begin_t, int end_t, struct string *curve_name);
void PE_AddMotionAlpha(int parts_no, int begin_a, int end_a, int begin_t, int end_t);
void PE_AddMotionAlpha_curve(int parts_no, int begin_a, int end_a, int begin_t, int end_t,
		struct string *curve_name);
void PE_AddMotionCG_by_index(int parts_no, int begin_cg_no, int nr_cg, int begin_t, int end_t);
void PE_AddMotionHGaugeRate(int parts_no, float begin_numerator, float begin_denominator,
			    float end_numerator, float end_denominator, int begin_t, int end_t);
void PE_AddMotionHGaugeRate_curve(int parts_no, float begin_numerator, float begin_denominator,
			    float end_numerator, float end_denominator, int begin_t, int end_t,
			    struct string *curve_name);
void PE_AddMotionVGaugeRate(int parts_no, float begin_numerator, float begin_denominator,
			    float end_numerator, float end_denominator, int begin_t, int end_t);
void PE_AddMotionVGaugeRate_curve(int parts_no, float begin_numerator, float begin_denominator,
			    float end_numerator, float end_denominator, int begin_t, int end_t,
			    struct string *curve_name);
void PE_AddMotionNumeralNumber(int parts_no, int begin_n, int end_n, int begin_t, int end_t);
void PE_AddMotionNumeralNumber_curve(int parts_no, int begin_n, int end_n, int begin_t,
		int end_t, struct string *curve_name);
void PE_AddMotionMagX(int parts_no, float begin, float end, int begin_t, int end_t);
void PE_AddMotionMagX_curve(int parts_no, float begin, float end, int begin_t, int end_t,
		struct string *curve_name);
void PE_AddMotionMagY(int parts_no, float begin, float end, int begin_t, int end_t);
void PE_AddMotionMagY_curve(int parts_no, float begin, float end, int begin_t, int end_t,
		struct string *curve_name);
void PE_AddMotionRotateX(int parts_no, float begin, float end, int begin_t, int end_t);
void PE_AddMotionRotateX_curve(int parts_no, float begin, float end, int begin_t, int end_t,
		struct string *curve_name);
void PE_AddMotionRotateY(int parts_no, float begin, float end, int begin_t, int end_t);
void PE_AddMotionRotateY_curve(int parts_no, float begin, float end, int begin_t, int end_t,
		struct string *curve_name);
void PE_AddMotionRotateZ(int parts_no, float begin, float end, int begin_t, int end_t);
void PE_AddMotionRotateZ_curve(int parts_no, float begin, float end, int begin_t, int end_t,
		struct string *curve_name);
void PE_AddMotionVibrationSize(int parts_no, int begin_w, int begin_h, int begin_t, int end_t);
void PE_AddMotionAddColor_curve(int parts_no, int br, int bg, int bb,
		int er, int eg, int eb, int begin_t, int end_t, struct string *curve_name);
void PE_AddMotionMulColor_curve(int parts_no, int br, int bg, int bb,
		int er, int eg, int eb, int begin_t, int end_t, struct string *curve_name);
bool PE_AddMotionCG_curve(int parts_no, struct string *base_cg, int start_no,
		int num_of_cg, int begin_t, int end_t, struct string *curve_name);
void PE_AddMotionSound(int sound_no, int begin_t);
void PE_AddMotionSound_name(struct string *sound_name, int begin_t);
void PE_AddWholeMotionVibrationSize(int begin_w, int begin_h, int begin_t, int end_t);
void PE_BeginMotion(void);
void PE_EndMotion(void);
void PE_SetMotionTime(int t);
void PE_SeekEndMotion(void);
void PE_UpdateMotionTime(int time, bool skip);
bool PE_IsMotion(void);
int PE_GetMotionEndTime(void);
void PE_PauseMotion(bool pause);

// text.c
bool PE_SetText(int parts_no, struct string *text, int state);
bool PE_AddPartsText(int parts_no, struct string *text, int state);
bool PE_SetPartsTextSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
bool PE_SetFont(int parts_no, int type, int size, int r, int g, int b, float bold_weight, int edge_r, int edge_g, int edge_b, float edge_weight, int state);
bool PE_SetPartsFontType(int parts_no, int type, int state);
bool PE_SetPartsFontSize(int parts_no, int size, int state);
bool PE_SetPartsFontColor(int parts_no, int r, int g, int b, int state);
bool PE_SetPartsFontBoldWeight(int parts_no, float bold_weight, int state);
bool PE_SetPartsFontEdgeColor(int parts_no, int r, int g, int b, int state);
bool PE_SetPartsFontEdgeWeight(int parts_no, float edge_weight, int state);
bool PE_SetTextCharSpace(int parts_no, int char_space, int state);
bool PE_SetTextLineSpace(int parts_no, int line_space, int state);

// flash.c
bool PE_ExistsFlashFile(struct string *flash_filename);
bool PE_SetPartsFlash(int parts_no, struct string *flash_filename, int state);
bool PE_IsPartsFlashEnd(int parts_no, int state);
int PE_GetPartsFlashCurrentFrameNumber(int parts_no, int state);
bool PE_BackPartsFlashBeginFrame(int parts_no, int state);
bool PE_StepPartsFlashFinalFrame(int parts_no, int state);
bool PE_SetPartsFlashAndStop(int parts_no, struct string *flash_filename, int state);
bool PE_StopPartsFlash(int parts_no, int state);
bool PE_StartPartsFlash(int parts_no, int state);
bool PE_GoFramePartsFlash(int parts_no, int frame_no, int state);
int PE_GetPartsFlashEndFrame(int parts_no, int state);

// flat.c
bool PE_ExistsFlatFile(struct string *filename);
bool PE_SetPartsFlat(int parts_no, struct string *filename, int state);
bool PE_SetPartsFlatThread(int parts_no, struct string *filename, int state);
bool PE_IsPartsFlatEnd(int parts_no, int state);
int PE_GetPartsFlatCurrentFrameNumber(int parts_no, int state);
bool PE_BackPartsFlatBeginFrame(int parts_no, int state);
bool PE_StepPartsFlatFinalFrame(int parts_no, int state);
bool PE_SetPartsFlatSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
void PE_GetFlatSurfaceArea(int parts_no, int *x, int *y, int *w, int *h, int state);
bool PE_SetPartsFlatAndStop(int parts_no, struct string *filename, int state);
bool PE_StopPartsFlat(int parts_no, int state);
bool PE_StartPartsFlat(int parts_no, int state);
bool PE_GoFramePartsFlat(int parts_no, int frame_no, int state);
int PE_GetPartsFlatEndFrame(int parts_no, int state);
void PE_SetFlatFPSRate(int parts_no, float rate, int state);
float PE_GetFlatFPSRate(int parts_no, int state);
int PE_GetFlatFPS(int parts_no, int state);
int PE_GetFlatFrame(int parts_no, struct string *label_name, int state);
void PE_JumpFlatLabel(int parts_no, struct string *label_name, int state);
int PE_GetFlatBeginSeekFrame(int parts_no, int state);
int PE_NumofFlatCG(int parts_no, int state);
struct string *PE_GetFlatCGName(int parts_no, int index, int state);
void PE_ChangeFlatCG(int parts_no, struct string *old_name, struct string *new_name, int state);
struct string *PE_GetProxyFlatCGName(int parts_no, struct string *old_name, int state);
void PE_ReturnChangeFlatCG(int parts_no, struct string *old_name, int state);
void PE_ClearChangeFlatCG(int parts_no, int state);
int PE_NumofFlatSound(int parts_no, int state);
struct string *PE_GetFlatSoundName(int parts_no, int index, int state);
void PE_ChangeFlatSound(int parts_no, struct string *old_name, struct string *new_name, int state);
struct string *PE_GetProxyFlatSound(int parts_no, struct string *old_name, int state);
void PE_ReturnChangeFlatSound(int parts_no, struct string *old_name, int state);
void PE_ClearChangeFlatSound(int parts_no, int state);
void PE_SetFlatEnableLayer(int parts_no, struct string *name, bool enable, int state);
bool PE_IsFlatEnableLayer(int parts_no, struct string *name, int state);
int PE_GetFlatGameViewWidth(int parts_no, int state);
int PE_GetFlatGameViewHeight(int parts_no, int state);
struct string *PE_GetPartsFlatDataInfo(int parts_no, int state);

// layoutbox.c
void PE_SetLayoutBoxLayoutType(int parts_no, int type);
int PE_GetLayoutBoxLayoutType(int parts_no);
void PE_SetLayoutBoxReturn(int parts_no, bool return_flag, int return_size);
void PE_SetLayoutBoxReturnF(int parts_no, bool return_flag, float return_size);
bool PE_IsLayoutBoxReturn(int parts_no);
int PE_GetLayoutBoxReturnSize(int parts_no);
float PE_GetLayoutBoxReturnSizeF(int parts_no);
void PE_SetLayoutBoxReturnSizeForRate(int parts_no, bool size_for_rate);
bool PE_IsLayoutBoxReturnSizeForRate(int parts_no);
void PE_SetLayoutBoxAlign(int parts_no, int align);
int PE_GetLayoutBoxAlign(int parts_no);
void PE_SetComponentMargin(int parts_no, int top, int bottom, int left, int right);
int PE_GetComponentMarginTop(int parts_no);
int PE_GetComponentMarginBottom(int parts_no);
int PE_GetComponentMarginLeft(int parts_no);
int PE_GetComponentMarginRight(int parts_no);
void PE_set_layoutbox_padding(int parts_no, int top, int bottom, int left, int right);
int PE_get_layoutbox_padding_top(int parts_no);
int PE_get_layoutbox_padding_bottom(int parts_no);
int PE_get_layoutbox_padding_left(int parts_no);
int PE_get_layoutbox_padding_right(int parts_no);

// Modifier UI scale hooks (100 = 100% original)
void parts_set_modifier_font_scale(int pct);
int parts_get_modifier_font_scale(void);
void parts_refresh_live_font_scale(void);
void parts_set_modifier_scrollbar_scale(int pct);
int parts_get_modifier_scrollbar_scale(void);
void parts_refresh_live_scrollbar_scale(void);

#endif /* SYSTEM4_PARTS_H */
