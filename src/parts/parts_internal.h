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

#include <cglm/types.h>
#include "gfx/gfx.h"
#include "gfx/font.h"
#include "queue.h"
#include "swf.h"

typedef struct cJSON cJSON;
struct string;
struct hash_table;

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

struct parts_text_char {
	Texture t;
	char ch[4];
	int advance;
	Point off;
};

struct parts_text_line {
	struct parts_text_char *chars;
	int nr_chars;
	unsigned height;
	unsigned width;
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
	PARTS_FLASH,
#define PARTS_NR_TYPES (PARTS_FLASH+1)
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
	struct string *cg_name;
	unsigned start_no;
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
	int cg_no;
	float rate;
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

struct parts_flash_object {
	TAILQ_ENTRY(parts_flash_object) entry;
	uint16_t depth;
	uint16_t character_id;
	mat4 matrix;
	struct swf_cxform_with_alpha color_transform;
};

struct parts_flash {
	struct parts_common common;
	struct string *name;
	struct swf *swf;

	struct swf_tag *tag;
	bool stopped;
	unsigned elapsed;
	int current_frame;
	struct hash_table *dictionary;
	struct hash_table *bitmaps;  // bitmap character id -> struct texture *
	struct hash_table *sprites;  // sprite character id -> struct parts_flash_object *
	TAILQ_HEAD(, parts_flash_object) display_list;
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
		struct parts_flash flash;
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
struct parts_flash *parts_get_flash(struct parts *parts, int state);
void parts_set_pos(struct parts *parts, Point pos);
void parts_set_dims(struct parts *parts, struct parts_common *common, int w, int h);
void parts_set_scale_x(struct parts *parts, float mag);
void parts_set_scale_y(struct parts *parts, float mag);
void parts_set_rotation_z(struct parts *parts, float rot);
void parts_set_alpha(struct parts *parts, int alpha);
void parts_set_state(struct parts *parts, enum parts_state_type state);
void parts_release(int parts_no);
void parts_release_all(void);
void parts_set_surface_area(struct parts *parts, struct parts_common *common, int x, int y, int w, int h);

// for save.c
void parts_list_resort(struct parts *parts);
void parts_state_reset(struct parts_state *state, enum parts_type type);
bool parts_cg_set(struct parts *parts, struct parts_cg *cg, struct string *cg_name);
bool parts_cg_set_by_index(struct parts *parts, struct parts_cg *cg, int cg_no);
void parts_text_append(struct parts *parts, struct parts_text *t, struct string *text);
bool parts_animation_set_cg_by_index(struct parts *parts, struct parts_animation *anim,
		int cg_no, int nr_frames, int frame_time);
bool parts_animation_set_cg(struct parts *parts, struct parts_animation *anim,
		struct string *cg_name, int start_no, int nr_frames, int frame_time);
bool parts_numeral_set_number(struct parts *parts, struct parts_numeral *num, int n);
bool parts_gauge_set_cg(struct parts *parts, struct parts_gauge *g, struct string *cg_name);
bool parts_gauge_set_cg_by_index(struct parts *parts, struct parts_gauge *g, int cg_no);
void parts_hgauge_set_rate(struct parts *parts, struct parts_gauge *g, float rate);
void parts_vgauge_set_rate(struct parts *parts, struct parts_gauge *g, float rate);

// text.c
void parts_text_free(struct parts_text *t);
struct string *parts_text_line_get(struct parts_text_line *line);
struct string *parts_text_get(struct parts_text *t);

// render.c
void parts_render_init(void);
void parts_engine_dirty(void);
void parts_dirty(struct parts *parts);
void parts_render(struct parts *parts);
void parts_render_family(struct parts *parts);

// motion.c
void parts_clear_motion(struct parts *parts);
void parts_add_motion(struct parts *parts, struct parts_motion *motion);

// input.c
extern Point parts_prev_pos;
extern bool parts_began_click;

// construction.c
void parts_cp_op_free(struct parts_cp_op *op);
void parts_add_cp_op(struct parts_construction_process *cproc, struct parts_cp_op *op);
bool parts_build_construction_process(struct parts *parts,
		struct parts_construction_process *cproc);

// flash.c
void parts_flash_free(struct parts_flash *f);
bool parts_flash_load(struct parts *parts, struct parts_flash *f, struct string *filename);
bool parts_flash_update(struct parts_flash *f, int passed_time);
bool parts_flash_seek(struct parts_flash *f, int frame);

// debug.c
struct sprite;
void parts_debug_init(void);
cJSON *parts_engine_to_json(struct sprite *sp, bool verbose);

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
