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
#include <cglm/cglm.h>

#include "system4/cg.h"
#include "system4/hashtable.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "asset_manager.h"
#include "audio.h"
#include "gfx/gfx.h"
#include "input.h"
#include "queue.h"
#include "scene.h"
#include "vm/page.h"
#include "xsystem4.h"
#include "hll.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

static inline float deg2rad(float deg)
{
	return deg * (M_PI / 180.0);
}

// Goat sprites are integrated into the scene via a single (virtual) sprite
static struct sprite goat_sprite;

// NOTE: actual value is +1
enum parts_state_type {
	PARTS_STATE_DEFAULT = 0,
	PARTS_STATE_HOVERED = 1,
	PARTS_STATE_CLICKED = 2,
#define PARTS_NR_STATES 3
};

enum parts_motion_type {
	PARTS_MOTION_POS,
	PARTS_MOTION_ALPHA,
	PARTS_MOTION_CG,
	PARTS_MOTION_HGUAGE_RATE,
	PARTS_MOTION_VGUAGE_RATE,
	PARTS_MOTION_NUMERAL_NUMBER,
	PARTS_MOTION_MAG_X,
	PARTS_MOTION_MAG_Y,
	PARTS_MOTION_ROTATE_X,
	PARTS_MOTION_ROTATE_Y,
	PARTS_MOTION_ROTATE_Z,
	PARTS_MOTION_VIBRATION_SIZE,
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

struct sound_motion {
	TAILQ_ENTRY(sound_motion) entry;
	int begin_time;
	int sound_no;
	bool played;
};

static TAILQ_HEAD(sound_motion_list, sound_motion) sound_motion_list =
	TAILQ_HEAD_INITIALIZER(sound_motion_list);

struct parts_text_line {
	unsigned height;
};

enum parts_type {
	PARTS_CG,
	PARTS_TEXT,
	PARTS_ANIMATION,
	PARTS_NUMERAL,
	PARTS_HGAUGE,
	PARTS_VGAUGE,
};

struct parts_cg {
	Texture texture;
	int no;
};

struct parts_text {
	Texture texture;
	unsigned nr_lines;
	struct parts_text_line *lines;
	unsigned char_space;
	unsigned line_space;
	Point cursor;
	struct text_metrics tm;
};

struct parts_animation {
	Texture texture;
	unsigned cg_no;
	unsigned nr_frames;
	Texture *frames;
	unsigned frame_time;
	unsigned elapsed;
	unsigned current_frame;
};

struct parts_numeral {
	Texture texture;
	Texture cg[12];
	int space;
	int show_comma;
	int cg_no;
};

struct parts_gauge {
	Texture texture;
	Texture cg;
};

struct parts_state {
	enum parts_type type;
	union {
		struct parts_cg cg;
		struct parts_text text;
		struct parts_animation anim;
		struct parts_numeral num;
		struct parts_gauge gauge;
	};
};

TAILQ_HEAD(parts_list, parts);
static struct parts_list parts_list = TAILQ_HEAD_INITIALIZER(parts_list);
static struct hash_table *parts_table = NULL;

struct parts {
	//struct sprite sp;
	enum parts_state_type state;
	struct parts_state states[PARTS_NR_STATES];
	TAILQ_ENTRY(parts) parts_list_entry;
	struct parts_list children;
	int no;
	int sprite_deform;
	bool clickable;
	int on_cursor_sound;
	int on_click_sound;
	int origin_mode;
	int parent;
	int linked_to;
	int linked_from;
	uint8_t alpha;
	Rectangle rect;
	int z;
	bool show;
	// The actual hitbox of the sprite (accounting for origin-mode, scale, etc)
	Rectangle pos;
	Point offset;
	struct { float x, y; } scale;
	struct { float x, y, z; } rotation;
	TAILQ_HEAD(, parts_motion) motion;
};

// true between calls to BeginClick and EndClick
static bool began_click = false;
// true when the left mouse button was down last update
static bool prev_clicking = false;
// the last (fully) clicked parts number
static int clicked_parts = 0;
// the current (partially) clicked parts number
static int click_down_parts = 0;
// the mouse position at last update
static Point prev_pos = {0};

// true after call to BeginMotion until the motion ends or EndMotion is called
static bool is_motion = false;
// the elapsed time of the current motion
static int motion_t = 0;
// the end time of the current motion
static int motion_end_t = 0;

struct parts_render_params {
	Point offset;
	uint8_t alpha;
};

static struct parts *parts_get(int parts_no);
static void parts_render_text(struct parts *parts, struct parts_render_params *params);
static void parts_render_cg(struct parts *parts, Texture *t, struct parts_render_params *params);

static void parts_render(struct parts *parts, struct parts_render_params *parent_params)
{
	if (!parts->show)
		return;

	if (parts->linked_to >= 0) {
		struct parts *link_parts = parts_get(parts->linked_to);
		if (!SDL_PointInRect(&prev_pos, &link_parts->pos))
			return;
	}

	struct parts_render_params params = *parent_params;
	// modify params per parts values
	params.alpha *= parts->alpha / 255.0;
	params.offset.x += parts->rect.x;
	params.offset.y += parts->rect.y;

	// render
	struct parts_state *state = &parts->states[parts->state];
	switch (state->type) {
	case PARTS_CG:
		if (state->cg.texture.handle)
			parts_render_cg(parts, &state->cg.texture, &params);
		break;
	case PARTS_TEXT:
		if (state->text.texture.handle)
			parts_render_text(parts, &params);
		break;
	case PARTS_ANIMATION:
		if (state->anim.texture.handle)
			parts_render_cg(parts, &state->anim.texture, &params);
		break;
	case PARTS_NUMERAL:
		if (state->num.texture.handle) {
			parts_render_cg(parts, &state->num.texture, &params);
		}
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		if (state->gauge.texture.handle)
			parts_render_cg(parts, &state->gauge.texture, &params);
		break;
	}

	// render children
	struct parts *child;
	TAILQ_FOREACH(child, &parts->children, parts_list_entry) {
		parts_render(child, &params);
	}
}

static void goat_render(possibly_unused struct sprite *_)
{
	struct parts_render_params params = {
		.offset = { 0, 0 },
		.alpha = 255,
	};
	struct parts *parts;
	TAILQ_FOREACH(parts, &parts_list, parts_list_entry) {
		parts_render(parts, &params);
	}
}

static inline void goat_dirty(void)
{
	scene_sprite_dirty(&goat_sprite);
}

static inline void parts_dirty(possibly_unused struct parts *parts)
{
	goat_dirty();
}

static bool GoatGUIEngine_Init(void)
{
	parts_table = ht_create(1024);
	goat_sprite.z = 0;
	goat_sprite.z2 = 2;
	goat_sprite.has_pixel = true;
	goat_sprite.has_alpha = true;
	goat_sprite.render = goat_render;
	scene_register_sprite(&goat_sprite);
	return 1;
}

static struct text_metrics default_text_metrics = {
	.color = { .r = 255, .g = 255, .b = 255, .a = 255 },
	.outline_color = { .r = 0, .g = 0, .b = 0, .a = 255 },
	.size = 16,
	.weight = FW_NORMAL,
	.face = FONT_GOTHIC,
	.outline_left = 0,
	.outline_up = 0,
	.outline_right = 0,
	.outline_down = 0,
};

static void parts_init(struct parts *parts)
{
	memset(parts, 0, sizeof(struct parts));
	parts->on_cursor_sound = -1;
	parts->on_click_sound = -1;
	parts->origin_mode = 1;
	parts->z = 1;
	parts->show = true;
	parts->parent = -1;
	parts->linked_to = -1;
	parts->linked_from = -1;
	parts->scale.x = 1.0;
	parts->scale.y = 1.0;
	parts->rotation.x = 0.0;
	parts->rotation.y = 0.0;
	parts->rotation.z = 0.0;
	TAILQ_INIT(&parts->children);
	TAILQ_INIT(&parts->motion);
}

static struct parts *parts_alloc(void)
{
	struct parts *parts = xcalloc(1, sizeof(struct parts));
	parts_init(parts);
	return parts;
}

static void parts_list_insert(struct parts_list *list, struct parts *parts)
{
	struct parts *p;
	TAILQ_FOREACH(p, list, parts_list_entry) {
		if (p->z >= parts->z) {
			TAILQ_INSERT_BEFORE(p, parts, parts_list_entry);
			return;
		}
	}
	TAILQ_INSERT_TAIL(list, parts, parts_list_entry);
	goat_dirty();
}

static struct parts *parts_get(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (slot->value)
		return slot->value;

	struct parts *parts = parts_alloc();
	parts->no = parts_no;
	slot->value = parts;
	parts_list_insert(&parts_list, parts);
	return parts;
}

static struct parts_list *parts_get_list(struct parts *parts)
{
	if (parts->parent < 0)
		return &parts_list;
	return &parts_get(parts->parent)->children;
}

static void parts_list_remove(struct parts_list *list, struct parts *parts)
{
	TAILQ_REMOVE(list, parts, parts_list_entry);
}

static void parts_state_free(struct parts_state *state)
{
	switch (state->type) {
	case PARTS_CG:
		gfx_delete_texture(&state->cg.texture);
		break;
	case PARTS_TEXT:
		free(state->text.lines);
		gfx_delete_texture(&state->text.texture);
		break;
	case PARTS_ANIMATION:
		for (unsigned i = 0; i < state->anim.nr_frames; i++) {
			gfx_delete_texture(&state->anim.frames[i]);
		}
		break;
	case PARTS_NUMERAL:
		gfx_delete_texture(&state->num.texture);
		for (int i = 0; i < 12; i++) {
			gfx_delete_texture(&state->num.cg[i]);
		}
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		gfx_delete_texture(&state->gauge.texture);
		gfx_delete_texture(&state->gauge.cg);
		break;
	}
	memset(state, 0, sizeof(struct parts_state));
}

static void parts_state_reset(struct parts_state *state, enum parts_type type)
{
	parts_state_free(state);
	state->type = type;
	if (type == PARTS_TEXT) {
		state->text.tm = default_text_metrics;
	}
}

static struct parts_cg *parts_get_cg(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_CG) {
		parts_state_reset(&parts->states[state], PARTS_CG);
	}
	return &parts->states[state].cg;
}

static struct parts_text *parts_get_text(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_TEXT) {
		parts_state_reset(&parts->states[state], PARTS_TEXT);
	}
	return &parts->states[state].text;
}

static struct parts_animation *parts_get_animation(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_ANIMATION) {
		parts_state_reset(&parts->states[state], PARTS_ANIMATION);
	}
	return &parts->states[state].anim;
}

static struct parts_numeral *parts_get_numeral(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_NUMERAL) {
		parts_state_reset(&parts->states[state], PARTS_NUMERAL);
	}
	return &parts->states[state].num;
}

static struct parts_gauge *parts_get_hgauge(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_HGAUGE) {
		parts_state_reset(&parts->states[state], PARTS_HGAUGE);
	}
	return &parts->states[state].gauge;
}

static struct parts_gauge *parts_get_vgauge(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_VGAUGE) {
		parts_state_reset(&parts->states[state], PARTS_VGAUGE);
	}
	return &parts->states[state].gauge;
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

static void parts_release(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (!slot->value)
		return;

	struct parts *parts = slot->value;
	parts_clear_motion(parts);
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		parts_state_free(&parts->states[i]);
	}

	while (!TAILQ_EMPTY(&parts->children)) {
		struct parts *child = TAILQ_FIRST(&parts->children);
		child->parent = -1;
		parts_list_remove(&parts->children, child);
		parts_list_insert(&parts_list, child);
	}

	parts_list_remove(parts_get_list(parts), parts);
	free(parts);
	slot->value = NULL;
	goat_dirty();
}

static void parts_release_all(void)
{
	while (!TAILQ_EMPTY(&parts_list)) {
		struct parts *parts = TAILQ_FIRST(&parts_list);
		parts_release(parts->no);
	}
}

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

static void parts_recalculate_offset(struct parts *parts)
{
	const int mode = parts->origin_mode;
	const int w = parts->rect.w;
	const int h = parts->rect.h;

	int x, y;
	switch (mode) {
	case 1: x = 0;    y = 0;    break;
	case 2: x = -w/2; y = 0;    break;
	case 3: x = -w;   y = -h/2; break;
	case 4: x = 0;    y = -h/2; break;
	case 5: x = -w/2; y = -h/2; break;
	case 6: x = -w;   y = -h/2; break;
	case 7: x = 0;    y = -h;   break;
	case 8: x = -w/2; y = -h;   break;
	case 9: x = -w;   y = -h;   break;
	default:
		// why...
		x = mode;
		y = (3*h)/4;
		break;
	}

	parts->offset.x = x;
	parts->offset.y = y;
}

static void parts_recalculate_pos(struct parts *parts)
{
	parts_recalculate_offset(parts);
	parts->pos = (Rectangle) {
		.x = parts->rect.x + parts->offset.x,
		.y = parts->rect.y + parts->offset.y,
		.w = parts->rect.w,
		.h = parts->rect.h,
	};
}

static void parts_set_pos(struct parts *parts, Point pos)
{
	parts->rect.x = pos.x;
	parts->rect.y = pos.y;
	parts_recalculate_pos(parts);
	parts_dirty(parts);
}

static void parts_set_scale_x(struct parts *parts, float mag)
{
	parts->scale.x = mag;
	parts_recalculate_pos(parts);
	parts_dirty(parts);
}

static void parts_set_scale_y(struct parts *parts, float mag)
{
	parts->scale.y = mag;
	parts_recalculate_pos(parts);
	parts_dirty(parts);
}

static void parts_set_rotation_z(struct parts *parts, float rot)
{
	parts->rotation.z = rot;
	parts_dirty(parts);
}

static void parts_set_alpha(struct parts *parts, int alpha)
{
	parts->alpha = max(0, min(255, alpha));
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		struct parts_state *s = &parts->states[i];
		switch (s->type) {
		case PARTS_CG:
			s->cg.texture.alpha_mod = parts->alpha;
			break;
		case PARTS_TEXT:
			s->text.texture.alpha_mod = parts->alpha;
			break;
		case PARTS_ANIMATION:
			for (unsigned i = 0; i < s->anim.nr_frames; i++) {
				s->anim.frames[i].alpha_mod = parts->alpha;
			}
			break;
		case PARTS_NUMERAL:
			s->num.texture.alpha_mod = parts->alpha;
			break;
		case PARTS_HGAUGE:
		case PARTS_VGAUGE:
			s->gauge.texture.alpha_mod = parts->alpha;
			break;
		}
	}
	parts_dirty(parts);
}

static void parts_add_cg(struct parts *parts, struct cg *cg)
{
	if (parts->rect.w && parts->rect.w != cg->metrics.w)
		WARNING("Width of parts CGs differ: %d / %d", parts->rect.w, cg->metrics.w);
	if (parts->rect.h && parts->rect.h != cg->metrics.h)
		WARNING("Heights of parts CGs differ: %d / %d", parts->rect.h, cg->metrics.h);
	parts->rect.w = cg->metrics.w;
	parts->rect.h = cg->metrics.h;
}

static bool parts_set_cg(struct parts *parts, int cg_no, int state)
{
	if (!cg_no) {
		parts_state_reset(&parts->states[state], PARTS_CG);
		parts_dirty(parts);
		return true;
	}

	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return false;
	struct parts_cg *parts_cg = parts_get_cg(parts, state);
	gfx_delete_texture(&parts_cg->texture);
	gfx_init_texture_with_cg(&parts_cg->texture, cg);
	parts_cg->no = cg_no;
	parts_add_cg(parts, cg);
	parts_recalculate_pos(parts);
	parts_dirty(parts);
	cg_free(cg);
	return true;
}

static void parts_set_hgauge_rate(struct parts *parts, float rate, int state);
static void parts_set_vgauge_rate(struct parts *parts, float rate, int state);
static bool parts_set_number(struct parts *parts, int n, int state);

static void parts_set_state(struct parts *parts, enum parts_state_type state)
{
	if (parts->state != state) {
		parts->state = state;
		parts_dirty(parts);
	}
}

static void parts_render_text(struct parts *parts, struct parts_render_params *params)
{
	parts_recalculate_offset(parts);
	Rectangle rect = {
		.x = params->offset.x + parts->offset.x,
		.y = params->offset.y + parts->offset.y,
		.w = parts->rect.w,
		.h = parts->rect.h
	};
	gfx_render_texture(&parts->states[parts->state].text.texture, &rect);
}

static void parts_render_cg(struct parts *parts, Texture *t, struct parts_render_params *params)
{
	parts_recalculate_offset(parts);

	mat4 mw_transform = GLM_MAT4_IDENTITY_INIT;
	glm_translate(mw_transform, (vec3) { params->offset.x, params->offset.y, 0 });
	// FIXME: need perspective for 3D rotate
	//glm_rotate_x(mw_transform, parts->rotation.x, mw_transform);
	//glm_rotate_y(mw_transform, parts->rotation.y, mw_transform);
	glm_rotate_z(mw_transform, parts->rotation.z, mw_transform);
	glm_scale(mw_transform, (vec3){ parts->scale.x, parts->scale.y, 1.0 });
	glm_translate(mw_transform, (vec3){ parts->offset.x, parts->offset.y, 0 });
	glm_scale(mw_transform, (vec3){ parts->rect.w, parts->rect.h, 1.0 });
	mat4 wv_transform = WV_TRANSFORM(config.view_width, config.view_height);

	struct gfx_render_job job = {
		.shader = NULL, // default shader
		.texture = t->handle,
		.world_transform = mw_transform[0],
		.view_transform = wv_transform[0],
		.data = t,
	};

	gfx_render(&job);
}

static void parts_update_with_motion(struct parts *parts, struct parts_motion *motion)
{
	switch (motion->type) {
	case PARTS_MOTION_POS:
		parts_set_pos(parts, motion_calculate_point(motion, motion_t));
		break;
	case PARTS_MOTION_ALPHA:
		parts_set_alpha(parts, motion_calculate_i(motion, motion_t));
		break;
	case PARTS_MOTION_CG:
		parts_set_cg(parts, motion_calculate_i(motion, motion_t), parts->state);
		break;
	case PARTS_MOTION_HGUAGE_RATE:
		parts_set_hgauge_rate(parts, motion_calculate_f(motion, motion_t), parts->state);
		break;
	case PARTS_MOTION_VGUAGE_RATE:
		parts_set_vgauge_rate(parts, motion_calculate_f(motion, motion_t), parts->state);
		break;
	case PARTS_MOTION_NUMERAL_NUMBER:
		parts_set_number(parts, motion_calculate_i(motion, motion_t), parts->state);
		break;
	case PARTS_MOTION_MAG_X:
		parts_set_scale_x(parts, motion_calculate_f(motion, motion_t));
		break;
	case PARTS_MOTION_MAG_Y:
		parts_set_scale_y(parts, motion_calculate_f(motion, motion_t));
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
		parts_set_rotation_z(parts, motion_calculate_f(motion, motion_t));
		break;
	default:
		WARNING("Invalid motion type: %d", motion->type);
	}
}

static void parts_list_update_motion(struct parts_list *list)
{
	struct parts *parts;
	TAILQ_FOREACH(parts, list, parts_list_entry) {
		struct parts_motion *motion;
		TAILQ_FOREACH(motion, &parts->motion, entry) {
			if (motion->begin_time > motion_t)
				break;
			// FIXME? What if a motion begins and ends within the span of another?
			//        This implementation will cancel the earlier motion and remain
			//        at the end-state of the second motion.
			parts_update_with_motion(parts, motion);
		}
		parts_list_update_motion(&parts->children);
	}
}

static void parts_update_all_motion(void)
{
	parts_list_update_motion(&parts_list);

	struct sound_motion *sound;
	TAILQ_FOREACH(sound, &sound_motion_list, entry) {
		if (sound->begin_time > motion_t)
			break;
		if (sound->played)
			continue;
		audio_play_sound(sound->sound_no);
		sound->played = true;
	}
}

/*
 * NOTE: If a motion begins at e.g. t=100 with a value of v=0, then that value
 *       becomes the initial value of v at t=0.
 */
static void parts_list_init_all_motion(struct parts_list *list)
{
	struct parts *parts;
	TAILQ_FOREACH(parts, list, parts_list_entry) {
		struct parts_motion *motion;
		bool initialized[PARTS_NR_MOTION_TYPES] = {0};
		TAILQ_FOREACH(motion, &parts->motion, entry) {
			if (!initialized[motion->type]) {
				parts_update_with_motion(parts, motion);
				initialized[motion->type] = true;
			}
		}
	}
}

static void parts_list_fini_all_motion(struct parts_list *list)
{
	struct parts *parts;
	TAILQ_FOREACH(parts, list, parts_list_entry) {
		parts_clear_motion(parts);
		parts_list_fini_all_motion(&parts->children);
	}
}

static void parts_fini_all_motion(void)
{
	parts_list_fini_all_motion(&parts_list);

	while (!TAILQ_EMPTY(&sound_motion_list)) {
		struct sound_motion *motion = TAILQ_FIRST(&sound_motion_list);
		TAILQ_REMOVE(&sound_motion_list, motion, entry);
		free(motion);
	}
}

static void parts_update_loop(struct parts *parts, int passed_time)
{
	if (parts->states[parts->state].type != PARTS_ANIMATION)
		return;

	struct parts_animation *anim = &parts->states[parts->state].anim;
	if (passed_time <= 0 || !anim->nr_frames)
		return;

	const unsigned elapsed = anim->elapsed + passed_time;
	const unsigned frame_diff = elapsed / anim->frame_time;
	const unsigned remainder = elapsed % anim->frame_time;

	if (frame_diff > 0) {
		anim->elapsed = remainder;
		anim->current_frame = (anim->current_frame + frame_diff) % anim->nr_frames;
		anim->texture = anim->frames[anim->current_frame];
		parts_dirty(parts);
	} else {
		anim->elapsed = elapsed;
	}
}

static void parts_update_mouse(struct parts *parts, Point cur_pos, bool cur_clicking)
{
	bool prev_in = SDL_PointInRect(&prev_pos, &parts->pos);
	bool cur_in = SDL_PointInRect(&cur_pos, &parts->pos);

	if (parts->linked_from >= 0 && cur_in != prev_in) {
		parts_dirty(parts_get(parts->linked_from));
	}

	if (!began_click || !parts->clickable)
		return;

	if (!cur_in) {
		parts_set_state(parts, PARTS_STATE_DEFAULT);
		return;
	}

	// click down: just remember the parts number for later
	if (cur_clicking && !prev_clicking) {
		click_down_parts = parts->no;
	}

	if (cur_clicking && click_down_parts == parts->no) {
		parts_set_state(parts, PARTS_STATE_CLICKED);
	} else {
		if (!prev_in) {
			audio_play_sound(parts->on_cursor_sound);
		}
		parts_set_state(parts, PARTS_STATE_HOVERED);
	}

	// click event: only if the click down event had same parts number
	if (prev_clicking && !cur_clicking && click_down_parts == parts->no) {
		audio_play_sound(parts->on_click_sound);
		clicked_parts = parts->no;
	}
}

static void GoatGUIEngine_Update(int passed_time, possibly_unused bool message_window_show)
{
	audio_update();

	Point cur_pos;
	bool cur_clicking = key_is_down(VK_LBUTTON);
	mouse_get_pos(&cur_pos.x, &cur_pos.y);

	struct parts *parts;
	TAILQ_FOREACH(parts, &parts_list, parts_list_entry) {
		parts_update_loop(parts, passed_time);
		parts_update_mouse(parts, cur_pos, cur_clicking);
	}

	if (prev_clicking && !cur_clicking) {
		if (!click_down_parts) {
			// TODO: play misclick sound
		}
		click_down_parts = 0;
	}

	prev_clicking = cur_clicking;
	prev_pos = cur_pos;
	goat_dirty();
}

static bool GoatGUIEngine_SetPartsCG(int parts_no, int cg_no, possibly_unused int sprite_deform, int state)
{
	state--;
	if (state < 0 || state > 2) {
		WARNING("Invalid parts state: %d", state);
		return false;
	}
	return parts_set_cg(parts_get(parts_no), cg_no, state);
}

static int GoatGUIEngine_GetPartsCGNumber(int parts_no, int state)
{
	state--;
	if (state < 0 || state > 2) {
		WARNING("Invalid parts state: %d", state);
		return false;
	}

	return parts_get_cg(parts_get(parts_no), state)->no;
}

static bool GoatGUIEngine_SetLoopCG(int parts_no, int cg_no, int nr_frames, int frame_time, int state)
{
	state--;
	if (state < 0 || state > 2) {
		WARNING("Invalid parts state: %d", state);
		return false;
	}
	if (nr_frames <= 0 || nr_frames > 10000) {
		WARNING("Invalid frame count: %d", nr_frames);
	}

	struct parts *parts = parts_get(parts_no);
	Texture *frames = xcalloc(nr_frames, sizeof(Texture));
	for (int i = 0; i < nr_frames; i++) {
		struct cg *cg = asset_cg_load(cg_no + i);
		if (!cg) {
			for (int j = 0; j < i; j++) {
				gfx_delete_texture(&frames[j]);
			}
			free(frames);
			return false;
		}
		gfx_init_texture_with_cg(&frames[i], cg);
		parts_add_cg(parts, cg);
		cg_free(cg);
	}

	struct parts_animation *anim = parts_get_animation(parts, state);
	anim->frames = frames; // free previous value?
	anim->nr_frames = nr_frames;
	anim->frame_time = frame_time;
	anim->elapsed = 0;
	anim->current_frame = 0;
	anim->texture = frames[0];
	return true;
}

static int extract_sjis_char(const char *src, char *dst)
{
	if (SJIS_2BYTE(*src)) {
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = '\0';
		return 2;
	}
	dst[0] = src[0];
	dst[1] = '\0';
	return 1;
}

static void parts_text_newline(struct parts_text *text)
{
	const unsigned height = text->lines[text->nr_lines-1].height;
	text->cursor = POINT(0, text->cursor.y + height + text->line_space);
	text->lines = xrealloc_array(text->lines, text->nr_lines, text->nr_lines+1, sizeof(struct parts_text_line));
	text->nr_lines++;
}

static void parts_text_append(struct parts *parts, struct string *text, int state)
{
	struct parts_text *t = parts_get_text(parts, state);

	if (!t->texture.handle) {
		gfx_init_texture_rgba(&t->texture, config.view_width, config.view_height,
				      (SDL_Color){ 0, 0, 0, 0 });
	}

	if (!t->nr_lines) {
		t->lines = xcalloc(1, sizeof(struct parts_text_line));
		t->lines[0].height = 0;
		t->nr_lines = 1;
	}

	const char *msgp = text->text;
	while (*msgp) {
		char c[4];
		int len = extract_sjis_char(msgp, c);
		msgp += len;

		if (c[0] == '\n') {
			parts_text_newline(t);
			continue;
		}

		t->cursor.x += gfx_render_text(&t->texture, t->cursor, c, &t->tm, t->char_space);

		const unsigned old_height = t->lines[t->nr_lines-1].height;
		const unsigned new_height = t->tm.size;
		t->lines[t->nr_lines-1].height = max(old_height, new_height);
	}
	parts->rect.w = t->cursor.x;
	parts->rect.h = t->cursor.y + t->lines[t->nr_lines-1].height;
}

static void parts_text_clear(struct parts *parts, int state)
{
	struct parts_text *text = parts_get_text(parts, state);
	free(text->lines);
	text->lines = NULL;
	text->nr_lines = 0;
	text->cursor.x = 0;
	text->cursor.y = 0;
	gfx_delete_texture(&text->texture);
}

static bool GoatGUIEngine_SetText(int parts_no, struct string *text, int state)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	struct parts *parts = parts_get(parts_no);
	parts_text_clear(parts, state);
	parts_text_append(parts, text, state);
	return true;
}

static bool GoatGUIEngine_AddPartsText(int parts_no, struct string *text, int state)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	struct parts *parts = parts_get(parts_no);
	parts_text_append(parts, text, state);
	return true;
}

bool GoatGUIEngine_DeletePartsTopTextLine(int PartsNumber, int State);

static bool GoatGUIEngine_SetFont(int parts_no, int type, int size, int r, int g, int b, float bold_weight, int edge_r, int edge_g, int edge_b, float edge_weight, int state)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	struct parts_text *text = parts_get_text(parts_get(parts_no), state);
	text->tm.face = type;
	text->tm.size = size;
	text->tm.color = (SDL_Color) { r, g, b, 255 };
	text->tm.weight = bold_weight * 1000;
	text->tm.outline_color = (SDL_Color) { edge_r, edge_g, edge_b, 255 };
	text->tm.outline_left = edge_weight;
	text->tm.outline_up = edge_weight;
	text->tm.outline_right = edge_weight;
	text->tm.outline_down = edge_weight;
	return true;
}

static bool GoatGUIEngine_SetPartsFontType(int parts_no, int type, int state)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	parts_get_text(parts_get(parts_no), state)->tm.face = type;
	return true;
}

static bool GoatGUIEngine_SetPartsFontSize(int parts_no, int size, int state)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	parts_get_text(parts_get(parts_no), state)->tm.size = size;
	return true;
}

static bool GoatGUIEngine_SetPartsFontColor(int parts_no, int r, int g, int b, int state)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	parts_get_text(parts_get(parts_no), state)->tm.color = (SDL_Color) { r, g, b, 255 };
	return true;
}

static bool GoatGUIEngine_SetPartsFontBoldWeight(int parts_no, float bold_weight, int state)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	parts_get_text(parts_get(parts_no), state)->tm.weight = bold_weight * 1000;
	return true;
}

static bool GoatGUIEngine_SetPartsFontEdgeColor(int parts_no, int r, int g, int b, int state)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	parts_get_text(parts_get(parts_no), state)->tm.outline_color = (SDL_Color) { r, g, b, 255 };
	return true;
}

static bool GoatGUIEngine_SetPartsFontEdgeWeight(int parts_no, float edge_weight, int state)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	struct parts_text *text = parts_get_text(parts_get(parts_no), state);
	text->tm.outline_left = edge_weight;
	text->tm.outline_up = edge_weight;
	text->tm.outline_right = edge_weight;
	text->tm.outline_down = edge_weight;
	return true;
}

static bool GoatGUIEngine_SetTextCharSpace(int parts_no, int char_space, int state)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	parts_get_text(parts_get(parts_no), state)->char_space = char_space;
	return true;
}

static bool GoatGUIEngine_SetTextLineSpace(int parts_no, int line_space, int state)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	parts_get_text(parts_get(parts_no), state)->line_space = line_space;
	return true;
}

static bool set_gauge_cg(int parts_no, int cg_no, int state, bool vert)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = vert ? parts_get_vgauge(parts, state) : parts_get_hgauge(parts, state);
	gfx_init_texture_with_cg(&g->cg, cg);
	gfx_init_texture_with_cg(&g->texture, cg);
	cg_free(cg);

	gfx_init_texture_rgba(&g->texture, g->cg.w, g->cg.h, (SDL_Color){0,0,0,255});
	gfx_copy_with_alpha_map(&g->texture, 0, 0, &g->cg, 0, 0, g->cg.w, g->cg.h);

	parts->rect.w = g->cg.w;
	parts->rect.h = g->cg.h;

	parts_dirty(parts);
	return true;
}

static bool GoatGUIEngine_SetHGaugeCG(int parts_no, int cg_no, int state)
{
	return set_gauge_cg(parts_no, cg_no, state, false);
}

static void parts_set_hgauge_rate(struct parts *parts, float rate, int state)
{
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	if (!g->texture.handle) {
		WARNING("HGauge texture uninitialized");
		return;
	}
	int pixels = rate * g->cg.w;
	gfx_copy_with_alpha_map(&g->texture, 0, 0, &g->cg, 0, 0, pixels, g->cg.h);
	gfx_fill_amap(&g->texture, pixels, 0, g->cg.w - pixels, g->cg.h, 0);
	parts_dirty(parts);
}

static bool GoatGUIEngine_SetHGaugeRate(int parts_no, int numerator, int denominator, int state)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	parts_set_hgauge_rate(parts_get(parts_no), (float)numerator/(float)denominator, state);
	return true;
}

static bool GoatGUIEngine_SetVGaugeCG(int parts_no, int cg_no, int state)
{
	return set_gauge_cg(parts_no, cg_no, state, true);
}

static void parts_set_vgauge_rate(struct parts *parts, float rate, int state)
{
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	if (!g->texture.handle) {
		WARNING("VGauge texture uninitialized");
		return;
	}
	int pixels = rate * g->cg.h;
	gfx_copy_with_alpha_map(&g->texture, 0, pixels, &g->cg, 0, pixels, g->cg.w, g->cg.h - pixels);
	gfx_fill_amap(&g->texture, 0, 0, g->cg.w, pixels, 0);
	parts_dirty(parts);
}


static bool GoatGUIEngine_SetVGaugeRate(int parts_no, int numerator, int denominator, int state)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	parts_set_vgauge_rate(parts_get(parts_no), (float)numerator/(float)denominator, state);
	return true;
}

static bool GoatGUIEngine_SetNumeralCG(int parts_no, int cg_no, int state)
{
	state--;
	if (state < 0 || state >= PARTS_NR_STATES)
		return false;

	struct parts_numeral *n = parts_get_numeral(parts_get(parts_no), state);
	for (int i = 0; i < 12; i++) {
		gfx_delete_texture(&n->cg[i]);
	}
	// NOTE: textures are loaded lazily since not every numeral font loaded
	//       in this manner is complete (some lack dash or comma glyphs)
	n->cg_no = cg_no;
	return true;
}

static bool GoatGUIEngine_SetNumeralLinkedCGNumberWidthWidthList(int parts_no, int cg_no, int w0, int w1, int w2, int w3, int w4, int w5, int w6, int w7, int w8, int w9, int w_minus, int w_comma, int state)
{
	// TODO? Create a generic numeral-font object that can be shared between parts.
	//       In the current implementation, each number stores a complete copy of
	//       its numeral font.

	state--;
	if (state < 0 || state > PARTS_NR_STATES)
		return false;

	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return false;

	int w[12] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w_minus, w_comma };
	Texture t = {0};
	gfx_init_texture_with_cg(&t, cg);
	cg_free(cg);

	int x = 0;
	struct parts_numeral *n = parts_get_numeral(parts_get(parts_no), state);
	for (int i = 0; i < 12; i++) {
		gfx_delete_texture(&n->cg[i]);
		if (w[i] <= 0)
			continue;

		gfx_init_texture_blank(&n->cg[i], w[i], t.h);
		gfx_copy_with_alpha_map(&n->cg[i], 0, 0, &t, x, 0, w[i], t.h);
		x += w[i];
	}

	gfx_delete_texture(&t);
	return true;
}

static bool parts_set_number(struct parts *parts, int n, int state)
{
	struct parts_numeral *num = parts_get_numeral(parts, state);
	bool negative = n < 0;
	if (negative)
		n *= -1;

	// extract digits
	int digits;
	uint8_t d[32];
	for (digits = 0; n; digits++) {
		d[digits] = n % 10;
		n /= 10;
	}

	// encode number as texture indices
	int nr_chars = 0;
	uint8_t chars[32];
	if (negative) {
		chars[nr_chars++] = 10;
	}
	for (int i = 0; i < digits; i++) {
		if (num->show_comma && i > 0 && i % 3 == 0) {
			chars[nr_chars++] = 11;
		}
		chars[nr_chars++] = d[i];
	}

	// load any uninitialized textures
	for (int i = 0; i < nr_chars; i++) {
		if (num->cg[chars[i]].handle)
			continue;
		struct cg *cg = asset_cg_load(num->cg_no + chars[i]);
		if (!cg) {
			WARNING("Failed to load numeral cg: %d", num->cg_no + chars[i]);
			continue;
		}
		gfx_init_texture_with_cg(&num->cg[chars[i]], cg);
		cg_free(cg);
	}

	// determine output dimensions
	int w = 0, h = 0;
	for (int i = nr_chars-1; i >= 0; i--) {
		if (!num->cg[chars[i]].handle)
			continue;
		w += num->cg[chars[i]].w;
		h = max(h, num->cg[chars[i]].h);
	}
	w += (nr_chars-1) * num->space;

	// copy chars to texture
	gfx_delete_texture(&num->texture);
	gfx_init_texture_rgba(&num->texture, w, h, (SDL_Color){0, 0, 0, 255});

	int x = 0;
	for (int i = nr_chars-1; i>= 0; i--) {
		Texture *ch = &num->cg[chars[i]];
		if (!ch->handle)
			continue;
		gfx_copy_with_alpha_map(&num->texture, x, 0, ch, 0, 0, ch->w, ch->h);
		x += ch->w + num->space;
	}

	parts->rect.w = w;
	parts->rect.h = h;

	parts_dirty(parts);
	return true;
}

static bool GoatGUIEngine_SetNumeralNumber(int parts_no, int n, int state)
{
	state--;
	if (state < 0 || state > PARTS_NR_STATES)
		return false;

	return parts_set_number(parts_get(parts_no), n, state);
}

static bool GoatGUIEngine_SetNumeralShowComma(int parts_no, bool show_comma, int state)
{
	state--;
	if (state < 0 || state > PARTS_NR_STATES)
		return false;

	parts_get_numeral(parts_get(parts_no), state)->show_comma = show_comma;
	return true;
}

static bool GoatGUIEngine_SetNumeralSpace(int parts_no, int space, int state)
{
	state--;
	if (state < 0 || state > PARTS_NR_STATES)
		return false;

	parts_get_numeral(parts_get(parts_no), state)->space = space;
	return true;
}

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

static void GoatGUIEngine_ReleaseAllParts(void)
{
	parts_release_all();
}

static void GoatGUIEngine_ReleaseAllPartsWithoutSystem(void)
{
	// FIXME: what's the difference?
	parts_release_all();
}

static void GoatGUIEngine_SetPos(int parts_no, int x, int y)
{
	parts_set_pos(parts_get(parts_no), (Point){ x, y });
}

/*
 * NOTE: If a ChipmunkSpriteEngine sprite is at Z=0, it's drawn behind;
 *       otherwise it's drawn in front, regardless of the parts Z-value.
 *       This is implemented by giving GoatGUIEngine sprites a Z-value of
 *       0 and a secondary Z-value of 1 + the value given here.
 */
static void GoatGUIEngine_SetZ(int parts_no, int z)
{
	struct parts *parts = parts_get(parts_no);
	parts->z = z;

	struct parts_list *list = parts_get_list(parts);
	parts_list_remove(list, parts);
	parts_list_insert(list, parts);
}

static void GoatGUIEngine_SetShow(int parts_no, bool show)
{
	struct parts *parts = parts_get(parts_no);
	if (parts->show != show) {
		parts->show = show;
		parts_dirty(parts);
	}
}

static void GoatGUIEngine_SetAlpha(int parts_no, int alpha)
{
	parts_set_alpha(parts_get(parts_no), alpha);
}

void GoatGUIEngine_SetPartsDrawFilter(int PartsNumber, int DrawFilter);

static void GoatGUIEngine_SetClickable(int parts_no, bool clickable)
{
	parts_get(parts_no)->clickable = !!clickable;
}

static int GoatGUIEngine_GetPartsX(int parts_no)
{
	return parts_get(parts_no)->rect.x;
}

static int GoatGUIEngine_GetPartsY(int parts_no)
{
	return parts_get(parts_no)->rect.y;
}

static int GoatGUIEngine_GetPartsZ(int parts_no)
{
	return parts_get(parts_no)->z;
}

static bool GoatGUIEngine_GetPartsShow(int parts_no)
{
	return parts_get(parts_no)->show;
}

static int GoatGUIEngine_GetPartsAlpha(int parts_no)
{
	return parts_get(parts_no)->alpha;
}

static bool GoatGUIEngine_GetPartsClickable(int parts_no)
{
	return parts_get(parts_no)->clickable;
}

static void GoatGUIEngine_SetPartsOriginPosMode(int parts_no, int origin_pos_mode)
{
	struct parts *parts = parts_get(parts_no);
	parts->origin_mode = origin_pos_mode;
	parts_recalculate_pos(parts);
	parts_dirty(parts);
}

static int GoatGUIEngine_GetPartsOriginPosMode(int parts_no)
{
	return parts_get(parts_no)->origin_mode;
}

static void GoatGUIEngine_SetParentPartsNumber(int parts_no, int parent_parts_no)
{
	struct parts *parts = parts_get(parts_no);
	if (parts->parent == parent_parts_no)
		return;

	parts_list_remove(parts_get_list(parts), parts);
	parts_list_insert(&parts_get(parent_parts_no)->children, parts);
	parts->parent = parent_parts_no;
}

static bool GoatGUIEngine_SetPartsGroupNumber(int PartsNumber, int GroupNumber)
{
	// TODO
	return true;
}

static void GoatGUIEngine_SetPartsGroupDecideOnCursor(int GroupNumber, bool DecideOnCursor)
{
	// TODO
}

static void GoatGUIEngine_SetPartsGroupDecideClick(int GroupNumber, bool DecideClick)
{
	// TODO
}

static void GoatGUIEngine_SetOnCursorShowLinkPartsNumber(int parts_no, int link_parts_no)
{
	struct parts *parts = parts_get(parts_no);
	struct parts *link_parts = parts_get(link_parts_no);
	parts->linked_to = link_parts_no;
	link_parts->linked_from = parts_no;
}

static void GoatGUIEngine_SetPartsMessageWindowShowLink(int parts_no, bool message_window_show_link)
{
	// TODO
}

bool GoatGUIEngine_GetPartsMessageWindowShowLink(int PartsNumber);

static bool GoatGUIEngine_SetPartsOnCursorSoundNumber(int parts_no, int sound_no)
{
	if (!asset_exists(ASSET_SOUND, sound_no-1)) {
		WARNING("Invalid sound number: %d", sound_no);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	parts->on_cursor_sound = sound_no;
	return true;
}

static bool GoatGUIEngine_SetPartsClickSoundNumber(int parts_no, int sound_no)
{
	if (!asset_exists(ASSET_SOUND, sound_no-1)) {
		WARNING("Invalid sound number: %d", sound_no);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	parts->on_click_sound = sound_no;
	return true;
}

static bool GoatGUIEngine_SetClickMissSoundNumber(int sound_no)
{
	// TODO
	return true;
}

static void GoatGUIEngine_BeginClick(void)
{
	began_click = true;
}

static void GoatGUIEngine_EndClick(void)
{
	began_click = false;
	clicked_parts = 0;
}

static int GoatGUIEngine_GetClickPartsNumber(void)
{
	return clicked_parts;
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

void GoatGUIEngine_AddMotionCG(int parts_no, int begin_cg_no, int nr_cg, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_CG, begin_t, end_t);
	motion->begin.i = begin_cg_no;
	motion->end.i = begin_cg_no + nr_cg - 1;
	parts_add_motion(parts, motion);
}

void GoatGUIEngine_AddMotionHGaugeRate(int parts_no, int begin_numerator, int begin_denominator,
				       int end_numerator, int end_denominator, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_HGUAGE_RATE, begin_t, end_t);
	motion->begin.f = (float)begin_numerator / (float)begin_denominator;
	motion->end.f = (float)end_numerator / (float)end_denominator;
	parts_add_motion(parts, motion);
}
void GoatGUIEngine_AddMotionVGaugeRate(int parts_no, int begin_numerator, int begin_denominator,
				       int end_numerator, int end_denominator, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_VGUAGE_RATE, begin_t, end_t);
	motion->begin.f = (float)begin_numerator / (float)begin_denominator;
	motion->end.f = (float)end_numerator / (float)end_denominator;
	parts_add_motion(parts, motion);
}

void GoatGUIEngine_AddMotionNumeralNumber(int parts_no, int begin_n, int end_n, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_NUMERAL_NUMBER, begin_t, end_t);
	motion->begin.i = begin_n;
	motion->end.i = end_n;
	parts_add_motion(parts, motion);
}

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

static void GoatGUIEngine_AddMotionVibrationSize(int parts_no, int begin_w, int begin_h, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_VIBRATION_SIZE, begin_t, end_t);
	motion->begin.x = begin_w;
	motion->begin.y = begin_h;
	parts_add_motion(parts, motion);
}

static void GoatGUIEngine_AddMotionSound(int sound_no, int begin_t)
{
	struct sound_motion *m = xcalloc(1, sizeof(struct sound_motion));
	m->sound_no = sound_no;
	m->begin_time = begin_t;

	struct sound_motion *p;
	TAILQ_FOREACH(p, &sound_motion_list, entry) {
		if (p->begin_time > m->begin_time) {
			TAILQ_INSERT_BEFORE(p, m, entry);
			return;
		}
	}
	TAILQ_INSERT_TAIL(&sound_motion_list, m, entry);
	if (m->begin_time > motion_end_t)
		motion_end_t = m->begin_time;
}

static void GoatGUIEngine_BeginMotion(void)
{
	if (began_click)
		return;

	// FIXME: starting a motion seems to clear non-default states
	motion_t = 0;
	is_motion = true;
	parts_list_init_all_motion(&parts_list);
}

static void GoatGUIEngine_EndMotion(void)
{
	if (began_click)
		return;
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

static void GoatGUIEngine_SetPartsMagX(int parts_no, float scale_x)
{
	parts_set_scale_x(parts_get(parts_no), scale_x);
}

static void GoatGUIEngine_SetPartsMagY(int parts_no, float scale_y)
{
	parts_set_scale_y(parts_get(parts_no), scale_y);
}

void GoatGUIEngine_SetPartsRotateX(int parts_no, float rot_x);
void GoatGUIEngine_SetPartsRotateY(int parts_no, float rot_y);

static void GoatGUIEngine_SetPartsRotateZ(int parts_no, float rot_z)
{
	parts_set_rotation_z(parts_get(parts_no), rot_z);
}

void GoatGUIEngine_SetPartsAlphaClipperPartsNumber(int PartsNumber, int AlphaClipperPartsNumber);
void GoatGUIEngine_SetPartsPixelDecide(int PartsNumber, bool PixelDecide);
bool GoatGUIEngine_SetThumbnailReductionSize(int ReductionSize);
bool GoatGUIEngine_SetThumbnailMode(bool Mode);

static bool GoatGUIEngine_Save(struct page **buffer)
{
	if (*buffer) {
		delete_page_vars(*buffer);
		free_page(*buffer);
	}
	*buffer = NULL;
	return true;
}

static bool GoatGUIEngine_Load(struct page **buffer)
{
	return true;
}

HLL_LIBRARY(GoatGUIEngine,
	    HLL_EXPORT(Init, GoatGUIEngine_Init),
	    HLL_EXPORT(Update, GoatGUIEngine_Update),
	    HLL_EXPORT(SetPartsCG, GoatGUIEngine_SetPartsCG),
	    HLL_EXPORT(GetPartsCGNumber, GoatGUIEngine_GetPartsCGNumber),
	    HLL_EXPORT(SetLoopCG, GoatGUIEngine_SetLoopCG),
	    HLL_EXPORT(SetText, GoatGUIEngine_SetText),
	    HLL_EXPORT(AddPartsText, GoatGUIEngine_AddPartsText),
	    HLL_TODO_EXPORT(DeletePartsTopTextLine, GoatGUIEngine_DeletePartsTopTextLine),
	    HLL_EXPORT(SetFont, GoatGUIEngine_SetFont),
	    HLL_EXPORT(SetPartsFontType, GoatGUIEngine_SetPartsFontType),
	    HLL_EXPORT(SetPartsFontSize, GoatGUIEngine_SetPartsFontSize),
	    HLL_EXPORT(SetPartsFontColor, GoatGUIEngine_SetPartsFontColor),
	    HLL_EXPORT(SetPartsFontBoldWeight, GoatGUIEngine_SetPartsFontBoldWeight),
	    HLL_EXPORT(SetPartsFontEdgeColor, GoatGUIEngine_SetPartsFontEdgeColor),
	    HLL_EXPORT(SetPartsFontEdgeWeight, GoatGUIEngine_SetPartsFontEdgeWeight),
	    HLL_EXPORT(SetTextCharSpace, GoatGUIEngine_SetTextCharSpace),
	    HLL_EXPORT(SetTextLineSpace, GoatGUIEngine_SetTextLineSpace),
	    HLL_EXPORT(SetHGaugeCG, GoatGUIEngine_SetHGaugeCG),
	    HLL_EXPORT(SetHGaugeRate, GoatGUIEngine_SetHGaugeRate),
	    HLL_EXPORT(SetVGaugeCG, GoatGUIEngine_SetVGaugeCG),
	    HLL_EXPORT(SetVGaugeRate, GoatGUIEngine_SetVGaugeRate),
	    HLL_EXPORT(SetNumeralCG, GoatGUIEngine_SetNumeralCG),
	    HLL_EXPORT(SetNumeralLinkedCGNumberWidthWidthList, GoatGUIEngine_SetNumeralLinkedCGNumberWidthWidthList),
	    HLL_EXPORT(SetNumeralNumber, GoatGUIEngine_SetNumeralNumber),
	    HLL_EXPORT(SetNumeralShowComma, GoatGUIEngine_SetNumeralShowComma),
	    HLL_EXPORT(SetNumeralSpace, GoatGUIEngine_SetNumeralSpace),
	    HLL_TODO_EXPORT(SetPartsRectangleDetectionSize, GoatGUIEngine_SetPartsRectangleDetectionSize),
	    HLL_TODO_EXPORT(SetPartsFlash, GoatGUIEngine_SetPartsFlash),
	    HLL_TODO_EXPORT(IsPartsFlashEnd, GoatGUIEngine_IsPartsFlashEnd),
	    HLL_TODO_EXPORT(GetPartsFlashCurrentFrameNumber, GoatGUIEngine_GetPartsFlashCurrentFrameNumber),
	    HLL_TODO_EXPORT(BackPartsFlashBeginFrame, GoatGUIEngine_BackPartsFlashBeginFrame),
	    HLL_TODO_EXPORT(StepPartsFlashFinalFrame, GoatGUIEngine_StepPartsFlashFinalFrame),
	    HLL_EXPORT(ReleaseParts, GoatGUIEngine_ReleaseParts),
	    HLL_EXPORT(ReleaseAllParts, GoatGUIEngine_ReleaseAllParts),
	    HLL_EXPORT(ReleaseAllPartsWithoutSystem, GoatGUIEngine_ReleaseAllPartsWithoutSystem),
	    HLL_EXPORT(SetPos, GoatGUIEngine_SetPos),
	    HLL_EXPORT(SetZ, GoatGUIEngine_SetZ),
	    HLL_EXPORT(SetShow, GoatGUIEngine_SetShow),
	    HLL_EXPORT(SetAlpha, GoatGUIEngine_SetAlpha),
	    HLL_TODO_EXPORT(SetPartsDrawFilter, GoatGUIEngine_SetPartsDrawFilter),
	    HLL_EXPORT(SetClickable, GoatGUIEngine_SetClickable),
	    HLL_EXPORT(GetPartsX, GoatGUIEngine_GetPartsX),
	    HLL_EXPORT(GetPartsY, GoatGUIEngine_GetPartsY),
	    HLL_EXPORT(GetPartsZ, GoatGUIEngine_GetPartsZ),
	    HLL_EXPORT(GetPartsShow, GoatGUIEngine_GetPartsShow),
	    HLL_EXPORT(GetPartsAlpha, GoatGUIEngine_GetPartsAlpha),
	    HLL_EXPORT(GetPartsClickable, GoatGUIEngine_GetPartsClickable),
	    HLL_EXPORT(SetPartsOriginPosMode, GoatGUIEngine_SetPartsOriginPosMode),
	    HLL_EXPORT(GetPartsOriginPosMode, GoatGUIEngine_GetPartsOriginPosMode),
	    HLL_EXPORT(SetParentPartsNumber, GoatGUIEngine_SetParentPartsNumber),
	    HLL_EXPORT(SetPartsGroupNumber, GoatGUIEngine_SetPartsGroupNumber),
	    HLL_EXPORT(SetPartsGroupDecideOnCursor, GoatGUIEngine_SetPartsGroupDecideOnCursor),
	    HLL_EXPORT(SetPartsGroupDecideClick, GoatGUIEngine_SetPartsGroupDecideClick),
	    HLL_EXPORT(SetOnCursorShowLinkPartsNumber, GoatGUIEngine_SetOnCursorShowLinkPartsNumber),
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
	    HLL_EXPORT(AddMotionCG, GoatGUIEngine_AddMotionCG),
	    HLL_EXPORT(AddMotionHGaugeRate, GoatGUIEngine_AddMotionHGaugeRate),
	    HLL_EXPORT(AddMotionVGaugeRate, GoatGUIEngine_AddMotionVGaugeRate),
	    HLL_EXPORT(AddMotionNumeralNumber, GoatGUIEngine_AddMotionNumeralNumber),
	    HLL_EXPORT(AddMotionMagX, GoatGUIEngine_AddMotionMagX),
	    HLL_EXPORT(AddMotionMagY, GoatGUIEngine_AddMotionMagY),
	    HLL_EXPORT(AddMotionRotateX, GoatGUIEngine_AddMotionRotateX),
	    HLL_EXPORT(AddMotionRotateY, GoatGUIEngine_AddMotionRotateY),
	    HLL_EXPORT(AddMotionRotateZ, GoatGUIEngine_AddMotionRotateZ),
	    HLL_EXPORT(AddMotionVibrationSize, GoatGUIEngine_AddMotionVibrationSize),
	    HLL_EXPORT(AddMotionSound, GoatGUIEngine_AddMotionSound),
	    HLL_EXPORT(BeginMotion, GoatGUIEngine_BeginMotion),
	    HLL_EXPORT(EndMotion, GoatGUIEngine_EndMotion),
	    HLL_EXPORT(SetMotionTime, GoatGUIEngine_SetMotionTime),
	    HLL_EXPORT(IsMotion, GoatGUIEngine_IsMotion),
	    HLL_EXPORT(GetMotionEndTime, GoatGUIEngine_GetMotionEndTime),
	    HLL_EXPORT(SetPartsMagX, GoatGUIEngine_SetPartsMagX),
	    HLL_EXPORT(SetPartsMagY, GoatGUIEngine_SetPartsMagY),
	    HLL_TODO_EXPORT(SetPartsRotateX, GoatGUIEngine_SetPartsRotateX),
	    HLL_TODO_EXPORT(SetPartsRotateY, GoatGUIEngine_SetPartsRotateY),
	    HLL_EXPORT(SetPartsRotateZ, GoatGUIEngine_SetPartsRotateZ),
	    HLL_TODO_EXPORT(SetPartsAlphaClipperPartsNumber, GoatGUIEngine_SetPartsAlphaClipperPartsNumber),
	    HLL_TODO_EXPORT(SetPartsPixelDecide, GoatGUIEngine_SetPartsPixelDecide),
	    HLL_TODO_EXPORT(SetThumbnailReductionSize, GoatGUIEngine_SetThumbnailReductionSize),
	    HLL_TODO_EXPORT(SetThumbnailMode, GoatGUIEngine_SetThumbnailMode),
	    HLL_EXPORT(Save, GoatGUIEngine_Save),
	    HLL_EXPORT(Load, GoatGUIEngine_Load));

