#!/usr/bin/env python3
"""Dependency-free native regression tests for Rance 03 map movement bug.

Compiles the real production function bodies from xsystem4/src/parts/{render,parts,input}.c
against minimal fixtures (no SDL, GL context, or game assets required).
Verifies:
  1. sprite_deform (0/1/2) combined with surface_area crops (including exact Rance 03
     PlayerMapIcon 388x121 / 4x97x121 origin_mode=8 frames, vertical crops, full CG,
     partially out-of-bounds crops, and parts_v2 reverse_lr composition) renders at
     the exact hitbox position with exact mirrored texel-center UVs.
  2. parts_cg_on_screen matches the cropped screen AABB for sprite_deform 0/1/2
     (no false off-screen culling near viewport edges).
  3. parts_set_surface_area marks the part dirty only when surface_area changes.
  4. parts_hittest flips pixel_decide alpha coordinates for PARTS_CG with
     sprite_deform 1/2 in both legacy and v2 modes without altering non-CG states.

Run: python3 xsystem4/test/native/test_rance03_map_regressions.py
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def extract_function(rel_path, name):
    source = (ROOT / rel_path).read_text()
    idx = source.index(name + "(")
    start = source.rfind("\n", 0, idx) + 1
    # Include preceding signature line if return type is on its own line.
    if not source[start:idx].strip():
        start = source.rfind("\n", 0, start - 1) + 1
    brace = source.index("{", idx)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS_PREFIX = r"""
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef float mat4[4][4];
typedef float vec3[3];
typedef float vec4[4];
typedef struct { int x, y; } Point;
typedef struct { float x, y; } SDL_FPoint;
typedef struct { int x, y, w, h; } Rectangle;
typedef Rectangle SDL_Rect;
typedef struct { uint8_t r, g, b, a; } SDL_Color;
typedef unsigned int GLuint;

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

enum parts_type {
	PARTS_UNINITIALIZED = 0,
	PARTS_CG = 1,
	PARTS_RECT_DETECTION = 2,
	PARTS_FLAT = 3,
	PARTS_LAYOUT_BOX = 4,
};
#define PARTS_LAYOUT_FREE 2
#define PARTS_STATE_DEFAULT 0
#define PARTS_NR_STATES 3

struct texture {
	GLuint handle;
	int w, h;
	bool opaque;
};

struct parts_common {
	struct texture texture;
	int w, h;
	Point origin_offset;
	Rectangle hitbox;
	Rectangle surface_area;
};

struct parts_rect {
	struct parts_common common;
	bool custom;
	bool is_cg_detection;
	Point points[4];
};

struct parts_layout_box {
	struct parts_common common;
	int layout_type;
};

struct parts_flat {
	struct parts_common common;
};

struct parts_state {
	enum parts_type type;
	union {
		struct parts_common common;
		struct parts_rect rect;
		struct parts_layout_box layout_box;
		struct parts_flat flat;
	};
};

struct parts_params {
	SDL_FPoint pos;
	struct { float x, y; } scale;
	struct { float x, y, z; } rotation;
	uint8_t alpha;
	SDL_Color add_color;
	SDL_Color multiply_color;
};

struct parts_scrollbar { float pos; };
struct parts_widget {
	bool enabled;
	bool checked;
	bool button_mode;
	struct { size_t size; } *text;
	struct parts_state label;
	struct parts_state disabled;
	struct parts_state checked_disabled;
	struct parts_state checked_states[PARTS_NR_STATES];
};

struct parts {
	struct parts *parent;
	int state;
	struct parts_state states[PARTS_NR_STATES];
	struct parts_params local;
	struct parts_params global;
	int origin_mode;
	bool position_is_ratio;
	int sprite_deform;
	int draw_filter;
	int component_type;
	int scroll_links[2];
	struct parts_scrollbar *scrollbar;
	struct parts_widget *widget;
	bool reverse_lr;
	bool reverse_tb;
	bool pixel_decide;
};

static struct { int view_width, view_height; } config = { 1024, 768 };
static bool parts_v2 = false;
static uint64_t parts_render_gen = 1;
static int dirty_calls = 0;
static int warning_calls = 0;

#define WARNING(...) do { warning_calls++; } while (0)

static bool SDL_PointInRect(const Point *p, const Rectangle *r) {
	return p->x >= r->x && p->x < r->x + r->w && p->y >= r->y && p->y < r->y + r->h;
}

static bool SDL_IntersectRect(const Rectangle *a, const Rectangle *b, Rectangle *res) {
	int x0 = max(a->x, b->x);
	int y0 = max(a->y, b->y);
	int x1 = min(a->x + a->w, b->x + b->w);
	int y1 = min(a->y + a->h, b->y + b->h);
	if (x1 <= x0 || y1 <= y0) {
		*res = (Rectangle){ 0, 0, 0, 0 };
		return false;
	}
	*res = (Rectangle){ x0, y0, x1 - x0, y1 - y0 };
	return true;
}

static void glm_mat4_identity(mat4 m) {
	memset(m, 0, sizeof(mat4));
	for (int i = 0; i < 4; i++) m[i][i] = 1.0f;
}
static void glm_mat4_copy(mat4 src, mat4 dst) { memcpy(dst, src, sizeof(mat4)); }
static void mat4_mul(mat4 a, mat4 b, mat4 out) {
	mat4 m = {0};
	for (int c = 0; c < 4; c++)
		for (int r = 0; r < 4; r++)
			for (int k = 0; k < 4; k++)
				m[c][r] += a[k][r] * b[c][k];
	memcpy(out, m, sizeof(m));
}
static void glm_translate(mat4 m, vec3 p) {
	mat4 t; glm_mat4_identity(t);
	t[3][0] = p[0]; t[3][1] = p[1]; t[3][2] = p[2];
	mat4_mul(m, t, m);
}
static void glm_scale(mat4 m, vec3 p) {
	for (int c = 0; c < 3; c++)
		for (int r = 0; r < 4; r++)
			m[c][r] *= p[c];
}
static float glm_rad(float deg) { return deg * 0.017453292519943295f; }
static void mat4_rot(mat4 m, float a, mat4 out, int x, int y) {
	mat4 t; glm_mat4_identity(t);
	t[x][x] = t[y][y] = cosf(a);
	t[x][y] = sinf(a);
	t[y][x] = -sinf(a);
	mat4_mul(m, t, out);
}
static void glm_rotate_z(mat4 m, float a, mat4 out) { mat4_rot(m, a, out, 0, 1); }
static void glm_rotate_x(mat4 m, float a, mat4 out) { mat4_rot(m, a, out, 1, 2); }
static void glm_rotate_y(mat4 m, float a, mat4 out) { mat4_rot(m, a, out, 2, 0); }

static struct parts *parts_try_get(int no) { (void)no; return NULL; }
static void parts_dirty(struct parts *p) { (void)p; dirty_calls++; }
static void parts_widget_get_size(struct parts *p, int *w, int *h) { (void)p; *w = *h = 0; }
static void parts_scrollbar_hitbox(struct parts *p, int s, Rectangle *r) { (void)p; (void)s; (void)r; }
static bool parts_flat_hittest(struct parts_flat *f, float x, float y) { (void)f; (void)x; (void)y; return false; }
static int parts_get_effective_alpha_clipper(struct parts *p) { (void)p; return 0; }
static void parts_get_effective_add_color(struct parts *p, vec3 out) { (void)p; out[0] = out[1] = out[2] = 0.0f; }
static bool get_alpha_clipper_info(int c, GLuint *t, vec4 **inv, float *x0, float *x1, float *y0, float *y1) {
	(void)c; (void)t; (void)inv; (void)x0; (void)x1; (void)y0; (void)y1;
	return false;
}

static int last_alpha_px = INT_MIN;
static int last_alpha_py = INT_MIN;
static int alpha_check_calls = 0;
static bool parts_common_check_alpha(struct parts_state *state, int px, int py) {
	alpha_check_calls++;
	last_alpha_px = px;
	last_alpha_py = py;
	return px >= 0 && py >= 0 && px < state->common.w && py < state->common.h;
}

static void parts_apply_local_matrix(struct parts *parts, mat4 out);
static void parts_world_matrix(struct parts *parts, mat4 out) {
	glm_mat4_identity(out);
	if (parts_v2) {
		if (parts->parent)
			parts_world_matrix(parts->parent, out);
		parts_apply_local_matrix(parts, out);
	} else {
		glm_translate(out, (vec3){ parts->global.pos.x, parts->global.pos.y, 0.0f });
		glm_scale(out, (vec3){ parts->global.scale.x, parts->global.scale.y, 1.0f });
	}
}

static bool last_draw_valid = false;
static mat4 last_draw_mw;
static vec4 last_draw_uv;
static float last_draw_aabb[4];

static void quad_screen_aabb(mat4 m, float *min_x, float *max_x, float *min_y, float *max_y);
static bool crop_quad_transform(mat4 mw_transform, int tex_w, int tex_h, const Rectangle *rect,
		mat4 out_mw, vec4 out_uv);
static void apply_sprite_deform_uv(int sprite_deform, vec4 uv_rect);

static void parts_render_texture_deform(struct texture *texture, mat4 mw_transform, Rectangle *rect,
		int sprite_deform, float blend_rate, vec3 add_color, vec3 multiply_color,
		int draw_filter, int alpha_clipper) {
	(void)add_color; (void)multiply_color; (void)draw_filter; (void)alpha_clipper;
	last_draw_valid = false;
	if (!texture || !texture->handle || blend_rate <= 0.0f)
		return;
	if (!crop_quad_transform(mw_transform, texture->w, texture->h, rect, last_draw_mw, last_draw_uv))
		return;
	apply_sprite_deform_uv(sprite_deform, last_draw_uv);
	quad_screen_aabb(last_draw_mw, &last_draw_aabb[0], &last_draw_aabb[1],
			&last_draw_aabb[2], &last_draw_aabb[3]);
	last_draw_valid = true;
}
"""

TESTS_MAIN = r"""
static bool approx(float a, float b) { return fabsf(a - b) < 1e-4f; }

static struct parts make_rance_map_icon(int pos_x, int pos_y) {
	struct parts p = {0};
	p.local.pos = p.global.pos = (SDL_FPoint){ (float)pos_x, (float)pos_y };
	p.local.scale.x = p.local.scale.y = 1.0f;
	p.global.scale.x = p.global.scale.y = 1.0f;
	p.global.alpha = 255;
	p.global.multiply_color = (SDL_Color){ 255, 255, 255, 255 };
	p.origin_mode = 8; // bottom-center (PlayerMapIcon::init)
	p.states[0].type = PARTS_CG;
	p.states[0].common.texture = (struct texture){ .handle = 1, .w = 388, .h = 121, .opaque = false };
	p.states[0].common.w = 388;
	p.states[0].common.h = 121;
	parts_common_recalculate_hitbox(&p, &p.states[0].common);
	return p;
}

static void test_rance03_player_map_icon_deform_and_surface_area(void) {
	const int TW = 388, TH = 121, FW = 97;
	const int POS_X = 512, POS_Y = 400;
	parts_v2 = false;
	struct parts p = make_rance_map_icon(POS_X, POS_Y);
	struct parts_common *c = &p.states[0].common;

	for (int deform = 0; deform <= 2; deform++) {
		p.sprite_deform = deform;
		for (int t = 0; t < 4; t++) {
			int sa_x = (deform == 2) ? 0 : t * FW;
			int sa_y = (deform == 2) ? t * 30 : 0;
			int sa_w = FW;
			int sa_h = (deform == 2) ? 30 : TH;
			parts_set_surface_area(&p, c, sa_x, sa_y, sa_w, sa_h);
			parts_render_cg(&p, c);
			assert(last_draw_valid);

			// Expected local placement is strictly anchored to bottom-center of the
			// cropped frame: [POS_X - sa_w/2, POS_X - sa_w/2 + sa_w] x [POS_Y - sa_h, POS_Y].
			float exp_x0 = (float)(POS_X - sa_w / 2);
			float exp_x1 = exp_x0 + (float)sa_w;
			float exp_y0 = (float)(POS_Y - sa_h);
			float exp_y1 = (float)POS_Y;
			assert(approx(last_draw_aabb[0], exp_x0));
			assert(approx(last_draw_aabb[1], exp_x1));
			assert(approx(last_draw_aabb[2], exp_y0));
			assert(approx(last_draw_aabb[3], exp_y1));
			assert(parts_cg_on_screen(&p, c));

			// Verify exact texel-center sampling at both ends of the cropped frame:
			// u(i) = uv[0] + ((i + 0.5) / sa_w) * uv[2] must hit the exact mirrored texel.
			float u_first = last_draw_uv[0] + (0.5f / sa_w) * last_draw_uv[2];
			float u_last  = last_draw_uv[0] + ((sa_w - 0.5f) / sa_w) * last_draw_uv[2];
			float v_first = last_draw_uv[1] + (0.5f / sa_h) * last_draw_uv[3];
			float v_last  = last_draw_uv[1] + ((sa_h - 0.5f) / sa_h) * last_draw_uv[3];

			float exp_u_first = (deform == 1) ? ((TW - 1 - sa_x) + 0.5f) / TW : (sa_x + 0.5f) / TW;
			float exp_u_last  = (deform == 1) ? ((TW - sa_x - sa_w) + 0.5f) / TW : (sa_x + sa_w - 0.5f) / TW;
			float exp_v_first = (deform == 2) ? ((TH - 1 - sa_y) + 0.5f) / TH : (sa_y + 0.5f) / TH;
			float exp_v_last  = (deform == 2) ? ((TH - sa_y - sa_h) + 0.5f) / TH : (sa_y + sa_h - 0.5f) / TH;

			assert(approx(u_first, exp_u_first));
			assert(approx(u_last, exp_u_last));
			assert(approx(v_first, exp_v_first));
			assert(approx(v_last, exp_v_last));
		}
	}

	// Full uncropped CG (surface_area = {0,0,0,0}) for deform 0/1/2
	parts_set_surface_area(&p, c, 0, 0, 0, 0);
	for (int deform = 0; deform <= 2; deform++) {
		p.sprite_deform = deform;
		parts_render_cg(&p, c);
		assert(last_draw_valid);
		assert(approx(last_draw_aabb[0], (float)(POS_X - TW / 2)));
		assert(approx(last_draw_aabb[1], (float)(POS_X - TW / 2 + TW)));
		assert(approx(last_draw_aabb[2], (float)(POS_Y - TH)));
		assert(approx(last_draw_aabb[3], (float)POS_Y));
		if (deform == 0) {
			assert(approx(last_draw_uv[0], 0.0f) && approx(last_draw_uv[2], 1.0f));
			assert(approx(last_draw_uv[1], 0.0f) && approx(last_draw_uv[3], 1.0f));
		} else if (deform == 1) {
			assert(approx(last_draw_uv[0], 1.0f) && approx(last_draw_uv[2], -1.0f));
			assert(approx(last_draw_uv[1], 0.0f) && approx(last_draw_uv[3], 1.0f));
		} else {
			assert(approx(last_draw_uv[0], 0.0f) && approx(last_draw_uv[2], 1.0f));
			assert(approx(last_draw_uv[1], 1.0f) && approx(last_draw_uv[3], -1.0f));
		}
	}

	// Partially out-of-bounds surface_area={350, 0, 100, 121} on 388x121: clipped to 38px wide
	p.sprite_deform = 1;
	parts_set_surface_area(&p, c, 350, 0, 100, TH);
	parts_render_cg(&p, c);
	assert(last_draw_valid);
	// origin_offset.x = -100/2 - 350 = -400; rx0=350, rx1=388 -> [POS_X - 50, POS_X - 12]
	assert(approx(last_draw_aabb[0], (float)(POS_X - 50)));
	assert(approx(last_draw_aabb[1], (float)(POS_X - 12)));
	assert(approx(last_draw_uv[0], 38.0f / TW));
	assert(approx(last_draw_uv[2], -38.0f / TW));

	// Invalid sprite_deform warns once and leaves quad/UV unflipped
	warning_calls = 0;
	p.sprite_deform = 99;
	parts_set_surface_area(&p, c, FW, 0, FW, TH);
	parts_render_cg(&p, c);
	assert(last_draw_valid && warning_calls == 1);
	assert(approx(last_draw_aabb[0], (float)(POS_X - FW / 2)));
	assert(approx(last_draw_uv[0], (float)FW / TW));
	assert(approx(last_draw_uv[2], (float)FW / TW));
}

static void test_cg_on_screen_and_v2_reverse_lr(void) {
	const int FW = 97, TH = 121;
	// Near left screen edge with deform=1 and t=3 (sa_x=291): old code shifted to x=-239..-142 and culled it!
	parts_v2 = false;
	struct parts p = make_rance_map_icon(100, 400);
	p.sprite_deform = 1;
	parts_set_surface_area(&p, &p.states[0].common, 3 * FW, 0, FW, TH);
	assert(parts_cg_on_screen(&p, &p.states[0].common));

	// Genuinely off-screen left must still cull
	p.local.pos.x = p.global.pos.x = -100.0f;
	parts_common_recalculate_hitbox(&p, &p.states[0].common);
	assert(!parts_cg_on_screen(&p, &p.states[0].common));

	// parts_v2=true with reverse_lr=true + sprite_deform=1 + surface_area (origin_mode=5 middle-center)
	parts_v2 = true;
	p = make_rance_map_icon(512, 400);
	p.origin_mode = 5;
	p.reverse_lr = true;
	p.sprite_deform = 1;
	parts_set_surface_area(&p, &p.states[0].common, 2 * FW, 0, FW, TH);
	parts_render_cg(&p, &p.states[0].common);
	assert(last_draw_valid);
	// origin_offset for middle-center of 97x121 is (-48, -60); reversed around 512 => [512-49, 512+48] = [463, 560]
	assert(approx(last_draw_aabb[0], 463.0f));
	assert(approx(last_draw_aabb[1], 560.0f));
	assert(approx(last_draw_aabb[2], 340.0f));
	assert(approx(last_draw_aabb[3], 461.0f));
	assert(parts_cg_on_screen(&p, &p.states[0].common));
}

static void test_surface_area_dirty_flag(void) {
	parts_v2 = false;
	struct parts p = make_rance_map_icon(512, 400);
	struct parts_common *c = &p.states[0].common;
	dirty_calls = 0;

	// Setting a new surface area dirties once
	parts_set_surface_area(&p, c, 97, 0, 97, 121);
	assert(dirty_calls == 1);

	// Repeating the identical surface area (every-frame PlayerMapIcon::update) does not dirty again
	parts_set_surface_area(&p, c, 97, 0, 97, 121);
	assert(dirty_calls == 1);

	// Changing to the next frame dirties once
	parts_set_surface_area(&p, c, 194, 0, 97, 121);
	assert(dirty_calls == 2);

	// Negative x/y is rejected and leaves surface_area and dirty_calls unchanged
	parts_set_surface_area(&p, c, -1, 0, 97, 121);
	assert(dirty_calls == 2 && c->surface_area.x == 194);
	parts_set_surface_area(&p, c, 0, -5, 97, 121);
	assert(dirty_calls == 2 && c->surface_area.x == 194);
}

static void test_flipped_alpha_hittest(void) {
	const int TW = 388, TH = 121, FW = 97;
	for (int v2 = 0; v2 <= 1; v2++) {
		parts_v2 = (v2 != 0);
		struct parts p = make_rance_map_icon(512, 400);
		p.pixel_decide = true;

		// Horizontal slice sa=(97, 0, 97, 121) -> rendered at [464..560, 279..399]
		parts_set_surface_area(&p, &p.states[0].common, FW, 0, FW, TH);

		// deform=0: leftmost pixel (464, 279) samples (97, 0)
		p.sprite_deform = 0;
		alpha_check_calls = 0;
		assert(parts_hittest(&p, 0, (Point){ 464, 279 }));
		assert(alpha_check_calls == 1 && last_alpha_px == 97 && last_alpha_py == 0);

		// deform=1 (H-flip): leftmost pixel (464, 279) samples (TW-1-97, 0) = (290, 0),
		// rightmost pixel (560, 279) samples (TW-1-193, 0) = (194, 0).
		p.sprite_deform = 1;
		alpha_check_calls = 0;
		assert(parts_hittest(&p, 0, (Point){ 464, 279 }));
		assert(alpha_check_calls == 1 && last_alpha_px == TW - 1 - 97 && last_alpha_py == 0);
		assert(parts_hittest(&p, 0, (Point){ 560, 279 }));
		assert(alpha_check_calls == 2 && last_alpha_px == TW - 1 - 193 && last_alpha_py == 0);

		// deform=2 (V-flip) on vertical slice sa=(0, 30, 97, 30) -> rendered at [464..560, 370..399]
		p.sprite_deform = 2;
		parts_set_surface_area(&p, &p.states[0].common, 0, 30, FW, 30);
		alpha_check_calls = 0;
		assert(parts_hittest(&p, 0, (Point){ 464, 370 }));
		assert(alpha_check_calls == 1 && last_alpha_px == 0 && last_alpha_py == TH - 1 - 30);
		assert(parts_hittest(&p, 0, (Point){ 464, 399 }));
		assert(alpha_check_calls == 2 && last_alpha_px == 0 && last_alpha_py == TH - 1 - 59);

		// Non-CG detection state must NOT flip alpha coordinates even if parts->sprite_deform != 0
		p.sprite_deform = 1;
		p.states[0].type = PARTS_RECT_DETECTION;
		p.states[0].rect.is_cg_detection = true;
		p.states[0].rect.custom = false;
		parts_set_surface_area(&p, &p.states[0].common, FW, 0, FW, TH);
		alpha_check_calls = 0;
		assert(parts_hittest(&p, 0, (Point){ 464, 279 }));
		assert(alpha_check_calls == 1 && last_alpha_px == 97 && last_alpha_py == 0);
	}
}

int main(void) {
	test_rance03_player_map_icon_deform_and_surface_area();
	test_cg_on_screen_and_v2_reverse_lr();
	test_surface_area_dirty_flag();
	test_flipped_alpha_hittest();
	puts("rance03 map movement regression fixtures passed");
	return 0;
}
"""


class Rance03MapRegressionTests(unittest.TestCase):
    def test_sprite_deform_surface_area_on_screen_dirty_and_hittest(self):
        source = "\n\n".join([
            HARNESS_PREFIX,
            extract_function("src/parts/parts.c", "calculate_offset"),
            extract_function("src/parts/parts.c", "parts_common_recalculate_hitbox"),
            extract_function("src/parts/parts.c", "parts_local_position"),
            extract_function("src/parts/parts.c", "parts_apply_local_matrix"),
            extract_function("src/parts/parts.c", "parts_set_surface_area"),
            extract_function("src/parts/render.c", "quad_screen_aabb"),
            extract_function("src/parts/render.c", "crop_quad_transform"),
            extract_function("src/parts/render.c", "apply_sprite_deform_uv"),
            extract_function("src/parts/render.c", "parts_render_cg"),
            extract_function("src/parts/render.c", "parts_cg_on_screen"),
            extract_function("src/parts/input.c", "parts_hittest_alpha_state"),
            extract_function("src/parts/input.c", "parts_apply_sprite_deform_pixel"),
            extract_function("src/parts/input.c", "parts_hittest"),
            TESTS_MAIN,
        ])
        cc = os.environ.get("CC", "cc")
        with tempfile.TemporaryDirectory(prefix="r03_map_test_") as tmp:
            src_path = Path(tmp) / "test_rance03_map.c"
            bin_path = Path(tmp) / "test_rance03_map"
            src_path.write_text(source)
            subprocess.run(
                [cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                 str(src_path), "-lm", "-o", str(bin_path)],
                check=True,
            )
            out = subprocess.run([str(bin_path)], check=True, capture_output=True, text=True)
            self.assertIn("rance03 map movement regression fixtures passed", out.stdout)


if __name__ == "__main__":
    unittest.main()
