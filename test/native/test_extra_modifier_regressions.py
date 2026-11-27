"""Host regression tests for live modifiers across System 4 and 3.5."""
import re
import unittest

from test_evenicle2_regressions import ROOT, compile_and_run
from test_vm_reign_regressions import extract_function


class ExtraModifierChecks(unittest.TestCase):
    def test_okusama_ntr_marker_is_case_insensitive(self):
        compile_and_run(r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <strings.h>
""" + extract_function("src/hacks.c", "has_ntr_marker") + r"""
int main(void)
{
    assert(has_ntr_marker("OKUSAMA_NTR.ain"));
    assert(has_ntr_marker("game_nTr"));
    assert(!has_ntr_marker("OKUSAMA_NT.ain"));
    assert(!has_ntr_marker(NULL));
}
""", [])

    def test_ally_edits_and_galzoo_base_stats(self):
        source = "src/modifier_extra.inc"
        defines = "\n".join(re.findall(
            r"^#define (?:R06_|GZ_).*", (ROOT / source).read_text(), re.M
        ))
        defines += "\n" + re.search(r"^#define EV_F_LV\s+.*",
                                     (ROOT / "src/modifier.c").read_text(), re.M).group()
        functions = "\n".join(extract_function(source, fn) for fn in (
            "ex_json_int", "ex_json_cheat_is", "ex_r06_action", "ex_gz_action"
        ))
        compile_and_run(r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "cJSON.h"
""" + defines + r"""
struct page { int hp, maxhp, lv, ap, maxap, base, love; };
static struct page array, chars[52];
static struct page *vm_get_global_page(const char *key, const char *alternate)
{
    return strcmp(key, "gc") && strcmp(key, "gs") ? NULL : &array;
}
static int vm_dd_array_len(struct page *page) { return page ? 52 : 0; }
static int vm_dd_array_elem_slot(struct page *page, int index) { return index; }
static struct page *vm_safe_get_page(int slot) { return &chars[slot]; }
static int *field(struct page *page, const char *name)
{
    if (!strcmp(name, R06_F_HP) || !strcmp(name, GZ_F_HP_CN)) return &page->hp;
    if (!strcmp(name, R06_F_HP_MAX)) return &page->maxhp;
    if (!strcmp(name, R06_F_LV)) return &page->lv;
    if (!strcmp(name, R06_F_AP)) return &page->ap;
    if (!strcmp(name, R06_F_AP_MAX)) return &page->maxap;
    if (!strcmp(name, GZ_F_HP_BASE_CN)) return &page->base;
    if (!strcmp(name, GZ_F_LOVE_CN)) return &page->love;
    return NULL;
}
static int vm_get_field_int(struct page *page, const char *name, const char *alternate, int def)
{ int *p = field(page, name); return p ? *p : def; }
static void vm_set_field_int(struct page *page, const char *name, const char *alternate, int val)
{ int *p = field(page, name); if (p) *p = val; }
static int ex_get_global_int(const char *name, const char *alternate, int def) { return def; }
static void ex_set_global_int(const char *name, const char *alternate, int val) {}
static int vm_dd_clamp(int n, int lo, int hi) { return n < lo ? lo : n > hi ? hi : n; }
static void ex_r06_locks(int mask) {}
static void ex_gz_locks(int mask) {}
""" + functions + r"""
static void send(void (*fn)(cJSON *), const char *json)
{
    cJSON *action = cJSON_Parse(json);
    assert(action);
    fn(action);
    cJSON_Delete(action);
}
int main(void)
{
    chars[50] = chars[51] = (struct page){ .hp = 4, .maxhp = 10, .lv = 2, .ap = 1, .maxap = 5 };
    send(ex_r06_action, "{\"cheat\":\"fullHeal\",\"exCharEdit\":{\"target\":-1,\"lv\":99}}");
    assert(chars[50].hp == 10 && chars[50].ap == 5 && chars[50].lv == 99);
    assert(chars[51].hp == 4 && chars[51].ap == 1 && chars[51].lv == 2);
    send(ex_r06_action, "{\"exCharEdit\":{\"target\":51,\"lv\":99}}");
    assert(chars[51].lv == 2);
    chars[1] = (struct page){ .hp = 40, .base = 90 };
    send(ex_gz_action, "{\"exCharEdit\":{\"target\":1,\"hp\":40,\"love\":15}}");
    assert(chars[1].hp == 40 && chars[1].base == 90 && chars[1].love == 15);
    send(ex_gz_action, "{\"exCharEdit\":{\"target\":1,\"hp\":60}}");
    assert(chars[1].hp == 60 && chars[1].base == 90);
    send(ex_gz_action, "{\"exCharEdit\":{\"target\":1,\"hp\":9999,\"boostBase\":1}}");
    assert(chars[1].hp == 9999 && chars[1].base == 9999);
}
""", [ROOT / "src/cJSON.c"], ["-lm"])

    def test_haharanman_assist_transitions(self):
        source = "src/modifier_extra.inc"
        state = re.search(r"static struct \{[^{}]*\} hhr_assists;", (ROOT / source).read_text()).group()
        functions = "\n".join(extract_function(source, fn) for fn in (
            "ex_hhr_reset_assists", "ex_hhr_config_assists"
        ))
        compile_and_run(r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
struct page { int nr_vars, values[6]; };
union vm_value { int i; };
static struct page global = { .nr_vars = 1 }, cfg = { .values = {0, 1, 0, 0, 1, 0} };
static struct { struct page *page; } heap[] = {{&global}};
static uint32_t seq = 1;
static int vm_find_global_idx(const char *name, const char *alternate) { return 0; }
static union vm_value global_get(int idx) { return (union vm_value){ .i = 1 }; }
static struct page *vm_safe_get_page(int slot) { return &cfg; }
static uint32_t heap_get_seq(int slot) { return seq; }
static int field(const char *name)
{
    const char *fields[] = {"<SkipSpend>", "<SkipUseCount>", "<BoostExp>",
        "<DisableTroll>", "<DisableAbleActionTimezone>", "<DisableCounterCondition>"};
    for (int i = 0; i < 6; i++) if (!strcmp(fields[i], name)) return i;
    assert(false);
}
static int vm_get_field_int(struct page *p, const char *name, const char *alt, int def)
{ return p->values[field(name)]; }
static void vm_set_field_int(struct page *p, const char *name, const char *alt, int val)
{ p->values[field(name)] = val; }
""" + state + functions + r"""
int main(void)
{
    const int initial[] = {0, 1, 0, 0, 1, 0}, all[] = {1, 1, 1, 1, 1, 1};
    ex_hhr_config_assists(12, false);
    ex_hhr_config_assists(12, false);
    assert(!memcmp(cfg.values, all, sizeof(all)));
    ex_hhr_config_assists(8, false);
    assert(!memcmp(cfg.values, initial, 3 * sizeof(int)) && cfg.values[3] == 1);
    ex_hhr_config_assists(0, false);
    assert(!memcmp(cfg.values, initial, sizeof(initial)));
    ex_hhr_config_assists(4, false);
    ex_hhr_config_assists(12, true);
    ex_hhr_config_assists(0, false);
    assert(!memcmp(cfg.values, all, sizeof(all)));
    ex_hhr_config_assists(12, false);
    seq++;
    memcpy(cfg.values, initial, sizeof(initial));
    ex_hhr_config_assists(12, false);
    ex_hhr_config_assists(0, false);
    assert(!memcmp(cfg.values, initial, sizeof(initial)));
    ex_hhr_reset_assists();
    assert(!hhr_assists.active_mask);
}
""", [])
