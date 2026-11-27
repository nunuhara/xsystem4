#!/usr/bin/env python3
"""Native C regression tests for Batch 3 CN System 4.x adaptations."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

from test_evenicle2_regressions import (
    LIBSYS4_CORE,
    ROOT,
    _ex_leaf,
    _ex_pstr,
    _ex_tree,
    _sjis,
    build_afa_v2,
    build_extf,
    compile_and_run,
)
from test_vm_reign_regressions import extract_function


def _gbk(text: str) -> bytes:
    return text.encode("gbk")


def _build_chs_tbl(entries: dict[int, int]) -> bytes:
    """Build a minimal Longinus CHS.TBL little-endian uint16 table."""
    max_code = max(entries.keys()) if entries else 0x8140
    table = bytearray((max_code + 1) * 2)
    for sjis_code, gbk_code in entries.items():
        struct.pack_into("<H", table, sjis_code * 2, gbk_code)
    return bytes(table)


def _build_xor49_dict(pairs: list[tuple[bytes, bytes, bytes]]) -> bytes:
    """Build an XOR-0x49 3-line-per-entry UI dictionary like NightRemasterDict.dat."""
    raw = bytearray()
    for src, dst, sep in pairs:
        raw.extend(src)
        raw.append(0x0A)
        raw.extend(dst)
        raw.append(0x0A)
        raw.extend(sep)
        raw.append(0x0A)
    return bytes(b ^ 0x49 for b in raw)


class Batch3CnNativeTests(unittest.TestCase):
    def test_hacks_and_system_service_tokens(self):
        fn_gvt = extract_function("src/hll/SystemService.c", "game_version_text")
        fn_gvbt = extract_function("src/hll/SystemService.c", "SystemService_GetGameVersionByText")

        harness = f"""
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
void message_layout_init_rance03(void) {{}}
_Noreturn void _vm_error(const char *fmt, ...) {{ abort(); }}

{fn_gvt}
{fn_gvbt}

int main(void) {{
    // 1. NightRemaster CN: v14 + C_Monster@nMoonPiece::get + "45qsdbdsfq5"
    struct ain *nr = ain_new(14, 1);
    ain_add_function(nr, "C_Monster@nMoonPiece::get");
    ain_add_string(nr, "45qsdbdsfq5");
    config.game_name = strdup("\\xd0\\xb0\\xd2\\xb9\\xbd\\xab\\xd6\\xc1"); // GBK "邪夜将至"
    config.view_width = 800;
    config.view_height = 600;
    sys4_set_text_gbk(false);
    apply_game_specific_hacks(nr);
    assert(game_night_remaster == true);
    assert(game_oyakorankan == false && game_toushin3 == false);
    assert(sys4_text_is_gbk == true);
    assert(config.view_width == 1280 && config.view_height == 720);
    struct string *v = SystemService_GetGameVersionByText();
    assert(strcmp(v->text, "00000045q0000sdb0500dsfq5") == 0);
    free_string(v);
    ain_free(nr);
    free(config.game_name);

    // 2. Oyakorankan: v6 + StatusHeroineInfomation + HeroineMapIcon overrides mistaken 1280x720 to 1024x768
    struct ain *oya = ain_new(6, 0);
    ain_add_struct(oya, "StatusHeroineInfomation");
    ain_add_struct(oya, "HeroineMapIcon");
    config.game_name = strdup("Oyakorankan");
    config.view_width = 1280;
    config.view_height = 720;
    sys4_set_text_gbk(false);
    apply_game_specific_hacks(oya);
    assert(game_oyakorankan == true);
    assert(game_night_remaster == false && game_toushin3 == false);
    assert(config.view_width == 1024 && config.view_height == 768);
    // Custom non-1280x720 viewports stay untouched.
    config.view_width = 800;
    config.view_height = 600;
    config.view_width_set = true;
    config.view_height_set = true;
    apply_game_specific_hacks(oya);
    assert(config.view_width == 800 && config.view_height == 600);
    config.view_width_set = false;
    config.view_height_set = false;
    ain_free(oya);
    free(config.game_name);

    // 3. Toushin 3 CN: v5 + Toushin3Loader library + GBK "斗神" game_name marker
    struct ain *t3 = ain_new(5, 0);
    ain_add_library(t3, "Toushin3Loader");
    config.game_name = strdup("\\xb6\\xb7\\xc9\\xf1\\xb6\\xbc\\xca\\xd03"); // GBK "斗神都市3"
    config.view_width = 800;
    config.view_height = 600;
    sys4_set_text_gbk(false);
    apply_game_specific_hacks(t3);
    assert(game_toushin3 == true);
    assert(game_night_remaster == false && game_oyakorankan == false);
    assert(sys4_text_is_gbk == true);
    ain_free(t3);
    free(config.game_name);

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

    def test_kiwi_v12_6ch_mixer_for_night_remaster(self):
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
bool game_ixseal = false;
bool game_okusama = false;
bool game_haharanman = false;
bool game_night_remaster = true;
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
    // 1. Empty config fallback populates 6 default channels with Master at 0.
    memset(&config, 0, sizeof(config));
    config.default_volume = 80;
    kiwi_prelink();
    assert(config.mixer_nr_channels == 6);
    assert(strcmp(config.mixer_channels[0], "Master") == 0);
    assert(strcmp(config.mixer_channels[3], "Voice") == 0);
    assert(strcmp(config.mixer_channels[4], "Ambient") == 0);
    assert(strcmp(config.mixer_channels[5], "BackVoice") == 0);

    mixer_init();
    assert(nr_mixers == 6);
    assert(master == &mixers[0]);
    for (int i = 1; i < nr_mixers; i++) {
        assert(mixers[i].parent == master);
    }

    // 2. Shipped NightRemasterCHSR18 AliceStart.ini config: channel 0 is GBK "主音量"
    //    and must be recognized as master directly (no extra 7th mixer), preserving all
    //    6 configured names and volumes while keeping all children attached to master.
    memset(&config, 0, sizeof(config));
    static char *shipped_names[] = {
        "\xd6\xf7\xd2\xf4\xc1\xbf",     // GBK "主音量"
        "\xa3\xc2\xa3\xc7\xa3\xcd",     // GBK "ＢＧＭ"
        "\xd2\xf4\xd0\xa7",             // GBK "音效"
        "\x89\xb9\x90\xba",             // SJIS "音声"
        "\x8a\xc2\x89\xb9",             // SJIS "環音"
        "\x94\x77\x8c\x69\x89\xb9\x90\xba" // SJIS "背景音声"
    };
    static int shipped_vols[] = { 80, 75, 70, 90, 65, 85 };
    config.mixer_nr_channels = 6;
    config.mixer_channels = shipped_names;
    config.mixer_volumes = shipped_vols;
    config.default_volume = 80;
    kiwi_prelink();
    assert(config.mixer_nr_channels == 6);
    assert(strcmp(config.mixer_channels[0], "\xd6\xf7\xd2\xf4\xc1\xbf") == 0);
    assert(config.mixer_volumes[1] == 75 && config.mixer_volumes[5] == 85);

    mixer_init();
    assert(nr_mixers == 6);
    assert(master == &mixers[0]);
    assert(strcmp(mixers[0].name, "\xd6\xf7\xd2\xf4\xc1\xbf") == 0);
    assert(strcmp(mixers[3].name, "\x89\xb9\x90\xba") == 0);
    for (int i = 1; i < nr_mixers; i++) {
        assert(mixers[i].parent == &mixers[0]);
    }
    assert(mixer_set_volume(0, 0) == true);
    assert(master->mixer.gain == 0.0f);

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(harness, LIBSYS4_CORE, ["-lz", "-lm"])
        self.assertEqual(out.strip(), "OK")

    def test_chs_tbl_and_xor49_text_dict(self):
        # Remap SJIS 0x889F ('亜') -> GBK '古' (0xB9C5), SJIS 0x8145 ('・') -> 0x003F ('?' must be skipped!),
        # and SJIS 0x81F4 ('♪') -> 0x003F ('?' must be skipped!).
        chs_tbl = _build_chs_tbl({
            0x889F: 0xB9C5,
            0x8145: 0x003F,
            0x81F4: 0x003F,
        })
        xor_dict = _build_xor49_dict([
            (_sjis("ゲーム設定"), _gbk(" 游戏设定"), b""),
            (_sjis("メッセージ速度"), _gbk("\u3000文章显示速度\u3000"), b"\xa1\xa1"),
            (_sjis("ロード時に確認をおこなう"), _sjis("ロード時に確認をおこなう"), b""),
            (_sjis("ゲームモードを選択してください"), _gbk("请选择游玩模式"), b""),
            (_sjis("チュートリアル"), _gbk("教程"), b""),
        ])

        with tempfile.TemporaryDirectory() as tmpdir:
            td = Path(tmpdir)
            chs_path = td / "CHS.TBL"
            dict_path = td / "NightRemasterDict.dat"
            chs_path.write_bytes(chs_tbl)
            dict_path.write_bytes(xor_dict)

            harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/utfsjis.h"

int main(int argc, char **argv) {
    assert(argc == 3);

    // 1. Longinus CHS.TBL loads into mai_* render table while keeping base CP932 sjis2utf intact.
    sys4_set_text_gbk(false);
    assert(sys4_load_chs_tbl(argv[1]) == true);
    assert(sys4_has_mai_word_table() == true);

    const char remapped[] = { (char)0x88, (char)0x9f, (char)0x81, (char)0x45, (char)0x81, (char)0xf4, 0 };
    char *mai_utf = mai_sjis2utf(remapped, 0);
    assert(strcmp(mai_utf, "\xe5\x8f\xa4\xe3\x83\xbb\xe2\x99\xaa") == 0); // "古・♪"
    free(mai_utf);

    // Base CP932 archive/path lookup must NOT be mutated by CHS.TBL.
    char *raw_utf = sjis2utf(remapped, 0);
    assert(strcmp(raw_utf, "\xe4\xba\x9c\xe3\x83\xbb\xe2\x99\xaa") == 0); // "亜・♪"
    free(raw_utf);
    sys4_reset_mai_word_table();

    // 2. XOR-0x49 UI/EX text dictionary (single-line, fullwidth-space separator, no-op skip, multi-line, idempotent).
    assert(sys4_load_text_dict(argv[2]) == true);
    const char *s1 = "\x83\x51\x81\x5b\x83\x80\x90\xdd\x92\xe8"; // SJIS "ゲーム設定"
    const char *d1 = sys4_text_dict_lookup(s1);
    assert(d1 != NULL && strcmp(d1, " \xd3\xce\xcf\xb7\xc9\xe8\xb6\xa8") == 0); // GBK " 游戏设定"
    // Idempotent: looking up the GBK output does not remap.
    assert(sys4_text_dict_lookup(d1) == NULL);
    // Entry after fullwidth-space separator (0xA1 0xA1) still parses.
    const char *s2 = "\x83\x81\x83\x62\x83\x5a\x81\x5b\x83\x57\x91\xac\x93\x78"; // SJIS "メッセージ速度"
    assert(sys4_text_dict_lookup(s2) != NULL);
    // Identity entry is skipped.
    const char *noop = "\x83\x8d\x81\x5b\x83\x68\x8e\x9e\x82\xc9\x8a\x6d\x94\x46\x82\xf0\x82\xa8\x82\xb1\x82\xc8\x82\xa4";
    assert(sys4_text_dict_lookup(noop) == NULL);

    // Multi-line translation translates matching lines and preserves unmatched lines & newlines.
    char multi[256];
    snprintf(multi, sizeof(multi), "%s\nUNMATCHED\n%s",
        "\x83\x51\x81\x5b\x83\x80\x83\x82\x81\x5b\x83\x68\x82\xf0\x91\x49\x91\xf0\x82\xb5\x82\xc4\x82\xad\x82\xbe\x82\xb3\x82\xa2",
        "\x83\x60\x83\x85\x81\x5b\x83\x67\x83\x8a\x83\x41\x83\x8b");
    char *translated = sys4_text_dict_translate_lines(multi);
    assert(translated != NULL);
    assert(strcmp(translated, "\xc7\xeb\xd1\xa1\xd4\xf1\xd3\xce\xcd\xe6\xc4\xa3\xca\xbd\nUNMATCHED\n\xbd\xcc\xb3\xcc") == 0);
    free(translated);

    assert(sys4_text_dict_translate_lines("ASCII\nONLY") == NULL);
    sys4_reset_text_dict();
    assert(sys4_text_dict_lookup(s1) == NULL);

    puts("OK");
    return 0;
}
"""
            out = compile_and_run(harness, LIBSYS4_CORE, ["-lz", "-lm"], [str(chs_path), str(dict_path)])
            self.assertEqual(out.strip(), "OK")

    def test_asset_manager_utf8_ain_basename_and_standalone_chs_afa(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            gdir = Path(tmpdir)
            # Simulate Oyakorankan: AliceStart.CHS has GBK CodeName "母娘乱館.CHS" while disk archives are UTF-8 "母娘乱館CG.afa" + standalone "CHS.afa".
            (gdir / "母娘乱館.CHS").write_bytes(b"AIN")
            (gdir / "母娘乱館CG.afa").write_bytes(build_afa_v2([
                (b"title_menu", b"JP_TITLE_MENU"),
                (b"base_only_cg", b"BASE_ONLY"),
            ]))
            (gdir / "CHS.afa").write_bytes(build_afa_v2([
                (b"title_menu", b"CN_TITLE_MENU"),
            ]))

            gbk_codename = _gbk("母娘乱館.CHS")
            gbk_codename_c = "".join(f"\\x{b:02x}" for b in gbk_codename)

            harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xsystem4.h"
#include "asset_manager.h"
#include "system4/archive.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

struct config config;
struct ain *ain = NULL;
bool game_rance6 = false;
bool game_rance7_mg = false;
bool game_rance8 = false;
bool game_rance9 = false;
bool game_rance10 = false;
bool game_dohnadohna = false;
bool game_evenicle = false;
bool game_evenicle2 = false;
bool game_escalayer_reboot = false;
bool game_galzoo = false;
bool game_ixseal = false;
bool game_haruka = false;
bool game_tsumashibori = false;
bool game_tsumamigui3 = false;
bool game_daibanchou = false;
bool game_daibanchou_en = false;
bool game_daiteikoku = false;
bool game_haruurare = false;

char *gamedir_path(const char *p) {{ return strdup(p); }}
char *gamedir_path_icase(const char *p) {{ return strdup(p); }}
char *savedir_path(const char *p) {{ return strdup(p); }}
const char *display_sjis0(const char *s) {{ return s ? s : ""; }}
const char *display_utf0(const char *s) {{ return s ? s : ""; }}
struct cg *cg_load_data(struct archive_data *dfile) {{ (void)dfile; return NULL; }}
struct cg *cg_load_file(const char *filename) {{ (void)filename; return NULL; }}
struct archive;
void cg_set_base_loader_by_name(struct cg *(*fn)(struct archive *, const char *)) {{ (void)fn; }}
void cg_set_base_loader_by_id(struct cg *(*fn)(struct archive *, int)) {{ (void)fn; }}
bool cg_get_metrics_data(struct archive_data *dfile, struct cg_metrics *dst) {{ (void)dfile; (void)dst; return false; }}
bool cg_get_metrics_file(const char *filename, struct cg_metrics *dst) {{ (void)filename; (void)dst; return false; }}
void save_cjk_canon_forms_buf(const char *s, char *sjis_buf, size_t sjis_cap, char **sjis_out,
        char *gbk_buf, size_t gbk_cap, char **gbk_out) {{
    (void)s; (void)sjis_buf; (void)sjis_cap; (void)gbk_buf; (void)gbk_cap;
    if (sjis_out) *sjis_out = NULL;
    if (gbk_out) *gbk_out = NULL;
}}
char *save_transcode_cstr_strict(const char *s, size_t len, bool src_is_gbk, bool dst_is_gbk, size_t *out_len) {{
    (void)s; (void)len; (void)src_is_gbk; (void)dst_is_gbk; (void)out_len;
    return NULL;
}}

#include "asset_manager.c"

static void check_cg(const char *name, const char *expected) {{
    struct archive_data *d = asset_get_by_name(ASSET_CG, name, NULL);
    assert(d != NULL);
    assert(d->size == strlen(expected));
    assert(memcmp(d->data, expected, d->size) == 0);
    archive_free_data(d);
}}

int main(int argc, char **argv) {{
    assert(argc == 2);
    sys4_set_text_gbk(false);
    memset(&config, 0, sizeof(config));
    config.game_dir = argv[1];
    // GBK CodeName under Shift-JIS mode would fail without ain_basename_utf8.
    config.ain_filename = "{gbk_codename_c}";
    config.ain_basename_utf8 = "\\xe6\\xaf\\x8d\\xe5\\xa8\\x98\\xe4\\xb9\\xb1\\xe9\\xa4\\xa8.CHS";
    asset_manager_init();

    // Standalone CHS.afa overrides base 母娘乱館CG.afa at AFA_PRIO_PATCH.
    check_cg("title_menu", "CN_TITLE_MENU");
    check_cg("base_only_cg", "BASE_ONLY");

    puts("OK");
    return 0;
}}
"""
            out = compile_and_run(
                harness,
                [
                    ROOT / "subprojects/libsys4/src/ald.c",
                    ROOT / "subprojects/libsys4/src/alk.c",
                    ROOT / "subprojects/libsys4/src/flat.c",
                    *LIBSYS4_CORE,
                ],
                ["-lz", "-lm"],
                [str(gdir)],
            )
            self.assertEqual(out.strip(), "OK")

    def test_parts_input_focus_and_click_delivery(self):
        # Verify NightRemaster widget buttons (clickable == false) still emit PARTS_MSG_MOUSE_CLICK
        # without clobbering clicked_parts (PE_GetClickPartsNumber()), while clickable == true sets it.
        fn_update = extract_function("src/parts/input.c", "parts_update_mouse")

        harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xsystem4.h"
#include "input.h"
#include "parts_internal.h"

static struct parts_context ctx_storage = {{0}};
struct parts_context *parts_current = &ctx_storage;
bool parts_v2 = true;
bool parts_began_click = true;
bool game_rance10 = false;
bool game_dohnadohna = false;
static bool prev_clicking = false;
static Point parts_prev_pos = {{0, 0}};
static int click_down_parts = 0;
static int clicked_parts = 0;
static bool click_fired_this_frame = false;
static struct parts *drag_parts = NULL;
static SDL_FPoint drag_initial_pos = {{0.0f, 0.0f}};
static Point drag_start_cursor = {{0, 0}};
static int msg_click_count = 0;
static int msg_click_last_parts = 0;
static int focused_parts = 0;

void parts_set_state(struct parts *p, enum parts_state_type s) {{ p->state = s; }}
static void parts_play_sound(struct parts *p, enum parts_state_type s) {{ (void)p; (void)s; }}
void parts_input_reset_part(struct parts *p) {{ (void)p; }}
bool parts_is_visible(struct parts *p) {{ (void)p; return true; }}
static bool parts_needs_mouse_hittest(struct parts *p) {{ (void)p; return true; }}
static bool parts_hittest(struct parts *p, enum parts_state_type s, Point pt) {{ (void)p; (void)s; (void)pt; return true; }}
void parts_scrollbar_input(struct parts *p, Point pt, bool h, bool c, bool pd, int dt, int w) {{
    (void)p; (void)pt; (void)h; (void)c; (void)pd; (void)dt; (void)w;
}}
struct parts *parts_try_get(int no) {{ (void)no; return NULL; }}
void parts_dirty(struct parts *p) {{ (void)p; }}
void parts_engine_dirty(void) {{}}
void PE_SetFocus(int no) {{ focused_parts = no; }}
void parts_widget_check(struct parts *p, bool c) {{ (void)p; (void)c; }}
void parts_msg_push(struct parts *p, int type, const char *fmt, ...) {{
    (void)fmt;
    if (type == PARTS_MSG_MOUSE_CLICK) {{
        msg_click_count++;
        msg_click_last_parts = p->no;
    }}
}}

{fn_update}

int main(void) {{
    struct parts_widget widget = {{ .enabled = true }};

    // 1. Widget button with parts->clickable == false (NightRemaster title buttons):
    //    emits PARTS_MSG_MOUSE_CLICK, leaves clicked_parts == 0 so AFL_Parts_EndWaitForClick(0) returns 0.
    struct parts btn = {{ .no = 1000010, .clickable = false, .widget = &widget, .enable_input = true, .linked_from = -1 }};
    bool hover_consumed = false, click_consumed = false;
    clicked_parts = 0;
    click_down_parts = 0;
    prev_clicking = false;
    parts_update_mouse(&btn, (Point){{10, 10}}, true, 16, 0, 0, &hover_consumed, &click_consumed);
    hover_consumed = false;
    click_consumed = false;
    prev_clicking = true;
    parts_update_mouse(&btn, (Point){{10, 10}}, false, 16, 0, 0, &hover_consumed, &click_consumed);
    assert(msg_click_count == 1 && msg_click_last_parts == 1000010);
    assert(clicked_parts == 0);

    // 2. Clickable part with parts->clickable == true sets clicked_parts == parts->no.
    struct parts cpart = {{ .no = 2000020, .clickable = true, .enable_input = true, .linked_from = -1 }};
    hover_consumed = false;
    click_consumed = false;
    clicked_parts = 0;
    click_down_parts = 0;
    prev_clicking = false;
    parts_update_mouse(&cpart, (Point){{10, 10}}, true, 16, 0, 0, &hover_consumed, &click_consumed);
    hover_consumed = false;
    click_consumed = false;
    prev_clicking = true;
    parts_update_mouse(&cpart, (Point){{10, 10}}, false, 16, 0, 0, &hover_consumed, &click_consumed);
    assert(msg_click_count == 2 && msg_click_last_parts == 2000020);
    assert(clicked_parts == 2000020);

    game_dohnadohna = true;
    hover_consumed = false;
    click_consumed = false;
    clicked_parts = 0;
    click_down_parts = 0;
    prev_clicking = false;
    parts_update_mouse(&btn, (Point){{10, 10}}, true, 16, 0, 0, &hover_consumed, &click_consumed);
    hover_consumed = false;
    click_consumed = false;
    prev_clicking = true;
    parts_update_mouse(&btn, (Point){{10, 10}}, false, 16, 0, 0, &hover_consumed, &click_consumed);
    assert(msg_click_count == 3 && msg_click_last_parts == btn.no);
    assert(clicked_parts == btn.no);

    // The Rance 10 choice script needs focus on the captured backdrop, not a native click.
    struct parts backdrop = {{ .no = 3000030, .enable_input = true, .linked_from = -1 }};
    for (int rance10 = 0; rance10 <= 1; rance10++) {{
        for (int pass_cursor = 0; pass_cursor <= 1; pass_cursor++) {{
            game_rance10 = rance10;
            backdrop.pass_cursor = pass_cursor;
            hover_consumed = click_consumed = false;
            clicked_parts = click_down_parts = focused_parts = 0;
            prev_clicking = false;
            parts_update_mouse(&backdrop, (Point){{10, 10}}, true, 16, 0, 0,
                    &hover_consumed, &click_consumed);
            assert(click_down_parts == (pass_cursor ? 0 : backdrop.no));
            assert(focused_parts == (rance10 && !pass_cursor ? backdrop.no : 0));

            hover_consumed = click_consumed = false;
            prev_clicking = true;
            parts_update_mouse(&backdrop, (Point){{10, 10}}, false, 16, 0, 0,
                    &hover_consumed, &click_consumed);
            assert(focused_parts == (rance10 && !pass_cursor ? backdrop.no : 0));
            assert(msg_click_count == 3 && clicked_parts == 0);
        }}
    }}

    puts("OK");
    return 0;
}}
"""
        out = compile_and_run(harness, [], ["-I" + str(ROOT / "src/parts")])
        self.assertEqual(out.strip(), "OK")

    def test_config_init_with_dir_chs_and_ini_precedence(self):
        fn_dir = extract_function("src/system4.c", "config_init_with_dir")
        with tempfile.TemporaryDirectory() as tmpdir:
            td = Path(tmpdir)
            chs_only = td / "chs_only"
            chs_only.mkdir()
            (chs_only / "AliceStart.CHS").write_bytes(b"CodeName = \"Oya.CHS\"\n")

            dual_no_tbl = td / "dual_no_tbl"
            dual_no_tbl.mkdir()
            (dual_no_tbl / "AliceStart.ini").write_bytes(b"CodeName = \"Oya.ain\"\n")
            (dual_no_tbl / "AliceStart.CHS").write_bytes(b"CodeName = \"Oya.CHS\"\n")

            dual_with_tbl = td / "dual_with_tbl"
            dual_with_tbl.mkdir()
            (dual_with_tbl / "AliceStart.ini").write_bytes(b"CodeName = \"Oya.ain\"\n")
            (dual_with_tbl / "AliceStart.CHS").write_bytes(b"CodeName = \"Oya.CHS\"\n")
            (dual_with_tbl / "CHS.TBL").write_bytes(b"\x00\x00")

            harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/file.h"

static char last_selected[1024];
static bool config_init_with_ini(const char *path) {{
    strncpy(last_selected, path, sizeof(last_selected) - 1);
    last_selected[sizeof(last_selected) - 1] = '\\0';
    return true;
}}

{fn_dir}

int main(int argc, char **argv) {{
    assert(argc == 4);

    // 1. CHS-only directory without CHS.TBL still launches via AliceStart.CHS fallback.
    last_selected[0] = '\\0';
    assert(config_init_with_dir(argv[1]) == true);
    assert(strstr(last_selected, "AliceStart.CHS") != NULL);

    // 2. Dual-config directory without CHS.TBL preserves standard AliceStart.ini precedence.
    last_selected[0] = '\\0';
    assert(config_init_with_dir(argv[2]) == true);
    assert(strstr(last_selected, "AliceStart.ini") != NULL);

    // 3. Dual-config directory WITH CHS.TBL prefers AliceStart.CHS.
    last_selected[0] = '\\0';
    assert(config_init_with_dir(argv[3]) == true);
    assert(strstr(last_selected, "AliceStart.CHS") != NULL);

    puts("OK");
    return 0;
}}
"""
            out = compile_and_run(
                harness,
                LIBSYS4_CORE,
                ["-lz", "-lm"],
                [str(chs_only), str(dual_no_tbl), str(dual_with_tbl)],
            )
            self.assertEqual(out.strip(), "OK")

    def test_utf8_ini_preserves_gbk_only_script_filename_for_legacy_script(self):
        def extract_def(rel_path: str, name: str) -> str:
            source = (ROOT / rel_path).read_text(encoding="utf-8")
            pos = 0
            while True:
                idx = source.index(name + "(", pos)
                start = source.rfind("\n", 0, idx) + 1
                semi = source.find(";", idx)
                brace = source.find("{", idx)
                if brace != -1 and (semi == -1 or brace < semi):
                    break
                pos = idx + len(name)
            depth = 1
            end = brace + 1
            while depth:
                depth += (source[end] == "{") - (source[end] == "}")
                end += 1
            return source[start:end]

        fn_file_utf8 = extract_def("src/system4.c", "file_text_is_utf8")
        fn_ain_utf8 = extract_def("src/system4.c", "ain_text_is_utf8")
        fn_set_utf8 = extract_def("src/system4.c", "set_utf8_release")
        fn_read_cfg = extract_def("src/system4.c", "read_config")
        fn_read_entries = extract_def("src/system4.c", "read_config_entries")
        fn_save_path = extract_def("src/system4.c", "get_save_path")
        fn_cfg_init_code = extract_def("src/system4.c", "config_init_with_utf8_codename")
        fn_cfg_init = extract_def("src/system4.c", "config_init")
        fn_resolve = extract_def("src/system4.c", "resolve_configured_ain_path")
        fn_init_ini = extract_def("src/system4.c", "config_init_with_ini")
        fn_init_dir = extract_def("src/system4.c", "config_init_with_dir")

        # Build a minimal valid AI2 v4 script with a legacy Shift-JIS message (non-UTF-8).
        msgs = [b"", _sjis("テスト")]
        raw_ain = (
            b"VERS"
            + struct.pack("<I", 4)
            + b"FUNC"
            + struct.pack("<I", 0)
            + b"STRT"
            + struct.pack("<I", 0)
            + b"MSG0"
            + struct.pack("<I", len(msgs))
            + b"".join(m + b"\x00" for m in msgs)
        )
        comp_ain = zlib.compress(raw_ain)
        ai2_bytes = b"AI2\x00\x00\x00\x00\x00" + struct.pack("<II", len(raw_ain), len(comp_ain)) + comp_ain

        with tempfile.TemporaryDirectory() as tmpdir:
            td = Path(tmpdir)
            home_dir = td / "home"
            home_dir.mkdir()
            legacy_home = td / "legacy_home"
            (legacy_home / "游?.CHS" / "SaveData").mkdir(parents=True)
            bslash_home = td / "bslash_home"
            (bslash_home / "episode1\\游戏.CHS" / "SaveData").mkdir(parents=True)

            game_dir = td / "utf8_gbk_chs"
            game_dir.mkdir()
            # 1. Untitled UTF-8 INI with GBK-only char '戏' (not in Shift-JIS).
            (game_dir / "System40.ini").write_bytes('CodeName = "游戏.CHS"\n'.encode("utf-8"))
            (game_dir / "游戏.CHS").write_bytes(ai2_bytes)

            ep1_dir = td / "ep1"
            (ep1_dir / "episode1").mkdir(parents=True)
            # 2. Untitled UTF-8 INI with backslash subdirectory in CodeName (distinct save path + mkdir_p).
            (ep1_dir / "System40.ini").write_bytes('CodeName = "episode1\\游戏.CHS"\n'.encode("utf-8"))
            (ep1_dir / "episode1" / "游戏.CHS").write_bytes(ai2_bytes)

            kana_dir = td / "utf8_halfwidth_kana"
            kana_dir.mkdir()
            # 3. Titled UTF-8 INI with half-width katakana GameName ("ﾗﾝｽ") + legacy script.
            (kana_dir / "System40.ini").write_bytes(
                'GameName = "ﾗﾝｽ"\nCodeName = "game.ain"\n'.encode("utf-8")
            )
            (kana_dir / "game.ain").write_bytes(ai2_bytes)

            harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xsystem4.h"
#include "system4.h"
#include "system4/ain.h"
#include "system4/file.h"
#include "system4/ini.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

struct config config;
static char *resolved_ain_path = NULL;
static const char *test_home_dir = NULL;

static struct string *ini_string(struct ini_entry *e) {{ return e->value.s; }}
static int ini_integer(struct ini_entry *e) {{ return e->value.i; }}
static bool ini_boolean(struct ini_entry *e) {{ return e->value.i != 0; }}
static void read_mixer_channels(struct ini_entry *e) {{ (void)e; }}
static char *get_xsystem4_home(void) {{ return xstrdup(test_home_dir); }}

char *gamedir_path(const char *path) {{
    char *utf = sjis2utf(path, strlen(path));
    for (char *p = utf; *p; p++) {{
        if (*p == '\\\\')
            *p = '/';
    }}
    char *res = path_join(config.game_dir, utf);
    free(utf);
    return res;
}}

char *gamedir_path_icase(const char *path) {{
    char *res = gamedir_path(path);
    if (!file_exists(res)) {{
        char *tmp = path_get_icase(res);
        free(res);
        res = tmp;
    }}
    return res;
}}

static bool read_config_entries(const char *path);

{fn_file_utf8}
{fn_ain_utf8}
{fn_set_utf8}
{fn_read_cfg}
{fn_read_entries}
{fn_save_path}
{fn_cfg_init_code}
{fn_cfg_init}
{fn_resolve}
{fn_init_ini}
{fn_init_dir}

int main(int argc, char **argv) {{
    assert(argc == 7);
    test_home_dir = argv[4];

    // 1. Untitled UTF-8 System40.ini pointing to legacy "游戏.CHS".
    memset(&config, 0, sizeof(config));
    assert(config_init_with_dir(argv[1]) == true);

    // Legacy script text encoding is preserved (not stuck in UTF-8 mode).
    assert(sys4_source_is_utf8 == false);
    assert(sys4_text_is_gbk == false);

    // Default save directory uses the lossless UTF-8 CodeName without '?' replacement.
    assert(config.save_dir != NULL);
    assert(strchr(config.save_dir, '?') == NULL);
    assert(strstr(config.save_dir, "/\\xe6\\xb8\\xb8\\xe6\\x88\\x8f.CHS/SaveData") != NULL);

    // Resolved on-disk script path and UTF-8 basename survive the legacy INI reread intact.
    char *ainfile = resolve_configured_ain_path(NULL);
    assert(ainfile != NULL);
    assert(file_exists(ainfile));
    assert(strcmp(path_basename(ainfile), "\\xe6\\xb8\\xb8\\xe6\\x88\\x8f.CHS") == 0);
    assert(config.ain_basename_utf8 != NULL);
    assert(strcmp(config.ain_basename_utf8, "\\xe6\\xb8\\xb8\\xe6\\x88\\x8f.CHS") == 0);

    // Opening the resolved script succeeds and reads back the legacy Shift-JIS message.
    int err = AIN_SUCCESS;
    struct ain *opened = ain_open(ainfile, &err);
    assert(opened != NULL && err == AIN_SUCCESS);
    assert(opened->nr_messages == 2);
    assert(strcmp(opened->messages[1]->text, "\\x83\\x65\\x83\\x58\\x83\\x67") == 0);
    ain_free(opened);
    free(ainfile);

    // 2. Untitled UTF-8 INI with backslash subdirectory normalizes separators in save_dir so
    //    mkdir_p creates nested parents on a fresh save root while preserving distinct paths.
    memset(&config, 0, sizeof(config));
    assert(config_init_with_dir(argv[2]) == true);
    assert(config.save_dir != NULL && strchr(config.save_dir, '?') == NULL);
    assert(strchr(config.save_dir, '\\\\') == NULL);
    assert(strstr(config.save_dir, "/episode1/\\xe6\\xb8\\xb8\\xe6\\x88\\x8f.CHS/SaveData") != NULL);
    assert(mkdir_p(config.save_dir) == 0);
    assert(is_directory(config.save_dir));
    assert(config.ain_basename_utf8 != NULL);
    assert(strcmp(config.ain_basename_utf8, "\\xe6\\xb8\\xb8\\xe6\\x88\\x8f.CHS") == 0);
    ainfile = resolve_configured_ain_path(NULL);
    assert(ainfile != NULL && file_exists(ainfile));
    free(ainfile);

    // 3. Titled UTF-8 INI with half-width katakana GameName ("ﾗﾝｽ") preserves CP932 save path
    //    without GBK '?' corruption.
    memset(&config, 0, sizeof(config));
    assert(config_init_with_dir(argv[3]) == true);
    assert(config.save_dir != NULL && strchr(config.save_dir, '?') == NULL);
    assert(strstr(config.save_dir, "/\\xef\\xbe\\x97\\xef\\xbe\\x9d\\xef\\xbd\\xbd/SaveData") != NULL);

    // 4. Pre-existing legacy save dir ("游?.CHS/SaveData") is preserved instead of stranding saves.
    test_home_dir = argv[5];
    memset(&config, 0, sizeof(config));
    assert(config_init_with_dir(argv[1]) == true);
    assert(config.save_dir != NULL);
    assert(strstr(config.save_dir, "/\\xe6\\xb8\\xb8?.CHS/SaveData") != NULL);

    // 5. Pre-existing POSIX literal-backslash save dir ("episode1\\游戏.CHS/SaveData") is preserved.
    test_home_dir = argv[6];
    memset(&config, 0, sizeof(config));
    assert(config_init_with_dir(argv[2]) == true);
    assert(config.save_dir != NULL);
    assert(strstr(config.save_dir, "/episode1\\\\\\xe6\\xb8\\xb8\\xe6\\x88\\x8f.CHS/SaveData") != NULL);

    puts("OK");
    return 0;
}}
"""
            ini_tab_c = td / "ini_parser.tab.c"
            ini_tab_h = td / "ini_parser.tab.h"
            ini_lex_c = td / "ini_lexer.yy.c"
            libsys4_src = ROOT / "subprojects/libsys4/src"
            subprocess.run(
                [
                    "bison",
                    str(libsys4_src / "ini_parser.y"),
                    f"--defines={ini_tab_h}",
                    f"--output={ini_tab_c}",
                ],
                check=True,
            )
            subprocess.run(
                ["flex", "-o", str(ini_lex_c), str(libsys4_src / "ini_lexer.l")],
                check=True,
            )
            ini_sources = [libsys4_src / "ini.c", ini_tab_c, ini_lex_c]
            out = compile_and_run(
                harness,
                [*LIBSYS4_CORE, *ini_sources],
                [
                    "-lz",
                    "-lm",
                    "-I" + str(td),
                    "-I" + str(libsys4_src),
                ],
                [str(game_dir), str(ep1_dir), str(kana_dir), str(home_dir), str(legacy_home), str(bslash_home)],
            )
            self.assertEqual(out.strip(), "OK")


if __name__ == "__main__":
    unittest.main()
