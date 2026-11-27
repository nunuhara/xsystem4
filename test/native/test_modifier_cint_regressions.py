#!/usr/bin/env python3
"""Host C regression tests for Mituki C_INT encryption and cross-game modifier fixes.

Verifies the real C implementations extracted from xsystem4/src/modifier_extra.inc
and xsystem4/src/modifier.c:
1. ex_cint_write_xor / ex_cbool_write / ex_ix_write_fac:
   - Shuffled pattern table (Ixseal, Tsumamigui 3, Maman): m_nPattern == 0x00070007
     routes both GetCodePattern() and GetBackUpCodePattern() to Decryption01
     (nIndex == 8), round-tripping positive values (including Ixseal faculty xor_extra),
     whereas old pattern 0 routes to Decryption03 (negative -> clamped to 0).
   - Unshuffled pattern table (Escalayer Reboot): m_nPattern == 0 routes to
     Decryption01 (nIndex == 1).
   - ValueEncryption HLL plaintext passthrough (Pascha 3 or any .ain importing
     "ValueEncryption"): writes plaintext val to values[0..2].
2. Escalayer Reboot m_bBoost lock guard (only writes when m_bBoost == 0) and
   [1, 10] customize clamp caps; Ixseal [1, 12] m_nMagicPlate and [1, 999]
   faculty clamp caps.
3. Rance 10 offline write_local_save_mod_str "allStatusPer" writes all 16
   percentage fields (with individual field overrides still winning).
4. Evenicle 2 GBK _CN fallback field names in ex_ev2_populate/locks/action.
5. Toushin 3 and Blade Briders live battle-copy sync in ex_t3_action/ex_bb_action.
6. fullHeal / VIT lock max > 0 guards across 8 extra games (Daibanchou,
   Daiteikoku, Blade Briders, Yoru ga Kuru, Waidonyo, Sanbiki, Okusama).
7. vm_ex_execute_action invoking vm_apply_ui_scale_from_json and persisting config.
8. Okusama m_possessList multi-entry gold updates and Pascha 3 empty-name unit skip.

Run: PYTHONPATH=xsystem4/test/native python3 -m unittest xsystem4/test/native/test_modifier_cint_regressions.py
"""
import re
import unittest

from test_evenicle2_regressions import ROOT, compile_and_run
from test_vm_reign_regressions import extract_function

EXTRA_INC = "src/modifier_extra.inc"
MODIFIER_C = "src/modifier.c"


def extract_defines(rel_path: str, prefix_regex: str) -> str:
    text = (ROOT / rel_path).read_text(encoding="utf-8")
    return "\n".join(re.findall(rf"^#define (?:{prefix_regex}).*", text, re.M))


class ModifierCIntAndCrossGameRegressions(unittest.TestCase):
    def test_mituki_cint_shuffled_unshuffled_and_plaintext_roundtrip(self):
        defines = extract_defines(EXTRA_INC, r"EX_KEY_")
        funcs = "\n\n".join(extract_function(EXTRA_INC, fn) for fn in (
            "ex_cint_read",
            "ex_cint_write_xor",
            "ex_field_cint_read",
            "ex_field_cint_write",
            "ex_cbool_write",
            "ex_ix_write_fac",
        ))
        compile_and_run(r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum page_type { STRUCT_PAGE = 1, ARRAY_PAGE = 2 };
union vm_value { int32_t i; float f; };
struct page {
    enum page_type type;
    int nr_vars;
    union vm_value values[8];
};

struct ain { bool has_value_encryption_hll; };
static struct ain g_ain_obj = { false };
static struct ain *ain = &g_ain_obj;

static int ain_get_library(struct ain *a, const char *name)
{
    if (a && a->has_value_encryption_hll && strcmp(name, "ValueEncryption") == 0)
        return 0;
    return -1;
}

static struct page *g_slots[16];
static struct page *vm_safe_get_page(int slot)
{
    return (slot >= 0 && slot < 16) ? g_slots[slot] : NULL;
}

static struct page g_fac_cint;
static int32_t g_fac_xk = 0;

static int32_t vm_get_field_int(struct page *p, const char *n1, const char *n2, int32_t def)
{
    (void)p; (void)n2;
    if (n1 && strcmp(n1, "m_nHitPointKey") == 0)
        return g_fac_xk;
    return def;
}

static struct page *vm_get_field_page(struct page *p, const char *n1, const char *n2)
{
    (void)p; (void)n2;
    if (n1 && strcmp(n1, "m_nHitPoint") == 0)
        return &g_fac_cint;
    return NULL;
}

""" + defines + "\n" + funcs + r"""

/* Exact Mituki C_VALUE_ENCRYPTION models from bytecode audit:
 * - Decryption01: plain XOR with GetXorValue(val, key_seed).
 * - Decryption03: byte shuffle + XOR 0xFF0000FF (produces huge negative int on
 *   Encrytion01 ciphertext -> C_INT::Get() clamps to 0 -> HP 0 / AP 0 bug!). */
static int32_t mituki_decryption01(int32_t code, int32_t plain_hint, uint32_t key_seed)
{
    uint32_t u = (uint32_t)plain_hint;
    uint32_t key = key_seed ^ ((u << 8) | u) ^ 0xAAAAAAAAu;
    return (int32_t)((uint32_t)code ^ key);
}

static int32_t mituki_decryption03(int32_t code)
{
    uint32_t c = (uint32_t)code;
    uint32_t shuffled =
        (c & 0xFF000000u) |
        (((c >>  8) & 0xFFu) << 16) |
        (((c >> 16) & 0xFFu) <<  8) |
        (c & 0x000000FFu);
    return (int32_t)(shuffled ^ 0xFF0000FFu);
}

static int32_t mituki_decrypt_shuffled(int32_t code, int32_t plain_hint, int32_t pat, uint32_t key_seed)
{
    int nIndex = pat + 1;
    if (nIndex == 8) /* nPattern == 7 -> Decryption01 */
        return mituki_decryption01(code, plain_hint, key_seed);
    if (nIndex == 1) /* nPattern == 0 -> Decryption03 (the bug!) */
        return mituki_decryption03(code);
    return -1;
}

static int32_t mituki_decrypt_unshuffled(int32_t code, int32_t plain_hint, int32_t pat, uint32_t key_seed)
{
    int nIndex = pat + 1;
    if (nIndex == 1) /* nPattern == 0 -> Decryption01 */
        return mituki_decryption01(code, plain_hint, key_seed);
    return -1;
}

static int32_t cint_read_checked(const struct page *cint, uint32_t key_seed, int32_t xor_extra, bool shuffled)
{
    int32_t plain = cint->values[0].i;
    int32_t code = (int32_t)((uint32_t)cint->values[1].i ^ (uint32_t)xor_extra);
    int32_t backup = cint->values[2].i;
    int32_t pat_code = cint->values[3].i & 0xFFFF;
    int32_t pat_bkup = (cint->values[3].i >> 16) & 0xFFFF;
    int32_t d1 = shuffled
        ? mituki_decrypt_shuffled(code, plain, pat_code, key_seed)
        : mituki_decrypt_unshuffled(code, plain, pat_code, key_seed);
    int32_t d2 = shuffled
        ? mituki_decrypt_shuffled(backup, plain, pat_bkup, key_seed)
        : mituki_decrypt_unshuffled(backup, plain, pat_bkup, key_seed);
    if (d1 != plain || d2 != plain || d1 < 0)
        return 0; /* game clamps tamper / negative decrypt to 0 */
    return d1;
}

int main(void)
{
    const int32_t vals[] = { 0, 1, 12, 100, 500, 999, 9999, 999999 };
    const uint32_t shuffled_keys[] = { EX_KEY_IXSEAL, EX_KEY_TSUMAMIGUI3, EX_KEY_MAMAN };

    /* 1. Shuffled table games (Ixseal, Tsumamigui 3, Maman): m_nPattern == 0x00070007. */
    g_ain_obj.has_value_encryption_hll = false;
    for (size_t k = 0; k < sizeof(shuffled_keys) / sizeof(shuffled_keys[0]); k++) {
        for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
            struct page cint = { .type = STRUCT_PAGE, .nr_vars = 4 };
            cint.values[3].i = 0x12345678;
            ex_cint_write_xor(&cint, vals[i], shuffled_keys[k], 0);
            assert(cint.values[0].i == vals[i]);
            assert(cint.values[3].i == 0x00070007);
            assert((cint.values[3].i & 0xFFFF) == 7);
            assert(((cint.values[3].i >> 16) & 0xFFFF) == 7);
            assert(cint_read_checked(&cint, shuffled_keys[k], 0, true) == vals[i]);

            /* Regression proof: if pattern were old 0, Decryption03 yields negative -> 0! */
            if (vals[i] > 0) {
                struct page buggy = cint;
                buggy.values[3].i = 0;
                int32_t bad = mituki_decrypt_shuffled(buggy.values[1].i, buggy.values[0].i, 0, shuffled_keys[k]);
                assert(bad < 0);
                assert(cint_read_checked(&buggy, shuffled_keys[k], 0, true) == 0);
            }
        }
    }

    /* 2. Ixseal faculty stats with non-zero xor_extra (m_nCode XORed, m_nBackUpCode not). */
    const int32_t xks[] = { 0x12345678, (int32_t)0x9ABCDEF0u, 0x0000A5A5 };
    for (size_t x = 0; x < sizeof(xks) / sizeof(xks[0]); x++) {
        g_fac_cint = (struct page){ .type = STRUCT_PAGE, .nr_vars = 4 };
        g_fac_xk = xks[x];
        struct page dummy_fac = { .type = STRUCT_PAGE, .nr_vars = 1 };
        ex_ix_write_fac(&dummy_fac, "m_nHitPoint", "m_nHitPointKey", 777);
        assert(g_fac_cint.values[0].i == 777);
        assert(g_fac_cint.values[3].i == 0x00070007);
        assert((g_fac_cint.values[1].i ^ g_fac_xk) == g_fac_cint.values[2].i);
        assert(cint_read_checked(&g_fac_cint, EX_KEY_IXSEAL, g_fac_xk, true) == 777);
    }

    /* 3. Unshuffled table game (Escalayer Reboot): m_nPattern == 0 -> Decryption01. */
    for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        struct page cint = { .type = STRUCT_PAGE, .nr_vars = 4 };
        cint.values[3].i = 0x00070007;
        ex_cint_write_xor(&cint, vals[i], EX_KEY_ESCALAYER_REBOOT, 0);
        assert(cint.values[0].i == vals[i]);
        assert(cint.values[3].i == 0);
        assert(cint_read_checked(&cint, EX_KEY_ESCALAYER_REBOOT, 0, false) == vals[i]);
    }

    /* 4. ex_cbool_write round-trips both Escalayer Reboot (pat 0) and Ixseal (pat 0x00070007). */
    struct page pkey = { .type = STRUCT_PAGE, .nr_vars = 4, .values = {{ .i = 41 }} };
    struct page pnb  = { .type = STRUCT_PAGE, .nr_vars = 4 };
    struct page cb   = { .type = STRUCT_PAGE, .nr_vars = 3, .values = {{ .i = 0 }, { .i = 1 }, { .i = 2 }} };
    g_slots[1] = &pnb;
    g_slots[2] = &pkey;
    ex_cbool_write(&cb, true, EX_KEY_ESCALAYER_REBOOT);
    assert(cb.values[0].i == 1);
    assert(pnb.values[0].i == 42 && pnb.values[3].i == 0);
    assert(cint_read_checked(&pnb, EX_KEY_ESCALAYER_REBOOT, 0, false) == 42);

    ex_cbool_write(&cb, true, EX_KEY_IXSEAL);
    assert(cb.values[0].i == 1);
    assert(pnb.values[0].i == 42 && pnb.values[3].i == 0x00070007);
    assert(cint_read_checked(&pnb, EX_KEY_IXSEAL, 0, true) == 42);

    /* 5. Plaintext passthrough via EX_KEY_PASCHA3 (key_seed == 0). */
    struct page p3 = { .type = STRUCT_PAGE, .nr_vars = 4 };
    p3.values[3].i = 99;
    ex_cint_write_xor(&p3, 12345, EX_KEY_PASCHA3, 0);
    assert(p3.values[0].i == 12345 && p3.values[1].i == 12345 && p3.values[2].i == 12345);
    assert(p3.values[3].i == 99);

    /* 6. Defensive ValueEncryption HLL detection overrides non-zero key_seed. */
    g_ain_obj.has_value_encryption_hll = true;
    struct page hll = { .type = STRUCT_PAGE, .nr_vars = 4 };
    hll.values[3].i = 55;
    ex_cint_write_xor(&hll, 888, EX_KEY_IXSEAL, 0x11223344);
    assert(hll.values[0].i == 888 && hll.values[1].i == 888 && hll.values[2].i == 888);
    assert(hll.values[3].i == 55);

    return 0;
}
""", [])

    def test_ixseal_and_escalayer_clamps_and_boost_guard(self):
        defines = extract_defines(EXTRA_INC, r"EX_KEY_")
        funcs = "\n\n".join(extract_function(EXTRA_INC, fn) for fn in (
            "ex_cint_read",
            "ex_cint_write_xor",
            "ex_field_cint_read",
            "ex_field_cint_write",
            "ex_json_int",
            "ex_json_cheat_is",
            "ex_er_locks",
            "ex_er_action",
            "ex_ix_write_fac",
            "ex_ix_locks",
            "ex_ix_action",
        ))
        compile_and_run(r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

enum page_type { STRUCT_PAGE = 1, ARRAY_PAGE = 2 };
union vm_value { int32_t i; float f; };
struct page {
    enum page_type type;
    int nr_vars;
    union vm_value values[8];
};

struct ain { int dummy; };
static struct ain *ain = NULL;
static int ain_get_library(struct ain *a, const char *name) { (void)a; (void)name; return -1; }
static int32_t vm_dd_clamp(int32_t n, int32_t lo, int32_t hi) { return n < lo ? lo : n > hi ? hi : n; }

static struct page g_gp, g_cp, g_hp, g_arr, g_h0, g_fac;
static struct page c_peace, c_love, c_libido, c_libido_lim, c_energy, c_energy_lim;
static struct page c_shield, c_atk, c_def, c_spd, c_boost;
static struct page c_plate, c_ix_hp, c_ix_hpmax, c_ix_atk, c_ix_def, c_ix_spd;
static struct page c_h_energy, c_h_love, c_h_pollute;

static int cbool_write_calls = 0;
static void ex_cbool_write(struct page *cbool, bool b, uint32_t key_seed)
{
    (void)key_seed;
    cbool_write_calls++;
    if (cbool) cbool->values[0].i = b ? 1 : 0;
}

static struct page *vm_get_global_page(const char *n1, const char *n2)
{
    (void)n2;
    if (!strcmp(n1, "g_cGameParam")) return &g_gp;
    if (!strcmp(n1, "g_cCustomizeParam")) return &g_cp;
    if (!strcmp(n1, "g_cHeroineParam")) return &g_hp;
    return NULL;
}

static struct page *vm_get_field_page(struct page *p, const char *n1, const char *n2)
{
    (void)n2;
    if (p == &g_gp) {
        if (!strcmp(n1, "m_nPeacePoint")) return &c_peace;
        if (!strcmp(n1, "m_nLove")) return &c_love;
        if (!strcmp(n1, "m_nLibido")) return &c_libido;
        if (!strcmp(n1, "m_nLibidoLimit")) return &c_libido_lim;
        if (!strcmp(n1, "m_nEnergy")) return &c_energy;
        if (!strcmp(n1, "m_nEnergyLimit")) return &c_energy_lim;
        if (!strcmp(n1, "m_bBoost")) return &c_boost;
        if (!strcmp(n1, "m_nMagicPlate")) return &c_plate;
    } else if (p == &g_cp) {
        if (!strcmp(n1, "m_nShield")) return &c_shield;
        if (!strcmp(n1, "m_nAttack")) return &c_atk;
        if (!strcmp(n1, "m_nDefense")) return &c_def;
        if (!strcmp(n1, "m_nSpeed")) return &c_spd;
    } else if (p == &g_hp) {
        if (!strcmp(n1, "m_acBase")) return &g_arr;
    } else if (p == &g_h0) {
        if (!strcmp(n1, "m_cFaculty")) return &g_fac;
        if (!strcmp(n1, "m_nEnergy")) return &c_h_energy;
        if (!strcmp(n1, "m_nFavorability")) return &c_h_love;
        if (!strcmp(n1, "m_cPollution")) return &c_h_pollute;
    } else if (p == &g_fac) {
        if (!strcmp(n1, "m_nHitPoint")) return &c_ix_hp;
        if (!strcmp(n1, "m_nHitPointLimit")) return &c_ix_hpmax;
        if (!strcmp(n1, "m_nAttack")) return &c_ix_atk;
        if (!strcmp(n1, "m_nDefense")) return &c_ix_def;
        if (!strcmp(n1, "m_nReaction")) return &c_ix_spd;
    }
    return NULL;
}

static int32_t vm_get_field_int(struct page *p, const char *n1, const char *n2, int32_t def)
{
    (void)p; (void)n2;
    if (n1 && strstr(n1, "Key")) return 0x13579BDF;
    return def;
}

static int vm_dd_array_len(struct page *p) { return p == &g_arr ? 1 : 0; }
static int vm_dd_array_elem_slot(struct page *p, int i) { (void)p; return i; }
static struct page *vm_safe_get_page(int slot) { return slot == 0 ? &g_h0 : NULL; }

""" + defines + "\n" + funcs + r"""

static void init_cint(struct page *p, int32_t v)
{
    memset(p, 0, sizeof(*p));
    p->type = STRUCT_PAGE;
    p->nr_vars = 4;
    p->values[0].i = v;
}

static void send(void (*fn)(cJSON *), const char *json)
{
    cJSON *act = cJSON_Parse(json);
    assert(act != NULL);
    fn(act);
    cJSON_Delete(act);
}

int main(void)
{
    init_cint(&c_peace, 1); init_cint(&c_love, 1); init_cint(&c_libido, 1);
    init_cint(&c_libido_lim, 100); init_cint(&c_energy, 1); init_cint(&c_energy_lim, 100);
    init_cint(&c_shield, 2); init_cint(&c_atk, 2); init_cint(&c_def, 2); init_cint(&c_spd, 2);
    init_cint(&c_boost, 0);
    init_cint(&c_plate, 3);
    init_cint(&c_ix_hp, 10); init_cint(&c_ix_hpmax, 50);
    init_cint(&c_ix_atk, 10); init_cint(&c_ix_def, 10); init_cint(&c_ix_spd, 10);
    init_cint(&c_h_energy, 10); init_cint(&c_h_love, 10); init_cint(&c_h_pollute, 10);

    /* Escalayer Reboot: m_bBoost lock guard prevents redundant writes once true. */
    ex_er_locks(1 << 2);
    assert(cbool_write_calls == 1 && c_boost.values[0].i == 1);
    ex_er_locks(1 << 2);
    assert(cbool_write_calls == 1);

    /* Escalayer Reboot: customize lock sets to 10 (not 9999/999) and pattern == 0. */
    ex_er_locks(1 << 3);
    assert(c_shield.values[0].i == 10 && c_shield.values[3].i == 0);
    assert(c_atk.values[0].i == 10 && c_atk.values[3].i == 0);
    assert(c_def.values[0].i == 10 && c_def.values[3].i == 0);
    assert(c_spd.values[0].i == 10 && c_spd.values[3].i == 0);

    /* Escalayer Reboot action clamps customize stats to [1, 10]. */
    send(ex_er_action, "{\"shield\":999,\"atk\":0,\"def\":50,\"spd\":-5}");
    assert(c_shield.values[0].i == 10);
    assert(c_atk.values[0].i == 1);
    assert(c_def.values[0].i == 10);
    assert(c_spd.values[0].i == 1);

    /* Ixseal: m_nMagicPlate lock caps at 12 (not 99999) with shuffled pattern 0x00070007. */
    ex_ix_locks(1 << 3);
    assert(c_plate.values[0].i == 12 && c_plate.values[3].i == 0x00070007);

    /* Ixseal action clamps m_nMagicPlate to [1, 12] and faculty stats to [1, 999] (hp [0, 999]). */
    send(ex_ix_action, "{\"gold\":99999}");
    assert(c_plate.values[0].i == 12);
    send(ex_ix_action, "{\"gold\":0}");
    assert(c_plate.values[0].i == 1);
    send(ex_ix_action, "{\"addGold\":50}");
    assert(c_plate.values[0].i == 12);

    send(ex_ix_action, "{\"exCharEdit\":{\"target\":0,\"hpMax\":99999,\"hp\":99999,\"atk\":99999,\"def\":0,\"spd\":9999}}");
    assert(c_ix_hpmax.values[0].i == 999 && c_ix_hpmax.values[3].i == 0x00070007);
    assert(c_ix_hp.values[0].i == 999 && c_ix_hp.values[3].i == 0x00070007);
    assert(c_ix_atk.values[0].i == 999 && c_ix_atk.values[3].i == 0x00070007);
    assert(c_ix_def.values[0].i == 1 && c_ix_def.values[3].i == 0x00070007);
    assert(c_ix_spd.values[0].i == 999 && c_ix_spd.values[3].i == 0x00070007);
    return 0;
}
""", [ROOT / "src/cJSON.c"], ["-lm"])

    def test_rance10_offline_write_local_save_all_status_per(self):
        fn = extract_function(MODIFIER_C, "write_local_save_mod_str")
        defines = extract_defines(MODIFIER_C, r"GBK_|SJIS_")
        compile_and_run(r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

""" + defines + r"""

enum savefile_error { SAVEFILE_SUCCESS = 0 };
struct gsave_global { int32_t value; };
struct gsave { int dummy; };

static struct gsave_global g_party_glob = { 10 };
static struct gsave_global g_gc_glob = { 1 };
static struct gsave_global g_pcp_glob = { 2 };
static struct gsave_global g_pb_glob = { 3 };

struct kv { const char *name; int32_t value; int writes; };
static struct kv cs_fields[16] = {
    { "m_AtkPerPhysical", 0, 0 },
    { "m_AtkPerMagical", 0, 0 },
    { "m_DefPerPhysical", 0, 0 },
    { "m_DefPerMagical", 0, 0 },
    { "m_PerAvoidance", 0, 0 },
    { "m_DecPerBadCondition", 0, 0 },
    { "m_perAutoBattleAtk", 0, 0 },
    { "m_PerCritical", 0, 0 },
    { "m_PerMultiAttack", 0, 0 },
    { "m_PerExpUp", 0, 0 },
    { "m_PerHpUp", 0, 0 },
    { "m_PerAutoHeal", 0, 0 },
    { "m_PerFindTreasure", 0, 0 },
    { "m_PerInjection", 0, 0 },
    { "m_AddPerHit", 0, 0 },
    { "StatusMaxPer", 0, 0 },
};

static void sys4_set_text_gbk(bool b) { (void)b; }
static struct gsave g_gs;
static struct gsave *gsave_read(const char *path, enum savefile_error *err) { (void)path; *err = SAVEFILE_SUCCESS; return &g_gs; }
static void gsave_free(struct gsave *gs) { (void)gs; }
static enum savefile_error gsave_write(struct gsave *gs, FILE *fp, bool enc, int lvl) { (void)gs; (void)fp; (void)enc; (void)lvl; return SAVEFILE_SUCCESS; }
static FILE *atomic_save_open(const char *path, const char *mode, char **tmp_out) { (void)path; (void)mode; *tmp_out = NULL; return (FILE*)1; }
static bool atomic_save_commit(FILE *fp, const char *path, char *tmp, bool b) { (void)fp; (void)path; (void)tmp; (void)b; return true; }
static void atomic_save_abort(FILE *fp, char *tmp) { (void)fp; (void)tmp; }
static void gs_set_tt(struct gsave *gs, int idx, int32_t v) { (void)gs; (void)idx; (void)v; }
static int gs_process_all_cards(struct gsave *gs, int a, int b, int c, int d) { (void)gs; (void)a; (void)b; (void)c; (void)d; return 0; }
static int gs_process_all_characters(struct gsave *gs, int a, int b) { (void)gs; (void)a; (void)b; return 0; }

static struct gsave_global *gs_find_global(struct gsave *gs, const char *n1, const char *n2)
{
    (void)gs; (void)n2;
    if (!strcmp(n1, "g_party")) return &g_party_glob;
    if (!strcmp(n1, "g_gameContext")) return &g_gc_glob;
    if (!strcmp(n1, "g_playerCommonParam")) return &g_pcp_glob;
    if (!strcmp(n1, "g_partyBonus")) return &g_pb_glob;
    return NULL;
}

static int32_t gs_get_field_int(struct gsave *gs, int32_t rec, const char *n1, const char *n2, int32_t def)
{
    (void)gs; (void)n2;
    if (rec == 10 && !strcmp(n1, "m_status")) return 20; /* ps_rec */
    if (rec == 20 && !strcmp(n1, "m_status")) return 30; /* cs_rec */
    return def;
}

static bool gs_set_field_int(struct gsave *gs, int32_t rec, const char *n1, const char *n2, int32_t val)
{
    (void)gs; (void)n2;
    if (rec == 30) {
        for (int i = 0; i < 16; i++) {
            if (!strcmp(cs_fields[i].name, n1)) {
                cs_fields[i].value = val;
                cs_fields[i].writes++;
                return true;
            }
        }
    }
    return false;
}

static void json_write_int_if_present(cJSON *mod, const char *key, struct gsave *gs, int32_t rec, const char *f1, const char *f2)
{
    cJSON *it = cJSON_GetObjectItem(mod, key);
    if (it && cJSON_IsNumber(it))
        gs_set_field_int(gs, rec, f1, f2, it->valueint);
}

""" + fn + r"""

int main(void)
{
    assert(write_local_save_mod_str("/tmp/dummy.asd", "{\"allStatusPer\":250,\"atkPerPhys\":500}"));
    assert(cs_fields[0].value == 500 && cs_fields[0].writes == 2);
    for (int i = 1; i < 16; i++) {
        assert(cs_fields[i].value == 250);
        assert(cs_fields[i].writes == 1);
    }
    return 0;
}
""", [ROOT / "src/cJSON.c"], ["-lm"])

    def test_evenicle2_gbk_cn_fallbacks(self):
        defines = extract_defines(MODIFIER_C, r"EV_F_") + "\n" + extract_defines(EXTRA_INC, r"EV2_F_")
        funcs = "\n\n".join(extract_function(EXTRA_INC, fn) for fn in (
            "ex_get_ref_int_at",
            "ex_set_ref_int_at",
            "ex_json_int",
            "ex_json_cheat_is",
            "ex_ev2_status_at",
            "ex_ev2_get_money",
            "ex_ev2_set_money",
            "ex_ev2_populate",
            "ex_ev2_locks",
            "ex_ev2_action",
        ))
        compile_and_run(r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

""" + defines + r"""

enum page_type { STRUCT_PAGE = 1, ARRAY_PAGE = 2 };
union vm_value { int32_t i; float f; };
struct page {
    enum page_type type;
    int nr_vars;
    union vm_value values[8];
};

static struct page g_sarr = { .type = ARRAY_PAGE, .nr_vars = 2 };
static struct page g_ref1 = { .type = STRUCT_PAGE, .nr_vars = 1 };
static struct page g_st1 = { .type = STRUCT_PAGE, .nr_vars = 1 };
static struct page g_main1 = { .type = STRUCT_PAGE, .nr_vars = 1 };
static struct page g_sub1 = { .type = STRUCT_PAGE, .nr_vars = 1 };
static struct page g_bat1 = { .type = STRUCT_PAGE, .nr_vars = 1 };
static struct page g_cmgr = { .type = STRUCT_PAGE, .nr_vars = 1 };
static struct page g_ilist = { .type = ARRAY_PAGE, .nr_vars = 2 };
static struct page g_info1 = { .type = STRUCT_PAGE, .nr_vars = 1 };
static struct page g_fref = { .type = STRUCT_PAGE, .nr_vars = 6, .values = { [4] = { .i = 9 }, [5] = { .i = 0 } } };
static struct page g_money_page = { .type = STRUCT_PAGE, .nr_vars = 1, .values = { { .i = 1234 } } };

/* CN-only fields: JP names must miss so only _CN fallback succeeds! */
static int32_t f_lv = 5, f_hp = 10, f_mhp = 100, f_bhp = 80;
static int32_t f_atk = 20, f_batk = 15, f_mag = 12, f_bmag = 10;
static int32_t f_def = 14, f_bdef = 11, f_spd = 9, f_bspd = 8;
static int32_t f_exp = 50, f_sp = 3, f_bp = 1;
static int32_t f_cid = 0, f_pset = 2;

static int32_t vm_dd_clamp(int32_t n, int32_t lo, int32_t hi) { return n < lo ? lo : n > hi ? hi : n; }

static struct page *vm_get_global_page(const char *n1, const char *n2)
{
    (void)n2;
    if (!strcmp(n1, "g_arfStatus")) return &g_sarr;
    if (!strcmp(n1, "g_clCharaInfoManager")) return &g_cmgr;
    if (!strcmp(n1, "g_FlagReference")) return &g_fref;
    return NULL;
}

static int vm_dd_array_len(struct page *p) { return p ? p->nr_vars : 0; }
static int vm_dd_array_elem_slot(struct page *p, int i)
{
    if (p == &g_sarr && i == 1) return 1;
    if (p == &g_ilist && i == 1) return 2;
    return -1;
}

static struct page *vm_safe_get_page(int slot)
{
    if (slot == 1) return &g_ref1;
    if (slot == 2) return &g_info1;
    if (slot == 9) return &g_money_page;
    return NULL;
}

static struct page *vm_get_field_page(struct page *p, const char *n1, const char *n2)
{
    (void)n2;
    if (p == &g_ref1 && !strcmp(n1, "rf")) return &g_st1;
    if (p == &g_st1 && !strcmp(n1, "m_clMain")) return &g_main1;
    if (p == &g_st1 && !strcmp(n1, "m_clSub")) return &g_sub1;
    if (p == &g_st1 && !strcmp(n1, "m_clBattle")) return &g_bat1;
    if (p == &g_cmgr && !strcmp(n1, "m_InfoList")) return &g_ilist;
    return NULL;
}

static union vm_value *vm_get_struct_member(struct page *p, const char *n1, const char *n2) { (void)p; (void)n1; (void)n2; return NULL; }

static int32_t *resolve_cn_only(struct page *p, const char *n1, const char *n2)
{
    const char *names[2] = { n1, n2 };
    for (int k = 0; k < 2; k++) {
        const char *n = names[k];
        if (!n) continue;
        if (p == &g_st1) {
            if (!strcmp(n, "m_IsUse")) { static int32_t one = 1; return &one; }
            if (!strcmp(n, "m_CharaNo")) { static int32_t one = 1; return &one; }
        } else if (p == &g_info1) {
            if (!strcmp(n, EV_F_CHARA_ID_CN)) return &f_cid;
            if (!strcmp(n, EV_F_PARTY_SET_CN)) return &f_pset;
        } else if (p == &g_main1) {
            if (!strcmp(n, EV_F_LV_CN)) return &f_lv;
            if (!strcmp(n, EV_F_HP_CN)) return &f_hp;
            if (!strcmp(n, EV_F_MAX_HP_CN)) return &f_mhp;
            if (!strcmp(n, EV_F_BASE_HP_CN)) return &f_bhp;
            if (!strcmp(n, EV_F_ATK_CN)) return &f_atk;
            if (!strcmp(n, EV_F_BASE_ATK_CN)) return &f_batk;
            if (!strcmp(n, EV_F_MAG_CN)) return &f_mag;
            if (!strcmp(n, EV_F_BASE_MAG_CN)) return &f_bmag;
            if (!strcmp(n, EV_F_DEF_CN)) return &f_def;
            if (!strcmp(n, EV_F_BASE_DEF_CN)) return &f_bdef;
            if (!strcmp(n, EV_F_SPD_CN)) return &f_spd;
            if (!strcmp(n, EV_F_BASE_SPD_CN)) return &f_bspd;
            if (!strcmp(n, EV_F_EXP_CN)) return &f_exp;
        } else if (p == &g_sub1) {
            if (!strcmp(n, EV2_F_SKILL_P_CN)) return &f_sp;
        } else if (p == &g_bat1) {
            if (!strcmp(n, EV2_F_BP_CN)) return &f_bp;
        }
    }
    return NULL;
}

static int32_t vm_get_field_int(struct page *p, const char *n1, const char *n2, int32_t def)
{
    int32_t *ptr = resolve_cn_only(p, n1, n2);
    return ptr ? *ptr : def;
}

static void vm_set_field_int(struct page *p, const char *n1, const char *n2, int32_t val)
{
    int32_t *ptr = resolve_cn_only(p, n1, n2);
    if (ptr) *ptr = val;
}

static char *vm_dd_get_field_utf8(struct page *p, const char *n1, const char *n2)
{
    if (p == &g_info1 && ((n1 && !strcmp(n1, EV_F_NAME_CN)) || (n2 && !strcmp(n2, EV_F_NAME_CN))))
        return strdup("AstaCN");
    return strdup("");
}

static void vm_ev_set_stat_with_base(struct page *st, const char *eff_f1, const char *eff_f2,
        const char *base_f1, const char *base_f2, int32_t target, int32_t min_v, int32_t max_v)
{
    int32_t clamped = vm_dd_clamp(target, min_v, max_v);
    int32_t cur = vm_get_field_int(st, eff_f1, eff_f2, 0);
    int32_t delta = clamped - cur;
    vm_set_field_int(st, eff_f1, eff_f2, clamped);
    if ((base_f1 || base_f2) && delta != 0) {
        int32_t bcur = vm_get_field_int(st, base_f1, base_f2, 0);
        vm_set_field_int(st, base_f1, base_f2, vm_dd_clamp(bcur + delta, min_v, max_v));
    }
}

""" + funcs + r"""

int main(void)
{
    /* Populate reads CN-only fields. */
    cJSON *root = cJSON_CreateObject();
    ex_ev2_populate(root);
    cJSON *chars = cJSON_GetObjectItemCaseSensitive(root, "exCharacters");
    assert(cJSON_GetArraySize(chars) == 1);
    cJSON *c0 = cJSON_GetArrayItem(chars, 0);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(c0, "name")->valuestring, "AstaCN") == 0);
    assert(cJSON_GetObjectItemCaseSensitive(c0, "lv")->valueint == 5);
    assert(cJSON_GetObjectItemCaseSensitive(c0, "hp")->valueint == 10);
    assert(cJSON_GetObjectItemCaseSensitive(c0, "skillP")->valueint == 3);
    assert(cJSON_GetObjectItemCaseSensitive(c0, "bp")->valueint == 1);
    cJSON_Delete(root);

    /* Locks update CN-only HP, skillP, BP. */
    ex_ev2_locks((1 << 1) | (1 << 2) | (1 << 3));
    assert(f_hp == 100 && f_sp == 99 && f_bp == 5);

    /* Action edits CN-only stats + base stats. */
    cJSON *act = cJSON_Parse("{\"exCharEdit\":{\"target\":1,\"lv\":20,\"hpMax\":200,\"atk\":50,\"skillP\":77,\"bp\":4}}");
    ex_ev2_action(act);
    cJSON_Delete(act);
    assert(f_lv == 20 && f_mhp == 200 && f_bhp == 180 && f_atk == 50 && f_batk == 45 && f_sp == 77 && f_bp == 4);
    return 0;
}
""", [ROOT / "src/cJSON.c"], ["-lm"])

    def test_toushin3_and_bladebriders_battle_sync(self):
        defines = extract_defines(EXTRA_INC, r"T3_")
        funcs = "\n\n".join(extract_function(EXTRA_INC, fn) for fn in (
            "ex_arr_int_get",
            "ex_arr_int_set",
            "ex_json_int",
            "ex_json_cheat_is",
            "ex_t3_param_arr",
            "ex_t3_action",
            "ex_bb_apply_char_edit",
            "ex_bb_action",
        ))
        compile_and_run(r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

""" + defines + r"""

enum page_type { STRUCT_PAGE = 1, ARRAY_PAGE = 2 };
union vm_value { int32_t i; float f; };
struct page {
    enum page_type type;
    int nr_vars;
    union vm_value values[16];
    const char *name;
    int32_t lv, hp, hpMax, baseHp, baseHpMax, atk, baseAtk, joinNo;
};

static struct page t3_cd = { .type = STRUCT_PAGE }, t3_pa = { .type = ARRAY_PAGE, .nr_vars = 10 };
static struct page t3_ba_holder = { .type = STRUCT_PAGE }, t3_ba = { .type = ARRAY_PAGE, .nr_vars = 10 };

static struct page bb_gd = { .type = STRUCT_PAGE }, bb_game = { .type = STRUCT_PAGE };
static struct page bb_allies = { .type = ARRAY_PAGE, .nr_vars = 2 };
static struct page bb_au[2], bb_st[2];
static struct page bb_fld = { .type = STRUCT_PAGE }, bb_fa = { .type = ARRAY_PAGE, .nr_vars = 2 };
static struct page bb_uo[2], bb_ud[2], bb_bst[2];

static int32_t vm_dd_clamp(int32_t n, int32_t lo, int32_t hi) { return n < lo ? lo : n > hi ? hi : n; }
static int32_t ex_get_global_int(const char *n1, const char *n2, int32_t def) { (void)n1; (void)n2; return def; }
static bool ex_set_global_int(const char *n1, const char *n2, int32_t v) { (void)n1; (void)n2; (void)v; return true; }
static void ex_t3_locks(int mask) { (void)mask; }
static void ex_bb_locks(int mask) { (void)mask; }

static struct page *vm_get_global_page(const char *n1, const char *n2)
{
    (void)n2;
    if (!strcmp(n1, "g_sCharaData")) return &t3_cd;
    if (!strcmp(n1, T3_G_B_ALLY)) return &t3_ba_holder;
    if (!strcmp(n1, "g_GrobalData")) return &bb_gd;
    if (!strcmp(n1, "g_rFieldDataManager")) return &bb_fld;
    return NULL;
}

static struct page *vm_get_field_page(struct page *p, const char *n1, const char *n2)
{
    (void)n2;
    if (p == &t3_cd && (!strcmp(n1, "m_sParam") || !strcmp(n1, "m_anParam"))) return &t3_pa;
    if (p == &t3_pa && !strcmp(n1, "m_anParam")) return &t3_pa;
    if (p == &t3_ba_holder && !strcmp(n1, "m_sParam")) return &t3_ba;
    if (p == &t3_ba && !strcmp(n1, "m_anParam")) return &t3_ba;
    if (p == &bb_gd && !strcmp(n1, "m_GameData")) return &bb_game;
    if (p == &bb_game && !strcmp(n1, "m_aAllyUnitData")) return &bb_allies;
    if (p == &bb_fld && !strcmp(n1, "m_aAllyUnitObject")) return &bb_fa;
    for (int i = 0; i < 2; i++) {
        if (p == &bb_au[i] && !strcmp(n1, "m_UnitStatusData")) return &bb_st[i];
        if (p == &bb_uo[i] && !strcmp(n1, "m_UnitData")) return &bb_ud[i];
        if (p == &bb_ud[i] && !strcmp(n1, "m_StatusData")) return &bb_bst[i];
    }
    return NULL;
}

static int vm_dd_array_len(struct page *p) { return p ? p->nr_vars : 0; }
static int vm_dd_array_elem_slot(struct page *p, int i)
{
    if (p == &bb_allies) return 10 + i;
    if (p == &bb_fa) return 20 + i;
    return -1;
}
static struct page *vm_safe_get_page(int slot)
{
    if (slot >= 10 && slot < 12) return &bb_au[slot - 10];
    if (slot >= 20 && slot < 22) return &bb_uo[slot - 20];
    return NULL;
}

static int32_t *bb_field(struct page *p, const char *n)
{
    if (!p || !n) return NULL;
    if (!strcmp(n, "m_nLevel")) return &p->lv;
    if (!strcmp(n, "m_nHP")) return &p->hp;
    if (!strcmp(n, "m_nHPMax")) return &p->hpMax;
    if (!strcmp(n, "m_nBaseHP")) return &p->baseHp;
    if (!strcmp(n, "m_nBaseHPMax")) return &p->baseHpMax;
    if (!strcmp(n, "m_nAttack")) return &p->atk;
    if (!strcmp(n, "m_nBaseAttack")) return &p->baseAtk;
    if (!strcmp(n, "m_eJoinFlagNo")) return &p->joinNo;
    return NULL;
}

static int32_t vm_get_field_int(struct page *p, const char *n1, const char *n2, int32_t def)
{
    (void)n2;
    int32_t *f = bb_field(p, n1);
    return f ? *f : def;
}
static void vm_set_field_int(struct page *p, const char *n1, const char *n2, int32_t v)
{
    (void)n2;
    int32_t *f = bb_field(p, n1);
    if (f) *f = v;
}
static char *vm_dd_get_field_utf8(struct page *p, const char *n1, const char *n2)
{
    (void)n1; (void)n2;
    return strdup((p && p->name) ? p->name : "");
}

""" + funcs + r"""

static void send(void (*fn)(cJSON *), const char *json)
{
    cJSON *act = cJSON_Parse(json);
    assert(act != NULL);
    fn(act);
    cJSON_Delete(act);
}

int main(void)
{
    /* Toushin 3: inactive battle copy (hpMax == 0) is NOT touched. */
    t3_pa.values[2].i = 100;
    t3_ba.values[2].i = 0;
    send(ex_t3_action, "{\"atk\":555,\"hp\":80}");
    assert(t3_pa.values[3].i == 555 && t3_pa.values[1].i == 80);
    assert(t3_ba.values[3].i == 0 && t3_ba.values[1].i == 0);

    /* Toushin 3: active battle copy (hpMax > 0) syncs stats AND boostAll! */
    t3_ba.values[2].i = 100;
    send(ex_t3_action, "{\"lv\":42,\"hp\":90,\"hpMax\":200,\"atk\":777,\"def\":333,\"hit\":95,\"eva\":88}");
    assert(t3_pa.values[0].i == 42 && t3_ba.values[0].i == 42);
    assert(t3_pa.values[1].i == 90 && t3_ba.values[1].i == 90);
    assert(t3_pa.values[2].i == 200 && t3_ba.values[2].i == 200);
    assert(t3_pa.values[3].i == 777 && t3_ba.values[3].i == 777);
    assert(t3_pa.values[4].i == 333 && t3_ba.values[4].i == 333);
    assert(t3_pa.values[5].i == 95 && t3_ba.values[5].i == 95);
    assert(t3_pa.values[6].i == 88 && t3_ba.values[6].i == 88);

    send(ex_t3_action, "{\"cheat\":\"boostAll\"}");
    assert(t3_pa.values[3].i == 9999 && t3_ba.values[3].i == 9999);
    assert(t3_pa.values[7].i == 100 && t3_ba.values[7].i == 100);

    /* Blade Briders: deployed battle array has REVERSED order vs m_aAllyUnitData! */
    bb_st[0] = (struct page){ .name = "Hero", .lv = 5, .hp = 20, .hpMax = 100, .atk = 10 };
    bb_st[1] = (struct page){ .name = "Mage", .lv = 4, .hp = 15, .hpMax = 0,   .atk = 8  };
    bb_bst[0] = (struct page){ .name = "Mage", .lv = 4, .hp = 15, .hpMax = 0,  .atk = 8  };
    bb_bst[1] = (struct page){ .name = "Hero", .lv = 5, .hp = 20, .hpMax = 100, .atk = 10 };

    /* Edit target=0 ("Hero") -> updates bb_st[0] and bb_bst[1] (NOT bb_bst[0] "Mage")! */
    send(ex_bb_action, "{\"exCharEdit\":{\"target\":0,\"atk\":999,\"fullHeal\":true}}");
    assert(bb_st[0].atk == 999 && bb_st[0].baseAtk == 999 && bb_st[0].hp == 100);
    assert(bb_bst[1].atk == 999 && bb_bst[1].baseAtk == 999 && bb_bst[1].hp == 100);
    assert(bb_st[1].atk == 8 && bb_bst[0].atk == 8);

    /* Edit target=-1 fullHeal on Mage (hpMax == 0) must NOT zero Mage's hp! */
    send(ex_bb_action, "{\"exCharEdit\":{\"target\":-1,\"fullHeal\":true}}");
    assert(bb_st[1].hp == 15 && bb_bst[0].hp == 15);
    return 0;
}
""", [ROOT / "src/cJSON.c"], ["-lm"])

    def test_fullheal_max_gt_0_guards_okusama_gold_and_pascha3_skip(self):
        defines = (
            extract_defines(EXTRA_INC, r"SB_") + "\n"
            + extract_defines(EXTRA_INC, r"EX_KEY_")
        )
        funcs = "\n\n".join(extract_function(EXTRA_INC, fn) for fn in (
            "ex_arr_int_get",
            "ex_arr_int_set",
            "ex_json_int",
            "ex_json_cheat_is",
            "ex_cint_read",
            "ex_cint_write_xor",
            "ex_field_cint_read",
            "ex_field_cint_write",
            "ex_db_locks",
            "ex_db_action",
            "ex_dt_action",
            "ex_yk_action",
            "ex_wd_action",
            "ex_sb_action",
            "ex_ok_action",
            "ex_p3_locks",
        ))
        compile_and_run(r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

""" + defines + r"""

enum page_type { STRUCT_PAGE = 1, ARRAY_PAGE = 2 };
union vm_value { int32_t i; float f; };
struct page {
    enum page_type type;
    int nr_vars;
    union vm_value values[12];
    const char *name;
    int32_t hp, hpMax, vit, vitMax, mp, mpMax, gold;
    struct page *c_hp, *c_sp, *c_ten;
};

struct ain { int dummy; };
static struct ain *ain = NULL;
static int ain_get_library(struct ain *a, const char *name) { (void)a; (void)name; return -1; }
static int32_t vm_dd_clamp(int32_t n, int32_t lo, int32_t hi) { return n < lo ? lo : n > hi ? hi : n; }
static int32_t ex_get_global_int(const char *n1, const char *n2, int32_t def) { (void)n1; (void)n2; return def; }
static bool ex_set_global_int(const char *n1, const char *n2, int32_t v) { (void)n1; (void)n2; (void)v; return true; }
static int32_t ex_db_get_gold(struct page *gv) { (void)gv; return 0; }
static void ex_db_set_gold(struct page *gv, int32_t v) { (void)gv; (void)v; }
static void ex_dt_locks(int m) { (void)m; }
static void ex_yk_locks(int m) { (void)m; }
static void ex_wd_locks(int m) { (void)m; }
static void ex_sb_locks(int m) { (void)m; }
static void ex_ok_locks(int m) { (void)m; }

static struct page g_gv = { .type = STRUCT_PAGE };
static struct page db_arr = { .type = ARRAY_PAGE, .nr_vars = 1 }, db_unit;
static struct page dt_ctx = { .type = STRUCT_PAGE }, dt_arr = { .type = ARRAY_PAGE, .nr_vars = 1 }, dt_unit;
static struct page yk_arr = { .type = ARRAY_PAGE, .nr_vars = 1 }, yk_unit;
static struct page wd_cm = { .type = STRUCT_PAGE }, wd_arr = { .type = ARRAY_PAGE, .nr_vars = 1 };
static struct page wd_unit = { .type = STRUCT_PAGE }, wd_pa = { .type = ARRAY_PAGE, .nr_vars = 12 };
static struct page sb_arr = { .type = ARRAY_PAGE, .nr_vars = 1 }, sb_unit;
static struct page ok_cm = { .type = STRUCT_PAGE }, ok_plist = { .type = ARRAY_PAGE, .nr_vars = 2 };
static struct page ok_pos[2], ok_slist = { .type = STRUCT_PAGE }, ok_uarr = { .type = ARRAY_PAGE, .nr_vars = 1 }, ok_unit;
static struct page p3_gp = { .type = STRUCT_PAGE }, p3_units = { .type = ARRAY_PAGE, .nr_vars = 2 };
static struct page p3_u[2], p3_hp[2], p3_sp[2], p3_ten[2];

static struct page *ex_db_player_servants(void) { return &db_arr; }
static struct page *ex_ok_charas(void) { return &ok_cm; }
static struct page *ex_p3_unit_list(void) { return &p3_units; }

static struct page *vm_get_global_page(const char *n1, const char *n2)
{
    (void)n2;
    if (!strcmp(n1, "GlobalVar")) return &g_gv;
    if (!strcmp(n1, "context")) return &dt_ctx;
    if (!strcmp(n1, "LAbility::m_pAbilities")) return &yk_arr;
    if (!strcmp(n1, "g_sCharaData")) return &wd_cm;
    if (!strcmp(n1, SB_G_CHR_CN)) return &sb_arr;
    if (!strcmp(n1, "g_cGameParam")) return &p3_gp;
    return NULL;
}

static struct page *vm_get_field_page(struct page *p, const char *n1, const char *n2)
{
    (void)n2;
    if (p == &dt_ctx && !strcmp(n1, "admiral")) return &dt_arr;
    if (p == &wd_cm && !strcmp(n1, "m_asData")) return &wd_arr;
    if (p == &wd_unit && (!strcmp(n1, "m_sParam") || !strcmp(n1, "m_anParam"))) return &wd_pa;
    if (p == &wd_pa && !strcmp(n1, "m_anParam")) return &wd_pa;
    if (p == &ok_cm && !strcmp(n1, "m_possessList")) return &ok_plist;
    if (p == &ok_cm && !strcmp(n1, "m_statusList")) return &ok_slist;
    if (p == &ok_slist && !strcmp(n1, "m_list")) return &ok_uarr;
    if ((p == &ok_pos[0] || p == &ok_pos[1]) && !strcmp(n1, "m_Money")) return p;
    if (p == &p3_u[0] || p == &p3_u[1]) {
        if (!strcmp(n1, "m_nHp")) return p->c_hp;
        if (!strcmp(n1, "m_nSp")) return p->c_sp;
        if (!strcmp(n1, "m_nTension")) return p->c_ten;
    }
    return NULL;
}

static int vm_dd_array_len(struct page *p) { return p ? p->nr_vars : 0; }
static int vm_dd_array_elem_slot(struct page *p, int i)
{
    if (p == &db_arr) return 1;
    if (p == &dt_arr) return 2;
    if (p == &yk_arr) return 3;
    if (p == &wd_arr) return 4;
    if (p == &sb_arr) return 5;
    if (p == &ok_plist) return 10 + i;
    if (p == &ok_uarr) return 15;
    if (p == &p3_units) return 20 + i;
    return -1;
}

static struct page *vm_safe_get_page(int slot)
{
    switch (slot) {
    case 1: return &db_unit;
    case 2: return &dt_unit;
    case 3: return &yk_unit;
    case 4: return &wd_unit;
    case 5: return &sb_unit;
    case 10: return &ok_pos[0];
    case 11: return &ok_pos[1];
    case 15: return &ok_unit;
    case 20: return &p3_u[0];
    case 21: return &p3_u[1];
    default: return NULL;
    }
}

static int32_t *field_ptr(struct page *p, const char *n)
{
    if (!p || !n) return NULL;
    if (!strcmp(n, "nNowHelth") || !strcmp(n, "hp_") || !strcmp(n, "hp") || !strcmp(n, SB_F_HP_CN) || !strcmp(n, "<HpNow>")) return &p->hp;
    if (!strcmp(n, "nMaxHelth") || !strcmp(n, "maxHp_") || !strcmp(n, "hp_max") || !strcmp(n, SB_F_HPMAX_CN) || !strcmp(n, "<HpMax>")) return &p->hpMax;
    if (!strcmp(n, "nNowVit")) return &p->vit;
    if (!strcmp(n, "nMaxVit")) return &p->vitMax;
    if (!strcmp(n, "<MpNow>")) return &p->mp;
    if (!strcmp(n, "<MpMax>")) return &p->mpMax;
    if (!strcmp(n, "<Value>")) return &p->gold;
    return NULL;
}

static int32_t vm_get_field_int(struct page *p, const char *n1, const char *n2, int32_t def)
{
    (void)n2;
    int32_t *f = field_ptr(p, n1);
    return f ? *f : def;
}
static void vm_set_field_int(struct page *p, const char *n1, const char *n2, int32_t v)
{
    (void)n2;
    int32_t *f = field_ptr(p, n1);
    if (f) *f = v;
}
static char *ex_get_cstring_utf8(struct page *p, const char *n)
{
    (void)n;
    return strdup((p && p->name) ? p->name : "");
}

""" + funcs + r"""

static void send(void (*fn)(cJSON *), const char *json)
{
    cJSON *act = cJSON_Parse(json);
    assert(act != NULL);
    fn(act);
    cJSON_Delete(act);
}

int main(void)
{
    /* 1. Daibanchou VIT lock + fullHeal must NOT zero nNowVit when nMaxVit == 0. */
    db_unit = (struct page){ .hp = 10, .hpMax = 200, .vit = 42, .vitMax = 0 };
    ex_db_locks(1 << 1);
    assert(db_unit.hp == 200 && db_unit.vit == 42);
    db_unit.hp = 10;
    send(ex_db_action, "{\"exCharEdit\":{\"target\":0,\"fullHeal\":true}}");
    assert(db_unit.hp == 200 && db_unit.vit == 42);

    /* 2. Daiteikoku fullHeal must NOT zero hp_ when maxHp_ == 0. */
    dt_unit = (struct page){ .hp = 42, .hpMax = 0 };
    send(ex_dt_action, "{\"exCharEdit\":{\"target\":0,\"fullHeal\":true}}");
    assert(dt_unit.hp == 42);

    /* 3. Yoru ga Kuru fullHeal must NOT zero hp when hp_max == 0. */
    yk_unit = (struct page){ .hp = 42, .hpMax = 0 };
    send(ex_yk_action, "{\"exCharEdit\":{\"target\":0,\"fullHeal\":true}}");
    assert(yk_unit.hp == 42);

    /* 4. Waidonyo fullHeal must NOT zero pa[1] when pa[2] == 0. */
    wd_pa.values[1].i = 42;
    wd_pa.values[2].i = 0;
    send(ex_wd_action, "{\"exCharEdit\":{\"target\":0,\"fullHeal\":true}}");
    assert(wd_pa.values[1].i == 42);

    /* 5. Sanbiki fullHeal keeps hp intact when hpMax == 0, heals when hpMax > 0. */
    sb_unit = (struct page){ .hp = 42, .hpMax = 0 };
    send(ex_sb_action, "{\"exCharEdit\":{\"target\":0,\"fullHeal\":true}}");
    assert(sb_unit.hp == 42);
    sb_unit = (struct page){ .hp = 10, .hpMax = 300 };
    send(ex_sb_action, "{\"exCharEdit\":{\"target\":0,\"fullHeal\":true}}");
    assert(sb_unit.hp == 300);

    /* 6. Okusama fullHeal must NOT zero HP or MP when max == 0; gold updates ALL m_possessList entries! */
    ok_unit = (struct page){ .hp = 42, .hpMax = 0, .mp = 33, .mpMax = 0 };
    ok_pos[0].gold = 100;
    ok_pos[1].gold = 250;
    send(ex_ok_action, "{\"gold\":5000,\"exCharEdit\":{\"target\":0,\"fullHeal\":true}}");
    assert(ok_unit.hp == 42 && ok_unit.mp == 33);
    assert(ok_pos[0].gold == 5000 && ok_pos[1].gold == 5000);
    send(ex_ok_action, "{\"addGold\":250}");
    assert(ok_pos[0].gold == 5250 && ok_pos[1].gold == 5250);

    /* 7. Pascha 3 lock bit 2 skips unused unit with empty m_szName, refills named unit. */
    for (int i = 0; i < 2; i++) {
        p3_hp[i] = (struct page){ .type = STRUCT_PAGE, .nr_vars = 4, .values = {{ .i = 10 }} };
        p3_sp[i] = (struct page){ .type = STRUCT_PAGE, .nr_vars = 4, .values = {{ .i = 5 }} };
        p3_ten[i] = (struct page){ .type = STRUCT_PAGE, .nr_vars = 4, .values = {{ .i = 1 }} };
        p3_u[i] = (struct page){ .type = STRUCT_PAGE, .name = (i == 0 ? "" : "Yuki"),
                                 .c_hp = &p3_hp[i], .c_sp = &p3_sp[i], .c_ten = &p3_ten[i] };
    }
    ex_p3_locks(1 << 2);
    assert(p3_hp[0].values[0].i == 10 && p3_sp[0].values[0].i == 5 && p3_ten[0].values[0].i == 1);
    assert(p3_hp[1].values[0].i == 9999 && p3_sp[1].values[0].i == 999 && p3_ten[1].values[0].i == 100);
    return 0;
}
""", [ROOT / "src/cJSON.c"], ["-lm"])

    def test_vm_ex_execute_action_applies_ui_scale(self):
        funcs = "\n\n".join(extract_function(EXTRA_INC, fn) for fn in (
            "ex_find_lock_slot",
            "ex_current_game_id",
            "ex_json_int",
            "vm_ex_execute_action",
        ))
        slots_decl = re.search(
            r"struct ex_game_lock_slot \{[^{}]*\};\s*static struct ex_game_lock_slot g_ex_locks\[EX_MAX_GAMES\] = \{.*?\};",
            (ROOT / EXTRA_INC).read_text(encoding="utf-8"),
            re.S,
        ).group()
        compile_and_run(r"""
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

#define EX_MAX_GAMES 32
static pthread_mutex_t g_mod_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_live_lock_mask = 7;

static bool game_rance6, game_evenicle2, game_evenicle2_rance, game_daibanchou;
static bool game_daiteikoku, game_galzoo, game_toushin3, game_bladebriders;
static bool game_night_remaster, game_waidonyo, game_sanbiki, game_dorapeko;
static bool game_haruurare, game_oyakorankan, game_tsumashibori, game_okusama;
static bool game_okusama_ntr, game_escalayer_reboot, game_ixseal = true;
static bool game_pascha3, game_haharanman, game_tsumamigui3, game_maman;

static int ui_scale_calls = 0;
static int persist_calls = 0;
static int last_persist_mask = -1;
static int ix_action_calls = 0;

static void vm_apply_ui_scale_from_json(cJSON *act, bool *persist_cfg, int *save_r10_mask)
{
    ui_scale_calls++;
    if (cJSON_GetObjectItem(act, "fontScale") || cJSON_GetObjectItem(act, "scrollbarScale"))
        *persist_cfg = true;
    *save_r10_mask = g_live_lock_mask;
}

static void persist_modifier_config_from_native(int mask)
{
    persist_calls++;
    last_persist_mask = mask;
}

static void ex_r06_action(cJSON *a) { (void)a; }
static void ex_ev2_action(cJSON *a) { (void)a; }
static void ex_db_action(cJSON *a) { (void)a; }
static void ex_dt_action(cJSON *a) { (void)a; }
static void ex_gz_action(cJSON *a) { (void)a; }
static void ex_t3_action(cJSON *a) { (void)a; }
static void ex_bb_action(cJSON *a) { (void)a; }
static void ex_yk_action(cJSON *a) { (void)a; }
static void ex_wd_action(cJSON *a) { (void)a; }
static void ex_sb_action(cJSON *a) { (void)a; }
static void ex_dp_action(cJSON *a) { (void)a; }
static void ex_hu_action(cJSON *a) { (void)a; }
static void ex_oy_action(cJSON *a) { (void)a; }
static void ex_ts_action(cJSON *a) { (void)a; }
static void ex_ok_action(cJSON *a) { (void)a; }
static void ex_er_action(cJSON *a) { (void)a; }
static void ex_ix_action(cJSON *a) { (void)a; ix_action_calls++; }
static void ex_p3_action(cJSON *a) { (void)a; }
static void ex_hhr_action(cJSON *a) { (void)a; }
static void ex_t3g_action(cJSON *a) { (void)a; }
static void ex_mm_action(cJSON *a) { (void)a; }

""" + slots_decl + "\n" + funcs + r"""

int main(void)
{
    /* UI scale only -> invokes vm_apply_ui_scale_from_json and persists once. */
    vm_ex_execute_action("{\"fontScale\":135,\"scrollbarScale\":180}");
    assert(ui_scale_calls == 1 && persist_calls == 1 && last_persist_mask == 7 && ix_action_calls == 1);

    /* Combined setGameLockMask + fontScale -> persists once, sets lock mask. */
    vm_ex_execute_action("{\"setGameLockMask\":5,\"fontScale\":120}");
    assert(ui_scale_calls == 2 && persist_calls == 2 && ix_action_calls == 2);
    struct ex_game_lock_slot *slot = ex_find_lock_slot("ixseal");
    assert(slot && slot->init && slot->mask == 5);

    /* Action with neither lock nor UI scale -> does NOT redundantly persist. */
    vm_ex_execute_action("{\"energy\":100}");
    assert(ui_scale_calls == 3 && persist_calls == 2 && ix_action_calls == 3);
    return 0;
}
""", [ROOT / "src/cJSON.c"], ["-lm", "-pthread"])


if __name__ == "__main__":
    unittest.main()
