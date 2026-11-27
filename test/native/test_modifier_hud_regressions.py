#!/usr/bin/env python3
"""Exercise VM arguments and visible-widget guards without game assets."""

from pathlib import Path
import subprocess
import tempfile
import unittest

from test_modifier_injected_key_regressions import extract_function


R02_ACTION_HARNESS = r"""
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

enum { STRUCT_PAGE, ARRAY_PAGE, AIN_ARRAY_INT };
struct page { int nr_vars; union { int i; } values[15]; };
static struct page characters = { 2, {{0}, {1}} }, character = { 15, {{0}, {1}, {0}, {0}, {1}} };
static struct { struct page *page; } heap[] = {{0}};
static pthread_mutex_t g_mod_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool s_r02_compatible = true, s_r02_applying, g_has_r02_lock_mask_init;
static int g_r02_lock_mask, g_live_lock_mask, s_r02_chara_type;
static int s_r02_gold_glob = -1, s_r02_karma_glob = -1;
static int s_r02_item_world = -1, s_r02_item_tree = -1, s_r02_item_potion = -1;
static int s_r02_item_candy = -1, s_r02_item_light = -1, s_r02_item_rope = -1;
static bool vm_globals_ready(void) { return true; }
static struct page *r02_get_cd_array(void) { return &characters; }
static bool r02_is_in_game(struct page *page) { return page == &characters; }
static void r02_unlock_all_gallery(void) { assert(false); }
static void persist_modifier_config_from_native(int mask) { assert(false); }
static void vm_apply_ui_scale_from_json(cJSON *act, bool *persist, int *mask) {}
static struct page *r03_checked_page(int slot, int type, int subtype, int vars) {
    return slot == 1 && type == STRUCT_PAGE ? &character : NULL;
}
static int vm_dd_clamp(double n, int low, int high) { return n < low ? low : n > high ? high : (int)n; }

%s

int main(void) {
    assert(!vm_execute_r02_action("{\"r02CharEdit\":{\"target\":2,\"lv\":42}}"));
    assert(character.values[1].i == 1);
    assert(vm_execute_r02_action("{\"r02CharEdit\":{\"target\":1,\"lv\":42}}"));
    assert(character.values[1].i == 42);
    return 0;
}
"""


HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef union vm_value { int i; } value;
enum { ARRAY_PAGE = 1, STRUCT_PAGE, DELEGATE_PAGE };
struct page { int type, nr_vars; struct { int rank; } array; value values[60]; };
struct sact_sprite { bool visible; };
static struct page sprites = { ARRAY_PAGE, 2, { 1 }, {{0}, {7}} };
static struct page widget, busho, busho_id;
static struct page battle_widgets = { ARRAY_PAGE, 1, { 1 }, {{9}} };
static struct page card_widgets = { ARRAY_PAGE, 1, { 1 }, {{10}} };
static struct page card_owner, card_list, card, status, delegate = { DELEGATE_PAGE };
static struct sact_sprite hp = { true };
static int s_r02_stat_sprites_glob = 1, s_r02_fn_disp_stat = 2, s_sr_fn_set_number = 3;
static int s_sr_glob_battle_place = 4, s_sr_fn_set_battle_hp = 5, s_sr_fn_get_busho_no = 7;
static int s_r08_battle_chara_glob = 20, s_r08_battle_chara_type = 21, s_r08_fn_update_battle_card = 22;
static int s_r09_party_glob = 23, s_r09_party_card_type = 24, s_r09_fn_update_party_card = 25;
static int s_r09_system_bar_delegate = 26, s_r09_fn_update_system_bar = 27;
static bool dg_callbegin_pending, already_on_stack;
static int pushed[2], count, calls, function, receiver, live_gold, shown_gold;
static bool widget_initialized, busho_visible, busho_destroyed;
static int card_calls, card_function, unit_number;
static bool card_visible;
enum {
    LOCK_R08_INF_GOLD = 1, LOCK_R08_MAGNUM_MAX = 2, LOCK_R08_ALLY_FULL_HP = 4, LOCK_R08_INF_SKILLS = 8,
    LOCK_R09_INF_RESOURCES = 1, LOCK_R09_ALLY_GOD = 2, LOCK_R09_INF_ACTION = 4,
    R09_REFRESH_CARDS = 1, R09_REFRESH_SYSTEM_BAR = 2,
};
static bool s_r08_compatible = true, s_r09_compatible = true, s_r08_applying, s_r09_applying;
static int s_r08_gold_glob = -1, s_r08_mag_glob = -1, s_r08_magmax_glob = -1;
static int s_r08_pta_glob, s_r08_sc_type = 28, s_r09_chara_type = 29;
static struct page character_indices = { ARRAY_PAGE, 2, {1}, {{10}, {10}} };
static struct page party_indices = { ARRAY_PAGE, 2, {1}, {{0}, {1}} };
static struct page resources = { ARRAY_PAGE, 4, {1} };
static struct page params = { ARRAY_PAGE, 22, {1} };
static struct { struct page *page; } heap[] = {{0}};
static bool vm_globals_ready(void) { return true; }
static struct page *r08_get_c_array(void) { return &character_indices; }
static struct page *r08_get_pt_array(int idx) { return &party_indices; }
static void r08_replenish_chara_skills(struct page *page) { assert(false); }
static struct page *r09_get_save_data_arr(void) { return &resources; }
static struct page *r09_get_chara_array(void) { return &character_indices; }
static struct page *r09_get_chara_params(struct page *page) { return &params; }
static struct page *r09_unwrap_arr_bool(int slot) { assert(false); return 0; }

static bool vm_callstack_has_fno(int fno) { return already_on_stack && fno == 2; }
static void stack_push(int arg) { assert(count < 2); pushed[count++] = arg; }
static void vm_call(int fno, int slot) {
    assert(!dg_callbegin_pending && count == 2);
    ++calls;
    function = fno;
    receiver = slot;
    count = 0;
}
static struct page *vm_get_global_page_idx(int idx) {
    if (idx == 20 || idx == 23) return &card_owner;
    if (idx == 26) return &delegate;
    return idx == 1 ? &sprites : idx == 4 ? &battle_widgets : 0;
}
static struct sact_sprite *sact_try_get_sprite(int id) { return id == 7 ? &hp : 0; }
static bool sprite_get_show(struct sact_sprite *sprite) { return sprite->visible; }
static value *vm_get_struct_member(struct page *page, const char *name, const char *unused) {
    static value slot = {8};
    return (page == &widget && !strcmp(name, "m_sGold"))
        || (page == &busho && !strcmp(name, "m_sHp")) ? &slot : 0;
}
static struct page *vm_safe_get_page(int slot) {
    return slot == 8 ? &widget : slot == 9 ? &busho : 0;
}
static struct page *vm_get_field_page(struct page *page, const char *name, const char *unused) {
    if (page == &busho && !strcmp(name, "m_sBushoNo")) return &busho_id;
    if (page == &battle_widgets && !strcmp(name, "m_asMikaBusho")) return &battle_widgets;
    if (page == &card_owner && !strcmp(name, "m_sList")) return &card_list;
    if (page == &card_owner && !strcmp(name, "m_asChara")) return &card_widgets;
    if (page == &card_list && !strcmp(name, "m_asCard")) return &card_widgets;
    if (page == &card && !strcmp(name, "m_sCard")) return &status;
    if (page == &status && !strcmp(name, "m_sHpBar")) return &status;
    return 0;
}
static int vm_get_field_int(struct page *page, const char *name, const char *unused, int fallback) {
    if (page == &card_list || page == &card || page == &status) {
        if (!strcmp(name, "m_bShow")) return card_visible;
        if (!strcmp(name, "m_nNo") || !strcmp(name, "m_nUnitNo")) return unit_number;
        if (!strcmp(name, "m_nMax")) return 355;
    }
    if (page == &busho_id) return !strcmp(name, "m_nCharaNo") ? 2003 : 0;
    if (page == &busho) {
        if (!strcmp(name, "m_bBattle")) return 1;
        if (!strcmp(name, "m_bDestroy")) return busho_destroyed;
        return busho_visible;
    }
    if (page != &widget) return fallback;
    if (!strcmp(name, "m_bShow")) return widget_initialized;
    return !strcmp(name, "m_bActive") ? widget_initialized : shown_gold;
}
static value global_get(int idx) { assert(idx == 6); return (value){live_gold}; }
static int vm_dd_call_int_0(int fno, int slot, int fallback) {
    if (fno == 22) {
        assert(slot == 10);
        card_function = fno;
        ++card_calls;
        return 1;
    }
    assert(fno == 7 && slot == 9);
    return 11;
}
static void vm_dd_call_void_0(int fno, int slot) {
    assert(slot == 10);
    card_function = fno;
    ++card_calls;
}
static struct page *r03_checked_page(int slot, int type, int subtype, int vars) {
    assert(slot == 10 && type == STRUCT_PAGE && (subtype == 21 || subtype == 24 || subtype == 28 || subtype == 29));
    return &card;
}
static struct page *sr_get_chara_page(int faction, int index) {
    assert(faction == 0 && index == 11);
    return &busho;
}
static bool scene_is_dirty, dirty_on_swap;
static int rendered;
static void handle_events(void) {}
static void sprite_call_plugins(void) {}
static void scene_render(void) { ++rendered; }
static void modifier_on_frame(void) { scene_is_dirty |= dirty_on_swap; }
static void gfx_swap(void) { modifier_on_frame(); }
static void gfx_mark_frame_presented(void) {}
static void gfx_clear_frame_presented(void) {}
static uint32_t SDL_GetTicks(void) { return 100; }

%s

int main(void) {
    hp.visible = false;
    r02_refresh_hud();
    assert(calls == 0);
    hp.visible = true;
    already_on_stack = true;
    r02_refresh_hud();
    assert(calls == 0 && count == 0);
    already_on_stack = false;
    dg_callbegin_pending = true;
    r02_refresh_hud();
    assert(calls == 1 && function == 2 && receiver == -1);
    assert(pushed[0] == 1 && pushed[1] == 0 && dg_callbegin_pending);

    live_gold = 103000;
    shown_gold = 3000;
    sr_refresh_number(&widget, "m_sGold", 6);
    assert(calls == 1);
    widget_initialized = true;
    sr_refresh_number(&widget, "m_sGold", 6);
    assert(calls == 2 && function == 3 && receiver == 8);
    assert(pushed[0] == 103000 && pushed[1] == 0 && dg_callbegin_pending);
    shown_gold = live_gold;
    sr_refresh_number(&widget, "m_sGold", 6);
    assert(calls == 2);

    sr_refresh_battle_hp(true);
    assert(calls == 2);
    busho_visible = true;
    sr_refresh_battle_hp(true);
    assert(calls == 3 && function == 5 && receiver == 8);
    assert(pushed[0] == 0 && pushed[1] == 11);
    busho_destroyed = true;
    sr_refresh_battle_hp(true);
    assert(calls == 3);

    scene_is_dirty = dirty_on_swap = true;
    sact_Update();
    assert(rendered == 1 && scene_is_dirty);
    dirty_on_swap = false;
    sact_Update();
    assert(rendered == 2 && !scene_is_dirty);

    r08_refresh_battle_cards();
    r09_refresh_party_cards();
    assert(card_calls == 0);
    unit_number = 1;
    r08_refresh_battle_cards();
    r09_refresh_party_cards();
    assert(card_calls == 0);
    card_visible = true;
    r08_refresh_battle_cards();
    assert(card_calls == 1 && card_function == 22);
    r09_refresh_party_cards();
    assert(card_calls == 2 && card_function == 25);
    r09_refresh_system_bar();
    assert(calls == 3);
    delegate.nr_vars = 3;
    r09_refresh_system_bar();
    assert(calls == 4 && function == 27 && receiver == -1);
    assert(pushed[0] == 0 && pushed[1] == 0);

    card.values[19].i = 12;
    card.values[20].i = 355;
    assert(vm_enforce_r08_locks(LOCK_R08_ALLY_FULL_HP));
    assert(card.values[19].i == 355);
    assert(!vm_enforce_r08_locks(LOCK_R08_ALLY_FULL_HP));
    params.values[10].i = 20;
    params.values[11].i = 55;
    assert(vm_enforce_r09_locks(LOCK_R09_ALLY_GOD | LOCK_R09_INF_RESOURCES)
            == (R09_REFRESH_CARDS | R09_REFRESH_SYSTEM_BAR));
    assert(params.values[10].i == 55 && resources.values[1].i == 999999);
    assert(vm_enforce_r09_locks(LOCK_R09_ALLY_GOD | LOCK_R09_INF_RESOURCES) == 0);
    return 0;
}
"""


class ModifierHudTests(unittest.TestCase):
    def test_rance02_level_edit_requests_hud_refresh_for_valid_character(self):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "r02_action.c"
            binary = Path(tmp) / "r02_action"
            source.write_text(R02_ACTION_HARNESS % extract_function("src/modifier.c", "vm_execute_r02_action"),
                              encoding="utf-8")
            subprocess.run(["cc", "-std=c11", "-pthread", "-I", str(root / "include"),
                            str(source), str(root / "src" / "cJSON.c"), "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_hud_refresh_only_calls_initialized_widgets_with_game_arguments(self):
        functions = "\n".join(
            extract_function("src/modifier.c", name)
            for name in (
                "vm_call_hud_setter", "r02_refresh_hud", "sr_refresh_number",
                "sr_battle_widgets", "sr_is_active_busho", "sr_get_busho_index",
                "sr_refresh_battle_hp",
                "r08_refresh_battle_cards", "r09_refresh_party_cards", "r09_refresh_system_bar",
                "vm_enforce_r08_locks", "vm_enforce_r09_locks",
            )
        ) + "\n" + extract_function("src/hll/SACT2.c", "sact_Update")
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "hud.c"
            binary = Path(tmp) / "hud"
            source.write_text(HARNESS % functions, encoding="utf-8")
            subprocess.run(["cc", "-std=c11", "-o", str(binary), str(source)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
