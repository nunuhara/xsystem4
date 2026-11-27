#!/usr/bin/env python3
"""Regression & performance tests for FLAT loading/rendering and single-pass blur.

Covers:
1. Fast 64-segment forward-differenced eval_quadratic_bezier_arclen accuracy
   (< 0.2 px max error across 1,000 curves vs exact reference) and speedup.
2. Pre-resolved O(1) timeline/emitter lib_idx caches matching
   parts_flat_find_library (including duplicates, missing libs, nested
   timelines, emitters, and PE_ChangeFlatCG overrides) with zero per-frame
   linear scans.
3. TALT shared atlas textures with UV sub-rects under real Mesa GLES 3.0:
   single texture upload per atlas, exact UV sub-rect & area crop rendering
   without neighbor bleed, emitter particles, hittest logical dims,
   PE_ChangeFlatCG (external + internal TALT + restore), GL reset / widget
   skin recovery, and single-owner cleanup (zero double-delete).
4. Single-pass separable box-blur shader in src/draw.c under real Mesa GLES 3.0:
   gfx_copy_{width,height}_blur and gfx_copy_amap_{width,height}_blur pixel
   equivalence within +/-1 LSB for blur = 0..25, full/sub-rect, 1 draw call
   per blur regardless of radius, and GL context reset recovery.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CC = os.environ.get("CC", "cc")


def extract_function(rel_path: str, name: str) -> str:
    source = (ROOT / rel_path).read_text(encoding="utf-8")
    idx = source.index(name + "(")
    start = source.rfind("\n", 0, idx) + 1
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        ch = source[end]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        end += 1
    return source[start:end]


def find_gles_env():
    sdl2_inc = os.environ.get("SDL2_INCLUDE", "/usr/include/SDL2")
    cglm_inc = os.environ.get("CGLM_INCLUDE", "/usr/include")
    gles_inc = os.environ.get("GLES_INCLUDE", "/usr/include")
    if not (Path(sdl2_inc) / "SDL.h").exists():
        return None
    if not (Path(cglm_inc) / "cglm/cglm.h").exists():
        return None
    if not (Path(gles_inc) / "GLES3/gl3.h").exists():
        return None
    return sdl2_inc, cglm_inc, gles_inc


def run_gles_binary(binary: Path, cwd: Path):
    env = os.environ.copy()
    env["SDL_VIDEODRIVER"] = "offscreen"
    env.pop("LIBGL_ALWAYS_SOFTWARE", None)
    return subprocess.run(
        [str(binary)],
        cwd=str(cwd),
        env=env,
        capture_output=True,
        text=True,
        check=True,
    )


class FlatAndBlurPerfRegressionTests(unittest.TestCase):
    def test_bezier_arclen_accuracy_and_speedup(self):
        fn_src = extract_function("src/parts/flat.c", "eval_quadratic_bezier_arclen")
        harness = f"""
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef float vec2[2];
static inline void glm_vec2_zero(vec2 v) {{ v[0] = 0.0f; v[1] = 0.0f; }}
static inline void glm_vec2_copy(vec2 a, vec2 dest) {{ dest[0] = a[0]; dest[1] = a[1]; }}

{fn_src}

static void ref_bezier_1024_orig(vec2 p0, vec2 p1, vec2 p2, int step_idx, vec2 out) {{
    if (step_idx > 1024) {{ glm_vec2_zero(out); return; }}
    vec2 pts[1024];
    const float inv_1024 = 1.0f / 1024.0f;
    for (int k = 0; k < 1024; k++) {{
        float s = (float)k * inv_1024;
        float om = 1.0f - s;
        float b0 = om * om, b1 = 2.0f * s * om, b2 = s * s;
        pts[k][0] = b0 * p0[0] + b1 * p1[0] + b2 * p2[0];
        pts[k][1] = b0 * p0[1] + b1 * p1[1] + b2 * p2[1];
    }}
    float seg_lens[1023];
    float total_len = 0.0f;
    for (int k = 0; k < 1023; k++) {{
        float dx = pts[k + 1][0] - pts[k][0];
        float dy = pts[k + 1][1] - pts[k][1];
        seg_lens[k] = sqrtf(dx * dx + dy * dy);
        total_len += seg_lens[k];
    }}
    float unit_len = total_len * inv_1024;
    float target_len = (float)step_idx * unit_len;
    float accum = 0.0f;
    for (int k = 0; k < 1023; k++) {{
        accum += seg_lens[k];
        if (target_len == accum) {{ glm_vec2_copy(pts[k + 1], out); return; }}
        if (accum > target_len) {{
            if (k == 1022 || unit_len == 0.0f || (pts[k][0] == 0.0f && pts[k][1] == 0.0f)) {{
                glm_vec2_copy(pts[k + 1], out);
            }} else {{
                float frac = (target_len - accum) / unit_len;
                out[0] = pts[k + 1][0] + (pts[k][0] - pts[k + 1][0]) * frac;
                out[1] = pts[k + 1][1] + (pts[k][1] - pts[k + 1][1]) * frac;
            }}
            return;
        }}
    }}
    glm_vec2_zero(out);
}}

static void ref_bezier_exact_4096(vec2 p0, vec2 p1, vec2 p2, int step_idx, vec2 out) {{
    if (step_idx > 1024) {{ glm_vec2_zero(out); return; }}
    if (step_idx <= 0) {{ glm_vec2_copy(p0, out); return; }}
    enum {{ M = 4096 }};
    static vec2 pts[M + 1];
    static float cum[M + 1];
    const float s_max = 1023.0f / 1024.0f;
    cum[0] = 0.0f;
    for (int k = 0; k <= M; k++) {{
        float s = s_max * (float)k / (float)M;
        float om = 1.0f - s;
        pts[k][0] = om * om * p0[0] + 2.0f * s * om * p1[0] + s * s * p2[0];
        pts[k][1] = om * om * p0[1] + 2.0f * s * om * p1[1] + s * s * p2[1];
        if (k > 0) {{
            float dx = pts[k][0] - pts[k - 1][0];
            float dy = pts[k][1] - pts[k - 1][1];
            cum[k] = cum[k - 1] + sqrtf(dx * dx + dy * dy);
        }}
    }}
    if (cum[M] == 0.0f) {{ glm_vec2_copy(p0, out); return; }}
    float target = cum[M] * ((float)step_idx / 1024.0f);
    int lo = 0, hi = M;
    while (lo + 1 < hi) {{
        int mid = (lo + hi) >> 1;
        if (cum[mid] < target) lo = mid; else hi = mid;
    }}
    float seg = cum[lo + 1] - cum[lo];
    float frac = seg > 0.0f ? (target - cum[lo]) / seg : 0.0f;
    out[0] = pts[lo][0] + (pts[lo + 1][0] - pts[lo][0]) * frac;
    out[1] = pts[lo][1] + (pts[lo + 1][1] - pts[lo][1]) * frac;
}}

int main(void) {{
    srand(12345);
    vec2 curves[1000][3];
    for (int i = 0; i < 1000; i++) {{
        for (int j = 0; j < 3; j++) {{
            curves[i][j][0] = ((float)rand() / (float)RAND_MAX) * 1280.0f;
            curves[i][j][1] = ((float)rand() / (float)RAND_MAX) * 720.0f;
        }}
    }}
    // Explicit edge cases: straight line, 180-deg hairpin cusp, zero-length degenerate curve.
    curves[0][0][0] = 0; curves[0][0][1] = 0;
    curves[0][1][0] = 500; curves[0][1][1] = 0;
    curves[0][2][0] = 1000; curves[0][2][1] = 0;

    curves[1][0][0] = 100; curves[1][0][1] = 100;
    curves[1][1][0] = 1100; curves[1][1][1] = 100;
    curves[1][2][0] = 100; curves[1][2][1] = 101;

    curves[2][0][0] = 77; curves[2][0][1] = 88;
    curves[2][1][0] = 77; curves[2][1][1] = 88;
    curves[2][2][0] = 77; curves[2][2][1] = 88;

    // Out-of-range step_idx > 1024 returns (0, 0) matching reference.
    vec2 oob;
    eval_quadratic_bezier_arclen(curves[0][0], curves[0][1], curves[0][2], 1025, oob);
    assert(oob[0] == 0.0f && oob[1] == 0.0f);

    double max_err = 0.0;
    double sum_err = 0.0;
    int samples = 0;
    for (int i = 0; i < 1000; i++) {{
        for (int st = 0; st <= 1024; st += 16) {{
            vec2 expected, actual;
            ref_bezier_exact_4096(curves[i][0], curves[i][1], curves[i][2], st, expected);
            eval_quadratic_bezier_arclen(curves[i][0], curves[i][1], curves[i][2], st, actual);
            double err = hypot((double)actual[0] - expected[0], (double)actual[1] - expected[1]);
            if (err > max_err) max_err = err;
            sum_err += err;
            samples++;
        }}
    }}
    double avg_err = sum_err / samples;
    assert(max_err < 0.2);

    // Benchmark 10,000 evaluations vs original 1024-step loop.
    struct timespec t0, t1, t2;
    volatile float sink = 0.0f;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int rep = 0; rep < 10; rep++) {{
        for (int i = 0; i < 1000; i++) {{
            vec2 o;
            ref_bezier_1024_orig(curves[i][0], curves[i][1], curves[i][2], (i * 13 + rep) % 1025, o);
            sink += o[0] + o[1];
        }}
    }}
    clock_gettime(CLOCK_MONOTONIC, &t1);
    for (int rep = 0; rep < 10; rep++) {{
        for (int i = 0; i < 1000; i++) {{
            vec2 o;
            eval_quadratic_bezier_arclen(curves[i][0], curves[i][1], curves[i][2], (i * 13 + rep) % 1025, o);
            sink += o[0] + o[1];
        }}
    }}
    clock_gettime(CLOCK_MONOTONIC, &t2);
    double ref_ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) * 1e-6;
    double fast_ms = (t2.tv_sec - t1.tv_sec) * 1e3 + (t2.tv_nsec - t1.tv_nsec) * 1e-6;
    double speedup = ref_ms / (fast_ms > 1e-6 ? fast_ms : 1e-6);
    printf("BEZIER max_err=%.4fpx avg_err=%.5fpx ref=%.2fms fast=%.2fms speedup=%.1fx\\n",
           max_err, avg_err, ref_ms, fast_ms, speedup);
    assert(speedup > 10.0);
    return 0;
}}
"""
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "bezier_test.c"
            exe = Path(tmp) / "bezier_test"
            src.write_text(harness, encoding="utf-8")
            subprocess.run(
                [CC, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", str(src), "-lm", "-o", str(exe)],
                check=True,
            )
            res = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
            self.assertIn("BEZIER", res.stdout)

    def test_flat_preresolved_lib_idx_and_talt_atlas_gles3(self):
        env_paths = find_gles_env()
        if not env_paths:
            self.skipTest("SDL2 / cglm / GLES3 headers not available")
        sdl2_inc, cglm_inc, gles_inc = env_paths

        harness = r'''
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cglm/cglm.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/flat.h"
#include "system4/hashtable.h"
#include "system4/string.h"

#include "asset_manager.h"
#include "gfx/gfx.h"
#include "gfx/private.h"
#include "parts.h"
#include "parts/parts_internal.h"
#include "reign.h"
#include "sact.h"
#include "scene.h"
#include "sprite.h"
#include "xsystem4.h"

struct config config = { .view_width = 128, .view_height = 128 };
struct ain test_ain = { .version = 12 };
struct ain *ain = &test_ain;
bool game_ixseal = false;
bool game_okusama = false;
bool parts_v2 = true;
bool parts_hierarchical_matrix = false;
bool parts_frame_did_heavy_load = false;
bool scene_is_dirty = false;
bool sys4_text_is_gbk = false;
static struct parts_context g_parts_ctx = {
    .list = TAILQ_HEAD_INITIALIZER(g_parts_ctx.list),
    .dirty = TAILQ_HEAD_INITIALIZER(g_parts_ctx.dirty),
};
struct parts_context *parts_current = &g_parts_ctx;
static struct string empty_str_backing = { .size = 0, .text = "" };
struct string EMPTY_STRING = { .size = 0, .text = "" };

void *_xmalloc(size_t n, const char *f) { (void)f; void *p = malloc(n ? n : 1); assert(p); return p; }
void *_xcalloc(size_t n, size_t z, const char *f) { (void)f; void *p = calloc(n ? n : 1, z ? z : 1); assert(p); return p; }
void *_xrealloc(void *old, size_t n, const char *f) { (void)f; void *p = realloc(old, n ? n : 1); assert(p); return p; }
char *_xstrdup(const char *s, const char *f) { (void)f; char *p = strdup(s); assert(p); return p; }
_Noreturn void sys_error(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); abort();
}
void sys_warning(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}
void sys_message(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stdout, fmt, ap); va_end(ap);
}
char *sjis_filename2utf(const char *src, size_t len) { (void)src; (void)len; return strdup("flat-test"); }
const char *display_sjis0(const char *s) { return s; }
void modifier_on_frame(void) {}
void icon_init(void) {}
void icon_fini(void) {}

struct string *make_string(const char *str, size_t len) {
    struct string *s = calloc(1, sizeof(struct string) + len + 1);
    s->size = (int)len;
    s->ref = 1;
    if (len) memcpy(s->text, str, len);
    s->text[len] = '\0';
    return s;
}
struct string *string_ref(struct string *s) {
    if (s && s != &EMPTY_STRING && s != &empty_str_backing) s->ref++;
    return s;
}
struct string *string_dup(const struct string *in) {
    return make_string(in->text, (size_t)in->size);
}
void free_string(struct string *s) {
    if (!s || s == &EMPTY_STRING || s == &empty_str_backing) return;
    if (s->ref <= 1) free(s);
    else s->ref--;
}

// Track texture lifecycle via glGenTextures/glDeleteTextures to prove
// single-owner atlas upload & deletion without inlining issues.
static int g_tex_creates = 0;
static int g_tex_deletes = 0;
static GLuint g_deleted_handles[256];
static int g_deleted_count = 0;

void __real_glGenTextures(GLsizei n, GLuint *textures);
void __wrap_glGenTextures(GLsizei n, GLuint *textures) {
    g_tex_creates += n;
    __real_glGenTextures(n, textures);
}
void __real_gfx_delete_texture(struct texture *t);
void __wrap_gfx_delete_texture(struct texture *t) {
    if (t && t->handle) {
        for (int i = 0; i < g_deleted_count; i++) {
            if (g_deleted_handles[i] == t->handle && glIsTexture(t->handle)) {
                fprintf(stderr, "DOUBLE DELETE detected for handle %u\n", t->handle);
                abort();
            }
        }
        if (g_deleted_count < 256)
            g_deleted_handles[g_deleted_count++] = t->handle;
        g_tex_deletes++;
    }
    __real_gfx_delete_texture(t);
}

// Track parts_flat_find_library calls so we can prove 0 linear scans per frame.
static int g_find_lib_calls = 0;
int __real_parts_flat_find_library(struct flat *fl, const char *name);
int __wrap_parts_flat_find_library(struct flat *fl, const char *name) {
    g_find_lib_calls++;
    return __real_parts_flat_find_library(fl, name);
}

// Synthetic asset/CG hooks:
// Magic 16-byte raw RGBA buffer format for test CGs: "RAW4" + w(u32) + h(u32) + has_alpha(u32) + RGBA pixels
static uint8_t *make_raw_cg(int w, int h, bool has_alpha, const uint8_t *rgba, size_t *out_sz) {
    size_t sz = 16 + (size_t)w * (size_t)h * 4;
    uint8_t *buf = malloc(sz);
    memcpy(buf, "RAW4", 4);
    uint32_t hdr[3] = { (uint32_t)w, (uint32_t)h, has_alpha ? 1u : 0u };
    memcpy(buf + 4, hdr, 12);
    memcpy(buf + 16, rgba, (size_t)w * (size_t)h * 4);
    *out_sz = sz;
    return buf;
}

struct cg *cg_load_buffer(uint8_t *data, size_t size) {
    if (!data || size < 16 || memcmp(data, "RAW4", 4) != 0)
        return NULL;
    uint32_t hdr[3];
    memcpy(hdr, data + 4, 12);
    size_t px_sz = (size_t)hdr[0] * (size_t)hdr[1] * 4;
    if (size < 16 + px_sz) return NULL;
    struct cg *cg = calloc(1, sizeof(*cg));
    cg->metrics.w = (int)hdr[0];
    cg->metrics.h = (int)hdr[1];
    cg->metrics.has_alpha = hdr[2] != 0;
    cg->metrics.has_pixel = true;
    cg->pixels = malloc(px_sz);
    memcpy(cg->pixels, data + 16, px_sz);
    return cg;
}
void cg_free(struct cg *cg) {
    if (!cg) return;
    free(cg->pixels);
    free(cg);
}

// External CG override fixture ("ext_blue")
struct cg *asset_cg_load_by_name(const char *name, int *no) {
    if (no) *no = 0;
    if (strcmp(name, "ext_blue") != 0)
        return NULL;
    struct cg *cg = calloc(1, sizeof(*cg));
    cg->metrics.w = 10;
    cg->metrics.h = 10;
    cg->metrics.has_alpha = true;
    cg->metrics.has_pixel = true;
    cg->pixels = malloc(10 * 10 * 4);
    uint8_t *p = cg->pixels;
    for (int i = 0; i < 100; i++) {
        p[i*4+0] = 20; p[i*4+1] = 40; p[i*4+2] = 240; p[i*4+3] = 255;
    }
    return cg;
}

// Synthetic flat_open hook: returns a prebuilt struct flat populated with LIBL + TALT + nested timelines + emitter
static uint8_t *g_talt_atlas_buf = NULL;
static size_t g_talt_atlas_sz = 0;
static uint8_t *g_standalone_cg_buf = NULL;
static size_t g_standalone_cg_sz = 0;

static void fake_archive_free_data(struct archive_data *data) {
    if (!data) return;
    free(data->data);
    free(data);
}
static struct archive_ops fake_archive_ops = {
    .free_data = fake_archive_free_data,
};
static struct archive fake_archive = {
    .ops = &fake_archive_ops,
};

struct archive_data *asset_get_by_name(enum asset_type type, const char *name, int *no) {
    (void)type; (void)no;
    if (strcmp(name, "synthetic_talt.flat") != 0)
        return NULL;
    // Pack: [g_talt_atlas_buf] [names: "talt_red", "talt_green", "talt_gold"]
    const char *n0 = "talt_red";
    const char *n1 = "talt_green";
    const char *n2 = "talt_gold";
    size_t l0 = strlen(n0), l1 = strlen(n1), l2 = strlen(n2);
    size_t total = g_talt_atlas_sz + l0 + l1 + l2;
    uint8_t *blob = calloc(1, total);
    memcpy(blob, g_talt_atlas_buf, g_talt_atlas_sz);
    size_t o0 = g_talt_atlas_sz;
    size_t o1 = o0 + l0;
    size_t o2 = o1 + l1;
    memcpy(blob + o0, n0, l0);
    memcpy(blob + o1, n1, l1);
    memcpy(blob + o2, n2, l2);
    struct archive_data *d = calloc(1, sizeof(*d));
    d->data = blob;
    d->size = total;
    d->archive = &fake_archive;
    return d;
}
bool asset_exists_by_name(enum asset_type type, const char *name, int *no) {
    (void)type; (void)no;
    return strcmp(name, "synthetic_talt.flat") == 0;
}

static struct flat_key_data_graphic make_key(float x, float y, int area_x, int area_y, int area_w, int area_h, int uk2) {
    struct flat_key_data_graphic k = {0};
    k.pos_x = x; k.pos_y = y;
    k.scale_x = 1.0f; k.scale_y = 1.0f;
    k.mul_r = 255; k.mul_g = 255; k.mul_b = 255;
    k.alpha = 255;
    k.area_x = area_x; k.area_y = area_y;
    k.area_width = area_w; k.area_height = area_h;
    k.uk2 = uk2;
    return k;
}

struct flat *flat_open(uint8_t *data, size_t size, int *error) {
    if (error) *error = 0;
    struct flat *fl = calloc(1, sizeof(*fl));
    fl->data = data;
    fl->data_size = size;
    fl->hdr.fps = 60;
    fl->hdr.meter = 128.0f;
    fl->hdr.game_view_width = 128;
    fl->hdr.game_view_height = 128;
    fl->hdr.width = 128;
    fl->hdr.height = 128;
    fl->hdr.version = 11;

    // 4 LIBL entries:
    //   0: "lib_plain" (standalone CG 8x8 cyan)
    //   1: "lib_plain" (DUPLICATE name -> first-match must pick 0!)
    //   2: "sub_tl"    (FLAT_LIB_TIMELINE referencing "talt_green" and "missing_lib")
    //   3: "em_gold"   (FLAT_LIB_EMITTER referencing "talt_gold")
    // Plus 3 TALT entries appended at indices 4, 5, 6:
    //   4: "talt_red"   at (4, 4, 16, 12)
    //   5: "talt_green" at (28, 6, 14, 18)
    //   6: "talt_gold"  at (48, 32, 12, 12)
    fl->nr_libraries = 4;
    fl->libraries = calloc(4, sizeof(struct flat_library));

    fl->libraries[0].name = make_string("lib_plain", 9);
    fl->libraries[0].type = FLAT_LIB_CG;
    fl->libraries[0].cg.data = g_standalone_cg_buf;
    fl->libraries[0].cg.size = g_standalone_cg_sz;

    fl->libraries[1].name = make_string("lib_plain", 9);
    fl->libraries[1].type = FLAT_LIB_CG;
    fl->libraries[1].cg.data = g_standalone_cg_buf;
    fl->libraries[1].cg.size = g_standalone_cg_sz;

    // Nested timeline library "sub_tl" with 2 graphic timelines:
    //  [0] "child_layer" -> "talt_green" (drawn at (40, 20) with out-of-bounds area crop (-2, 2, 18, 10) -> must clamp to [0..14) x [2..12)!)
    //  [1] "missing_layer" -> "nonexistent_lib" (must resolve to -1)
    fl->libraries[2].name = make_string("sub_tl", 6);
    fl->libraries[2].type = FLAT_LIB_TIMELINE;
    fl->libraries[2].timeline.nr_timelines = 2;
    fl->libraries[2].timeline.timelines = calloc(2, sizeof(struct flat_timeline));

    struct flat_timeline *ctl0 = &fl->libraries[2].timeline.timelines[0];
    ctl0->name = make_string("child_layer", 11);
    ctl0->library_name = make_string("talt_green", 10);
    ctl0->type = FLAT_TIMELINE_GRAPHIC;
    ctl0->begin_frame = 0;
    ctl0->frame_count = 5;
    ctl0->graphic.count = 5;
    ctl0->graphic.keys = calloc(5, sizeof(struct flat_key_data_graphic));
    for (int f = 0; f < 5; f++)
        ctl0->graphic.keys[f] = make_key(40.0f, 20.0f, -2, 2, 18, 10, 0);

    struct flat_timeline *ctl1 = &fl->libraries[2].timeline.timelines[1];
    ctl1->name = make_string("missing_layer", 13);
    ctl1->library_name = make_string("nonexistent_lib", 15);
    ctl1->type = FLAT_TIMELINE_GRAPHIC;
    ctl1->begin_frame = 0;
    ctl1->frame_count = 5;
    ctl1->graphic.count = 5;
    ctl1->graphic.keys = calloc(5, sizeof(struct flat_key_data_graphic));
    for (int f = 0; f < 5; f++)
        ctl1->graphic.keys[f] = make_key(0.0f, 0.0f, 0, 0, 0, 0, 0);

    // Emitter "em_gold" spawning 1 stationary particle using "talt_gold" (12x12) with particle_align=5 (center)
    fl->libraries[3].name = make_string("em_gold", 7);
    fl->libraries[3].type = FLAT_LIB_EMITTER;
    fl->libraries[3].emitter = (struct flat_emitter){
        .library_name = make_string("talt_gold", 9),
        .particle_align = 5,
        .create_count = 1,
        .particle_lifetime = 5,
        .direction_type = 1,
        .pos_track_mode = 1,
        .parent_key_mode = 1,
        .begin_scale = 1.0f, .end_scale = 1.0f,
        .begin_x_scale = 1.0f, .end_x_scale = 1.0f,
        .begin_y_scale = 1.0f, .end_y_scale = 1.0f,
    };

    // Root timelines:
    //  [0] "layer_red"   -> "talt_red" at (10, 10)
    //  [1] "layer_sub"   -> "sub_tl" at (0, 0)
    //  [2] "layer_emit"  -> "em_gold" at (90, 90)
    //  [3] "layer_plain" -> "lib_plain" at (10, 80)
    fl->nr_timelines = 4;
    fl->timelines = calloc(4, sizeof(struct flat_timeline));

    const char *tnames[4] = { "layer_red", "layer_sub", "layer_emit", "layer_plain" };
    const char *lnames[4] = { "talt_red", "sub_tl", "em_gold", "lib_plain" };
    float xs[4] = { 10.0f, 0.0f, 90.0f, 10.0f };
    float ys[4] = { 10.0f, 0.0f, 90.0f, 80.0f };
    for (int t = 0; t < 4; t++) {
        fl->timelines[t].name = make_string(tnames[t], (unsigned)strlen(tnames[t]));
        fl->timelines[t].library_name = make_string(lnames[t], (unsigned)strlen(lnames[t]));
        fl->timelines[t].type = FLAT_TIMELINE_GRAPHIC;
        fl->timelines[t].begin_frame = 0;
        fl->timelines[t].frame_count = 5;
        fl->timelines[t].graphic.count = 5;
        fl->timelines[t].graphic.keys = calloc(5, sizeof(struct flat_key_data_graphic));
        for (int f = 0; f < 5; f++)
            fl->timelines[t].graphic.keys[f] = make_key(xs[t], ys[t], 0, 0, 0, 0, 0);
    }

    // 1 TALT entry with 3 sub-rects
    fl->nr_talt_entries = 1;
    fl->talt_entries = calloc(1, sizeof(struct talt_entry));
    fl->talt_entries[0].off = 0;
    fl->talt_entries[0].size = (uint32_t)g_talt_atlas_sz;
    fl->talt_entries[0].nr_meta = 3;
    fl->talt_entries[0].metadata = calloc(3, sizeof(struct talt_metadata));

    size_t o0 = g_talt_atlas_sz;
    size_t o1 = o0 + 8;  // "talt_red"
    size_t o2 = o1 + 10; // "talt_green"
    fl->talt_entries[0].metadata[0] = (struct talt_metadata){ 8,  (uint32_t)o0, 4,  4,  16, 12 };
    fl->talt_entries[0].metadata[1] = (struct talt_metadata){ 10, (uint32_t)o1, 28, 6,  14, 18 };
    fl->talt_entries[0].metadata[2] = (struct talt_metadata){ 9,  (uint32_t)o2, 48, 32, 12, 12 };
    return fl;
}

void flat_free(struct flat *fl) {
    if (!fl) return;
    for (size_t i = 0; i < fl->nr_timelines; i++) {
        free_string(fl->timelines[i].name);
        free_string(fl->timelines[i].library_name);
        free(fl->timelines[i].graphic.keys);
    }
    free(fl->timelines);
    for (size_t i = 0; i < fl->nr_libraries; i++) {
        free_string(fl->libraries[i].name);
        if (fl->libraries[i].type == FLAT_LIB_TIMELINE) {
            for (size_t j = 0; j < fl->libraries[i].timeline.nr_timelines; j++) {
                free_string(fl->libraries[i].timeline.timelines[j].name);
                free_string(fl->libraries[i].timeline.timelines[j].library_name);
                free(fl->libraries[i].timeline.timelines[j].graphic.keys);
            }
            free(fl->libraries[i].timeline.timelines);
        } else if (fl->libraries[i].type == FLAT_LIB_EMITTER) {
            free_string(fl->libraries[i].emitter.library_name);
        }
    }
    free(fl->libraries);
    for (size_t i = 0; i < fl->nr_talt_entries; i++)
        free(fl->talt_entries[i].metadata);
    free(fl->talt_entries);
    free(fl);
}

// Minimal Parts stubs required by flat.c & render.c
static struct parts g_part;
struct parts *parts_try_get(int no) { return no == 1 ? &g_part : NULL; }
struct parts *parts_get(int no) { assert(no == 1); return &g_part; }
struct parts_flat *parts_get_flat(struct parts *p, int s) {
    p->states[s].type = PARTS_FLAT;
    return &p->states[s].flat;
}
void parts_set_dims(struct parts *p, struct parts_common *c, int w, int h) { (void)p; c->w = w; c->h = h; }
void parts_set_surface_area(struct parts *p, struct parts_common *c, int x, int y, int w, int h) {
    (void)p; c->surface_area = (Rectangle){ x, y, w, h };
}
void parts_world_matrix(struct parts *p, mat4 out) { (void)p; glm_mat4_identity(out); }
bool parts_is_visible(struct parts *p) { (void)p; return true; }
struct parts_state *parts_display_state(struct parts *p) { return &p->states[p->state]; }
int parts_get_modifier_font_scale(void) { return 100; }
int parts_get_modifier_scrollbar_scale(void) { return 100; }
bool parts_build_construction_process(struct parts *p, struct parts_construction_process *c) { (void)p; (void)c; return true; }
int parts_scrollbar_range(struct parts_scrollbar *s) { (void)s; return 0; }
int parts_scrollbar_skin_state(struct parts_scrollbar *s, int piece) { (void)s; (void)piece; return 0; }
void parts_scrollbar_rects(struct parts *p, Rectangle r[4]) { (void)p; (void)r; }
void parts_widget_get_size(struct parts *p, int *w, int *h) { (void)p; if (w) *w = 0; if (h) *h = 0; }
struct sact_sprite *sact_try_get_sprite(int sp) { (void)sp; return NULL; }
struct texture *sprite_get_texture(struct sact_sprite *sp) { (void)sp; return NULL; }
struct RE_plugin *RE_get_plugin(int p) { (void)p; return NULL; }
void parts_text_box_set_display(struct parts *p, const char *c) { (void)p; (void)c; }

extern void parts_render_init(void);
extern void test_render_flat(struct parts *p, struct parts_flat *f);

static void render_to_target(Texture *target, struct parts *p, struct parts_flat *f) {
    GLuint fb = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, target, 0, 0, 128, 128);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    test_render_flat(p, f);
    gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fb);
}

static void expect_px(const uint8_t *got, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t *p = got + (y * 128 + x) * 4;
    if (p[0] != r || p[1] != g || p[2] != b) {
        fprintf(stderr, "pixel (%d,%d): got (%u,%u,%u,%u) expected (%u,%u,%u)\n",
                x, y, p[0], p[1], p[2], p[3], r, g, b);
        abort();
    }
}

int main(void) {
    gfx_init();
    parts_render_init();

    // Build 64x64 TALT atlas filled with bright magenta sentinel (255,0,255,255) outside sub-rects
    // so ANY bleed outside sub-rects is immediately caught!
    uint8_t atlas_px[64 * 64 * 4];
    for (int i = 0; i < 64 * 64; i++) {
        atlas_px[i*4+0] = 255; atlas_px[i*4+1] = 0; atlas_px[i*4+2] = 255; atlas_px[i*4+3] = 255;
    }
    // talt_red: (4,4,16,12) = (220, 30, 30, 255)
    for (int y = 4; y < 16; y++) for (int x = 4; x < 20; x++) {
        int i = (y * 64 + x) * 4;
        atlas_px[i+0] = 220; atlas_px[i+1] = 30; atlas_px[i+2] = 30; atlas_px[i+3] = 255;
    }
    // talt_green: (28,6,14,18) = (30, 210, 40, 255)
    for (int y = 6; y < 24; y++) for (int x = 28; x < 42; x++) {
        int i = (y * 64 + x) * 4;
        atlas_px[i+0] = 30; atlas_px[i+1] = 210; atlas_px[i+2] = 40; atlas_px[i+3] = 255;
    }
    // talt_gold: (48,32,12,12) = (240, 200, 20, 255)
    for (int y = 32; y < 44; y++) for (int x = 48; x < 60; x++) {
        int i = (y * 64 + x) * 4;
        atlas_px[i+0] = 240; atlas_px[i+1] = 200; atlas_px[i+2] = 20; atlas_px[i+3] = 255;
    }
    g_talt_atlas_buf = make_raw_cg(64, 64, true, atlas_px, &g_talt_atlas_sz);

    // Standalone 8x8 cyan CG (0, 200, 220, 255)
    uint8_t plain_px[8 * 8 * 4];
    for (int i = 0; i < 64; i++) {
        plain_px[i*4+0] = 0; plain_px[i*4+1] = 200; plain_px[i*4+2] = 220; plain_px[i*4+3] = 255;
    }
    g_standalone_cg_buf = make_raw_cg(8, 8, true, plain_px, &g_standalone_cg_sz);

    // Prepare parts
    memset(&g_part, 0, sizeof(g_part));
    g_part.local.scale.x = g_part.local.scale.y = 1.0f;
    g_part.global.scale.x = g_part.global.scale.y = 1.0f;
    g_part.global.alpha = 255;
    g_part.global.multiply_color = (SDL_Color){ 255, 255, 255, 255 };
    g_part.states[0].type = PARTS_FLAT;

    Texture target = {0};
    gfx_init_texture_blank(&target, 128, 128);

    // Load synthetic TALT flat
    g_tex_creates = 0;
    g_tex_deletes = 0;
    g_deleted_count = 0;
    struct string *fname = make_string("synthetic_talt.flat", 19);
    assert(parts_flat_load(&g_part, &g_part.states[0].flat, fname));
    struct parts_flat *f = &g_part.states[0].flat;
    struct parts_flat_asset *a = f->asset;
    assert(a != NULL);

    // 2 standalone LIBL CGs (indices 0, 1) + 1 shared TALT atlas (for 3 TALT entries 4, 5, 6) = 3 textures total!
    assert(a->nr_talt_atlases == 1);
    assert(g_tex_creates == 3);
    // Verify logical sub-rect dims & shared atlas handle
    assert(f->textures[4].w == 16 && f->textures[4].h == 12);
    assert(f->textures[5].w == 14 && f->textures[5].h == 18);
    assert(f->textures[6].w == 12 && f->textures[6].h == 12);
    assert(f->textures[4].handle == a->talt_atlases[0].handle);
    assert(f->textures[5].handle == a->talt_atlases[0].handle);
    assert(f->textures[6].handle == a->talt_atlases[0].handle);
    assert(a->talt_atlases[0].w == 64 && a->talt_atlases[0].h == 64);

    // Verify pre-resolved lib_idx matches parts_flat_find_library for every timeline & emitter (including duplicate & missing)
    assert(a->root_timeline_lib_idx[0] == __real_parts_flat_find_library(f->flat, "talt_red") && a->root_timeline_lib_idx[0] == 4);
    assert(a->root_timeline_lib_idx[1] == __real_parts_flat_find_library(f->flat, "sub_tl") && a->root_timeline_lib_idx[1] == 2);
    assert(a->root_timeline_lib_idx[2] == __real_parts_flat_find_library(f->flat, "em_gold") && a->root_timeline_lib_idx[2] == 3);
    assert(a->root_timeline_lib_idx[3] == __real_parts_flat_find_library(f->flat, "lib_plain") && a->root_timeline_lib_idx[3] == 0);
    assert(a->lib_timeline_lib_idx[2][0] == 5);
    assert(a->lib_timeline_lib_idx[2][1] == -1);
    assert(a->emitter_cg_lib_idx[3] == 6);

    // Verify ZERO parts_flat_find_library calls across update + render + hittest + layer enable!
    g_find_lib_calls = 0;
    f->fps_rate = 1.0f;
    parts_flat_update(f, 0);   // consumes needs_advance and spawns child state for sub_tl
    parts_flat_update(f, 20);  // advances 1 frame
    render_to_target(&target, &g_part, f);
    assert(parts_flat_hittest(f, 15.0f, 15.0f));    // inside talt_red (10..26, 10..22)
    assert(!parts_flat_hittest(f, 30.0f, 15.0f));   // outside talt_red (would falsely hit if atlas 64x64 used!)
    assert(parts_flat_hittest(f, 45.0f, 24.0f));    // inside nested child_layer
    struct string *clname = make_string("child_layer", 11);
    assert(PE_IsFlatEnableLayer(1, clname, 1));
    PE_SetFlatEnableLayer(1, clname, false, 1);
    assert(!PE_IsFlatEnableLayer(1, clname, 1));
    PE_SetFlatEnableLayer(1, clname, true, 1);
    assert(g_find_lib_calls == 0);
    free_string(clname);

    // Verify exact rendered pixels (ZERO magenta sentinel bleed!)
    uint8_t *got = gfx_get_pixels(&target);
    // 1. talt_red at (10..26, 10..22)
    expect_px(got, 10, 10, 220, 30, 30);
    expect_px(got, 25, 21, 220, 30, 30);
    expect_px(got, 9,  10, 0,   0,  0);
    expect_px(got, 26, 10, 0,   0,  0);
    // 2. nested child_layer (talt_green 14x18 at (40,20) with area_x=-2, area_y=2, area_w=18, area_h=10):
    //    clamped local [0..14) x [2..12), shifted by (-area_x, -area_y) = (+2, -2) -> on-screen (42..56, 20..30)!
    expect_px(got, 41, 22, 0,  0,   0);   // left of clamped area (no magenta bleed!)
    expect_px(got, 42, 20, 30, 210, 40);
    expect_px(got, 55, 29, 30, 210, 40);
    expect_px(got, 56, 25, 0,  0,   0);   // right of clamped area (no magenta bleed!)
    expect_px(got, 45, 30, 0,  0,   0);
    // 3. emitter em_gold at (90, 90) centered (align=5) -> 12x12 at (84..96, 84..96)
    expect_px(got, 84, 84, 240, 200, 20);
    expect_px(got, 95, 95, 240, 200, 20);
    expect_px(got, 83, 90, 0,   0,   0);
    expect_px(got, 96, 90, 0,   0,   0);
    // 4. standalone lib_plain at (10..18, 80..88)
    expect_px(got, 10, 80, 0, 200, 220);
    expect_px(got, 17, 87, 0, 200, 220);
    free(got);

    // Test PE_ChangeFlatCG:
    // (a) external standalone CG replacing TALT entry "talt_red" -> draws blue (10x10 at 10..20, 10..20)
    struct string *s_red = make_string("talt_red", 8);
    struct string *s_ext = make_string("ext_blue", 8);
    struct string *s_gold = make_string("talt_gold", 9);
    PE_ChangeFlatCG(1, s_red, s_ext, 1);
    render_to_target(&target, &g_part, f);
    got = gfx_get_pixels(&target);
    expect_px(got, 12, 12, 20, 40, 240);
    expect_px(got, 22, 12, 0,  0,  0);
    free(got);

    // (b) internal TALT redirect: replace "talt_red" with "talt_gold" (12x12 gold at 10..22, 10..22)
    PE_ChangeFlatCG(1, s_red, s_gold, 1);
    render_to_target(&target, &g_part, f);
    got = gfx_get_pixels(&target);
    expect_px(got, 12, 12, 240, 200, 20);
    expect_px(got, 21, 21, 240, 200, 20);
    expect_px(got, 22, 12, 0,   0,   0);
    free(got);

    // (c) restore via PE_ReturnChangeFlatCG -> back to red (16x12 at 10..26, 10..22)
    PE_ReturnChangeFlatCG(1, s_red, 1);
    render_to_target(&target, &g_part, f);
    got = gfx_get_pixels(&target);
    expect_px(got, 12, 12, 220, 30, 30);
    expect_px(got, 25, 21, 220, 30, 30);
    free(got);
    free_string(s_red); free_string(s_ext); free_string(s_gold);

    // Test unbacked GL context reset + parts_flat_recover_widget_skin on TALT shared atlas!
    gfx_release_context_backup();
    SDL_GLContext old_ctx = sdl.gl.context;
    assert(SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0) == 0);
    SDL_GLContext rep_ctx = SDL_GL_CreateContext(sdl.window);
    assert(rep_ctx && rep_ctx != old_ctx);
    gfx_restore_window();
    parts_flat_recover_widget_skin(f, false);
    // Recreate target in new context and verify recovered TALT atlas renders pixel-exact!
    gfx_init_texture_blank(&target, 128, 128);
    render_to_target(&target, &g_part, f);
    got = gfx_get_pixels(&target);
    expect_px(got, 12, 12, 220, 30, 30);
    expect_px(got, 42, 20, 30, 210, 40);
    expect_px(got, 84, 84, 240, 200, 20);
    expect_px(got, 10, 80, 0, 200, 220);
    free(got);
    SDL_GL_DeleteContext(old_ctx);

    // Verify single-owner cleanup (ZERO double-delete when freeing flat + idle eviction on GL reset!)
    int deletes_before = g_tex_deletes;
    parts_flat_free(f);
    assert(g_tex_deletes == deletes_before); // borrowed textures NOT deleted on instance free (asset now idle, ref==0)

    // Unbacked GL context reset while asset is idle (ref==0) MUST evict the dead-texture asset!
    gfx_release_context_backup();
    SDL_GLContext old_ctx2 = sdl.gl.context;
    assert(SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0) == 0);
    SDL_GLContext rep_ctx2 = SDL_GL_CreateContext(sdl.window);
    assert(rep_ctx2 && rep_ctx2 != old_ctx2);
    gfx_restore_window();
    SDL_GL_DeleteContext(old_ctx2);

    // Reloading after idle eviction must create fresh live textures (3 new uploads) and render pixel-exact!
    int creates_before_reload = g_tex_creates;
    memset(&g_part.states[0].flat, 0, sizeof(g_part.states[0].flat));
    assert(parts_flat_load(&g_part, &g_part.states[0].flat, fname));
    f = &g_part.states[0].flat;
    assert(g_tex_creates == creates_before_reload + 3);
    assert(f->asset && f->asset->talt_atlases[0].handle != 0);
    gfx_init_texture_blank(&target, 128, 128);
    f->fps_rate = 1.0f;
    parts_flat_update(f, 0);
    parts_flat_update(f, 20);
    render_to_target(&target, &g_part, f);
    got = gfx_get_pixels(&target);
    expect_px(got, 12, 12, 220, 30, 30);
    expect_px(got, 42, 20, 30, 210, 40);
    expect_px(got, 84, 84, 240, 200, 20);
    expect_px(got, 10, 80, 0, 200, 220);
    free(got);

    deletes_before = g_tex_deletes;
    parts_flat_free(f);
    assert(g_tex_deletes == deletes_before); // borrowed textures NOT deleted on instance free
    parts_flat_cache_clear();
    // Exactly 3 owned asset textures deleted once: 2 standalone LIBL CGs + 1 shared TALT atlas!
    assert(g_tex_deletes == deletes_before + 3);

    gfx_delete_texture(&target);
    free_string(fname);
    free(g_talt_atlas_buf);
    free(g_standalone_cg_buf);
    assert(glGetError() == GL_NO_ERROR);
    puts("PASS: FLAT pre-resolved lib_idx (0 scans/frame), TALT shared atlas UV sub-rects, overrides, GL reset, single-owner cleanup");
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            tdir = Path(tmp)
            src = tdir / "flat_talt_gles.c"
            rwrap = tdir / "render_wrap.c"
            exe = tdir / "flat_talt_gles"
            src.write_text(harness, encoding="utf-8")
            rwrap.write_text(
                '#include "parts/render.c"\n'
                'void test_render_flat(struct parts *p, struct parts_flat *f) { parts_render_flat(p, f); }\n',
                encoding="utf-8",
            )
            cmd = [
                CC, "-std=gnu11", "-O0", "-g",
                "-ffunction-sections", "-fdata-sections",
                "-DUSE_GLES", f'-DXSYS4_DATA_DIR="{ROOT}"',
                f"-I{sdl2_inc}", f"-I{cglm_inc}", f"-I{gles_inc}",
                f"-I{ROOT / 'include'}", f"-I{ROOT / 'src'}",
                f"-I{ROOT / 'subprojects/libsys4/include'}",
                str(src), str(rwrap),
                str(ROOT / "src/video.c"),
                str(ROOT / "src/draw.c"),
                str(ROOT / "src/parts/flat.c"),
                str(ROOT / "subprojects/libsys4/src/mt19937int.c"),
                str(ROOT / "subprojects/libsys4/src/hashtable.c"),
                "-Wl,--gc-sections",
                "-Wl,--wrap=glGenTextures",
                "-Wl,--wrap=gfx_delete_texture",
                "-Wl,--wrap=parts_flat_find_library",
                "-l:libSDL2-2.0.so.0", "-l:libGLESv2.so.2", "-lm",
                "-o", str(exe),
            ]
            subprocess.run(cmd, check=True)
            res = run_gles_binary(exe, tdir)
            self.assertIn("PASS: FLAT pre-resolved lib_idx", res.stdout)

    def test_single_pass_box_blur_gles3_equivalence(self):
        env_paths = find_gles_env()
        if not env_paths:
            self.skipTest("SDL2 / cglm / GLES3 headers not available")
        sdl2_inc, cglm_inc, gles_inc = env_paths

        harness = r'''
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "system4.h"
#include "gfx/gfx.h"
#include "gfx/private.h"
#include "xsystem4.h"

struct config config = { .view_width = 128, .view_height = 128 };

void *_xmalloc(size_t n, const char *f) { (void)f; void *p = malloc(n ? n : 1); assert(p); return p; }
void *_xcalloc(size_t n, size_t z, const char *f) { (void)f; void *p = calloc(n ? n : 1, z ? z : 1); assert(p); return p; }
void *_xrealloc(void *old, size_t n, const char *f) { (void)f; void *p = realloc(old, n ? n : 1); assert(p); return p; }
_Noreturn void sys_error(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); abort();
}
void sys_warning(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}
void sys_message(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stdout, fmt, ap); va_end(ap);
}
char *sjis_filename2utf(const char *src, size_t len) { (void)src; (void)len; return strdup("blur-test"); }
void modifier_on_frame(void) {}
void icon_init(void) {}
void icon_fini(void) {}

static int g_draw_calls = 0;
void __real_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
void __wrap_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    g_draw_calls++;
    __real_glDrawElements(mode, count, type, indices);
}

static void cpu_box_blur(uint8_t *dst, const uint8_t *src,
        int dst_w, int dst_h, int src_w, int src_h, int dx, int dy, int sx, int sy,
        int w, int h, int blur, bool vertical, bool amap) {
    int r = blur > 0 ? blur : 0;
    double denom = (double)(r * 2 + 1);
    for (int y = 0; y < h; y++) {
        int dst_y = dy + y;
        int base_y = sy + y;
        if (dst_y < 0 || dst_y >= dst_h || base_y < 0 || base_y >= src_h)
            continue;
        for (int x = 0; x < w; x++) {
            int dst_x = dx + x;
            int base_x = sx + x;
            if (dst_x < 0 || dst_x >= dst_w || base_x < 0 || base_x >= src_w)
                continue;
            int c_lo = amap ? 3 : 0;
            int c_hi = amap ? 4 : 3;
            for (int c = c_lo; c < c_hi; c++) {
                double sum = 0.0;
                for (int k = -r; k <= r; k++) {
                    int xx = x + (vertical ? 0 : k);
                    int yy = y + (vertical ? k : 0);
                    int sxx = sx + xx;
                    int syy = sy + yy;
                    if (xx >= 0 && xx < w && yy >= 0 && yy < h &&
                            sxx >= 0 && sxx < src_w && syy >= 0 && syy < src_h) {
                        sum += src[(syy * src_w + sxx) * 4 + c];
                    }
                }
                int val = (int)llround(sum / denom);
                if (val < 0) val = 0;
                if (val > 255) val = 255;
                dst[(dst_y * dst_w + dst_x) * 4 + c] = (uint8_t)val;
            }
        }
    }
}

static void run_blur_matrix(void) {
    enum { SW = 37, SH = 29, DW = 41, DH = 33 };
    uint8_t src_px[SW * SH * 4];
    uint8_t base_dst[DW * DH * 4];
    uint8_t expected[DW * DH * 4];

    for (int y = 0; y < SH; y++) {
        for (int x = 0; x < SW; x++) {
            int i = (y * SW + x) * 4;
            src_px[i+0] = (uint8_t)((x * 37 + y * 13 + ((x % 4 == 0) ? 190 : 0)) & 255);
            src_px[i+1] = (uint8_t)((x * 11 + y * 53 + ((y % 3 == 0) ? 170 : 0)) & 255);
            src_px[i+2] = (uint8_t)(((x ^ y) * 29) & 255);
            src_px[i+3] = (uint8_t)((x * 19 + y * 7 + 41) & 255);
        }
    }
    for (int i = 0; i < DW * DH * 4; i++)
        base_dst[i] = (uint8_t)((i * 73 + 17) & 255);

    Texture src = {0}, dst = {0};
    gfx_init_texture_with_pixels(&src, SW, SH, src_px);
    gfx_init_texture_with_pixels(&dst, DW, DH, base_dst);

    const int radii[] = { 0, 1, 2, 5, 12, 25 };
    for (size_t ri = 0; ri < sizeof(radii) / sizeof(radii[0]); ri++) {
        int blur = radii[ri];
        for (int amap = 0; amap < 2; amap++) {
            for (int vertical = 0; vertical < 2; vertical++) {
                for (int sub = 0; sub < 2; sub++) {
                    int dx = sub ? 5 : 0, dy = sub ? 4 : 0;
                    int sx = sub ? 3 : 0, sy = sub ? 2 : 0;
                    int w  = sub ? 18 : SW, h = sub ? 14 : SH;

                    gfx_update_texture_with_pixels(&dst, base_dst);
                    memcpy(expected, base_dst, sizeof(expected));
                    cpu_box_blur(expected, src_px, DW, DH, SW, SH, dx, dy, sx, sy, w, h, blur, vertical != 0, amap != 0);

                    g_draw_calls = 0;
                    if (!amap && !vertical)
                        gfx_copy_width_blur(&dst, dx, dy, &src, sx, sy, w, h, blur);
                    else if (!amap && vertical)
                        gfx_copy_height_blur(&dst, dx, dy, &src, sx, sy, w, h, blur);
                    else if (amap && !vertical)
                        gfx_copy_amap_width_blur(&dst, dx, dy, &src, sx, sy, w, h, blur);
                    else
                        gfx_copy_amap_height_blur(&dst, dx, dy, &src, sx, sy, w, h, blur);

                    // Single-pass guarantee: strictly 1 GL draw call regardless of blur radius!
                    assert(g_draw_calls == 1);

                    uint8_t *got = gfx_get_pixels(&dst);
                    for (int i = 0; i < DW * DH * 4; i++) {
                        int diff = abs((int)got[i] - (int)expected[i]);
                        if (diff > 1) {
                            int px = i / 4;
                            fprintf(stderr,
                                "BLUR MISMATCH blur=%d amap=%d vert=%d sub=%d (%d,%d) ch=%d got=%u exp=%u\n",
                                blur, amap, vertical, sub, px % DW, px / DW, i % 4, got[i], expected[i]);
                            abort();
                        }
                    }
                    free(got);
                }
            }
        }
    }

    // In-place src == dst blur (must avoid GLES 3.0 feedback loop via temp copy)
    Texture inplace = {0};
    gfx_init_texture_with_pixels(&inplace, DW, DH, base_dst);
    const int ip_radii[] = { 0, 1, 5, 12 };
    for (size_t ri = 0; ri < sizeof(ip_radii) / sizeof(ip_radii[0]); ri++) {
        int blur = ip_radii[ri];
        for (int amap = 0; amap < 2; amap++) {
            for (int vertical = 0; vertical < 2; vertical++) {
                for (int sub = 0; sub < 2; sub++) {
                    int dx = sub ? 5 : 0, dy = sub ? 4 : 0;
                    int sx = sub ? 3 : 0, sy = sub ? 2 : 0;
                    int w  = sub ? 18 : DW, h = sub ? 14 : DH;

                    gfx_update_texture_with_pixels(&inplace, base_dst);
                    memcpy(expected, base_dst, sizeof(expected));
                    cpu_box_blur(expected, base_dst, DW, DH, DW, DH, dx, dy, sx, sy, w, h, blur, vertical != 0, amap != 0);

                    if (!amap && !vertical)
                        gfx_copy_width_blur(&inplace, dx, dy, &inplace, sx, sy, w, h, blur);
                    else if (!amap && vertical)
                        gfx_copy_height_blur(&inplace, dx, dy, &inplace, sx, sy, w, h, blur);
                    else if (amap && !vertical)
                        gfx_copy_amap_width_blur(&inplace, dx, dy, &inplace, sx, sy, w, h, blur);
                    else
                        gfx_copy_amap_height_blur(&inplace, dx, dy, &inplace, sx, sy, w, h, blur);

                    uint8_t *got = gfx_get_pixels(&inplace);
                    for (int i = 0; i < DW * DH * 4; i++) {
                        int diff = abs((int)got[i] - (int)expected[i]);
                        if (diff > 1) {
                            int px = i / 4;
                            fprintf(stderr,
                                "INPLACE BLUR MISMATCH blur=%d amap=%d vert=%d sub=%d (%d,%d) ch=%d got=%u exp=%u\n",
                                blur, amap, vertical, sub, px % DW, px / DW, i % 4, got[i], expected[i]);
                            abort();
                        }
                    }
                    free(got);
                }
            }
        }
    }

    // Out-of-bounds source rectangles (negative sx/sy and past-end sx+w/sy+h):
    // out-of-bounds taps MUST contribute 0 (never clamp/repeat the edge texel!),
    // matching both distinct src/dst and in-place src == dst!
    struct { int dx, dy, sx, sy, w, h; } oob_cases[] = {
        { 0, 0, -1,  0, SW + 2, SH     },
        { 0, 0, -3, -2, SW + 6, SH + 4 },
        { 2, 1, SW - 6, SH - 5, 12, 10 },
        { 1, 2, -4, -3, 10, 10         },
        { 0, 0, -20, 0, 10, 10         },
    };
    const int oob_radii[] = { 0, 1, 3, 8 };
    for (size_t ci = 0; ci < sizeof(oob_cases) / sizeof(oob_cases[0]); ci++) {
        int dx = oob_cases[ci].dx, dy = oob_cases[ci].dy;
        int sx = oob_cases[ci].sx, sy = oob_cases[ci].sy;
        int w  = oob_cases[ci].w,  h  = oob_cases[ci].h;
        for (size_t ri = 0; ri < sizeof(oob_radii) / sizeof(oob_radii[0]); ri++) {
            int blur = oob_radii[ri];
            for (int amap = 0; amap < 2; amap++) {
                for (int vertical = 0; vertical < 2; vertical++) {
                    // Distinct src != dst
                    gfx_update_texture_with_pixels(&dst, base_dst);
                    memcpy(expected, base_dst, sizeof(expected));
                    cpu_box_blur(expected, src_px, DW, DH, SW, SH, dx, dy, sx, sy, w, h, blur, vertical != 0, amap != 0);

                    if (!amap && !vertical)
                        gfx_copy_width_blur(&dst, dx, dy, &src, sx, sy, w, h, blur);
                    else if (!amap && vertical)
                        gfx_copy_height_blur(&dst, dx, dy, &src, sx, sy, w, h, blur);
                    else if (amap && !vertical)
                        gfx_copy_amap_width_blur(&dst, dx, dy, &src, sx, sy, w, h, blur);
                    else
                        gfx_copy_amap_height_blur(&dst, dx, dy, &src, sx, sy, w, h, blur);

                    uint8_t *got = gfx_get_pixels(&dst);
                    for (int i = 0; i < DW * DH * 4; i++) {
                        int diff = abs((int)got[i] - (int)expected[i]);
                        if (diff > 1) {
                            int px = i / 4;
                            fprintf(stderr,
                                "OOB BLUR MISMATCH case=%zu blur=%d amap=%d vert=%d (%d,%d) ch=%d got=%u exp=%u\n",
                                ci, blur, amap, vertical, px % DW, px / DW, i % 4, got[i], expected[i]);
                            abort();
                        }
                    }
                    free(got);

                    // In-place src == dst
                    gfx_update_texture_with_pixels(&inplace, base_dst);
                    memcpy(expected, base_dst, sizeof(expected));
                    cpu_box_blur(expected, base_dst, DW, DH, DW, DH, dx, dy, sx, sy, w, h, blur, vertical != 0, amap != 0);

                    if (!amap && !vertical)
                        gfx_copy_width_blur(&inplace, dx, dy, &inplace, sx, sy, w, h, blur);
                    else if (!amap && vertical)
                        gfx_copy_height_blur(&inplace, dx, dy, &inplace, sx, sy, w, h, blur);
                    else if (amap && !vertical)
                        gfx_copy_amap_width_blur(&inplace, dx, dy, &inplace, sx, sy, w, h, blur);
                    else
                        gfx_copy_amap_height_blur(&inplace, dx, dy, &inplace, sx, sy, w, h, blur);

                    got = gfx_get_pixels(&inplace);
                    for (int i = 0; i < DW * DH * 4; i++) {
                        int diff = abs((int)got[i] - (int)expected[i]);
                        if (diff > 1) {
                            int px = i / 4;
                            fprintf(stderr,
                                "OOB INPLACE BLUR MISMATCH case=%zu blur=%d amap=%d vert=%d (%d,%d) ch=%d got=%u exp=%u\n",
                                ci, blur, amap, vertical, px % DW, px / DW, i % 4, got[i], expected[i]);
                            abort();
                        }
                    }
                    free(got);
                }
            }
        }
    }

    // Explicit edge-repeat regression guard: solid white 10x10 blurred with sx=-2, blur=2
    // must produce 3/5 * 255 = 153 at the left edge of src (dst x=2), NOT 255!
    {
        uint8_t white_px[10 * 10 * 4];
        memset(white_px, 255, sizeof(white_px));
        Texture wsrc = {0}, wdst = {0};
        gfx_init_texture_with_pixels(&wsrc, 10, 10, white_px);
        gfx_init_texture_rgba(&wdst, 14, 10, (SDL_Color){ 0, 0, 0, 255 });
        gfx_copy_width_blur(&wdst, 0, 0, &wsrc, -2, 0, 14, 10, 2);
        uint8_t *got = gfx_get_pixels(&wdst);
        // dst x=2 maps to src x=0: taps i=-2,-1 are OOB (0), i=0,1,2 are 255 -> 765/5 = 153!
        int edge_r = got[(5 * 14 + 2) * 4 + 0];
        assert(abs(edge_r - 153) <= 1);
        // dst x=11 maps to src x=9: taps i=1,2 are OOB (0) -> 765/5 = 153!
        int redge_r = got[(5 * 14 + 11) * 4 + 0];
        assert(abs(redge_r - 153) <= 1);
        free(got);
        gfx_delete_texture(&wdst);
        gfx_delete_texture(&wsrc);
    }

    // Large radius that would overflow signed 32-bit int `blur_radius * 2 + 1` in GLSL:
    // must divide by ~2.15e9 cleanly (producing 0), never negative/wrapped colors!
    gfx_update_texture_with_pixels(&dst, base_dst);
    gfx_copy_width_blur(&dst, 0, 0, &src, 0, 0, SW, SH, 0x40000000);
    {
        uint8_t *got = gfx_get_pixels(&dst);
        for (int y = 0; y < SH; y++) {
            for (int x = 0; x < SW; x++) {
                int i = (y * DW + x) * 4;
                assert(got[i + 0] == 0 && got[i + 1] == 0 && got[i + 2] == 0);
                assert(got[i + 3] == base_dst[i + 3]);
            }
        }
        free(got);
    }

    gfx_delete_texture(&inplace);
    gfx_delete_texture(&dst);
    gfx_delete_texture(&src);

    // 5. gfx_blur_rgb_to_fresh (blur_mode=1, GL_BLEND disabled): single pass H/V + paired H+V (blur=10)
    //    must match cpu_box_blur within +/-1 LSB for RGB and preserve source alpha EXACTLY (0 LSB diff),
    //    with strictly 1 draw call per pass!
    {
        Texture fsrc = {0}, ftmp = {0}, fdst = {0};
        uint8_t mid_exp[SW * SH * 4];
        uint8_t pair_exp[SW * SH * 4];
        gfx_init_texture_with_pixels(&fsrc, SW, SH, src_px);
        gfx_init_texture_blank(&ftmp, SW, SH);
        gfx_init_texture_blank(&fdst, SW, SH);

        // Single pass H and V for radii 1, 5, 10
        const int fradii[] = { 1, 5, 10 };
        for (size_t ri = 0; ri < sizeof(fradii) / sizeof(fradii[0]); ri++) {
            int br = fradii[ri];
            for (int vert = 0; vert < 2; vert++) {
                memcpy(mid_exp, src_px, sizeof(mid_exp));
                cpu_box_blur(mid_exp, src_px, SW, SH, SW, SH, 0, 0, 0, 0, SW, SH, br, vert != 0, false);
                g_draw_calls = 0;
                gfx_blur_rgb_to_fresh(&ftmp, &fsrc, br, vert != 0);
                assert(g_draw_calls == 1);
                uint8_t *got = gfx_get_pixels(&ftmp);
                for (int i = 0; i < SW * SH * 4; i++) {
                    int diff = abs((int)got[i] - (int)mid_exp[i]);
                    if ((i % 4) == 3)
                        assert(diff == 0); // exact source alpha preservation!
                    else
                        assert(diff <= 1);
                }
                free(got);
            }
        }

        // Paired H+V (blur=10, Dohna Dohna Config Tab blur pattern): 2 draws total, zero copy passes!
        memcpy(mid_exp, src_px, sizeof(mid_exp));
        cpu_box_blur(mid_exp, src_px, SW, SH, SW, SH, 0, 0, 0, 0, SW, SH, 10, false, false);
        memcpy(pair_exp, mid_exp, sizeof(pair_exp));
        cpu_box_blur(pair_exp, mid_exp, SW, SH, SW, SH, 0, 0, 0, 0, SW, SH, 10, true, false);
        g_draw_calls = 0;
        gfx_blur_rgb_to_fresh(&ftmp, &fsrc, 10, false);
        gfx_blur_rgb_to_fresh(&fdst, &ftmp, 10, true);
        assert(g_draw_calls == 2);
        uint8_t *got = gfx_get_pixels(&fdst);
        for (int i = 0; i < SW * SH * 4; i++) {
            int diff = abs((int)got[i] - (int)pair_exp[i]);
            if ((i % 4) == 3)
                assert(diff == 0);
            else
                assert(diff <= 2); // two sequential rounded passes -> at most +/-2 LSB vs double-precision CPU
        }
        free(got);

        gfx_delete_texture(&fdst);
        gfx_delete_texture(&ftmp);
        gfx_delete_texture(&fsrc);
    }

    // 6. RGB-only (pixel-only) texture blurred in-place via copy+blur retains RGB format so
    //    subsequent gfx_fill_amap(..., 0) writes are discarded (alpha stays 255).
    {
        Texture rgb = {0}, snap = {0};
        gfx_init_texture_rgb(&rgb, SW, SH, (SDL_Color){ 120, 180, 220, 255 });
        gfx_init_texture_blank(&snap, SW, SH);
        gfx_copy_with_alpha_map(&snap, 0, 0, &rgb, 0, 0, SW, SH);
        gfx_copy_width_blur(&rgb, 0, 0, &snap, 0, 0, SW, SH, 4);
        gfx_fill_amap(&rgb, 0, 0, SW, SH, 0);
        uint8_t *got = gfx_get_pixels(&rgb);
        for (int i = 0; i < SW * SH; i++)
            assert(got[i * 4 + 3] == 255);
        free(got);
        gfx_delete_texture(&snap);
        gfx_delete_texture(&rgb);
    }

    assert(glGetError() == GL_NO_ERROR);
}

static void test_frame_presented_state_machine(void) {
    // Verify gfx_mark_frame_presented / gfx_consume_frame_presented / gfx_clear_frame_presented
    // and automatic invalidation on draw/view operations outside gfx_swap().
    assert(gfx_consume_frame_presented() == false);

    // Dirty SACT2/Chipmunk frame: gfx_swap() then gfx_mark_frame_presented() -> UpdateView consumes once!
    gfx_swap();
    gfx_mark_frame_presented();
    assert(gfx_consume_frame_presented() == true);
    assert(gfx_consume_frame_presented() == false);

    // Clean SACT2/Chipmunk frame: gfx_clear_frame_presented() -> UpdateView swaps normally.
    gfx_mark_frame_presented();
    gfx_clear_frame_presented();
    assert(gfx_consume_frame_presented() == false);

    // Drawing to main_surface or clearing or setting view offset clears the flag so direct draws are never skipped!
    gfx_mark_frame_presented();
    gfx_clear();
    assert(gfx_consume_frame_presented() == false);

    gfx_mark_frame_presented();
    gfx_set_view_offset(4, 2);
    assert(gfx_consume_frame_presented() == false);
    gfx_set_view_offset(0, 0);

    gfx_mark_frame_presented();
    gfx_reset_view();
    assert(gfx_consume_frame_presented() == false);

    Texture off = {0};
    gfx_init_texture_blank(&off, 16, 16);
    gfx_mark_frame_presented();
    GLuint fb = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, &off, 0, 0, 16, 16);
    gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fb);
    // Drawing to main_surface (including via a copied handle struct like vmSurface_New) clears it!
    assert(gfx_consume_frame_presented() == true);
    gfx_mark_frame_presented();
    Texture alias_main = *gfx_main_surface();
    fb = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, &alias_main, 0, 0, 128, 128);
    gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fb);
    assert(gfx_consume_frame_presented() == false);
    gfx_delete_texture(&off);
}

int main(void) {
    gfx_init();

    // 1. Run full matrix before GL context reset + frame_presented state machine
    run_blur_matrix();
    test_frame_presented_state_machine();

    // 2. Simulate replacement GLES context reset and verify draw_on_gl_reset rebuilds box_blur_shader!
    gfx_release_context_backup();
    SDL_GLContext old_ctx = sdl.gl.context;
    assert(SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0) == 0);
    SDL_GLContext rep_ctx = SDL_GL_CreateContext(sdl.window);
    assert(rep_ctx && rep_ctx != old_ctx);
    gfx_restore_window();
    run_blur_matrix();
    SDL_GL_DeleteContext(old_ctx);

    // 3. Benchmark single-pass blur vs multi-pass (1 + 2*blur) draws on 512x256 with blur=16
    Texture bsrc = {0}, bdst = {0};
    uint8_t *bpx = calloc(512 * 256, 4);
    for (int i = 0; i < 512 * 256 * 4; i++) bpx[i] = (uint8_t)(i & 255);
    gfx_init_texture_with_pixels(&bsrc, 512, 256, bpx);
    gfx_init_texture_blank(&bdst, 512, 256);
    free(bpx);

    glFinish();
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    g_draw_calls = 0;
    for (int i = 0; i < 10; i++) {
        gfx_copy_width_blur(&bdst, 0, 0, &bsrc, 0, 0, 512, 256, 16);
        gfx_copy_height_blur(&bdst, 0, 0, &bsrc, 0, 0, 512, 256, 16);
    }
    glFinish();
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) * 1e-6;
    assert(g_draw_calls == 20); // 20 single-pass draws vs 660 multi-pass draws!
    printf("PASS: single-pass box blur (4 variants x 6 radii x full/subrect + GL reset, +/-1 LSB, 20 draws vs 660 in %.2fms)\n", ms);
    gfx_delete_texture(&bdst);
    gfx_delete_texture(&bsrc);
    assert(glGetError() == GL_NO_ERROR);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            tdir = Path(tmp)
            src = tdir / "blur_gles.c"
            exe = tdir / "blur_gles"
            src.write_text(harness, encoding="utf-8")
            cmd = [
                CC, "-std=gnu11", "-O2", "-g",
                "-ffunction-sections", "-fdata-sections",
                "-DUSE_GLES", f'-DXSYS4_DATA_DIR="{ROOT}"',
                f"-I{sdl2_inc}", f"-I{cglm_inc}", f"-I{gles_inc}",
                f"-I{ROOT / 'include'}", f"-I{ROOT / 'subprojects/libsys4/include'}",
                str(src),
                str(ROOT / "src/video.c"),
                str(ROOT / "src/draw.c"),
                "-Wl,--gc-sections",
                "-Wl,--wrap=glDrawElements",
                "-l:libSDL2-2.0.so.0", "-l:libGLESv2.so.2", "-lm",
                "-o", str(exe),
            ]
            subprocess.run(cmd, check=True)
            res = run_gles_binary(exe, tdir)
            self.assertIn("PASS: single-pass box blur", res.stdout)


if __name__ == "__main__":
    unittest.main()
