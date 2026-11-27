#!/usr/bin/env python3
"""Native C regression tests for cross-game HLL, path resolution, Math,
audio mixer, game detection, and VM zero-vector guard fixes.
"""
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

from test_evenicle2_regressions import LIBSYS4_CORE, ROOT, compile_and_run
from test_vm_reign_regressions import extract_function


def build_rea_archive(entries: list[tuple[bytes, bytes]], raw_index: bytes | None = None) -> bytes:
    """Build a synthetic R8HF .rea archive."""
    mdcd = bytearray()
    index_raw = bytearray()
    if raw_index is not None:
        index_raw.extend(raw_index)
    else:
        for name, payload in entries:
            comp = zlib.compress(payload)
            offset = len(mdcd)
            mdcd.extend(comp)
            index_raw.extend(struct.pack("<I", len(name)))
            index_raw.extend(name)
            index_raw.extend(struct.pack("<II", offset, len(comp)))
    mdlf = zlib.compress(bytes(index_raw))
    out = bytearray()
    out.extend(b"R8HF")
    out.extend(struct.pack("<5I", 0, 0, 0, 0, 0))
    out.extend(b"MDLF")
    out.extend(struct.pack("<I", len(mdlf)))
    out.extend(mdlf)
    out.extend(b"MDCD")
    out.extend(struct.pack("<I", len(mdcd)))
    out.extend(mdcd)
    return bytes(out)


class CrossGameHllRegressionsTest(unittest.TestCase):
    def test_file2_archive_paths_and_fini(self):
        with tempfile.TemporaryDirectory() as tmp:
            game_dir = Path(tmp) / "game"
            cwd_dir = Path(tmp) / "elsewhere"
            abs_dir = Path(tmp) / "abs_saves"
            (game_dir / "subdir").mkdir(parents=True)
            cwd_dir.mkdir(parents=True)
            abs_dir.mkdir(parents=True)

            # Entry 0: offset=0, non-zero compressed length.
            # Entry 1: non-zero offset != compressed length.
            payload0 = struct.pack("<Bi", 0x2A, 123456) + struct.pack("<f", 3.5) + b"entry0\x00"
            payload1 = struct.pack("<Bi", 0x7F, -987654) + struct.pack("<f", -12.25) + b"second_entry_long_payload_1234567890\x00"
            rea_bytes = build_rea_archive([
                (b"first.bin", payload0),
                (b"second.bin", payload1),
            ])
            (game_dir / "pack.rea").write_bytes(rea_bytes)

            # Malformed archive with negative signed name_len (-1 = 0xFFFFFFFF).
            bad_index = struct.pack("<I", 0xFFFFFFFF) + b"oops"
            (game_dir / "bad.rea").write_bytes(build_rea_archive([], raw_index=bad_index))

            # Lowercase file on disk to test case-insensitive read resolution.
            (game_dir / "subdir" / "casefile.bin").write_bytes(struct.pack("<i", 7777))

            harness = r"""
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "system4.h"
#include "system4/file.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "xsystem4.h"
#include "hll/hll.h"

struct config config;
_Noreturn void _vm_error(const char *fmt, ...) { (void)fmt; abort(); }

#include "hll/File2.c"

int main(int argc, char **argv) {
    assert(argc == 4);
    const char *game_dir = argv[1];
    const char *cwd_dir = argv[2];
    const char *abs_dir = argv[3];

    assert(chdir(cwd_dir) == 0);
    config.game_dir = (char *)game_dir;

    // 1. OpenArchiveForRead: entry 0 (offset == 0) AND entry 1 (offset != length).
    struct string *arch = cstr_to_string("PACK");
    struct string *e0 = cstr_to_string("first.bin");
    struct string *e1 = cstr_to_string("second.bin");
    assert(File2_OpenArchiveForRead(arch, e0));
    int b = 0, iv = 0;
    float fv = 0.0f;
    struct string *sv = NULL;
    assert(File2_ReadByte(&b) && b == 0x2A);
    assert(File2_ReadInt(&iv) && iv == 123456);
    assert(File2_ReadFloat(&fv) && fabsf(fv - 3.5f) < 1e-6f);
    assert(File2_ReadString(&sv) && sv && strcmp(sv->text, "entry0") == 0);
    assert(File2_Close());

    assert(File2_OpenArchiveForRead(arch, e1));
    assert(File2_ReadByte(&b) && b == 0x7F);
    assert(File2_ReadInt(&iv) && iv == -987654);
    assert(File2_ReadFloat(&fv) && fabsf(fv - (-12.25f)) < 1e-6f);
    assert(File2_ReadString(&sv) && sv && strcmp(sv->text, "second_entry_long_payload_1234567890") == 0);
    // Test _ModuleFini cleans up open archive stream without VM_ERROR.
    File2_ModuleFini();
    assert(current_stream == NULL && current_file == NULL);
    // Calling _ModuleFini again when nothing is open must not trigger VM_ERROR.
    File2_ModuleFini();

    // 2. Malformed negative name_len archive must be rejected cleanly.
    struct string *bad_arch = cstr_to_string("bad");
    assert(!File2_OpenArchiveForRead(bad_arch, e0));
    assert(current_stream == NULL);

    // 3. Missing archive must not leak and returns false.
    struct string *missing_arch = cstr_to_string("does_not_exist");
    assert(!File2_OpenArchiveForRead(missing_arch, e0));

    // 4. Relative path resolution against config.game_dir (different cwd) + icase read.
    struct string *icase_rel = cstr_to_string("SubDir\\CaseFile.BIN");
    assert(File2_OpenForRead(icase_rel));
    iv = 0;
    assert(File2_ReadInt(&iv) && iv == 7777);
    assert(File2_Close());

    // 5. Relative write goes under config.game_dir, not cwd.
    struct string *rel_write = cstr_to_string("subdir\\written.bin");
    assert(File2_OpenForWrite(rel_write));
    assert(File2_WriteInt(424242));
    File2_ModuleFini();
    assert(current_file == NULL);

    assert(File2_OpenForRead(rel_write));
    iv = 0;
    assert(File2_ReadInt(&iv) && iv == 424242);
    assert(File2_Close());

    // 6. Absolute path is preserved (not prefixed with config.game_dir).
    char abs_file[512];
    snprintf(abs_file, sizeof(abs_file), "%s/direct.bin", abs_dir);
    struct string *abs_s = cstr_to_string(abs_file);
    assert(File2_OpenForWrite(abs_s));
    assert(File2_WriteInt(9911));
    assert(File2_Close());
    assert(file_exists(abs_file));

    free_string(sv);
    free_string(arch);
    free_string(e0);
    free_string(e1);
    free_string(bad_arch);
    free_string(missing_arch);
    free_string(icase_rel);
    free_string(rel_write);
    free_string(abs_s);
    puts("OK");
    return 0;
}
"""
            out = compile_and_run(
                harness,
                [ROOT / "src/util.c", *LIBSYS4_CORE],
                ["-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-unused-function", "-lz", "-lm"],
                [str(game_dir), str(cwd_dir), str(abs_dir)],
            )
            self.assertEqual(out.strip(), "OK")
            self.assertTrue((game_dir / "subdir" / "written.bin").exists())
            self.assertFalse((cwd_dir / "subdir" / "written.bin").exists())
            self.assertTrue((abs_dir / "direct.bin").exists())

    def test_file_banmisc_soundfileplayer_paths_and_fini(self):
        with tempfile.TemporaryDirectory() as tmp:
            game_dir = Path(tmp) / "game"
            cwd_dir = Path(tmp) / "other_cwd"
            abs_dir = Path(tmp) / "abs_out"
            (game_dir / "data").mkdir(parents=True)
            cwd_dir.mkdir(parents=True)
            abs_dir.mkdir(parents=True)
            (game_dir / "data" / "sample.ogg").write_bytes(b"OggS")
            (game_dir / "data" / "old.dat").write_bytes(b"hello")

            harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/file.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "xsystem4.h"
#include "audio.h"
#include "iarray.h"
#include "savedata.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "hll/hll.h"

struct config config;
struct ain *ain;
struct vm_pointer *heap;

_Noreturn void _vm_error(const char *fmt, ...) { (void)fmt; abort(); }
void variable_fini(union vm_value v, enum ain_data_type type, bool call_dtor) { (void)v; (void)type; (void)call_dtor; }
int heap_alloc_string(struct string *s) { (void)s; return 0; }
struct string *heap_get_string(int slot) { (void)slot; return &EMPTY_STRING; }
struct page *heap_get_page(int slot) { (void)slot; return NULL; }
struct page *alloc_page(enum page_type type, int type_index, int nr_vars) {
    struct page *p = xcalloc(1, sizeof(*p) + sizeof(union vm_value) * nr_vars);
    p->type = type; p->index = type_index; p->nr_vars = nr_vars;
    return p;
}
void free_page(struct page *p) { free(p); }
void delete_page_vars(struct page *p) { (void)p; }
enum ain_data_type array_type(enum ain_data_type t) { return t; }
void json_load_page(struct page *page, cJSON *json, bool call_dtor) { (void)page; (void)json; (void)call_dtor; }

// Stubs for SoundFilePlayer audio functions
static char last_bgm_path[512];
static int unprepare_calls = 0;
int bgm_get_unused_channel(void) { return 7; }
int bgm_prepare_from_file(int ch, char *path) {
    assert(ch == 7);
    snprintf(last_bgm_path, sizeof(last_bgm_path), "%s", path);
    return file_exists(path) ? 1 : 0;
}
int bgm_unprepare(int ch) { assert(ch == 7); unprepare_calls++; return 1; }
int bgm_set_loop_count(int ch, int c) { (void)ch; (void)c; return 1; }
int bgm_play(int ch) { (void)ch; return 1; }
int bgm_stop(int ch) { (void)ch; return 1; }
bool bgm_is_playing(int ch) { (void)ch; return false; }
int bgm_get_length(int ch) { (void)ch; return 0; }
int bgm_get_pos(int ch) { (void)ch; return 0; }
int bgm_fade(int ch, int t, int v, bool s) { (void)ch; (void)t; (void)v; (void)s; return 1; }
int bgm_stop_fade(int ch) { (void)ch; return 1; }
bool bgm_is_fading(int ch) { (void)ch; return false; }
int bgm_prepare(int ch, int no) { (void)ch; (void)no; return 0; }
int wav_prepare(int ch, int no) { (void)ch; (void)no; return 0; }
int wav_play(int ch) { (void)ch; return 0; }
int wav_stop(int ch) { (void)ch; return 0; }
int wav_fade(int ch, int t, int v, bool s) { (void)ch; (void)t; (void)v; (void)s; return 0; }
int wav_reverse_LR(int ch) { (void)ch; return 0; }
bool wav_is_playing(int ch) { (void)ch; return false; }
int wav_unprepare(int ch) { (void)ch; return 0; }

// Minimal iarray stubs for BanMisc SaveStruct/LoadStruct
void iarray_init_writer(struct iarray_writer *w, const char *h) { (void)h; memset(w, 0, sizeof(*w)); }
void iarray_write_struct(struct iarray_writer *w, struct page *p, bool t) { (void)w; (void)p; (void)t; }
uint8_t *iarray_to_buffer(struct iarray_writer *w, size_t *sz) {
    (void)w;
    *sz = 4;
    uint8_t *b = xmalloc(4);
    LittleEndian_putDW(b, 0, 0x12345678);
    return b;
}
void iarray_free_writer(struct iarray_writer *w) { (void)w; }
bool iarray_init_reader(struct iarray_reader *r, struct page *a, const char *h) {
    (void)h; memset(r, 0, sizeof(*r)); r->data = a->values; r->size = a->nr_vars;
    return true;
}
struct page *iarray_read_struct(struct iarray_reader *r, int struct_type, bool t) {
    (void)t;
    assert(r->data && r->size == 1 && r->data[0].i == 0x12345678);
    struct page *p = alloc_page(STRUCT_PAGE, struct_type, 1);
    p->values[0].i = r->data[0].i;
    return p;
}

#include "hll/File.c"
#include "hll/BanMisc.c"
#include "hll/SoundFilePlayer.c"

int main(int argc, char **argv) {
    assert(argc == 4);
    const char *game_dir = argv[1];
    const char *cwd_dir = argv[2];
    const char *abs_dir = argv[3];

    assert(chdir(cwd_dir) == 0);
    config.game_dir = (char *)game_dir;

    // --- File.c ---
    struct string *mk = cstr_to_string("sub_created\\nested");
    assert(File_MakeDirectory(mk) == 1);
    char expected_dir[512];
    snprintf(expected_dir, sizeof(expected_dir), "%s/sub_created/nested", game_dir);
    assert(is_directory(expected_dir));

    struct string *rel_dat = cstr_to_string("Data\\OLD.DAT");
    struct page *date_page = alloc_page(STRUCT_PAGE, 0, 7);
    assert(File_GetTime(rel_dat, &date_page) == 1);
    assert(date_page->values[0].i >= 2000);
    free_page(date_page);

    assert(File_Open(rel_dat, FILE_READ) == 1);
    File_ModuleFini();
    assert(current_file == NULL);
    File_ModuleFini(); // safe when idle

    assert(File_Delete(rel_dat) == 1);
    // Missing delete returns 0 and must not leak path
    assert(File_Delete(rel_dat) == 0);

    // --- BanMisc.c ---
    struct page *st = alloc_page(STRUCT_PAGE, 0, 1);
    st->values[0].i = 0;
    struct string *bm_rel = cstr_to_string("SaveSub\\slot1.dat");
    assert(BanMisc_SaveStruct(st, bm_rel) == 1);
    char expected_bm[512];
    snprintf(expected_bm, sizeof(expected_bm), "%s/SaveSub/slot1.dat", game_dir);
    assert(file_exists(expected_bm));

    struct string *bm_icase = cstr_to_string("savesub\\SLOT1.DAT");
    assert(BanMisc_LoadStruct(&st, bm_icase) == 1);
    assert(st->values[0].i == 0x12345678);

    char abs_bm[512];
    snprintf(abs_bm, sizeof(abs_bm), "%s/bm_abs.dat", abs_dir);
    struct string *bm_abs = cstr_to_string(abs_bm);
    assert(BanMisc_SaveStruct(st, bm_abs) == 1);
    assert(file_exists(abs_bm));
    free_page(st);

    // --- SoundFilePlayer.c ---
    struct string *snd_rel = cstr_to_string("DATA\\SAMPLE.OGG");
    assert(SoundFilePlayer_Load(snd_rel));
    assert(strstr(last_bgm_path, game_dir) == last_bgm_path);
    assert(channel == 7);
    // Verify _ModuleFini is SoundFilePlayer_Release and cleans up channel.
    bool found_fini = false;
    for (int i = 0; lib_SoundFilePlayer.functions[i].name; i++) {
        if (!strcmp(lib_SoundFilePlayer.functions[i].name, "_ModuleFini")) {
            ((void (*)(void))lib_SoundFilePlayer.functions[i].fun)();
            found_fini = true;
            break;
        }
    }
    assert(found_fini && channel == -1 && unprepare_calls == 1);

    free_string(mk);
    free_string(rel_dat);
    free_string(bm_rel);
    free_string(bm_icase);
    free_string(bm_abs);
    free_string(snd_rel);
    puts("OK");
    return 0;
}
"""
            out = compile_and_run(
                harness,
                [ROOT / "src/util.c", ROOT / "src/cJSON.c", *LIBSYS4_CORE],
                ["-lz", "-lm"],
                [str(game_dir), str(cwd_dir), str(abs_dir)],
            )
            self.assertEqual(out.strip(), "OK")

    def test_math_nan_randtable_and_fini(self):
        harness = r"""
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hll/Math.c"

int main(void) {
    // 1. NaN guard in Acos/Asin
    float nan_val = NAN;
    assert(Math_Acos(nan_val) == 0.0f && !isnan(Math_Acos(nan_val)));
    assert(Math_Asin(nan_val) == 0.0f && !isnan(Math_Asin(nan_val)));
    assert(fabsf(Math_Acos(1.0f) - 0.0f) < 1e-4f);
    assert(fabsf(Math_Acos(-1.0f) - 180.0f) < 1e-4f);
    assert(fabsf(Math_Acos(2.0f) - 0.0f) < 1e-4f);
    assert(fabsf(Math_Asin(1.0f) - 90.0f) < 1e-4f);
    assert(fabsf(Math_Asin(-2.0f) - (-90.0f)) < 1e-4f);

    // 2. size <= 0 guard
    Math_RandTableInit(1, 0);
    Math_RandTableInit(2, -10);
    assert(find_shuffle_table(1) == NULL);
    assert(find_shuffle_table(2) == NULL);
    assert(Math_RandTable(1) == 0);

    // 3. Fisher-Yates fixed-point reachability (size=2 must reach [0, 1] AND [1, 0])
    Math_SetSeed(12345);
    int saw_id = 0, saw_swap = 0;
    for (int trial = 0; trial < 200; trial++) {
        Math_RandTableInit(10, 2);
        int a = Math_RandTable(10);
        int b = Math_RandTable(10);
        assert((a == 0 && b == 1) || (a == 1 && b == 0));
        if (a == 0 && b == 1) saw_id++;
        else saw_swap++;
    }
    assert(saw_id > 20 && saw_swap > 20);

    // 4. Permutation validity + fixed-point reachability for size=5
    int fixed_hits[5] = {0};
    Math_RandTableInit(20, 5);
    for (int cycle = 0; cycle < 200; cycle++) {
        bool seen[5] = {false};
        for (int pos = 0; pos < 5; pos++) {
            int v = Math_RandTable(20);
            assert(v >= 0 && v < 5 && !seen[v]);
            seen[v] = true;
            if (v == pos) fixed_hits[pos]++;
        }
    }
    for (int i = 0; i < 5; i++)
        assert(fixed_hits[i] > 5);

    // 5. _ModuleFini cleans up shuffle tables and resets mt_initialized
    bool found_fini = false;
    for (int i = 0; lib_Math.functions[i].name; i++) {
        if (!strcmp(lib_Math.functions[i].name, "_ModuleFini")) {
            ((void (*)(void))lib_Math.functions[i].fun)();
            found_fini = true;
            break;
        }
    }
    assert(found_fini);
    assert(shuffle_tables == NULL);
    assert(mt_initialized == false);

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            LIBSYS4_CORE,
            ["-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-unused-function", "-lz", "-lm"],
        )
        self.assertEqual(out.strip(), "OK")

    def test_system_service_overloads_mobile_defaults_and_fini(self):
        with tempfile.TemporaryDirectory() as stub_tmp:
            stub_inc = Path(stub_tmp)
            (stub_inc / "OpenGLES" / "ES3").mkdir(parents=True)
            (stub_inc / "OpenGLES" / "ES3" / "gl.h").write_text(
                "#include <GLES3/gl3.h>\n", encoding="utf-8"
            )
            (stub_inc / "EnginePlatform.h").write_text(
                "void alicehub_ios_open_manual(const char *path);\n", encoding="utf-8"
            )
            cases = [
                ([], "Linux", False, True),
                (["-DALICEHUB_IOS", f"-I{stub_inc}"], "iOS", True, False),
                (["-D__SWITCH__"], "Nintendo Switch", True, False),
            ]
            for extra_flags, expected_os, expected_vsync, expected_min in cases:
                harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#ifdef ALICEHUB_IOS
void alicehub_ios_open_manual(const char *path) {{ (void)path; }}
#endif

#include "xsystem4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "hll/hll.h"
#include "gfx/gfx.h"
#include "gfx/private.h"
#include "input.h"
#include "mixer.h"
#include "savedata.h"
#include "vm/heap.h"
#include "vm/page.h"

struct config config;
struct ain *ain;
bool vm_reset_once = false;
struct sdl_private sdl;
bool game_rance10, game_dohnadohna, game_evenicle2_rance, game_evenicle2,
     game_haharanman, game_okusama, game_night_remaster, game_ixseal,
     game_tsumamigui3, game_tsumamigui3_jun, game_tsumamigui3_ntr,
     game_evenicle, game_evenicle_rance, game_evenicle_gaiden, game_bladebriders;

_Noreturn void _vm_error(const char *fmt, ...) {{ (void)fmt; abort(); }}
void static_library_replace(struct static_library *l, const char *n, void *f) {{ (void)l; (void)n; (void)f; }}
int mixer_get_numof(void) {{ return 0; }}
const char *mixer_get_name(int n) {{ (void)n; return NULL; }}
int mixer_get_volume(int n, int *v) {{ (void)n; (void)v; return 0; }}
int mixer_get_mute(int n, int *m) {{ (void)n; (void)m; return 0; }}
int mixer_set_name(int n, const char *s) {{ (void)n; (void)s; return 0; }}
int mixer_set_volume(int n, int v) {{ (void)n; (void)v; return 0; }}
int mixer_set_mute(int n, int m) {{ (void)n; (void)m; return 0; }}
bool gfx_is_fullscreen(void) {{ return false; }}
bool gfx_set_fullscreen(bool on) {{ (void)on; return true; }}
static int swap_calls = 0;
void gfx_swap(void) {{ swap_calls++; }}
static bool mock_frame_presented = false;
bool gfx_consume_frame_presented(void) {{ bool was = mock_frame_presented; mock_frame_presented = false; return was; }}
static int wait_vsync_calls = 0;
static bool last_wait_vsync = false;
void gfx_set_wait_vsync(bool on) {{ wait_vsync_calls++; last_wait_vsync = on; }}
void mouse_set_pos(int x, int y) {{ (void)x; (void)y; }}
bool mouse_show_cursor(bool on) {{ (void)on; return true; }}
void input_reset_close_hook(void) {{}}
bool input_has_system_message(void) {{ return false; }}
bool input_pop_system_message(int *m) {{ (void)m; return false; }}
void input_set_close_hook(bool f) {{ (void)f; }}
bool input_get_close_hook(void) {{ return false; }}
void get_date(int *y, int *m, int *d, int *w) {{ *y = 2026; *m = 1; *d = 1; *w = 4; }}
void get_time(int *h, int *m, int *s, int *ms) {{ *h = 1; *m = 2; *s = 3; *ms = 4; }}
static int save_json_calls = 0;
static cJSON *mock_loaded_window_json = NULL;
int save_json(const char *f, cJSON *j) {{ (void)f; (void)j; save_json_calls++; return 1; }}
cJSON *load_json(const char *f) {{
    (void)f;
    if (!mock_loaded_window_json) return NULL;
    return cJSON_Duplicate(mock_loaded_window_json, true);
}}
char *savedir_path(const char *p) {{ return strdup(p); }}
struct page *alloc_array(int r, union vm_value *d, enum ain_data_type t, int s, bool i) {{
    (void)r; (void)d; (void)t; (void)s; (void)i; return NULL;
}}
void delete_page_vars(struct page *p) {{ (void)p; }}
void free_page(struct page *p) {{ (void)p; }}
const char *SDL_GetWindowTitle(SDL_Window *w) {{ (void)w; return "OriginalTitle"; }}
void SDL_SetWindowTitle(SDL_Window *w, const char *t) {{ (void)w; (void)t; }}
void SDL_EnableScreenSaver(void) {{}}
void SDL_DisableScreenSaver(void) {{}}
int SDL_GetSystemRAM(void) {{ return 4096; }}
int SDL_OpenURL(const char *u) {{ (void)u; return 0; }}
const char *SDL_GetError(void) {{ return ""; }}

#include "hll/SystemService.c"

static void *resolve_entry(const char *name, const struct ain_hll_function *fn) {{
    for (int i = 0; lib_SystemService.functions[i].name; i++) {{
        if (!strcmp(lib_SystemService.functions[i].name, name)) {{
            assert(lib_SystemService.functions[i].resolve != NULL);
            return lib_SystemService.functions[i].resolve(fn);
        }}
    }}
    abort();
}}

int main(void) {{
    config.view_width = 1280;
    config.view_height = 720;

    // 1. 0-arg vs 1-arg overload resolution for GetOSInfo and GetScreenInfo
    struct ain_hll_function f0 = {{ .name = "GetOSInfo", .nr_arguments = 0, .return_type.data = AIN_STRING }};
    struct ain_hll_argument arg_ref = {{ .type.data = AIN_REF_STRING }};
    struct ain_hll_function f1 = {{ .name = "GetOSInfo", .nr_arguments = 1, .arguments = &arg_ref, .return_type.data = AIN_VOID }};

    struct ain_hll_function f1_bool = {{ .name = "GetOSInfo", .nr_arguments = 1, .arguments = &arg_ref, .return_type.data = AIN_BOOL }};
    struct ain_hll_function f_bad = {{ .name = "GetOSInfo", .nr_arguments = 2, .return_type.data = AIN_VOID }};

    assert(resolve_entry("GetOSInfo", &f0) == (void *)SystemService_GetOSInfo);
    assert(resolve_entry("GetOSInfo", &f1) == (void *)SystemService_GetOSInfo_ref);
    assert(resolve_entry("GetOSInfo", &f1_bool) == (void *)SystemService_GetOSInfo_ref);
    assert(resolve_entry("GetOSInfo", &f_bad) == NULL);
    assert(resolve_entry("GetScreenInfo", &f0) == (void *)SystemService_GetScreenInfo);
    assert(resolve_entry("GetScreenInfo", &f1) == (void *)SystemService_GetScreenInfo_ref);
    assert(resolve_entry("GetScreenInfo", &f1_bool) == (void *)SystemService_GetScreenInfo_ref);
    assert(resolve_entry("GetScreenInfo", &f_bad) == NULL);

    struct string *os0 = SystemService_GetOSInfo();
    assert(strcmp(os0->text, "{expected_os}") == 0);
    free_string(os0);

    struct string *os1 = cstr_to_string("old");
    SystemService_GetOSInfo_ref(&os1);
    assert(strcmp(os1->text, "{expected_os}") == 0);
    free_string(os1);

    struct string *scr0 = SystemService_GetScreenInfo();
    assert(strcmp(scr0->text, "1280x720") == 0);
    free_string(scr0);

    struct string *scr1 = cstr_to_string("old");
    SystemService_GetScreenInfo_ref(&scr1);
    assert(strcmp(scr1->text, "1280x720") == 0);
    free_string(scr1);

    // 2. Platform window_settings defaults, no-file VSync sync & mobile minimize lock
    load_window_settings();
    assert(wait_vsync_calls == 1 && last_wait_vsync == {"true" if expected_vsync else "false"});
    int vsync = -1, min_inact = -1;
    assert(SystemService_GetWindowSetting(WINDOW_SETTINGS_WAIT_VSYNC, &vsync));
    assert(SystemService_GetWindowSetting(WINDOW_SETTINGS_MINIMIZE_BY_FULL_SCREEN_INACTIVE, &min_inact));
    assert((bool)vsync == {"true" if expected_vsync else "false"});
    assert((bool)min_inact == {"true" if expected_min else "false"});

    assert(SystemService_SetWindowSetting(WINDOW_SETTINGS_MINIMIZE_BY_FULL_SCREEN_INACTIVE, 1));
    assert(SystemService_GetWindowSetting(WINDOW_SETTINGS_MINIMIZE_BY_FULL_SCREEN_INACTIVE, &min_inact));
    assert((bool)min_inact == {"true" if expected_min else "false"});

    // 2b. Dirty-checked SetWindowSetting & SetWaitVSyncMode: zero disk writes / zero VSync resets on no-op!
    save_json_calls = 0;
    int vs_before = wait_vsync_calls;
    for (int r = 0; r < 60; r++) {{
        assert(SystemService_SetWindowSetting(WINDOW_SETTINGS_WAIT_VSYNC, vsync));
        assert(SystemService_SetWindowSetting(WINDOW_SETTINGS_BACK_TO_TITLE_CONFIRM, 1));
        assert(SystemService_SetWindowSetting(WINDOW_SETTINGS_CLOSE_GAME_CONFIRM, 1));
        SystemService_SetWaitVSyncMode(vsync);
    }}
    assert(save_json_calls == 0 && wait_vsync_calls == vs_before);
    // Changing wait_vsync triggers exactly 1 gfx_set_wait_vsync + 1 save_json; repeating is a no-op.
    assert(SystemService_SetWindowSetting(WINDOW_SETTINGS_WAIT_VSYNC, !vsync));
    assert(save_json_calls == 1 && wait_vsync_calls == vs_before + 1 && last_wait_vsync == !vsync);
    assert(SystemService_SetWindowSetting(WINDOW_SETTINGS_WAIT_VSYNC, !vsync));
    assert(save_json_calls == 1 && wait_vsync_calls == vs_before + 1);
    SystemService_SetWaitVSyncMode(vsync);
    assert(save_json_calls == 2 && wait_vsync_calls == vs_before + 2 && last_wait_vsync == vsync);
    SystemService_SetWaitVSyncMode(vsync);
    assert(save_json_calls == 2 && wait_vsync_calls == vs_before + 2);
    // SystemServiceEx.SetWaitVSync keeps window_settings.wait_vsync in sync without writing JSON!
    system_service_sync_wait_vsync(!vsync);
    assert(save_json_calls == 2 && wait_vsync_calls == vs_before + 3 && last_wait_vsync == !vsync);
    assert(SystemService_SetWindowSetting(WINDOW_SETTINGS_WAIT_VSYNC, vsync));
    assert(save_json_calls == 3 && wait_vsync_calls == vs_before + 4 && last_wait_vsync == vsync);

    // 2c. UpdateView double-swap dedup: skips redundant gfx_swap when frame already presented.
    swap_calls = 0;
    mock_frame_presented = true;
    assert(SystemService_UpdateView());
    assert(swap_calls == 0);
    assert(SystemService_UpdateView());
    assert(swap_calls == 1);

    // 2d. AIN v14 (Dohna Dohna: no SetWaitVSyncMode) WindowSetting enum shift + stale JSON immunity.
    struct ain *dd_ain = ain_new(14, 1);
    ain_add_library(dd_ain, "SystemService");
    ain = dd_ain;
    assert(window_settings_uses_v14_layout() == true);
    mock_loaded_window_json = cJSON_CreateObject();
    cJSON_AddBoolToObject(mock_loaded_window_json, WAIT_VSYNC_JSON_KEY, !({"true" if expected_vsync else "false"}));
    window_settings = window_settings_defaults();
    save_json_calls = 0;
    vs_before = wait_vsync_calls;
    load_window_settings();
    assert(window_settings.wait_vsync == {"true" if expected_vsync else "false"});
    assert(wait_vsync_calls == vs_before + 1 && last_wait_vsync == {"true" if expected_vsync else "false"});
    // type=2 is MinimizeByFullScreenInactive (false by default on mobile), NOT WAIT_VSYNC!
    int dd_min = -1, dd_title = -1, dd_close = -1, dd_bad = -1;
    assert(SystemService_GetWindowSetting(2, &dd_min));
    assert((bool)dd_min == {"true" if expected_min else "false"});
    assert(SystemService_SetWindowSetting(2, 0));
    assert(window_settings.wait_vsync == {"true" if expected_vsync else "false"});
    assert(save_json_calls == 0 && wait_vsync_calls == vs_before + 1);
    assert(SystemService_SetWindowSetting(3, 0) && SystemService_GetWindowSetting(3, &dd_title) && dd_title == 0);
    assert(SystemService_SetWindowSetting(4, 0) && SystemService_GetWindowSetting(4, &dd_close) && dd_close == 0);
    assert(!SystemService_GetWindowSetting(5, &dd_bad));
    assert(save_json_calls == 0);
    // AIN v14 WITH SetWaitVSyncMode exported keeps standard enum layout.
    int ss_lib = ain_get_library(dd_ain, "SystemService");
    dd_ain->libraries[ss_lib].nr_functions = 1;
    dd_ain->libraries[ss_lib].functions = xcalloc(1, sizeof(struct ain_hll_function));
    dd_ain->libraries[ss_lib].functions[0].name = xstrdup("SetWaitVSyncMode");
    assert(window_settings_uses_v14_layout() == false);
    cJSON_Delete(mock_loaded_window_json);
    mock_loaded_window_json = NULL;
    ain_free(dd_ain);
    ain = NULL;
    window_settings = window_settings_defaults();

    // 3. Mutate all static states, then invoke _ModuleFini and verify full cleanup/reset.
    struct string *k = cstr_to_string("k1");
    struct string *v = cstr_to_string("v1");
    SystemVariable_Set(k, v);
    GameVariable_Set(k, v);
    assert(SystemVariable_NumofKey() == 1 && GameVariable_NumofKey() == 1);
    SystemService_AddBackupSaveFileName(k);
    assert(nr_backup_save_files == 1);
    sdl.window = (SDL_Window *)1;
    SystemService_ShowWaitMessage(true);
    assert(previous_title != NULL);
    SystemService_SetHideMouseCursorByGame(true);
    SystemService_SetMouseCursorConfig(0, 1);
    SystemService_SetMouseCursorConfig(2, 999);
    SystemService_SetViewResizableMode(true);
    SystemService_Debug_SetShowMipmapLevel(true);
    SystemService_SetResizableUserControl(true);
    SystemService_SetWindowSetting(WINDOW_SETTINGS_WAIT_VSYNC, !vsync);

    bool found_fini = false;
    for (int i = 0; lib_SystemService.functions[i].name; i++) {{
        if (!strcmp(lib_SystemService.functions[i].name, "_ModuleFini")) {{
            ((void (*)(void))lib_SystemService.functions[i].fun)();
            found_fini = true;
            break;
        }}
    }}
    assert(found_fini);
    // SystemVariable/GameVariable must survive vm_reset() (_ModuleFini),
    // and only clear on process/iOS session exit via system_service_end_session().
    assert(SystemVariable_NumofKey() == 1 && GameVariable_NumofKey() == 1);
    assert(backup_save_files == NULL && nr_backup_save_files == 0);
    assert(previous_title == NULL);
    assert(hide_mouse_cursor_by_game == false);
    assert(mouse_cursor_config[0] == 0 && mouse_cursor_config[1] == 0 && mouse_cursor_config[2] == 3000);
    assert(view_resizable_mode == false && show_mipmap_level == false && resizable_user_control == false);
    assert(window_settings.wait_vsync == {"true" if expected_vsync else "false"});
    assert(window_settings.minimize_by_full_screen_inactive == {"true" if expected_min else "false"});

    system_service_end_session();
    assert(system_variables == NULL && game_variables == NULL);

    free_string(k);
    free_string(v);
    puts("OK");
    return 0;
}}
"""
                out = compile_and_run(
                    harness,
                    [*LIBSYS4_CORE, ROOT / "src/cJSON.c"],
                    ["-lz", "-lm", *extra_flags],
                )
                self.assertEqual(out.strip(), "OK")

    def test_clipboard_fileop_guiengine_chipmunk_mainexv12(self):
        with tempfile.TemporaryDirectory() as tmp:
            list_dir = Path(tmp) / "files"
            list_dir.mkdir()
            for idx in range(5):
                (list_dir / f"item_{idx}.txt").write_text("x", encoding="utf-8")

            harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/ex.h"
#include "system4/string.h"
#include "xsystem4.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "vm/v12.h"
#include "hll/hll.h"

struct config config;
struct ain *ain;
bool game_evenicle2 = false;
bool game_okusama = false;
bool game_haharanman = false;
bool game_rance10 = false;
bool game_dohnadohna = false;
bool game_ixseal = false;

static int error_calls = 0;
_Noreturn void _vm_error(const char *fmt, ...) { (void)fmt; abort(); }
void static_library_replace(struct static_library *l, const char *n, void *f) { (void)l; (void)n; (void)f; }

// Track VM string heap allocations to verify zero string-slot leak in FileOperation.GetFileList.
static struct vm_pointer mock_heap[64];
struct vm_pointer *heap = mock_heap;
static int active_string_slots = 0;
static int total_string_allocs = 0;

int heap_alloc_string(struct string *s) {
    for (int i = 1; i < 64; i++) {
        if (!mock_heap[i].ref) {
            mock_heap[i].ref = 1;
            mock_heap[i].type = VM_STRING;
            mock_heap[i].s = s;
            active_string_slots++;
            total_string_allocs++;
            return i;
        }
    }
    abort();
}
struct page *alloc_page(enum page_type type, int type_index, int nr_vars) {
    struct page *p = xcalloc(1, sizeof(*p) + sizeof(union vm_value) * nr_vars);
    p->type = type; p->index = type_index; p->nr_vars = nr_vars;
    return p;
}
struct page *alloc_array(int rank, union vm_value *dims, enum ain_data_type dt, int st, bool init) {
    (void)st; (void)init;
    assert(rank == 1 && dt == AIN_ARRAY_STRING);
    int n = dims->i;
    struct page *p = alloc_page(ARRAY_PAGE, dt, n);
    p->array.rank = 1;
    for (int i = 0; i < n; i++) {
        // Real alloc_array pre-allocates a VM_STRING slot for each element!
        p->values[i].i = heap_alloc_string(string_ref(&EMPTY_STRING));
    }
    return p;
}
struct page *v12_array_new(const struct ain_type *t, int n, bool init) {
    (void)t; (void)init;
    union vm_value dim = { .i = n };
    return alloc_array(1, &dim, AIN_ARRAY_STRING, 0, false);
}
void delete_page_vars(struct page *p) {
    for (int i = 0; i < p->nr_vars; i++) {
        int slot = p->values[i].i;
        assert(slot > 0 && slot < 64 && mock_heap[slot].ref == 1);
        free_string(mock_heap[slot].s);
        mock_heap[slot].s = NULL;
        mock_heap[slot].ref = 0;
        active_string_slots--;
    }
}
void free_page(struct page *p) { free(p); }

// Stubs for MainEXFile_v12.c
struct archive *RE_open_3d_archive(void) { return NULL; }
struct archive_data *RE_archive_find_txtex(struct archive *ar, const char *dir) { (void)ar; (void)dir; return NULL; }
struct page *heap_get_page(int slot) { (void)slot; return NULL; }
int heap_alloc_page(struct page *p) { (void)p; return 0; }
struct string *heap_get_string(int slot) { return mock_heap[slot].s; }
void heap_unref(int slot) { (void)slot; }
struct page *copy_page(struct page *p) { return p; }

#include "hll/FileOperation.c"
#include "hll/MainEXFile_v12.c"

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *dir = argv[1];

    // 1. Clipboard.GetText overload: 0-arg (v12+) and 1-arg ref string (v6)
    struct ain_hll_function cb0 = { .name = "GetText", .nr_arguments = 0, .return_type.data = AIN_STRING };
    struct ain_hll_argument ref_str = { .type.data = AIN_REF_STRING };
    struct ain_hll_function cb1 = { .name = "GetText", .nr_arguments = 1, .arguments = &ref_str, .return_type.data = AIN_VOID };
    void *(*cb_resolver)(const struct ain_hll_function *) = NULL;
    for (int i = 0; lib_Clipboard.functions[i].name; i++) {
        if (!strcmp(lib_Clipboard.functions[i].name, "GetText"))
            cb_resolver = lib_Clipboard.functions[i].resolve;
    }
    assert(cb_resolver != NULL);
    assert(cb_resolver(&cb0) == (void *)Clipboard_GetText);
    assert(cb_resolver(&cb1) == (void *)Clipboard_GetText_ref);

    struct string *txt = cstr_to_string("alice_clip");
    Clipboard_SetText(txt);
    struct string *got0 = Clipboard_GetText();
    assert(strcmp(got0->text, "alice_clip") == 0);
    struct string *got1 = cstr_to_string("stale");
    Clipboard_GetText_ref(&got1);
    assert(strcmp(got1->text, "alice_clip") == 0);
    Clipboard_ModuleFini();
    free_string(txt);
    free_string(got0);
    free_string(got1);

    // 2. FileOperation.GetFileList: no VM_STRING slot leak
    struct string *dir_s = cstr_to_string(dir);
    struct page *list_page = NULL;
    assert(FileOperation_GetFileList(dir_s, &list_page));
    assert(list_page != NULL && list_page->nr_vars == 5);
    // Must allocate exactly 5 slots (in alloc_array) and overwrite in-place, NOT 10 slots!
    assert(total_string_allocs == 5);
    assert(active_string_slots == 5);
    for (int i = 0; i < 5; i++) {
        struct string *s = mock_heap[list_page->values[i].i].s;
        assert(s && strncmp(s->text, "item_", 5) == 0);
    }
    delete_page_vars(list_page);
    free_page(list_page);
    assert(active_string_slots == 0);
    free_string(dir_s);

    // 3. MainEXFile_v12 mainex_init() with config.ex_path == NULL succeeds cleanly
    config.ex_path = NULL;
    mainex_init();
    assert(readers != NULL && readers[1] != NULL && readers[1]->nr_blocks == 0);
    assert(error_calls == 0);
    mainex_fini();
    assert(readers == NULL && nr_readers == 0);

    puts("OK");
    return 0;
}
"""
            out = compile_and_run(
                harness,
                [ROOT / "src/util.c", *LIBSYS4_CORE],
                ["-lz", "-lm"],
                [str(list_dir)],
            )
            self.assertEqual(out.strip(), "OK")

        # Also verify GUIEngine.GetMessageType overload and ChipmunkSpriteEngine brightness + ModuleFini.
        fn_gui_0 = extract_function("src/hll/GUIEngine.c", "GUIEngine_GetMessageType")
        fn_gui_1 = extract_function("src/hll/GUIEngine.c", "GUIEngine_GetMessageType_idx")
        fn_gui_res = extract_function("src/hll/GUIEngine.c", "GUIEngine_ResolveGetMessageType")
        gui_src = (ROOT / "src/hll/GUIEngine.c").read_text(encoding="utf-8")
        self.assertIn("HLL_OVERLOAD(GetMessageType, GUIEngine_ResolveGetMessageType)", gui_src)

        cs_src = (ROOT / "src/hll/ChipmunkSpriteEngine.c").read_text(encoding="utf-8")
        self.assertIn("HLL_EXPORT(SP_SetBrightness, sact_SP_SetBrightness)", cs_src)
        self.assertIn("HLL_EXPORT(SP_GetBrightness, sact_SP_GetBrightness)", cs_src)
        self.assertNotIn("ChipmunkSpriteEngine_SP_SetBrightness", cs_src)
        self.assertNotIn("ChipmunkSpriteEngine_SP_GetBrightness", cs_src)
        fn_cs_fini = extract_function("src/hll/ChipmunkSpriteEngine.c", "ChipmunkSpriteEngine_ModuleFini")

        harness2 = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include "system4.h"
#include "system4/ain.h"

static bool keep_previous_view = false;
static bool frame_skip_all_while_msg_skip = true;
static int sact_fini_calls = 0, csm_clear_calls = 0;
static void sact_ModuleFini(void) {{ sact_fini_calls++; }}
static void CharSpriteManager_Clear(void) {{ csm_clear_calls++; }}

{fn_gui_0}
{fn_gui_1}
{fn_gui_res}
{fn_cs_fini}

int main(void) {{
    struct ain_hll_function g0 = {{ .name = "GetMessageType", .nr_arguments = 0 }};
    struct ain_hll_argument arg_int = {{ .type.data = AIN_INT }};
    struct ain_hll_function g1 = {{ .name = "GetMessageType", .nr_arguments = 1, .arguments = &arg_int }};
    struct ain_hll_argument arg_str = {{ .type.data = AIN_STRING }};
    struct ain_hll_function gbad = {{ .name = "GetMessageType", .nr_arguments = 1, .arguments = &arg_str }};
    assert(GUIEngine_ResolveGetMessageType(&g0) == (void *)GUIEngine_GetMessageType);
    assert(GUIEngine_ResolveGetMessageType(&g1) == (void *)GUIEngine_GetMessageType_idx);
    assert(GUIEngine_ResolveGetMessageType(&gbad) == NULL);
    assert(GUIEngine_GetMessageType() == 0);
    assert(GUIEngine_GetMessageType_idx(3) == 0);

    ChipmunkSpriteEngine_ModuleFini();
    assert(sact_fini_calls == 1 && csm_clear_calls == 1);
    assert(keep_previous_view == true && frame_skip_all_while_msg_skip == false);
    puts("OK");
    return 0;
}}
"""
        out2 = compile_and_run(harness2, [], ["-Wall", "-Wextra"])
        self.assertEqual(out2.strip(), "OK")

    def test_audio_mixer_parent_tree_and_kiwi_backvoice_group(self):
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "xsystem4.h"
#include "asset_manager.h"
#include "audio.h"
#include "mixer.h"
#include "system4/archive.h"
#include "system4/string.h"
#include "hll/hll.h"

struct config config;
struct ain *ain;
bool game_rance10, game_dohnadohna, game_evenicle2, game_rance03, game_ixseal,
     game_okusama, game_haharanman, game_night_remaster, game_tsumamigui3_jun,
     game_tsumamigui3_ntr, game_escalayer_reboot, game_tsumamigui3, game_maman;

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *dev, int cap, const SDL_AudioSpec *want,
                                      SDL_AudioSpec *have, int allowed) {
    (void)dev; (void)cap; (void)allowed;
    *have = *want;
    return 2;
}
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int p) { (void)dev; (void)p; }
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }
void SDL_LockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }
void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }
const char *SDL_GetError(void) { return ""; }
const char *display_utf0(const char *s) { return s ? s : ""; }
bool asset_exists(enum asset_type t, int no) { (void)t; (void)no; return false; }
bool asset_exists_by_name(enum asset_type t, const char *n, int *id) { (void)t; (void)n; (void)id; return false; }
struct archive_data *asset_get(enum asset_type t, int no) { (void)t; (void)no; return NULL; }
struct archive_data *asset_get_by_name(enum asset_type t, const char *n, int *id) { (void)t; (void)n; (void)id; return NULL; }
void static_library_replace(struct static_library *l, const char *n, void *f) { (void)l; (void)n; (void)f; }

#include "audio_mixer.c"
#include "audio.c"
#include "hll/KiwiSoundEngine.c"

static void reset_mixers(void) {
    for (int i = 0; i < nr_mixers; i++) {
        free(mixers[i].name);
        free(mixers[i].children);
    }
    free(mixers);
    mixers = master = NULL;
    nr_mixers = 0;
}

int main(void) {
    // 1. Dohna Dohna: Ambient/System/Movie/Extra must parent to Master, NOT Voice!
    static char *dd_names[] = { "Master", "BGM", "SE", "Voice", "SubVoice", "Ambient", "System", "Movie", "Extra" };
    static int dd_vols[] = { 80, 80, 80, 100, 80, 80, 80, 80, 80 };
    memset(&config, 0, sizeof(config));
    config.mixer_nr_channels = 9;
    config.mixer_channels = dd_names;
    config.mixer_volumes = dd_vols;
    game_dohnadohna = true;
    mixer_init();
    assert(master == &mixers[0]);
    for (int i = 1; i < 9; i++) {
        assert(mixers[i].parent == &mixers[0]);
    }
    assert(mixers[3].nr_children == 0);
    reset_mixers();
    game_dohnadohna = false;

    // 2. Heartful Maman: channels after Voice must parent to Master, NOT Voice!
    static char *mm_names[] = { "Master", "BGM", "SE", "Voice", "Ambient", "BackVoice", "System" };
    static int mm_vols[] = { 80, 80, 80, 100, 80, 80, 80 };
    config.mixer_nr_channels = 7;
    config.mixer_channels = mm_names;
    config.mixer_volumes = mm_vols;
    game_maman = true;
    mixer_init();
    assert(master == &mixers[0]);
    for (int i = 1; i < 7; i++) {
        assert(mixers[i].parent == &mixers[0]);
    }
    assert(mixers[3].nr_children == 0);
    reset_mixers();
    game_maman = false;

    // 3. KiwiSoundEngine_GetBackVoiceGroup for Tsumamigui 3 and Escalayer Reboot.
    game_escalayer_reboot = false;
    game_tsumamigui3 = false;
    assert(KiwiSoundEngine_GetBackVoiceGroup() == 0);
    game_tsumamigui3 = true;
    assert(KiwiSoundEngine_GetBackVoiceGroup() == 5);
    game_tsumamigui3 = false;
    game_escalayer_reboot = true;
    assert(KiwiSoundEngine_GetBackVoiceGroup() == 5);

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            [ROOT / "src/id_pool.c", ROOT / "src/audio_meta.c", *LIBSYS4_CORE],
            ["-lsndfile", "-lz", "-lm"],
        )
        self.assertEqual(out.strip(), "OK")

    def test_hacks_and_vm_zero_vector_guard(self):
        guard_fn = extract_function("src/vm.c", "vm_zero_vector_div_guard")
        harness = f"""
#define VM_PRIVATE
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xsystem4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm.h"

void apply_game_specific_hacks(struct ain *ain);
struct config config;
struct ain *ain;
struct function_call call_stack[4096];
int32_t call_stack_ptr = 0;

bool message_layout_init_rance03(struct ain *a) {{ (void)a; return false; }}
_Noreturn void _vm_error(const char *fmt, ...) {{ (void)fmt; abort(); }}

{guard_fn}

static void reset_cfg(const char *name) {{
    memset(&config, 0, sizeof(config));
    config.view_width = 800;
    config.view_height = 600;
    config.game_name = (char *)name;
}}

int main(void) {{
    // 1. ain_has_gbk_string past index 10000 (e.g. string at index 11000).
    struct ain *big = ain_new(6, 0);
    ain_add_struct(big, "CPolygonUnitObject");
    ain_add_global(big, "g_GrobalData");
    for (int i = 0; i < 11000; i++)
        ain_add_string(big, "pad");
    ain_add_string(big, "wf:-3");
    reset_cfg("BladeBriders");
    apply_game_specific_hacks(big);
    assert(game_bladebriders == true);
    ain_free(big);

    // 2. BladeBriders structural guard rejects bare string-only AIN v6.
    struct ain *bb_fake = ain_new(6, 0);
    ain_add_string(bb_fake, "wf:-3");
    reset_cfg("FakeV6");
    apply_game_specific_hacks(bb_fake);
    assert(game_bladebriders == false);
    ain_free(bb_fake);

    // 3. Haharanman structural guard: rejects string-only, accepts with g_cGameParam + g_cHeroParam.
    struct ain *hhr_fake = ain_new(14, 0);
    ain_add_string(hhr_fake, "83h2mjr");
    reset_cfg("FakeV14");
    apply_game_specific_hacks(hhr_fake);
    assert(game_haharanman == false);
    ain_add_global(hhr_fake, "g_cGameParam");
    ain_add_global(hhr_fake, "g_cHeroParam");
    apply_game_specific_hacks(hhr_fake);
    assert(game_haharanman == true);
    ain_free(hhr_fake);

    // 4. Dungeons & Dolls detected structurally via DrawDungeon2 BEFORE Rance 8 fallback.
    struct ain *dnd = ain_new(6, 0);
    ain_add_library(dnd, "DrawDungeon2");
    ain_add_library(dnd, "TapirEngine");
    ain_add_library(dnd, "MonsterInfo");
    ain_add_library(dnd, "GoatGUIEngine");
    reset_cfg("Renamed Dungeons");
    apply_game_specific_hacks(dnd);
    assert(game_dungeons_and_dolls == true);
    assert(game_rance8 == false);
    ain_free(dnd);

    // 5. Dual SJIS/GBK game_sengokurance detection.
    struct ain *sr_sjis = ain_new(5, 0);
    ain_add_struct(sr_sjis, "s\\x83L\\x83\\x83\\x83\\x89");
    ain_add_global(sr_sjis, "g_sBattle");
    ain_add_global(sr_sjis, "\\x81\\x9f\\x8b\\xe0");
    reset_cfg("Sengoku");
    apply_game_specific_hacks(sr_sjis);
    assert(game_sengokurance == true);
    ain_free(sr_sjis);

    struct ain *sr_gbk = ain_new(5, 0);
    ain_add_struct(sr_gbk, "s\\xa5\\xad\\xa5\\xe3\\xa5\\xe9");
    ain_add_global(sr_gbk, "g_sBattle");
    ain_add_global(sr_gbk, "\\xa1\\xf4\\xbd\\xf0");
    reset_cfg("SengokuCN");
    apply_game_specific_hacks(sr_gbk);
    assert(game_sengokurance == true);
    ain_free(sr_gbk);

    // 6. Dual SJIS/GBK game_rance02 + structural game_rance02_mg + "Rance 02" sets game_rance02.
    struct ain *r02_gbk = ain_new(6, 0);
    ain_add_struct(r02_gbk, "\\xa5\\xad\\xa5\\xe3\\xa5\\xe9\\xa5\\xc7\\xa9\\x60\\xa5\\xbf");
    ain_add_global(r02_gbk, "\\xca\\xc0\\xc9\\xab\\xb0\\xa9");
    ain_add_global(r02_gbk, "\\xa5\\xab\\xa5\\xeb\\xa5\\xde");
    ain_add_function(r02_gbk, "dummy0");
    int cw_fno = ain_add_function(r02_gbk, "_CalculateWidth");
    r02_gbk->code_size = 256;
    r02_gbk->code = calloc(1, r02_gbk->code_size);
    r02_gbk->functions[cw_fno].address = 6;
    reset_cfg("ModdedName");
    apply_game_specific_hacks(r02_gbk);
    assert(game_rance02 == true);
    assert(game_rance02_mg == true);
    ain_free(r02_gbk);

    struct ain *r02_name = ain_new(6, 0);
    reset_cfg("Rance 02");
    apply_game_specific_hacks(r02_name);
    assert(game_rance02 == true);
    assert(game_rance02_mg == false);
    ain_free(r02_name);

    // 7. game_okusama_ntr SJIS form AND state reset across sessions.
    struct ain *oku_jp = ain_new(14, 0);
    ain_add_function(oku_jp, "AdvGauge@UpdatePregnancy");
    ain_add_struct(oku_jp, "CharaStatusList");
    ain_add_global(oku_jp, "g_resources");
    ain_add_function(oku_jp, "\\x82\\x64\\x82\\x75\\x81\\x5e\\x82\\x6d\\x82\\x73\\x82\\x71\\x81\\x5e\\x92\\x45\\x8f\\x88\\x81\\x5e\\x82\\x50");
    reset_cfg("OkusamaJP");
    apply_game_specific_hacks(oku_jp);
    assert(game_okusama == true && game_okusama_ntr == true);
    ain_free(oku_jp);

    // Next session with neutral AIN must reset game_okusama_ntr, game_rance01, game_rance02, game_dungeons_and_dolls.
    struct ain *neutral = ain_new(6, 0);
    // Also test GBK marker "粉彩铃音" enables GBK mode when text_enc is unknown.
    reset_cfg("\\xb7\\xdb\\xb2\\xca\\xc1\\xe5\\xd2\\xf4");
    apply_game_specific_hacks(neutral);
    assert(game_okusama == false && game_okusama_ntr == false);
    assert(game_rance01 == false && game_rance02 == false && game_rance02_mg == false);
    assert(game_dungeons_and_dolls == false && game_evenicle_rance_cn == false);
    assert(sys4_text_is_gbk == true);
    ain_free(neutral);

    // 8. vm_zero_vector_div_guard BladeBriders methods.
    struct ain *vm_ain = ain_new(6, 0);
    int f_other = ain_add_function(vm_ain, "Other@Func");
    int f_p = ain_add_function(vm_ain, "CParticleObject@SetDirection");
    int f_poly = ain_add_function(vm_ain, "CPolygonUnitObject@GetDirectionAngle");
    int f_bb = ain_add_function(vm_ain, "CBillBoardUnitObject@GetDirectionAngle");
    ain = vm_ain;
    call_stack_ptr = 1;
    call_stack[0].fno = f_other;
    assert(!vm_zero_vector_div_guard());
    call_stack[0].fno = f_p;
    assert(vm_zero_vector_div_guard());
    call_stack[0].fno = f_poly;
    assert(vm_zero_vector_div_guard());
    call_stack[0].fno = f_bb;
    assert(vm_zero_vector_div_guard());
    ain_free(vm_ain);

    puts("OK");
    return 0;
}}
"""
        out = compile_and_run(
            harness,
            [ROOT / "src/hacks.c", ROOT / "src/escalayer_reboot_cn_ui.c", *LIBSYS4_CORE],
            ["-lz", "-lm"],
        )
        self.assertEqual(out.strip(), "OK")

    def test_savedir_lookup_cache_and_invalidation(self):
        with tempfile.TemporaryDirectory() as tmp:
            save_dir = Path(tmp) / "SaveData"
            save_dir.mkdir()
            for name in ("Save001.asd", "Save002.asd", "Save003.asd", "Save100.asd", "Save200.asd", "SystemData.asd"):
                (save_dir / name).write_bytes(b"ASD")

            harness = r"""
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "xsystem4.h"
#include "system4/file.h"

struct config config;
struct ain *ain;
bool game_dohnadohna = true;

static int g_opendir_calls = 0;
static bool g_fail_opendir_eacces = false;
DIR *__real_opendir(const char *name);
DIR *__wrap_opendir(const char *name) {
    g_opendir_calls++;
    if (g_fail_opendir_eacces) {
        errno = EACCES;
        return NULL;
    }
    return __real_opendir(name);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    memset(&config, 0, sizeof(config));
    config.save_dir = argv[1];

    // 1. Simulate Dohna Dohna's 1,200 synchronous ExistSaveFile calls (600 slots x 2):
    //    must perform strictly ONE opendir() across all 1,200 queries!
    g_opendir_calls = 0;
    int found = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int slot = 0; slot < 600; slot++) {
            char name[32];
            if (pass == 1 && slot == 1)
                snprintf(name, sizeof(name), "save%03d.ASD", slot); // mixed-case icase lookup
            else
                snprintf(name, sizeof(name), "Save%03d.asd", slot);
            bool ex = savedir_file_exists(name);
            bool expect = (slot == 1 || slot == 2 || slot == 3 || slot == 100 || slot == 200);
            assert(ex == expect);
            if (ex) found++;
        }
    }
    assert(found == 10);
    assert(g_opendir_calls == 1);

    // 2. savedir_path() icase resolution hits the cached snapshot with 0 extra opendir() calls.
    char *p1 = savedir_path("save001.ASD");
    assert(p1 != NULL && strstr(p1, "Save001.asd") != NULL);
    free(p1);
    assert(g_opendir_calls == 1);

    // 3. In-process write via file_open_utf8 invalidates immediately (0ms wait).
    char *p50 = savedir_path("Save050.asd");
    FILE *fp = file_open_utf8(p50, "wb");
    assert(fp != NULL);
    fwrite("OK", 1, 2, fp);
    fclose(fp);
    assert(savedir_file_exists("Save050.asd") == true);
    assert(g_opendir_calls == 2);
    assert(savedir_file_exists("Save051.asd") == false);
    assert(g_opendir_calls == 2);

    // 4. In-process delete via remove_utf8 invalidates immediately.
    assert(remove_utf8(p50) == 0);
    free(p50);
    assert(savedir_file_exists("Save050.asd") == false);
    assert(g_opendir_calls == 3);

    // 5. External unlink() inside TTL window is immediately caught on cache hit and invalidates!
    char *p2 = savedir_path("Save002.asd");
    assert(unlink(p2) == 0);
    free(p2);
    assert(savedir_file_exists("Save002.asd") == false);
    assert(savedir_file_exists("Save003.asd") == true);
    assert(g_opendir_calls == 4);

    // 6. Explicit savedir_cache_invalidate() refreshes cleanly.
    savedir_cache_invalidate();
    assert(savedir_file_exists("SystemData.asd") == true);
    assert(g_opendir_calls == 5);

    // 7. Unlistable (traverse-only / EACCES on opendir) directory falls back to direct file_exists()
    //    so known save paths are NOT falsely reported as missing!
    savedir_cache_invalidate();
    g_fail_opendir_eacces = true;
    assert(savedir_file_exists("Save001.asd") == true);
    assert(savedir_file_exists("Save999.asd") == false);
    g_fail_opendir_eacces = false;
    savedir_cache_invalidate();
    assert(savedir_file_exists("Save001.asd") == true);

    puts("OK");
    return 0;
}
"""
            out = compile_and_run(
                harness,
                [ROOT / "src/util.c", *LIBSYS4_CORE],
                ["-Wl,--wrap=opendir", "-lz", "-lm"],
                [str(save_dir)],
            )
            self.assertEqual(out.strip(), "OK")


if __name__ == "__main__":
    unittest.main()
