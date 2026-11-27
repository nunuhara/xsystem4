#!/usr/bin/env python3
"""Host C regression tests for dungeon GLES 3.0 / GL reset fixes and iOS session teardown."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LIBSYS4_INC = ROOT / "subprojects/libsys4/include"
LIBSYS4_SRC = ROOT / "subprojects/libsys4/src"


def pkg_cflags(*pkgs: str) -> list[str]:
    return subprocess.check_output(["pkg-config", "--cflags", *pkgs], text=True).split()


def compile_and_run(
    c_src: str,
    extra_sources: list[Path] | None = None,
    extra_flags: list[str] | None = None,
    use_gles: bool = True,
) -> str:
    cc = shutil.which("cc") or shutil.which("gcc")
    assert cc, "C compiler required"
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        src_path = tmp / "harness.c"
        bin_path = tmp / "harness"
        src_path.write_text(c_src, encoding="utf-8")
        cmd = [
            cc,
            "-std=gnu11",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Wno-unused-parameter",
            "-Wno-unused-function",
            "-Wno-sign-compare",
            "-Wno-type-limits",
            "-D_DEFAULT_SOURCE",
            *(["-DUSE_GLES"] if use_gles else []),
            f"-I{ROOT / 'include'}",
            f"-I{ROOT / 'src'}",
            f"-I{ROOT / 'src/hll'}",
            f"-I{LIBSYS4_INC}",
            *pkg_cflags("sdl2", "cglm"),
            str(src_path),
            *[str(p) for p in (extra_sources or [])],
            *(extra_flags or []),
            "-o",
            str(bin_path),
        ]
        res_c = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res_c.returncode != 0:
            raise AssertionError(f"Compile failed ({res_c.returncode}):\n{res_c.stderr}")
        res_r = subprocess.run([str(bin_path)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res_r.returncode != 0:
            raise AssertionError(
                f"Run failed ({res_r.returncode}):\nstdout:\n{res_r.stdout}\nstderr:\n{res_r.stderr}"
            )
        return res_r.stdout


GL_MOCK_HEADER = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx/gl.h"

static GLenum g_active_tex = GL_TEXTURE0;
static GLuint g_bound_tex2d[8];
static GLuint g_bound_cubemap[8];
static GLuint g_bound_array_buf = 0;
static GLuint g_bound_elem_buf = 0;
static GLuint g_bound_vao = 0;
static GLuint g_next_id = 10;
static bool g_blend_enabled = true;
static GLenum g_blend_src_rgb = GL_SRC_ALPHA;
static GLenum g_blend_dst_rgb = GL_ONE_MINUS_SRC_ALPHA;
static GLenum g_blend_src_a = GL_ONE;
static GLenum g_blend_dst_a = GL_ZERO;
static GLenum g_fbo_status = GL_FRAMEBUFFER_COMPLETE;
static GLuint g_attached_depth_rb = 0;
static int g_active_tex_wrong_on_bind = 0;
int g_program_deleted = 0;

struct tex_upload {
    GLenum target;
    GLenum unit;
    GLuint tex;
    GLsizei w;
    GLsizei h;
    uint8_t first_rgba[4];
};
static struct tex_upload g_uploads[64];
static int g_nr_uploads = 0;

static void mock_gl_reset(void) {
    g_active_tex = GL_TEXTURE0;
    memset(g_bound_tex2d, 0, sizeof(g_bound_tex2d));
    memset(g_bound_cubemap, 0, sizeof(g_bound_cubemap));
    g_bound_array_buf = 0;
    g_bound_elem_buf = 0;
    g_bound_vao = 0;
    g_next_id = 10;
    g_blend_enabled = true;
    g_blend_src_rgb = GL_SRC_ALPHA;
    g_blend_dst_rgb = GL_ONE_MINUS_SRC_ALPHA;
    g_blend_src_a = GL_ONE;
    g_blend_dst_a = GL_ZERO;
    g_fbo_status = GL_FRAMEBUFFER_COMPLETE;
    g_attached_depth_rb = 0;
    g_active_tex_wrong_on_bind = 0;
    g_program_deleted = 0;
    g_nr_uploads = 0;
}

GL_APICALL void GL_APIENTRY glActiveTexture(GLenum texture) { g_active_tex = texture; }
GL_APICALL void GL_APIENTRY glGenTextures(GLsizei n, GLuint *textures) {
    for (GLsizei i = 0; i < n; i++) textures[i] = g_next_id++;
}
GL_APICALL void GL_APIENTRY glDeleteTextures(GLsizei n, const GLuint *textures) { (void)n; (void)textures; }
GL_APICALL GLboolean GL_APIENTRY glIsTexture(GLuint texture) { return texture != 0 ? GL_TRUE : GL_FALSE; }
GL_APICALL void GL_APIENTRY glBindTexture(GLenum target, GLuint texture) {
    int unit = (int)(g_active_tex - GL_TEXTURE0);
    if (unit < 0 || unit >= 8) unit = 0;
    if (target == GL_TEXTURE_2D) g_bound_tex2d[unit] = texture;
    if (target == GL_TEXTURE_CUBE_MAP) g_bound_cubemap[unit] = texture;
}
GL_APICALL void GL_APIENTRY glTexImage2D(GLenum target, GLint level, GLint internalformat,
    GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels) {
    (void)level; (void)internalformat; (void)border; (void)format; (void)type;
    if (g_active_tex != GL_TEXTURE0) g_active_tex_wrong_on_bind++;
    if (g_nr_uploads < 64) {
        struct tex_upload *u = &g_uploads[g_nr_uploads++];
        u->target = target;
        u->unit = g_active_tex;
        int unit = (int)(g_active_tex - GL_TEXTURE0);
        if (unit < 0 || unit >= 8) unit = 0;
        u->tex = (target == GL_TEXTURE_2D) ? g_bound_tex2d[unit] : g_bound_cubemap[unit];
        u->w = width;
        u->h = height;
        if (pixels) memcpy(u->first_rgba, pixels, 4);
        else memset(u->first_rgba, 0, 4);
    }
}
GL_APICALL void GL_APIENTRY glTexParameteri(GLenum target, GLenum pname, GLint param) {
    (void)target; (void)pname; (void)param;
}
GL_APICALL void GL_APIENTRY glGenerateMipmap(GLenum target) { (void)target; }
GL_APICALL void GL_APIENTRY glGenVertexArrays(GLsizei n, GLuint *arrays) {
    for (GLsizei i = 0; i < n; i++) arrays[i] = g_next_id++;
}
GL_APICALL void GL_APIENTRY glDeleteVertexArrays(GLsizei n, const GLuint *arrays) { (void)n; (void)arrays; }
GL_APICALL GLboolean GL_APIENTRY glIsVertexArray(GLuint array) { return array != 0 ? GL_TRUE : GL_FALSE; }
GL_APICALL void GL_APIENTRY glBindVertexArray(GLuint array) { g_bound_vao = array; }
GL_APICALL void GL_APIENTRY glGenBuffers(GLsizei n, GLuint *buffers) {
    for (GLsizei i = 0; i < n; i++) buffers[i] = g_next_id++;
}
GL_APICALL void GL_APIENTRY glDeleteBuffers(GLsizei n, const GLuint *buffers) { (void)n; (void)buffers; }
GL_APICALL void GL_APIENTRY glBindBuffer(GLenum target, GLuint buffer) {
    if (target == GL_ARRAY_BUFFER) g_bound_array_buf = buffer;
    if (target == GL_ELEMENT_ARRAY_BUFFER) g_bound_elem_buf = buffer;
}
GL_APICALL void GL_APIENTRY glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    (void)target; (void)size; (void)data; (void)usage;
}
GL_APICALL void GL_APIENTRY glEnableVertexAttribArray(GLuint index) { (void)index; }
GL_APICALL void GL_APIENTRY glVertexAttribPointer(GLuint index, GLint size, GLenum type,
    GLboolean normalized, GLsizei stride, const void *pointer) {
    (void)index; (void)size; (void)type; (void)normalized; (void)stride; (void)pointer;
}
GL_APICALL void GL_APIENTRY glUseProgram(GLuint program) { (void)program; }
GL_APICALL void GL_APIENTRY glDeleteProgram(GLuint program) { if (program) g_program_deleted++; }
GL_APICALL GLboolean GL_APIENTRY glIsProgram(GLuint program) { return program != 0 ? GL_TRUE : GL_FALSE; }
GL_APICALL GLint GL_APIENTRY glGetUniformLocation(GLuint program, const GLchar *name) {
    (void)program; (void)name; return 1;
}
GL_APICALL void GL_APIENTRY glUniform1i(GLint loc, GLint v0) { (void)loc; (void)v0; }
GL_APICALL void GL_APIENTRY glUniform1f(GLint loc, GLfloat v0) { (void)loc; (void)v0; }
GL_APICALL void GL_APIENTRY glUniform2f(GLint loc, GLfloat v0, GLfloat v1) { (void)loc; (void)v0; (void)v1; }
GL_APICALL void GL_APIENTRY glUniform4f(GLint loc, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    (void)loc; (void)v0; (void)v1; (void)v2; (void)v3;
}
GL_APICALL void GL_APIENTRY glUniformMatrix4fv(GLint loc, GLsizei count, GLboolean transpose, const GLfloat *value) {
    (void)loc; (void)count; (void)transpose; (void)value;
}
GL_APICALL void GL_APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    (void)mode; (void)count; (void)type; (void)indices;
}
GL_APICALL void GL_APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    (void)mode; (void)first; (void)count;
}
GL_APICALL void GL_APIENTRY glDepthFunc(GLenum func) { (void)func; }
GL_APICALL void GL_APIENTRY glEnable(GLenum cap) { if (cap == GL_BLEND) g_blend_enabled = true; }
GL_APICALL void GL_APIENTRY glDisable(GLenum cap) { if (cap == GL_BLEND) g_blend_enabled = false; }
GL_APICALL GLboolean GL_APIENTRY glIsEnabled(GLenum cap) {
    if (cap == GL_BLEND) return g_blend_enabled ? GL_TRUE : GL_FALSE;
    return GL_FALSE;
}
GL_APICALL void GL_APIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor) {
    g_blend_src_rgb = g_blend_src_a = sfactor;
    g_blend_dst_rgb = g_blend_dst_a = dfactor;
}
GL_APICALL void GL_APIENTRY glBlendFuncSeparate(GLenum srgb, GLenum drgb, GLenum sa, GLenum da) {
    g_blend_src_rgb = srgb;
    g_blend_dst_rgb = drgb;
    g_blend_src_a = sa;
    g_blend_dst_a = da;
}
GL_APICALL void GL_APIENTRY glPolygonOffset(GLfloat factor, GLfloat units) { (void)factor; (void)units; }
GL_APICALL void GL_APIENTRY glCullFace(GLenum mode) { (void)mode; }
GL_APICALL void GL_APIENTRY glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    (void)r; (void)g; (void)b; (void)a;
}
GL_APICALL void GL_APIENTRY glClear(GLbitfield mask) { (void)mask; }
GL_APICALL void GL_APIENTRY glGenRenderbuffers(GLsizei n, GLuint *rbs) {
    for (GLsizei i = 0; i < n; i++) rbs[i] = g_next_id++;
}
GL_APICALL void GL_APIENTRY glDeleteRenderbuffers(GLsizei n, const GLuint *rbs) { (void)n; (void)rbs; }
GL_APICALL void GL_APIENTRY glBindRenderbuffer(GLenum target, GLuint rb) { (void)target; (void)rb; }
GL_APICALL void GL_APIENTRY glRenderbufferStorage(GLenum target, GLenum fmt, GLsizei w, GLsizei h) {
    (void)target; (void)fmt; (void)w; (void)h;
}
GL_APICALL void GL_APIENTRY glFramebufferRenderbuffer(GLenum target, GLenum att, GLenum rbtarget, GLuint rb) {
    (void)target; (void)rbtarget;
    if (att == GL_DEPTH_ATTACHMENT) g_attached_depth_rb = rb;
}
GL_APICALL GLenum GL_APIENTRY glCheckFramebufferStatus(GLenum target) {
    (void)target;
    return g_fbo_status;
}
"""


class DungeonGlesAndResetRegressionTests(unittest.TestCase):
    def test_dtx_unknown_version_returns_null(self):
        harness = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4/cg.h"
#include "dungeon/dtx.h"

const char *display_utf0(const char *s) { return s; }

static int g_cg_loads = 0;
struct cg *cg_load_buffer(uint8_t *data, size_t size) {
    g_cg_loads++;
    struct cg *cg = calloc(1, sizeof(*cg));
    cg->metrics.w = (int)data[0];
    cg->metrics.h = (int)size;
    return cg;
}

int main(void) {
    assert(dtx_create_cg(NULL, DTX_WALL, 0) == NULL);

    struct dtx *dtx = calloc(1, sizeof(*dtx));
    dtx->nr_rows = 8;
    dtx->nr_columns = 2;
    dtx->entries = calloc(dtx->nr_rows * dtx->nr_columns, sizeof(struct dtx_entry));
    uint8_t tags[8];
    for (int r = 0; r < 8; r++) {
        tags[r] = (uint8_t)(100 + r);
        dtx->entries[r * dtx->nr_columns + 0].data = &tags[r];
        dtx->entries[r * dtx->nr_columns + 0].size = 1;
    }

    // Unknown version must return NULL without calling cg_load_buffer or reading uninitialized row.
    dtx->version = 99;
    assert(dtx_create_cg(dtx, DTX_WALL, 0) == NULL);
    assert(dtx_create_cg(dtx, DTX_LIGHTMAP, 0) == NULL);
    assert(g_cg_loads == 0);

    // Known versions map to the expected rows.
    dtx->version = 0;
    struct cg *cg = dtx_create_cg(dtx, DTX_LIGHTMAP, 0);
    assert(cg && cg->metrics.w == 106);
    free(cg);

    dtx->version = 1;
    cg = dtx_create_cg(dtx, DTX_LIGHTMAP, 0);
    assert(cg && cg->metrics.w == 107);
    free(cg);

    dtx->version = 0xffffffffu;
    cg = dtx_create_cg(dtx, DTX_LIGHTMAP, 0);
    assert(cg && cg->metrics.w == 103);
    free(cg);

    free(dtx->entries);
    free(dtx);
    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            [
                ROOT / "src/dungeon/dtx.c",
                ROOT / "src/dungeon/mtrand43.c",
                LIBSYS4_SRC / "buffer.c",
                LIBSYS4_SRC / "file.c",
                LIBSYS4_SRC / "string.c",
                LIBSYS4_SRC / "system.c",
                LIBSYS4_SRC / "utfsjis.c",
            ],
        )
        self.assertEqual(out.strip(), "OK")

    def test_dungeon_map_gl_reset_preserves_walked_cells(self):
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dungeon/dgn.h"
#include "dungeon/dungeon.h"
#include "dungeon/map.h"
#include "gfx/gfx.h"
#include "sact.h"

static bool g_remap_ok = false;
static int g_next_tex = 1;
static int g_init_tex_calls = 0;

struct reveal_call {
    GLuint tex;
    int x, y, w, h;
    int r, g, b, a;
    bool amap_only;
};
static struct reveal_call g_reveals[64];
static int g_nr_reveals = 0;

bool gfx_remap_texture(struct texture *t) {
    if (!g_remap_ok && t) t->handle = 0;
    return g_remap_ok;
}
void gfx_init_texture_rgba(struct texture *t, int w, int h, SDL_Color color) {
    (void)color;
    g_init_tex_calls++;
    t->handle = g_next_tex++;
    t->w = w;
    t->h = h;
}
void gfx_delete_texture(struct texture *t) { if (t) t->handle = 0; }
void gfx_fill(struct texture *dst, int x, int y, int w, int h, int r, int g, int b) {
    (void)dst; (void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b;
}
void gfx_fill_amap(struct texture *dst, int x, int y, int w, int h, int a) {
    if (g_nr_reveals < 64) {
        g_reveals[g_nr_reveals++] = (struct reveal_call){
            .tex = dst->handle, .x = x, .y = y, .w = w, .h = h, .a = a, .amap_only = true
        };
    }
}
void gfx_fill_with_alpha(Texture *dst, int x, int y, int w, int h, int r, int g, int b, int a) {
    if (g_nr_reveals < 64) {
        g_reveals[g_nr_reveals++] = (struct reveal_call){
            .tex = dst->handle, .x = x, .y = y, .w = w, .h = h,
            .r = r, .g = g, .b = b, .a = a, .amap_only = false
        };
    }
}
void gfx_copy(struct texture *dst, int dx, int dy, struct texture *src, int sx, int sy, int w, int h) {
    (void)dst; (void)dx; (void)dy; (void)src; (void)sx; (void)sy; (void)w; (void)h;
}
void gfx_copy_with_alpha_map(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h) {
    (void)dst; (void)dx; (void)dy; (void)src; (void)sx; (void)sy; (void)w; (void)h;
}
bool scene_is_dirty = false;
void scene_register_sprite(struct sprite *sp) { (void)sp; }
void scene_unregister_sprite(struct sprite *sp) { (void)sp; }
static struct sact_sprite g_dummy_sprite;
static struct texture g_dummy_sym_tex = { .handle = 999, .w = 11, .h = 11 };
struct sact_sprite *sact_get_sprite(int sp_no) { (void)sp_no; return &g_dummy_sprite; }
struct texture *sprite_get_texture(struct sact_sprite *sp) { (void)sp; return &g_dummy_sym_tex; }
struct dungeon_context *dungeon_get_context(int surface) { (void)surface; return NULL; }
_Noreturn void _vm_error(const char *fmt, ...) { (void)fmt; abort(); }

int main(void) {
    // 1. DRAW_DUNGEON_1: walked cells across two floors are re-revealed after GL reset.
    struct dungeon_context ctx = {0};
    ctx.version = DRAW_DUNGEON_1;
    ctx.dgn = dgn_new(3, 2, 3);
    for (uint32_t y = 0; y < 2; y++) {
        for (uint32_t z = 0; z < 3; z++) {
            for (uint32_t x = 0; x < 3; x++) {
                struct dgn_cell *c = dgn_cell_at(ctx.dgn, x, y, z);
                c->x = x; c->y = y; c->z = z;
                c->floor = 0;
                c->north_wall = c->south_wall = c->east_wall = c->west_wall = -1;
                c->north_door = c->south_door = c->east_door = c->west_door = -1;
                c->stairs_texture = -1;
            }
        }
    }
    ctx.map = dungeon_map_create(DRAW_DUNGEON_1);
    dungeon_map_init(&ctx);
    dgn_cell_at(ctx.dgn, 1, 0, 2)->walked = 1;
    dgn_cell_at(ctx.dgn, 2, 1, 0)->walked = 1;

    // Remap succeeds: no reinit and no extra reveal calls.
    g_remap_ok = true;
    g_init_tex_calls = 0;
    g_nr_reveals = 0;
    dungeon_map_on_gl_reset(&ctx);
    assert(g_init_tex_calls == 0 && g_nr_reveals == 0);

    // Remap fails: textures rebuilt and ONLY the two walked cells re-revealed with alpha=255.
    g_remap_ok = false;
    dungeon_map_on_gl_reset(&ctx);
    assert(g_init_tex_calls == 2);
    assert(g_nr_reveals == 2);
    assert(g_reveals[0].amap_only && g_reveals[0].a == 255);
    assert(g_reveals[1].amap_only && g_reveals[1].a == 255);
    assert(g_reveals[0].tex != g_reveals[1].tex);

    dungeon_map_free(ctx.map);
    dgn_free(ctx.dgn);

    // 2. DRAW_DUNGEON_2: walked (bit 0 = 1 -> black) and seen (bit 1 = 2 -> yellow) both preserved.
    memset(&ctx, 0, sizeof(ctx));
    ctx.version = DRAW_DUNGEON_2;
    ctx.dgn = dgn_new(3, 1, 3);
    ctx.dgn->start_x = 0;
    ctx.dgn->start_y = 0;
    for (uint32_t z = 0; z < 3; z++) {
        for (uint32_t x = 0; x < 3; x++) {
            struct dgn_cell *c = dgn_cell_at(ctx.dgn, x, 0, z);
            c->x = x; c->y = 0; c->z = z;
            c->floor = 0;
        }
    }
    ctx.map = dungeon_map_create(DRAW_DUNGEON_2);
    dungeon_map_init(&ctx);
    dgn_cell_at(ctx.dgn, 1, 0, 1)->walked = 3; // walked -> rg = 0
    dgn_cell_at(ctx.dgn, 2, 0, 1)->walked = 2; // neighbor seen -> rg = 255
    g_remap_ok = false;
    g_nr_reveals = 0;
    dungeon_map_on_gl_reset(&ctx);
    assert(g_nr_reveals == 2);
    assert(!g_reveals[0].amap_only && g_reveals[0].r == 0 && g_reveals[0].g == 0 && g_reveals[0].a == 255);
    assert(!g_reveals[1].amap_only && g_reveals[1].r == 255 && g_reveals[1].g == 255 && g_reveals[1].a == 255);

    dungeon_map_free(ctx.map);
    dgn_free(ctx.dgn);
    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            [
                ROOT / "src/dungeon/map.c",
                ROOT / "src/dungeon/dgn.c",
                LIBSYS4_SRC / "buffer.c",
                LIBSYS4_SRC / "file.c",
                LIBSYS4_SRC / "string.c",
                LIBSYS4_SRC / "system.c",
                LIBSYS4_SRC / "utfsjis.c",
            ],
            extra_flags=["-lm"],
        )
        self.assertEqual(out.strip(), "OK")

    def test_skybox_and_renderer_gles_fixes(self):
        harness = GL_MOCK_HEADER + r"""
#include "system4/cg.h"
#include "dungeon/dgn.h"
#include "dungeon/dtx.h"
#include "dungeon/dungeon.h"
#include "dungeon/polyobj.h"
#include "dungeon/renderer.h"
#include "dungeon/skybox.h"
#include "gfx/gfx.h"
#include "sact.h"

static bool g_partial_skybox = false;
static bool g_blend_during_gfx_render = true;
static int g_gfx_render_calls = 0;
static int g_copy_reverse_ud_calls = 0;

void gfx_load_shader(struct shader *dst, const char *v, const char *f) {
    (void)v; (void)f;
    dst->program = g_next_id++;
    dst->world_transform = 1;
    dst->view_transform = 2;
    dst->texture = 3;
    dst->vertex_pos = 0;
    dst->vertex_uv = 1;
    dst->prepare = NULL;
}
GLuint gfx_set_framebuffer(GLenum target, Texture *t, int x, int y, int w, int h) {
    (void)target; (void)t; (void)x; (void)y; (void)w; (void)h;
    return 99;
}
void gfx_reset_framebuffer(GLenum target, GLuint fbo) { (void)target; (void)fbo; }
void gfx_render(struct gfx_render_job *job) {
    (void)job;
    g_gfx_render_calls++;
    g_blend_during_gfx_render = g_blend_enabled;
}
void gfx_copy_reverse_UD(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h) {
    (void)dst; (void)dx; (void)dy; (void)src; (void)sx; (void)sy; (void)w; (void)h;
    g_copy_reverse_ud_calls++;
}
struct cg *dtx_create_cg(struct dtx *dtx, enum dtx_texture_type type, int index) {
    (void)dtx;
    if (type == DTX_SKYBOX) {
        if (!g_partial_skybox) return NULL;
        if (index != 0 && index != 3) return NULL;
        struct cg *cg = calloc(1, sizeof(*cg));
        cg->metrics.w = 8;
        cg->metrics.h = 8;
        cg->pixels = calloc(8 * 8, 4);
        ((uint8_t *)cg->pixels)[0] = 0x55;
        ((uint8_t *)cg->pixels)[3] = 0xff;
        return cg;
    }
    if (type == DTX_WALL && index == 0) {
        struct cg *cg = calloc(1, sizeof(*cg));
        cg->metrics.w = 4;
        cg->metrics.h = 4;
        cg->pixels = calloc(4 * 4, 4);
        return cg;
    }
    return NULL;
}
void cg_free(struct cg *cg) {
    if (!cg) return;
    free(cg->pixels);
    free(cg);
}
struct cg *polyobj_get_texture(struct polyobj *po, int i) { (void)po; (void)i; return NULL; }
struct polyobj_object *polyobj_get_object(struct polyobj *po, int i) { (void)po; (void)i; return NULL; }
void polyobj_object_free(struct polyobj_object *obj) { (void)obj; }
struct sact_sprite *sact_try_get_sprite(int sp_no) { (void)sp_no; return NULL; }
struct texture *sprite_get_texture(struct sact_sprite *sp) { return sp ? &sp->texture : NULL; }
Uint32 SDL_GetTicks(void) { return 100; }

int main(void) {
    // 1. Skybox: all 6 faces missing -> 1x1 black opaque fallback on GL_TEXTURE0, GL_ARRAY_BUFFER unbound.
    mock_gl_reset();
    g_active_tex = GL_TEXTURE1;
    g_partial_skybox = false;
    struct skybox *sb = skybox_create(DRAW_DUNGEON_1, NULL);
    assert(sb != NULL);
    assert(g_bound_vao == 0);
    assert(g_bound_array_buf == 0);
    assert(g_active_tex_wrong_on_bind == 0);
    assert(g_nr_uploads == 6);
    for (int i = 0; i < 6; i++) {
        assert(g_uploads[i].w == 1 && g_uploads[i].h == 1);
        assert(g_uploads[i].first_rgba[0] == 0 && g_uploads[i].first_rgba[3] == 255);
    }
    skybox_free(sb);

    // 2. Skybox: partial faces (8x8 on faces 0 & 3) -> missing faces also uploaded as 8x8 for cube completeness.
    mock_gl_reset();
    g_active_tex = GL_TEXTURE2;
    g_partial_skybox = true;
    struct dtx dummy_dtx = { .nr_columns = 1 };
    sb = skybox_create(DRAW_DUNGEON_1, &dummy_dtx);
    assert(sb != NULL);
    assert(g_active_tex_wrong_on_bind == 0);
    assert(g_nr_uploads == 6);
    for (int i = 0; i < 6; i++) {
        assert(g_uploads[i].w == 8 && g_uploads[i].h == 8);
        if (i == 0 || i == 3)
            assert(g_uploads[i].first_rgba[0] == 0x55);
        else
            assert(g_uploads[i].first_rgba[0] == 0 && g_uploads[i].first_rgba[3] == 255);
    }
    skybox_free(sb);

    // 3. Renderer: init_material switches to GL_TEXTURE0; render restores 2D blend func and GL_BLEND.
    mock_gl_reset();
    g_active_tex = GL_TEXTURE1;
    g_partial_skybox = false;
    struct dungeon_renderer *r = dungeon_renderer_create(DRAW_DUNGEON_1, 0, &dummy_dtx, NULL, 0, NULL);
    assert(r != NULL);
    assert(g_active_tex_wrong_on_bind == 0);

    mat4 id = GLM_MAT4_IDENTITY_INIT;
    g_blend_enabled = true;
    dungeon_renderer_render(r, NULL, 0, NULL, id, id);
    assert(g_blend_enabled == true);
    assert(g_blend_src_rgb == GL_SRC_ALPHA && g_blend_dst_rgb == GL_ONE_MINUS_SRC_ALPHA);
    assert(g_blend_src_a == GL_ONE && g_blend_dst_a == GL_ZERO);
    assert(g_active_tex == GL_TEXTURE0);

    // If caller had GL_BLEND disabled, render preserves disabled state while restoring blend func.
    g_blend_enabled = false;
    dungeon_renderer_render(r, NULL, 0, NULL, id, id);
    assert(g_blend_enabled == false);
    assert(g_blend_src_a == GL_ONE && g_blend_dst_a == GL_ZERO);

    // 4. Post-processing raster scroll disables GL_BLEND during pass and restores it afterward.
    Texture src = { .handle = 100, .w = 64, .h = 64 };
    Texture dst = { .handle = 101, .w = 64, .h = 64 };
    dungeon_renderer_set_raster_scroll(r, 1);
    g_blend_enabled = true;
    g_gfx_render_calls = 0;
    dungeon_renderer_run_post_processing(r, &src, &dst);
    assert(g_gfx_render_calls == 1);
    assert(g_blend_during_gfx_render == false);
    assert(g_blend_enabled == true);

    // 0-size or null-handle texture safely skipped.
    Texture zero = { .handle = 0, .w = 0, .h = 0 };
    dungeon_renderer_run_post_processing(r, &zero, &dst);
    assert(g_gfx_render_calls == 1);

    dungeon_renderer_free(r);
    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            [
                ROOT / "src/dungeon/skybox.c",
                ROOT / "src/dungeon/renderer.c",
                LIBSYS4_SRC / "buffer.c",
                LIBSYS4_SRC / "file.c",
                LIBSYS4_SRC / "string.c",
                LIBSYS4_SRC / "system.c",
                LIBSYS4_SRC / "utfsjis.c",
            ],
            extra_flags=["-lm"],
        )
        self.assertEqual(out.strip(), "OK")

    def test_dungeon_event_textures_and_fbo_guards(self):
        harness = GL_MOCK_HEADER + r"""
#include "system4.h"
#include "system4/alk.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/dlf.h"
#include "system4/file.h"
#include "dungeon/dgn.h"
#include "dungeon/dtx.h"
#include "dungeon/dungeon.h"
#include "dungeon/generator.h"
#include "dungeon/map.h"
#include "dungeon/polyobj.h"
#include "dungeon/renderer.h"
#include "dungeon/tes.h"
#include "gfx/gfx.h"
#include "cJSON.h"
#include "json.h"
#include "sact.h"
#include "sprite.h"
#include "vm.h"
#include "vm/page.h"
#include "xsystem4.h"

static int g_init_rgba_calls = 0;
static int g_init_blank_calls = 0;
static int g_set_fbo_calls = 0;
static int g_reset_fbo_calls = 0;
static int g_render_calls = 0;
static int g_post_calls = 0;

const char *display_utf0(const char *s) { return s; }
char *gamedir_path(const char *p) { return strdup(p); }
char *gamedir_path_icase(const char *p) { return strdup(p); }
bool scene_is_dirty = false;
void scene_register_sprite(struct sprite *sp) { (void)sp; }
void scene_unregister_sprite(struct sprite *sp) { (void)sp; }
struct sact_sprite *sact_try_get_sprite(int sp) { (void)sp; return NULL; }
struct texture *sprite_get_texture(struct sact_sprite *sp) { return &sp->texture; }
void gfx_init_texture_rgba(struct texture *t, int w, int h, SDL_Color color) {
    (void)color;
    g_init_rgba_calls++;
    t->handle = g_next_id++;
    t->w = w;
    t->h = h;
}
void gfx_init_texture_blank(struct texture *t, int w, int h) {
    g_init_blank_calls++;
    t->handle = g_next_id++;
    t->w = w;
    t->h = h;
}
bool gfx_remap_texture(struct texture *t) { (void)t; return false; }
void gfx_delete_texture(struct texture *t) { if (t) t->handle = 0; }
void gfx_register_reset_callback(gfx_reset_callback cb, void *u) { (void)cb; (void)u; }
void gfx_unregister_reset_callback(gfx_reset_callback cb, void *u) { (void)cb; (void)u; }
GLuint gfx_set_framebuffer(GLenum target, Texture *t, int x, int y, int w, int h) {
    (void)target; (void)t; (void)x; (void)y; (void)w; (void)h;
    g_set_fbo_calls++;
    return 77;
}
void gfx_reset_framebuffer(GLenum target, GLuint fbo) {
    (void)target; (void)fbo;
    g_reset_fbo_calls++;
}

// Minimal archive/cg mocks for load_event_textures
static struct archive_data *mock_archive_get(struct archive *ar, int no) {
    if (no == 1) return NULL; // missing archive entry
    struct archive_data *d = calloc(1, sizeof(*d));
    d->archive = ar;
    d->no = no;
    return d;
}
static void mock_archive_free_data(struct archive_data *d) { free(d); }
static void mock_archive_free(struct archive *ar) { free(ar); }
static struct archive_ops mock_ar_ops = {
    .get = mock_archive_get,
    .free_data = mock_archive_free_data,
    .free = mock_archive_free,
};
struct alk_archive *alk_open(const char *path, int flags, int *error) {
    (void)path; (void)flags;
    if (error) *error = ARCHIVE_SUCCESS;
    struct alk_archive *alk = calloc(1, sizeof(*alk));
    alk->ar.ops = &mock_ar_ops;
    alk->nr_files = 3;
    return alk;
}
struct cg *cg_load_data(struct archive_data *dfile) {
    if (dfile->no == 2) return NULL; // corrupt CG
    struct cg *cg = calloc(1, sizeof(*cg));
    cg->metrics.w = 4;
    cg->metrics.h = 4;
    cg->pixels = calloc(4 * 4, 4);
    ((uint8_t *)cg->pixels)[0] = 0xaa;
    ((uint8_t *)cg->pixels)[3] = 0xff;
    return cg;
}
void cg_free(struct cg *cg) { if (cg) { free(cg->pixels); free(cg); } }
struct dlf_archive *dlf_open(const char *p, int f, int *e) { (void)p; (void)f; (void)e; return NULL; }
struct dgn *dgn_parse(uint8_t *d, size_t s, bool b) { (void)d; (void)s; (void)b; return NULL; }
void dgn_free(struct dgn *d) { (void)d; }
struct dgn_cell *dgn_cell_at(struct dgn *d, uint32_t x, uint32_t y, uint32_t z) { (void)d; (void)x; (void)y; (void)z; return NULL; }
int dgn_calc_conquer(struct dgn *d) { (void)d; return 0; }
struct dgn_cell **dgn_get_visible_cells(struct dgn *d, int x, int y, int z, int *n) { (void)d; (void)x; (void)y; (void)z; *n = 0; return NULL; }
struct dgn *dgn_generate_drawdungeon2(int n) { (void)n; return NULL; }
struct dtx *dtx_parse(uint8_t *d, size_t s) { (void)d; (void)s; return NULL; }
struct dtx *dtx_load_from_dtl(const char *p, uint32_t s) { (void)p; (void)s; return NULL; }
void dtx_free(struct dtx *d) { (void)d; }
struct tes *tes_parse(uint8_t *d, size_t s) { (void)d; (void)s; return NULL; }
void tes_free(struct tes *t) { (void)t; }
struct polyobj *polyobj_load(const char *p) { (void)p; return NULL; }
void polyobj_free(struct polyobj *p) { (void)p; }
struct dungeon_map *dungeon_map_create(enum draw_dungeon_version v) { (void)v; return NULL; }
void dungeon_map_free(struct dungeon_map *m) { (void)m; }
void dungeon_map_init(struct dungeon_context *c) { (void)c; }
void dungeon_map_on_gl_reset(struct dungeon_context *c) { (void)c; }
void dungeon_map_reveal(struct dungeon_context *c, int x, int y, int z, bool f) { (void)c; (void)x; (void)y; (void)z; (void)f; }
void dungeon_map_hide(struct dungeon_context *c, int x, int y, int z) { (void)c; (void)x; (void)y; (void)z; }
struct dungeon_renderer *dungeon_renderer_create(enum draw_dungeon_version v, int n, struct dtx *dtx, GLuint *e, int ne, struct polyobj *po) {
    (void)v; (void)n; (void)dtx; (void)e; (void)ne; (void)po; return NULL;
}
void dungeon_renderer_free(struct dungeon_renderer *r) { (void)r; }
void dungeon_renderer_reset_gl(struct dungeon_renderer *r, struct dtx *d, GLuint *e, int ne, struct polyobj *po) {
    (void)r; (void)d; (void)e; (void)ne; (void)po;
}
bool dungeon_renderer_has_event_textures(const struct dungeon_renderer *r) { (void)r; return false; }
void dungeon_renderer_render(struct dungeon_renderer *r, struct dgn_cell **c, int nc, struct drawfield_character *ch, mat4 v, mat4 p) {
    (void)r; (void)c; (void)nc; (void)ch; (void)v; (void)p;
    g_render_calls++;
}
void dungeon_renderer_run_post_processing(struct dungeon_renderer *r, struct texture *s, struct texture *d) {
    (void)r; (void)s; (void)d;
    g_post_calls++;
}
struct page *realloc_array(struct page *s, int r, union vm_value *d, enum ain_data_type t, int st, bool i) {
    (void)s; (void)r; (void)d; (void)t; (void)st; (void)i; return NULL;
}
_Noreturn void _vm_error(const char *fmt, ...) { (void)fmt; abort(); }
cJSON *sact_sprite_to_json(struct sact_sprite *sp, bool v) { (void)sp; (void)v; return NULL; }
cJSON *cJSON_CreateObject(void) { return NULL; }
cJSON *cJSON_CreateArray(void) { return NULL; }
cJSON *cJSON_CreateNumber(double n) { (void)n; return NULL; }
void cJSON_AddItemToObject(cJSON *o, const char *s, cJSON *i) { (void)o; (void)s; (void)i; }
void cJSON_AddItemToObjectCS(cJSON *o, const char *s, cJSON *i) { (void)o; (void)s; (void)i; }
void cJSON_AddItemToArray(cJSON *a, cJSON *i) { (void)a; (void)i; }
cJSON *cJSON_AddNumberToObject(cJSON *o, const char *n, double v) { (void)o; (void)n; (void)v; return NULL; }
cJSON *cJSON_AddBoolToObject(cJSON *o, const char *n, cJSON_bool v) { (void)o; (void)n; (void)v; return NULL; }
cJSON *cJSON_AddStringToObject(cJSON *o, const char *n, const char *v) { (void)o; (void)n; (void)v; return NULL; }
cJSON *num3_to_json(double a, double b, double c) { (void)a; (void)b; (void)c; return NULL; }
cJSON *num3_to_json_object(const char *ka, double a, const char *kb, double b, const char *kc, double c) {
    (void)ka; (void)a; (void)kb; (void)b; (void)kc; (void)c; return NULL;
}

#include "dungeon/dungeon.c"

int main(void) {
    // 1. dungeon_context_create clears render texture via gfx_init_texture_rgba (not blank).
    mock_gl_reset();
    struct dungeon_context *ctx = dungeon_context_create(DRAW_DUNGEON_1, 64, 48);
    assert(ctx && g_init_rgba_calls == 1 && g_init_blank_calls == 0);
    assert(ctx->texture.handle != 0 && ctx->depth_buffer != 0);

    // GL reset rebuilds cleared texture via gfx_init_texture_rgba.
    dungeon_context_on_gl_reset(ctx);
    assert(g_init_rgba_calls == 2 && g_init_blank_calls == 0);

    // 2. Incomplete FBO must warn and skip (restoring FBO and detaching depth rb), not abort.
    struct dgn dgn = { .size_x = 1, .size_y = 1, .size_z = 1 };
    int dummy_renderer = 1;
    ctx->loaded = true;
    ctx->draw_enabled = true;
    ctx->dgn = &dgn;
    ctx->renderer = (struct dungeon_renderer *)&dummy_renderer;
    struct sact_sprite sp = {0};
    sp.plugin = &ctx->plugin;
    g_fbo_status = GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    g_set_fbo_calls = g_reset_fbo_calls = g_render_calls = g_post_calls = 0;
    dungeon_render(&sp);
    assert(g_set_fbo_calls == 1 && g_reset_fbo_calls == 1);
    assert(g_attached_depth_rb == 0);
    assert(g_render_calls == 0 && g_post_calls == 0);

    // Complete FBO renders normally.
    g_fbo_status = GL_FRAMEBUFFER_COMPLETE;
    dungeon_render(&sp);
    assert(g_render_calls == 1 && g_post_calls == 1);

    ctx->dgn = NULL;
    ctx->renderer = NULL;
    dungeon_context_free(&ctx->plugin);

    // 3. 0-size context does not allocate texture/renderbuffer and dungeon_render skips safely.
    g_init_rgba_calls = g_set_fbo_calls = 0;
    ctx = dungeon_context_create(DRAW_DUNGEON_1, 0, 0);
    assert(ctx && g_init_rgba_calls == 0 && ctx->texture.handle == 0 && ctx->depth_buffer == 0);
    ctx->loaded = true;
    ctx->draw_enabled = true;
    ctx->dgn = &dgn;
    ctx->renderer = (struct dungeon_renderer *)&dummy_renderer;
    sp.plugin = &ctx->plugin;
    dungeon_render(&sp);
    assert(g_set_fbo_calls == 0);
    ctx->dgn = NULL;
    ctx->renderer = NULL;
    dungeon_context_free(&ctx->plugin);

    // 4. load_event_textures switches to GL_TEXTURE0 and uploads 1x1 transparent fallback on missing/corrupt CGs.
    mock_gl_reset();
    g_active_tex = GL_TEXTURE2;
    int nr_tex = 0;
    GLuint *ev_tex = load_event_textures(&nr_tex);
    assert(ev_tex && nr_tex == 3);
    assert(g_active_tex_wrong_on_bind == 0);
    assert(g_nr_uploads == 3);
    assert(g_uploads[0].w == 4 && g_uploads[0].h == 4 && g_uploads[0].first_rgba[0] == 0xaa);
    assert(g_uploads[1].w == 1 && g_uploads[1].h == 1 && g_uploads[1].first_rgba[3] == 0);
    assert(g_uploads[2].w == 1 && g_uploads[2].h == 1 && g_uploads[2].first_rgba[3] == 0);
    free(ev_tex);

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            [
                LIBSYS4_SRC / "file.c",
                LIBSYS4_SRC / "string.c",
                LIBSYS4_SRC / "system.c",
                LIBSYS4_SRC / "utfsjis.c",
            ],
            extra_flags=["-lm"],
        )
        self.assertEqual(out.strip(), "OK")


class HllAndMovieTeardownRegressionTests(unittest.TestCase):
    def test_input_string_null_guard_and_module_fini(self):
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4/string.h"

struct ain *ain = NULL;
static const struct input_text_handlers *g_legacy_handlers = NULL;
static int g_set_handlers_calls = 0;

void input_set_legacy_text_handlers(const struct input_text_handlers *h) {
    g_legacy_handlers = h;
    g_set_handlers_calls++;
}
bool PE_IsTextBoxComposing(void) { return false; }
int ain_get_library(struct ain *a, const char *n) { (void)a; (void)n; return -1; }
int ain_get_library_function(struct ain *a, int l, const char *n) { (void)a; (void)l; (void)n; return -1; }

#include "hll/InputString.c"

static void (*find_export(struct static_library *lib, const char *name))(void) {
    for (int i = 0; lib->functions[i].name; i++) {
        if (!strcmp(lib->functions[i].name, name))
            return (void (*)(void))lib->functions[i].fun;
    }
    return NULL;
}

int main(void) {
    void (*fini)(void) = find_export(&lib_InputString, "_ModuleFini");
    assert(fini != NULL);

    // 1. Fini without init is safe.
    fini();

    // 2. GetResultString when result == NULL must return empty string instead of crashing.
    assert(result == NULL);
    struct string *s = InputString_GetResultString();
    assert(s != NULL && s->size == 0 && strcmp(s->text, "") == 0);
    free_string(s);

    // 3. OpenIME + handle_input with result == NULL works safely.
    InputString_OpenIME();
    assert(InputString_IsOpenIME());
    assert(g_legacy_handlers != NULL);
    handle_input("hello");
    s = InputString_GetResultString();
    assert(s && strcmp(s->text, "hello") == 0);
    free_string(s);

    // 4. ModuleFini closes IME, unregisters legacy handlers, frees result, resets state.
    InputString_SetFont(24, NULL, 700);
    InputString_SetPos(10, 20);
    fini();
    assert(!InputString_IsOpenIME());
    assert(g_legacy_handlers == NULL);
    assert(result == NULL);
    assert(font_size == 0 && font_weight == 0 && pos.x == 0 && pos.y == 0);

    // 5. Double-fini is safe and GetResultString after fini still returns empty string.
    fini();
    s = InputString_GetResultString();
    assert(s != NULL && s->size == 0);
    free_string(s);

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            [
                LIBSYS4_SRC / "file.c",
                LIBSYS4_SRC / "string.c",
                LIBSYS4_SRC / "system.c",
                LIBSYS4_SRC / "utfsjis.c",
            ],
        )
        self.assertEqual(out.strip(), "OK")

    def test_msgskip_anteater_fonts_data_bitarray_draweffect_pastelchime2_charsprite_fini(self):
        # Compile each module cleanly in its own small TU and invoke their exported _ModuleFini!
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void run_msgskip_test(void);
extern void run_anteater_test(void);
extern void run_newfont_test(void);
extern void run_sengokurancefont_test(void);
extern void run_data_test(void);
extern void run_bitarray_test(void);
extern void run_draweffect_test(void);
extern void run_pastelchime2_test(void);
extern void run_charsprite_test(void);

int main(void) {
    run_msgskip_test();
    run_anteater_test();
    run_newfont_test();
    run_sengokurancefont_test();
    run_data_test();
    run_bitarray_test();
    run_draweffect_test();
    run_pastelchime2_test();
    run_charsprite_test();
    puts("OK");
    return 0;
}
"""
        tu_msgskip = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hll/MsgSkip.c"
void run_msgskip_test(void) {
    MsgSkip_ModuleFini(); // fini without init
    MsgSkip_SetEnable(1);
    MsgSkip_SetState(2);
    assert(MsgSkip_GetEnable() == 1 && MsgSkip_GetState() == 2);
    MsgSkip_ModuleFini();
    assert(MsgSkip_GetEnable() == 0 && MsgSkip_GetState() == 0);
    MsgSkip_ModuleFini(); // double fini
}
"""
        tu_anteater = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int g_mock_scene_struct_no;
#include "hll/AnteaterADVEngine.c"
void run_anteater_test(void) {
    AnteaterADVEngine_ModuleFini(); // fini without init
    g_mock_scene_struct_no = 7;
    assert(scene_struct_no() == 7);
    ADVLogList_SetEnable(false);
    AnteaterADVEngine_ModuleFini();
    assert(cached_scene_struct_no == -1);
    assert(ADVLogList_IsEnable() == true);
    // Relaunch with a different game where the struct has a different index.
    g_mock_scene_struct_no = 19;
    assert(scene_struct_no() == 19);
    AnteaterADVEngine_ModuleFini();
    AnteaterADVEngine_ModuleFini();
}
"""
        tu_newfont = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hll/NewFont.c"
void run_newfont_test(void) {
    bool found = false;
    for (int i = 0; lib_NewFont.functions[i].name; i++) {
        if (!strcmp(lib_NewFont.functions[i].name, "_ModuleFini")) {
            found = (lib_NewFont.functions[i].fun == NewFont_ModuleFini);
            break;
        }
    }
    assert(found);
    NewFont_ModuleFini(); // fini without init
    NewFont_SetTextPos(33, 44);
    NewFont_y_pad = 5;
    NewFont_metrics.font_size = (struct font_size *)0x1234;
    NewFont_shadow_metrics.font_size = (struct font_size *)0x5678;
    NewFont_ModuleFini();
    assert(NewFont_GetTextX() == 0 && NewFont_GetTextY() == 0);
    assert(NewFont_y_pad == 0);
    assert(NewFont_metrics.font_size == NULL && NewFont_shadow_metrics.font_size == NULL);
    NewFont_ModuleFini(); // double fini
}
"""
        tu_srfont = r"""
#include "gfx/gl.h"
#define ALICEHUB_IOS 1
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hll/SengokuRanceFont.c"
void run_sengokurancefont_test(void) {
    bool found = false;
    for (int i = 0; lib_SengokuRanceFont.functions[i].name; i++) {
        if (!strcmp(lib_SengokuRanceFont.functions[i].name, "_ModuleFini")) {
            found = (lib_SengokuRanceFont.functions[i].fun == SengokuRanceFont_ModuleFini);
            break;
        }
    }
    assert(found);
    SengokuRanceFont_ModuleFini();
    SengokuRanceFont_SP_SetFontSize(1, 28.0f);
    assert(sr_tp_table != NULL);
    SengokuRanceFont_ModuleFini();
    assert(sr_tp_table == NULL);
    sengoku_rance_font_end_session(); // idempotent with ModuleFini
    assert(sr_tp_table == NULL);
}
"""
        tu_data = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hll/Data.c"
void run_data_test(void) {
    bool found = false;
    for (int i = 0; lib_Data.functions[i].name; i++) {
        if (!strcmp(lib_Data.functions[i].name, "_ModuleFini")) {
            found = (lib_Data.functions[i].fun == Data_ModuleFini);
            break;
        }
    }
    assert(found);
    Data_ModuleFini();
    uint8_t *buf = malloc(16);
    buffer_init(&reader, buf, 16);
    reader.index = 4;
    Data_ModuleFini();
    assert(reader.buf == NULL && reader.size == 0 && reader.index == 0);
    Data_ModuleFini();
}
"""
        tu_bitarray = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hll/Bitarray.c"
void run_bitarray_test(void) {
    bool found = false;
    for (int i = 0; lib_Bitarray.functions[i].name; i++) {
        if (!strcmp(lib_Bitarray.functions[i].name, "_ModuleFini")) {
            found = (lib_Bitarray.functions[i].fun == Bitarray_ModuleFini);
            break;
        }
    }
    assert(found);
    Bitarray_ModuleFini();
    int h = Bitarray_Create(64);
    assert(h == 0);
    Bitarray_SetBit(h, 5, 1);
    assert(Bitarray_GetBit(h, 5) == 1);
    Bitarray_ModuleFini();
    assert(arrays == NULL && nr_arrays == 0);
    Bitarray_ModuleFini();
    int h2 = Bitarray_Create(32);
    assert(h2 == 0 && Bitarray_GetBit(h2, 5) == 0);
    Bitarray_ModuleFini();
}
"""
        tu_draweffect = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hll/DrawEffect.c"
void run_draweffect_test(void) {
    bool found = false;
    for (int i = 0; lib_DrawEffect.functions[i].name; i++) {
        if (!strcmp(lib_DrawEffect.functions[i].name, "_ModuleFini")) {
            found = (lib_DrawEffect.functions[i].fun == DrawEffect_ModuleFini);
            break;
        }
    }
    assert(found);
    DrawEffect_ModuleFini();
    config.view_width = 800;
    config.view_height = 600;
    assert(DrawEffect_Begin(1, 0, 0, 2, 0, 0, 3, 0, 0, 800, 600, 1));
    assert(draw_effect.on == true);
    DrawEffect_ModuleFini();
    assert(draw_effect.on == false && draw_effect.type == 0 && draw_effect.dst == 0);
    DrawEffect_ModuleFini();
}
"""
        tu_pastel = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hll/PastelChime2.c"
void run_pastelchime2_test(void) {
    bool found = false;
    for (int i = 0; lib_PastelChime2.functions[i].name; i++) {
        if (!strcmp(lib_PastelChime2.functions[i].name, "_ModuleFini")) {
            found = (lib_PastelChime2.functions[i].fun == PastelChime2_ModuleFini);
            break;
        }
    }
    assert(found);
    PastelChime2_ModuleFini();
    PastelChime2_Field_SetSprite(42);
    struct player_move_point *pt = calloc(1, sizeof(*pt));
    TAILQ_INSERT_TAIL(&player_move_queue, pt, entry);
    assert(draw_field_sprite_number == 42 && !TAILQ_EMPTY(&player_move_queue));
    PastelChime2_ModuleFini();
    assert(draw_field_sprite_number == 0 && TAILQ_EMPTY(&player_move_queue));
    PastelChime2_ModuleFini();
}
"""
        tu_charsprite = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hll/CharSpriteManager.c"
void run_charsprite_test(void) {
    CharSpriteManager_Clear();
    int h = CharSpriteManager_CreateHandle();
    assert(h >= 0 && chars.size > 0);
    CharSpriteManager_Clear();
    assert(chars.size == 0 && chars.sprites == NULL && !loaded);
    CharSpriteManager_Clear();
}
"""
        stubs = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/ain.h"
#include "system4/archive.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "asset_manager.h"
#include "dungeon/dgn.h"
#include "dungeon/dungeon.h"
#include "dungeon/generator.h"
#include "dungeon/map.h"
#include "effect.h"
#include "gfx/font.h"
#include "gfx/gfx.h"
#include "hll/hll.h"
#include "hll/iarray.h"
#include "sact.h"
#include "savedata.h"
#include "sprite.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

struct config config;
struct ain *ain = NULL;
struct vm_pointer *heap = NULL;
bool game_rance02_mg = false;
bool scene_is_dirty = false;
int g_mock_scene_struct_no = 7;

const char *display_utf0(const char *s) { return s; }
const char *display_sjis0(const char *s) { return s; }
char *savedir_path(const char *p) { return strdup(p); }
char *gamedir_path(const char *p) { return strdup(p); }
FILE *atomic_save_open(const char *p, const char *m, char **t) { (void)p; (void)m; *t = NULL; return NULL; }
void atomic_save_abort(FILE *f, char *t) { (void)f; free(t); }
bool atomic_save_commit(FILE *f, const char *p, char *t, bool b) { (void)f; (void)p; free(t); (void)b; return true; }
void savedata_set_background_flush_hook(void (*hook)(void)) { (void)hook; }
int ain_get_struct(struct ain *a, char *name) { (void)a; (void)name; return g_mock_scene_struct_no; }
int ain_get_library(struct ain *a, const char *name) { (void)a; (void)name; return 0; }
int ain_get_library_function(struct ain *a, int l, const char *name) { (void)a; (void)l; (void)name; return -1; }
void static_library_replace(struct static_library *l, const char *n, void *f) { (void)l; (void)n; (void)f; }
void iarray_init_writer(struct iarray_writer *w, const char *h) { memset(w, 0, sizeof(*w)); (void)h; }
void iarray_free_writer(struct iarray_writer *w) { (void)w; }
void iarray_write(struct iarray_writer *w, int v) { (void)w; (void)v; }
void iarray_write_float(struct iarray_writer *w, float v) { (void)w; (void)v; }
void iarray_write_string(struct iarray_writer *w, struct string *s) { (void)w; (void)s; }
void iarray_write_struct(struct iarray_writer *w, struct page *p, bool b) { (void)w; (void)p; (void)b; }
void iarray_write_array(struct iarray_writer *w, struct page *p) { (void)w; (void)p; }
struct page *iarray_to_page(struct iarray_writer *w) { (void)w; return NULL; }
bool iarray_init_reader(struct iarray_reader *r, struct page *p, const char *h) { (void)r; (void)p; (void)h; return false; }
int iarray_read(struct iarray_reader *r) { (void)r; return 0; }
float iarray_read_float(struct iarray_reader *r) { (void)r; return 0.f; }
struct string *iarray_read_string(struct iarray_reader *r) { (void)r; return string_ref(&EMPTY_STRING); }
struct page *iarray_read_struct(struct iarray_reader *r, int s, bool b) { (void)r; (void)s; (void)b; return NULL; }
struct page *iarray_read_array(struct iarray_reader *r, struct ain_type *t) { (void)r; (void)t; return NULL; }
struct page *alloc_page(enum page_type t, int ti, int nv) { (void)t; (void)ti; (void)nv; return NULL; }
struct page *copy_page(struct page *p) { (void)p; return NULL; }
void free_page(struct page *p) { (void)p; }
void delete_page_vars(struct page *p) { (void)p; }
void delete_page(int s) { (void)s; }
void heap_set_page(int s, struct page *p) { (void)s; (void)p; }
int alloc_struct(int no) { (void)no; return 0; }
enum ain_data_type variable_type(struct page *p, int v, int *st, int *r) { (void)p; (void)v; (void)st; (void)r; return AIN_INT; }
void variable_set(struct page *p, int v, enum ain_data_type t, union vm_value val) { (void)p; (void)v; (void)t; (void)val; }
struct page *array_pushback(struct page *a, union vm_value v, enum ain_data_type t, int st) { (void)a; (void)v; (void)t; (void)st; return NULL; }
struct page *heap_get_page(int s) { (void)s; return NULL; }
struct string *heap_get_string(int s) { (void)s; return &EMPTY_STRING; }
int heap_alloc_page(struct page *p) { (void)p; return 0; }
int heap_alloc_string(struct string *s) { (void)s; return 0; }
void heap_unref(int s) { (void)s; }
_Noreturn void _vm_error(const char *fmt, ...) { (void)fmt; abort(); }
Uint32 SDL_GetTicks(void) { return 0; }
int sact_SP_GetShow(int n) { (void)n; return 0; }
int sact_SP_SetShow(int n, int s) { (void)n; (void)s; return 1; }
int sact_SP_GetPosX(int n) { (void)n; return 0; }
int sact_SP_GetPosY(int n) { (void)n; return 0; }
int sact_SP_SetPos(int n, int x, int y) { (void)n; (void)x; (void)y; return 1; }
int sact_SP_GetWidth(int n) { (void)n; return 0; }
int sact_SP_GetHeight(int n) { (void)n; return 0; }
enum font_weight gfx_int_to_font_weight(int w) { (void)w; return (enum font_weight)FW_NORMAL; }
struct font_size *gfx_font_get_size(unsigned f, float s) { (void)f; (void)s; return NULL; }
int gfx_get_fnl_substitute_y_pad(unsigned f, float s) { (void)f; (void)s; return 0; }
float _gfx_render_text(Texture *t, char *msg, struct text_render_metrics *tm) { (void)t; (void)msg; (void)tm; return 0.f; }
int gfx_render_text(Texture *t, float x, int y, char *msg, struct text_style *ts, bool b) { (void)t; (void)x; (void)y; (void)msg; (void)ts; (void)b; return 0; }
float gfx_size_text(struct text_style *ts, const char *s) { (void)ts; (void)s; return 0.f; }
float gfx_size_char(struct text_style *ts, const char *c) { (void)ts; (void)c; return 8.f; }
float gfx_get_actual_font_size(unsigned f, float s) { (void)f; return s; }
float gfx_get_actual_font_size_round_down(unsigned f, float s) { (void)f; return s; }
void gfx_init_texture_rgba(struct texture *t, int w, int h, SDL_Color c) { (void)t; (void)w; (void)h; (void)c; }
void scene_register_sprite(struct sprite *sp) { (void)sp; }
void scene_unregister_sprite(struct sprite *sp) { (void)sp; }
void scene_set_sprite_z(struct sprite *sp, int z) { if (sp) sp->z = z; }
struct sact_sprite *sact_get_sprite(int n) { (void)n; return NULL; }
struct sact_sprite *sact_try_get_sprite(int n) { (void)n; return NULL; }
struct texture *sprite_get_texture(struct sact_sprite *sp) { (void)sp; return NULL; }
void sprite_init(struct sact_sprite *sp) { memset(sp, 0, sizeof(*sp)); }
void sprite_free(struct sact_sprite *sp) { (void)sp; }
void sprite_init_color(struct sact_sprite *sp, int w, int h, int r, int g, int b, int a) { (void)sp; (void)w; (void)h; (void)r; (void)g; (void)b; (void)a; }
void sprite_set_pos(struct sact_sprite *sp, int x, int y) { (void)sp; (void)x; (void)y; }
void sprite_set_blend_rate(struct sact_sprite *sp, int r) { (void)sp; (void)r; }
void sprite_text_clear(struct sact_sprite *sp) { (void)sp; }
void sprite_text_copy(struct sact_sprite *d, struct sact_sprite *s) { (void)d; (void)s; }
bool asset_exists(enum asset_type t, int n) { (void)t; (void)n; return false; }
struct archive_data *asset_get(enum asset_type t, int n) { (void)t; (void)n; return NULL; }
void effect_update_texture(int t, Texture *d, Texture *o, Texture *n, float r) { (void)t; (void)d; (void)o; (void)n; (void)r; }
struct dungeon_context *dungeon_get_context(int s) { (void)s; return NULL; }
struct dgn *dgn_new(uint32_t x, uint32_t y, uint32_t z) { (void)x; (void)y; (void)z; return NULL; }
void dgn_free(struct dgn *d) { (void)d; }
struct dgn_cell *dgn_cell_at(struct dgn *d, uint32_t x, uint32_t y, uint32_t z) { (void)d; (void)x; (void)y; (void)z; return NULL; }
void dgn_calc_lightmap(struct dgn *d) { (void)d; }
void dungeon_map_init(struct dungeon_context *c) { (void)c; }
void dungeon_map_reveal(struct dungeon_context *c, int x, int y, int z, bool f) { (void)c; (void)x; (void)y; (void)z; (void)f; }
void dungeon_set_camera(int s, float x, float y, float z, float a, float ap) { (void)s; (void)x; (void)y; (void)z; (void)a; (void)ap; }
void dungeon_set_chara_pos(int s, int n, float x, float y, float z) { (void)s; (void)n; (void)x; (void)y; (void)z; }
void dungeon_set_chara_cg(int s, int n, int c) { (void)s; (void)n; (void)c; }
void dungeon_set_chara_show(int s, int n, bool sh) { (void)s; (void)n; (void)sh; }
bool dungeon_project_world_to_screen(struct dungeon_context *c, vec3 w, Point *p) { (void)c; (void)w; (void)p; return false; }
struct dgn *dgn_generate_drawfield(int a, int b, int c, int d, int e, int f, int g, uint32_t h, uint8_t *i) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; return NULL;
}
"""
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            tus = []
            for name, body in (
                ("tu_msgskip.c", tu_msgskip),
                ("tu_anteater.c", tu_anteater),
                ("tu_newfont.c", tu_newfont),
                ("tu_srfont.c", tu_srfont),
                ("tu_data.c", tu_data),
                ("tu_bitarray.c", tu_bitarray),
                ("tu_draweffect.c", tu_draweffect),
                ("tu_pastel.c", tu_pastel),
                ("tu_charsprite.c", tu_charsprite),
                ("stubs.c", stubs),
            ):
                p = tmp / name
                p.write_text(body, encoding="utf-8")
                tus.append(p)
            out = compile_and_run(
                harness,
                tus + [
                    LIBSYS4_SRC / "buffer.c",
                    LIBSYS4_SRC / "file.c",
                    LIBSYS4_SRC / "hashtable.c",
                    LIBSYS4_SRC / "string.c",
                    LIBSYS4_SRC / "system.c",
                    LIBSYS4_SRC / "utfsjis.c",
                    LIBSYS4_SRC / "zlib.c",
                ],
                extra_flags=["-lz", "-lm"],
            )
            self.assertEqual(out.strip(), "OK")

    def test_fillangle_gpx2plus_drawmovie_and_movie_end_session(self):
        harness = r"""
#include <assert.h>
#include <stdio.h>

extern void run_fillangle_test(void);
extern void run_gpx2plus_test(void);
extern void run_drawmovie_test(void);
extern void run_movie_end_session_test(void);

int main(void) {
    run_fillangle_test();
    run_gpx2plus_test();
    run_drawmovie_test();
    run_movie_end_session_test();
    puts("OK");
    return 0;
}
"""
        tu_fillangle = r"""
#include <assert.h>
#include <stdbool.h>
#include <string.h>
extern void *g_mock_gl_ctx;
extern int g_unregister_reset_calls;
extern int g_program_deleted;
#include "hll/FillAngle.c"
void run_fillangle_test(void) {
    bool found = false;
    for (int i = 0; lib_FillAngle.functions[i].name; i++) {
        if (!strcmp(lib_FillAngle.functions[i].name, "_ModuleFini")) {
            found = (lib_FillAngle.functions[i].fun == FillAngle_ModuleFini);
            break;
        }
    }
    assert(found);
    FillAngle_ModuleFini(); // fini without init

    // Case 1: GL context gone -> skips glDeleteProgram, still zeroes shader and unregisters callback.
    shader.s.program = 77;
    fill_angle_reset_registered = true;
    g_mock_gl_ctx = NULL;
    g_program_deleted = 0;
    g_unregister_reset_calls = 0;
    FillAngle_ModuleFini();
    assert(shader.s.program == 0 && !fill_angle_reset_registered);
    assert(g_unregister_reset_calls == 1 && g_program_deleted == 0);

    // Case 2: GL context active -> deletes program and resets state.
    shader.s.program = 88;
    fill_angle_reset_registered = true;
    g_mock_gl_ctx = (void *)0x1;
    FillAngle_ModuleFini();
    assert(shader.s.program == 0 && !fill_angle_reset_registered);
    assert(g_unregister_reset_calls == 2 && g_program_deleted == 1);

    // Double-fini is a safe no-op.
    FillAngle_ModuleFini();
    assert(g_unregister_reset_calls == 2 && g_program_deleted == 1);
}
"""
        tu_gpx2plus = r"""
#include <assert.h>
#include <stdbool.h>
#include <string.h>
extern int g_gfx_init_calls;
extern int g_gfx_font_init_calls;
extern int g_audio_init_calls;
extern int g_gfx_delete_tex_calls;
#include "hll/Gpx2Plus.c"
void run_gpx2plus_test(void) {
    bool found = false;
    for (int i = 0; lib_Gpx2Plus.functions[i].name; i++) {
        if (!strcmp(lib_Gpx2Plus.functions[i].name, "_ModuleFini")) {
            found = (lib_Gpx2Plus.functions[i].fun == Gpx2Plus_ModuleFini);
            break;
        }
    }
    assert(found);
    Gpx2Plus_ModuleFini(); // fini without init

    Gpx2Plus_Init(NULL, NULL);
    assert(g_gfx_init_calls == 1 && g_gfx_font_init_calls == 1 && g_audio_init_calls == 1);
    int s1 = Gpx2Plus_Create(32, 32, 32);
    assert(s1 > 0);
    surfaces[s1]->texture.handle = 123;

    Gpx2Plus_ModuleFini();
    assert(surfaces == NULL && nr_surfaces == 0 && msgbuf.text == NULL);
    assert(g_gfx_delete_tex_calls >= 2);

    // Double-fini is safe.
    Gpx2Plus_ModuleFini();

    // Session 2 re-initializes gfx/font/audio cleanly!
    Gpx2Plus_Init(NULL, NULL);
    assert(g_gfx_init_calls == 2 && g_gfx_font_init_calls == 2 && g_audio_init_calls == 2);
    Gpx2Plus_ModuleFini();
}
"""
        tu_drawmovie = r"""
#include <assert.h>
#include <stdbool.h>
#include <string.h>
extern int g_movie_free_calls;
#include "hll/DrawMovie.c"
static bool has_module_fini(struct static_library *lib) {
    for (int i = 0; lib->functions[i].name; i++) {
        if (!strcmp(lib->functions[i].name, "_ModuleFini"))
            return lib->functions[i].fun == DrawMovie_Release;
    }
    return false;
}
void run_drawmovie_test(void) {
    assert(has_module_fini(&lib_DrawMovie));
    assert(has_module_fini(&lib_DrawMovie2));
    assert(has_module_fini(&lib_DrawMovie3));
    DrawMovie_Release();
    struct string *fname = make_string("test.mpg", 8);
    assert(DrawMovie_Load(fname));
    free_string(fname);
    assert(mc != NULL);
    g_movie_free_calls = 0;
    DrawMovie_Release();
    assert(mc == NULL && g_movie_free_calls == 1);
    DrawMovie_Release();
    assert(g_movie_free_calls == 1);
}
"""
        tu_movie = r"""
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#define movie_load movie_plmpeg_real_load
#define movie_free movie_plmpeg_real_free
#define movie_play movie_plmpeg_real_play
#define movie_draw movie_plmpeg_real_draw
#define movie_set_volume movie_plmpeg_real_set_volume
#define movie_is_end movie_plmpeg_real_is_end
#define movie_get_position movie_plmpeg_real_get_position
#include "movie_plmpeg.c"
#undef movie_load
#undef movie_free
#undef movie_play
#undef movie_draw
#undef movie_set_volume
#undef movie_is_end
#undef movie_get_position
extern int g_unregister_reset_calls;
void run_movie_end_session_test(void) {
    int before = g_unregister_reset_calls;
    movie_end_session(); // fini without init
    assert(g_unregister_reset_calls == before + 1);
    assert(movie_shader.program == 0);

    movie_shader.program = 55;
    movie_shader.prepare = prepare_movie_shader;
    struct movie_context dummy = {0};
    register_movie(&dummy);
    unregister_movie(&dummy);
    assert(active_movies != NULL && nr_active_movies == 0);

    movie_end_session();
    assert(movie_shader.program == 0 && movie_shader.prepare == NULL);
    assert(active_movies == NULL && cap_active_movies == 0);

    // Double call is completely safe.
    movie_end_session();
    assert(movie_shader.program == 0 && active_movies == NULL);
}
"""
        stubs = GL_MOCK_HEADER + r"""
#include <SDL.h>
#include "system4.h"
#include "system4/cg.h"
#include "system4/string.h"
#include "asset_manager.h"
#include "audio.h"
#include "effect.h"
#include "gfx/font.h"
#include "gfx/gfx.h"
#include "input.h"
#include "kiwi.h"
#include "mixer.h"
#include "movie.h"
#include "sact.h"
#include "sprite.h"
#include "sts_mixer.h"
#include "vm.h"
#include "xsystem4.h"

struct config config = { .view_width = 800, .view_height = 600 };
bool game_daibanchou_en = false;
void *g_mock_gl_ctx = NULL;
int g_unregister_reset_calls = 0;
int g_gfx_init_calls = 0;
int g_gfx_font_init_calls = 0;
int g_audio_init_calls = 0;
int g_gfx_delete_tex_calls = 0;
int g_movie_free_calls = 0;
static Texture g_main_surf = { .handle = 1, .w = 800, .h = 600 };
const char *effect_names[NR_EFFECTS];

SDL_GLContext SDL_GL_GetCurrentContext(void) { return g_mock_gl_ctx; }
SDL_mutex *SDL_CreateMutex(void) { return NULL; }
void SDL_DestroyMutex(SDL_mutex *m) { (void)m; }
int SDL_LockMutex(SDL_mutex *m) { (void)m; return 0; }
int SDL_UnlockMutex(SDL_mutex *m) { (void)m; return 0; }
Uint32 SDL_GetTicks(void) { return 0; }
void SDL_Delay(Uint32 ms) { (void)ms; }

const char *display_utf0(const char *s) { return s; }
char *gamedir_path_icase(const char *p) { (void)p; return NULL; }
int gfx_init(void) { g_gfx_init_calls++; return 0; }
void gfx_font_init(void) { g_gfx_font_init_calls++; }
void audio_init(void) { g_audio_init_calls++; }
Texture *gfx_main_surface(void) { return &g_main_surf; }
void gfx_clear(void) {}
void gfx_swap(void) {}
void gfx_load_shader(struct shader *s, const char *v, const char *f) { (void)v; (void)f; s->program = 1; }
void gfx_register_reset_callback(gfx_reset_callback cb, void *u) { (void)cb; (void)u; }
void gfx_unregister_reset_callback(gfx_reset_callback cb, void *u) { (void)cb; (void)u; g_unregister_reset_calls++; }
void gfx_delete_texture(struct texture *t) { g_gfx_delete_tex_calls++; if (t) t->handle = 0; }
void gfx_init_texture_rgba(struct texture *t, int w, int h, SDL_Color c) { (void)c; t->handle = 1; t->w = w; t->h = h; }
void gfx_init_texture_with_cg(struct texture *t, struct cg *cg) { (void)cg; t->handle = 1; }
GLuint gfx_set_framebuffer(GLenum t, Texture *tx, int x, int y, int w, int h) { (void)t; (void)tx; (void)x; (void)y; (void)w; (void)h; return 1; }
void gfx_reset_framebuffer(GLenum t, GLuint f) { (void)t; (void)f; }
void gfx_render(struct gfx_render_job *j) { (void)j; }
void gfx_render_texture(struct texture *t, Rectangle *r) { (void)t; (void)r; }
SDL_Color gfx_get_pixel(Texture *t, int x, int y) { (void)t; (void)x; (void)y; return (SDL_Color){0}; }
void gfx_copy(struct texture *d, int dx, int dy, struct texture *s, int sx, int sy, int w, int h) { (void)d; (void)dx; (void)dy; (void)s; (void)sx; (void)sy; (void)w; (void)h; }
void gfx_copy_bright(struct texture *d, int dx, int dy, struct texture *s, int sx, int sy, int w, int h, int r) { (void)d; (void)dx; (void)dy; (void)s; (void)sx; (void)sy; (void)w; (void)h; (void)r; }
void gfx_copy_amap(struct texture *d, int dx, int dy, struct texture *s, int sx, int sy, int w, int h) { (void)d; (void)dx; (void)dy; (void)s; (void)sx; (void)sy; (void)w; (void)h; }
void gfx_copy_sprite(struct texture *d, int dx, int dy, struct texture *s, int sx, int sy, int w, int h, SDL_Color c) { (void)d; (void)dx; (void)dy; (void)s; (void)sx; (void)sy; (void)w; (void)h; (void)c; }
void gfx_blend(Texture *d, int dx, int dy, Texture *s, int sx, int sy, int w, int h, int a) { (void)d; (void)dx; (void)dy; (void)s; (void)sx; (void)sy; (void)w; (void)h; (void)a; }
void gfx_blend_amap(struct texture *d, int dx, int dy, struct texture *s, int sx, int sy, int w, int h) { (void)d; (void)dx; (void)dy; (void)s; (void)sx; (void)sy; (void)w; (void)h; }
void gfx_blend_amap_alpha(struct texture *d, int dx, int dy, struct texture *s, int sx, int sy, int w, int h, int a) { (void)d; (void)dx; (void)dy; (void)s; (void)sx; (void)sy; (void)w; (void)h; (void)a; }
void gfx_fill(struct texture *d, int x, int y, int w, int h, int r, int g, int b) { (void)d; (void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b; }
void gfx_fill_alpha_color(struct texture *d, int x, int y, int w, int h, int r, int g, int b, int a) { (void)d; (void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b; (void)a; }
void gfx_fill_amap(struct texture *d, int x, int y, int w, int h, int a) { (void)d; (void)x; (void)y; (void)w; (void)h; (void)a; }
void gfx_copy_stretch(struct texture *d, int dx, int dy, int dw, int dh, struct texture *s, int sx, int sy, int sw, int sh) { (void)d; (void)dx; (void)dy; (void)dw; (void)dh; (void)s; (void)sx; (void)sy; (void)sw; (void)sh; }
void gfx_copy_stretch_amap(struct texture *d, int dx, int dy, int dw, int dh, struct texture *s, int sx, int sy, int sw, int sh) { (void)d; (void)dx; (void)dy; (void)dw; (void)dh; (void)s; (void)sx; (void)sy; (void)sw; (void)sh; }
void gfx_copy_reverse_LR(Texture *d, int dx, int dy, Texture *s, int sx, int sy, int w, int h) { (void)d; (void)dx; (void)dy; (void)s; (void)sx; (void)sy; (void)w; (void)h; }
void gfx_copy_reverse_UD(Texture *d, int dx, int dy, Texture *s, int sx, int sy, int w, int h) { (void)d; (void)dx; (void)dy; (void)s; (void)sx; (void)sy; (void)w; (void)h; }
void gfx_copy_reverse_amap_LR(Texture *d, int dx, int dy, Texture *s, int sx, int sy, int w, int h) { (void)d; (void)dx; (void)dy; (void)s; (void)sx; (void)sy; (void)w; (void)h; }
void gfx_draw_text_to_pmap(Texture *d, int x, int y, char *m) { (void)d; (void)x; (void)y; (void)m; }
void gfx_draw_text_to_amap(Texture *d, int x, int y, char *m) { (void)d; (void)x; (void)y; (void)m; }
int gfx_render_text(Texture *d, float x, int y, char *m, struct text_style *ts, bool b) { (void)d; (void)x; (void)y; (void)m; (void)ts; (void)b; return 0; }
bool gfx_set_font_size(unsigned int s) { (void)s; return true; }
int gfx_get_font_size(void) { return 16; }
bool gfx_set_font_face(enum font_face f) { (void)f; return true; }
bool gfx_set_font_weight(int w) { (void)w; return true; }
int gfx_get_font_weight(void) { return FW_NORMAL; }
bool gfx_set_font_underline(bool u) { (void)u; return true; }
bool gfx_get_font_underline(void) { return false; }
bool gfx_set_font_strikeout(bool s) { (void)s; return true; }
bool gfx_get_font_strikeout(void) { return false; }
bool gfx_set_font_space(int s) { (void)s; return true; }
int gfx_get_font_space(void) { return 0; }
bool gfx_set_font_color(SDL_Color c) { (void)c; return true; }
SDL_Color gfx_get_font_color(void) { return (SDL_Color){0}; }
struct cg *asset_cg_load(int n) { (void)n; return NULL; }
void cg_free(struct cg *cg) { (void)cg; }
void effect_update_texture(int t, Texture *d, Texture *o, Texture *n, float r) { (void)t; (void)d; (void)o; (void)n; (void)r; }
void handle_events(void) {}
bool scene_is_dirty = false;
void scene_register_sprite(struct sprite *sp) { (void)sp; }
void scene_unregister_sprite(struct sprite *sp) { (void)sp; }
struct sact_sprite *sact_try_get_sprite(int n) { (void)n; return NULL; }
struct texture *sprite_get_texture(struct sact_sprite *sp) { (void)sp; return NULL; }
_Noreturn void _vm_error(const char *fmt, ...) { (void)fmt; abort(); }
int mixer_stream_play(sts_mixer_stream_t *s, int v) { (void)s; (void)v; return -1; }
void mixer_stream_stop(int v) { (void)v; }
void mixer_stream_stop_stream(sts_mixer_stream_t *s) { (void)s; }
bool mixer_stream_set_volume(int v, int vol) { (void)v; (void)vol; return true; }
bool mixer_stream_set_volume_stream(sts_mixer_stream_t *s, int vol) { (void)s; (void)vol; return true; }
bool kiwi_prepare_from_memory(int id, uint8_t *buf, size_t sz, int g) { (void)id; free(buf); (void)sz; (void)g; return false; }
bool kiwi_play_sound(int id) { (void)id; return true; }
bool kiwi_stop_sound(int id) { (void)id; return true; }
bool kiwi_unprepare_sound(int id) { (void)id; return true; }
bool kiwi_seek_sound(int id, int ms) { (void)id; (void)ms; return true; }

// Stubs for DrawMovie.c (movie_plmpeg.c has its own renamed symbols above)
struct movie_context *movie_load(const char *f) { (void)f; return (struct movie_context *)calloc(1, 8); }
void movie_free(struct movie_context *mc) { g_movie_free_calls++; free(mc); }
bool movie_play(struct movie_context *mc) { (void)mc; return true; }
bool movie_draw(struct movie_context *mc, struct sact_sprite *sp) { (void)mc; (void)sp; return true; }
bool movie_set_volume(struct movie_context *mc, int v) { (void)mc; (void)v; return true; }
bool movie_is_end(struct movie_context *mc) { (void)mc; return false; }
int movie_get_position(struct movie_context *mc) { (void)mc; return 0; }
"""
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            tus = []
            for name, body in (
                ("tu_fillangle.c", tu_fillangle),
                ("tu_gpx2plus.c", tu_gpx2plus),
                ("tu_drawmovie.c", tu_drawmovie),
                ("tu_movie.c", tu_movie),
                ("stubs.c", stubs),
            ):
                p = tmp / name
                p.write_text(body, encoding="utf-8")
                tus.append(p)
            out = compile_and_run(
                harness,
                tus + [
                    LIBSYS4_SRC / "file.c",
                    LIBSYS4_SRC / "string.c",
                    LIBSYS4_SRC / "system.c",
                    LIBSYS4_SRC / "utfsjis.c",
                ],
                extra_flags=["-lm"],
            )
            self.assertEqual(out.strip(), "OK")

    def test_video_ios_movie_end_session_wiring_and_syntax_compile(self):
        video_src = (ROOT / "src/video.c").read_text(encoding="utf-8", errors="ignore")
        self.assertIn("void movie_end_session(void);", video_src)
        self.assertGreaterEqual(video_src.count("movie_end_session();"), 2)

        movie_h = (ROOT / "include/movie.h").read_text(encoding="utf-8", errors="ignore")
        self.assertIn("void movie_end_session(void);", movie_h)

        # Syntax-compile all modified xsystem4 C files with GLES headers.
        cc = shutil.which("cc") or shutil.which("gcc")
        assert cc, "C compiler required"
        c_files = [
            "src/dungeon/map.c",
            "src/dungeon/renderer.c",
            "src/dungeon/dungeon.c",
            "src/dungeon/skybox.c",
            "src/dungeon/dtx.c",
            "src/movie_plmpeg.c",
            "src/video.c",
            "src/hll/Gpx2Plus.c",
            "src/hll/AnteaterADVEngine.c",
            "src/hll/NewFont.c",
            "src/hll/SengokuRanceFont.c",
            "src/hll/DrawMovie.c",
            "src/hll/FillAngle.c",
            "src/hll/InputString.c",
            "src/hll/MsgSkip.c",
            "src/hll/Data.c",
            "src/hll/Bitarray.c",
            "src/hll/DrawEffect.c",
            "src/hll/PastelChime2.c",
            "src/hll/CharSpriteManager.c",
        ]
        base_cmd = [
            cc,
            "-std=gnu11",
            "-fsyntax-only",
            "-Wall",
            "-Wextra",
            "-Wno-unused-parameter",
            "-Wno-sign-compare",
            "-D_DEFAULT_SOURCE",
            "-DUSE_GLES",
            "-DXSYS4_DATA_DIR=\".\"",
            f"-I{ROOT / 'include'}",
            f"-I{ROOT / 'src'}",
            f"-I{ROOT / 'src/hll'}",
            f"-I{LIBSYS4_INC}",
            *pkg_cflags("sdl2", "cglm", "freetype2"),
        ]
        for rel in c_files:
            res = subprocess.run(base_cmd + [str(ROOT / rel)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.assertEqual(res.returncode, 0, f"Syntax check failed for {rel}:\n{res.stderr}")


if __name__ == "__main__":
    unittest.main()
