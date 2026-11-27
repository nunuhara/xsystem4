"""Host native regression tests for Daiakuji System 3.5 modifier."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO_ROOT = Path(__file__).resolve().parents[3]
XSYS35_SRC = REPO_ROOT / "xsystem35" / "src"


def _sdl2_flags():
    cflags = subprocess.check_output(["pkg-config", "--cflags", "sdl2"], text=True).split()
    libs = subprocess.check_output(["pkg-config", "--libs", "sdl2"], text=True).split()
    return cflags, libs


class DaiakujiModifierRegressions(unittest.TestCase):
    def test_daiakuji_modifier_sjis_and_gbk_real_c(self):
        cflags, libs = _sdl2_flags()
        harness = r"""
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "nact.h"
#include "utfsjis.h"
#include "variable.h"
#include "escalayer_modifier.h"

static uint32_t test_ticks = 1000;
uint32_t test_SDL_GetTicks(void) { return test_ticks; }

NACTINFO nact_data;
NACTINFO *nact = &nact_data;

void sys_error(char *format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fprintf(stderr, "\n");
	exit(1);
}

void sys_message(int level, char *format, ...) {
	(void)level;
	(void)format;
}

/* Real Daiakuji System39.ain variable indices */
enum {
	V_FX_PNT      = 110,
	V_REGION_NO   = 111,
	V_ORG_NO      = 112,
	V_UNIT_NO     = 113,
	V_ITEM_NO     = 116,
	V_SIT         = 127,
	V_R_SECURITY  = 144,
	V_G_UNIT_CNT  = 155,
	V_G_UNIT_MAX  = 156,
	V_B_CTRL      = 160,
	V_B_LOYALTY   = 164,
	V_B_HP        = 170,
	V_B_HP_MAX    = 171,
	V_I_EQUIP     = 214,
	V_GOLD        = 238,
	V_DEBT        = 334,
	V_MONTHLY     = 335,
	V_SUBSIDY     = 337,
	V_OFFICE_COST = 338,
	V_LOVE_ENUM   = 341,
	V_SCRATCH_HP  = 355,
	V_EX_DIG_CNT  = 391,
	V_POPULAR     = 446,
	DA_VARNUM     = 500,
};

static char *var_table[DA_VARNUM];

static void setup_var_table(bool use_gbk) {
	for (int i = 0; i < DA_VARNUM; i++)
		var_table[i] = "";

	/* ASCII variables are identical in JP Shift-JIS and CN GBK */
	var_table[V_SIT] = "SIT";
	var_table[V_GOLD] = "GOLD";

	if (use_gbk) {
		/* Exact GBK bytes from shipping Daiakuji CN System39.ain */
		var_table[V_R_SECURITY]  = "R\xd6\xce\xb0\xb2\xa5\xec\xa5\xd9\xa5\xeb";
		var_table[V_G_UNIT_CNT]  = "G\xb2\xbf\xcf\xc2\xca\xfd";
		var_table[V_G_UNIT_MAX]  = "G\xb2\xbf\xcf\xc2\xd7\xee\xb4\xf3\xca\xfd";
		var_table[V_B_CTRL]      = "B\xd6\xc6\xd3\xf9";
		var_table[V_B_LOYALTY]   = "B\xd6\xd2\xd5\x5c\xd0\xc4";
		var_table[V_B_HP]        = "B\xcc\xe5\xc1\xa6";
		var_table[V_B_HP_MAX]    = "B\xd7\xee\xb4\xf3\xcc\xe5\xc1\xa6";
		var_table[V_I_EQUIP]     = "I\xd7\xb0\x82\xe4";
		var_table[V_DEBT]        = "\xa1\xee\xbd\xe8\xbd\xf0";
		var_table[V_MONTHLY]     = "\xa1\xee\xd4\xc2\x92\x42\xa4\xa4";
		var_table[V_SUBSIDY]     = "\xa1\xee\xd1\x61\xd6\xfa\xbd\xf0";
		var_table[V_OFFICE_COST] = "\xa1\xee\xbd\xf0\xa3\xaf\xca\xc2\x84\xd5\xcb\xf9";
		var_table[V_LOVE_ENUM]   = "\xa1\xef\x90\xdb";
		var_table[V_SCRATCH_HP]  = "\xa1\xef\xa3\xc8\xa3\xd0";
		var_table[V_EX_DIG_CNT]  = "\xa3\xc5\xa3\xd8\xa5\xa2\xa5\xa4\xa5\xc6\xa5\xe0";
		var_table[V_POPULAR]     = "\xa1\xee\xa5\xae\xa5\xe3\xa5\xeb\xa5\xd0\xa9\x60\xa5\xac\xa9\x60\xc8\xcb\x9a\xdd";
	} else {
		/* Shift-JIS bytes from original JP Daiakuji System39.ain */
		var_table[V_R_SECURITY]  = "R\x8e\xa1\x88\xc0\x83\x8c\x83\x78\x83\x8b";
		var_table[V_G_UNIT_CNT]  = "G\x95\x94\x89\xba\x90\x94";
		var_table[V_G_UNIT_MAX]  = "G\x95\x94\x89\xba\x8d\xc5\x91\xe5\x90\x94";
		var_table[V_B_CTRL]      = "B\x90\xa7\x8c\xe4";
		var_table[V_B_LOYALTY]   = "B\x92\x89\x90\xbd\x90\x53";
		var_table[V_B_HP]        = "B\x91\xcc\x97\xcd";
		var_table[V_B_HP_MAX]    = "B\x8d\xc5\x91\xe5\x91\xcc\x97\xcd";
		var_table[V_I_EQUIP]     = "I\x91\x95\x94\xf5";
		var_table[V_DEBT]        = "\x81\x99\x8e\xd8\x8b\xe0";
		var_table[V_MONTHLY]     = "\x81\x99\x8c\x8e\x95\xa5\x82\xa2";
		var_table[V_SUBSIDY]     = "\x81\x99\x95\xe2\x8f\x95\x8b\xe0";
		var_table[V_OFFICE_COST] = "\x81\x99\x8b\xe0\x81\x5e\x8e\x96\x96\xb1\x8f\x8a";
		var_table[V_LOVE_ENUM]   = "\x81\x9a\x88\xa4";
		var_table[V_SCRATCH_HP]  = "\x81\x9a\x82\x67\x82\x6f";
		var_table[V_EX_DIG_CNT]  = "\x82\x64\x82\x77\x83\x41\x83\x43\x83\x65\x83\x80";
		var_table[V_POPULAR]     = "\x81\x99\x83\x4d\x83\x83\x83\x8b\x83\x6f\x81\x5b\x83\x4b\x81\x5b\x90\x6c\x8b\x43";
	}
}

static void run_suite(bool use_gbk) {
	v_reset();
	memset(&nact_data, 0, sizeof(nact_data));
	escalayer_modifier_reset(true);

	setup_var_table(use_gbk);
	nact->ain.varnum = DA_VARNUM;
	nact->ain.var = var_table;
	v_set_names(DA_VARNUM, var_table);

	assert(escalayer_modifier_available());
	assert(strcmp(xsystem35_modifier_current_game(), "daiakuji") == 0);

	/* Allocate pages matching Daiakuji SCO055.SCO (page 0) */
	assert(v_allocatePage(17, 51, false));   /* SIT */
	assert(v_allocatePage(34, 39, true));    /* R治安レベル */
	assert(v_allocatePage(54, 11, true));    /* G部下数 */
	assert(v_allocatePage(55, 11, true));    /* G部下最大数 */
	assert(v_allocatePage(64, 471, true));   /* B制御 */
	assert(v_allocatePage(68, 471, true));   /* B忠誠心 */
	assert(v_allocatePage(74, 471, true));   /* B体力 */
	assert(v_allocatePage(75, 471, true));   /* B最大体力 */
	assert(v_allocatePage(132, 51, true));   /* I装備 */

	assert(v_bindArray(V_SIT,        &sysVar[V_FX_PNT],    0, 17));
	assert(v_bindArray(V_R_SECURITY, &sysVar[V_REGION_NO], 0, 34));
	assert(v_bindArray(V_G_UNIT_CNT, &sysVar[V_ORG_NO],    0, 54));
	assert(v_bindArray(V_G_UNIT_MAX, &sysVar[V_ORG_NO],    0, 55));
	/* Deliberately simulate 組織設定 pointing DS offset at Enemy Org 2 (offset=60) */
	assert(v_bindArray(V_B_CTRL,     &sysVar[V_UNIT_NO],  60, 64));
	assert(v_bindArray(V_B_LOYALTY,  &sysVar[V_UNIT_NO],  60, 68));
	assert(v_bindArray(V_B_HP,       &sysVar[V_UNIT_NO],  60, 74));
	assert(v_bindArray(V_B_HP_MAX,   &sysVar[V_UNIT_NO],  60, 75));
	assert(v_bindArray(V_I_EQUIP,    &sysVar[V_ITEM_NO],   0, 132));

	/* Initial game state */
	sysVar[V_GOLD]        = 50;   /* 500万 starting cash */
	sysVar[V_OFFICE_COST] = 10;   /* 100万 monthly office maintenance expense */
	sysVar[V_DEBT]        = 500;
	sysVar[V_MONTHLY]     = 42;
	sysVar[V_SUBSIDY]     = 10;
	sysVar[V_LOVE_ENUM]   = 1;    /* Minhua heroine route */
	sysVar[V_SCRATCH_HP]  = 0;
	sysVar[V_EX_DIG_CNT]  = 0;
	sysVar[V_POPULAR]     = 5;

	/* Org 1 (player): 3 slots used (2 active allies, 1 deleted slot) */
	varPage[54].value[1] = 3;
	varPage[55].value[1] = 60;
	varPage[64].value[1] = 1; varPage[74].value[1] = 80; varPage[75].value[1] = 120; varPage[68].value[1] = 50;
	varPage[64].value[2] = 1; varPage[74].value[2] = 60; varPage[75].value[2] = 100; varPage[68].value[2] = 15;
	varPage[64].value[3] = 0; varPage[74].value[3] = 0;  varPage[75].value[3] = 0;   varPage[68].value[3] = 0;
	/* Org 2 (enemy) starts at index 61 — must never be buffed */
	varPage[64].value[61] = 1; varPage[74].value[61] = 90; varPage[75].value[61] = 90; varPage[68].value[61] = 40;

	/* Regions degraded to 普通/恶化/无法 (1..3) */
	for (int i = 1; i <= 37; i++)
		varPage[34].value[i] = (vmvar_t)((i % 3) + 1);

	/* Items: 1..48 mostly unowned (99), item 3 equipped on ally 1, item 19 in inventory (0) */
	for (int i = 1; i <= 48; i++) {
		varPage[132].value[i] = 99;
		varPage[17].value[i] = 0;
	}
	varPage[132].value[3] = 1;
	varPage[132].value[19] = 0;

	/* 1. da_gold_add=10000 adds to GOLD, NOT ☆金／事務所 */
	xsystem35_modifier_apply_action("da_gold_add=10000");
	test_ticks += 300;
	escalayer_modifier_tick();
	assert(sysVar[V_GOLD] == 10050);
	assert(sysVar[V_OFFICE_COST] == 10);

	/* 2. da_gold_max sets GOLD to 60000, leaves ☆金／事務所 alone */
	xsystem35_modifier_apply_action("da_gold_max");
	test_ticks += 300;
	escalayer_modifier_tick();
	assert(sysVar[V_GOLD] == 60000);
	assert(sysVar[V_OFFICE_COST] == 10);

	/* 3. Lock bit 0 keeps GOLD >= 50000, leaves ☆金／事務所 alone */
	sysVar[V_GOLD] = 123;
	xsystem35_modifier_set_locks(1 << 0);
	test_ticks += 300;
	escalayer_modifier_tick();
	assert(sysVar[V_GOLD] == 50000);
	assert(sysVar[V_OFFICE_COST] == 10);

	/* 4. da_debt_zero clears BOTH ☆借金 and ☆月払い */
	xsystem35_modifier_set_locks(0);
	xsystem35_modifier_apply_action("da_debt_zero");
	test_ticks += 300;
	escalayer_modifier_tick();
	assert(sysVar[V_DEBT] == 0);
	assert(sysVar[V_MONTHLY] == 0);

	/* Lock bit 1 also zeroes debt + monthly payment */
	sysVar[V_DEBT] = 250;
	sysVar[V_MONTHLY] = 20;
	xsystem35_modifier_set_locks(1 << 1);
	test_ticks += 300;
	escalayer_modifier_tick();
	assert(sysVar[V_DEBT] == 0);
	assert(sysVar[V_MONTHLY] == 0);

	/* 5. Lock bit 2 heals Org 1 allies to B最大体力 without raising max or touching enemies */
	xsystem35_modifier_set_locks(1 << 2);
	test_ticks += 300;
	escalayer_modifier_tick();
	assert(varPage[74].value[1] == 120 && varPage[75].value[1] == 120);
	assert(varPage[74].value[2] == 100 && varPage[75].value[2] == 100);
	assert(varPage[74].value[3] == 0   && varPage[75].value[3] == 0);
	assert(varPage[74].value[61] == 90 && varPage[75].value[61] == 90);
	assert(sysVar[V_SCRATCH_HP] == 0);

	/* 6. da_hp_boost boosts active Org 1 allies to 999/999 even with enemy DS offset */
	xsystem35_modifier_set_locks(0);
	xsystem35_modifier_apply_action("da_hp_boost");
	test_ticks += 300;
	escalayer_modifier_tick();
	assert(varPage[74].value[1] == 999 && varPage[75].value[1] == 999);
	assert(varPage[74].value[2] == 999 && varPage[75].value[2] == 999);
	assert(varPage[74].value[3] == 0   && varPage[75].value[3] == 0);
	assert(varPage[74].value[61] == 90 && varPage[75].value[61] == 90);

	/* 7. da_love_max + lock bit 3 max Org 1 B忠誠心 (99) & ☆ギャルバーガー人気 (100), never corrupt ★愛 */
	xsystem35_modifier_apply_action("da_love_max");
	test_ticks += 300;
	escalayer_modifier_tick();
	assert(varPage[68].value[1] == 99);
	assert(varPage[68].value[2] == 99);
	assert(varPage[68].value[3] == 0);
	assert(varPage[68].value[61] == 40);
	assert(sysVar[V_POPULAR] == 100);
	assert(sysVar[V_LOVE_ENUM] == 1);

	varPage[68].value[1] = 40;
	sysVar[V_POPULAR] = 10;
	xsystem35_modifier_set_locks(1 << 3);
	test_ticks += 300;
	escalayer_modifier_tick();
	assert(varPage[68].value[1] == 99);
	assert(sysVar[V_POPULAR] == 100);
	assert(sysVar[V_LOVE_ENUM] == 1);
	xsystem35_modifier_set_locks(0);

	/* 8. da_security_max sets R治安レベル[1..37] to 0 (良好), NOT 100 (无法) */
	xsystem35_modifier_apply_action("da_security_max");
	test_ticks += 300;
	escalayer_modifier_tick();
	for (int i = 1; i <= 37; i++)
		assert(varPage[34].value[i] == 0);

	/* 9. da_items_unlock unlocks unowned items (99 -> 0), keeps equipped, marks SIT = 1, ignores ＥＸアイテム */
	xsystem35_modifier_apply_action("da_items_unlock");
	test_ticks += 300;
	escalayer_modifier_tick();
	assert(varPage[132].value[3] == 1);
	assert(varPage[132].value[19] == 0);
	for (int i = 1; i <= 48; i++) {
		if (i != 3)
			assert(varPage[132].value[i] == 0);
		assert(varPage[17].value[i] == 1);
	}
	assert(sysVar[V_EX_DIG_CNT] == 0);

	/* 10. Live state JSON */
	test_ticks += 300;
	escalayer_modifier_tick();
	char *json = xsystem35_modifier_live_state();
	assert(json != NULL);
	cJSON *root = cJSON_Parse(json);
	assert(root != NULL);
	assert(strcmp(cJSON_GetObjectItem(root, "game")->valuestring, "daiakuji") == 0);
	assert(cJSON_GetObjectItem(root, "gold")->valueint == 50000);
	assert(cJSON_GetObjectItem(root, "debt")->valueint == 0);
	assert(cJSON_GetObjectItem(root, "subsidy")->valueint == 10);
	assert(cJSON_GetObjectItem(root, "popular")->valueint == 100);
	assert(cJSON_GetObjectItem(root, "hp")->valueint == 999);
	assert(cJSON_GetObjectItem(root, "locks")->valueint == 0);
	cJSON_Delete(root);
	free(json);
}

int main(void) {
	v_init();
	run_suite(false); /* JP Shift-JIS */
	run_suite(true);  /* CN GBK */
	return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            inc_dir = tmp_path / "inc"
            inc_dir.mkdir()
            (inc_dir / "config.h").write_text("#define XSYSTEM35_SDL_VERSION 2\n")
            c_file = tmp_path / "test_daiakuji_modifier.c"
            c_file.write_text(harness)
            bin_file = tmp_path / "test_daiakuji_modifier"

            cmd = [
                os.environ.get("CC", "cc"),
                "-Wall",
                "-Wno-pointer-sign",
                f"-I{inc_dir}",
                f"-I{XSYS35_SRC}",
                *cflags,
                "-DSDL_GetTicks=test_SDL_GetTicks",
                str(c_file),
                str(XSYS35_SRC / "escalayer_modifier.c"),
                str(XSYS35_SRC / "variable.c"),
                str(XSYS35_SRC / "utfsjis.c"),
                str(XSYS35_SRC / "cJSON.c"),
                *libs,
                "-lpthread",
                "-lm",
                "-o",
                str(bin_file),
            ]
            subprocess.run(cmd, check=True)
            subprocess.run([str(bin_file)], check=True)

    def test_platform_labels_do_not_reference_office_gold(self):
        switch_menu = (REPO_ROOT / "cmake" / "switch" / "hub" / "runtime_menu_system35.c").read_text()
        self.assertNotIn("Max office gold", switch_menu)
        self.assertIn('"Max gold"', switch_menu)

        mod_src = (XSYS35_SRC / "escalayer_modifier.c").read_text()
        self.assertNotIn("☆金／事務所", mod_src)
        self.assertIn('find_var_names("GOLD", "GOLD")', mod_src)


if __name__ == "__main__":
    unittest.main()
