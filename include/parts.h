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

// parts.c
bool PE_Init(void);
void PE_Reset(void);
void PE_Update(int passed_time, bool message_window_show);
void PE_UpdateComponent(int passed_time);
void PE_UpdateParts(int passed_time, bool is_skip, bool message_window_show);
void PE_SetDelegateIndex(int parts_no, int delegate_index);
int PE_GetDelegateIndex(int parts_no);
bool PE_SetPartsCG(int parts_no, struct string *cg_name, int sprite_deform, int state);
bool PE_SetPartsCG_by_index(int parts_no, int cg_no, int sprite_deform, int state);
bool PE_SetPartsCG_by_string_index(int parts_no, struct string *cg_no,
		int sprite_deform, int state);
void PE_GetPartsCGName(int parts_no, struct string **cg_name, int state);
bool PE_SetPartsCGSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
int PE_GetPartsCGNumber(int parts_no, int state);
bool PE_SetLoopCG_by_index(int parts_no, int cg_no, int nr_frames, int frame_time, int state);
bool PE_SetLoopCG(int parts_no, struct string *cg_name, int start_no, int nr_frames,
		int frame_time, int state);
bool PE_SetLoopCGSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
bool PE_SetHGaugeCG(int parts_no, struct string *cg_name, int state);
bool PE_SetHGaugeCG_by_index(int parts_no, int cg_no, int state);
bool PE_SetHGaugeRate(int parts_no, float numerator, float denominator, int state);
bool PE_SetHGaugeRate_int(int parts_no, int numerator, int denominator, int state);
bool PE_SetHGaugeSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
bool PE_SetVGaugeCG(int parts_no, struct string *cg_name, int state);
bool PE_SetVGaugeCG_by_index(int parts_no, int cg_no, int state);
bool PE_SetVGaugeRate(int parts_no, float numerator, float denominator, int state);
bool PE_SetVGaugeRate_int(int parts_no, int numerator, int denominator, int state);
bool PE_SetVGaugeSurfaceArea(int parts_no, int x, int y, int w, int h, int state);
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
void PE_ReleaseParts(int parts_no);
void PE_ReleaseAllParts(void);
void PE_ReleaseAllPartsWithoutSystem(void);
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
int PE_GetPartsUpperLeftPosX(int parts_no, int state);
int PE_GetPartsUpperLeftPosY(int parts_no, int state);
void PE_SetPartsOriginPosMode(int parts_no, int origin_pos_mode);
int PE_GetPartsOriginPosMode(int parts_no);
void PE_SetParentPartsNumber(int parts_no, int parent_parts_no);
int PE_GetParentPartsNumber(int parts_no);
bool PE_SetPartsGroupNumber(int parts_no, int group_no);
void PE_SetPartsMessageWindowShowLink(int parts_no, bool message_window_show_link);
bool PE_GetPartsMessageWindowShowLink(int parts_no);
void PE_SetSpeedupRateByMessageSkip(int parts_no, int rate);
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
void PE_SetInputState(int parts_no, int state);
int PE_GetInputState(int parts_no);
bool PE_SetPartsRectangleDetectionSize(int parts_no, int w, int h, int state);
bool PE_SetPartsCGDetectionSize(int parts_no, struct string *cg_name, int state);
bool PE_Save(struct page **buffer);
bool PE_SaveWithoutHideParts(struct page **buffer);
bool PE_Load(struct page **buffer);
// GUIEngine
int PE_GetFreeNumber(void);
bool PE_IsExist(int parts_no);

// construction.c
bool PE_AddCreateToPartsConstructionProcess(int parts_no, int w, int h, int state);
bool PE_AddCreatePixelOnlyToPartsConstructionProcess(int parts_no, int w, int h, int state);
bool PE_AddCreateCGToProcess(int parts_no, struct string *cg_name, int state);
bool PE_AddFillToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int state);
bool PE_AddFillAlphaColorToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int a, int state);
bool PE_AddFillAMapToPartsConstructionProcess(int parts_no, int x, int y, int w, int h, int a, int state);
bool PE_AddDrawRectToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int state);
bool PE_AddDrawCutCGToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, int state);
bool PE_AddCopyCutCGToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, int state);
bool PE_AddGrayFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		bool full_size, int state);
bool PE_AddCopyTextToPartsConstructionProcess(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state);
bool PE_AddDrawTextToPartsConstructionProcess(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state);
bool PE_BuildPartsConstructionProcess(int parts_no, int state);
bool PE_ClearPartsConstructionProcess(int parts_no, int state);
bool PE_SetPartsConstructionSurfaceArea(int parts_no, int x, int y, int w, int h, int state);

// input.c
void PE_UpdateInputState(int passed_time);
void PE_SetClickable(int parts_no, bool clickable);
bool PE_GetPartsClickable(int parts_no);
void PE_SetPartsGroupDecideOnCursor(int group_no, bool decide_on_cursor);
void PE_SetPartsGroupDecideClick(int group_no, bool decide_click);
void PE_SetOnCursorShowLinkPartsNumber(int parts_no, int link_parts_no);
int PE_GetOnCursorShowLinkPartsNumber(int parts_no);
bool PE_SetPartsOnCursorSoundNumber(int parts_no, int sound_no);
bool PE_SetPartsClickSoundNumber(int parts_no, int sound_no);
bool PE_SetClickMissSoundNumber(int sound_no);
void PE_BeginInput(void);
void PE_EndInput(void);
int PE_GetClickPartsNumber(void);
bool PE_IsCursorIn(int parts_no, int mouse_x, int mouse_y, int state);

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
void PE_AddMotionSound(int sound_no, int begin_t);
void PE_AddWholeMotionVibrationSize(int begin_w, int begin_h, int begin_t, int end_t);
void PE_BeginMotion(void);
void PE_EndMotion(void);
void PE_SetMotionTime(int t);
void PE_SeekEndMotion(void);
void PE_UpdateMotionTime(int time, bool skip);
bool PE_IsMotion(void);
int PE_GetMotionEndTime(void);

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

#endif /* SYSTEM4_PARTS_H */
