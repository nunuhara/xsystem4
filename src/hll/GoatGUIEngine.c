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

#include "system4/hashtable.h"
#include <cglm/cglm.h>

#include "gfx/gfx.h"
#include "queue.h"
#include "sprite.h"
#include "xsystem4.h"
#include "hll.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

static inline float deg2rad(float deg)
{
	return deg * (M_PI / 180.0);
}

enum parts_motion_type {
	PARTS_MOTION_POS,
	PARTS_MOTION_ALPHA,
	//PARTS_MOTION_CG,
	//PARTS_MOTION_HGUAGE_RATE,
	//PARTS_MOTION_VGUAGE_RATE,
	//PARTS_MOTION_NUMERAL_NUMBER,
	PARTS_MOTION_MAG_X,
	PARTS_MOTION_MAG_Y,
	PARTS_MOTION_ROTATE_X,
	PARTS_MOTION_ROTATE_Y,
	PARTS_MOTION_ROTATE_Z,
	//PARTS_MOTION_VIBRATION_SIZE,
	//PARTS_MOTION_SOUND,
	PARTS_NR_MOTION_TYPES
};

union parts_motion_param {
	int i;
	float f;
	struct {
		int x;
		int y;
	};
};

struct parts_motion {
	TAILQ_ENTRY(parts_motion) entry;
	enum parts_motion_type type;
	union parts_motion_param begin;
	union parts_motion_param end;
	int begin_time;
	int end_time;
};

struct parts {
	struct sact_sprite sp;
	int no;
	int sprite_deform;
	int state;
	bool clickable;
	int on_cursor_sound;
	int on_click_sound;
	int origin_mode;
	struct { float x, y; } scale;
	struct { float x, y, z; } rotation;
	TAILQ_HEAD(, parts_motion) motion;
};

static void parts_render_motion(struct sact_sprite *sp);

static struct hash_table *parts_table = NULL;

static bool GoatGUIEngine_Init(void)
{
	parts_table = ht_create(1024);
	return 1;
}

static struct parts *parts_alloc(void)
{
	struct parts *parts = xcalloc(1, sizeof(struct parts));
	parts->origin_mode = 1;
	parts->scale.x = 1.0;
	parts->scale.y = 1.0;
	parts->rotation.x = 0.0;
	parts->rotation.y = 0.0;
	parts->rotation.z = 0.0;
	parts->sp.z = 0;
	parts->sp.z2 = 2;
	TAILQ_INIT(&parts->motion);
	parts->sp.render = parts_render_motion;
	return parts;
}

static struct parts *parts_get(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (slot->value)
		return slot->value;

	struct parts *parts = parts_alloc();
	parts->no = parts_no;
	slot->value = parts;
	return parts;
}

possibly_unused static struct parts *parts_try_get(int parts_no)
{
	return ht_get_int(parts_table, parts_no, NULL);
}

static struct parts_motion *parts_motion_alloc(enum parts_motion_type type, int begin_time, int end_time)
{
	struct parts_motion *motion = xcalloc(1, sizeof(struct parts_motion));
	motion->type = type;
	motion->begin_time = begin_time;
	motion->end_time = end_time;
	return motion;
}

static void parts_motion_free(struct parts_motion *motion)
{
	free(motion);
}

static void parts_clear_motion(struct parts *parts)
{
	while (!TAILQ_EMPTY(&parts->motion)) {
		struct parts_motion *motion = TAILQ_FIRST(&parts->motion);
		TAILQ_REMOVE(&parts->motion, motion, entry);
		parts_motion_free(motion);
	}
}

static void parts_clear(struct parts *parts)
{
	parts_clear_motion(parts);
	sprite_free(&parts->sp);
}

static void parts_release(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (!slot->value)
		return;

	parts_clear(slot->value);
	free(slot->value);
	slot->value = NULL;
}

static bool is_motion = false;
static int motion_t = 0;
static int motion_end_t = 0;

static void parts_add_motion(struct parts *parts, struct parts_motion *motion)
{
	struct parts_motion *p;
	TAILQ_FOREACH(p, &parts->motion, entry) {
		if (p->begin_time > motion->begin_time) {
			TAILQ_INSERT_BEFORE(p, motion, entry);
			return;
		}
	}
	TAILQ_INSERT_TAIL(&parts->motion, motion, entry);
	if (motion->end_time > motion_end_t)
		motion_end_t = motion->end_time;
	// FIXME? What happens if we add a motion while is_motion=true?
	//        Should we call parts_update_motion here?
}

static inline float motion_progress(struct parts_motion *m, int t)
{
	return (float)(t - m->begin_time) / (m->end_time - m->begin_time);
}

static int motion_calculate_i(struct parts_motion *m, int t)
{
	if (t >= m->end_time)
		return m->end.i;

	const int delta = m->end.i - m->begin.i;
	const float progress = motion_progress(m, t);
	return m->begin.i + (delta * progress);
}

static float motion_calculate_f(struct parts_motion *m, int t)
{
	if (t >= m->end_time)
		return m->end.f;

	const float delta = m->end.f - m->begin.f;
	const float progress = motion_progress(m, t);
	return m->begin.f + (delta * progress);
}

static Point motion_calculate_point(struct parts_motion *m, int t)
{
	if (t >= m->end_time)
		return (Point) { m->end.x, m->end.y };

	const int delta_x = m->end.x - m->begin.x;
	const int delta_y = m->end.y - m->begin.y;
	const float progress = motion_progress(m, t);
	return (Point) {
		m->begin.x + (delta_x * progress),
		m->begin.y + (delta_y * progress)
	};
}

static void origin_mode_translate(mat4 dst, int mode, int w, int h)
{
	vec3 off =  { 0, 0, 0 };
	switch (mode) {
	case 1: return;
	case 2: off[0] = -w/2; off[1] = 0;    break;
	case 3: off[0] = -w;   off[1] = -h/2; break;
	case 4: off[0] = 0;    off[1] = -h/2; break;
	case 5: off[0] = -w/2; off[1] = -h/2; break;
	case 6: off[0] = -w;   off[1] = -h/2; break;
	case 7: off[0] = 0;    off[1] = -h;   break;
	case 8: off[0] = -w/2; off[1] = -h;   break;
	case 9: off[0] = -w;   off[1] = -h;   break;
	default:
		// why...
		off[0] = mode;
		off[1] = (3*h)/4;
		break;
	}

	glm_translate(dst, off);
}

static void parts_render_motion(struct sact_sprite *sp)
{
	struct parts *parts = (struct parts*)sp;
	if (!parts->sp.cg_no)
		return;

	Rectangle *d = &parts->sp.rect;
	mat4 mw_transform = GLM_MAT4_IDENTITY_INIT;
	glm_translate(mw_transform, (vec3) { d->x, d->y, 0 });
	// FIXME: need perspective for 3D rotate
	//glm_rotate_x(mw_transform, parts->rotation.x, mw_transform);
	//glm_rotate_y(mw_transform, parts->rotation.y, mw_transform);
	glm_rotate_z(mw_transform, parts->rotation.z, mw_transform);
	glm_scale(mw_transform, (vec3){ parts->scale.x, parts->scale.y, 1.0 });
	origin_mode_translate(mw_transform, parts->origin_mode, d->w, d->h);
	//glm_scale(mw_transform, (vec3){ d->w * parts->scale.x, d->h * parts->scale.y, 1.0 });
	glm_scale(mw_transform, (vec3){ d->w, d->h, 1.0 });
	mat4 wv_transform = WV_TRANSFORM(config.view_width, config.view_height);

	struct gfx_render_job job = {
		.shader = NULL, // default shader
		.texture = parts->sp.texture.handle,
		.world_transform = mw_transform[0],
		.view_transform = wv_transform[0],
		.data = &parts->sp.texture,
	};

	gfx_render(&job);
}

static void parts_update_with_motion(struct parts *parts, struct parts_motion *motion)
{
	switch (motion->type) {
	case PARTS_MOTION_POS: {
		Point pt = motion_calculate_point(motion, motion_t);
		sprite_set_pos(&parts->sp, pt.x, pt.y);
		break;
	}
	case PARTS_MOTION_ALPHA:
		sprite_set_blend_rate(&parts->sp, motion_calculate_i(motion, motion_t));
		break;
	case PARTS_MOTION_MAG_X:
		parts->scale.x = motion_calculate_f(motion, motion_t);
		sprite_dirty(&parts->sp);
		break;
	case PARTS_MOTION_MAG_Y:
		parts->scale.y = motion_calculate_f(motion, motion_t);
		sprite_dirty(&parts->sp);
		break;
		/*
		  case PARTS_MOTION_ROTATE_X:
		  parts->rotation.x = motion_calculate_f(motion, motion_t);
		  sprite_dirty(&parts->sp);
		  break;
		  case PARTS_MOTION_ROTATE_Y:
		  parts->rotation.y = motion_calculate_f(motion, motion_t);
		  sprite_dirty(&parts->sp);
		  break;
		*/
	case PARTS_MOTION_ROTATE_Z:
		parts->rotation.z = motion_calculate_f(motion, motion_t);
		sprite_dirty(&parts->sp);
		break;
	default:
		WARNING("Invalid motion type: %d", motion->type);
	}
}

static void parts_update_motion(struct parts *parts)
{
	struct parts_motion *motion;
	TAILQ_FOREACH(motion, &parts->motion, entry) {
		if (motion->begin_time > motion_t)
			break;
		// FIXME? What if a motion begins and ends within the span of another?
		//        This implementation will cancel the earlier motion and remain
		//        at the end-state of the second motion.
		parts_update_with_motion(parts, motion);
	}
}

static void parts_init_motion(struct parts *parts)
{
	if (!parts)
		return;

	bool initialized[PARTS_NR_MOTION_TYPES] = {0};

	struct parts_motion *motion;
	TAILQ_FOREACH(motion, &parts->motion, entry) {
		if (!initialized[motion->type]) {
			parts_update_with_motion(parts, motion);
			initialized[motion->type] = true;
		}
	}
}

static void render_all_iter(void *parts)
{
	if (!parts)
		return;
	parts_update_motion(parts);
	parts_render_motion(parts);
}

static void parts_update_all_motion(void)
{
	// TODO? Could optimize by keeping a linked list of parts with motion data
	ht_foreach_value(parts_table, render_all_iter);
}

static void init_all_iter(void *parts)
{
	if (!parts)
		return;
	parts_init_motion(parts);
	parts_render_motion(parts);
}

static void parts_init_all_motion(void)
{
	ht_foreach_value(parts_table, init_all_iter);
}

static void fini_all_iter(void *parts)
{
	if (!parts)
		return;
	parts_clear_motion(parts);
}

static void parts_fini_all_motion(void)
{
	ht_foreach_value(parts_table, fini_all_iter);
}

static void GoatGUIEngine_Update(possibly_unused int passed_time, possibly_unused bool message_window_show)
{
	// ???
}

static bool GoatGUIEngine_SetPartsCG(int parts_no, int cg_no, possibly_unused int sprite_deform, possibly_unused int state)
{
	struct parts *parts = parts_get(parts_no);
	if (!sprite_set_cg(&parts->sp, cg_no))
		return false;
	return true;
}

static int GoatGUIEngine_GetPartsCGNumber(int parts_no, possibly_unused int state)
{
	return parts_get(parts_no)->sp.cg_no;
}

bool GoatGUIEngine_SetLoopCG(int PartsNumber, int CGNumber, int NumofCG, int TimePerCG, int State);
bool GoatGUIEngine_SetText(int PartsNumber, struct string *pIText, int State);
bool GoatGUIEngine_AddPartsText(int PartsNumber, struct string *pIText, int State);
bool GoatGUIEngine_DeletePartsTopTextLine(int PartsNumber, int State);

static bool GoatGUIEngine_SetFont(int parts_no, int type, int size, int r, int g, int b, float bold_weight, int edge_r, int edge_g, int edge_b, float edge_weight, int state)
{
	// TODO
	return true;
}

bool GoatGUIEngine_SetPartsFontType(int PartsNumber, int Type, int State);
bool GoatGUIEngine_SetPartsFontSize(int PartsNumber, int Size, int State);
bool GoatGUIEngine_SetPartsFontColor(int PartsNumber, int R, int G, int B, int State);
bool GoatGUIEngine_SetPartsFontBoldWeight(int PartsNumber, float BoldWeight, int State);
bool GoatGUIEngine_SetPartsFontEdgeColor(int PartsNumber, int R, int G, int B, int State);
bool GoatGUIEngine_SetPartsFontEdgeWeight(int PartsNumber, float EdgeWeight, int State);

static bool GoatGUIEngine_SetTextCharSpace(int parts_no, int char_space, int state)
{
	// TODO
	return true;
}

bool GoatGUIEngine_SetTextLineSpace(int PartsNumber, int LineSpace, int State);
bool GoatGUIEngine_SetHGaugeCG(int PartsNumber, int CGNumber, int State);
bool GoatGUIEngine_SetHGaugeRate(int PartsNumber, int Numerator, int Denominator, int State);
bool GoatGUIEngine_SetVGaugeCG(int PartsNumber, int CGNumber, int State);
bool GoatGUIEngine_SetVGaugeRate(int PartsNumber, int Numerator, int Denominator, int State);
bool GoatGUIEngine_SetNumeralCG(int PartsNumber, int CGNumber, int State);
bool GoatGUIEngine_SetNumeralLinkedCGNumberWidthWidthList(int PartsNumber, int CGNumber, int Width0, int Width1, int Width2, int Width3, int Width4, int Width5, int Width6, int Width7, int Width8, int Width9, int WidthMinus, int WidthComma, int State);
bool GoatGUIEngine_SetNumeralNumber(int PartsNumber, int Number, int State);
bool GoatGUIEngine_SetNumeralShowComma(int PartsNumber, bool ShowComma, int State);
bool GoatGUIEngine_SetNumeralSpace(int PartsNumber, int NumeralSpace, int State);
bool GoatGUIEngine_SetPartsRectangleDetectionSize(int PartsNumber, int Width, int Height, int State);
bool GoatGUIEngine_SetPartsFlash(int PartsNumber, struct string *pIFlashFileName, int State);
bool GoatGUIEngine_IsPartsFlashEnd(int PartsNumber, int State);
int GoatGUIEngine_GetPartsFlashCurrentFrameNumber(int PartsNumber, int State);
bool GoatGUIEngine_BackPartsFlashBeginFrame(int PartsNumber, int State);
bool GoatGUIEngine_StepPartsFlashFinalFrame(int PartsNumber, int State);

static void GoatGUIEngine_ReleaseParts(int parts_no)
{
	parts_release(parts_no);
}

void GoatGUIEngine_ReleaseAllParts(void);
void GoatGUIEngine_ReleaseAllPartsWithoutSystem(void);

static void GoatGUIEngine_SetPos(int parts_no, int x, int y)
{
	struct parts *parts = parts_get(parts_no);
	sprite_set_pos(&parts->sp, x, y);
}

/*
 * NOTE: If a ChipmunkSpriteEngine sprite is at Z=0, it's drawn behind;
 *       otherwise it's drawn in front, regardless of the parts Z-value.
 *       This is implemented by giving GoatGUIEngine sprites a Z-value of
 *       0 and a secondary Z-value of 1 + the value given here.
 */
static void GoatGUIEngine_SetZ(int parts_no, int z)
{
	sprite_set_z2(&parts_get(parts_no)->sp, 0, z+1);
}

static void GoatGUIEngine_SetShow(int parts_no, bool show)
{
	struct parts *parts = parts_get(parts_no);
	sprite_set_show(&parts->sp, show);
}

static void GoatGUIEngine_SetAlpha(int parts_no, int alpha)
{
	struct parts *parts = parts_get(parts_no);
	sprite_set_blend_rate(&parts->sp, alpha);
}

void GoatGUIEngine_SetPartsDrawFilter(int PartsNumber, int DrawFilter);

static void GoatGUIEngine_SetClickable(int parts_no, bool clickable)
{
	struct parts *parts = parts_get(parts_no);
	parts->clickable = !!clickable;
}

int GoatGUIEngine_GetPartsX(int PartsNumber);
int GoatGUIEngine_GetPartsY(int PartsNumber);

static int GoatGUIEngine_GetPartsZ(int parts_no)
{
	return sprite_get_z2(&parts_get(parts_no)->sp) - 1;
}

bool GoatGUIEngine_GetPartsShow(int PartsNumber);
int GoatGUIEngine_GetPartsAlpha(int PartsNumber);

static bool GoatGUIEngine_GetPartsClickable(int parts_no)
{
	return parts_get(parts_no)->clickable;
}

static void GoatGUIEngine_SetPartsOriginPosMode(int parts_no, int origin_pos_mode)
{
	struct parts *parts = parts_get(parts_no);
	parts->origin_mode = origin_pos_mode;
	sprite_dirty(&parts->sp);
}

static int GoatGUIEngine_GetPartsOriginPosMode(int parts_no)
{
	return parts_get(parts_no)->origin_mode;
}

void GoatGUIEngine_SetParentPartsNumber(int PartsNumber, int ParentPartsNumber);
bool GoatGUIEngine_SetPartsGroupNumber(int PartsNumber, int GroupNumber);
void GoatGUIEngine_SetPartsGroupDecideOnCursor(int GroupNumber, bool DecideOnCursor);
void GoatGUIEngine_SetPartsGroupDecideClick(int GroupNumber, bool DecideClick);
void GoatGUIEngine_SetOnCursorShowLinkPartsNumber(int PartsNumber, int LinkPartsNumber);

static void GoatGUIEngine_SetPartsMessageWindowShowLink(int parts_no, bool message_window_show_link)
{
	// TODO
}

bool GoatGUIEngine_GetPartsMessageWindowShowLink(int PartsNumber);

static bool GoatGUIEngine_SetPartsOnCursorSoundNumber(int parts_no, int sound_no)
{
	struct parts *parts = parts_get(parts_no);
	parts->on_cursor_sound = sound_no;
	return true; // FIXME: verify sound number
}

static bool GoatGUIEngine_SetPartsClickSoundNumber(int parts_no, int sound_no)
{
	struct parts *parts = parts_get(parts_no);
	parts->on_click_sound = sound_no;
	return true; // FIXME: verify sound number
}

static bool GoatGUIEngine_SetClickMissSoundNumber(int ClickMissSoundNumber)
{
	// TODO
	return true;
}

static void GoatGUIEngine_BeginClick(void)
{
	// TODO
}

static void GoatGUIEngine_EndClick(void)
{
	// TODO
}

static int GoatGUIEngine_GetClickPartsNumber(void)
{
	return 0; // TODO
}

static void GoatGUIEngine_AddMotionPos(int parts_no, int begin_x, int begin_y, int end_x, int end_y, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_POS, begin_t, end_t);
	motion->begin.x = begin_x;
	motion->begin.y = begin_y;
	motion->end.x = end_x;
	motion->end.y = end_y;
	parts_add_motion(parts, motion);

}

static void GoatGUIEngine_AddMotionAlpha(int parts_no, int begin_a, int end_a, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_ALPHA, begin_t, end_t);
	motion->begin.i = begin_a;
	motion->end.i = end_a;
	parts_add_motion(parts, motion);
}

void GoatGUIEngine_AddMotionCG(int PartsNumber, int BeginCGNumber, int NumofCG, int BeginTime, int EndTime);
void GoatGUIEngine_AddMotionHGaugeRate(int PartsNumber, int BeginNumerator, int BeginDenominator, int EndNumerator, int EndDenominator, int BeginTime, int EndTime);
void GoatGUIEngine_AddMotionVGaugeRate(int PartsNumber, int BeginNumerator, int BeginDenominator, int EndNumerator, int EndDenominator, int BeginTime, int EndTime);
void GoatGUIEngine_AddMotionNumeralNumber(int PartsNumber, int BeginNumber, int EndNumber, int BeginTime, int EndTime);

static void GoatGUIEngine_AddMotionMagX(int parts_no, float begin, float end, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_MAG_X, begin_t, end_t);
	motion->begin.f = begin;
	motion->end.f = end;
	parts_add_motion(parts, motion);
}

static void GoatGUIEngine_AddMotionMagY(int parts_no, float begin, float end, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_MAG_Y, begin_t, end_t);
	motion->begin.f = begin;
	motion->end.f = end;
	parts_add_motion(parts, motion);
}

static void GoatGUIEngine_AddMotionRotateX(int parts_no, float begin, float end, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_ROTATE_X, begin_t, end_t);
	motion->begin.f = deg2rad(begin);
	motion->end.f = deg2rad(end);
	parts_add_motion(parts, motion);
}

static void GoatGUIEngine_AddMotionRotateY(int parts_no, float begin, float end, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_ROTATE_Y, begin_t, end_t);
	motion->begin.f = deg2rad(begin);
	motion->end.f = deg2rad(end);
	parts_add_motion(parts, motion);
}

static void GoatGUIEngine_AddMotionRotateZ(int parts_no, float begin, float end, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_ROTATE_Z, begin_t, end_t);
	motion->begin.f = deg2rad(begin);
	motion->end.f = deg2rad(end);
	parts_add_motion(parts, motion);
}

void GoatGUIEngine_AddMotionVibrationSize(int PartsNumber, int BeginWidth, int BeginHeight, int BeginTime, int EndTime);
void GoatGUIEngine_AddMotionSound(int SoundNumber, int BeginTime);

static void GoatGUIEngine_BeginMotion(void)
{
	motion_t = 0;
	is_motion = true;
	parts_init_all_motion();
}

static void GoatGUIEngine_EndMotion(void)
{
	motion_t = 0;
	motion_end_t = 0;
	is_motion = false;
	parts_fini_all_motion();
}

static void GoatGUIEngine_SetMotionTime(int t)
{
	if (!is_motion)
		return;
	if (motion_t == t)
		return;
	motion_t = t;
	parts_update_all_motion();
	if (t >= motion_end_t)
		GoatGUIEngine_EndMotion();
}

static bool GoatGUIEngine_IsMotion(void)
{
	return is_motion;
}

static int GoatGUIEngine_GetMotionEndTime(void)
{
	return motion_end_t;
}

void GoatGUIEngine_SetPartsMagX(int PartsNumber, float MagX);
void GoatGUIEngine_SetPartsMagY(int PartsNumber, float MagY);
void GoatGUIEngine_SetPartsRotateX(int PartsNumber, float RotateX);
void GoatGUIEngine_SetPartsRotateY(int PartsNumber, float RotateY);
void GoatGUIEngine_SetPartsRotateZ(int PartsNumber, float RotateZ);
void GoatGUIEngine_SetPartsAlphaClipperPartsNumber(int PartsNumber, int AlphaClipperPartsNumber);
void GoatGUIEngine_SetPartsPixelDecide(int PartsNumber, bool PixelDecide);
bool GoatGUIEngine_SetThumbnailReductionSize(int ReductionSize);
bool GoatGUIEngine_SetThumbnailMode(bool Mode);
bool GoatGUIEngine_Save(struct page **SaveDataBuffer);
bool GoatGUIEngine_Load(struct page **SaveDataBuffer);

HLL_LIBRARY(GoatGUIEngine,
	    HLL_EXPORT(Init, GoatGUIEngine_Init),
	    HLL_EXPORT(Update, GoatGUIEngine_Update),
	    HLL_EXPORT(SetPartsCG, GoatGUIEngine_SetPartsCG),
	    HLL_EXPORT(GetPartsCGNumber, GoatGUIEngine_GetPartsCGNumber),
	    HLL_TODO_EXPORT(SetLoopCG, GoatGUIEngine_SetLoopCG),
	    HLL_TODO_EXPORT(SetText, GoatGUIEngine_SetText),
	    HLL_TODO_EXPORT(AddPartsText, GoatGUIEngine_AddPartsText),
	    HLL_TODO_EXPORT(DeletePartsTopTextLine, GoatGUIEngine_DeletePartsTopTextLine),
	    HLL_EXPORT(SetFont, GoatGUIEngine_SetFont),
	    HLL_TODO_EXPORT(SetPartsFontType, GoatGUIEngine_SetPartsFontType),
	    HLL_TODO_EXPORT(SetPartsFontSize, GoatGUIEngine_SetPartsFontSize),
	    HLL_TODO_EXPORT(SetPartsFontColor, GoatGUIEngine_SetPartsFontColor),
	    HLL_TODO_EXPORT(SetPartsFontBoldWeight, GoatGUIEngine_SetPartsFontBoldWeight),
	    HLL_TODO_EXPORT(SetPartsFontEdgeColor, GoatGUIEngine_SetPartsFontEdgeColor),
	    HLL_TODO_EXPORT(SetPartsFontEdgeWeight, GoatGUIEngine_SetPartsFontEdgeWeight),
	    HLL_EXPORT(SetTextCharSpace, GoatGUIEngine_SetTextCharSpace),
	    HLL_TODO_EXPORT(SetTextLineSpace, GoatGUIEngine_SetTextLineSpace),
	    HLL_TODO_EXPORT(SetHGaugeCG, GoatGUIEngine_SetHGaugeCG),
	    HLL_TODO_EXPORT(SetHGaugeRate, GoatGUIEngine_SetHGaugeRate),
	    HLL_TODO_EXPORT(SetVGaugeCG, GoatGUIEngine_SetVGaugeCG),
	    HLL_TODO_EXPORT(SetVGaugeRate, GoatGUIEngine_SetVGaugeRate),
	    HLL_TODO_EXPORT(SetNumeralCG, GoatGUIEngine_SetNumeralCG),
	    HLL_TODO_EXPORT(SetNumeralLinkedCGNumberWidthWidthList, GoatGUIEngine_SetNumeralLinkedCGNumberWidthWidthList),
	    HLL_TODO_EXPORT(SetNumeralNumber, GoatGUIEngine_SetNumeralNumber),
	    HLL_TODO_EXPORT(SetNumeralShowComma, GoatGUIEngine_SetNumeralShowComma),
	    HLL_TODO_EXPORT(SetNumeralSpace, GoatGUIEngine_SetNumeralSpace),
	    HLL_TODO_EXPORT(SetPartsRectangleDetectionSize, GoatGUIEngine_SetPartsRectangleDetectionSize),
	    HLL_TODO_EXPORT(SetPartsFlash, GoatGUIEngine_SetPartsFlash),
	    HLL_TODO_EXPORT(IsPartsFlashEnd, GoatGUIEngine_IsPartsFlashEnd),
	    HLL_TODO_EXPORT(GetPartsFlashCurrentFrameNumber, GoatGUIEngine_GetPartsFlashCurrentFrameNumber),
	    HLL_TODO_EXPORT(BackPartsFlashBeginFrame, GoatGUIEngine_BackPartsFlashBeginFrame),
	    HLL_TODO_EXPORT(StepPartsFlashFinalFrame, GoatGUIEngine_StepPartsFlashFinalFrame),
	    HLL_EXPORT(ReleaseParts, GoatGUIEngine_ReleaseParts),
	    HLL_TODO_EXPORT(ReleaseAllParts, GoatGUIEngine_ReleaseAllParts),
	    HLL_TODO_EXPORT(ReleaseAllPartsWithoutSystem, GoatGUIEngine_ReleaseAllPartsWithoutSystem),
	    HLL_EXPORT(SetPos, GoatGUIEngine_SetPos),
	    HLL_EXPORT(SetZ, GoatGUIEngine_SetZ),
	    HLL_EXPORT(SetShow, GoatGUIEngine_SetShow),
	    HLL_EXPORT(SetAlpha, GoatGUIEngine_SetAlpha),
	    HLL_TODO_EXPORT(SetPartsDrawFilter, GoatGUIEngine_SetPartsDrawFilter),
	    HLL_EXPORT(SetClickable, GoatGUIEngine_SetClickable),
	    HLL_TODO_EXPORT(GetPartsX, GoatGUIEngine_GetPartsX),
	    HLL_TODO_EXPORT(GetPartsY, GoatGUIEngine_GetPartsY),
	    HLL_EXPORT(GetPartsZ, GoatGUIEngine_GetPartsZ),
	    HLL_TODO_EXPORT(GetPartsShow, GoatGUIEngine_GetPartsShow),
	    HLL_TODO_EXPORT(GetPartsAlpha, GoatGUIEngine_GetPartsAlpha),
	    HLL_EXPORT(GetPartsClickable, GoatGUIEngine_GetPartsClickable),
	    HLL_EXPORT(SetPartsOriginPosMode, GoatGUIEngine_SetPartsOriginPosMode),
	    HLL_EXPORT(GetPartsOriginPosMode, GoatGUIEngine_GetPartsOriginPosMode),
	    HLL_TODO_EXPORT(SetParentPartsNumber, GoatGUIEngine_SetParentPartsNumber),
	    HLL_TODO_EXPORT(SetPartsGroupNumber, GoatGUIEngine_SetPartsGroupNumber),
	    HLL_TODO_EXPORT(SetPartsGroupDecideOnCursor, GoatGUIEngine_SetPartsGroupDecideOnCursor),
	    HLL_TODO_EXPORT(SetPartsGroupDecideClick, GoatGUIEngine_SetPartsGroupDecideClick),
	    HLL_TODO_EXPORT(SetOnCursorShowLinkPartsNumber, GoatGUIEngine_SetOnCursorShowLinkPartsNumber),
	    HLL_EXPORT(SetPartsMessageWindowShowLink, GoatGUIEngine_SetPartsMessageWindowShowLink),
	    HLL_TODO_EXPORT(GetPartsMessageWindowShowLink, GoatGUIEngine_GetPartsMessageWindowShowLink),
	    HLL_EXPORT(SetPartsOnCursorSoundNumber, GoatGUIEngine_SetPartsOnCursorSoundNumber),
	    HLL_EXPORT(SetPartsClickSoundNumber, GoatGUIEngine_SetPartsClickSoundNumber),
	    HLL_EXPORT(SetClickMissSoundNumber, GoatGUIEngine_SetClickMissSoundNumber),
	    HLL_EXPORT(BeginClick, GoatGUIEngine_BeginClick),
	    HLL_EXPORT(EndClick, GoatGUIEngine_EndClick),
	    HLL_EXPORT(GetClickPartsNumber, GoatGUIEngine_GetClickPartsNumber),
	    HLL_EXPORT(AddMotionPos, GoatGUIEngine_AddMotionPos),
	    HLL_EXPORT(AddMotionAlpha, GoatGUIEngine_AddMotionAlpha),
	    HLL_TODO_EXPORT(AddMotionCG, GoatGUIEngine_AddMotionCG),
	    HLL_TODO_EXPORT(AddMotionHGaugeRate, GoatGUIEngine_AddMotionHGaugeRate),
	    HLL_TODO_EXPORT(AddMotionVGaugeRate, GoatGUIEngine_AddMotionVGaugeRate),
	    HLL_TODO_EXPORT(AddMotionNumeralNumber, GoatGUIEngine_AddMotionNumeralNumber),
	    HLL_EXPORT(AddMotionMagX, GoatGUIEngine_AddMotionMagX),
	    HLL_EXPORT(AddMotionMagY, GoatGUIEngine_AddMotionMagY),
	    HLL_EXPORT(AddMotionRotateX, GoatGUIEngine_AddMotionRotateX),
	    HLL_EXPORT(AddMotionRotateY, GoatGUIEngine_AddMotionRotateY),
	    HLL_EXPORT(AddMotionRotateZ, GoatGUIEngine_AddMotionRotateZ),
	    HLL_TODO_EXPORT(AddMotionVibrationSize, GoatGUIEngine_AddMotionVibrationSize),
	    HLL_TODO_EXPORT(AddMotionSound, GoatGUIEngine_AddMotionSound),
	    HLL_EXPORT(BeginMotion, GoatGUIEngine_BeginMotion),
	    HLL_EXPORT(EndMotion, GoatGUIEngine_EndMotion),
	    HLL_EXPORT(SetMotionTime, GoatGUIEngine_SetMotionTime),
	    HLL_EXPORT(IsMotion, GoatGUIEngine_IsMotion),
	    HLL_EXPORT(GetMotionEndTime, GoatGUIEngine_GetMotionEndTime),
	    HLL_TODO_EXPORT(SetPartsMagX, GoatGUIEngine_SetPartsMagX),
	    HLL_TODO_EXPORT(SetPartsMagY, GoatGUIEngine_SetPartsMagY),
	    HLL_TODO_EXPORT(SetPartsRotateX, GoatGUIEngine_SetPartsRotateX),
	    HLL_TODO_EXPORT(SetPartsRotateY, GoatGUIEngine_SetPartsRotateY),
	    HLL_TODO_EXPORT(SetPartsRotateZ, GoatGUIEngine_SetPartsRotateZ),
	    HLL_TODO_EXPORT(SetPartsAlphaClipperPartsNumber, GoatGUIEngine_SetPartsAlphaClipperPartsNumber),
	    HLL_TODO_EXPORT(SetPartsPixelDecide, GoatGUIEngine_SetPartsPixelDecide),
	    HLL_TODO_EXPORT(SetThumbnailReductionSize, GoatGUIEngine_SetThumbnailReductionSize),
	    HLL_TODO_EXPORT(SetThumbnailMode, GoatGUIEngine_SetThumbnailMode),
	    HLL_TODO_EXPORT(Save, GoatGUIEngine_Save),
	    HLL_TODO_EXPORT(Load, GoatGUIEngine_Load));

