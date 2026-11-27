#!/usr/bin/env python3
"""Host C regression tests for GALZOO Island (CN) compatibility fixes in xsystem4."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def encode_bgi(text: str) -> bytes:
    raw = text.encode("ascii")
    return bytes(((b >> 4) | ((b & 0xF) << 4)) for b in raw)


def sdl_cflags() -> list[str]:
    return subprocess.check_output(["pkg-config", "--cflags", "sdl2"], text=True).split()


def extract_c_function(src: str, signature_prefix: str) -> str:
    idx = src.find(signature_prefix)
    assert idx >= 0, f"could not locate {signature_prefix} in src/hacks.c"
    end = src.find("\n}", idx)
    assert end >= 0, f"could not locate closing brace for {signature_prefix}"
    return src[idx : end + 2]


def extract_galzoo_hack_functions() -> str:
    hacks_src = (ROOT / "src/hacks.c").read_text(encoding="utf-8", errors="ignore")
    return "\n\n".join(
        [
            extract_c_function(hacks_src, "static bool ain_has_gbk_string("),
            extract_c_function(hacks_src, "void apply_game_specific_hacks("),
        ]
    )


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
        res_r = subprocess.run([str(bin_path), *(args or [])], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res_r.returncode != 0:
            raise AssertionError(f"Run failed ({res_r.returncode}):\nstdout:\n{res_r.stdout}\nstderr:\n{res_r.stderr}")
        return res_r.stdout


LIBSYS4_BGI = [
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


class GalzooRegressionsTest(unittest.TestCase):
    def test_bgi_dynamic_growth_above_100_entries(self):
        # Model GALZOOBA.bgi layout: 101 track records up to track #115, plus growth
        # across doublings (300 records), legacy 5-field format, and smaller re-reads.
        galzoo_lines = ["NumofLink = 116\r\n"]
        track_nos = list(range(1, 101)) + [115]
        assert len(track_nos) == 101
        for idx, no in enumerate(track_nos):
            galzoo_lines.append(f"{no}, 0, {idx * 100}, {idx * 100 + 50}, {no % 2}, {70 + (no % 31)}\r\n")
        bgi_101 = encode_bgi("".join(galzoo_lines))

        large_lines = ["NumofLink = 301\r\n"]
        for no in range(1, 301):
            large_lines.append(f"{no}, 2, {no * 10}, {no * 10 + 9}, 0, 100\r\n")
        bgi_300 = encode_bgi("".join(large_lines))

        legacy_lines = ["NumofLink = 151\r\n"]
        for no in range(1, 151):
            legacy_lines.append(f"{no}, 0, {no}, {no + 1000}, -1\r\n")
        bgi_legacy = encode_bgi("".join(legacy_lines))

        small_lines = ["NumofLink = 4\r\n"]
        for no in range(1, 4):
            small_lines.append(f"{no}, 1, 10, 20, 3, 88\r\n")
        bgi_small = encode_bgi("".join(small_lines))

        harness = r"""
#include <assert.h>
#include <stdio.h>
#include "mixer.h"
#include "xsystem4.h"

const char *display_utf0(const char *utf) { return utf; }

int main(int argc, char **argv) {
    assert(argc == 5);

    // 1. GALZOOBA.bgi pattern: 101st track is no=115 (> old BGI_MAX 100).
    bgi_read(argv[1]);
    struct bgi *t100 = bgi_get(100);
    assert(t100 && t100->no == 100 && t100->volume == 70 + (100 % 31));
    struct bgi *t115 = bgi_get(115);
    assert(t115 && t115->no == 115);
    assert(t115->loop_start == 100 * 100 && t115->loop_end == 100 * 100 + 50);
    assert(t115->channel == 1 && t115->volume == 70 + (115 % 31));
    assert(bgi_get(116) == NULL);

    // 2. Grow past initial 128 and 256 capacities.
    bgi_read(argv[2]);
    struct bgi *t257 = bgi_get(257);
    assert(t257 && t257->loop_count == 2 && t257->loop_start == 2570 && t257->loop_end == 2579);
    struct bgi *t300 = bgi_get(300);
    assert(t300 && t300->loop_start == 3000 && t300->channel == 0 && t300->volume == 100);

    // 3. Legacy 5-field -1 format >100 entries.
    bgi_read(argv[3]);
    struct bgi *l150 = bgi_get(150);
    assert(l150 && l150->no == 150 && l150->loop_start == 150 && l150->loop_end == 1150);
    assert(l150->channel == -1 && l150->volume == 100);

    // 4. Re-read smaller file resets count cleanly with no stale entries.
    bgi_read(argv[4]);
    struct bgi *s3 = bgi_get(3);
    assert(s3 && s3->channel == 3 && s3->volume == 88);
    assert(bgi_get(150) == NULL);

    puts("OK");
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            p1 = tmp / "galzoo.bgi"
            p2 = tmp / "large.bgi"
            p3 = tmp / "legacy.bgi"
            p4 = tmp / "small.bgi"
            p1.write_bytes(bgi_101)
            p2.write_bytes(bgi_300)
            p3.write_bytes(bgi_legacy)
            p4.write_bytes(bgi_small)
            out = compile_and_run(harness, LIBSYS4_BGI, ["-lm"], [str(p1), str(p2), str(p3), str(p4)])
            self.assertEqual(out.strip(), "OK")

    def test_galzoo_hacks_and_gbk_detection(self):
        harness = r"""
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "xsystem4.h"

struct config config;
void message_layout_init_rance03(void) {}
void apply_game_specific_hacks(struct ain *ain);

static struct ain *make_galzoo_ain(int version, const char *lib_name, const char *struct_name, const char *str_val) {
    struct ain *ain = ain_new(version, 0);
    ain_add_library(ain, lib_name);
    ain_add_struct(ain, struct_name);
    ain_add_string(ain, str_val);
    ain_init_member_functions(ain, strdup);
    return ain;
}

int main(void) {
    // 1. Default CN GameName + GALZOO AIN v5.
    config = (struct config){ .view_width = 800, .view_height = 600, .game_name = "GALZOO\xba\xba\xbb\xaf\xb0\xe6V1.00" };
    struct ain *ain_cn = make_galzoo_ain(5, "DrawDungeon14", "CDungeon", "\xd2\xd1\xb6\xc1\xce\xc4\xb1\xbe\xcc\xf8\xb9\xfd");
    apply_game_specific_hacks(ain_cn);
    assert(game_galzoo && !game_rance6 && sys4_text_is_gbk);
    assert(config.view_width == 800 && config.view_height == 600);
    ain_free(ain_cn);

    // 2. Modified/English GameName + CN AIN strings (font / dungeon / skip).
    const char *markers[] = {
        "\xba\xda\xcc\xe5",                                         // 黑体
        "\xcd\xf2\xc0\xef\xb3\xa4\xb3\xc7",                         // 万里长城
        "\xd2\xd1\xb6\xc1\xce\xc4\xb1\xbe\xcc\xf8\xb9\xfd",         // 已读文本跳过
    };
    for (int i = 0; i < 3; i++) {
        config = (struct config){ .view_width = 800, .view_height = 600, .game_name = "GALZOO Island" };
        struct ain *ain_mod = make_galzoo_ain(5, "DrawDungeon14", "CDungeon", markers[i]);
        apply_game_specific_hacks(ain_mod);
        assert(game_galzoo && sys4_text_is_gbk);
        ain_free(ain_mod);
    }

    // 3. JP GALZOO AIN must stay non-GBK when GameName is JP.
    config = (struct config){ .view_width = 800, .view_height = 600, .game_name = "GALZOO\x83" "A\x83" "C\x83\x89\x83\x93\x83h" };
    struct ain *ain_jp = make_galzoo_ain(5, "DrawDungeon14", "CDungeon", "\x96\x9c\x97\xa2\x82\xcc\x92\xb7\x8f\xe9");
    apply_game_specific_hacks(ain_jp);
    assert(game_galzoo && !sys4_text_is_gbk);
    ain_free(ain_jp);

    // 4. Rance VI does not trigger game_galzoo.
    config = (struct config){ .view_width = 800, .view_height = 600, .game_name = "Rance6" };
    struct ain *ain_r6 = make_galzoo_ain(4, "DrawDungeon", "CDungeon", "\x93\xfa\x96\x7b\x8c\xea");
    ain_add_library(ain_r6, "ACXLoader");
    apply_game_specific_hacks(ain_r6);
    assert(game_rance6 && !game_galzoo);
    ain_free(ain_r6);

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            [*LIBSYS4_AIN, ROOT / "src/hacks.c", ROOT / "src/escalayer_reboot_cn_ui.c"],
            ["-lz", "-lm"],
        )
        self.assertEqual(out.strip(), "OK")

    def test_galzoo_gbk_text_conversion_and_source_wiring(self):
        harness = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4/utfsjis.h"

int main(void) {
    sys4_set_text_gbk(true);
    struct {
        const char *gbk;
        const char *utf8;
    } cases[] = {
        { "\xd2\xd1\xb6\xc1\xce\xc4\xb1\xbe\xcc\xf8\xb9\xfd", "已读文本跳过" },
        { "\xa3\xc2\xa3\xc7\xa3\xcd", "ＢＧＭ" },
        { "\xd2\xf4\xd0\xa7", "音效" },
        { "\xc5\xae\xd7\xd3", "女子" },
        { "\xc4\xd0\xd7\xd3", "男子" },
        { "\xba\xda\xcc\xe5", "黑体" },
        { "\xcd\xf2\xc0\xef\xb3\xa4\xb3\xc7", "万里长城" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char *u = sjis2utf(cases[i].gbk, 0);
        assert(u && !strcmp(u, cases[i].utf8));
        char *roundtrip = utf2sjis(u, 0);
        assert(roundtrip && !strcmp(roundtrip, cases[i].gbk));
        free(u);
        free(roundtrip);
    }
    sys4_set_text_gbk(false);
    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            [
                ROOT / "subprojects/libsys4/src/utfsjis.c",
                ROOT / "subprojects/libsys4/src/file.c",
                ROOT / "subprojects/libsys4/src/string.c",
                ROOT / "subprojects/libsys4/src/system.c",
            ],
        )
        self.assertEqual(out.strip(), "OK")

        # Sanity-check public wiring in headers/sources.
        self.assertIn("extern bool game_galzoo;", (ROOT / "include/xsystem4.h").read_text(encoding="utf-8", errors="ignore"))
        self.assertNotIn("BGI_MAX", (ROOT / "src/audio_meta.c").read_text(encoding="utf-8", errors="ignore"))


if __name__ == "__main__":
    unittest.main()
