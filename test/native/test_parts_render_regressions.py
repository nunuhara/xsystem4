#!/usr/bin/env python3
"""Dependency-free regression harness for PartsEngine2 reversal/construction blur.

Compiles the actual function bodies from the engine against small matrix/texture
fixtures. No SDL, GL context or copyrighted game data is needed. This verifies
transform semantics and the no-feedback invariant, NOT real GPU/game rendering.
Run: python3 xsystem4/test/native/test_parts_render_regressions.py
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_vm_reign_regressions import compile_and_run

ROOT = Path(__file__).resolve().parents[2]


def function(path, name):
    source = (ROOT / path).read_text()
    start = source.rfind('\n', 0, source.index(name + '(')) + 1
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class FontSessionTests(unittest.TestCase):
    def test_next_game_loads_its_own_fonts(self):
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#define ALICEHUB_IOS
#define MAX_FNL_FONTS 2
#define FONT_GOTHIC 0
#define FONT_MINCHO 1
#define FW_BOLD 1
#define ERROR(...) abort()
struct font { int id; };
static struct { int face, size; struct font *font_size; } text_sprite_ts = {0, 16, NULL};
struct fnl { unsigned nr_fonts; };
struct font_metrics { int size, face, weight; unsigned char color[4]; };
static struct font_metrics font_metrics;
static struct { const char *fnl_path; } config;
static struct { int version; } *ain;
static bool font_initialized, gfx_text_advance_edges, game_rance7_mg;
static struct font *font_ttf[2], *font_fnl[MAX_FNL_FONTS];
static struct fnl *session_fnl;
static float fnl_substitute_scales[MAX_FNL_FONTS];
static unsigned fnl_substitute_count;
static void *glyph_cache_trackers;
static size_t nr_glyph_cache_trackers, cap_glyph_cache_trackers;
static int game, loaded, released, ft_sessions;
static void ft_font_init(void) { assert(!ft_sessions); ft_sessions++; }
static void ft_font_fini(void) { assert(ft_sessions); ft_sessions--; }
static void init_fnl_substitute_scale(void) {
    assert(!fnl_substitute_count && fnl_substitute_scales[0] == 0);
    if (!config.fnl_path) {
        fnl_substitute_scales[0] = 0.6f;
        fnl_substitute_count = 1;
    }
}
static struct font *load_font(int face) {
    struct font *f = malloc(sizeof(*f));
    f->id = game * 10 + face; loaded++;
    return f;
}
static bool fnl_uses_ttf_substitute(void) { return !config.fnl_path; }
static struct fnl *fnl_open(const char *path) {
    struct fnl *lib = malloc(sizeof(*lib)); lib->nr_fonts = 1;
    assert(!strcmp(path, "second.fnl")); return lib;
}
static struct font *fnl_font_load(struct fnl *lib, unsigned index) {
    assert(index < lib->nr_fonts);
    return load_font(2);
}
static void fnl_free(struct fnl *lib) { assert(!font_fnl[0]); free(lib); }
static void ft_font_free(struct font *font) { if (font) { released++; free(font); } }
static void fnl_font_free(struct font *font) { ft_font_free(font); }
''' + "\n".join(function("src/text.c", name) for name in (
            "void gfx_font_init", "void gfx_font_fini",
        )) + r'''
int main(void) {
    game = 1;
    gfx_font_init();
    gfx_font_init();
    assert(loaded == 2 && font_ttf[0]->id == 10 && !font_fnl[0]);
    text_sprite_ts.font_size = font_ttf[text_sprite_ts.face];
    assert(gfx_text_advance_edges && fnl_substitute_count == 1);
    glyph_cache_trackers = malloc(16);
    nr_glyph_cache_trackers = cap_glyph_cache_trackers = 1;
    font_metrics.size = 99;
    gfx_font_fini();
    assert(released == 2 && !glyph_cache_trackers && !nr_glyph_cache_trackers);
    assert(!font_ttf[0] && !gfx_text_advance_edges && font_metrics.size == 16);
    assert(!text_sprite_ts.font_size && text_sprite_ts.face == 0 && text_sprite_ts.size == 16);
    game = 2;
    config.fnl_path = "second.fnl";
    gfx_font_init();
    assert(loaded == 5 && font_ttf[0]->id == 20 && font_fnl[0]->id == 22);
    text_sprite_ts.font_size = font_ttf[text_sprite_ts.face];
    assert(text_sprite_ts.font_size->id == 20);
    gfx_font_fini();
    assert(released == 5 && !ft_sessions && !session_fnl);
}
'''
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "font.c"
            binary = Path(temp) / "font"
            source.write_text(harness)
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


FIXTURES = r'''
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef float mat4[4][4];
typedef float vec3[3];
typedef struct { float x, y; } SDL_FPoint;
typedef struct { int x, y; } Point;
#define PARTS_LAYOUT_BOX 1
#define PARTS_LAYOUT_FREE 2
struct parts {
    struct parts *parent;
    int component_type;
    struct { int type; struct { int layout_type; } layout_box;
             struct { Point origin_offset; } common; } states[1];
    struct { float x, y, z; } dummy;
    struct { SDL_FPoint pos, scale; struct { float x, y, z; } rotation; } local;
    int scroll_links[2];
    struct { float pos; } *scrollbar;
    bool reverse_lr, reverse_tb;
};
static struct parts component;
static int transform_dirty, draw_dirty;
static struct parts *parts_get(int n) { assert(n == 1); return &component; }
static struct parts *parts_try_get(int n) { (void)n; return NULL; }
static SDL_FPoint parts_local_position(struct parts *p) { return p->local.pos; }
static void parts_component_dirty(struct parts *p) { (void)p; transform_dirty++; }
static void parts_dirty(struct parts *p) { (void)p; draw_dirty++; }
static float glm_rad(float x) { return x * 0.017453292519943295f; }
static void identity(mat4 m) {
    memset(m, 0, sizeof(mat4));
    for (int i = 0; i < 4; i++) m[i][i] = 1;
}
static void mul(mat4 a, mat4 b, mat4 out) {
    mat4 m = {0};
    for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++)
        for (int k = 0; k < 4; k++) m[c][r] += a[k][r] * b[c][k];
    memcpy(out, m, sizeof m);
}
static void glm_translate(mat4 m, vec3 p) {
    mat4 t; identity(t);
    for (int i = 0; i < 3; i++) t[3][i] = p[i];
    mul(m, t, m);
}
static void glm_scale(mat4 m, vec3 p) {
    for (int c = 0; c < 3; c++) for (int r = 0; r < 4; r++) m[c][r] *= p[c];
}
static void rotate(mat4 m, float a, mat4 out, int x, int y) {
    mat4 t; identity(t);
    t[x][x] = t[y][y] = cosf(a); t[x][y] = sinf(a); t[y][x] = -sinf(a);
    mul(m, t, out);
}
static void glm_rotate_z(mat4 m, float a, mat4 out) { rotate(m, a, out, 0, 1); }
static void glm_rotate_x(mat4 m, float a, mat4 out) { rotate(m, a, out, 1, 2); }
static void glm_rotate_y(mat4 m, float a, mat4 out) { rotate(m, a, out, 2, 0); }

typedef struct texture { int handle, w, h; unsigned char *pixels; } Texture;
struct parts_construction_process { struct { Texture texture; } common; };
struct parts_cp_filter { int x, y, w, h, blur; bool full_size; };
static int allocated, deleted, blurs, next_handle = 10;
static void gfx_init_texture_blank(Texture *t, int w, int h) {
    *t = (Texture){++next_handle, w, h, calloc((size_t)w*h, 4)};
    assert(t->pixels); allocated++;
}
static void gfx_delete_texture(Texture *t) { free(t->pixels); t->handle = 0; deleted++; }
static void gfx_copy_with_alpha_map(Texture *d, int dx, int dy, Texture *s,
                                   int sx, int sy, int w, int h) {
    assert(d->handle != s->handle);
    for (int y = 0; y < h; y++)
        memcpy(d->pixels + ((dy+y)*d->w+dx)*4, s->pixels + ((sy+y)*s->w+sx)*4, (size_t)w*4);
}
static void blur(Texture *d, int dx, int dy, Texture *s, int sx, int sy,
                 int w, int h, int radius, bool vertical) {
    // This is the GLES invariant: source MUST NOT be attached as destination.
    assert(s->handle != d->handle);
    assert(w > 0 && h > 0 && radius > 0);
    blurs++;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        for (int c = 0; c < 3; c++) {
            int sum = 0;
            for (int i = -radius; i <= radius; i++) {
                int xx = x + (vertical ? 0 : i), yy = y + (vertical ? i : 0);
                if (xx >= 0 && xx < w && yy >= 0 && yy < h)
                    sum += s->pixels[((sy+yy)*s->w + sx+xx)*4+c];
            }
            d->pixels[((dy+y)*d->w + dx+x)*4+c] = sum/(2*radius+1);
        }
        // Match the production blur functions: preserve destination alpha.
    }
}
static void gfx_copy_height_blur(Texture *d, int dx, int dy, Texture *s, int sx, int sy,
                                int w, int h, int r) { blur(d,dx,dy,s,sx,sy,w,h,r,true); }
static void gfx_copy_width_blur(Texture *d, int dx, int dy, Texture *s, int sx, int sy,
                               int w, int h, int r) { blur(d,dx,dy,s,sx,sy,w,h,r,false); }
static void gfx_blur_rgb_to_fresh(Texture *d, Texture *s, int r, bool vertical) {
    assert(s->handle != d->handle && d->w == s->w && d->h == s->h);
    // Preserve source alpha in the fresh destination texture, matching box_blur_shader blur_mode=1.
    for (int i = 0; i < s->w * s->h; i++)
        d->pixels[i * 4 + 3] = s->pixels[i * 4 + 3];
    blur(d, 0, 0, s, 0, 0, s->w, s->h, r, vertical);
}
static int grays;
static void gfx_copy_grayscale(Texture *d, int dx, int dy, Texture *s, int sx, int sy,
                               int w, int h) {
    // GLES invariant: source MUST NOT be attached as destination.
    assert(s->handle != d->handle);
    assert(w > 0 && h > 0);
    grays++;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        unsigned char *sp = s->pixels + ((sy + y) * s->w + sx + x) * 4;
        unsigned char *dp = d->pixels + ((dy + y) * d->w + dx + x) * 4;
        unsigned char g = (unsigned char)roundf(0.299f * sp[0] + 0.587f * sp[1] + 0.114f * sp[2]);
        dp[0] = dp[1] = dp[2] = g;
        // Preserve destination alpha.
    }
}
'''

TESTS = r'''
static void close_matrix(mat4 a, mat4 b) {
    for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++)
        assert(fabsf(a[c][r] - b[c][r]) < 0.0001f);
}
static void test_container_origin(void) {
    struct parts parent = {0};
    component = (struct parts){0};
    component.local.scale = (SDL_FPoint){1, 1};
    mat4 actual, expected;
    // An unattached component has no container origin.
    identity(actual); identity(expected);
    parts_apply_local_matrix(&component, actual);
    close_matrix(actual, expected);
    component.parent = &parent;
    parent.component_type = 15;
    parent.states[0].common.origin_offset = (Point){-192, -108};
    identity(actual);
    glm_translate(actual, (vec3){210, 135, 0});
    parts_apply_local_matrix(&component, actual);
    assert(actual[3][0] == 18 && actual[3][1] == 27);
    for (int kind = 0; kind < 4; kind++) {
        parent.component_type = kind == 0 ? 15 : 0;
        parent.states[0].type = kind == 1 || kind == 2 ? PARTS_LAYOUT_BOX : 0;
        parent.states[0].layout_box.layout_type = kind == 1 ? PARTS_LAYOUT_FREE : 0;
        for (int empty = 0; empty < 2; empty++) {
            parent.states[0].common.origin_offset = empty ? (Point){0, 0} : (Point){-192, -108};
            for (int rotated = 0; rotated < 2; rotated++) {
                component.local.pos = (SDL_FPoint){7, 9};
                component.local.rotation.z = rotated ? 20 : 0;
                identity(actual); identity(expected);
                // Parent scale/rotation must transform the origin translation too.
                glm_translate(actual, (vec3){210, 135, 0});
                glm_rotate_z(actual, glm_rad(13), actual);
                glm_scale(actual, (vec3){2, 3, 1});
                memcpy(expected, actual, sizeof(mat4));
                parts_apply_local_matrix(&component, actual);
                bool offset = kind < 2 && !empty;
                glm_translate(expected, (vec3){7 + (offset ? -192 : 0), 9 + (offset ? -108 : 0), 0});
                if (rotated) glm_rotate_z(expected, glm_rad(20), expected);
                close_matrix(actual, expected);
            }
        }
    }
}
static void test_reversal(void) {
    // CG and FLAT both take this same local transform through parts_world_matrix.
    for (int rotation = 0; rotation < 2; rotation++) {
        for (int flags = 0; flags < 4; flags++) {
            component = (struct parts){0};
            component.local.pos = (SDL_FPoint){120, 70};
            component.local.scale = (SDL_FPoint){2, 3};
            if (rotation) {
                component.local.rotation.x = 11;
                component.local.rotation.y = 19;
                component.local.rotation.z = 37;
            }
            component.reverse_lr = flags & 1;
            component.reverse_tb = flags & 2;
            mat4 actual, expected;
            identity(actual); identity(expected);
            // Simulate a transformed parent too: flips must not replace it.
            glm_translate(actual, (vec3){7, 9, 0});
            glm_translate(expected, (vec3){7, 9, 0});
            parts_apply_local_matrix(&component, actual);
            glm_translate(expected, (vec3){120,70,0});
            if (rotation) {
                glm_rotate_z(expected, glm_rad(37), expected);
                glm_rotate_x(expected, glm_rad(-11), expected);
                glm_rotate_y(expected, glm_rad(19), expected);
            }
            glm_scale(expected, (vec3){flags & 1 ? -2 : 2, flags & 2 ? -3 : 3, 1});
            close_matrix(actual, expected);
            // The public magnification stays independent of reversal.
            assert(component.local.scale.x == 2 && component.local.scale.y == 3);
        }
    }
    component = (struct parts){0};
    transform_dirty = draw_dirty = 0;
    PartsEngine_SetComponentReverseLR(1, true);
    assert(component.reverse_lr && transform_dirty == 1 && draw_dirty == 1);
    PartsEngine_SetComponentReverseLR(1, true);
    assert(transform_dirty == 1 && draw_dirty == 1);
    PartsEngine_SetComponentReverseLR(1, false);
    PartsEngine_SetComponentReverseTB(1, true);
    assert(!component.reverse_lr && component.reverse_tb);
    assert(transform_dirty == 3 && draw_dirty == 3);
}
static void test_blur(void) {
    for (int vertical = 0; vertical < 2; vertical++) for (int full = 0; full < 2; full++) {
        struct parts_construction_process cp = {0};
        gfx_init_texture_blank(&cp.common.texture, 7, 5);
        unsigned char original[7*5*4];
        for (int i = 0; i < 7*5*4; i++) original[i] = cp.common.texture.pixels[i] = i % 251;
        Texture expected = {0}, source = {0};
        gfx_init_texture_blank(&expected, 7, 5);
        gfx_init_texture_blank(&source, 7, 5);
        memcpy(expected.pixels, original, sizeof original);
        memcpy(source.pixels, original, sizeof original);
        struct parts_cp_filter op = {1, 1, 4, 3, 1, full};
        blur(&expected, full?0:1, full?0:1, &source, full?0:1, full?0:1,
             full?7:4, full?5:3, 1, vertical);
        assert(build_blur_filter(&cp, &op, vertical));
        assert(!memcmp(expected.pixels, cp.common.texture.pixels, sizeof original));
        // Format-preserving full-texture blur (RGB pixel-only target) keeps the original handle!
        if (full) {
            struct parts_construction_process cp_rgb = {0};
            gfx_init_texture_blank(&cp_rgb.common.texture, 7, 5);
            memcpy(cp_rgb.common.texture.pixels, original, sizeof original);
            int rgb_handle = cp_rgb.common.texture.handle;
            assert(build_blur_filter_ex(&cp_rgb, &op, vertical, true));
            assert(cp_rgb.common.texture.handle == rgb_handle);
            assert(!memcmp(expected.pixels, cp_rgb.common.texture.pixels, sizeof original));
            gfx_delete_texture(&cp_rgb.common.texture);
        }
        // A zero radius or empty rect does not allocate/copy or touch the target.
        int before = allocated;
        op.blur = 0;
        assert(build_blur_filter(&cp, &op, vertical) && allocated == before);
        assert(!memcmp(expected.pixels, cp.common.texture.pixels, sizeof original));
        gfx_delete_texture(&cp.common.texture);
        gfx_delete_texture(&expected); gfx_delete_texture(&source);
    }
    struct parts_construction_process missing = {0};
    struct parts_cp_filter op = {0, 0, 7, 5, 2, true};
    assert(!build_blur_filter(&missing, &op, false));
    // Paired full-texture H+V blur: 1 temp alloc, exact match to sequential H then V blur.
    {
        struct parts_construction_process cp = {0};
        gfx_init_texture_blank(&cp.common.texture, 7, 5);
        unsigned char original[7*5*4];
        for (int i = 0; i < 7*5*4; i++) original[i] = cp.common.texture.pixels[i] = (i * 19 + 7) % 251;
        Texture mid = {0}, expected = {0}, src = {0};
        gfx_init_texture_blank(&src, 7, 5);
        gfx_init_texture_blank(&mid, 7, 5);
        gfx_init_texture_blank(&expected, 7, 5);
        memcpy(src.pixels, original, sizeof original);
        memcpy(mid.pixels, original, sizeof original);
        memcpy(expected.pixels, original, sizeof original);
        blur(&mid, 0, 0, &src, 0, 0, 7, 5, 2, false);
        blur(&expected, 0, 0, &mid, 0, 0, 7, 5, 1, true);

        struct parts_cp_filter op_h = {0, 0, 7, 5, 2, true};
        struct parts_cp_filter op_v = {0, 0, 7, 5, 1, false};
        assert(blur_op_is_full_texture(&cp, &op_h));
        assert(blur_op_is_full_texture(&cp, &op_v));
        int alloc_before = allocated;
        int handle_before = cp.common.texture.handle;
        assert(build_blur_filter_pair(&cp, &op_h, false, &op_v, true));
        assert(allocated == alloc_before + 1);
        assert(cp.common.texture.handle == handle_before);
        assert(!memcmp(expected.pixels, cp.common.texture.pixels, sizeof original));

        gfx_delete_texture(&cp.common.texture);
        gfx_delete_texture(&expected);
        gfx_delete_texture(&mid);
        gfx_delete_texture(&src);
    }
    assert(allocated == deleted);
}
static void test_gray(void) {
    for (int full = 0; full < 2; full++) {
        struct parts_construction_process cp = {0};
        gfx_init_texture_blank(&cp.common.texture, 7, 5);
        unsigned char original[7*5*4];
        for (int i = 0; i < 7*5*4; i++) original[i] = cp.common.texture.pixels[i] = (i * 37) % 251;
        Texture expected = {0}, source = {0};
        gfx_init_texture_blank(&expected, 7, 5);
        gfx_init_texture_blank(&source, 7, 5);
        memcpy(expected.pixels, original, sizeof original);
        memcpy(source.pixels, original, sizeof original);
        struct parts_cp_filter op = {1, 1, 4, 3, 0, full};
        gfx_copy_grayscale(&expected, full ? 0 : 1, full ? 0 : 1, &source,
                           full ? 0 : 1, full ? 0 : 1, full ? 7 : 4, full ? 5 : 3);
        assert(build_gray_filter(&cp, &op));
        assert(!memcmp(expected.pixels, cp.common.texture.pixels, sizeof original));
        int before = allocated;
        struct parts_cp_filter empty = {1, 1, 0, 3, 0, false};
        assert(build_gray_filter(&cp, &empty) && allocated == before);
        assert(!memcmp(expected.pixels, cp.common.texture.pixels, sizeof original));
        gfx_delete_texture(&cp.common.texture);
        gfx_delete_texture(&expected);
        gfx_delete_texture(&source);
    }
    struct parts_construction_process missing = {0};
    struct parts_cp_filter op = {0, 0, 7, 5, 0, true};
    assert(!build_gray_filter(&missing, &op));
    assert(allocated == deleted);
}
int main(void) { test_reversal(); test_container_origin(); test_blur(); test_gray(); puts("parts render regression fixtures passed"); }
'''


class PartsRenderingTests(unittest.TestCase):
    def test_okusama_alpha_blend_cut_cg_adds_coverage_to_construction_surface(self):
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
enum { GL_ONE, GL_ZERO };
enum { PARTS_CP_DRAW_CUT_CG, PARTS_CP_ALPHA_BLEND_CUT_CG };
typedef struct { uint8_t r, g, b, a; } SDL_Color;
typedef struct { int handle, w, h; float alpha; } Texture;
struct parts_construction_process { struct { Texture texture; } common; };
struct parts_cp_cut_cg { int cg_name, dx, dy, dw, dh, sx, sy, sw, sh; };
struct parts_cp_op { int type; struct parts_cp_cut_cg cut_cg; };
struct copy_data { int dx, dy, dw, dh, sx, sy, sw, sh, dst_texture; };
#define STRETCH_DATA(...) { __VA_ARGS__, 0 }
static int copy_straight_alpha_shader;
static bool game_okusama;
static Texture source = {1, 64, 220, 1.0f};
static bool source_alpha_enabled;
static float under_alpha;
static int blank_inits, rgba_inits;
static Texture *cproc_get_cached_texture(int name) { return name ? &source : 0; }
static void gfx_init_texture_blank(Texture *dst, int w, int h) {
    blank_inits++;
    *dst = (Texture){2, w, h, 0};
}
static void gfx_init_texture_rgba(Texture *dst, int w, int h, SDL_Color c) {
    assert(c.r == 0 && c.g == 0 && c.b == 0 && c.a == 0);
    rgba_inits++;
    *dst = (Texture){2, w, h, 0};
}
static void gfx_copy_with_alpha_map(Texture *dst, int dx, int dy, Texture *src,
                                    int sx, int sy, int w, int h) {
    assert(dst->handle == 2 && src->handle == 1 && dx == 0 && dy == 0);
    assert(sx == 2 && sy == 3 && w == 32 && h == 110);
    under_alpha = src->alpha;
}
static void gfx_delete_texture(Texture *dst) { assert(dst->handle == 2); }
static void glBlendFuncSeparate(int rgb_src, int rgb_dst, int alpha_src, int alpha_dst) {
    assert(rgb_src == GL_ONE && rgb_dst == GL_ZERO);
    assert(alpha_src == GL_ONE && alpha_dst == GL_ZERO);
    source_alpha_enabled = true;
}
static void run_copy_shader(int *shader, Texture *dst, Texture *src, struct copy_data *data) {
    assert(shader == &copy_straight_alpha_shader && source_alpha_enabled && data->dst_texture == 2);
    assert(data->dx == 2 && data->dy == 3 && data->dw == 32 && data->dh == 110);
    assert(data->sx == 4 && data->sy == 5 && data->sw == 64 && data->sh == 220);
    dst->alpha = src->alpha + (1.0f - src->alpha) * under_alpha;
}
static void restore_blend_mode(void) { source_alpha_enabled = false; }
static void gfx_copy_stretch_blend_amap(Texture *dst, int dx, int dy, int dw, int dh,
                                      Texture *src, int sx, int sy, int sw, int sh) {
    assert(dst->alpha == 0 && src == &source);
    assert(dx == 2 && dy == 3 && dw == 32 && dh == 110);
    assert(sx == 4 && sy == 5 && sw == 64 && sh == 220);
}
''' + function('src/draw.c', 'gfx_copy_stretch_blend_straight_alpha') + '\n' + function(
            'src/parts/construction.c', 'build_draw_cut_cg'
        ) + r'''
int main(void) {
    struct parts_construction_process cproc = { .common.texture = { .handle = 1, .w = 64, .h = 220 } };
    struct parts_cp_op cmd = { PARTS_CP_ALPHA_BLEND_CUT_CG, {1, 2, 3, 32, 110, 4, 5, 0, 0} };
    assert(build_draw_cut_cg(&cproc, &cmd) && cproc.common.texture.alpha == 0);
    game_okusama = true;
    cmd.type = PARTS_CP_DRAW_CUT_CG;
    assert(build_draw_cut_cg(&cproc, &cmd) && cproc.common.texture.alpha == 0);
    cmd.type = PARTS_CP_ALPHA_BLEND_CUT_CG;
    assert(build_draw_cut_cg(&cproc, &cmd) && cproc.common.texture.alpha == 1 && blank_inits == 1 && rgba_inits == 0);
    source.alpha = 0.5f;
    cproc.common.texture.alpha = 0;
    assert(build_draw_cut_cg(&cproc, &cmd) && cproc.common.texture.alpha == 0.5f);
    cproc.common.texture.alpha = 0.5f;
    assert(build_draw_cut_cg(&cproc, &cmd) && cproc.common.texture.alpha == 0.75f);
    // Out-of-bounds target region must zero-initialize under texture via gfx_init_texture_rgba.
    cproc.common.texture.w = 10;
    assert(build_draw_cut_cg(&cproc, &cmd) && rgba_inits == 1);
    assert(!source_alpha_enabled);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c, exe = Path(tmp) / 'cut_cg.c', Path(tmp) / 'cut_cg'
            c.write_text(code)
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_button_label_uses_authored_area_without_layout_whitespace(self):
        code = r'''
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
typedef struct { int x, y, w, h; } Rectangle;
typedef struct { int x, y; } Point;
struct parts_text_char { char ch[4]; int advance; };
struct parts_text_line {
    struct parts_text_char *chars;
    int nr_chars;
    unsigned height;
    float width;
};
struct parts_text {
    struct { Point origin_offset; } common;
    unsigned nr_lines;
    struct parts_text_line *lines;
};
static bool sys4_text_is_gbk = true;
''' + function('src/parts/render.c', 'place_button_label') + r'''
int main(void) {
    struct parts_text_char chars[] = { {" ", 5}, {"\xa1\xa1", 15}, {"C", 10} };
    struct parts_text_line lines[] = { {0}, { chars, 3, 16, 30 } };
    struct parts_text label = { .nr_lines = 2, .lines = lines };
    struct parts_text_line line;
    assert(!place_button_label(&label, &line, (Rectangle){0}, (Point){0}));
    assert(place_button_label(&label, &line, (Rectangle){84, 9, 50, 18}, (Point){3, 4}));
    assert(label.nr_lines == 1 && label.lines->nr_chars == 1 && label.lines->width == 10);
    assert(!strcmp(label.lines->chars->ch, "C"));
    assert(label.common.origin_offset.x == 107 && label.common.origin_offset.y == 14);
    assert(lines[1].nr_chars == 3 && lines[1].width == 30);
    label = (struct parts_text){ .nr_lines = 2, .lines = lines };
    lines[0] = lines[1];
    assert(!place_button_label(&label, &line, (Rectangle){84, 9, 50, 18}, (Point){3, 4}));
    assert(label.nr_lines == 2);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c, exe = Path(tmp) / 'button_label.c', Path(tmp) / 'button_label'
            c.write_text(code)
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(c), '-lm', '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_only_okusama_building_empty_parts_v2_construction_erases_surface(self):
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/queue.h>
struct parts { bool dirty; };
struct texture { unsigned handle; };
struct parts_common { struct texture texture; int w, h; bool alpha_mask; };
struct op { TAILQ_ENTRY(op) link; };
struct parts_construction_process {
    struct parts_common common;
    bool needs_build, needs_pixel_snapshot;
    TAILQ_HEAD(ops, op) ops;
};
static bool game_okusama, parts_v2;
static int builds;
static void gfx_delete_texture(struct texture *t) { t->handle = 0; }
static void parts_common_free_alpha_mask(struct parts_common *c) { c->alpha_mask = false; }
static void parts_set_dims(struct parts *p, struct parts_common *c, int w, int h) {
    (void)p; c->w = w; c->h = h;
}
static void parts_dirty(struct parts *p) { p->dirty = true; }
static bool parts_build_construction_process(struct parts *p, struct parts_construction_process *c) {
    (void)p; (void)c; builds++; return true;
}
''' + function('src/parts/construction.c', 'build_explicit_construction_process') + r'''
int main(void) {
    struct parts p = {0};
    struct parts_construction_process c = {
        .common = { {42}, 1280, 820, true },
        .needs_build = true, .needs_pixel_snapshot = true,
    };
    TAILQ_INIT(&c.ops);
    assert(build_explicit_construction_process(&p, &c));
    assert(builds == 1 && c.common.texture.handle == 42 && !p.dirty);
    parts_v2 = true;
    assert(build_explicit_construction_process(&p, &c));
    assert(builds == 2 && c.common.texture.handle == 42 && c.common.alpha_mask && !p.dirty);
    assert(c.common.w == 1280 && c.common.h == 820 && c.needs_pixel_snapshot);
    game_okusama = true;
    struct op op;
    TAILQ_INSERT_TAIL(&c.ops, &op, link);
    assert(build_explicit_construction_process(&p, &c));
    assert(builds == 3 && c.common.texture.handle == 42 && !p.dirty);
    TAILQ_REMOVE(&c.ops, &op, link);
    assert(build_explicit_construction_process(&p, &c));
    assert(builds == 3 && p.dirty && !c.common.texture.handle && !c.common.alpha_mask);
    assert(!c.common.w && !c.common.h && !c.needs_build && !c.needs_pixel_snapshot);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c, exe = Path(tmp) / 'construction.c', Path(tmp) / 'construction'
            c.write_text(code)
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_production_functions(self):
        code = FIXTURES + '\n'.join([
            function('src/parts/parts.c', 'parts_apply_local_matrix'),
            function('src/hll/PartsEngine.c', 'PartsEngine_SetComponentReverseLR'),
            function('src/hll/PartsEngine.c', 'PartsEngine_SetComponentReverseTB'),
            function('src/parts/construction.c', 'blur_op_is_full_texture'),
            function('src/parts/construction.c', 'build_blur_filter_pair'),
            function('src/parts/construction.c', 'build_blur_filter_ex'),
            function('src/parts/construction.c', 'build_blur_filter'),
            function('src/parts/construction.c', 'build_gray_filter'),
        ]) + TESTS
        with tempfile.TemporaryDirectory() as tmp:
            c = Path(tmp) / 'test.c'
            exe = Path(tmp) / 'test'
            c.write_text(code)
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(c), '-lm', '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_activity_scale_legacy_and_v2(self):
        # Exercise the real activity scale helper instead of asserting that the
        # loader never bakes reversal (that is only correct for PartsEngine2).
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
struct parts { bool reverse_lr, reverse_tb; float sx, sy; };
struct ex_tree { bool lr, tb; float sx, sy; };
static bool parts_v2;
static float number(struct ex_tree *t, const char *key, float fallback) {
    (void)fallback;
    return !strcmp(key, "左右反转") ? t->lr : t->tb;
}
static float item(struct ex_tree *t, const char *key, int i, float fallback) {
    (void)key; (void)fallback; return i ? t->sy : t->sx;
}
static void parts_set_scale_x(struct parts *p, float v) { p->sx = v; }
static void parts_set_scale_y(struct parts *p, float v) { p->sy = v; }
''' + function('src/parts/activity.c', 'load_part_scale') + r'''
int main(void) {
    for (int v2 = 0; v2 < 2; v2++) for (int f = 0; f < 4; f++)
    for (int signs = 0; signs < 4; signs++) {
        parts_v2 = v2;
        struct parts p = {0};
        struct ex_tree t = {f & 1, f & 2, signs & 1 ? -2 : 2, signs & 2 ? -3 : 3};
        load_part_scale(&p, &t);
        assert(p.reverse_lr == t.lr && p.reverse_tb == t.tb);
        assert(p.sx == (v2 || !t.lr ? t.sx : -t.sx));
        assert(p.sy == (v2 || !t.tb ? t.sy : -t.sy));
    }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c, exe = Path(tmp) / 'activity.c', Path(tmp) / 'activity'
            c.write_text(code)
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_restored_window_requests_a_present(self):
        # Compile the actual event dispatch helpers, not text-only assertions.
        # The GLES integration suite tests actual GL context loss & presentation separately.
        code = r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>
enum { SDL_WINDOWEVENT_EXPOSED, SDL_WINDOWEVENT_ENTER, SDL_WINDOWEVENT_LEAVE,
       SDL_WINDOWEVENT_RESTORED, SDL_WINDOWEVENT_FOCUS_GAINED,
       SDL_WINDOWEVENT_FOCUS_LOST, SDL_WINDOWEVENT_SIZE_CHANGED, VK_CONTROL = 17 };
typedef struct { struct { int event; } window; } SDL_Event;
static bool mouse_focus, keyboard_focus, kbd_ctrl_down;
static atomic_bool touch_ctrl_skip;
static bool kbd_key_state[256], sdl_mouse_down[8], key_state[256];
static bool multi_touch_active, two_finger_tap_candidate, two_finger_scrolled, synthetic_mouse_warped;
static int touch_press_id = 0;
static int cleared_touch_fingers = 0;
static int restored, foreground, swaps, traces, recovered, backup_released;
static bool next_recover_result = false;
static bool mock_gl_ready = true;
static atomic_bool android_redraw_requested = false;
static void gfx_trace_resume(const char *phase) { (void)phase; traces++; }
static void gfx_restore_window(void) { restored++; }
static void gfx_android_on_foreground(void) { foreground++; }
static void gfx_swap(void) { swaps++; }
static void gfx_update_screen_scale(void) {}
static void gfx_reset_frame_pacer(void) {}
static bool gfx_recover_gl_if_needed(void) { recovered++; return next_recover_result; }
static bool gfx_release_context_backup_if_ready(void) {
    if (!mock_gl_ready) return false;
    backup_released++;
    return true;
}
static void mixer_set_app_backgrounded(bool v) { (void)v; }
static void mixer_set_window_focus(bool v) { (void)v; }
static void clear_active_touch_fingers(void) { cleared_touch_fingers++; }
static void release_long_touch_ctrl(void) {}
static void reset_gamepad_transient_state(void) {}
static void reset_movie_hold_state(void) {}
''' + '\n'.join([
            function('src/input.c', 'on_render_device_reset'),
            function('src/input.c', 'finish_event_drain'),
            function('src/input.c', 'handle_window_event'),
        ]) + r'''
int main(void) {
    bool did_swap = false, returned_fg = false;
    SDL_Event event = {.window.event = SDL_WINDOWEVENT_RESTORED};
    handle_window_event(&event, &did_swap, &returned_fg);
    assert(restored == 1 && foreground == 1 && swaps == 0 && traces == 1 && did_swap && returned_fg);

    event.window.event = SDL_WINDOWEVENT_EXPOSED;
    handle_window_event(&event, &did_swap, &returned_fg);
    assert(restored == 1 && swaps == 1 && traces == 1);

    // SDL_RENDER_DEVICE_RESET invokes GL recovery and restores when needed.
    did_swap = false;
    next_recover_result = true;
    on_render_device_reset(&did_swap);
    assert(recovered == 1 && restored == 2 && did_swap);

    // Early foreground drain before GL context is ready must retain backups.
    mock_gl_ready = false;
    atomic_store(&android_redraw_requested, false);
    finish_event_drain(true, true);
    assert(backup_released == 0);

    // Pending nativeRequestRedraw presents once if no swap already ran, and backup frees in fg when GL is ready.
    mock_gl_ready = true;
    atomic_store(&android_redraw_requested, true);
    finish_event_drain(false, true);
    assert(restored == 3 && backup_released == 1 && !atomic_load(&android_redraw_requested));
    atomic_store(&android_redraw_requested, true);
    finish_event_drain(true, false);
    assert(restored == 3 && backup_released == 1);

    // Focus loss clears active touch tracking and synthetic warp state.
    multi_touch_active = true;
    two_finger_tap_candidate = true;
    two_finger_scrolled = true;
    synthetic_mouse_warped = true;
    event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    handle_window_event(&event, &did_swap, &returned_fg);
    assert(cleared_touch_fingers == 1 && !multi_touch_active &&
           !two_finger_tap_candidate && !two_finger_scrolled && !synthetic_mouse_warped);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c, exe = Path(tmp) / 'event.c', Path(tmp) / 'event'
            c.write_text(code)
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_update_screen_scale_ignores_zero_drawable(self):
        code = r'''
#include <assert.h>
struct { void *window; int w, h; struct { int x, y, w, h; } viewport; } sdl = {
    .w = 1280, .h = 720, .viewport = { 10, 20, 800, 450 }
};
static int mock_dw = 0, mock_dh = 0;
static void SDL_GL_GetDrawableSize(void *win, int *w, int *h) {
    (void)win; *w = mock_dw; *h = mock_dh;
}
''' + function('src/video.c', 'void gfx_update_screen_scale') + r'''
int main(void) {
    gfx_update_screen_scale();
    assert(sdl.viewport.w == 800 && sdl.viewport.h == 450);
    mock_dw = 1920; mock_dh = 0;
    gfx_update_screen_scale();
    assert(sdl.viewport.w == 800 && sdl.viewport.h == 450);
    mock_dw = 1920; mock_dh = 1080;
    gfx_update_screen_scale();
    assert(sdl.viewport.w == 1920 && sdl.viewport.h == 1080);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c, exe = Path(tmp) / 'scale.c', Path(tmp) / 'scale'
            c.write_text(code)
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_android_delay_processes_each_lifecycle_event_once(self):
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
typedef uint32_t Uint32;
typedef int64_t int64_t;
enum { SDL_PEEKEVENT = 1, SDL_APP_WILLENTERBACKGROUND = 0x103, SDL_APP_DIDENTERFOREGROUND = 0x106, VK_CONTROL = 17 };
typedef struct { Uint32 type; } SDL_Event;
static SDL_Event queue[32];
static int queue_len = 0;
static Uint32 fake_ticks = 0;
static int bg_calls = 0, fg_calls = 0, restore_calls = 0;
static bool kbd_ctrl_down, kbd_key_state[256], sdl_mouse_down[8], key_state[256];
static bool multi_touch_active, two_finger_tap_candidate, two_finger_scrolled, synthetic_mouse_warped;
static int touch_press_id = 0;
static int cleared_touch_fingers = 0;
static atomic_bool touch_ctrl_skip = false;
static bool s_android_in_background = false;
static int s_delay_will_bg_handled = 0, s_delay_did_fg_handled = 0;
static int64_t s_frame_sleep_ns = 0;
static int on_iter_hook = 0;
static bool sys_is_sdl_main_thread(void) { return true; }
static int64_t android_monotonic_ns(void) { return (int64_t)fake_ticks * 1000000LL; }
static Uint32 SDL_GetTicks(void) { return fake_ticks; }
static void SDL_Delay(Uint32 ms) { fake_ticks += ms; }
static void SDL_PumpEvents(void) {
    if (on_iter_hook == 1 && fake_ticks == 16) {
        queue[queue_len++] = (SDL_Event){ .type = SDL_APP_WILLENTERBACKGROUND };
    } else if (on_iter_hook == 1 && fake_ticks == 48) {
        queue[queue_len++] = (SDL_Event){ .type = SDL_APP_DIDENTERFOREGROUND };
    } else if (on_iter_hook == 2 && fake_ticks == 316) {
        queue[queue_len++] = (SDL_Event){ .type = SDL_APP_WILLENTERBACKGROUND };
        queue[queue_len++] = (SDL_Event){ .type = SDL_APP_DIDENTERFOREGROUND };
    }
}
static int SDL_PeepEvents(SDL_Event *events, int numevents, int action, Uint32 minType, Uint32 maxType) {
    assert(action == SDL_PEEKEVENT);
    int out = 0;
    for (int i = 0; i < queue_len && out < numevents; i++) {
        if (queue[i].type >= minType && queue[i].type <= maxType)
            events[out++] = queue[i];
    }
    return out;
}
static void gfx_android_on_background(void) { s_android_in_background = true; bg_calls++; }
static void gfx_android_on_foreground(void) { s_android_in_background = false; fg_calls++; }
static void gfx_restore_window(void) { restore_calls++; }
static void gfx_trace_resume(const char *p) { (void)p; }
static void clear_active_touch_fingers(void) { cleared_touch_fingers++; }
static void release_long_touch_ctrl(void) {}
static void reset_gamepad_transient_state(void) {}
static void reset_movie_hold_state(void) {}
static void mixer_set_app_backgrounded(bool v) { (void)v; }
static void savedata_flush_on_background(void) {}
''' + '\n'.join([
            function('src/video.c', 'bool gfx_android_consume_lifecycle_event'),
            function('src/video.c', 'void gfx_android_delay'),
            function('src/input.c', 'static void on_app_will_enter_background'),
            function('src/input.c', 'static void on_app_did_enter_foreground'),
        ]) + r'''
int main(void) {
    on_iter_hook = 1;
    gfx_android_delay(128);
    assert(bg_calls == 1 && fg_calls == 1 && restore_calls == 1);

    // Calling gfx_android_delay again before handle_events drains the queue must not re-fire.
    on_iter_hook = 0;
    gfx_android_delay(64);
    assert(bg_calls == 1 && fg_calls == 1 && restore_calls == 1);

    // Draining queued WILL + DID in input.c consumes them without re-running backup readback.
    bool returned_fg = false;
    assert(queue_len == 2);
    multi_touch_active = true;
    two_finger_tap_candidate = true;
    two_finger_scrolled = true;
    synthetic_mouse_warped = true;
    on_app_will_enter_background(&returned_fg);
    assert(!returned_fg && bg_calls == 1);
    assert(cleared_touch_fingers == 1 && !multi_touch_active &&
           !two_finger_tap_candidate && !two_finger_scrolled && !synthetic_mouse_warped);
    on_app_did_enter_foreground(&returned_fg);
    assert(returned_fg && fg_calls == 1 && s_delay_will_bg_handled == 0 && s_delay_did_fg_handled == 0);
    queue_len = 0;

    // A second lifecycle pair after draining fires cleanly once.
    fake_ticks = 300;
    on_iter_hook = 2;
    gfx_android_delay(64);
    assert(bg_calls == 2 && fg_calls == 2 && restore_calls == 2);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c, exe = Path(tmp) / 'delay.c', Path(tmp) / 'delay'
            c.write_text(code)
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-D__ANDROID__', '-Wall', '-Wextra', '-Werror',
                            str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_dungeon_reset_rebuilds_context_and_renderer(self):
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define min(a, b) ((a) < (b) ? (a) : (b))
typedef unsigned int GLuint;
typedef int GLint;
typedef unsigned int GLenum;
typedef size_t GLsizeiptr;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned short GLushort;
typedef uint8_t GLboolean;
enum {
    GL_ARRAY_BUFFER = 1, GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW, GL_FLOAT, GL_FALSE,
    GL_UNSIGNED_SHORT, GL_TEXTURE0, GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP,
    GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
    GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, GL_TEXTURE_CUBE_MAP_POSITIVE_X,
    GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
    GL_RGBA, GL_UNSIGNED_BYTE, GL_TEXTURE_MAG_FILTER, GL_TEXTURE_MIN_FILTER,
    GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R,
    GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR, GL_CLAMP_TO_EDGE,
    GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
    DTX_SKYBOX = 5, DTX_NR_CELL_TEXTURE_TYPES = 7, DRAW_FIELD = 3, DRAW_DUNGEON_14 = 2
};
typedef struct { uint8_t r, g, b, a; } SDL_Color;
typedef struct texture { GLuint handle; int w, h; bool opaque; uint32_t ctx_gen; } Texture;
struct shader { GLuint program, world_transform, view_transform, texture; GLint vertex_pos, vertex_uv; void (*prepare)(void*, void*); };
struct dtx { int nr_columns; };
struct polyobj { int nr_textures; };
struct dgn_cell { int walked; };
struct dgn { uint32_t size_x, size_y, size_z; struct dgn_cell *cells; };
static struct dgn_cell *dgn_cell_at(struct dgn *d, uint32_t x, uint32_t y, uint32_t z) {
    return d->cells ? &d->cells[(y * d->size_z + z) * d->size_x + x] : NULL;
}
struct cg { struct { int w, h; bool has_alpha; } metrics; uint8_t *pixels; };
static uint8_t dummy_cg_pixels[4] = { 255, 255, 255, 255 };
struct dungeon_map { Texture *textures; int nr_textures; };
struct vertex { float x, y, z, u, v; };
static GLuint next_id = 1000;
static int gl_delete_calls = 0;
static int map_init_calls = 0;
static int map_reveal_calls = 0;
static bool remap_succeeds = true;
static void glGenVertexArrays(int n, GLuint *p) { for (int i = 0; i < n; i++) p[i] = ++next_id; }
static void glGenBuffers(int n, GLuint *p) { for (int i = 0; i < n; i++) p[i] = ++next_id; }
static void glGenTextures(int n, GLuint *p) { for (int i = 0; i < n; i++) p[i] = ++next_id; }
static void glGenRenderbuffers(int n, GLuint *p) { for (int i = 0; i < n; i++) p[i] = ++next_id; }
static void glBindVertexArray(GLuint v) { (void)v; }
static void glBindBuffer(GLenum t, GLuint b) { (void)t; (void)b; }
static void glBufferData(GLenum t, GLsizeiptr s, const void *d, GLenum u) { (void)t; (void)s; (void)d; (void)u; }
static void glEnableVertexAttribArray(GLuint i) { (void)i; }
static void glVertexAttribPointer(GLuint i, GLint s, GLenum t, GLboolean n, GLsizei st, const void *p) { (void)i; (void)s; (void)t; (void)n; (void)st; (void)p; }
static void glActiveTexture(GLenum u) { (void)u; }
static int last_skybox_indices[6] = { -1, -1, -1, -1, -1, -1 };
static int skybox_load_pos = 0;
static bool in_cubemap_bind = false;
static void glBindTexture(GLenum t, GLuint tx) {
    if (t == GL_TEXTURE_CUBE_MAP) {
        in_cubemap_bind = (tx != 0);
        if (tx != 0) skybox_load_pos = 0;
    }
}
static void glTexImage2D(GLenum t, GLint l, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum fmt, GLenum tp, const void *px) { (void)t; (void)l; (void)ifmt; (void)w; (void)h; (void)b; (void)fmt; (void)tp; (void)px; }
static void glTexParameteri(GLenum t, GLenum p, GLint v) { (void)t; (void)p; (void)v; }
static void glBindRenderbuffer(GLenum t, GLuint r) { (void)t; (void)r; }
static void glRenderbufferStorage(GLenum t, GLenum f, GLsizei w, GLsizei h) { (void)t; (void)f; (void)w; (void)h; }
static GLint glGetUniformLocation(GLuint p, const char *n) { (void)p; (void)n; return 1; }
static void glUseProgram(GLuint p) { (void)p; }
static void glUniform1i(GLint l, GLint v) { (void)l; (void)v; }
static void glUniform2f(GLint l, GLfloat a, GLfloat b) { (void)l; (void)a; (void)b; }
static void gfx_load_shader(struct shader *s, const char *v, const char *f) { (void)v; (void)f; s->program = ++next_id; s->vertex_pos = 1; s->vertex_uv = 2; }
static void sys_warning(const char *fmt, ...) { (void)fmt; }
#define WARNING(...) sys_warning(__VA_ARGS__)
static void *xcalloc(size_t n, size_t s) { return calloc(n, s); }
static char *gamedir_path(const char *p) { return strdup(p); }
static struct cg *dtx_create_cg(struct dtx *d, int type, int idx) {
    (void)d;
    if (type == DTX_SKYBOX && in_cubemap_bind) {
        last_skybox_indices[skybox_load_pos % 6] = idx;
        skybox_load_pos++;
        struct cg *cg = calloc(1, sizeof(struct cg));
        cg->metrics.w = 1; cg->metrics.h = 1; cg->pixels = dummy_cg_pixels;
        return cg;
    }
    if (idx == 0) {
        struct cg *cg = calloc(1, sizeof(struct cg));
        cg->metrics.w = 1; cg->metrics.h = 1; cg->pixels = dummy_cg_pixels;
        return cg;
    }
    return NULL;
}
static void cg_free(struct cg *cg) { free(cg); }
static struct polyobj *polyobj_load(const char *p) { (void)p; struct polyobj *po = calloc(1, sizeof(*po)); po->nr_textures = 1; return po; }
static void polyobj_free(struct polyobj *p) { free(p); }
static struct cg *polyobj_get_texture(struct polyobj *p, int i) { (void)p; (void)i; return calloc(1, sizeof(struct cg)); }
static bool gfx_remap_texture(Texture *t) {
    if (!remap_succeeds) { t->handle = 0; return false; }
    t->handle = ++next_id; return true;
}
static void gfx_init_texture_rgba(Texture *t, int w, int h, SDL_Color c) { (void)c; t->handle = ++next_id; t->w = w; t->h = h; }
static GLuint *load_event_textures(int *n) { *n = 2; GLuint *tx = xcalloc(2, sizeof(GLuint)); glGenTextures(2, tx); return tx; }

struct skybox { struct shader shader; GLint proj_transform; GLuint vao, attr_buffer, index_buffer, texture; int nr_indices; int texture_set; };
''' + '\n'.join([
            function('src/dungeon/skybox.c', 'static void skybox_load_textures'),
            function('src/dungeon/skybox.c', 'static void skybox_init_gl'),
            function('src/dungeon/skybox.c', 'void skybox_set_texture_set'),
            function('src/dungeon/skybox.c', 'void skybox_reset_gl'),
        ]) + r'''
struct raster_shader { struct shader s; GLint amp, t; };
struct geometry { GLuint vao, attr_buffer; void *vertices; size_t vertices_size; int nr_vertices; GLenum mode; };
struct material { GLuint texture; bool opaque; };
struct mesh { struct geometry *geometry; int material; };
struct object3d { int nr_meshes; struct mesh *meshes; };
struct dungeon_renderer {
    int version; struct shader shader;
    GLint local_transform, proj_transform, alpha_mod, use_lightmap, light_texture, use_fog, uv_offset, uv_scale;
    struct geometry *wall_geometry, *door_left_geometry, *door_right_geometry, *roof_geometry, *stairs_geometry, *floating_marker_geometry;
    struct material *materials; int nr_materials, nr_dtx_columns;
    GLuint *event_textures; int nr_event_textures; bool has_event_textures;
    int nr_polyobjs; struct object3d *polyobjs;
    int nr_polyobj_materials; struct material *polyobj_materials;
    struct skybox *skybox;
    struct raster_shader raster_shader; bool raster_scroll; float raster_amp;
};
struct dungeon_context {
    int version; struct dungeon_map *map; struct dgn *dgn; struct dtx *dtx;
    struct dungeon_renderer *renderer; Texture texture; GLuint depth_buffer;
};
static void dungeon_map_init(struct dungeon_context *ctx) { map_init_calls++; ctx->map->textures[0].handle = ++next_id; }
static void dungeon_map_reveal(struct dungeon_context *ctx, int x, int y, int z, bool redraw) { (void)ctx; (void)x; (void)y; (void)z; (void)redraw; map_reveal_calls++; }
static void prepare_raster_shader(void *j, void *d) { (void)j; (void)d; }
static void load_raster_shader(struct raster_shader *s) { gfx_load_shader(&s->s, "", ""); (void)prepare_raster_shader; }
static void init_material(struct material *m, struct cg *cg) { (void)cg; glGenTextures(1, &m->texture); }
''' + '\n'.join([
            function('src/dungeon/renderer.c', 'static void geometry_init_gl'),
            function('src/dungeon/renderer.c', 'static void load_polyobj_materials'),
            function('src/dungeon/renderer.c', 'static void dungeon_renderer_load_shader'),
            function('src/dungeon/renderer.c', 'static void load_dtx_materials'),
            function('src/dungeon/renderer.c', 'bool dungeon_renderer_has_event_textures'),
            function('src/dungeon/renderer.c', 'void dungeon_renderer_reset_gl'),
            function('src/dungeon/map.c', 'void dungeon_map_on_gl_reset'),
            function('src/dungeon/dungeon.c', 'static void init_depth_buffer'),
            function('src/dungeon/dungeon.c', 'static void dungeon_context_on_gl_reset'),
        ]) + r'''
int main(void) {
    struct vertex dummy_verts[2] = { {0, 0, 0, 0, 0}, {1, 0, 0, 0, 0} };
    struct geometry g_wall = { .vao = 11, .attr_buffer = 12, .vertices = dummy_verts, .vertices_size = sizeof(dummy_verts) };
    struct geometry g_poly = { .vao = 13, .attr_buffer = 14, .vertices = dummy_verts, .vertices_size = sizeof(dummy_verts) };
    struct mesh poly_mesh = { .geometry = &g_poly, .material = 0 };
    struct object3d polyobj = { .nr_meshes = 1, .meshes = &poly_mesh };
    struct material mats[7] = { { .texture = 21, .opaque = true } };
    struct material pmats[1] = { { .texture = 22, .opaque = true } };
    GLuint *ev_tex = xcalloc(1, sizeof(GLuint)); ev_tex[0] = 23;
    struct skybox sb = { .shader.program = 31, .vao = 32, .attr_buffer = 33, .index_buffer = 34, .texture = 35 };
    struct dungeon_renderer r = {
        .version = DRAW_DUNGEON_14,
        .shader.program = 41, .raster_shader.s.program = 42, .raster_scroll = true,
        .wall_geometry = &g_wall, .materials = mats, .nr_materials = 7, .nr_dtx_columns = 1,
        .event_textures = ev_tex, .nr_event_textures = 1, .has_event_textures = true,
        .nr_polyobjs = 1, .polyobjs = &polyobj, .nr_polyobj_materials = 1, .polyobj_materials = pmats,
        .skybox = &sb
    };
    Texture map_tex = { .handle = 51, .w = 32, .h = 32 };
    struct dungeon_map map = { .textures = &map_tex, .nr_textures = 1 };
    struct dgn dgn = { .size_y = 1 };
    struct dtx dtx = { .nr_columns = 1 };
    struct dungeon_context ctx = {
        .version = DRAW_DUNGEON_14, .map = &map, .dgn = &dgn, .dtx = &dtx,
        .renderer = &r, .texture = { .handle = 61, .w = 640, .h = 480 }, .depth_buffer = 62
    };
    dungeon_context_on_gl_reset(&ctx);
    assert(gl_delete_calls == 0);
    assert(ctx.texture.handle >= 1000 && ctx.depth_buffer >= 1000);
    assert(map_tex.handle >= 1000 && map_init_calls == 0);
    assert(r.shader.program >= 1000 && r.raster_shader.s.program >= 1000);
    assert(g_wall.vao >= 1000 && g_wall.attr_buffer >= 1000);
    assert(g_poly.vao >= 1000 && g_poly.attr_buffer >= 1000);
    assert(mats[0].texture >= 1000 && pmats[0].texture >= 1000);
    assert(r.nr_event_textures == 2 && r.event_textures[0] >= 1000 && r.event_textures[1] >= 1000);
    assert(sb.shader.program >= 1000 && sb.vao >= 1000 && sb.attr_buffer >= 1000 && sb.index_buffer >= 1000 && sb.texture >= 1000);
    for (int i = 0; i < 6; i++)
        assert(last_skybox_indices[i] == i);

    // Switching skybox texture set (e.g. Rance VI Dungeon 16 set 1 -> DTX indices 6..11) persists across GL reset.
    skybox_set_texture_set(&sb, &dtx, 1);
    for (int i = 0; i < 6; i++)
        assert(last_skybox_indices[i] == 6 + i);

    // Unbacked reset re-initializes ctx->texture and ctx->map via dungeon_map_init cleanly.
    remap_succeeds = false;
    memset(last_skybox_indices, -1, sizeof(last_skybox_indices));
    dungeon_context_on_gl_reset(&ctx);
    assert(ctx.texture.handle >= 1000 && map_init_calls == 1 && map_tex.handle >= 1000);
    assert(sb.texture_set == 1);
    for (int i = 0; i < 6; i++)
        assert(last_skybox_indices[i] == 6 + i);
    free(r.event_textures);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c, exe = Path(tmp) / 'dungeon_reset.c', Path(tmp) / 'dungeon_reset'
            c.write_text(code)
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
                            str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_reign_reset_rebuilds_models_effects_and_ubos(self):
        code = r'''
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define MAX_BONES 308
#define RE_NR_BACK_CGS 2
enum RE_plugin_version { RE_REIGN_PLUGIN = 0, RE_TAPIR_PLUGIN = 1, RE_SEAL_PLUGIN = 2 };
typedef unsigned int GLuint;
typedef int GLint;
typedef unsigned int GLenum;
typedef size_t GLsizeiptr;
typedef int GLsizei;
typedef float mat3x4[3][4];
typedef struct texture { GLuint handle; int w, h; bool opaque; uint32_t ctx_gen; } Texture;
struct string { int size; char text[1]; };
struct archive { int dummy; };
struct archive_data { int dummy; };
struct ht_slot { union { char *key; int ikey; }; void *value; };
struct hash_table { struct ht_slot slots[4]; int count; };
struct pol_material_group { uint32_t nr_children; };
struct pol_mesh { char *name; uint32_t material; };
struct pol { uint32_t nr_materials; struct pol_material_group *materials; uint32_t nr_meshes; struct pol_mesh **meshes; };
struct mesh { char *name; GLuint vao, attr_buffer, index_buffer; int nr_vertices, nr_indices, material; };
struct material { int nr_color_maps; GLuint *color_maps; GLuint specular_map, alpha_map, light_map, normal_map, blend_texture; };
struct model { char *path; int nr_meshes; struct mesh *meshes; int nr_materials; struct material *materials; int nr_bones; uint8_t sphere_color[4]; };
struct billboard_texture { GLuint texture; bool has_alpha; };
struct pae_object { struct model *model; };
struct pae { char *path; struct hash_table *textures; int nr_objects; struct pae_object *objects; };
struct pae_tex_reset_ctx { struct pae *pae; struct archive *aar; };
struct s3de_object { struct model *model; };
struct s3de { char *path; struct hash_table *textures; int nr_objects; struct s3de_object *objects; };
struct s3de_tex_reset_ctx { struct s3de *s3de; struct archive *aar; };
struct RE_back_cg { int no; struct string *name; Texture texture; };
struct RE_renderer { int dummy; };
struct RE_instance { struct model *model; mat3x4 *bone_transforms; GLuint bone_transforms_texture; bool bones_dirty; struct RE_instance *shadow_volume_instance; };
struct RE_plugin {
    enum RE_plugin_version version; struct archive *aar; struct hash_table *model_cache; struct hash_table *effect_cache;
    struct RE_instance **instances; int nr_instances;
    struct RE_back_cg back_cg[RE_NR_BACK_CGS]; struct RE_renderer *renderer;
};
enum {
    GL_UNIFORM_BUFFER = 1, GL_DYNAMIC_DRAW, GL_TEXTURE_2D, GL_TEXTURE0,
    GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER, GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T,
    GL_TEXTURE_BASE_LEVEL, GL_TEXTURE_MAX_LEVEL, GL_NEAREST, GL_CLAMP_TO_EDGE,
    GL_RGBA, GL_RGBA32F, GL_UNSIGNED_BYTE, GL_FLOAT
};
static GLuint next_id = 2000;
static int gl_delete_calls = 0;
static int billboard_path_reloads = 0, back_cg_no_reloads = 0, back_cg_name_reloads = 0;
static int pol_mat_reloads = 0, pol_mesh_resets = 0;
static void *xmalloc(size_t n) { return malloc(n ? n : 1); }
static void glGenTextures(int n, GLuint *p) { for (int i = 0; i < n; i++) p[i] = ++next_id; }
static void glActiveTexture(GLenum t) { (void)t; }
static void glBindTexture(GLenum t, GLuint tx) { (void)t; (void)tx; }
static void glTexParameteri(GLenum t, GLenum p, GLint v) { (void)t; (void)p; (void)v; }
static void glTexImage2D(GLenum t, GLint l, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum fmt, GLenum tp, const void *px) { (void)t; (void)l; (void)ifmt; (void)w; (void)h; (void)b; (void)fmt; (void)tp; (void)px; }
static void glTexSubImage2D(GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum tp, const void *px) { (void)t; (void)l; (void)x; (void)y; (void)w; (void)h; (void)fmt; (void)tp; (void)px; }
static void ht_foreach(struct hash_table *ht, void(*fun)(struct ht_slot*, void*), void *data) {
    for (int i = 0; i < ht->count; i++) fun(&ht->slots[i], data);
}
static struct string *string_ref(struct string *s) { return s; }
static void free_string(struct string *s) { (void)s; }
static bool gfx_remap_texture(Texture *t) { (void)t; return false; }
static void RE_renderer_reload_billboard_textures_by_path(struct RE_renderer *r, struct archive *aar) { (void)r; (void)aar; billboard_path_reloads++; }
static bool RE_back_cg_set(struct RE_back_cg *bcg, int no) { (void)no; back_cg_no_reloads++; bcg->texture.handle = ++next_id; return true; }
static bool RE_back_cg_set_name(struct RE_back_cg *bcg, struct string *n, struct archive *aar) { (void)n; (void)aar; back_cg_name_reloads++; bcg->texture.handle = ++next_id; return true; }
static void init_sphere_mesh(struct mesh *m) { m->vao = ++next_id; m->attr_buffer = ++next_id; m->index_buffer = ++next_id; }
static GLuint RE_load_aar_texture(struct archive *a, const char *d, const char *n, const char **e, int ne, bool *ha) {
    (void)a; (void)d; (void)n; (void)e; (void)ne; if (ha) *ha = true; return ++next_id;
}
static struct pol_material_group fake_pol_mat_group = { .nr_children = 0 };
static struct pol_mesh fake_pol_mesh = { .name = "body", .material = 0 };
static struct pol_mesh *fake_pol_meshes[1] = { &fake_pol_mesh };
static struct pol fake_pol = { .nr_materials = 1, .materials = &fake_pol_mat_group, .nr_meshes = 1, .meshes = fake_pol_meshes };
static struct pol *load_pol_with_opr(struct archive *a, const char *p) { (void)a; return strcmp(p, "HERO") == 0 ? &fake_pol : NULL; }
static void pol_free(struct pol *p) { (void)p; }
static void load_model_materials_from_pol(struct model *m, struct pol *p, void *amt, struct archive *a, const char *path, enum RE_plugin_version v, const int *o, bool r) {
    (void)p; (void)amt; (void)a; (void)path; (void)v; (void)o; (void)r;
    pol_mat_reloads++;
    m->materials[0].color_maps = xmalloc(sizeof(GLuint));
    m->materials[0].nr_color_maps = 1;
    m->materials[0].color_maps[0] = ++next_id;
}
static void reset_pol_mesh_gl(struct model *m, struct pol_mesh *pm, uint32_t gi, int mat, int *mi) {
    (void)pm; (void)gi; (void)mat;
    pol_mesh_resets++;
    m->meshes[*mi].vao = ++next_id;
    m->meshes[*mi].attr_buffer = ++next_id;
    (*mi)++;
}
''' + '\n'.join([
            function('src/3d/model.c', 'static void clear_material_gl_handles'),
            function('src/3d/model.c', 'static void clear_mesh_gl_handles'),
            function('src/3d/model.c', 'static void init_sphere_material_gl'),
            function('src/3d/model.c', 'void model_reset_gl'),
            function('src/3d/particle.c', 'static void reset_pae_texture_cb'),
            function('src/3d/particle.c', 'void pae_reset_gl'),
            function('src/3d/s3de.c', 'static void reset_s3de_texture_cb'),
            function('src/3d/s3de.c', 'void s3de_reset_gl'),
            function('src/3d/reign.c', 'static void init_bone_transforms_identity'),
            function('src/3d/reign.c', 'static void init_instance_bone_texture'),
            function('src/3d/reign.c', 'static void reset_cached_model_cb'),
            function('src/3d/reign.c', 'static void reset_cached_effect_cb'),
            function('src/3d/reign.c', 'static void reset_instance_gl'),
            function('src/3d/reign.c', 'static void re_plugin_on_gl_reset'),
        ]) + r'''
int main(void) {
    GLuint *cm = xmalloc(sizeof(GLuint)); cm[0] = 91;
    struct mesh sphere_mesh = { .vao = 71, .attr_buffer = 72, .index_buffer = 73 };
    struct material sphere_mat = { .nr_color_maps = 1, .color_maps = cm };
    struct model sphere = { .path = NULL, .nr_meshes = 1, .meshes = &sphere_mesh, .nr_materials = 1, .materials = &sphere_mat, .sphere_color = {255,0,0,64} };
    GLuint *hm_cm = xmalloc(sizeof(GLuint)); hm_cm[0] = 95;
    struct mesh pol_mesh = { .vao = 74, .attr_buffer = 75 };
    struct material pol_mat = { .nr_color_maps = 1, .color_maps = hm_cm };
    struct model pol_model = { .path = "HERO", .nr_meshes = 1, .meshes = &pol_mesh, .nr_materials = 1, .materials = &pol_mat };
    struct billboard_texture pae_bt = { .texture = 81 };
    struct hash_table pae_ht = { .slots = { { .key = "fx", .value = &pae_bt } }, .count = 1 };
    struct pae_object pae_obj = { .model = &sphere };
    struct pae pae = { .path = "FX", .textures = &pae_ht, .nr_objects = 1, .objects = &pae_obj };
    struct billboard_texture s3de_bt = { .texture = 82 };
    struct hash_table s3de_ht = { .slots = { { .key = "s3", .value = &s3de_bt } }, .count = 1 };
    struct s3de_object s3de_obj = { .model = &sphere };
    struct s3de s3de = { .path = "S3", .textures = &s3de_ht, .nr_objects = 1, .objects = &s3de_obj };

    mat3x4 bone_buf;
    struct RE_instance shvol = { .model = &sphere };
    struct RE_instance inst = { .bone_transforms = &bone_buf, .bone_transforms_texture = 92, .shadow_volume_instance = &shvol };
    struct RE_instance *inst_arr[1] = { &inst };
    struct hash_table model_ht = { .slots = { { .key = "m0", .value = &sphere }, { .key = "m1", .value = &pol_model } }, .count = 2 };
    struct hash_table effect_ht = { .slots = { { .key = "e", .value = &pae } }, .count = 1 };
    struct archive dummy_aar = {0};
    struct RE_renderer dummy_r = {0};
    struct string sname = { .size = 2 };
    struct RE_plugin plugin = {
        .version = RE_REIGN_PLUGIN, .aar = &dummy_aar, .model_cache = &model_ht, .effect_cache = &effect_ht,
        .instances = inst_arr, .nr_instances = 1, .renderer = &dummy_r,
        .back_cg = { { .no = 5, .texture.handle = 93 }, { .name = &sname, .texture.handle = 94 } }
    };
    re_plugin_on_gl_reset(&plugin);
    assert(gl_delete_calls == 0);
    assert(sphere_mesh.vao >= 2000 && sphere_mesh.attr_buffer >= 2000 && sphere_mesh.index_buffer >= 2000);
    assert(sphere_mat.color_maps && sphere_mat.color_maps[0] >= 2000);
    assert(pol_mat_reloads == 1 && pol_mesh_resets == 1);
    assert(pol_mesh.vao >= 2000 && pol_mesh.attr_buffer >= 2000 && pol_mat.color_maps && pol_mat.color_maps[0] >= 2000);
    assert(inst.bone_transforms_texture >= 2000);
    assert(pae_bt.texture >= 2000);
    assert(billboard_path_reloads == 1 && back_cg_no_reloads == 1 && back_cg_name_reloads == 1);

    effect_ht.slots[0].value = &s3de;
    plugin.version = RE_SEAL_PLUGIN;
    re_plugin_on_gl_reset(&plugin);
    assert(s3de_bt.texture >= 2000);
    free(sphere_mat.color_maps);
    free(pol_mat.color_maps);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c, exe = Path(tmp) / 'reign_reset.c', Path(tmp) / 'reign_reset'
            c.write_text(code)
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_effect_init_registers_gl_reset_callback(self):
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef unsigned int GLuint;
typedef int GLint;
typedef struct texture { GLuint handle; int w, h; bool opaque; unsigned int ctx_gen; } Texture;
struct shader { GLuint program; void (*prepare)(void *, void *); };
struct effect_shader { struct shader s; const char *f_path; GLint old, resolution, progress; };
typedef void (*gfx_reset_callback)(void *);
enum effect { EFFECT_CROSSFADE = 1, EFFECT_BLUR_CROSSFADE = 2, NR_EFFECTS = 3 };
static const char *effect_names[NR_EFFECTS] = { NULL, "CROSSFADE", "BLUR_CROSSFADE" };
static void (*effect_functions[NR_EFFECTS])(Texture *, Texture *, Texture *, float) = {0};
static struct effect_shader crossfade_shader = { .f_path = "cf" };
static struct effect_shader blur_crossfade_shader = { .f_path = "bcf" };
static struct effect_shader *effect_shaders[NR_EFFECTS] = {
    [EFFECT_CROSSFADE] = &crossfade_shader,
    [EFFECT_BLUR_CROSSFADE] = &blur_crossfade_shader,
};
static struct { bool on; enum effect type; Texture old, view; } effect = {0};
static bool effect_reset_registered = false;
static gfx_reset_callback registered_cb = NULL;
static int register_calls = 0, shader_loads = 0, remap_calls = 0;
static GLuint next_prog = 500;
static void sys_warning(const char *fmt, ...) { (void)fmt; }
#define WARNING(...) sys_warning(__VA_ARGS__)
static void gfx_register_reset_callback(gfx_reset_callback cb, void *user) { (void)user; registered_cb = cb; register_calls++; }
static void load_effect_shader(struct effect_shader *s) { s->s.program = ++next_prog; shader_loads++; }
static bool gfx_remap_texture(Texture *t) {
    remap_calls++;
    if (t->handle && t->handle < 1000) t->handle += 1000;
    return t->handle != 0;
}
static void gfx_delete_texture(Texture *t) { t->handle = 0; }
static void gfx_copy_main_surface(Texture *dst) { static GLuint h = 10; dst->handle = ++h; dst->w = 64; dst->h = 64; }
static void gfx_set_view(Texture *t) { (void)t; }
static void gfx_reset_view(void) {}
''' + '\n'.join([
            function('src/effect.c', 'static void effect_on_gl_reset'),
            function('src/effect.c', 'static void ensure_effect_reset_registered'),
            function('src/effect.c', 'int effect_init'),
            function('src/effect.c', 'int effect_fini'),
            function('src/effect.c', 'void effect_end_session'),
        ]) + r'''
int main(void) {
    assert(effect_init(EFFECT_CROSSFADE) == 1);
    assert(effect_reset_registered && registered_cb == effect_on_gl_reset && register_calls == 1);
    assert(shader_loads == 1 && crossfade_shader.s.program == 501);
    GLuint orig_old = effect.old.handle, orig_view = effect.view.handle;

    // Context reset clears cached shader program and remaps active effect textures.
    registered_cb(NULL);
    assert(crossfade_shader.s.program == 0);
    assert(effect.old.handle == orig_old + 1000);
    assert(effect.view.handle == orig_view + 1000);

    // Next effect_init lazily reloads the shader program cleanly and does not duplicate registration.
    assert(effect_init(EFFECT_CROSSFADE) == 1);
    assert(register_calls == 1 && shader_loads == 2 && crossfade_shader.s.program == 502);

    effect_end_session();
    assert(!effect.on && !effect.old.handle && !effect.view.handle);
    assert(!effect_reset_registered && crossfade_shader.s.program == 0);
    assert(effect_init(EFFECT_CROSSFADE) == 1);
    assert(register_calls == 2 && shader_loads == 3 && crossfade_shader.s.program == 503);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c, exe = Path(tmp) / 'effect_reset.c', Path(tmp) / 'effect_reset'
            c.write_text(code)
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_vmgraph_copy_stretch_remaps_source_before_filter(self):
        code = r'''
#include <assert.h>
#include <stdbool.h>
typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef int GLint;
enum { GL_TEXTURE_2D = 1, GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER, GL_NEAREST, GL_LINEAR };
typedef struct texture { GLuint handle; int w, h; bool opaque; unsigned int ctx_gen; } Texture;
struct vm_surface { Texture texture; };
static struct vm_surface dst_surf = { .texture = { .handle = 11, .ctx_gen = 1 } };
static struct vm_surface src_surf = { .texture = { .handle = 17, .ctx_gen = 1 } };
static struct vm_surface *vm_surface_get(int no) {
    if (no == 1) return &dst_surf;
    if (no == 2) return &src_surf;
    return 0;
}
static GLuint bound_tex = 0;
static int near_binds_on_remapped = 0, lin_binds_on_remapped = 0, copy_calls = 0;
static void glBindTexture(GLenum target, GLuint tex) {
    assert(target == GL_TEXTURE_2D);
    if (tex != 0) assert(tex == 402 || tex == 401);
    bound_tex = tex;
}
static void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    assert(target == GL_TEXTURE_2D);
    (void)pname;
    if (bound_tex == 402 && param == GL_NEAREST) near_binds_on_remapped++;
    if (bound_tex == 402 && param == GL_LINEAR) lin_binds_on_remapped++;
}
static bool gfx_remap_texture(Texture *t) {
    if (t == &dst_surf.texture) t->handle = 401;
    if (t == &src_surf.texture) t->handle = 402;
    t->ctx_gen = 2;
    return true;
}
static void gfx_copy_stretch(Texture *d, int dx, int dy, int dw, int dh, Texture *s, float sx, float sy, float sw, float sh) {
    (void)dx; (void)dy; (void)dw; (void)dh; (void)sx; (void)sy; (void)sw; (void)sh;
    assert(d->handle == 401 && s->handle == 402);
    assert(near_binds_on_remapped == 2 && lin_binds_on_remapped == 0);
    copy_calls++;
}
''' + function('src/hll/vmGraph.c', 'static void vmGraph_CopyStretch') + r'''
int main(void) {
    vmGraph_CopyStretch(1, 0, 0, 10, 10, 2, 0.f, 0.f, 5.f, 5.f);
    assert(copy_calls == 1 && near_binds_on_remapped == 2 && lin_binds_on_remapped == 2);
    assert(bound_tex == 0);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c, exe = Path(tmp) / 'vmgraph.c', Path(tmp) / 'vmgraph'
            c.write_text(code)
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_save_flags_have_version_gate(self):
        save = (ROOT / 'src/parts/save.c').read_text()
        self.assertIn('#define CURRENT_SAVE_VERSION 36', save)
        for axis in ('lr', 'tb'):
            self.assertIn(f'iarray_write(w, parts->reverse_{axis});', save)
            self.assertIn(f'parts->reverse_{axis} = version >= 32 ? !!iarray_read(r) : false;', save)

    def test_rance6_draw_font_and_hll_helpers(self):
        code = r'''
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int GLenum;
typedef float mat4[4][4];
enum { GL_ZERO = 0, GL_ONE = 1, FONT_GOTHIC = 0, FONT_MINCHO = 1 };

typedef struct texture { int w, h; } Texture;
struct copy_data { int dx, dy, sx, sy, w, h; };
struct shader { int dummy; };
static struct { struct shader s; } copy_shader;
#define MAT4(m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33) \
    { {m00, m10, m20, m30}, {m01, m11, m21, m31}, {m02, m12, m22, m32}, {m03, m13, m23, m33} }
#define WV_TRANSFORM(w, h) { {w, 0, 0, 0}, {0, h, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1} }
#define COPY_DATA(_dx, _dy, _sx, _sy, _w, _h) { _dx, _dy, _sx, _sy, _w, _h }

static GLenum last_blend[4];
static float last_m11 = 0.f, last_m31 = 0.f, last_m00 = 0.f, last_m30 = 0.f;
static int restore_calls = 0;
static void glBlendFuncSeparate(GLenum sr, GLenum dr, GLenum sa, GLenum da) {
    last_blend[0] = sr; last_blend[1] = dr; last_blend[2] = sa; last_blend[3] = da;
}
static void run_draw_shader(struct shader *s, Texture *dst, Texture *src, mat4 mw, mat4 wv, struct copy_data *d) {
    (void)s; (void)dst; (void)src; (void)wv; (void)d;
    last_m00 = mw[0][0]; last_m30 = mw[3][0];
    last_m11 = mw[1][1]; last_m31 = mw[3][1];
}
static void restore_blend_mode(void) { restore_calls++; }

static bool sys4_text_is_gbk = false;
static struct { int face; } font_metrics;
static int font_warnings = 0;
static const char *display_sjis0(const char *s) { return s; }
static void sys_warning(const char *fmt, ...) { (void)fmt; font_warnings++; }
#define WARNING(...) sys_warning(__VA_ARGS__)

struct dungeon_renderer { int dummy; };
struct dtx { int dummy; };
struct dungeon_context { bool loaded; struct dungeon_renderer *renderer; struct dtx *dtx; };
static struct dungeon_context mock_dgn_ctx;
static struct dungeon_context *dungeon_get_context(int surface) {
    return surface == 1 ? &mock_dgn_ctx : NULL;
}
static int last_sky_set = -1;
static void dungeon_renderer_set_sky_texture_set(struct dungeon_renderer *r, struct dtx *dtx, int set) {
    (void)r; (void)dtx;
    last_sky_set = set;
}

struct string { int size; char *text; };
static char *last_wav_path = NULL;
static char *gamedir_path_icase(const char *p) { return strcmp(p, "hit.wav") == 0 ? strdup("/game/HIT.WAV") : NULL; }
static char *gamedir_path(const char *p) { (void)p; return strdup("/game/fallback.wav"); }
static int wav_prepare_from_file(int ch, char *path) {
    (void)ch;
    free(last_wav_path);
    last_wav_path = strdup(path);
    return 1;
}
''' + '\n'.join([
            function('src/draw.c', 'void gfx_copy_reverse_amap_UD'),
            function('src/text.c', 'void gfx_set_font_name'),
            function('src/hll/DrawDungeon.c', 'static float DrawDungeon_GetLoadingPercent'),
            function('src/hll/DrawDungeon.c', 'static void DrawDungeon_SetSkyTextureSet'),
            function('src/hll/SACT2.c', 'static int sact_Sound_PrepareFromFile'),
        ]) + r'''
int main(void) {
    Texture dst = { 64, 64 }, src = { 32, 48 };
    gfx_copy_reverse_amap_UD(&dst, 4, 6, &src, 2, 10, 16, 20);
    assert(last_blend[0] == GL_ZERO && last_blend[1] == GL_ONE);
    assert(last_blend[2] == GL_ONE && last_blend[3] == GL_ZERO);
    assert(last_m00 == 32.f && last_m30 == -2.f);
    assert(last_m11 == -48.f && last_m31 == 30.f);
    assert(restore_calls == 1);

    // Font names in Shift-JIS and GBK (both v1.10 黑体/宋体 and v0.90 GBK MS Gothic/Mincho)
    sys4_text_is_gbk = true;
    font_metrics.face = -1;
    gfx_set_font_name("\xba\xda\xcc\xe5");
    assert(font_metrics.face == FONT_GOTHIC && font_warnings == 0);
    gfx_set_font_name("\xcb\xce\xcc\xe5");
    assert(font_metrics.face == FONT_MINCHO && font_warnings == 0);
    gfx_set_font_name("\xa3\xcd\xa3\xd3\x20\xa5\xb4\xa5\xb7\xa5\xc3\xa5\xaf");
    assert(font_metrics.face == FONT_GOTHIC && font_warnings == 0);
    gfx_set_font_name("\xa3\xcd\xa3\xd3\x20\xc3\xf7\xb3\xaf");
    assert(font_metrics.face == FONT_MINCHO && font_warnings == 0);

    // DrawDungeon helpers
    mock_dgn_ctx.loaded = false;
    assert(DrawDungeon_GetLoadingPercent(0) == 0.0f);
    assert(DrawDungeon_GetLoadingPercent(1) == 0.0f);
    mock_dgn_ctx.loaded = true;
    assert(DrawDungeon_GetLoadingPercent(1) == 100.0f);

    struct dungeon_renderer dr = {0};
    struct dtx dtx = {0};
    mock_dgn_ctx.renderer = &dr;
    mock_dgn_ctx.dtx = &dtx;
    DrawDungeon_SetSkyTextureSet(1, 1);
    assert(last_sky_set == 1);

    // SACT2 Sound_PrepareFromFile
    struct string empty_s = { 0, "" }, hit_s = { 7, "hit.wav" };
    assert(sact_Sound_PrepareFromFile(3, NULL) == 0);
    assert(sact_Sound_PrepareFromFile(3, &empty_s) == 0);
    assert(sact_Sound_PrepareFromFile(3, &hit_s) == 1);
    assert(last_wav_path && strcmp(last_wav_path, "/game/HIT.WAV") == 0);
    free(last_wav_path);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c, exe = Path(tmp) / 'r06_helpers.c', Path(tmp) / 'r06_helpers'
            c.write_text(code)
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
                            str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


class WidgetTextureRecoveryTests(unittest.TestCase):
    def test_gl_reset_restores_all_cg_skins_and_keeps_suspended_skins_unloaded(self):
        source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))
#define xmalloc malloc
enum { PARTS_UNINITIALIZED, PARTS_CG, PARTS_FLAT, PARTS_NR_STATES = 3 };
enum { PARTS_STATE_DEFAULT, PARTS_STATE_HOVERED, PARTS_STATE_CLICKED };
struct texture { int handle, w; };
struct rect { int x, y, w, h; };
struct common { struct texture texture; int w, h; struct rect surface_area, hitbox; };
struct string { int size; const char *text; };
struct cg { int id; };
struct parts_cg { struct common common; int no; struct string *name; };
struct parts_text_char { struct texture t, t_edge; };
struct parts_text_line { int nr_chars; struct parts_text_char *chars; };
struct parts_text { struct common common; unsigned nr_lines; struct parts_text_line *lines; };
struct parts_flat { int unused; };
struct parts_state {
    int type;
    union {
        struct common common;
        struct parts_cg cg;
        struct parts_text text;
        struct parts_flat flat;
    };
};
struct parts_widget {
    struct parts_state disabled, checked_disabled, checked_states[PARTS_NR_STATES], label;
    int color[3];
    bool button_mode;
};
struct parts {
    struct parts_widget *widget;
    struct parts_state states[PARTS_NR_STATES];
    bool textures_suspended;
};
static int loaded, labels_rebuilt;
static bool gfx_remap_texture(struct texture *t) {
    if (t->handle == -1)
        t->handle = 0;
    return t->handle != 0;
}
static struct cg *asset_cg_load(int no) {
    static struct cg cg;
    cg.id = no;
    loaded++;
    return &cg;
}
static struct cg *asset_cg_load_by_name(const char *name, int *no) {
    assert(name[0] == 'd');
    return asset_cg_load(*no = 99);
}
static void gfx_init_texture_with_cg(struct texture *t, struct cg *cg) { t->handle = cg->id; }
static void cg_free(struct cg *cg) { assert(cg); }
static void parts_flat_recover_widget_skin(struct parts_flat *f, bool suspended) {
    (void)f; (void)suspended; assert(false);
}
static void gfx_init_texture_with_pixels(struct texture *t, int w, int h, void *pixels) {
    assert(!t->handle && w == 44 && h == 30 && pixels);
    t->handle = 200;
}
static void parts_widget_rebuild_label(struct parts *p) { assert(p->widget); labels_rebuilt++; }
''' + "\n".join(function("src/parts/widgets.c", signature) for signature in (
            "static void fill_rgba_rect",
            "void parts_widget_restore_plain_skin",
        )) + "\n" + "\n".join(function("src/parts/parts.c", signature) for signature in (
            "static void recover_cg_state",
            "static void recover_widget_skin",
            "static bool recover_text_state",
            "static void recover_widget_states",
        )) + r'''
int main(void) {
    struct string disabled = {1, "disabled"};
    struct parts_text_char ch = {.t = {-1, 8}, .t_edge = {7, 8}};
    struct parts_text_line line = {1, &ch};
    struct parts_widget w = {
        .disabled = {.type = PARTS_CG, .cg = {.common.texture = {-1, 44}, .name = &disabled}},
        .checked_disabled = {.type = PARTS_CG, .cg = {.common.texture = {-1, 44}, .no = 10}},
        .label.text = {.nr_lines = 1, .lines = &line},
    };
    struct parts p = {.widget = &w};
    for (int i = 0; i < PARTS_NR_STATES; i++) {
        p.states[i] = (struct parts_state){.type = PARTS_CG, .cg = {.common.texture = {-1, 44}, .no = i+1}};
        w.checked_states[i] = (struct parts_state){.type = PARTS_CG, .cg = {.common.texture = {-1, 44}, .no = i+4}};
    }
    p.states[1].common.texture.handle = 42;
    w.checked_states[2].cg.no = 0;
    struct common *plain = &w.checked_states[2].common;
    plain->w = 44;
    plain->h = 30;
    plain->surface_area = (struct rect){2, 3, 20, 10};
    plain->hitbox = (struct rect){5, 6, 20, 10};
    recover_widget_states(&p);
    assert(p.states[0].common.texture.handle == 1);
    assert(p.states[1].common.texture.handle == 42);
    assert(p.states[2].common.texture.handle == 3);
    assert(w.checked_states[0].common.texture.handle == 4);
    assert(w.checked_states[1].common.texture.handle == 5);
    assert(w.checked_states[2].common.texture.handle == 200);
    assert(plain->surface_area.x == 2 && plain->surface_area.y == 3);
    assert(plain->surface_area.w == 20 && plain->surface_area.h == 10);
    assert(plain->hitbox.x == 5 && plain->hitbox.y == 6);
    assert(plain->hitbox.w == 20 && plain->hitbox.h == 10);
    assert(w.disabled.common.texture.handle == 99);
    assert(w.checked_disabled.common.texture.handle == 10);
    assert(loaded == 6 && labels_rebuilt == 1);
    assert(!ch.t.handle && ch.t_edge.handle == 7);
    p.textures_suspended = true;
    p.states[0].common.texture.handle = 0;
    w.disabled.common.texture.handle = 0;
    recover_widget_states(&p);
    assert(!p.states[0].common.texture.handle && !w.disabled.common.texture.handle);
    assert(loaded == 6 && labels_rebuilt == 1);
}
'''
        compile_and_run(source)


class DrawFieldLockTests(unittest.TestCase):
    def test_door_lock_returns_success_only_when_door_exists(self):
        source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
struct dgn_cell {
    int north_door, east_door, south_door, west_door;
    int north_door_lock, east_door_lock, south_door_lock, west_door_lock;
};
struct dgn { struct dgn_cell cell; };
struct dungeon_context { struct dgn *dgn; };
static struct dgn map;
static struct dungeon_context ctx = {&map};
static int updates;
static struct dungeon_context *dungeon_get_context(int surface) { return surface == 1 ? &ctx : NULL; }
static bool dgn_is_in_map(struct dgn *dgn, int x, int y, int z) {
    return dgn == &map && x == 0 && y == 0 && z == 0;
}
static struct dgn_cell *dgn_cell_at(struct dgn *dgn, int x, int y, int z) {
    assert(dgn_is_in_map(dgn, x, y, z));
    return &dgn->cell;
}
static void dungeon_map_update_cell(struct dungeon_context *c, int x, int y, int z) {
    assert(c == &ctx && dgn_is_in_map(c->dgn, x, y, z));
    updates++;
}
'''
        source += "\n".join(function(
            "src/hll/DrawDungeon.c", f"static bool DrawField_SetDoor{direction}Lock"
        ) for direction in "NESW")
        source += r'''
int main(void) {
    bool (*setters[])(int, int, int, int, int) = {
        DrawField_SetDoorNLock, DrawField_SetDoorELock, DrawField_SetDoorSLock, DrawField_SetDoorWLock
    };
    struct dgn_cell *c = &map.cell;
    int *doors[] = {&c->north_door, &c->east_door, &c->south_door, &c->west_door};
    int *locks[] = {&c->north_door_lock, &c->east_door_lock, &c->south_door_lock, &c->west_door_lock};
    for (int i = 0; i < 4; i++) {
        *doors[i] = -1;
        assert(!setters[i](1, 0, 0, 0, 3) && *locks[i] == 0 && updates == i);
        *doors[i] = 1;
        assert(!setters[i](0, 0, 0, 0, 3) && !setters[i](1, -1, 0, 0, 3) && updates == i);
        assert(setters[i](1, 0, 0, 0, 3) && *locks[i] == 3 && updates == i + 1);
    }
    return 0;
}
'''
        compile_and_run(source)


if __name__ == '__main__':
    unittest.main()
