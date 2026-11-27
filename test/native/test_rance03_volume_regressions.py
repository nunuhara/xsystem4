#!/usr/bin/env python3
"""Host C regression tests for Rance 03 volume adjustment and multi-timemap sound lookup."""

import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def _ex_encode_tables() -> bytes:
    inv = [0] * 256
    for i in range(256):
        t = i
        bc = bin(i).count("1")
        if bc % 2 == 0:
            t = ((t << (8 - bc)) | (t >> bc)) & 0xFF
        else:
            t = ((t >> (8 - bc)) | (t << bc)) & 0xFF
        inv[t] = i
    return bytes(inv)


_EX_ENC = _ex_encode_tables()


def _ex_pstr(b: bytes) -> bytes:
    return struct.pack("<i", len(b)) + b


def _ex_block(name: bytes, val_type: int, payload: bytes) -> bytes:
    body = _ex_pstr(name) + payload
    return struct.pack("<II", val_type, len(body)) + body


def _ex_soundinfo_table(rows: list[tuple[bytes, ...]], col_types: list[tuple[bytes, int]]) -> bytes:
    out = bytearray()
    out.extend(struct.pack("<I", len(col_types)))
    for idx, (col_name, col_type) in enumerate(col_types):
        out.extend(struct.pack("<I", col_type))
        out.extend(_ex_pstr(col_name))
        out.extend(struct.pack("<II", 0, 1 if idx == 0 else 0))
    out.extend(struct.pack("<II", len(col_types), len(rows)))
    for row in rows:
        assert len(row) == len(col_types)
        for val, (_, col_type) in zip(row, col_types):
            out.extend(struct.pack("<I", col_type))
            if col_type == 3:  # EX_STRING
                out.extend(_ex_pstr(val))
            elif col_type == 1:  # EX_INT
                out.extend(struct.pack("<i", val))
            else:
                raise ValueError(f"Unsupported column type: {col_type}")
    return _ex_block(b"SoundInfo", 4, bytes(out))


def build_extf(blocks: list[bytes]) -> bytes:
    raw = b"".join(blocks)
    comp = zlib.compress(raw)
    enc = bytes(_EX_ENC[b] for b in comp)
    return (
        b"HEAD\x00\x00\x00\x00"
        b"EXTF\x00\x00\x00\x00"
        + struct.pack("<I", len(blocks))
        + b"DATA"
        + struct.pack("<II", len(comp), len(raw))
        + enc
    )


def sdl_cflags() -> list[str]:
    return subprocess.check_output(["pkg-config", "--cflags", "sdl2", "sndfile"], text=True).split()


def compile_and_run(
    c_src: str,
    extra_sources: list[Path],
    extra_flags: list[str] | None = None,
    args: list[str] | None = None,
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
            "-Werror=implicit-function-declaration",
            "-Wno-unused-parameter",
            "-Wno-unused-function",
            "-Wno-sign-compare",
            "-Wno-type-limits",
            "-D_DEFAULT_SOURCE",
            "-DUSE_GLES",
            f"-I{ROOT / 'include'}",
            f"-I{ROOT / 'src'}",
            f"-I{ROOT / 'src' / 'hll'}",
            f"-I{ROOT / 'subprojects' / 'libsys4' / 'include'}",
            *sdl_cflags(),
            str(src_path),
            *[str(p) for p in extra_sources],
            *(extra_flags or []),
            *(["-framework", "CoreFoundation"] if sys.platform == "darwin" else []),
            "-o",
            str(bin_path),
        ]
        res_c = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res_c.returncode != 0:
            raise AssertionError(f"Compile failed ({res_c.returncode}):\n{res_c.stderr}")
        res_r = subprocess.run(
            [str(bin_path), *(args or [])],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if res_r.returncode != 0:
            raise AssertionError(
                f"Run failed ({res_r.returncode}):\nstdout:\n{res_r.stdout}\nstderr:\n{res_r.stderr}"
            )
        return res_r.stdout


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


class TestRance03VolumeRegressions(unittest.TestCase):
    def test_rance03_8_mixers_defaults_hierarchy_and_roundtrip(self):
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sndfile.h>
#include <SDL.h>

#include "xsystem4.h"
#include "asset_manager.h"
#include "audio.h"
#include "id_pool.h"
#include "mixer.h"
#include "system4/archive.h"
#include "system4/string.h"
#include "hll/hll.h"

struct config config;
bool game_rance10 = false;
bool game_dohnadohna = false;
bool game_maman = false;
bool game_evenicle2 = false;
bool game_rance03 = true;
bool game_ixseal = false;
bool game_okusama = false;
bool game_haharanman = false;
bool game_night_remaster = false;
bool game_escalayer_reboot, game_tsumamigui3;
bool game_tsumamigui3_jun = false;
bool game_tsumamigui3_ntr = false;

int SDL_InitSubSystem(Uint32 flags) { return 0; }
static int open_calls, close_calls, pause_state = -1;
static bool fail_open;
SDL_AudioDeviceID SDL_OpenAudioDevice(const char *dev, int iscap, const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int allowed) {
    open_calls++;
    if (obtained) *obtained = *desired;
    return fail_open ? 0 : 2;
}
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on) { pause_state = pause_on; }
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev) { close_calls++; }
#ifdef ALICEHUB_IOS
const char *SDL_GetCurrentAudioDriver(void) { return "test-coreaudio"; }
static Uint64 test_ticks;
Uint64 SDL_GetTicks64(void) { return test_ticks; }
SDL_mutex *SDL_CreateMutex(void) { return (SDL_mutex*)1; }
int SDL_LockMutex(SDL_mutex *m) { return 0; }
int SDL_UnlockMutex(SDL_mutex *m) { return 0; }
SDL_Thread *SDL_CreateThread(SDL_ThreadFunction f, const char *n, void *d) { return (SDL_Thread*)1; }
static int joined_threads, quit_calls;
void SDL_WaitThread(SDL_Thread *t, int *status) { assert(t); joined_threads++; }
void SDL_Delay(Uint32 ms) { test_ticks += ms; }
void SDL_DestroyMutex(SDL_mutex *m) {}
void SDL_Quit(void) { quit_calls++; }
void audio_metadata_reset(void) {}
#endif
void SDL_LockAudioDevice(SDL_AudioDeviceID dev) {}
void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev) {}
const char *SDL_GetError(void) { return ""; }
Uint32 SDL_GetTicks(void) { return 0; }

SNDFILE *sf_open(const char *p, int m, SF_INFO *i) { return NULL; }
SNDFILE *sf_open_virtual(SF_VIRTUAL_IO *v, int m, SF_INFO *i, void *u) { return NULL; }
int sf_close(SNDFILE *s) { return 0; }
int sf_error(SNDFILE *s) { return 0; }
const char *sf_strerror(SNDFILE *s) { return ""; }
sf_count_t sf_seek(SNDFILE *s, sf_count_t f, int w) { return 0; }
static sf_count_t remaining_frames;
sf_count_t sf_readf_float(SNDFILE *s, float *p, sf_count_t f) {
    sf_count_t n = remaining_frames < f ? remaining_frames : f;
    memset(p, 0, n * 2 * sizeof(float));
    remaining_frames -= n;
    return n;
}

bool asset_exists_by_name(enum asset_type t, const char *n, int *id) { return false; }
struct archive_data *asset_get(enum asset_type t, int no) { return NULL; }
struct archive_data *asset_get_by_name(enum asset_type t, const char *n, int *id_out) { return NULL; }
void asset_foreach_file(enum asset_type t, void (*cb)(struct archive_data*, void*), void *u) {}
void bgi_read(const char *path) {}
struct bgi *bgi_get(int no) { return NULL; }
void wai_load(const char *path) {}
struct wai *wai_get(int no) { return NULL; }
const char *display_sjis0(const char *s) { return s ? s : ""; }
char *gamedir_path_icase(const char *p) { return NULL; }
void audio_init(void) {}
void id_pool_init(struct id_pool *p, int b) {}
void id_pool_delete(struct id_pool *p) {}
int id_pool_get_first(struct id_pool *p) { return -1; }
int id_pool_get_next(struct id_pool *p, int i) { return -1; }
void *id_pool_get(struct id_pool *p, int i) { return NULL; }
void *id_pool_set(struct id_pool *p, int i, void *d) { return NULL; }
void *id_pool_release(struct id_pool *p, int i) { return NULL; }
void static_library_replace(struct static_library *l, const char *n, void *f) {}

#include "audio_mixer.c"
#include "hll/KiwiSoundEngine_v12.c"

int main(void) {
#ifdef ALICEHUB_IOS
    mixer_retry_audio_device(); assert(open_calls == 0);
#endif
    static const char *const expected_names[8] = {
        "Master", "BGM", "SE", "Voice", "Ambient", "BackVoice", "VoiceMale", "VoiceFemale"
    };
    static const int expected_defaults[8] = { 80, 80, 80, 100, 80, 80, 80, 80 };

    memset(&config, 0, sizeof(config));
    config.default_volume = 100;
    game_rance03 = true;

    kiwi_prelink();
    assert(config.mixer_nr_channels == 8);
    for (int i = 0; i < 8; i++) {
        assert(strcmp(config.mixer_channels[i], expected_names[i]) == 0);
        assert(config.mixer_volumes[i] == expected_defaults[i]);
        int def_vol = -1;
        assert(kiwi_mixer_default(i, &def_vol) && def_vol == expected_defaults[i]);
    }
    int dummy = -1;
    assert(!kiwi_mixer_default(-1, &dummy));
    assert(!kiwi_mixer_default(8, &dummy));

    mixer_init();
    assert(mixer_get_numof() == 8);
    assert(nr_mixers == 8);
    assert(master == &mixers[0]);

    // Rance 03 CGroupTree::GetParentGroup hierarchy:
    // groups 1..5 -> Master (0); groups 6..7 -> Voice (3).
    assert(mixers[0].parent == NULL);
    assert(mixers[0].nr_children == 5);
    for (int i = 1; i <= 5; i++) {
        assert(mixers[i].parent == &mixers[0]);
    }
    assert(mixers[3].nr_children == 2);
    assert(mixers[6].parent == &mixers[3]);
    assert(mixers[7].parent == &mixers[3]);
    assert(mixers[1].nr_children == 0);
    assert(mixers[2].nr_children == 0);
    assert(mixers[4].nr_children == 0);
    assert(mixers[5].nr_children == 0);
    assert(mixers[6].nr_children == 0);
    assert(mixers[7].nr_children == 0);

#ifdef ALICEHUB_IOS
    assert(open_calls == 0 && audio_device == 0);
    fail_open = true;
    service_audio_device(); assert(open_calls == 1 && audio_device == 0);
    mixer_retry_audio_device(); assert(open_calls == 1);
    service_audio_device(); assert(open_calls == 2 && audio_device == 0);

    struct channel sound = { .file = (SNDFILE*)1,
        .info = { .samplerate = 44100, .channels = 2, .frames = 4410 } };
    assert(init_channel(&sound));
    remaining_frames = 4410;
    assert(channel_play(&sound) && channel_is_playing(&sound));
    mixer_set_app_backgrounded(true);
    advance_audio_clock(test_ticks = 500);
    assert(sound.frame == 0 && channel_is_playing(&sound));
    mixer_set_app_backgrounded(false);
    advance_audio_clock(test_ticks = 750);
    assert(sound.frame == 4410 && !channel_is_playing(&sound));

    // Empty streams also finish without an audio device.
    assert(channel_play(&sound));
    advance_audio_clock(test_ticks = 1000);
    assert(!channel_is_playing(&sound));

    // A device that opens but stops calling back must not freeze fades or EOF.
    fail_open = false;
    mixer_retry_audio_device(); service_audio_device();
    assert(audio_device == 2 && pause_state == 0);
    float pcm[CHUNK_SIZE * 2];
    audio_callback(NULL, (Uint8*)pcm, sizeof(pcm));
    advance_audio_clock(test_ticks = 1050);
    assert(clock_tick == 1050);
    remaining_frames = 44100;
    channel_play(&sound);
    assert(channel_fade(&sound, 150, 23, false));
    assert(channel_is_fading(&sound));
    advance_audio_clock(test_ticks = 1250);
    assert(!channel_is_fading(&sound) && sound.volume == 23);
    channel_fade(&sound, 150, 0, true);
    advance_audio_clock(test_ticks = 1500);
    assert(!channel_is_playing(&sound) && !channel_is_fading(&sound));

    mixer_set_global_focus(false); mixer_set_window_focus(false);
    remaining_frames = 4410;
    channel_play(&sound);
    uint_least32_t frame = sound.frame;
    advance_audio_clock(test_ticks = 1750);
    assert(sound.frame == frame);
    mixer_set_window_focus(true);
    advance_audio_clock(test_ticks = 2000);
    assert(!channel_is_playing(&sound));

    // Late callbacks and a stale watchdog timestamp never double-advance.
    audio_callback(NULL, (Uint8*)pcm, sizeof(pcm));
    advance_audio_clock(1999);
    assert(clock_tick == 2000);
    mixer_retry_audio_device(); service_audio_device();
    assert(open_calls == 3 && close_calls == 0);
    mixer_handle_device_removed(99); service_audio_device();
    assert(close_calls == 0);
    mixer_set_app_backgrounded(true);
    mixer_handle_device_removed(2); assert(close_calls == 0);
    service_audio_device(); assert(close_calls == 1 && open_calls == 4);
    assert(atomic_load(&output_paused));
    audio_callback(NULL, (Uint8*)pcm, sizeof(pcm));
    mixer_set_app_backgrounded(false);
    advance_audio_clock(test_ticks = 2250);
    mixer_retry_audio_device(); assert(close_calls == 1);
    test_ticks = 3000;
    service_audio_device(); assert(close_calls == 2 && open_calls == 5);

    // An older tick after foreground resume must not rewind or underflow.
    atomic_store(&last_callback, 2500);
    mixer_set_app_backgrounded(true);
    test_ticks = 3101;
    mixer_set_app_backgrounded(false);
    remaining_frames = 44100;
    channel_play(&sound);
    frame = sound.frame;
    advance_audio_clock(3100);
    assert(clock_tick == 3101 && sound.frame == frame);

    // Starting a fade near the end of the grace period must not backdate it.
    test_ticks = 4000;
    audio_callback(NULL, (Uint8*)pcm, sizeof(pcm));
    advance_audio_clock(test_ticks = 4090);
    channel_play(&sound);
    test_ticks = 4095;
    channel_fade(&sound, 80, 0, true);
    advance_audio_clock(test_ticks = 4110);
    assert(channel_is_playing(&sound) && channel_is_fading(&sound));
    advance_audio_clock(test_ticks = 4200);
    assert(!channel_is_playing(&sound));

    puts("Audio clock: failed open, empty/finite sound, stalled callback, fades, pause and recovery passed");
#endif

    for (int i = 0; i < 8; i++) {
        assert(strcmp(mixer_get_name(i), expected_names[i]) == 0);
        int vol = -1;
        assert(mixer_get_volume(i, &vol) && vol == expected_defaults[i]);
        int mute = -1;
        assert(mixer_get_mute(i, &mute) && mute == 0);
    }

    // Exact 0..100 float32 round-trip (specifically guards 53 -> 52 and 59 -> 58 truncation).
    for (int v = 0; v <= 100; v++) {
        assert(mixer_set_volume(0, v));
        int got = -1;
        assert(mixer_get_volume(0, &got));
        assert(got == v);
    }
    for (int ch = 0; ch < 8; ch++) {
        assert(mixer_set_volume(ch, 53));
        int got = -1;
        assert(mixer_get_volume(ch, &got) && got == 53);
        assert(mixer_set_volume(ch, 59));
        assert(mixer_get_volume(ch, &got) && got == 59);
        assert(mixer_set_mute(ch, 1));
        int mute = -1;
        assert(mixer_get_mute(ch, &mute) && mute == 1);
        assert(mixer_set_mute(ch, 0));
        assert(mixer_get_mute(ch, &mute) && mute == 0);
    }

    // Clamping and out-of-range channel rejection.
    int clamped = -1;
    assert(mixer_set_volume(1, -25));
    assert(mixer_get_volume(1, &clamped) && clamped == 0);
    assert(mixer_set_volume(1, 150));
    assert(mixer_get_volume(1, &clamped) && clamped == 100);
    assert(mixer_get_name(-1) == NULL);
    assert(mixer_get_name(8) == NULL);
    assert(!mixer_get_volume(-1, &clamped));
    assert(!mixer_get_volume(8, &clamped));
    assert(!mixer_set_volume(-1, 50));
    assert(!mixer_set_volume(8, 50));
    assert(!mixer_get_mute(-1, &clamped));
    assert(!mixer_get_mute(8, &clamped));
    assert(!mixer_set_mute(-1, 1));
    assert(!mixer_set_mute(8, 1));

    // kiwi_prelink() must preserve pre-configured channels (e.g. AliceStart.ini VolumeValancer).
    static const char *const custom_names[3] = { "Master", "BGM", "SE" };
    static const int custom_vols[3] = { 90, 70, 60 };
    memset(&config, 0, sizeof(config));
    set_default_mixers(custom_names, custom_vols, 3);
    kiwi_prelink();
    assert(config.mixer_nr_channels == 3);
    assert(config.mixer_volumes[0] == 90);
    assert(config.mixer_volumes[1] == 70);
    assert(config.mixer_volumes[2] == 60);

#ifdef ALICEHUB_IOS
    int opens = open_calls, closes = close_calls;
    assert(!mixer_fini());
    assert(master == NULL && joined_threads == 1 && close_calls == closes);
    mixer_retry_audio_device();
    assert(open_calls == opens && atomic_load(&audio_stopping));
    assert(!mixer_fini() && joined_threads == 1 && close_calls == closes);
    assert(!alicehub_ios_finish_audio_shutdown() && quit_calls == 0);
    memset(pcm, 0xff, sizeof(pcm));
    audio_callback(NULL, (Uint8*)pcm, sizeof(pcm));
    for (size_t i = 0; i < sizeof(pcm) / sizeof(float); i++)
        assert(pcm[i] == 0);
    audio_device_main(NULL);
    assert(audio_device == 0 && close_calls == closes + 1);
    assert(alicehub_ios_finish_audio_shutdown() && joined_threads == 2 && quit_calls == 1);
    assert(alicehub_ios_finish_audio_shutdown() && quit_calls == 1);
#endif

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(harness, LIBSYS4_CORE, ["-lz", "-lm"])
        self.assertEqual(out.strip(), "OK")
        out = compile_and_run(harness, LIBSYS4_CORE, ["-DALICEHUB_IOS", "-lz", "-lm"])
        self.assertTrue(out.strip().endswith("OK"))

    def test_kiwi_multi_timemap_lookup_case_insensitive_cjk_fallback_and_fini(self):
        cols_6 = [
            (b"Name", 3),
            (b"Group", 1),
            (b"LoopStart", 1),
            (b"LoopEnd", 1),
            (b"FlagA", 1),
            (b"FlagB", 1),
        ]
        cols_2 = [
            (b"Name", 3),
            (b"Group", 1),
        ]
        sound2_ex = build_extf([
            _ex_soundinfo_table([
                (b"bgm01", 1, 1000, 50000, 0, 0),
                (b"se_click", 2, 0, 0, 0, 0),
                ("兰斯Attack".encode("gbk"), 2, 0, 0, 0, 0),
                (b"Hack Slash", 2, 0, 0, 0, 0),
                # GBK lead 0x81 + trail 0x41 ('A') and trail 0x5C ('\\') must not be altered.
                (b"\x81\x41Test", 2, 0, 0, 0, 0),
                (b"\x81\x5cSlash", 2, 0, 0, 0, 0),
            ], cols_6),
        ])
        voice_ex = build_extf([
            _ex_soundinfo_table([
                (b"vo_m_001", 6, 0, 0, 0, 0),
                (b"vo_f_001", 7, 0, 0, 0, 0),
            ], cols_6),
        ])
        extra_ex = build_extf([
            _ex_soundinfo_table([
                (b"amb_wind", 4),
            ], cols_2),
        ])

        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sndfile.h>
#include <SDL.h>

#include "xsystem4.h"
#include "asset_manager.h"
#include "audio.h"
#include "id_pool.h"
#include "mixer.h"
#include "system4/archive.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "hll/hll.h"

struct config config;
bool game_rance10 = false;
bool game_dohnadohna = false;
bool game_maman = false;
bool game_evenicle2 = false;
bool game_rance03 = true;
bool game_ixseal = false;
bool game_okusama = false;
bool game_haharanman = false;
bool game_night_remaster = false;
bool game_escalayer_reboot, game_tsumamigui3;
bool game_tsumamigui3_jun = false;
bool game_tsumamigui3_ntr = false;

static uint8_t *g_sound2_buf = NULL;
static size_t g_sound2_size = 0;
static uint8_t *g_voice_buf = NULL;
static size_t g_voice_size = 0;
static uint8_t *g_extra_buf = NULL;
static size_t g_extra_size = 0;

static bool g_foreach_enabled = true;
static bool g_fallback_enabled = false;
static int g_sound2_get_calls = 0;
static int g_fake_trail_get_calls = 0;

static uint8_t *read_all(const char *path, size_t *out_size) {
    FILE *fp = fopen(path, "rb");
    assert(fp != NULL);
    assert(fseek(fp, 0, SEEK_END) == 0);
    long sz = ftell(fp);
    assert(sz > 0);
    assert(fseek(fp, 0, SEEK_SET) == 0);
    uint8_t *buf = malloc((size_t)sz);
    assert(buf != NULL);
    assert(fread(buf, 1, (size_t)sz, fp) == (size_t)sz);
    fclose(fp);
    *out_size = (size_t)sz;
    return buf;
}

static void mock_archive_free(struct archive_data *d) {
    free(d->data);
    free(d->name);
    free(d);
}
static struct archive_ops mock_archive_ops = { .free_data = mock_archive_free };
static struct archive mock_archive = { .ops = &mock_archive_ops };

static struct archive_data *make_archive_data(const char *name, const uint8_t *buf, size_t size) {
    struct archive_data *d = calloc(1, sizeof(*d));
    d->archive = &mock_archive;
    d->name = strdup(name);
    d->size = size;
    d->data = malloc(size);
    memcpy(d->data, buf, size);
    return d;
}

int SDL_InitSubSystem(Uint32 flags) { return 0; }
SDL_AudioDeviceID SDL_OpenAudioDevice(const char *dev, int iscap, const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int allowed) {
    if (obtained) *obtained = *desired;
    return 2;
}
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on) {}
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev) {}
void SDL_LockAudioDevice(SDL_AudioDeviceID dev) {}
void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev) {}
const char *SDL_GetError(void) { return ""; }
Uint32 SDL_GetTicks(void) { return 0; }

static int g_fake_sf_handle = 1;
SNDFILE *sf_open(const char *p, int m, SF_INFO *i) { return NULL; }
SNDFILE *sf_open_virtual(SF_VIRTUAL_IO *v, int m, SF_INFO *i, void *u) {
    if (i) {
        i->frames = 100000;
        i->samplerate = 44100;
        i->channels = 2;
    }
    return (SNDFILE *)&g_fake_sf_handle;
}
int sf_close(SNDFILE *s) { return 0; }
int sf_error(SNDFILE *s) { return SF_ERR_NO_ERROR; }
const char *sf_strerror(SNDFILE *s) { return ""; }
sf_count_t sf_seek(SNDFILE *s, sf_count_t f, int w) { return f; }
sf_count_t sf_readf_float(SNDFILE *s, float *p, sf_count_t f) { return 0; }

bool asset_exists_by_name(enum asset_type t, const char *n, int *id) { return false; }
struct archive_data *asset_get(enum asset_type t, int no) { return NULL; }
struct archive_data *asset_get_by_name(enum asset_type t, const char *n, int *id_out) {
    static const uint8_t dummy_audio[4] = { 'O', 'g', 'g', 'S' };
    if (t == ASSET_SOUND && strcmp(n, "timemap_sound2.ex") == 0) {
        g_sound2_get_calls++;
        return make_archive_data(n, g_sound2_buf, g_sound2_size);
    }
    if (t == ASSET_SOUND && strcmp(n, "\x81\x5ctimemap_fake.ex") == 0) {
        g_fake_trail_get_calls++;
        return make_archive_data(n, g_extra_buf, g_extra_size);
    }
    if (t == ASSET_BGM && strcmp(n, "sub\\TIMEMAP_EXTRA.EX") == 0) {
        return make_archive_data(n, g_extra_buf, g_extra_size);
    }
    if (t == ASSET_VOICE && strcmp(n, "timemap_sound.ex") == 0) {
        return make_archive_data(n, g_voice_buf, g_voice_size);
    }
    if (g_fallback_enabled && t == ASSET_SOUND && strcmp(n, "timemap_sound.ex") == 0) {
        return make_archive_data(n, g_voice_buf, g_voice_size);
    }
    if (t == ASSET_SOUND && (strcmp(n, "bgm01.ogg") == 0 || strcmp(n, "\xc0\xbc\xcb\xb9" "ATTACK.wav") == 0)) {
        return make_archive_data(n, dummy_audio, sizeof(dummy_audio));
    }
    if (t == ASSET_VOICE && strcmp(n, "vo_f_001.ogg") == 0) {
        return make_archive_data(n, dummy_audio, sizeof(dummy_audio));
    }
    return NULL;
}
void asset_foreach_file(enum asset_type t, void (*cb)(struct archive_data*, void*), void *u) {
    if (!g_foreach_enabled)
        return;
    if (t == ASSET_SOUND) {
        struct archive_data d1 = { .name = "timemap_sound2.ex" };
        struct archive_data d1_dup = { .name = "TIMEMAP_SOUND2.EX" };
        struct archive_data d_other = { .name = "other.ex" };
        // GBK char 0x81 0x5C followed by timemap_fake.ex: 0x5C is a trail byte, NOT a backslash separator!
        struct archive_data d_cjk_trail = { .name = "\x81\x5ctimemap_fake.ex" };
        cb(&d1, u);
        cb(&d1_dup, u);
        cb(&d_other, u);
        cb(&d_cjk_trail, u);
    } else if (t == ASSET_BGM) {
        struct archive_data d_sub = { .name = "sub\\TIMEMAP_EXTRA.EX" };
        cb(&d_sub, u);
    } else if (t == ASSET_VOICE) {
        struct archive_data d2 = { .name = "timemap_sound.ex" };
        cb(&d2, u);
    }
}
void bgi_read(const char *path) {}
struct bgi *bgi_get(int no) { return NULL; }
void wai_load(const char *path) {}
struct wai *wai_get(int no) { return NULL; }
const char *display_sjis0(const char *s) { return s ? s : ""; }
char *gamedir_path_icase(const char *p) { return NULL; }
void audio_init(void) {}
void id_pool_init(struct id_pool *p, int b) {}
void id_pool_delete(struct id_pool *p) {}
int id_pool_get_first(struct id_pool *p) { return -1; }
int id_pool_get_next(struct id_pool *p, int i) { return -1; }
void *id_pool_get(struct id_pool *p, int i) { return NULL; }
void *id_pool_set(struct id_pool *p, int i, void *d) { return NULL; }
void *id_pool_release(struct id_pool *p, int i) { return NULL; }
void static_library_replace(struct static_library *l, const char *n, void *f) {}

#include "audio_mixer.c"
#include "hll/KiwiSoundEngine_v12.c"

static int query_group(const char *name) {
    struct string *s = cstr_to_string(name);
    int g = sound_group(s);
    free_string(s);
    return g;
}

int main(int argc, char **argv) {
    assert(argc == 4);
    g_sound2_buf = read_all(argv[1], &g_sound2_size);
    g_voice_buf = read_all(argv[2], &g_voice_size);
    g_extra_buf = read_all(argv[3], &g_extra_size);

    sys4_set_text_gbk(true);
    memset(&config, 0, sizeof(config));
    config.default_volume = 100;
    kiwi_prelink();
    mixer_init();

    kiwi_init();
    assert(nr_sound_metas == 3);
    assert(sound_row_index != NULL);
    assert(g_sound2_get_calls == 1);      // duplicate .vanilla.bak entry deduplicated
    assert(g_fake_trail_get_calls == 0);  // CJK 0x5C trail byte not mistaken for backslash

    // Multi-archive sound group lookups (exact, with extensions, ASCII case-insensitive).
    assert(query_group("bgm01") == 1);
    assert(query_group("bgm01.ogg") == 1);
    assert(query_group("BGM01.WAV") == 1);
    assert(query_group("se_click.wav") == 2);
    assert(query_group("\xc0\xbc\xcb\xb9" "Attack") == 2);
    assert(query_group("\xc0\xbc\xcb\xb9" "ATTACK") == 2);
    assert(query_group("\xc0\xbc\xcb\xb9" "ATTACK.wav") == 2);
    assert(query_group("HACK SLASH") == 2);
    assert(query_group("hack slash.ogg") == 2);
    assert(query_group("vo_m_001") == 6);
    assert(query_group("VO_M_001.ogg") == 6);
    assert(query_group("vo_f_001.wav") == 7);
    assert(query_group("amb_wind") == 4);
    assert(query_group("nonexistent") == 0);

    // CJK trail-byte preservation in sound_key_normalize (0x41 'A' and 0x5C '\\').
    assert(query_group("\x81\x41TEST") == 2);
    assert(query_group("\x81\x61test") == 0);
    assert(query_group("\x81\x5cSLASH") == 2);
    assert(query_group("\x81/slash") == 0);

    // open_sound applies group + loop points from multi-timemap rows.
    struct string *s_bgm = cstr_to_string("bgm01.ogg");
    struct channel *ch_bgm = open_sound(s_bgm);
    free_string(s_bgm);
    assert(ch_bgm != NULL);
    assert(channel_get_group(ch_bgm) == 1);
    assert(ch_bgm->loop_start == 1000);
    assert(ch_bgm->loop_end == 50000);
    channel_close(ch_bgm);

    struct string *s_vf = cstr_to_string("vo_f_001.ogg");
    struct channel *ch_vf = open_sound(s_vf);
    free_string(s_vf);
    assert(ch_vf != NULL);
    assert(channel_get_group(ch_vf) == 7);
    channel_close(ch_vf);

    struct string *s_se = cstr_to_string("\xc0\xbc\xcb\xb9" "ATTACK.wav");
    struct channel *ch_se = open_sound(s_se);
    free_string(s_se);
    assert(ch_se != NULL);
    assert(channel_get_group(ch_se) == 2);
    channel_close(ch_se);

    // kiwi_fini() cleans up all loaded timemaps and index cleanly.
    kiwi_fini();
    assert(nr_sound_metas == 0);
    assert(sound_metas == NULL);
    assert(sound_row_entries == NULL);
    assert(nr_sound_row_entries == 0);
    assert(sound_row_index == NULL);
    assert(query_group("bgm01") == 0);

    // Fallback path when asset_foreach_file yields no timemaps: loads ASSET_SOUND/timemap_sound.ex.
    g_foreach_enabled = false;
    g_fallback_enabled = true;
    kiwi_init();
    assert(nr_sound_metas == 1);
    assert(query_group("vo_m_001") == 6);
    assert(query_group("vo_f_001") == 7);
    kiwi_fini();
    assert(nr_sound_metas == 0);

    free(g_sound2_buf);
    free(g_voice_buf);
    free(g_extra_buf);
    puts("OK");
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            p_sound2 = tmp / "timemap_sound2.ex"
            p_voice = tmp / "timemap_sound.ex"
            p_extra = tmp / "timemap_extra.ex"
            p_sound2.write_bytes(sound2_ex)
            p_voice.write_bytes(voice_ex)
            p_extra.write_bytes(extra_ex)
            out = compile_and_run(
                harness,
                LIBSYS4_CORE,
                ["-lz", "-lm"],
                args=[str(p_sound2), str(p_voice), str(p_extra)],
            )
            self.assertEqual(out.strip(), "OK")


if __name__ == "__main__":
    unittest.main()
