#!/usr/bin/env python3
"""Native C regression tests for Batch 2 CN System 4.x adaptations."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from test_evenicle2_regressions import (
    LIBSYS4_CORE,
    ROOT,
    _ex_block,
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


class Batch2CnNativeTests(unittest.TestCase):
    def test_okusama_button_label_keeps_outline_without_double_counting_its_advance(self):
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
struct text_style { unsigned face; float font_spacing, edge; void *font_size; };
struct parts_text { struct text_style ts; int line_space; };
struct parts_state { struct parts_text text; };
struct string {};
struct parts_widget {
    bool defer_rebuild, dirty_label;
    struct string *text;
    struct parts_state label;
};
struct parts { struct parts_widget *widget; int component_type; };
enum { PARTS_TEXT };
static bool game_okusama, gfx_text_advance_edges;
static float appended_spacing;
static void parts_state_reset(struct parts_state *s, int type) { s->text = (struct parts_text){0}; }
static float text_style_edge_width(struct text_style *s) { return s->edge; }
static void parts_text_append(struct parts *p, struct parts_text *text, struct string *source) {
    assert(text->ts.edge == 4 && text->line_space == -10);
    appended_spacing = text->ts.font_spacing;
}
static void parts_recalculate_hitbox(struct parts *p) {}
static void parts_component_dirty(struct parts *p) {}
static void parts_dirty(struct parts *p) {}
""" + extract_function("src/parts/widgets.c", "rebuild_label") + r"""
int main(void) {
    struct parts_widget widget = { .label.text = { { 256, -3, 4, NULL }, -10 } };
    struct parts parts = { .widget = &widget };
    gfx_text_advance_edges = true;
    rebuild_label(&parts);
    assert(appended_spacing == -3);
    game_okusama = true;
    rebuild_label(&parts);
    assert(appended_spacing == -7 && widget.label.text.ts.font_spacing == -3);
    rebuild_label(&parts);
    assert(appended_spacing == -7 && widget.label.text.ts.font_spacing == -3);
    parts.component_type = 1;
    rebuild_label(&parts);
    assert(appended_spacing == -3);
    parts.component_type = 0;
    gfx_text_advance_edges = false;
    rebuild_label(&parts);
    assert(appended_spacing == -3);
}
"""
        compile_and_run(harness, [])

    def test_hacks_and_system_service_tokens(self):
        fn_gvt = extract_function("src/hll/SystemService.c", "game_version_text")
        fn_gvbt = extract_function("src/hll/SystemService.c", "SystemService_GetGameVersionByText")
        fn_set_out = extract_function("src/hll/SystemService.c", "set_out_cstr")
        fn_sysname = extract_function("src/hll/SystemService.c", "SystemService_GetSystemName")
        fn_scnlist = extract_function("src/hll/SystemService.c", "SystemService_GetSceneNameList")

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
{fn_set_out}
{fn_sysname}
{fn_scnlist}

int main(void) {{
    // 1. Okusama CN: v14 + AdvGauge@UpdatePregnancy + "4fnd6e97tn9"
    struct ain *oku = ain_new(14, 0);
    ain_add_function(oku, "AdvGauge@UpdatePregnancy");
    ain_add_string(oku, "4fnd6e97tn9");
    config.game_name = strdup("\\xcc\\xab\\xcc\\xab\\xb5\\xc4\\xbb\\xd6\\xb8\\xb4\\xca\\xf5"); // GBK "太太的恢复术"
    config.view_width = 800;
    config.view_height = 600;
    sys4_set_text_gbk(false);
    apply_game_specific_hacks(oku);
    assert(game_okusama == true);
    assert(game_evenicle2 == false && game_haharanman == false);
    assert(config.view_width == 1280 && config.view_height == 720);
    struct string *v = SystemService_GetGameVersionByText();
    assert(strcmp(v->text, "0000004fn0000d6e000097tn9000") == 0);
    free_string(v);
    ain_free(oku);
    free(config.game_name);

    // 2. Evenicle 2 Rance: v14 + C_MedicaMenu@0 + "th74s65a" (takes priority over game_evenicle2 token)
    struct ain *e2r = ain_new(14, 0);
    ain_add_function(e2r, "C_MedicaMenu@0");
    ain_add_string(e2r, "th74s65a");
    config.game_name = strdup("Evenicle2Rance");
    config.view_width = 800;
    config.view_height = 600;
    apply_game_specific_hacks(e2r);
    assert(game_evenicle2 == true && game_evenicle2_rance == true);
    v = SystemService_GetGameVersionByText();
    assert(strcmp(v->text, "7f4ssdth7855sbk65asde4fdsrfg4rvn4aq") == 0);
    free_string(v);
    ain_free(e2r);
    free(config.game_name);

    // 3. Base Evenicle 2 without "th74s65a" keeps base token
    struct ain *e2 = ain_new(14, 0);
    ain_add_function(e2, "C_MedicaMenu@0");
    config.game_name = strdup("Evenicle2");
    apply_game_specific_hacks(e2);
    assert(game_evenicle2 == true && game_evenicle2_rance == false);
    v = SystemService_GetGameVersionByText();
    assert(strcmp(v->text, "8938gohds983ogh1223fg086qirkcvn36") == 0);
    free_string(v);
    ain_free(e2);
    free(config.game_name);

    // 4. Haharanman: v14 + "83h2mjr" -> 1280x720 + version token
    struct ain *haha = ain_new(14, 0);
    ain_add_string(haha, "83h2mjr");
    ain_add_global(haha, "g_cGameParam");
    ain_add_global(haha, "g_cHeroParam");
    config.game_name = strdup("Haharanman");
    config.view_width = 800;
    config.view_height = 600;
    apply_game_specific_hacks(haha);
    assert(game_haharanman == true && game_okusama == false);
    assert(config.view_width == 1280 && config.view_height == 720);
    v = SystemService_GetGameVersionByText();
    assert(strcmp(v->text, "bmjr78365hd5ghd2337fh2jf") == 0);
    free_string(v);
    ain_free(haha);
    free(config.game_name);

    // 5. Evenicle Gaiden: v6 + CBigMap + "aa6sdd5432" -> GetSceneNameList token, not game_evenicle
    struct ain *eg = ain_new(6, 0);
    ain_add_struct(eg, "CBigMap");
    ain_add_function(eg, "CMapPlayer@RunPathFindMove");
    ain_add_string(eg, "aa6sdd5432");
    config.game_name = strdup("EvenicleApc");
    apply_game_specific_hacks(eg);
    assert(game_evenicle_gaiden == true && game_evenicle == false && game_evenicle_rance == false);
    struct string *scn = NULL;
    SystemService_GetSceneNameList(&scn);
    assert(scn && strcmp(scn->text, "000aa6sd000000320000000d54") == 0);
    free_string(scn);
    ain_free(eg);
    free(config.game_name);

    // 6. Evenicle Rance: v6 + CBigMap + "56s5d4" -> GetSystemName token, not game_evenicle
    struct ain *er = ain_new(6, 0);
    ain_add_struct(er, "CBigMap");
    ain_add_function(er, "CMapPlayer@RunPathFindMove");
    ain_add_string(er, "56s5d4");
    config.game_name = strdup("EvenicleRance");
    apply_game_specific_hacks(er);
    assert(game_evenicle_rance == true && game_evenicle == false && game_evenicle_gaiden == false);
    struct string *sn = NULL;
    SystemService_GetSystemName(&sn);
    assert(sn && strcmp(sn->text, "s065605d4") == 0);
    free_string(sn);
    ain_free(er);
    free(config.game_name);

    // 7. Base Evenicle still gets game_evenicle + "828pg55b349"
    struct ain *ev1 = ain_new(6, 0);
    ain_add_struct(ev1, "CBigMap");
    ain_add_function(ev1, "CMapPlayer@RunPathFindMove");
    config.game_name = strdup("Evenicle");
    apply_game_specific_hacks(ev1);
    assert(game_evenicle == true && game_evenicle_rance == false && game_evenicle_gaiden == false);
    sn = NULL;
    SystemService_GetSystemName(&sn);
    assert(sn && strcmp(sn->text, "828pg55b349") == 0);
    free_string(sn);
    ain_free(ev1);
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

    def test_kiwi_v12_8ch_mixers_for_okusama_and_haharanman(self):
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

static void verify_8ch(void) {
    memset(&config, 0, sizeof(config));
    config.default_volume = 100;
    kiwi_prelink();
    assert(config.mixer_nr_channels == 8);
    assert(strcmp(config.mixer_channels[0], "Master") == 0);
    assert(strcmp(config.mixer_channels[3], "Voice") == 0);
    assert(strcmp(config.mixer_channels[6], "SystemVoice") == 0);
    assert(strcmp(config.mixer_channels[7], "Extra") == 0);
    assert(config.mixer_volumes[0] == 80);
    assert(config.mixer_volumes[3] == 100);
    assert(config.mixer_volumes[6] == 100);
    assert(config.mixer_volumes[7] == 80);

    mixer_init();
    assert(nr_mixers == 8);
    // Ambient (4), BackVoice (5), SystemVoice (6), Extra (7) must attach to Master (0), not Voice (3).
    for (int i = 1; i < nr_mixers; i++) {
        assert(mixers[i].parent == &mixers[0]);
    }
}

int main(int argc, char **argv) {
    assert(argc == 2);
    game_okusama = strcmp(argv[1], "okusama") == 0;
    game_haharanman = strcmp(argv[1], "haharanman") == 0;
    assert(game_okusama || game_haharanman);
    verify_8ch();

    puts("OK");
    return 0;
}
"""
        for mode in ("okusama", "haharanman"):
            out = compile_and_run(harness, LIBSYS4_CORE, ["-lz", "-lm"], args=[mode])
            self.assertEqual(out.strip(), "OK")

    def test_activity_okusama_cn_pact_and_hybrid_sjis_under_gbk(self):
        # 1. Official CN Okusama PACT tree with 子组件 / 种类別信息 / 组件类型 / 缺省组件 / 缺省名 /
        #    文本内容窗口 / 横进度槽组件 / 横スライダー条, plus a leaf 子パーツリスト alongside tree 子组件.
        def oku_state(kind: str, extra: list[bytes]) -> bytes:
            kids = [_ex_leaf(_gbk("组件类型"), 3, _ex_pstr(_gbk(kind))), *extra]
            return _ex_tree(_gbk("通常状态"), kids)

        def oku_part(name: str, kind: str, info_children: list[bytes],
                     children: list[bytes] | None = None,
                     add_dummy_leaf_list: bool = False) -> bytes:
            info = [_ex_leaf(_gbk("组件类型"), 3, _ex_pstr(_gbk(kind))), *info_children]
            fields = [
                _ex_leaf(_gbk("显示"), 1, struct.pack("<i", 1)),
                _ex_leaf(_gbk("点击许可"), 1, struct.pack("<i", 1)),
                _ex_tree(_gbk("种类別信息"), info),
            ]
            if add_dummy_leaf_list:
                # Leaf named 子パーツリスト must be skipped by child_parts_tree() in favor of tree 子组件.
                fields.append(_ex_leaf(_gbk("子パーツリスト"), 1, struct.pack("<i", 0)))
            if children is not None:
                fields.append(_ex_tree(_gbk("子组件"), children))
            return _ex_tree(_gbk(name), fields)

        oku_root = oku_part(
            "root_fg",
            "フォームグループ",
            [],
            children=[
                oku_part("flat_bg", "低等级组件", [
                    oku_state("缺省组件", [_ex_leaf(_gbk("缺省名"), 3, _ex_pstr(b"title_bg.flat"))]),
                ]),
                oku_part("gauge_h", "低等级组件", [
                    oku_state("横进度槽组件", [_ex_leaf(_gbk("ＣＧ名"), 3, _ex_pstr(b"gauge.cg"))]),
                ]),
                oku_part("slider_h", "横スライダー条", []),
                oku_part("msg_win", "文本内容窗口", [
                    _ex_leaf(_gbk("文本内容编号"), 1, struct.pack("<i", 42)),
                ]),
            ],
            add_dummy_leaf_list=True,
        )
        oku_ex = build_extf([
            _ex_block(_gbk("アクティビティ"), 6, _ex_tree(_gbk("アクティビティ"), [oku_root])),
        ])

        # 2. Hybrid Shift-JIS PACT (like Evenicle 2 Rance CN) loaded while sys4_text_is_gbk == true.
        sjis_label = "テスト"
        e2r_root = _ex_tree(_sjis("root_sjis"), [
            _ex_tree(_sjis("種類別情報"), [
                _ex_leaf(_sjis("パーツタイプ"), 3, _ex_pstr(_sjis("低レベルパーツ"))),
                _ex_tree(_sjis("通常状態"), [
                    _ex_leaf(_sjis("パーツタイプ"), 3, _ex_pstr(_sjis("テキストパーツ"))),
                    _ex_leaf(_sjis("テキスト"), 3, _ex_pstr(_sjis(sjis_label))),
                ]),
            ]),
            _ex_tree(_sjis("子パーツ"), [
                _ex_tree(_sjis("btn_sjis"), [
                    _ex_tree(_sjis("種類別情報"), [
                        _ex_leaf(_sjis("パーツタイプ"), 3, _ex_pstr(_sjis("ボタン"))),
                        _ex_leaf(_sjis("テキスト"), 3, _ex_pstr(_sjis(sjis_label))),
                    ]),
                ]),
            ]),
        ])
        e2r_ex = build_extf([
            _ex_block(_sjis("アクティビティ"), 6, _ex_tree(_sjis("アクティビティ"), [e2r_root])),
        ])

        expected_gbk_hex = "".join(f"\\x{b:02x}" for b in _gbk(sjis_label))

        harness = f"""
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
#include "system4/archive.h"
#include "system4/ex.h"
#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

struct ain *ain = NULL;
struct vm_pointer *heap = NULL;
size_t heap_size = 0;
bool parts_v2 = true;

_Noreturn void _vm_error(const char *fmt, ...) {{ abort(); }}

const char *parts_utf8_to_cjk_literal(const char *literal) {{
    if (!literal || !literal[0]) return "";
    static char bufs[64][256];
    static int next = 0;
    char *enc = utf2sjis(literal, strlen(literal));
    char *slot = bufs[next++ % 64];
    snprintf(slot, 256, "%s", enc);
    free(enc);
    return slot;
}}

static const char *g_pact_paths[2];
static int got_flat = 0, got_hgauge = 0, got_hslider = 0, got_msgwin = 0;
static int got_sjis_text_as_gbk = 0, got_sjis_btn_as_gbk = 0;
static struct parts fake_parts[64];
static struct parts_text dummy_texts[64];
static int next_pno = 1;

struct page *array_pushback(struct page *dst, union vm_value v, enum ain_data_type dt, int st) {{ return dst; }}
bool asset_exists_by_name(enum asset_type t, const char *n, int *id) {{ return false; }}
static void dummy_archive_free_data(struct archive_data *d) {{ free(d->data); free(d); }}
static struct archive_ops dummy_archive_ops = {{ .free_data = dummy_archive_free_data }};
static struct archive dummy_archive = {{ .ops = &dummy_archive_ops }};
struct archive_data *asset_get_by_name(enum asset_type t, const char *n, int *id_out) {{
    int idx = !strcmp(n, "oku.pactex") ? 0 : !strcmp(n, "e2r.pactex") ? 1 : -1;
    if (idx < 0) return NULL;
    size_t sz = 0;
    uint8_t *raw = file_read(g_pact_paths[idx], &sz);
    assert(raw);
    struct archive_data *d = xcalloc(1, sizeof(*d));
    d->archive = &dummy_archive;
    d->data = raw;
    d->size = sz;
    return d;
}}
const char *display_sjis0(const char *s) {{ return s ? s : ""; }}
char *gamedir_path_icase(const char *p) {{ return NULL; }}
void iarray_write(struct iarray_writer *w, int v) {{}}
void iarray_write_string(struct iarray_writer *w, struct string *s) {{}}
int iarray_read(struct iarray_reader *r) {{ return 0; }}
struct string *iarray_read_string(struct iarray_reader *r) {{ return NULL; }}
int mainex_v12_register(struct ex *e) {{ return 2; }}
int mainex_v12_register_at(int i, struct ex *e) {{ return 2; }}
void mainex_v12_unregister_activity(int i) {{}}
int PE_GetFreeNumber(void) {{ return next_pno++; }}
struct parts *parts_get(int no) {{ struct parts *p = &fake_parts[no]; p->no = no; return p; }}
struct parts *parts_try_get(int no) {{ return (no > 0 && no < next_pno) ? &fake_parts[no] : NULL; }}
void parts_release(int no) {{}}
void parts_state_reset(struct parts_state *s, enum parts_type t) {{}}
void parts_set_dims(struct parts *p, struct parts_common *c, int w, int h) {{}}
void parts_set_scale_x(struct parts *p, float s) {{}}
void parts_set_scale_y(struct parts *p, float s) {{}}
struct parts_cg *parts_get_cg(struct parts *p, int s) {{ return NULL; }}
struct parts_construction_process *parts_get_construction_process(struct parts *p, int s) {{ return NULL; }}
struct parts_text *parts_get_text(struct parts *p, int s) {{ return &dummy_texts[p->no]; }}
void parts_message_window_create(struct parts *p) {{ p->component_type = 10; got_msgwin++; }}
void parts_message_window_free(struct parts *p) {{}}
void parts_panel_create(struct parts *p) {{}}
void parts_panel_free(struct parts *p) {{}}
void parts_checkbox_create(struct parts *p) {{}}
void parts_scrollbar_create(struct parts *p, bool h) {{}}
void parts_scrollbar_free(struct parts *p) {{}}
int parts_scrollbar_range(struct parts_scrollbar *s) {{ return 0; }}
void parts_scrollbar_rebuild(struct parts *p) {{}}
void parts_sliderbar_create(struct parts *p, bool h) {{
    assert(h == true);
    p->scrollbar = xcalloc(1, sizeof(struct parts_scrollbar));
    for (int r = 0; r < 2; r++)
        for (int i = 0; i < 5; i++)
            p->scrollbar->names[r][i] = string_ref(&EMPTY_STRING);
    got_hslider++;
}}
void parts_widget_create_deferred(struct parts *p, int t) {{}}
void parts_widget_create(struct parts *p) {{}}
void parts_widget_end_batch(struct parts *p) {{}}
void parts_widget_free(struct parts *p) {{}}
bool parts_cp_add_v14(int parts_no, const struct parts_cp_v14 *src, int state) {{ return true; }}
void parts_cp_set_last_fill_full_size(int parts_no, int state, bool full_size) {{}}
void PE_Parts_SetSoundName(int a, struct string *b, int c) {{}}

#include "parts/activity.c"

void PE_SetParentPartsNumber(int c, int p) {{}}
bool PE_SetHGaugeCG(int no, struct string *s, int st) {{ if (s && !strcmp(s->text, "gauge.cg")) got_hgauge++; return true; }}
bool PE_SetVGaugeCG(int no, struct string *s, int st) {{ return true; }}
bool PE_SetLoopCG(int no, struct string *s, int start, int nr, int spd, int st) {{ return true; }}
void PE_SetCheckBoxOverrideCheckedCGName(int a, struct string *b) {{}}
void PE_SetButtonSize(int a, int b, int c) {{}}
void PE_SetButtonEnable(int a, bool b) {{}}
void PE_SetButtonColor(int a, int b, int c, int d) {{}}
void PE_SetButtonFontProperty(int a, int b, int c, int d, int e, int f, float g, int h, int i, int j, float k) {{}}
void PE_SetButtonText(int a, struct string *b) {{
    if (b && !strcmp(b->text, "{expected_gbk_hex}")) got_sjis_btn_as_gbk++;
}}
void PE_SetButtonTextOriginPosMode(int a, int b) {{}}
void PE_SetButtonCharSpace(int a, int b) {{}}
void PE_SetButtonLineSpace(int a, int b) {{}}
void PE_SetButtonCGName(int a, struct string *b) {{}}
void PE_SetButtonFlatName(int a, struct string *b) {{}}
void PE_SetCheckBoxButtonMode(int a, bool b) {{}}
void PE_SetCheckBoxCGName(int a, struct string *b) {{}}
void PE_SetCheckBoxCharSpace(int a, int b) {{}}
void PE_SetCheckBoxColor(int a, int b, int c, int d) {{}}
void PE_SetCheckBoxEnable(int a, bool b) {{}}
void PE_SetCheckBoxFlatName(int a, struct string *b) {{}}
void PE_SetCheckBoxFontProperty(int a, int b, int c, int d, int e, int f, float g, int h, int i, int j, float k) {{}}
void PE_SetCheckBoxLineSpace(int a, int b) {{}}
void PE_SetCheckBoxOverrideCheckedFlatName(int a, struct string *b) {{}}
void PE_SetCheckBoxOverrideCGName(int a, struct string *b) {{}}
void PE_SetCheckBoxOverrideFlatName(int a, struct string *b) {{}}
void PE_SetCheckBoxSize(int a, int b, int c) {{}}
void PE_SetCheckBoxText(int a, struct string *b) {{}}
void PE_SetCheckBoxTextOriginPosMode(int a, int b) {{}}
void PE_SetClickable(int a, bool b) {{}}
void PE_SetComment(int a, struct string *b) {{}}
void PE_SetComponentCheckBoxShowLinkNumber(int a, int b) {{}}
void PE_SetComponentMargin(int a, int b, int c, int d, int e) {{}}
void PE_SetComponentPos(int a, float b, float c) {{}}
void PE_SetComponentSubColorMode(int a, bool b) {{}}
void PE_SetDrag(int a, bool b) {{}}
void PE_SetEnableSpeedUp(int a, bool b) {{}}
void PE_SetFlatFPSRate(int a, float b, int c) {{}}
bool PE_SetHGaugeRate(int a, float b, float c, int d) {{ return true; }}
void PE_SetHGaugeReverse(int a, bool b, int c) {{}}
bool PE_SetHGaugeSurfaceArea(int a, int b, int c, int d, int e, int f) {{ return true; }}
void PE_SetInputState(int a, int b) {{}}
void PE_SetKeyWaitCGName(int a, struct string *b, int c, int d, int e) {{}}
void PE_SetKeyWaitFlatName(int a, struct string *b) {{}}
void PE_SetKeyWaitPos(int a, int b, int c, int d) {{}}
void PE_SetLayoutBoxAlign(int a, int b) {{}}
void PE_SetLayoutBoxLayoutType(int a, int b) {{}}
void PE_set_layoutbox_padding(int a, int b, int c, int d, int e) {{}}
void PE_SetLayoutBoxReturnF(int a, bool b, float c) {{}}
void PE_SetLayoutBoxReturnSizeForRate(int a, bool b) {{}}
bool PE_SetLoopCGSurfaceArea(int a, int b, int c, int d, int e, int f) {{ return true; }}
void PE_SetMessageWindowActive(int a, bool b) {{}}
void PE_SetMessageWindowCGName(int a, struct string *b) {{}}
void PE_SetMessageWindowFlatName(int a, struct string *b) {{}}
void PE_SetMessageWindowInactiveMultipleColor(int a, int b, int c, int d) {{}}
void PE_SetMessageWindowRubyCharSpace(int a, int b) {{}}
void PE_SetMessageWindowRubyFont(int a, int b, int c, int d, int e, int f, float g, int h, int i, int j, float k) {{}}
void PE_SetMessageWindowRubyLineSpace(int a, int b) {{}}
void PE_SetMessageWindowText(int a, struct string *b, int c, struct string *d, int e, int f) {{
    assert(c == 42);
}}
void PE_SetMessageWindowTextArea(int a, int b, int c, int d, int e) {{}}
void PE_SetMessageWindowTextFont(int a, int b, int c, int d, int e, int f, float g, int h, int i, int j, float k) {{}}
void PE_SetMessageWindowTextOriginPosMode(int a, int b) {{}}
void PE_SetMessageWindowTextSpace(int a, int b, int c) {{}}
void PE_SetMessageWindowTextSpeed(int a, int b) {{}}
void PE_SetMultiplyColor(int a, int b, int c, int d) {{}}
bool PE_SetNumeralCG(int a, struct string *b, int c) {{ return true; }}
void PE_SetNumeralFont(int a, int b, int c, int d, int e, int f, float g, int h, int i, int j, float k, int l) {{}}
void PE_SetNumeralFullPitch(int a, bool b, int c) {{}}
bool PE_SetNumeralLength(int a, int b, int c) {{ return true; }}
bool PE_SetNumeralLinkedCGNumberWidthWidthList(int a, struct string *b, int c, int d, int e, int f, int g, int h, int i, int j, int k, int l, int m, int n, int o) {{ return true; }}
bool PE_SetNumeralNumber(int a, int b, int c) {{ return true; }}
bool PE_SetNumeralShowComma(int a, bool b, int c) {{ return true; }}
void PE_SetNumeralShowPadding(int a, bool b, int c) {{}}
void PE_SetNumeralShowType(int a, int b, int c) {{}}
bool PE_SetNumeralSpace(int a, int b, int c) {{ return true; }}
bool PE_SetNumeralSurfaceArea(int a, int b, int c, int d, int e, int f) {{ return true; }}
void PE_SetOnCursorShowLinkPartsNumber(int a, int b) {{}}
void PE_SetPanelAlphaGradationBottom(int a, int b) {{}}
void PE_SetPanelAlphaGradationLeft(int a, int b) {{}}
void PE_SetPanelAlphaGradationRight(int a, int b) {{}}
void PE_SetPanelAlphaGradationTop(int a, int b) {{}}
void PE_SetPanelColor(int a, int b, int c, int d, int e) {{}}
void PE_SetPanelSize(int a, int b, int c) {{}}
void PE_SetPartsAlphaClipperPartsNumber(int a, int b) {{}}
bool PE_SetPartsCG(int a, struct string *b, int c, int d) {{ return true; }}
bool PE_SetPartsCGDetectionSize(int a, struct string *b, int c) {{ return true; }}
bool PE_SetPartsCGSurfaceArea(int a, int b, int c, int d, int e, int f) {{ return true; }}
bool PE_SetPartsConstructionSurfaceArea(int a, int b, int c, int d, int e, int f) {{ return true; }}
void PE_SetPartsDrawFilter(int a, int b) {{}}
bool PE_SetPartsFlat(int a, struct string *b, int c) {{
    if (b && !strcmp(b->text, "title_bg.flat")) got_flat++;
    return true;
}}
bool PE_SetPartsFlatSurfaceArea(int a, int b, int c, int d, int e, int f) {{ return true; }}
bool PE_SetPartsFontBoldWeight(int a, float b, int c) {{ return true; }}
bool PE_SetPartsFontColor(int a, int b, int c, int d, int e) {{ return true; }}
bool PE_SetPartsFontEdgeColor(int a, int b, int c, int d, int e) {{ return true; }}
bool PE_SetPartsFontEdgeWeight(int a, float b, int c) {{ return true; }}
bool PE_SetPartsFontSize(int a, int b, int c) {{ return true; }}
bool PE_SetPartsFontType(int a, int b, int c) {{ return true; }}
void PE_SetPartsMagX(int a, float b) {{}}
void PE_SetPartsMagY(int a, float b) {{}}
void PE_SetPartsMessageWindowShowLink(int a, bool b) {{}}
void PE_SetPartsOriginPosMode(int a, int b) {{}}
void PE_SetPartsPixelDecide(int a, bool b) {{}}
void PE_SetPartsRotateX(int a, float b) {{}}
void PE_SetPartsRotateY(int a, float b) {{}}
void PE_SetPartsRotateZ(int a, float b) {{}}
bool PE_SetPartsTextSurfaceArea(int a, int b, int c, int d, int e, int f) {{ return true; }}
void PE_SetPassCursor(int a, bool b) {{}}
void PE_SetRectangleDetectionPoint(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {{}}
void PE_SetShow(int a, bool b) {{}}
bool PE_SetText(int a, struct string *b, int c) {{
    if (b && !strcmp(b->text, "{expected_gbk_hex}")) got_sjis_text_as_gbk++;
    return true;
}}
void PE_SetTextBoxText(int a, struct string *b) {{}}
void PE_SetTextBoxReadOnly(int a, bool b) {{}}
void PE_SetTextBoxMaxTextLength(int a, int b) {{}}
bool PE_SetTextCharSpace(int a, int b, int c) {{ return true; }}
bool PE_SetTextLineSpace(int a, int b, int c) {{ return true; }}
bool PE_SetVGaugeRate(int a, float b, float c, int d) {{ return true; }}
void PE_SetVGaugeReverse(int a, bool b, int c) {{}}
bool PE_SetVGaugeSurfaceArea(int a, int b, int c, int d, int e, int f) {{ return true; }}
void PE_SetWheelable(int a, bool b) {{}}
void PE_SetZ(int a, int b) {{}}
void PE_SetAddColor(int a, int b, int c, int d) {{}}
void PE_SetAlpha(int a, int b) {{}}
void PE_UpdateComponent(int a) {{}}
void PE_ChangeFlatCG(int a, struct string *b, struct string *c, int d) {{}}
void PE_ChangeFlatSound(int a, struct string *b, struct string *c, int d) {{}}
bool PE_GoFramePartsFlat(int a, int b, int c) {{ return true; }}
bool PE_AddAddFilterToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h, bool i, int j) {{ return true; }}
bool PE_AddAlphaBlendTextToPartsConstructionProcess(int a, int b, int c, struct string *d, int e, int f, int g, int h, int i, float j, int k, int l, int m, float n, int o, int p, int q) {{ return true; }}
bool PE_AddCGBlendToPartsConstructionProcess(int a, struct string *b, int c, int d, int e) {{ return true; }}
bool PE_AddCopyCutCGToPartsConstructionProcess(int a, struct string *b, int c, int d, int e, int f, int g, int h, int i, int j, int k, int l) {{ return true; }}
bool PE_AddCopyTextToPartsConstructionProcess(int a, int b, int c, struct string *d, int e, int f, int g, int h, int i, float j, int k, int l, int m, float n, int o, int p, int q) {{ return true; }}
bool PE_AddCreateCGToProcess(int a, struct string *b, int c) {{ return true; }}
bool PE_AddCreatePixelOnlyToPartsConstructionProcess(int a, int b, int c, int d) {{ return true; }}
bool PE_AddCreateToPartsConstructionProcess(int a, int b, int c, int d) {{ return true; }}
bool PE_AddCutCGAlphaBlendToPartsConstructionProcess(int a, struct string *b, int c, int d, int e, int f, int g, int h, int i, int j, int k, bool l, int m) {{ return true; }}
bool PE_AddCutCGOnlyAlphaToPartsConstructionProcess(int a, struct string *b, int c, int d, int e, int f, int g, int h, int i, int j, int k, bool l, int m) {{ return true; }}
bool PE_AddDrawCutCGToPartsConstructionProcess(int a, struct string *b, int c, int d, int e, int f, int g, int h, int i, int j, int k, int l) {{ return true; }}
bool PE_AddDrawLineToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {{ return true; }}
bool PE_AddDrawRectToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h, int i) {{ return true; }}
bool PE_AddDrawTextToPartsConstructionProcess(int a, int b, int c, struct string *d, int e, int f, int g, int h, int i, float j, int k, int l, int m, float n, int o, int p, int q) {{ return true; }}
bool PE_AddFillAlphaColorToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {{ return true; }}
bool PE_AddFillAMapToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g) {{ return true; }}
bool PE_AddFillGradationHorizonToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j, int k, int l) {{ return true; }}
bool PE_AddFillToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h, int i) {{ return true; }}
bool PE_AddFillWithAlphaToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {{ return true; }}
bool PE_AddGrayFilterToPartsConstructionProcess(int a, int b, int c, int d, int e, bool f, int g) {{ return true; }}
bool PE_AddHBlurFilterToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, bool g, int h) {{ return true; }}
bool PE_AddMulAMapGradationHorizonToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h) {{ return true; }}
bool PE_AddMulAMapGradationVerticalToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h) {{ return true; }}
bool PE_AddMulFilterToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, int g, int h, bool i, int j) {{ return true; }}
bool PE_AddOnlyAlphaTextToPartsConstructionProcess(int a, int b, int c, struct string *d, int e, int f, int g, int h, int i, float j, int k, int l, int m, float n, int o, int p, int q) {{ return true; }}
bool PE_AddVBlurFilterToPartsConstructionProcess(int a, int b, int c, int d, int e, int f, bool g, int h) {{ return true; }}
bool PE_BuildPartsConstructionProcess(int a, int b) {{ return true; }}
void PE_AddRadioButtonBoxChild(int a, int b) {{}}
void PE_ClearRadioButtonBoxChild(int a) {{}}

int main(int argc, char **argv) {{
    assert(argc == 3);
    g_pact_paths[0] = argv[1];
    g_pact_paths[1] = argv[2];
    sys4_set_text_gbk(true);

    struct string *n1 = cstr_to_string("oku");
    struct string *f1 = cstr_to_string("oku.pactex");
    assert(PE_ReadActivityFile(n1, f1, false));
    assert(got_flat == 1);
    assert(got_hgauge == 1);
    assert(got_hslider == 1);
    assert(got_msgwin == 1);

    struct string *n2 = cstr_to_string("e2r");
    struct string *f2 = cstr_to_string("e2r.pactex");
    assert(PE_ReadActivityFile(n2, f2, false));
    assert(got_sjis_text_as_gbk == 1);
    assert(got_sjis_btn_as_gbk == 1);
    assert(sys4_text_is_gbk == true);

    free_string(n1); free_string(f1);
    free_string(n2); free_string(f2);
    puts("OK");
    return 0;
}}
"""
        with tempfile.TemporaryDirectory() as tmpdir:
            p1 = Path(tmpdir) / "oku.pactex"
            p2 = Path(tmpdir) / "e2r.pactex"
            p1.write_bytes(oku_ex)
            p2.write_bytes(e2r_ex)
            out = compile_and_run(harness, LIBSYS4_CORE, ["-lz", "-lm"], [str(p1), str(p2)])
            self.assertEqual(out.strip(), "OK")

    def test_widgets_and_scrollbar_sjis_suffix_fallback_under_gbk(self):
        fn_w_cjk = extract_function("src/parts/widgets.c", "parts_utf8_to_cjk_literal")
        fn_w_enc = extract_function("src/parts/widgets.c", "load_skin_with_encoded_suffix")
        fn_w_load = extract_function("src/parts/widgets.c", "load_skin")
        fn_sb_enc = extract_function("src/parts/scrollbar.c", "load_skin_enc")
        fn_sb_load = extract_function("src/parts/scrollbar.c", "load_skin")
        # Rename scrollbar load_skin to avoid collision with widgets load_skin
        fn_sb_load = fn_sb_load.replace("load_skin(", "sb_load_skin(", 1)

        sjis_base = "".join(f"\\x{b:02x}" for b in _sjis("ボタン"))
        sjis_normal = "".join(f"\\x{b:02x}" for b in _sjis("ボタン／通常"))
        sjis_bar_on = "".join(f"\\x{b:02x}" for b in _sjis("ボタン／バー／オン"))

        harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parts.h"
#include "parts_internal.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

static int cg_calls = 0;
void parts_state_reset(struct parts_state *s, enum parts_type t) {{ s->type = t; }}
bool parts_flat_load(struct parts *p, struct parts_flat *f, struct string *name) {{ return false; }}
bool parts_cg_set(struct parts *p, struct parts_cg *cg, struct string *name) {{
    cg_calls++;
    return !strcmp(name->text, "{sjis_normal}") || !strcmp(name->text, "{sjis_bar_on}");
}}

{fn_w_cjk}
{fn_w_enc}
{fn_w_load}
{fn_sb_enc}
{fn_sb_load}

int main(void) {{
    sys4_set_text_gbk(true);
    struct parts p = {{0}};
    struct parts_state st = {{0}};
    struct string *base = cstr_to_string("{sjis_base}");

    // Widget load_skin: GBK suffix fails first, then Shift-JIS fallback succeeds and restores GBK mode.
    cg_calls = 0;
    assert(load_skin(&p, &st, base, false, "／通常") == true);
    assert(cg_calls == 2);
    assert(sys4_text_is_gbk == true);

    // Scrollbar load_skin: GBK piece+suffix fails first, then Shift-JIS fallback succeeds and restores GBK mode.
    cg_calls = 0;
    assert(sb_load_skin(&p, &st, base, false, "／バー", "／オン") == true);
    assert(cg_calls == 2);
    assert(sys4_text_is_gbk == true);

    free_string(base);
    puts("OK");
    return 0;
}}
"""
        out = compile_and_run(harness, LIBSYS4_CORE, ["-lz", "-lm"])
        self.assertEqual(out.strip(), "OK")

    def test_asset_manager_mounts_chs_and_chs2_ahead_of_base(self):
        import zlib

        def build_afa_v2_ts(entries: list[tuple[bytes, bytes, int]]) -> bytes:
            info_raw = bytearray()
            data_buf = bytearray()
            data_off_rel = 8
            for idx, (name, payload, ts) in enumerate(entries):
                padded = len(name)
                info_raw.extend(struct.pack("<ii", len(name), padded))
                info_raw.extend(name)
                info_raw.extend(struct.pack("<iIIii", idx + 1, ts, 0, data_off_rel + len(data_buf), len(payload)))
                data_buf.extend(payload)
            info_comp = zlib.compress(bytes(info_raw))
            data_start = 44 + len(info_comp)
            header = (
                b"AFAH"
                + struct.pack("<I", 0x1C)
                + b"AlicArch"
                + struct.pack("<III", 2, 0, data_start)
                + b"INFO"
                + struct.pack("<III", len(info_comp) + 16, len(info_raw), len(entries))
            )
            data_hdr = b"DATA" + struct.pack("<I", 8 + len(data_buf))
            return header + info_comp + data_hdr + bytes(data_buf)

        sjis_menu = _sjis("メニュー")
        gbk_menu = _gbk("菜单")
        query_menu = _sjis("タイトル")

        with tempfile.TemporaryDirectory() as tmpdir:
            gdir = Path(tmpdir)
            (gdir / "Evenicle2Rance.ain").write_bytes(b"AIN")
            # Base archives have NEWER nonzero timestamps (200 / 400) than CHS/CHS2 (100 / 300).
            (gdir / "Evenicle2RanceCG.afa").write_bytes(build_afa_v2_ts([
                (b"title_btn", b"JP_CG_NEWER", 200),
                (b"mod_btn", b"JP_MOD_BTN", 400),
                (b"base_only", b"BASE_CG", 200),
                (sjis_menu, b"JP_CANON_NEWER", 200),
            ]))
            (gdir / "Evenicle2RanceCHS.afa").write_bytes(build_afa_v2_ts([
                (b"title_btn", b"CN_CG_OLDER", 100),
                (b"mod_btn", b"CN_MOD_BTN", 300),
                (gbk_menu, b"CN_CANON_OLDER", 100),
            ]))
            (gdir / "Evenicle2RanceFlat.afa").write_bytes(build_afa_v2_ts([
                (b"title_banner", b"JP_FLAT_NEWER", 200),
                (b"base_flat", b"BASE_FLAT", 200),
            ]))
            (gdir / "Evenicle2RanceCHS2.afa").write_bytes(build_afa_v2_ts([
                (b"title_banner", b"CN_FLAT_OLDER", 100),
            ]))
            # User mod archive with oldest timestamp (50) must still beat both CHS and Base.
            mod_afa = gdir / "UserMod.afa"
            mod_afa.write_bytes(build_afa_v2_ts([
                (b"mod_btn", b"MOD_CG_OLDEST", 50),
            ]))

            sjis_menu_c = "".join(f"\\x{b:02x}" for b in sjis_menu)
            gbk_menu_c = "".join(f"\\x{b:02x}" for b in gbk_menu)
            query_menu_c = "".join(f"\\x{b:02x}" for b in query_menu)

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
    (void)sjis_cap; (void)gbk_cap;
    if (sjis_out) *sjis_out = NULL;
    if (gbk_out) *gbk_out = NULL;
    if (!strcmp(s, "{sjis_menu_c}") || !strcmp(s, "{gbk_menu_c}") || !strcmp(s, "{query_menu_c}")) {{
        strcpy(sjis_buf, "CANON_MENU");
        if (sjis_out) *sjis_out = sjis_buf;
        if (gbk_out) {{
            strcpy(gbk_buf, "CANON_MENU");
            *gbk_out = gbk_buf;
        }}
    }}
}}
char *save_transcode_cstr_strict(const char *s, size_t len, bool src_is_gbk, bool dst_is_gbk, size_t *out_len) {{
    (void)s; (void)len; (void)src_is_gbk; (void)dst_is_gbk; (void)out_len;
    return NULL;
}}

#include "asset_manager.c"

static void check_entry(enum asset_type type, const char *name, const char *expected) {{
    struct archive_data *d = asset_get_by_name(type, name, NULL);
    assert(d != NULL);
    assert(d->size == strlen(expected));
    assert(memcmp(d->data, expected, d->size) == 0);
    archive_free_data(d);
}}

int main(int argc, char **argv) {{
    assert(argc == 3);
    memset(&config, 0, sizeof(config));
    config.game_dir = argv[1];
    config.ain_filename = "Evenicle2Rance.ain";
    asset_manager_init();

    // 1. CHS / CHS2 beat base archives in raw lookup even when base timestamps are newer (200 > 100).
    check_entry(ASSET_CG, "title_btn", "CN_CG_OLDER");
    check_entry(ASSET_CG, "base_only", "BASE_CG");
    check_entry(ASSET_FLAT, "title_banner", "CN_FLAT_OLDER");
    check_entry(ASSET_FLAT, "base_flat", "BASE_FLAT");

    // 2. CHS beats base archive in canonical non-ASCII lookup even when base timestamp is newer (200 > 100).
    check_entry(ASSET_CG, "{query_menu_c}", "CN_CANON_OLDER");

    // 3. Before mounting mod, CHS (ts=300) beats base (ts=400).
    check_entry(ASSET_CG, "mod_btn", "CN_MOD_BTN");

    // 4. After mounting user mod (ts=50), mod priority still beats both CHS (300) and Base (400),
    //    while non-mod entries still resolve to CHS and Base fallback.
    assert(asset_manager_load_archive(ASSET_CG, argv[2]) == true);
    check_entry(ASSET_CG, "mod_btn", "MOD_CG_OLDEST");
    check_entry(ASSET_CG, "title_btn", "CN_CG_OLDER");
    check_entry(ASSET_CG, "base_only", "BASE_CG");

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
                [str(gdir), str(mod_afa)],
            )
            self.assertEqual(out.strip(), "OK")


if __name__ == "__main__":
    unittest.main()
