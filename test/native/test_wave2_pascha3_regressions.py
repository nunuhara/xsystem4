#!/usr/bin/env python3
"""Native C regression tests for Wave 2 Pastel Chime 3 CN adaptations:
1. VSFile.c: read-only game_dir VSF files (Data/Parameter/*.dat) preserve raw
   Shift-JIS lookup keys in GBK mode, while save_dir VSF files still perform
   cross-encoding save migration.
2. MADLoader.c + util.c: mad_read_header() safely returns false on missing
   files without calling fclose(NULL), and gamedir_path() resolves both raw
   Shift-JIS and GBK paths to UTF-8 Data/Mad/ME/ME_ためる.mad & ME_精神集中.mad.
3. system4.c: GameName containing GBK "粉彩铃音" enables GBK mode.
"""
import unittest

from test_evenicle2_regressions import (
    LIBSYS4_CORE,
    ROOT,
    compile_and_run,
)
from test_font_and_draw_scissor_regressions import extract_function


class Wave2Pascha3NativeTests(unittest.TestCase):
    def test_parts_engine_hll_signatures(self):
        source = r'''
#include <assert.h>
#include <string.h>
#include "system4/ain.h"
#include "system4/string.h"
#include "hll.h"
#include "parts.h"
#include "parts_activity.h"
#include "parts_internal.h"

static struct parts part;
static int numeral_updates;
static bool activity_edit;

struct parts *parts_get(int no) { assert(no == 42); return &part; }
struct parts_gauge *parts_get_hgauge(struct parts *p, int state) { return &p->states[state].gauge; }
struct parts_gauge *parts_get_vgauge(struct parts *p, int state) { return &p->states[state].gauge; }
struct parts_numeral *parts_get_numeral(struct parts *p, int state) { return &p->states[state].num; }
void parts_hgauge_set_rate(struct parts *p, struct parts_gauge *g, float rate) { assert(0); }
void parts_vgauge_set_rate(struct parts *p, struct parts_gauge *g, float rate) { assert(0); }
static bool parts_numeral_update(struct parts *p, struct parts_numeral *n) {
    assert(p == &part && n == &p->states[2].num);
    numeral_updates++;
    return true;
}
bool PE_ReadActivityFile(struct string *name, struct string *file, bool edit) {
    assert(name && file);
    activity_edit = edit;
    return true;
}
'''
        source += "\n".join(extract_function("src/parts/parts.c", name) for name in (
            "PE_SetHGaugeRate", "PE_SetVGaugeRate", "PE_SetHGaugeRate_int", "PE_SetVGaugeRate_int",
            "parts_set_numeral_font", "PE_SetNumeralFont", "PE_SetNumeralFontWithPitch",
        ))
        source += "\n".join(extract_function("src/hll/PartsEngine.c", name) for name in (
            "PartsEngine_ReadActivityFile_legacy", "PartsEngine_ResolveReadActivityFile",
            "PartsEngine_ResolveGaugeRate", "PartsEngine_ResolveSetNumeralFont",
        ))
        hll = (ROOT / "src/hll/PartsEngine.c").read_text()
        source += "\nHLL_LIBRARY(PartsEngine,\n" + "\n".join(
            line.strip() for line in hll.splitlines()
            if any(line.strip().startswith(f"HLL_OVERLOAD({name},") for name in (
                "ReadActivityFile", "SetHGaugeRate", "SetVGaugeRate", "SetNumeralFont",
            ))
        ).rstrip(",") + ");\n"
        source += r'''
static void *resolve(char *name, enum ain_data_type result, int count, const enum ain_data_type *types) {
    struct ain_hll_argument args[13] = {0};
    for (int i = 0; i < count; i++)
        args[i].type.data = types[i];
    struct ain_hll_function f = {.name = name, .return_type = {.data = result},
                                .nr_arguments = count, .arguments = args};
    for (struct static_hll_function *e = lib_PartsEngine.functions; e->name; e++)
        if (!strcmp(e->name, name))
            return e->resolve(&f);
    return NULL;
}

int main(void) {
    const enum ain_data_type fractional[] = {AIN_INT, AIN_FLOAT, AIN_FLOAT, AIN_INT};
    bool (*horizontal)(int, float, float, int) = resolve("SetHGaugeRate", AIN_BOOL, 4, fractional);
    bool (*vertical)(int, float, float, int) = resolve("SetVGaugeRate", AIN_BOOL, 4, fractional);
    assert(horizontal && horizontal(42, 0.5f, 2.0f, 1));
    assert(vertical && vertical(42, 1.5f, 2.0f, 2));
    assert(part.states[0].gauge.rate == 0.25f && part.states[1].gauge.rate == 0.75f);
    const enum ain_data_type integral[] = {AIN_INT, AIN_INT, AIN_INT, AIN_INT};
    bool (*legacy_gauge)(int, int, int, int) = resolve("SetHGaugeRate", AIN_BOOL, 4, integral);
    assert(legacy_gauge && legacy_gauge(42, 2, 4, 1) && part.states[0].gauge.rate == 0.5f);

    enum ain_data_type numeral[] = {AIN_INT, AIN_INT, AIN_INT, AIN_INT, AIN_INT, AIN_INT,
                                    AIN_FLOAT, AIN_INT, AIN_INT, AIN_INT, AIN_FLOAT, AIN_BOOL, AIN_INT};
    bool (*set_font)(int, int, int, int, int, int, float, int, int, int, float, bool, int) =
        resolve("SetNumeralFont", AIN_BOOL, 13, numeral);
    struct parts_numeral *n = &part.states[2].num;
    assert(set_font && set_font(42, 2, 24, 10, 20, 30, 1.0f, 40, 50, 60, 2.0f, true, 3));
    assert(n->full_pitch && n->ts.size == 24 && numeral_updates == 1);
    assert(!set_font(42, 2, 12, 0, 0, 0, 0, 0, 0, 0, 0, false, 0));
    assert(n->full_pitch && numeral_updates == 1);
    numeral[11] = AIN_INT;
    void (*set_font_legacy)(int, int, int, int, int, int, float, int, int, int, float, int) =
        resolve("SetNumeralFont", AIN_VOID, 12, numeral);
    assert(set_font_legacy);
    set_font_legacy(42, 2, 16, 0, 0, 0, 0, 0, 0, 0, 0, 3);
    assert(n->full_pitch && n->ts.size == 16 && numeral_updates == 2);
    assert(set_font(42, 2, 12, 0, 0, 0, 0, 0, 0, 0, 0, false, 3));
    assert(!n->full_pitch && numeral_updates == 3);

    const enum ain_data_type activity[] = {AIN_STRING, AIN_STRING, AIN_BOOL};
    struct string name = {0}, file = {0};
    bool (*read_activity)(struct string *, struct string *, bool) = resolve("ReadActivityFile", AIN_BOOL, 3, activity);
    bool (*read_activity_legacy)(struct string *, struct string *) = resolve("ReadActivityFile", AIN_BOOL, 2, activity);
    assert(read_activity && read_activity(&name, &file, true) && activity_edit);
    assert(read_activity_legacy && read_activity_legacy(&name, &file) && !activity_edit);
    return 0;
}
'''
        compile_and_run(source, [])

    def test_system4_pascha3_gbk_trigger(self):
        src = (ROOT / "src/system4.c").read_text(encoding="utf-8", errors="ignore")
        self.assertIn(r'"\xb7\xdb\xb2\xca\xc1\xe5\xd2\xf4"', src)

    def test_vsfile_gamedir_preserves_raw_keys_and_savedir_converts(self):
        fn_bias = extract_function("src/savedata.c", "save_text_gbk_bias")
        harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "system4.h"
#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "savedata.h"
#include "xsystem4.h"

struct config config;
_Noreturn void _vm_error(const char *fmt, ...) {{ abort(); }}

static bool cjk_is_ascii_only(const char *s) {{
    if (!s) return true;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {{
        if (*p >= 0x80) return false;
    }}
    return true;
}}

{fn_bias}

struct string *save_decode_vm_string(const char *raw, size_t len, bool src_is_gbk) {{
    (void)raw;
    (void)len;
    (void)src_is_gbk;
    return make_string("<CONVERTED>", 11);
}}

// Include VSFile.c directly so static functions can be exercised cleanly.
#include "hll/VSFile.c"

static void write_vsf_string(const char *path, const char *raw_text) {{
    FILE *fp = fopen(path, "wb");
    assert(fp != NULL);
    assert(fwrite("VSF\\0\\0\\0\\0", 1, 8, fp) == 8);
    uint8_t tag = 3; // VSF_STRING
    assert(fwrite(&tag, 1, 1, fp) == 1);
    assert(fwrite(raw_text, 1, strlen(raw_text) + 1, fp) == strlen(raw_text) + 1);
    fclose(fp);
}}

int main(void) {{
    char tmpl[] = "/tmp/xsystem4_vsfile_test_XXXXXX";
    char *root = mkdtemp(tmpl);
    assert(root != NULL);

    char gamedir[256], savedir[256], pdir[512], gfile[768], sfile[768], ddir[512];
    snprintf(gamedir, sizeof(gamedir), "%s/game", root);
    snprintf(savedir, sizeof(savedir), "%s/save", root);
    snprintf(pdir, sizeof(pdir), "%s/Data/Parameter", gamedir);
    snprintf(gfile, sizeof(gfile), "%s/keys.dat", pdir);
    snprintf(sfile, sizeof(sfile), "%s/slot0.vsf", savedir);
    snprintf(ddir, sizeof(ddir), "%s/Data", gamedir);

    assert(mkdir(gamedir, 0755) == 0);
    assert(mkdir(savedir, 0755) == 0);
    assert(mkdir_p(pdir) == 0);

    // Shift-JIS katakana string "デバッ君テスト" (strong negative GBK bias: -12 or less)
    const char *sjis_key = "\\x83\\x66\\x83\\x6f\\x83\\x62\\x8c\\x4e\\x83\\x65\\x83\\x58\\x83\\x67";
    assert(save_text_gbk_bias(sjis_key) <= -12);

    write_vsf_string(gfile, sjis_key);
    write_vsf_string(sfile, sjis_key);

    memset(&config, 0, sizeof(config));
    config.game_dir = gamedir;
    config.save_dir = savedir;
    sys4_set_text_gbk(true);

    // 1. Reading game_dir VSF in GBK mode MUST preserve raw Shift-JIS lookup key verbatim.
    struct string *grel = make_string("Data\\\\Parameter\\\\keys.dat", 23);
    assert(VSFile_OpenForRead(grel));
    assert(vs_from_gamedir == true);
    struct string *out = NULL;
    assert(VSFile_ReadString(&out));
    assert(out != NULL);
    assert(strcmp(out->text, sjis_key) == 0);
    free_string(out);
    out = NULL;
    assert(VSFile_Close());
    assert(vs_from_gamedir == false);
    free_string(grel);

    // 2. Reading save_dir VSF in GBK mode MUST still perform SJIS -> GBK conversion.
    struct string *srel = make_string("slot0.vsf", 9);
    assert(VSFile_OpenForRead(srel));
    assert(vs_from_gamedir == false);
    assert(VSFile_ReadString(&out));
    assert(out != NULL);
    assert(strcmp(out->text, "<CONVERTED>") == 0);
    free_string(out);
    out = NULL;
    assert(VSFile_Close());
    free_string(srel);

    // Cleanup
    unlink(gfile);
    unlink(sfile);
    rmdir(pdir);
    rmdir(ddir);
    rmdir(gamedir);
    rmdir(savedir);
    rmdir(root);

    puts("OK");
    return 0;
}}
"""
        out = compile_and_run(
            harness,
            [ROOT / "src/util.c", *LIBSYS4_CORE],
            [
                f"-I{ROOT / 'src'}",
                f"-I{ROOT / 'src/hll'}",
                "-ffunction-sections",
                "-fdata-sections",
                "-Wl,--gc-sections",
                "-lz",
                "-lm",
            ],
        )
        self.assertEqual(out.strip(), "OK")

    def test_mad_loader_null_guard_and_path_resolution(self):
        mad_src = (ROOT / "src/hll/MADLoader.c").read_text(encoding="utf-8", errors="ignore")
        self.assertIn("if (!fp)\n\t\treturn false;", mad_src)

        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "system4.h"
#include "system4/file.h"
#include "system4/utfsjis.h"
#include "xsystem4.h"

struct config config;

int main(void) {
    char tmpl[] = "/tmp/xsystem4_mad_path_test_XXXXXX";
    char *root = mkdtemp(tmpl);
    assert(root != NULL);

    char mdir[512], f1[512], f2[512];
    snprintf(mdir, sizeof(mdir), "%s/Data/Mad/ME", root);
    assert(mkdir_p(mdir) == 0);
    snprintf(f1, sizeof(f1), "%s/ME_\xe3\x81\x9f\xe3\x82\x81\xe3\x82\x8b.mad", mdir);
    snprintf(f2, sizeof(f2), "%s/ME_\xe7\xb2\xbe\xe7\xa5\x9e\xe9\x9b\x86\xe4\xb8\xad.mad", mdir);

    FILE *fp = fopen(f1, "wb"); assert(fp); fwrite("MAD\0", 1, 4, fp); fclose(fp);
    fp = fopen(f2, "wb"); assert(fp); fwrite("MAD\0", 1, 4, fp); fclose(fp);

    memset(&config, 0, sizeof(config));
    config.game_dir = root;
    sys4_set_text_gbk(true);

    // 1. Raw Shift-JIS paths from VM strings resolve to UTF-8 filenames in GBK mode.
    const char *sjis_tameru = "Data\\Mad\\ME\\ME_\x82\xbd\x82\xdf\x82\xe9.mad";
    const char *sjis_seishin = "Data\\Mad\\ME\\ME_\x90\xb8\x90\x5f\x8f\x57\x92\x86.mad";
    char *p1 = gamedir_path(sjis_tameru);
    char *p2 = gamedir_path(sjis_seishin);
    assert(file_exists(p1));
    assert(file_exists(p2));
    free(p1);
    free(p2);

    // 2. Raw GBK paths also resolve cleanly to the same UTF-8 filenames.
    const char *gbk_tameru = "Data\\Mad\\ME\\ME_\xa4\xbf\xa4\xe1\xa4\xeb.mad";
    const char *gbk_seishin = "Data\\Mad\\ME\\ME_\xbe\xab\xc9\xf1\xbc\xaf\xd6\xd0.mad";
    p1 = gamedir_path(gbk_tameru);
    p2 = gamedir_path(gbk_seishin);
    assert(file_exists(p1));
    assert(file_exists(p2));
    free(p1);
    free(p2);

    unlink(f1);
    unlink(f2);
    rmdir(mdir);
    char d1[512], d2[512];
    snprintf(d1, sizeof(d1), "%s/Data/Mad", root);
    snprintf(d2, sizeof(d2), "%s/Data", root);
    rmdir(d1);
    rmdir(d2);
    rmdir(root);

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            [ROOT / "src/util.c", *LIBSYS4_CORE],
            [f"-I{ROOT / 'src'}", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-lz", "-lm"],
        )
        self.assertEqual(out.strip(), "OK")


if __name__ == "__main__":
    unittest.main()
