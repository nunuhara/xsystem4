#!/usr/bin/env python3
"""Native C regression tests for:
1. Toushin 3 battle crash (Shift-JIS .pae/.3de/.txtex.ex parsing under GBK mode + pae_free NULL safety).
2. Cross-version AIN encoding detection (Shift-JIS CN repacks like Rance IX with GBK GameName,
   GBK SC/TC repacks with JP/EN GameName, large AIN late-message GBK, UTF-8 AIN with non-UTF-8 INI).
"""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

from test_evenicle2_regressions import (
    LIBSYS4_CORE,
    ROOT,
    compile_and_run,
)


def _extract_def(rel_path: str, name: str) -> str:
    source = (ROOT / rel_path).read_text(encoding="utf-8", errors="ignore")
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


def _build_ai2(messages: list[bytes], strings: list[bytes] | None = None) -> bytes:
    parts = [
        b"VERS", struct.pack("<I", 4),
        b"FUNC", struct.pack("<I", 0),
        b"STRT", struct.pack("<I", 0),
        b"MSG0", struct.pack("<I", len(messages)),
        b"".join(m + b"\x00" for m in messages),
    ]
    if strings is not None:
        parts.extend([
            b"STR0", struct.pack("<I", len(strings)),
            b"".join(s + b"\x00" for s in strings),
        ])
    raw = b"".join(parts)
    comp = zlib.compress(raw)
    return b"AI2\x00\x00\x00\x00\x00" + struct.pack("<II", len(raw), len(comp)) + comp


class Toushin3PaeAndEncodingTests(unittest.TestCase):
    def test_toushin3_sjis_pae_and_s3de_under_gbk_and_malformed_safety(self):
        # Minimal Shift-JIS .pae using Japanese keywords, Shift-JIS bone name ("刀ボーン00"),
        # and Shift-JIS texture name ("コピー ～ furea_aka.tga").
        pae_sjis = (
            'オブジェクト "01" = {\r\n'
            '  種類 = ビルボード\r\n'
            '  移動タイプ = 直線\r\n'
            '  位置 = ボーン[ 0 , "刀ボーン00" , 0.0 , 1.0 , 2.0 ]\r\n'
            '  テクスチャ = "コピー ～ furea_aka.tga"\r\n'
            '  フレーム = 0 , 30\r\n'
            '  個数 = 1\r\n'
            '}\r\n'
        ).encode("cp932")

        # Malformed .pae WITHOUT any trailing newline (tests parse_error eol == NULL AND pae_free textures == NULL).
        pae_bad_no_nl = b'UNKNOWN_TOPLEVEL "bad" = {'
        pae_bad_nl = b'UNKNOWN_TOPLEVEL "bad" = {\n  x = 1\n}\n'

        # Minimal Shift-JIS .3de and GBK-translated .3de (testing Shift-JIS-first + GBK fallback).
        s3de_sjis = (
            'オブジェクト "01" = {\r\n'
            '  種類 = ビルボード\r\n'
            '}\r\n'
        ).encode("cp932")
        s3de_gbk = (
            'オブジェクト "01" = {\r\n'
            '  种类 = ビルボード\r\n'
            '}\r\n'
        ).encode("gbk")

        with tempfile.TemporaryDirectory() as tmpdir:
            td = Path(tmpdir)
            (td / "good.pae").write_bytes(pae_sjis)
            (td / "bad_no_nl.pae").write_bytes(pae_bad_no_nl)
            (td / "bad_nl.pae").write_bytes(pae_bad_nl)
            (td / "good_sjis.3de").write_bytes(s3de_sjis)
            (td / "good_gbk.3de").write_bytes(s3de_gbk)

            harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cglm/cglm.h>

#include "system4.h"
#include "system4/archive.h"
#include "system4/file.h"
#include "system4/hashtable.h"
#include "system4/utfsjis.h"
#include "3d_internal.h"

static const char *g_dir = NULL;
static char g_last_tex_name[256] = {0};

void glDeleteTextures(GLsizei n, const GLuint *textures) { (void)n; (void)textures; }
void RE_renderer_free(struct RE_renderer *r) { (void)r; }
void model_free(struct model *m) { (void)m; }
struct model *model_load(struct archive *aar, const char *path, enum RE_plugin_version v) {
    (void)aar; (void)path; (void)v; return NULL;
}
const char *RE_path_basename(const char *path) {
    const char *p = strrchr(path, '\\');
    const char *q = strrchr(path, '/');
    if (q && (!p || q > p)) p = q;
    return p ? p + 1 : path;
}
char *RE_path_dirname(const char *path) {
    const char *p = strrchr(path, '\\');
    const char *q = strrchr(path, '/');
    if (q && (!p || q > p)) p = q;
    if (!p) return xstrdup("");
    char *r = xmalloc((size_t)(p - path) + 1);
    memcpy(r, path, (size_t)(p - path));
    r[p - path] = '\0';
    return r;
}

static void dummy_free_data(struct archive_data *d) {
    if (d) {
        free(d->data);
        free(d->name);
        free(d);
    }
}
static struct archive_ops dummy_ops = { .free_data = dummy_free_data };
static struct archive dummy_archive = { .ops = &dummy_ops };

struct archive_data *RE_get_aar_entry(struct archive *aar, const char *dir, const char *name, const char *ext) {
    (void)aar; (void)dir;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s%s", g_dir, name, ext ? ext : "");
    size_t sz = 0;
    uint8_t *buf = file_read(path, &sz);
    if (!buf)
        return NULL;
    struct archive_data *d = xcalloc(1, sizeof(*d));
    d->data = buf;
    d->size = sz;
    d->name = xstrdup(name);
    d->archive = &dummy_archive;
    return d;
}

GLuint RE_load_aar_texture(struct archive *aar, const char *dir, const char *name,
        const char **exts, int nr_exts, bool *has_alpha_out) {
    (void)aar; (void)dir; (void)exts; (void)nr_exts;
    snprintf(g_last_tex_name, sizeof(g_last_tex_name), "%s", name ? name : "");
    if (has_alpha_out)
        *has_alpha_out = true;
    return 7;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    g_dir = argv[1];

    // Simulate Toushin 3 CN runtime: sys4_text_is_gbk = true.
    sys4_set_text_gbk(true);

    // 1. Shift-JIS .pae parses cleanly in GBK mode, preserving Shift-JIS bone & texture names.
    struct pae *pe = pae_load(&dummy_archive, "Data\\ParticleEffect\\good", RE_REIGN_PLUGIN);
    assert(pe != NULL);
    assert(pe->nr_objects == 1);
    struct pae_object *obj = &pe->objects[0];
    assert(obj->nr_textures == 1);
    // Expected CP932 bytes for "コピー ～ furea_aka.tga"
    const char *expected_tex_sjis = "\x83\x52\x83\x73\x81\x5b \x81\x60 furea_aka.tga";
    assert(strcmp(obj->textures[0], expected_tex_sjis) == 0);
    assert(strcmp(g_last_tex_name, expected_tex_sjis) == 0);

    // Expected CP932 bytes for "刀ボーン00"
    const char *expected_bone_sjis = "\x93\x81\x83\x7b\x81\x5b\x83\x93" "00";
    assert(obj->position[0] != NULL);
    assert(obj->position[0]->units[0].type == PARTICLE_POS_UNIT_BONE);
    assert(strcmp(obj->position[0]->units[0].bone.name, expected_bone_sjis) == 0);
    pae_free(pe);

    // 2. Malformed .pae (both without newline and with newline) must fail cleanly without SIGSEGV!
    assert(pae_load(&dummy_archive, "Data\\ParticleEffect\\bad_no_nl", RE_REIGN_PLUGIN) == NULL);
    assert(pae_load(&dummy_archive, "Data\\ParticleEffect\\bad_nl", RE_REIGN_PLUGIN) == NULL);

    // 3. NULL safety on pae_free and pae_calc_frame_range.
    pae_free(NULL);
    struct pae *empty_pe = xcalloc(1, sizeof(*empty_pe));
    pae_free(empty_pe);
    struct motion mot;
    memset(&mot, 0, sizeof(mot));
    pae_calc_frame_range(NULL, &mot);

    // 4. Shift-JIS .3de loads cleanly under GBK mode.
    struct s3de *s = s3de_load(&dummy_archive, "Data\\Effect\\good_sjis");
    assert(s != NULL);
    assert(s->nr_objects == 1);
    s3de_free(s);

    // 5. GBK-translated .3de also loads cleanly under GBK mode via GBK fallback.
    struct s3de *sg = s3de_load(&dummy_archive, "Data\\Effect\\good_gbk");
    assert(sg != NULL);
    assert(sg->nr_objects == 1);
    s3de_free(sg);

    puts("OK");
    return 0;
}
"""
            sources = [
                ROOT / "src/3d/particle.c",
                ROOT / "src/3d/s3de.c",
                ROOT / "src/3d/parser.c",
                *LIBSYS4_CORE,
            ]
            out = compile_and_run(
                harness,
                sources,
                [f"-I{ROOT / 'src/3d'}", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-lz", "-lm"],
                [str(td)],
            )
            self.assertEqual(out.strip(), "OK")

    def test_cross_version_ain_encoding_detection(self):
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xsystem4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

void apply_game_specific_hacks(struct ain *ain);
struct config config;
void message_layout_init_rance03(void) {}
_Noreturn void _vm_error(const char *fmt, ...) { abort(); }

static void add_messages_n(struct ain *ain, const char *text, int n) {
    int old = ain->nr_messages;
    ain->messages = realloc(ain->messages, (size_t)(old + n) * sizeof(struct string *));
    for (int i = 0; i < n; i++) {
        ain->messages[old + i] = make_string(text, strlen(text));
    }
    ain->nr_messages = old + n;
}

static struct ain *make_rance9_ain(void) {
    struct ain *ain = ain_new(6, 0);
    ain_add_library(ain, "SealEngine");
    ain_add_library(ain, "DrawMovie3");
    ain_add_struct(ain, "T_PartsNo");
    return ain;
}

int main(void) {
    // 1. Rance IX Shift-JIS Traditional Chinese repack with GBK GameName ("兰斯9 赫尔曼革命 中文版"):
    //    MUST stay Shift-JIS (sys4_text_is_gbk == false) so "開始游戲"/"讀取進度"/"快速讀取" do NOT become "奐巒熰滶"!
    struct ain *r9_sjis = make_rance9_ain();
    // CP932: "「隊長，這次的士兵志愿者名單，全部都在這里了！開始游戲、讀取進度、快速讀取。」"
    const char *r9_sjis_msg =
        "\x81\x75\x91\xe0\x92\xb7\x81\x43\x98\x6f\x8e\x9f\x93\x49\x8e\x6d\x95\xba\x8e\x75\x8a\xe8\x8e\xd2\x96\xbc\x92\x50\x81\x43"
        "\x8a\x4a\x8e\x6e\x9f\xe0\x9d\x45\x81\x41\xe6\xa4\x8e\xe6\x90\x69\x93\x78\x81\x41\x89\xf5\x91\xac\xe6\xa4\x8e\xe6\x81\x42\x81\x76";
    add_messages_n(r9_sjis, r9_sjis_msg, 20);
    memset(&config, 0, sizeof(config));
    config.view_width = 800;
    config.view_height = 600;
    // GBK "兰斯9 中文版"
    config.game_name = strdup("\xc0\xbc\xcb\xb9" "9 \xd6\xd0\xce\xc4\xb0\xe6");
    sys4_set_source_utf8(false);
    sys4_set_text_gbk(true); // pre-set by config_init from GBK GameName!
    apply_game_specific_hacks(r9_sjis);
    assert(game_rance9 == true);
    assert(sys4_text_is_gbk == false);
    assert(config.view_width == 1280 && config.view_height == 720);
    // Verify title button strings decode to exact Chinese, NOT mojibake!
    char *btn1 = sjis2utf("\x8a\x4a\x8e\x6e\x9f\xe0\x9d\x45", 0);
    char *btn2 = sjis2utf("\xe6\xa4\x8e\xe6\x90\x69\x93\x78", 0);
    char *btn3 = sjis2utf("\x89\xf5\x91\xac\xe6\xa4\x8e\xe6", 0);
    assert(strcmp(btn1, "開始游戲") == 0);
    assert(strcmp(btn2, "讀取進度") == 0);
    assert(strcmp(btn3, "快速讀取") == 0);
    free(btn1); free(btn2); free(btn3);
    free(config.game_name);
    ain_free(r9_sjis);

    // 2. Rance IX GBK Simplified Chinese repack with unmodified JP GameName ("ランス９"):
    //    MUST enable GBK (sys4_text_is_gbk == true)!
    struct ain *r9_gbk = make_rance9_ain();
    // GBK: "开始游戏、读取进度、快速读取。兰斯九赫尔曼革命简体中文汉化版。"
    const char *r9_gbk_msg =
        "\xbf\xaa\xca\xbc\xd3\xce\xcf\xb7\xa1\xa2\xb6\xc1\xc8\xa1\xbd\xf8\xb6\xc8\xa1\xa2\xbf\xec\xcb\xd9\xb6\xc1\xc8\xa1\xa1\xa3"
        "\xc0\xbc\xcb\xb9\xbe\xc5\xba\xd5\xb6\xfb\xc2\xfc\xb8\xef\xc3\xfc\xbc\xf2\xcc\xe5\xd6\xd0\xce\xc4\xba\xba\xbb\xaf\xb0\xe6\xa1\xa3";
    add_messages_n(r9_gbk, r9_gbk_msg, 10);
    memset(&config, 0, sizeof(config));
    config.view_width = 800;
    config.view_height = 600;
    config.game_name = strdup("\x83\x89\x83\x93\x83\x58\x82\x58"); // SJIS "ランス９"
    sys4_set_text_gbk(false);
    apply_game_specific_hacks(r9_gbk);
    assert(game_rance9 == true);
    assert(sys4_text_is_gbk == true);
    free(config.game_name);
    ain_free(r9_gbk);

    // 3. Traditional Chinese GBK repack with ASCII GameName ("Rance9"):
    //    MUST enable GBK (sys4_text_is_gbk == true)!
    struct ain *tc_gbk = make_rance9_ain();
    // GBK TC: "開始遊戲、讀取進度、快速讀取。隊長，這次的士兵志願者名單！"
    const char *tc_gbk_msg =
        "\xe9\x5f\xca\xbc\xdf\x5b\x91\xf2\xa1\xa2\xd7\x78\xc8\xa1\xdf\x4d\xb6\xc8\xa1\xa2\xbf\xec\xcb\xd9\xd7\x78\xc8\xa1\xa1\xa3"
        "\xe9\x94\xe9\x6c\xa3\xac\xdf\x40\xb4\xce\xb5\xc4\xca\xbf\xb1\xf8\xd6\xbe\xee\x85\xd5\xdf\xc3\xfb\x85\xce\xa3\xa1";
    add_messages_n(tc_gbk, tc_gbk_msg, 10);
    memset(&config, 0, sizeof(config));
    config.view_width = 800;
    config.view_height = 600;
    config.game_name = strdup("Rance9");
    sys4_set_text_gbk(false);
    apply_game_specific_hacks(tc_gbk);
    assert(sys4_text_is_gbk == true);
    free(config.game_name);
    ain_free(tc_gbk);

    // 4. Large AIN (20,000 messages) whose first 10,000 messages are empty/ASCII and later 10,000 are GBK:
    //    Evenly-sampled detector MUST detect GBK!
    struct ain *late_gbk = ain_new(6, 0);
    add_messages_n(late_gbk, "", 10000);
    add_messages_n(late_gbk, r9_gbk_msg, 10000);
    memset(&config, 0, sizeof(config));
    config.game_name = strdup("CustomGame");
    sys4_set_text_gbk(false);
    apply_game_specific_hacks(late_gbk);
    assert(sys4_text_is_gbk == true);
    free(config.game_name);
    ain_free(late_gbk);

    // 5. Japanese Shift-JIS AIN whose AliceStart.ini was edited to Chinese GBK GameName ("斗神都市3"):
    //    MUST stay Shift-JIS (sys4_text_is_gbk == false)!
    struct ain *jp_t3 = ain_new(5, 0);
    ain_add_library(jp_t3, "Toushin3Loader");
    const char *jp_msg =
        "\x82\xa8\x82\xcd\x82\xe6\x82\xa4\x82\xb2\x82\xb4\x82\xa2\x82\xdc\x82\xb7\x81\x42"
        "\x82\xb1\x82\xea\x82\xcd\x83\x65\x83\x58\x83\x67\x82\xc5\x82\xb7\x81\x42";
    add_messages_n(jp_t3, jp_msg, 10);
    memset(&config, 0, sizeof(config));
    config.game_name = strdup("\xb6\xb7\xc9\xf1\xb6\xbc\xca\xd0" "3"); // GBK "斗神都市3"
    sys4_set_text_gbk(true);
    apply_game_specific_hacks(jp_t3);
    assert(game_toushin3 == true);
    assert(sys4_text_is_gbk == false);
    free(config.game_name);
    ain_free(jp_t3);

    // 6. Partial GBK translation (25% GBK, 75% JP) with Chinese GBK GameName ("xx汉化版"):
    //    AIN_ENC_UNKNOWN falls back to GameName heuristic -> keeps GBK enabled!
    struct ain *partial_gbk = ain_new(6, 0);
    add_messages_n(partial_gbk, r9_gbk_msg, 5);
    add_messages_n(partial_gbk, jp_msg, 15);
    memset(&config, 0, sizeof(config));
    config.game_name = strdup("Game\xba\xba\xbb\xaf\xb0\xe6"); // GBK "Game汉化版"
    sys4_set_text_gbk(false);
    apply_game_specific_hacks(partial_gbk);
    assert(sys4_text_is_gbk == true);
    free(config.game_name);
    ain_free(partial_gbk);

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            [ROOT / "src/hacks.c", ROOT / "src/escalayer_reboot_cn_ui.c", *LIBSYS4_CORE],
            ["-lz", "-lm"],
        )
        self.assertEqual(out.strip(), "OK")

    def test_utf8_ain_detection_and_mixed_source_safety(self):
        fn_ain_utf8 = _extract_def("src/system4.c", "ain_text_is_utf8")
        fn_ain_content_utf8 = _extract_def("src/system4.c", "ain_content_is_utf8")

        # 1. UTF-8 AIN with 19 UTF-8 messages and 1 leftover Shift-JIS dev message -> MUST detect UTF-8.
        utf8_msgs = ["テスト".encode("cp932")] + ["你好世界，开始游戏！".encode("utf-8")] * 19
        # 2. Shift-JIS AIN with 100 Shift-JIS messages and 1 accidentally UTF-8-valid sequence -> MUST NOT detect UTF-8.
        sjis_msgs = ["「開始游戲」".encode("cp932")] * 100 + ["あ".encode("utf-8")]
        # 3. UTF-8 AIN with empty messages and UTF-8 strings -> MUST detect UTF-8.
        utf8_strs_only = ["开始游戏".encode("utf-8"), "读取进度".encode("utf-8")]

        with tempfile.TemporaryDirectory() as tmpdir:
            td = Path(tmpdir)
            p_utf8 = td / "utf8_mixed.ain"
            p_sjis = td / "sjis_mixed.ain"
            p_strs = td / "utf8_strs.ain"
            p_utf8.write_bytes(_build_ai2(utf8_msgs))
            p_sjis.write_bytes(_build_ai2(sjis_msgs))
            p_strs.write_bytes(_build_ai2([], utf8_strs_only))

            harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

{fn_ain_utf8}
{fn_ain_content_utf8}

int main(int argc, char **argv) {{
    assert(argc == 4);
    assert(ain_text_is_utf8(argv[1]) == true);
    assert(ain_text_is_utf8(argv[2]) == false);
    assert(ain_text_is_utf8(argv[3]) == true);

    int err = 0;
    sys4_set_source_utf8(false);
    struct ain *a1 = ain_open(argv[1], &err);
    assert(a1 && ain_content_is_utf8(a1) == true);
    ain_free(a1);

    struct ain *a2 = ain_open(argv[2], &err);
    assert(a2 && ain_content_is_utf8(a2) == false);
    ain_free(a2);

    // Mixed source safety: with sys4_source_is_utf8=true, valid UTF-8 converts to GBK,
    // while invalid-UTF-8 legacy Shift-JIS bytes (e.g. "追加CG") are preserved untouched.
    sys4_set_source_utf8(true);
    sys4_set_text_gbk(true);
    char *u = sys4_source_strdup("\\xe5\\xbc\\x80\\xe5\\xa7\\x8b"); // UTF-8 "开始"
    assert(strcmp(u, "\\xbf\\xaa\\xca\\xbc") == 0); // GBK "开始"
    free(u);

    const char *legacy_sjis = "\\x92\\xc7\\x89\\xc1" "CG"; // SJIS "追加CG" (invalid UTF-8)
    char *kept = sys4_source_strdup(legacy_sjis);
    assert(strcmp(kept, legacy_sjis) == 0);
    free(kept);

    struct string *s_kept = sys4_source_make_string(legacy_sjis, strlen(legacy_sjis));
    assert(strcmp(s_kept->text, legacy_sjis) == 0);
    free_string(s_kept);

    puts("OK");
    return 0;
}}
"""
            out = compile_and_run(
                harness,
                LIBSYS4_CORE,
                ["-lz", "-lm"],
                [str(p_utf8), str(p_sjis), str(p_strs)],
            )
            self.assertEqual(out.strip(), "OK")

    def test_gamepad_sensitivity_clamping_range(self):
        # Verify both xsystem4/src/input.c and xsystem35/src/alicehub_bridge.c clamp
        # cursorSpeedPct to 20..300% and deadzonePct to 5..45%, and preserve toggle latches
        # when only sensitivity/deadzone changes.
        src = (ROOT / "src/input.c").read_text(encoding="utf-8", errors="ignore")
        fn = _extract_def("src/input.c", "input_set_gamepad_config_json")
        self.assertIn("if (p < 20) p = 20;", fn)
        self.assertIn("if (p > 300) p = 300;", fn)
        self.assertIn("gp_cfg.cursor_speed = 720.0f * ((float)p / 100.0f);", fn)
        self.assertIn("if (p < 5) p = 5;", fn)
        self.assertIn("if (p > 45) p = 45;", fn)
        self.assertIn("if (phys_mapping_changed)", fn)
        self.assertIn("cfg.cursor_speed", src)

        s35 = (ROOT.parent / "xsystem35/src/alicehub_bridge.c").read_text(encoding="utf-8", errors="ignore")
        self.assertIn("if (p < 20) p = 20;", s35)
        self.assertIn("if (p > 300) p = 300;", s35)
        self.assertIn("if (phys_mapping_changed)", s35)


if __name__ == "__main__":
    unittest.main()
