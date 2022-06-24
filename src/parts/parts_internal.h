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

#ifndef SYSTEM4_PARTS_INTERNAL_H
#define SYSTEM4_PARTS_INTERNAL_H

#include "gfx/gfx.h"
#include "gfx/font.h"
#include "queue.h"

struct string;

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
	PARTS_MOTION_HGAUGE_RATE,
	PARTS_MOTION_VGAUGE_RATE,
	PARTS_MOTION_NUMERAL_NUMBER,
	PARTS_MOTION_MAG_X,
	PARTS_MOTION_MAG_Y,
	PARTS_MOTION_ROTATE_X,
	PARTS_MOTION_ROTATE_Y,
	PARTS_MOTION_ROTATE_Z,
	PARTS_MOTION_VIBRATION_SIZE
#define PARTS_NR_MOTION_TYPES (PARTS_MOTION_VIBRATION_SIZE+1)
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

struct parts_text_line {
	unsigned height;
};

enum parts_type {
	PARTS_UNINITIALIZED,
	PARTS_CG,
	PARTS_TEXT,
	PARTS_ANIMATION,
	PARTS_NUMERAL,
	PARTS_HGAUGE,
	PARTS_VGAUGE,
	PARTS_CONSTRUCTION_PROCESS,
};

struct parts_common {
	Texture texture;
	int w, h;
	Point origin_offset;
	Rectangle hitbox;
	Rectangle surface_area;
};

struct parts_cg {
	struct parts_common common;
	int no;
	struct string *name;
};

struct parts_text {
	struct parts_common common;
	unsigned nr_lines;
	struct parts_text_line *lines;
	unsigned line_space;
	Point cursor;
	struct text_style ts;
};

struct parts_animation {
	struct parts_common common;
	unsigned cg_no;
	unsigned nr_frames;
	Texture *frames;
	unsigned frame_time;
	unsigned elapsed;
	unsigned current_frame;
};

struct parts_numeral {
	struct parts_common common;
	Texture cg[12];
	bool have_num;
	int num;
	int space;
	int show_comma;
	int length;
	int cg_no;
};

struct parts_gauge {
	struct parts_common common;
	Texture cg;
};

enum parts_cp_op_type {
	PARTS_CP_CREATE,
	PARTS_CP_CREATE_PIXEL_ONLY,
	PARTS_CP_CG,
	PARTS_CP_FILL,
	PARTS_CP_FILL_ALPHA_COLOR,
	PARTS_CP_FILL_AMAP,
	PARTS_CP_DRAW_CUT_CG,
	PARTS_CP_COPY_CUT_CG,
	PARTS_CP_DRAW_TEXT,
	PARTS_CP_COPY_TEXT,
#define PARTS_NR_CP_TYPES (PARTS_CP_COPY_TEXT+1)
};

struct parts_cp_create {
	int w;
	int h;
};

struct parts_cp_cg {
	int no;
};

struct parts_cp_fill {
	int x, y, w, h;
	int r, g, b, a;
};

struct parts_cp_cut_cg {
	int cg_no;
	int dx, dy, dw, dh;
	int sx, sy, sw, sh;
	int interp_type;
};

struct parts_cp_text {
	struct string *text;
	int x, y;
	int line_space;
	struct text_style style;
};

struct parts_cp_op {
	TAILQ_ENTRY(parts_cp_op) entry;
	enum parts_cp_op_type type;
	union {
		struct parts_cp_create create;
		struct parts_cp_cg cg;
		struct parts_cp_fill fill;
		struct parts_cp_cut_cg cut_cg;
		struct parts_cp_text text;
	};
};

struct parts_construction_process {
	struct parts_common common;
	TAILQ_HEAD(, parts_cp_op) ops;
};

struct parts_state {
	enum parts_type type;
	union {
		struct parts_common common;
		struct parts_cg cg;
		struct parts_text text;
		struct parts_animation anim;
		struct parts_numeral num;
		struct parts_gauge gauge;
		struct parts_construction_process cproc;
	};
};

TAILQ_HEAD(parts_list, parts);

struct parts_params {
	int z;
	Point pos;
	bool show;
	uint8_t alpha;
	struct { float x, y; } scale;
	struct { float x, y, z; } rotation;
	SDL_Color add_color;
	SDL_Color multiply_color;
};

struct parts {
	//struct sprite sp;
	enum parts_state_type state;
	struct parts_state states[PARTS_NR_STATES];
	TAILQ_ENTRY(parts) parts_list_entry;
	TAILQ_ENTRY(parts) child_list_entry;
	TAILQ_ENTRY(parts) dirty_list_entry;
	struct parts_list children;
	struct parts_params local;
	struct parts_params global;
	struct parts *parent;
	int dirty;
	int no;
	int delegate_index;
	int sprite_deform;
	bool clickable;
	int on_cursor_sound;
	int on_click_sound;
	int origin_mode;
	int pending_parent;
	int linked_to;
	int linked_from;
	int draw_filter;
	TAILQ_HEAD(, parts_motion) motion;
};

#define PARTS_LIST_FOREACH(iter) TAILQ_FOREACH(iter, &parts_list, parts_list_entry)
#define PARTS_FOREACH_CHILD(iter, parent) TAILQ_FOREACH(iter, &parent->children, child_list_entry)

// parts.c
extern struct parts_list parts_list;
struct parts *parts_try_get(int parts_no);
struct parts *parts_get(int parts_no);
struct parts_cg *parts_get_cg(struct parts *parts, int state);
struct parts_text *parts_get_text(struct parts *parts, int state);
struct parts_animation *parts_get_animation(struct parts *parts, int state);
struct parts_numeral *parts_get_numeral(struct parts *parts, int state);
struct parts_gauge *parts_get_hgauge(struct parts *parts, int state);
struct parts_gauge *parts_get_vgauge(struct parts *parts, int state);
struct parts_construction_process *parts_get_construction_process(struct parts *parts, int state);
void parts_set_pos(struct parts *parts, Point pos);
void parts_set_dims(struct parts *parts, struct parts_common *common, int w, int h);
void parts_set_scale_x(struct parts *parts, float mag);
void parts_set_scale_y(struct parts *parts, float mag);
void parts_set_rotation_z(struct parts *parts, float rot);
void parts_set_alpha(struct parts *parts, int alpha);
bool parts_set_cg_by_index(struct parts *parts, int cg_no, int state);
bool parts_set_cg_by_name(struct parts *parts, struct string *cg_name, int state);
void parts_set_hgauge_rate(struct parts *parts, float rate, int state);
void parts_set_vgauge_rate(struct parts *parts, float rate, int state);
bool parts_set_number(struct parts *parts, int n, int state);
void parts_set_state(struct parts *parts, enum parts_state_type state);
void parts_release(int parts_no);
void parts_release_all(void);
void parts_set_surface_area(struct parts *parts, struct parts_common *common, int x, int y, int w, int h);

// render.c
void parts_render_init(void);
void parts_engine_dirty(void);
void parts_dirty(struct parts *parts);
void parts_render(struct parts *parts);

// motion.c
void parts_clear_motion(struct parts *parts);
void parts_add_motion(struct parts *parts, struct parts_motion *motion);

// input.c
extern Point parts_prev_pos;
extern bool parts_began_click;

// construction.c
void parts_cp_op_free(struct parts_cp_op *op);

// debug.c
void parts_debug_init(void);
void parts_print(struct parts *parts);
void parts_engine_print(void);

static inline bool parts_state_valid(int state)
{
	return state >= 0 && state <= 2;
}

static inline int parts_get_width(struct parts *parts)
{
	return parts->states[parts->state].common.w;
}

static inline int parts_get_height(struct parts *parts)
{
	return parts->states[parts->state].common.h;
}

#endif /* SYSTEM4_PARTS_INTERNAL_H */
