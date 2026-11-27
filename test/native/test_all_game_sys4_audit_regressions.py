#!/usr/bin/env python3
"""Host C regression tests for System 4 (xsystem4 + libsys4) 170-game audit fixes.

Run: PYTHONPATH=xsystem4/test/native python3 -m unittest xsystem4/test/native/test_all_game_sys4_audit_regressions.py
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

_TEST_DIR = str(Path(__file__).resolve().parent)
if _TEST_DIR not in sys.path:
    sys.path.insert(0, _TEST_DIR)

from test_evenicle2_regressions import LIBSYS4_CORE, ROOT, compile_and_run
from test_vm_reign_regressions import extract_function

LIBSYS4_EXTRA = [
    *LIBSYS4_CORE,
    ROOT / "subprojects/libsys4/src/ald.c",
]


def _extract_defines(rel_path: str, prefix: str) -> str:
    lines = []
    for line in (ROOT / rel_path).read_text(encoding="utf-8", errors="ignore").splitlines():
        if line.startswith("#define " + prefix):
            lines.append(line)
    return "\n".join(lines)


class Sys4AuditRegressionsTest(unittest.TestCase):
    def test_cjk_repair_and_dir_find_mojibake_icase(self):
        fn_non_ascii = extract_function("src/util.c", "path_component_has_non_ascii")
        fn_dir_find = extract_function("src/util.c", "dir_find_mojibake_icase")
        harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "system4.h"
#include "system4/file.h"
#include "system4/utfsjis.h"

{fn_non_ascii}
{fn_dir_find}

int main(int argc, char **argv) {{
    assert(argc == 2);
    const char *dir = argv[1];

    // 1. Valid Japanese filenames and previously-corrupted forms MUST NOT mutate.
    const char *keep[] = {{
        "戦闘EF.acx",
        "戦闘EFT.acx",
        "鷹鶚EF.acx",
        "ランス・クエストCG.afa",
        "鬼畜王ランスGA.ald",
        "スタッフ.alm",
        "日本語データ.afa",
    }};
    for (size_t i = 0; i < sizeof(keep) / sizeof(keep[0]); i++) {{
        char *rep = cjk_repair_sjis_as_gbk_utf8(keep[i]);
        assert(rep == NULL);
    }}

    // 2. Real GBK-mojibake Shift-JIS filenames MUST repair accurately and idempotently.
    struct {{ const char *in; const char *want; }} fix[] = {{
        {{ "愴崙儔儞僗GA.ald", "戦国ランスGA.ald" }},
        {{ "僗僞僢僼.alm", "スタッフ.alm" }},
        {{ "儔儞僗丒僋僄僗僩", "ランス・クエスト" }},
        {{ "儔儞僗丒僋僄僗僩CG.afa", "ランス・クエストCG.afa" }},
        {{ "儔儞僗丒僋僄僗僩AFF.afa", "ランス・クエストAFF.afa" }},
    }};
    for (size_t i = 0; i < sizeof(fix) / sizeof(fix[0]); i++) {{
        char *rep = cjk_repair_sjis_as_gbk_utf8(fix[i].in);
        assert(rep && strcmp(rep, fix[i].want) == 0);
        char *rep2 = cjk_repair_sjis_as_gbk_utf8(rep);
        assert(rep2 == NULL);
        free(rep);
    }}

    // 3. dir_find_mojibake_icase matches raw<->repaired in both directions and valid JP directly.
    char *m1 = dir_find_mojibake_icase(dir, "ランス・クエストCG.afa");
    assert(m1 && strcmp(path_basename(m1), "儔儞僗丒僋僄僗僩CG.afa") == 0);
    free(m1);

    char *m2 = dir_find_mojibake_icase(dir, "儔儞僗丒僋僄僗僩CG.afa");
    assert(m2 && strcmp(path_basename(m2), "儔儞僗丒僋僄僗僩CG.afa") == 0);
    free(m2);

    char *m3 = dir_find_mojibake_icase(dir, "戦闘EF.acx");
    assert(m3 && strcmp(path_basename(m3), "戦闘EF.acx") == 0);
    free(m3);

    char *m4 = dir_find_mojibake_icase(dir, "鷹鶚EF.acx");
    assert(m4 && strcmp(path_basename(m4), "鷹鶚EF.acx") == 0);
    free(m4);

    puts("OK");
    return 0;
}}
"""
        with tempfile.TemporaryDirectory() as tmpdir:
            td = Path(tmpdir)
            (td / "儔儞僗丒僋僄僗僩CG.afa").write_bytes(b"AFA")
            (td / "戦闘EF.acx").write_bytes(b"ACX")
            (td / "鷹鶚EF.acx").write_bytes(b"ACX")
            out = compile_and_run(harness, LIBSYS4_CORE, ["-lz", "-lm"], [str(td)])
            self.assertEqual(out.strip(), "OK")

    def test_asset_manager_mojibake_base_and_ts0_precedence(self):
        fn_ts = extract_function("src/asset_manager.c", "afa_entry_timestamp")
        fn_beats = extract_function("src/asset_manager.c", "afa_candidate_beats")
        harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "system4/utfsjis.h"

#define AFA_PRIO_MOD 2
struct afa_entry {{
    uint32_t unknown0;
    uint32_t unknown1;
}};

{fn_ts}
{fn_beats}

static bool match_archive_base(const char *base, const char *d_name, const char **suffix_out) {{
    char *base_repaired = cjk_repair_sjis_as_gbk_utf8(base);
    size_t base_len = strlen(base);
    size_t base_repaired_len = base_repaired ? strlen(base_repaired) : 0;
    char *repaired = cjk_repair_sjis_as_gbk_utf8(d_name);
    const char *match_name = repaired ? repaired : d_name;

    const char *matched_name = NULL;
    size_t matched_prefix_len = 0;
    if (base_repaired && !strncasecmp(base_repaired, match_name, base_repaired_len)) {{
        matched_name = match_name;
        matched_prefix_len = base_repaired_len;
    }} else if (!strncasecmp(base, match_name, base_len)) {{
        matched_name = match_name;
        matched_prefix_len = base_len;
    }} else if (!strncasecmp(base, d_name, base_len)) {{
        matched_name = d_name;
        matched_prefix_len = base_len;
    }}
    bool ok = false;
    if (matched_name) {{
        *suffix_out = matched_name + matched_prefix_len;
        ok = (strcasecmp(*suffix_out, "CG.afa") == 0 || strcasecmp(*suffix_out, "AFF.afa") == 0);
    }}
    free(repaired);
    free(base_repaired);
    return ok;
}}

int main(void) {{
    // 1. Rance 08 mojibake base ("儔儞僗丒僋僄僗僩") matches both mojibake and clean UTF-8 .afa files.
    const char *suf = NULL;
    assert(match_archive_base("儔儞僗丒僋僄僗僩", "儔儞僗丒僋僄僗僩CG.afa", &suf));
    assert(match_archive_base("儔儞僗丒僋僄僗僩", "儔儞僗丒僋僄僗僩AFF.afa", &suf));
    assert(match_archive_base("儔儞僗丒僋僄僗僩", "ランス・クエストCG.afa", &suf));
    assert(match_archive_base("ランス・クエスト", "儔儞僗丒僋僄僗僩CG.afa", &suf));
    assert(!match_archive_base("儔儞僗丒僋僄僗僩", "OtherCG.afa", &suf));

    // 2. ts == 0 always beats any non-zero timestamp, while equal ts == 0 keeps earlier winner.
    struct afa_entry e0 = {{ 0, 0 }}, e100 = {{ 100, 0 }}, e200 = {{ 200, 0 }};
    assert(afa_candidate_beats(0, &e100, 0, &e0));
    assert(!afa_candidate_beats(0, &e0, 0, &e100));
    assert(!afa_candidate_beats(0, &e0, 0, &e0));
    assert(afa_candidate_beats(0, &e100, 0, &e200));
    assert(!afa_candidate_beats(0, &e200, 0, &e100));

    puts("OK");
    return 0;
}}
"""
        out = compile_and_run(harness, LIBSYS4_CORE, ["-lz", "-lm"])
        self.assertEqual(out.strip(), "OK")

        # Verify unified ts==0 delegation and ALD overwrite free in asset_manager.c.
        am_src = (ROOT / "src/asset_manager.c").read_text(encoding="utf-8")
        self.assertIn("afa_candidate_beats(best_prio, best_entry, prio, e)", am_src)
        self.assertIn("asset_manager_free_slot(type);", am_src)
        self.assertIn("base_repaired = cjk_repair_sjis_as_gbk_utf8(base);", am_src)

    def test_evenicle2_reverse_erase_loop_patch_and_haniwapetit_detection(self):
        real_ains = [
            p for p in (
                Path("/tmp/alicehub_audit_scripts/evenicle2/Evenicle2.ain"),
                Path("/tmp/alicehub_audit_scripts/evenicle2rance/Evenicle2Rance.ain"),
            )
            if p.is_file()
        ]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/hashtable.h"
#include "system4/instructions.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "xsystem4.h"

struct config config;
void message_layout_init_rance03(void) {}
void apply_game_specific_hacks(struct ain *ain);

static void emit_op0(struct buffer *b, uint16_t op) {
    buffer_write_int16(b, op);
}
static void emit_op1(struct buffer *b, uint16_t op, int32_t a0) {
    buffer_write_int16(b, op);
    buffer_write_int32(b, a0);
}
static void emit_op2(struct buffer *b, uint16_t op, int32_t a0, int32_t a1) {
    buffer_write_int16(b, op);
    buffer_write_int32(b, a0);
    buffer_write_int32(b, a1);
}

static void emit_op3(struct buffer *b, uint16_t op, int32_t a0, int32_t a1, int32_t a2) {
    buffer_write_int16(b, op);
    buffer_write_int32(b, a0);
    buffer_write_int32(b, a1);
    buffer_write_int32(b, a2);
}

int main(int argc, char **argv) {
    // 1. Synthetic AIN v14: FUNC marker terminates function body (no ENDFUNC in v14).
    struct ain *a = ain_new(14, 0);
    a->libraries = xcalloc(1, sizeof(*a->libraries));
    a->nr_libraries = 1;
    a->libraries[0].name = xstrdup("Array");
    a->libraries[0].nr_functions = 2;
    a->libraries[0].functions = xcalloc(2, sizeof(*a->libraries[0].functions));
    a->libraries[0].functions[0].name = xstrdup("Numof");
    a->libraries[0].functions[1].name = xstrdup("Erase");
    ain_add_struct(a, "CReservePolyMotion");
    int fno = ain_add_function(a, "CReservePolyMotion@EraseUnnecessaryInList");
    int fno2 = ain_add_function(a, "NextFunc");
    a->functions[fno].vars = xcalloc(2, sizeof(struct ain_variable));
    a->functions[fno].nr_vars = 2;
    a->functions[fno].vars[0].name = xstrdup("i");
    a->functions[fno].vars[1].name = xstrdup("list");

    struct buffer b;
    buffer_init(&b, NULL, 0);
    emit_op1(&b, FUNC, fno);
    a->functions[fno].address = b.index;
    // Init (52 bytes): PUSHLOCALPAGE; PUSH 0; PUSHLOCALPAGE; PUSH 1; X_REF 1;
    //                  CALLHLL Array Numof 0; PUSH 1; SUB; X_ASSIGN 1; POP
    emit_op0(&b, PUSHLOCALPAGE);
    emit_op1(&b, PUSH, 0);
    emit_op0(&b, PUSHLOCALPAGE);
    emit_op1(&b, PUSH, 1);
    emit_op1(&b, X_REF, 1);
    emit_op3(&b, CALLHLL, 0, 0, 0);
    emit_op1(&b, PUSH, 1);
    emit_op0(&b, SUB);
    emit_op1(&b, X_ASSIGN, 1);
    emit_op0(&b, POP);
    // Cond (28 bytes): PUSHLOCALPAGE; PUSH 0; X_REF 1; PUSH 0; GTE; IFZ exit_addr
    uint32_t cond_addr = (uint32_t)b.index;
    emit_op0(&b, PUSHLOCALPAGE);
    emit_op1(&b, PUSH, 0);
    emit_op1(&b, X_REF, 1);
    emit_op1(&b, PUSH, 0);
    emit_op0(&b, GTE);
    uint32_t ifz_arg = (uint32_t)b.index + 2;
    emit_op1(&b, IFZ, 0);
    // Body: CALLHLL Array Erase 0; POP
    emit_op3(&b, CALLHLL, 0, 1, 0);
    emit_op0(&b, POP);
    // Step (26 bytes): PUSHLOCALPAGE; PUSH 0; X_DUP 2; INC; POP; POP; JUMP cond_addr
    emit_op0(&b, PUSHLOCALPAGE);
    emit_op1(&b, PUSH, 0);
    emit_op1(&b, X_DUP, 2);
    size_t inc_pos = b.index;
    emit_op0(&b, INC);
    emit_op0(&b, POP);
    emit_op0(&b, POP);
    emit_op1(&b, JUMP, (int32_t)cond_addr);
    uint32_t exit_addr = (uint32_t)b.index;
    emit_op0(&b, RETURN);
    // Trailing FUNC with NO ENDFUNC (AIN v14 layout!)
    emit_op1(&b, FUNC, fno2);
    a->functions[fno2].address = b.index;
    emit_op0(&b, RETURN);
    LittleEndian_putDW(b.buf, ifz_arg, exit_addr);
    a->code = b.buf;
    a->code_size = b.index;

    // Trigger game_evenicle2 via AIN v14 + C_MedicaMenu@0.
    ain_add_function(a, "C_MedicaMenu@0");
    ain_index_functions(a);
    ain_index_structures(a);

    // Ensure our INC is patched to DEC when terminating on FUNC!
    memset(&config, 0, sizeof(config));
    config.view_width = 1280; config.view_height = 720;
    apply_game_specific_hacks(a);
    assert(game_evenicle2);
    assert(LittleEndian_getW(a->code, inc_pos) == DEC);
    ain_free(a);

    // 2. Real Evenicle2 & Evenicle2Rance AIN files (if present in audit corpus).
    for (int i = 1; i < argc; i++) {
        int err = 0;
        struct ain *real = ain_open(argv[i], &err);
        assert(real && err == AIN_SUCCESS);
        int fn = ain_get_function(real, "CReservePolyMotion@EraseUnnecessaryInList");
        assert(fn >= 0);
        // Count INC vs DEC inside function before & after hacks.
        size_t start = real->functions[fn].address;
        size_t end = real->code_size;
        int inc_before = 0, dec_before = 0;
        for (size_t pc = start; pc < end;) {
            uint16_t op = LittleEndian_getW(real->code, pc);
            if (op == FUNC || op == ENDFUNC) break;
            if (op == INC) inc_before++;
            if (op == DEC) dec_before++;
            pc += instruction_width(op);
        }
        assert(inc_before >= 1);
        memset(&config, 0, sizeof(config));
        config.ain_filename = argv[i];
        apply_game_specific_hacks(real);
        assert(game_evenicle2 || game_evenicle2_rance);
        int inc_after = 0, dec_after = 0;
        for (size_t pc = start; pc < end;) {
            uint16_t op = LittleEndian_getW(real->code, pc);
            if (op == FUNC || op == ENDFUNC) break;
            if (op == INC) inc_after++;
            if (op == DEC) dec_after++;
            pc += instruction_width(op);
        }
        assert(dec_after == dec_before + 1 && inc_after == inc_before - 1);
        ain_free(real);
    }

    // 3. Haniwa Petit structural detection & GBK GameName markers for maman / rance1digest.
    struct ain *hp = ain_new(6, 0);
    hp->globals = xcalloc(3, sizeof(*hp->globals));
    hp->nr_globals = 3;
    hp->globals[0].name = xstrdup("g_cScenarioDebug");
    hp->globals[1].name = xstrdup("g_nMSG_Mode");
    hp->globals[2].name = xstrdup("g_nMSG_W");
    ain_index_functions(hp);
    ain_index_structures(hp);
    memset(&config, 0, sizeof(config));
    apply_game_specific_hacks(hp);
    assert(game_haniwapetit);
    ain_free(hp);

    // Early GBK markers for Maman & Rance 1 Digest
    struct ain *mm = ain_new(8, 0);
    ain_index_functions(mm); ain_index_structures(mm);
    memset(&config, 0, sizeof(config));
    config.game_name = "\xa4\xcf\xa1\xab\xa4\xc8\xa4\xd5\xa4\xeb\xa5\xde\xa5\xde\xa5\xf3";
    apply_game_specific_hacks(mm);
    assert(sys4_text_is_gbk);
    ain_free(mm);

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            [*LIBSYS4_CORE, ROOT / "src/hacks.c", ROOT / "src/escalayer_reboot_cn_ui.c"],
            ["-lz", "-lm"],
            [str(p) for p in real_ains],
        )
        lines = [l.strip() for l in out.strip().splitlines() if l.strip()]
        self.assertEqual(lines[-1], "OK")
        patched = [l for l in lines if "Patched reverse-erase loop INC->DEC in CReservePolyMotion@EraseUnnecessaryInList" in l]
        self.assertEqual(len(patched), 1 + len(real_ains))

    def test_cow_string_clear_ref_and_sact_int_to_zenkaku_hankaku(self):
        fn_zen = extract_function("src/hll/SACT2.c", "sact_IntToZenkaku")
        fn_han = extract_function("src/hll/SACT2.c", "sact_IntToHankaku")
        harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

{fn_zen}
{fn_han}

int main(void) {{
    // 1. string_clear on NULL and CoW string MUST NOT crash or mutate literal.
    string_clear(NULL);
    struct string *lit = make_string("LITERAL", 7);
    lit->cow = true;
    string_clear(lit);
    assert(lit->size == 7 && strcmp(lit->text, "LITERAL") == 0);

    // 2. string_clear_ref detaches CoW string cleanly without mutating the shared literal.
    struct string *alias = string_ref(lit);
    string_clear_ref(&alias);
    assert(alias != lit);
    assert(alias->size == 0 && strcmp(alias->text, "") == 0);
    assert(lit->size == 7 && strcmp(lit->text, "LITERAL") == 0);
    free_string(alias);

    // 3. string_clear_ref on *s == NULL allocates a fresh empty string.
    struct string *nil = NULL;
    string_clear_ref(&nil);
    assert(nil != NULL && nil->size == 0);
    free_string(nil);

    // 4. sact_IntToZenkaku and sact_IntToHankaku on CoW literal and NULL.
    struct string *out = string_ref(lit);
    sact_IntToHankaku(&out, 42, 4, 1);
    assert(out != lit && strcmp(out->text, "0042") == 0);
    assert(lit->size == 7 && strcmp(lit->text, "LITERAL") == 0);
    free_string(out);

    out = NULL;
    sact_IntToZenkaku(&out, 7, 2, 1);
    assert(out != NULL && out->size > 0);
    free_string(out);

    sact_IntToHankaku(NULL, 1, 1, 0);
    sact_IntToZenkaku(NULL, 1, 1, 0);
    free_string(lit);

    puts("OK");
    return 0;
}}
"""
        out = compile_and_run(harness, LIBSYS4_CORE, ["-lz", "-lm"])
        self.assertEqual(out.strip(), "OK")

    def test_pass_register_bounds_reuse_and_atomic_save(self):
        pr_src = (ROOT / "src/hll/PassRegister.c").read_text(encoding="utf-8")
        self.assertIn("atomic_save_open(", pr_src)
        self.assertIn("atomic_save_commit(", pr_src)
        self.assertIn("atomic_save_abort(", pr_src)
        self.assertIn("free_register_entries(reg);", pr_src)
        self.assertIn("data_len >= 4", pr_src)
        self.assertIn("data_len < PSR_HEADER_SIZE", pr_src)

        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/file.h"
#include "system4/string.h"
#include "savedata.h"
#include "xsystem4.h"

struct config config;
const char *display_utf0(const char *s) { return s ? s : ""; }
bool save_symbols_equal(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
void _vm_error(const char *fmt, ...) { (void)fmt; abort(); }
char *savedir_path(const char *path) { return path_join(config.save_dir, path); }
FILE *atomic_save_open(const char *path, const char *mode, char **tmp_out) {
    char *tmp = xmalloc(strlen(path) + 8);
    sprintf(tmp, "%s.tmp", path);
    FILE *fp = file_open_utf8(tmp, mode);
    if (!fp) { free(tmp); *tmp_out = NULL; return NULL; }
    *tmp_out = tmp;
    return fp;
}
bool atomic_save_commit(FILE *fp, const char *path, char *tmp, bool keep_backup) {
    (void)keep_backup;
    bool ok = fclose(fp) == 0 && rename(tmp, path) == 0;
    if (!ok) remove(tmp);
    free(tmp);
    return ok;
}
void atomic_save_abort(FILE *fp, char *tmp) {
    if (fp) fclose(fp);
    if (tmp) { remove(tmp); free(tmp); }
}

// Include PassRegister.c directly to test static logic!
#include "hll/PassRegister.c"

int main(int argc, char **argv) {
    assert(argc == 2);
    config.save_dir = argv[1];

    // 1. Truncated files (<4 bytes and <16 bytes) must fail cleanly without OOB reads.
    char *p_tiny = path_join(argv[1], "tiny.psr");
    FILE *f = fopen(p_tiny, "wb"); fwrite("PS", 1, 2, f); fclose(f);
    char *p_short = path_join(argv[1], "short.psr");
    f = fopen(p_short, "wb"); fwrite("PSR\0short", 1, 9, f); fclose(f);

    struct string *s_tiny = make_string("tiny.psr", 8);
    struct string *s_short = make_string("short.psr", 9);
    int h = 1;
    assert(PassRegister_SetFileName(h, s_tiny));
    assert(registers[h].nr_integers == 0 && registers[h].nr_strings == 0);
    assert(PassRegister_SetFileName(h, s_short));
    assert(registers[h].nr_integers == 0 && registers[h].nr_strings == 0);

    // 2. Round-trip save, UnregistText, and handle reuse clearing previous entries!
    struct string *s_a = make_string("a.psr", 5);
    struct string *s_b = make_string("b.psr", 5);
    struct string *txt1 = make_string("alpha", 5);
    struct string *txt2 = make_string("beta", 4);
    assert(PassRegister_SetFileName(h, s_a));
    assert(PassRegister_RegistNumber(h, 100));
    assert(PassRegister_RegistText(h, txt1));
    assert(PassRegister_RegistText(h, txt2));
    assert(PassRegister_ExistNumber(h, 100));
    assert(PassRegister_ExistText(h, txt1));
    assert(PassRegister_UnregistText(h, txt1));
    assert(!PassRegister_ExistText(h, txt1));
    assert(PassRegister_ExistText(h, txt2));

    // Reuse same handle for a fresh file -> old entries must NOT leak into new handle state!
    assert(PassRegister_SetFileName(h, s_b));
    assert(!PassRegister_ExistNumber(h, 100));
    assert(!PassRegister_ExistText(h, txt2));
    assert(registers[h].nr_integers == 0 && registers[h].nr_strings == 0);

    // Reopen a.psr -> persisted state reloads cleanly.
    assert(PassRegister_SetFileName(h, s_a));
    assert(PassRegister_ExistNumber(h, 100));
    assert(!PassRegister_ExistText(h, txt1));
    assert(PassRegister_ExistText(h, txt2));
    assert(PassRegister_UnregistAll(h));
    assert(registers[h].nr_integers == 0 && registers[h].nr_strings == 0);

    PassRegister_fini();
    free_string(s_tiny); free_string(s_short); free_string(s_a); free_string(s_b);
    free_string(txt1); free_string(txt2);
    free(p_tiny); free(p_short);
    puts("OK");
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmpdir:
            out = compile_and_run(harness, [*LIBSYS4_CORE, ROOT / "src/cJSON.c"], ["-lz", "-lm"], [tmpdir])
            self.assertEqual(out.strip(), "OK")

    def test_savedata_option_skip_flush_hook_gdss_cap_and_comment_sidecar(self):
        fn_bg = extract_function("src/savedata.c", "is_background_flush_save")
        fn_base = extract_function("src/savedata.c", "save_basename")
        harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

{fn_base}
{fn_bg}

int main(void) {{
    assert(is_background_flush_save("GlobalSave.asd"));
    assert(is_background_flush_save("SaveData/GlobalSaveData.asd"));
    assert(is_background_flush_save("SaveData\\\\GlobalSaveData.asd"));
    assert(!is_background_flush_save("SaveData/Save001.asd"));
    puts("OK");
    return 0;
}}
"""
        out = compile_and_run(harness, [], [])
        self.assertEqual(out.strip(), "OK")

        # Verify source-level invariants across savedata.c, MsgSkip.c, resume.c, page.c, vm.c, parts/*.
        sd_src = (ROOT / "src/savedata.c").read_text(encoding="utf-8")
        self.assertIn("x4ss_skip_tagged_value_depth(r, tag, depth);", sd_src)
        self.assertIn("if (!primary_valid && file_exists(bak_path))", sd_src)
        self.assertIn("savedata_set_background_flush_hook(", sd_src)
        self.assertIn("uint32_t max_roots = (sec_off[3] - sec_off[2]) / 17u;", sd_src)
        self.assertIn("char *comment_path = make_comment_sidecar_path(path);", sd_src)
        self.assertIn("save_decode_vm_string(c->text, c->size, src_is_gbk)", sd_src)

        ms_src = (ROOT / "src/hll/MsgSkip.c").read_text(encoding="utf-8")
        self.assertIn("savedata_set_background_flush_hook(msgskip_save);", ms_src)
        self.assertIn("savedata_set_background_flush_hook(NULL);", ms_src)
        self.assertNotIn("msgskip_save", sd_src)

        rs_src = (ROOT / "src/resume.c").read_text(encoding="utf-8")
        self.assertIn("if (!fname || !fname[0])", rs_src)
        self.assertIn("int slot = array->values[i].i;\n\t\tfree_string(heap[slot].s);", rs_src)

        pg_src = (ROOT / "src/page.c").read_text(encoding="utf-8")
        self.assertIn("if (page->nr_vars <= 0)\n\t\treturn 0;", pg_src)
        self.assertIn("array_compare_member_float", pg_src)

        vm_src = (ROOT / "src/vm.c").read_text(encoding="utf-8")
        for marker in (
            "A_ALLOC: invalid array page index",
            "A_REALLOC: invalid array page index",
            "A_PUSHBACK: invalid array page index",
            "A_INSERT: invalid array page index",
        ):
            self.assertIn(marker, vm_src)

        parts_src = (ROOT / "src/parts/parts.c").read_text(encoding="utf-8")
        self.assertIn("return ht_get_int(parts_table, parts_no, NULL);", parts_src)
        self.assertIn("if (!parts_current->background)", parts_src)
        self.assertIn("if (!parts_v2 || child == no)", parts_src)
        flat_src = (ROOT / "src/parts/flat.c").read_text(encoding="utf-8")
        self.assertIn("if (cur) {\n\t\t\tif (birth_frame > 0)", flat_src)
        self.assertIn("parts_dirty(parts);", flat_src)

    def test_ald_bounds_and_malformed_header_size(self):
        # Build an ALD with:
        # - entry 0: disk=1, ptr=999 (out of bounds of fileptr table!)
        # - entry 1: disk=1, ptr=0 pointing to a record with hdr_size=8 (< 16!)
        # - entry 2: disk=1, ptr=1 pointing to a valid record with hdr_size=16 (empty name)
        def pad256(b: bytes) -> bytes:
            rem = len(b) % 256
            return b if rem == 0 else b + b"\x00" * (256 - rem)

        def u24(n: int) -> bytes:
            return bytes((n & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF))

        bad_rec = pad256(struct.pack("<IIII", 8, 4, 0, 0) + b"XXXX")
        empty_name_rec = pad256(struct.pack("<IIII", 16, 5, 0, 0) + b"HELLO")

        ptr_sectors = 1
        map_sectors = 1
        first_sec = ptr_sectors + map_sectors
        sec0 = first_sec
        sec1 = sec0 + len(bad_rec) // 256
        sec2 = sec1 + len(empty_name_rec) // 256

        ptr_tbl = pad256(u24(ptr_sectors) + u24(sec0) + u24(sec1) + u24(sec2))
        map_tbl = pad256(
            struct.pack("<BH", 1, 1000)  # no=0: ptr=999 >= filecnt!
            + struct.pack("<BH", 1, 1)   # no=1: ptr=0 -> hdr_size=8 (<16)
            + struct.pack("<BH", 1, 2)   # no=2: ptr=1 -> hdr_size=16 (valid empty name)
        )
        ald_bytes = ptr_tbl + map_tbl + bad_rec + empty_name_rec

        harness = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "system4/ald.h"
#include "system4/archive.h"

int main(int argc, char **argv) {
    assert(argc == 2);
    char *files[1] = { argv[1] };
    for (int mode = 0; mode < 2; mode++) {
        int err = 0;
        int flags = mode == 0 ? 0 : ARCHIVE_MMAP;
        struct archive *ar = ald_open(files, 1, flags, &err);
        assert(ar && err == 0);
        // Entry 0: OOB ptr -> cleanly missing (no OOB heap read!)
        assert(!archive_exists(ar, 0));
        assert(archive_get(ar, 0) == NULL);
        // Entry 1: hdr_size < 16 -> rejected cleanly (no underflow xcalloc!)
        assert(archive_get(ar, 1) == NULL);
        // Entry 2: hdr_size == 16 -> valid empty name and payload "HELLO"
        struct archive_data *d = archive_get(ar, 2);
        assert(d != NULL);
        assert(d->name != NULL && strcmp(d->name, "") == 0);
        assert(d->size == 5 && memcmp(d->data, "HELLO", 5) == 0);
        archive_free_data(d);
        archive_free(ar);
    }
    puts("OK");
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmpdir:
            p = Path(tmpdir) / "testGA.ald"
            p.write_bytes(ald_bytes)
            out = compile_and_run(harness, LIBSYS4_EXTRA, ["-lz", "-lm"], [str(p)])
            self.assertEqual(out.strip(), "OK")

    def test_modifier_extra_waidonyo_dorapeko_tsumamigui3_maman(self):
        extra = "src/modifier_extra.inc"
        defines = "\n".join([
            _extract_defines(extra, "DP_F_"),
            _extract_defines(extra, "EX_KEY_"),
        ])
        funcs = "\n\n".join(extract_function(extra, fn) for fn in (
            "ex_arr_int_get",
            "ex_arr_int_set",
            "ex_json_int",
            "ex_json_cheat_is",
            "ex_wd_populate",
            "ex_wd_locks",
            "ex_wd_action",
            "ex_dp_char_active",
            "ex_dp_locks",
            "ex_dp_action",
            "ex_t3g_locks",
            "ex_t3g_action",
            "ex_mm_locks",
            "ex_mm_action",
        ))
        harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

{defines}

enum page_type {{ STRUCT_PAGE = 1, ARRAY_PAGE = 2 }};
union vm_value {{ int32_t i; }};
struct page {{
    enum page_type type;
    int nr_vars;
    union vm_value values[16];
    const char *name;
    int32_t join, lv, atk, act, love;
    int32_t money, energy, wild, wlim, condom, mcon, humor;
    int32_t enemy_semen, numof;
    struct page *hp_arr;
    struct page *exp_arr;
}};

static int32_t vm_dd_clamp(int32_t n, int32_t lo, int32_t hi) {{ return n < lo ? lo : n > hi ? hi : n; }}

static struct page wd_sd = {{ .type = ARRAY_PAGE, .nr_vars = 8 }};
static struct page wd_cm = {{ .type = STRUCT_PAGE }}, wd_arr = {{ .type = ARRAY_PAGE, .nr_vars = 0 }};

static struct page dp_cd = {{ .type = ARRAY_PAGE, .nr_vars = 2 }};
static struct page dp_c[2], dp_hp[2], dp_exp[2];

static struct page gp_t3m = {{ .type = STRUCT_PAGE }};
static struct page t3_hp = {{ .type = STRUCT_PAGE }}, t3_harr = {{ .type = ARRAY_PAGE, .nr_vars = 1 }}, t3_h0 = {{ .type = STRUCT_PAGE }};
static struct page t3_ip = {{ .type = STRUCT_PAGE }}, t3_iarr = {{ .type = ARRAY_PAGE, .nr_vars = 0 }};
static struct page mm_fp = {{ .type = STRUCT_PAGE }}, mm_farr = {{ .type = ARRAY_PAGE, .nr_vars = 0 }};

static struct page *vm_get_global_page(const char *n1, const char *n2) {{
    (void)n2;
    if (!strcmp(n1, "g_anSaveData")) return &wd_sd;
    if (!strcmp(n1, "g_sCharaData")) return &wd_cm;
    if (!strcmp(n1, "CD")) return &dp_cd;
    if (!strcmp(n1, "g_cGameParam")) return &gp_t3m;
    if (!strcmp(n1, "g_cHeroineParam")) return &t3_hp;
    if (!strcmp(n1, "g_cItemParam")) return &t3_ip;
    if (!strcmp(n1, "g_cFondnessParam")) return &mm_fp;
    return NULL;
}}

static struct page *vm_get_field_page(struct page *p, const char *n1, const char *n2) {{
    (void)n2;
    if (p == &wd_cm && !strcmp(n1, "m_asData")) return &wd_arr;
    if ((p == &dp_c[0] || p == &dp_c[1]) && !strcmp(n1, DP_F_HP)) return p->hp_arr;
    if ((p == &dp_c[0] || p == &dp_c[1]) && !strcmp(n1, DP_F_EXP)) return p->exp_arr;
    if (p == &t3_hp && !strcmp(n1, "m_acBase")) return &t3_harr;
    if (p == &t3_ip && !strcmp(n1, "m_acBase")) return &t3_iarr;
    if (p == &mm_fp && !strcmp(n1, "m_anValue")) return &mm_farr;
    return NULL;
}}

static int vm_dd_array_len(struct page *p) {{ return p ? p->nr_vars : 0; }}
static int vm_dd_array_elem_slot(struct page *p, int i) {{
    if (p == &dp_cd) return 10 + i;
    if (p == &t3_harr) return 20 + i;
    return -1;
}}
static struct page *vm_safe_get_page(int slot) {{
    if (slot == 10) return &dp_c[0];
    if (slot == 11) return &dp_c[1];
    if (slot == 20) return &t3_h0;
    return NULL;
}}
static char *vm_dd_get_field_utf8(struct page *p, const char *n1, const char *n2) {{
    (void)n1; (void)n2;
    return strdup((p && p->name) ? p->name : "");
}}
static int32_t vm_get_field_int(struct page *p, const char *n1, const char *n2, int32_t def) {{
    (void)n2;
    if (!p) return def;
    if (!strcmp(n1, DP_F_JOIN)) return p->join;
    if (!strcmp(n1, DP_F_LV)) return p->lv;
    if (!strcmp(n1, DP_F_ATK)) return p->atk;
    if (!strcmp(n1, DP_F_ACT)) return p->act;
    if (!strcmp(n1, DP_F_LOVE)) return p->love;
    return def;
}}
static void vm_set_field_int(struct page *p, const char *n1, const char *n2, int32_t v) {{
    (void)n2;
    if (!p) return;
    if (!strcmp(n1, DP_F_LV)) p->lv = v;
    else if (!strcmp(n1, DP_F_ATK)) p->atk = v;
    else if (!strcmp(n1, DP_F_ACT)) p->act = v;
    else if (!strcmp(n1, DP_F_LOVE)) p->love = v;
}}
static int32_t ex_cint_read(struct page *p, int32_t def) {{ (void)p; return def; }}
static void ex_cint_write_xor(struct page *p, int32_t v, int32_t k, int32_t e) {{ (void)p; (void)v; (void)k; (void)e; }}
static int32_t *cint_field_ptr(struct page *p, const char *n) {{
    if (!p) return NULL;
    if (!strcmp(n, "m_nMoney")) return &p->money;
    if (!strcmp(n, "m_nEnergy")) return &p->energy;
    if (!strcmp(n, "m_nLevel")) return &p->lv;
    if (!strcmp(n, "m_nRelation")) return &p->love;
    if (!strcmp(n, "m_nSeed")) return &p->atk;
    if (!strcmp(n, "m_nEnemySemen")) return &p->enemy_semen;
    if (!strcmp(n, "m_nNumof")) return &p->numof;
    if (!strcmp(n, "m_nCondom")) return &p->condom;
    if (!strcmp(n, "m_nWild")) return &p->wild;
    if (!strcmp(n, "m_nWildLimit")) return &p->wlim;
    if (!strcmp(n, "m_nMotherComplex")) return &p->mcon;
    if (!strcmp(n, "m_nHumor")) return &p->humor;
    return NULL;
}}
static int32_t ex_field_cint_read(struct page *p, const char *n, int32_t def) {{
    int32_t *f = cint_field_ptr(p, n);
    return f ? *f : def;
}}
static void ex_field_cint_write(struct page *p, const char *n, int32_t v, int32_t key) {{
    (void)key;
    int32_t *f = cint_field_ptr(p, n);
    if (f) *f = v;
}}

{funcs}

static void send(void (*fn)(cJSON *), const char *json) {{
    cJSON *a = cJSON_Parse(json);
    assert(a != NULL);
    fn(a);
    cJSON_Delete(a);
}}

int main(void) {{
    // 1. Waidonyo gold reads/writes g_anSaveData[3] and supports addGold.
    wd_sd.values[3].i = 1234;
    cJSON *root = cJSON_CreateObject();
    ex_wd_populate(root);
    assert(cJSON_GetObjectItem(root, "gold")->valueint == 1234);
    cJSON_Delete(root);
    send(ex_wd_action, "{{\\"gold\\":50000}}");
    assert(wd_sd.values[3].i == 50000);
    send(ex_wd_action, "{{\\"addGold\\":2500}}");
    assert(wd_sd.values[3].i == 52500);
    ex_wd_locks(1 << 1);
    assert(wd_sd.values[3].i == 999999);

    // 2. Dorapeko heals/boosts active characters even when c加入 == 0 (protagonist CD[9] Est)!
    dp_hp[0] = (struct page){{ .type = ARRAY_PAGE, .nr_vars = 2, .values = {{{{.i = 10}}, {{.i = 250}}}} }};
    dp_hp[1] = (struct page){{ .type = ARRAY_PAGE, .nr_vars = 2, .values = {{{{.i = 0}}, {{.i = 0}}}} }};
    dp_exp[0] = (struct page){{ .type = ARRAY_PAGE, .nr_vars = 2 }};
    dp_exp[1] = (struct page){{ .type = ARRAY_PAGE, .nr_vars = 2 }};
    dp_c[0] = (struct page){{ .name = "Est", .join = 0, .love = 10, .hp_arr = &dp_hp[0], .exp_arr = &dp_exp[0] }};
    dp_c[1] = (struct page){{ .name = "",    .join = 0, .love = 10, .hp_arr = &dp_hp[1], .exp_arr = &dp_exp[1] }};
    ex_dp_locks((1 << 0) | (1 << 1) | (1 << 2));
    assert(dp_hp[0].values[0].i == 250 && dp_c[0].love == 100 && dp_c[0].act == 1);
    assert(dp_c[1].love == 10 && dp_c[1].act == 0);

    // 3. Tsumamigui 3 bytecode-safe clamps: energy 0..20, money 0..999999, lv 0..10.
    gp_t3m.money = 0; gp_t3m.energy = 0;
    ex_t3g_locks((1 << 0) | (1 << 1));
    assert(gp_t3m.money == 999999 && gp_t3m.energy == 20);
    send(ex_t3g_action, "{{\\"gold\\":5000000,\\"energy\\":999,\\"exCharEdit\\":{{\\"target\\":0,\\"lv\\":99}}}}");
    assert(gp_t3m.money == 999999 && gp_t3m.energy == 20 && t3_h0.lv == 10);

    // 4. Maman bytecode-safe clamps: condom 0..5, humor 0..5, wild 0..100, mcon 0..100.
    gp_t3m.condom = 0; gp_t3m.humor = 0; gp_t3m.wild = 0; gp_t3m.wlim = 80;
    ex_mm_locks((1 << 0) | (1 << 1));
    assert(gp_t3m.condom == 5 && gp_t3m.humor == 5 && gp_t3m.wild == 80);
    send(ex_mm_action, "{{\\"condom\\":99,\\"humor\\":100,\\"wild\\":500,\\"mcon\\":500}}");
    assert(gp_t3m.condom == 5 && gp_t3m.humor == 5 && gp_t3m.wild == 100 && gp_t3m.mcon == 100);

    puts("OK");
    return 0;
}}
"""
        out = compile_and_run(harness, [ROOT / "src/cJSON.c"], ["-lm"])
        self.assertEqual(out.strip(), "OK")

    def test_hll_fixes_source_and_behavior(self):
        # Test Dialog.InputMsg / InputNum, PlayDemo, Sound2ex.PreloadSound, vmCG.Release, File.Copy, Toushin3Loader.
        self.assertIn("HLL_LIBRARY(Dialog,", (ROOT / "src/hll/vmDialog.c").read_text(encoding="utf-8"))
        self.assertIn("&lib_Dialog", (ROOT / "src/ffi.c").read_text(encoding="utf-8"))
        self.assertIn("HLL_EXPORT(Copy, File_Copy)", (ROOT / "src/hll/File.c").read_text(encoding="utf-8"))
        self.assertIn("if (!fp)\n\t\treturn NULL;", (ROOT / "src/hll/Toushin3Loader.c").read_text(encoding="utf-8"))
        self.assertIn("HLL_EXPORT(Release, vmCG_Release)", (ROOT / "src/hll/vmCG.c").read_text(encoding="utf-8"))
        self.assertIn("static int Sound2ex_PreloadSound(", (ROOT / "src/hll/Sound2ex.c").read_text(encoding="utf-8"))
        self.assertIn("HLL_EXPORT(Play, PlayDemo_Play)", (ROOT / "src/hll/PlayDemo.c").read_text(encoding="utf-8"))
        self.assertIn("HLL_EXPORT(IsExistIf, ADVEngine_IsExistIf)", (ROOT / "src/hll/FileOperation.c").read_text(encoding="utf-8"))
        self.assertIn("HLL_EXPORT(Create, EXWriter_Create)", (ROOT / "src/hll/FileOperation.c").read_text(encoding="utf-8"))
        self.assertIn("GameVariable_Get_v8", (ROOT / "src/hll/SystemService.c").read_text(encoding="utf-8"))
        self.assertIn("HLL_EXPORT(Parts_SetPartsFlat, PE_SetPartsFlat)", (ROOT / "src/hll/GUIEngine.c").read_text(encoding="utf-8"))
        self.assertIn("HLL_EXPORT(BlendAMapAlphaDeformedSprite, DrawGraph_BlendAMapAlphaDeformedSprite)", (ROOT / "src/hll/DrawGraph.c").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
