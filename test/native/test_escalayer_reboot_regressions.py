#!/usr/bin/env python3
"""Dependency-free native regression tests for Beat Angel Escalayer Reboot fixes.

Covers:
1. cjk_gbk_zenkaku_to_sjis_buf() (libsys4/src/utfsjis.c) folds GBK fullwidth
   digits/punctuation emitted by %D/%F formatting back to Shift-JIS while leaving
   SJIS and GBK double-byte characters intact. The CN fan patch formats SJIS EX
   keys / CG names such as 'メッセージウィンドウ.%02D番.テキスト.サイズ' and
   'カレンダー／月／%02D' while sys4_text_is_gbk is set, producing mixed strings.
2. ex_cjk_equals_n() (libsys4/src/ex.c) matches those mixed keys against the
   SJIS keys stored in EscalayerReboot.ex, and every EX lookup path uses it.
3. AFA basename lookup normalizes the same mixed names before falling back.
4. PartsFunc IDs are shifted by 3 for Escalayer Reboot (CDrawMovie@CreateMovieParts
   passes 162 for InitPartsMovie with 8 ints).
5. SystemService.EscalayerReboot011116 returns a key accepted by main().
6. Game detection / GBK detection / window title wiring.

Run: python3 xsystem4/test/native/test_escalayer_reboot_regressions.py
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def read(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8", errors="surrogateescape")


def extract_function(rel_path: str, name: str) -> str:
    source = read(rel_path)
    # Skip forward declarations and call sites: take the first match whose
    # parameter list is followed by '{' before any ';'.
    pattern = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(name) + r"\(")
    for m in pattern.finditer(source):
        semi = source.find(";", m.end())
        brace = source.find("{", m.end())
        if brace != -1 and (semi == -1 or brace < semi):
            idx = m.start()
            break
    else:
        raise ValueError(f"definition of {name} not found in {rel_path}")
    start = source.rfind("\n", 0, idx) + 1
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def compile_and_run(c_source: str) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "test.c"
        exe = Path(tmp) / "test_bin"
        src.write_text(c_source, encoding="utf-8", errors="surrogateescape")
        subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", str(src), "-o", str(exe)],
            check=True,
        )
        subprocess.run([str(exe)], check=True)


PRELUDE = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool sys4_text_is_gbk = true;
static void *xmalloc(size_t n) { void *p = malloc(n); assert(p); return p; }
"""


class EscalayerRebootRegressionTests(unittest.TestCase):
    def test_gbk_zenkaku_fold_and_ex_key_match(self) -> None:
        fold = extract_function("subprojects/libsys4/src/utfsjis.c", "cjk_gbk_zenkaku_to_sjis_buf")
        has_non_ascii = extract_function("subprojects/libsys4/src/ex.c", "ex_bytes_has_non_ascii")
        equals = extract_function("subprojects/libsys4/src/ex.c", "ex_cjk_equals_n")
        c_code = PRELUDE + fold + r"""
/* Strict decode stub: report failure so only the zenkaku fold is exercised. */
static char *cjk2utf_strict_buf(const char *src, size_t len, bool gbk, char *buf, size_t cap, bool *ok)
{
    (void)gbk;
    char *out = (buf && len < cap) ? buf : xmalloc(len + 1);
    memcpy(out, src, len);
    out[len] = 0;
    *ok = false;
    return out;
}
""" + has_non_ascii + "\n" + equals + r"""
int main(void)
{
    /* GBK '０１' + SJIS '番' (as produced by %02D in GBK mode) -> SJIS '０１番' */
    const char mixed[] = "\xa3\xb0\xa3\xb1\x94\xd4";
    const char sjis[] = "\x82\x4f\x82\x50\x94\xd4";
    bool changed = false;
    char buf[64];
    char *out = cjk_gbk_zenkaku_to_sjis_buf(mixed, strlen(mixed), buf, sizeof(buf), &changed);
    assert(changed);
    assert(!strcmp(out, sjis));

    /* 'カレンダー／月／' (SJIS) + GBK '０７' -> SJIS 'カレンダー／月／０７' */
    const char cg_mixed[] = "\x83\x4a\x83\x8c\x83\x93\x83\x5f\x81\x5b\x81\x5e\x8c\x8e\x81\x5e\xa3\xb0\xa3\xb7";
    const char cg_sjis[]  = "\x83\x4a\x83\x8c\x83\x93\x83\x5f\x81\x5b\x81\x5e\x8c\x8e\x81\x5e\x82\x4f\x82\x56";
    out = cjk_gbk_zenkaku_to_sjis_buf(cg_mixed, strlen(cg_mixed), buf, sizeof(buf), &changed);
    assert(changed);
    assert(!strcmp(out, cg_sjis));

    /* Fullwidth minus / period / ideographic space */
    out = cjk_gbk_zenkaku_to_sjis_buf("\xa3\xad\xa3\xae\xa1\xa1", 6, buf, sizeof(buf), &changed);
    assert(changed);
    assert(!memcmp(out, "\x81\x7c\x81\x44\x81\x40", 7));

    /* Pure SJIS / ASCII / ordinary GBK text is untouched. */
    out = cjk_gbk_zenkaku_to_sjis_buf(sjis, strlen(sjis), buf, sizeof(buf), &changed);
    assert(!changed && !strcmp(out, sjis));
    out = cjk_gbk_zenkaku_to_sjis_buf("abc.01", 6, buf, sizeof(buf), &changed);
    assert(!changed && !strcmp(out, "abc.01"));
    const char gbk_text[] = "\xbd\xf8\xd0\xd0\xbf\xec\xcb\xd9"; /* 进行快速 */
    out = cjk_gbk_zenkaku_to_sjis_buf(gbk_text, strlen(gbk_text), buf, sizeof(buf), &changed);
    assert(!changed && !strcmp(out, gbk_text));
    /* SJIS trail byte 0xa3 inside a double-byte char must not be folded. */
    const char sjis_trail[] = "\x83\xa3\xb0";
    out = cjk_gbk_zenkaku_to_sjis_buf(sjis_trail, 3, buf, sizeof(buf), &changed);
    assert(!changed && !memcmp(out, sjis_trail, 4));

    /* Heap path when buffer is too small. */
    char tiny[2];
    out = cjk_gbk_zenkaku_to_sjis_buf(mixed, strlen(mixed), tiny, sizeof(tiny), &changed);
    assert(out != tiny && changed && !strcmp(out, sjis));
    free(out);

    /* EX key comparison */
    assert(ex_cjk_equals_n(sjis, strlen(sjis), mixed, strlen(mixed)));
    assert(!ex_cjk_equals_n(sjis, strlen(sjis), "\xa3\xb0\xa3\xb2\x94\xd4", 6));
    assert(!ex_cjk_equals_n("abc", 3, "abc", 3)); /* ASCII handled by strcmp callers */
    sys4_text_is_gbk = false;
    assert(!ex_cjk_equals_n(sjis, strlen(sjis), mixed, strlen(mixed)));
    return 0;
}
"""
        compile_and_run(c_code)

    def test_ex_lookup_paths_use_cjk_fallback(self) -> None:
        ex_c = read("subprojects/libsys4/src/ex.c")
        for fn in ("ex_tree_get_path", "_ex_get", "ex_get_block", "ex_tree_get_child",
                   "ex_row_at_string_key", "ex_col_from_name"):
            body = extract_function("subprojects/libsys4/src/ex.c", fn)
            self.assertIn("ex_cjk_equals_n(", body, fn)
            self.assertIn("sys4_text_is_gbk", body, fn)
        self.assertIn('#include "system4/utfsjis.h"', ex_c)
        header = read("subprojects/libsys4/include/system4/utfsjis.h")
        self.assertIn("cjk_gbk_zenkaku_to_sjis_buf(", header)

    def test_afa_lookup_normalizes_gbk_zenkaku(self) -> None:
        body = extract_function("src/asset_manager.c", "afa_find_best_by_basename")
        self.assertIn("cjk_gbk_zenkaku_to_sjis_buf(", body)
        norm = body.index("cjk_gbk_zenkaku_to_sjis_buf(")
        self.assertLess(norm, body.index("afa_ensure_canon_index(manager)"))
        self.assertIn("archive_basename_buf(lookup_name", body)
        self.assertIn("save_transcode_cstr_strict(lookup_name", body)

    def test_parts_func_id_shift(self) -> None:
        body = extract_function("src/hll/PartsEngine.c", "PartsEngine_PartsFunc")
        self.assertRegex(body, r"game_escalayer_reboot\)\s*&&\s*func_id\s*>=\s*3")
        # 162 (+3) must land on InitPartsMovie with 8 ints.
        case = re.search(r"case 165:.*?REQUIRE_INTS\((\d+)\).*?PE_init_parts_movie", body, re.S)
        self.assertIsNotNone(case)
        self.assertEqual(case.group(1), "8")

    def test_system_service_key(self) -> None:
        src = read("src/hll/SystemService.c")
        m = re.search(r'SystemService_EscalayerReboot011116\(struct string \*\*text\)\s*\{\s*'
                      r'set_out_cstr\(text, "([^"]*)"\);', src)
        self.assertIsNotNone(m)
        key = m.group(1)
        # main() in EscalayerReboot.ain: Length()==14 && s[0]+s[2]+s[4]+s[9..11] == "kkrsre"
        self.assertEqual(len(key), 14)
        self.assertEqual(key[0] + key[2] + key[4] + key[9:12], "kkrsre")
        self.assertIn("HLL_EXPORT(EscalayerReboot011116, SystemService_EscalayerReboot011116)", src)

    def test_game_detection_and_title(self) -> None:
        hacks = read("src/hacks.c")
        self.assertIn('"C_BATTLE_PLAYER_RESULT_COMMAND@SetEscalayerPunch"', hacks)
        self.assertIn("game_escalayer_reboot = false;", hacks)
        self.assertRegex(hacks, r"game_escalayer_reboot && \(ain_has_gbk_string\(ain,")
        self.assertIn("extern bool game_escalayer_reboot;", read("include/xsystem4.h"))
        title = extract_function("src/video.c", "set_window_title")
        self.assertIn("sjis_filename2utf(config.game_name", title)

    def test_cn_ui_patch_is_game_and_gbk_scoped(self) -> None:
        hacks = read("src/hacks.c")
        self.assertIn('#include "escalayer_reboot_cn_ui.h"', hacks)
        self.assertRegex(
            hacks,
            r"if\s*\(game_escalayer_reboot\s*&&\s*sys4_text_is_gbk\)\s*"
            r"escalayer_reboot_replace_cn_ui_text\(ain->strings,\s*ain->nr_strings\)",
        )
        # Must not become a global encoding switch.
        self.assertNotIn("sys4_set_text_gbk(false)", extract_function("src/hacks.c", "apply_game_specific_hacks"))

    def test_cn_ui_exact_replace_and_negative_controls(self) -> None:
        includes = [
            str(ROOT / "include"),
            str(ROOT / "subprojects/libsys4/include"),
        ]
        sources = [
            ROOT / "src/escalayer_reboot_cn_ui.c",
            ROOT / "subprojects/libsys4/src/string.c",
        ]
        harness = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/string.h"
#include "escalayer_reboot_cn_ui.h"

bool sys4_text_is_gbk;
bool sys_silent = true;
void (*sys_error_handler)(const char *msg);
void (*sys_warning_handler)(const char *msg);
void (*sys_exit_handler)(int code);
_Noreturn void sys_error(const char *fmt, ...) { (void)fmt; abort(); }
void sys_warning(const char *fmt, ...) { (void)fmt; }
void sys_message(const char *fmt, ...) { (void)fmt; }
void *_xrealloc(void *ptr, size_t size, const char *func) { (void)func; void *p = realloc(ptr, size); assert(p || size == 0); return p; }
char *_xstrdup(const char *in, const char *func) {
	(void)func;
	size_t n = strlen(in) + 1;
	char *p = malloc(n);
	assert(p);
	memcpy(p, in, n);
	return p;
}

void *_xmalloc(size_t size, const char *func)
{
	(void)func;
	void *p = malloc(size);
	assert(p);
	return p;
}

void *_xcalloc(size_t nmemb, size_t size, const char *func)
{
	(void)func;
	void *p = calloc(nmemb, size);
	assert(p);
	return p;
}

void *_xcalloc_aligned(size_t alignment, size_t size, const char *func)
{
	(void)alignment;
	return _xcalloc(1, size, func);
}

static struct string *dup_cstr(const char *s)
{
	struct string *out = cstr_to_string(s);
	return out;
}

int main(void)
{
	int n = escalayer_reboot_cn_ui_pair_count();
	assert(n == 77);

	/* Table internals: unique SJIS sources, unique GBK targets, no overlap. */
	for (int i = 0; i < n; i++) {
		const char *sj = escalayer_reboot_cn_ui_sjis(i);
		const char *gb = escalayer_reboot_cn_ui_gbk(i);
		assert(sj && gb && *sj && *gb);
		assert(strcmp(sj, gb) != 0);
		for (int j = i + 1; j < n; j++) {
			assert(strcmp(sj, escalayer_reboot_cn_ui_sjis(j)) != 0);
			assert(strcmp(gb, escalayer_reboot_cn_ui_gbk(j)) != 0);
		}
	}

	/* Positive + negative controls packed into one table.
	 * 0..n-1 originals, then already-GBK story, resource key, substring near-miss,
	 * ASCII, and a second copy of original 0 to prove every exact slot is rewritten.
	 */
	const char *key_sjis = "\x83\x52\x83\x93\x83\x74\x83\x42\x83\x4f\x81\x5e\x83\x74\x83\x8c\x81\x5b\x83\x80\x81\x5e\x83\x51\x81\x5b\x83\x80\x90\xdd\x92\xe8"; /* コンフィグ／フレーム／ゲーム設定 */
	const char *gbk_story = "\xd4\xd8\xc8\xeb\xbf\xec\xcb\xd9\xb1\xa3\xb4\xe6\xb4\xe6\xb5\xb5\xc2\xf0\xa3\xbf"; /* 载入快速保存存档吗？ */
	const char *ascii = "Permit Access to the Game Server";
	char near_miss[256];
	snprintf(near_miss, sizeof(near_miss), "%sx", escalayer_reboot_cn_ui_sjis(0));

	int nr = n + 6;
	struct string **tab = calloc((size_t)nr, sizeof(*tab));
	assert(tab);
	for (int i = 0; i < n; i++)
		tab[i] = dup_cstr(escalayer_reboot_cn_ui_sjis(i));
	tab[n] = dup_cstr(gbk_story);
	tab[n + 1] = dup_cstr(key_sjis);
	tab[n + 2] = dup_cstr(near_miss);
	tab[n + 3] = dup_cstr(ascii);
	tab[n + 4] = dup_cstr(escalayer_reboot_cn_ui_sjis(0));
	tab[n + 5] = NULL; /* must be ignored */

	int replaced = escalayer_reboot_replace_cn_ui_text(tab, nr);
	assert(replaced == n + 1); /* n originals + extra copy of slot 0 */

	for (int i = 0; i < n; i++)
		assert(!strcmp(tab[i]->text, escalayer_reboot_cn_ui_gbk(i)));
	assert(!strcmp(tab[n]->text, gbk_story));
	assert(!strcmp(tab[n + 1]->text, key_sjis));
	assert(!strcmp(tab[n + 2]->text, near_miss));
	assert(!strcmp(tab[n + 3]->text, ascii));
	assert(!strcmp(tab[n + 4]->text, escalayer_reboot_cn_ui_gbk(0)));
	assert(tab[n + 5] == NULL);

	/* Indexes are preserved (same slots), only the text changed. */
	assert(escalayer_reboot_replace_cn_ui_text(tab, nr) == 0);

	/* NULL / empty table is a no-op. */
	assert(escalayer_reboot_replace_cn_ui_text(NULL, 3) == 0);
	assert(escalayer_reboot_replace_cn_ui_text(tab, 0) == 0);

	for (int i = 0; i < nr; i++) {
		if (tab[i])
			free_string(tab[i]);
	}
	free(tab);
	assert(escalayer_reboot_cn_ui_sjis(-1) == NULL);
	assert(escalayer_reboot_cn_ui_gbk(n) == NULL);
	puts("ok");
	return 0;
}
"""
        self._compile_and_run_ui(harness, includes, sources, sanitize=False)
        self._compile_and_run_ui(harness, includes, sources, sanitize=True)

    def test_cn_ui_table_matches_known_ain_strings_if_present(self) -> None:
        """Optional evidence check: table SJIS bytes equal the CN AIN display strings."""
        ain_path = Path("/workspace/escalayer-intro-review/cdn/EscalayerReboot.ain.decomp")
        if not ain_path.is_file():
            self.skipTest("CN AIN dump is not present in this environment")
        import struct
        raw = ain_path.read_bytes()
        off = raw.find(b"STR0")
        self.assertNotEqual(off, -1)
        nstr = struct.unpack_from("<I", raw, off + 4)[0]
        idx = off + 8
        table = []
        for _ in range(nstr):
            z = raw.find(b"\x00", idx)
            table.append(raw[idx:z])
            idx = z + 1
        expected_indexes = [
            2999, 3000, 3006, 3007, 3008, 3009, 3010, 3011, 3013,
            3021, 3022, 3023, 3038, 3040, 3041, 3042, 3043, 3044, 3045,
            3046, 3047, 3048, 3049, 3050, 3051, 3054, 3055, 3056, 3057,
            3058, 3059, 3060, 3061, 3062, 3063, 3065, 3066, 3067, 3068,
            3069, 3070, 3071, 3072, 3073, 3074, 3076, 3077, 3078, 3079,
            3080, 3081, 3082, 3085, 3086, 3087, 3088, 3089, 3090, 3091,
            3092, 3093, 3094, 3095, 3098, 3580, 3581, 3587, 3588, 3589,
            3590, 3591, 3592, 3593, 3594, 3595, 3596, 3597,
        ]
        def parse_c_string(lit: str) -> bytes:
            # lit includes the surrounding quotes.
            out = bytearray()
            i = 1
            while i < len(lit) - 1:
                if lit[i] == "\\" and i + 3 < len(lit) and lit[i + 1] == "x":
                    out.append(int(lit[i + 2:i + 4], 16))
                    i += 4
                elif lit[i] == "\\":
                    esc = {"n": 10, "t": 9, "r": 13, "\\": 92, '"': 34, "0": 0}
                    out.append(esc[lit[i + 1]])
                    i += 2
                else:
                    out.append(ord(lit[i]))
                    i += 1
            return bytes(out)

        src_text = (ROOT / "src/escalayer_reboot_cn_ui.c").read_text(encoding="utf-8")
        pair_re = re.compile(r'\{ ("(?:\\.|[^"\\])*"), ("(?:\\.|[^"\\])*") \}')
        pairs = [parse_c_string(a) for a, _ in pair_re.findall(src_text)]
        self.assertEqual(len(pairs), 77)
        for i, expected in zip(expected_indexes, pairs, strict=True):
            self.assertEqual(table[i], expected, f"STR0[{i}] drifted from the allowlist")
            jp = expected.decode("cp932")
            self.assertFalse(jp.startswith("コンフィグ／") or jp.startswith("タイトル／"), jp)
        # Resource keys in the same neighborhood must remain outside the table.
        for s in table:
            try:
                jp = s.decode("cp932")
            except UnicodeDecodeError:
                continue
            if jp.startswith("コンフィグ／") or jp.startswith("タイトル／") or jp.startswith("ネットワーク接続確認／"):
                self.assertNotIn(s, pairs, jp)

    def _compile_and_run_ui(self, c_source: str, includes: list[str], sources: list[Path], sanitize: bool) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "test.c"
            exe = Path(tmp) / "test_bin"
            src.write_text(c_source, encoding="utf-8")
            cmd = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                   "-ffunction-sections", "-fdata-sections"]
            if sanitize:
                cmd += ["-fsanitize=address,undefined"]
            cmd += ["-Wl,--gc-sections"]
            for inc in includes:
                cmd += [f"-I{inc}"]
            cmd += [str(src), *[str(s) for s in sources], "-o", str(exe)]
            subprocess.run(cmd, check=True)
            env = None
            if sanitize:
                import os
                env = os.environ.copy()
                env["ASAN_OPTIONS"] = "halt_on_error=1:abort_on_error=1"
                env["UBSAN_OPTIONS"] = "halt_on_error=1:abort_on_error=1"
            subprocess.run([str(exe)], check=True, env=env)


if __name__ == "__main__":
    unittest.main()
