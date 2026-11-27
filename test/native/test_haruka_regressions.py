#!/usr/bin/env python3
"""Host C regression tests for Beat Blades Haruka (超昂閃忍ハルカ) compatibility in xsystem4."""

import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def encode_bgi(text: str) -> bytes:
    raw = text.encode("ascii")
    return bytes(((b >> 4) | ((b & 0xF) << 4)) for b in raw)


def pkg_cflags(*pkgs: str) -> list[str]:
    return subprocess.check_output(["pkg-config", "--cflags", *pkgs], text=True).split()


def extract_c_function(src: str, signature_prefix: str) -> str:
    idx = src.find(signature_prefix)
    assert idx >= 0, f"could not locate {signature_prefix}"
    end = src.find("\n}", idx)
    assert end >= 0, f"could not locate closing brace for {signature_prefix}"
    return src[idx : end + 2]


def compile_and_run(
    c_src: str,
    extra_sources: list[Path],
    extra_flags: list[str] | None = None,
    args: list[str] | None = None,
    pkgs: tuple[str, ...] = ("sdl2",),
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
            f"-I{ROOT / 'subprojects' / 'libsys4' / 'include'}",
            *pkg_cflags(*pkgs),
            str(src_path),
            *[str(p) for p in extra_sources],
            *(extra_flags or []),
            "-o",
            str(bin_path),
        ]
        res_c = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res_c.returncode != 0:
            raise AssertionError(f"Compile failed ({res_c.returncode}):\n{res_c.stderr}")
        res_r = subprocess.run([str(bin_path), *(args or [])], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res_r.returncode != 0:
            raise AssertionError(f"Run failed ({res_r.returncode}):\nstdout:\n{res_r.stdout}\nstderr:\n{res_r.stderr}")
        return res_r.stdout


LIBSYS4_BGI_WAI = [
    ROOT / "subprojects/libsys4/src/system.c",
    ROOT / "subprojects/libsys4/src/file.c",
    ROOT / "subprojects/libsys4/src/utfsjis.c",
    ROOT / "subprojects/libsys4/src/string.c",
    ROOT / "src/audio_meta.c",
]

LIBSYS4_AIN = [
    ROOT / "subprojects/libsys4/src/ain.c",
    ROOT / "subprojects/libsys4/src/buffer.c",
    ROOT / "subprojects/libsys4/src/file.c",
    ROOT / "subprojects/libsys4/src/hashtable.c",
    ROOT / "subprojects/libsys4/src/instructions.c",
    ROOT / "subprojects/libsys4/src/mt19937int.c",
    ROOT / "subprojects/libsys4/src/string.c",
    ROOT / "subprojects/libsys4/src/system.c",
    ROOT / "subprojects/libsys4/src/utfsjis.c",
    ROOT / "subprojects/libsys4/src/zlib.c",
]


class HarukaRegressionsTest(unittest.TestCase):
    def test_translated_ain_finds_original_audio_archives_and_metadata(self):
        harness = r"""
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/utfsjis.h"
#include "asset_manager.h"
#include "mixer.h"
#include "xsystem4.h"

struct config config;

const char *display_utf0(const char *s) { return s; }
char *gamedir_path(const char *s) { return path_join(config.game_dir, s); }
char *gamedir_path_icase(const char *s) { return gamedir_path(s); }
char *savedir_path(const char *s) { abort(); }
struct cg *cg_load_data(struct archive_data *d) { (void)d; return NULL; }
struct cg *cg_load_file(const char *f) { (void)f; return NULL; }
void cg_set_base_loader_by_name(cg_base_loader_by_name_fn fn) { (void)fn; }
void cg_set_base_loader_by_id(cg_base_loader_by_id_fn fn) { (void)fn; }

static void check_asset(enum asset_type type, const char *name) {
    struct archive_data *data = asset_get(type, 1);
    assert(data && !strcmp(data->name, name));
    archive_free_data(data);
}

int main(int argc, char **argv) {
    assert(argc == 6);
    config.game_dir = argv[1];
    bool patched = !strcmp(argv[2], "AliceCN.ain");
    sys4_set_text_gbk(patched);
    config.ain_filename = utf2sjis(argv[2], strlen(argv[2]));
    asset_manager_init();
    check_asset(ASSET_CG, argv[3]);
    check_asset(ASSET_SOUND, "voice.wav");
    if (!strcmp(argv[4], "-")) {
        assert(!asset_exists(ASSET_BGM, 1) && !config.bgi_path);
    } else {
        check_asset(ASSET_BGM, argv[4]);
        assert(config.bgi_path);
        bgi_read(config.bgi_path);
        assert(bgi_get(1) && bgi_get(1)->loop_start == atoi(argv[5]));
    }
    assert(config.wai_path);
    wai_load(config.wai_path);
    assert(wai_get(1) && wai_get(1)->channel == 4);
}
"""
        def write_ald(path, entry):
            data = bytearray(3 * 256)
            data[:9] = bytes((1, 0, 0, 2, 0, 0, 3, 0, 0))
            data[256:259] = bytes((1, 1, 0))
            struct.pack_into("<II", data, 512, 32, 1)
            data[528:528 + len(entry)] = entry
            path.write_bytes(data)

        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            prefix = "超昂閃忍ハルカ"
            write_ald(tmp / f"{prefix}GA.ald", b"original.qnt")
            write_ald(tmp / f"{prefix}BA.ald", b"music.wav")
            write_ald(tmp / f"{prefix}WA.ald", b"voice.wav")
            (tmp / f"{prefix}BA.BGI").write_bytes(encode_bgi("1, 0, 100, 1000, -1\n"))
            wai = bytearray(48)
            struct.pack_into("<4sIII", wai, 0, b"XI2\0", 1, 2, 3)
            struct.pack_into("<III", wai, 36, 1, 0, 4)
            (tmp / f"{prefix}.wai").write_bytes(wai)
            libsys4 = ROOT / "subprojects/libsys4/src"
            sources = [
                *LIBSYS4_BGI_WAI, ROOT / "src/asset_manager.c", ROOT / "src/savedata.c",
                *(libsys4 / f"{src}.c" for src in ("ald", "afa", "afa3", "archive", "buffer", "hashtable", "zlib")),
            ]
            flags = ["-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-lz", "-lm"]
            compile_and_run(harness, sources, flags, [tmpdir, f"{prefix}.ain", "original.qnt", "music.wav", "100"])
            with self.subTest(layout="renamed script"):
                compile_and_run(harness, sources, flags, [tmpdir, "AliceCN.ain", "original.qnt", "music.wav", "100"])
            patched = [tmpdir, "AliceCN.ain", "translated.qnt"]
            with self.subTest(layout="renamed script and translated graphics"):
                write_ald(tmp / "AliceCNGA.ald", b"translated.qnt")
                compile_and_run(harness, sources, flags, [*patched, "music.wav", "100"])
            with self.subTest(layout="patch also supplies BGM"):
                write_ald(tmp / "AliceCNBA.ald", b"patch.wav")
                (tmp / "AliceCN.bgi").write_bytes(encode_bgi("1, 0, 200, 1000, -1\n"))
                compile_and_run(harness, sources, flags, [*patched, "patch.wav", "200"])
            with self.subTest(layout="ambiguous fallback BGM"):
                (tmp / "AliceCNBA.ald").unlink()
                (tmp / "AliceCN.bgi").unlink()
                write_ald(tmp / "UnrelatedBA.ald", b"unrelated.wav")
                compile_and_run(harness, sources, flags, [*patched, "-", "0"])

    def test_playmovie_set_volume_and_lifecycle(self):
        # Directly compile src/hll/PlayMovie.c against a mocked movie backend and verify
        # Haruka's PlayMovie.SetVolume export, null-context behavior, volume propagation,
        # and _ModuleFini teardown.
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4/string.h"
#include "hll/hll.h"
#include "movie.h"

struct movie_context {
    char filename[128];
    int volume;
    bool playing;
    bool ended;
};

static int mock_free_count = 0;
static int mock_set_volume_calls = 0;
static int mock_last_volume = -1;
static struct movie_context *mock_active = NULL;

struct movie_context *movie_load(const char *filename) {
    struct movie_context *mc = calloc(1, sizeof(*mc));
    strncpy(mc->filename, filename, sizeof(mc->filename) - 1);
    mc->volume = 100;
    mock_active = mc;
    return mc;
}

void movie_free(struct movie_context *mc) {
    mock_free_count++;
    if (mock_active == mc)
        mock_active = NULL;
    free(mc);
}

bool movie_play(struct movie_context *mc) {
    if (!mc) return false;
    mc->playing = true;
    return true;
}

bool movie_draw(struct movie_context *mc, struct sact_sprite *sprite) {
    (void)sprite;
    return mc && mc->playing && !mc->ended;
}

bool movie_is_end(struct movie_context *mc) {
    return !mc || mc->ended;
}

bool movie_set_volume(struct movie_context *mc, int volume) {
    mock_set_volume_calls++;
    mock_last_volume = volume;
    if (!mc) return false;
    mc->volume = volume;
    return true;
}

extern struct static_library lib_PlayMovie;

static void *find_export(const char *name) {
    for (int i = 0; lib_PlayMovie.functions[i].name; i++) {
        if (!strcmp(lib_PlayMovie.functions[i].name, name))
            return lib_PlayMovie.functions[i].fun;
    }
    return NULL;
}

int main(void) {
    int (*fn_init)(void) = find_export("Init");
    int (*fn_load)(struct string *) = find_export("Load");
    int (*fn_play)(void) = find_export("Play");
    int (*fn_is_play)(void) = find_export("IsPlay");
    bool (*fn_set_volume)(int) = find_export("SetVolume");
    void (*fn_stop)(void) = find_export("Stop");
    void (*fn_release)(void) = find_export("Release");
    void (*fn_fini)(void) = find_export("_ModuleFini");
    assert(fn_init && fn_load && fn_play && fn_is_play && fn_set_volume && fn_stop && fn_release && fn_fini);

    assert(fn_init() == 1);
    // Calling SetVolume before Load returns false safely.
    assert(fn_set_volume(40) == false);
    assert(mock_set_volume_calls == 1 && mock_last_volume == 40);

    struct string *s = make_string("movie\\Opening.alm", strlen("movie\\Opening.alm"));
    assert(fn_load(s) == 1);
    assert(mock_active && !strcmp(mock_active->filename, "movie\\Opening.alm"));
    assert(fn_play() == 1);
    assert(fn_is_play() == 1);

    // Lower volume during playback (as Haruka's 忍者ムービー再生 does when BGM slider != 100).
    assert(fn_set_volume(40) == true);
    assert(mock_active->volume == 40);
    // Mute during playback.
    assert(fn_set_volume(0) == true);
    assert(mock_active->volume == 0);

    // Release frees context cleanly; subsequent SetVolume returns false.
    fn_release();
    assert(mock_free_count == 1 && mock_active == NULL);
    assert(fn_set_volume(80) == false);

    // _ModuleFini cleans up an active movie on VM reset/exit.
    assert(fn_load(s) == 1);
    fn_fini();
    assert(mock_free_count == 2 && mock_active == NULL);
    // Double _ModuleFini is a no-op.
    fn_fini();
    assert(mock_free_count == 2);

    free_string(s);
    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            [
                ROOT / "src/hll/PlayMovie.c",
                ROOT / "subprojects/libsys4/src/file.c",
                ROOT / "subprojects/libsys4/src/string.c",
                ROOT / "subprojects/libsys4/src/system.c",
                ROOT / "subprojects/libsys4/src/utfsjis.c",
            ],
        )
        self.assertEqual(out.strip(), "OK")

    def test_movie_set_volume_clamping_both_backends(self):
        plmpeg_src = (ROOT / "src/movie_plmpeg.c").read_text(encoding="utf-8", errors="ignore")
        ffmpeg_src = (ROOT / "src/movie_ffmpeg.c").read_text(encoding="utf-8", errors="ignore")
        fn_plmpeg = extract_c_function(plmpeg_src, "bool movie_set_volume(")
        fn_ffmpeg = extract_c_function(ffmpeg_src, "bool movie_set_volume(")
        # Keep both backends identical in volume clamping/dispatch behavior.
        self.assertEqual(fn_plmpeg.replace("\r\n", "\n"), fn_ffmpeg.replace("\r\n", "\n"))

        harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

struct sts_mixer_stream_t {{
    int dummy;
}};

struct movie_context {{
    int voice;
    int volume;
    struct sts_mixer_stream_t sts_stream;
}};

static int mixer_calls = 0;
static struct sts_mixer_stream_t *last_stream = NULL;
static int last_mixer_vol = -99;

static bool mixer_stream_set_volume_stream(struct sts_mixer_stream_t *stream, int volume) {{
    mixer_calls++;
    last_stream = stream;
    last_mixer_vol = volume;
    return true;
}}

{fn_plmpeg}

int main(void) {{
    assert(movie_set_volume(NULL, 50) == false);
    assert(mixer_calls == 0);

    // Clamp & forward stream pointer cleanly (safe no-op in mixer when not streaming).
    struct movie_context mc = {{ .voice = -1, .volume = 100 }};
    assert(movie_set_volume(&mc, -25) == true);
    assert(mc.volume == 0 && mixer_calls == 1 && last_stream == &mc.sts_stream && last_mixer_vol == 0);
    assert(movie_set_volume(&mc, 180) == true);
    assert(mc.volume == 100 && mixer_calls == 2 && last_stream == &mc.sts_stream && last_mixer_vol == 100);
    assert(movie_set_volume(&mc, 42) == true);
    assert(mc.volume == 42 && mixer_calls == 3 && last_stream == &mc.sts_stream && last_mixer_vol == 42);

    mc.voice = 3;
    assert(movie_set_volume(&mc, -10) == true);
    assert(mc.volume == 0 && mixer_calls == 4 && last_stream == &mc.sts_stream && last_mixer_vol == 0);
    assert(movie_set_volume(&mc, 250) == true);
    assert(mc.volume == 100 && mixer_calls == 5 && last_stream == &mc.sts_stream && last_mixer_vol == 100);
    assert(movie_set_volume(&mc, 37) == true);
    assert(mc.volume == 37 && mixer_calls == 6 && last_stream == &mc.sts_stream && last_mixer_vol == 37);

    puts("OK");
    return 0;
}}
"""
        out = compile_and_run(harness, [])
        self.assertEqual(out.strip(), "OK")

    def test_fill_angle_negative_sweep_and_dirty_flag(self):
        # Compile src/hll/FillAngle.c directly against mocked GL/gfx/sact hooks to verify
        # Haruka's C_DRAW_DATE@_DrawGuage calls (negative angles [-360..0], full-circle,
        # zero-angle, positive angles, alpha clamping, and sprite_dirty notification).
        harness = r"""
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define GL_GLEXT_PROTOTYPES
#include "gfx/gfx.h"
#include "hll/hll.h"
#include "sact.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool scene_is_dirty = false;
static struct sact_sprite mock_sp;
static bool mock_sp_valid = true;
static int dirty_calls = 0;
static int render_calls = 0;
static int fill_amap_calls = 0;
static int last_fill_amap_alpha = -1;
static float u_center[2] = {0, 0};
static float u_start_angle = 0.0f;
static float u_angle = 0.0f;
static float u_color[4] = {0, 0, 0, 0};

// Minimal OpenGL/gfx/sact stubs for FillAngle.c
struct sact_sprite *sact_try_get_sprite(int sp_no) {
    return (mock_sp_valid && sp_no == 7) ? &mock_sp : NULL;
}

struct texture *sprite_get_texture(struct sact_sprite *sp) {
    return &sp->texture;
}

void scene_register_sprite(struct sprite *sp) {
    assert(sp == &mock_sp.sp);
    dirty_calls++;
}

void scene_unregister_sprite(struct sprite *sp) {
    (void)sp;
}

void gfx_load_shader(struct shader *dst, const char *v, const char *f) {
    (void)v;
    (void)f;
    dst->program = 42;
}

void gfx_register_reset_callback(gfx_reset_callback cb, void *user) {
    (void)cb;
    (void)user;
}

void gfx_unregister_reset_callback(gfx_reset_callback cb, void *user) {
    (void)cb;
    (void)user;
}

SDL_GLContext SDL_GL_GetCurrentContext(void) {
    return NULL;
}

GLboolean glIsProgram(GLuint program) {
    (void)program;
    return GL_FALSE;
}

void glDeleteProgram(GLuint program) {
    (void)program;
}

GLuint gfx_set_framebuffer(GLenum target, Texture *t, int x, int y, int w, int h) {
    (void)target; (void)t; (void)x; (void)y; (void)w; (void)h;
    return 99;
}

void gfx_reset_framebuffer(GLenum target, GLuint fbo) {
    (void)target;
    assert(fbo == 99);
}

void gfx_render(struct gfx_render_job *job) {
    assert(job && job->shader && job->shader->program == 42);
    render_calls++;
}

void gfx_fill_amap(Texture *dst, int x, int y, int w, int h, int a) {
    assert(dst == &mock_sp.texture);
    assert(x == 0 && y == 0 && w == dst->w && h == dst->h);
    fill_amap_calls++;
    last_fill_amap_alpha = a;
}

GLint glGetUniformLocation(GLuint program, const GLchar *name) {
    assert(program == 42);
    if (!strcmp(name, "center")) return 1;
    if (!strcmp(name, "start_angle")) return 2;
    if (!strcmp(name, "angle")) return 3;
    if (!strcmp(name, "color")) return 4;
    return -1;
}

void glUseProgram(GLuint program) { (void)program; }
void glUniform2f(GLint loc, GLfloat v0, GLfloat v1) {
    if (loc == 1) { u_center[0] = v0; u_center[1] = v1; }
}
void glUniform1f(GLint loc, GLfloat v0) {
    if (loc == 2) u_start_angle = v0;
    if (loc == 3) u_angle = v0;
}
void glUniform4f(GLint loc, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
    if (loc == 4) { u_color[0] = v0; u_color[1] = v1; u_color[2] = v2; u_color[3] = v3; }
}
void glBlendFuncSeparate(GLenum a, GLenum b, GLenum c, GLenum d) {
    (void)a; (void)b; (void)c; (void)d;
}

// Mirror shaders/fill_angle.f.glsl fragment predicate: true => pixel is overwritten with alpha.
static bool shader_fills_pixel(float uv_x, float uv_y) {
    float px = uv_x - u_center[0];
    float py = uv_y - u_center[1];
    float a = atan2f(py, px);
    float two_pi = (float)(2.0 * M_PI);
    float diff = a - u_start_angle;
    float m = diff - two_pi * floorf(diff / two_pi);
    return m <= u_angle;
}

extern struct static_library lib_FillAngle;

int main(void) {
    bool (*fn_fill)(int, int, int, int, int, int) = lib_FillAngle.functions[0].fun;
    assert(fn_fill && !strcmp(lib_FillAngle.functions[0].name, "FillAngleAMAP"));

    // Invalid sprite and zero-size texture return false safely.
    mock_sp.sp.has_pixel = true;
    mock_sp.texture.w = 157;
    mock_sp.texture.h = 161;
    scene_is_dirty = false;
    assert(fn_fill(999, 73, 82, 0, -180, 0) == false);
    mock_sp.texture.w = 0;
    assert(fn_fill(7, 73, 82, 0, -180, 0) == false);
    assert(!scene_is_dirty && dirty_calls == 0 && render_calls == 0 && fill_amap_calls == 0);

    // Haruka Peace Gauge dimensions (CG 20101: 157x161, center=(73, 82)).
    mock_sp.texture.w = 157;
    mock_sp.texture.h = 161;

    // 1. Peace = 100% -> angle = 0: no draw, returns true (avoids 1px ray cut).
    assert(fn_fill(7, 73, 82, 0, 0, 0) == true);
    assert(!scene_is_dirty && render_calls == 0 && fill_amap_calls == 0);

    // 2. Peace = 0% -> angle = -360: full-texture AMAP clear + sprite_dirty.
    assert(fn_fill(7, 73, 82, 0, -360, 0) == true);
    assert(scene_is_dirty && fill_amap_calls == 1 && last_fill_amap_alpha == 0 && dirty_calls == 1);
    scene_is_dirty = false;

    // 3. Large sweeps (+/-720) and alpha clamping.
    assert(fn_fill(7, 73, 82, 0, -720, -50) == true);
    assert(fill_amap_calls == 2 && last_fill_amap_alpha == 0 && dirty_calls == 2);
    assert(fn_fill(7, 73, 82, 0, 360, 300) == true);
    assert(fill_amap_calls == 3 && last_fill_amap_alpha == 255 && dirty_calls == 3);

    // 4. Peace = 25% -> start=0, angle=-270: erases [90°, 360°], keeps [0°, 90°] (bottom red crescent).
    assert(fn_fill(7, 73, 82, 0, -270, 0) == true);
    assert(render_calls == 1 && dirty_calls == 4);
    assert(fabsf(u_start_angle - (float)(M_PI * 0.5)) < 1e-4f);
    assert(fabsf(u_angle - (float)(M_PI * 1.5)) < 1e-4f);
    assert(fabsf(u_color[3] - 0.0f) < 1e-6f);
    // 45 deg (bottom-right, +x +y): KEPT (not erased)
    assert(shader_fills_pixel(u_center[0] + 0.2f, u_center[1] + 0.2f) == false);
    // 135 deg (bottom-left, -x +y): ERASED
    assert(shader_fills_pixel(u_center[0] - 0.2f, u_center[1] + 0.2f) == true);
    // 270 deg (top, 0 -y): ERASED
    assert(shader_fills_pixel(u_center[0], u_center[1] - 0.2f) == true);

    // 5. Peace = 50% -> start=0, angle=-180: erases [180°, 360°] (top half), keeps [0°, 180°] (bottom half).
    assert(fn_fill(7, 73, 82, 0, -180, 0) == true);
    assert(render_calls == 2 && dirty_calls == 5);
    assert(fabsf(u_start_angle - (float)M_PI) < 1e-4f);
    assert(fabsf(u_angle - (float)M_PI) < 1e-4f);
    assert(shader_fills_pixel(u_center[0], u_center[1] + 0.2f) == false);
    assert(shader_fills_pixel(u_center[0], u_center[1] - 0.2f) == true);

    // 6. Positive sweep (start=90, angle=90, alpha=128) keeps clockwise sector [90°, 180°].
    assert(fn_fill(7, 73, 82, 90, 90, 128) == true);
    assert(render_calls == 3 && dirty_calls == 6);
    assert(fabsf(u_start_angle - (float)(M_PI * 0.5)) < 1e-4f);
    assert(fabsf(u_angle - (float)(M_PI * 0.5)) < 1e-4f);
    assert(fabsf(u_color[3] - (128.0f / 255.0f)) < 1e-4f);
    assert(shader_fills_pixel(u_center[0] - 0.2f, u_center[1] + 0.2f) == true);
    assert(shader_fills_pixel(u_center[0] + 0.2f, u_center[1] + 0.2f) == false);

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            [ROOT / "src/hll/FillAngle.c"],
            ["-lm"],
            pkgs=("sdl2", "cglm"),
        )
        self.assertEqual(out.strip(), "OK")

    def test_haruka_bgi_wai_confirm3_and_cp932_hacks(self):
        # 1. Model Haruka's exact 38-track nibble-swapped 5-field BGI (tracks 1..28, 30..32, 50..56)
        #    and XI2 v3 WAI covering all 6 VolumeValancer mixer channels (0..5).
        bgi_lines = [
            "Version=  1\r\n",
            "NumofLink  = 57NumofDWORD = 256\r\n",
            "DWORD{\r\n",
        ]
        haruka_tracks = list(range(1, 29)) + [30, 31, 32] + list(range(50, 57))
        assert len(haruka_tracks) == 38
        for no in haruka_tracks:
            loop_cnt = 1 if no in (19, 30) else 0
            bgi_lines.append(f"        {no}, {loop_cnt}, {no * 1000}, {no * 1000 + 50000}, -1\r\n")
        bgi_lines.append("}\r\n")
        bgi_bytes = encode_bgi("".join(bgi_lines))

        # Build minimal XI2 v3 WAI (36-byte header + 12-byte records for 12 waves across channels 0..5).
        nr_waves = 12
        wai_buf = bytearray(36 + nr_waves * 12)
        wai_buf[0:4] = b"XI2\x00"
        struct.pack_into("<III", wai_buf, 4, 1, nr_waves + 1, 3)
        for i in range(nr_waves):
            struct.pack_into("<III", wai_buf, 36 + i * 12, i + 1, 0, i % 6)

        harness = f"""
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "hll/hll.h"
#include "message_layout.h"
#include "mixer.h"
#include "xsystem4.h"

struct config config;

void apply_game_specific_hacks(struct ain *ain);

const char *display_utf0(const char *utf) {{ return utf; }}

bool message_layout_init_rance03(struct ain *ain) {{ (void)ain; abort(); }}

extern struct static_library lib_Confirm3;

static void *find_confirm3(const char *name) {{
    for (int i = 0; lib_Confirm3.functions[i].name; i++) {{
        if (!strcmp(lib_Confirm3.functions[i].name, name))
            return lib_Confirm3.functions[i].fun;
    }}
    return NULL;
}}

int main(int argc, char **argv) {{
    assert(argc == 3);

    // 1. Verify Haruka BGI (nibble-swapped 5-field -1 format, 38 tracks up to #56).
    bgi_read(argv[1]);
    struct bgi *t1 = bgi_get(1);
    assert(t1 && t1->loop_count == 0 && t1->loop_start == 1000 && t1->loop_end == 51000 && t1->channel == -1 && t1->volume == 100);
    struct bgi *t19 = bgi_get(19);
    assert(t19 && t19->loop_count == 1);
    struct bgi *t56 = bgi_get(56);
    assert(t56 && t56->loop_start == 56000 && t56->loop_end == 106000);
    assert(bgi_get(29) == NULL && bgi_get(57) == NULL);

    // 2. Verify Haruka WAI (XI2 v3 across VolumeValancer channels 0..5).
    wai_load(argv[2]);
    for (int i = 1; i <= 12; i++) {{
        struct wai *w = wai_get(i);
        assert(w && w->channel == (i - 1) % 6);
    }}
    assert(wai_get(0) == NULL && wai_get(13) == NULL);

    // 3. Verify Confirm3 DVD-protection bypass contract (CheckDVD_CheckProtectFile skips DM.DAT).
    int (*c3_get_drive)(struct string *) = find_confirm3("GetDrive");
    int (*c3_get_random)(int) = find_confirm3("GetRandom");
    bool (*c3_open)(struct string *) = find_confirm3("File_Open");
    bool (*c3_set_pos)(int) = find_confirm3("File_SetPos");
    bool (*c3_read_byte)(int *) = find_confirm3("File_ReadByte");
    bool (*c3_close)(void) = find_confirm3("File_Close");
    assert(c3_get_drive && c3_get_random && c3_open && c3_set_pos && c3_read_byte && c3_close);

    struct string *drv = make_string("C:\\\\", 3);
    struct string *dm = make_string("C:\\\\DM.DAT", 9);
    assert(c3_get_drive(drv) == 5); // DRIVE_CDROM
    assert(c3_open(dm) == true);
    int pos = c3_get_random(12345) % 100000000;
    assert(c3_set_pos(pos) == true);
    int byte_val = 0x77;
    assert(c3_read_byte(&byte_val) == true && byte_val == 0);
    assert(c3_close() == true);
    assert(((c3_get_random(pos * 1180) & 0xff00) >> 8) == byte_val);
    free_string(drv);
    free_string(dm);

    // 4. Verify hacks.c keeps Haruka in pure CP932 (non-GBK) 800x600 mode with zero game_* false positives.
    struct ain *ain = ain_new(5, 0);
    const char *haruka_libs[] = {{
        "DrawGraph", "DrawPluginManager", "Math", "MsgSkip", "SystemService",
        "OutputLog", "MusicSystem", "SACTDX", "InputString", "PlayMovie",
        "PassRegister", "AliceLogo3", "Confirm3", "FillAngle", "ACXLoader", "PixelRestore"
    }};
    for (size_t i = 0; i < sizeof(haruka_libs) / sizeof(haruka_libs[0]); i++)
        ain_add_library(ain, haruka_libs[i]);
    ain_add_string(ain, "\\x92\\xb4\\x8d\\x56\\x91\\x4d\\x94\\x45\\x83\\x6e\\x83\\x8b\\x83\\x4a" "DM.DAT");
    ain_add_string(ain, "Data\\\\CGMode.acx");
    ain_init_member_functions(ain, strdup);

    // CP932 bytes for "超昂閃忍ハルカ"
    config = (struct config){{
        .view_width = 800,
        .view_height = 600,
        .game_name = "\\x92\\xb4\\x8d\\x56\\x91\\x4d\\x94\\x45\\x83\\x6e\\x83\\x8b\\x83\\x4a"
    }};
    apply_game_specific_hacks(ain);
    assert(!sys4_text_is_gbk);
    assert(config.view_width == 800 && config.view_height == 600);
    assert(!game_galzoo && !game_sengokurance && !game_rance6 && !game_ixseal && !game_escalayer_reboot && !game_tsumashibori);

    char *utf_name = sjis2utf(config.game_name, 0);
    assert(utf_name && !strcmp(utf_name, "超昂閃忍ハルカ"));
    free(utf_name);
    ain_free(ain);

    puts("OK");
    return 0;
}}
"""
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            bgi_path = tmp / "haruka.bgi"
            wai_path = tmp / "haruka.wai"
            bgi_path.write_bytes(bgi_bytes)
            wai_path.write_bytes(bytes(wai_buf))
            out = compile_and_run(
                harness,
                [
                    *LIBSYS4_AIN,
                    ROOT / "src/audio_meta.c",
                    ROOT / "src/escalayer_reboot_cn_ui.c",
                    ROOT / "src/hacks.c",
                    ROOT / "src/hll/Confirm3.c",
                ],
                ["-lz", "-lm"],
                [str(bgi_path), str(wai_path)],
                pkgs=("sdl2", "cglm"),
            )
            self.assertEqual(out.strip(), "OK")

        # Verify DrawGraph.CopyAMap marks destination texture dirty via DTEX(dst).
        dg_src = (ROOT / "src/hll/DrawGraph.c").read_text(encoding="utf-8", errors="ignore")
        fn_copy_amap = extract_c_function(dg_src, "static void DrawGraph_CopyAMap(")
        self.assertIn("DTEX(dst)", fn_copy_amap)


if __name__ == "__main__":
    unittest.main()
