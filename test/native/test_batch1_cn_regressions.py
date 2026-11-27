#!/usr/bin/env python3
"""Native C regression tests for Batch 1 CN System 4.x adaptations."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_vm_reign_regressions import extract_function


class Batch1CnNativeTests(unittest.TestCase):
    def test_ain_messages_look_gbk(self):
        function = extract_function("src/hacks.c", "ain_messages_look_gbk")
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct string { int size; char *text; };
struct ain { int nr_messages; struct string **messages; };
''' + function + r'''
static struct string *mk(const char *s) {
    struct string *st = calloc(1, sizeof(*st));
    st->text = strdup(s);
    st->size = (int)strlen(s);
    return st;
}
int main(void) {
    // 1. GBK Chinese messages (>64 pairs) -> true
    struct string *gbk_msgs[10];
    // "大帝国大番长校内赤线区欢迎你" in GBK (14 hanzi * 10 = 140 pairs)
    const char *cn = "\xb4\xf3\xb5\xdb\xb9\xfa\xb4\xf3\xb7\xac\xb3\xa4\xd0\xa3\xc4\xda\xb3\xe0\xcf\xdf\xc7\xf8\xbb\xb6\xd3\xad\xc4\xe3";
    for (int i = 0; i < 10; i++) gbk_msgs[i] = mk(cn);
    struct ain ain_cn = { .nr_messages = 10, .messages = gbk_msgs };
    assert(ain_messages_look_gbk(&ain_cn) == true);

    // 2. Shift-JIS Japanese messages (hiragana/katakana + kanji) -> false
    struct string *jp_msgs[10];
    // "おはようございます。これはテストです。" in Shift-JIS
    const char *jp = "\x82\xa8\x82\xcd\x82\xe6\x82\xa4\x82\xb2\x82\xb4\x82\xa2\x82\xdc\x82\xb7\x81\x42\x82\xb1\x82\xea\x82\xcd\x83\x65\x83\x58\x83\x67\x82\xc5\x82\xb7\x81\x42";
    for (int i = 0; i < 10; i++) jp_msgs[i] = mk(jp);
    struct ain ain_jp = { .nr_messages = 10, .messages = jp_msgs };
    assert(ain_messages_look_gbk(&ain_jp) == false);

    // 3. Shift-JIS kanji-heavy text whose trail bytes fall in 0xB0..0xD7 must not misalign into fake GBK pairs
    struct string *sjis_kanji_msgs[10];
    // Repeating SJIS 0x90B0 0x90B1 0x90B2 ... (lead 0x90, trail 0xB0..0xB9)
    const char *sjis_kanji = "\x90\xb0\x90\xb1\x90\xb2\x90\xb3\x90\xb4\x90\xb5\x90\xb6\x90\xb7\x90\xb8\x90\xb9";
    for (int i = 0; i < 10; i++) sjis_kanji_msgs[i] = mk(sjis_kanji);
    struct ain ain_sjis_kanji = { .nr_messages = 10, .messages = sjis_kanji_msgs };
    assert(ain_messages_look_gbk(&ain_sjis_kanji) == false);

    // 4. Short / NULL messages -> false (does not crash)
    struct string *short_msgs[2] = { NULL, mk("hello world") };
    struct ain ain_short = { .nr_messages = 2, .messages = short_msgs };
    assert(ain_messages_look_gbk(&ain_short) == false);

    return 0;
}
'''
        with tempfile.TemporaryDirectory() as td:
            src = Path(td) / "test_gbk.c"
            exe = Path(td) / "test_gbk"
            src.write_text(harness)
            subprocess.run(["cc", "-D_GNU_SOURCE", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(src), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_gpx2plus_chinese_font_names(self):
        function = extract_function("src/hll/Gpx2Plus.c", "Gpx2Plus_SetFontName")
        harness = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum font_face { FONT_NONE = -1, FONT_GOTHIC = 0, FONT_MINCHO = 1 };
struct string { int size; char *text; };
static enum font_face last_face = FONT_NONE;
static int warn_count = 0;

static char *sjis2utf(const char *s, size_t n) { (void)n; return strdup(s); }
static const char *display_utf0(const char *s) { return s; }
static void gfx_set_font_face(enum font_face f) { last_face = f; }
static void record_warning(const char *fmt, const char *arg) { (void)fmt; (void)arg; warn_count++; }
#define WARNING(fmt, arg) record_warning(fmt, arg)
''' + function + r'''
static void check(const char *utf8_name, enum font_face expected_face, int expected_warns) {
    struct string s = { .size = (int)strlen(utf8_name), .text = (char *)utf8_name };
    last_face = FONT_NONE;
    int before = warn_count;
    Gpx2Plus_SetFontName(&s);
    assert(last_face == expected_face);
    assert(warn_count - before == expected_warns);
}
int main(void) {
    check("ＭＳ ゴシック", FONT_GOTHIC, 0);
    check("MS Gothic", FONT_GOTHIC, 0);
    check("黑体", FONT_GOTHIC, 0);
    check("黒体", FONT_GOTHIC, 0);
    check("SimHei", FONT_GOTHIC, 0);
    check("ＭＳ 明朝", FONT_MINCHO, 0);
    check("MS Mincho", FONT_MINCHO, 0);
    check("宋体", FONT_MINCHO, 0);
    check("SimSun", FONT_MINCHO, 0);
    check("UnknownFont", FONT_NONE, 1);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as td:
            src = Path(td) / "test_font.c"
            exe = Path(td) / "test_font"
            src.write_text(harness)
            subprocess.run(["cc", "-D_GNU_SOURCE", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(src), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_config_init_with_dir_prefers_alicestart2_for_haru_c(self):
        function = extract_function("src/system4.c", "config_init_with_dir")
        harness = r'''
#define _DEFAULT_SOURCE
#include <assert.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static char chosen_ini[512];
static char *path_join(const char *a, const char *b) {
    char *p; assert(asprintf(&p, "%s/%s", a, b) >= 0); return p;
}
static bool file_exists(const char *p) { struct stat s; return stat(p, &s) == 0; }
static char *path_get_icase(const char *p) {
    char *dir = strdup(p); char *base = strrchr(dir, '/'); *base++ = 0;
    DIR *d = opendir(dir); char *result = NULL; struct dirent *e;
    while (d && (e = readdir(d))) {
        if (!strcasecmp(base, e->d_name)) { result = path_join(dir, e->d_name); break; }
    }
    if (d) closedir(d);
    free(dir); return result;
}
static bool config_init_with_ini(const char *ini_path) {
    snprintf(chosen_ini, sizeof(chosen_ini), "%s", ini_path);
    return true;
}
''' + function + r'''
int main(int argc, char **argv) {
    assert(argc == 3);
    chosen_ini[0] = '\0';
    assert(config_init_with_dir(argv[1]) == true);
    const char *base = strrchr(chosen_ini, '/');
    base = base ? base + 1 : chosen_ini;
    assert(strcmp(base, argv[2]) == 0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "test_ini.c"
            exe = root / "test_ini"
            src.write_text(harness)
            subprocess.run(["cc", "-D_GNU_SOURCE", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(src), "-o", str(exe)], check=True)

            # Dual INI + haru_c.ain -> AliceStart2.ini wins
            d1 = root / "dual_cn"
            d1.mkdir()
            (d1 / "AliceStart.ini").touch()
            (d1 / "AliceStart2.ini").touch()
            (d1 / "haru_c.ain").touch()
            subprocess.run([str(exe), str(d1), "AliceStart2.ini"], check=True)

            # Dual INI without haru_c.ain -> AliceStart.ini wins
            d2 = root / "dual_jp"
            d2.mkdir()
            (d2 / "AliceStart.ini").touch()
            (d2 / "AliceStart2.ini").touch()
            subprocess.run([str(exe), str(d2), "AliceStart.ini"], check=True)

            # Only AliceStart2.ini -> falls back to AliceStart2.ini
            d3 = root / "only_ini2"
            d3.mkdir()
            (d3 / "AliceStart2.ini").touch()
            subprocess.run([str(exe), str(d3), "AliceStart2.ini"], check=True)


if __name__ == "__main__":
    unittest.main()
