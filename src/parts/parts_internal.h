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
	unsigned line_space;
	Point cursor;
	struct text_style ts;
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

enum parts_cp_op_type {
	PARTS_CP_CG,
	PARTS_CP_FILL_ALPHA_COLOR
};

struct parts_cp_cg {
	int no;
};

struct parts_cp_fill_alpha_color {
	int x, y, w, h;
	int r, g, b, a;
};

struct parts_cp_op {
	TAILQ_ENTRY(parts_cp_op) entry;
	enum parts_cp_op_type type;
	union {
		struct parts_cp_cg cg;
		struct parts_cp_fill_alpha_color fill_alpha_color;
	};
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
	TAILQ_HEAD(, parts_cp_op) construction_process;
};

TAILQ_HEAD(parts_list, parts);

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

// parts.c
extern struct parts_list parts_list;
struct parts *parts_get(int parts_no);
struct parts_cg *parts_get_cg(struct parts *parts, int state);
struct parts_text *parts_get_text(struct parts *parts, int state);
struct parts_animation *parts_get_animation(struct parts *parts, int state);
struct parts_numeral *parts_get_numeral(struct parts *parts, int state);
struct parts_gauge *parts_get_hgauge(struct parts *parts, int state);
struct parts_gauge *parts_get_vgauge(struct parts *parts, int state);
void parts_recalculate_offset(struct parts *parts);
void parts_recalculate_pos(struct parts *parts);
void parts_set_pos(struct parts *parts, Point pos);
void parts_set_scale_x(struct parts *parts, float mag);
void parts_set_scale_y(struct parts *parts, float mag);
void parts_set_rotation_z(struct parts *parts, float rot);
void parts_set_alpha(struct parts *parts, int alpha);
void parts_set_cg_dims(struct parts *parts, struct cg *cg);
bool parts_set_cg_by_index(struct parts *parts, int cg_no, int state);
void parts_set_hgauge_rate(struct parts *parts, float rate, int state);
void parts_set_vgauge_rate(struct parts *parts, float rate, int state);
bool parts_set_number(struct parts *parts, int n, int state);
void parts_set_state(struct parts *parts, enum parts_state_type state);
void parts_release(int parts_no);
void parts_release_all(void);

// render.c
void parts_render_init(void);
void parts_engine_dirty(void);
void parts_dirty(struct parts *parts);

// motion.c
void parts_clear_motion(struct parts *parts);
void parts_add_motion(struct parts *parts, struct parts_motion *motion);

// input.c
extern Point parts_prev_pos;
extern bool parts_began_click;

// construction.c
void parts_cp_op_free(struct parts_cp_op *op);

static inline bool parts_state_valid(int state)
{
	return state >= 0 && state <= 2;
}

#endif /* SYSTEM4_PARTS_INTERNAL_H */
