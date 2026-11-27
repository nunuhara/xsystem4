#!/usr/bin/env python3
"""Wave-2 host C regression tests for xsystem4 + libsys4 deep fixes.

Covers:
1. Audio mixer loop EOF deadlock fix, loop position clamping, and stream-pointer stop/volume.
2. Movie ffmpeg/plmpeg unconditional stream stop and ffmpeg sws resize/failure cleanup.
3. libsys4 CG/PMS/AJP/DCF/PNG hardening (including Adam7 interlaced PNG support).
4. libsys4 AAR/AFA/AFA3/DLF/AIN/FLAT + dungeon DTX/TES/PolyObj hardening.
5. 3D renderer normal mat3 extraction, FBO viewport restore, sphere index stride,
   particle string_buf init, parser guards, and GLSL ES 3.00 shader fixes.
6. effect_oscillate small-dst SIGFPE guard, draw.c zero-dim guards, DrawNumeral negative guard.
7. HLL AIN_REF_BOOL (int *) ABI fixes, 11 unprefixed FLAT exports, DrawField.BeginLoad*,
   SP_CreateCopy, and Math.AbsF AIN_INT overload.
8. iOS in-process relaunch session resets, sys4_text_is_gbk save/restore, and JNI OOM guards.
"""

from __future__ import annotations

import os
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LIBSYS4_INC = ROOT / "subprojects/libsys4/include"
LIBSYS4_SRC = ROOT / "subprojects/libsys4/src"

LIBSYS4_CORE = [
    LIBSYS4_SRC / "buffer.c",
    LIBSYS4_SRC / "file.c",
    LIBSYS4_SRC / "hashtable.c",
    LIBSYS4_SRC / "mt19937int.c",
    LIBSYS4_SRC / "string.c",
    LIBSYS4_SRC / "system.c",
    LIBSYS4_SRC / "utfsjis.c",
    LIBSYS4_SRC / "zlib.c",
]


def pkg_flags(*pkgs: str, mode: str = "--cflags") -> list[str]:
    return subprocess.check_output(["pkg-config", mode, *pkgs], text=True).split()


def compile_and_run(
    c_src: str,
    extra_sources: list[Path] | None = None,
    extra_flags: list[str] | None = None,
    run_args: list[str] | None = None,
    pkgs: tuple[str, ...] = ("sdl2",),
) -> str:
    cc = shutil.which("cc") or shutil.which("gcc")
    assert cc, "C compiler required"
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        src_path = tmp / "harness.c"
        bin_path = tmp / "harness"
        src_path.write_text(c_src, encoding="utf-8")
        cflags = pkg_flags(*pkgs, mode="--cflags") if pkgs else []
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
            f"-I{ROOT / 'include'}",
            f"-I{ROOT / 'src'}",
            f"-I{ROOT / 'src/hll'}",
            f"-I{LIBSYS4_INC}",
            *cflags,
            str(src_path),
            *[str(p) for p in (extra_sources or [])],
            *(extra_flags or []),
            "-o",
            str(bin_path),
        ]
        res_c = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res_c.returncode != 0:
            raise AssertionError(f"Compile failed ({res_c.returncode}):\n{res_c.stderr}")
        env = dict(os.environ)
        env["SDL_VIDEODRIVER"] = "offscreen"
        env["SDL_AUDIODRIVER"] = "dummy"
        res_r = subprocess.run(
            [str(bin_path), *(run_args or [])],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=env,
            timeout=15,
        )
        if res_r.returncode != 0:
            raise AssertionError(
                f"Run failed ({res_r.returncode}):\nstdout:\n{res_r.stdout}\nstderr:\n{res_r.stderr}"
            )
        return res_r.stdout


def make_interlaced_png(width: int, height: int, rgba_rows: list[bytes], with_alpha: bool = True) -> bytes:
    """Build a valid Adam7-interlaced PNG (color type 6 RGBA or 2 RGB)."""
    bpp = 4 if with_alpha else 3
    color_type = 6 if with_alpha else 2
    adam7 = [
        (0, 0, 8, 8),
        (4, 0, 8, 8),
        (0, 4, 4, 8),
        (2, 0, 4, 4),
        (0, 2, 2, 4),
        (1, 0, 2, 2),
        (0, 1, 1, 2),
    ]
    raw = bytearray()
    for x0, y0, dx, dy in adam7:
        if x0 >= width or y0 >= height:
            continue
        for y in range(y0, height, dy):
            raw.append(0)  # filter type None
            row = rgba_rows[y]
            for x in range(x0, width, dx):
                px = row[x * 4 : x * 4 + bpp]
                raw.extend(px)

    def chunk(ctype: bytes, data: bytes) -> bytes:
        crc = zlib.crc32(ctype + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + ctype + data + struct.pack(">I", crc)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 1)  # interlace = 1 (Adam7)
    idat = zlib.compress(bytes(raw))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")


def make_pcm_wav(num_frames: int = 64, sample_rate: int = 44100) -> bytes:
    num_channels = 2
    bits_per_sample = 16
    data_size = num_frames * num_channels * (bits_per_sample // 8)
    samples = b"\x00\x10" * (num_frames * num_channels)
    return (
        b"RIFF"
        + struct.pack("<I", 36 + data_size)
        + b"WAVE"
        + b"fmt "
        + struct.pack(
            "<IHHIIHH",
            16,
            1,
            num_channels,
            sample_rate,
            sample_rate * num_channels * 2,
            num_channels * 2,
            bits_per_sample,
        )
        + b"data"
        + struct.pack("<I", data_size)
        + samples
    )


class Wave2Sys4DeepRegressionsTest(unittest.TestCase):
    def test_audio_loop_eof_no_deadlock_clamp_and_stream_pointer_ops(self):
        wav_bytes = make_pcm_wav(64)
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "system4.h"
#include "system4/archive.h"
#include "system4/file.h"
#include "asset_manager.h"
#include "audio.h"
#include "mixer.h"
#include "xsystem4.h"

struct config config = {
    .mixer_nr_channels = 0,
    .mixer_channels = NULL,
    .mixer_volumes = NULL,
};

bool asset_exists(enum asset_type type, int no) { (void)type; (void)no; return false; }
struct archive_data *asset_get(enum asset_type type, int no) { (void)type; (void)no; return NULL; }
struct archive_data *asset_get_by_name(enum asset_type type, const char *name, int *no) {
    (void)type; (void)name; (void)no; return NULL;
}
struct bgi *bgi_get(int no) { (void)no; return NULL; }
struct wai *wai_get(int no) { (void)no; return NULL; }
void bgi_read(const char *path) { (void)path; }
void wai_load(const char *path) { (void)path; }
char *gamedir_path(const char *path) { return strdup(path); }
bool game_night_remaster, game_escalayer_reboot, game_tsumamigui3, game_evenicle2;
bool game_ixseal, game_rance03, game_okusama, game_haharanman;
bool game_tsumamigui3_jun, game_tsumamigui3_ntr, game_maman;
bool game_rance10, game_dohnadohna;

static void dummy_free_data(struct archive_data *data) {
    if (!data) return;
    free(data->data);
    free(data);
}
static struct archive_ops dummy_ops = { .free_data = dummy_free_data };
static struct archive dummy_ar = { .ops = &dummy_ops };

#include "audio_mixer.c"

static int dummy_stream_cb(sts_mixer_sample_t *sample, void *userdata) {
    (void)sample; (void)userdata;
    return STS_STREAM_CONTINUE;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    assert(SDL_Init(SDL_INIT_AUDIO) == 0);
    mixer_init();

    // 1. Load a 64-frame WAV and verify loop clamp + EOF at loop_start terminates cleanly.
    size_t sz = 0;
    uint8_t *wav = file_read(argv[1], &sz);
    assert(wav && sz > 44);

    struct archive_data *dfile = xcalloc(1, sizeof(*dfile));
    dfile->data = wav;
    dfile->size = sz;
    dfile->archive = &dummy_ar;

    struct channel *ch = channel_open_archive_data(dfile, ASSET_BGM, -1);
    assert(ch != NULL);
    assert(ch->info.frames == 64);

    // Clamp checks
    channel_set_loop_start_pos(ch, -100);
    assert(ch->loop_start == 0);
    channel_set_loop_start_pos(ch, 999999);
    assert(ch->loop_start == 64);

    channel_set_loop_end_pos(ch, -50);
    assert(ch->loop_end == 0);
    channel_set_loop_end_pos(ch, 999999);
    assert(ch->loop_end == 64);

    // Infinite loop with loop_start == frames (EOF) and frame == 64:
    // previously spun forever inside audio_lock()!
    ch->loop_count = 0;
    ch->loop_start = 64;
    ch->loop_end = 0;
    ch->frame = 64;
    sf_seek(ch->file, 64, SEEK_SET);
    float buf[128];
    uint_least32_t num_read = 0;
    int status = cb_read_frames(ch, buf, 64, &num_read);
    assert(status == STS_STREAM_COMPLETE);

    // Also test frame < loop_start hitting EOF with loop_start == frames:
    ch->frame = 60;
    sf_seek(ch->file, 60, SEEK_SET);
    num_read = 0;
    status = cb_read_frames(ch, buf, 64, &num_read);
    assert(status == STS_STREAM_COMPLETE);

    channel_close(ch);

    // 2. Test mixer_stream_stop_stream & mixer_stream_set_volume_stream match by exact stream pointer.
    sts_mixer_stream_t s1 = {0}, s2 = {0};
    float b1[64] = {0}, b2[64] = {0};
    s1.sample.frequency = 44100;
    s1.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_FLOAT;
    s1.sample.length = 32;
    s1.sample.data = b1;
    s1.callback = dummy_stream_cb;
    s2 = s1;
    s2.sample.data = b2;

    int v1 = mixer_stream_play(&s1, 100);
    int v2 = mixer_stream_play(&s2, 100);
    assert(v1 >= 0 && v2 >= 0 && v1 != v2);

    // Change volume of s1 only
    mixer_stream_set_volume_stream(&s1, 25);
    audio_lock();
    float g1 = master->mixer.voices[v1].gain;
    float g2 = master->mixer.voices[v2].gain;
    audio_unlock();
    assert(g1 < g2);

    // Stop s1 by pointer; s2 must remain active!
    mixer_stream_stop_stream(&s1);
    audio_lock();
    assert(master->mixer.voices[v1].state == STS_MIXER_VOICE_STOPPED);
    assert(master->mixer.voices[v2].state == STS_MIXER_VOICE_STREAMING);
    assert(master->mixer.voices[v2].stream == &s2);
    audio_unlock();

    // Repeated stop of already-stopped s1 is a safe no-op and must not affect s2!
    mixer_stream_stop_stream(&s1);
    audio_lock();
    assert(master->mixer.voices[v2].state == STS_MIXER_VOICE_STREAMING);
    audio_unlock();

    mixer_stream_stop_stream(&s2);
    SDL_Quit();
    puts("OK");
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as td:
            wav_path = Path(td) / "test.wav"
            wav_path.write_bytes(wav_bytes)
            out = compile_and_run(
                harness,
                extra_sources=[*LIBSYS4_CORE],
                extra_flags=[
                    *pkg_flags("sdl2", "sndfile", mode="--cflags"),
                    *pkg_flags("sdl2", "sndfile", mode="--libs"),
                    "-lz",
                    "-lm",
                    "-lpthread",
                ],
                run_args=[str(wav_path)],
                pkgs=(),
            )
            self.assertTrue(out.strip().endswith("OK"))

    def test_movie_plmpeg_and_ffmpeg_unconditional_stop_and_sws_resize(self):
        # Verify source-level invariants in movie_ffmpeg.c & movie_plmpeg.c
        ffmpeg_src = (ROOT / "src/movie_ffmpeg.c").read_text(encoding="utf-8")
        plmpeg_src = (ROOT / "src/movie_plmpeg.c").read_text(encoding="utf-8")
        self.assertIn("mixer_stream_stop_stream(&mc->sts_stream);", ffmpeg_src)
        self.assertIn("mixer_stream_set_volume_stream(&mc->sts_stream", ffmpeg_src)
        self.assertNotIn("mixer_stream_stop(mc->audio_voice)", ffmpeg_src)
        self.assertIn("texture->w != mc->sws_w || texture->h != mc->sws_h", ffmpeg_src)
        self.assertIn("mixer_stream_stop_stream(&mc->sts_stream);", plmpeg_src)
        self.assertIn("mixer_stream_set_volume_stream(&mc->sts_stream", plmpeg_src)
        self.assertNotIn("mixer_stream_stop(mc->audio_voice)", plmpeg_src)

        # Runtime C test against real movie_plmpeg.c: movie_free must stop stream unconditionally
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "gfx/gl.h"
#include "gfx/gfx.h"
#include "sts_mixer.h"
#include "mixer.h"
#include "movie.h"
#include "scene.h"
#include "sprite.h"
#include "xsystem4.h"

struct config config = { .view_width = 640, .view_height = 480 };
bool scene_is_dirty = false;

GL_APICALL void GL_APIENTRY glActiveTexture(GLenum t) { (void)t; }
GL_APICALL void GL_APIENTRY glBindTexture(GLenum t, GLuint id) { (void)t; (void)id; }
GL_APICALL void GL_APIENTRY glGenTextures(GLsizei n, GLuint *ids) { for (GLsizei i = 0; i < n; i++) ids[i] = (GLuint)(i + 1); }
GL_APICALL void GL_APIENTRY glDeleteTextures(GLsizei n, const GLuint *ids) { (void)n; (void)ids; }
GL_APICALL GLboolean GL_APIENTRY glIsTexture(GLuint id) { return id ? GL_TRUE : GL_FALSE; }
GL_APICALL void GL_APIENTRY glTexImage2D(GLenum t, GLint l, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void *p) {
    (void)t; (void)l; (void)ifmt; (void)w; (void)h; (void)b; (void)f; (void)ty; (void)p;
}
GL_APICALL void GL_APIENTRY glTexParameteri(GLenum t, GLenum p, GLint v) { (void)t; (void)p; (void)v; }
GL_APICALL void GL_APIENTRY glUseProgram(GLuint p) { (void)p; }
GL_APICALL GLint GL_APIENTRY glGetUniformLocation(GLuint p, const GLchar *n) { (void)p; (void)n; return 0; }
GL_APICALL void GL_APIENTRY glUniform1i(GLint l, GLint v) { (void)l; (void)v; }

static int g_stop_stream_calls = 0;
static sts_mixer_stream_t *g_last_stopped = NULL;
static int g_set_vol_stream_calls = 0;

int mixer_stream_play(sts_mixer_stream_t *stream, int volume) { (void)stream; (void)volume; return 0; }
void mixer_stream_stop(int voice) { (void)voice; assert(!"legacy mixer_stream_stop must not be called"); }
void mixer_stream_stop_stream(sts_mixer_stream_t *stream) {
    g_stop_stream_calls++;
    g_last_stopped = stream;
}
bool mixer_stream_set_volume(int voice, int volume) { (void)voice; (void)volume; assert(!"legacy set_volume"); return false; }
bool mixer_stream_set_volume_stream(sts_mixer_stream_t *stream, int volume) {
    (void)stream; (void)volume;
    g_set_vol_stream_calls++;
    return true;
}
void gfx_load_shader(struct shader *s, const char *v, const char *f) { (void)s; (void)v; (void)f; }
void gfx_register_reset_callback(void (*cb)(void *), void *u) { (void)cb; (void)u; }
void gfx_unregister_reset_callback(void (*cb)(void *), void *u) { (void)cb; (void)u; }
GLuint gfx_set_framebuffer(GLenum t, Texture *d, int x, int y, int w, int h) { (void)t; (void)d; (void)x; (void)y; (void)w; (void)h; return 0; }
void gfx_reset_framebuffer(GLenum t, GLuint fb) { (void)t; (void)fb; }
void gfx_render(struct gfx_render_job *j) { (void)j; }
void gfx_init_texture_blank(Texture *dst, int w, int h) { memset(dst, 0, sizeof(*dst)); dst->w = w; dst->h = h; }
void gfx_init_texture_rgb(Texture *dst, int w, int h, SDL_Color color) { (void)color; gfx_init_texture_blank(dst, w, h); }
void gfx_delete_texture(Texture *t) { if (t) memset(t, 0, sizeof(*t)); }
void gfx_update_texture_with_pixels(Texture *t, void *pixels) { (void)t; (void)pixels; }
void gfx_clear(void) {}
void gfx_swap(void) {}
Texture *gfx_main_surface(void) { static Texture t = {0}; return &t; }
Texture *sprite_get_texture(struct sact_sprite *sp) { return sp ? &sp->texture : NULL; }
void scene_register_sprite(struct sprite *sp) { (void)sp; }
void scene_unregister_sprite(struct sprite *sp) { (void)sp; }
char *gamedir_path_icase(const char *path) { (void)path; return NULL; }
bool kiwi_prepare_from_memory(int id, uint8_t *buf, size_t size, int group) { (void)id; (void)buf; (void)size; (void)group; return false; }
bool kiwi_play_sound(int id) { (void)id; return false; }
bool kiwi_stop_sound(int ch) { (void)ch; return false; }
bool kiwi_seek_sound(int ch, int ms) { (void)ch; (void)ms; return false; }
bool kiwi_unprepare_sound(int ch) { (void)ch; return false; }

#include "movie_plmpeg.c"

int main(void) {
    struct movie_context *mc = xcalloc(1, sizeof(*mc));
    mc->voice = -1; // Simulate EOF / voice already cleared
    mc->plm_playing = false;
    movie_set_volume(mc, 50);
    assert(g_set_vol_stream_calls == 1);
    movie_free(mc);
    assert(g_stop_stream_calls == 1);
    assert(g_last_stopped != NULL);
    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            extra_sources=[*LIBSYS4_CORE],
            extra_flags=[
                "-DUSE_GLES",
                *pkg_flags("sdl2", "cglm", mode="--cflags"),
                *pkg_flags("sdl2", mode="--libs"),
                "-lz",
                "-lm",
            ],
            pkgs=(),
        )
        self.assertEqual(out.strip(), "OK")

    def test_libsys4_cg_pms_ajp_dcf_and_adam7_interlaced_png(self):
        # Build a 9x7 Adam7-interlaced RGBA PNG and a 7x5 Adam7-interlaced RGB PNG
        w1, h1 = 9, 7
        rows1 = []
        for y in range(h1):
            row = bytearray()
            for x in range(w1):
                row.extend([(x * 29) & 0xFF, (y * 37) & 0xFF, ((x + y) * 17) & 0xFF, 128 + ((x * 7 + y) & 0x7F)])
            rows1.append(bytes(row))
        png_rgba = make_interlaced_png(w1, h1, rows1, with_alpha=True)

        w2, h2 = 7, 5
        rows2 = []
        for y in range(h2):
            row = bytearray()
            for x in range(w2):
                row.extend([(x * 31 + 3) & 0xFF, (y * 41 + 5) & 0xFF, ((x ^ y) * 23) & 0xFF, 255])
            rows2.append(bytes(row))
        png_rgb = make_interlaced_png(w2, h2, rows2, with_alpha=False)

        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/ajp.h"
#include "system4/cg.h"
#include "system4/dcf.h"
#include "system4/file.h"
#include "system4/little_endian.h"
#include "system4/pms.h"
#include "system4/png.h"

int main(int argc, char **argv) {
    assert(argc == 3);

    // 1. Truncated PMS header (< 32 bytes) rejected by cg_load_buffer & pms_checkfmt_sized.
    uint8_t short_pms[20] = { 'P', 'M', 1, 0, 44, 0, 8, 0 };
    assert(cg_load_buffer(short_pms, sizeof(short_pms)) == NULL);
    assert(!pms_checkfmt_sized(short_pms, sizeof(short_pms)));

    // 2. Malformed PMS16 with prev-line opcode 0xFE on y=0 (would underflow pic - scanline!).
    uint8_t bad_pms[64] = {0};
    bad_pms[0] = 'P'; bad_pms[1] = 'M';
    LittleEndian_putW(bad_pms, 2, 1);   // version
    LittleEndian_putW(bad_pms, 4, 44);  // header_size
    LittleEndian_putW(bad_pms, 6, 16);  // bpp = 16
    LittleEndian_putDW(bad_pms, 24, 4); // width
    LittleEndian_putDW(bad_pms, 28, 4); // height
    LittleEndian_putDW(bad_pms, 32, 44);// data_index
    LittleEndian_putDW(bad_pms, 36, 0); // alpha_index
    bad_pms[44] = 0xFE; // copy from previous scanline on y=0!
    struct cg cg = {0};
    pms_extract(bad_pms, sizeof(bad_pms), &cg);
    assert(cg.pixels == NULL);

    // Overflowing PMS dimensions (65535 x 65535) rejected cleanly.
    LittleEndian_putDW(bad_pms, 24, 65535);
    LittleEndian_putDW(bad_pms, 28, 65535);
    memset(&cg, 0, sizeof(cg));
    pms_extract(bad_pms, sizeof(bad_pms), &cg);
    assert(cg.pixels == NULL);

    // 3. DCF bad magic ("XXXX") must be rejected (fixed && -> || bug in dcf_get_qnt!).
    uint8_t bad_dcf[64];
    memset(bad_dcf, 'X', sizeof(bad_dcf));
    struct cg_metrics dcf_m = {0};
    dcf_get_metrics(bad_dcf, sizeof(bad_dcf), &dcf_m);
    assert(dcf_m.w == 0 && dcf_m.h == 0);

    // Truncated DCF with valid "dcf " magic and absurd dfdl_size must not crash.
    memset(bad_dcf, 0, sizeof(bad_dcf));
    memcpy(bad_dcf, "dcf ", 4);
    LittleEndian_putDW(bad_dcf, 4, 4); // dcfh_size
    memcpy(bad_dcf + 12, "dfdl", 4);
    LittleEndian_putDW(bad_dcf, 16, 2); // dfdl_size < 4!
    memset(&cg, 0, sizeof(cg));
    dcf_extract(bad_dcf, sizeof(bad_dcf), &cg, NULL);
    assert(cg.pixels == NULL);

    // 4. Adam7 interlaced RGBA PNG
    size_t sz1 = 0;
    uint8_t *d1 = file_read(argv[1], &sz1);
    assert(d1 && sz1 > 0);
    struct cg *c1 = cg_load_buffer(d1, sz1);
    free(d1);
    assert(c1 && c1->pixels && c1->metrics.w == 9 && c1->metrics.h == 7);
    for (int y = 0; y < 7; y++) {
        for (int x = 0; x < 9; x++) {
            uint8_t *p = (uint8_t *)c1->pixels + (y * 9 + x) * 4;
            assert(p[0] == ((x * 29) & 0xFF));
            assert(p[1] == ((y * 37) & 0xFF));
            assert(p[2] == (((x + y) * 17) & 0xFF));
            assert(p[3] == (128 + ((x * 7 + y) & 0x7F)));
        }
    }
    cg_free(c1);

    // 5. Adam7 interlaced RGB PNG (opaque alpha must be 0xFF)
    size_t sz2 = 0;
    uint8_t *d2 = file_read(argv[2], &sz2);
    assert(d2 && sz2 > 0);
    struct cg *c2 = cg_load_buffer(d2, sz2);
    free(d2);
    assert(c2 && c2->pixels && c2->metrics.w == 7 && c2->metrics.h == 5);
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 7; x++) {
            uint8_t *p = (uint8_t *)c2->pixels + (y * 7 + x) * 4;
            assert(p[0] == ((x * 31 + 3) & 0xFF));
            assert(p[1] == ((y * 41 + 5) & 0xFF));
            assert(p[2] == (((x ^ y) * 23) & 0xFF));
            assert(p[3] == 0xFF);
        }
    }
    cg_free(c2);

    puts("OK");
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as td:
            p1 = Path(td) / "adam7_rgba.png"
            p2 = Path(td) / "adam7_rgb.png"
            p1.write_bytes(png_rgba)
            p2.write_bytes(png_rgb)
            cg_sources = [
                *LIBSYS4_CORE,
                LIBSYS4_SRC / "archive.c",
                LIBSYS4_SRC / "cg.c",
                LIBSYS4_SRC / "ajp.c",
                LIBSYS4_SRC / "dcf.c",
                LIBSYS4_SRC / "jpeg.c",
                LIBSYS4_SRC / "pcf.c",
                LIBSYS4_SRC / "pms.c",
                LIBSYS4_SRC / "png.c",
                LIBSYS4_SRC / "qnt.c",
                LIBSYS4_SRC / "rou.c",
                LIBSYS4_SRC / "tga.c",
                LIBSYS4_SRC / "webp.c",
            ]
            out = compile_and_run(
                harness,
                extra_sources=cg_sources,
                extra_flags=[
                    *pkg_flags("libpng", "libwebp", "libturbojpeg", mode="--cflags"),
                    *pkg_flags("libpng", "libwebp", "libturbojpeg", mode="--libs"),
                    "-lz",
                    "-lm",
                    "-lpthread",
                ],
                run_args=[str(p1), str(p2)],
                pkgs=(),
            )
            self.assertTrue(out.strip().endswith("OK"))

    def test_libsys4_aar_dlf_ain_flat_afa3_and_dungeon_parsers(self):
        # Verify afa3.o exports no global `dict` or `rnd` symbols
        cc = shutil.which("cc") or shutil.which("gcc")
        with tempfile.TemporaryDirectory() as td:
            obj = Path(td) / "afa3.o"
            subprocess.run(
                [cc, "-c", f"-I{LIBSYS4_INC}", str(LIBSYS4_SRC / "afa3.c"), "-o", str(obj)],
                check=True,
            )
            nm_out = subprocess.check_output(["nm", "-g", "--defined-only", str(obj)], text=True)
            for line in nm_out.splitlines():
                sym = line.split()[-1]
                self.assertNotEqual(sym, "dict")
                self.assertNotEqual(sym, "rnd")

            # Build malformed AAR (first_entry_offset < 16)
            bad_aar1 = Path(td) / "bad_offset.aar"
            bad_aar1.write_bytes(b"AAR\x00" + struct.pack("<III", 0, 1, 8) + b"\x00" * 16)

            # Build AAR with symlink cycle (a -> b -> a)
            # Entry format v2: off(4), size(4), type(4), name\0, target\0 (XOR/sub 0x60)
            def enc_v2(s: bytes) -> bytes:
                return bytes(((b + 0x60) & 0xFF) for b in s) + b"\x00"

            e0 = struct.pack("<IIi", 0, 0, -1) + enc_v2(b"a") + enc_v2(b"b")
            e1 = struct.pack("<IIi", 0, 0, -1) + enc_v2(b"b") + enc_v2(b"a")
            first_off = 12 + len(e0) + len(e1)
            # Patch first entry off to first_off so header's first_entry_offset matches index end
            e0 = struct.pack("<IIi", first_off, 0, -1) + enc_v2(b"a") + enc_v2(b"b")
            cyc_aar = Path(td) / "symlink_cycle.aar"
            cyc_aar.write_bytes(b"AAR\x00" + struct.pack("<II", 2, 2) + e0 + e1)

            # Build DLF with out-of-bounds entry offset/size
            dlf_hdr = bytearray(b"DLF\x00\x00\x00\x00\x00")
            dlf_hdr.extend(struct.pack("<II", 99999, 512))  # entry 0 OOB
            dlf_hdr.extend(b"\x00" * (8 * (300 - 1)))
            bad_dlf = Path(td) / "oob.dlf"
            bad_dlf.write_bytes(bytes(dlf_hdr))

            # Build DTL with zero offset loop
            bad_dtl = Path(td) / "zero_off.dtl"
            bad_dtl.write_bytes(b"DTL\x00" + struct.pack("<III", 0, 0, 0) + b"\x00" * 16)

            # Build PolyObj with bad magic
            bad_po = Path(td) / "bad.lin"
            bad_po.write_bytes(b"NOPE" + b"\x00" * 32)

            harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/aar.h"
#include "system4/ain.h"
#include "system4/dlf.h"
#include "system4/flat.h"
#include "dungeon/dtx.h"
#include "dungeon/polyobj.h"
#include "dungeon/tes.h"

struct cg *cg_load_buffer(uint8_t *data, size_t size) { (void)data; (void)size; return NULL; }
void mtrand43_init(void *state, uint32_t seed) { (void)state; (void)seed; }
uint32_t mtrand43_genrand(void *state) { (void)state; return 0; }
char *display_utf0(const char *s) { return (char *)s; }

int main(int argc, char **argv) {
    assert(argc == 6);
    int err = 0;

    // 1. Malformed AAR with first_entry_offset < 16 rejected cleanly
    assert(aar_open(argv[1], 0, &err) == NULL);

    // 2. Symlink cycle in AAR terminates cleanly (depth cap <= 16)
    struct aar_archive *ar = aar_open(argv[2], 0, &err);
    assert(ar != NULL);
    struct archive_data *d = archive_get(&ar->ar, 0);
    assert(d == NULL);
    archive_free(&ar->ar);

    // 3. Out-of-bounds DLF entry rejected cleanly
    struct dlf_archive *dlf = dlf_open(argv[3], 0, &err);
    assert(dlf != NULL);
    assert(archive_get(&dlf->ar, 0) == NULL);
    archive_free(&dlf->ar);

    // 4. Dungeon DTX / TES / PolyObj bounds & bad magic
    uint8_t short_buf[8] = {0};
    assert(dtx_parse(short_buf, sizeof(short_buf)) == NULL);
    assert(tes_parse(short_buf, sizeof(short_buf)) == NULL);

    // DTX with huge rows*cols
    uint8_t huge_dtx[16] = {0};
    huge_dtx[0] = 'D'; huge_dtx[1] = 'T'; huge_dtx[2] = 'X';
    // version=0, rows=65536, cols=65536 -> overflows int32 / exceeds buffer
    huge_dtx[8] = 0; huge_dtx[9] = 0; huge_dtx[10] = 1; huge_dtx[11] = 0;
    huge_dtx[12] = 0; huge_dtx[13] = 0; huge_dtx[14] = 1; huge_dtx[15] = 0;
    assert(dtx_parse(huge_dtx, sizeof(huge_dtx)) == NULL);

    assert(dtx_load_from_dtl(argv[4], 1) == NULL);
    assert(polyobj_load(argv[5]) == NULL);

    // 5. AIN bad AI2 magic (byte 7 non-zero) rejected cleanly
    uint8_t bad_ai2[16] = { 'A', 'I', '2', 0, 0, 0, 0, 'X', 0, 0, 0, 0, 0, 0, 0, 0 };
    assert(ain_open_conv != NULL);
    FILE *fp = fopen("/tmp/wave2_bad_ai2.ain", "wb");
    assert(fp);
    fwrite(bad_ai2, 1, sizeof(bad_ai2), fp);
    fclose(fp);
    assert(ain_open("/tmp/wave2_bad_ai2.ain", &err) == NULL);
    remove("/tmp/wave2_bad_ai2.ain");

    puts("OK");
    return 0;
}
"""
            out = compile_and_run(
                harness,
                extra_sources=[
                    *LIBSYS4_CORE,
                    LIBSYS4_SRC / "aar.c",
                    LIBSYS4_SRC / "dlf.c",
                    LIBSYS4_SRC / "ain.c",
                    LIBSYS4_SRC / "instructions.c",
                    LIBSYS4_SRC / "flat.c",
                    ROOT / "src/dungeon/dtx.c",
                    ROOT / "src/dungeon/polyobj.c",
                    ROOT / "src/dungeon/tes.c",
                ],
                extra_flags=["-lz", "-lm"],
                run_args=[str(bad_aar1), str(cyc_aar), str(bad_dlf), str(bad_dtl), str(bad_po)],
                pkgs=("cglm",),
            )
            self.assertTrue(out.strip().endswith("OK"))

    def test_3d_fixes_and_gles3_shaders(self):
        # 1. Verify shaders have no uniform shadowing and cap linear_blur taps
        for name in ("vwave_crossfade.f.glsl", "vwave_scroll_crossfade.f.glsl"):
            src = (ROOT / "shaders/effects" / name).read_text(encoding="utf-8")
            self.assertNotRegex(src, r"\bvec3\s+old\b")
            self.assertIn("vec3 old_col", src)
            self.assertIn("vec3 new_col", src)

        blur_src = (ROOT / "shaders/effects/linear_blur.f.glsl").read_text(encoding="utf-8")
        self.assertIn("MAX_TAPS", blur_src)
        self.assertIn("step_px", blur_src)

        # 2. Verify 3d/renderer.c normal_transform mat3 extraction & glViewport restore
        rend3d = (ROOT / "src/3d/renderer.c").read_text(encoding="utf-8")
        self.assertGreaterEqual(rend3d.count("glm_mat4_pick3"), 4)
        self.assertNotIn("glUniformMatrix3fv(s->normal_transform, 1, GL_FALSE, normal_transform[0])", rend3d.replace("mat3 normal_transform;", ""))
        self.assertIn("glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);\n\tglViewport(0, 0, texture->w, texture->h);", rend3d.replace("\r\n", "\n"))

        # 3. Verify 3d/parser.c guards & 3d/model.c sphere index stride at runtime
        harness = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "3d_internal.h"

void mpr_free(struct mpr *mpr) { (void)mpr; }

int main(void) {
    uint8_t tiny[3] = { 0, 1, 2 };
    assert(pol_parse(tiny, 3) == NULL);
    assert(mot_parse(tiny, 3, "test") == NULL);
    assert(amt_parse(tiny, 3) == NULL);
    assert(pol_parse(NULL, 0) == NULL);
    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            extra_sources=[*LIBSYS4_CORE, LIBSYS4_SRC / "ex.c", ROOT / "src/3d/parser.c"],
            extra_flags=[f"-I{ROOT / 'src/3d'}", *pkg_flags("sdl2", "cglm", mode="--cflags"), "-lz", "-lm"],
            pkgs=(),
        )
        self.assertEqual(out.strip(), "OK")

        model_src = (ROOT / "src/3d/model.c").read_text(encoding="utf-8")
        self.assertIn("int row_stride = w_segments + 1;", model_src)
        particle_src = (ROOT / "src/3d/particle.c").read_text(encoding="utf-8")
        self.assertIn("string_buf[0] = '\\0';", particle_src)
        ft_src = (ROOT / "src/font_freetype.c").read_text(encoding="utf-8")
        self.assertIn("FT_Done_Face(font->font);", ft_src)

    def test_effect_oscillate_small_dst_and_draw_numeral_negative(self):
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "gfx/gfx.h"
#include "sprite.h"
#include "plugin.h"

// Stubs for DrawNumeral.c
static int g_blend_calls = 0;
static Texture g_main = { .handle = 1, .w = 320, .h = 240 };
Texture *gfx_main_surface(void) { return &g_main; }
void gfx_blend_amap_bright(Texture *dst, int dx, int dy, Texture *src, int sx, int sy, int w, int h, int b) {
    (void)dst; (void)dx; (void)dy; (void)sx; (void)sy; (void)w; (void)h; (void)b;
    assert(src != NULL);
    assert(src->w == 16 && src->h == 24);
    g_blend_calls++;
}
void gfx_delete_texture(Texture *t) { if (t) t->handle = 0; }
void gfx_init_texture_with_cg(Texture *dst, struct cg *cg) { (void)dst; (void)cg; }
struct cg *asset_cg_load(int no) { (void)no; return NULL; }
void cg_free(struct cg *cg) { (void)cg; }
struct sact_sprite *sact_try_get_sprite(int sp) { (void)sp; return NULL; }
struct sact_sprite *sact_get_sprite(int sp) { (void)sp; return NULL; }
void sprite_bind_plugin(struct sact_sprite *sp, struct draw_plugin *plugin) { (void)sp; (void)plugin; }
bool scene_is_dirty = false;

#include "hll/DrawNumeral.c"

int main(void) {
    // 1. DrawNumeral with negative value -42 must not index texture['-' - '0'] (-3)!
    struct cg_entry *entry = xcalloc(1, sizeof(*entry));
    for (int i = 0; i < 10; i++) {
        entry->texture[i].handle = (GLuint)(100 + i);
        entry->texture[i].w = 16;
        entry->texture[i].h = 24;
    }
    struct draw_numeral_plugin *plugin = xcalloc(1, sizeof(*plugin));
    plugin->p.name = plugin_name;
    plugin->p.free = DrawNumeral_free;
    plugin->p.render = DrawNumeral_render;
    plugin->cg_set = entry;
    strcpy(plugin->format, "%4d");
    plugin->value = -42;
    plugin->brightness = 255;

    struct sact_sprite sp = {0};
    sp.plugin = &plugin->p;
    DrawNumeral_render(&sp);
    // "-42" has 2 digits ('4' and '2'), '-' and leading space must be skipped safely.
    assert(g_blend_calls == 2);

    // 2. ModuleFini cleanup + UAF-safe DrawNumeral_free after ModuleFini
    cg_table = ht_create(16);
    ht_put_int(cg_table, 1, NULL)->value = entry;
    DrawNumeral_ModuleFini();
    assert(cg_table == NULL);
    // Calling plugin free AFTER ModuleFini must not UAF freed entry!
    DrawNumeral_free(&plugin->p);

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            extra_sources=[*LIBSYS4_CORE],
            extra_flags=[*pkg_flags("sdl2", "cglm", mode="--cflags"), "-lz", "-lm"],
            pkgs=(),
        )
        self.assertEqual(out.strip(), "OK")

        # Verify effect_oscillate guard and draw.c zero-dim guards in source
        eff_src = (ROOT / "src/effect.c").read_text(encoding="utf-8")
        self.assertIn("rx > 0 ? (int)((rand() % rx", eff_src)
        self.assertIn("ry > 0 ? (int)((rand() % ry", eff_src)

        draw_src = (ROOT / "src/draw.c").read_text(encoding="utf-8")
        self.assertGreaterEqual(draw_src.count("if (!dst || !src || w <= 0 || h <= 0)"), 7)
        self.assertIn("if (!dst || !src || dst->w <= 0 || dst->h <= 0)", draw_src)
        self.assertIn("if (!dst || dst->w <= 0 || dst->h <= 0)", draw_src)

    def test_hll_ref_bool_abi_flat_exports_drawfield_sp_copy_and_math_absf(self):
        # 1. Verify all 6 AIN_REF_BOOL functions use `int *` and write full 32-bit 0/1
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/cg.h"
#include "system4/string.h"
#include "hll.h"
#include "asset_manager.h"

// Stubs for CGManager.c
bool asset_manager_load_archive(enum asset_type type, const char *name) { (void)type; (void)name; return true; }
bool asset_exists_by_name(enum asset_type type, const char *name, int *no) { (void)type; (void)name; (void)no; return true; }
bool asset_cg_get_metrics_by_name(const char *name, struct cg_metrics *dst) {
    (void)name;
    memset(dst, 0, sizeof(*dst));
    dst->w = 64; dst->h = 48; dst->bpp = 32;
    dst->has_pixel = true; dst->has_alpha = false;
    return true;
}

#include "hll/CGManager.c"
#include "hll/Math.c"

int main(void) {
    // 1. CGManager_GetInfo must overwrite all 32 bits of AIN_REF_BOOL outputs!
    struct string *name = cstr_to_string("test");
    int w = 0, h = 0, bpp = 0;
    int exist_pixel = 0x12345600;
    int exist_alpha = 0x789ABC01;
    assert(CGManager_GetInfo(name, &w, &h, &bpp, &exist_pixel, &exist_alpha));
    assert(w == 64 && h == 48 && bpp == 32);
    assert(exist_pixel == 1);
    assert(exist_alpha == 0);
    free_string(name);

    // 2. Math.AbsF overload for AIN_INT return type (majonoshokuzai) AND AIN_FLOAT return type
    struct ain_hll_argument arg_float = { .type = { .data = AIN_FLOAT } };
    struct ain_hll_function fn_int = {
        .name = "AbsF",
        .return_type = { .data = AIN_INT },
        .nr_arguments = 1,
        .arguments = &arg_float,
    };
    int (*absf_int)(float) = (int (*)(float))Math_ResolveAbsF(&fn_int);
    assert(absf_int != NULL);
    assert(absf_int(-3.75f) == 3);
    assert(absf_int(5.2f) == 5);

    struct ain_hll_function fn_float = {
        .name = "AbsF",
        .return_type = { .data = AIN_FLOAT },
        .nr_arguments = 1,
        .arguments = &arg_float,
    };
    float (*absf_float)(float) = (float (*)(float))Math_ResolveAbsF(&fn_float);
    assert(absf_float != NULL);
    assert(absf_float(-3.75f) == 3.75f);

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            extra_sources=[*LIBSYS4_CORE],
            extra_flags=[*pkg_flags("sdl2", "cglm", mode="--cflags"), "-lz", "-lm"],
            pkgs=(),
        )
        self.assertEqual(out.strip(), "OK")

        # 2. Verify all 11 unprefixed FLAT exports in PartsEngine.c
        pe_src = (ROOT / "src/hll/PartsEngine.c").read_text(encoding="utf-8")
        for sym in (
            "SetPartsFlat",
            "SetPartsFlatAndStop",
            "StartPartsFlat",
            "StopPartsFlat",
            "IsPartsFlatEnd",
            "BackPartsFlatBeginFrame",
            "StepPartsFlatFinalFrame",
            "GoFramePartsFlat",
            "GetPartsFlatCurrentFrameNumber",
            "GetPartsFlatEndFrame",
            "ExistsFlatFile",
        ):
            self.assertIn(f"HLL_EXPORT({sym}, PE_{sym})", pe_src)

        # 3. Verify all 6 AIN_REF_BOOL functions switched to int *
        self.assertIn("int *top, int *bottom, int *left, int *right", pe_src)
        sact_src = (ROOT / "src/hll/SACT2.c").read_text(encoding="utf-8")
        self.assertIn("bool sact_Joypad_GetDigitalStickStatus(int num, int type, int *left, int *right, int *up, int *down)", sact_src)
        self.assertIn("HLL_EXPORT(SP_CreateCopy, sact_SP_CreateCopy)", sact_src)
        self.assertIn("HLL_EXPORT(SP_CreateCopy, sact_SP_CreateCopy)", (ROOT / "src/hll/ChipmunkSpriteEngine.c").read_text(encoding="utf-8"))
        self.assertIn("HLL_EXPORT(SP_CreateCopy, sact_SP_CreateCopy)", (ROOT / "src/hll/StoatSpriteEngine.c").read_text(encoding="utf-8"))
        p2_src = (ROOT / "src/hll/PastelChime2.c").read_text(encoding="utf-8")
        self.assertIn("int *open_out, int *close_out", p2_src)
        csd_src = (ROOT / "src/hll/CommonSystemData.c").read_text(encoding="utf-8")
        self.assertIn("CommonSystemData_GetDataBool(struct string *name, int *value)", csd_src)
        reign_src = (ROOT / "src/hll/ReignEngine.c").read_text(encoding="utf-8")
        self.assertIn("GetDrawDOF, int PluginNumber, int *DrawDOF", reign_src)

        # 4. Verify DrawField BeginLoad* exports
        dd_src = (ROOT / "src/hll/DrawDungeon.c").read_text(encoding="utf-8")
        for sym in (
            "BeginLoad",
            "IsLoadEnd",
            "IsLoadSucceeded",
            "BeginLoadTexture",
            "IsLoadTextureEnd",
            "IsLoadTextureSucceeded",
        ):
            self.assertIn(f"HLL_EXPORT({sym}, DrawField_{sym})", dd_src)

    def test_ios_relaunch_resets_gbk_save_restore_and_jni_oom_guards(self):
        # 1. Test sys4_text_is_gbk save/restore in modifier_api_read/write_local_save_* under MODIFIER_OFFLINE_ONLY
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "system4/utfsjis.h"
#include "modifier_api.h"

FILE *atomic_save_open(const char *path, char **tmp_path_out) {
    (void)path;
    if (tmp_path_out) *tmp_path_out = NULL;
    return NULL;
}
bool atomic_save_commit(FILE *f, const char *tmp_path, const char *path) {
    (void)f; (void)tmp_path; (void)path; return false;
}
void atomic_save_abort(FILE *f, const char *tmp_path) {
    (void)f; (void)tmp_path;
}

int main(void) {
    // Verify sys4_text_is_gbk is preserved across local save summary/write calls
    sys4_set_text_gbk(true);
    char *s = modifier_api_read_local_save_summary("/nonexistent_wave2_save.asd");
    free(s);
    assert(sys4_text_is_gbk == true);

    bool ok = modifier_api_write_local_save_mod("/nonexistent_wave2_save.asd", "{}");
    assert(!ok);
    assert(sys4_text_is_gbk == true);

    sys4_set_text_gbk(false);
    s = modifier_api_read_local_save_summary("/nonexistent_wave2_save.asd");
    free(s);
    assert(sys4_text_is_gbk == false);

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            extra_sources=[
                *LIBSYS4_CORE,
                LIBSYS4_SRC / "savefile.c",
                ROOT / "src/cJSON.c",
                ROOT / "src/modifier.c",
            ],
            extra_flags=["-DMODIFIER_OFFLINE_ONLY", "-lz", "-lm", "-lpthread"],
            pkgs=(),
        )
        self.assertEqual(out.strip(), "OK")

        # 2. Verify iOS teardown ordering in system4.c, vm.c, parts.c, input.c, modifier.c, HLL _ModuleFini
        sys4_src = (ROOT / "src/system4.c").read_text(encoding="utf-8")
        ios_main = sys4_src[sys4_src.index("int xsystem4_main"):]
        pos_setjmp = ios_main.index("if (setjmp(target) == 0)")
        pos_clear_exit = ios_main.index("sys_exit_handler = NULL;")
        pos_ini_abort = ios_main.index("ini_abort_parse();")
        pos_audio_end = ios_main.index("audio_end_session();")
        pos_vm_end = ios_main.index("vm_end_session();")
        pos_input_end = ios_main.index("input_end_session();")
        pos_gfx_fini = ios_main.index("gfx_fini();")
        pos_free_ain = ios_main.index("free(resolved_ain_path);")
        self.assertLess(pos_setjmp, pos_clear_exit)
        self.assertLess(pos_clear_exit, pos_ini_abort)
        self.assertLess(pos_ini_abort, pos_audio_end)
        self.assertLess(pos_audio_end, pos_vm_end)
        self.assertLess(pos_input_end, pos_gfx_fini)
        self.assertGreater(pos_free_ain, pos_gfx_fini)
        self.assertGreaterEqual(ios_main.count("set_utf8_release(false);"), 2)

        ini_src = (LIBSYS4_SRC / "ini.c").read_text(encoding="utf-8")
        ini_hdr = (LIBSYS4_INC / "system4/ini.h").read_text(encoding="utf-8")
        self.assertIn("void ini_abort_parse(void)", ini_src)
        self.assertNotIn("defined(__SWITCH__) || defined(ALICEHUB_IOS)", ini_src)
        self.assertIn("void ini_abort_parse(void);", ini_hdr)

        vm_src = (ROOT / "src/vm.c").read_text(encoding="utf-8")
        self.assertIn("page_end_session();", vm_src)
        self.assertIn("modifier_end_session();", vm_src)

        parts_src = (ROOT / "src/parts/parts.c").read_text(encoding="utf-8")
        end_fn = parts_src[parts_src.index("void parts_end_session(void)"):parts_src.index("static bool parts_has_dirty_parent")]
        self.assertIn("parts_activity_end_session();", end_fn)
        self.assertIn("parts_motion_end_session();", end_fn)
        self.assertIn("parts_scrollbar_end_session();", end_fn)
        # parts_multi_controller reset must be unconditional (after the if (parts_engine_initialized) block)
        self.assertGreater(end_fn.index("parts_multi_controller = false;"), end_fn.index("parts_engine_initialized = false;"))

        # 3. Verify all S2-6 HLL _ModuleFini hooks & icon_fini
        for rel, hook in (
            ("src/hll/DrawNumeral.c", "HLL_EXPORT(_ModuleFini, DrawNumeral_ModuleFini)"),
            ("src/hll/P3MapSprite.c", "HLL_EXPORT(_ModuleFini, P3MapSprite_Delete)"),
            ("src/hll/vmData.c", "HLL_EXPORT(_ModuleFini, vmData_ModuleFini)"),
            ("src/hll/Toushin3Loader.c", "HLL_EXPORT(_ModuleFini, Toushin3Loader_ModuleFini)"),
            ("src/hll/SystemService.c", "input_reset_close_hook();"),
            ("src/hll/ChrLoader.c", "HLL_EXPORT(_ModuleFini, ChrLoader_ModuleFini)"),
            ("src/hll/MsgLogManager.c", "HLL_EXPORT(_ModuleFini, MsgLogManager_ModuleFini)"),
            ("src/hll/MsgLogViewer.c", "HLL_EXPORT(_ModuleFini, MsgLogViewer_Clear)"),
            ("src/hll/ACXLoader.c", "HLL_EXPORT(_ModuleFini, ACXLoader_Release)"),
            ("src/hll/DALKDemo.c", "HLL_EXPORT(_ModuleFini, DALKDemo_Release)"),
        ):
            self.assertIn(hook, (ROOT / rel).read_text(encoding="utf-8"), f"Missing {hook} in {rel}")
        self.assertEqual((ROOT / "src/hll/ACXLoader.c").read_text(encoding="utf-8").count("HLL_EXPORT(_ModuleFini, ACXLoader_Release)"), 2)
        self.assertIn("icon_fini();", (ROOT / "src/video.c").read_text(encoding="utf-8"))

        # 4. Verify modifier.c JNI OOM guards and modifier_api_* routing
        mod_src = (ROOT / "src/modifier.c").read_text(encoding="utf-8")
        jni_sec = mod_src[mod_src.index("nativeReadLocalSaveSummary"):]
        self.assertIn("modifier_api_read_local_save_summary(path)", jni_sec)
        self.assertIn("modifier_api_write_local_save_mod(path, mod)", jni_sec)
        self.assertIn("modifier_api_read_global_save_summary(gp, cp)", jni_sec)
        self.assertIn("modifier_api_write_global_save_mod(gp, cp, mod)", jni_sec)
        # No direct read_global_save_summary_str / write_global_save_mod_str from JNI
        self.assertNotIn("read_global_save_summary_str", jni_sec)
        self.assertNotIn("write_global_save_mod_str", jni_sec)
        self.assertNotIn("read_local_save_summary_str", jni_sec)
        self.assertNotIn("write_local_save_mod_str", jni_sec)

        # 5. Verify modifier_api_current_game unconditionally checks vm_is_ready()
        cur_game_fn = mod_src[mod_src.index("const char *modifier_api_current_game(void)"):mod_src.index("char *modifier_api_get_live_state_json")]
        self.assertIn("if (!vm_is_ready())", cur_game_fn)
        self.assertNotIn("#ifdef __SWITCH__", cur_game_fn)

    def test_chr_loader_owned_audio_blob_ini_abort_parse_and_dialog(self):
        # 1. ChrLoader audio blob must own a copied buffer that survives chr_loader_init
        #    cache replacement and ChrLoader_ModuleFini teardown.
        wav_bytes = make_pcm_wav(32)
        chr_harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "system4.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/little_endian.h"
#include "system4/mt19937int.h"
#include "system4/string.h"
#include "audio.h"
#include "sact.h"
#include "xsystem4.h"

static const char *g_gamedir = NULL;
static struct archive_data *g_prepared_dfile = NULL;

char *gamedir_path(const char *path) { return path_join(g_gamedir, path); }
struct sact_sprite *sact_get_sprite(int sp) { (void)sp; return NULL; }
void sprite_set_cg(struct sact_sprite *sp, struct cg *cg) { (void)sp; (void)cg; }
struct cg *cg_load_buffer(uint8_t *data, size_t size) { (void)data; (void)size; return NULL; }
void cg_free(struct cg *cg) { (void)cg; }
int wav_prepare_from_archive_data(int id, int metadata_no, struct archive_data *dfile) {
    (void)id; (void)metadata_no;
    g_prepared_dfile = dfile;
    return dfile != NULL;
}

#include "hll/ChrLoader.c"

int main(int argc, char **argv) {
    assert(argc == 3);
    g_gamedir = argv[1];
    size_t wav_sz = 0;
    uint8_t *wav_orig = file_read(argv[2], &wav_sz);
    assert(wav_orig && wav_sz > 44);

    // Build encrypted Enmy .chr file at <gamedir>/CHR/1.chr
    char *chr_dir = path_join(g_gamedir, "CHR");
    assert(mkdir(chr_dir, 0755) == 0);
    size_t snd_off = 164;
    size_t chr_sz = snd_off + wav_sz;
    uint8_t *chr_raw = xcalloc(1, chr_sz);
    memcpy(chr_raw, "CHR\0Enmy", 8);
    LittleEndian_putDW(chr_raw, 8, 44);
    LittleEndian_putDW(chr_raw, 36, (uint32_t)snd_off);
    LittleEndian_putDW(chr_raw, 40, (uint32_t)wav_sz);
    memcpy(chr_raw + snd_off, wav_orig, wav_sz);
    mt19937_xorcode(chr_raw + 4, chr_sz - 4, 12753);

    char *chr_path = path_join(chr_dir, "1.chr");
    FILE *fp = fopen(chr_path, "wb");
    assert(fp);
    assert(fwrite(chr_raw, 1, chr_sz, fp) == chr_sz);
    fclose(fp);
    free(chr_raw);
    free(chr_path);
    free(chr_dir);

    chr_loader_init("CHR");
    int handle = chr_loader_get_blob_handle(1, 4);
    assert(handle != 0);
    size_t raw_sz = 0;
    uint8_t *raw_blob = chr_loader_get_blob(handle, &raw_sz);
    assert(raw_blob && raw_sz == wav_sz);

    struct archive_data *d1 = chr_loader_get_blob_as_archive_data(handle);
    struct archive_data *d2 = chr_loader_get_blob_as_archive_data(handle);
    assert(d1 && d2 && d1->data && d2->data);
    assert(d1->size == wav_sz && d2->size == wav_sz);
    assert(d1->data != raw_blob && d2->data != raw_blob && d1->data != d2->data);
    assert(memcmp(d1->data, wav_orig, wav_sz) == 0);

    // Replace cache via chr_loader_init -> frees old chr_table buffers; d1/d2 must survive!
    chr_loader_init("CHR");
    assert(memcmp(d1->data, wav_orig, wav_sz) == 0);
    assert(memcmp(d2->data, wav_orig, wav_sz) == 0);

    // Also test ChrLoader_LoadWavFile handoff surviving ChrLoader_ModuleFini
    assert(ChrLoader_LoadWavFile(0, 1));
    assert(g_prepared_dfile != NULL && g_prepared_dfile->size == wav_sz);
    ChrLoader_ModuleFini();
    assert(memcmp(d1->data, wav_orig, wav_sz) == 0);
    assert(memcmp(d2->data, wav_orig, wav_sz) == 0);
    assert(memcmp(g_prepared_dfile->data, wav_orig, wav_sz) == 0);

    archive_free_data(d1);
    archive_free_data(d2);
    archive_free_data(g_prepared_dfile);
    free(wav_orig);
    puts("OK");
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as td:
            gd = Path(td) / "game"
            gd.mkdir()
            wav_path = Path(td) / "voice.wav"
            wav_path.write_bytes(wav_bytes)
            out = compile_and_run(
                chr_harness,
                extra_sources=[*LIBSYS4_CORE],
                extra_flags=["-lz", "-lm"],
                run_args=[str(gd), str(wav_path)],
                pkgs=("sdl2", "cglm"),
            )
            self.assertEqual(out.strip(), "OK")

        # 2. ini_abort_parse must be compiled & exported unconditionally without ALICEHUB_IOS or __SWITCH__
        cc = shutil.which("cc") or shutil.which("gcc")
        flex = shutil.which("flex")
        bison = shutil.which("bison")
        assert cc and flex and bison
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            tab_c = tmp / "ini_parser.tab.c"
            tab_h = tmp / "ini_parser.tab.h"
            lex_c = tmp / "ini_lexer.yy.c"
            ini_o = tmp / "ini.o"
            subprocess.run(
                [bison, str(LIBSYS4_SRC / "ini_parser.y"), f"--defines={tab_h}", f"--output={tab_c}"],
                check=True,
            )
            subprocess.run(
                [flex, "-o", str(lex_c), str(LIBSYS4_SRC / "ini_lexer.l")],
                check=True,
            )
            # Compile ini.c WITHOUT -DALICEHUB_IOS or -D__SWITCH__
            subprocess.run(
                [
                    cc, "-std=gnu11", "-O2", "-Wall", "-Wextra",
                    "-D_DEFAULT_SOURCE",
                    f"-I{LIBSYS4_INC}", f"-I{LIBSYS4_SRC}", f"-I{tmp}",
                    "-c", str(LIBSYS4_SRC / "ini.c"), "-o", str(ini_o),
                ],
                check=True,
            )
            nm_out = subprocess.check_output(["nm", "-g", "--defined-only", str(ini_o)], text=True)
            syms = {line.split()[-1] for line in nm_out.splitlines() if line.strip()}
            self.assertTrue("ini_abort_parse" in syms or "_ini_abort_parse" in syms, f"Missing ini_abort_parse in {syms}")

            ini_harness = r"""
#include <assert.h>
#include <stdio.h>
#include "system4/ini.h"

int main(void) {
    ini_abort_parse();
    puts("OK");
    return 0;
}
"""
            out = compile_and_run(
                ini_harness,
                extra_sources=[*LIBSYS4_CORE, tab_c, lex_c],
                extra_flags=[f"-I{LIBSYS4_SRC}", f"-I{tmp}", str(ini_o), "-lz", "-lm"],
                pkgs=(),
            )
            self.assertEqual(out.strip(), "OK")

        # 3. Dialog_InputMsg preserves default / initializes NULL / truncates safely and returns 1;
        #    Dialog_InputNum and Dialog_MenuMsg clamp safely and return 0.
        dlg_harness = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>

#include "system4.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "hll.h"

#include "hll/vmDialog.c"

int main(void) {
    sys4_set_text_gbk(false);

    // NULL *msg is initialized to empty string and returns 1 (never freezes dungeonsdolls)
    struct string *s = NULL;
    assert(Dialog_InputMsg(NULL, 8, &s) == 1);
    assert(s != NULL && s->size == 0 && strcmp(s->text, "") == 0);
    free_string(s);

    // CoW default "マスター" (8 SJIS bytes) with max_len=8 stays intact
    const char sjis_master[] = "\x83\x7d\x83\x58\x83\x5e\x81\x5b";
    struct string *lit = make_string(sjis_master, 8);
    lit->cow = true;
    struct string *name = string_ref(lit);
    assert(Dialog_InputMsg(NULL, 8, &name) == 1);
    assert(name->size == 8 && memcmp(name->text, sjis_master, 8) == 0);

    // Odd max_len=5 truncates cleanly at 4-byte SJIS boundary without mutating CoW literal
    assert(Dialog_InputMsg(NULL, 5, &name) == 1);
    assert(name != lit);
    assert(name->size == 4 && memcmp(name->text, sjis_master, 4) == 0);
    assert(lit->size == 8 && memcmp(lit->text, sjis_master, 8) == 0);
    free_string(name);
    free_string(lit);

    // Dialog_InputNum clamps into [min, max] and returns 0
    int v = 0;
    assert(Dialog_InputNum(NULL, 1, 50, &v) == 0);
    assert(v == 1);
    v = 999;
    assert(Dialog_InputNum(NULL, 1, 50, &v) == 0);
    assert(v == 50);
    assert(Dialog_InputNum(NULL, 1, 50, NULL) == 0);

    // Dialog_MenuMsg clamps negative selection and returns 0
    int sel = -5;
    assert(Dialog_MenuMsg(NULL, NULL, &sel) == 0);
    assert(sel == 0);
    assert(Dialog_MenuMsg(NULL, NULL, NULL) == 0);

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            dlg_harness,
            extra_sources=[*LIBSYS4_CORE],
            extra_flags=[*pkg_flags("sdl2", mode="--cflags"), *pkg_flags("sdl2", mode="--libs"), "-lz", "-lm"],
            pkgs=(),
        )
        self.assertEqual(out.strip(), "OK")


if __name__ == "__main__":
    unittest.main()
