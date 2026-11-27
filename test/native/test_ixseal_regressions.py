#!/usr/bin/env python3
"""Host C regression tests for Beat Valkyrie Ixseal (AIN v11 + CN) compatibility fixes."""

import re
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def _ex_encode_tables():
    decode = [0] * 256
    inv = [0] * 256
    for i in range(256):
        t = i
        bc = bin(i).count("1")
        if bc % 2 == 0:
            t = ((t << (8 - bc)) | (t >> bc)) & 0xFF
        else:
            t = ((t >> (8 - bc)) | (t << bc)) & 0xFF
        decode[i] = t
        inv[t] = i
    return bytes(inv)


_EX_ENC = _ex_encode_tables()


def _gbk(text: str) -> bytes:
    return text.encode("gbk")


def _ex_pstr(b: bytes) -> bytes:
    return struct.pack("<i", len(b)) + b


def _ex_leaf(name: bytes, val_type: int, payload: bytes) -> bytes:
    body = _ex_pstr(name) + payload
    return (
        _ex_pstr(name)
        + struct.pack("<II", 1, val_type)
        + struct.pack("<I", len(body))
        + body
        + struct.pack("<I", 0)
    )


def _ex_tree(name: bytes, children: list[bytes]) -> bytes:
    out = bytearray(_ex_pstr(name) + struct.pack("<II", 0, len(children)))
    for c in children:
        out.extend(c)
    return bytes(out)


def _ex_list(items: list[tuple[int, bytes]]) -> bytes:
    out = bytearray(struct.pack("<I", len(items)))
    for val_type, payload in items:
        out.extend(struct.pack("<II", val_type, len(payload)))
        out.extend(payload)
    return bytes(out)


def _ex_table(
    fields: list[tuple[int, bytes, int]],
    rows: list[list[tuple[int, bytes]]],
) -> bytes:
    out = bytearray(struct.pack("<I", len(fields)))
    for ft, fname, is_index in fields:
        out.extend(struct.pack("<I", ft))
        out.extend(_ex_pstr(fname))
        out.extend(struct.pack("<II", 0, is_index))
    out.extend(struct.pack("<II", len(fields), len(rows)))
    for row in rows:
        for cell_type, payload in row:
            out.extend(struct.pack("<I", cell_type))
            out.extend(payload)
    return bytes(out)


def _ex_block(name: bytes, val_type: int, payload: bytes) -> bytes:
    body = _ex_pstr(name) + payload
    return struct.pack("<II", val_type, len(body)) + body


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


def extract_function(rel_path: str, name: str) -> str:
    source = (ROOT / rel_path).read_text(encoding="utf-8")
    pattern = re.compile(
        rf"^(?:static\s+)?(?:inline\s+)?[A-Za-z_][A-Za-z0-9_\s\*]*\b{re.escape(name)}\([^;{{}}]*\)\s*\r?\n?\{{",
        re.MULTILINE,
    )
    match = pattern.search(source)
    if not match:
        raise ValueError(f"Function definition not found: {name} in {rel_path}")
    start = match.start()
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def sdl_cflags() -> list[str]:
    return subprocess.check_output(["pkg-config", "--cflags", "sdl2"], text=True).split()


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


class TestIxsealRegressions(unittest.TestCase):
    def test_hacks_gbk_detection_resolution_and_version_text(self):
        fn_gvt = extract_function("src/hll/SystemService.c", "game_version_text")
        fn_gvbt = extract_function("src/hll/SystemService.c", "SystemService_GetGameVersionByText")
        harness = f"""
#include <assert.h>
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
void message_layout_init_rance03(void) {{}}
_Noreturn void _vm_error(const char *fmt, ...) {{ abort(); }}

{fn_gvt}
{fn_gvbt}

static int part_int(const char *s, int start, int len) {{
    char buf[16] = {{0}};
    memcpy(buf, s + start, len);
    return atoi(buf);
}}

int main(void) {{
    // 1. CN Ixseal: v11 + TrainingSelect::C_EXECUTE, no JP P_ActivityExecute
    struct ain *cn_ain = ain_new(11, 0);
    ain_add_struct(cn_ain, "TrainingSelect::C_EXECUTE");
    config.game_name = strdup("\\xb3\\xac\\xb0\\xba\\xc9\\xf1\\xc6\\xef"); // GBK "超昂神骑"
    config.view_width = 800;
    config.view_height = 600;
    apply_game_specific_hacks(cn_ain);
    assert(game_ixseal == true);
    assert(sys4_text_is_gbk == true);
    assert(config.view_width == 1280);
    assert(config.view_height == 720);

    // Explicit viewport preserved
    config.view_width = 1600;
    config.view_height = 900;
    apply_game_specific_hacks(cn_ain);
    assert(config.view_width == 1600 && config.view_height == 900);

    // SystemService.GetGameVersionByText() satisfies all three main() assertions
    struct string *v = SystemService_GetGameVersionByText();
    assert(v && v->size >= 28);
    assert(part_int(v->text, 5, 1) == part_int(v->text, 27, 1));
    assert(part_int(v->text, 4, 1) == part_int(v->text, 13, 1));
    char combined[32] = {{0}};
    memcpy(combined + 0, v->text + 6, 3);
    memcpy(combined + 3, v->text + 10, 2);
    memcpy(combined + 5, v->text + 22, 3);
    memcpy(combined + 8, v->text + 15, 4);
    assert(strcmp(combined, "es34fs68g3q2") == 0);
    free_string(v);

    // Custom GameName still detects GBK for CN Ixseal
    free(config.game_name);
    config.game_name = strdup("Ixseal Custom");
    sys4_set_text_gbk(false);
    apply_game_specific_hacks(cn_ain);
    assert(game_ixseal == true);
    assert(sys4_text_is_gbk == true);
    ain_free(cn_ain);

    // 2. JP Ixseal keeps SJIS (sys4_text_is_gbk == false) while still enabling 1280x720
    struct ain *jp_ain = ain_new(11, 0);
    ain_add_struct(jp_ain, "TrainingSelect::C_EXECUTE");
    ain_add_function(jp_ain, "\\x82\\x6f\\x81\\x51\\x83\\x41\\x83\\x4e\\x83\\x65\\x83\\x42\\x83\\x72\\x83\\x65\\x83\\x42\\x8e\\xc0\\x8d\\x73");
    free(config.game_name);
    config.game_name = strdup("\\x92\\xb4\\x8d\\x82\\x90\\x5f\\x8b\\x52"); // SJIS
    config.view_width = 800;
    config.view_height = 600;
    sys4_set_text_gbk(false);
    apply_game_specific_hacks(jp_ain);
    assert(game_ixseal == true);
    assert(sys4_text_is_gbk == false);
    assert(config.view_width == 1280 && config.view_height == 720);
    ain_free(jp_ain);

    // 3. Non-v11 does not set game_ixseal
    struct ain *v12 = ain_new(12, 0);
    ain_add_struct(v12, "TrainingSelect::C_EXECUTE");
    apply_game_specific_hacks(v12);
    assert(game_ixseal == false);
    ain_free(v12);

    free(config.game_name);
    puts("OK");
    return 0;
}}
"""
        out = compile_and_run(harness, [ROOT / "src/hacks.c", ROOT / "src/escalayer_reboot_cn_ui.c", *LIBSYS4_CORE], ["-lz", "-lm"])
        self.assertEqual(out.strip(), "OK")

    def test_mainexfile_and_afafactory_v11_prelink(self):
        main_ex = build_extf([
            _ex_block(b"ScoreVal", 1, struct.pack("<i", 42)),
        ])
        with tempfile.TemporaryDirectory() as tmpdir:
            ex_path = Path(tmpdir) / "Ixseal.ex"
            ex_path.write_bytes(main_ex)
            harness = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xsystem4.h"
#include "hll/hll.h"
#include "asset_manager.h"
#include "reign.h"
#include "system4/ain.h"
#include "system4/archive.h"
#include "system4/ex.h"
#include "system4/string.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "vm/v12.h"

struct config config;
struct ain *ain = NULL;
struct vm_pointer *heap = NULL;
size_t heap_size = 0;
bool game_evenicle2 = false;
bool game_okusama = false;
bool game_haharanman = false;

extern struct static_library lib_MainEXFile;
extern struct static_library lib_AFAFactory;

static void *find_exported(struct static_library *lib, const char *name) {
    for (int i = 0; lib->functions[i].name; i++) {
        if (!strcmp(lib->functions[i].name, name))
            return lib->functions[i].fun;
    }
    return NULL;
}

void static_library_replace(struct static_library *lib, const char *name, void *fun) {
    for (int i = 0; lib->functions[i].name; i++) {
        if (!strcmp(lib->functions[i].name, name)) {
            lib->functions[i].fun = fun;
            return;
        }
    }
}

void library_export_v12(struct static_library *lib, const struct static_hll_function *funs, size_t n) {
    // Allow MainEXFile_PreLink's mainex_v12_prelink() to update existing static_library table or record replacements
}
struct page *v12_array_new(const struct ain_type *type, int size, bool init) { return NULL; }
struct page *alloc_array(int rank, union vm_value *dimensions, enum ain_data_type type, int struc, bool init) { return NULL; }
void delete_page_vars(struct page *p) {}
void free_page(struct page *p) {}
char *gamedir_path(const char *p) { return strdup(p); }
char *gamedir_path_icase(const char *p) { return strdup(p); }
struct archive *RE_open_3d_archive(void) { return NULL; }
struct archive_data *RE_archive_find_txtex(struct archive *ar, const char *model_dir) { return NULL; }
struct archive *asset_archive_by_type(enum asset_type type) { return NULL; }
bool asset_manager_load_archive(enum asset_type type, const char *archive_name) { return false; }
void asset_foreach_file(enum asset_type t, void (*cb)(struct archive_data*, void*), void *u) {}
const char *display_utf0(const char *s) { return s ? s : ""; }

static void add_hll_fun(struct ain_library *lib, const char *name, enum ain_data_type ret, int nargs, const enum ain_data_type *args) {
    lib->functions = xrealloc_array(lib->functions, lib->nr_functions, lib->nr_functions + 1, sizeof(struct ain_hll_function));
    struct ain_hll_function *f = &lib->functions[lib->nr_functions++];
    memset(f, 0, sizeof(*f));
    f->name = strdup(name);
    f->return_type.data = ret;
    f->nr_arguments = nargs;
    if (nargs > 0) {
        f->arguments = xcalloc(nargs, sizeof(struct ain_hll_argument));
        for (int i = 0; i < nargs; i++)
            f->arguments[i].type.data = args[i];
    }
}

int main(int argc, char **argv) {
    assert(argc == 2);
    config.ex_path = argv[1];

    // Mock v11 AIN with v12-style MainEXFile & AFAFactory signatures
    ain = ain_new(11, 0);
    ain->libraries = xcalloc(2, sizeof(struct ain_library));
    ain->nr_libraries = 2;
    ain->libraries[0].name = strdup("MainEXFile");
    ain->libraries[1].name = strdup("AFAFactory");

    enum ain_data_type add_args[] = { AIN_STRING };
    enum ain_data_type row_args[] = { AIN_STRING, AIN_INT };
    enum ain_data_type afa_args[] = { AIN_INT, AIN_INT };
    add_hll_fun(&ain->libraries[0], "AddEXReader", AIN_INT, 1, add_args);
    add_hll_fun(&ain->libraries[0], "Row", AIN_INT, 2, row_args);
    add_hll_fun(&ain->libraries[1], "GetTitleByIndex", AIN_STRING, 2, afa_args);
    add_hll_fun(&ain->libraries[1], "GetSearchTitleByIndex", AIN_STRING, 2, afa_args);

    void (*mainex_prelink)(void) = find_exported(&lib_MainEXFile, "_PreLink");
    void (*afa_prelink)(void) = find_exported(&lib_AFAFactory, "_PreLink");
    assert(mainex_prelink && afa_prelink);

    void *orig_row = find_exported(&lib_MainEXFile, "Row");
    void *orig_afa_title = find_exported(&lib_AFAFactory, "GetTitleByIndex");
    void *orig_afa_search = find_exported(&lib_AFAFactory, "GetSearchTitleByIndex");

    mainex_prelink();
    afa_prelink();

    // Must have swapped Row/AddEXReader (v12) and string-returning AFAFactory getters for v11!
    assert(find_exported(&lib_MainEXFile, "Row") != orig_row);
    assert(find_exported(&lib_AFAFactory, "GetTitleByIndex") != orig_afa_title);
    assert(find_exported(&lib_AFAFactory, "GetSearchTitleByIndex") != orig_afa_search);

    void (*init_fn)(void) = find_exported(&lib_MainEXFile, "_ModuleInit");
    int (*int_fn)(struct string *, int, int) = find_exported(&lib_MainEXFile, "Int");
    assert(init_fn && int_fn);
    init_fn();
    struct string *s_key = make_string("ScoreVal", 8);
    assert(int_fn(s_key, 0, 0) == 42);
    free_string(s_key);

    // Calling v11 GetTitleByIndex must return an owned string directly (not treat first arg as pointer)
    struct string *(*get_title)(int, int) = find_exported(&lib_AFAFactory, "GetTitleByIndex");
    struct string *t = get_title(0, 999);
    assert(t && t->size == 0);
    free_string(t);
    t = get_title(2, 999);
    assert(t && t->size == 0);
    free_string(t);

    puts("OK");
    return 0;
}
"""
            # Extract static_library_replace tracking or use MainEXFile_v12.c's real mainex_v12_prelink:
            # MainEXFile.c and AFAFactory.c export non-const struct static_library; static_library_replace mutates fun pointer.
            # Notice mainex_v12_prelink fills lib_MainEXFile directly when using ffi helper; verify with ffi.c's static_library_replace pattern!
            harness_clean = harness.replace(
                "void library_export_v12(struct static_library *lib, const struct static_hll_function *funs, size_t n) {",
                "static void noop_unused(void) { (void)0; } void dummy_unused(void) {"
            )
            out = compile_and_run(
                harness_clean,
                [
                    ROOT / "src/hll/MainEXFile.c",
                    ROOT / "src/hll/MainEXFile_v12.c",
                    ROOT / "src/hll/AFAFactory.c",
                    *LIBSYS4_CORE,
                ],
                ["-lz", "-lm"],
                [str(ex_path)],
            )
            self.assertEqual(out.strip(), "OK")

    def test_textsurfacemanager_v11_stateful_prelink(self):
        harness = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xsystem4.h"
#include "gfx/font.h"
#include "hll/hll.h"
#include "system4/ain.h"
#include "system4/string.h"

struct ain *ain = NULL;
extern struct static_library lib_TextSurfaceManager;

static int last_face = -1;
static float last_size = -1.f;
void gfx_font_init(void) {}
float gfx_size_char_unstyled(struct text_style *ts, const char *text) {
    last_face = ts->face;
    last_size = ts->size;
    return ts->size * 0.5f;
}

static void *find_exported(struct static_library *lib, const char *name) {
    for (int i = 0; lib->functions[i].name; i++) {
        if (!strcmp(lib->functions[i].name, name))
            return lib->functions[i].fun;
    }
    return NULL;
}

void static_library_replace(struct static_library *lib, const char *name, void *fun) {
    for (int i = 0; lib->functions[i].name; i++) {
        if (!strcmp(lib->functions[i].name, name)) {
            lib->functions[i].fun = fun;
            return;
        }
    }
}

int main(void) {
    ain = ain_new(11, 0);
    ain->libraries = xcalloc(1, sizeof(struct ain_library));
    ain->nr_libraries = 1;
    ain->libraries[0].name = strdup("TextSurfaceManager");
    ain->libraries[0].nr_functions = 1;
    ain->libraries[0].functions = xcalloc(1, sizeof(struct ain_hll_function));
    ain->libraries[0].functions[0].name = strdup("GetFontWidth");
    ain->libraries[0].functions[0].nr_arguments = 2;

    void (*prelink)(void) = find_exported(&lib_TextSurfaceManager, "_PreLink");
    void (*set_type)(int) = find_exported(&lib_TextSurfaceManager, "SetFontType");
    void (*set_size)(int) = find_exported(&lib_TextSurfaceManager, "SetFontSize");
    void (*set_color)(int, int, int) = find_exported(&lib_TextSurfaceManager, "SetFontColor");
    void (*set_bold)(float) = find_exported(&lib_TextSurfaceManager, "SetFontBoldWeight");
    void (*set_edge)(float) = find_exported(&lib_TextSurfaceManager, "SetFontEdgeWeight");
    void (*set_edge_color)(int, int, int) = find_exported(&lib_TextSurfaceManager, "SetFontEdgeColor");
    assert(prelink && set_type && set_size && set_color && set_bold && set_edge && set_edge_color);

    void *v12_get_width = find_exported(&lib_TextSurfaceManager, "GetFontWidth");
    prelink();
    bool (*v11_get_width)(struct string *, int *) = find_exported(&lib_TextSurfaceManager, "GetFontWidth");
    assert((void *)v11_get_width != v12_get_width);

    set_type(256);
    set_size(24);
    set_color(255, 255, 255);
    set_bold(1.2f);
    set_edge(2.5f);
    set_edge_color(0, 0, 0);

    int w = -1;
    struct string *ch = make_string("A", 1);
    assert(v11_get_width(ch, &w) == true);
    assert(last_face == 256 && last_size == 24.f);
    // ceilf(24 * 0.5) = 12, + 2 * ceilf(2.5) = 12 + 6 = 18
    assert(w == 18);
    free_string(ch);

    struct string *empty = make_string("", 0);
    assert(v11_get_width(empty, &w) == false);
    assert(w == 0);
    free_string(empty);

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(harness, [ROOT / "src/hll/TextSurfaceManager.c", *LIBSYS4_CORE], ["-lz", "-lm"])
        self.assertEqual(out.strip(), "OK")

    def test_kiwi_ixseal_18_mixers_and_non_nested_voice(self):
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
bool game_rance03 = false;
bool game_ixseal = true;
bool game_okusama = false;
bool game_haharanman = false;
bool game_night_remaster = false;
bool game_escalayer_reboot, game_tsumamigui3;
bool game_tsumamigui3_jun = false;
bool game_tsumamigui3_ntr = false;

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

SNDFILE *sf_open(const char *p, int m, SF_INFO *i) { return NULL; }
SNDFILE *sf_open_virtual(SF_VIRTUAL_IO *v, int m, SF_INFO *i, void *u) { return NULL; }
int sf_close(SNDFILE *s) { return 0; }
int sf_error(SNDFILE *s) { return 0; }
const char *sf_strerror(SNDFILE *s) { return ""; }
sf_count_t sf_seek(SNDFILE *s, sf_count_t f, int w) { return 0; }
sf_count_t sf_readf_float(SNDFILE *s, float *p, sf_count_t f) { return 0; }

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
    memset(&config, 0, sizeof(config));
    config.default_volume = 100;
    kiwi_prelink();
    assert(config.mixer_nr_channels == 18);
    mixer_init();

    assert(mixer_get_numof() == 18);
    assert(strcmp(mixer_get_name(0), "Master") == 0);
    assert(strcmp(mixer_get_name(3), "Voice") == 0);
    assert(strcmp(mixer_get_name(5), "BackVoice") == 0);
    assert(strcmp(mixer_get_name(17), "BackVoiceChara6") == 0);

    int vol = -1;
    assert(mixer_get_volume(0, &vol) && vol == 80);
    assert(mixer_get_volume(3, &vol) && vol == 100);
    assert(mixer_get_volume(5, &vol) && vol == 18);
    assert(mixer_get_volume(17, &vol) && vol == 80);

    assert(mixer_set_volume(5, 45));
    assert(mixer_get_volume(5, &vol) && vol == 45);
    assert(mixer_set_mute(3, 1));
    assert(mixer_set_mute(5, 0));

    // Channels after Voice (e.g. BackVoice [5] and BackVoiceChara6 [17]) must stay children of Master, not Voice.
    assert(mixers[3].nr_children == 0);
    assert(mixers[5].parent == master);
    assert(mixers[17].parent == master);

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            LIBSYS4_CORE,
            ["-lz", "-lm"],
        )
        self.assertEqual(out.strip(), "OK")

    def test_vm_v11_hll_func_71_and_string_erase(self):
        fn_cb = extract_function("src/vm.c", "is_callback_binding_op")
        fn_is_hll = extract_function("src/vm_v12.c", "is_hll_func_type")
        vm_v12_src = (ROOT / "src/vm_v12.c").read_text(encoding="utf-8")
        assert "is_hll_func_type(f->arguments[1].type.data)" in vm_v12_src
        assert "is_hll_func_type(f->arguments[arg].type.data)" in vm_v12_src
        assert "int count = f->nr_arguments >= 3 ? args[2].i : 1;" in vm_v12_src

        harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "system4/ain.h"
#include "system4/little_endian.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

static struct ain *ain = NULL;
{fn_is_hll}

#define RETURN 0
#define CALLFUNC 1
#define CALLMETHOD 2
#define CALLHLL 3
#define DG_CALL 4
#define DG_CALLBEGIN 5
#define DG_SET 6
#define DG_ADD 7
#define DG_EXIST 8
#define DG_ERASE 9
#define CALLFUNC2 10

{fn_cb}

int main(void) {{
    assert(is_hll_func_type(AIN_HLL_FUNC));
    assert(is_hll_func_type(AIN_HLL_FUNC_71));
    assert(!is_hll_func_type(AIN_INT));

    // is_callback_binding_op must recognize CALLHLL with AIN_HLL_FUNC_71 callback
    uint8_t code[16] = {{0}};
    LittleEndian_putW(code, 0, CALLHLL);
    LittleEndian_putDW(code, 2, 0);
    LittleEndian_putDW(code, 6, 0);
    struct ain_hll_argument args[2] = {{{{.type = {{.data = AIN_REF_ARRAY}}}}, {{.type = {{.data = AIN_HLL_FUNC_71}}}}}};
    struct ain_hll_function hf = {{.name = "Sort", .nr_arguments = 2, .arguments = args}};
    struct ain_library lib = {{.name = "Array", .nr_functions = 1, .functions = &hf}};
    struct ain mock_ain = {{.code = code, .code_size = sizeof(code), .nr_libraries = 1, .libraries = &lib}};
    ain = &mock_ain;
    assert(is_callback_binding_op(CALLHLL, 0) == true);

    // Verify 2-arg String.Erase erases 1 character in GBK mode
    sys4_set_text_gbk(true);
    struct string *s = make_string("\\xb3\\xac" "AB", 4); // "超AB"
    int nargs = 2;
    int arg2 = 0;
    int count = nargs >= 3 ? arg2 : 1;
    for (int i = 0; i < count; i++)
        string_erase(&s, 1);
    assert(s->size == 3 && strcmp(s->text, "\\xb3\\xac" "B") == 0);
    free_string(s);

    puts("OK");
    return 0;
}}
"""
        out = compile_and_run(harness, LIBSYS4_CORE, ["-lz", "-lm"])
        self.assertEqual(out.strip(), "OK")

    def test_parts_v11_ixseal_prelink_messages_drag_components_and_cn_pact(self):
        # 1. Verify v11 message/input/PartsEngine prelink behavior against real source checks + PACT loader
        parts_eng_src = (ROOT / "src/hll/PartsEngine.c").read_text(encoding="utf-8")
        self.assertIn("if (ain->version >= 11)", parts_eng_src)
        self.assertIn('"GetActivityIntentDataDestination"', parts_eng_src)
        input_src = (ROOT / "src/parts/input.c").read_text(encoding="utf-8")
        self.assertIn('parts_msg_push(drag_parts, PARTS_MSG_DRAG_END, "i", VK_LBUTTON);', input_src)
        msg_src = (ROOT / "src/parts/message.c").read_text(encoding="utf-8")
        self.assertIn("return msg ? msg->type : (ain->version >= 8 ? -1 : 0);", msg_src)

        def cn_state_node(kind: str, extra: list[bytes] | None = None) -> bytes:
            kids = [_ex_leaf(_gbk("パーツタイプ"), 3, _ex_pstr(_gbk(kind)))]
            if extra:
                kids.extend(extra)
            return _ex_tree(_gbk("通常状态"), kids)

        def cn_part_node(
            name: str,
            kind: str,
            info_children: list[bytes],
            children_list: list[bytes] | None = None,
            children_key: str = "子パーツリスト",
        ) -> bytes:
            info = [_ex_leaf(_gbk("パーツタイプ"), 3, _ex_pstr(_gbk(kind))), *info_children]
            fields = [
                _ex_leaf(_gbk("名前"), 3, _ex_pstr(_gbk(name))),
                _ex_tree(_gbk("种类别情报"), info),
            ]
            if children_list is not None:
                fields.append(_ex_tree(_gbk(children_key), children_list))
            return _ex_tree(_gbk(name), fields)

        root_part = cn_part_node(
            "fg",
            "フォームグループ",
            [],
            children_list=[
                cn_part_node(
                    "txt",
                    "低レベルパーツ",
                    [
                        cn_state_node(
                            "テキストパーツ",
                            [
                                _ex_tree(_gbk("テキスト装饰"), [_ex_leaf(_gbk("フォント缘取り"), 2, struct.pack("<f", 1.5))]),
                                _ex_tree(_gbk("ルビ装饰"), []),
                                _ex_leaf(_gbk("テキスト"), 3, _ex_pstr(_gbk("超昂神骑"))),
                                _ex_leaf(_gbk("一文字边りの表示时间"), 1, struct.pack("<i", 20)),
                            ],
                        ),
                    ],
                ),
                cn_part_node(
                    "cp",
                    "低レベルパーツ",
                    [
                        cn_state_node(
                            "构筑パーツ",
                            [
                                _ex_tree(_gbk("手顺リスト"), []),
                            ],
                        ),
                    ],
                ),
                cn_part_node("vsb", "纵スクロールバー", []),
                cn_part_node(
                    "mw",
                    "文本窗口",
                    [
                        _ex_leaf(_gbk("文本番号"), 1, struct.pack("<i", 7)),
                    ],
                ),
            ],
        )
        pact_ex = build_extf([
            _ex_block(b"PactData", 6, _ex_tree(b"PactData", [root_part])),
        ])

        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xsystem4.h"
#include "asset_manager.h"
#include "parts.h"
#include "parts_activity.h"
#include "parts_message_window.h"
#include "parts_widgets.h"
#include "parts_internal.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "system4/ex.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

struct ain *ain = NULL;
struct vm_pointer *heap = NULL;
size_t heap_size = 0;
bool parts_v2 = true;

#include <stdarg.h>
_Noreturn void _vm_error(const char *fmt, ...) { abort(); }

const char *parts_utf8_to_cjk_literal(const char *literal) {
    if (!literal || !literal[0])
        return "";
    static char bufs[32][256];
    static int next = 0;
    char *enc = utf2sjis(literal, strlen(literal));
    char *slot = bufs[next++ % 32];
    snprintf(slot, 256, "%s", enc);
    free(enc);
    return slot;
}

static int got_special_10 = 0, got_scrollbar = 0, got_cp = 0, got_text = 0, got_child_parent = 0;
static struct parts fake_parts[32];
static struct parts_text dummy_texts[32];
static struct parts_construction_process dummy_cp[32];
static int next_pno = 1;

struct page *array_pushback(struct page *dst, union vm_value v, enum ain_data_type dt, int st) { return dst; }
bool asset_exists_by_name(enum asset_type t, const char *n, int *id) { return false; }
struct archive_data *asset_get_by_name(enum asset_type t, const char *n, int *id_out) { return NULL; }
const char *display_sjis0(const char *s) { return s ? s : ""; }
char *gamedir_path_icase(const char *p) { return NULL; }
void iarray_write(struct iarray_writer *w, int v) {}
void iarray_write_string(struct iarray_writer *w, struct string *s) {}
int iarray_read(struct iarray_reader *r) { return 0; }
struct string *iarray_read_string(struct iarray_reader *r) { return NULL; }
int mainex_v12_register(struct ex *e) { return 2; }
int mainex_v12_register_at(int i, struct ex *e) { return 2; }
void mainex_v12_unregister_activity(int i) {}
int PE_GetFreeNumber(void) { return next_pno++; }
struct parts *parts_get(int no) {
    struct parts *p = &fake_parts[no];
    p->no = no;
    return p;
}
struct parts *parts_try_get(int no) { return (no > 0 && no < next_pno) ? &fake_parts[no] : NULL; }
void parts_release(int no) {}
void parts_state_reset(struct parts_state *s, enum parts_type t) {}
void parts_set_dims(struct parts *p, struct parts_common *c, int w, int h) {}
void parts_set_scale_x(struct parts *p, float s) {}
void parts_set_scale_y(struct parts *p, float s) {}
struct parts_cg *parts_get_cg(struct parts *p, int s) { return NULL; }
struct parts_construction_process *parts_get_construction_process(struct parts *p, int s) {
    got_cp++;
    return &dummy_cp[p->no];
}
struct parts_text *parts_get_text(struct parts *p, int s) { return &dummy_texts[p->no]; }
void parts_message_window_create(struct parts *p) { p->component_type = 10; got_special_10++; }
void parts_message_window_free(struct parts *p) {}
void parts_panel_create(struct parts *p) {}
void parts_panel_free(struct parts *p) {}
void parts_checkbox_create(struct parts *p) {}
void parts_scrollbar_create(struct parts *p, bool h) {
    assert(h == false);
    p->scrollbar = xcalloc(1, sizeof(struct parts_scrollbar));
    for (int r = 0; r < 2; r++)
        for (int i = 0; i < 5; i++)
            p->scrollbar->names[r][i] = string_ref(&EMPTY_STRING);
    got_scrollbar++;
}
void parts_scrollbar_free(struct parts *p) {}
int parts_scrollbar_range(struct parts_scrollbar *s) { return 0; }
void parts_scrollbar_rebuild(struct parts *p) {}
void parts_sliderbar_create(struct parts *p, bool h) {}
void parts_widget_create_deferred(struct parts *p, int t) {}
void parts_widget_create(struct parts *p) {}
void parts_widget_end_batch(struct parts *p) {}
void parts_widget_free(struct parts *p) {}
bool parts_cp_add_v14(int parts_no, const struct parts_cp_v14 *src, int state) { return true; }
void parts_cp_set_last_fill_full_size(int parts_no, int state, bool full_size) {}
void PE_Parts_SetSoundName(int a, struct string *b, int c) {}

#include "parts/activity.c"

void PE_SetParentPartsNumber(int c, int p) { got_child_parent++; }
bool PE_SetHGaugeCG(int no, struct string *s, int st) { return true; }
bool PE_SetVGaugeCG(int no, struct string *s, int st) { return true; }
bool PE_SetLoopCG(int no, struct string *s, int start, int nr, int spd, int st) { return true; }
void PE_SetCheckBoxOverrideCheckedCGName(int a, struct string *b) {}
void PE_SetButtonSize(int a, int b, int c) {}
void PE_SetButtonEnable(int a, bool b) {}
void PE_SetButtonColor(int a, int b, int c, int d) {}
void PE_SetButtonFontProperty(int a, int b, int c, int d, int e, int f, float g, int h, int i, int j, float k) {}
void PE_SetButtonText(int a, struct string *b) {}
void PE_SetButtonTextOriginPosMode(int a, int b) {}
void PE_SetButtonCharSpace(int a, int b) {}
void PE_SetButtonLineSpace(int a, int b) {}
void PE_SetButtonCGName(int a, struct string *b) {}
void PE_SetButtonFlatName(int a, struct string *b) {}
void PE_SetCheckBoxButtonMode(int a, bool b) {}
void PE_SetCheckBoxCGName(int a, struct string *b) {}
void PE_SetCheckBoxCharSpace(int a, int b) {}
void PE_SetCheckBoxColor(int a, int b, int c, int d) {}
void PE_SetCheckBoxEnable(int a, bool b) {}
void PE_SetCheckBoxFlatName(int a, struct string *b) {}
void PE_SetCheckBoxFontProperty(int a, int b, int c, int d, int e, int f, float g, int h, int i, int j, float k) {}
void PE_SetCheckBoxLineSpace(int a, int b) {}
void PE_SetCheckBoxOverrideCheckedFlatName(int a, struct string *b) {}
void PE_SetCheckBoxOverrideCGName(int a, struct string *b) {}
void PE_SetCheckBoxOverrideFlatName(int a, struct string *b) {}
void PE_SetCheckBoxSize(int a, int b, int c) {}
void PE_SetCheckBoxText(int a, struct string *b) {}
void PE_SetCheckBoxTextOriginPosMode(int a, int b) {}
void PE_SetClickable(int a, bool b) {}
void PE_SetComment(int a, struct string *b) {}
void PE_SetComponentCheckBoxShowLinkNumber(int a, int b) {}
void PE_SetComponentMargin(int a, int b, int c, int d, int e) {}
void PE_SetComponentPos(int a, float b, float c) {}
void PE_SetComponentSubColorMode(int a, bool b) {}
void PE_SetDrag(int a, bool b) {}
void PE_SetEnableSpeedUp(int a, bool b) {}
void PE_SetFlatFPSRate(int a, float b, int c) {}
bool PE_SetHGaugeRate(int a, float b, float c, int d) { return true; }
void PE_SetHGaugeReverse(int a, bool b, int c) {}
bool PE_SetHGaugeSurfaceArea(int a, int b, int c, int d, int e, int f) { return true; }
void PE_SetInputState(int a, int b) {}
void PE_SetKeyWaitCGName(int a, struct string *b, int c, int d, int e) {}
void PE_SetKeyWaitFlatName(int a, struct string *b) {}
void PE_SetKeyWaitPos(int a, int b, int c, int d) {}
void PE_SetLayoutBoxAlign(int a, int b) {}
void PE_SetLayoutBoxLayoutType(int a, int b) {}
void PE_set_layoutbox_padding(int a, int b, int c, int d, int e) {}
void PE_SetLayoutBoxReturnF(int a, bool b, float c) {}
void PE_SetLayoutBoxReturnSizeForRate(int a, bool b) {}
bool PE_SetLoopCGSurfaceArea(int a, int b, int c, int d, int e, int f) { return true; }
void PE_SetMessageWindowActive(int a, bool b) {}
void PE_SetMessageWindowCGName(int a, struct string *b) {}
void PE_SetMessageWindowFlatName(int a, struct string *b) {}
void PE_SetMessageWindowInactiveMultipleColor(int a, int b, int c, int d) {}
void PE_SetMessageWindowRubyCharSpace(int a, int b) {}
void PE_SetMessageWindowRubyFont(int a, int b, int c, int d, int e, int f, float g, int h, int i, int j, float k) {}
void PE_SetMessageWindowRubyLineSpace(int a, int b) {}
void PE_SetMessageWindowText(int a, struct string *b, int c, struct string *d, int e, int f) {}
void PE_SetMessageWindowTextArea(int a, int b, int c, int d, int e) {}
void PE_SetMessageWindowTextFont(int a, int b, int c, int d, int e, int f, float g, int h, int i, int j, float k) {}
void PE_SetMessageWindowTextOriginPosMode(int a, int b) {}
void PE_SetMessageWindowTextSpace(int a, int b, int c) {}
void PE_SetMessageWindowTextSpeed(int a, int b) {}
void PE_SetMultiplyColor(int a, int b, int c, int d) {}
bool PE_SetNumeralCG(int a, struct string *b, int c) { return true; }
void PE_SetNumeralFont(int a, int b, int c, int d, int e, int f, float g, int h, int i, int j, float k, int l) {}
void PE_SetNumeralFullPitch(int a, bool b, int c) {}
bool PE_SetNumeralLength(int a, int b, int c) { return true; }
bool PE_SetNumeralLinkedCGNumberWidthWidthList(int a, struct string *b, int c, int d, int e, int f, int g, int h, int i, int j, int k, int l, int m, int n, int o) { return true; }
bool PE_SetNumeralNumber(int a, int b, int c) { return true; }
bool PE_SetNumeralShowComma(int a, bool b, int c) { return true; }
void PE_SetNumeralShowPadding(int a, bool b, int c) {}
void PE_SetNumeralShowType(int a, int b, int c) {}
bool PE_SetNumeralSpace(int a, int b, int c) { return true; }
bool PE_SetNumeralSurfaceArea(int a, int b, int c, int d, int e, int f) { return true; }
void PE_SetOnCursorShowLinkPartsNumber(int a, int b) {}
void PE_SetPanelAlphaGradationBottom(int a, int b) {}
void PE_SetPanelAlphaGradationLeft(int a, int b) {}
void PE_SetPanelAlphaGradationRight(int a, int b) {}
void PE_SetPanelAlphaGradationTop(int a, int b) {}
void PE_SetPanelColor(int a, int b, int c, int d, int e) {}
void PE_SetPanelSize(int a, int b, int c) {}
void PE_SetPartsAlphaClipperPartsNumber(int a, int b) {}
bool PE_SetPartsCG(int a, struct string *b, int c, int d) { return true; }
bool PE_SetPartsCGDetectionSize(int a, struct string *b, int c) { return true; }
bool PE_SetPartsCGSurfaceArea(int a, int b, int c, int d, int e, int f) { return true; }
bool PE_SetPartsConstructionSurfaceArea(int a, int b, int c, int d, int e, int f) { return true; }
void PE_SetPartsDrawFilter(int a, int b) {}
bool PE_SetPartsFlat(int a, struct string *b, int c) { return true; }
bool PE_SetPartsFlatSurfaceArea(int a, int b, int c, int d, int e, int f) { return true; }
bool PE_SetPartsFontBoldWeight(int a, float b, int c) { return true; }
bool PE_SetPartsFontColor(int a, int b, int c, int d, int e) { return true; }
bool PE_SetPartsFontEdgeColor(int a, int b, int c, int d, int e) { return true; }
bool PE_SetPartsFontEdgeWeight(int a, float b, int c) { return true; }
bool PE_SetPartsFontSize(int a, int b, int c) { return true; }
bool PE_SetPartsFontType(int a, int b, int c) { return true; }
void PE_SetPartsMagX(int a, float b) {}
void PE_SetPartsMagY(int a, float b) {}
void PE_SetPartsMessageWindowShowLink(int a, bool b) {}
void PE_SetPartsOriginPosMode(int a, int b) {}
void PE_SetPartsPixelDecide(int a, bool b) {}
void PE_SetPartsRotateX(int a, float b) {}
void PE_SetPartsRotateY(int a, float b) {}
void PE_SetPartsRotateZ(int a, float b) {}
bool PE_SetPartsTextSurfaceArea(int a, int b, int c, int d, int e, int f) { return true; }
void PE_SetPassCursor(int a, bool b) {}
void PE_SetRectangleDetectionPoint(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {}
void PE_SetShow(int a, bool b) {}
bool PE_SetText(int a, struct string *b, int c) {
    if (b && !strcmp(b->text, "\xb3\xac\xb0\xba\xc9\xf1\xc6\xef"))
        got_text++;
    return true;
}
void PE_SetTextBoxText(int a, struct string *b) {}
void PE_SetTextBoxReadOnly(int a, bool b) {}
void PE_SetTextBoxMaxTextLength(int a, int b) {}
bool PE_SetTextCharSpace(int a, int b, int c) { return true; }
bool PE_SetTextLineSpace(int a, int b, int c) { return true; }
bool PE_SetVGaugeRate(int a, float b, float c, int d) { return true; }
void PE_SetVGaugeReverse(int a, bool b, int c) {}
bool PE_SetVGaugeSurfaceArea(int a, int b, int c, int d, int e, int f) { return true; }
void PE_SetWheelable(int a, bool b) {}
void PE_SetZ(int a, int b) {}
void PE_SetAddColor(int a, int b, int c, int d) {}
void PE_SetAlpha(int a, int b) {}
void PE_UpdateComponent(int a) {}
void parts_render_update(void) {}
void PE_ChangeFlatCG(int a, struct string *b, struct string *c, int d) {}
void PE_ChangeFlatSound(int a, struct string *b, struct string *c, int d) {}
bool PE_GoFramePartsFlat(int a, int b, int c) { return true; }
bool PE_AddAddFilterToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h, bool i, int j) { return true; }
bool PE_AddAlphaBlendTextToPartsConstructionProcess(int a, int b, int c, struct string *d, int e, int f, int g, int h, int i, float j, int k, int l, int m, float n, int o, int p, int q) { return true; }
bool PE_AddCGBlendToPartsConstructionProcess(int a, struct string *b, int c, int d, int e) { return true; }
bool PE_AddCopyCutCGToPartsConstructionProcess(int a, struct string *b, int c, int d, int e, int f, int g, int h, int i, int j, int k, int l) { return true; }
bool PE_AddCopyTextToPartsConstructionProcess(int a, int b, int c, struct string *d, int e, int f, int g, int h, int i, float j, int k, int l, int m, float n, int o, int p, int q) { return true; }
bool PE_AddCreateCGToProcess(int a, struct string *b, int c) { return true; }
bool PE_AddCreatePixelOnlyToPartsConstructionProcess(int a, int b, int c, int d) { return true; }
bool PE_AddCreateToPartsConstructionProcess(int a, int b, int c, int d) { return true; }
bool PE_AddCutCGAlphaBlendToPartsConstructionProcess(int a, struct string *b, int c, int d, int e, int f, int g, int h, int i, int j, int k, bool l, int m) { return true; }
bool PE_AddCutCGOnlyAlphaToPartsConstructionProcess(int a, struct string *b, int c, int d, int e, int f, int g, int h, int i, int j, int k, bool l, int m) { return true; }
bool PE_AddDrawCutCGToPartsConstructionProcess(int a, struct string *b, int c, int d, int e, int f, int g, int h, int i, int j, int k, int l) { return true; }
bool PE_AddDrawLineToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) { return true; }
bool PE_AddDrawRectToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h, int i) { return true; }
bool PE_AddDrawTextToPartsConstructionProcess(int a, int b, int c, struct string *d, int e, int f, int g, int h, int i, float j, int k, int l, int m, float n, int o, int p, int q) { return true; }
bool PE_AddFillAlphaColorToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) { return true; }
bool PE_AddFillAMapToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g) { return true; }
bool PE_AddFillGradationHorizonToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j, int k, int l) { return true; }
bool PE_AddFillToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h, int i) { return true; }
bool PE_AddFillWithAlphaToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) { return true; }
bool PE_AddGrayFilterToPartsConstructionProcess(int a, int b, int c, int d, int e, bool f, int g) { return true; }
bool PE_AddHBlurFilterToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, bool g, int h) { return true; }
bool PE_AddMulAMapGradationHorizonToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h) { return true; }
bool PE_AddMulAMapGradationVerticalToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h) { return true; }
bool PE_AddMulFilterToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h, bool i, int j) { return true; }
bool PE_AddOnlyAlphaTextToPartsConstructionProcess(int a, int b, int c, struct string *d, int e, int f, int g, int h, int i, float j, int k, int l, int m, float n, int o, int p, int q) { return true; }
bool PE_AddVBlurFilterToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, bool g, int h) { return true; }
bool PE_BuildPartsConstructionProcess(int a, int b) { return true; }
void PE_AddRadioButtonBoxChild(int a, int b) {}
void PE_ClearRadioButtonBoxChild(int a) {}

int main(int argc, char **argv) {
    assert(argc == 2);
    sys4_set_text_gbk(true);
    size_t sz = 0;
    uint8_t *raw = file_read(argv[1], &sz);
    struct ex *ex = ex_read(raw, sz);
    free(raw);
    assert(ex);

    struct string *act_name = make_string("ixseal", 6);
    assert(PE_CreateActivity(act_name));
    struct activity *a = find_activity(act_name);
    assert(a);
    load_part(a, &ex->blocks[0].val.tree->children[0], 0, false);

    assert(got_text == 1);
    assert(got_cp == 1);
    assert(got_scrollbar == 1);
    assert(got_special_10 == 1);
    assert(got_child_parent == 4);

    ex_free(ex);
    free_string(act_name);
    puts("OK");
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmpdir:
            ex_path = Path(tmpdir) / "ixseal_pact.ex"
            ex_path.write_bytes(pact_ex)
            out = compile_and_run(harness, LIBSYS4_CORE, ["-lz", "-lm"], [str(ex_path)])
            self.assertEqual(out.strip(), "OK")

    def test_parts_inline_construction_record_layouts(self):
        source = "src/hll/PartsEngine.c"
        functions = "\n".join(
            extract_function(source, name)
            for name in (
                "PartsEngine_add_construction_process_extended",
                "PartsEngine_get_construction_process_v6_arrays",
                "PartsEngine_AddPartsConstructionProcess_v6",
                "PartsEngine_GetPartsConstructionProcess_v6",
            )
        )
        harness = r"""
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "vm.h"
#include "vm/page.h"

#undef VM_ERROR
#define VM_ERROR(...) abort()

static struct page *make_page(int n)
{
    struct page *p = calloc(1, sizeof(*p) + n * sizeof(*p->values));
    assert(p);
    p->nr_vars = n;
    return p;
}

static union vm_value decoded[32];
static int a2, blur;
static void PartsEngine_add_construction_process(union vm_value *ints,
        union vm_value *floats, union vm_value *strings, int alpha2, int radius)
{
    memcpy(decoded, ints, sizeof(decoded));
    assert(floats[0].f == 1.5f && floats[1].f == 2.0f);
    assert(strings[0].i == 501 && strings[1].i == 502);
    a2 = alpha2;
    blur = radius;
}

static void PartsEngine_GetPartsConstructionProcess(int part, int index,
        struct page **ints, struct page **floats, struct page **strings, int state)
{
    assert(part == 123 && index == 4 && state == 2);
    *ints = make_page(32);
    for (int i = 0; i < 32; i++)
        (*ints)->values[i].i = 100 + i;
}

void delete_page_vars(struct page *p) {}
void free_page(struct page *p) { free(p); }
""" + functions + r"""

int main(void)
{
    struct page *floats = make_page(2), *strings = make_page(2);
    floats->values[0].f = 1.5f;
    floats->values[1].f = 2.0f;
    strings->values[0].i = 501;
    strings->values[1].i = 502;

    // Command 23: alpha-blended text. The extended record has A2 at 21,
    // followed by spacing, face, size, colors, full-size, and blur.
    const int extended[] = {
        123, 2, 23, 0, 0, 0, 0, 0, 6, 8, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 137, 3, -2, 257, 25, 250, 240, 230, 19, 22, 24, 1, 7
    };
    for (int nr_ints = 32; nr_ints <= 34; nr_ints++) {
        bool is_extended = nr_ints > 32;
        struct page *add = make_page(nr_ints);
        struct page *get = make_page(nr_ints + 1);
        for (int i = 0; i < add->nr_vars; i++)
            add->values[i].i = extended[i + (!is_extended && i >= 21)];
        PartsEngine_AddPartsConstructionProcess_v6(&add, &floats, &strings);
        for (int i = 0; i < 32; i++)
            assert(decoded[i].i == extended[i + (i >= 21)]);
        assert(a2 == (is_extended ? 137 : 0));
        assert(blur == (nr_ints == 34 ? 7 : 0));

        get->values[0].i = 123;
        get->values[1].i = 4;
        get->values[2].i = 2;
        PartsEngine_GetPartsConstructionProcess_v6(&get, &floats, &strings);
        assert(get->values[0].i == 123 && get->values[1].i == 4 && get->values[2].i == 2);
        for (int i = 3; i < get->nr_vars; i++)
            assert(get->values[i].i == 100 + i - 3 + (!is_extended && i >= 22));

        free_page(add);
        free_page(get);
    }
    free_page(floats);
    free_page(strings);
}
"""
        compile_and_run(harness, [])

    def test_location_banner_completes_when_its_root_flat_loops(self):
        harness = r"""
#include <assert.h>

#include "parts.h"
#include "parts_internal.h"
#include "xsystem4.h"

bool game_ixseal = true;
static struct parts part;

struct parts *parts_get(int no)
{
    return &part;
}

struct parts_flat *parts_get_flat(struct parts *parts, int state)
{
    return &parts->states[state].flat;
}

void parts_dirty(struct parts *parts) {}

int main(void)
{
    struct flat_script_key key = { .frame_index = 95, .has_jump = true, .jump_frame = 51 };
    struct flat_timeline timeline = {
        .type = FLAT_TIMELINE_SCRIPT, .frame_count = 121,
        .script = { .keys = &key, .count = 1 },
    };
    struct flat asset = { .hdr = { .fps = 25 }, .nr_timelines = 1, .timelines = &timeline };
    struct parts_flat *flat = &part.states[0].flat;
    flat->flat = &asset;
    flat->end_frame = 120;
    flat->fps_rate = 1;
    flat->needs_advance = true;
    flat->root_state = parts_flat_layer_state_new(1);

    parts_flat_update(flat, 0);
    for (int i = 0; i < 94; i++)
        parts_flat_update(flat, 40);
    assert(flat->root_state->current_frame == 94);
    assert(!PE_IsPartsFlatEnd(0, 1));

    parts_flat_update(flat, 100);
    assert(flat->root_state->current_frame >= 51);
    assert(flat->root_state->current_frame < 95);
    assert(PE_IsPartsFlatEnd(0, 1));
    parts_flat_update(flat, 100);
    assert(PE_IsPartsFlatEnd(0, 1));

    game_ixseal = false;
    assert(!PE_IsPartsFlatEnd(0, 1));
    flat->root_state->current_frame = flat->end_frame;
    assert(PE_IsPartsFlatEnd(0, 1));
    game_ixseal = true;
    assert(PE_BackPartsFlatBeginFrame(0, 1));
    assert(!PE_IsPartsFlatEnd(0, 1));
    parts_flat_layer_state_free(flat->root_state);
}
"""
        compile_and_run(
            harness,
            [ROOT / "src/parts/flat.c", ROOT / "subprojects/libsys4/src/system.c"],
            ["-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-lm"],
        )

    def test_parts_v2_set_active_controller_ignores_invalid_id(self):
        fn_get_idx = extract_function("src/parts/parts.c", "PE_GetControllerIndex")
        fn_set_active = extract_function("src/parts/parts.c", "PE_set_active_controller")
        fn_get_active = extract_function("src/parts/parts.c", "PE_get_active_controller")
        harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "parts_internal.h"
#include "vm.h"

static struct parts_context live_ctx = {{
    .list = TAILQ_HEAD_INITIALIZER(live_ctx.list),
    .dirty = TAILQ_HEAD_INITIALIZER(live_ctx.dirty),
}};
struct parts_context *parts_current = &live_ctx;
bool parts_v2 = true;
static int warn_count = 0;

_Noreturn void _vm_error(const char *fmt, ...) {{ abort(); }}
#undef WARNING
#define WARNING(...) do {{ warn_count++; }} while (0)

{fn_get_idx}
{fn_set_active}
{fn_get_active}

int main(void) {{
    ctrl_stack.nr_controllers = 2;
    ctrl_stack.overlay_id = 1000010000;
    ctrl_stack.ids[0] = 1000010001;
    ctrl_stack.ids[1] = 1000010002;
    ctrl_stack.active = 1;

    // Mitsuki's ■全消し passes legacy layer ID 0; must warn and leave active controller unchanged.
    PE_set_active_controller(0);
    assert(warn_count == 1);
    assert(PE_get_active_controller() == 1000010002);

    PE_set_active_controller(1000010001);
    assert(warn_count == 1);
    assert(ctrl_stack.active == 0);
    assert(PE_get_active_controller() == 1000010001);

    PE_set_active_controller(1000010000);
    assert(ctrl_stack.active == PARTS_CONTROLLER_SYSTEM_OVERLAY);
    assert(PE_get_active_controller() == 1000010000);

    puts("OK");
    return 0;
}}
"""
        out = compile_and_run(harness, LIBSYS4_CORE, ["-lz", "-lm"])
        self.assertEqual(out.strip(), "OK")

    def test_mainex_v12_and_legacy_float_widens_ex_int(self):
        # Ixseal stores whole-number skill multipliers like 无消费攻击.攻击力 = 1 as EX_INT,
        # then queries them via MainEXFile.Float / AFloat / A2Float (and ref out_float).
        no_cost_attack = _gbk("无消费攻击")
        attack_power = _gbk("攻击力")
        main_ex = build_extf([
            _ex_block(
                no_cost_attack,
                6,
                _ex_tree(no_cost_attack, [
                    _ex_leaf(attack_power, 1, struct.pack("<i", 1)),
                ]),
            ),
            _ex_block(
                b"Skill",
                6,
                _ex_tree(b"Skill", [
                    _ex_leaf(b"IntPow", 1, struct.pack("<i", 1)),
                    _ex_leaf(b"FltPow", 2, struct.pack("<f", 1.5)),
                    _ex_leaf(b"StrPow", 3, _ex_pstr(b"not_num")),
                ]),
            ),
            _ex_block(
                b"ListVals",
                5,
                _ex_list([
                    (1, struct.pack("<i", 1)),
                    (1, struct.pack("<i", 40)),
                    (2, struct.pack("<f", -2.5)),
                    (3, _ex_pstr(b"nope")),
                ]),
            ),
            _ex_block(
                b"SkillTable",
                4,
                _ex_table(
                    [
                        (1, b"ID", 1),
                        (3, b"Name", 1),
                        (1, b"PowerInt", 0),
                        (2, b"PowerFlt", 0),
                        (3, b"Note", 0),
                    ],
                    [
                        [
                            (1, struct.pack("<i", 10)),
                            (3, _ex_pstr(b"Weak")),
                            (1, struct.pack("<i", 1)),
                            (2, struct.pack("<f", 1.25)),
                            (3, _ex_pstr(b"str0")),
                        ],
                        [
                            (1, struct.pack("<i", 20)),
                            (3, _ex_pstr(b"Strong")),
                            (1, struct.pack("<i", 3)),
                            (2, struct.pack("<f", -0.5)),
                            (3, _ex_pstr(b"str1")),
                        ],
                    ],
                ),
            ),
            _ex_block(
                b"StrSkillTable",
                4,
                _ex_table(
                    [
                        (3, b"Name", 1),
                        (1, b"PowerInt", 0),
                    ],
                    [
                        [
                            (3, _ex_pstr(b"Weak")),
                            (1, struct.pack("<i", 1)),
                        ],
                    ],
                ),
            ),
        ])
        with tempfile.TemporaryDirectory() as tmpdir:
            ex_path = Path(tmpdir) / "IxsealSkills.ex"
            ex_path.write_bytes(main_ex)
            harness = r"""
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xsystem4.h"
#include "hll/hll.h"
#include "mainex_v12.h"
#include "reign.h"
#include "system4/ain.h"
#include "system4/archive.h"
#include "system4/ex.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "vm/v12.h"

struct config config;
struct ain *ain = NULL;
struct vm_pointer *heap = NULL;
size_t heap_size = 0;
bool game_evenicle2 = false;
bool game_okusama = false;
bool game_haharanman = false;

extern struct static_library lib_MainEXFile;
void mainex_v12_prelink(struct static_library *library);

static void *find_exported(struct static_library *lib, const char *name) {
    for (int i = 0; lib->functions[i].name; i++) {
        if (!strcmp(lib->functions[i].name, name))
            return lib->functions[i].fun;
    }
    return NULL;
}

void static_library_replace(struct static_library *lib, const char *name, void *fun) {
    for (int i = 0; lib->functions[i].name; i++) {
        if (!strcmp(lib->functions[i].name, name)) {
            lib->functions[i].fun = fun;
            return;
        }
    }
}

struct page *v12_array_new(const struct ain_type *type, int size, bool init) { return NULL; }
struct page *alloc_array(int rank, union vm_value *dimensions, enum ain_data_type type, int struc, bool init) { return NULL; }
void delete_page_vars(struct page *p) {}
void free_page(struct page *p) {}
char *gamedir_path(const char *p) { return strdup(p); }
char *gamedir_path_icase(const char *p) { return strdup(p); }
struct archive *RE_open_3d_archive(void) { return NULL; }
struct archive_data *RE_archive_find_txtex(struct archive *ar, const char *model_dir) { return NULL; }
const char *display_utf0(const char *s) { return s ? s : ""; }

static void add_hll_fun(struct ain_library *lib, const char *name, enum ain_data_type ret,
        int nargs, const enum ain_data_type *args) {
    lib->functions = xrealloc_array(lib->functions, lib->nr_functions,
            lib->nr_functions + 1, sizeof(struct ain_hll_function));
    struct ain_hll_function *f = &lib->functions[lib->nr_functions++];
    memset(f, 0, sizeof(*f));
    f->name = strdup(name);
    f->return_type.data = ret;
    f->nr_arguments = nargs;
    if (nargs > 0) {
        f->arguments = xcalloc(nargs, sizeof(struct ain_hll_argument));
        for (int i = 0; i < nargs; i++)
            f->arguments[i].type.data = args[i];
    }
}

int main(int argc, char **argv) {
    assert(argc == 2);
    config.ex_path = argv[1];
    sys4_set_text_gbk(true);

    struct string *s_ixseal = make_string("\xce\xde\xcf\xfb\xb7\xd1\xb9\xa5\xbb\xf7.\xb9\xa5\xbb\xf7\xc1\xa6", 17);
    struct string *s_int = make_string("Skill.IntPow", 12);
    struct string *s_flt = make_string("Skill.FltPow", 12);
    struct string *s_str = make_string("Skill.StrPow", 12);
    struct string *s_list = make_string("ListVals", 8);
    struct string *s_tbl = make_string("SkillTable", 10);
    struct string *s_stbl = make_string("StrSkillTable", 13);
    struct string *s_col_int = make_string("PowerInt", 8);
    struct string *s_col_flt = make_string("PowerFlt", 8);
    struct string *s_col_note = make_string("Note", 4);
    struct string *s_key_weak = make_string("Weak", 4);

    // 1. Legacy handle-based MainEXFile.c float getters widen EX_INT -> float.
    {
        void (*init_fn)(void) = find_exported(&lib_MainEXFile, "_ModuleInit");
        void (*fini_fn)(void) = find_exported(&lib_MainEXFile, "_ModuleFini");
        int (*handle_fn)(struct string *) = find_exported(&lib_MainEXFile, "Handle");
        bool (*flt_fn)(int, float *) = find_exported(&lib_MainEXFile, "Float");
        bool (*aflt_fn)(int, int, float *) = find_exported(&lib_MainEXFile, "AFloat");
        bool (*a2flt_fn)(int, int, int, float *) = find_exported(&lib_MainEXFile, "A2Float");
        bool (*ia2flt_fn)(int, int, struct string *, float *) = find_exported(&lib_MainEXFile, "IA2Float");
        bool (*sa2flt_fn)(int, struct string *, struct string *, float *) = find_exported(&lib_MainEXFile, "SA2Float");
        bool (*ra2flt_fn)(int, int, struct string *, float *) = find_exported(&lib_MainEXFile, "RA2Float");
        assert(init_fn && fini_fn && handle_fn && flt_fn && aflt_fn && a2flt_fn && ia2flt_fn && sa2flt_fn && ra2flt_fn);

        init_fn();
        float out = -99.0f;
        assert(flt_fn(handle_fn(s_ixseal), &out) && out == 1.0f);
        assert(flt_fn(handle_fn(s_flt), &out) && out == 1.5f);
        assert(!flt_fn(handle_fn(s_str), &out));

        int h_list = handle_fn(s_list);
        assert(aflt_fn(h_list, 0, &out) && out == 1.0f);
        assert(aflt_fn(h_list, 1, &out) && out == 40.0f);
        assert(aflt_fn(h_list, 2, &out) && out == -2.5f);
        assert(!aflt_fn(h_list, 3, &out));

        int h_tbl = handle_fn(s_tbl);
        int h_stbl = handle_fn(s_stbl);
        assert(a2flt_fn(h_tbl, 0, 2, &out) && out == 1.0f);
        assert(a2flt_fn(h_tbl, 1, 2, &out) && out == 3.0f);
        assert(a2flt_fn(h_tbl, 0, 3, &out) && out == 1.25f);
        assert(!a2flt_fn(h_tbl, 0, 4, &out));
        assert(ia2flt_fn(h_tbl, 10, s_col_int, &out) && out == 1.0f);
        assert(sa2flt_fn(h_stbl, s_key_weak, s_col_int, &out) && out == 1.0f);
        assert(ra2flt_fn(h_tbl, 1, s_col_int, &out) && out == 3.0f);
        assert(!ra2flt_fn(h_tbl, 0, s_col_note, &out));
        fini_fn();
    }

    // 2. MainEXFile_v12 value-returning getters (Ixseal / Rance 10 / Evenicle 2) with all game flags false.
    {
        ain = ain_new(11, 0);
        ain->libraries = xcalloc(1, sizeof(struct ain_library));
        ain->nr_libraries = 1;
        ain->libraries[0].name = strdup("MainEXFile");
        enum ain_data_type int_args[] = { AIN_STRING, AIN_INT, AIN_INT };
        add_hll_fun(&ain->libraries[0], "Int", AIN_INT, 3, int_args);

        mainex_v12_prelink(&lib_MainEXFile);
        void (*init_fn)(void) = find_exported(&lib_MainEXFile, "_ModuleInit");
        void (*fini_fn)(void) = find_exported(&lib_MainEXFile, "_ModuleFini");
        float (*flt_fn)(struct string *, float, int) = find_exported(&lib_MainEXFile, "Float");
        float (*aflt_fn)(struct string *, int, float, int) = find_exported(&lib_MainEXFile, "AFloat");
        float (*a2flt_fn)(struct string *, int, int, float, int) = find_exported(&lib_MainEXFile, "A2Float");
        assert(init_fn && fini_fn && flt_fn && aflt_fn && a2flt_fn);

        init_fn();
        assert(flt_fn(s_ixseal, 0.0f, 0) == 1.0f);
        assert(flt_fn(s_int, 0.0f, 0) == 1.0f);
        assert(flt_fn(s_flt, 0.0f, 0) == 1.5f);
        assert(flt_fn(s_str, -7.0f, 0) == -7.0f);

        assert(aflt_fn(s_list, 0, 0.0f, 0) == 1.0f);
        assert(aflt_fn(s_list, 1, 0.0f, 0) == 40.0f);
        assert(aflt_fn(s_list, 2, 0.0f, 0) == -2.5f);
        assert(aflt_fn(s_list, 3, -7.0f, 0) == -7.0f);

        assert(a2flt_fn(s_tbl, 0, 2, 0.0f, 0) == 1.0f);
        assert(a2flt_fn(s_tbl, 1, 2, 0.0f, 0) == 3.0f);
        assert(a2flt_fn(s_tbl, 0, 3, 0.0f, 0) == 1.25f);
        assert(a2flt_fn(s_tbl, 0, 4, -7.0f, 0) == -7.0f);
        fini_fn();
        ain_free(ain);
        ain = NULL;
    }

    // 3. MainEXFile_v12 ref out-parameter getters (out_float).
    {
        ain = ain_new(11, 0);
        ain->libraries = xcalloc(1, sizeof(struct ain_library));
        ain->nr_libraries = 1;
        ain->libraries[0].name = strdup("MainEXFile");
        enum ain_data_type ref_int_args[] = { AIN_STRING, AIN_REF_INT, AIN_INT };
        add_hll_fun(&ain->libraries[0], "Int", AIN_BOOL, 3, ref_int_args);

        mainex_v12_prelink(&lib_MainEXFile);
        void (*init_fn)(void) = find_exported(&lib_MainEXFile, "_ModuleInit");
        void (*fini_fn)(void) = find_exported(&lib_MainEXFile, "_ModuleFini");
        bool (*flt_fn)(struct string *, float *, int) = find_exported(&lib_MainEXFile, "Float");
        bool (*aflt_fn)(struct string *, int, float *, int) = find_exported(&lib_MainEXFile, "AFloat");
        bool (*a2flt_fn)(struct string *, int, int, float *, int) = find_exported(&lib_MainEXFile, "A2Float");
        bool (*ia2flt_fn)(struct string *, int, struct string *, float *, int) = find_exported(&lib_MainEXFile, "IA2Float");
        bool (*sa2flt_fn)(struct string *, struct string *, struct string *, float *, int) = find_exported(&lib_MainEXFile, "SA2Float");
        bool (*ra2flt_fn)(struct string *, int, struct string *, float *, int) = find_exported(&lib_MainEXFile, "RA2Float");
        assert(init_fn && fini_fn && flt_fn && aflt_fn && a2flt_fn && ia2flt_fn && sa2flt_fn && ra2flt_fn);

        init_fn();
        float out = -99.0f;
        assert(flt_fn(s_ixseal, &out, 0) && out == 1.0f);
        assert(flt_fn(s_flt, &out, 0) && out == 1.5f);
        assert(!flt_fn(s_str, &out, 0));

        assert(aflt_fn(s_list, 0, &out, 0) && out == 1.0f);
        assert(aflt_fn(s_list, 1, &out, 0) && out == 40.0f);
        assert(aflt_fn(s_list, 2, &out, 0) && out == -2.5f);
        assert(!aflt_fn(s_list, 3, &out, 0));

        assert(a2flt_fn(s_tbl, 0, 2, &out, 0) && out == 1.0f);
        assert(a2flt_fn(s_tbl, 1, 2, &out, 0) && out == 3.0f);
        assert(a2flt_fn(s_tbl, 0, 3, &out, 0) && out == 1.25f);
        assert(!a2flt_fn(s_tbl, 0, 4, &out, 0));

        assert(ia2flt_fn(s_tbl, 10, s_col_int, &out, 0) && out == 1.0f);
        assert(sa2flt_fn(s_stbl, s_key_weak, s_col_int, &out, 0) && out == 1.0f);
        assert(ra2flt_fn(s_tbl, 1, s_col_int, &out, 0) && out == 3.0f);
        assert(ra2flt_fn(s_tbl, 1, s_col_flt, &out, 0) && out == -0.5f);
        assert(!ra2flt_fn(s_tbl, 0, s_col_note, &out, 0));
        fini_fn();
        ain_free(ain);
        ain = NULL;
    }

    free_string(s_ixseal); free_string(s_int); free_string(s_flt); free_string(s_str);
    free_string(s_list); free_string(s_tbl); free_string(s_stbl); free_string(s_col_int); free_string(s_col_flt);
    free_string(s_col_note); free_string(s_key_weak);
    puts("OK");
    return 0;
}
"""
            out = compile_and_run(
                harness,
                [
                    ROOT / "src/hll/MainEXFile.c",
                    ROOT / "src/hll/MainEXFile_v12.c",
                    *LIBSYS4_CORE,
                ],
                ["-lz", "-lm"],
                [str(ex_path)],
            )
            self.assertEqual(out.strip(), "OK")


if __name__ == "__main__":
    unittest.main()
