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
#include "system4/flat.h"
#include "gfx/gfx.h"
#include "gfx/font.h"
#include "queue.h"
#include "scene.h"
#include "swf.h"

typedef struct cJSON cJSON;
struct string;
struct hash_table;

extern bool parts_v2;
extern bool parts_hierarchical_matrix;

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
	PARTS_MOTION_VIBRATION_SIZE,
	PARTS_MOTION_ADD_COLOR,
	PARTS_MOTION_MUL_COLOR,
	PARTS_MOTION_CG_NAME
#define PARTS_NR_MOTION_TYPES (PARTS_MOTION_CG_NAME+1)
};

union parts_motion_param {
	int i;
	float f;
	struct {
		int x;
		int y;
	};
	SDL_Color color;
};

struct parts_motion {
	TAILQ_ENTRY(parts_motion) entry;
	enum parts_motion_type type;
	union parts_motion_param begin;
	union parts_motion_param end;
	struct string *cg_name;
	int begin_time;
	int end_time;
};

TAILQ_HEAD(parts_motion_list, parts_motion);

struct sound_motion {
	TAILQ_ENTRY(sound_motion) entry;
	int begin_time;
	int sound_no;
	struct string *sound_name;
	bool played;
};

struct parts_text_char {
	Texture t;
	Texture t_edge;
	char ch[4];
	int advance;
	int cache_idx;
	Point off;
	Point pad;
	// Message-window timing; ordinary Parts text ignores these fields.
	double fade_start, fade_time;
};

struct parts_text_line {
	struct parts_text_char *chars;
	int nr_chars;
	unsigned height;
	float width;
	int line_space;
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
	PARTS_FLAT,
	PARTS_MOVIE,
	PARTS_RECT_DETECTION,
	PARTS_LAYOUT_BOX,
	PARTS_3DLAYER,
#define PARTS_NR_TYPES (PARTS_3DLAYER+1)
};

struct parts_common {
	Texture texture;
	int w, h;
	Point origin_offset;
	Rectangle hitbox;
	Rectangle surface_area;
	uint8_t *alpha_mask;
	int alpha_w, alpha_h;
};

struct parts_cg_asset;

struct parts_cg {
	struct parts_common common;
	int no;
	struct string *name;
	struct parts_cg_asset *cg_asset;
	int suspended_w;
	int suspended_h;
	uint64_t load_gen;
	uint64_t async_gen;
};

struct parts_text {
	struct parts_common common;
	unsigned nr_lines;
	struct parts_text_line *lines;
	int line_space;
	struct { float x; int y; } cursor;
	struct text_style ts;
	struct text_style ruby_ts;
	int ruby_char_space;
	int show_time;
	int text_pos;
	bool enable_tag;
	bool fixed;
	bool count_return;
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

enum parts_numeral_font_type {
	// each digit has a separate CG (only first CG number stored in `cg_no` member)
	PARTS_NUMERAL_FONT_SEPARATE = 0,
	// digits packed into single CG
	PARTS_NUMERAL_FONT_COMBINED = 1,
	// each digit has a separate CG (CG numbers stored in `width` member)
	PARTS_NUMERAL_FONT_SEPARATE2 = 2,
};

struct parts_numeral_font {
	enum parts_numeral_font_type type;
	int cg_no;
	int width[12];
	struct string *cg_name;
	Texture cg[12];
};

struct parts_numeral {
	struct parts_common common;
	bool have_num;
	int num;
	int space;
	int show_comma;
	int length;
	int font_no;
	int show_type;
	bool full_pitch;
	bool show_padding;
	struct text_style ts;
	struct string *cg_name;
};

struct parts_gauge {
	struct parts_common common;
	Texture cg;
	int cg_no;
	struct string *cg_name;
	bool reverse;
	float rate;
	float numerator;
	float denominator;
};

// Serialized in save data. Do not reorder.
enum parts_cp_op_type {
	PARTS_CP_CREATE,
	PARTS_CP_CREATE_PIXEL_ONLY,
	PARTS_CP_CG,
	PARTS_CP_FILL,
	PARTS_CP_FILL_ALPHA_COLOR,
	PARTS_CP_FILL_AMAP,
	PARTS_CP_DRAW_RECT,
	PARTS_CP_DRAW_CUT_CG,
	PARTS_CP_COPY_CUT_CG,
	PARTS_CP_DRAW_TEXT,
	PARTS_CP_COPY_TEXT,
	PARTS_CP_GRAY_FILTER,
	PARTS_CP_FILL_WITH_ALPHA,
	PARTS_CP_DRAW_LINE,
	PARTS_CP_ALPHA_BLEND_TEXT,
	PARTS_CP_ONLY_ALPHA_TEXT,
	PARTS_CP_FILL_GRADATION_HORIZON,
	PARTS_CP_ALPHA_BLEND_CUT_CG,
	PARTS_CP_ONLY_ALPHA_CUT_CG,
	PARTS_CP_MUL_AMAP_GRADATION_HORIZON,
	PARTS_CP_MUL_AMAP_GRADATION_VERTICAL,
	PARTS_CP_BLEND_CG,
	PARTS_CP_ADD_FILTER,
	PARTS_CP_MUL_FILTER,
	PARTS_CP_HBLUR_FILTER,
	PARTS_CP_VBLUR_FILTER,
	PARTS_CP_V14,
#define PARTS_NR_CP_TYPES (PARTS_CP_V14+1)
};

struct parts_cp_create {
	int w;
	int h;
};

struct parts_cp_cg {
	struct string *name;
	int x, y;
};

struct parts_cp_fill {
	int x, y, w, h;
	int r, g, b, a;
	int r2, g2, b2, a2;
	bool full_size;
};

struct parts_cp_cut_cg {
	struct string *cg_name;
	int dx, dy, dw, dh;
	int sx, sy, sw, sh;
	int interp_type;
	bool scale;
};

struct parts_cp_text {
	struct string *text;
	int x, y;
	int line_space;
	struct text_style style;
};

struct parts_cp_filter {
	int x, y, w, h;
	int r, g, b;
	int blur;
	bool full_size;
};

struct parts_cp_line {
	int x0, y0, x1, y1;
	int r, g, b, a;
};

struct parts_cp_v14 {
	int command;
	int interp_type;
	int sx, sy, sw, sh;
	int dx, dy, dx2, dy2, dw, dh;
	int r, g, b, a;
	int r2, g2, b2, a2;
	bool full_size;
	int blur;
	int radius_x, radius_y;
	int line_width;
	bool round_edge, round_corner;
	int angle, start_angle, sweep_angle;
	struct string *cg_name;
	int nr_coords;
	int *coords;
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
		struct parts_cp_filter filter;
		struct parts_cp_line line;
		struct parts_cp_v14 v14;
	};
};

struct parts_construction_process {
	struct parts_common common;
	TAILQ_HEAD(, parts_cp_op) ops;
	bool needs_build;
	bool needs_pixel_snapshot;
};

enum parts_flash_blend_mode {
	PARTS_FLASH_BLEND_NORMAL0    = 0,
	PARTS_FLASH_BLEND_NORMAL1    = 1,
	PARTS_FLASH_BLEND_LAYER      = 2,
	PARTS_FLASH_BLEND_MULTIPLY   = 3,
	PARTS_FLASH_BLEND_SCREEN     = 4,
	PARTS_FLASH_BLEND_LIGHTEN    = 5,
	PARTS_FLASH_BLEND_DARKEN     = 6,
	PARTS_FLASH_BLEND_DIFFERENCE = 7,
	PARTS_FLASH_BLEND_ADD        = 8,
	PARTS_FLASH_BLEND_SUBTRACT   = 9,
	PARTS_FLASH_BLEND_INVERT     = 10,
	PARTS_FLASH_BLEND_ALPHA      = 11,
	PARTS_FLASH_BLEND_ERASE      = 12,
	PARTS_FLASH_BLEND_OVERLAY    = 13,
	PARTS_FLASH_BLEND_HARDLIGHT  = 14,
};

enum parts_draw_filter {
	PARTS_DRAW_FILTER_NORMAL   = 0,
	PARTS_DRAW_FILTER_ADDITIVE = 1,
	PARTS_DRAW_FILTER_MULTIPLY = 2,
	PARTS_DRAW_FILTER_SCREEN   = 3,
};

struct parts_flash_object {
	TAILQ_ENTRY(parts_flash_object) entry;
	uint16_t depth;
	uint16_t character_id;
	mat4 matrix;
	struct swf_cxform_with_alpha color_transform;
	enum parts_flash_blend_mode blend_mode;
};

struct parts_flash {
	struct parts_common common;
	struct string *name;
	struct swf *swf;

	struct swf_tag *tag;
	bool has_ended;
	bool stopped;
	unsigned elapsed;
	int current_frame;
	struct hash_table *dictionary;
	struct hash_table *bitmaps;  // bitmap character id -> struct texture *
	struct hash_table *sprites;  // sprite character id -> struct parts_flash_object *
	TAILQ_HEAD(, parts_flash_object) display_list;
};

#define FLAT_MAX_ANCESTOR_DEPTH 32

struct flat_emitter_birth {
	const struct flat_timeline *timeline;
	int timeline_idx;
	int frame;
	unsigned instance;
	bool has_position;
	vec3 position;
	bool has_ancestor_keys;
	int ancestor_count;
	struct flat_key_data_graphic ancestor_keys[FLAT_MAX_ANCESTOR_DEPTH];
	struct flat_emitter_birth *next;
};

struct flat_layer_state {
	int current_frame;
	bool stopped;
	bool suppress_advance;
	bool has_looped;
	int jump_target;  // pending jump frame, -1 = none
	// Per-timeline: last matched script key index for change detection
	// (-2 = uninitialized, -1 = no match, >= 0 = key index)
	int *last_script_keys;
	bool *disabled_timelines;
	// Per-timeline: child layer state (for TIMELINE libs), NULL otherwise
	struct flat_layer_state **children;
	size_t nr_timelines;
	int last_rendered_frame;
	struct flat_emitter_birth *emitter_births;
};

// Per-frame CG list for a STOP_MOTION library. lib_indices[k] is
// the library index of the CG to display at frame k.
struct flat_stop_motion_frames {
	int *lib_indices;
	int count;
};

struct parts_flat_asset {
	char *name;
	size_t name_len;
	int ref;
	struct flat *flat;
	size_t nr_libraries;
	Texture *base_textures;
	Rectangle *base_rects;      // per-library UV sub-rect within atlas (or full texture)
	int *base_atlas_idx;        // per-library index into talt_atlases, or -1 if standalone
	Texture *talt_atlases;      // owned shared TALT atlas textures (deleted once on destroy)
	size_t nr_talt_atlases;
	struct flat_stop_motion_frames *stop_motion_frames;
	int *root_timeline_lib_idx; // pre-resolved library index for flat->timelines[i]
	int **lib_timeline_lib_idx; // per FLAT_LIB_TIMELINE library: pre-resolved timeline indices
	int *emitter_cg_lib_idx;    // per FLAT_LIB_EMITTER library: pre-resolved CG/stop-motion index
	int end_frame;
	int width;
	int height;
	struct parts_flat_asset *prev;
	struct parts_flat_asset *next;
};

struct parts_flat {
	struct parts_common common;
	struct string *name;
	struct parts_flat_asset *asset;
	struct flat *flat;
	bool stopped;
	bool needs_advance;
	double elapsed;
	float fps_rate;
	int end_frame;
	int begin_seek_frame;
	int pending_seek_delta;
	struct flat_layer_state *root_state;
	size_t nr_libraries;
	Texture *textures;  // indexed by library index (only CG libs have valid textures)
	bool *owns_texture; // true if textures[i] was overridden per-instance (PE_ChangeFlatCG)
	struct string **proxy_cg_names;
	// Indexed by library index. Only entries for STOP_MOTION libraries have lib_indices populated.
	struct flat_stop_motion_frames *stop_motion_frames;
};

struct movie_context;

struct parts_movie {
	struct parts_common common;
	int sprite_no;  // SACT sprite number used as movie render target
	struct movie_context *mc;
};

enum parts_layout_type {
	PARTS_LAYOUT_FREE       = 0,  // no automatic layout
	PARTS_LAYOUT_VERTICAL   = 1,
	PARTS_LAYOUT_HORIZONTAL = 2,
};

// In AliceSoft's PartsEngine implementation, LayoutBox is a component type
// that is NOT per-state. We store it per-state for uniformity with other
// component types, but parts_get_layout_box() and all code in layoutbox.c
// operate on states[0] only. This is safe as long as game scripts never
// convert a LayoutBox parts to/from another component type.
struct parts_layout_box {
	struct parts_common common;
	enum parts_layout_type layout_type;
	bool wrap;
	bool wrap_size_is_rate;
	float wrap_size;
	int align;
	int padding_top;
	int padding_bottom;
	int padding_left;
	int padding_right;
};

struct parts_3dlayer {
	struct parts_common common;
	int plugin;    // ReignEngine plugin handle (-1 = none)
	int sprite_no; // SACT sprite used as render target
};

struct parts_rect {
	struct parts_common common;
	bool custom;
	bool is_cg_detection;
	Point points[4];
	struct string *cg_name;
};

struct parts_state {
	enum parts_type type;
	struct parts_texture_backup *texture_backups;
	union {
		struct parts_common common;
		struct parts_cg cg;
		struct parts_text text;
		struct parts_animation anim;
		struct parts_numeral num;
		struct parts_gauge gauge;
		struct parts_construction_process cproc;
		struct parts_flash flash;
		struct parts_flat flat;
		struct parts_movie movie;
		struct parts_layout_box layout_box;
		struct parts_3dlayer layer3d;
		struct parts_rect rect;
	};
};

TAILQ_HEAD(parts_list_head, parts);

struct parts_params {
	int z;
	SDL_FPoint pos;
	bool show;
	uint8_t alpha;
	struct { float x, y; } scale;
	struct { float x, y, z; } rotation;
	SDL_Color add_color;
	SDL_Color multiply_color;
};

struct parts_user_value {
	struct string *key, *value;
	struct parts_user_value *next;
};

struct parts_widget {
	bool enabled;
	bool checked, button_mode;
	bool defer_rebuild, dirty_bg, dirty_label;
	int width, height, color[3], text_origin;
	Rectangle label_area;
	struct string *cg_name, *flat_name, *text;
	struct string *override_cg[2], *override_flat[2];
	struct parts_state disabled, label;
	struct parts_state checked_states[PARTS_NR_STATES], checked_disabled;
};

struct parts_panel {
	int width, height;
	int color[4];
	int gradient[4]; // top, bottom, left, right, in pixels
};

struct parts_scrollbar {
	bool horizontal;
	bool is_slider;
	int width, height, before, after, total, view, pos, step;
	int total_link, view_link, hovered, pressed, repeat_ms;
	float rate, drag_offset;
	int reported_pos, reported_range;
	float reported_rate;
	struct string *names[2][5]; // CG/FLAT: base, first arrow, last arrow, thumb, background
	struct parts_state skins[4][4];
	bool loaded[4][4];
};

struct parts_message_window {
	bool active, fixed, key_show;
	int inactive_color[3], text_origin, text_speed, flat_wait_frame;
	Rectangle text_area;
	struct string *cg_name, *flat_name, *text, *function;
	struct string *key_cg_name, *key_flat_name;
	int message, version, step;
	int key_x, key_y, key_z, key_start, key_count, key_time;
	int elapsed;
	int ruby_line_space;
	double finish_time, speed_rate;
	struct parts_state label, key_wait;
};

struct parts {
	struct sprite sp;
	enum parts_state_type state;
	struct parts_state states[PARTS_NR_STATES];
	TAILQ_ENTRY(parts) parts_list_entry;
	TAILQ_ENTRY(parts) child_list_entry;
	TAILQ_ENTRY(parts) dirty_list_entry;
	struct parts_list_head children;
	struct parts_params local;
	struct parts_params global;
	struct parts *parent;
	int dirty;
	int no;
	struct string *comment;
	int component_type;
	struct string *user_component_name;
	struct parts_user_value *user_values;
	struct parts_widget *widget;
	struct parts_panel *panel;
	struct parts_scrollbar *scrollbar;
	int scroll_links[3];
	struct parts_message_window *message_box;
	bool textures_suspended;
	int delegate_index;
	int event_unique_id;
	bool sub_color_mode;
	int multi_textbox_mode;
	int sprite_deform;
	bool clickable;
	bool wheelable;
	bool enable_input;
	bool pass_cursor;
	bool lock_input_state;
	bool want_save;
	bool draggable;
	bool pixel_decide;
	bool lock_update;
	int swipe_type;
	int on_cursor_sound;
	int on_click_sound;
	struct string *sound_names[PARTS_NR_STATES];
	int origin_mode;
	bool position_is_ratio;
	int pending_parent;
	int linked_to;
	int linked_from;
	int checkbox_show_link;
	int *radio_children;
	int nr_radio_children;
	struct string *text_box_text;
	struct parts_state *text_box_label;
	int text_box_max_length;
	bool text_box_read_only;
	bool is_hovered;
	bool focused;
	int active_part; // cached focused descendant, used on controller roots
	int hover_time;
	int draw_filter;
	bool message_window;
	bool message_window_effect_link;
	bool enable_speed_up;
	int alpha_clipper_parts_no;
	bool enable_clip_area;
	Rectangle clip_area;
	bool reverse_tb;
	bool reverse_lr;
	int texture_filter_type;
	int texture_address_type;
	bool mipmap;
	bool update_look_at_pos;
	bool anchor_top;
	bool anchor_bottom;
	bool anchor_left;
	bool anchor_right;
	int margin_top;
	int margin_bottom;
	int margin_left;
	int margin_right;
	struct parts_motion_list motion;
	int controller_no;
	mat4 world_matrix;
	mat4 committed_matrix;
	uint64_t world_matrix_gen;
	struct parts *world_matrix_self;
};

#define PARTS_LIST_FOREACH(iter) TAILQ_FOREACH(iter, &parts_list, parts_list_entry)
#define PARTS_LIST_FOREACH_REVERSE(iter) TAILQ_FOREACH_REVERSE(iter, &parts_list, parts_list_head, parts_list_entry)
#define PARTS_FOREACH_CHILD(iter, parent) TAILQ_FOREACH(iter, &parent->children, child_list_entry)

// Controllers are identified by their position in the stack (0 = bottom). The
// system overlay controller lives outside the stack.
#define PARTS_CONTROLLER_STACK_MAX 10000
#define PARTS_CONTROLLER_SYSTEM_OVERLAY PARTS_CONTROLLER_STACK_MAX

struct parts_controller_stack {
	int nr_controllers;
	int active;  // stack index or PARTS_CONTROLLER_SYSTEM_OVERLAY
	int ids[PARTS_CONTROLLER_STACK_MAX];
	int overlay_id;
};
extern bool parts_multi_controller;

// A loaded BackScene owns a separate graph, ID namespace and numeral textures.
// Select a context only while loading, releasing or rendering that graph. Queue
// heads stay at fixed addresses, so their intrusive links never need rebasing.
struct parts_context {
	struct parts_list_head list, dirty;
	struct hash_table *table;
	Point origin;
	struct parts_controller_stack controllers;
	struct parts_numeral_font *fonts;
	int font_count;
	bool background;
	struct sprite sprite;
	int next_free_no;
};
extern struct parts_context *parts_current;
extern uint64_t parts_render_gen;

static inline int parts_get_effective_alpha_clipper(struct parts *parts)
{
	for (struct parts *p = parts; p; p = p->parent) {
		if (p->alpha_clipper_parts_no > 0)
			return p->alpha_clipper_parts_no;
	}
	return 0;
}
#define parts_list (parts_current->list)
#define ctrl_stack (parts_current->controllers)
#define parts_numeral_fonts (parts_current->fonts)
#define parts_nr_numeral_fonts (parts_current->font_count)

struct parts_context *parts_context_new_background(void);
struct parts_context *parts_context_select(struct parts_context *context);
void parts_context_free(struct parts_context *context);

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
struct parts_flat *parts_get_flat(struct parts *parts, int state);
struct parts_movie *parts_get_movie(struct parts *parts, int state);
struct parts_layout_box *parts_get_layout_box(struct parts *parts);
struct parts_3dlayer *parts_get_3dlayer(struct parts *parts, int state);
void parts_set_pos(struct parts *parts, Point pos);
void parts_set_posf(struct parts *parts, SDL_FPoint pos);
SDL_FPoint parts_local_position(struct parts *parts);
void parts_set_global_pos(Point pos);
void parts_set_dims(struct parts *parts, struct parts_common *common, int w, int h);
void parts_set_scale_x(struct parts *parts, float mag);
void parts_set_scale_y(struct parts *parts, float mag);
void parts_set_rotation_x(struct parts *parts, float rot);
void parts_set_rotation_y(struct parts *parts, float rot);
void parts_set_rotation_z(struct parts *parts, float rot);
void parts_set_alpha(struct parts *parts, int alpha);
void parts_set_state(struct parts *parts, enum parts_state_type state);
void parts_release(int parts_no);
void parts_release_all(void);
void parts_set_surface_area(struct parts *parts, struct parts_common *common, int x, int y, int w, int h);
bool parts_is_visible(struct parts *parts);
void parts_common_free_alpha_mask(struct parts_common *common);
void parts_common_set_alpha_from_cg(struct parts_common *common, const struct cg *cg);
bool parts_common_check_alpha(struct parts_state *state, int px, int py);
extern bool parts_message_window_show;
extern float parts_message_window_mul_color_rate;
extern float parts_message_window_alpha_rate;

// for save.c
void parts_list_resort(struct parts *parts);
void parts_component_dirty(struct parts *parts);
void parts_recalculate_hitbox(struct parts *parts);
void parts_world_matrix(struct parts *parts, mat4 out);
void parts_widget_create(struct parts *parts);
void parts_checkbox_create(struct parts *parts);
void parts_widget_create_deferred(struct parts *parts, int type);
void parts_widget_end_batch(struct parts *parts);
const char *parts_utf8_to_cjk_literal(const char *literal);
void parts_widget_free(struct parts *parts);
void parts_widget_rebuild(struct parts *parts);
void parts_widget_restore_plain_skin(struct parts *parts, struct parts_state *state,
		int index, bool checked);
void parts_widget_get_size(struct parts *parts, int *width, int *height);
void parts_widget_check(struct parts *parts, bool checked);
void parts_panel_create(struct parts *parts);
void parts_panel_free(struct parts *parts);
void parts_panel_rebuild(struct parts *parts);
void parts_scrollbar_create(struct parts *parts, bool horizontal);
void parts_sliderbar_create(struct parts *parts, bool horizontal);
void parts_scrollbar_free(struct parts *parts);
void parts_scrollbar_rebuild(struct parts *parts);
int parts_scrollbar_range(struct parts_scrollbar *s);
int parts_scrollbar_skin_state(struct parts_scrollbar *s, int piece);
void parts_scrollbar_rects(struct parts *parts, Rectangle rects[4]);
bool parts_scrollbar_update(struct parts *parts, int elapsed);
void parts_scrollbar_update_links(void);
void parts_scrollbar_notify_changes(void);
void parts_scrollbar_input(struct parts *parts, Point cursor, bool hovered, bool down, bool pressed, int elapsed, int wheel);
void parts_message_window_create(struct parts *parts);
void parts_message_window_free(struct parts *parts);
void parts_message_window_rebuild_text(struct parts *parts);
bool parts_message_window_update(struct parts *parts, int passed_time);
bool parts_state_update(struct parts_state *state, int passed_time);
struct parts_state *parts_display_state(struct parts *parts);
struct parts_texture_pixels {
	uint8_t *rgba;
	int w, h;
	GLint min_filter, mag_filter, wrap_s, wrap_t;
	bool opaque;
};

void parts_state_reset(struct parts_state *state, enum parts_type type);
void parts_state_resume_textures(struct parts *parts, struct parts_state *state);
void parts_state_discard_texture_backups(struct parts_state *state);
int parts_copy_texture_pixels(struct parts_state *state, Texture *texture, struct parts_texture_pixels *out);
bool parts_cg_set(struct parts *parts, struct parts_cg *cg, struct string *cg_name);
bool parts_cg_set_by_index(struct parts *parts, struct parts_cg *cg, int cg_no);
void parts_cg_suspend(struct parts_cg *cg);
void parts_cg_resume(struct parts *parts, struct parts_cg *cg);
void parts_cg_async_flush_part(int parts_no);
void parts_cg_async_flush_all(void);
bool PE_SetPartsCGThread(int parts_no, struct string *cg_name, int state);
bool PE_IsThreadLoading(int parts_no, int state);
bool PE_IsExistWorkerThread(void);
void parts_set_mixed_display_text(bool enable);
bool parts_uses_mixed_display_text(void);
char *parts_prepare_display_text(const char *text);
void parts_text_append(struct parts *parts, struct parts_text *t, struct string *text);
bool parts_animation_set_cg_by_index(struct parts *parts, struct parts_animation *anim,
		int cg_no, int nr_frames, int frame_time);
bool parts_animation_set_cg(struct parts *parts, struct parts_animation *anim,
		struct string *cg_name, int start_no, int nr_frames, int frame_time);
void parts_numeral_font_init(struct parts_numeral_font *font);
int parts_load_numeral_font_separate_string(struct string *cg_name);
int parts_load_numeral_font_combined_by_name(struct string *cg_name, int w[12]);
bool parts_numeral_set_number(struct parts *parts, struct parts_numeral *num, int n);
bool parts_gauge_set_cg(struct parts *parts, struct parts_gauge *g, struct string *cg_name);
bool parts_gauge_set_cg_by_index(struct parts *parts, struct parts_gauge *g, int cg_no);
void parts_hgauge_set_rate(struct parts *parts, struct parts_gauge *g, float rate);
void parts_vgauge_set_rate(struct parts *parts, struct parts_gauge *g, float rate);

// text.c
void parts_text_free(struct parts_text *t);
void parts_text_end_session(void);
const char *parts_text_append_char(struct parts_text *t, const char *str);
void parts_text_update_dims(struct parts *parts, struct parts_text *t);
struct string *parts_text_line_get(struct parts_text_line *line);
struct string *parts_text_get(struct parts_text *t);
bool PE_DeletePartsTopTextLine(int parts_no, int state);

// render.c
void parts_render_init(void);
void parts_render_fini(void);
void parts_render_update(void);
bool parts_is_dirty(void);
void parts_engine_dirty(void);
void parts_engine_clean(void);
void parts_dirty(struct parts *parts);
void parts_sprite_render(struct sprite *sp);
void parts_background_render(struct sprite *sprite);
void parts_render_pass_begin(void);
void parts_render_pass_end(void);
bool parts_render_pass_suspend(void);
void parts_render_pass_resume(bool was_active);
bool parts_sprite_is_fullscreen_opaque(struct sprite *sp);
bool parts_cg_on_screen(struct parts *parts, struct parts_common *common);
void parts_render(struct parts *parts);
void parts_render_family(struct parts *parts);

// motion.c
void parts_clear_motion(struct parts *parts);
void parts_motion_end_session(void);
void parts_activity_end_session(void);
void parts_scrollbar_end_session(void);
void parts_add_motion(struct parts *parts, struct parts_motion *motion);

// input.c
extern bool parts_began_click;
extern bool parts_frame_did_heavy_load;
bool parts_input_is_busy(void);
void parts_text_box_set_display(struct parts *parts, const char *composition);
void parts_input_reset_part(struct parts *parts);
void parts_input_reset(void);
struct iarray_writer;
struct iarray_reader;
void parts_input_save_state(struct iarray_writer *w);
void parts_input_load_state(struct iarray_reader *r, int version);
void parts_input_load_legacy_state(void);

// message.c
enum parts_message_type {
	PARTS_MSG_MOUSE_ENTER    = 1,
	PARTS_MSG_MOUSE_MOVE     = 2,
	PARTS_MSG_MOUSE_LEAVE    = 3,
	PARTS_MSG_MOUSE_WHEEL    = 4,
	PARTS_MSG_MOUSE_CLICK    = 5,
	PARTS_MSG_MOUSE_ON       = 6,
	PARTS_MSG_DRAG_BEGIN     = 7,
	PARTS_MSG_DRAGGING       = 8,
	PARTS_MSG_DRAG_END       = 9,
	PARTS_MSG_DROP_ENTER     = 10,
	PARTS_MSG_DROP_ON        = 11,
	PARTS_MSG_DROPPED        = 12,
	PARTS_MSG_DROP_LEAVE     = 13,
	PARTS_MSG_KEY_TRIGGER    = 14,
	PARTS_MSG_KEY_DOWN       = 15,
	PARTS_MSG_KEY_UP         = 17,
	PARTS_MSG_FOCUS_IN       = 18,
	PARTS_MSG_FOCUS_OUT      = 19,
	PARTS_MSG_SCROLL         = 20,
	PARTS_MSG_TEXT_CHANGED   = 21,
	PARTS_MSG_CHECK_CHANGED  = 22,
	PARTS_MSG_CREATED        = 23,
	PARTS_MSG_RADIO_BUTTON_BOX = 24,
	PARTS_MSG_TEXT_FIXED     = 25,
	PARTS_MSG_SWIPE_BEGIN    = 27,
	PARTS_MSG_SWIPING        = 28,
	PARTS_MSG_SWIPE_END      = 29,
};

void parts_msg_push(struct parts* parts, int type, const char *fmt, ...);

// construction.c
void parts_cp_op_free(struct parts_cp_op *op);
void parts_add_cp_op(struct parts_construction_process *cproc, struct parts_cp_op *op);
void parts_cp_set_last_fill_full_size(int parts_no, int state, bool full_size);
bool parts_cp_add_v14(int parts_no, const struct parts_cp_v14 *src, int state);
bool parts_build_construction_process(struct parts *parts,
		struct parts_construction_process *cproc);
bool parts_clear_construction_process(struct parts_construction_process *cproc);
void parts_cproc_cache_clear(void);

// flash.c
void parts_flash_free(struct parts_flash *f);
bool parts_flash_load(struct parts *parts, struct parts_flash *f, struct string *filename);
bool parts_flash_update(struct parts_flash *f, int passed_time);
bool parts_flash_seek(struct parts_flash *f, int frame);

// flat.c
void parts_flat_free(struct parts_flat *f);
void parts_flat_cache_clear(void);
bool parts_flat_load(struct parts *parts, struct parts_flat *f, struct string *filename);
void parts_flat_recover_widget_skin(struct parts_flat *f, bool suspended);
bool parts_flat_update(struct parts_flat *f, int passed_time);
struct flat_layer_state *parts_flat_layer_state_new(size_t nr_timelines);
void parts_flat_layer_state_free(struct flat_layer_state *s);
void parts_flat_clear_emitter_births(struct flat_layer_state *state);
struct flat_emitter_birth *parts_flat_get_emitter_birth(
		struct flat_layer_state *state,
		const struct flat_timeline *timeline, int timeline_idx,
		int frame, unsigned instance, bool create);
bool parts_flat_hittest(struct parts_flat *f, float x, float y);
struct flat_timeline;
unsigned parts_flat_key_count(const struct flat_timeline *tl, int frame);
const struct flat_key_data_graphic *parts_flat_key(const struct flat_timeline *tl, int frame, unsigned instance);
int parts_flat_find_library(struct flat *fl, const char *name);
int parts_flat_stop_motion_get_cg_lib(struct parts_flat *f, int sm_lib_idx, int local);

struct flat_emitter;
struct flat_key_data_graphic;

struct flat_emitter_particle {
	vec2 pos;          // emitter-space position (pixels)
	vec2 scale;
	vec3 rot;          // degrees (x, y, z)
	float fade_alpha;  // 0-1.0
	int cg_lib_idx;    // CG library index of the texture for this particle
};

// Per-key emitter properties after applying the emitter's inherit_* flags.
struct flat_emitter_layer_effective {
	vec2 pos;
	bool reverse_lr, reverse_tb;
	float alpha;
	vec3 add_color;
	vec3 mul_color;
	int draw_filter;
	bool use_scale;
	bool use_rotation;
	bool use_origin;
};

struct flat_key_stack {
	const struct flat_key_data_graphic *keys[FLAT_MAX_ANCESTOR_DEPTH];
	const struct flat_timeline *timelines[FLAT_MAX_ANCESTOR_DEPTH];
	int locals[FLAT_MAX_ANCESTOR_DEPTH];
	unsigned instances[FLAT_MAX_ANCESTOR_DEPTH];
	int count;
	struct parts_flat_asset *asset;
	const int *timeline_lib_idx;
};

typedef void (*flat_emitter_particle_fn)(const struct flat_emitter_particle *p,
		void *ud);
bool parts_flat_emitter_get_align_offset(struct parts_flat *f, int emitter_lib_idx, vec2 out);
void parts_flat_foreach_emitter_particle(struct parts_flat *f, int emitter_lib_idx,
		const struct flat_timeline *timeline, unsigned instance,
		int birth_frame, int age, int frame_count,
		flat_emitter_particle_fn fn, void *ud);
void parts_flat_build_layer_matrix(const struct flat_key_data_graphic *key,
		vec2 pos,
		bool use_rotation, bool use_scale, bool use_origin,
		bool reverse_lr, bool reverse_tb,
		mat4 out);
void parts_flat_build_emitter_base_matrix(const struct flat_emitter *em,
		const struct flat_key_stack *stack, mat4 out);
void parts_flat_emitter_resolve_layer(
		const struct flat_emitter *em,
		const struct flat_key_data_graphic *key,
		float parts_alpha, float layer_alpha,
		struct flat_emitter_layer_effective *out);

// layoutbox.c
void parts_do_layout(struct parts *parts);
void parts_get_layout_size(struct parts *parts, int *w, int *h);
void parts_update_layout_tree(struct parts *parts);

// modifier UI scale internal helpers
struct text_style parts_get_effective_text_style(const struct text_style *src);
struct text_style parts_get_effective_text_style_pct(const struct text_style *src, int pct);
void parts_text_rerender_all(struct parts *parts, struct parts_text *t);
void parts_widget_rebuild_label(struct parts *parts);
void parts_scrollbar_hitbox(struct parts *parts, int state, Rectangle *out_hitbox);
struct parts_context *parts_get_back_scene(void);

// debug.c
struct sprite;
void parts_debug_init(void);
cJSON *parts_engine_to_json(struct sprite *sp, bool verbose);
cJSON *parts_sprite_to_json(struct sprite *sp, bool verbose);

static inline bool parts_state_valid(int state)
{
	return state >= 0 && state <= 2;
}

#endif /* SYSTEM4_PARTS_INTERNAL_H */
