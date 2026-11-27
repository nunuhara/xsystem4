#!/usr/bin/env python3
"""Dependency-free native regression tests for Evenicle S_GETPART / string_copy().

Compiles the actual C implementations of string_copy(), string_find(),
make_string(), free_string() from xsystem4/subprojects/libsys4/src/string.c and
sjis_index() from xsystem4/subprojects/libsys4/src/utfsjis.c (against the real
system4/string.h and system4/utfsjis.h headers) to verify:

1. Evenicle's `[6682] 文字列分割` splitter loop preserves the final token when
   `S_FIND` returns `-1` and calls `S_GETPART(s, 0, -1)` on real Evenicle.ex
   skill script strings (Shift-JIS and GBK modes).
2. Exact System 4.3 `Evenicle.exe` (`0x005b7560`/`0x005c4320`) `S_GETPART` edge
   cases, including `len <= 0`, `index + len < 0` vs `>= 0`, `INT_MIN`/`INT_MAX`
   without signed overflow UB, negative `index` clamping, empty string, and
   `index >= char_len`.
3. Shift-JIS (including 1-byte half-width katakana, 0x81..0x9f/0xe0..0xfc lead
   bytes, and ASCII-range trail bytes) and GBK (`sys4_text_is_gbk = true`,
   including 0x81..0xfe lead bytes and ASCII-range trail bytes) multibyte
   character indexing boundaries.

Run: python3 xsystem4/test/native/test_evenicle_string_getpart_regressions.py
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LIBSYS4_INC = ROOT / "subprojects" / "libsys4" / "include"


def extract_function(rel_path: str, name: str) -> str:
    source = (ROOT / rel_path).read_text(encoding="utf-8")
    pattern = re.compile(
        rf"^(?:static\s+)?[A-Za-z_][A-Za-z0-9_\s\*]*\b{re.escape(name)}\([^;{{}}]*\)\s*\r?\n?\{{",
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


def c_bytes_expr(data: bytes) -> tuple[str, int]:
    """Return a C11 compound-literal char* expression and byte length."""
    if not data:
        return '""', 0
    hex_elems = ", ".join(f"0x{b:02x}" for b in data)
    return f"(const char *)(const uint8_t[]){{ {hex_elems}, 0x00 }}", len(data)


def compile_and_run(c_source: str) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "test.c"
        exe = Path(tmp) / "test_bin"
        src.write_text(c_source, encoding="utf-8")
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-O2",
                f"-I{LIBSYS4_INC}",
                str(src),
                "-o",
                str(exe),
            ],
            check=True,
        )
        subprocess.run([str(exe)], check=True)


def build_string_harness(body: str) -> str:
    fns = "\n\n".join(
        [
            extract_function("subprojects/libsys4/src/utfsjis.c", "sjis_index"),
            extract_function("subprojects/libsys4/src/string.c", "alloc_string"),
            extract_function("subprojects/libsys4/src/string.c", "free_string"),
            extract_function("subprojects/libsys4/src/string.c", "make_string"),
            extract_function("subprojects/libsys4/src/string.c", "string_copy"),
            extract_function("subprojects/libsys4/src/string.c", "string_find"),
        ]
    )
    return f"""
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/string.h"
#include "system4/utfsjis.h"

bool sys4_text_is_gbk = false;

static void *xmalloc(size_t size) {{
    void *p = malloc(size ? size : 1);
    assert(p != NULL);
    return p;
}}

#define ERROR(fmt, ...) do {{ fprintf(stderr, "ERROR: " fmt "\\n", ##__VA_ARGS__); abort(); }} while (0)

{fns}

static void assert_str_eq(const struct string *actual, const char *expected, size_t expected_len) {{
    if ((size_t)actual->size != expected_len ||
        memcmp(actual->text, expected, expected_len) != 0 ||
        actual->text[actual->size] != '\\0') {{
        fprintf(stderr, "string mismatch: got len=%d, expected len=%zu\\n",
                actual->size, expected_len);
        abort();
    }}
}}

static void assert_getpart(const char *src_bytes, size_t src_len,
                           int index, int len,
                           const char *exp_bytes, size_t exp_len) {{
    struct string *s = make_string(src_bytes, src_len);
    struct string *out = string_copy(s, index, len);
    assert_str_eq(out, exp_bytes, exp_len);
    free_string(out);
    free_string(s);
}}

/* Exact Evenicle [6682] 文字列分割 loop:
 *   findPos = S_FIND(s, delim);
 *   push_back(S_GETPART(s, 0, findPos));
 *   if (findPos < 0) break;
 *   s = S_GETPART(s, findPos + 1, 99999);
 */
static int evenicle_split(const struct string *input, const struct string *delim,
                          struct string **out_tokens, int max_tokens) {{
    struct string *s = make_string(input->text, (size_t)input->size);
    int count = 0;
    for (;;) {{
        int find_pos = string_find(s, delim);
        struct string *part = string_copy(s, 0, find_pos);
        assert(count < max_tokens);
        out_tokens[count++] = part;
        if (find_pos < 0)
            break;
        struct string *rest = string_copy(s, find_pos + 1, 99999);
        free_string(s);
        s = rest;
    }}
    free_string(s);
    return count;
}}

int main(void) {{
    (void)assert_getpart;
    (void)evenicle_split;
{body}
    return 0;
}}
"""


def emit_split_check(
    line: str,
    delim: str,
    expected: list[str],
    encoding: str,
    gbk_mode: bool,
    tag: str,
) -> str:
    src_expr, src_len = c_bytes_expr(line.encode(encoding))
    delim_expr, delim_len = c_bytes_expr(delim.encode(encoding))
    checks = []
    for idx, tok in enumerate(expected):
        tok_expr, tok_len = c_bytes_expr(tok.encode(encoding))
        checks.append(f"        assert_str_eq(tokens[{idx}], {tok_expr}, {tok_len});")
    checks_str = "\n".join(checks)
    return f"""
    {{
        /* {tag} */
        sys4_text_is_gbk = {"true" if gbk_mode else "false"};
        struct string *input = make_string({src_expr}, {src_len});
        struct string *delim = make_string({delim_expr}, {delim_len});
        struct string *tokens[16] = {{0}};
        int n = evenicle_split(input, delim, tokens, 16);
        assert(n == {len(expected)});
{checks_str}
        for (int i = 0; i < n; i++)
            free_string(tokens[i]);
        free_string(delim);
        free_string(input);
    }}
"""


def emit_getpart_check(
    src: str | bytes,
    index: int | str,
    length: int | str,
    expected: str | bytes,
    encoding: str = "ascii",
) -> str:
    src_b = src if isinstance(src, bytes) else src.encode(encoding)
    exp_b = expected if isinstance(expected, bytes) else expected.encode(encoding)
    src_expr, src_len = c_bytes_expr(src_b)
    exp_expr, exp_len = c_bytes_expr(exp_b)
    return (
        f"    assert_getpart({src_expr}, {src_len}, {index}, {length}, "
        f"{exp_expr}, {exp_len});"
    )


class EvenicleStringGetpartRegressionTests(unittest.TestCase):
    def test_evenicle_skill_script_splitter_sjis_and_gbk(self) -> None:
        blocks = [
            emit_split_check(
                "フラット再生 開始時間=100 ファイル=バトル／剣／攻撃 _基準=相手",
                " ",
                [
                    "フラット再生",
                    "開始時間=100",
                    "ファイル=バトル／剣／攻撃",
                    "_基準=相手",
                ],
                encoding="cp932",
                gbk_mode=False,
                tag="Evenicle.ex normal sword attack effect (SJIS)",
            ),
            emit_split_check(
                "_基準=相手",
                "=",
                ["_基準", "相手"],
                encoding="cp932",
                gbk_mode=False,
                tag="Evenicle.ex key=value split preserving target anchor (SJIS)",
            ),
            emit_split_check(
                "フラット再生 開始時間=20 ファイル=バトル／補助／攻撃態勢 _音=2075 _位置Ｘ=0 _位置Ｙ=0 _基準=自分",
                " ",
                [
                    "フラット再生",
                    "開始時間=20",
                    "ファイル=バトル／補助／攻撃態勢",
                    "_音=2075",
                    "_位置Ｘ=0",
                    "_位置Ｙ=0",
                    "_基準=自分",
                ],
                encoding="cp932",
                gbk_mode=False,
                tag="Evenicle.ex self-buff skill effect (SJIS)",
            ),
            emit_split_check(
                "効果反映 開始時間=200",
                " ",
                ["効果反映", "開始時間=200"],
                encoding="cp932",
                gbk_mode=False,
                tag="Evenicle.ex effect apply command (SJIS)",
            ),
            emit_split_check(
                "キャラモーション タイプ=攻撃",
                " ",
                ["キャラモーション", "タイプ=攻撃"],
                encoding="cp932",
                gbk_mode=False,
                tag="Evenicle.ex chara motion command (SJIS)",
            ),
            emit_split_check(
                "_基準=相手",
                " ",
                ["_基準=相手"],
                encoding="cp932",
                gbk_mode=False,
                tag="Single token with no delimiter (SJIS)",
            ),
            emit_split_check(
                "",
                " ",
                [""],
                encoding="cp932",
                gbk_mode=False,
                tag="Empty input string",
            ),
            emit_split_check(
                "播放动画 开始时间=100 文件=战斗／剑／攻击 _基准=对手",
                " ",
                [
                    "播放动画",
                    "开始时间=100",
                    "文件=战斗／剑／攻击",
                    "_基准=对手",
                ],
                encoding="gbk",
                gbk_mode=True,
                tag="GBK skill script command preserving _基准=对手",
            ),
            emit_split_check(
                "播放动画 开始时间=20 文件=战斗／辅助／攻击态势 _音=2075 _位置X=0 _位置Y=0 _基准=自己",
                " ",
                [
                    "播放动画",
                    "开始时间=20",
                    "文件=战斗／辅助／攻击态势",
                    "_音=2075",
                    "_位置X=0",
                    "_位置Y=0",
                    "_基准=自己",
                ],
                encoding="gbk",
                gbk_mode=True,
                tag="GBK self-buff skill command preserving _基准=自己",
            ),
            emit_split_check(
                "_基准=对手",
                "=",
                ["_基准", "对手"],
                encoding="gbk",
                gbk_mode=True,
                tag="GBK key=value split preserving final Chinese token",
            ),
        ]
        compile_and_run(build_string_harness("\n".join(blocks)))

    def test_s_getpart_system43_edge_cases(self) -> None:
        checks = [
            "    sys4_text_is_gbk = false;",
            # len < 0 with index == 0 -> full string (Evenicle.exe 0x005b7560 / 0x005c4320)
            emit_getpart_check("abcdef", 0, -1, "abcdef"),
            emit_getpart_check("abcdef", 0, -100, "abcdef"),
            emit_getpart_check("abcdef", 0, "INT_MIN", "abcdef"),
            # len == 0 -> empty string
            emit_getpart_check("abcdef", 0, 0, ""),
            emit_getpart_check("abcdef", 3, 0, ""),
            # empty input string
            emit_getpart_check("", 0, -1, ""),
            emit_getpart_check("", 0, 0, ""),
            emit_getpart_check("", 0, "INT_MIN", ""),
            emit_getpart_check("", -5, -1, ""),
            emit_getpart_check("", 0, 10, ""),
            # non-zero index with negative len: empty iff index + len >= 0, suffix iff index + len < 0
            emit_getpart_check("abcdef", 3, -1, ""),
            emit_getpart_check("abcdef", 3, -2, ""),
            emit_getpart_check("abcdef", 3, -3, ""),
            emit_getpart_check("abcdef", 3, -4, "def"),
            emit_getpart_check("abcdef", 3, -100, "def"),
            emit_getpart_check("abcdef", 3, "INT_MIN", "def"),
            # INT_MAX + INT_MIN == -1 < 0 must not overflow signed int
            emit_getpart_check("abcdef", "INT_MAX", "INT_MIN", ""),
            # negative index clamps to 0 before checking index + len
            emit_getpart_check("abcdef", -5, 3, "abc"),
            emit_getpart_check("abcdef", -5, -1, "abcdef"),
            emit_getpart_check("abcdef", -5, 0, ""),
            emit_getpart_check("abcdef", "INT_MIN", -1, "abcdef"),
            emit_getpart_check("abcdef", "INT_MIN", 2, "ab"),
            # index >= char_len -> empty string
            emit_getpart_check("abcdef", 16, -1, ""),
            emit_getpart_check("abcdef", 16, -100, ""),
            emit_getpart_check("abcdef", 6, -100, ""),
            emit_getpart_check("abcdef", 6, 1, ""),
            # large positive len -> suffix from char index to end
            emit_getpart_check("abcdef", 2, 99999, "cdef"),
            emit_getpart_check("abcdef", 2, "INT_MAX", "cdef"),
            # standard positive slices
            emit_getpart_check("abcdef", 0, 3, "abc"),
            emit_getpart_check("abcdef", 2, 3, "cde"),
            emit_getpart_check("abcdef", 5, 1, "f"),
        ]
        compile_and_run(build_string_harness("\n".join(checks)))

    def test_multibyte_sjis_and_gbk_indexing_boundaries(self) -> None:
        sjis_trail_src, sjis_trail_len = c_bytes_expr("表\\剣".encode("cp932"))
        sjis_bs_src, sjis_bs_len = c_bytes_expr(b"\\")
        gbk_trail_src, gbk_trail_len = c_bytes_expr("丄A测试".encode("gbk"))
        gbk_a_src, gbk_a_len = c_bytes_expr(b"A")

        checks = [
            # --- Shift-JIS mode ---
            "    sys4_text_is_gbk = false;",
            # "_基準=相手": 6 chars (bytes: 1 + 2 + 2 + 1 + 2 + 2 = 10)
            emit_getpart_check("_基準=相手", 0, -1, "_基準=相手", "cp932"),
            emit_getpart_check("_基準=相手", 0, 3, "_基準", "cp932"),
            emit_getpart_check("_基準=相手", 1, 2, "基準", "cp932"),
            emit_getpart_check("_基準=相手", 3, 1, "=", "cp932"),
            emit_getpart_check("_基準=相手", 4, 2, "相手", "cp932"),
            emit_getpart_check("_基準=相手", 4, 99999, "相手", "cp932"),
            emit_getpart_check("_基準=相手", 4, -4, "", "cp932"),
            emit_getpart_check("_基準=相手", 4, -5, "相手", "cp932"),
            emit_getpart_check("_基準=相手", 6, -10, "", "cp932"),
            # Mixed ASCII, 1-byte half-width katakana (0xB1, 0xB2), 0x81..0x9F (剣), 0xE0..0xFC (遙)
            # "Aｱｲ剣遙B": 6 chars (bytes: 1 + 1 + 1 + 2 + 2 + 1 = 8)
            emit_getpart_check("Aｱｲ剣遙B", 0, -1, "Aｱｲ剣遙B", "cp932"),
            emit_getpart_check("Aｱｲ剣遙B", 1, 2, "ｱｲ", "cp932"),
            emit_getpart_check("Aｱｲ剣遙B", 3, 1, "剣", "cp932"),
            emit_getpart_check("Aｱｲ剣遙B", 4, 1, "遙", "cp932"),
            emit_getpart_check("Aｱｲ剣遙B", 3, -3, "", "cp932"),
            emit_getpart_check("Aｱｲ剣遙B", 3, -4, "剣遙B", "cp932"),
            emit_getpart_check("Aｱｲ剣遙B", 5, -6, "B", "cp932"),
            emit_getpart_check("Aｱｲ剣遙B", 6, -10, "", "cp932"),
            # Shift-JIS 2-byte char with 0x5C ('\\') trail byte ("表" = 0x95 0x5C)
            emit_getpart_check("表\\剣", 0, 1, "表", "cp932"),
            emit_getpart_check("表\\剣", 1, 1, "\\", "cp932"),
            emit_getpart_check("表\\剣", 2, -3, "剣", "cp932"),
            f"""
    {{
        struct string *hay = make_string({sjis_trail_src}, {sjis_trail_len});
        struct string *ndl = make_string({sjis_bs_src}, {sjis_bs_len});
        /* Must match char index 1 (literal '\\\\'), not trail byte of '表' */
        assert(string_find(hay, ndl) == 1);
        free_string(ndl);
        free_string(hay);
    }}""",
            # Truncated trailing 2-byte SJIS lead byte (0x82 with no trail byte)
            emit_getpart_check(b"a\x82", 0, -1, b"a\x82"),
            emit_getpart_check(b"a\x82", 1, -2, b"\x82"),
            emit_getpart_check(b"a\x82", 2, -10, b""),
            # --- GBK mode ---
            "    sys4_text_is_gbk = true;",
            # "_基准=对手": GBK lead bytes 0xBB, 0xD7, 0xB6, 0xCA are all in 0xA1..0xDF
            # (would be 1-byte half-width katakana in Shift-JIS, must be 2-byte in GBK)
            emit_getpart_check("_基准=对手", 0, -1, "_基准=对手", "gbk"),
            emit_getpart_check("_基准=对手", 0, 3, "_基准", "gbk"),
            emit_getpart_check("_基准=对手", 1, 2, "基准", "gbk"),
            emit_getpart_check("_基准=对手", 3, 1, "=", "gbk"),
            emit_getpart_check("_基准=对手", 4, 2, "对手", "gbk"),
            emit_getpart_check("_基准=对手", 4, 99999, "对手", "gbk"),
            emit_getpart_check("_基准=对手", 4, -4, "", "gbk"),
            emit_getpart_check("_基准=对手", 4, -5, "对手", "gbk"),
            emit_getpart_check("_基准=对手", 6, -10, "", "gbk"),
            # GBK 2-byte char "丄" (0x81 0x41) has ASCII 'A' as trail byte, followed by literal 'A'
            emit_getpart_check("丄A测试", 0, 1, "丄", "gbk"),
            emit_getpart_check("丄A测试", 1, 1, "A", "gbk"),
            emit_getpart_check("丄A测试", 2, 2, "测试", "gbk"),
            emit_getpart_check("丄A测试", 2, -2, "", "gbk"),
            emit_getpart_check("丄A测试", 2, -3, "测试", "gbk"),
            emit_getpart_check("丄A测试", 4, -10, "", "gbk"),
            f"""
    {{
        struct string *hay = make_string({gbk_trail_src}, {gbk_trail_len});
        struct string *ndl = make_string({gbk_a_src}, {gbk_a_len});
        /* Must match char index 1 (literal 'A'), not trail byte 0x41 of '丄' */
        assert(string_find(hay, ndl) == 1);
        free_string(ndl);
        free_string(hay);
    }}""",
            # Truncated trailing 2-byte GBK lead byte (0xBB with no trail byte)
            emit_getpart_check(b"a\xbb", 0, -1, b"a\xbb"),
            emit_getpart_check(b"a\xbb", 1, -2, b"\xbb"),
            emit_getpart_check(b"a\xbb", 2, -10, b""),
        ]
        compile_and_run(build_string_harness("\n".join(checks)))


if __name__ == "__main__":
    unittest.main()
