#!/usr/bin/env python3
"""Regression tests for dialogue box opacity/brightness, PartsEngine ABI fixes,
save v36 message_window_effect_link, PE_ReleaseAll_list, and parts_text_end_session.

Run: python3 xsystem4/test/native/test_msgwindow_opacity_regressions.py
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

LIBSYS4_CORE = [
    ROOT / "subprojects/libsys4/src/buffer.c",
    ROOT / "subprojects/libsys4/src/hashtable.c",
    ROOT / "subprojects/libsys4/src/string.c",
    ROOT / "subprojects/libsys4/src/system.c",
    ROOT / "subprojects/libsys4/src/utfsjis.c",
    ROOT / "subprojects/libsys4/src/file.c",
    ROOT / "subprojects/libsys4/src/ex.c",
    ROOT / "subprojects/libsys4/src/archive.c",
    ROOT / "subprojects/libsys4/src/afa.c",
    ROOT / "subprojects/libsys4/src/afa3.c",
    ROOT / "subprojects/libsys4/src/aar.c",
    ROOT / "subprojects/libsys4/src/zlib.c",
    ROOT / "subprojects/libsys4/src/ain.c",
    ROOT / "subprojects/libsys4/src/instructions.c",
    ROOT / "subprojects/libsys4/src/mt19937int.c",
]


def sdl_cflags() -> list[str]:
    return subprocess.check_output(["pkg-config", "--cflags", "sdl2"], text=True).split()


def compile_and_run(
    c_src: str,
    extra_sources: list[Path],
    extra_flags: list[str] | None = None,
    args: list[str] | None = None,
) -> str:
    cc = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
    assert cc, "C compiler required"
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        src_path = tmp / "harness.c"
        bin_path = tmp / "harness"
        src_path.write_text(c_src, encoding="utf-8")
        cmd = [
            cc,
            "-std=c11",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Wno-unused-parameter",
            "-Wno-unused-function",
            "-Wno-sign-compare",
            "-Wno-type-limits",
            "-D_DEFAULT_SOURCE",
            "-DUSE_GLES",
            f"-I{ROOT / 'include'}",
            f"-I{ROOT / 'src'}",
            f"-I{ROOT / 'src' / 'hll'}",
            f"-I{ROOT / 'src' / 'parts'}",
            f"-I{ROOT / 'subprojects' / 'libsys4' / 'include'}",
            *sdl_cflags(),
            str(src_path),
            *[str(p) for p in extra_sources],
            *(extra_flags or []),
            "-o",
            str(bin_path),
        ]
        res_c = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res_c.returncode != 0:
            raise AssertionError(f"Compile failed ({res_c.returncode}):\n{res_c.stderr}")
        res_r = subprocess.run(
            [str(bin_path), *(args or [])], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        if res_r.returncode != 0:
            raise AssertionError(
                f"Run failed ({res_r.returncode}):\nstdout:\n{res_r.stdout}\nstderr:\n{res_r.stderr}"
            )
        return res_r.stdout


def extract_c_fn(rel_path: str, name: str) -> str:
    source = (ROOT / rel_path).read_text(encoding="utf-8")
    match = re.search(
        r"^(?:static\s+)?[\w *]+\b" + re.escape(name) + r"\([^;]*?\)\s*\{",
        source,
        re.MULTILINE,
    )
    if not match:
        raise ValueError(f"Function {name} not found in {rel_path}")
    start = match.start()
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class MessageWindowOpacityAndPartsABITests(unittest.TestCase):
    def test_message_window_and_effect_link_render(self):
        harness = r"""
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cglm/cglm.h>

#include "system4.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "gfx/gfx.h"
#include "gfx/private.h"
#include "sact.h"
#include "scene.h"
#include "sprite.h"
#include "reign.h"
#include "xsystem4.h"
#include "parts.h"
#include "parts/parts_internal.h"

struct config config = { .view_width = 1280, .view_height = 720 };
struct sdl_private sdl = { .gl = { .vao = 1, .vbo = 2, .rect_ibo = 3 } };
bool game_okusama = false;
bool scene_is_dirty = false;
struct parts_context *parts_current = NULL;

struct draw_record {
    GLuint tex;
    float blend_rate;
    float mul[3];
};

static struct draw_record draws[32];
static int nr_draws = 0;
static GLuint cur_tex0 = 0;
static float cur_blend = 0.0f;
static float cur_mul[3] = {0};

static struct parts *last_cproc_build_parts = NULL;
static int cproc_build_calls = 0;
static GLuint next_cproc_tex = 500;

void gfx_load_shader(struct shader *s, const char *v, const char *f) {
    (void)v; (void)f;
    memset(s, 0, sizeof(*s));
    s->program = 1;
    s->view_transform = 1;
    s->world_transform = 2;
    s->texture = 3;
    s->vertex_pos = 0;
    s->vertex_uv = 1;
}
void gfx_register_reset_callback(gfx_reset_callback cb, void *user) { (void)cb; (void)user; }
bool gfx_remap_texture(Texture *t) { return t && t->handle != 0; }
GLint glGetUniformLocation(GLuint p, const GLchar *n) {
    (void)p;
    if (!strcmp(n, "uv_rect")) return 10;
    if (!strcmp(n, "blend_rate")) return 11;
    if (!strcmp(n, "add_color")) return 12;
    if (!strcmp(n, "multiply_color")) return 13;
    if (!strcmp(n, "draw_filter")) return 14;
    if (!strcmp(n, "use_clipper")) return 15;
    if (!strcmp(n, "clipper_tex")) return 16;
    if (!strcmp(n, "inv_clipper_transform")) return 17;
    return 20;
}
void glActiveTexture(GLenum t) { (void)t; }
void glBindBuffer(GLenum t, GLuint b) { (void)t; (void)b; }
void glBindTexture(GLenum t, GLuint tx) { if (t == GL_TEXTURE_2D) cur_tex0 = tx; }
void glBindVertexArray(GLuint a) { (void)a; }
void glBlendFuncSeparate(GLenum a, GLenum b, GLenum c, GLenum d) { (void)a;(void)b;(void)c;(void)d; }
void glDisable(GLenum c) { (void)c; }
void glDisableVertexAttribArray(GLuint i) { (void)i; }
void glDrawElements(GLenum m, GLsizei c, GLenum t, const void *i) {
    (void)m; (void)c; (void)t; (void)i;
    assert(nr_draws < 32);
    draws[nr_draws++] = (struct draw_record){
        .tex = cur_tex0,
        .blend_rate = cur_blend,
        .mul = { cur_mul[0], cur_mul[1], cur_mul[2] },
    };
}
void glEnable(GLenum c) { (void)c; }
void glEnableVertexAttribArray(GLuint i) { (void)i; }
void glUniform1f(GLint l, GLfloat v) { if (l == 11) cur_blend = v; }
void glUniform1i(GLint l, GLint v) { (void)l; (void)v; }
void glUniform3fv(GLint l, GLsizei c, const GLfloat *v) {
    (void)c;
    if (l == 13) memcpy(cur_mul, v, sizeof(cur_mul));
}
void glUniform4fv(GLint l, GLsizei c, const GLfloat *v) { (void)l; (void)c; (void)v; }
void glUniformMatrix4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)l; (void)c; (void)t; (void)v; }
void glUseProgram(GLuint p) { (void)p; }
void glVertexAttribPointer(GLuint i, GLint s, GLenum t, GLboolean n, GLsizei st, const void *p) {
    (void)i; (void)s; (void)t; (void)n; (void)st; (void)p;
}

struct sact_sprite *sact_try_get_sprite(int sp) { (void)sp; return NULL; }
struct texture *sprite_get_texture(struct sact_sprite *sp) { (void)sp; return NULL; }
struct RE_plugin *RE_get_plugin(int p) { (void)p; return NULL; }
struct parts *parts_try_get(int no) { (void)no; return NULL; }
struct parts *parts_get(int no) { (void)no; return NULL; }
void parts_text_box_set_display(struct parts *p, const char *c) { (void)p; (void)c; }
void parts_world_matrix(struct parts *parts, mat4 out) {
    (void)parts;
    glm_mat4_identity(out);
}
bool parts_is_visible(struct parts *parts) {
    return parts && parts->global.show && parts->global.alpha > 0;
}
struct parts_state *parts_display_state(struct parts *parts) {
    return &parts->states[parts->state];
}
int parts_get_modifier_font_scale(void) { return 100; }
int parts_get_modifier_scrollbar_scale(void) { return 100; }
bool parts_build_construction_process(struct parts *parts, struct parts_construction_process *cproc) {
    last_cproc_build_parts = parts;
    cproc_build_calls++;
    cproc->needs_build = false;
    cproc->common.texture.handle = ++next_cproc_tex;
    cproc->common.texture.w = cproc->common.w;
    cproc->common.texture.h = cproc->common.h;
    return true;
}
int parts_scrollbar_range(struct parts_scrollbar *s) { (void)s; return 0; }
int parts_scrollbar_skin_state(struct parts_scrollbar *s, int piece) { (void)s; (void)piece; return 0; }
void parts_scrollbar_rects(struct parts *parts, Rectangle rects[4]) { (void)parts; memset(rects, 0, sizeof(Rectangle) * 4); }
void parts_widget_get_size(struct parts *parts, int *w, int *h) { (void)parts; *w = 0; *h = 0; }
void parts_flat_clear_emitter_births(struct flat_layer_state *s) { (void)s; }
struct flat_emitter_birth *parts_flat_get_emitter_birth(struct flat_layer_state *s, const struct flat_timeline *t, int i, int f, unsigned inst, bool c) {
    (void)s; (void)t; (void)i; (void)f; (void)inst; (void)c; return NULL;
}
unsigned parts_flat_key_count(const struct flat_timeline *t, int f) { (void)t; (void)f; return 0; }
const struct flat_key_data_graphic *parts_flat_key(const struct flat_timeline *t, int f, unsigned i) { (void)t; (void)f; (void)i; return NULL; }
int parts_flat_find_library(struct flat *fl, const char *n) { (void)fl; (void)n; return -1; }
int parts_flat_stop_motion_get_cg_lib(struct parts_flat *f, int s, int l) { (void)f; (void)s; (void)l; return -1; }
bool parts_flat_emitter_get_align_offset(struct parts_flat *f, int idx, vec2 out) { (void)f; (void)idx; (void)out; return false; }
void parts_flat_foreach_emitter_particle(struct parts_flat *f, int idx, const struct flat_timeline *t, unsigned inst, int b, int a, int fc, flat_emitter_particle_fn fn, void *ud) {
    (void)f; (void)idx; (void)t; (void)inst; (void)b; (void)a; (void)fc; (void)fn; (void)ud;
}
void parts_flat_build_layer_matrix(const struct flat_key_data_graphic *k, vec2 pos, bool r, bool s, bool o, bool rl, bool rt, mat4 out) {
    (void)k; (void)pos; (void)r; (void)s; (void)o; (void)rl; (void)rt; glm_mat4_identity(out);
}
void parts_flat_build_emitter_base_matrix(const struct flat_emitter *e, const struct flat_key_stack *s, mat4 out) {
    (void)e; (void)s; glm_mat4_identity(out);
}
void parts_flat_emitter_resolve_layer(const struct flat_emitter *e, const struct flat_key_data_graphic *k, float pa, float la, struct flat_emitter_layer_effective *out) {
    (void)e; (void)k; (void)pa; (void)la; memset(out, 0, sizeof(*out));
}

#include "parts/render.c"

static void assert_near(float actual, float expected) {
    assert(fabsf(actual - expected) < 1e-4f);
}

static struct parts make_cg_part(GLuint tex, int w, int h, bool opaque) {
    struct parts p;
    memset(&p, 0, sizeof(p));
    p.sp.render = parts_sprite_render;
    p.global.show = true;
    p.global.alpha = 255;
    p.global.scale.x = 1.0f;
    p.global.scale.y = 1.0f;
    p.global.multiply_color = (SDL_Color){255, 255, 255, 255};
    p.local = p.global;
    p.states[0].type = PARTS_CG;
    p.states[0].common.w = w;
    p.states[0].common.h = h;
    p.states[0].common.texture.handle = tex;
    p.states[0].common.texture.w = w;
    p.states[0].common.texture.h = h;
    p.states[0].common.texture.opaque = opaque;
    return p;
}

int main(void) {
    parts_render_init();

    // 1. PARTS_MESSAGE_WINDOW: background scales, text/key-wait/children do NOT.
    struct parts msg = make_cg_part(101, 800, 200, false);
    struct parts_message_window mw;
    memset(&mw, 0, sizeof(mw));
    mw.active = true;
    mw.fixed = true;
    mw.key_show = true;
    mw.key_z = 1;
    mw.inactive_color[0] = 200;
    mw.inactive_color[1] = 100;
    mw.inactive_color[2] = 50;
    mw.text_area = (Rectangle){0, 0, 800, 200};
    struct parts_text_char glyph = {
        .t = { .handle = 102, .w = 20, .h = 20 },
        .advance = 20,
    };
    struct parts_text_line line = { .chars = &glyph, .nr_chars = 1, .height = 20, .width = 20 };
    mw.label.type = PARTS_TEXT;
    mw.label.text.nr_lines = 1;
    mw.label.text.lines = &line;
    mw.label.text.common.w = 20;
    mw.label.text.common.h = 20;
    mw.key_wait.type = PARTS_CG;
    mw.key_wait.common.w = 16;
    mw.key_wait.common.h = 16;
    mw.key_wait.common.texture = (Texture){ .handle = 103, .w = 16, .h = 16 };
    msg.message_box = &mw;
    msg.message_window_effect_link = false; // always 0 in AliceSoft PACTs

    struct parts child_btn = make_cg_part(104, 60, 30, false);
    child_btn.parent = &msg;

    // 1a. Half opacity & brightness (0.5, 0.5)
    parts_message_window_mul_color_rate = 0.5f;
    parts_message_window_alpha_rate = 0.5f;
    nr_draws = 0;
    parts_render(&msg);
    parts_render(&child_btn);
    assert(nr_draws == 4);
    // Background (101): lroundf(255 * 0.5) = 128
    assert(draws[0].tex == 101);
    assert_near(draws[0].blend_rate, 128.0f / 255.0f);
    assert_near(draws[0].mul[0], 128.0f / 255.0f);
    assert_near(draws[0].mul[1], 128.0f / 255.0f);
    assert_near(draws[0].mul[2], 128.0f / 255.0f);
    // Text (102): unscaled
    assert(draws[1].tex == 102);
    assert_near(draws[1].blend_rate, 1.0f);
    assert_near(draws[1].mul[0], 1.0f);
    // Key-wait (103): unscaled
    assert(draws[2].tex == 103);
    assert_near(draws[2].blend_rate, 1.0f);
    assert_near(draws[2].mul[0], 1.0f);
    // Child button (104): unscaled
    assert(draws[3].tex == 104);
    assert_near(draws[3].blend_rate, 1.0f);
    assert_near(draws[3].mul[0], 1.0f);
    // msg.global must be restored intact
    assert(msg.global.alpha == 255 && msg.global.multiply_color.r == 255);

    // 1b. Zero opacity (alpha=0.0): background skipped, text/key-wait/children still full alpha!
    parts_message_window_alpha_rate = 0.0f;
    parts_message_window_mul_color_rate = 0.5f;
    nr_draws = 0;
    parts_render(&msg);
    parts_render(&child_btn);
    assert(nr_draws == 3);
    assert(draws[0].tex == 102);
    assert_near(draws[0].blend_rate, 1.0f);
    assert(draws[1].tex == 103);
    assert_near(draws[1].blend_rate, 1.0f);
    assert(draws[2].tex == 104);
    assert_near(draws[2].blend_rate, 1.0f);
    assert(msg.global.alpha == 255);

    // 1c. Inactive window with key_z < 0: inactive_color applies to text/key-wait/bg;
    //     message_window_*_rate applies ONLY to bg on top of inactive_color.
    mw.active = false;
    mw.key_z = -1;
    parts_message_window_alpha_rate = 0.5f;
    parts_message_window_mul_color_rate = 0.5f;
    nr_draws = 0;
    parts_render(&msg);
    assert(nr_draws == 3);
    assert(draws[0].tex == 103);
    assert_near(draws[0].blend_rate, 1.0f);
    assert_near(draws[0].mul[0], 200.0f / 255.0f);
    assert_near(draws[0].mul[1], 100.0f / 255.0f);
    assert_near(draws[0].mul[2], 50.0f / 255.0f);
    assert(draws[1].tex == 101);
    assert_near(draws[1].blend_rate, 128.0f / 255.0f);
    assert_near(draws[1].mul[0], 100.0f / 255.0f);
    assert_near(draws[1].mul[1], 50.0f / 255.0f);
    assert_near(draws[1].mul[2], 25.0f / 255.0f);
    assert(draws[2].tex == 102);
    assert_near(draws[2].blend_rate, 1.0f);
    assert_near(draws[2].mul[0], 200.0f / 255.0f);
    assert(msg.global.alpha == 255 && msg.global.multiply_color.r == 255);

    // 1d. MessageWindow with PARTS_CONSTRUCTION_PROCESS background builds on real part
    mw.active = true;
    mw.key_show = false;
    msg.states[0].type = PARTS_CONSTRUCTION_PROCESS;
    msg.states[0].cproc.needs_build = true;
    msg.states[0].common.texture.handle = 0;
    msg.states[0].common.w = 640;
    msg.states[0].common.h = 180;
    cproc_build_calls = 0;
    last_cproc_build_parts = NULL;
    nr_draws = 0;
    parts_render(&msg);
    assert(cproc_build_calls == 1 && last_cproc_build_parts == &msg);
    assert(!msg.states[0].cproc.needs_build && msg.states[0].common.texture.handle == 501);
    assert(nr_draws == 2 && draws[0].tex == 501);
    assert_near(draws[0].blend_rate, 128.0f / 255.0f);

    // 2. Non-message_box CG & ConstructionProcess with message_window_effect_link
    struct parts linked_cg = make_cg_part(201, 1280, 720, true);
    linked_cg.message_window_effect_link = true;
    parts_message_window_alpha_rate = 0.5f;
    parts_message_window_mul_color_rate = 0.25f;
    nr_draws = 0;
    parts_render(&linked_cg);
    assert(nr_draws == 1 && draws[0].tex == 201);
    assert_near(draws[0].blend_rate, 128.0f / 255.0f);
    assert_near(draws[0].mul[0], 64.0f / 255.0f); // lroundf(255 * 0.25) = 64
    assert(linked_cg.global.alpha == 255 && linked_cg.global.multiply_color.r == 255);

    // Descendant inherits parent's message_window_effect_link
    struct parts linked_container = make_cg_part(0, 0, 0, false);
    linked_container.message_window_effect_link = true;
    struct parts linked_descendant = make_cg_part(202, 1280, 720, true);
    linked_descendant.parent = &linked_container;
    nr_draws = 0;
    parts_render(&linked_descendant);
    assert(nr_draws == 1 && draws[0].tex == 202);
    assert_near(draws[0].blend_rate, 128.0f / 255.0f);
    assert_near(draws[0].mul[0], 64.0f / 255.0f);
    assert(linked_descendant.global.alpha == 255);

    // ConstructionProcess with message_window_effect_link (e.g. Ixseal Config sample)
    struct parts sample_cp;
    memset(&sample_cp, 0, sizeof(sample_cp));
    sample_cp.sp.render = parts_sprite_render;
    sample_cp.global.show = true;
    sample_cp.global.alpha = 255;
    sample_cp.global.scale.x = 1.0f;
    sample_cp.global.scale.y = 1.0f;
    sample_cp.global.multiply_color = (SDL_Color){255, 255, 255, 255};
    sample_cp.message_window_effect_link = true;
    sample_cp.states[0].type = PARTS_CONSTRUCTION_PROCESS;
    sample_cp.states[0].cproc.needs_build = true;
    sample_cp.states[0].common.w = 400;
    sample_cp.states[0].common.h = 120;

    // At alpha_rate=0.0, drawing is skipped and global is restored
    parts_message_window_alpha_rate = 0.0f;
    cproc_build_calls = 0;
    nr_draws = 0;
    parts_render(&sample_cp);
    assert(nr_draws == 0 && cproc_build_calls == 0);
    assert(sample_cp.states[0].cproc.needs_build);
    assert(sample_cp.global.alpha == 255);

    // At alpha_rate=0.5, cproc builds on REAL &sample_cp and draws once; second frame reuses cache
    parts_message_window_alpha_rate = 0.5f;
    parts_message_window_mul_color_rate = 0.5f;
    parts_render(&sample_cp);
    assert(cproc_build_calls == 1 && last_cproc_build_parts == &sample_cp);
    assert(!sample_cp.states[0].cproc.needs_build && sample_cp.states[0].common.texture.handle == 502);
    assert(nr_draws == 1 && draws[0].tex == 502);
    assert_near(draws[0].blend_rate, 128.0f / 255.0f);
    assert_near(draws[0].mul[0], 128.0f / 255.0f);
    assert(sample_cp.global.alpha == 255 && sample_cp.global.multiply_color.r == 255);

    parts_render(&sample_cp);
    assert(cproc_build_calls == 1 && nr_draws == 2);

    // Unlinked part is completely unaffected even at alpha_rate = 0.0
    struct parts unlinked_cg = make_cg_part(203, 1280, 720, true);
    parts_message_window_alpha_rate = 0.0f;
    parts_message_window_mul_color_rate = 0.25f;
    nr_draws = 0;
    parts_render(&unlinked_cg);
    assert(nr_draws == 1 && draws[0].tex == 203);
    assert_near(draws[0].blend_rate, 1.0f);
    assert_near(draws[0].mul[0], 1.0f);

    // 3. parts_sprite_is_fullscreen_opaque
    parts_message_window_alpha_rate = 1.0f;
    assert(parts_sprite_is_fullscreen_opaque(&linked_cg.sp));
    assert(parts_sprite_is_fullscreen_opaque(&linked_descendant.sp));
    assert(parts_sprite_is_fullscreen_opaque(&unlinked_cg.sp));

    parts_message_window_alpha_rate = 0.99f;
    assert(!parts_sprite_is_fullscreen_opaque(&linked_cg.sp));
    assert(!parts_sprite_is_fullscreen_opaque(&linked_descendant.sp));
    assert(parts_sprite_is_fullscreen_opaque(&unlinked_cg.sp));

    parts_message_window_alpha_rate = 0.0f;
    assert(!parts_sprite_is_fullscreen_opaque(&linked_cg.sp));
    assert(!parts_sprite_is_fullscreen_opaque(&linked_descendant.sp));
    assert(parts_sprite_is_fullscreen_opaque(&unlinked_cg.sp));

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(harness, LIBSYS4_CORE, ["-lz", "-lm"])
        self.assertEqual(out.strip(), "OK")

    def test_rates_update_matrix_and_prelink_abi(self):
        parts_fns = "\n".join(
            extract_c_fn("src/parts/parts.c", name)
            for name in (
                "clamp_message_window_rate",
                "PE_SetMessageWindowShow",
                "PE_SetMessageWindowEffectRates",
            )
        )
        pe_fns = "\n".join(
            extract_c_fn("src/hll/PartsEngine.c", name)
            for name in (
                "PartsEngine_ResolveSetNumeralFont",
                "PartsEngine_ResolveReleaseAll",
                "PartsEngine_Parts_ChangeFlatSound_int",
                "PartsEngine_ResolveChangeFlatSound",
                "PartsEngine_Update_v8",
                "PartsEngine_Update_v12",
                "PartsEngine_UpdateMatrix",
                "PartsEngine_UpdateMatrix_v8",
                "PartsEngine_SetComponentMessageWindowEffectLink",
                "PartsEngine_SetResetTimerByChangeInputStatus",
                "PartsEngine_GetResetTimerByChangeInputStatus",
                "PartsEngine_Parts_SetResetTimerByChangeInputStatus",
                "PartsEngine_Parts_GetResetTimerByChangeInputStatus",
                "PartsEngine_ResolveSetResetTimerByChangeInputStatus",
                "PartsEngine_ResolveGetResetTimerByChangeInputStatus",
                "get_fun",
                "PartsEngine_PreLink",
            )
        )

        # Verify direct exports in PartsEngine.c library table
        pe_src = (ROOT / "src/hll/PartsEngine.c").read_text(encoding="utf-8")
        self.assertIn("HLL_EXPORT(DeletePartsTopTextLine, PE_DeletePartsTopTextLine)", pe_src)
        self.assertIn(
            "HLL_EXPORT(AddDrawLineToPartsConstructionProcess, PE_AddDrawLineToPartsConstructionProcess)",
            pe_src,
        )
        self.assertIn("HLL_OVERLOAD(Parts_SetNumeralFont, PartsEngine_ResolveSetNumeralFont)", pe_src)
        self.assertIn("HLL_OVERLOAD(ReleaseAll, PartsEngine_ResolveReleaseAll)", pe_src)
        self.assertIn(
            "HLL_OVERLOAD(SetResetTimerByChangeInputStatus, PartsEngine_ResolveSetResetTimerByChangeInputStatus)",
            pe_src,
        )
        self.assertIn(
            "HLL_OVERLOAD(GetResetTimerByChangeInputStatus, PartsEngine_ResolveGetResetTimerByChangeInputStatus)",
            pe_src,
        )
        self.assertIn("HLL_OVERLOAD(Parts_ChangeFlatSound, PartsEngine_ResolveChangeFlatSound)", pe_src)
        self.assertIn("HLL_EXPORT(UpdateMatrix, PartsEngine_UpdateMatrix)", pe_src)

        harness = (
            r"""
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define possibly_unused __attribute__((unused))

enum ain_data_type {
    AIN_VOID = 0,
    AIN_INT = 10,
    AIN_FLOAT = 11,
    AIN_STRING = 12,
    AIN_REF_STRING = 20,
    AIN_REF_ARRAY_INT = 21,
    AIN_BOOL = 50,
    AIN_ARRAY = 79,
    AIN_REF_ARRAY = 80,
};

struct ain_type { enum ain_data_type data; };
struct ain_hll_argument { const char *name; struct ain_type type; };
struct ain_hll_function {
    const char *name;
    struct ain_type return_type;
    int nr_arguments;
    struct ain_hll_argument *arguments;
};
struct ain_library {
    const char *name;
    int nr_functions;
    struct ain_hll_function *functions;
};
struct ain {
    int version;
    int nr_libraries;
    struct ain_library *libraries;
};
struct page;
struct string;
struct parts { bool message_window_effect_link; };

static struct ain *ain = NULL;
static bool parts_v2 = false;
static bool parts_hierarchical_matrix = true;
static bool parts_message_window_show = true;
static float parts_message_window_mul_color_rate = 1.0f;
static float parts_message_window_alpha_rate = 1.0f;
static int engine_dirty_calls = 0;
static int part_dirty_calls = 0;
static int update_calls = 0;
static int update_ts_calls = 0;
static int update_comp_calls = 0;
static bool last_update_show = true;
static int last_passed_time = 0, last_scaled_time = 0;
static struct parts mock_part;

static void parts_engine_dirty(void) { engine_dirty_calls++; }
static void parts_dirty(struct parts *p) { (void)p; part_dirty_calls++; }
static struct parts *parts_get(int no) { assert(no == 7); return &mock_part; }
static void PE_enable_multi_controller(void) {}
static void PE_Update(int pt, bool show) {
    update_calls++; last_passed_time = pt; last_update_show = show;
}
static void PE_UpdateWithTimeScale(int pt, int st, bool show) {
    update_ts_calls++; last_passed_time = pt; last_scaled_time = st; last_update_show = show;
}
static void PE_UpdateComponent(int pt) { (void)pt; update_comp_calls++; }
static void parts_render_update(void) {}

// Sentinel functions for overload resolution assertions
static void PE_SetNumeralFont(void) {}
static void PE_SetNumeralFontWithPitch(void) {}
static void PE_ReleaseAllParts(void) {}
static void PE_ReleaseAll_list(struct page **p) { (void)p; }
static void PE_ChangeFlatSound(int a, struct string *b, struct string *c, int d) { (void)a;(void)b;(void)c;(void)d; }

static int ain_get_library(struct ain *a, const char *name) {
    for (int i = 0; i < a->nr_libraries; i++)
        if (!strcmp(a->libraries[i].name, name)) return i;
    return -1;
}
static int ain_get_library_function(struct ain *a, int libno, const char *name) {
    struct ain_library *lib = &a->libraries[libno];
    for (int i = 0; i < lib->nr_functions; i++)
        if (!strcmp(lib->functions[i].name, name)) return i;
    return -1;
}

static void *replaced_update_matrix = NULL;
static void *replaced_update_component = NULL;
static int lib_PartsEngine_dummy possibly_unused;
#define lib_PartsEngine lib_PartsEngine_dummy
static void record_replace(const char *name, void *fn) {
    if (!strcmp(name, "UpdateMatrix")) replaced_update_matrix = fn;
    if (!strcmp(name, "UpdateComponent")) replaced_update_component = fn;
}
#define static_library_replace(lib, name, fn) record_replace((name), (void *)(fn))

// Dummies referenced only inside static_library_replace in PartsEngine_PreLink
static void *PartsEngine_SetComponentType_v12, *PartsEngine_GetComponentType_v12;
static void *PartsEngine_GetPartsCGName_v12, *PartsEngine_GetMessageVariableString_v12;
static void *PE_GetActivityIntentDataDestination;
static void *PE_SetButtonSize, *PE_SetButtonEnable, *PE_IsButtonEnable, *PE_SetButtonColor;
static void *PE_GetButtonR, *PE_GetButtonG, *PE_GetButtonB, *PE_SetButtonFontProperty;
static void *PE_GetButtonFontProperty, *PE_SetButtonCGName, *PE_GetButtonCGName;
static void *PE_SetButtonText, *PE_GetButtonText;
static void *PE_SetLayoutBoxReturnF, *PE_GetLayoutBoxReturnSizeF, *PE_AddMotionSound_name;
static void *PartsEngine_GetClickMissSoundName_v6, *PartsEngine_Parts_GetSoundName_v6;
static void *PartsEngine_Parts_GetPartsFlatDataInfo_v6, *PartsEngine_GetButtonCGName_v6;
static void *PartsEngine_GetButtonFlatName_v6, *PartsEngine_GetButtonText_v6;
static void *PartsEngine_GetCheckBoxCGName_v6, *PartsEngine_GetCheckBoxFlatName_v6;
static void *PartsEngine_GetCheckBoxText_v6, *PartsEngine_GetVScrollbarCGName_v6;
static void *PartsEngine_GetVScrollbarFlatName_v6, *PartsEngine_GetHScrollbarCGName_v6;
static void *PartsEngine_GetHScrollbarFlatName_v6, *PartsEngine_GetTextBoxText_v6;
static void *PartsEngine_WidgetGetEmptyString_v6, *PartsEngine_WidgetGetItemEmptyString_v6;
static void *PartsEngine_GetActivityPartsName_v6, *PartsEngine_GetLoopCGCGName_v6;
static void *PartsEngine_GetTextPartsText_v6, *PartsEngine_Parts_GetHGaugeCG_v6;
static void *PartsEngine_Parts_GetVGaugeCG_v6, *PartsEngine_Parts_GetNumeralCGName_v6;
static void *PartsEngine_Parts_GetPartsCGDetectionCGName_v6, *PartsEngine_Parts_GetPartsFlatName_v6;
static void *PartsEngine_GetFlatCGName_v6, *PartsEngine_GetProxyFlatCGName_v6;
static void *PartsEngine_GetFlatSoundName_v6, *PartsEngine_GetProxyFlatSound_v6;
static void *PartsEngine_LoadActivityEXText_v6, *PE_GetComponentAbsoluteSquarePos;
static void *PartsEngine_PlayPartsMovie_v6, *PE_AddDrawCutCGToPartsConstructionProcess;
static void *PE_AddCopyCutCGToPartsConstructionProcess, *PartsEngine_Update_Pascha3PC;
static void *PartsEngine_Save_v14, *PartsEngine_AddPartsConstructionProcess_v6;
static void *PartsEngine_AddPartsConstructionProcess_v14, *PartsEngine_GetPartsConstructionProcess_v6;
static void *PartsEngine_GetPartsConstructionProcess_v14;
static void *PartsEngine_GetMessageWindowCGName_v6, *PartsEngine_GetMessageWindowFlatName_v6;
static void *PartsEngine_GetMessageWindowText_v6, *PartsEngine_GetActivityEXText_v6;
static void *PartsEngine_AddMotionHGaugeRate_int_curve, *PartsEngine_AddMotionHGaugeRate_int;
static void *PartsEngine_AddMotionVGaugeRate_int_curve, *PartsEngine_AddMotionVGaugeRate_int;
static void *PE_AddMotionHGaugeRate, *PE_AddMotionVGaugeRate;

"""
            + parts_fns
            + "\n"
            + pe_fns
            + r"""

int main(void) {
    // 1. Clamping & dirty behavior
    PE_SetMessageWindowEffectRates(1.0f, 1.0f);
    assert(engine_dirty_calls == 0);
    PE_SetMessageWindowEffectRates(0.4f, 0.6f);
    assert(engine_dirty_calls == 1);
    assert(fabsf(parts_message_window_mul_color_rate - 0.4f) < 1e-6f);
    assert(fabsf(parts_message_window_alpha_rate - 0.6f) < 1e-6f);
    PE_SetMessageWindowEffectRates(0.4f, 0.6f);
    assert(engine_dirty_calls == 1);

    // Out-of-range & non-finite clamp cleanly
    PE_SetMessageWindowEffectRates(-0.5f, 2.5f);
    assert(parts_message_window_mul_color_rate == 0.0f);
    assert(parts_message_window_alpha_rate == 1.0f);
    assert(engine_dirty_calls == 2);

    PE_SetMessageWindowEffectRates(NAN, INFINITY);
    assert(parts_message_window_mul_color_rate == 1.0f);
    assert(parts_message_window_alpha_rate == 1.0f);
    assert(engine_dirty_calls == 3);

    PE_SetMessageWindowEffectRates(-INFINITY, 0.25f);
    assert(parts_message_window_mul_color_rate == 1.0f);
    assert(fabsf(parts_message_window_alpha_rate - 0.25f) < 1e-6f);
    assert(engine_dirty_calls == 4);

    // 2. PartsEngine_Update_v8 & PartsEngine_Update_v12 forward rates
    PartsEngine_Update_v8(16, false, false, 0.75f, 0.5f);
    assert(update_calls == 1 && last_passed_time == 16 && !last_update_show);
    assert(fabsf(parts_message_window_mul_color_rate - 0.75f) < 1e-6f);
    assert(fabsf(parts_message_window_alpha_rate - 0.5f) < 1e-6f);

    PartsEngine_Update_v12(16, 32, true, 0.2f, 0.8f);
    assert(update_ts_calls == 1 && last_passed_time == 16 && last_scaled_time == 32 && last_update_show);
    assert(fabsf(parts_message_window_mul_color_rate - 0.2f) < 1e-6f);
    assert(fabsf(parts_message_window_alpha_rate - 0.8f) < 1e-6f);

    // 3. 1-arg UpdateMatrix (AIN v7 Tsumamigui3) preserves rates!
    parts_message_window_show = true;
    int dirty_before = engine_dirty_calls;
    PartsEngine_UpdateMatrix(false);
    assert(!parts_message_window_show && update_comp_calls == 1);
    assert(engine_dirty_calls == dirty_before + 1);
    assert(fabsf(parts_message_window_mul_color_rate - 0.2f) < 1e-6f);
    assert(fabsf(parts_message_window_alpha_rate - 0.8f) < 1e-6f);

    // 3-arg UpdateMatrix updates all three
    PartsEngine_UpdateMatrix_v8(true, 0.9f, 0.3f);
    assert(parts_message_window_show && update_comp_calls == 2);
    assert(fabsf(parts_message_window_mul_color_rate - 0.9f) < 1e-6f);
    assert(fabsf(parts_message_window_alpha_rate - 0.3f) < 1e-6f);

    // 4. PartsEngine_SetComponentMessageWindowEffectLink dirties only on change
    mock_part.message_window_effect_link = false;
    part_dirty_calls = 0;
    PartsEngine_SetComponentMessageWindowEffectLink(7, false);
    assert(part_dirty_calls == 0);
    PartsEngine_SetComponentMessageWindowEffectLink(7, true);
    assert(mock_part.message_window_effect_link && part_dirty_calls == 1);
    PartsEngine_SetComponentMessageWindowEffectLink(7, true);
    assert(part_dirty_calls == 1);

    // 5. PartsEngine_PreLink UpdateMatrix selection (3-arg -> v8, 1-arg -> 1-arg)
    struct ain_hll_argument um3_args[3] = {
        { "show", { AIN_BOOL } }, { "mul", { AIN_FLOAT } }, { "alpha", { AIN_FLOAT } }
    };
    struct ain_hll_function funcs_v8[1] = {
        { "UpdateMatrix", { AIN_VOID }, 3, um3_args }
    };
    struct ain_library lib_v8 = { "PartsEngine", 1, funcs_v8 };
    struct ain mock_ain_v8 = { 8, 1, &lib_v8 };
    ain = &mock_ain_v8;
    replaced_update_matrix = NULL;
    PartsEngine_PreLink();
    assert(replaced_update_matrix == (void *)PartsEngine_UpdateMatrix_v8);

    struct ain_hll_argument um1_args[1] = { { "show", { AIN_BOOL } } };
    struct ain_hll_function funcs_v7[1] = {
        { "UpdateMatrix", { AIN_VOID }, 1, um1_args }
    };
    struct ain_library lib_v7 = { "PartsEngine", 1, funcs_v7 };
    struct ain mock_ain_v7 = { 7, 1, &lib_v7 };
    ain = &mock_ain_v7;
    replaced_update_matrix = NULL;
    PartsEngine_PreLink();
    assert(replaced_update_matrix == (void *)PartsEngine_UpdateMatrix);

    // 6. Overload resolvers
    // Parts_SetNumeralFont
    struct ain_hll_argument nf13[13] = {0};
    nf13[11].type.data = AIN_BOOL;
    nf13[12].type.data = AIN_INT;
    struct ain_hll_function fn_nf13 = { "Parts_SetNumeralFont", { AIN_BOOL }, 13, nf13 };
    assert(PartsEngine_ResolveSetNumeralFont(&fn_nf13) == (void *)PE_SetNumeralFontWithPitch);
    struct ain_hll_argument nf12[12] = {0};
    nf12[11].type.data = AIN_INT;
    struct ain_hll_function fn_nf12 = { "Parts_SetNumeralFont", { AIN_VOID }, 12, nf12 };
    assert(PartsEngine_ResolveSetNumeralFont(&fn_nf12) == (void *)PE_SetNumeralFont);

    // ReleaseAll
    struct ain_hll_function fn_ra0 = { "ReleaseAll", { AIN_VOID }, 0, NULL };
    assert(PartsEngine_ResolveReleaseAll(&fn_ra0) == (void *)PE_ReleaseAllParts);
    struct ain_hll_argument ra1_v6[1] = { { "list", { AIN_REF_ARRAY_INT } } };
    struct ain_hll_function fn_ra1_v6 = { "ReleaseAll", { AIN_VOID }, 1, ra1_v6 };
    assert(PartsEngine_ResolveReleaseAll(&fn_ra1_v6) == (void *)PE_ReleaseAll_list);
    struct ain_hll_argument ra1_v12[1] = { { "list", { AIN_REF_ARRAY } } };
    struct ain_hll_function fn_ra1_v12 = { "ReleaseAll", { AIN_VOID }, 1, ra1_v12 };
    assert(PartsEngine_ResolveReleaseAll(&fn_ra1_v12) == (void *)PE_ReleaseAll_list);

    // Set/GetResetTimerByChangeInputStatus
    struct ain_hll_argument rt_args[3] = {0};
    struct ain_hll_function fn_srt2 = { "SetResetTimerByChangeInputStatus", { AIN_VOID }, 2, rt_args };
    struct ain_hll_function fn_srt3 = { "SetResetTimerByChangeInputStatus", { AIN_VOID }, 3, rt_args };
    assert(PartsEngine_ResolveSetResetTimerByChangeInputStatus(&fn_srt2) == (void *)PartsEngine_SetResetTimerByChangeInputStatus);
    assert(PartsEngine_ResolveSetResetTimerByChangeInputStatus(&fn_srt3) == (void *)PartsEngine_Parts_SetResetTimerByChangeInputStatus);
    struct ain_hll_function fn_grt1 = { "GetResetTimerByChangeInputStatus", { AIN_BOOL }, 1, rt_args };
    struct ain_hll_function fn_grt2 = { "GetResetTimerByChangeInputStatus", { AIN_BOOL }, 2, rt_args };
    assert(PartsEngine_ResolveGetResetTimerByChangeInputStatus(&fn_grt1) == (void *)PartsEngine_GetResetTimerByChangeInputStatus);
    assert(PartsEngine_ResolveGetResetTimerByChangeInputStatus(&fn_grt2) == (void *)PartsEngine_Parts_GetResetTimerByChangeInputStatus);

    // Parts_ChangeFlatSound
    struct ain_hll_argument cfs_int[4] = {
        { "no", { AIN_INT } }, { "old", { AIN_STRING } }, { "snd", { AIN_INT } }, { "st", { AIN_INT } }
    };
    struct ain_hll_function fn_cfs_int = { "Parts_ChangeFlatSound", { AIN_VOID }, 4, cfs_int };
    assert(PartsEngine_ResolveChangeFlatSound(&fn_cfs_int) == (void *)PartsEngine_Parts_ChangeFlatSound_int);
    struct ain_hll_argument cfs_str[4] = {
        { "no", { AIN_INT } }, { "old", { AIN_STRING } }, { "snd", { AIN_STRING } }, { "st", { AIN_INT } }
    };
    struct ain_hll_function fn_cfs_str = { "Parts_ChangeFlatSound", { AIN_VOID }, 4, cfs_str };
    assert(PartsEngine_ResolveChangeFlatSound(&fn_cfs_str) == (void *)PE_ChangeFlatSound);

    puts("OK");
    return 0;
}
"""
        )
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp) / "abi.c", Path(tmp) / "abi"
            src.write_text(harness, encoding="utf-8")
            subprocess.run(
                [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                 str(src), "-lm", "-o", str(exe)],
                check=True,
            )
            res = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            self.assertEqual(res.stdout.strip(), "OK")

    def test_save_v36_effect_link_and_pact_aliases(self):
        # Verify PACT alias entries in activity.c
        act_src = (ROOT / "src/parts/activity.c").read_text(encoding="utf-8")
        self.assertIn('"消息窗口视觉效果连动"', act_src)
        self.assertIn('"信息窗口视觉效果连动"', act_src)

        save_src = (ROOT / "src/parts/save.c").read_text(encoding="utf-8")
        self.assertIn("#define CURRENT_SAVE_VERSION 36", save_src)

        funcs = ""
        for path, names in [
            ("src/page.c", ["array_type", "unref_array_type", "alloc_array"]),
            ("src/vm_v12.c", ["type_stride", "v12_array_new"]),
            ("src/hll/iarray.c", ["iarray_to_page"]),
            ("src/parts/parts.c", ["parts_set_posf"]),
            (
                "src/parts/save.c",
                [
                    "save_parts_params",
                    "load_parts_params",
                    "save_part_fonts",
                    "load_part_fonts",
                    "save_parts",
                    "load_parts",
                    "PE_SaveParts",
                    "PE_LoadParts",
                ],
            ),
        ]:
            funcs += "\n".join(extract_c_fn(path, n) for n in names) + "\n"

        harness = (
            r"""
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/string.h"
#include "vm/page.h"
#include "vm/heap.h"
#include "vm/v12.h"
#include "parts.h"
#include "parts/parts_internal.h"
#include "hll/iarray.h"
#define CURRENT_SAVE_VERSION 36
#define MIN_SINGLE_PART_SAVE_VERSION 32
#define PARTS_SAVED_FONTS (PARTS_NR_STATES + 1)
struct numeral_font_map { int source, target; };
bool parts_v2 = true, parts_multi_controller = false;
static struct parts_context context = { .list = TAILQ_HEAD_INITIALIZER(context.list) };
struct parts_context *parts_current = &context;
#define parts_list (parts_current->list)
struct string EMPTY_STRING;
static struct parts *objects[8];
static SDL_FPoint root_pos = {0, 0};
static struct ain program = { .version = 12 };
struct ain *ain = &program;
void *_xcalloc(size_t n, size_t s, const char *f) { (void)f; return calloc(n, s); }
void *_xmalloc(size_t n, const char *f) { (void)f; return malloc(n); }
void *xrealloc_array(void *p, size_t old, size_t n, size_t s) { (void)old; return realloc(p, n * s); }
struct string *make_string(const char *s, size_t n) {
    struct string *r = calloc(1, sizeof(*r) + n + 1);
    r->size = n; r->ref = 1; memcpy(r->text, s, n); return r;
}
struct string *string_ref(struct string *s) { return make_string(s->text, s->size); }
void free_string(struct string *s) { if (s && s != &EMPTY_STRING) free(s); }
void iarray_init_writer(struct iarray_writer *w, const char *s) {
    *w = (struct iarray_writer){0}; do { iarray_write(w, *s); } while (*s++);
}
void iarray_write(struct iarray_writer *w, int v) {
    w->data = realloc(w->data, (w->size + 1) * sizeof(int)); w->data[w->size++] = v;
}
void iarray_write_at(struct iarray_writer *w, unsigned p, int v) { w->data[p] = v; }
void iarray_free_writer(struct iarray_writer *w) { free(w->data); }
struct page *alloc_page(enum page_type t, int index, int n) {
    struct page *p = calloc(1, sizeof(*p) + n * sizeof(union vm_value));
    p->type = t; p->index = index; p->nr_vars = n; return p;
}
_Noreturn void _vm_error(const char *fmt, ...) { (void)fmt; abort(); }
void sys_warning(const char *fmt, ...) { (void)fmt; abort(); }
int ain_type_slot_width(const struct ain_type *t) { (void)t; return 1; }
int variable_init_typed(union vm_value *v, const struct ain_type *t, bool a, bool b) {
    (void)t; (void)a; (void)b; v->i = 0; return 1;
}
union vm_value variable_initval(enum ain_data_type t) { (void)t; return (union vm_value){ .i = 0 }; }
int alloc_struct(int n) { (void)n; abort(); }
void init_struct(int n, int slot) { (void)n; (void)slot; abort(); }
void create_struct(int n, union vm_value *v) { (void)n; (void)v; abort(); }
int32_t heap_alloc_slot(enum vm_pointer_type t) { (void)t; abort(); }
void heap_set_page(int slot, struct page *p) { (void)slot; (void)p; abort(); }
bool iarray_init_reader(struct iarray_reader *r, struct page *p, const char *s) {
    *r = (struct iarray_reader){ .data = p->values, .size = p->nr_vars };
    do { if (iarray_read(r) != *s) return false; } while (*s++);
    return !r->error;
}
int iarray_read(struct iarray_reader *r) {
    if (r->pos >= r->size) { r->error = 1; return 0; }
    return r->data[r->pos++].i;
}
void iarray_write_float(struct iarray_writer *w, float f) { union vm_value v = { .f = f }; iarray_write(w, v.i); }
float iarray_read_float(struct iarray_reader *r) { union vm_value v = { .i = iarray_read(r) }; return v.f; }
void iarray_write_string(struct iarray_writer *w, struct string *s) {
    for (int i = 0; s && i < s->size; i++) iarray_write(w, s->text[i]);
    iarray_write(w, 0);
}
struct string *iarray_read_string(struct iarray_reader *r) {
    char s[256]; int n = 0, c;
    while ((c = iarray_read(r))) { assert(n < 255); s[n++] = c; }
    return make_string(s, n);
}
void iarray_write_string_or_null(struct iarray_writer *w, struct string *s) { iarray_write_string(w, s); }
struct string *iarray_read_string_or_null(struct iarray_reader *r) {
    struct string *s = iarray_read_string(r);
    if (!s->size) { free_string(s); return NULL; }
    return s;
}
void iarray_write_color(struct iarray_writer *w, SDL_Color *c) {
    iarray_write(w, c->r); iarray_write(w, c->g); iarray_write(w, c->b); iarray_write(w, c->a);
}
void iarray_read_color(struct iarray_reader *r, SDL_Color *c) {
    c->r = iarray_read(r); c->g = iarray_read(r); c->b = iarray_read(r); c->a = iarray_read(r);
}
void iarray_write_text_style(struct iarray_writer *w, struct text_style *s) { (void)w; (void)s; }
void iarray_read_text_style(struct iarray_reader *r, struct text_style *s) { (void)r; (void)s; }
void delete_page_vars(struct page *p) { (void)p; }
void free_page(struct page *p) { free(p); }
struct parts *parts_try_get(int n) { return n >= 0 && n < 8 ? objects[n] : NULL; }
struct parts *parts_get(int n) {
    assert(n >= 0 && n < 8);
    if (!objects[n]) {
        objects[n] = calloc(1, sizeof(struct parts));
        objects[n]->no = n;
        TAILQ_INIT(&objects[n]->children);
        TAILQ_INIT(&objects[n]->motion);
        TAILQ_INSERT_TAIL(&parts_list, objects[n], parts_list_entry);
    }
    return objects[n];
}
void parts_release(int n) {
    struct parts *p = parts_try_get(n); if (!p) return;
    TAILQ_REMOVE(&parts_list, p, parts_list_entry);
    free_string(p->comment); free_string(p->user_component_name); free_string(p->text_box_text);
    for (int i = 0; i < 3; i++) free_string(p->sound_names[i]);
    free(p->radio_children); free(p); objects[n] = NULL;
}
void PE_UpdateComponent(int t) { (void)t; }
void parts_cg_async_flush_part(int parts_no) { (void)parts_no; }
void parts_widget_rebuild(struct parts *p) { (void)p; }
void parts_state_reset(struct parts_state *s, enum parts_type t) { memset(s, 0, sizeof(*s)); s->type = t; }
void parts_panel_rebuild(struct parts *p) { (void)p; }
void parts_scrollbar_rebuild(struct parts *p) { (void)p; }
void parts_list_resort(struct parts *p) { (void)p; }
void parts_component_dirty(struct parts *p) { (void)p; }
void parts_recalculate_hitbox(struct parts *p) { (void)p; }
void parts_dirty(struct parts *p) { (void)p; }
static void parts_update_global_pos(struct parts *p, SDL_FPoint pos) {
    p->global.pos = (SDL_FPoint){ pos.x + p->local.pos.x, pos.y + p->local.pos.y };
}
void parts_numeral_font_init(struct parts_numeral_font *f) { (void)f; }
void PE_SuspendTexture(int n) { (void)n; }
void parts_add_motion(struct parts *p, struct parts_motion *m) { (void)p; (void)m; abort(); }
static void save_parts_state(struct iarray_writer *w, struct parts *p, struct parts_state *s) {
    (void)p; iarray_write(w, s->type);
}
static void load_parts_state(struct iarray_reader *r, struct parts *p, struct parts_state *s, int v, const struct numeral_font_map *f) {
    (void)p; (void)v; (void)f; s->type = iarray_read(r);
}
static void save_message_window(struct iarray_writer *w, struct parts *p, struct parts_message_window *m) {
    (void)p; iarray_write(w, m != NULL);
}
static void load_message_window(struct iarray_reader *r, struct parts *p, int v, const struct numeral_font_map *f) {
    (void)p; (void)v; (void)f; (void)iarray_read(r);
}
static void save_parts_motion(struct iarray_writer *w, struct parts_motion *m) { (void)w; (void)m; abort(); }
static struct parts_motion *load_parts_motion(struct iarray_reader *r) { (void)r; abort(); }
"""
            + funcs
            + r"""
int main(void) {
    // 1. v36 roundtrip preserves message_window_effect_link = true
    struct parts *src = parts_get(1);
    src->message_window_effect_link = true;
    struct page *buf = NULL;
    assert(PE_SaveParts(1, &buf));
    assert(buf->values[5].i == 36);
    assert(PE_LoadParts(2, &buf));
    assert(parts_get(2)->message_window_effect_link == true);

    // 2. Downgrade v36 -> v35 by stripping the 1 int before motion_count & empty comment
    assert(buf->values[buf->nr_vars - 3].i == 1); // message_window_effect_link
    assert(buf->values[buf->nr_vars - 2].i == 0); // motion_count
    assert(buf->values[buf->nr_vars - 1].i == 0); // empty comment NUL
    memmove(buf->values + buf->nr_vars - 3, buf->values + buf->nr_vars - 2, 2 * sizeof(*buf->values));
    buf->nr_vars -= 1;
    buf->values[5].i = 35;
    buf->values[6].i = buf->nr_vars;
    assert(PE_LoadParts(3, &buf));
    assert(parts_get(3)->message_window_effect_link == false);

    // 3. v36 roundtrip preserves message_window_effect_link = false
    src->message_window_effect_link = false;
    assert(PE_SaveParts(1, &buf));
    assert(PE_LoadParts(4, &buf));
    assert(parts_get(4)->message_window_effect_link == false);

    free_page(buf);
    for (int i = 0; i < 8; i++) parts_release(i);
    puts("OK");
    return 0;
}
"""
        )
        flags = subprocess.check_output(["pkg-config", "--cflags", "sdl2"], text=True).split()
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp) / "save36.c", Path(tmp) / "save36"
            src.write_text(harness, encoding="utf-8")
            subprocess.run(
                [os.environ.get("CC", "cc"), "-std=c11", "-D_DEFAULT_SOURCE", "-DUSE_GLES",
                 "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation", *flags,
                 f"-I{ROOT / 'include'}", f"-I{ROOT / 'src'}",
                 f"-I{ROOT / 'subprojects/libsys4/include'}",
                 str(src), "-lm", "-o", str(exe)],
                check=True,
            )
            res = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            self.assertEqual(res.stdout.strip(), "OK")

    def test_release_all_list_and_text_end_session(self):
        fn_release_all = extract_c_fn("src/parts/parts.c", "PE_ReleaseAll_list")
        harness = (
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/ain.h"
#include "vm.h"
#include "parts/text.c"

bool game_evenicle_rance_cn = false;
bool gfx_text_advance_edges = false;
static int reset_cb_registrations = 0;
static int deleted_textures = 0;
static GLuint next_tex = 10;

void gfx_register_reset_callback(gfx_reset_callback cb, void *user) {
    (void)cb; (void)user;
    reset_cb_registrations++;
}
void gfx_unregister_reset_callback(gfx_reset_callback cb, void *user) {
    (void)cb; (void)user;
}
bool gfx_remap_texture(Texture *t) { return t && t->handle != 0; }
void gfx_init_texture_rgba(Texture *t, int w, int h, SDL_Color c) {
    (void)c;
    t->handle = ++next_tex;
    t->w = w;
    t->h = h;
}
void gfx_delete_texture(Texture *t) {
    if (t && t->handle) {
        deleted_textures++;
        t->handle = 0;
    }
}
float gfx_size_char(struct text_style *ts, const char *ch) { (void)ts; (void)ch; return 16.0f; }
float gfx_render_text_edge_only(Texture *t, float x, int y, char *ch, struct text_style *ts) {
    (void)t; (void)x; (void)y; (void)ch; (void)ts; return 16.0f;
}
float gfx_render_text_core_only(Texture *t, float x, int y, char *ch, struct text_style *ts) {
    (void)t; (void)x; (void)y; (void)ch; (void)ts; return 16.0f;
}
void parts_set_dims(struct parts *p, struct parts_common *c, int w, int h) { (void)p; c->w = w; c->h = h; }
void parts_dirty(struct parts *p) { (void)p; }
void parts_component_dirty(struct parts *p) { (void)p; }
struct parts *parts_get(int no) { (void)no; return NULL; }
struct parts *parts_try_get(int no) { (void)no; return NULL; }
struct parts_text *parts_get_text(struct parts *p, int s) { (void)p; (void)s; return NULL; }
void parts_set_surface_area(struct parts *p, struct parts_common *c, int x, int y, int w, int h) {
    (void)p; (void)c; (void)x; (void)y; (void)w; (void)h;
}

// Fixtures for PE_ReleaseAll_list
static struct parts_context ctx = { .list = TAILQ_HEAD_INITIALIZER(ctx.list) };
struct parts_context *parts_current = &ctx;
static int released_all_calls = 0;
struct page {
    int nr_vars;
    union vm_value values[16];
};
static struct page *array_pushback(struct page *dst, union vm_value v, enum ain_data_type dt, int st) {
    (void)dt; (void)st;
    if (!dst) dst = calloc(1, sizeof(*dst));
    assert(dst->nr_vars < 16);
    dst->values[dst->nr_vars++] = v;
    return dst;
}
void parts_release_all(void) {
    released_all_calls++;
    while (!TAILQ_EMPTY(&parts_list)) {
        struct parts *p = TAILQ_FIRST(&parts_list);
        TAILQ_REMOVE(&parts_list, p, parts_list_entry);
    }
}

"""
            + fn_release_all
            + r"""

int main(void) {
    // 1. PE_ReleaseAll_list populates erase_number_list and releases all parts
    struct parts p1 = { .no = 101 }, p2 = { .no = 202 }, p3 = { .no = 303 };
    TAILQ_INSERT_TAIL(&parts_list, &p1, parts_list_entry);
    TAILQ_INSERT_TAIL(&parts_list, &p2, parts_list_entry);
    TAILQ_INSERT_TAIL(&parts_list, &p3, parts_list_entry);

    struct page *erased = NULL;
    PE_ReleaseAll_list(&erased);
    assert(released_all_calls == 1);
    assert(TAILQ_EMPTY(&parts_list));
    assert(erased && erased->nr_vars == 3);
    assert(erased->values[0].i == 101);
    assert(erased->values[1].i == 202);
    assert(erased->values[2].i == 303);
    free(erased);

    // NULL erase_number_list is safe
    TAILQ_INSERT_TAIL(&parts_list, &p1, parts_list_entry);
    PE_ReleaseAll_list(NULL);
    assert(released_all_calls == 2 && TAILQ_EMPTY(&parts_list));

    // 2. parts_text_end_session clears glyph cache, init flag, and font scale
    parts_set_modifier_font_scale(140);
    assert(parts_get_modifier_font_scale() == 140);

    struct parts_text txt;
    memset(&txt, 0, sizeof(txt));
    txt.ts.size = 20.0f;
    txt.ts.scale_x = 1.0f;
    txt.ts.edge_left = 1.0f;
    parts_text_append_char(&txt, "A");
    parts_text_append_char(&txt, "B");
    assert(glyph_cache_initialized);
    assert(reset_cb_registrations == 1);
    assert(glyph_cache_used == 2);
    assert(glyph_lru_head >= 0 && glyph_lru_tail >= 0);

    parts_text_free(&txt);
    memset(&txt, 0, sizeof(txt));
    txt.ts.size = 20.0f;
    txt.ts.scale_x = 1.0f;

    int del_before = deleted_textures;
    parts_text_end_session();
    // 2 glyphs x (core + edge) = 4 textures deleted
    assert(deleted_textures == del_before + 4);
    assert(glyph_cache_used == 0);
    assert(!glyph_cache_initialized);
    assert(glyph_lru_head == -1 && glyph_lru_tail == -1);
    assert(parts_get_modifier_font_scale() == 100);

    // Session 2 re-initializes glyph cache and re-registers GL reset callback
    parts_text_append_char(&txt, "A");
    assert(glyph_cache_initialized);
    assert(reset_cb_registrations == 2);
    assert(glyph_cache_used == 1);
    parts_text_free(&txt);
    parts_text_end_session();

    puts("OK");
    return 0;
}
"""
        )
        out = compile_and_run(harness, LIBSYS4_CORE, ["-lz", "-lm"])
        self.assertEqual(out.strip(), "OK")


if __name__ == "__main__":
    unittest.main()
