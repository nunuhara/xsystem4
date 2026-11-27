#!/usr/bin/env python3
"""Host C regression tests for Tsumashibori (妻しぼり CN) compatibility in xsystem4."""

import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def sdl_cflags() -> list[str]:
    return subprocess.check_output(["pkg-config", "--cflags", "sdl2"], text=True).split()


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


def pad256(data: bytes) -> bytes:
    rem = len(data) % 256
    return data if rem == 0 else data + b"\x00" * (256 - rem)


def put_u24le(n: int) -> bytes:
    return bytes((n & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF))


def build_ald_data_volume(entries: list[tuple[bytes, bytes]]) -> bytes:
    """Build a minimal valid ALD volume containing `entries` = [(name_bytes, payload_bytes), ...]."""
    # Pointer table: 3 bytes ptrsize + 3 bytes per file + 3 bytes end-of-last-file sentinel
    ptr_raw_len = 3 * (len(entries) + 2)
    ptr_sectors = (ptr_raw_len + 255) // 256
    # Empty 1-sector link table (data volumes don't need link entries; only GA holds the master file map)
    map_sectors = 1
    first_data_sector = ptr_sectors + map_sectors

    blocks = []
    offsets = []
    cur_sector = first_data_sector
    for name, payload in entries:
        hdr_size = 32
        name_field = name[: hdr_size - 17] + b"\x00"
        name_field = name_field.ljust(hdr_size - 16, b"\x00")
        hdr = struct.pack("<IIII", hdr_size, len(payload), 0, 0) + name_field
        rec = pad256(hdr + payload)
        offsets.append(cur_sector)
        blocks.append(rec)
        cur_sector += len(rec) // 256
    offsets.append(cur_sector)  # end sentinel

    ptr_tbl = bytearray()
    ptr_tbl += put_u24le(ptr_sectors)
    for sec in offsets:
        ptr_tbl += put_u24le(sec)
    ptr_tbl = bytearray(pad256(bytes(ptr_tbl)))
    assert len(ptr_tbl) == ptr_sectors * 256

    link_tbl = b"\x00" * (map_sectors * 256)
    return bytes(ptr_tbl) + link_tbl + b"".join(blocks)


def build_ald_link_volume(link_entries: list[tuple[int, int]]) -> bytes:
    """Build a minimal GA.ald link-table-only volume pointing into sparse disks (vol 1..26)."""
    ptr_sectors = 1
    map_bytes = bytearray()
    for vol, idx in link_entries:
        map_bytes += struct.pack("<BH", vol, idx)
    map_padded = pad256(bytes(map_bytes))
    map_sectors = max(1, len(map_padded) // 256)
    map_padded = map_padded.ljust(map_sectors * 256, b"\x00")

    ptr_tbl = bytearray()
    ptr_tbl += put_u24le(ptr_sectors)
    ptr_tbl += put_u24le(ptr_sectors + map_sectors)
    ptr_tbl = bytearray(pad256(bytes(ptr_tbl)))
    return bytes(ptr_tbl) + map_padded


def build_acx(col_types: list[int], rows: list[list]) -> bytes:
    body = bytearray()
    body += struct.pack("<I", len(col_types))
    for ct in col_types:
        body += struct.pack("<I", ct)
    body += struct.pack("<I", len(rows))
    for row in rows:
        for ct, val in zip(col_types, row):
            if ct == 0:
                body += struct.pack("<i", val)
            elif ct == 2:
                body += val + b"\x00"
            else:
                raise ValueError(f"Unsupported col type {ct}")
    comp = zlib.compress(bytes(body))
    return b"ACX\x00\x00\x00\x00\x00" + struct.pack("<II", len(comp), len(body)) + comp


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

LIBSYS4_ALD = [
    ROOT / "subprojects/libsys4/src/ald.c",
    ROOT / "subprojects/libsys4/src/archive.c",
    ROOT / "subprojects/libsys4/src/file.c",
    ROOT / "subprojects/libsys4/src/string.c",
    ROOT / "subprojects/libsys4/src/system.c",
    ROOT / "subprojects/libsys4/src/utfsjis.c",
]

LIBSYS4_ACX = [
    ROOT / "subprojects/libsys4/src/acx.c",
    ROOT / "subprojects/libsys4/src/file.c",
    ROOT / "subprojects/libsys4/src/string.c",
    ROOT / "subprojects/libsys4/src/system.c",
    ROOT / "subprojects/libsys4/src/utfsjis.c",
    ROOT / "subprojects/libsys4/src/zlib.c",
]


class TsumashiboriRegressionsTest(unittest.TestCase):
    def test_tsumashibori_hacks_and_gbk_detection(self):
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

static struct ain *make_tsuma_ain(int version, const char *str_val) {
    struct ain *ain = ain_new(version, 0);
    ain_add_struct(ain, "PLAY_TABLE_SAKURA");
    ain_add_struct(ain, "PLAY_TABLE_MADOKA");
    ain_add_struct(ain, "SAKURA_SIDE_SPRITE");
    ain_add_string(ain, str_val);
    ain_init_member_functions(ain, strdup);
    return ain;
}

int main(void) {
    // 1. Default CN GameName ("妻しぼり" in GBK) + Tsumashibori AIN v5.
    config = (struct config){ .view_width = 800, .view_height = 600, .game_name = "\xc6\xde\xa4\xb7\xa4\xdc\xa4\xea" };
    struct ain *ain_cn = make_tsuma_ain(5, "\xc6\xde\xa4\xb7\xa4\xdc\xa4\xea" "99.asd");
    apply_game_specific_hacks(ain_cn);
    assert(game_tsumashibori && !game_galzoo && sys4_text_is_gbk);
    assert(config.view_width == 800 && config.view_height == 600);
    ain_free(ain_cn);

    // 2. Modified/English GameName + CN AIN strings still enables GBK.
    const char *cn_markers[] = {
        "\xc6\xde\xa4\xb7\xa4\xdc\xa4\xea", // 妻しぼり (GBK)
        "\xba\xda\xcc\xe5",                 // 黑体 (GBK)
    };
    for (int i = 0; i < 2; i++) {
        config = (struct config){ .view_width = 800, .view_height = 600, .game_name = "Tsumashibori" };
        struct ain *ain_mod = make_tsuma_ain(5, cn_markers[i]);
        apply_game_specific_hacks(ain_mod);
        assert(game_tsumashibori && sys4_text_is_gbk);
        ain_free(ain_mod);
    }

    // 3. Original JP Tsumashibori ("妻しぼり" in Shift-JIS) stays non-GBK.
    config = (struct config){ .view_width = 800, .view_height = 600, .game_name = "\x8d\xc8\x82\xb5\x82\xda\x82\xe8" };
    struct ain *ain_jp = make_tsuma_ain(5, "\x8d\xc8\x82\xb5\x82\xda\x82\xe8" "99.asd");
    apply_game_specific_hacks(ain_jp);
    assert(game_tsumashibori && !sys4_text_is_gbk);
    ain_free(ain_jp);

    // 4. Other AIN v5 game (GALZOO) does not trigger game_tsumashibori.
    config = (struct config){ .view_width = 800, .view_height = 600, .game_name = "GALZOO" };
    struct ain *ain_gal = ain_new(5, 0);
    ain_add_library(ain_gal, "DrawDungeon14");
    ain_add_struct(ain_gal, "CDungeon");
    ain_add_string(ain_gal, "test");
    ain_init_member_functions(ain_gal, strdup);
    apply_game_specific_hacks(ain_gal);
    assert(game_galzoo && !game_tsumashibori);
    ain_free(ain_gal);

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

    def test_tsumashibori_config_init_gbk_save_path(self):
        sys4_src = (ROOT / "src/system4.c").read_text(encoding="utf-8", errors="ignore")
        config_init_utf8_src = extract_c_function(sys4_src, "static void config_init_with_utf8_codename(")
        config_init_src = extract_c_function(sys4_src, "static void config_init(")
        get_save_path_src = extract_c_function(sys4_src, "static char *get_save_path(")
        harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/file.h"
#include "system4/utfsjis.h"
#include "xsystem4.h"

struct config config;
static char *get_xsystem4_home(void) {{ return xstrdup("/tmp/xsys4_home"); }}

{get_save_path_src}

{config_init_utf8_src}

{config_init_src}

int main(void) {{
    // 1. CN System40.ini (GBK GameName & CodeName) enables GBK before get_save_path().
    sys4_set_text_gbk(false);
    config = (struct config){{
        .game_name = xstrdup("\\xc6\\xde\\xa4\\xb7\\xa4\\xdc\\xa4\\xea"),
        .ain_filename = xstrdup("\\xc6\\xde\\xa4\\xb7\\xa4\\xdc\\xa4\\xea.ain"),
        .save_dir = xstrdup("SaveData"),
    }};
    config_init();
    assert(sys4_text_is_gbk);
    assert(!strcmp(config.save_dir, "/tmp/xsys4_home/妻しぼり/SaveData"));
    free(config.game_name); free(config.ain_filename); free(config.save_dir); free(config.home_dir);

    // 2. Modified English GameName with GBK CodeName still enables GBK in config_init().
    sys4_set_text_gbk(false);
    config = (struct config){{
        .game_name = xstrdup("Tsumashibori"),
        .ain_filename = xstrdup("\\xc6\\xde\\xa4\\xb7\\xa4\\xdc\\xa4\\xea.ain"),
        .save_dir = xstrdup("SaveData"),
    }};
    config_init();
    assert(sys4_text_is_gbk);
    assert(!strcmp(config.save_dir, "/tmp/xsys4_home/Tsumashibori/SaveData"));
    free(config.game_name); free(config.ain_filename); free(config.save_dir); free(config.home_dir);

    // 3. Original JP System40.ini (Shift-JIS GameName & CodeName) keeps GBK off.
    sys4_set_text_gbk(false);
    config = (struct config){{
        .game_name = xstrdup("\\x8d\\xc8\\x82\\xb5\\x82\\xda\\x82\\xe8"),
        .ain_filename = xstrdup("\\x8d\\xc8\\x82\\xb5\\x82\\xda\\x82\\xe8.ain"),
        .save_dir = xstrdup("SaveData"),
    }};
    config_init();
    assert(!sys4_text_is_gbk);
    assert(!strcmp(config.save_dir, "/tmp/xsys4_home/妻しぼり/SaveData"));
    free(config.game_name); free(config.ain_filename); free(config.save_dir); free(config.home_dir);

    puts("OK");
    return 0;
}}
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

    def test_sparse_noncontiguous_ald_volumes_ga_gb_gc_gm_gz(self):
        # Mirror Tsumashibori CN 汉化修图补丁 sparse CG volume layout: GA(1), GB(2), GC(3), GM(13), GZ(26)
        # plus empty slot (0, 0) and malformed zero-volume/nonzero-ptr slot (0, 1) to catch unsigned-char map_disk bugs.
        link_entries = [
            (0, 0),   # id 0: empty
            (2, 1),   # id 1: GB entry 1
            (3, 1),   # id 2: GC entry 1
            (13, 1),  # id 3: GM entry 1
            (26, 1),  # id 4: GZ entry 1 (CN patched CG)
            (26, 2),  # id 5: GZ entry 2 (CN patched CG)
            (0, 1),   # id 6: disk=0-1=-1 with ptr=0 (must return NULL cleanly, never OOB!)
            (14, 1),  # id 7: missing volume GN (files[13] == NULL) -> must return NULL cleanly
        ]
        ga = build_ald_link_volume(link_entries)
        gb = build_ald_data_volume([(b"cg_gb_01.QNT", b"PAYLOAD_GB_1")])
        gc = build_ald_data_volume([(b"cg_gc_01.QNT", b"PAYLOAD_GC_1")])
        gm = build_ald_data_volume([(b"cg_gm_01.QNT", b"PAYLOAD_GM_ORIG_GA")])
        gz = build_ald_data_volume([
            (b"cg50000.QNT", b"PAYLOAD_GZ_CN_TITLE"),
            (b"cg50100.QNT", b"PAYLOAD_GZ_CN_CONFIG"),
        ])

        harness = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "system4/ald.h"
#include "system4/archive.h"

int main(int argc, char **argv) {
    assert(argc == 6);
    char *files[26] = {0};
    files[0]  = argv[1]; // GA (vol 1)
    files[1]  = argv[2]; // GB (vol 2)
    files[2]  = argv[3]; // GC (vol 3)
    files[12] = argv[4]; // GM (vol 13)
    files[25] = argv[5]; // GZ (vol 26)

    int err = 0;
    struct archive *ar = ald_open(files, 26, 0, &err);
    assert(ar && err == 0);
    assert(archive_nr_files(ar) == 5);

    // Empty / invalid / missing-volume slots must cleanly report missing.
    assert(!archive_exists(ar, 0));
    assert(archive_get(ar, 0) == NULL);
    assert(!archive_exists(ar, 6));
    assert(archive_get(ar, 6) == NULL);
    assert(!archive_exists(ar, 7));
    assert(archive_get(ar, 7) == NULL);

    struct {
        int id;
        const char *name;
        const char *payload;
    } expected[] = {
        { 1, "cg_gb_01.QNT", "PAYLOAD_GB_1" },
        { 2, "cg_gc_01.QNT", "PAYLOAD_GC_1" },
        { 3, "cg_gm_01.QNT", "PAYLOAD_GM_ORIG_GA" },
        { 4, "cg50000.QNT",  "PAYLOAD_GZ_CN_TITLE" },
        { 5, "cg50100.QNT",  "PAYLOAD_GZ_CN_CONFIG" },
    };
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        assert(archive_exists(ar, expected[i].id));
        struct archive_data *d = archive_get(ar, expected[i].id);
        assert(d != NULL);
        assert(!strcmp(d->name, expected[i].name));
        assert(d->size == strlen(expected[i].payload));
        assert(!memcmp(d->data, expected[i].payload, d->size));
        archive_free_data(d);
    }

    archive_free(ar);
    puts("OK");
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            paths = []
            for fname, blob in [
                ("妻しぼりGA.ald", ga),
                ("妻しぼりGB.ald", gb),
                ("妻しぼりGC.ald", gc),
                ("妻しぼりGM.ald", gm),
                ("妻しぼりGZ.ald", gz),
            ]:
                p = tmp / fname
                p.write_bytes(blob)
                paths.append(str(p))
            out = compile_and_run(harness, LIBSYS4_ALD, [], paths)
            self.assertEqual(out.strip(), "OK")

    def test_tsumashibori_gbk_text_filenames_acx_and_zenkaku(self):
        # Build synthetic Album.acx with Shift-JIS col 1 scenario key + GBK cols 2 & 3 display text.
        acx_bytes = build_acx(
            [0, 2, 2, 2],
            [
                [1, "咲良／共通／０６".encode("cp932"), "愉快的除草".encode("gbk"), "事件Ｈ".encode("gbk")],
                [2, "円／単発／０２".encode("cp932"), "早安之吻".encode("gbk"), "起床事件".encode("gbk")],
            ],
        )
        harness = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4/acx.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

int main(int argc, char **argv) {
    assert(argc == 2);
    sys4_set_text_gbk(true);

    // 1. VolumeValancer[0..7], MsgSkipName, font, and GBK ext chars (咲/円/姫).
    struct {
        const char *gbk;
        const char *utf8;
    } cases[] = {
        { "\xc6\xde\xa4\xb7\xa4\xdc\xa4\xea", "妻しぼり" },
        { "\xd2\xd1\xb6\xc1\xce\xc4\xb1\xbe\xcc\xf8\xb9\xfd", "已读文本跳过" },
        { "\xa3\xc2\xa3\xc7\xa3\xcd", "ＢＧＭ" },
        { "\xd2\xf4\xd0\xa7", "音效" },
        { "\x86\x44\xc1\xbc", "咲良" },
        { "\x83\xd2", "円" },
        { "\x8a\xaa\xcf\xe3", "姫香" },
        { "\xc2\xc9\xd7\xd3", "律子" },
        { "\xd1\xa9\xbb\xe6", "雪绘" },
        { "\xc1\xf4\xc3\xc0\xc4\xce", "留美奈" },
        { "\xba\xda\xcc\xe5", "黑体" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char *u = sjis2utf(cases[i].gbk, 0);
        assert(u && !strcmp(u, cases[i].utf8));
        char *rt = utf2sjis(u, 0);
        assert(rt && !strcmp(rt, cases[i].gbk));
        free(u);
        free(rt);
    }

    // 2. Runtime & save filenames decode cleanly via sjis_filename2utf.
    struct {
        const char *gbk;
        const char *utf8;
    } fcases[] = {
        { "\xc6\xde\xa4\xb7\xa4\xdc\xa4\xea.ain", "妻しぼり.ain" },
        { "\xc6\xde\xa4\xb7\xa4\xdc\xa4\xea\xa3\xcf\xa3\xd0.mpg", "妻しぼりＯＰ.mpg" },
        { "\xc6\xde\xa4\xb7\xa4\xdc\xa4\xea" "SYS.asd", "妻しぼりSYS.asd" },
        { "\xc6\xde\xa4\xb7\xa4\xdc\xa4\xea" "0.asd", "妻しぼり0.asd" },
        { "\xc6\xde\xa4\xb7\xa4\xdc\xa4\xea" "99.asd", "妻しぼり99.asd" },
    };
    for (size_t i = 0; i < sizeof(fcases) / sizeof(fcases[0]); i++) {
        char *u = sjis_filename2utf(fcases[i].gbk, 0);
        assert(u && !strcmp(u, fcases[i].utf8));
        free(u);
    }

    // 3. Full-width number formatting emits GBK full-width digits/minus/space.
    char numbuf[64];
    int nlen = int_to_cstr(numbuf, sizeof(numbuf), -12, 4, false, true);
    assert(nlen == 8);
    char *num_utf = sjis2utf(numbuf, nlen);
    assert(num_utf && !strcmp(num_utf, "\xe3\x80\x80\xef\xbc\x8d\xef\xbc\x91\xef\xbc\x92")); // "　－１２"
    free(num_utf);

    // 4. Mixed-encoding Album.acx preserves raw Shift-JIS col 1 key while cols 2/3 decode cleanly as GBK.
    int err = 0;
    struct acx *acx = acx_load(argv[1], &err);
    assert(acx && err == ACX_SUCCESS && acx->nr_lines == 2 && acx->nr_columns == 4);
    assert(acx_get_int(acx, 0, 0) == 1);
    // Col 1 raw Shift-JIS bytes must remain untouched for AIN function-name dispatch.
    assert(!strcmp(acx_get_string(acx, 0, 1)->text, "\x8d\xe7\x97\xc7\x81\x5e\x8b\xa4\x92\xca\x81\x5e\x82\x4f\x82\x55"));
    char *t0 = sjis2utf(acx_get_string(acx, 0, 2)->text, 0);
    char *c0 = sjis2utf(acx_get_string(acx, 0, 3)->text, 0);
    assert(t0 && !strcmp(t0, "愉快的除草"));
    assert(c0 && !strcmp(c0, "事件Ｈ"));
    free(t0);
    free(c0);
    acx_free(acx);

    sys4_set_text_gbk(false);
    puts("OK");
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmpdir:
            acx_path = Path(tmpdir) / "Album.acx"
            acx_path.write_bytes(acx_bytes)
            out = compile_and_run(harness, LIBSYS4_ACX, ["-lz"], [str(acx_path)])
            self.assertEqual(out.strip(), "OK")

        self.assertIn("extern bool game_tsumashibori;", (ROOT / "include/xsystem4.h").read_text(encoding="utf-8", errors="ignore"))
        self.assertIn("int16_t *map_disk;", (ROOT / "subprojects/libsys4/include/system4/ald.h").read_text(encoding="utf-8", errors="ignore"))


if __name__ == "__main__":
    unittest.main()
