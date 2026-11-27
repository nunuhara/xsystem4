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
#include <string.h>

#include "system4/ain.h"
#include "system4/file.h"
#include "system4/little_endian.h"
#include "system4/string.h"

#include "vm/heap.h"
#include "vm/page.h"
#include "audio.h"
#include "id_pool.h"
#include "kiwi.h"
#include "movie.h"
#include "parts.h"
#include "../parts/parts_internal.h"
#include "parts_activity.h"
#include "parts_widgets.h"
#include "parts_scrollbar.h"
#include "parts_message_window.h"
#include "sact.h"
#include "xsystem4.h"
#include "hll.h"

static struct id_pool user_parts_numbers;

static bool PartsEngine_ReadActivityFile_legacy(struct string *name, struct string *file)
{
	return PE_ReadActivityFile(name, file, false);
}

static void *PartsEngine_ResolveReadActivityFile(const struct ain_hll_function *f)
{
	if (f->return_type.data != AIN_BOOL || (f->nr_arguments != 2 && f->nr_arguments != 3)
			|| f->arguments[0].type.data != AIN_STRING || f->arguments[1].type.data != AIN_STRING)
		return NULL;
	if (f->nr_arguments == 2)
		return PartsEngine_ReadActivityFile_legacy;
	return f->arguments[2].type.data == AIN_BOOL ? PE_ReadActivityFile : NULL;
}

static void *PartsEngine_ResolveGaugeRate(const struct ain_hll_function *f)
{
	if (f->return_type.data != AIN_BOOL || f->nr_arguments != 4
			|| f->arguments[0].type.data != AIN_INT || f->arguments[3].type.data != AIN_INT)
		return NULL;
	bool horizontal = !strcmp(f->name, "SetHGaugeRate");
	enum ain_data_type ratio_type = f->arguments[1].type.data;
	if (f->arguments[2].type.data != ratio_type)
		return NULL;
	if (ratio_type == AIN_FLOAT)
		return horizontal ? PE_SetHGaugeRate : PE_SetVGaugeRate;
	if (ratio_type == AIN_INT)
		return horizontal ? PE_SetHGaugeRate_int : PE_SetVGaugeRate_int;
	return NULL;
}

static void *PartsEngine_ResolveSetNumeralFont(const struct ain_hll_function *f)
{
	if (f->nr_arguments == 13 && f->return_type.data == AIN_BOOL
			&& f->arguments[11].type.data == AIN_BOOL && f->arguments[12].type.data == AIN_INT)
		return PE_SetNumeralFontWithPitch;
	if (f->nr_arguments == 12 && f->return_type.data == AIN_VOID && f->arguments[11].type.data == AIN_INT)
		return PE_SetNumeralFont;
	return NULL;
}

static void *PartsEngine_ResolveReleaseAll(const struct ain_hll_function *f)
{
	if (f->nr_arguments == 0)
		return PE_ReleaseAllParts;
	if (f->nr_arguments == 1 && (f->arguments[0].type.data == AIN_REF_ARRAY_INT
			|| f->arguments[0].type.data == AIN_REF_ARRAY))
		return PE_ReleaseAll_list;
	return NULL;
}

static void PartsEngine_Parts_ChangeFlatSound_int(int parts_no, struct string *old_name,
		int sound_no, int state)
{
	(void)parts_no;
	(void)old_name;
	(void)sound_no;
	(void)state;
}

static void *PartsEngine_ResolveChangeFlatSound(const struct ain_hll_function *f)
{
	if (f->nr_arguments != 4)
		return NULL;
	if (f->arguments[2].type.data == AIN_INT)
		return PartsEngine_Parts_ChangeFlatSound_int;
	if (f->arguments[2].type.data == AIN_STRING)
		return PE_ChangeFlatSound;
	return NULL;
}

static void PartsEngine_ModuleInit(void)
{
	PE_Init();
	id_pool_init(&user_parts_numbers, 1);
}

static void PartsEngine_ModuleFini(void)
{
	PE_Reset();
	id_pool_delete(&user_parts_numbers);
}

static int PartsEngine_CreateUserPartsNumber(void)
{
	int id = id_pool_get_unused(&user_parts_numbers);
	// Reserve the number before a component is created; released numbers are reused.
	id_pool_set(&user_parts_numbers, id, &user_parts_numbers);
	return id;
}

static void PartsEngine_ReleaseUserPartsNumber(int id)
{
	id_pool_release(&user_parts_numbers, id);
}

static void PartsEngine_Update(int passed_time, bool is_skip, bool message_window_show)
{
	PE_Update(passed_time, message_window_show);
}

static void PartsEngine_Update_v8(int passed_time, possibly_unused bool is_skip,
		bool message_window_show, float multiply_rate, float alpha_rate)
{
	PE_SetMessageWindowEffectRates(multiply_rate, alpha_rate);
	PE_Update(passed_time, message_window_show);
}

static void PartsEngine_Update_v12(int passed_time, int scaled_time, bool message_window_show,
		float multiply_rate, float alpha_rate)
{
	PE_SetMessageWindowEffectRates(multiply_rate, alpha_rate);
	PE_UpdateWithTimeScale(passed_time, scaled_time, message_window_show);
}

// Rance X inserted six component kinds before the sprite state kinds.
// Values are confirmed by the corresponding C*Parts constructors in its AIN.
static void PartsEngine_SetComponentType_v12(int parts_no, int type, int state)
{
	if (ain->version >= 14) {
		if (type == 12 || type == 13) {
			parts_sliderbar_create(parts_get(parts_no), type == 13);
			return;
		}
		if ((type >= 0 && type <= 7) || (type >= 9 && type <= 11)) {
			PE_SetSpecialComponentType(parts_no, type);
			return;
		}
		if (type >= 14 && type <= 18) {
			PE_SetSpecialComponentType(parts_no, type - 2);
			return;
		}
		if (type == 27) {
			PE_SetComponentType(parts_no, 17, state);
			if (parts_state_valid(state - 1))
				parts_get(parts_no)->states[state - 1].rect.is_cg_detection = true;
			return;
		}
		if ((type >= 19 && type <= 26) || (type >= 28 && type <= 30))
			type -= 8;
		else if (type != 8)
			VM_ERROR("Unimplemented AIN v14 component type %d", type);
		PE_SetComponentType(parts_no, type, state);
		return;
	}
	if ((type >= 0 && type <= 7) || (type >= 9 && type <= 16)) {
		PE_SetSpecialComponentType(parts_no, type);
		return;
	}
	if (type == 25) {
		PE_SetComponentType(parts_no, 17, state);
		if (parts_state_valid(state - 1))
			parts_get(parts_no)->states[state - 1].rect.is_cg_detection = true;
		return;
	}
	if ((type >= 17 && type <= 24) || (type >= 26 && type <= 28))
		type -= 6;
	else if (type != 8)
		VM_ERROR("Unimplemented AIN v12 component type %d", type);
	PE_SetComponentType(parts_no, type, state);
}

static int PartsEngine_GetComponentType_v12(int parts_no, int state)
{
	struct parts *p = parts_try_get(parts_no);
	if (ain->version >= 14) {
		if (p && p->scrollbar && p->scrollbar->is_slider)
			return p->scrollbar->horizontal ? 13 : 12;
		int special = PE_GetSpecialComponentType(parts_no);
		if (special >= 0)
			return special >= 12 ? special + 2 : special;
		int type = PE_GetComponentType(parts_no, state);
		if (type == 17 && parts_state_valid(state - 1) && p && p->states[state - 1].rect.is_cg_detection)
			return 27;
		return type >= 11 ? type + 8 : type;
	}
	int special = PE_GetSpecialComponentType(parts_no);
	if (special >= 0) return special;
	int type = PE_GetComponentType(parts_no, state);
	if (type == 17 && parts_state_valid(state - 1) && p && p->states[state - 1].rect.is_cg_detection)
		return 25;
	return type >= 11 ? type + 6 : type;
}

static void PartsEngine_UpdateMatrix(bool show)
{
	PE_SetMessageWindowShow(show);
	PE_UpdateComponent(0);
	parts_render_update();
}

static void PartsEngine_UpdateMatrix_v8(bool show, float multiply_rate, float alpha_rate)
{
	PE_SetMessageWindowShow(show);
	PE_SetMessageWindowEffectRates(multiply_rate, alpha_rate);
	PE_UpdateComponent(0);
	parts_render_update();
}

static struct string *PartsEngine_GetPartsCGName_v12(int parts_no, int state)
{
	struct string *name = string_ref(&EMPTY_STRING);
	PE_GetPartsCGName(parts_no, &name, state);
	return name;
}

static struct string *PartsEngine_GetMessageVariableString_v12(int index)
{
	struct string *text = NULL;
	PE_GetMessageVariableString(index, &text);
	return text;
}

static void PartsEngine_GetActivityIntentDataDestination_v6(struct string **out, int index,
		struct string *name, struct string *part)
{
	struct string *s = PE_GetActivityIntentDataDestination(index, name, part);
	if (out) {
		if (*out)
			free_string(*out);
		*out = s;
	} else if (s) {
		free_string(s);
	}
}

// Async CG decode runs CPU decompression on background worker threads; GL
// texture upload and part attachment run on the render thread during PE_Update
// / PE_UpdateComponent / PE_IsThreadLoading / PE_IsExistWorkerThread.
static bool PartsEngine_SetPartsCGThread(int no, struct string *name, int state)
{
	return PE_SetPartsCGThread(no, name, state);
}

static bool PartsEngine_IsThreadLoading(int no, int state)
{
	return PE_IsThreadLoading(no, state);
}

static bool PartsEngine_IsExistWorkerThread(void)
{
	return PE_IsExistWorkerThread();
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
	PE_SetComponentPos(parts_no, x, y);
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

static void PartsEngine_Parts_GetPartsUpperLeftPos(int parts_no, float *x, float *y, int state)
{
	*x = PE_GetPartsUpperLeftPosX(parts_no, state);
	*y = PE_GetPartsUpperLeftPosY(parts_no, state);
}

static void PartsEngine_Parts_GetPartsSize(int parts_no, int *w, int *h, int state)
{
	*w = PE_GetPartsWidth(parts_no, state);
	*h = PE_GetPartsHeight(parts_no, state);
}

static int PartsEngine_GetComponentWidth(int parts_no)
{
	return PE_GetPartsWidth(parts_no, PE_GetInputState(parts_no));
}

static int PartsEngine_GetComponentHeight(int parts_no)
{
	return PE_GetPartsHeight(parts_no, PE_GetInputState(parts_no));
}

static int PartsEngine_GetComponentAddColorR(int parts_no)
{
	int r = 0, g = 0, b = 0;
	PE_GetAddColor(parts_no, &r, &g, &b);
	return r;
}

static int PartsEngine_GetComponentAddColorG(int parts_no)
{
	int r = 0, g = 0, b = 0;
	PE_GetAddColor(parts_no, &r, &g, &b);
	return g;
}

static int PartsEngine_GetComponentAddColorB(int parts_no)
{
	int r = 0, g = 0, b = 0;
	PE_GetAddColor(parts_no, &r, &g, &b);
	return b;
}

static int PartsEngine_GetComponentMulColorR(int parts_no)
{
	int r = 255, g = 255, b = 255;
	PE_GetMultiplyColor(parts_no, &r, &g, &b);
	return r;
}

static int PartsEngine_GetComponentMulColorG(int parts_no)
{
	int r = 255, g = 255, b = 255;
	PE_GetMultiplyColor(parts_no, &r, &g, &b);
	return g;
}

static int PartsEngine_GetComponentMulColorB(int parts_no)
{
	int r = 255, g = 255, b = 255;
	PE_GetMultiplyColor(parts_no, &r, &g, &b);
	return b;
}

static float PartsEngine_GetComponentRotateX(int parts_no)
{
	return parts_get(parts_no)->local.rotation.x;
}

static float PartsEngine_GetComponentRotateY(int parts_no)
{
	return parts_get(parts_no)->local.rotation.y;
}

static int PartsEngine_GetComponentAlphaClipper(int parts_no)
{
	return parts_get(parts_no)->alpha_clipper_parts_no;
}

static int parts_max_global_z(struct parts *parts)
{
	int max_z = parts->global.z;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		int cz = parts_max_global_z(child);
		if (cz > max_z)
			max_z = cz;
	}
	return max_z;
}

static int PartsEngine_GetComponentAbsoluteMaxPosZ(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? parts_max_global_z(parts) : 0;
}

static bool PartsEngine_IsLockInputState(int parts_no)
{
	return parts_get(parts_no)->lock_input_state;
}

static void PartsEngine_Parts_SetPosForRate(int parts_no, bool flag)
{
	struct parts *p = parts_get(parts_no);
	if (p->position_is_ratio != flag) {
		p->position_is_ratio = flag;
		parts_component_dirty(p);
	}
}

static bool PartsEngine_Parts_IsPosForRate(int parts_no)
{
	return parts_get(parts_no)->position_is_ratio;
}

static bool PartsEngine_LoadActivityEXText(struct string *name, struct string *text, bool edit)
{
	(void)edit;
	PE_SetActivityEXText(name, text);
	return true;
}

static bool PartsEngine_SaveActivityEXText(struct string **text, struct string *name)
{
	if (!text)
		return false;
	if (*text)
		free_string(*text);
	*text = PE_GetActivityEXText(name);
	return true;
}

static bool PartsEngine_WriteActivityFile(struct string *name, struct string *file_name)
{
	(void)name;
	(void)file_name;
	return false;
}

static void PartsEngine_SetComponentMessageWindowEffectLink(int parts_no, bool link)
{
	struct parts *p = parts_get(parts_no);
	if (p->message_window_effect_link != link) {
		p->message_window_effect_link = link;
		parts_dirty(p);
	}
}

static bool PartsEngine_IsComponentMessageWindowEffectLink(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->message_window_effect_link : false;
}

static void PartsEngine_SetComponentEnableClipArea(int parts_no, bool enable)
{
	struct parts *p = parts_get(parts_no);
	if (p->enable_clip_area != enable) {
		p->enable_clip_area = enable;
		parts_component_dirty(p);
	}
}

static bool PartsEngine_IsComponentEnableClipArea(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->enable_clip_area : false;
}

static void PartsEngine_SetComponentClipArea(int parts_no, int x, int y, int w, int h)
{
	struct parts *p = parts_get(parts_no);
	p->clip_area = (Rectangle){ x, y, w, h };
	parts_component_dirty(p);
}

static int PartsEngine_GetComponentClipAreaPosX(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->clip_area.x : 0;
}

static int PartsEngine_GetComponentClipAreaPosY(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->clip_area.y : 0;
}

static int PartsEngine_GetComponentClipAreaPosWidth(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->clip_area.w : 0;
}

static int PartsEngine_GetComponentClipAreaPosHeight(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->clip_area.h : 0;
}

static void PartsEngine_SetComponentReverseTB(int parts_no, bool reverse)
{
	struct parts *p = parts_get(parts_no);
	if (p->reverse_tb == reverse)
		return;
	p->reverse_tb = reverse;
	parts_component_dirty(p);
	parts_dirty(p);
}

static void PartsEngine_SetComponentReverseLR(int parts_no, bool reverse)
{
	struct parts *p = parts_get(parts_no);
	if (p->reverse_lr == reverse)
		return;
	p->reverse_lr = reverse;
	parts_component_dirty(p);
	parts_dirty(p);
}

static bool PartsEngine_GetComponentReverseTB(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->reverse_tb : false;
}

static bool PartsEngine_GetComponentReverseLR(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->reverse_lr : false;
}

static void PartsEngine_SetComponentTextureFilterType(int parts_no, int type)
{
	parts_get(parts_no)->texture_filter_type = type;
}

static int PartsEngine_GetComponentTextureFilterType(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->texture_filter_type : 0;
}

static void PartsEngine_SetComponentTextureAddressType(int parts_no, int type)
{
	parts_get(parts_no)->texture_address_type = type;
}

static int PartsEngine_GetComponentTextureAddressType(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->texture_address_type : 0;
}

static void PartsEngine_SetComponentMipmap(int parts_no, bool mipmap)
{
	parts_get(parts_no)->mipmap = mipmap;
}

static bool PartsEngine_IsComponentMipmap(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->mipmap : false;
}

static void PartsEngine_Parts_SetUpdateLookAtPos(int parts_no, bool enable)
{
	parts_get(parts_no)->update_look_at_pos = enable;
}

static bool PartsEngine_Parts_IsUpdateLookAtPos(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->update_look_at_pos : false;
}

static void PartsEngine_Parts_SetAnchor(int parts_no, bool top, bool bottom, bool left, bool right)
{
	struct parts *p = parts_get(parts_no);
	p->anchor_top = top;
	p->anchor_bottom = bottom;
	p->anchor_left = left;
	p->anchor_right = right;
}

static bool PartsEngine_Parts_IsAnchorTop(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->anchor_top : false;
}

static bool PartsEngine_Parts_IsAnchorBottom(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->anchor_bottom : false;
}

static bool PartsEngine_Parts_IsAnchorLeft(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->anchor_left : false;
}

static bool PartsEngine_Parts_IsAnchorRight(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->anchor_right : false;
}

static void PartsEngine_SetMotionData(int parts_no, struct string *motion_name, int time, int fps)
{
	(void)parts_no;
	(void)motion_name;
	(void)time;
	(void)fps;
}

static int PartsEngine_GetMotionBeginFrame(struct string *motion_name)
{
	(void)motion_name;
	return 0;
}

static int PartsEngine_GetMotionEndFrame(struct string *motion_name)
{
	(void)motion_name;
	return 0;
}

static struct string *PartsEngine_GetTextBoxText_v12(int parts_no)
{
	return PE_GetTextBoxText(parts_no);
}

static void PartsEngine_SelectTextBoxAll(int parts_no)
{
	(void)parts_no;
}

static void PartsEngine_SetSpinBoxValue(int parts_no, float value)
{
	(void)parts_no;
	(void)value;
}

static float PartsEngine_GetSpinBoxValue(int parts_no)
{
	(void)parts_no;
	return 0.0f;
}

static void PartsEngine_SelectSpinBoxAll(int parts_no)
{
	(void)parts_no;
}

static void PartsEngine_CancelFormClose(int parts_no)
{
	(void)parts_no;
}

static bool PartsEngine_Parts_IsResizable(int parts_no)
{
	(void)parts_no;
	return false;
}

static void PartsEngine_SetComponentVibrationPos(int parts_no, float x, float y)
{
	(void)parts_no;
	(void)x;
	(void)y;
}

static void PartsEngine_SetComponentShowEditor(int parts_no, bool show)
{
	(void)parts_no;
	(void)show;
}

static bool PartsEngine_IsComponentShowEditor(int parts_no)
{
	(void)parts_no;
	return true;
}

static bool PartsEngine_Parts_GetPartsDrag(int parts_no)
{
	return parts_get(parts_no)->draggable;
}

static void PartsEngine_Parts_SetSwipeType(int parts_no, int swipe_type)
{
	PE_Parts_SetSwipeType(parts_no, swipe_type);
}

static int PartsEngine_Parts_GetSwipeType(int parts_no)
{
	return PE_Parts_GetSwipeType(parts_no);
}

static void PartsEngine_Parts_StopSwipe(void)
{
	PE_Parts_StopSwipe();
}

static bool PartsEngine_Parts_IsPartsPixelDecide(int parts_no)
{
	return parts_get(parts_no)->pixel_decide;
}

static void PartsEngine_Parts_SetLockUpdate(int parts_no, bool lock_update)
{
	parts_get(parts_no)->lock_update = lock_update;
}

static bool PartsEngine_Parts_IsLockUpdate(int parts_no)
{
	return parts_get(parts_no)->lock_update;
}

static void PartsEngine_Parts_SetFlatBeginSeekFrame(int parts_no, int seek, int state)
{
	if (!parts_state_valid(--state))
		return;
	parts_get_flat(parts_get(parts_no), state)->begin_seek_frame = seek;
}

static void PartsEngine_WidgetSetSize(int parts_no, int w, int h)
{
	struct parts *p = parts_get(parts_no);
	parts_set_dims(p, &p->states[0].common, w, h);
	parts_component_dirty(p);
}

static void PartsEngine_SetMultiTextBoxMode(int parts_no, int mode)
{
	parts_get(parts_no)->multi_textbox_mode = mode;
}

static int PartsEngine_GetMultiTextBoxMode(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->multi_textbox_mode : 0;
}

static bool PartsEngine_Save_v14(struct page **buf, bool compress)
{
	(void)compress;
	return PE_Save(buf);
}

static void PartsEngine_WidgetStub_ii(int parts_no, int a, int b)
{
	(void)parts_no; (void)a; (void)b;
}

static void PartsEngine_WidgetStub_i(int parts_no, int a)
{
	(void)parts_no; (void)a;
}

static void PartsEngine_WidgetStub_f(int parts_no, float a)
{
	(void)parts_no; (void)a;
}

static void PartsEngine_WidgetStub_b(int parts_no, bool a)
{
	(void)parts_no; (void)a;
}

static void PartsEngine_WidgetStub_s(int parts_no, struct string *s)
{
	(void)parts_no; (void)s;
}

static void PartsEngine_WidgetStub_is(int parts_no, int idx, struct string *s)
{
	(void)parts_no; (void)idx; (void)s;
}

static void PartsEngine_WidgetStub_iii(int parts_no, int r, int g, int b)
{
	(void)parts_no; (void)r; (void)g; (void)b;
}

static void PartsEngine_WidgetStub_iiii(int parts_no, int top, int bottom, int left, int right)
{
	(void)parts_no; (void)top; (void)bottom; (void)left; (void)right;
}

static int PartsEngine_WidgetGetZero(int parts_no)
{
	(void)parts_no;
	return 0;
}

static float PartsEngine_WidgetGetFloatZero(int parts_no)
{
	(void)parts_no;
	return 0.0f;
}

static int PartsEngine_WidgetGetMinusOne(int parts_no)
{
	(void)parts_no;
	return -1;
}

static bool PartsEngine_WidgetGetFalse(int parts_no)
{
	(void)parts_no;
	return false;
}

static struct string *PartsEngine_WidgetGetEmptyString(int parts_no)
{
	(void)parts_no;
	return string_ref(&EMPTY_STRING);
}

static struct string *PartsEngine_WidgetGetItemEmptyString(int parts_no, int index)
{
	(void)parts_no; (void)index;
	return string_ref(&EMPTY_STRING);
}

static void pf_assign_out_string(struct string **out, struct string *s)
{
	if (!out) {
		if (s)
			free_string(s);
		return;
	}
	if (*out)
		free_string(*out);
	*out = s ? s : string_ref(&EMPTY_STRING);
}

static void PartsEngine_GetClickMissSoundName_v6(struct string **out)
{
	pf_assign_out_string(out, PE_GetClickMissSoundName());
}

static void PartsEngine_Parts_GetSoundName_v6(int no, struct string **out, int state)
{
	pf_assign_out_string(out, PE_Parts_GetSoundName(no, state));
}

static void PartsEngine_Parts_GetPartsFlatDataInfo_v6(int no, struct string **out, int state)
{
	pf_assign_out_string(out, PE_GetPartsFlatDataInfo(no, state));
}

#define DEFINE_V6_STR_GETTER(name) \
static void PartsEngine_##name##_v6(int no, struct string **out) \
{ \
	pf_assign_out_string(out, PE_##name(no)); \
}
DEFINE_V6_STR_GETTER(GetButtonCGName)
DEFINE_V6_STR_GETTER(GetButtonFlatName)
DEFINE_V6_STR_GETTER(GetButtonText)
DEFINE_V6_STR_GETTER(GetCheckBoxCGName)
DEFINE_V6_STR_GETTER(GetCheckBoxFlatName)
DEFINE_V6_STR_GETTER(GetCheckBoxText)
DEFINE_V6_STR_GETTER(GetVScrollbarCGName)
DEFINE_V6_STR_GETTER(GetVScrollbarFlatName)
DEFINE_V6_STR_GETTER(GetHScrollbarCGName)
DEFINE_V6_STR_GETTER(GetHScrollbarFlatName)
DEFINE_V6_STR_GETTER(GetTextBoxText)
DEFINE_V6_STR_GETTER(GetMessageWindowCGName)
DEFINE_V6_STR_GETTER(GetMessageWindowFlatName)
DEFINE_V6_STR_GETTER(GetMessageWindowText)
#undef DEFINE_V6_STR_GETTER

static void PartsEngine_GetActivityEXText_v6(struct string *name, struct string **out)
{
	pf_assign_out_string(out, PE_GetActivityEXText(name));
}

static void PartsEngine_AddMotionHGaugeRate_int(int parts_no, int begin_numerator,
		int begin_denominator, int end_numerator, int end_denominator, int begin_t, int end_t)
{
	PE_AddMotionHGaugeRate(parts_no, (float)begin_numerator, (float)begin_denominator,
			(float)end_numerator, (float)end_denominator, begin_t, end_t);
}

static void PartsEngine_AddMotionHGaugeRate_int_curve(int parts_no, int begin_numerator,
		int begin_denominator, int end_numerator, int end_denominator, int begin_t, int end_t,
		struct string *curve_name)
{
	PE_AddMotionHGaugeRate_curve(parts_no, (float)begin_numerator, (float)begin_denominator,
			(float)end_numerator, (float)end_denominator, begin_t, end_t, curve_name);
}

static void PartsEngine_AddMotionVGaugeRate_int(int parts_no, int begin_numerator,
		int begin_denominator, int end_numerator, int end_denominator, int begin_t, int end_t)
{
	PE_AddMotionVGaugeRate(parts_no, (float)begin_numerator, (float)begin_denominator,
			(float)end_numerator, (float)end_denominator, begin_t, end_t);
}

static void PartsEngine_AddMotionVGaugeRate_int_curve(int parts_no, int begin_numerator,
		int begin_denominator, int end_numerator, int end_denominator, int begin_t, int end_t,
		struct string *curve_name)
{
	PE_AddMotionVGaugeRate_curve(parts_no, (float)begin_numerator, (float)begin_denominator,
			(float)end_numerator, (float)end_denominator, begin_t, end_t, curve_name);
}

static void PartsEngine_SetModeMarkShow(int parts_no, int type, bool show)
{
	(void)parts_no;
	(void)type;
	(void)show;
}

static bool PartsEngine_IsModeMarkShow(int parts_no, int type)
{
	(void)parts_no;
	(void)type;
	return false;
}

static void PartsEngine_SetSoundNumber(int parts_no, int sound_no, int state)
{
	struct parts *p = parts_get(parts_no);
	if (state == 2)
		p->on_cursor_sound = sound_no;
	else if (state == 3)
		p->on_click_sound = sound_no;
}

static int PartsEngine_GetSoundNumber(int parts_no, int state)
{
	struct parts *p = parts_try_get(parts_no);
	if (!p)
		return -1;
	if (state == 2)
		return p->on_cursor_sound;
	if (state == 3)
		return p->on_click_sound;
	return -1;
}

static void PartsEngine_WidgetSetOnCursorSoundNumber(int parts_no, int sound_no)
{
	parts_get(parts_no)->on_cursor_sound = sound_no;
}

static void PartsEngine_WidgetSetClickSoundNumber(int parts_no, int sound_no)
{
	parts_get(parts_no)->on_click_sound = sound_no;
}

static int PartsEngine_WidgetGetOnCursorSoundNumber(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->on_cursor_sound : -1;
}

static int PartsEngine_WidgetGetClickSoundNumber(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->on_click_sound : -1;
}

static bool PartsEngine_WidgetIsDrag(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return p ? p->draggable : false;
}

static void PartsEngine_WidgetGetEmptyString_v6(int parts_no, struct string **out)
{
	(void)parts_no;
	pf_assign_out_string(out, string_ref(&EMPTY_STRING));
}

static void PartsEngine_WidgetGetItemEmptyString_v6(int parts_no, int index, struct string **out)
{
	(void)parts_no; (void)index;
	pf_assign_out_string(out, string_ref(&EMPTY_STRING));
}

static void PartsEngine_SetResetTimerByChangeInputStatus(int parts_no, bool reset)
{
	(void)parts_no; (void)reset;
}

static bool PartsEngine_GetResetTimerByChangeInputStatus(int parts_no)
{
	(void)parts_no;
	return false;
}

static void PartsEngine_Parts_SetResetTimerByChangeInputStatus(int parts_no, bool reset, int state)
{
	(void)parts_no; (void)reset; (void)state;
}

static bool PartsEngine_Parts_GetResetTimerByChangeInputStatus(int parts_no, int state)
{
	(void)parts_no; (void)state;
	return false;
}

static void *PartsEngine_ResolveSetResetTimerByChangeInputStatus(const struct ain_hll_function *f)
{
	if (f->nr_arguments == 3)
		return PartsEngine_Parts_SetResetTimerByChangeInputStatus;
	if (f->nr_arguments == 2)
		return PartsEngine_SetResetTimerByChangeInputStatus;
	return NULL;
}

static void *PartsEngine_ResolveGetResetTimerByChangeInputStatus(const struct ain_hll_function *f)
{
	if (f->nr_arguments == 2)
		return PartsEngine_Parts_GetResetTimerByChangeInputStatus;
	if (f->nr_arguments == 1)
		return PartsEngine_GetResetTimerByChangeInputStatus;
	return NULL;
}

static void PartsEngine_WidgetSetFontProperty(int parts_no, int type, int size,
		int r, int g, int b, float bold, int er, int eg, int eb, float edge)
{
	(void)parts_no; (void)type; (void)size; (void)r; (void)g; (void)b;
	(void)bold; (void)er; (void)eg; (void)eb; (void)edge;
}

static void PartsEngine_WidgetGetFontProperty(int parts_no, int *type, int *size,
		int *r, int *g, int *b, float *bold, int *er, int *eg, int *eb, float *edge)
{
	(void)parts_no;
	*type = 0; *size = 16; *r = 255; *g = 255; *b = 255;
	*bold = 0.0f; *er = 0; *eg = 0; *eb = 0; *edge = 0.0f;
}

static void PartsEngine_GetFormGroupViewSnap(int parts_no, int table_idx,
		int *top, int *bottom, int *left, int *right)
{
	(void)parts_no; (void)table_idx;
	if (top) *top = 0;
	if (bottom) *bottom = 0;
	if (left) *left = 0;
	if (right) *right = 0;
}

static int PartsEngine_NumofFormGroupVList(int parts_no, int table_idx)
{
	(void)parts_no; (void)table_idx;
	return 0;
}

static int PartsEngine_NumofFormGroupForm(int parts_no, int table_idx, int vlist_idx)
{
	(void)parts_no; (void)table_idx; (void)vlist_idx;
	return 0;
}

static int PartsEngine_GetFormGroupFormNumber(int parts_no, int table_idx, int vlist_idx, int form_idx)
{
	(void)parts_no; (void)table_idx; (void)vlist_idx; (void)form_idx;
	return 0;
}

static void PartsEngine_SetFormGroupViewSpan(int parts_no, int table_idx,
		bool top, bool bottom, bool left, bool right)
{
	(void)parts_no; (void)table_idx;
	(void)top; (void)bottom; (void)left; (void)right;
}

static void PartsEngine_AddFormGroupFormNumber(int parts_no, int table_idx, int vlist_idx, int add_form)
{
	(void)parts_no; (void)table_idx; (void)vlist_idx; (void)add_form;
}

static int PartsEngine_Parts_GetPartsCGDeform(int parts_no, int state)
{
	(void)state;
	struct parts *p = parts_try_get(parts_no);
	return p ? p->sprite_deform : 0;
}

static void PartsEngine_GetStateSurfaceArea(int parts_no, enum parts_type expected,
		int *x, int *y, int *w, int *h, int state)
{
	*x = *y = *w = *h = 0;
	if (!parts_state_valid(--state))
		return;
	struct parts *p = parts_try_get(parts_no);
	if (!p || p->states[state].type != expected)
		return;
	Rectangle r = p->states[state].common.surface_area;
	*x = r.x;
	*y = r.y;
	*w = r.w;
	*h = r.h;
}

static void PartsEngine_GetLoopCGSurfaceArea(int parts_no, int *x, int *y, int *w, int *h, int state)
{
	PartsEngine_GetStateSurfaceArea(parts_no, PARTS_ANIMATION, x, y, w, h, state);
}

static struct string *PartsEngine_GetLoopCGCGName(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return string_ref(&EMPTY_STRING);
	struct parts *p = parts_try_get(parts_no);
	if (!p || p->states[state].type != PARTS_ANIMATION || !p->states[state].anim.cg_name)
		return string_ref(&EMPTY_STRING);
	return string_dup(p->states[state].anim.cg_name);
}

static int PartsEngine_GetLoopCGStartNo(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *p = parts_try_get(parts_no);
	if (!p || p->states[state].type != PARTS_ANIMATION)
		return 0;
	return p->states[state].anim.start_no;
}

static int PartsEngine_GetLoopCGNumofCG(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *p = parts_try_get(parts_no);
	if (!p || p->states[state].type != PARTS_ANIMATION)
		return 0;
	return p->states[state].anim.nr_frames;
}

static int PartsEngine_Parts_GetLoopCGTimeParCG(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *p = parts_try_get(parts_no);
	if (!p || p->states[state].type != PARTS_ANIMATION)
		return 0;
	return p->states[state].anim.frame_time;
}

static bool PartsEngine_Parts_DeletePartsTopTextLine(int parts_no, int state)
{
	return PE_DeletePartsTopTextLine(parts_no, state);
}

static void PartsEngine_Parts_SetPartsTextHighlight(int parts_no, int start, int length,
		int r, int g, int b, int state)
{
	(void)parts_no; (void)start; (void)length; (void)r; (void)g; (void)b; (void)state;
}

static void PartsEngine_Parts_AddPartsTextHighlight(int parts_no, int start, int length,
		int r, int g, int b, int state)
{
	(void)parts_no; (void)start; (void)length; (void)r; (void)g; (void)b; (void)state;
}

static void PartsEngine_Parts_ClearPartsTextHighlight(int parts_no, int state)
{
	(void)parts_no; (void)state;
}

static void PartsEngine_Parts_SetPartsTextCountReturn(int parts_no, bool count, int state)
{
	if (!parts_state_valid(--state))
		return;
	parts_get_text(parts_get(parts_no), state)->count_return = count;
}

static bool PartsEngine_Parts_GetPartsTextCountReturn(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *p = parts_try_get(parts_no);
	if (!p || p->states[state].type != PARTS_TEXT)
		return false;
	return p->states[state].text.count_return;
}

static void PartsEngine_Parts_SetTextShowTime(int parts_no, int time, int state)
{
	if (!parts_state_valid(--state))
		return;
	parts_get_text(parts_get(parts_no), state)->show_time = time;
}

static int PartsEngine_Parts_GetTextShowTime(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *p = parts_try_get(parts_no);
	if (!p || p->states[state].type != PARTS_TEXT)
		return 0;
	return p->states[state].text.show_time;
}

static void PartsEngine_Parts_SetTextPos(int parts_no, int pos, int state)
{
	if (!parts_state_valid(--state))
		return;
	parts_get_text(parts_get(parts_no), state)->text_pos = pos;
}

static int PartsEngine_Parts_GetTextPos(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *p = parts_try_get(parts_no);
	if (!p || p->states[state].type != PARTS_TEXT)
		return 0;
	return p->states[state].text.text_pos;
}

static void PartsEngine_Parts_SetTextEnableTag(int parts_no, bool enable, int state)
{
	if (!parts_state_valid(--state))
		return;
	parts_get_text(parts_get(parts_no), state)->enable_tag = enable;
}

static bool PartsEngine_Parts_IsTextEnableTag(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *p = parts_try_get(parts_no);
	if (!p || p->states[state].type != PARTS_TEXT)
		return false;
	return p->states[state].text.enable_tag;
}

static void PartsEngine_Parts_FixText(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return;
	parts_get_text(parts_get(parts_no), state)->fixed = true;
}

static bool PartsEngine_Parts_IsFixedText(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return true;
	struct parts *p = parts_try_get(parts_no);
	if (!p || p->states[state].type != PARTS_TEXT)
		return true;
	return p->states[state].text.fixed;
}

static void PartsEngine_Parts_SetRubyFont(int parts_no, int type, int size,
		int r, int g, int b, float bold_weight, int er, int eg, int eb,
		float edge_weight, int state)
{
	if (!parts_state_valid(--state))
		return;
	struct parts_text *t = parts_get_text(parts_get(parts_no), state);
	t->ruby_ts.face = type;
	t->ruby_ts.size = size;
	t->ruby_ts.color = (SDL_Color){ r, g, b, 255 };
	t->ruby_ts.bold_width = bold_weight;
	t->ruby_ts.edge_color = (SDL_Color){ er, eg, eb, 255 };
	text_style_set_edge_width(&t->ruby_ts, edge_weight);
}

static void PartsEngine_Parts_GetRubyFont(int parts_no, int *type, int *size,
		int *r, int *g, int *b, float *bold_weight, int *er, int *eg, int *eb,
		float *edge_weight, int state)
{
	*type = 0; *size = 10; *r = *g = *b = 255; *bold_weight = 0.0f;
	*er = *eg = *eb = 0; *edge_weight = 0.0f;
	if (!parts_state_valid(--state))
		return;
	struct parts *p = parts_try_get(parts_no);
	if (!p || p->states[state].type != PARTS_TEXT)
		return;
	struct text_style *ts = &p->states[state].text.ruby_ts;
	*type = ts->face;
	*size = ts->size;
	*r = ts->color.r;
	*g = ts->color.g;
	*b = ts->color.b;
	*bold_weight = ts->bold_width;
	*er = ts->edge_color.r;
	*eg = ts->edge_color.g;
	*eb = ts->edge_color.b;
	*edge_weight = ts->edge_left;
}

static void PartsEngine_Parts_SetRubyCharSpace(int parts_no, int space, int state)
{
	if (!parts_state_valid(--state))
		return;
	parts_get_text(parts_get(parts_no), state)->ruby_char_space = space;
}

static int PartsEngine_Parts_GetRubyCharSpace(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *p = parts_try_get(parts_no);
	if (!p || p->states[state].type != PARTS_TEXT)
		return 0;
	return p->states[state].text.ruby_char_space;
}

static struct string *PartsEngine_Parts_GetHGaugeCG(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return string_ref(&EMPTY_STRING);
	struct parts *p = parts_try_get(parts_no);
	if (!p || p->states[state].type != PARTS_HGAUGE || !p->states[state].gauge.cg_name)
		return string_ref(&EMPTY_STRING);
	return string_dup(p->states[state].gauge.cg_name);
}

static bool PartsEngine_Parts_IsHGaugeReverse(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *p = parts_try_get(parts_no);
	if (!p || p->states[state].type != PARTS_HGAUGE)
		return false;
	return p->states[state].gauge.reverse;
}

static void PartsEngine_GetHGaugeSurfaceArea(int parts_no, int *x, int *y, int *w, int *h, int state)
{
	PartsEngine_GetStateSurfaceArea(parts_no, PARTS_HGAUGE, x, y, w, h, state);
}

static struct string *PartsEngine_Parts_GetVGaugeCG(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return string_ref(&EMPTY_STRING);
	struct parts *p = parts_try_get(parts_no);
	if (!p || p->states[state].type != PARTS_VGAUGE || !p->states[state].gauge.cg_name)
		return string_ref(&EMPTY_STRING);
	return string_dup(p->states[state].gauge.cg_name);
}

static bool PartsEngine_Parts_IsVGaugeReverse(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *p = parts_try_get(parts_no);
	if (!p || p->states[state].type != PARTS_VGAUGE)
		return false;
	return p->states[state].gauge.reverse;
}

static void PartsEngine_GetVGaugeSurfaceArea(int parts_no, int *x, int *y, int *w, int *h, int state)
{
	PartsEngine_GetStateSurfaceArea(parts_no, PARTS_VGAUGE, x, y, w, h, state);
}

static bool PartsEngine_IsNumerarlFullPitch(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *p = parts_try_get(parts_no);
	if (!p || p->states[state].type != PARTS_NUMERAL)
		return false;
	return p->states[state].num.full_pitch;
}

static bool PartsEngine_Parts_SetPartsRectangleDetectionSurfaceArea(int parts_no,
		int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *p = parts_get(parts_no);
	if (p->states[state].type != PARTS_RECT_DETECTION)
		parts_state_reset(&p->states[state], PARTS_RECT_DETECTION);
	parts_set_surface_area(p, &p->states[state].common, x, y, w, h);
	return true;
}

static void PartsEngine_GetRectangleDetectionSurfaceArea(int parts_no,
		int *x, int *y, int *w, int *h, int state)
{
	PartsEngine_GetStateSurfaceArea(parts_no, PARTS_RECT_DETECTION, x, y, w, h, state);
}

static struct string *PartsEngine_Parts_GetPartsCGDetectionCGName(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return string_ref(&EMPTY_STRING);
	struct parts *p = parts_try_get(parts_no);
	if (!p || p->states[state].type != PARTS_RECT_DETECTION || !p->states[state].rect.cg_name)
		return string_ref(&EMPTY_STRING);
	return string_dup(p->states[state].rect.cg_name);
}

static bool PartsEngine_Parts_SetPartsCGDetectionSurfaceArea(int parts_no,
		int x, int y, int w, int h, int state)
{
	return PartsEngine_Parts_SetPartsRectangleDetectionSurfaceArea(parts_no, x, y, w, h, state);
}

static void PartsEngine_GetCGDetectionSurfaceArea(int parts_no,
		int *x, int *y, int *w, int *h, int state)
{
	PartsEngine_GetStateSurfaceArea(parts_no, PARTS_RECT_DETECTION, x, y, w, h, state);
}

static void PartsEngine_GetConstructionSurfaceArea(int parts_no,
		int *x, int *y, int *w, int *h, int state)
{
	PartsEngine_GetStateSurfaceArea(parts_no, PARTS_CONSTRUCTION_PROCESS, x, y, w, h, state);
}

static bool PartsEngine_SaveParts(int parts_no, struct page **save_data_buffer)
{
	return PE_SaveParts(parts_no, save_data_buffer);
}

static bool PartsEngine_LoadParts(int parts_no, struct page **save_data_buffer)
{
	return PE_LoadParts(parts_no, save_data_buffer);
}

static void PartsEngine_Debug_GetSpriteRect(int parts_no,
		int *top, int *bottom, int *left, int *right, int state)
{
	if (top) *top = 0;
	if (bottom) *bottom = 0;
	if (left) *left = 0;
	if (right) *right = 0;
	if (!parts_state_valid(--state))
		return;
	struct parts *p = parts_try_get(parts_no);
	if (!p)
		return;
	Rectangle r = p->states[state].common.hitbox;
	if (left) *left = r.x;
	if (top) *top = r.y;
	if (right) *right = r.x + r.w;
	if (bottom) *bottom = r.y + r.h;
}

static struct parts_movie *try_get_parts_movie(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return NULL;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_MOVIE)
		return NULL;
	return &parts->states[state].movie;
}

static bool PartsEngine_CreatePartsMovie(int parts_no, struct string *file_name,
		int sound_id, int sound_group, int red, int green, int blue, int state)
{
	int width = config.view_width > 0 ? config.view_width : 1280;
	int height = config.view_height > 0 ? config.view_height : 720;
	struct movie_context *mc = NULL;
	if (file_name && file_name->size > 0) {
		mc = movie_load_apeg(file_name->text, sound_id, sound_group);
		if (mc) {
			int w = movie_get_width(mc);
			int h = movie_get_height(mc);
			if (w > 0 && h > 0) {
				width = w;
				height = h;
			}
		} else {
			char *path = gamedir_path_icase(file_name->text);
			if (path) {
				FILE *fp = file_open_utf8(path, "rb");
				if (fp) {
					uint8_t hdr[24];
					if (fread(hdr, 1, sizeof(hdr), fp) == sizeof(hdr)
							&& !memcmp(hdr, "APEG", 4)) {
						int w = LittleEndian_getW(hdr, 12);
						int h = LittleEndian_getW(hdr, 14);
						if (w > 0 && h > 0) {
							width = w;
							height = h;
						}
					}
					fclose(fp);
				}
				free(path);
			}
		}
	}
	int r = (red == 16 && green == 128 && blue == 128) ? 0 : red;
	int g = (red == 16 && green == 128 && blue == 128) ? 0 : green;
	int b = (red == 16 && green == 128 && blue == 128) ? 0 : blue;
	if (!PE_init_parts_movie(parts_no, width, height, r, g, b, state)) {
		movie_free(mc);
		return false;
	}
	if (mc) {
		struct parts_movie *movie = try_get_parts_movie(parts_no, state);
		if (movie) {
			movie->mc = mc;
			if (movie->sprite_no >= 0) {
				movie_draw(mc, sact_try_get_sprite(movie->sprite_no));
				struct parts *parts = parts_try_get(parts_no);
				if (parts)
					parts_dirty(parts);
			}
		} else {
			movie_free(mc);
		}
	}
	return true;
}

static bool PartsEngine_ReleasePartsMovie(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_MOVIE)
		return false;
	parts_state_reset(&parts->states[state], PARTS_UNINITIALIZED);
	parts_dirty(parts);
	return true;
}

static bool PartsEngine_PlayPartsMovie(int parts_no, int msec, int state)
{
	struct parts_movie *movie = try_get_parts_movie(parts_no, state);
	if (!movie || movie->sprite_no < 0)
		return false;
	if (movie->mc) {
		movie_play_at(movie->mc, msec);
		movie_draw(movie->mc, sact_try_get_sprite(movie->sprite_no));
		struct parts *parts = parts_try_get(parts_no);
		if (parts)
			parts_dirty(parts);
	}
	return true;
}

static void PartsEngine_SetMovieTime(int parts_no, int msec, int state)
{
	struct parts_movie *movie = try_get_parts_movie(parts_no, state);
	if (!movie || !movie->mc)
		return;
	movie_set_time(movie->mc, msec);
	if (movie->sprite_no >= 0) {
		movie_draw(movie->mc, sact_try_get_sprite(movie->sprite_no));
		struct parts *parts = parts_try_get(parts_no);
		if (parts)
			parts_dirty(parts);
	}
}

static bool PartsEngine_IsEndPartsMovie(int parts_no, int state)
{
	struct parts_movie *movie = try_get_parts_movie(parts_no, state);
	if (!movie || !movie->mc)
		return true;
	return movie_is_end(movie->mc);
}

static int PartsEngine_GetPartsMovieEndTime(int parts_no, int state)
{
	struct parts_movie *movie = try_get_parts_movie(parts_no, state);
	if (!movie || !movie->mc)
		return 0;
	return movie_get_end_time(movie->mc);
}

static int PartsEngine_GetPartsMovieCurrentTime(int parts_no, int state)
{
	struct parts_movie *movie = try_get_parts_movie(parts_no, state);
	if (!movie || !movie->mc)
		return 0;
	return movie_get_position(movie->mc);
}

// Rance 9: return type changed from int to float
static float PartsEngine_Parts_GetComponentPosX(int parts_no)
{
	return PE_GetComponentPosX(parts_no);
}

static float PartsEngine_GetComponentPosY(int parts_no)
{
	return PE_GetComponentPosY(parts_no);
}

// Rance 9: X/Y coordinate types changed from int to float
static void PartsEngine_AddComponentMotionPos(int parts_no, float begin_x, float begin_y,
		float end_x, float end_y, int begin_t, int end_t,
		struct string *curve_name)
{
	PE_AddMotionPos_curve(parts_no, begin_x, begin_y, end_x, end_y,
			begin_t, end_t, curve_name);
}

static void PartsEngine_add_construction_process(union vm_value *ints,
		union vm_value *floats, union vm_value *strings, int a2, int blur)
{
	int parts_no    = ints[0].i;
	int state       = ints[1].i;
	int command     = ints[2].i;
	int interp_type = ints[3].i;
	int sx          = ints[4].i;
	int sy          = ints[5].i;
	int sw          = ints[6].i;
	int sh          = ints[7].i;
	int dx          = ints[8].i;
	int dy          = ints[9].i;
	int dw          = ints[12].i;
	int dh          = ints[13].i;
	int r           = ints[14].i;
	int g           = ints[15].i;
	int b           = ints[16].i;
	int a           = ints[17].i;
	int r2          = ints[18].i;
	int g2          = ints[19].i;
	int b2          = ints[20].i;
	int char_space  = ints[21].i;
	int line_space  = ints[22].i;
	int font_type   = ints[23].i;
	int font_size   = ints[24].i;
	int font_r      = ints[25].i;
	int font_g      = ints[26].i;
	int font_b      = ints[27].i;
	int edge_r      = ints[28].i;
	int edge_g      = ints[29].i;
	int edge_b      = ints[30].i;
	int full_size   = ints[31].i;
	float bold_weight = floats[0].f;
	float edge_weight = floats[1].f;
	struct string *text    = heap_get_string(strings[0].i);
	struct string *cg_name = heap_get_string(strings[1].i);

	switch (command) {
	case 0:  // CASConstructionProcess::SetCreate
		PE_AddCreateToPartsConstructionProcess(parts_no, dw, dh, state);
		break;
	case 1:  // CASConstructionProcess::SetCreatePixelOnly
		PE_AddCreatePixelOnlyToPartsConstructionProcess(parts_no, dw, dh, state);
		break;
	case 2:  // CASConstructionProcess::SetCreateCG
		PE_AddCreateCGToProcess(parts_no, cg_name, state);
		break;
	case 3:  // CASConstructionProcess::SetFill
		PE_AddFillToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, state);
		parts_cp_set_last_fill_full_size(parts_no, state, !!full_size);
		break;
	case 4:  // CASConstructionProcess::SetFillAlphaColor
		PE_AddFillAlphaColorToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, a, state);
		parts_cp_set_last_fill_full_size(parts_no, state, !!full_size);
		break;
	case 5:  // CASConstructionProcess::SetFillAMap
		PE_AddFillAMapToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, a, state);
		parts_cp_set_last_fill_full_size(parts_no, state, !!full_size);
		break;
	case 6:  // CASConstructionProcess::SetFillWithAlpha
		PE_AddFillWithAlphaToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, a, state);
		parts_cp_set_last_fill_full_size(parts_no, state, !!full_size);
		break;
	case 7:  // CASConstructionProcess::SetDrawText
		PE_AddDrawTextToPartsConstructionProcess(parts_no,
				dx, dy, text, font_type, font_size,
				font_r, font_g, font_b, bold_weight,
				edge_r, edge_g, edge_b, edge_weight,
				char_space, line_space, state);
		break;
	case 8:  // CASConstructionProcess::SetCopyText
		PE_AddCopyTextToPartsConstructionProcess(parts_no,
				dx, dy, text, font_type, font_size,
				font_r, font_g, font_b, bold_weight,
				edge_r, edge_g, edge_b, edge_weight,
				char_space, line_space, state);
		break;
	case 9:  // CASConstructionProcess::SetFillGradationHorizon
		PE_AddFillGradationHorizonToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, r2, g2, b2, state);
		parts_cp_set_last_fill_full_size(parts_no, state, !!full_size);
		break;
	case 10:  // CASConstructionProcess::SetDrawRect
		PE_AddDrawRectToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, state);
		parts_cp_set_last_fill_full_size(parts_no, state, !!full_size);
		break;
	case 11:  // CASConstructionProcess::SetCutCGBlend (equal scale)
		PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, dw, dh,
				interp_type, state);
		break;
	case 12:  // CASConstructionProcess::SetCutCGCopy (equal scale)
		PE_AddCopyCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, dw, dh,
				interp_type, state);
		break;
	case 13:  // CASConstructionProcess::SetCutCGScaleBlend
		PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, sw, sh,
				interp_type, state);
		break;
	case 14:  // CASConstructionProcess::SetCutCGScaleCopy
		PE_AddCopyCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, sw, sh,
				interp_type, state);
		break;
	case 15:  // CASConstructionProcess::SetGrayFilter
		PE_AddGrayFilterToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, full_size, state);
		break;
	case 16:  // CASConstructionProcess::SetAddFilter
		PE_AddAddFilterToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, full_size, state);
		break;
	case 17:  // CASConstructionProcess::SetMulFilter
		PE_AddMulFilterToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, full_size, state);
		break;
	case 18:  // CASConstructionProcess::SetDrawLine
		PE_AddDrawLineToPartsConstructionProcess(parts_no, dx, dy, ints[10].i, ints[11].i,
				r, g, b, a, state);
		break;
	case 19:  // CASConstructionProcess::SetAlphaBlendCutCG (v12)
		PE_AddCutCGAlphaBlendToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, sw ? sw : dw, sh ? sh : dh,
				interp_type, false, state);
		break;
	case 20:  // CASConstructionProcess::SetAlphaBlendCutCGScale (v12)
		PE_AddCutCGAlphaBlendToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, sw, sh,
				interp_type, true, state);
		break;
	case 21:  // CASConstructionProcess::SetOnlyAlphaCutCG (v12)
		PE_AddCutCGOnlyAlphaToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, sw ? sw : dw, sh ? sh : dh,
				interp_type, false, state);
		break;
	case 22:  // CASConstructionProcess::SetOnlyAlphaCutCGScale (v12)
		PE_AddCutCGOnlyAlphaToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, sw, sh,
				interp_type, true, state);
		break;
	case 23:  // CASConstructionProcess::SetAlphaBlendText (v12)
		PE_AddAlphaBlendTextToPartsConstructionProcess(parts_no,
				dx, dy, text, font_type, font_size,
				font_r, font_g, font_b, bold_weight,
				edge_r, edge_g, edge_b, edge_weight,
				char_space, line_space, state);
		break;
	case 24:  // CASConstructionProcess::SetOnlyAlphaText (v12)
		PE_AddOnlyAlphaTextToPartsConstructionProcess(parts_no,
				dx, dy, text, font_type, font_size,
				font_r, font_g, font_b, bold_weight,
				edge_r, edge_g, edge_b, edge_weight,
				char_space, line_space, state);
		break;
	case 25:  // CASConstructionProcess::SetMulAMapGradationHorizon (v12)
		PE_AddMulAMapGradationHorizonToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, a, a2, state);
		parts_cp_set_last_fill_full_size(parts_no, state, !!full_size);
		break;
	case 26:  // CASConstructionProcess::SetMulAMapGradationVertical (v12)
		PE_AddMulAMapGradationVerticalToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, a, a2, state);
		parts_cp_set_last_fill_full_size(parts_no, state, !!full_size);
		break;
	case 27:  // CASConstructionProcess::SetHBlurFilter (v12)
		PE_AddHBlurFilterToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, blur, full_size, state);
		break;
	case 28:  // CASConstructionProcess::SetVBlurFilter (v12)
		PE_AddVBlurFilterToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, blur, full_size, state);
		break;
	case 29:  // CASConstructionProcess::SetBlendCG (v12)
		PE_AddCGBlendToPartsConstructionProcess(parts_no, cg_name, dx, dy, state);
		break;
	default:
		WARNING("AddConstructProcess: unknown command %d", command);
		break;
	}
}

// Extended records insert A2 before the text fields and may append Blur.
static void PartsEngine_add_construction_process_extended(int parts_no, int state,
		const union vm_value *src, union vm_value *floats, union vm_value *strings, int blur)
{
	union vm_value ints[32];
	ints[0].i = parts_no;
	ints[1].i = state;
	memcpy(ints + 2, src, 19 * sizeof(*src));
	memcpy(ints + 21, src + 20, 11 * sizeof(*src));
	PartsEngine_add_construction_process(ints, floats, strings, src[19].i, blur);
}

static void PartsEngine_AddPartsConstructionProcess(int parts_no, struct page **array_int,
		struct page **array_float, struct page **array_string, int state)
{
	if (!array_int || !*array_int || (*array_int)->nr_vars != 32
			|| !array_float || !*array_float || (*array_float)->nr_vars != 2
			|| !array_string || !*array_string || (*array_string)->nr_vars != 2)
		VM_ERROR("Invalid v12 construction process arrays");
	union vm_value *src = (*array_int)->values;
	int command = src[0].i;
	if (command < 0 || command > 29)
		VM_ERROR("Unimplemented v12 construction process command %d", command);
	PartsEngine_add_construction_process_extended(parts_no, state, src,
			(*array_float)->values, (*array_string)->values, src[31].i);
}

static void PartsEngine_AddPartsConstructionProcess_v14(int parts_no, struct page **array_int,
		struct page **array_float, struct page **array_string, struct page **array_pos, int state)
{
	if (!array_int || !*array_int || (*array_int)->nr_vars < 32
			|| !array_float || !*array_float || (*array_float)->nr_vars != 2
			|| !array_string || !*array_string || (*array_string)->nr_vars != 2)
		VM_ERROR("Invalid v14 construction process arrays");
	union vm_value *src = (*array_int)->values;
	int nr_int = (*array_int)->nr_vars;
	int command = src[0].i;
	if (command >= 0 && command <= 29) {
		PartsEngine_add_construction_process_extended(parts_no, state, src,
				(*array_float)->values, (*array_string)->values, src[31].i);
		return;
	}
	if (command >= 30 && command <= 132) {
		struct parts_cp_v14 v = { 0 };
		v.command = command;
		v.interp_type = src[1].i;
		v.sx = src[2].i; v.sy = src[3].i; v.sw = src[4].i; v.sh = src[5].i;
		v.dx = src[6].i; v.dy = src[7].i; v.dx2 = src[8].i; v.dy2 = src[9].i;
		v.dw = src[10].i; v.dh = src[11].i;
		v.r = src[12].i; v.g = src[13].i; v.b = src[14].i; v.a = src[15].i;
		v.r2 = src[16].i; v.g2 = src[17].i; v.b2 = src[18].i; v.a2 = src[19].i;
		v.full_size = !!src[30].i;
		v.blur = src[31].i;
		if (nr_int > 32) v.radius_x = src[32].i;
		if (nr_int > 33) v.radius_y = src[33].i;
		if (nr_int > 34) v.line_width = src[34].i;
		if (nr_int > 35) v.round_edge = !!src[35].i;
		if (nr_int > 36) v.round_corner = !!src[36].i;
		if (nr_int > 37) v.angle = src[37].i;
		if (nr_int > 38) v.start_angle = src[38].i;
		if (nr_int > 39) v.sweep_angle = src[39].i;
		v.cg_name = heap_get_string((*array_string)->values[1].i);
		int nr_coords = (array_pos && *array_pos) ? (*array_pos)->nr_vars : 0;
		int *coords = NULL;
		if (nr_coords > 0) {
			coords = xcalloc(nr_coords, sizeof(int));
			for (int i = 0; i < nr_coords; i++)
				coords[i] = (*array_pos)->values[i].i;
		}
		v.nr_coords = nr_coords;
		v.coords = coords;
		parts_cp_add_v14(parts_no, &v, state);
		free(coords);
		return;
	}
	WARNING("Unimplemented v14 construction process command %d", command);
}

static int PartsEngine_Parts_GetPartsConstructionProcessCount(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_CONSTRUCTION_PROCESS)
		return 0;
	int count = 0;
	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &parts->states[state].cproc.ops, entry)
		count++;
	return count;
}

static void PartsEngine_get_construction_process_common(int parts_no, int index,
		struct page **array_int, int int_len, struct page **array_float,
		struct page **array_string, struct page **array_pos, int state)
{
	if (!parts_state_valid(--state) || index < 0)
		return;
	if (!array_int || !array_float || !array_string)
		return;
	if (!*array_int || (*array_int)->nr_vars != int_len) {
		union vm_value dim = { .i = int_len };
		*array_int = realloc_array(*array_int, 1, &dim, AIN_ARRAY_INT, -1, false);
	}
	if (!*array_float || (*array_float)->nr_vars != 2) {
		union vm_value dim = { .i = 2 };
		*array_float = realloc_array(*array_float, 1, &dim, AIN_ARRAY_FLOAT, -1, false);
	}
	if (!*array_string || (*array_string)->nr_vars != 2) {
		union vm_value dim = { .i = 2 };
		*array_string = realloc_array(*array_string, 1, &dim, AIN_ARRAY_STRING, -1, false);
	}
	if (array_pos && *array_pos) {
		union vm_value dim = { .i = 0 };
		*array_pos = realloc_array(*array_pos, 1, &dim, AIN_ARRAY_INT, -1, false);
	}
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_CONSTRUCTION_PROCESS)
		return;
	struct parts_cp_op *op;
	int i = 0;
	TAILQ_FOREACH(op, &parts->states[state].cproc.ops, entry) {
		if (i == index)
			break;
		i++;
	}
	if (!op)
		return;

	union vm_value *ints = (*array_int)->values;
	union vm_value *floats = (*array_float)->values;
	union vm_value *strings = (*array_string)->values;
	for (int k = 0; k < int_len; k++) ints[k].i = 0;
	floats[0].f = 0.0f;
	floats[1].f = 0.0f;
	free_string(heap[strings[0].i].s);
	heap[strings[0].i].s = string_ref(&EMPTY_STRING);
	free_string(heap[strings[1].i].s);
	heap[strings[1].i].s = string_ref(&EMPTY_STRING);

	switch (op->type) {
	case PARTS_CP_CREATE:
		ints[0].i = 0;
		ints[10].i = op->create.w;
		ints[11].i = op->create.h;
		break;
	case PARTS_CP_CREATE_PIXEL_ONLY:
		ints[0].i = 1;
		ints[10].i = op->create.w;
		ints[11].i = op->create.h;
		break;
	case PARTS_CP_CG:
		ints[0].i = 2;
		free_string(heap[strings[1].i].s);
		heap[strings[1].i].s = string_dup(op->cg.name);
		break;
	case PARTS_CP_BLEND_CG:
		ints[0].i = 29;
		ints[6].i = op->cg.x;
		ints[7].i = op->cg.y;
		free_string(heap[strings[1].i].s);
		heap[strings[1].i].s = string_dup(op->cg.name);
		break;
	case PARTS_CP_FILL:
	case PARTS_CP_FILL_ALPHA_COLOR:
	case PARTS_CP_FILL_AMAP:
	case PARTS_CP_FILL_WITH_ALPHA:
	case PARTS_CP_FILL_GRADATION_HORIZON:
	case PARTS_CP_DRAW_RECT:
	case PARTS_CP_MUL_AMAP_GRADATION_HORIZON:
	case PARTS_CP_MUL_AMAP_GRADATION_VERTICAL:
		ints[0].i = op->type == PARTS_CP_FILL ? 3
			: op->type == PARTS_CP_FILL_ALPHA_COLOR ? 4
			: op->type == PARTS_CP_FILL_AMAP ? 5
			: op->type == PARTS_CP_FILL_WITH_ALPHA ? 6
			: op->type == PARTS_CP_FILL_GRADATION_HORIZON ? 9
			: op->type == PARTS_CP_DRAW_RECT ? 10
			: op->type == PARTS_CP_MUL_AMAP_GRADATION_HORIZON ? 25 : 26;
		ints[6].i = op->fill.x;
		ints[7].i = op->fill.y;
		ints[10].i = op->fill.w;
		ints[11].i = op->fill.h;
		ints[12].i = op->fill.r;
		ints[13].i = op->fill.g;
		ints[14].i = op->fill.b;
		ints[15].i = op->fill.a;
		ints[16].i = op->fill.r2;
		ints[17].i = op->fill.g2;
		ints[18].i = op->fill.b2;
		ints[19].i = op->fill.a2;
		ints[30].i = op->fill.full_size;
		break;
	case PARTS_CP_DRAW_TEXT:
	case PARTS_CP_COPY_TEXT:
	case PARTS_CP_ALPHA_BLEND_TEXT:
	case PARTS_CP_ONLY_ALPHA_TEXT:
		ints[0].i = op->type == PARTS_CP_DRAW_TEXT ? 7
			: op->type == PARTS_CP_COPY_TEXT ? 8
			: op->type == PARTS_CP_ALPHA_BLEND_TEXT ? 23 : 24;
		ints[6].i = op->text.x;
		ints[7].i = op->text.y;
		ints[20].i = (int)op->text.style.font_spacing;
		ints[21].i = op->text.line_space;
		ints[22].i = op->text.style.face;
		ints[23].i = (int)op->text.style.size;
		ints[24].i = op->text.style.color.r;
		ints[25].i = op->text.style.color.g;
		ints[26].i = op->text.style.color.b;
		ints[27].i = op->text.style.edge_color.r;
		ints[28].i = op->text.style.edge_color.g;
		ints[29].i = op->text.style.edge_color.b;
		floats[0].f = op->text.style.bold_width;
		floats[1].f = op->text.style.edge_left;
		free_string(heap[strings[0].i].s);
		heap[strings[0].i].s = string_dup(op->text.text);
		break;
	case PARTS_CP_DRAW_CUT_CG:
	case PARTS_CP_COPY_CUT_CG:
	case PARTS_CP_ALPHA_BLEND_CUT_CG:
	case PARTS_CP_ONLY_ALPHA_CUT_CG: {
		bool scaled = op->cut_cg.scale;
		ints[0].i = op->type == PARTS_CP_DRAW_CUT_CG ? (scaled ? 13 : 11)
			: op->type == PARTS_CP_COPY_CUT_CG ? (scaled ? 14 : 12)
			: op->type == PARTS_CP_ALPHA_BLEND_CUT_CG ? (scaled ? 20 : 19)
			: (scaled ? 22 : 21);
		ints[1].i = op->cut_cg.interp_type;
		ints[2].i = op->cut_cg.sx;
		ints[3].i = op->cut_cg.sy;
		ints[4].i = op->cut_cg.sw;
		ints[5].i = op->cut_cg.sh;
		ints[6].i = op->cut_cg.dx;
		ints[7].i = op->cut_cg.dy;
		ints[10].i = op->cut_cg.dw;
		ints[11].i = op->cut_cg.dh;
		free_string(heap[strings[1].i].s);
		heap[strings[1].i].s = string_dup(op->cut_cg.cg_name);
		break;
	}
	case PARTS_CP_GRAY_FILTER:
		ints[0].i = 15;
		ints[6].i = op->filter.x;
		ints[7].i = op->filter.y;
		ints[10].i = op->filter.w;
		ints[11].i = op->filter.h;
		ints[30].i = op->filter.full_size;
		break;
	case PARTS_CP_ADD_FILTER:
	case PARTS_CP_MUL_FILTER:
		ints[0].i = op->type == PARTS_CP_ADD_FILTER ? 16 : 17;
		ints[6].i = op->filter.x;
		ints[7].i = op->filter.y;
		ints[10].i = op->filter.w;
		ints[11].i = op->filter.h;
		ints[12].i = op->filter.r;
		ints[13].i = op->filter.g;
		ints[14].i = op->filter.b;
		ints[30].i = op->filter.full_size;
		break;
	case PARTS_CP_HBLUR_FILTER:
	case PARTS_CP_VBLUR_FILTER:
		ints[0].i = op->type == PARTS_CP_HBLUR_FILTER ? 27 : 28;
		ints[6].i = op->filter.x;
		ints[7].i = op->filter.y;
		ints[10].i = op->filter.w;
		ints[11].i = op->filter.h;
		ints[30].i = op->filter.full_size;
		ints[31].i = op->filter.blur;
		break;
	case PARTS_CP_DRAW_LINE:
		ints[0].i = 18;
		ints[6].i = op->line.x0;
		ints[7].i = op->line.y0;
		ints[8].i = op->line.x1;
		ints[9].i = op->line.y1;
		ints[12].i = op->line.r;
		ints[13].i = op->line.g;
		ints[14].i = op->line.b;
		ints[15].i = op->line.a;
		break;
	case PARTS_CP_V14: {
		const struct parts_cp_v14 *v = &op->v14;
		ints[0].i = v->command;
		ints[1].i = v->interp_type;
		ints[2].i = v->sx; ints[3].i = v->sy; ints[4].i = v->sw; ints[5].i = v->sh;
		ints[6].i = v->dx; ints[7].i = v->dy; ints[8].i = v->dx2; ints[9].i = v->dy2;
		ints[10].i = v->dw; ints[11].i = v->dh;
		ints[12].i = v->r; ints[13].i = v->g; ints[14].i = v->b; ints[15].i = v->a;
		ints[16].i = v->r2; ints[17].i = v->g2; ints[18].i = v->b2; ints[19].i = v->a2;
		ints[30].i = v->full_size;
		ints[31].i = v->blur;
		if (int_len >= 40) {
			ints[32].i = v->radius_x;
			ints[33].i = v->radius_y;
			ints[34].i = v->line_width;
			ints[35].i = v->round_edge;
			ints[36].i = v->round_corner;
			ints[37].i = v->angle;
			ints[38].i = v->start_angle;
			ints[39].i = v->sweep_angle;
		}
		if (v->cg_name) {
			free_string(heap[strings[1].i].s);
			heap[strings[1].i].s = string_dup(v->cg_name);
		}
		if (array_pos && v->nr_coords > 0 && v->coords) {
			union vm_value dim = { .i = v->nr_coords };
			*array_pos = realloc_array(*array_pos, 1, &dim, AIN_ARRAY_INT, -1, false);
			for (int k = 0; k < v->nr_coords; k++)
				(*array_pos)->values[k].i = v->coords[k];
		}
		break;
	}
	}
}

static void PartsEngine_GetPartsConstructionProcess(int parts_no, int index,
		struct page **array_int, struct page **array_float,
		struct page **array_string, int state)
{
	PartsEngine_get_construction_process_common(parts_no, index, array_int, 32,
			array_float, array_string, NULL, state);
}

static void PartsEngine_GetPartsConstructionProcess_v14(int parts_no, int index,
		struct page **array_int, struct page **array_float,
		struct page **array_string, struct page **array_pos, int state)
{
	PartsEngine_get_construction_process_common(parts_no, index, array_int, 40,
			array_float, array_string, array_pos, state);
}

static bool PartsEngine_get_construction_process_v6_arrays(union vm_value *ints, int nr_ints,
		struct page **array_float, struct page **array_string)
{
	struct page *tmp_int = NULL;
	PartsEngine_GetPartsConstructionProcess(ints[0].i, ints[1].i, &tmp_int,
			array_float, array_string, ints[2].i);
	if (tmp_int && tmp_int->nr_vars >= 32) {
		if (nr_ints == 34 || nr_ints == 35) {
			memcpy(ints + 3, tmp_int->values, (nr_ints - 3) * sizeof(*ints));
		} else {
			memcpy(ints + 3, tmp_int->values, 19 * sizeof(*ints));
			memcpy(ints + 22, tmp_int->values + 20, 11 * sizeof(*ints));
		}
	}
	if (tmp_int) {
		delete_page_vars(tmp_int);
		free_page(tmp_int);
	}
	return true;
}

static void pf_set_str(union vm_value *strings, int slot, struct string *s)
{
	free_string(heap[strings[slot].i].s);
	heap[strings[slot].i].s = s ? s : string_ref(&EMPTY_STRING);
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
	int nr_floats = (array_float && *array_float) ? (*array_float)->nr_vars : 0;
	union vm_value *floats = nr_floats ? (*array_float)->values : NULL;
	int nr_strings = (array_string && *array_string) ? (*array_string)->nr_vars : 0;
	union vm_value *strings = nr_strings ? (*array_string)->values : NULL;

	// AIN v12+, Rance 9, and Beat Angel Escalayer Reboot omitted IDs 3..5
	// (GetControllerIndex/ID/Length) from PartsFunc, shifting all subsequent IDs down by 3.
	if (ain && (ain->version >= 12 || game_rance9 || game_escalayer_reboot) && func_id >= 3)
		func_id += 3;

#define REQUIRE_INTS(n) \
	if (nr_ints != (n)) VM_ERROR("Invalid arguments for PartsFunc %d: expected %d ints, got %d", func_id, (n), nr_ints)
#define REQUIRE_FLOATS(n) \
	if (nr_floats < (n)) VM_ERROR("Invalid arguments for PartsFunc %d: expected %d floats, got %d", func_id, (n), nr_floats)
#define REQUIRE_STRINGS(n) \
	if (nr_strings < (n)) VM_ERROR("Invalid arguments for PartsFunc %d: expected %d strings, got %d", func_id, (n), nr_strings)

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
	case 3:  // int GetControllerIndex(int id)
		REQUIRE_INTS(2);
		ints[1].i = PE_GetControllerIndex(ints[0].i);
		return 1;
	case 4:  // int GetControllerID(int index)
		REQUIRE_INTS(2);
		ints[1].i = PE_GetControllerID(ints[0].i);
		return 1;
	case 5:  // int GetControllerLength()
		REQUIRE_INTS(1);
		ints[0].i = PE_GetControllerLength();
		return 1;
	case 6:  // void PauseMotion(bool pause)
		REQUIRE_INTS(1);
		PE_PauseMotion(!!ints[0].i);
		return 1;
	case 7:  // void SetWantSave(int parts_no, bool want_save)
		REQUIRE_INTS(2);
		PE_parts_set_want_save(ints[0].i, !!ints[1].i);
		return 1;
	case 8:  // bool IsWantSave(int parts_no)
		REQUIRE_INTS(2);
		ints[1].i = PE_parts_get_want_save(ints[0].i);
		return 1;
	case 9:  // bool SaveThumbnail(string filename, int reduction_factor)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(1);
		ints[1].i = PE_save_thumbnail(heap_get_string(strings[0].i), ints[0].i);
		return 1;
	case 10:  // bool IsExistActivity(string name)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(1);
		ints[0].i = PE_IsExistActivity(heap_get_string(strings[0].i));
		return 1;
	case 11:  // bool CreateActivity(string name)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(1);
		ints[0].i = PE_CreateActivity(heap_get_string(strings[0].i));
		return 1;
	case 12:  // bool AddActivityParts(string name, string part_name, int no)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(2);
		ints[1].i = PE_AddActivityParts(heap_get_string(strings[0].i), heap_get_string(strings[1].i), ints[0].i);
		return 1;
	case 13:  // bool RemoveActivityParts(string name, string part_name)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(2);
		ints[0].i = PE_RemoveActivityParts(heap_get_string(strings[0].i), heap_get_string(strings[1].i));
		return 1;
	case 14:  // bool RemoveAllActivityParts(string name)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(1);
		ints[0].i = PE_RemoveAllActivityParts(heap_get_string(strings[0].i));
		return 1;
	case 15:  // int NumofActivityParts(string name)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(1);
		ints[0].i = PE_NumofActivityParts(heap_get_string(strings[0].i));
		return 1;
	case 16: {  // bool GetActivityParts(int index, string name, ref string part_name, ref int no)
		REQUIRE_INTS(3);
		REQUIRE_STRINGS(2);
		struct string *pn = string_ref(&EMPTY_STRING);
		ints[2].i = PE_GetActivityParts(ints[0].i, heap_get_string(strings[0].i), &pn, &ints[1].i);
		pf_set_str(strings, 1, pn);
		return 1;
	}
	case 17:  // bool IsExistActivityPartsByName(string name, string part_name)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(2);
		ints[0].i = PE_IsExistActivityPartsByName(heap_get_string(strings[0].i), heap_get_string(strings[1].i));
		return 1;
	case 18:  // bool IsExistActivityPartsByNumber(string name, int no)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(1);
		ints[1].i = PE_IsExistActivityPartsByNumber(heap_get_string(strings[0].i), ints[0].i);
		return 1;
	case 19:  // int GetActivityPartsNumber(string name, string part_name)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(2);
		ints[0].i = PE_GetActivityPartsNumber(heap_get_string(strings[0].i), heap_get_string(strings[1].i));
		return 1;
	case 20:  // string GetActivityPartsName(string name, int no)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(2);
		pf_set_str(strings, 1, PE_GetActivityPartsName(heap_get_string(strings[0].i), ints[0].i));
		return 1;
	case 21:  // bool ReadActivityFile(string name, string filename)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(2);
		ints[0].i = PE_ReadActivityFile(heap_get_string(strings[0].i), heap_get_string(strings[1].i), false);
		return 1;
	case 22:  // bool WriteActivityFile(string name, string filename)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(2);
		ints[0].i = PartsEngine_WriteActivityFile(heap_get_string(strings[0].i), heap_get_string(strings[1].i));
		return 1;
	case 23:  // bool IsExistActivityFile(string filename)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(1);
		ints[0].i = PE_IsExistActivityFile(heap_get_string(strings[0].i));
		return 1;
	case 24:  // void SetActivityIntentData(string name, string src, string dest, int type)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(3);
		PE_SetActivityIntentData(heap_get_string(strings[0].i), heap_get_string(strings[1].i),
				heap_get_string(strings[2].i), ints[0].i);
		return 1;
	case 25:  // void AddActivityIntentDataDestination(string name, string src, string dest)
		REQUIRE_STRINGS(3);
		PE_AddActivityIntentDataDestination(heap_get_string(strings[0].i),
				heap_get_string(strings[1].i), heap_get_string(strings[2].i));
		return 1;
	case 26:  // bool IsExistActivityIntentData(string name, string src)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(2);
		ints[0].i = PE_IsExistActivityIntentData(heap_get_string(strings[0].i), heap_get_string(strings[1].i));
		return 1;
	case 27:  // int NumofActivityIntentDataDestination(string name, string src)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(2);
		ints[0].i = PE_NumofActivityIntentDataDestination(heap_get_string(strings[0].i), heap_get_string(strings[1].i));
		return 1;
	case 28:  // string GetActivityIntentDataDestination(int index, string name, string src)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(3);
		pf_set_str(strings, 2, PE_GetActivityIntentDataDestination(ints[0].i,
				heap_get_string(strings[0].i), heap_get_string(strings[1].i)));
		return 1;
	case 29:  // int GetActivityIntentDataType(string name, string src)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(2);
		ints[0].i = PE_GetActivityIntentDataType(heap_get_string(strings[0].i), heap_get_string(strings[1].i));
		return 1;
	case 30:  // void SetActivityFunc(string name, string part, int state, string id, string func)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(4);
		PE_SetActivityFunc(heap_get_string(strings[0].i), heap_get_string(strings[1].i), ints[0].i,
				heap_get_string(strings[2].i), heap_get_string(strings[3].i));
		return 1;
	case 31:  // string GetActivityFunc(string name, string part, int state, string id)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(4);
		pf_set_str(strings, 3, PE_GetActivityFunc(heap_get_string(strings[0].i),
				heap_get_string(strings[1].i), ints[0].i, heap_get_string(strings[2].i)));
		return 1;
	case 32:  // int NumofActivityFunc(string name, string part, int state)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(2);
		ints[1].i = PE_NumofActivityFunc(heap_get_string(strings[0].i), heap_get_string(strings[1].i), ints[0].i);
		return 1;
	case 33: {  // void GetActivityFuncPair(int idx, string name, string part, int state, ref string id, ref string fn)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(4);
		struct string *oid = string_ref(&EMPTY_STRING);
		struct string *ofn = string_ref(&EMPTY_STRING);
		PE_GetActivityFuncPair(ints[0].i, heap_get_string(strings[0].i), heap_get_string(strings[1].i),
				ints[1].i, &oid, &ofn);
		pf_set_str(strings, 2, oid);
		pf_set_str(strings, 3, ofn);
		return 1;
	}
	case 34:  // void AddActivityCloseParts(string name, string part)
		REQUIRE_STRINGS(2);
		PE_AddActivityCloseParts(heap_get_string(strings[0].i), heap_get_string(strings[1].i));
		return 1;
	case 35:  // void RemoveActivityCloseParts(string name, string part)
		REQUIRE_STRINGS(2);
		PE_RemoveActivityCloseParts(heap_get_string(strings[0].i), heap_get_string(strings[1].i));
		return 1;
	case 36:  // void RemoveAllActivityCloseParts(string name)
		REQUIRE_STRINGS(1);
		PE_RemoveAllActivityCloseParts(heap_get_string(strings[0].i));
		return 1;
	case 37:  // bool IsExistActivityCloseParts(string name, string part)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(2);
		ints[0].i = PE_IsExistActivityCloseParts(heap_get_string(strings[0].i), heap_get_string(strings[1].i));
		return 1;
	case 38:  // void SetActivityEndKey(string name, int key)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(1);
		PE_SetActivityEndKey(heap_get_string(strings[0].i), ints[0].i);
		return 1;
	case 39:  // void EraseActivityEndKey(string name, int key)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(1);
		PE_EraseActivityEndKey(heap_get_string(strings[0].i), ints[0].i);
		return 1;
	case 40:  // bool IsExistActivityEndKey(string name, int key)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(1);
		ints[1].i = PE_IsExistActivityEndKey(heap_get_string(strings[0].i), ints[0].i);
		return 1;
	case 41:  // int NumofActivityEndKey(string name)
		REQUIRE_INTS(1);
		REQUIRE_STRINGS(1);
		ints[0].i = PE_NumofActivityEndKey(heap_get_string(strings[0].i));
		return 1;
	case 42:  // int GetActivityEndKey(string name, int index)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(1);
		ints[1].i = PE_GetActivityEndKey(heap_get_string(strings[0].i), ints[0].i);
		return 1;
	case 43:  // float PARTS_GetAbsoluteX(int number)
		REQUIRE_INTS(1);
		REQUIRE_FLOATS(1);
		floats[0].f = PE_parts_get_absolute_x(ints[0].i);
		return 1;
	case 44:  // float PARTS_GetAbsoluteY(int number)
		REQUIRE_INTS(1);
		REQUIRE_FLOATS(1);
		floats[0].f = PE_parts_get_absolute_y(ints[0].i);
		return 1;
	case 45:  // int PARTS_GetAbsoluteZ(int number)
		REQUIRE_INTS(2);
		ints[1].i = PE_parts_get_absolute_z(ints[0].i);
		return 1;
	case 46:  // int PARTS_GetAbsoluteMaxZ(int number)
		REQUIRE_INTS(2);
		ints[1].i = PartsEngine_GetComponentAbsoluteMaxPosZ(ints[0].i);
		return 1;
	case 47:  // void GetComponentAbsoluteSquarePos(int no, ref float x0..y3, int state)
		REQUIRE_INTS(2);
		REQUIRE_FLOATS(8);
		PE_GetComponentAbsoluteSquarePos(ints[0].i,
				&floats[0].f, &floats[1].f, &floats[2].f, &floats[3].f,
				&floats[4].f, &floats[5].f, &floats[6].f, &floats[7].f, ints[1].i);
		return 1;
	case 48:  // void PARTS_SetLockInputState(int number, bool lock)
		REQUIRE_INTS(2);
		PE_parts_set_lock_input_state(ints[0].i, !!ints[1].i);
		return 1;
	case 49:  // bool PARTS_IsLockInputState(int number)
		REQUIRE_INTS(2);
		ints[1].i = PartsEngine_IsLockInputState(ints[0].i);
		return 1;
	case 50:  // void SetComponentScrollPosXLinkNumber(int no, int link)
		REQUIRE_INTS(2);
		PE_SetComponentScrollPosXLinkNumber(ints[0].i, ints[1].i);
		return 1;
	case 51:  // void SetComponentScrollPosYLinkNumber(int no, int link)
		REQUIRE_INTS(2);
		PE_SetComponentScrollPosYLinkNumber(ints[0].i, ints[1].i);
		return 1;
	case 52:  // int GetComponentScrollPosXLinkNumber(int no)
		REQUIRE_INTS(2);
		ints[1].i = PE_GetComponentScrollPosXLinkNumber(ints[0].i);
		return 1;
	case 53:  // int GetComponentScrollPosYLinkNumber(int no)
		REQUIRE_INTS(2);
		ints[1].i = PE_GetComponentScrollPosYLinkNumber(ints[0].i);
		return 1;
	case 54:  // void SetComponentScrollAlphaLinkNumber(int no, int link)
		REQUIRE_INTS(2);
		PE_SetComponentScrollAlphaLinkNumber(ints[0].i, ints[1].i);
		return 1;
	case 55:  // int GetComponentScrollAlphaLinkNumber(int no)
		REQUIRE_INTS(2);
		ints[1].i = PE_GetComponentScrollAlphaLinkNumber(ints[0].i);
		return 1;
	case 56:  // void SetComponentCheckBoxShowLinkNumber(int no, int cb_no)
		REQUIRE_INTS(2);
		PE_SetComponentCheckBoxShowLinkNumber(ints[0].i, ints[1].i);
		return 1;
	case 57:  // int GetComponentCheckBoxShowLinkNumber(int no)
		REQUIRE_INTS(2);
		ints[1].i = PE_GetComponentCheckBoxShowLinkNumber(ints[0].i);
		return 1;
	case 58:  // bool IsExistChild(int no, int child)
		REQUIRE_INTS(3);
		ints[2].i = PE_IsExistChild(ints[0].i, ints[1].i);
		return 1;
	case 59:  // void ClearChild(int no)
		REQUIRE_INTS(1);
		PE_ClearChild(ints[0].i);
		return 1;
	case 60:  // void AddChild(int number, int child_number)
		REQUIRE_INTS(2);
		PE_AddChild(ints[0].i, ints[1].i);
		return 1;
	case 61:  // void InsertChild(int no, int index, int child)
		REQUIRE_INTS(3);
		PE_InsertChild(ints[0].i, ints[1].i, ints[2].i);
		return 1;
	case 62:  // void RemoveChild(int no, int child)
		REQUIRE_INTS(2);
		PE_RemoveChild(ints[0].i, ints[1].i);
		return 1;
	case 63:  // int NumofChild(int no)
		REQUIRE_INTS(2);
		ints[1].i = PE_NumofChild(ints[0].i);
		return 1;
	case 64:  // int GetChildIndex(int no, int child)
		REQUIRE_INTS(3);
		ints[2].i = PE_GetChildIndex(ints[0].i, ints[1].i);
		return 1;
	case 65:  // int GetChild(int no, int index)
		REQUIRE_INTS(3);
		ints[2].i = PE_GetChild(ints[0].i, ints[1].i);
		return 1;
	case 66:  // void SetButtonTextOriginPosMode(int no, int mode)
		REQUIRE_INTS(2);
		PE_SetButtonTextOriginPosMode(ints[0].i, ints[1].i);
		return 1;
	case 67:  // int GetButtonTextOriginPosMode(int no)
		REQUIRE_INTS(2);
		ints[1].i = PE_GetButtonTextOriginPosMode(ints[0].i);
		return 1;
	case 68:  // void SetButtonCharSpace(int no, int space)
		REQUIRE_INTS(2);
		PE_SetButtonCharSpace(ints[0].i, ints[1].i);
		return 1;
	case 69:  // int GetButtonCharSpace(int no)
		REQUIRE_INTS(2);
		ints[1].i = PE_GetButtonCharSpace(ints[0].i);
		return 1;
	case 70:  // void SetButtonLineSpace(int no, int space)
		REQUIRE_INTS(2);
		PE_SetButtonLineSpace(ints[0].i, ints[1].i);
		return 1;
	case 71:  // int GetButtonLineSpace(int no)
		REQUIRE_INTS(2);
		ints[1].i = PE_GetButtonLineSpace(ints[0].i);
		return 1;
	case 72:  // int GetCheckBoxButtonWidth(int no)
		REQUIRE_INTS(2);
		ints[1].i = PE_GetCheckBoxButtonWidth(ints[0].i);
		return 1;
	case 73:  // int GetCheckBoxButtonHeight(int no)
		REQUIRE_INTS(2);
		ints[1].i = PE_GetCheckBoxButtonHeight(ints[0].i);
		return 1;
	case 74:  // void SetCheckBoxTextOriginPosMode(int no, int mode)
		REQUIRE_INTS(2);
		PE_SetCheckBoxTextOriginPosMode(ints[0].i, ints[1].i);
		return 1;
	case 75:  // int GetCheckBoxTextOriginPosMode(int no)
		REQUIRE_INTS(2);
		ints[1].i = PE_GetCheckBoxTextOriginPosMode(ints[0].i);
		return 1;
	case 76:  // void SetCheckBoxButtonMode(int no, bool mode)
		REQUIRE_INTS(2);
		PE_SetCheckBoxButtonMode(ints[0].i, !!ints[1].i);
		return 1;
	case 77:  // bool IsCheckBoxButtonMode(int no)
		REQUIRE_INTS(2);
		ints[1].i = PE_IsCheckBoxButtonMode(ints[0].i);
		return 1;
	case 78:  // void SetCheckBoxCharSpace(int no, int space)
		REQUIRE_INTS(2);
		PE_SetCheckBoxCharSpace(ints[0].i, ints[1].i);
		return 1;
	case 79:  // int GetCheckBoxCharSpace(int no)
		REQUIRE_INTS(2);
		ints[1].i = PE_GetCheckBoxCharSpace(ints[0].i);
		return 1;
	case 80:  // void SetCheckBoxLineSpace(int no, int space)
		REQUIRE_INTS(2);
		PE_SetCheckBoxLineSpace(ints[0].i, ints[1].i);
		return 1;
	case 81:  // int GetCheckBoxLineSpace(int no)
		REQUIRE_INTS(2);
		ints[1].i = PE_GetCheckBoxLineSpace(ints[0].i);
		return 1;
	case 82:  // void SetTextBoxReadOnly(int no, bool ro)
		REQUIRE_INTS(2);
		PE_SetTextBoxReadOnly(ints[0].i, !!ints[1].i);
		return 1;
	case 83:  // bool IsTextBoxReadOnly(int no)
		REQUIRE_INTS(2);
		ints[1].i = PE_IsTextBoxReadOnly(ints[0].i);
		return 1;
	case 94:  // void SetLayoutBoxPadding(int parts_no, int top, int bottom, int left, int right)
		REQUIRE_INTS(5);
		PE_set_layoutbox_padding(ints[0].i, ints[1].i, ints[2].i, ints[3].i, ints[4].i);
		return 1;
	case 95:  // int GetLayoutBoxPaddingTop(int parts_no)
		REQUIRE_INTS(2);
		ints[1].i = PE_get_layoutbox_padding_top(ints[0].i);
		return 1;
	case 96:  // int GetLayoutBoxPaddingBottom(int parts_no)
		REQUIRE_INTS(2);
		ints[1].i = PE_get_layoutbox_padding_bottom(ints[0].i);
		return 1;
	case 97:  // int GetLayoutBoxPaddingLeft(int parts_no)
		REQUIRE_INTS(2);
		ints[1].i = PE_get_layoutbox_padding_left(ints[0].i);
		return 1;
	case 98:  // int GetLayoutBoxPaddingRight(int parts_no)
		REQUIRE_INTS(2);
		ints[1].i = PE_get_layoutbox_padding_right(ints[0].i);
		return 1;
	case 99:  // bool IsRadioButtonBoxExistGUI(int no, int child)
		REQUIRE_INTS(3);
		ints[2].i = PE_IsRadioButtonBoxExistGUI(ints[0].i, ints[1].i);
		return 1;
	case 100:  // void ClearRadioButtonBoxChild(int no)
		REQUIRE_INTS(1);
		PE_ClearRadioButtonBoxChild(ints[0].i);
		return 1;
	case 101:  // void AddRadioButtonBoxChild(int no, int child)
		REQUIRE_INTS(2);
		PE_AddRadioButtonBoxChild(ints[0].i, ints[1].i);
		return 1;
	case 102:  // void RemoveRadioButtonBoxChild(int no, int child)
		REQUIRE_INTS(2);
		PE_RemoveRadioButtonBoxChild(ints[0].i, ints[1].i);
		return 1;
	case 103:  // int NumofRadioButtonBoxChild(int no)
		REQUIRE_INTS(2);
		ints[1].i = PE_NumofRadioButtonBoxChild(ints[0].i);
		return 1;
	case 104:  // int GetRadioButtonBoxChild(int no, int index)
		REQUIRE_INTS(3);
		ints[2].i = PE_GetRadioButtonBoxChild(ints[0].i, ints[1].i);
		return 1;
	case 105:  // int Parts_GetPartsCGDeform(int parts_no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PartsEngine_Parts_GetPartsCGDeform(ints[0].i, ints[1].i);
		return 1;
	case 106:  // void GetPartsCGSurfaceArea(int parts_no, int *x, int *y, int *w, int *h, int state)
		REQUIRE_INTS(6);
		PE_GetPartsCGSurfaceArea(ints[0].i, &ints[1].i, &ints[2].i, &ints[3].i, &ints[4].i, ints[5].i);
		return 1;
	case 107:  // void GetLoopCGSurfaceArea(int no, int *x, int *y, int *w, int *h, int state)
		REQUIRE_INTS(6);
		PartsEngine_GetLoopCGSurfaceArea(ints[0].i, &ints[1].i, &ints[2].i, &ints[3].i, &ints[4].i, ints[5].i);
		return 1;
	case 108:  // string GetLoopCGCGName(int no, int state)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(1);
		pf_set_str(strings, 0, PartsEngine_GetLoopCGCGName(ints[0].i, ints[1].i));
		return 1;
	case 109:  // int GetLoopCGStartNo(int no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PartsEngine_GetLoopCGStartNo(ints[0].i, ints[1].i);
		return 1;
	case 110:  // int GetLoopCGNumofCG(int no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PartsEngine_GetLoopCGNumofCG(ints[0].i, ints[1].i);
		return 1;
	case 111:  // int Parts_GetLoopCGTimeParCG(int no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PartsEngine_Parts_GetLoopCGTimeParCG(ints[0].i, ints[1].i);
		return 1;
	case 112:  // string GetTextPartsText(int no, int state)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(1);
		pf_set_str(strings, 0, PE_GetTextPartsText(ints[0].i, ints[1].i));
		return 1;
	case 113:  // void GetPartsTextSurfaceArea(int no, int *x, int *y, int *w, int *h, int state)
		REQUIRE_INTS(6);
		PE_GetPartsTextSurfaceArea(ints[0].i, &ints[1].i, &ints[2].i, &ints[3].i, &ints[4].i, ints[5].i);
		return 1;
	case 114:  // void GetPartsTextFontProperty(int no, ref int..., ref float..., int state)
		REQUIRE_INTS(10);
		REQUIRE_FLOATS(2);
		PE_GetPartsTextFontProperty(ints[0].i, &ints[1].i, &ints[2].i, &ints[3].i, &ints[4].i, &ints[5].i,
				&floats[0].f, &ints[6].i, &ints[7].i, &ints[8].i, &floats[1].f, ints[9].i);
		return 1;
	case 115:  // int Parts_GetTextCharSpace(int no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PE_GetTextCharSpace(ints[0].i, ints[1].i);
		return 1;
	case 116:  // int Parts_GetTextLineSpace(int no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PE_GetTextLineSpace(ints[0].i, ints[1].i);
		return 1;
	case 117:  // string Parts_GetHGaugeCG(int no, int state)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(1);
		pf_set_str(strings, 0, PartsEngine_Parts_GetHGaugeCG(ints[0].i, ints[1].i));
		return 1;
	case 118:  // float Parts_GetHGaugeNumerator(int no, int state)
		REQUIRE_INTS(2);
		REQUIRE_FLOATS(1);
		floats[0].f = PE_GetHGaugeNumerator(ints[0].i, ints[1].i);
		return 1;
	case 119:  // float Parts_GetHGaugeDenominator(int no, int state)
		REQUIRE_INTS(2);
		REQUIRE_FLOATS(1);
		floats[0].f = PE_GetHGaugeDenominator(ints[0].i, ints[1].i);
		return 1;
	case 120:  // void GetHGaugeSurfaceArea(int no, int *x, int *y, int *w, int *h, int state)
		REQUIRE_INTS(6);
		PartsEngine_GetHGaugeSurfaceArea(ints[0].i, &ints[1].i, &ints[2].i, &ints[3].i, &ints[4].i, ints[5].i);
		return 1;
	case 121:  // string Parts_GetVGaugeCG(int no, int state)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(1);
		pf_set_str(strings, 0, PartsEngine_Parts_GetVGaugeCG(ints[0].i, ints[1].i));
		return 1;
	case 122:  // float Parts_GetVGaugeNumerator(int no, int state)
		REQUIRE_INTS(2);
		REQUIRE_FLOATS(1);
		floats[0].f = PE_GetVGaugeNumerator(ints[0].i, ints[1].i);
		return 1;
	case 123:  // float Parts_GetVGaugeDenominator(int no, int state)
		REQUIRE_INTS(2);
		REQUIRE_FLOATS(1);
		floats[0].f = PE_GetVGaugeDenominator(ints[0].i, ints[1].i);
		return 1;
	case 124:  // void GetVGaugeSurfaceArea(int no, int *x, int *y, int *w, int *h, int state)
		REQUIRE_INTS(6);
		PartsEngine_GetVGaugeSurfaceArea(ints[0].i, &ints[1].i, &ints[2].i, &ints[3].i, &ints[4].i, ints[5].i);
		return 1;
	case 125:  // string Parts_GetNumeralCGName(int no, int state)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(1);
		pf_set_str(strings, 0, PE_GetNumeralCGName(ints[0].i, ints[1].i));
		return 1;
	case 126:  // void GetNumeralCGNumberWidthList(int no, ref int w0..comma, int state)
		REQUIRE_INTS(14);
		PE_GetNumeralCGNumberWidthList(ints[0].i,
				&ints[1].i, &ints[2].i, &ints[3].i, &ints[4].i, &ints[5].i, &ints[6].i,
				&ints[7].i, &ints[8].i, &ints[9].i, &ints[10].i, &ints[11].i, &ints[12].i,
				ints[13].i);
		return 1;
	case 127:  // void SetNumeralFont(int no, int face..eb, float bold, float edge, int state)
		REQUIRE_INTS(10);
		REQUIRE_FLOATS(2);
		PE_SetNumeralFont(ints[0].i, ints[1].i, ints[2].i, ints[3].i, ints[4].i, ints[5].i,
				floats[0].f, ints[6].i, ints[7].i, ints[8].i, floats[1].f, ints[9].i);
		return 1;
	case 128:  // void GetNumeralFont(int no, ref int..., ref float..., int state)
		REQUIRE_INTS(10);
		REQUIRE_FLOATS(2);
		PE_GetNumeralFont(ints[0].i, &ints[1].i, &ints[2].i, &ints[3].i, &ints[4].i, &ints[5].i,
				&floats[0].f, &ints[6].i, &ints[7].i, &ints[8].i, &floats[1].f, ints[9].i);
		return 1;
	case 129:  // void SetNumeralFullPitch(int no, bool full, int state)
		REQUIRE_INTS(3);
		PE_SetNumeralFullPitch(ints[0].i, !!ints[1].i, ints[2].i);
		return 1;
	case 130:  // bool IsNumerarlFullPitch(int no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PartsEngine_IsNumerarlFullPitch(ints[0].i, ints[1].i);
		return 1;
	case 131:  // int Parts_GetNumeralNumber(int no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PE_GetNumeralNumber(ints[0].i, ints[1].i);
		return 1;
	case 132:  // bool Parts_IsNumeralShowComma(int no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PE_IsNumeralShowComma(ints[0].i, ints[1].i);
		return 1;
	case 133:  // int Parts_GetNumeralSpace(int no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PE_GetNumeralSpace(ints[0].i, ints[1].i);
		return 1;
	case 134:  // int GetNumeralLength(int no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PE_GetNumeralLength(ints[0].i, ints[1].i);
		return 1;
	case 135:  // void GetNumeralSurfaceArea(int no, int *x, int *y, int *w, int *h, int state)
		REQUIRE_INTS(6);
		PE_GetNumeralSurfaceArea(ints[0].i, &ints[1].i, &ints[2].i, &ints[3].i, &ints[4].i, ints[5].i);
		return 1;
	case 136:  // void SetNumeralShowType(int no, int type, int state)
		REQUIRE_INTS(3);
		PE_SetNumeralShowType(ints[0].i, ints[1].i, ints[2].i);
		return 1;
	case 137:  // int Parts_GetNumeralCGType(int no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PE_GetNumeralCGType(ints[0].i, ints[1].i);
		return 1;
	case 138:  // void SetNumeralShowPadding(int no, bool show, int state)
		REQUIRE_INTS(3);
		PE_SetNumeralShowPadding(ints[0].i, !!ints[1].i, ints[2].i);
		return 1;
	case 139:  // bool IsNumeralShowPadding(int no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PE_IsNumeralShowPadding(ints[0].i, ints[1].i);
		return 1;
	case 140:  // void GetRectangleDetectionSurfaceArea(int no, int *x, int *y, int *w, int *h, int state)
		REQUIRE_INTS(6);
		PartsEngine_GetRectangleDetectionSurfaceArea(ints[0].i, &ints[1].i, &ints[2].i, &ints[3].i, &ints[4].i, ints[5].i);
		return 1;
	case 141:  // void SetRectangleDetectionPoint(int no, int x0..y3, int state)
		REQUIRE_INTS(10);
		PE_SetRectangleDetectionPoint(ints[0].i,
				ints[1].i, ints[2].i, ints[3].i, ints[4].i,
				ints[5].i, ints[6].i, ints[7].i, ints[8].i, ints[9].i);
		return 1;
	case 142:  // void GetRectangleDetectionPoint(int no, int *x0..*y3, int state)
		REQUIRE_INTS(10);
		PE_GetRectangleDetectionPoint(ints[0].i,
				&ints[1].i, &ints[2].i, &ints[3].i, &ints[4].i,
				&ints[5].i, &ints[6].i, &ints[7].i, &ints[8].i, ints[9].i);
		return 1;
	case 143:  // string Parts_GetPartsCGDetectionCGName(int no, int state)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(1);
		pf_set_str(strings, 0, PartsEngine_Parts_GetPartsCGDetectionCGName(ints[0].i, ints[1].i));
		return 1;
	case 144:  // void GetCGDetectionSurfaceArea(int no, int *x, int *y, int *w, int *h, int state)
		REQUIRE_INTS(6);
		PartsEngine_GetCGDetectionSurfaceArea(ints[0].i, &ints[1].i, &ints[2].i, &ints[3].i, &ints[4].i, ints[5].i);
		return 1;
	case 145:  // string Parts_GetPartsFlatName(int no, int state)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(1);
		pf_set_str(strings, 0, PE_GetPartsFlatName(ints[0].i, ints[1].i));
		return 1;
	case 146:  // bool Parts_IsPartsFlatStop(int no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PE_IsPartsFlatStop(ints[0].i, ints[1].i);
		return 1;
	case 147:  // void GetFlatSurfaceArea(int no, int *x, int *y, int *w, int *h, int state)
		REQUIRE_INTS(6);
		PE_GetFlatSurfaceArea(ints[0].i, &ints[1].i, &ints[2].i, &ints[3].i, &ints[4].i, ints[5].i);
		return 1;
	case 148:  // int NumofFlatCG(int no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PE_NumofFlatCG(ints[0].i, ints[1].i);
		return 1;
	case 149:  // string GetFlatCGName(int no, int index, int state)
		REQUIRE_INTS(3);
		REQUIRE_STRINGS(1);
		pf_set_str(strings, 0, PE_GetFlatCGName(ints[0].i, ints[1].i, ints[2].i));
		return 1;
	case 150:  // string GetProxyFlatCGName(int no, string old_name, int state)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(2);
		pf_set_str(strings, 1, PE_GetProxyFlatCGName(ints[0].i, heap_get_string(strings[0].i), ints[1].i));
		return 1;
	case 151:  // void ReturnChangeFlatCG(int no, string old_name, int state)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(1);
		PE_ReturnChangeFlatCG(ints[0].i, heap_get_string(strings[0].i), ints[1].i);
		return 1;
	case 152:  // void ClearChangeFlatCG(int no, int state)
		REQUIRE_INTS(2);
		PE_ClearChangeFlatCG(ints[0].i, ints[1].i);
		return 1;
	case 153:  // int NumofFlatSound(int no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PE_NumofFlatSound(ints[0].i, ints[1].i);
		return 1;
	case 154:  // string GetFlatSoundName(int no, int index, int state)
		REQUIRE_INTS(3);
		REQUIRE_STRINGS(1);
		pf_set_str(strings, 0, PE_GetFlatSoundName(ints[0].i, ints[1].i, ints[2].i));
		return 1;
	case 155:  // string GetProxyFlatSound(int no, string old_name, int state)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(2);
		pf_set_str(strings, 1, PE_GetProxyFlatSound(ints[0].i, heap_get_string(strings[0].i), ints[1].i));
		return 1;
	case 156:  // void ReturnChangeFlatSound(int no, string old_name, int state)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(1);
		PE_ReturnChangeFlatSound(ints[0].i, heap_get_string(strings[0].i), ints[1].i);
		return 1;
	case 157:  // void ClearChangeFlatSound(int no, int state)
		REQUIRE_INTS(2);
		PE_ClearChangeFlatSound(ints[0].i, ints[1].i);
		return 1;
	case 158:  // void SetFlatFPSRate(int no, float rate, int state)
		REQUIRE_INTS(2);
		REQUIRE_FLOATS(1);
		PE_SetFlatFPSRate(ints[0].i, floats[0].f, ints[1].i);
		return 1;
	case 159:  // float GetFlatFPSRate(int no, int state)
		REQUIRE_INTS(2);
		REQUIRE_FLOATS(1);
		floats[0].f = PE_GetFlatFPSRate(ints[0].i, ints[1].i);
		return 1;
	case 160:  // int GetFlatFPS(int no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PE_GetFlatFPS(ints[0].i, ints[1].i);
		return 1;
	case 161:  // int Parts_GetPartsConstructionProcessCount(int no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PartsEngine_Parts_GetPartsConstructionProcessCount(ints[0].i, ints[1].i);
		return 1;
	case 162:  // AddConstructProcess(ArrayInt[32], ArrayFloat[2], ArrayString[2])
		REQUIRE_INTS(32);
		REQUIRE_FLOATS(2);
		REQUIRE_STRINGS(2);
		PartsEngine_add_construction_process(ints, floats, strings, 0, 0);
		return 1;
	case 163:  // GetConstructProcess(ArrayInt[33], ArrayFloat[2], ArrayString[2])
		REQUIRE_INTS(33);
		REQUIRE_FLOATS(2);
		REQUIRE_STRINGS(2);
		return PartsEngine_get_construction_process_v6_arrays(ints, 33, array_float, array_string) ? 1 : 0;
	case 164:  // void GetConstructionSurfaceArea(int no, int *x, int *y, int *w, int *h, int state)
		REQUIRE_INTS(6);
		PartsEngine_GetConstructionSurfaceArea(ints[0].i, &ints[1].i, &ints[2].i, &ints[3].i, &ints[4].i, ints[5].i);
		return 1;
	case 165:  // bool InitPartsMovie(int parts_no, int width, int height, int bg_r, int bg_g, int bg_b, int state)
		REQUIRE_INTS(8);
		ints[7].i = PE_init_parts_movie(ints[0].i, ints[1].i, ints[2].i, ints[3].i, ints[4].i, ints[5].i, ints[6].i);
		return 1;
	case 166:  // int GetMovieSprite(int parts_no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PE_get_movie_sprite(ints[0].i, ints[1].i);
		return 1;
	default:
		WARNING("Unknown func_id: %d", func_id);
		return 0;
	}
#undef REQUIRE_INTS
#undef REQUIRE_FLOATS
#undef REQUIRE_STRINGS
}

static void PartsEngine_GetActivityPartsName_v6(struct string **out, struct string *name, int no)
{
	pf_assign_out_string(out, PE_GetActivityPartsName(name, no));
}

static bool PartsEngine_LoadActivityEXText_v6(struct string *name, struct string *text)
{
	PE_SetActivityEXText(name, text);
	return true;
}

static bool PartsEngine_PlayPartsMovie_v6(int parts_no, int state)
{
	return PartsEngine_PlayPartsMovie(parts_no, 0, state);
}

static void PartsEngine_AddPartsConstructionProcess_v6(struct page **array_int,
		struct page **array_float, struct page **array_string)
{
	if (!array_int || !*array_int || (*array_int)->nr_vars < 32
			|| !array_float || !*array_float || (*array_float)->nr_vars < 2
			|| !array_string || !*array_string || (*array_string)->nr_vars < 2)
		VM_ERROR("Invalid v6 construction process arrays");
	union vm_value *ints = (*array_int)->values;
	int nr_ints = (*array_int)->nr_vars;
	if (nr_ints == 33 || nr_ints == 34) {
		PartsEngine_add_construction_process_extended(ints[0].i, ints[1].i, ints + 2,
				(*array_float)->values, (*array_string)->values, nr_ints == 34 ? ints[33].i : 0);
	} else {
		PartsEngine_add_construction_process(ints,
				(*array_float)->values, (*array_string)->values, 0, 0);
	}
}

static void PartsEngine_GetPartsConstructionProcess_v6(struct page **array_int,
		struct page **array_float, struct page **array_string)
{
	if (!array_int || !*array_int || (*array_int)->nr_vars < 33
			|| !array_float || !*array_float || (*array_float)->nr_vars < 2
			|| !array_string || !*array_string || (*array_string)->nr_vars < 2)
		VM_ERROR("Invalid v6 construction process arrays");
	PartsEngine_get_construction_process_v6_arrays((*array_int)->values, (*array_int)->nr_vars,
			array_float, array_string);
}

#define DEFINE_V6_OUT0_STATE_GETTER(name, fn) \
static void PartsEngine_##name##_v6(struct string **out, int no, int state) \
{ \
	pf_assign_out_string(out, fn(no, state)); \
}
DEFINE_V6_OUT0_STATE_GETTER(GetLoopCGCGName, PartsEngine_GetLoopCGCGName)
DEFINE_V6_OUT0_STATE_GETTER(GetTextPartsText, PE_GetTextPartsText)
DEFINE_V6_OUT0_STATE_GETTER(Parts_GetHGaugeCG, PartsEngine_Parts_GetHGaugeCG)
DEFINE_V6_OUT0_STATE_GETTER(Parts_GetVGaugeCG, PartsEngine_Parts_GetVGaugeCG)
DEFINE_V6_OUT0_STATE_GETTER(Parts_GetNumeralCGName, PE_GetNumeralCGName)
DEFINE_V6_OUT0_STATE_GETTER(Parts_GetPartsCGDetectionCGName, PartsEngine_Parts_GetPartsCGDetectionCGName)
DEFINE_V6_OUT0_STATE_GETTER(Parts_GetPartsFlatName, PE_GetPartsFlatName)
#undef DEFINE_V6_OUT0_STATE_GETTER

static void PartsEngine_GetFlatCGName_v6(struct string **out, int no, int index, int state)
{
	pf_assign_out_string(out, PE_GetFlatCGName(no, index, state));
}

static void PartsEngine_GetProxyFlatCGName_v6(struct string **out, int no,
		struct string *old_name, int state)
{
	pf_assign_out_string(out, PE_GetProxyFlatCGName(no, old_name, state));
}

static void PartsEngine_GetFlatSoundName_v6(struct string **out, int no, int index, int state)
{
	pf_assign_out_string(out, PE_GetFlatSoundName(no, index, state));
}

static void PartsEngine_GetProxyFlatSound_v6(struct string **out, int no,
		struct string *old_name, int state)
{
	pf_assign_out_string(out, PE_GetProxyFlatSound(no, old_name, state));
}

static void PartsEngine_PreLink(void);

HLL_LIBRARY(PartsEngine,
	    HLL_EXPORT(SetComponentScrollPosXLinkNumber, PE_SetComponentScrollPosXLinkNumber),
	    HLL_EXPORT(SetComponentScrollPosYLinkNumber, PE_SetComponentScrollPosYLinkNumber),
	    HLL_EXPORT(GetComponentScrollPosXLinkNumber, PE_GetComponentScrollPosXLinkNumber),
	    HLL_EXPORT(GetComponentScrollPosYLinkNumber, PE_GetComponentScrollPosYLinkNumber),
	    HLL_EXPORT(SetComponentScrollAlphaLinkNumber, PE_SetComponentScrollAlphaLinkNumber),
	    HLL_EXPORT(GetComponentScrollAlphaLinkNumber, PE_GetComponentScrollAlphaLinkNumber),
	    HLL_EXPORT(SetVScrollbarOverrideUpCGName, PE_SetVScrollbarOverrideUpCGName),
	    HLL_EXPORT(GetVScrollbarOverrideUpCGName, PE_GetVScrollbarOverrideUpCGName),
	    HLL_EXPORT(SetVScrollbarOverrideDownCGName, PE_SetVScrollbarOverrideDownCGName),
	    HLL_EXPORT(GetVScrollbarOverrideDownCGName, PE_GetVScrollbarOverrideDownCGName),
	    HLL_EXPORT(SetVScrollbarOverrideBarCGName, PE_SetVScrollbarOverrideBarCGName),
	    HLL_EXPORT(GetVScrollbarOverrideBarCGName, PE_GetVScrollbarOverrideBarCGName),
	    HLL_EXPORT(SetVScrollbarOverrideBGCGName, PE_SetVScrollbarOverrideBGCGName),
	    HLL_EXPORT(GetVScrollbarOverrideBGCGName, PE_GetVScrollbarOverrideBGCGName),
	    HLL_EXPORT(SetVScrollbarFlatName, PE_SetVScrollbarFlatName),
	    HLL_EXPORT(GetVScrollbarFlatName, PE_GetVScrollbarFlatName),
	    HLL_EXPORT(SetVScrollbarOverrideUpFlatName, PE_SetVScrollbarOverrideUpFlatName),
	    HLL_EXPORT(GetVScrollbarOverrideUpFlatName, PE_GetVScrollbarOverrideUpFlatName),
	    HLL_EXPORT(SetVScrollbarOverrideDownFlatName, PE_SetVScrollbarOverrideDownFlatName),
	    HLL_EXPORT(GetVScrollbarOverrideDownFlatName, PE_GetVScrollbarOverrideDownFlatName),
	    HLL_EXPORT(SetVScrollbarOverrideBarFlatName, PE_SetVScrollbarOverrideBarFlatName),
	    HLL_EXPORT(GetVScrollbarOverrideBarFlatName, PE_GetVScrollbarOverrideBarFlatName),
	    HLL_EXPORT(SetVScrollbarOverrideBGFlatName, PE_SetVScrollbarOverrideBGFlatName),
	    HLL_EXPORT(GetVScrollbarOverrideBGFlatName, PE_GetVScrollbarOverrideBGFlatName),
	    HLL_EXPORT(SetVScrollTotalSizeLinkNumber, PE_SetVScrollTotalSizeLinkNumber),
	    HLL_EXPORT(SetVScrollViewSizeLinkNumber, PE_SetVScrollViewSizeLinkNumber),
	    HLL_EXPORT(GetVScrollTotalSizeLinkNumber, PE_GetVScrollTotalSizeLinkNumber),
	    HLL_EXPORT(GetVScrollViewSizeLinkNumber, PE_GetVScrollViewSizeLinkNumber),
	    HLL_EXPORT(SetHScrollbarOverrideLeftCGName, PE_SetHScrollbarOverrideLeftCGName),
	    HLL_EXPORT(GetHScrollbarOverrideLeftCGName, PE_GetHScrollbarOverrideLeftCGName),
	    HLL_EXPORT(SetHScrollbarOverrideRightCGName, PE_SetHScrollbarOverrideRightCGName),
	    HLL_EXPORT(GetHScrollbarOverrideRightCGName, PE_GetHScrollbarOverrideRightCGName),
	    HLL_EXPORT(SetHScrollbarOverrideBarCGName, PE_SetHScrollbarOverrideBarCGName),
	    HLL_EXPORT(GetHScrollbarOverrideBarCGName, PE_GetHScrollbarOverrideBarCGName),
	    HLL_EXPORT(SetHScrollbarOverrideBGCGName, PE_SetHScrollbarOverrideBGCGName),
	    HLL_EXPORT(GetHScrollbarOverrideBGCGName, PE_GetHScrollbarOverrideBGCGName),
	    HLL_EXPORT(SetHScrollbarFlatName, PE_SetHScrollbarFlatName),
	    HLL_EXPORT(GetHScrollbarFlatName, PE_GetHScrollbarFlatName),
	    HLL_EXPORT(SetHScrollbarOverrideLeftFlatName, PE_SetHScrollbarOverrideLeftFlatName),
	    HLL_EXPORT(GetHScrollbarOverrideLeftFlatName, PE_GetHScrollbarOverrideLeftFlatName),
	    HLL_EXPORT(SetHScrollbarOverrideRightFlatName, PE_SetHScrollbarOverrideRightFlatName),
	    HLL_EXPORT(GetHScrollbarOverrideRightFlatName, PE_GetHScrollbarOverrideRightFlatName),
	    HLL_EXPORT(SetHScrollbarOverrideBarFlatName, PE_SetHScrollbarOverrideBarFlatName),
	    HLL_EXPORT(GetHScrollbarOverrideBarFlatName, PE_GetHScrollbarOverrideBarFlatName),
	    HLL_EXPORT(SetHScrollbarOverrideBGFlatName, PE_SetHScrollbarOverrideBGFlatName),
	    HLL_EXPORT(GetHScrollbarOverrideBGFlatName, PE_GetHScrollbarOverrideBGFlatName),
	    HLL_EXPORT(SetHScrollTotalSizeLinkNumber, PE_SetHScrollTotalSizeLinkNumber),
	    HLL_EXPORT(SetHScrollViewSizeLinkNumber, PE_SetHScrollViewSizeLinkNumber),
	    HLL_EXPORT(GetHScrollTotalSizeLinkNumber, PE_GetHScrollTotalSizeLinkNumber),
	    HLL_EXPORT(GetHScrollViewSizeLinkNumber, PE_GetHScrollViewSizeLinkNumber),
	    HLL_EXPORT(SetVSliderBarSize, PE_SetVSliderBarSize),
	    HLL_EXPORT(SetVSliderBarTotalSize, PE_SetVSliderBarTotalSize),
	    HLL_EXPORT(SetVSliderBarViewSize, PE_SetVSliderBarViewSize),
	    HLL_EXPORT(SetVSliderBarScrollPos, PE_SetVSliderBarScrollPos),
	    HLL_EXPORT(SetVSliderBarScrollRate, PE_SetVSliderBarScrollRate),
	    HLL_EXPORT(GetVSliderBarTotalSize, PE_GetVSliderBarTotalSize),
	    HLL_EXPORT(GetVSliderBarViewSize, PE_GetVSliderBarViewSize),
	    HLL_EXPORT(GetVSliderBarScrollPos, PE_GetVSliderBarScrollPos),
	    HLL_EXPORT(GetVSliderBarScrollRate, PE_GetVSliderBarScrollRate),
	    HLL_EXPORT(SetVSliderBarCGName, PE_SetVSliderBarCGName),
	    HLL_EXPORT(SetVSliderBarOverrideBarCGName, PE_SetVSliderBarOverrideBarCGName),
	    HLL_EXPORT(SetVSliderBarOverrideBGCGName, PE_SetVSliderBarOverrideBGCGName),
	    HLL_EXPORT(GetVSliderBarCGName, PE_GetVSliderBarCGName),
	    HLL_EXPORT(GetVSliderBarOverrideBarCGName, PE_GetVSliderBarOverrideBarCGName),
	    HLL_EXPORT(GetVSliderBarOverrideBGCGName, PE_GetVSliderBarOverrideBGCGName),
	    HLL_EXPORT(SetVSliderBarFlatName, PE_SetVSliderBarFlatName),
	    HLL_EXPORT(SetVSliderBarOverrideBarFlatName, PE_SetVSliderBarOverrideBarFlatName),
	    HLL_EXPORT(SetVSliderBarOverrideBGFlatName, PE_SetVSliderBarOverrideBGFlatName),
	    HLL_EXPORT(GetVSliderBarFlatName, PE_GetVSliderBarFlatName),
	    HLL_EXPORT(GetVSliderBarOverrideBarFlatName, PE_GetVSliderBarOverrideBarFlatName),
	    HLL_EXPORT(GetVSliderBarOverrideBGFlatName, PE_GetVSliderBarOverrideBGFlatName),
	    HLL_EXPORT(SetVSliderTotalSizeLinkNumber, PE_SetVSliderTotalSizeLinkNumber),
	    HLL_EXPORT(SetVSliderViewSizeLinkNumber, PE_SetVSliderViewSizeLinkNumber),
	    HLL_EXPORT(GetVSliderTotalSizeLinkNumber, PE_GetVSliderTotalSizeLinkNumber),
	    HLL_EXPORT(GetVSliderViewSizeLinkNumber, PE_GetVSliderViewSizeLinkNumber),
	    HLL_EXPORT(SetHSliderBarSize, PE_SetHSliderBarSize),
	    HLL_EXPORT(SetHSliderBarTotalSize, PE_SetHSliderBarTotalSize),
	    HLL_EXPORT(SetHSliderBarViewSize, PE_SetHSliderBarViewSize),
	    HLL_EXPORT(SetHSliderBarScrollPos, PE_SetHSliderBarScrollPos),
	    HLL_EXPORT(SetHSliderBarScrollRate, PE_SetHSliderBarScrollRate),
	    HLL_EXPORT(GetHSliderBarTotalSize, PE_GetHSliderBarTotalSize),
	    HLL_EXPORT(GetHSliderBarViewSize, PE_GetHSliderBarViewSize),
	    HLL_EXPORT(GetHSliderBarScrollPos, PE_GetHSliderBarScrollPos),
	    HLL_EXPORT(GetHSliderBarScrollRate, PE_GetHSliderBarScrollRate),
	    HLL_EXPORT(SetHSliderBarCGName, PE_SetHSliderBarCGName),
	    HLL_EXPORT(SetHSliderBarOverrideBarCGName, PE_SetHSliderBarOverrideBarCGName),
	    HLL_EXPORT(SetHSliderBarOverrideBGCGName, PE_SetHSliderBarOverrideBGCGName),
	    HLL_EXPORT(GetHSliderBarCGName, PE_GetHSliderBarCGName),
	    HLL_EXPORT(GetHSliderBarOverrideBarCGName, PE_GetHSliderBarOverrideBarCGName),
	    HLL_EXPORT(GetHSliderBarOverrideBGCGName, PE_GetHSliderBarOverrideBGCGName),
	    HLL_EXPORT(SetHSliderBarFlatName, PE_SetHSliderBarFlatName),
	    HLL_EXPORT(SetHSliderBarOverrideBarFlatName, PE_SetHSliderBarOverrideBarFlatName),
	    HLL_EXPORT(SetHSliderBarOverrideBGFlatName, PE_SetHSliderBarOverrideBGFlatName),
	    HLL_EXPORT(GetHSliderBarFlatName, PE_GetHSliderBarFlatName),
	    HLL_EXPORT(GetHSliderBarOverrideBarFlatName, PE_GetHSliderBarOverrideBarFlatName),
	    HLL_EXPORT(GetHSliderBarOverrideBGFlatName, PE_GetHSliderBarOverrideBGFlatName),
	    HLL_EXPORT(SetHSliderTotalSizeLinkNumber, PE_SetHSliderTotalSizeLinkNumber),
	    HLL_EXPORT(SetHSliderViewSizeLinkNumber, PE_SetHSliderViewSizeLinkNumber),
	    HLL_EXPORT(GetHSliderTotalSizeLinkNumber, PE_GetHSliderTotalSizeLinkNumber),
	    HLL_EXPORT(GetHSliderViewSizeLinkNumber, PE_GetHSliderViewSizeLinkNumber),
	    HLL_EXPORT(SetEnableInput, PE_SetEnableInput),
	    HLL_EXPORT(IsEnableInput, PE_IsEnableInput),
	    HLL_EXPORT(SeekMessage, PE_SeekMessage),
	    HLL_EXPORT(GetMessageUniqueID, PE_GetMessageUniqueID),
	    HLL_EXPORT(SetEventID, PE_SetEventID),
	    HLL_EXPORT(SetComponentSubColorMode, PE_SetComponentSubColorMode),
	    HLL_EXPORT(IsComponentSubColorMode, PE_IsComponentSubColorMode),
	    HLL_EXPORT(SetMultiTextBoxMode, PartsEngine_SetMultiTextBoxMode),
	    HLL_EXPORT(GetMultiTextBoxMode, PartsEngine_GetMultiTextBoxMode),
	    HLL_EXPORT(SetMessageWindowRubyFont, PE_SetMessageWindowRubyFont),
	    HLL_EXPORT(GetMessageWindowRubyFont, PE_GetMessageWindowRubyFont),
	    HLL_EXPORT(SetMessageWindowRubyCharSpace, PE_SetMessageWindowRubyCharSpace),
	    HLL_EXPORT(GetMessageWindowRubyCharSpace, PE_GetMessageWindowRubyCharSpace),
	    HLL_EXPORT(SetMessageWindowRubyLineSpace, PE_SetMessageWindowRubyLineSpace),
	    HLL_EXPORT(GetMessageWindowRubyLineSpace, PE_GetMessageWindowRubyLineSpace),
	    HLL_EXPORT(_PreLink, PartsEngine_PreLink),
	    // for versions without PartsEngine.Init
	    HLL_EXPORT(_ModuleInit, PartsEngine_ModuleInit),
	    HLL_EXPORT(_ModuleFini, PartsEngine_ModuleFini),
	    // Oyako Rankan
	    HLL_EXPORT(Init, PE_Init),
	    HLL_EXPORT(Parts_SetComment, PE_SetComment),
	    HLL_EXPORT(Parts_GetComment, PE_GetComment),
	    HLL_EXPORT(SetWantSaveBackScene, PE_parts_set_want_save),
	    HLL_EXPORT(IsWantSaveBackScene, PE_parts_get_want_save),
	    HLL_EXPORT(SaveBackScene, PE_SaveBackScene),
	    HLL_EXPORT(LoadBackScene, PE_LoadBackScene),
	    HLL_EXPORT(ClearBackScene, PE_ClearBackScene),
	    HLL_EXPORT(SetShowForBackScene, PE_SetShowForBackScene),
	    HLL_EXPORT(SetMulColorForBackScene, PE_SetMulColorForBackScene),
	    HLL_EXPORT(SetAlphaForBackScene, PE_SetAlphaForBackScene),
	    HLL_EXPORT(SetFontColorForBackScene, PE_SetFontColorForBackScene),
	    HLL_EXPORT(SetPanelSize, PE_SetPanelSize),
	    HLL_EXPORT(SetPanelColor, PE_SetPanelColor),
	    HLL_EXPORT(GetPanelR, PE_GetPanelR),
	    HLL_EXPORT(GetPanelG, PE_GetPanelG),
	    HLL_EXPORT(GetPanelB, PE_GetPanelB),
	    HLL_EXPORT(GetPanelA, PE_GetPanelA),
	    HLL_EXPORT(SetPanelAlphaGradationTop, PE_SetPanelAlphaGradationTop),
	    HLL_EXPORT(SetPanelAlphaGradationBottom, PE_SetPanelAlphaGradationBottom),
	    HLL_EXPORT(SetPanelAlphaGradationLeft, PE_SetPanelAlphaGradationLeft),
	    HLL_EXPORT(SetPanelAlphaGradationRight, PE_SetPanelAlphaGradationRight),
	    HLL_EXPORT(GetPanelAlphaGradationTop, PE_GetPanelAlphaGradationTop),
	    HLL_EXPORT(GetPanelAlphaGradationBottom, PE_GetPanelAlphaGradationBottom),
	    HLL_EXPORT(GetPanelAlphaGradationLeft, PE_GetPanelAlphaGradationLeft),
	    HLL_EXPORT(GetPanelAlphaGradationRight, PE_GetPanelAlphaGradationRight),
	    HLL_EXPORT(SetActiveController, PE_set_active_controller),
	    HLL_EXPORT(GetActiveController, PE_get_active_controller),
	    HLL_EXPORT(GetActiveParts, PE_GetActiveParts),
	    HLL_EXPORT(GetSystemOverlayController, PE_get_system_controller),
	    HLL_EXPORT(GetControllerIndex, PE_GetControllerIndex),
	    HLL_EXPORT(GetControllerID, PE_GetControllerID),
	    HLL_EXPORT(GetControllerLength, PE_GetControllerLength),
	    HLL_EXPORT(Parts_SetParentPartsNumber, PE_SetParentPartsNumber),
	    HLL_EXPORT(Parts_GetParentPartsNumber, PE_GetParentPartsNumber),
	    HLL_EXPORT(IsExistChild, PE_IsExistChild),
	    HLL_EXPORT(ClearChild, PE_ClearChild),
	    HLL_EXPORT(AddChild, PE_AddChild),
	    HLL_EXPORT(InsertChild, PE_InsertChild),
	    HLL_EXPORT(RemoveChild, PE_RemoveChild),
	    HLL_EXPORT(NumofChild, PE_NumofChild),
	    HLL_EXPORT(GetChildIndex, PE_GetChildIndex),
	    HLL_EXPORT(GetChild, PE_GetChild),
	    HLL_EXPORT(AddPartsConstructionProcess, PartsEngine_AddPartsConstructionProcess),
	    HLL_EXPORT(Parts_GetPartsConstructionProcessCount, PartsEngine_Parts_GetPartsConstructionProcessCount),
	    HLL_EXPORT(GetPartsConstructionProcess, PartsEngine_GetPartsConstructionProcess),
	    HLL_EXPORT(Update, PartsEngine_Update),
	    HLL_EXPORT(UpdateGUIController, PE_UpdateInputState),
	    HLL_EXPORT(GetFreeSystemPartsNumber, PE_GetFreeNumber),
	    // FIXME: what is the difference?
	    HLL_EXPORT(GetFreeSystemPartsNumberNotSaved, PE_GetFreeNumber),
	    HLL_EXPORT(CreateUserPartsNumber, PartsEngine_CreateUserPartsNumber),
	    HLL_EXPORT(ReleaseUserPartsNumber, PartsEngine_ReleaseUserPartsNumber),
	    HLL_EXPORT(SetEnableInputProcess, PE_SetEnableInputProcess),
	    HLL_EXPORT(IsEnableInputProcess, PE_IsEnableInputProcess),
	    HLL_EXPORT(Parts_SetSoundName, PE_Parts_SetSoundName),
	    HLL_EXPORT(Parts_GetSoundName, PE_Parts_GetSoundName),
	    HLL_EXPORT(Parts_PlaySound, PE_Parts_PlaySound),
	    HLL_EXPORT(Parts_SetWheelable, PE_SetWheelable),
	    HLL_EXPORT(SetClickMissSoundName, PE_SetClickMissSoundName),
	    HLL_EXPORT(GetClickMissSoundName, PE_GetClickMissSoundName),
	    HLL_EXPORT(IsExistActivity, PE_IsExistActivity),
	    HLL_EXPORT(CreateActivity, PE_CreateActivity),
	    HLL_OVERLOAD(ReadActivityFile, PartsEngine_ResolveReadActivityFile),
	    HLL_EXPORT(IsExistActivityFile, PE_IsExistActivityFile),
	    HLL_EXPORT(AddActivityParts, PE_AddActivityParts),
	    HLL_EXPORT(RemoveActivityParts, PE_RemoveActivityParts),
	    HLL_EXPORT(RemoveAllActivityParts, PE_RemoveAllActivityParts),
	    HLL_EXPORT(NumofActivityParts, PE_NumofActivityParts),
	    HLL_EXPORT(GetActivityParts, PE_GetActivityParts),
	    HLL_EXPORT(IsExistActivityPartsByName, PE_IsExistActivityPartsByName),
	    HLL_EXPORT(IsExistActivityPartsByNumber, PE_IsExistActivityPartsByNumber),
	    HLL_EXPORT(GetActivityPartsNumber, PE_GetActivityPartsNumber),
	    HLL_EXPORT(GetActivityPartsName, PE_GetActivityPartsName),
	    HLL_EXPORT(GetActivityEXID, PE_GetActivityEXID),
	    HLL_EXPORT(LoadActivityEXText, PartsEngine_LoadActivityEXText),
	    HLL_EXPORT(SaveActivityEXText, PartsEngine_SaveActivityEXText),
	    HLL_EXPORT(SetActivityEXText, PE_SetActivityEXText),
	    HLL_EXPORT(GetActivityEXText, PE_GetActivityEXText),
	    HLL_EXPORT(WriteActivityFile, PartsEngine_WriteActivityFile),
	    HLL_EXPORT(SetActivityBG, PE_SetActivityBG),
	    HLL_EXPORT(GetActivityBG, PE_GetActivityBG),
	    HLL_EXPORT(AddActivityCloseParts, PE_AddActivityCloseParts),
	    HLL_EXPORT(RemoveActivityCloseParts, PE_RemoveActivityCloseParts),
	    HLL_EXPORT(RemoveAllActivityCloseParts, PE_RemoveAllActivityCloseParts),
	    HLL_EXPORT(IsExistActivityCloseParts, PE_IsExistActivityCloseParts),
	    HLL_EXPORT(SetActivityIntentData, PE_SetActivityIntentData),
	    HLL_EXPORT(AddActivityIntentDataDestination, PE_AddActivityIntentDataDestination),
	    HLL_EXPORT(IsExistActivityIntentData, PE_IsExistActivityIntentData),
	    HLL_EXPORT(NumofActivityIntentDataDestination, PE_NumofActivityIntentDataDestination),
	    HLL_EXPORT(GetActivityIntentDataDestination, PartsEngine_GetActivityIntentDataDestination_v6),
	    HLL_EXPORT(GetActivityIntentDataType, PE_GetActivityIntentDataType),
	    HLL_EXPORT(AddActivityLockedParts, PE_AddActivityLockedParts),
	    HLL_EXPORT(IsExistActivityLockedParts, PE_IsExistActivityLockedParts),
	    HLL_EXPORT(SetActivityEndKey, PE_SetActivityEndKey),
	    HLL_EXPORT(PauseMotion, PE_PauseMotion),
	    HLL_EXPORT(EraseActivityEndKey, PE_EraseActivityEndKey),
	    HLL_EXPORT(IsExistActivityEndKey, PE_IsExistActivityEndKey),
	    HLL_EXPORT(NumofActivityEndKey, PE_NumofActivityEndKey),
	    HLL_EXPORT(GetActivityEndKey, PE_GetActivityEndKey),
	    HLL_EXPORT(SetUserComponentName, PE_SetUserComponentName),
	    HLL_EXPORT(GetUserComponentName, PE_GetUserComponentName),
	    HLL_EXPORT(SetUserComponentData, PE_SetUserComponentData),
	    HLL_EXPORT(GetUserComponentData, PE_GetUserComponentData),
	    HLL_EXPORT(IsExistParts, PE_IsExist),
	    HLL_EXPORT(SetPartsCG, PE_SetPartsCG),
	    HLL_EXPORT(GetPartsCGName, PE_GetPartsCGName),
	    HLL_EXPORT(SetPartsCGSurfaceArea, PE_SetPartsCGSurfaceArea),
	    HLL_EXPORT(SetLoopCG, PE_SetLoopCG),
	    HLL_EXPORT(SetLoopCGSurfaceArea, PE_SetLoopCGSurfaceArea),
	    HLL_EXPORT(SetText, PE_SetText),
	    HLL_EXPORT(AddPartsText, PE_AddPartsText),
	    HLL_EXPORT(DeletePartsTopTextLine, PE_DeletePartsTopTextLine),
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
	    HLL_OVERLOAD(SetHGaugeRate, PartsEngine_ResolveGaugeRate),
	    HLL_EXPORT(SetVGaugeCG, PE_SetVGaugeCG),
	    HLL_OVERLOAD(SetVGaugeRate, PartsEngine_ResolveGaugeRate),
	    HLL_EXPORT(SetHGaugeSurfaceArea, PE_SetHGaugeSurfaceArea),
	    HLL_EXPORT(SetVGaugeSurfaceArea, PE_SetVGaugeSurfaceArea),
	    HLL_EXPORT(SetNumeralCG, PE_SetNumeralCG),
	    HLL_EXPORT(SetNumeralLinkedCGNumberWidthWidthList, PE_SetNumeralLinkedCGNumberWidthWidthList),
	    HLL_OVERLOAD(SetNumeralFont, PartsEngine_ResolveSetNumeralFont),
	    HLL_EXPORT(GetNumeralFont, PE_GetNumeralFont),
	    HLL_EXPORT(SetNumeralFullPitch, PE_SetNumeralFullPitch),
	    HLL_EXPORT(SetNumeralShowType, PE_SetNumeralShowType),
	    HLL_EXPORT(SetNumeralShowPadding, PE_SetNumeralShowPadding),
	    HLL_EXPORT(IsNumeralShowPadding, PE_IsNumeralShowPadding),
	    HLL_EXPORT(GetNumeralLength, PE_GetNumeralLength),
	    HLL_EXPORT(GetNumeralCGNumberWidthList, PE_GetNumeralCGNumberWidthList),
	    HLL_EXPORT(GetNumeralSurfaceArea, PE_GetNumeralSurfaceArea),
	    HLL_EXPORT(SetNumeralNumber, PE_SetNumeralNumber),
	    HLL_EXPORT(SetNumeralShowComma, PE_SetNumeralShowComma),
	    HLL_EXPORT(SetNumeralSpace, PE_SetNumeralSpace),
	    HLL_EXPORT(SetNumeralLength, PE_SetNumeralLength),
	    HLL_EXPORT(SetNumeralSurfaceArea, PE_SetNumeralSurfaceArea),
	    HLL_EXPORT(SetPartsRectangleDetectionSize, PE_SetPartsRectangleDetectionSize),
	    HLL_TODO_EXPORT(SetPartsRectangleDetectionSurfaceArea, PartsEngine_SetPartsRectangleDetectionSurfaceArea),
	    HLL_EXPORT(SetPartsCGDetectionSize, PE_SetPartsCGDetectionSize),
	    HLL_EXPORT(SetPartsCGDetectionSurfaceArea, PartsEngine_Parts_SetPartsCGDetectionSurfaceArea),
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
	    HLL_EXPORT(AddFillGradationHorizonToPartsConstructionProcess, PE_AddFillGradationHorizonToPartsConstructionProcess),
	    HLL_EXPORT(AddDrawRectToPartsConstructionProcess, PE_AddDrawRectToPartsConstructionProcess),
	    HLL_EXPORT(AddDrawLineToPartsConstructionProcess, PE_AddDrawLineToPartsConstructionProcess),
	    HLL_EXPORT(AddDrawCutCGToPartsConstructionProcess, PartsEngine_AddDrawCutCGToPartsConstructionProcess_old),
	    HLL_EXPORT(AddCopyCutCGToPartsConstructionProcess, PartsEngine_AddCopyCutCGToPartsConstructionProcess_old),
	    HLL_EXPORT(AddGrayFilterToPartsConstructionProcess, PE_AddGrayFilterToPartsConstructionProcess),
	    HLL_EXPORT(AddAddFilterToPartsConstructionProcess, PE_AddAddFilterToPartsConstructionProcess),
	    HLL_EXPORT(AddMulFilterToPartsConstructionProcess, PE_AddMulFilterToPartsConstructionProcess),
	    HLL_EXPORT(BuildPartsConstructionProcess, PE_BuildPartsConstructionProcess),
	    HLL_EXPORT(AddDrawTextToPartsConstructionProcess, PE_AddDrawTextToPartsConstructionProcess),
	    HLL_EXPORT(AddCopyTextToPartsConstructionProcess, PE_AddCopyTextToPartsConstructionProcess),
	    HLL_EXPORT(SetPartsConstructionSurfaceArea, PE_SetPartsConstructionSurfaceArea),
	    HLL_EXPORT(ReleaseParts, PE_ReleaseParts),
	    HLL_EXPORT(SuspendTexture, PE_SuspendTexture),
	    HLL_EXPORT(ResumeTexture, PE_ResumeTexture),
	    HLL_EXPORT(ReleaseAllParts, PE_ReleaseAllParts),
	    HLL_EXPORT(ReleaseAllPartsWithoutSystem, PE_ReleaseAllPartsWithoutSystem),
	    HLL_EXPORT(SetPos, PE_SetPos),
	    HLL_EXPORT(SetZ, PE_SetZ),
	    HLL_EXPORT(SetShow, PE_SetShow),
	    HLL_EXPORT(SetAlpha, PE_SetAlpha),
	    HLL_EXPORT(SetPartsDrawFilter, PE_SetPartsDrawFilter),
	    HLL_EXPORT(SetAddColor, PE_SetAddColor),
	    HLL_EXPORT(SetMultiplyColor, PE_SetMultiplyColor),
	    HLL_EXPORT(SetPassCursor, PE_SetPassCursor),
	    HLL_EXPORT(SetClickable, PE_SetClickable),
	    HLL_EXPORT(SetSpeedupRateByMessageSkip, PE_SetSpeedupRateByMessageSkip),
	    HLL_OVERLOAD(SetResetTimerByChangeInputStatus, PartsEngine_ResolveSetResetTimerByChangeInputStatus),
	    HLL_EXPORT(GetPartsX, PE_GetPartsX),
	    HLL_EXPORT(GetPartsY, PE_GetPartsY),
	    HLL_EXPORT(GetPartsZ, PE_GetPartsZ),
	    HLL_EXPORT(GetPartsShow, PE_GetPartsShow),
	    HLL_EXPORT(GetPartsAlpha, PE_GetPartsAlpha),
	    HLL_EXPORT(GetAddColor, PE_GetAddColor),
	    HLL_EXPORT(GetMultiplyColor, PE_GetMultiplyColor),
	    HLL_EXPORT(GetPartsPassCursor, PE_GetPartsPassCursor),
	    HLL_EXPORT(GetPartsClickable, PE_GetPartsClickable),
	    HLL_EXPORT(GetPartsSpeedupRateByMessageSkip, PE_GetSpeedupRateByMessageSkip),
	    HLL_OVERLOAD(GetResetTimerByChangeInputStatus, PartsEngine_ResolveGetResetTimerByChangeInputStatus),
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
	    HLL_EXPORT(SetSoundNumber, PartsEngine_SetSoundNumber),
	    HLL_EXPORT(GetSoundNumber, PartsEngine_GetSoundNumber),
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
	    HLL_EXPORT(SaveThumbnail, PE_save_thumbnail),
	    HLL_EXPORT(Save, PE_Save),
	    HLL_EXPORT(SaveWithoutHideParts, PE_SaveWithoutHideParts),
	    HLL_EXPORT(Load, PE_Load),
	    // Rance 9
	    HLL_EXPORT(PartsFunc, PartsEngine_PartsFunc),
	    HLL_EXPORT(Release, PE_ReleaseParts),
	    HLL_OVERLOAD(ReleaseAll, PartsEngine_ResolveReleaseAll),
	    HLL_EXPORT(ReleaseAllWithoutSystem, PE_ReleaseAllWithoutSystem),
	    HLL_EXPORT(GetFreeNumber, PE_GetFreeNumber),
	    HLL_EXPORT(IsExist, PE_IsExist),
	    HLL_EXPORT(AddController, PE_AddController),
	    HLL_EXPORT(RemoveController, PE_RemoveController),
	    HLL_EXPORT(MoveController, PE_MoveController),
	    HLL_EXPORT(UpdateComponent, PartsEngine_Update),
	    HLL_EXPORT(UpdateMatrix, PartsEngine_UpdateMatrix),
	    HLL_EXPORT(GetComponentAbsoluteSquarePos, PE_GetComponentAbsoluteSquarePos),
	    HLL_EXPORT(SetRectangleDetectionPoint, PE_SetRectangleDetectionPoint),
	    HLL_EXPORT(GetRectangleDetectionPoint, PE_GetRectangleDetectionPoint),
	    HLL_EXPORT(Parts_SetThumbnailReductionSize, PE_SetThumbnailReductionSize),
	    HLL_EXPORT(Parts_SetThumbnailMode, PE_SetThumbnailMode),
	    HLL_EXPORT(GetClickNumber, PE_GetClickPartsNumber),
	    HLL_EXPORT(StopSoundWithoutSystemSound, audio_stop_non_system_channels),
	    HLL_EXPORT(ReleaseActivity, PE_ReleaseActivity),
	    HLL_EXPORT(CrateActivityBinary, PE_CrateActivityBinary),
	    HLL_EXPORT(ReadActivityBinary, PE_ReadActivityBinary),
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
	    HLL_EXPORT(SetFocus, PE_SetFocus),
	    HLL_EXPORT(IsFocus, PE_IsFocus),
	    HLL_EXPORT(SetComponentType, PE_SetComponentType),
	    HLL_EXPORT(GetComponentType, PE_GetComponentType),
	    HLL_EXPORT(SetComponentPos, PartsEngine_SetComponentPos),
	    HLL_EXPORT(SetComponentPosZ, PE_SetZ),
	    HLL_EXPORT(GetComponentPosX, PartsEngine_Parts_GetComponentPosX),
	    HLL_EXPORT(GetComponentPosY, PartsEngine_GetComponentPosY),
	    HLL_EXPORT(GetComponentPosZ, PE_GetPartsZ),
	    HLL_EXPORT(GetComponentAbsolutePos, PE_GetComponentAbsolutePos),
	    HLL_EXPORT(GetComponentAbsolutePosX, PE_GetComponentAbsolutePosX),
	    HLL_EXPORT(GetComponentAbsolutePosY, PE_GetComponentAbsolutePosY),
	    HLL_EXPORT(GetComponentAbsolutePosZ, PE_parts_get_absolute_z),
	    HLL_EXPORT(GetComponentAbsoluteMaxPosZ, PartsEngine_GetComponentAbsoluteMaxPosZ),
	    HLL_EXPORT(Parts_GetPartsUpperLeftPos, PartsEngine_Parts_GetPartsUpperLeftPos),
	    HLL_EXPORT(Parts_GetPartsUpperLeftPosX, PartsEngine_Parts_GetPartsUpperLeftPosX),
	    HLL_EXPORT(Parts_GetPartsUpperLeftPosY, PartsEngine_Parts_GetPartsUpperLeftPosY),
	    HLL_EXPORT(SetComponentOriginPosMode, PE_SetPartsOriginPosMode),
	    HLL_EXPORT(GetComponentOriginPosMode, PE_GetPartsOriginPosMode),
	    HLL_EXPORT(GetComponentWidth, PartsEngine_GetComponentWidth),
	    HLL_EXPORT(GetComponentHeight, PartsEngine_GetComponentHeight),
	    HLL_EXPORT(Parts_GetPartsSize, PartsEngine_Parts_GetPartsSize),
	    HLL_EXPORT(Parts_GetPartsWidth, PE_GetPartsWidth),
	    HLL_EXPORT(Parts_GetPartsHeight, PE_GetPartsHeight),
	    HLL_EXPORT(Parts_SetPosForRate, PartsEngine_Parts_SetPosForRate),
	    HLL_EXPORT(Parts_IsPosForRate, PartsEngine_Parts_IsPosForRate),
	    HLL_EXPORT(SetLockInputState, PE_parts_set_lock_input_state),
	    HLL_EXPORT(IsLockInputState, PartsEngine_IsLockInputState),
	    HLL_EXPORT(SetComponentShow, PE_SetShow),
	    HLL_EXPORT(IsComponentShow, PE_GetPartsShow),
	    HLL_EXPORT(SetComponentMessageWindowShowLink, PE_SetPartsMessageWindowShowLink),
	    HLL_EXPORT(IsComponentMessageWindowShowLink, PE_GetPartsMessageWindowShowLink),
	    HLL_EXPORT(SetComponentMessageWindowEffectLink, PartsEngine_SetComponentMessageWindowEffectLink),
	    HLL_EXPORT(IsComponentMessageWindowEffectLink, PartsEngine_IsComponentMessageWindowEffectLink),
	    HLL_EXPORT(SetComponentAlpha, PE_SetAlpha),
	    HLL_EXPORT(GetComponentAlpha, PE_GetPartsAlpha),
	    HLL_EXPORT(SetComponentAddColor, PE_SetAddColor),
	    HLL_EXPORT(GetComponentAddColorR, PartsEngine_GetComponentAddColorR),
	    HLL_EXPORT(GetComponentAddColorG, PartsEngine_GetComponentAddColorG),
	    HLL_EXPORT(GetComponentAddColorB, PartsEngine_GetComponentAddColorB),
	    HLL_EXPORT(SetComponentMulColor, PE_SetMultiplyColor),
	    HLL_EXPORT(GetComponentMulColorR, PartsEngine_GetComponentMulColorR),
	    HLL_EXPORT(GetComponentMulColorG, PartsEngine_GetComponentMulColorG),
	    HLL_EXPORT(GetComponentMulColorB, PartsEngine_GetComponentMulColorB),
	    HLL_EXPORT(SetComponentDrawFilter, PE_SetPartsDrawFilter),
	    HLL_EXPORT(GetComponentDrawFilter, PE_GetPartsDrawFilter),
	    HLL_EXPORT(SetComponentMagX, PE_SetPartsMagX),
	    HLL_EXPORT(SetComponentMagY, PE_SetPartsMagY),
	    HLL_EXPORT(GetComponentMagX, PE_GetPartsMagX),
	    HLL_EXPORT(GetComponentMagY, PE_GetPartsMagY),
	    HLL_EXPORT(SetComponentRotateX, PE_SetPartsRotateX),
	    HLL_EXPORT(SetComponentRotateY, PE_SetPartsRotateY),
	    HLL_EXPORT(SetComponentRotateZ, PE_SetPartsRotateZ),
	    HLL_EXPORT(GetComponentRotateX, PartsEngine_GetComponentRotateX),
	    HLL_EXPORT(GetComponentRotateY, PartsEngine_GetComponentRotateY),
	    HLL_EXPORT(GetComponentRotateZ, PE_GetPartsRotateZ),
	    HLL_EXPORT(SetComponentEnableClipArea, PartsEngine_SetComponentEnableClipArea),
	    HLL_EXPORT(IsComponentEnableClipArea, PartsEngine_IsComponentEnableClipArea),
	    HLL_EXPORT(SetComponentClipArea, PartsEngine_SetComponentClipArea),
	    HLL_EXPORT(GetComponentClipAreaPosX, PartsEngine_GetComponentClipAreaPosX),
	    HLL_EXPORT(GetComponentClipAreaPosY, PartsEngine_GetComponentClipAreaPosY),
	    HLL_EXPORT(GetComponentClipAreaPosWidth, PartsEngine_GetComponentClipAreaPosWidth),
	    HLL_EXPORT(GetComponentClipAreaPosHeight, PartsEngine_GetComponentClipAreaPosHeight),
	    HLL_EXPORT(SetComponentReverseTB, PartsEngine_SetComponentReverseTB),
	    HLL_EXPORT(SetComponentReverseLR, PartsEngine_SetComponentReverseLR),
	    HLL_EXPORT(GetComponentReverseTB, PartsEngine_GetComponentReverseTB),
	    HLL_EXPORT(GetComponentReverseLR, PartsEngine_GetComponentReverseLR),
	    HLL_EXPORT(SetComponentMargin, PE_SetComponentMargin),
	    HLL_EXPORT(GetComponentMarginTop, PE_GetComponentMarginTop),
	    HLL_EXPORT(GetComponentMarginBottom, PE_GetComponentMarginBottom),
	    HLL_EXPORT(GetComponentMarginLeft, PE_GetComponentMarginLeft),
	    HLL_EXPORT(GetComponentMarginRight, PE_GetComponentMarginRight),
	    HLL_EXPORT(SetComponentAlphaClipper, PE_SetPartsAlphaClipperPartsNumber),
	    HLL_EXPORT(GetComponentAlphaClipper, PartsEngine_GetComponentAlphaClipper),
	    HLL_EXPORT(SetComponentTextureFilterType, PartsEngine_SetComponentTextureFilterType),
	    HLL_EXPORT(GetComponentTextureFilterType, PartsEngine_GetComponentTextureFilterType),
	    HLL_EXPORT(SetComponentTextureAddressType, PartsEngine_SetComponentTextureAddressType),
	    HLL_EXPORT(GetComponentTextureAddressType, PartsEngine_GetComponentTextureAddressType),
	    HLL_EXPORT(SetComponentMipmap, PartsEngine_SetComponentMipmap),
	    HLL_EXPORT(IsComponentMipmap, PartsEngine_IsComponentMipmap),
	    HLL_EXPORT(Parts_SetUpdateLookAtPos, PartsEngine_Parts_SetUpdateLookAtPos),
	    HLL_EXPORT(Parts_IsUpdateLookAtPos, PartsEngine_Parts_IsUpdateLookAtPos),
	    HLL_EXPORT(Parts_SetAnchor, PartsEngine_Parts_SetAnchor),
	    HLL_EXPORT(Parts_IsAnchorTop, PartsEngine_Parts_IsAnchorTop),
	    HLL_EXPORT(Parts_IsAnchorBottom, PartsEngine_Parts_IsAnchorBottom),
	    HLL_EXPORT(Parts_IsAnchorLeft, PartsEngine_Parts_IsAnchorLeft),
	    HLL_EXPORT(Parts_IsAnchorRight, PartsEngine_Parts_IsAnchorRight),
	    HLL_EXPORT(SetMotionData, PartsEngine_SetMotionData),
	    HLL_EXPORT(GetMotionBeginFrame, PartsEngine_GetMotionBeginFrame),
	    HLL_EXPORT(GetMotionEndFrame, PartsEngine_GetMotionEndFrame),
	    HLL_EXPORT(SetComponentSpeedupRateByMessageSkip, PE_SetSpeedupRateByMessageSkip),
	    HLL_EXPORT(GetComponentSpeedupRateByMessageSkip, PE_GetSpeedupRateByMessageSkip),
	    HLL_EXPORT(SetEnableSpeedUp, PE_SetEnableSpeedUp),
	    HLL_EXPORT(IsEnableSpeedUp, PE_IsEnableSpeedUp),
	    HLL_EXPORT(AddComponentMotionPos, PartsEngine_AddComponentMotionPos),
	    HLL_EXPORT(AddComponentMotionAlpha, PE_AddMotionAlpha_curve),
	    HLL_EXPORT(AddComponentMotionAddColor, PE_AddMotionAddColor_curve),
	    HLL_EXPORT(AddComponentMotionMulColor, PE_AddMotionMulColor_curve),
	    HLL_EXPORT(AddComponentMotionCG, PE_AddMotionCG_curve),
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
	    HLL_EXPORT(SuspendBuildView, PartsEngine_SelectTextBoxAll),
	    HLL_EXPORT(SuspendBuildViewAt, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(ResumeBuildView, PartsEngine_SelectTextBoxAll),
	    HLL_TODO_EXPORT(SetButtonSize, PartsEngine_SetButtonSize),
	    HLL_EXPORT(SetButtonDrag, PE_SetDrag),
	    HLL_EXPORT(IsButtonDrag, PartsEngine_WidgetIsDrag),
	    HLL_TODO_EXPORT(SetButtonEnable, PartsEngine_SetButtonEnable),
	    HLL_TODO_EXPORT(IsButtonEnable, PartsEngine_IsButtonEnable),
	    HLL_EXPORT(SetButtonPixelDecide, PE_SetPartsPixelDecide),
	    HLL_EXPORT(IsButtonPixelDecide, PartsEngine_Parts_IsPartsPixelDecide),
	    HLL_TODO_EXPORT(SetButtonColor, PartsEngine_SetButtonColor),
	    HLL_TODO_EXPORT(GetButtonR, PartsEngine_GetButtonR),
	    HLL_TODO_EXPORT(GetButtonG, PartsEngine_GetButtonG),
	    HLL_TODO_EXPORT(GetButtonB, PartsEngine_GetButtonB),
	    HLL_TODO_EXPORT(SetButtonFontProperty, PartsEngine_SetButtonFontProperty),
	    HLL_TODO_EXPORT(GetButtonFontProperty, PartsEngine_GetButtonFontProperty),
	    HLL_EXPORT(SetButtonOnCursorSoundNumber, PartsEngine_WidgetSetOnCursorSoundNumber),
	    HLL_EXPORT(SetButtonClickSoundNumber, PartsEngine_WidgetSetClickSoundNumber),
	    HLL_EXPORT(GetButtonOnCursorSoundNumber, PartsEngine_WidgetGetOnCursorSoundNumber),
	    HLL_EXPORT(GetButtonClickSoundNumber, PartsEngine_WidgetGetClickSoundNumber),
	    HLL_TODO_EXPORT(SetButtonCGName, PartsEngine_SetButtonCGName),
	    HLL_TODO_EXPORT(GetButtonCGName, PartsEngine_GetButtonCGName),
	    HLL_TODO_EXPORT(SetButtonText, PartsEngine_SetButtonText),
	    HLL_TODO_EXPORT(GetButtonText, PartsEngine_GetButtonText),
	    HLL_EXPORT(SetButtonFlatName, PE_SetButtonFlatName),
	    HLL_EXPORT(GetButtonFlatName, PE_GetButtonFlatName),
	    HLL_EXPORT(SetButtonTextOriginPosMode, PE_SetButtonTextOriginPosMode),
	    HLL_EXPORT(GetButtonTextOriginPosMode, PE_GetButtonTextOriginPosMode),
	    HLL_EXPORT(SetButtonCharSpace, PE_SetButtonCharSpace),
	    HLL_EXPORT(GetButtonCharSpace, PE_GetButtonCharSpace),
	    HLL_EXPORT(SetButtonLineSpace, PE_SetButtonLineSpace),
	    HLL_EXPORT(GetButtonLineSpace, PE_GetButtonLineSpace),
	    HLL_EXPORT(SetMessageWindowActive, PE_SetMessageWindowActive),
	    HLL_EXPORT(SetMessageWindowInactiveMultipleColor, PE_SetMessageWindowInactiveMultipleColor),
	    HLL_EXPORT(GetMessageWindowInactiveMultipleColorR, PE_GetMessageWindowInactiveMultipleColorR),
	    HLL_EXPORT(GetMessageWindowInactiveMultipleColorG, PE_GetMessageWindowInactiveMultipleColorG),
	    HLL_EXPORT(GetMessageWindowInactiveMultipleColorB, PE_GetMessageWindowInactiveMultipleColorB),
	    HLL_EXPORT(SetMessageWindowCGName, PE_SetMessageWindowCGName),
	    HLL_EXPORT(GetMessageWindowCGName, PE_GetMessageWindowCGName),
	    HLL_EXPORT(SetMessageWindowFlatName, PE_SetMessageWindowFlatName),
	    HLL_EXPORT(GetMessageWindowFlatName, PE_GetMessageWindowFlatName),
	    HLL_EXPORT(SetMessageWindowFlatShowWaitFrameNumber, PE_SetMessageWindowFlatShowWaitFrameNumber),
	    HLL_EXPORT(GetMessageWindowFlatShowWaitFrameNumber, PE_GetMessageWindowFlatShowWaitFrameNumber),
	    HLL_EXPORT(IsOverMessageWindowFlatShowWaitFrame, PE_IsOverMessageWindowFlatShowWaitFrame),
	    HLL_EXPORT(BackMessageWindowFlatBeginFrame, PE_BackMessageWindowFlatBeginFrame),
	    HLL_EXPORT(StepMessageWindowFlatFinalFrame, PE_StepMessageWindowFlatFinalFrame),
	    HLL_EXPORT(SetKeyWaitCGName, PE_SetKeyWaitCGName),
	    HLL_EXPORT(GetKeyWaitCGName, PE_GetKeyWaitCGName),
	    HLL_EXPORT(SetKeyWaitFlatName, PE_SetKeyWaitFlatName),
	    HLL_EXPORT(GetKeyWaitFlatName, PE_GetKeyWaitFlatName),
	    HLL_EXPORT(SetKeyWaitPos, PE_SetKeyWaitPos),
	    HLL_EXPORT(GetKeyWaitPosX, PE_GetKeyWaitPosX),
	    HLL_EXPORT(GetKeyWaitPosY, PE_GetKeyWaitPosY),
	    HLL_EXPORT(GetKeyWaitPosZ, PE_GetKeyWaitPosZ),
	    HLL_EXPORT(SetKeyWaitShow, PE_SetKeyWaitShow),
	    HLL_EXPORT(IsKeyWaitShow, PE_IsKeyWaitShow),
	    HLL_EXPORT(SetModeMarkShow, PartsEngine_SetModeMarkShow),
	    HLL_EXPORT(IsModeMarkShow, PartsEngine_IsModeMarkShow),
	    HLL_EXPORT(SetMessageWindowText, PE_SetMessageWindowText),
	    HLL_EXPORT(GetMessageWindowText, PE_GetMessageWindowText),
	    HLL_EXPORT(FixMessageWindowText, PE_FixMessageWindowText),
	    HLL_EXPORT(IsFixedMessageWindowText, PE_IsFixedMessageWindowText),
	    HLL_EXPORT(SetMessageWindowTextArea, PE_SetMessageWindowTextArea),
	    HLL_EXPORT(GetMessageWindowTextArea, PE_GetMessageWindowTextArea),
	    HLL_EXPORT(SetMessageWindowTextOriginPosMode, PE_SetMessageWindowTextOriginPosMode),
	    HLL_EXPORT(GetMessageWindowTextOriginPosMode, PE_GetMessageWindowTextOriginPosMode),
	    HLL_EXPORT(SetMessageWindowTextFont, PE_SetMessageWindowTextFont),
	    HLL_EXPORT(GetMessageWindowTextFont, PE_GetMessageWindowTextFont),
	    HLL_EXPORT(SetMessageWindowTextSpeed, PE_SetMessageWindowTextSpeed),
	    HLL_EXPORT(GetMessageWindowTextSpeed, PE_GetMessageWindowTextSpeed),
	    HLL_EXPORT(SetMessageWindowTextSpace, PE_SetMessageWindowTextSpace),
	    HLL_EXPORT(GetMessageWindowTextSpace, PE_GetMessageWindowTextSpace),
	    HLL_EXPORT(SetCheckBoxSize, PE_SetCheckBoxSize),
	    HLL_EXPORT(GetCheckBoxButtonWidth, PE_GetCheckBoxButtonWidth),
	    HLL_EXPORT(GetCheckBoxButtonHeight, PE_GetCheckBoxButtonHeight),
	    HLL_EXPORT(SetCheckBoxEnable, PE_SetCheckBoxEnable),
	    HLL_EXPORT(IsCheckBoxEnable, PE_IsCheckBoxEnable),
	    HLL_EXPORT(SetCheckBoxOverrideCGName, PE_SetCheckBoxOverrideCGName),
	    HLL_EXPORT(GetCheckBoxOverrideCGName, PE_GetCheckBoxOverrideCGName),
	    HLL_EXPORT(SetCheckBoxOverrideCheckedCGName, PE_SetCheckBoxOverrideCheckedCGName),
	    HLL_EXPORT(GetCheckBoxOverrideCheckedCGName, PE_GetCheckBoxOverrideCheckedCGName),
	    HLL_EXPORT(SetCheckBoxFlatName, PE_SetCheckBoxFlatName),
	    HLL_EXPORT(GetCheckBoxFlatName, PE_GetCheckBoxFlatName),
	    HLL_EXPORT(SetCheckBoxOverrideFlatName, PE_SetCheckBoxOverrideFlatName),
	    HLL_EXPORT(GetCheckBoxOverrideFlatName, PE_GetCheckBoxOverrideFlatName),
	    HLL_EXPORT(SetCheckBoxOverrideCheckedFlatName, PE_SetCheckBoxOverrideCheckedFlatName),
	    HLL_EXPORT(GetCheckBoxOverrideCheckedFlatName, PE_GetCheckBoxOverrideCheckedFlatName),
	    HLL_EXPORT(SetCheckBoxTextOriginPosMode, PE_SetCheckBoxTextOriginPosMode),
	    HLL_EXPORT(GetCheckBoxTextOriginPosMode, PE_GetCheckBoxTextOriginPosMode),
	    HLL_EXPORT(SetCheckBoxButtonMode, PE_SetCheckBoxButtonMode),
	    HLL_EXPORT(IsCheckBoxButtonMode, PE_IsCheckBoxButtonMode),
	    HLL_EXPORT(SetCheckBoxCharSpace, PE_SetCheckBoxCharSpace),
	    HLL_EXPORT(GetCheckBoxCharSpace, PE_GetCheckBoxCharSpace),
	    HLL_EXPORT(SetCheckBoxLineSpace, PE_SetCheckBoxLineSpace),
	    HLL_EXPORT(GetCheckBoxLineSpace, PE_GetCheckBoxLineSpace),
	    HLL_EXPORT(SetCheckBoxDrag, PE_SetDrag),
	    HLL_EXPORT(IsCheckBoxDrag, PartsEngine_WidgetIsDrag),
	    HLL_EXPORT(CheckBoxChecked, PE_CheckBoxChecked),
	    HLL_EXPORT(IsCheckBoxChecked, PE_IsCheckBoxChecked),
	    HLL_EXPORT(SetCheckBoxColor, PE_SetCheckBoxColor),
	    HLL_EXPORT(GetCheckBoxR, PE_GetCheckBoxR),
	    HLL_EXPORT(GetCheckBoxG, PE_GetCheckBoxG),
	    HLL_EXPORT(GetCheckBoxB, PE_GetCheckBoxB),
	    HLL_EXPORT(SetCheckBoxFontProperty, PE_SetCheckBoxFontProperty),
	    HLL_EXPORT(GetCheckBoxFontProperty, PE_GetCheckBoxFontProperty),
	    HLL_EXPORT(SetCheckBoxOnCursorSoundNumber, PartsEngine_WidgetSetOnCursorSoundNumber),
	    HLL_EXPORT(SetCheckBoxClickSoundNumber, PartsEngine_WidgetSetClickSoundNumber),
	    HLL_EXPORT(GetCheckBoxOnCursorSoundNumber, PartsEngine_WidgetGetOnCursorSoundNumber),
	    HLL_EXPORT(GetCheckBoxClickSoundNumber, PartsEngine_WidgetGetClickSoundNumber),
	    HLL_EXPORT(SetCheckBoxCGName, PE_SetCheckBoxCGName),
	    HLL_EXPORT(GetCheckBoxCGName, PE_GetCheckBoxCGName),
	    HLL_EXPORT(SetCheckBoxText, PE_SetCheckBoxText),
	    HLL_EXPORT(GetCheckBoxText, PE_GetCheckBoxText),
	    HLL_EXPORT(SetComponentCheckBoxShowLinkNumber, PE_SetComponentCheckBoxShowLinkNumber),
	    HLL_EXPORT(GetComponentCheckBoxShowLinkNumber, PE_GetComponentCheckBoxShowLinkNumber),
	    HLL_EXPORT(IsRadioButtonBoxExistGUI, PE_IsRadioButtonBoxExistGUI),
	    HLL_EXPORT(ClearRadioButtonBoxChild, PE_ClearRadioButtonBoxChild),
	    HLL_EXPORT(AddRadioButtonBoxChild, PE_AddRadioButtonBoxChild),
	    HLL_EXPORT(RemoveRadioButtonBoxChild, PE_RemoveRadioButtonBoxChild),
	    HLL_EXPORT(NumofRadioButtonBoxChild, PE_NumofRadioButtonBoxChild),
	    HLL_EXPORT(GetRadioButtonBoxChild, PE_GetRadioButtonBoxChild),
	    HLL_EXPORT(SetVScrollbarOnCursorSoundNumber, PartsEngine_WidgetSetOnCursorSoundNumber),
	    HLL_EXPORT(SetVScrollbarClickSoundNumber, PartsEngine_WidgetSetClickSoundNumber),
	    HLL_EXPORT(GetVScrollbarOnCursorSoundNumber, PartsEngine_WidgetGetOnCursorSoundNumber),
	    HLL_EXPORT(GetVScrollbarClickSoundNumber, PartsEngine_WidgetGetClickSoundNumber),
	    HLL_EXPORT(SetVScrollbarSize, PE_SetVScrollbarSize),
	    HLL_EXPORT(SetVScrollbarUpHeight, PE_SetVScrollbarUpHeight),
	    HLL_EXPORT(SetVScrollbarDownHeight, PE_SetVScrollbarDownHeight),
	    HLL_EXPORT(GetVScrollbarUpHeight, PE_GetVScrollbarUpHeight),
	    HLL_EXPORT(GetVScrollbarDownHeight, PE_GetVScrollbarDownHeight),
	    HLL_EXPORT(SetVScrollbarTotalSize, PE_SetVScrollbarTotalSize),
	    HLL_EXPORT(SetVScrollbarViewSize, PE_SetVScrollbarViewSize),
	    HLL_EXPORT(SetVScrollbarScrollPos, PE_SetVScrollbarScrollPos),
	    HLL_EXPORT(SetVScrollbarScrollRate, PE_SetVScrollbarScrollRate),
	    HLL_EXPORT(SetVScrollbarMoveSizeByButton, PE_SetVScrollbarMoveSizeByButton),
	    HLL_EXPORT(GetVScrollbarTotalSize, PE_GetVScrollbarTotalSize),
	    HLL_EXPORT(GetVScrollbarViewSize, PE_GetVScrollbarViewSize),
	    HLL_EXPORT(GetVScrollbarScrollPos, PE_GetVScrollbarScrollPos),
	    HLL_EXPORT(GetVScrollbarScrollRate, PE_GetVScrollbarScrollRate),
	    HLL_EXPORT(GetVScrollbarMoveSizeByButton, PE_GetVScrollbarMoveSizeByButton),
	    HLL_EXPORT(SetVScrollbarCGName, PE_SetVScrollbarCGName),
	    HLL_EXPORT(GetVScrollbarCGName, PE_GetVScrollbarCGName),
	    HLL_EXPORT(SetHScrollbarOnCursorSoundNumber, PartsEngine_WidgetSetOnCursorSoundNumber),
	    HLL_EXPORT(SetHScrollbarClickSoundNumber, PartsEngine_WidgetSetClickSoundNumber),
	    HLL_EXPORT(GetHScrollbarOnCursorSoundNumber, PartsEngine_WidgetGetOnCursorSoundNumber),
	    HLL_EXPORT(GetHScrollbarClickSoundNumber, PartsEngine_WidgetGetClickSoundNumber),
	    HLL_EXPORT(SetHScrollbarSize, PE_SetHScrollbarSize),
	    HLL_EXPORT(SetHScrollbarLeftWidth, PE_SetHScrollbarLeftWidth),
	    HLL_EXPORT(SetHScrollbarRightWidth, PE_SetHScrollbarRightWidth),
	    HLL_EXPORT(GetHScrollbarLeftWidth, PE_GetHScrollbarLeftWidth),
	    HLL_EXPORT(GetHScrollbarRightWidth, PE_GetHScrollbarRightWidth),
	    HLL_EXPORT(SetHScrollbarTotalSize, PE_SetHScrollbarTotalSize),
	    HLL_EXPORT(SetHScrollbarViewSize, PE_SetHScrollbarViewSize),
	    HLL_EXPORT(SetHScrollbarScrollPos, PE_SetHScrollbarScrollPos),
	    HLL_EXPORT(SetHScrollbarScrollRate, PE_SetHScrollbarScrollRate),
	    HLL_EXPORT(SetHScrollbarMoveSizeByButton, PE_SetHScrollbarMoveSizeByButton),
	    HLL_EXPORT(GetHScrollbarTotalSize, PE_GetHScrollbarTotalSize),
	    HLL_EXPORT(GetHScrollbarViewSize, PE_GetHScrollbarViewSize),
	    HLL_EXPORT(GetHScrollbarScrollPos, PE_GetHScrollbarScrollPos),
	    HLL_EXPORT(GetHScrollbarScrollRate, PE_GetHScrollbarScrollRate),
	    HLL_EXPORT(GetHScrollbarMoveSizeByButton, PE_GetHScrollbarMoveSizeByButton),
	    HLL_EXPORT(SetHScrollbarCGName, PE_SetHScrollbarCGName),
	    HLL_EXPORT(GetHScrollbarCGName, PE_GetHScrollbarCGName),
	    HLL_EXPORT(SetTextBoxSize, PartsEngine_WidgetSetSize),
	    HLL_EXPORT(SetTextBoxFontProperty, PartsEngine_WidgetSetFontProperty),
	    HLL_EXPORT(GetTextBoxFontProperty, PartsEngine_WidgetGetFontProperty),
	    HLL_EXPORT(SetTextBoxReadOnly, PE_SetTextBoxReadOnly),
	    HLL_EXPORT(IsTextBoxReadOnly, PE_IsTextBoxReadOnly),
	    HLL_EXPORT(SetTextBoxText, PE_SetTextBoxText),
	    HLL_EXPORT(GetTextBoxText, PartsEngine_GetTextBoxText_v12),
	    HLL_EXPORT(SelectTextBoxAll, PartsEngine_SelectTextBoxAll),
	    HLL_EXPORT(SetSpinBoxValue, PartsEngine_SetSpinBoxValue),
	    HLL_EXPORT(GetSpinBoxValue, PartsEngine_GetSpinBoxValue),
	    HLL_EXPORT(SelectSpinBoxAll, PartsEngine_SelectSpinBoxAll),
	    HLL_EXPORT(CancelFormClose, PartsEngine_CancelFormClose),
	    HLL_EXPORT(SetTextBoxMaxTextLength, PE_SetTextBoxMaxTextLength),
	    HLL_EXPORT(GetTextBoxMaxTextLength, PE_GetTextBoxMaxTextLength),
	    HLL_EXPORT(SetTextBoxBGColor, PartsEngine_WidgetStub_iii),
	    HLL_EXPORT(GetTextBoxBGR, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetTextBoxBGG, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetTextBoxBGB, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetTextBoxReadOnlyBGColor, PartsEngine_WidgetStub_iii),
	    HLL_EXPORT(GetTextBoxReadOnlyBGR, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetTextBoxReadOnlyBGG, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetTextBoxReadOnlyBGB, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetTextBoxFrameColor, PartsEngine_WidgetStub_iii),
	    HLL_EXPORT(GetTextBoxFrameR, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetTextBoxFrameG, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetTextBoxFrameB, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetTextBoxSelectColor, PartsEngine_WidgetStub_iii),
	    HLL_EXPORT(GetTextBoxSelectR, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetTextBoxSelectG, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetTextBoxSelectB, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetTextBoxCGName, PartsEngine_WidgetStub_s),
	    HLL_EXPORT(GetTextBoxCGName, PartsEngine_WidgetGetEmptyString),
	    HLL_EXPORT(SetTextBoxCharSpace, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(GetTextBoxCharSpace, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetOpenTextBoxIME, PartsEngine_WidgetStub_b),
	    HLL_EXPORT(IsOpenTextBoxIME, PartsEngine_WidgetGetFalse),
	    HLL_TODO_EXPORT(OpenTextBoxIME, PartsEngine_OpenTextBoxIME),
	    HLL_TODO_EXPORT(CloseTextBoxIME, PartsEngine_CloseTextBoxIME),
	    HLL_EXPORT(SetListBoxSize, PartsEngine_WidgetSetSize),
	    HLL_EXPORT(SetListBoxLineHeight, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(GetListBoxLineHeight, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetListBoxMargin, PartsEngine_WidgetStub_ii),
	    HLL_EXPORT(GetListBoxWidthMargin, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetListBoxHeightMargin, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetListBoxCGName, PartsEngine_WidgetStub_s),
	    HLL_EXPORT(GetListBoxCGName, PartsEngine_WidgetGetEmptyString),
	    HLL_EXPORT(SetListBoxScrollPos, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(GetListBoxScrollPos, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(AddListBoxItem, PartsEngine_WidgetStub_s),
	    HLL_EXPORT(InsertListBoxItem, PartsEngine_WidgetStub_is),
	    HLL_EXPORT(SetListBoxItem, PartsEngine_WidgetStub_is),
	    HLL_EXPORT(GetListBoxItemCount, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetListBoxItem, PartsEngine_WidgetGetItemEmptyString),
	    HLL_EXPORT(EraseListBoxItem, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(ClearListBoxItem, PartsEngine_SelectTextBoxAll),
	    HLL_EXPORT(GetListBoxOnCursorItemIndex, PartsEngine_WidgetGetMinusOne),
	    HLL_EXPORT(GetListBoxOnCursorItem, PartsEngine_WidgetGetEmptyString),
	    HLL_EXPORT(SetListBoxFontProperty, PartsEngine_WidgetSetFontProperty),
	    HLL_EXPORT(GetListBoxFontProperty, PartsEngine_WidgetGetFontProperty),
	    HLL_EXPORT(SetListBoxSelectIndex, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(GetListBoxSelectIndex, PartsEngine_WidgetGetMinusOne),
	    HLL_EXPORT(SetListBoxCharSpace, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(GetListBoxCharSpace, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetComboBoxSize, PartsEngine_WidgetSetSize),
	    HLL_EXPORT(SetComboBoxTextMargin, PartsEngine_WidgetStub_ii),
	    HLL_EXPORT(GetComboBoxTextWidthMargin, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetComboBoxTextHeightMargin, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetComboBoxCGName, PartsEngine_WidgetStub_s),
	    HLL_EXPORT(GetComboBoxCGName, PartsEngine_WidgetGetEmptyString),
	    HLL_EXPORT(SetComboBoxText, PartsEngine_WidgetStub_s),
	    HLL_EXPORT(GetComboBoxText, PartsEngine_WidgetGetEmptyString),
	    HLL_EXPORT(SetComboBoxFontProperty, PartsEngine_WidgetSetFontProperty),
	    HLL_EXPORT(GetComboBoxFontProperty, PartsEngine_WidgetGetFontProperty),
	    HLL_EXPORT(SetComboBoxCharSpace, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(GetComboBoxCharSpace, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetComboBoxListBoxHeight, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(SetComboBoxListBoxLineHeight, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(GetComboBoxListBoxHeight, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetComboBoxListBoxLineHeight, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetComboBoxListBoxMargin, PartsEngine_WidgetStub_ii),
	    HLL_EXPORT(GetComboBoxListBoxWidthMargin, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetComboBoxListBoxHeightMargin, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetComboBoxListBoxScrollPos, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(GetComboBoxListBoxScrollPos, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(AddComboBoxListBoxItem, PartsEngine_WidgetStub_s),
	    HLL_EXPORT(InsertComboBoxListBoxItem, PartsEngine_WidgetStub_is),
	    HLL_EXPORT(SetComboBoxListBoxItem, PartsEngine_WidgetStub_is),
	    HLL_EXPORT(GetComboBoxListBoxItemCount, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetComboBoxListBoxItem, PartsEngine_WidgetGetItemEmptyString),
	    HLL_EXPORT(EraseComboBoxListBoxItem, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(ClearComboBoxListBoxItem, PartsEngine_SelectTextBoxAll),
	    HLL_EXPORT(SetComboBoxListBoxFontProperty, PartsEngine_WidgetSetFontProperty),
	    HLL_EXPORT(GetComboBoxListBoxFontProperty, PartsEngine_WidgetGetFontProperty),
	    HLL_EXPORT(SetComboBoxListBoxSelectIndex, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(GetComboBoxListBoxSelectIndex, PartsEngine_WidgetGetMinusOne),
	    HLL_EXPORT(SetComboBoxListBoxCharSpace, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(GetComboBoxListBoxCharSpace, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(IsComboBoxOpenListBox, PartsEngine_WidgetGetFalse),
	    HLL_EXPORT(SetMultiTextBoxSize, PartsEngine_WidgetSetSize),
	    HLL_EXPORT(SetMultiTextBoxFontProperty, PartsEngine_WidgetSetFontProperty),
	    HLL_EXPORT(GetMultiTextBoxFontProperty, PartsEngine_WidgetGetFontProperty),
	    HLL_EXPORT(SetMultiTextBoxReadOnly, PartsEngine_WidgetStub_b),
	    HLL_EXPORT(IsMultiTextBoxReadOnly, PartsEngine_WidgetGetFalse),
	    HLL_EXPORT(SetMultiTextBoxText, PE_SetTextBoxText),
	    HLL_EXPORT(GetMultiTextBoxText, PartsEngine_GetTextBoxText_v12),
	    HLL_EXPORT(SetMultiTextBoxMaxTextLength, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(GetMultiTextBoxMaxTextLength, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetMultiTextBoxBGColor, PartsEngine_WidgetStub_iii),
	    HLL_EXPORT(GetMultiTextBoxBGR, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetMultiTextBoxBGG, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetMultiTextBoxBGB, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetMultiTextBoxReadOnlyBGColor, PartsEngine_WidgetStub_iii),
	    HLL_EXPORT(GetMultiTextBoxReadOnlyBGR, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetMultiTextBoxReadOnlyBGG, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetMultiTextBoxReadOnlyBGB, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetMultiTextBoxFrameColor, PartsEngine_WidgetStub_iii),
	    HLL_EXPORT(GetMultiTextBoxFrameR, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetMultiTextBoxFrameG, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetMultiTextBoxFrameB, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetMultiTextBoxSelectColor, PartsEngine_WidgetStub_iii),
	    HLL_EXPORT(GetMultiTextBoxSelectR, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetMultiTextBoxSelectG, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetMultiTextBoxSelectB, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetMultiTextBoxVScrollShow, PartsEngine_WidgetStub_b),
	    HLL_EXPORT(SetMultiTextBoxHScrollShow, PartsEngine_WidgetStub_b),
	    HLL_EXPORT(IsMultiTextBoxVScrollShow, PartsEngine_WidgetGetFalse),
	    HLL_EXPORT(IsMultiTextBoxHScrollShow, PartsEngine_WidgetGetFalse),
	    HLL_EXPORT(SetMultiTextBoxCGName, PartsEngine_WidgetStub_s),
	    HLL_EXPORT(GetMultiTextBoxCGName, PartsEngine_WidgetGetEmptyString),
	    HLL_EXPORT(SetMultiTextBoxCharSpace, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(GetMultiTextBoxCharSpace, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetMultiTextBoxLineSpace, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(GetMultiTextBoxLineSpace, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetMultiTextBoxPadding, PartsEngine_WidgetStub_iiii),
	    HLL_EXPORT(GetMultiTextBoxPaddingTop, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetMultiTextBoxPaddingBottom, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetMultiTextBoxPaddingLeft, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetMultiTextBoxPaddingRight, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetSpinBoxSize, PartsEngine_WidgetSetSize),
	    HLL_EXPORT(SetSpinBoxFontProperty, PartsEngine_WidgetSetFontProperty),
	    HLL_EXPORT(GetSpinBoxFontProperty, PartsEngine_WidgetGetFontProperty),
	    HLL_EXPORT(SetSpinBoxReadOnly, PartsEngine_WidgetStub_b),
	    HLL_EXPORT(IsSpinBoxReadOnly, PartsEngine_WidgetGetFalse),
	    HLL_EXPORT(SetSpinBoxDefaultValue, PartsEngine_WidgetStub_f),
	    HLL_EXPORT(GetSpinBoxDefaultValue, PartsEngine_WidgetGetFloatZero),
	    HLL_EXPORT(SetSpinBoxMaxValue, PartsEngine_WidgetStub_f),
	    HLL_EXPORT(GetSpinBoxMaxValue, PartsEngine_WidgetGetFloatZero),
	    HLL_EXPORT(SetSpinBoxMinValue, PartsEngine_WidgetStub_f),
	    HLL_EXPORT(GetSpinBoxMinValue, PartsEngine_WidgetGetFloatZero),
	    HLL_EXPORT(SetSpinBoxIncrementalValue, PartsEngine_WidgetStub_f),
	    HLL_EXPORT(GetSpinBoxIncrementalValue, PartsEngine_WidgetGetFloatZero),
	    HLL_EXPORT(SetSpinBoxDecimalPlace, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(GetSpinBoxDecimalPlace, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetSpinBoxBGColor, PartsEngine_WidgetStub_iii),
	    HLL_EXPORT(GetSpinBoxBGR, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetSpinBoxBGG, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetSpinBoxBGB, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetSpinBoxReadOnlyBGColor, PartsEngine_WidgetStub_iii),
	    HLL_EXPORT(GetSpinBoxReadOnlyBGR, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetSpinBoxReadOnlyBGG, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetSpinBoxReadOnlyBGB, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetSpinBoxFrameColor, PartsEngine_WidgetStub_iii),
	    HLL_EXPORT(GetSpinBoxFrameR, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetSpinBoxFrameG, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetSpinBoxFrameB, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetSpinBoxSelectColor, PartsEngine_WidgetStub_iii),
	    HLL_EXPORT(GetSpinBoxSelectR, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetSpinBoxSelectG, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetSpinBoxSelectB, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetSpinBoxCGName, PartsEngine_WidgetStub_s),
	    HLL_EXPORT(GetSpinBoxCGName, PartsEngine_WidgetGetEmptyString),
	    HLL_EXPORT(SetSpinBoxCharSpace, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(GetSpinBoxCharSpace, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(FormClose, PartsEngine_SelectTextBoxAll),
	    HLL_EXPORT(SetFormSize, PartsEngine_WidgetSetSize),
	    HLL_EXPORT(SetFormMinSize, PartsEngine_WidgetStub_ii),
	    HLL_EXPORT(GetFormMinWidth, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetFormMinHeight, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetFormMaxSize, PartsEngine_WidgetStub_ii),
	    HLL_EXPORT(GetFormMaxWidth, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetFormMaxHeight, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetFormResizable, PartsEngine_WidgetStub_b),
	    HLL_EXPORT(IsFormResizable, PartsEngine_WidgetGetFalse),
	    HLL_EXPORT(SetFormCaptionPlusHeight, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(GetFormCaptionPlusHeight, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetFormIcon, PartsEngine_WidgetStub_s),
	    HLL_EXPORT(GetFormIcon, PartsEngine_WidgetGetEmptyString),
	    HLL_EXPORT(SetFormTitle, PartsEngine_WidgetStub_s),
	    HLL_EXPORT(GetFormTitle, PartsEngine_WidgetGetEmptyString),
	    HLL_EXPORT(SetFormCaptionColor, PartsEngine_WidgetStub_iii),
	    HLL_EXPORT(GetFormCaptionR, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetFormCaptionG, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetFormCaptionB, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetFormClientColor, PartsEngine_WidgetStub_iii),
	    HLL_EXPORT(GetFormClientR, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetFormClientG, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetFormClientB, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetFormFold, PartsEngine_WidgetStub_b),
	    HLL_EXPORT(IsFormFold, PartsEngine_WidgetGetFalse),
	    HLL_EXPORT(SetFormEnableCloseButton, PartsEngine_WidgetStub_b),
	    HLL_EXPORT(IsFormEnableCloseButton, PartsEngine_WidgetGetFalse),
	    HLL_EXPORT(SetFormVScrollPos, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(GetFormVScrollPos, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetFormVScrollTotalSize, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetFormVScrollViewSize, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(SetFormHScrollPos, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(GetFormHScrollPos, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetFormHScrollTotalSize, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetFormHScrollViewSize, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(NumofFormGroupTable, PartsEngine_WidgetGetZero),
	    HLL_EXPORT(GetFormGroupViewSnap, PartsEngine_GetFormGroupViewSnap),
	    HLL_EXPORT(NumofFormGroupVList, PartsEngine_NumofFormGroupVList),
	    HLL_EXPORT(NumofFormGroupForm, PartsEngine_NumofFormGroupForm),
	    HLL_EXPORT(GetFormGroupFormNumber, PartsEngine_GetFormGroupFormNumber),
	    HLL_EXPORT(AddFormGroupTable, PartsEngine_SelectTextBoxAll),
	    HLL_EXPORT(SetFormGroupViewSpan, PartsEngine_SetFormGroupViewSpan),
	    HLL_EXPORT(AddFormGroupVList, PartsEngine_WidgetStub_i),
	    HLL_EXPORT(AddFormGroupFormNumber, PartsEngine_AddFormGroupFormNumber),
	    HLL_EXPORT(SetLayoutBoxLayoutType, PE_SetLayoutBoxLayoutType),
	    HLL_EXPORT(GetLayoutBoxLayoutType, PE_GetLayoutBoxLayoutType),
	    HLL_EXPORT(SetLayoutBoxReturn, PE_SetLayoutBoxReturn),
	    HLL_EXPORT(IsLayoutBoxReturn, PE_IsLayoutBoxReturn),
	    HLL_EXPORT(GetLayoutBoxReturnSize, PE_GetLayoutBoxReturnSize),
	    HLL_EXPORT(SetLayoutBoxReturnSizeForRate, PE_SetLayoutBoxReturnSizeForRate),
	    HLL_EXPORT(IsLayoutBoxReturnSizeForRate, PE_IsLayoutBoxReturnSizeForRate),
	    HLL_EXPORT(SetLayoutBoxAlign, PE_SetLayoutBoxAlign),
	    HLL_EXPORT(GetLayoutBoxAlign, PE_GetLayoutBoxAlign),
	    HLL_EXPORT(SetLayoutBoxPadding, PE_set_layoutbox_padding),
	    HLL_EXPORT(GetLayoutBoxPaddingTop, PE_get_layoutbox_padding_top),
	    HLL_EXPORT(GetLayoutBoxPaddingBottom, PE_get_layoutbox_padding_bottom),
	    HLL_EXPORT(GetLayoutBoxPaddingLeft, PE_get_layoutbox_padding_left),
	    HLL_EXPORT(GetLayoutBoxPaddingRight, PE_get_layoutbox_padding_right),
	    HLL_EXPORT(Parts_SetPartsCG, PE_SetPartsCG),
	    HLL_EXPORT(Parts_SetPartsCGThread, PartsEngine_SetPartsCGThread),
	    HLL_EXPORT(Parts_IsThreadLoading, PartsEngine_IsThreadLoading),
	    HLL_EXPORT(IsExistWorkerThread, PartsEngine_IsExistWorkerThread),
	    HLL_EXPORT(Parts_BuildPartsConstructionProcessThread, PE_BuildPartsConstructionProcessThread),
	    HLL_EXPORT(Parts_GetPartsCGName, PE_GetPartsCGName),
	    HLL_EXPORT(Parts_GetPartsCGDeform, PartsEngine_Parts_GetPartsCGDeform),
	    HLL_EXPORT(Parts_SetPartsCGSurfaceArea, PE_SetPartsCGSurfaceArea),
	    HLL_EXPORT(GetPartsCGSurfaceArea, PE_GetPartsCGSurfaceArea),
	    HLL_EXPORT(Parts_SetLoopCG, PE_SetLoopCG),
	    HLL_EXPORT(Parts_SetLoopCGSurfaceArea, PE_SetLoopCGSurfaceArea),
	    HLL_EXPORT(GetLoopCGSurfaceArea, PartsEngine_GetLoopCGSurfaceArea),
	    HLL_EXPORT(GetLoopCGCGName, PartsEngine_GetLoopCGCGName),
	    HLL_EXPORT(GetLoopCGStartNo, PartsEngine_GetLoopCGStartNo),
	    HLL_EXPORT(GetLoopCGNumofCG, PartsEngine_GetLoopCGNumofCG),
	    HLL_EXPORT(Parts_GetLoopCGTimeParCG, PartsEngine_Parts_GetLoopCGTimeParCG),
	    HLL_EXPORT(Parts_SetText, PE_SetText),
	    HLL_EXPORT(GetTextPartsText, PE_GetTextPartsText),
	    HLL_EXPORT(GetPartsTextFontProperty, PE_GetPartsTextFontProperty),
	    HLL_EXPORT(GetPartsTextSurfaceArea, PE_GetPartsTextSurfaceArea),
	    HLL_EXPORT(Parts_GetTextCharSpace, PE_GetTextCharSpace),
	    HLL_EXPORT(Parts_GetTextLineSpace, PE_GetTextLineSpace),
	    HLL_EXPORT(Parts_AddPartsText, PE_AddPartsText),
	    HLL_EXPORT(Parts_DeletePartsTopTextLine, PartsEngine_Parts_DeletePartsTopTextLine),
	    HLL_EXPORT(Parts_SetPartsTextSurfaceArea, PE_SetPartsTextSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsTextHighlight, PartsEngine_Parts_SetPartsTextHighlight),
	    HLL_EXPORT(Parts_AddPartsTextHighlight, PartsEngine_Parts_AddPartsTextHighlight),
	    HLL_EXPORT(Parts_ClearPartsTextHighlight, PartsEngine_Parts_ClearPartsTextHighlight),
	    HLL_EXPORT(Parts_SetPartsTextCountReturn, PartsEngine_Parts_SetPartsTextCountReturn),
	    HLL_EXPORT(Parts_GetPartsTextCountReturn, PartsEngine_Parts_GetPartsTextCountReturn),
	    HLL_EXPORT(Parts_SetTextShowTime, PartsEngine_Parts_SetTextShowTime),
	    HLL_EXPORT(Parts_GetTextShowTime, PartsEngine_Parts_GetTextShowTime),
	    HLL_EXPORT(Parts_SetTextPos, PartsEngine_Parts_SetTextPos),
	    HLL_EXPORT(Parts_GetTextPos, PartsEngine_Parts_GetTextPos),
	    HLL_EXPORT(Parts_SetTextEnableTag, PartsEngine_Parts_SetTextEnableTag),
	    HLL_EXPORT(Parts_IsTextEnableTag, PartsEngine_Parts_IsTextEnableTag),
	    HLL_EXPORT(Parts_FixText, PartsEngine_Parts_FixText),
	    HLL_EXPORT(Parts_IsFixedText, PartsEngine_Parts_IsFixedText),
	    HLL_EXPORT(Parts_SetFont, PE_SetFont),
	    HLL_EXPORT(Parts_SetPartsFontType, PE_SetPartsFontType),
	    HLL_EXPORT(Parts_SetPartsFontSize, PE_SetPartsFontSize),
	    HLL_EXPORT(Parts_SetPartsFontColor, PE_SetPartsFontColor),
	    HLL_EXPORT(Parts_SetPartsFontBoldWeight, PE_SetPartsFontBoldWeight),
	    HLL_EXPORT(Parts_SetPartsFontEdgeColor, PE_SetPartsFontEdgeColor),
	    HLL_EXPORT(Parts_SetPartsFontEdgeWeight, PE_SetPartsFontEdgeWeight),
	    HLL_EXPORT(Parts_SetTextCharSpace, PE_SetTextCharSpace),
	    HLL_EXPORT(Parts_SetTextLineSpace, PE_SetTextLineSpace),
	    HLL_EXPORT(Parts_SetRubyFont, PartsEngine_Parts_SetRubyFont),
	    HLL_EXPORT(Parts_GetRubyFont, PartsEngine_Parts_GetRubyFont),
	    HLL_EXPORT(Parts_SetRubyCharSpace, PartsEngine_Parts_SetRubyCharSpace),
	    HLL_EXPORT(Parts_GetRubyCharSpace, PartsEngine_Parts_GetRubyCharSpace),
	    HLL_EXPORT(Parts_SetHGaugeCG, PE_SetHGaugeCG),
	    HLL_EXPORT(Parts_GetHGaugeCG, PartsEngine_Parts_GetHGaugeCG),
	    HLL_EXPORT(Parts_SetHGaugeRate, PE_SetHGaugeRate),
	    HLL_EXPORT(Parts_GetHGaugeNumerator, PE_GetHGaugeNumerator),
	    HLL_EXPORT(Parts_GetHGaugeDenominator, PE_GetHGaugeDenominator),
	    HLL_EXPORT(Parts_SetHGaugeReverse, PE_SetHGaugeReverse),
	    HLL_EXPORT(Parts_IsHGaugeReverse, PartsEngine_Parts_IsHGaugeReverse),
	    HLL_EXPORT(Parts_SetVGaugeCG, PE_SetVGaugeCG),
	    HLL_EXPORT(Parts_GetVGaugeCG, PartsEngine_Parts_GetVGaugeCG),
	    HLL_EXPORT(Parts_SetVGaugeRate, PE_SetVGaugeRate),
	    HLL_EXPORT(Parts_GetVGaugeNumerator, PE_GetVGaugeNumerator),
	    HLL_EXPORT(Parts_GetVGaugeDenominator, PE_GetVGaugeDenominator),
	    HLL_EXPORT(Parts_SetVGaugeReverse, PE_SetVGaugeReverse),
	    HLL_EXPORT(Parts_IsVGaugeReverse, PartsEngine_Parts_IsVGaugeReverse),
	    HLL_EXPORT(Parts_SetHGaugeSurfaceArea, PE_SetHGaugeSurfaceArea),
	    HLL_EXPORT(GetHGaugeSurfaceArea, PartsEngine_GetHGaugeSurfaceArea),
	    HLL_EXPORT(Parts_SetVGaugeSurfaceArea, PE_SetVGaugeSurfaceArea),
	    HLL_EXPORT(GetVGaugeSurfaceArea, PartsEngine_GetVGaugeSurfaceArea),
	    HLL_EXPORT(Parts_SetNumeralCG, PE_SetNumeralCG),
	    HLL_EXPORT(Parts_GetNumeralCGName, PE_GetNumeralCGName),
	    HLL_EXPORT(Parts_GetNumeralCGType, PE_GetNumeralCGType),
	    HLL_EXPORT(Parts_GetNumeralNumber, PE_GetNumeralNumber),
	    HLL_EXPORT(Parts_IsNumeralShowComma, PE_IsNumeralShowComma),
	    HLL_EXPORT(Parts_GetNumeralSpace, PE_GetNumeralSpace),
	    HLL_EXPORT(IsNumerarlFullPitch, PartsEngine_IsNumerarlFullPitch),
	    HLL_EXPORT(Parts_SetNumeralLinkedCGNumberWidthWidthList, PE_SetNumeralLinkedCGNumberWidthWidthList),
	    HLL_OVERLOAD(Parts_SetNumeralFont, PartsEngine_ResolveSetNumeralFont),
	    HLL_EXPORT(Parts_SetNumeralNumber, PE_SetNumeralNumber),
	    HLL_EXPORT(Parts_SetNumeralShowComma, PE_SetNumeralShowComma),
	    HLL_EXPORT(Parts_SetNumeralSpace, PE_SetNumeralSpace),
	    HLL_EXPORT(Parts_SetNumeralLength, PE_SetNumeralLength),
	    HLL_EXPORT(Parts_SetNumeralSurfaceArea, PE_SetNumeralSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsRectangleDetectionSize, PE_SetPartsRectangleDetectionSize),
	    HLL_EXPORT(Parts_SetPartsRectangleDetectionSurfaceArea, PartsEngine_Parts_SetPartsRectangleDetectionSurfaceArea),
	    HLL_EXPORT(GetRectangleDetectionSurfaceArea, PartsEngine_GetRectangleDetectionSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsCGDetectionSize, PE_SetPartsCGDetectionSize),
	    HLL_EXPORT(Parts_GetPartsCGDetectionCGName, PartsEngine_Parts_GetPartsCGDetectionCGName),
	    HLL_EXPORT(Parts_SetPartsCGDetectionSurfaceArea, PartsEngine_Parts_SetPartsCGDetectionSurfaceArea),
	    HLL_EXPORT(GetCGDetectionSurfaceArea, PartsEngine_GetCGDetectionSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsFlat, PE_SetPartsFlat),
	    HLL_EXPORT(SetPartsFlat, PE_SetPartsFlat),
	    HLL_EXPORT(Parts_SetPartsFlatThread, PE_SetPartsFlat),
	    HLL_EXPORT(Parts_GetPartsFlatName, PE_GetPartsFlatName),
	    HLL_EXPORT(Parts_IsPartsFlatStop, PE_IsPartsFlatStop),
	    HLL_EXPORT(Parts_IsPartsFlatEnd, PE_IsPartsFlatEnd),
	    HLL_EXPORT(IsPartsFlatEnd, PE_IsPartsFlatEnd),
	    HLL_EXPORT(Parts_GetPartsFlatCurrentFrameNumber, PE_GetPartsFlatCurrentFrameNumber),
	    HLL_EXPORT(GetPartsFlatCurrentFrameNumber, PE_GetPartsFlatCurrentFrameNumber),
	    HLL_EXPORT(Parts_BackPartsFlatBeginFrame, PE_BackPartsFlatBeginFrame),
	    HLL_EXPORT(BackPartsFlatBeginFrame, PE_BackPartsFlatBeginFrame),
	    HLL_EXPORT(Parts_StepPartsFlatFinalFrame, PE_StepPartsFlatFinalFrame),
	    HLL_EXPORT(StepPartsFlatFinalFrame, PE_StepPartsFlatFinalFrame),
	    HLL_EXPORT(Parts_SetPartsFlatSurfaceArea, PE_SetPartsFlatSurfaceArea),
	    HLL_EXPORT(GetFlatSurfaceArea, PE_GetFlatSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsFlatAndStop, PE_SetPartsFlatAndStop),
	    HLL_EXPORT(SetPartsFlatAndStop, PE_SetPartsFlatAndStop),
	    HLL_EXPORT(Parts_StopPartsFlat, PE_StopPartsFlat),
	    HLL_EXPORT(StopPartsFlat, PE_StopPartsFlat),
	    HLL_EXPORT(Parts_StartPartsFlat, PE_StartPartsFlat),
	    HLL_EXPORT(StartPartsFlat, PE_StartPartsFlat),
	    HLL_EXPORT(Parts_GoFramePartsFlat, PE_GoFramePartsFlat),
	    HLL_EXPORT(GoFramePartsFlat, PE_GoFramePartsFlat),
	    HLL_EXPORT(Parts_GetPartsFlatEndFrame, PE_GetPartsFlatEndFrame),
	    HLL_EXPORT(GetPartsFlatEndFrame, PE_GetPartsFlatEndFrame),
	    HLL_EXPORT(SetFlatFPSRate, PE_SetFlatFPSRate),
	    HLL_EXPORT(GetFlatFPSRate, PE_GetFlatFPSRate),
	    HLL_EXPORT(GetFlatFPS, PE_GetFlatFPS),
	    HLL_EXPORT(Parts_SetFlatBeginSeekFrame, PartsEngine_Parts_SetFlatBeginSeekFrame),
	    HLL_EXPORT(Parts_GetFlatBeginSeekFrame, PE_GetFlatBeginSeekFrame),
	    HLL_EXPORT(Parts_GetFlatFrame, PE_GetFlatFrame),
	    HLL_EXPORT(Parts_JumpFlatLabel, PE_JumpFlatLabel),
	    HLL_EXPORT(Parts_ExistsFlatFile, PE_ExistsFlatFile),
	    HLL_EXPORT(ExistsFlatFile, PE_ExistsFlatFile),
	    HLL_EXPORT(Parts_GetPartsFlatDataInfo, PE_GetPartsFlatDataInfo),
	    HLL_EXPORT(NumofFlatCG, PE_NumofFlatCG),
	    HLL_EXPORT(GetFlatCGName, PE_GetFlatCGName),
	    HLL_EXPORT(Parts_ChangeFlatCG, PE_ChangeFlatCG),
	    HLL_EXPORT(GetProxyFlatCGName, PE_GetProxyFlatCGName),
	    HLL_EXPORT(ReturnChangeFlatCG, PE_ReturnChangeFlatCG),
	    HLL_EXPORT(ClearChangeFlatCG, PE_ClearChangeFlatCG),
	    HLL_EXPORT(NumofFlatSound, PE_NumofFlatSound),
	    HLL_EXPORT(GetFlatSoundName, PE_GetFlatSoundName),
	    HLL_OVERLOAD(Parts_ChangeFlatSound, PartsEngine_ResolveChangeFlatSound),
	    HLL_EXPORT(GetProxyFlatSound, PE_GetProxyFlatSound),
	    HLL_EXPORT(ReturnChangeFlatSound, PE_ReturnChangeFlatSound),
	    HLL_EXPORT(ClearChangeFlatSound, PE_ClearChangeFlatSound),
	    HLL_EXPORT(Parts_SetFlatEnableLayer, PE_SetFlatEnableLayer),
	    HLL_EXPORT(Parts_IsFlatEnableLayer, PE_IsFlatEnableLayer),
	    HLL_EXPORT(GetFlatGameViewWidth, PE_GetFlatGameViewWidth),
	    HLL_EXPORT(GetFlatGameViewHeight, PE_GetFlatGameViewHeight),
	    HLL_EXPORT(CreatePartsMovie, PartsEngine_CreatePartsMovie),
	    HLL_EXPORT(ReleasePartsMovie, PartsEngine_ReleasePartsMovie),
	    HLL_EXPORT(PlayPartsMovie, PartsEngine_PlayPartsMovie),
	    HLL_EXPORT(SetMovieTime, PartsEngine_SetMovieTime),
	    HLL_EXPORT(IsEndPartsMovie, PartsEngine_IsEndPartsMovie),
	    HLL_EXPORT(GetPartsMovieEndTime, PartsEngine_GetPartsMovieEndTime),
	    HLL_EXPORT(GetPartsMovieCurrentTime, PartsEngine_GetPartsMovieCurrentTime),
	    HLL_EXPORT(Parts_ClearPartsConstructionProcess, PE_ClearPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreateToPartsConstructionProcess, PE_AddCreateToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreatePixelOnlyToPartsConstructionProcess, PE_AddCreatePixelOnlyToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreateCGToProcess, PE_AddCreateCGToProcess),
	    HLL_EXPORT(Parts_AddFillToPartsConstructionProcess, PE_AddFillToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillAlphaColorToPartsConstructionProcess, PE_AddFillAlphaColorToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillAMapToPartsConstructionProcess, PE_AddFillAMapToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillWithAlphaToPartsConstructionProcess, PE_AddFillWithAlphaToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillGradationHorizonToPartsConstructionProcess, PE_AddFillGradationHorizonToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawRectToPartsConstructionProcess, PE_AddDrawRectToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawCutCGToPartsConstructionProcess, PE_AddDrawCutCGToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCopyCutCGToPartsConstructionProcess, PE_AddCopyCutCGToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddGrayFilterToPartsConstructionProcess, PE_AddGrayFilterToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddAddFilterToPartsConstructionProcess, PE_AddAddFilterToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddMulFilterToPartsConstructionProcess, PE_AddMulFilterToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawLineToPartsConstructionProcess, PE_AddDrawLineToPartsConstructionProcess),
	    HLL_EXPORT(Parts_BuildPartsConstructionProcess, PE_BuildPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawTextToPartsConstructionProcess, PE_AddDrawTextToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCopyTextToPartsConstructionProcess, PE_AddCopyTextToPartsConstructionProcess),
	    HLL_EXPORT(Parts_SetPartsConstructionSurfaceArea, PE_SetPartsConstructionSurfaceArea),
	    HLL_EXPORT(GetConstructionSurfaceArea, PartsEngine_GetConstructionSurfaceArea),
	    HLL_EXPORT(Parts_CreateParts3DLayerPluginID, PE_CreateParts3DLayerPluginID),
	    HLL_EXPORT(Parts_GetParts3DLayerPluginID, PE_GetParts3DLayerPluginID),
	    HLL_EXPORT(Parts_ReleaseParts3DLayerPluginID, PE_ReleaseParts3DLayerPluginID),
	    HLL_EXPORT(Parts_SetPassCursor, PE_SetPassCursor),
	    HLL_EXPORT(Parts_SetClickable, PE_SetClickable),
	    HLL_EXPORT(Parts_SetResetTimerByChangeInputStatus, PartsEngine_Parts_SetResetTimerByChangeInputStatus),
	    HLL_EXPORT(Parts_SetDrag, PE_SetDrag),
	    HLL_EXPORT(Parts_SetParentPartsNumber, PE_SetParentPartsNumber),
	    HLL_EXPORT(Parts_SetInputState, PE_SetInputState),
	    HLL_EXPORT(Parts_SetOnCursorShowLinkPartsNumber, PE_SetOnCursorShowLinkPartsNumber),
	    HLL_EXPORT(Parts_SetSoundNumber, PartsEngine_SetSoundNumber),
	    HLL_EXPORT(Parts_SetPartsPixelDecide, PE_SetPartsPixelDecide),
	    HLL_EXPORT(Parts_GetPartsPassCursor, PE_GetPartsPassCursor),
	    HLL_EXPORT(Parts_GetPartsClickable, PE_GetPartsClickable),
	    HLL_EXPORT(Parts_GetResetTimerByChangeInputStatus, PartsEngine_Parts_GetResetTimerByChangeInputStatus),
	    HLL_EXPORT(Parts_GetPartsDrag, PartsEngine_Parts_GetPartsDrag),
	    HLL_EXPORT(Parts_SetSwipeType, PartsEngine_Parts_SetSwipeType),
	    HLL_EXPORT(Parts_GetSwipeType, PartsEngine_Parts_GetSwipeType),
	    HLL_EXPORT(Parts_StopSwipe, PartsEngine_Parts_StopSwipe),
	    HLL_EXPORT(Parts_GetParentPartsNumber, PE_GetParentPartsNumber),
	    HLL_EXPORT(Parts_GetInputState, PE_GetInputState),
	    HLL_EXPORT(Parts_GetOnCursorShowLinkPartsNumber, PE_GetOnCursorShowLinkPartsNumber),
	    HLL_EXPORT(Parts_GetSoundNumber, PartsEngine_GetSoundNumber),
	    HLL_EXPORT(Parts_IsPartsPixelDecide, PartsEngine_Parts_IsPartsPixelDecide),
	    HLL_EXPORT(Parts_SetLockUpdate, PartsEngine_Parts_SetLockUpdate),
	    HLL_EXPORT(Parts_IsLockUpdate, PartsEngine_Parts_IsLockUpdate),
	    HLL_EXPORT(Parts_IsResizable, PartsEngine_Parts_IsResizable),
	    HLL_EXPORT(SetComponentVibrationPos, PartsEngine_SetComponentVibrationPos),
	    HLL_EXPORT(SetComponentShowEditor, PartsEngine_SetComponentShowEditor),
	    HLL_EXPORT(IsComponentShowEditor, PartsEngine_IsComponentShowEditor),
	    HLL_EXPORT(Parts_IsCursorIn, PE_IsCursorIn),
	    HLL_EXPORT(SaveParts, PartsEngine_SaveParts),
	    HLL_EXPORT(LoadParts, PartsEngine_LoadParts),
	    HLL_EXPORT(Debug_GetSpriteRect, PartsEngine_Debug_GetSpriteRect)
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
	parts_v2 = (ain->version >= 12
		|| get_fun(libno, "GetControllerID") != NULL
		|| get_fun(libno, "PartsFunc") != NULL);
	parts_hierarchical_matrix = (ain->version >= 11
		|| get_fun(libno, "UpdateMatrix") != NULL);

	if (ain->version >= 11) {
		static_library_replace(&lib_PartsEngine, "SetComponentType", PartsEngine_SetComponentType_v12);
		static_library_replace(&lib_PartsEngine, "GetComponentType", PartsEngine_GetComponentType_v12);
	}
	fun = get_fun(libno, "UpdateComponent");
	if (fun && fun->nr_arguments == 5) {
		if (fun->arguments[1].type.data == AIN_BOOL) {
			static_library_replace(&lib_PartsEngine, "UpdateComponent", PartsEngine_Update_v8);
		} else {
			static_library_replace(&lib_PartsEngine, "UpdateComponent", PartsEngine_Update_v12);
		}
	}
	fun = get_fun(libno, "UpdateMatrix");
	if (fun && fun->nr_arguments == 3) {
		static_library_replace(&lib_PartsEngine, "UpdateMatrix", PartsEngine_UpdateMatrix_v8);
	} else if (fun && fun->nr_arguments == 1) {
		static_library_replace(&lib_PartsEngine, "UpdateMatrix", PartsEngine_UpdateMatrix);
	}
	fun = get_fun(libno, "Parts_GetPartsCGName");
	if (fun && fun->return_type.data == AIN_STRING) {
		static_library_replace(&lib_PartsEngine, "Parts_GetPartsCGName", PartsEngine_GetPartsCGName_v12);
	}
	fun = get_fun(libno, "GetMessageVariableString");
	if (fun && fun->nr_arguments == 1 && fun->return_type.data == AIN_STRING) {
		static_library_replace(&lib_PartsEngine, "GetMessageVariableString", PartsEngine_GetMessageVariableString_v12);
	}
	fun = get_fun(libno, "GetActivityIntentDataDestination");
	if (fun && fun->nr_arguments == 3 && fun->return_type.data == AIN_STRING) {
		static_library_replace(&lib_PartsEngine, "GetActivityIntentDataDestination", PE_GetActivityIntentDataDestination);
	}

	if (parts_v2) {
#define BUTTON_API(name) static_library_replace(&lib_PartsEngine, #name, PE_##name)
		BUTTON_API(SetButtonSize);
		BUTTON_API(SetButtonEnable);
		BUTTON_API(IsButtonEnable);
		BUTTON_API(SetButtonColor);
		BUTTON_API(GetButtonR);
		BUTTON_API(GetButtonG);
		BUTTON_API(GetButtonB);
		BUTTON_API(SetButtonFontProperty);
		BUTTON_API(GetButtonFontProperty);
		BUTTON_API(SetButtonCGName);
		BUTTON_API(GetButtonCGName);
		BUTTON_API(SetButtonText);
		BUTTON_API(GetButtonText);
#undef BUTTON_API
	}

	fun = get_fun(libno, "SetLayoutBoxReturn");
	if (fun && fun->nr_arguments >= 3 && fun->arguments[2].type.data == AIN_FLOAT) {
		static_library_replace(&lib_PartsEngine, "SetLayoutBoxReturn", PE_SetLayoutBoxReturnF);
	}
	fun = get_fun(libno, "GetLayoutBoxReturnSize");
	if (fun && fun->return_type.data == AIN_FLOAT) {
		static_library_replace(&lib_PartsEngine, "GetLayoutBoxReturnSize", PE_GetLayoutBoxReturnSizeF);
	}
	fun = get_fun(libno, "AddMotionSound");
	if (fun && fun->nr_arguments == 2 && fun->arguments[0].type.data == AIN_STRING) {
		static_library_replace(&lib_PartsEngine, "AddMotionSound", PE_AddMotionSound_name);
	}

#define REPLACE_IF_VOID(hll_name, fn_ptr) do { \
	fun = get_fun(libno, hll_name); \
	if (fun && fun->return_type.data == AIN_VOID) \
		static_library_replace(&lib_PartsEngine, hll_name, fn_ptr); \
} while (0)
	REPLACE_IF_VOID("GetClickMissSoundName", PartsEngine_GetClickMissSoundName_v6);
	REPLACE_IF_VOID("Parts_GetSoundName", PartsEngine_Parts_GetSoundName_v6);
	REPLACE_IF_VOID("Parts_GetPartsFlatDataInfo", PartsEngine_Parts_GetPartsFlatDataInfo_v6);
	REPLACE_IF_VOID("GetButtonCGName", PartsEngine_GetButtonCGName_v6);
	REPLACE_IF_VOID("GetButtonFlatName", PartsEngine_GetButtonFlatName_v6);
	REPLACE_IF_VOID("GetButtonText", PartsEngine_GetButtonText_v6);
	REPLACE_IF_VOID("GetCheckBoxCGName", PartsEngine_GetCheckBoxCGName_v6);
	REPLACE_IF_VOID("GetCheckBoxFlatName", PartsEngine_GetCheckBoxFlatName_v6);
	REPLACE_IF_VOID("GetCheckBoxText", PartsEngine_GetCheckBoxText_v6);
	REPLACE_IF_VOID("GetVScrollbarCGName", PartsEngine_GetVScrollbarCGName_v6);
	REPLACE_IF_VOID("GetVScrollbarFlatName", PartsEngine_GetVScrollbarFlatName_v6);
	REPLACE_IF_VOID("GetHScrollbarCGName", PartsEngine_GetHScrollbarCGName_v6);
	REPLACE_IF_VOID("GetHScrollbarFlatName", PartsEngine_GetHScrollbarFlatName_v6);
	REPLACE_IF_VOID("GetTextBoxText", PartsEngine_GetTextBoxText_v6);
	REPLACE_IF_VOID("GetMultiTextBoxText", PartsEngine_GetTextBoxText_v6);
	REPLACE_IF_VOID("GetTextBoxCGName", PartsEngine_WidgetGetEmptyString_v6);
	REPLACE_IF_VOID("GetListBoxCGName", PartsEngine_WidgetGetEmptyString_v6);
	REPLACE_IF_VOID("GetListBoxOnCursorItem", PartsEngine_WidgetGetEmptyString_v6);
	REPLACE_IF_VOID("GetComboBoxCGName", PartsEngine_WidgetGetEmptyString_v6);
	REPLACE_IF_VOID("GetComboBoxText", PartsEngine_WidgetGetEmptyString_v6);
	REPLACE_IF_VOID("GetMultiTextBoxCGName", PartsEngine_WidgetGetEmptyString_v6);
	REPLACE_IF_VOID("GetSpinBoxCGName", PartsEngine_WidgetGetEmptyString_v6);
	REPLACE_IF_VOID("GetMessageWindowCGName", PartsEngine_GetMessageWindowCGName_v6);
	REPLACE_IF_VOID("GetMessageWindowFlatName", PartsEngine_GetMessageWindowFlatName_v6);
	REPLACE_IF_VOID("GetMessageWindowText", PartsEngine_GetMessageWindowText_v6);
	REPLACE_IF_VOID("GetActivityEXText", PartsEngine_GetActivityEXText_v6);
	REPLACE_IF_VOID("GetListBoxItem", PartsEngine_WidgetGetItemEmptyString_v6);
	REPLACE_IF_VOID("GetComboBoxListBoxItem", PartsEngine_WidgetGetItemEmptyString_v6);
#undef REPLACE_IF_VOID

	fun = get_fun(libno, "AddMotionHGaugeRate");
	if (fun && fun->nr_arguments >= 7) {
		if (fun->arguments[1].type.data == AIN_INT) {
			static_library_replace(&lib_PartsEngine, "AddMotionHGaugeRate",
					fun->nr_arguments == 8
						? (void *)PartsEngine_AddMotionHGaugeRate_int_curve
						: (void *)PartsEngine_AddMotionHGaugeRate_int);
		} else if (fun->nr_arguments == 7 && fun->arguments[1].type.data == AIN_FLOAT) {
			static_library_replace(&lib_PartsEngine, "AddMotionHGaugeRate",
					PE_AddMotionHGaugeRate);
		}
	}
	fun = get_fun(libno, "AddMotionVGaugeRate");
	if (fun && fun->nr_arguments >= 7) {
		if (fun->arguments[1].type.data == AIN_INT) {
			static_library_replace(&lib_PartsEngine, "AddMotionVGaugeRate",
					fun->nr_arguments == 8
						? (void *)PartsEngine_AddMotionVGaugeRate_int_curve
						: (void *)PartsEngine_AddMotionVGaugeRate_int);
		} else if (fun->nr_arguments == 7 && fun->arguments[1].type.data == AIN_FLOAT) {
			static_library_replace(&lib_PartsEngine, "AddMotionVGaugeRate",
					PE_AddMotionVGaugeRate);
		}
	}

#define REPLACE_IF_VOID_OUT0(hll_name, fn_ptr) do { \
	fun = get_fun(libno, hll_name); \
	if (fun && fun->return_type.data == AIN_VOID \
			&& fun->nr_arguments >= 1 \
			&& fun->arguments[0].type.data == AIN_REF_STRING) \
		static_library_replace(&lib_PartsEngine, hll_name, fn_ptr); \
} while (0)
	REPLACE_IF_VOID_OUT0("GetActivityPartsName", PartsEngine_GetActivityPartsName_v6);
	REPLACE_IF_VOID_OUT0("GetLoopCGCGName", PartsEngine_GetLoopCGCGName_v6);
	REPLACE_IF_VOID_OUT0("GetTextPartsText", PartsEngine_GetTextPartsText_v6);
	REPLACE_IF_VOID_OUT0("Parts_GetHGaugeCG", PartsEngine_Parts_GetHGaugeCG_v6);
	REPLACE_IF_VOID_OUT0("Parts_GetVGaugeCG", PartsEngine_Parts_GetVGaugeCG_v6);
	REPLACE_IF_VOID_OUT0("Parts_GetNumeralCGName", PartsEngine_Parts_GetNumeralCGName_v6);
	REPLACE_IF_VOID_OUT0("Parts_GetPartsCGDetectionCGName", PartsEngine_Parts_GetPartsCGDetectionCGName_v6);
	REPLACE_IF_VOID_OUT0("Parts_GetPartsFlatName", PartsEngine_Parts_GetPartsFlatName_v6);
	REPLACE_IF_VOID_OUT0("GetFlatCGName", PartsEngine_GetFlatCGName_v6);
	REPLACE_IF_VOID_OUT0("GetProxyFlatCGName", PartsEngine_GetProxyFlatCGName_v6);
	REPLACE_IF_VOID_OUT0("GetFlatSoundName", PartsEngine_GetFlatSoundName_v6);
	REPLACE_IF_VOID_OUT0("GetProxyFlatSound", PartsEngine_GetProxyFlatSound_v6);
#undef REPLACE_IF_VOID_OUT0

	fun = get_fun(libno, "LoadActivityEXText");
	if (fun && fun->nr_arguments == 2) {
		static_library_replace(&lib_PartsEngine, "LoadActivityEXText",
				PartsEngine_LoadActivityEXText_v6);
	}
	fun = get_fun(libno, "GetComponentAbsolutePos");
	if (fun && fun->nr_arguments == 10) {
		static_library_replace(&lib_PartsEngine, "GetComponentAbsolutePos",
				PE_GetComponentAbsoluteSquarePos);
	}
	fun = get_fun(libno, "PlayPartsMovie");
	if (fun && fun->nr_arguments == 2) {
		static_library_replace(&lib_PartsEngine, "PlayPartsMovie",
				PartsEngine_PlayPartsMovie_v6);
	}
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
	fun = get_fun(libno, "Save");
	if (fun && fun->nr_arguments == 2) {
		static_library_replace(&lib_PartsEngine, "Save",
				PartsEngine_Save_v14);
	}
	fun = get_fun(libno, "AddPartsConstructionProcess");
	if (fun && fun->nr_arguments == 3) {
		static_library_replace(&lib_PartsEngine, "AddPartsConstructionProcess",
				PartsEngine_AddPartsConstructionProcess_v6);
	} else if (fun && fun->nr_arguments == 6) {
		static_library_replace(&lib_PartsEngine, "AddPartsConstructionProcess",
				PartsEngine_AddPartsConstructionProcess_v14);
	}
	fun = get_fun(libno, "GetPartsConstructionProcess");
	if (fun && fun->nr_arguments == 3) {
		static_library_replace(&lib_PartsEngine, "GetPartsConstructionProcess",
				PartsEngine_GetPartsConstructionProcess_v6);
	} else if (fun && fun->nr_arguments == 7) {
		static_library_replace(&lib_PartsEngine, "GetPartsConstructionProcess",
				PartsEngine_GetPartsConstructionProcess_v14);
	}
	if (get_fun(libno, "AddController")) {
		PE_enable_multi_controller();
	}
}
