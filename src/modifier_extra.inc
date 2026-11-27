/*
 * Extended Live In-Game Modifiers for Translated Chinese System 4 Games
 * Included by modifier.c inside #ifndef MODIFIER_OFFLINE_ONLY.
 *
 * Every global/struct/member name and encryption formula below is verified
 * directly against the game's .ain schema and bytecode.
 */

#ifndef SYSTEM4_MODIFIER_EXTRA_INC
#define SYSTEM4_MODIFIER_EXTRA_INC

/* =========================================================================
 * Generic Per-Game Lock Mask Storage (persisted in modifier_config.json)
 * ========================================================================= */

#ifndef EX_MAX_GAMES
#define EX_MAX_GAMES 32
#endif

struct ex_game_lock_slot {
	const char *id;
	int mask;
	bool init;
};

static struct ex_game_lock_slot g_ex_locks[EX_MAX_GAMES] = {
	{ "rance06",         0, false },
	{ "evenicle2",       0, false },
	{ "evenicle2rance",  0, false },
	{ "daibanchou",      0, false },
	{ "daiteikoku",      0, false },
	{ "galzoo",          0, false },
	{ "toushin3",        0, false },
	{ "bladebriders",    0, false },
	{ "yorugakuru",      0, false },
	{ "waidonyo",        0, false },
	{ "sanbiki",         0, false },
	{ "dorapeko",        0, false },
	{ "haruurare",       0, false },
	{ "oyakorankan",     0, false },
	{ "tsumashibori",    0, false },
	{ "okusamajun",      0, false },
	{ "okusamantr",      0, false },
	{ "escalayerreboot", 0, false },
	{ "ixseal",          0, false },
	{ "pascha3",         0, false },
	{ "haharanman",      0, false },
	{ "tsumamigui3",     0, false },
	{ "maman",           0, false },
	{ NULL,              0, false },
};

static struct ex_game_lock_slot *ex_find_lock_slot(const char *game_id)
{
	if (!game_id || !game_id[0])
		return NULL;
	if (!strcmp(game_id, "escalayer_reboot"))
		game_id = "escalayerreboot";
	for (int i = 0; i < EX_MAX_GAMES && g_ex_locks[i].id; i++) {
		if (!strcmp(g_ex_locks[i].id, game_id))
			return &g_ex_locks[i];
	}
	return NULL;
}

static bool ex_all_locks_initialized_locked(void)
{
	for (int i = 0; i < EX_MAX_GAMES && g_ex_locks[i].id; i++) {
		if (!g_ex_locks[i].init)
			return false;
	}
	return true;
}

static void ex_reset_locks_locked(void)
{
	for (int i = 0; i < EX_MAX_GAMES && g_ex_locks[i].id; i++) {
		g_ex_locks[i].mask = 0;
		g_ex_locks[i].init = false;
	}
}

static bool ex_is_active_extra_game(void)
{
	return game_rance6 || game_evenicle2 || game_evenicle2_rance ||
	       game_daibanchou || game_daiteikoku || game_galzoo ||
	       game_toushin3 || game_bladebriders || game_night_remaster ||
	       game_waidonyo || game_sanbiki || game_dorapeko ||
	       game_haruurare || game_oyakorankan || game_tsumashibori ||
	       game_okusama || game_escalayer_reboot || game_ixseal ||
	       game_pascha3 || game_haharanman || game_tsumamigui3 ||
	       game_maman;
}

static const char *ex_current_game_id(void)
{
	if (game_evenicle2_rance) return "evenicle2rance";
	if (game_evenicle2) return "evenicle2";
	if (game_rance6) return "rance06";
	if (game_daibanchou) return "daibanchou";
	if (game_daiteikoku) return "daiteikoku";
	if (game_galzoo) return "galzoo";
	if (game_toushin3) return "toushin3";
	if (game_bladebriders) return "bladebriders";
	if (game_night_remaster) return "yorugakuru";
	if (game_waidonyo) return "waidonyo";
	if (game_sanbiki) return "sanbiki";
	if (game_dorapeko) return "dorapeko";
	if (game_haruurare) return "haruurare";
	if (game_oyakorankan) return "oyakorankan";
	if (game_tsumashibori) return "tsumashibori";
	if (game_okusama_ntr) return "okusamantr";
	if (game_okusama) return "okusamajun";
	if (game_escalayer_reboot) return "escalayerreboot";
	if (game_ixseal) return "ixseal";
	if (game_pascha3) return "pascha3";
	if (game_haharanman) return "haharanman";
	if (game_tsumamigui3) return "tsumamigui3";
	if (game_maman) return "maman";
	return "";
}

static int ex_get_lock_mask_locked(const char *game_id)
{
	struct ex_game_lock_slot *slot = ex_find_lock_slot(game_id);
	return slot ? slot->mask : 0;
}

static void ex_snapshot_locks_locked(int *out_masks, bool *out_init)
{
	for (int i = 0; i < EX_MAX_GAMES; i++) {
		out_masks[i] = g_ex_locks[i].mask;
		out_init[i] = g_ex_locks[i].init;
	}
}

static void ex_load_locks_from_json_locked(cJSON *root)
{
	if (!root)
		return;
	cJSON *map = cJSON_GetObjectItemCaseSensitive(root, "extra_locks");
	if (!map || !cJSON_IsObject(map))
		return;
	for (int i = 0; i < EX_MAX_GAMES && g_ex_locks[i].id; i++) {
		if (g_ex_locks[i].init)
			continue;
		cJSON *v = cJSON_GetObjectItemCaseSensitive(map, g_ex_locks[i].id);
		if (!v && !strcmp(g_ex_locks[i].id, "escalayerreboot"))
			v = cJSON_GetObjectItemCaseSensitive(map, "escalayer_reboot");
		if (v && cJSON_IsNumber(v)) {
			g_ex_locks[i].mask = v->valueint;
			g_ex_locks[i].init = true;
		}
	}
}

static bool ex_locks_differ_from_json(cJSON *root, const int *snap_masks, const bool *snap_init)
{
	cJSON *map = root ? cJSON_GetObjectItemCaseSensitive(root, "extra_locks") : NULL;
	for (int i = 0; i < EX_MAX_GAMES && g_ex_locks[i].id; i++) {
		if (!snap_init[i])
			continue;
		cJSON *v = (map && cJSON_IsObject(map))
			? cJSON_GetObjectItemCaseSensitive(map, g_ex_locks[i].id)
			: NULL;
		int disk_v = (v && cJSON_IsNumber(v)) ? v->valueint : 0;
		if (!v && snap_masks[i] == 0)
			continue;
		if (disk_v != snap_masks[i])
			return true;
	}
	return false;
}

static void ex_save_locks_to_json(cJSON *root, const int *snap_masks, const bool *snap_init)
{
	if (!root)
		return;
	cJSON *map = cJSON_GetObjectItemCaseSensitive(root, "extra_locks");
	if (map && !cJSON_IsObject(map)) {
		cJSON_DeleteItemFromObjectCaseSensitive(root, "extra_locks");
		map = NULL;
	}
	for (int i = 0; i < EX_MAX_GAMES && g_ex_locks[i].id; i++) {
		if (!snap_init[i])
			continue;
		if (!map) {
			if (snap_masks[i] == 0)
				continue;
			map = cJSON_CreateObject();
			if (!map)
				return;
			cJSON_AddItemToObject(root, "extra_locks", map);
		}
		cJSON_DeleteItemFromObjectCaseSensitive(map, g_ex_locks[i].id);
		if (!strcmp(g_ex_locks[i].id, "escalayerreboot"))
			cJSON_DeleteItemFromObjectCaseSensitive(map, "escalayer_reboot");
		if (snap_masks[i] != 0)
			cJSON_AddNumberToObject(map, g_ex_locks[i].id, snap_masks[i]);
	}
}

/* =========================================================================
 * Low-Level VM / Encrypted Wrapper Helpers
 * ========================================================================= */

static int32_t ex_get_global_int(const char *n1, const char *n2, int32_t def)
{
	if (!vm_globals_ready())
		return def;
	int idx = vm_find_global_idx(n1, n2);
	if (idx < 0 || idx >= heap[0].page->nr_vars)
		return def;
	return heap[0].page->values[idx].i;
}

static bool ex_set_global_int(const char *n1, const char *n2, int32_t val)
{
	if (!vm_globals_ready())
		return false;
	int idx = vm_find_global_idx(n1, n2);
	if (idx < 0 || idx >= heap[0].page->nr_vars)
		return false;
	heap[0].page->values[idx].i = val;
	return true;
}

static bool ex_get_ref_int_at(struct page *p, int slot_idx, int32_t *out)
{
	if (!p || p->type != STRUCT_PAGE || slot_idx < 0 || slot_idx + 1 >= p->nr_vars)
		return false;
	struct page *target = vm_safe_get_page(p->values[slot_idx].i);
	int32_t var_idx = p->values[slot_idx + 1].i;
	if (!target || var_idx < 0 || var_idx >= target->nr_vars)
		return false;
	if (out)
		*out = target->values[var_idx].i;
	return true;
}

static bool ex_set_ref_int_at(struct page *p, int slot_idx, int32_t val)
{
	if (!p || p->type != STRUCT_PAGE || slot_idx < 0 || slot_idx + 1 >= p->nr_vars)
		return false;
	struct page *target = vm_safe_get_page(p->values[slot_idx].i);
	int32_t var_idx = p->values[slot_idx + 1].i;
	if (!target || var_idx < 0 || var_idx >= target->nr_vars)
		return false;
	target->values[var_idx].i = val;
	return true;
}

/* Mituki C_INT: [0]=m_nValue, [1]=m_nCode, [2]=m_nBackUpCode, [3]=m_nPattern.
 * key_seed == 0 or ValueEncryption HLL present => plaintext passthrough (pascha3).
 * key_seed != 0 => exact Mituki Encrytion01 (u ^ key) verified against bytecode:
 *   - escalayerreboot: unshuffled table, nPattern = 0 (nIndex == 1) -> m_nPattern = 0.
 *   - ixseal, tsumamigui3, maman: shuffled table where nPattern = 0 calls Decryption03
 *     (byte shuffle + XOR 0xFF0000FF -> negative -> clamped to 0), whereas Encrytion01/
 *     Decryption01 is nIndex == 8 (nPattern = 7) -> m_nPattern = 0x00070007 so both
 *     GetCodePattern() and GetBackUpCodePattern() return 7. */
#define EX_KEY_ESCALAYER_REBOOT ((uint32_t)-732065958)
#define EX_KEY_IXSEAL           ((uint32_t)-592035238)
#define EX_KEY_TSUMAMIGUI3      ((uint32_t)-1266931878)
#define EX_KEY_MAMAN            ((uint32_t)-1266931878)
#define EX_KEY_HAHARANMAN       ((uint32_t)-1792731796)
#define EX_KEY_PASCHA3          0u

static int32_t ex_cint_read(struct page *cint, int32_t def)
{
	if (!cint || cint->type != STRUCT_PAGE || cint->nr_vars < 1)
		return def;
	return cint->values[0].i;
}

static void ex_cint_write_xor(struct page *cint, int32_t val, uint32_t key_seed, int32_t xor_extra)
{
	if (!cint || cint->type != STRUCT_PAGE || cint->nr_vars < 3)
		return;
	if (key_seed == 0u || (ain && ain_get_library(ain, "ValueEncryption") >= 0)) {
		cint->values[0].i = val;
		cint->values[1].i = val;
		cint->values[2].i = val;
		return;
	}
	uint32_t u = (uint32_t)val;
	uint32_t key = key_seed ^ ((u << 8) | u) ^ 0xAAAAAAAAu;
	uint32_t enc = u ^ key;
	int32_t plain_pat = (key_seed == EX_KEY_IXSEAL ||
			     key_seed == EX_KEY_TSUMAMIGUI3 ||
			     key_seed == EX_KEY_MAMAN) ? 0x00070007 : 0;
	cint->values[0].i = val;
	cint->values[1].i = (int32_t)(enc ^ (uint32_t)xor_extra);
	cint->values[2].i = (int32_t)enc;
	if (cint->nr_vars > 3)
		cint->values[3].i = plain_pat;
}

static int32_t ex_field_cint_read(struct page *parent, const char *name, int32_t def)
{
	return ex_cint_read(vm_get_field_page(parent, name, NULL), def);
}

static void ex_field_cint_write(struct page *parent, const char *name, int32_t val, uint32_t key_seed)
{
	ex_cint_write_xor(vm_get_field_page(parent, name, NULL), val, key_seed, 0);
}

static void ex_cbool_write(struct page *cbool, bool b, uint32_t key_seed)
{
	if (!cbool || cbool->type != STRUCT_PAGE || cbool->nr_vars < 3)
		return;
	struct page *pkey = vm_safe_get_page(cbool->values[2].i);
	struct page *pnb  = vm_safe_get_page(cbool->values[1].i);
	if (!pkey || !pnb)
		return;
	cbool->values[0].i = b ? 1 : 0;
	int32_t kv = ex_cint_read(pkey, 0);
	ex_cint_write_xor(pnb, kv + (b ? 1 : 0), key_seed, 0);
}

/* haharanman CEncryptInt: [0]=m_nValue, [1]=wrap:encryptint::CParameter
 * CParameter: [0]=m_nValue, [1]=m_nBackup, [2]=m_nPattern, [3]=m_nXorKey */
static int32_t ex_hhr_read(struct page *parent, const char *name, int32_t def)
{
	struct page *ci = vm_get_field_page(parent, name, NULL);
	if (!ci || ci->type != STRUCT_PAGE || ci->nr_vars < 1)
		return def;
	return ci->values[0].i;
}

static void ex_hhr_write(struct page *parent, const char *name, int32_t val)
{
	struct page *ci = vm_get_field_page(parent, name, NULL);
	if (!ci || ci->type != STRUCT_PAGE || ci->nr_vars < 2)
		return;
	ci->values[0].i = val;
	struct page *param = vm_safe_get_page(ci->values[1].i);
	if (param && param->type == STRUCT_PAGE && param->nr_vars >= 4) {
		uint32_t u = (uint32_t)val;
		uint32_t key = EX_KEY_HAHARANMAN ^ ((u << 8) | u) ^ 0xAAAAAAAAu;
		int32_t enc = (int32_t)(u ^ key);
		param->values[0].i = enc;
		param->values[1].i = enc;
		param->values[2].i = 0;
		param->values[3].i = (int32_t)key;
	}
}

static char *ex_get_cstring_utf8(struct page *parent, const char *name)
{
	struct page *cs = vm_get_field_page(parent, name, NULL);
	if (!cs || cs->type != STRUCT_PAGE)
		return strdup("");
	return vm_dd_get_field_utf8(cs, "m_szString", NULL);
}

static int32_t ex_arr_int_get(struct page *arr, int idx, int32_t def)
{
	if (!arr || arr->type != ARRAY_PAGE || idx < 0 || idx >= arr->nr_vars)
		return def;
	return arr->values[idx].i;
}

static void ex_arr_int_set(struct page *arr, int idx, int32_t val)
{
	if (!arr || arr->type != ARRAY_PAGE || idx < 0 || idx >= arr->nr_vars)
		return;
	arr->values[idx].i = val;
}

static bool ex_json_int(cJSON *obj, const char *key, int32_t *out)
{
	if (!obj || !key)
		return false;
	cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
	if (it && cJSON_IsNumber(it)) {
		*out = (int32_t)it->valueint;
		return true;
	}
	return false;
}

static bool ex_json_cheat_is(cJSON *act, const char *name)
{
	if (!act || !name)
		return false;
	cJSON *it = cJSON_GetObjectItemCaseSensitive(act, "cheat");
	return it && cJSON_IsString(it) && it->valuestring && !strcmp(it->valuestring, name);
}

/* =========================================================================
 * 1. Rance 06 (rance06)
 * ========================================================================= */
#define R06_G_GOLD      "\x81\x9e\x82\x66\x82\x6e\x82\x6b\x82\x63" /* ◇ＧＯＬＤ */
#define R06_G_LUCK      "\x81\x9f\x90\xed\x93\xac\x81\x5e\x8a\x4a\x89\x5e" /* ◆戦闘／開運 */
#define R06_G_PARTY_MY  "\x96\xa1\x95\xfb\x83\x70\x81\x5b\x83\x65\x83\x42" /* 味方パーティ */
#define R06_G_PARTY_EN  "\x93\x47\x83\x70\x81\x5b\x83\x65\x83\x42" /* 敵パーティ */
#define R06_F_NAME      "\x96\xbc\x91\x4f" /* 名前 */
#define R06_F_LV        "\x82\x6b\x82\x75" /* ＬＶ */
#define R06_F_LV_MAX    "\x82\x6b\x82\x75\x8d\xc5\x91\xe5" /* ＬＶ最大 */
#define R06_F_HP        "\x91\xcc\x97\xcd" /* 体力 */
#define R06_F_HP_MAX    "\x91\xcc\x97\xcd\x8d\xc5\x91\xe5" /* 体力最大 */
#define R06_F_AP        "\x8a\x88\x93\xae\x97\xcd" /* 活動力 */
#define R06_F_AP_MAX    "\x8a\x88\x93\xae\x97\xcd\x8d\xc5\x91\xe5" /* 活動力最大 */
#define R06_F_EXP       "\x82\x64\x82\x77\x82\x6f" /* ＥＸＰ */
#define R06_F_PATK      "\x95\xa8\x97\x9d\x8d\x55\x8c\x82\x97\xcd" /* 物理攻撃力 */
#define R06_F_PDEF      "\x95\xa8\x97\x9d\x96\x68\x8c\xe4\x97\xcd" /* 物理防御力 */
#define R06_F_MATK      "\x96\x82\x96\x40\x8d\x55\x8c\x82\x97\xcd" /* 魔法攻撃力 */
#define R06_F_MDEF      "\x96\x82\x96\x40\x96\x68\x8c\xe4\x97\xcd" /* 魔法防御力 */
#define R06_F_HIT       "\x95\xa8\x97\x9d\x96\xbd\x92\x86\x97\xcd" /* 物理命中力 */
#define R06_F_EVA       "\x95\xa8\x97\x9d\x89\xf1\x94\xf0\x97\xcd" /* 物理回避力 */
#define R06_F_ITEM_CNT  "\x8c\xc2\x90\x94" /* 個数 */
#define R06_ENEMY_SLOT_BASE 51

static void ex_r06_populate(cJSON *root)
{
	struct page *gc = vm_get_global_page("gc", NULL);
	int n = vm_dd_array_len(gc);
	cJSON_AddBoolToObject(root, "inGame", n > 0);
	struct page *pe = vm_get_global_page(R06_G_PARTY_EN, NULL);
	struct page *pe_chr = vm_get_field_page(pe, "m_anChr", NULL);
	bool in_battle = false;
	for (int i = 0; i < vm_dd_array_len(pe_chr); i++) {
		int cno = ex_arr_int_get(pe_chr, i, 0);
		if (cno > 0 && (R06_ENEMY_SLOT_BASE + i) < n) { in_battle = true; break; }
	}
	cJSON_AddBoolToObject(root, "inBattle", in_battle);
	cJSON_AddNumberToObject(root, "gold", ex_get_global_int(R06_G_GOLD, NULL, 0));
	cJSON_AddNumberToObject(root, "luck", ex_get_global_int(R06_G_LUCK, NULL, 0));

	cJSON *chars = cJSON_CreateArray();
	int added = 0;
	for (int i = 1; i < n && i < R06_ENEMY_SLOT_BASE && added < 48; i++) {
		struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(gc, i));
		if (!c) continue;
		int lv = vm_get_field_int(c, R06_F_LV, NULL, 0);
		int mhp = vm_get_field_int(c, R06_F_HP_MAX, NULL, 0);
		char *nm = vm_dd_get_field_utf8(c, R06_F_NAME, NULL);
		if (lv <= 0 || mhp <= 0 || !nm[0]) { free(nm); continue; }
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddStringToObject(jc, "name", nm);
		free(nm);
		cJSON_AddNumberToObject(jc, "lv", lv);
		cJSON_AddNumberToObject(jc, "lvMax", vm_get_field_int(c, R06_F_LV_MAX, NULL, lv));
		cJSON_AddNumberToObject(jc, "hp", vm_get_field_int(c, R06_F_HP, NULL, 0));
		cJSON_AddNumberToObject(jc, "hpMax", mhp);
		cJSON_AddNumberToObject(jc, "ap", vm_get_field_int(c, R06_F_AP, NULL, 0));
		cJSON_AddNumberToObject(jc, "apMax", vm_get_field_int(c, R06_F_AP_MAX, NULL, 0));
		cJSON_AddNumberToObject(jc, "exp", vm_get_field_int(c, R06_F_EXP, NULL, 0));
		cJSON_AddNumberToObject(jc, "atk", vm_get_field_int(c, R06_F_PATK, NULL, 0));
		cJSON_AddNumberToObject(jc, "def", vm_get_field_int(c, R06_F_PDEF, NULL, 0));
		cJSON_AddNumberToObject(jc, "mag", vm_get_field_int(c, R06_F_MATK, NULL, 0));
		cJSON_AddNumberToObject(jc, "res", vm_get_field_int(c, R06_F_MDEF, NULL, 0));
		cJSON_AddNumberToObject(jc, "hit", vm_get_field_int(c, R06_F_HIT, NULL, 0));
		cJSON_AddNumberToObject(jc, "eva", vm_get_field_int(c, R06_F_EVA, NULL, 0));
		cJSON_AddItemToArray(chars, jc);
		added++;
	}
	cJSON_AddItemToObject(root, "exCharacters", chars);
}

static void ex_r06_locks(int mask)
{
	struct page *gc = vm_get_global_page("gc", NULL);
	int n = vm_dd_array_len(gc);
	if (n <= 0) return;
	if ((mask & (1 << 0)) && ex_get_global_int(R06_G_GOLD, NULL, 0) < 999999)
		ex_set_global_int(R06_G_GOLD, NULL, 999999);
	if (mask & (1 << 1)) {
		struct page *pm = vm_get_global_page(R06_G_PARTY_MY, NULL);
		struct page *an = vm_get_field_page(pm, "m_anChr", NULL);
		for (int i = 0; i < vm_dd_array_len(an); i++) {
			int idx = ex_arr_int_get(an, i, 0);
			if (idx <= 0 || idx >= n) continue;
			struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(gc, idx));
			if (!c) continue;
			int mhp = vm_get_field_int(c, R06_F_HP_MAX, NULL, 0);
			int map = vm_get_field_int(c, R06_F_AP_MAX, NULL, 0);
			if (mhp > 0) vm_set_field_int(c, R06_F_HP, NULL, mhp);
			if (map > 0) vm_set_field_int(c, R06_F_AP, NULL, map);
		}
	}
	if (mask & (1 << 2)) {
		struct page *pe = vm_get_global_page(R06_G_PARTY_EN, NULL);
		struct page *an = vm_get_field_page(pe, "m_anChr", NULL);
		for (int i = 0; i < vm_dd_array_len(an); i++) {
			int cno = ex_arr_int_get(an, i, 0);
			int slot = R06_ENEMY_SLOT_BASE + i;
			if (cno <= 0 || slot >= n) continue;
			struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(gc, slot));
			if (c && vm_get_field_int(c, R06_F_HP, NULL, 0) > 1)
				vm_set_field_int(c, R06_F_HP, NULL, 1);
		}
	}
	if (mask & (1 << 3)) {
		struct page *gi = vm_get_global_page("gi", NULL);
		int in = vm_dd_array_len(gi);
		for (int i = 0; i < in; i++) {
			struct page *it = vm_safe_get_page(vm_dd_array_elem_slot(gi, i));
			if (it && vm_get_field_int(it, R06_F_ITEM_CNT, NULL, 0) > 0 &&
			    vm_get_field_int(it, R06_F_ITEM_CNT, NULL, 0) < 99)
				vm_set_field_int(it, R06_F_ITEM_CNT, NULL, 99);
		}
	}
	if ((mask & (1 << 4)) && ex_get_global_int(R06_G_LUCK, NULL, 0) < 999)
		ex_set_global_int(R06_G_LUCK, NULL, 999);
}

static void ex_r06_action(cJSON *act)
{
	int32_t v;
	if (ex_json_int(act, "gold", &v)) ex_set_global_int(R06_G_GOLD, NULL, vm_dd_clamp(v, 0, 99999999));
	if (ex_json_int(act, "addGold", &v)) ex_set_global_int(R06_G_GOLD, NULL, vm_dd_clamp(ex_get_global_int(R06_G_GOLD, NULL, 0) + v, 0, 99999999));
	if (ex_json_int(act, "luck", &v)) ex_set_global_int(R06_G_LUCK, NULL, vm_dd_clamp(v, 0, 9999));

	struct page *gc = vm_get_global_page("gc", NULL);
	int n = vm_dd_array_len(gc);
	int ally_end = n < R06_ENEMY_SLOT_BASE ? n : R06_ENEMY_SLOT_BASE;
	if (ex_json_cheat_is(act, "fullHeal")) {
		for (int i = 1; i < ally_end; i++) {
			struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(gc, i));
			if (!c) continue;
			int mhp = vm_get_field_int(c, R06_F_HP_MAX, NULL, 0);
			int map = vm_get_field_int(c, R06_F_AP_MAX, NULL, 0);
			if (mhp > 0) vm_set_field_int(c, R06_F_HP, NULL, mhp);
			if (map > 0) vm_set_field_int(c, R06_F_AP, NULL, map);
		}
	} else if (ex_json_cheat_is(act, "maxItems")) {
		struct page *gi = vm_get_global_page("gi", NULL);
		for (int i = 0; i < vm_dd_array_len(gi); i++) {
			struct page *it = vm_safe_get_page(vm_dd_array_elem_slot(gi, i));
			if (it && vm_get_field_int(it, R06_F_ITEM_CNT, NULL, 0) > 0)
				vm_set_field_int(it, R06_F_ITEM_CNT, NULL, 99);
		}
	} else if (ex_json_cheat_is(act, "enemy1Hp")) {
		ex_r06_locks(1 << 2);
	}

	cJSON *ce = cJSON_GetObjectItemCaseSensitive(act, "exCharEdit");
	if (ce && cJSON_IsObject(ce) && n > 0) {
		int32_t target = -1;
		ex_json_int(ce, "target", &target);
		for (int i = 1; i < ally_end; i++) {
			if (target >= 0 && i != target) continue;
			struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(gc, i));
			if (!c || vm_get_field_int(c, R06_F_HP_MAX, NULL, 0) <= 0) continue;
			if (target < 0 && vm_get_field_int(c, R06_F_LV, NULL, 0) <= 0) continue;
			if (ex_json_int(ce, "lv", &v)) vm_set_field_int(c, R06_F_LV, NULL, vm_dd_clamp(v, 1, 999));
			if (ex_json_int(ce, "lvMax", &v)) vm_set_field_int(c, R06_F_LV_MAX, NULL, vm_dd_clamp(v, 1, 999));
			if (ex_json_int(ce, "hpMax", &v)) vm_set_field_int(c, R06_F_HP_MAX, NULL, vm_dd_clamp(v, 1, 999999));
			if (ex_json_int(ce, "hp", &v)) vm_set_field_int(c, R06_F_HP, NULL, vm_dd_clamp(v, 0, 999999));
			if (ex_json_int(ce, "apMax", &v)) vm_set_field_int(c, R06_F_AP_MAX, NULL, vm_dd_clamp(v, 1, 999));
			if (ex_json_int(ce, "ap", &v)) vm_set_field_int(c, R06_F_AP, NULL, vm_dd_clamp(v, 0, 999));
			if (ex_json_int(ce, "exp", &v)) vm_set_field_int(c, R06_F_EXP, NULL, vm_dd_clamp(v, 0, 99999999));
			if (ex_json_int(ce, "atk", &v)) vm_set_field_int(c, R06_F_PATK, NULL, vm_dd_clamp(v, 0, 99999));
			if (ex_json_int(ce, "def", &v)) vm_set_field_int(c, R06_F_PDEF, NULL, vm_dd_clamp(v, 0, 99999));
			if (ex_json_int(ce, "mag", &v)) vm_set_field_int(c, R06_F_MATK, NULL, vm_dd_clamp(v, 0, 99999));
			if (ex_json_int(ce, "res", &v)) vm_set_field_int(c, R06_F_MDEF, NULL, vm_dd_clamp(v, 0, 99999));
			if (ex_json_int(ce, "hit", &v)) vm_set_field_int(c, R06_F_HIT, NULL, vm_dd_clamp(v, 0, 999));
			if (ex_json_int(ce, "eva", &v)) vm_set_field_int(c, R06_F_EVA, NULL, vm_dd_clamp(v, 0, 999));
			if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(ce, "fullHeal"))) {
				int mhp = vm_get_field_int(c, R06_F_HP_MAX, NULL, 0);
				int map = vm_get_field_int(c, R06_F_AP_MAX, NULL, 0);
				if (mhp > 0) vm_set_field_int(c, R06_F_HP, NULL, mhp);
				if (map > 0) vm_set_field_int(c, R06_F_AP, NULL, map);
			}
		}
	}
}

/* =========================================================================
 * 2. Evenicle 2 & Evenicle 2 Rance (evenicle2 / evenicle2rance)
 * ========================================================================= */
#define EV2_F_SKILL_P    "\x83\x58\x83\x4c\x83\x8b\x82\x6f" /* スキルＰ */
#define EV2_F_SKILL_P_CN EV_F_SKILL_P_CN
#define EV2_F_BP         "\x82\x61\x82\x6f"                 /* ＢＰ */
#define EV2_F_BP_CN      EV_F_BP_CN

static struct page *ex_ev2_status_at(struct page *sarr, int idx)
{
	struct page *ref = vm_safe_get_page(vm_dd_array_elem_slot(sarr, idx));
	return ref ? vm_get_field_page(ref, "rf", NULL) : NULL;
}

static bool ex_ev2_get_money(int32_t *out)
{
	struct page *fref = vm_get_global_page("g_FlagReference", NULL);
	return ex_get_ref_int_at(fref, 4, out);
}

static void ex_ev2_set_money(int32_t val)
{
	struct page *fref = vm_get_global_page("g_FlagReference", NULL);
	ex_set_ref_int_at(fref, 4, vm_dd_clamp(val, 0, 99999999));
}

static void ex_ev2_populate(cJSON *root)
{
	struct page *sarr = vm_get_global_page("g_arfStatus", NULL);
	int sn = vm_dd_array_len(sarr);
	int32_t money = 0;
	bool has_money = ex_ev2_get_money(&money);
	cJSON_AddBoolToObject(root, "inGame", sn > 0 && has_money);
	struct page *bt = vm_get_global_page("g_ptBattle", NULL);
	struct page *im = vm_get_field_page(bt, "m_InfoManager", NULL);
	struct page *pi = vm_get_field_page(im, "m_PersonsInfo", NULL);
	bool in_battle = pi && vm_get_field_int(pi, "m_EnemyAliveCount", NULL, 0) > 0;
	cJSON_AddBoolToObject(root, "inBattle", in_battle);
	cJSON_AddNumberToObject(root, "gold", money);

	struct page *cmgr = vm_get_global_page("g_clCharaInfoManager", NULL);
	struct page *ilist = vm_get_field_page(cmgr, "m_InfoList", NULL);
	int ilen = vm_dd_array_len(ilist);

	cJSON *chars = cJSON_CreateArray();
	int added = 0;
	for (int i = 1; i < sn && i < 32 && added < 24; i++) {
		struct page *st = ex_ev2_status_at(sarr, i);
		if (!st || !vm_get_field_int(st, "m_IsUse", NULL, 0)) continue;
		struct page *mainp = vm_get_field_page(st, "m_clMain", NULL);
		if (!mainp || vm_get_field_int(mainp, EV_F_LV, EV_F_LV_CN, 0) <= 0) continue;
		int32_t cno = vm_get_field_int(st, "m_CharaNo", NULL, i);
		char *nm = NULL;
		if (cno >= 0 && cno < ilen) {
			struct page *info = vm_safe_get_page(vm_dd_array_elem_slot(ilist, cno));
			if (info) {
				if (vm_get_field_int(info, EV_F_CHARA_ID, EV_F_CHARA_ID_CN, 0) > 0)
					continue;
				if (vm_get_field_int(info, EV_F_PARTY_SET, EV_F_PARTY_SET_CN, 0) <= 1)
					continue;
				nm = vm_dd_get_field_utf8(info, EV_F_NAME, EV_F_NAME_CN);
			}
		}
		if (!nm) nm = strdup("");
		struct page *subp = vm_get_field_page(st, "m_clSub", NULL);
		struct page *batp = vm_get_field_page(st, "m_clBattle", NULL);
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddStringToObject(jc, "name", nm);
		free(nm);
		cJSON_AddNumberToObject(jc, "lv", vm_get_field_int(mainp, EV_F_LV, EV_F_LV_CN, 1));
		cJSON_AddNumberToObject(jc, "hp", vm_get_field_int(mainp, EV_F_HP, EV_F_HP_CN, 0));
		cJSON_AddNumberToObject(jc, "hpMax", vm_get_field_int(mainp, EV_F_MAX_HP, EV_F_MAX_HP_CN, 0));
		cJSON_AddNumberToObject(jc, "atk", vm_get_field_int(mainp, EV_F_ATK, EV_F_ATK_CN, 0));
		cJSON_AddNumberToObject(jc, "mag", vm_get_field_int(mainp, EV_F_MAG, EV_F_MAG_CN, 0));
		cJSON_AddNumberToObject(jc, "def", vm_get_field_int(mainp, EV_F_DEF, EV_F_DEF_CN, 0));
		cJSON_AddNumberToObject(jc, "spd", vm_get_field_int(mainp, EV_F_SPD, EV_F_SPD_CN, 0));
		cJSON_AddNumberToObject(jc, "exp", vm_get_field_int(mainp, EV_F_EXP, EV_F_EXP_CN, 0));
		cJSON_AddNumberToObject(jc, "skillP", vm_get_field_int(subp, EV2_F_SKILL_P, EV2_F_SKILL_P_CN, 0));
		cJSON_AddNumberToObject(jc, "bp", vm_get_field_int(batp, EV2_F_BP, EV2_F_BP_CN, 0));
		cJSON_AddItemToArray(chars, jc);
		added++;
	}
	cJSON_AddItemToObject(root, "exCharacters", chars);
}

static void ex_ev2_locks(int mask)
{
	struct page *sarr = vm_get_global_page("g_arfStatus", NULL);
	int sn = vm_dd_array_len(sarr);
	if (sn <= 0) return;

	if (mask & (1 << 0)) {
		struct page *pf = vm_get_global_page("g_pfGame", NULL);
		if (pf) {
			struct page *enc = vm_get_field_page(pf, "m_clEncount", NULL);
			if (enc) {
				union vm_value *vmax = vm_get_struct_member(enc, "m_MaxBarCount", NULL);
				union vm_value *vnow = vm_get_struct_member(enc, "m_NowBarCount", NULL);
				if (vmax && vnow && vmax->f > 0.0f && vnow->f < vmax->f)
					vnow->f = vmax->f;
			}
		}
	}
	if (mask & ((1 << 1) | (1 << 2) | (1 << 3))) {
		struct page *cmgr = vm_get_global_page("g_clCharaInfoManager", NULL);
		struct page *ilist = vm_get_field_page(cmgr, "m_InfoList", NULL);
		int ilen = vm_dd_array_len(ilist);
		for (int i = 1; i < sn && i < 32; i++) {
			struct page *st = ex_ev2_status_at(sarr, i);
			if (!st || !vm_get_field_int(st, "m_IsUse", NULL, 0)) continue;
			int32_t cno = vm_get_field_int(st, "m_CharaNo", NULL, i);
			if (cno >= 0 && cno < ilen) {
				struct page *info = vm_safe_get_page(vm_dd_array_elem_slot(ilist, cno));
				if (info && (vm_get_field_int(info, EV_F_CHARA_ID, EV_F_CHARA_ID_CN, 0) > 0 ||
				             vm_get_field_int(info, EV_F_PARTY_SET, EV_F_PARTY_SET_CN, 0) <= 1))
					continue;
			}
			struct page *mainp = vm_get_field_page(st, "m_clMain", NULL);
			if (!mainp || vm_get_field_int(mainp, EV_F_LV, EV_F_LV_CN, 0) <= 0) continue;
			if (mask & (1 << 1)) {
				int mhp = vm_get_field_int(mainp, EV_F_MAX_HP, EV_F_MAX_HP_CN, 0);
				if (mhp > 0) vm_set_field_int(mainp, EV_F_HP, EV_F_HP_CN, mhp);
			}
			if (mask & (1 << 2)) {
				struct page *subp = vm_get_field_page(st, "m_clSub", NULL);
				if (subp && vm_get_field_int(subp, EV2_F_SKILL_P, EV2_F_SKILL_P_CN, 0) < 99)
					vm_set_field_int(subp, EV2_F_SKILL_P, EV2_F_SKILL_P_CN, 99);
			}
			if (mask & (1 << 3)) {
				struct page *batp = vm_get_field_page(st, "m_clBattle", NULL);
				if (batp && vm_get_field_int(batp, EV2_F_BP, EV2_F_BP_CN, 0) < 5)
					vm_set_field_int(batp, EV2_F_BP, EV2_F_BP_CN, 5);
			}
		}
	}
	if (mask & (1 << 4)) {
		struct page *bt = vm_get_global_page("g_ptBattle", NULL);
		struct page *im = vm_get_field_page(bt, "m_InfoManager", NULL);
		struct page *pi = vm_get_field_page(im, "m_PersonsInfo", NULL);
		struct page *elist = vm_get_field_page(pi, "m_EnemyInfoList", NULL);
		for (int i = 0; i < vm_dd_array_len(elist); i++) {
			struct page *ep = vm_safe_get_page(vm_dd_array_elem_slot(elist, i));
			if (!ep || !vm_get_field_int(ep, "m_IsExist", NULL, 0)) continue;
			int32_t cno = vm_get_field_int(ep, "m_CharaNo", NULL, -1);
			if (cno >= 0 && cno < sn) {
				struct page *est = ex_ev2_status_at(sarr, cno);
				struct page *em = vm_get_field_page(est, "m_clMain", NULL);
				if (em && vm_get_field_int(em, EV_F_HP, EV_F_HP_CN, 0) > 1)
					vm_set_field_int(em, EV_F_HP, EV_F_HP_CN, 1);
			}
		}
	}
	if (mask & (1 << 5)) {
		int32_t money = 0;
		if (ex_ev2_get_money(&money) && money < 9999999)
			ex_ev2_set_money(9999999);
	}
}

static void ex_ev2_action(cJSON *act)
{
	int32_t v;
	if (ex_json_int(act, "gold", &v)) ex_ev2_set_money(v);
	if (ex_json_int(act, "addGold", &v)) {
		int32_t cur = 0;
		if (ex_ev2_get_money(&cur)) ex_ev2_set_money(cur + v);
	}
	if (ex_json_cheat_is(act, "fullHeal")) ex_ev2_locks((1 << 1) | (1 << 3));
	if (ex_json_cheat_is(act, "enemy1Hp")) ex_ev2_locks(1 << 4);
	if (ex_json_cheat_is(act, "maxItems")) {
		struct page *imgr = vm_get_global_page("g_clItemManager", NULL);
		struct page *ilist = vm_get_field_page(imgr, "m_clItemInfoList", NULL);
		struct page *arr = vm_get_field_page(ilist, "m_ItemData", NULL);
		for (int i = 0; i < vm_dd_array_len(arr); i++) {
			struct page *it = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
			if (it && vm_get_field_int(it, "m_HaveCount", NULL, 0) > 0)
				vm_set_field_int(it, "m_HaveCount", NULL, 99);
		}
	}
	cJSON *ce = cJSON_GetObjectItemCaseSensitive(act, "exCharEdit");
	if (ce && cJSON_IsObject(ce)) {
		struct page *sarr = vm_get_global_page("g_arfStatus", NULL);
		int sn = vm_dd_array_len(sarr);
		struct page *cmgr = vm_get_global_page("g_clCharaInfoManager", NULL);
		struct page *ilist = vm_get_field_page(cmgr, "m_InfoList", NULL);
		int ilen = vm_dd_array_len(ilist);
		int32_t target = -1;
		ex_json_int(ce, "target", &target);
		for (int i = 1; i < sn && i < 32; i++) {
			if (target >= 0 && i != target) continue;
			struct page *st = ex_ev2_status_at(sarr, i);
			if (!st || !vm_get_field_int(st, "m_IsUse", NULL, 0)) continue;
			if (target < 0) {
				int32_t cno = vm_get_field_int(st, "m_CharaNo", NULL, i);
				if (cno >= 0 && cno < ilen) {
					struct page *info = vm_safe_get_page(vm_dd_array_elem_slot(ilist, cno));
					if (info && (vm_get_field_int(info, EV_F_CHARA_ID, EV_F_CHARA_ID_CN, 0) > 0 ||
					             vm_get_field_int(info, EV_F_PARTY_SET, EV_F_PARTY_SET_CN, 0) <= 1))
						continue;
				}
			}
			struct page *mainp = vm_get_field_page(st, "m_clMain", NULL);
			struct page *subp = vm_get_field_page(st, "m_clSub", NULL);
			struct page *batp = vm_get_field_page(st, "m_clBattle", NULL);
			if (!mainp || vm_get_field_int(mainp, EV_F_LV, EV_F_LV_CN, 0) <= 0) continue;
			if (ex_json_int(ce, "lv", &v)) vm_set_field_int(mainp, EV_F_LV, EV_F_LV_CN, vm_dd_clamp(v, 1, 999));
			if (ex_json_int(ce, "hpMax", &v)) vm_ev_set_stat_with_base(mainp, EV_F_MAX_HP, EV_F_MAX_HP_CN, EV_F_BASE_HP, EV_F_BASE_HP_CN, v, 1, 999999);
			if (ex_json_int(ce, "hp", &v)) vm_set_field_int(mainp, EV_F_HP, EV_F_HP_CN, vm_dd_clamp(v, 0, 999999));
			if (ex_json_int(ce, "atk", &v)) vm_ev_set_stat_with_base(mainp, EV_F_ATK, EV_F_ATK_CN, EV_F_BASE_ATK, EV_F_BASE_ATK_CN, v, 0, 99999);
			if (ex_json_int(ce, "mag", &v)) vm_ev_set_stat_with_base(mainp, EV_F_MAG, EV_F_MAG_CN, EV_F_BASE_MAG, EV_F_BASE_MAG_CN, v, 0, 99999);
			if (ex_json_int(ce, "def", &v)) vm_ev_set_stat_with_base(mainp, EV_F_DEF, EV_F_DEF_CN, EV_F_BASE_DEF, EV_F_BASE_DEF_CN, v, 0, 99999);
			if (ex_json_int(ce, "spd", &v)) vm_ev_set_stat_with_base(mainp, EV_F_SPD, EV_F_SPD_CN, EV_F_BASE_SPD, EV_F_BASE_SPD_CN, v, 0, 99999);
			if (ex_json_int(ce, "exp", &v)) vm_set_field_int(mainp, EV_F_EXP, EV_F_EXP_CN, vm_dd_clamp(v, 0, 99999999));
			if (ex_json_int(ce, "skillP", &v) && subp) vm_set_field_int(subp, EV2_F_SKILL_P, EV2_F_SKILL_P_CN, vm_dd_clamp(v, 0, 9999));
			if (ex_json_int(ce, "bp", &v) && batp) vm_set_field_int(batp, EV2_F_BP, EV2_F_BP_CN, vm_dd_clamp(v, 0, 5));
			if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(ce, "fullHeal"))) {
				int mhp = vm_get_field_int(mainp, EV_F_MAX_HP, EV_F_MAX_HP_CN, 0);
				if (mhp > 0) vm_set_field_int(mainp, EV_F_HP, EV_F_HP_CN, mhp);
				if (batp) vm_set_field_int(batp, EV2_F_BP, EV2_F_BP_CN, 5);
			}
		}
	}
}

/* =========================================================================
 * 3. Daibanchou (daibanchou) — includes exact FilterGold (^1021), nGoldDammy, nGoldCrc1 & nGoldCrc2!
 * ========================================================================= */
static int32_t ex_db_get_gold(struct page *gv)
{
	if (!gv) return 0;
	int32_t raw = vm_get_field_int(gv, "nGold", NULL, 0);
	int32_t dammy = vm_get_field_int(gv, "nGoldDammy", NULL, 0);
	if ((raw ^ 1021) == dammy)
		return dammy;
	return dammy >= 0 ? dammy : (raw ^ 1021);
}

static void ex_db_set_gold(struct page *gv, int32_t val)
{
	if (!gv) return;
	int32_t g = vm_dd_clamp(val, 0, 9999999);
	int32_t filtered = g ^ 1021;
	uint32_t ug = (uint32_t)g;
	int32_t crc1 = (int32_t)(((ug & 0xFFu) + ((ug >> 8) & 0xFFu) + ((ug >> 16) & 0xFFu) + ((ug >> 24) & 0xFFu)) & 0xFFu);
	int32_t crc2 = ((((filtered * 3) / 7) * 5) & 0xFF);
	vm_set_field_int(gv, "nGold", NULL, filtered);
	vm_set_field_int(gv, "nGoldDammy", NULL, g);
	vm_set_field_int(gv, "nGoldCrc1", NULL, crc1);
	vm_set_field_int(gv, "nGoldCrc2", NULL, crc2);
}

static struct page *ex_db_player_servants(void)
{
	struct page *orgs = vm_get_global_page("aOrganization", NULL);
	if (vm_dd_array_len(orgs) <= 0) return NULL;
	struct page *org0 = vm_safe_get_page(vm_dd_array_elem_slot(orgs, 0));
	struct page *mgr = vm_get_field_page(org0, "Servant", NULL);
	return vm_get_field_page(mgr, "aServant", NULL);
}

static void ex_db_populate(cJSON *root)
{
	struct page *gv = vm_get_global_page("GlobalVar", NULL);
	struct page *servs = ex_db_player_servants();
	int sn = vm_dd_array_len(servs);
	cJSON_AddBoolToObject(root, "inGame", gv != NULL && sn > 0);
	cJSON_AddBoolToObject(root, "inBattle", false);
	cJSON_AddNumberToObject(root, "gold", ex_db_get_gold(gv));
	cJSON_AddNumberToObject(root, "turn", vm_get_field_int(gv, "nTurnCount", NULL, 0));
	cJSON_AddNumberToObject(root, "turnLimit", vm_get_field_int(gv, "nTurnLimit", NULL, 0));

	cJSON *chars = cJSON_CreateArray();
	int added = 0;
	for (int i = 0; i < sn && added < 48; i++) {
		struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(servs, i));
		if (!c || vm_get_field_int(c, "nMaxHelth", NULL, 0) <= 0) continue;
		struct page *np = vm_get_field_page(c, "m_sName", NULL);
		char *nm = vm_dd_get_field_utf8(np, R06_F_NAME, NULL);
		if (!nm[0]) { free(nm); continue; }
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddStringToObject(jc, "name", nm);
		free(nm);
		cJSON_AddNumberToObject(jc, "lv", vm_get_field_int(c, "nLv", NULL, 1));
		cJSON_AddNumberToObject(jc, "exp", vm_get_field_int(c, "nExp", NULL, 0));
		cJSON_AddNumberToObject(jc, "hp", vm_get_field_int(c, "nNowHelth", NULL, 0));
		cJSON_AddNumberToObject(jc, "hpMax", vm_get_field_int(c, "nMaxHelth", NULL, 0));
		cJSON_AddNumberToObject(jc, "vit", vm_get_field_int(c, "nNowVit", NULL, 0));
		cJSON_AddNumberToObject(jc, "vitMax", vm_get_field_int(c, "nMaxVit", NULL, 0));
		cJSON_AddNumberToObject(jc, "atk", vm_get_field_int(c, "nAttack", NULL, 0));
		cJSON_AddNumberToObject(jc, "hit", vm_get_field_int(c, "nHit", NULL, 0));
		cJSON_AddNumberToObject(jc, "eva", vm_get_field_int(c, "nAvoid", NULL, 0));
		cJSON_AddNumberToObject(jc, "love", vm_get_field_int(c, "nLoyalty", NULL, 0));
		cJSON_AddItemToArray(chars, jc);
		added++;
	}
	cJSON_AddItemToObject(root, "exCharacters", chars);
}

static void ex_db_locks(int mask)
{
	struct page *gv = vm_get_global_page("GlobalVar", NULL);
	struct page *servs = ex_db_player_servants();
	int sn = vm_dd_array_len(servs);
	if (!gv || sn <= 0) return;
	if ((mask & (1 << 0)) && ex_db_get_gold(gv) < 999999)
		ex_db_set_gold(gv, 999999);
	if (mask & ((1 << 1) | (1 << 2))) {
		for (int i = 0; i < sn; i++) {
			struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(servs, i));
			if (!c || vm_get_field_int(c, "nMaxHelth", NULL, 0) <= 0) continue;
			if (mask & (1 << 1)) {
				int mhp = vm_get_field_int(c, "nMaxHelth", NULL, 0);
				int mvit = vm_get_field_int(c, "nMaxVit", NULL, 0);
				if (mhp > 0) vm_set_field_int(c, "nNowHelth", NULL, mhp);
				if (mvit > 0) vm_set_field_int(c, "nNowVit", NULL, mvit);
			}
			if ((mask & (1 << 2)) && vm_get_field_int(c, "nLoyalty", NULL, 0) < 100)
				vm_set_field_int(c, "nLoyalty", NULL, 100);
		}
	}
	if ((mask & (1 << 3)) && vm_get_field_int(gv, "nTurnCount", NULL, 0) > 1)
		vm_set_field_int(gv, "nTurnCount", NULL, 1);
}

static void ex_db_action(cJSON *act)
{
	struct page *gv = vm_get_global_page("GlobalVar", NULL);
	int32_t v;
	if (ex_json_int(act, "gold", &v)) ex_db_set_gold(gv, v);
	if (ex_json_int(act, "addGold", &v)) ex_db_set_gold(gv, ex_db_get_gold(gv) + v);
	if (ex_json_int(act, "turn", &v) && gv) vm_set_field_int(gv, "nTurnCount", NULL, vm_dd_clamp(v, 0, 999));
	if (ex_json_cheat_is(act, "fullHeal")) ex_db_locks((1 << 1) | (1 << 2));

	cJSON *ce = cJSON_GetObjectItemCaseSensitive(act, "exCharEdit");
	if (ce && cJSON_IsObject(ce)) {
		struct page *servs = ex_db_player_servants();
		int sn = vm_dd_array_len(servs);
		int32_t target = -1;
		ex_json_int(ce, "target", &target);
		for (int i = 0; i < sn; i++) {
			if (target >= 0 && i != target) continue;
			struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(servs, i));
			if (!c || vm_get_field_int(c, "nMaxHelth", NULL, 0) <= 0) continue;
			if (ex_json_int(ce, "lv", &v)) vm_set_field_int(c, "nLv", NULL, vm_dd_clamp(v, 1, 99));
			if (ex_json_int(ce, "exp", &v)) vm_set_field_int(c, "nExp", NULL, vm_dd_clamp(v, 0, 999999));
			if (ex_json_int(ce, "hpMax", &v)) vm_set_field_int(c, "nMaxHelth", NULL, vm_dd_clamp(v, 1, 99999));
			if (ex_json_int(ce, "hp", &v)) vm_set_field_int(c, "nNowHelth", NULL, vm_dd_clamp(v, 0, 99999));
			if (ex_json_int(ce, "vitMax", &v)) vm_set_field_int(c, "nMaxVit", NULL, vm_dd_clamp(v, 1, 9999));
			if (ex_json_int(ce, "vit", &v)) vm_set_field_int(c, "nNowVit", NULL, vm_dd_clamp(v, 0, 9999));
			if (ex_json_int(ce, "atk", &v)) vm_set_field_int(c, "nAttack", NULL, vm_dd_clamp(v, 0, 9999));
			if (ex_json_int(ce, "hit", &v)) vm_set_field_int(c, "nHit", NULL, vm_dd_clamp(v, 0, 999));
			if (ex_json_int(ce, "eva", &v)) vm_set_field_int(c, "nAvoid", NULL, vm_dd_clamp(v, 0, 999));
			if (ex_json_int(ce, "love", &v)) vm_set_field_int(c, "nLoyalty", NULL, vm_dd_clamp(v, 0, 100));
			if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(ce, "fullHeal"))) {
				int mhp = vm_get_field_int(c, "nMaxHelth", NULL, 0);
				int mvit = vm_get_field_int(c, "nMaxVit", NULL, 0);
				if (mhp > 0) vm_set_field_int(c, "nNowHelth", NULL, mhp);
				if (mvit > 0) vm_set_field_int(c, "nNowVit", NULL, mvit);
			}
		}
	}
}

/* =========================================================================
 * 4. Daiteikoku (daiteikoku)
 * ========================================================================= */
static void ex_dt_populate(cJSON *root)
{
	struct page *ctx = vm_get_global_page("context", NULL);
	struct page *adms = vm_get_field_page(ctx, "admiral", NULL);
	int an = vm_dd_array_len(adms);
	cJSON_AddBoolToObject(root, "inGame", ctx != NULL && an > 0);
	cJSON_AddBoolToObject(root, "inBattle", false);
	cJSON_AddNumberToObject(root, "resource", vm_get_field_int(ctx, "resource", NULL, 0));
	cJSON_AddNumberToObject(root, "technology", vm_get_field_int(ctx, "technology", NULL, 0));
	cJSON_AddNumberToObject(root, "turn", vm_get_field_int(ctx, "turn", NULL, 0));

	cJSON *chars = cJSON_CreateArray();
	int added = 0;
	for (int i = 0; i < an && added < 60; i++) {
		struct page *a = vm_safe_get_page(vm_dd_array_elem_slot(adms, i));
		if (!a || vm_get_field_int(a, "state_", NULL, 0) != 1) continue;
		char *nm = vm_dd_get_field_utf8(a, "name_", NULL);
		if (!nm[0]) { free(nm); continue; }
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddStringToObject(jc, "name", nm);
		free(nm);
		cJSON_AddNumberToObject(jc, "lv", vm_get_field_int(a, "level_", NULL, 1));
		cJSON_AddNumberToObject(jc, "exp", vm_get_field_int(a, "exp_", NULL, 0));
		cJSON_AddNumberToObject(jc, "hp", vm_get_field_int(a, "hp_", NULL, 0));
		cJSON_AddNumberToObject(jc, "hpMax", vm_get_field_int(a, "maxHp_", NULL, 0));
		cJSON_AddNumberToObject(jc, "scout", vm_get_field_int(a, "scout_", NULL, 0));
		struct page *atk = vm_get_field_page(a, "atk_", NULL);
		cJSON_AddNumberToObject(jc, "atk", ex_arr_int_get(atk, 0, 0));
		cJSON_AddItemToArray(chars, jc);
		added++;
	}
	cJSON_AddItemToObject(root, "exCharacters", chars);
}

static void ex_dt_locks(int mask)
{
	struct page *ctx = vm_get_global_page("context", NULL);
	if (!ctx) return;
	if ((mask & (1 << 0)) && vm_get_field_int(ctx, "resource", NULL, 0) < 999999)
		vm_set_field_int(ctx, "resource", NULL, 999999);
	if ((mask & (1 << 1)) && vm_get_field_int(ctx, "technology", NULL, 0) < 999999)
		vm_set_field_int(ctx, "technology", NULL, 999999);
	if (mask & ((1 << 2) | (1 << 3))) {
		struct page *adms = vm_get_field_page(ctx, "admiral", NULL);
		for (int i = 0; i < vm_dd_array_len(adms); i++) {
			struct page *a = vm_safe_get_page(vm_dd_array_elem_slot(adms, i));
			if (!a || vm_get_field_int(a, "state_", NULL, 0) != 1) continue;
			if (mask & (1 << 2)) {
				int mhp = vm_get_field_int(a, "maxHp_", NULL, 0);
				if (mhp > 0) vm_set_field_int(a, "hp_", NULL, mhp);
			}
			if (mask & (1 << 3))
				vm_set_field_int(a, "movedCount_", NULL, 0);
		}
	}
	if (mask & (1 << 4)) {
		int my_nat = vm_get_field_int(ctx, "myNation", NULL, 0);
		struct page *stars = vm_get_field_page(ctx, "star", NULL);
		for (int i = 0; i < vm_dd_array_len(stars); i++) {
			struct page *st = vm_safe_get_page(vm_dd_array_elem_slot(stars, i));
			if (st && vm_get_field_int(st, "manageNationIndex_", NULL, -1) == my_nat) {
				if (vm_get_field_int(st, "peace_", NULL, 0) < 100)
					vm_set_field_int(st, "peace_", NULL, 100);
			}
		}
	}
}

static void ex_dt_action(cJSON *act)
{
	struct page *ctx = vm_get_global_page("context", NULL);
	if (!ctx) return;
	int32_t v;
	if (ex_json_int(act, "resource", &v)) vm_set_field_int(ctx, "resource", NULL, vm_dd_clamp(v, 0, 99999999));
	if (ex_json_int(act, "addResource", &v)) vm_set_field_int(ctx, "resource", NULL, vm_dd_clamp(vm_get_field_int(ctx, "resource", NULL, 0) + v, 0, 99999999));
	if (ex_json_int(act, "technology", &v)) vm_set_field_int(ctx, "technology", NULL, vm_dd_clamp(v, 0, 99999999));
	if (ex_json_int(act, "addTechnology", &v)) vm_set_field_int(ctx, "technology", NULL, vm_dd_clamp(vm_get_field_int(ctx, "technology", NULL, 0) + v, 0, 99999999));
	if (ex_json_cheat_is(act, "fullHeal")) ex_dt_locks((1 << 2) | (1 << 3));
	if (ex_json_cheat_is(act, "unlockShips")) {
		struct page *ships = vm_get_field_page(ctx, "ship", NULL);
		for (int i = 0; i < vm_dd_array_len(ships); i++) {
			struct page *sh = vm_safe_get_page(vm_dd_array_elem_slot(ships, i));
			if (!sh) continue;
			vm_set_field_int(sh, "isMakable_", NULL, 1);
			vm_set_field_int(sh, "isDevelopEnable_", NULL, 1);
			if (vm_get_field_int(sh, "count_", NULL, 0) < 99)
				vm_set_field_int(sh, "count_", NULL, 99);
		}
	}
	cJSON *ce = cJSON_GetObjectItemCaseSensitive(act, "exCharEdit");
	if (ce && cJSON_IsObject(ce)) {
		struct page *adms = vm_get_field_page(ctx, "admiral", NULL);
		int32_t target = -1;
		ex_json_int(ce, "target", &target);
		for (int i = 0; i < vm_dd_array_len(adms); i++) {
			if (target >= 0 && i != target) continue;
			struct page *a = vm_safe_get_page(vm_dd_array_elem_slot(adms, i));
			if (!a || (target < 0 && vm_get_field_int(a, "state_", NULL, 0) != 1)) continue;
			if (ex_json_int(ce, "lv", &v)) vm_set_field_int(a, "level_", NULL, vm_dd_clamp(v, 1, 99));
			if (ex_json_int(ce, "exp", &v)) vm_set_field_int(a, "exp_", NULL, vm_dd_clamp(v, 0, 999999));
			if (ex_json_int(ce, "hpMax", &v)) vm_set_field_int(a, "maxHp_", NULL, vm_dd_clamp(v, 1, 99999));
			if (ex_json_int(ce, "hp", &v)) vm_set_field_int(a, "hp_", NULL, vm_dd_clamp(v, 0, 99999));
			if (ex_json_int(ce, "scout", &v)) vm_set_field_int(a, "scout_", NULL, vm_dd_clamp(v, 0, 9999));
			if (ex_json_int(ce, "atk", &v)) {
				struct page *atk = vm_get_field_page(a, "atk_", NULL);
				for (int k = 0; k < vm_dd_array_len(atk); k++)
					ex_arr_int_set(atk, k, vm_dd_clamp(v, 0, 9999));
			}
			if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(ce, "fullHeal"))) {
				int mhp = vm_get_field_int(a, "maxHp_", NULL, 0);
				if (mhp > 0) vm_set_field_int(a, "hp_", NULL, mhp);
				vm_set_field_int(a, "movedCount_", NULL, 0);
			}
		}
	}
}

/* =========================================================================
 * 5. Galzoo Island (galzoo) — supports both CN GBK and JP SJIS .ain names!
 * ========================================================================= */
#define GZ_G_GOLD_CN   "\xa1\xf4\xa3\xc7\xa3\xcf\xa3\xcc\xa3\xc4"
#define GZ_G_GOLD_JP   "\x81\x9f\x82\x66\x82\x6e\x82\x6b\x82\x63"
#define GZ_G_EXP_CN    "\xa1\xf4\xc3\xd4\x8c\x6d\xa3\xc5\xa3\xd8\xa3\xd0"
#define GZ_G_EXP_JP    "\x81\x9f\x96\xc0\x8b\x7b\x82\x64\x82\x77\x82\x6f"
#define GZ_G_DROP_CN   "\xa1\xf4\x91\xe9\xea\x4c\xa3\xaf\xa5\xc9\xa5\xed\xa5\xc3\xa5\xd7\xb4\x5f\xc2\xca"
#define GZ_G_DROP_JP   "\x81\x9f\x90\xed\x93\xac\x81\x5e\x83\x68\x83\x8d\x83\x62\x83\x76\x8a\x6d\x97\xa6"
#define GZ_G_NOENC_CN  "\xa1\xf4\xc3\xd4\x8c\x6d\xa3\xaf\xbd\xd3\xb4\xa5\xbd\xfb\xd6\xb9"
#define GZ_G_NOENC_JP  "\x81\x9f\x96\xc0\x8b\x7b\x81\x5e\x90\xda\x90\x47\x8b\xd6\x8e\x7e"
#define GZ_F_NAME_CN   "\xc3\xfb\xc7\xb0"
#define GZ_F_CAP_CN    "\xb2\xb6\xbb\xf1"
#define GZ_F_CAP_JP    "\x95\xdf\x8a\x6c"
#define GZ_F_LOVE_CN   "\xba\xc3\xb8\xd0\xb6\xc8"
#define GZ_F_LOVE_JP   "\x8d\x44\x8a\xb4\x93\x78"
#define GZ_F_LMAX_CN   "\xba\xc3\xb8\xd0\xb6\xc8\xc9\xcf\xcf\xde"
#define GZ_F_LMAX_JP   "\x8d\x44\x8a\xb4\x93\x78\x8f\xe3\x8c\xc0"
#define GZ_F_HP_CN     "\xcc\xe5\xc1\xa6"
#define GZ_F_HP_JP     "\x91\xcc\x97\xcd"
#define GZ_F_HPMAX_CN  "\xcc\xe5\xc1\xa6\xd7\xee\xb4\xf3"
#define GZ_F_HPMAX_JP  "\x91\xcc\x97\xcd\x8d\xc5\x91\xe5"
#define GZ_F_ATK_CN    "\xb9\xa5\xbb\xf7\xc1\xa6"
#define GZ_F_ATK_JP    "\x8d\x55\x8c\x82\x97\xcd"
#define GZ_F_DEF_CN    "\xb7\xc0\xd3\xf9\xc1\xa6"
#define GZ_F_DEF_JP    "\x96\x68\x8c\xe4\x97\xcd"
#define GZ_F_MAG_CN    "\xc4\xa7\xb7\xa8\xc1\xa6"
#define GZ_F_MAG_JP    "\x96\x82\x96\x40\x97\xcd"
#define GZ_F_RES_CN    "\xc4\xa7\xb5\xd6\xc1\xa6"
#define GZ_F_RES_JP    "\x96\x82\x92\xef\x97\xcd"
#define GZ_F_EVA_CN    "\xbb\xd8\xb1\xdc\xc1\xa6"
#define GZ_F_EVA_JP    "\x89\xf1\x94\xf0\x97\xcd"
#define GZ_F_HP_BASE_CN  "\xcc\xe5\xc1\xa6\xbb\xf9\xb4\xa1\xd6\xb5"
#define GZ_F_HP_BASE_JP  "\x91\xcc\x97\xcd\x8a\xee\x91\x62\x92\x6c"
#define GZ_F_ATK_BASE_CN "\xb9\xa5\xbb\xf7\xc1\xa6\xbb\xf9\xb4\xa1\xd6\xb5"
#define GZ_F_ATK_BASE_JP "\x8d\x55\x8c\x82\x97\xcd\x8a\xee\x91\x62\x92\x6c"
#define GZ_F_DEF_BASE_CN "\xb7\xc0\xd3\xf9\xc1\xa6\xbb\xf9\xb4\xa1\xd6\xb5"
#define GZ_F_DEF_BASE_JP "\x96\x68\x8c\xe4\x97\xcd\x8a\xee\x91\x62\x92\x6c"
#define GZ_F_MAG_BASE_CN "\xc4\xa7\xb7\xa8\xc1\xa6\xbb\xf9\xb4\xa1\xd6\xb5"
#define GZ_F_MAG_BASE_JP "\x96\x82\x96\x40\x97\xcd\x8a\xee\x91\x62\x92\x6c"
#define GZ_F_RES_BASE_CN "\xc4\xa7\xb5\xd6\xc1\xa6\xbb\xf9\xb4\xa1\xd6\xb5"
#define GZ_F_RES_BASE_JP "\x96\x82\x92\xef\x97\xcd\x8a\xee\x91\x62\x92\x6c"
#define GZ_F_EVA_BASE_CN "\xbb\xd8\xb1\xdc\xc1\xa6\xbb\xf9\xb4\xa1\xd6\xb5"
#define GZ_F_EVA_BASE_JP "\x89\xf1\x94\xf0\x97\xcd\x8a\xee\x91\x62\x92\x6c"
#define GZ_F_LV_CN     "\xa3\xcc\xa3\xd6"
#define GZ_F_EXP_CN    "\xbd\x55\xf2\x59\x82\x8e"
#define GZ_F_ICNT_CN   "\x82\x80\xca\xfd"

static void ex_gz_populate(cJSON *root)
{
	struct page *gs = vm_get_global_page("gs", NULL);
	struct page *fi = vm_get_global_page("fi", NULL);
	int gn = vm_dd_array_len(gs);
	cJSON_AddBoolToObject(root, "inGame", gn > 0);
	bool in_battle = false;
	if (vm_dd_array_len(fi) > 2) {
		struct page *ef = vm_safe_get_page(vm_dd_array_elem_slot(fi, 2));
		if (ef && vm_get_field_int(ef, GZ_F_HP_CN, GZ_F_HP_JP, 0) > 0)
			in_battle = true;
	}
	cJSON_AddBoolToObject(root, "inBattle", in_battle);
	cJSON_AddNumberToObject(root, "gold", ex_get_global_int(GZ_G_GOLD_CN, GZ_G_GOLD_JP, 0));
	cJSON_AddNumberToObject(root, "exp", ex_get_global_int(GZ_G_EXP_CN, GZ_G_EXP_JP, 0));

	cJSON *chars = cJSON_CreateArray();
	int added = 0;
	for (int i = 1; i < gn && added < 48; i++) {
		struct page *g = vm_safe_get_page(vm_dd_array_elem_slot(gs, i));
		if (!g || vm_get_field_int(g, GZ_F_CAP_CN, GZ_F_CAP_JP, 0) <= 0) continue;
		char *nm = vm_dd_get_field_utf8(g, GZ_F_NAME_CN, R06_F_NAME);
		if (!nm[0]) { free(nm); continue; }
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddStringToObject(jc, "name", nm);
		free(nm);
		cJSON_AddNumberToObject(jc, "lv", vm_get_field_int(g, GZ_F_LV_CN, EV_F_LV, 1));
		cJSON_AddNumberToObject(jc, "hp", vm_get_field_int(g, GZ_F_HP_CN, GZ_F_HP_JP, 0));
		cJSON_AddNumberToObject(jc, "atk", vm_get_field_int(g, GZ_F_ATK_CN, GZ_F_ATK_JP, 0));
		cJSON_AddNumberToObject(jc, "def", vm_get_field_int(g, GZ_F_DEF_CN, GZ_F_DEF_JP, 0));
		cJSON_AddNumberToObject(jc, "mag", vm_get_field_int(g, GZ_F_MAG_CN, GZ_F_MAG_JP, 0));
		cJSON_AddNumberToObject(jc, "res", vm_get_field_int(g, GZ_F_RES_CN, GZ_F_RES_JP, 0));
		cJSON_AddNumberToObject(jc, "eva", vm_get_field_int(g, GZ_F_EVA_CN, GZ_F_EVA_JP, 0));
		cJSON_AddNumberToObject(jc, "love", vm_get_field_int(g, GZ_F_LOVE_CN, GZ_F_LOVE_JP, 0));
		cJSON_AddItemToArray(chars, jc);
		added++;
	}
	cJSON_AddItemToObject(root, "exCharacters", chars);
}

static void ex_gz_locks(int mask)
{
	if ((mask & (1 << 0)) && ex_get_global_int(GZ_G_GOLD_CN, GZ_G_GOLD_JP, 0) < 999999)
		ex_set_global_int(GZ_G_GOLD_CN, GZ_G_GOLD_JP, 999999);
	if (mask & (1 << 1)) {
		/* fi[1] is the player; fi[2] is the active battle enemy. */
		struct page *fi = vm_get_global_page("fi", NULL);
		if (vm_dd_array_len(fi) > 2) {
			struct page *ef = vm_safe_get_page(vm_dd_array_elem_slot(fi, 2));
			if (ef && vm_get_field_int(ef, GZ_F_HP_CN, GZ_F_HP_JP, 0) > 1)
				vm_set_field_int(ef, GZ_F_HP_CN, GZ_F_HP_JP, 1);
		}
	}
	if (mask & (1 << 2))
		ex_set_global_int(GZ_G_DROP_CN, GZ_G_DROP_JP, 100);
	if (mask & (1 << 3))
		ex_set_global_int(GZ_G_NOENC_CN, GZ_G_NOENC_JP, 1);
	if (mask & (1 << 4)) {
		struct page *gi = vm_get_global_page("gi", NULL);
		for (int i = 0; i < vm_dd_array_len(gi); i++) {
			struct page *it = vm_safe_get_page(vm_dd_array_elem_slot(gi, i));
			if (it && vm_get_field_int(it, GZ_F_ICNT_CN, R06_F_ITEM_CNT, 0) > 0 &&
			    vm_get_field_int(it, GZ_F_ICNT_CN, R06_F_ITEM_CNT, 0) < 99)
				vm_set_field_int(it, GZ_F_ICNT_CN, R06_F_ITEM_CNT, 99);
		}
	}
}

static void ex_gz_action(cJSON *act)
{
	int32_t v;
	if (ex_json_int(act, "gold", &v)) ex_set_global_int(GZ_G_GOLD_CN, GZ_G_GOLD_JP, vm_dd_clamp(v, 0, 99999999));
	if (ex_json_int(act, "addGold", &v)) ex_set_global_int(GZ_G_GOLD_CN, GZ_G_GOLD_JP, vm_dd_clamp(ex_get_global_int(GZ_G_GOLD_CN, GZ_G_GOLD_JP, 0) + v, 0, 99999999));
	if (ex_json_int(act, "exp", &v)) ex_set_global_int(GZ_G_EXP_CN, GZ_G_EXP_JP, vm_dd_clamp(v, 0, 99999999));
	if (ex_json_cheat_is(act, "enemy1Hp")) ex_gz_locks(1 << 1);
	if (ex_json_cheat_is(act, "maxItems")) ex_gz_locks(1 << 4);

	cJSON *ce = cJSON_GetObjectItemCaseSensitive(act, "exCharEdit");
	if (ce && cJSON_IsObject(ce)) {
		struct page *gs = vm_get_global_page("gs", NULL);
		int32_t target = -1;
		int32_t boost_base = 0;
		ex_json_int(ce, "target", &target);
		ex_json_int(ce, "boostBase", &boost_base);
		for (int i = 1; i < vm_dd_array_len(gs); i++) {
			if (target >= 0 && i != target) continue;
			struct page *g = vm_safe_get_page(vm_dd_array_elem_slot(gs, i));
			if (!g || (target < 0 && vm_get_field_int(g, GZ_F_CAP_CN, GZ_F_CAP_JP, 0) <= 0)) continue;
			if (ex_json_int(ce, "lv", &v)) vm_set_field_int(g, GZ_F_LV_CN, EV_F_LV, vm_dd_clamp(v, 1, 999));
			if (ex_json_int(ce, "hp", &v)) {
				int32_t cv = vm_dd_clamp(v, 1, 99999);
				vm_set_field_int(g, GZ_F_HP_CN, GZ_F_HP_JP, cv);
				if (boost_base) vm_set_field_int(g, GZ_F_HP_BASE_CN, GZ_F_HP_BASE_JP, cv);
			}
			if (ex_json_int(ce, "atk", &v)) {
				int32_t cv = vm_dd_clamp(v, 0, 99999);
				vm_set_field_int(g, GZ_F_ATK_CN, GZ_F_ATK_JP, cv);
				if (boost_base) vm_set_field_int(g, GZ_F_ATK_BASE_CN, GZ_F_ATK_BASE_JP, cv);
			}
			if (ex_json_int(ce, "def", &v)) {
				int32_t cv = vm_dd_clamp(v, 0, 99999);
				vm_set_field_int(g, GZ_F_DEF_CN, GZ_F_DEF_JP, cv);
				if (boost_base) vm_set_field_int(g, GZ_F_DEF_BASE_CN, GZ_F_DEF_BASE_JP, cv);
			}
			if (ex_json_int(ce, "mag", &v)) {
				int32_t cv = vm_dd_clamp(v, 0, 99999);
				vm_set_field_int(g, GZ_F_MAG_CN, GZ_F_MAG_JP, cv);
				if (boost_base) vm_set_field_int(g, GZ_F_MAG_BASE_CN, GZ_F_MAG_BASE_JP, cv);
			}
			if (ex_json_int(ce, "res", &v)) {
				int32_t cv = vm_dd_clamp(v, 0, 99999);
				vm_set_field_int(g, GZ_F_RES_CN, GZ_F_RES_JP, cv);
				if (boost_base) vm_set_field_int(g, GZ_F_RES_BASE_CN, GZ_F_RES_BASE_JP, cv);
			}
			if (ex_json_int(ce, "eva", &v)) {
				int32_t cv = vm_dd_clamp(v, 0, 999);
				vm_set_field_int(g, GZ_F_EVA_CN, GZ_F_EVA_JP, cv);
				if (boost_base) vm_set_field_int(g, GZ_F_EVA_BASE_CN, GZ_F_EVA_BASE_JP, cv);
			}
			if (ex_json_int(ce, "love", &v)) {
				int lmax = vm_get_field_int(g, GZ_F_LMAX_CN, GZ_F_LMAX_JP, 9999);
				vm_set_field_int(g, GZ_F_LOVE_CN, GZ_F_LOVE_JP, vm_dd_clamp(v, 0, lmax > 0 ? lmax : 9999));
			}
		}
	}
}

/* =========================================================================
 * 6. Toushin Toshi III (toushin3)
 * ========================================================================= */
#define T3_G_GOLD      "\x81\x9f\x8b\xe0"
#define T3_G_NOENC     "\x81\x9f\x82\x63\x82\x61\x83\x47\x83\x93\x83\x4a\x83\x45\x83\x93\x83\x67\x8b\xd6\x8e\x7e"
#define T3_G_B_ALLY    "g_s\x90\xed\x93\xac\x96\xa1\x95\xfb\x83\x66\x81\x5b\x83\x5e"
#define T3_G_B_ENEMY   "g_s\x90\xed\x93\xac\x93\x47\x83\x66\x81\x5b\x83\x5e"

static struct page *ex_t3_param_arr(struct page *holder)
{
	struct page *sp = vm_get_field_page(holder, "m_sParam", NULL);
	return vm_get_field_page(sp, "m_anParam", NULL);
}

static void ex_t3_populate(cJSON *root)
{
	struct page *cd = vm_get_global_page("g_sCharaData", NULL);
	struct page *pa = ex_t3_param_arr(cd);
	cJSON_AddBoolToObject(root, "inGame", cd != NULL && vm_dd_array_len(pa) >= 10);
	struct page *be = vm_get_global_page(T3_G_B_ENEMY, NULL);
	struct page *ep = ex_t3_param_arr(be);
	cJSON_AddBoolToObject(root, "inBattle", ep != NULL && ex_arr_int_get(ep, 1, 0) > 0);
	cJSON_AddNumberToObject(root, "gold", ex_get_global_int(T3_G_GOLD, NULL, 0));
	cJSON_AddNumberToObject(root, "lv", ex_arr_int_get(pa, 0, 0));
	cJSON_AddNumberToObject(root, "hp", ex_arr_int_get(pa, 1, 0));
	cJSON_AddNumberToObject(root, "hpMax", ex_arr_int_get(pa, 2, 0));
	cJSON_AddNumberToObject(root, "atk", ex_arr_int_get(pa, 3, 0));
	cJSON_AddNumberToObject(root, "def", ex_arr_int_get(pa, 4, 0));
	cJSON_AddNumberToObject(root, "hit", ex_arr_int_get(pa, 5, 0));
	cJSON_AddNumberToObject(root, "eva", ex_arr_int_get(pa, 6, 0));
	cJSON_AddNumberToObject(root, "exp", vm_get_field_int(cd, "m_nExp", NULL, 0));
}

static void ex_t3_locks(int mask)
{
	if ((mask & (1 << 0)) && ex_get_global_int(T3_G_GOLD, NULL, 0) < 999999)
		ex_set_global_int(T3_G_GOLD, NULL, 999999);
	if (mask & (1 << 1)) {
		struct page *pa = ex_t3_param_arr(vm_get_global_page("g_sCharaData", NULL));
		int mhp = ex_arr_int_get(pa, 2, 0);
		if (mhp > 0) ex_arr_int_set(pa, 1, mhp);
		struct page *ba = ex_t3_param_arr(vm_get_global_page(T3_G_B_ALLY, NULL));
		int bmhp = ex_arr_int_get(ba, 2, 0);
		if (bmhp > 0) ex_arr_int_set(ba, 1, bmhp);
	}
	if (mask & (1 << 2)) {
		struct page *ep = ex_t3_param_arr(vm_get_global_page(T3_G_B_ENEMY, NULL));
		if (ex_arr_int_get(ep, 1, 0) > 1) ex_arr_int_set(ep, 1, 1);
	}
	if (mask & (1 << 3))
		ex_set_global_int(T3_G_NOENC, NULL, 1);
	if (mask & (1 << 4)) {
		struct page *gi = vm_get_global_page("gi", NULL);
		for (int i = 0; i < vm_dd_array_len(gi); i++) {
			struct page *it = vm_safe_get_page(vm_dd_array_elem_slot(gi, i));
			if (it && vm_get_field_int(it, R06_F_ITEM_CNT, NULL, 0) > 0 &&
			    vm_get_field_int(it, R06_F_ITEM_CNT, NULL, 0) < 99)
				vm_set_field_int(it, R06_F_ITEM_CNT, NULL, 99);
		}
	}
}

static void ex_t3_action(cJSON *act)
{
	struct page *cd = vm_get_global_page("g_sCharaData", NULL);
	struct page *pa = ex_t3_param_arr(cd);
	struct page *ba = ex_t3_param_arr(vm_get_global_page(T3_G_B_ALLY, NULL));
	if (ba && ex_arr_int_get(ba, 2, 0) <= 0)
		ba = NULL;
	int32_t v;
	if (ex_json_int(act, "gold", &v)) ex_set_global_int(T3_G_GOLD, NULL, vm_dd_clamp(v, 0, 99999999));
	if (ex_json_int(act, "addGold", &v)) ex_set_global_int(T3_G_GOLD, NULL, vm_dd_clamp(ex_get_global_int(T3_G_GOLD, NULL, 0) + v, 0, 99999999));
	if (ex_json_int(act, "lv", &v)) { int32_t cv = vm_dd_clamp(v, 1, 999); ex_arr_int_set(pa, 0, cv); ex_arr_int_set(ba, 0, cv); }
	if (ex_json_int(act, "hp", &v)) { int32_t cv = vm_dd_clamp(v, 0, 999999); ex_arr_int_set(pa, 1, cv); ex_arr_int_set(ba, 1, cv); }
	if (ex_json_int(act, "hpMax", &v)) { int32_t cv = vm_dd_clamp(v, 1, 999999); ex_arr_int_set(pa, 2, cv); ex_arr_int_set(ba, 2, cv); }
	if (ex_json_int(act, "atk", &v)) { int32_t cv = vm_dd_clamp(v, 0, 99999); ex_arr_int_set(pa, 3, cv); ex_arr_int_set(ba, 3, cv); }
	if (ex_json_int(act, "def", &v)) { int32_t cv = vm_dd_clamp(v, 0, 99999); ex_arr_int_set(pa, 4, cv); ex_arr_int_set(ba, 4, cv); }
	if (ex_json_int(act, "hit", &v)) { int32_t cv = vm_dd_clamp(v, 0, 100); ex_arr_int_set(pa, 5, cv); ex_arr_int_set(ba, 5, cv); }
	if (ex_json_int(act, "eva", &v)) { int32_t cv = vm_dd_clamp(v, 0, 100); ex_arr_int_set(pa, 6, cv); ex_arr_int_set(ba, 6, cv); }
	if (ex_json_int(act, "exp", &v) && cd) vm_set_field_int(cd, "m_nExp", NULL, vm_dd_clamp(v, 0, 99999999));
	if (ex_json_cheat_is(act, "fullHeal")) ex_t3_locks(1 << 1);
	if (ex_json_cheat_is(act, "enemy1Hp")) ex_t3_locks(1 << 2);
	if (ex_json_cheat_is(act, "maxItems")) ex_t3_locks(1 << 4);
	if (ex_json_cheat_is(act, "boostAll") && (pa || ba)) {
		struct page *targets[2] = { pa, ba };
		for (int t = 0; t < 2; t++) {
			if (!targets[t]) continue;
			ex_arr_int_set(targets[t], 0, 99);
			ex_arr_int_set(targets[t], 2, 9999);
			ex_arr_int_set(targets[t], 1, 9999);
			ex_arr_int_set(targets[t], 3, 9999);
			ex_arr_int_set(targets[t], 4, 9999);
			ex_arr_int_set(targets[t], 5, 100);
			ex_arr_int_set(targets[t], 6, 100);
			ex_arr_int_set(targets[t], 7, 100);
		}
	}
}

/* =========================================================================
 * 7. Blade Briders (bladebriders)
 * ========================================================================= */
static void ex_bb_populate(cJSON *root)
{
	struct page *gd = vm_get_global_page("g_GrobalData", NULL);
	struct page *game = vm_get_field_page(gd, "m_GameData", NULL);
	struct page *chap = vm_get_field_page(gd, "m_ChapterData", NULL);
	struct page *allies = vm_get_field_page(game, "m_aAllyUnitData", NULL);
	struct page *fld = vm_get_global_page("g_rFieldDataManager", NULL);
	cJSON_AddBoolToObject(root, "inGame", game != NULL && vm_dd_array_len(allies) > 0);
	cJSON_AddBoolToObject(root, "inBattle", fld != NULL && vm_get_field_int(gd, "m_bBattleScene", NULL, 0) != 0);
	cJSON_AddNumberToObject(root, "gold", vm_get_field_int(game, "m_nMoney", NULL, 0));
	cJSON_AddNumberToObject(root, "ap", vm_get_field_int(chap, "m_nActionPoint", NULL, 0));
	cJSON_AddNumberToObject(root, "turn", vm_get_field_int(chap, "m_nNowTurn", NULL, 0));

	cJSON *chars = cJSON_CreateArray();
	for (int i = 0; i < vm_dd_array_len(allies) && i < 48; i++) {
		struct page *au = vm_safe_get_page(vm_dd_array_elem_slot(allies, i));
		struct page *st = vm_get_field_page(au, "m_UnitStatusData", NULL);
		if (!st) continue;
		char *nm = vm_dd_get_field_utf8(st, "m_szUnitName", NULL);
		if (!nm[0]) { free(nm); continue; }
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddStringToObject(jc, "name", nm);
		free(nm);
		cJSON_AddNumberToObject(jc, "lv", vm_get_field_int(st, "m_nLevel", NULL, 1));
		cJSON_AddNumberToObject(jc, "exp", vm_get_field_int(st, "m_nEXP", NULL, 0));
		cJSON_AddNumberToObject(jc, "hp", vm_get_field_int(st, "m_nHP", NULL, 0));
		cJSON_AddNumberToObject(jc, "hpMax", vm_get_field_int(st, "m_nHPMax", NULL, 0));
		cJSON_AddNumberToObject(jc, "atk", vm_get_field_int(st, "m_nAttack", NULL, 0));
		cJSON_AddNumberToObject(jc, "def", vm_get_field_int(st, "m_nDefence", NULL, 0));
		cJSON_AddNumberToObject(jc, "hit", vm_get_field_int(st, "m_nHit", NULL, 0));
		cJSON_AddNumberToObject(jc, "eva", vm_get_field_int(st, "m_nAvoidance", NULL, 0));
		cJSON_AddNumberToObject(jc, "move", vm_get_field_int(st, "m_nMovePower", NULL, 0));
		cJSON_AddItemToArray(chars, jc);
	}
	cJSON_AddItemToObject(root, "exCharacters", chars);
}

static void ex_bb_locks(int mask)
{
	struct page *gd = vm_get_global_page("g_GrobalData", NULL);
	struct page *game = vm_get_field_page(gd, "m_GameData", NULL);
	struct page *chap = vm_get_field_page(gd, "m_ChapterData", NULL);
	if (!game) return;
	if ((mask & (1 << 0)) && vm_get_field_int(game, "m_nMoney", NULL, 0) < 999999)
		vm_set_field_int(game, "m_nMoney", NULL, 999999);
	if (mask & (1 << 1)) {
		struct page *allies = vm_get_field_page(game, "m_aAllyUnitData", NULL);
		for (int i = 0; i < vm_dd_array_len(allies); i++) {
			struct page *au = vm_safe_get_page(vm_dd_array_elem_slot(allies, i));
			struct page *st = vm_get_field_page(au, "m_UnitStatusData", NULL);
			int mhp = vm_get_field_int(st, "m_nHPMax", NULL, 0);
			if (st && mhp > 0) vm_set_field_int(st, "m_nHP", NULL, mhp);
		}
		struct page *fld = vm_get_global_page("g_rFieldDataManager", NULL);
		struct page *fa = vm_get_field_page(fld, "m_aAllyUnitObject", NULL);
		for (int i = 0; i < vm_dd_array_len(fa); i++) {
			struct page *uo = vm_safe_get_page(vm_dd_array_elem_slot(fa, i));
			struct page *ud = vm_get_field_page(uo, "m_UnitData", NULL);
			struct page *st = vm_get_field_page(ud, "m_StatusData", NULL);
			int mhp = vm_get_field_int(st, "m_nHPMax", NULL, 0);
			if (st && mhp > 0) vm_set_field_int(st, "m_nHP", NULL, mhp);
		}
	}
	if (mask & (1 << 2)) {
		struct page *fld = vm_get_global_page("g_rFieldDataManager", NULL);
		struct page *fe = vm_get_field_page(fld, "m_aAIUnitObject", NULL);
		for (int i = 0; i < vm_dd_array_len(fe); i++) {
			struct page *uo = vm_safe_get_page(vm_dd_array_elem_slot(fe, i));
			struct page *ud = vm_get_field_page(uo, "m_UnitData", NULL);
			struct page *st = vm_get_field_page(ud, "m_StatusData", NULL);
			if (st && vm_get_field_int(st, "m_nHP", NULL, 0) > 1)
				vm_set_field_int(st, "m_nHP", NULL, 1);
		}
	}
	if ((mask & (1 << 3)) && chap && vm_get_field_int(chap, "m_nActionPoint", NULL, 0) < 99)
		vm_set_field_int(chap, "m_nActionPoint", NULL, 99);
	if (mask & (1 << 4))
		vm_set_field_int(game, "m_bGrowthBonus", NULL, 1);
}

static void ex_bb_apply_char_edit(struct page *st, cJSON *ce)
{
	if (!st || !ce) return;
	int32_t v;
	if (ex_json_int(ce, "lv", &v)) vm_set_field_int(st, "m_nLevel", NULL, vm_dd_clamp(v, 1, 99));
	if (ex_json_int(ce, "exp", &v)) vm_set_field_int(st, "m_nEXP", NULL, vm_dd_clamp(v, 0, 99999));
	if (ex_json_int(ce, "hpMax", &v)) { vm_set_field_int(st, "m_nHPMax", NULL, vm_dd_clamp(v, 1, 99999)); vm_set_field_int(st, "m_nBaseHPMax", NULL, vm_dd_clamp(v, 1, 99999)); }
	if (ex_json_int(ce, "hp", &v)) { vm_set_field_int(st, "m_nHP", NULL, vm_dd_clamp(v, 0, 99999)); vm_set_field_int(st, "m_nBaseHP", NULL, vm_dd_clamp(v, 0, 99999)); }
	if (ex_json_int(ce, "atk", &v)) { vm_set_field_int(st, "m_nAttack", NULL, vm_dd_clamp(v, 0, 9999)); vm_set_field_int(st, "m_nBaseAttack", NULL, vm_dd_clamp(v, 0, 9999)); }
	if (ex_json_int(ce, "def", &v)) { vm_set_field_int(st, "m_nDefence", NULL, vm_dd_clamp(v, 0, 9999)); vm_set_field_int(st, "m_nBaseDefence", NULL, vm_dd_clamp(v, 0, 9999)); }
	if (ex_json_int(ce, "hit", &v)) { vm_set_field_int(st, "m_nHit", NULL, vm_dd_clamp(v, 0, 999)); vm_set_field_int(st, "m_nBaseHit", NULL, vm_dd_clamp(v, 0, 999)); }
	if (ex_json_int(ce, "eva", &v)) { vm_set_field_int(st, "m_nAvoidance", NULL, vm_dd_clamp(v, 0, 999)); vm_set_field_int(st, "m_nBaseAvoidance", NULL, vm_dd_clamp(v, 0, 999)); }
	if (ex_json_int(ce, "move", &v)) { vm_set_field_int(st, "m_nMovePower", NULL, vm_dd_clamp(v, 1, 30)); vm_set_field_int(st, "m_nBaseMovePower", NULL, vm_dd_clamp(v, 1, 30)); }
	if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(ce, "fullHeal"))) {
		int mhp = vm_get_field_int(st, "m_nHPMax", NULL, 0);
		if (mhp > 0) vm_set_field_int(st, "m_nHP", NULL, mhp);
	}
}

static void ex_bb_action(cJSON *act)
{
	struct page *gd = vm_get_global_page("g_GrobalData", NULL);
	struct page *game = vm_get_field_page(gd, "m_GameData", NULL);
	struct page *chap = vm_get_field_page(gd, "m_ChapterData", NULL);
	int32_t v;
	if (ex_json_int(act, "gold", &v) && game) vm_set_field_int(game, "m_nMoney", NULL, vm_dd_clamp(v, 0, 99999999));
	if (ex_json_int(act, "addGold", &v) && game) vm_set_field_int(game, "m_nMoney", NULL, vm_dd_clamp(vm_get_field_int(game, "m_nMoney", NULL, 0) + v, 0, 99999999));
	if (ex_json_int(act, "ap", &v) && chap) vm_set_field_int(chap, "m_nActionPoint", NULL, vm_dd_clamp(v, 0, 999));
	if (ex_json_cheat_is(act, "fullHeal")) ex_bb_locks(1 << 1);
	if (ex_json_cheat_is(act, "enemy1Hp")) ex_bb_locks(1 << 2);

	cJSON *ce = cJSON_GetObjectItemCaseSensitive(act, "exCharEdit");
	if (ce && cJSON_IsObject(ce) && game) {
		struct page *allies = vm_get_field_page(game, "m_aAllyUnitData", NULL);
		int32_t target = -1;
		ex_json_int(ce, "target", &target);
		bool found_target = false;
		char *target_name = NULL;
		int32_t target_join = -1;
		for (int i = 0; i < vm_dd_array_len(allies); i++) {
			if (target >= 0 && i != target) continue;
			struct page *au = vm_safe_get_page(vm_dd_array_elem_slot(allies, i));
			struct page *st = vm_get_field_page(au, "m_UnitStatusData", NULL);
			if (!st) continue;
			if (target >= 0 && !found_target) {
				found_target = true;
				target_name = vm_dd_get_field_utf8(st, "m_szUnitName", NULL);
				target_join = vm_get_field_int(st, "m_eJoinFlagNo", NULL, -1);
			}
			ex_bb_apply_char_edit(st, ce);
		}
		if (target < 0 || found_target) {
			struct page *fld = vm_get_global_page("g_rFieldDataManager", NULL);
			struct page *fa = vm_get_field_page(fld, "m_aAllyUnitObject", NULL);
			for (int i = 0; i < vm_dd_array_len(fa); i++) {
				struct page *uo = vm_safe_get_page(vm_dd_array_elem_slot(fa, i));
				struct page *ud = vm_get_field_page(uo, "m_UnitData", NULL);
				struct page *bst = vm_get_field_page(ud, "m_StatusData", NULL);
				if (!bst) continue;
				if (target >= 0) {
					bool match = false;
					if (target_name && target_name[0]) {
						char *bnm = vm_dd_get_field_utf8(bst, "m_szUnitName", NULL);
						match = (strcmp(bnm, target_name) == 0);
						free(bnm);
					} else if (target_join > 0) {
						match = (vm_get_field_int(bst, "m_eJoinFlagNo", NULL, -1) == target_join);
					} else {
						match = (i == target);
					}
					if (!match) continue;
				}
				ex_bb_apply_char_edit(bst, ce);
			}
		}
		free(target_name);
	}
}

/* =========================================================================
 * 8. Yoru ga Kuru! Remastered (yorugakuru)
 * ========================================================================= */
static char *ex_yk_find_name(struct page *cdarr, int32_t id)
{
	for (int i = 0; i < vm_dd_array_len(cdarr); i++) {
		struct page *cd = vm_safe_get_page(vm_dd_array_elem_slot(cdarr, i));
		if (cd && vm_get_field_int(cd, "<Id>", NULL, -1) == id)
			return vm_dd_get_field_utf8(cd, "<sName>", NULL);
	}
	return strdup("");
}

static void ex_yk_populate(cJSON *root)
{
	struct page *abs = vm_get_global_page("LAbility::m_pAbilities", NULL);
	struct page *cds = vm_get_global_page("LAbility::m_aCharacterData", NULL);
	int n = vm_dd_array_len(abs);
	struct page *ab0 = n > 0 ? vm_safe_get_page(vm_dd_array_elem_slot(abs, 0)) : NULL;
	cJSON_AddBoolToObject(root, "inGame", n > 0 && ab0 != NULL);
	cJSON_AddBoolToObject(root, "inBattle", false);
	cJSON_AddNumberToObject(root, "gold", vm_get_field_int(ab0, "money", NULL, 0));

	cJSON *chars = cJSON_CreateArray();
	for (int i = 0; i < n && i < 24; i++) {
		struct page *a = vm_safe_get_page(vm_dd_array_elem_slot(abs, i));
		if (!a) continue;
		int32_t id = vm_get_field_int(a, "id", NULL, i);
		char *nm = ex_yk_find_name(cds, id);
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddStringToObject(jc, "name", nm);
		free(nm);
		cJSON_AddNumberToObject(jc, "lv", vm_get_field_int(a, "level", NULL, 1));
		cJSON_AddNumberToObject(jc, "hp", vm_get_field_int(a, "hp", NULL, 0));
		cJSON_AddNumberToObject(jc, "hpMax", vm_get_field_int(a, "hp_max", NULL, 0));
		cJSON_AddNumberToObject(jc, "exp", vm_get_field_int(a, "experience", NULL, 0));
		cJSON_AddNumberToObject(jc, "atk", vm_get_field_int(a, "STR", NULL, 0));
		cJSON_AddNumberToObject(jc, "spd", vm_get_field_int(a, "QIK", NULL, 0));
		cJSON_AddNumberToObject(jc, "hit", vm_get_field_int(a, "DEX", NULL, 0));
		cJSON_AddNumberToObject(jc, "mag", vm_get_field_int(a, "SPR", NULL, 0));
		cJSON_AddItemToArray(chars, jc);
	}
	cJSON_AddItemToObject(root, "exCharacters", chars);
}

static void ex_yk_locks(int mask)
{
	struct page *abs = vm_get_global_page("LAbility::m_pAbilities", NULL);
	int n = vm_dd_array_len(abs);
	for (int i = 0; i < n; i++) {
		struct page *a = vm_safe_get_page(vm_dd_array_elem_slot(abs, i));
		if (!a) continue;
		if ((mask & (1 << 0)) && vm_get_field_int(a, "money", NULL, 0) < 999999)
			vm_set_field_int(a, "money", NULL, 999999);
		if (mask & (1 << 1)) {
			int mhp = vm_get_field_int(a, "hp_max", NULL, 0);
			if (mhp > 0) vm_set_field_int(a, "hp", NULL, mhp);
		}
		if (mask & (1 << 2)) {
			vm_set_field_int(a, "m_nDown", NULL, 0);
			vm_set_field_int(a, "m_nPoison", NULL, 0);
			vm_set_field_int(a, "m_nParalysis", NULL, 0);
			vm_set_field_int(a, "m_nSleep", NULL, 0);
			vm_set_field_int(a, "m_nBound", NULL, 0);
			vm_set_field_int(a, "m_nConfusion", NULL, 0);
		}
	}
	if (mask & (1 << 3)) {
		ex_set_global_int("L_CasualMode::RateMoney", NULL, 200);
		ex_set_global_int("L_CasualMode::IsRecoverAfterBattle", NULL, 1);
		ex_set_global_int("L_CasualMode::IsExpandConsumeItem", NULL, 1);
	}
}

static void ex_yk_action(cJSON *act)
{
	struct page *abs = vm_get_global_page("LAbility::m_pAbilities", NULL);
	int n = vm_dd_array_len(abs);
	int32_t v;
	if (ex_json_int(act, "gold", &v)) {
		for (int i = 0; i < n; i++) {
			struct page *a = vm_safe_get_page(vm_dd_array_elem_slot(abs, i));
			if (a) vm_set_field_int(a, "money", NULL, vm_dd_clamp(v, 0, 999999));
		}
	}
	if (ex_json_int(act, "addGold", &v)) {
		for (int i = 0; i < n; i++) {
			struct page *a = vm_safe_get_page(vm_dd_array_elem_slot(abs, i));
			if (a) vm_set_field_int(a, "money", NULL, vm_dd_clamp(vm_get_field_int(a, "money", NULL, 0) + v, 0, 999999));
		}
	}
	if (ex_json_cheat_is(act, "fullHeal")) ex_yk_locks((1 << 1) | (1 << 2));

	cJSON *ce = cJSON_GetObjectItemCaseSensitive(act, "exCharEdit");
	if (ce && cJSON_IsObject(ce)) {
		int32_t target = -1;
		ex_json_int(ce, "target", &target);
		for (int i = 0; i < n; i++) {
			if (target >= 0 && i != target) continue;
			struct page *a = vm_safe_get_page(vm_dd_array_elem_slot(abs, i));
			if (!a) continue;
			if (ex_json_int(ce, "lv", &v)) vm_set_field_int(a, "level", NULL, vm_dd_clamp(v, 1, 99));
			if (ex_json_int(ce, "hpMax", &v)) vm_set_field_int(a, "hp_max", NULL, vm_dd_clamp(v, 1, 99999));
			if (ex_json_int(ce, "hp", &v)) vm_set_field_int(a, "hp", NULL, vm_dd_clamp(v, 0, 99999));
			if (ex_json_int(ce, "exp", &v)) vm_set_field_int(a, "experience", NULL, vm_dd_clamp(v, 0, 9999999));
			if (ex_json_int(ce, "atk", &v)) { vm_set_field_int(a, "STR", NULL, vm_dd_clamp(v, 0, 9999)); vm_set_field_int(a, "strength", NULL, vm_dd_clamp(v, 0, 9999)); }
			if (ex_json_int(ce, "spd", &v)) { vm_set_field_int(a, "QIK", NULL, vm_dd_clamp(v, 0, 9999)); vm_set_field_int(a, "quickness", NULL, vm_dd_clamp(v, 0, 9999)); }
			if (ex_json_int(ce, "hit", &v)) { vm_set_field_int(a, "DEX", NULL, vm_dd_clamp(v, 0, 9999)); vm_set_field_int(a, "dexterity", NULL, vm_dd_clamp(v, 0, 9999)); }
			if (ex_json_int(ce, "mag", &v)) { vm_set_field_int(a, "SPR", NULL, vm_dd_clamp(v, 0, 9999)); vm_set_field_int(a, "spirit", NULL, vm_dd_clamp(v, 0, 9999)); }
			if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(ce, "fullHeal"))) {
				int mhp = vm_get_field_int(a, "hp_max", NULL, 0);
				if (mhp > 0) vm_set_field_int(a, "hp", NULL, mhp);
			}
		}
	}
}

/* =========================================================================
 * 9. Waidonyo (waidonyo)
 * ========================================================================= */
static void ex_wd_populate(cJSON *root)
{
	struct page *cm = vm_get_global_page("g_sCharaData", NULL);
	struct page *arr = vm_get_field_page(cm, "m_asData", NULL);
	int n = vm_dd_array_len(arr);
	struct page *sd = vm_get_global_page("g_anSaveData", NULL);
	cJSON_AddBoolToObject(root, "inGame", n > 0 || vm_dd_array_len(sd) > 3);
	struct page *bt = vm_get_global_page("g_sBattle", NULL);
	cJSON_AddBoolToObject(root, "inBattle", bt != NULL && vm_get_field_int(bt, "m_nMode", NULL, 0) > 0);
	int32_t gold = ex_arr_int_get(sd, 3, 0);
	cJSON_AddNumberToObject(root, "gold", gold);

	cJSON *chars = cJSON_CreateArray();
	for (int i = 0; i < n && i < 48; i++) {
		struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
		struct page *sp = vm_get_field_page(c, "m_sParam", NULL);
		struct page *pa = vm_get_field_page(sp, "m_anParam", NULL);
		if (!sp || vm_dd_array_len(pa) < 9) continue;
		char *nm = vm_dd_get_field_utf8(sp, "m_szName", NULL);
		if (!nm[0]) { free(nm); continue; }
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddStringToObject(jc, "name", nm);
		free(nm);
		cJSON_AddNumberToObject(jc, "lv", ex_arr_int_get(pa, 0, 1));
		cJSON_AddNumberToObject(jc, "hp", ex_arr_int_get(pa, 1, 0));
		cJSON_AddNumberToObject(jc, "hpMax", ex_arr_int_get(pa, 2, 0));
		cJSON_AddNumberToObject(jc, "atk", ex_arr_int_get(pa, 3, 0));
		cJSON_AddNumberToObject(jc, "def", ex_arr_int_get(pa, 4, 0));
		cJSON_AddNumberToObject(jc, "mag", ex_arr_int_get(pa, 5, 0));
		cJSON_AddNumberToObject(jc, "res", ex_arr_int_get(pa, 6, 0));
		cJSON_AddNumberToObject(jc, "hit", ex_arr_int_get(pa, 7, 0));
		cJSON_AddItemToArray(chars, jc);
	}
	cJSON_AddItemToObject(root, "exCharacters", chars);
}

static void ex_wd_locks(int mask)
{
	struct page *cm = vm_get_global_page("g_sCharaData", NULL);
	struct page *arr = vm_get_field_page(cm, "m_asData", NULL);
	if (mask & (1 << 0)) {
		for (int i = 0; i < vm_dd_array_len(arr); i++) {
			struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
			struct page *sp = vm_get_field_page(c, "m_sParam", NULL);
			struct page *pa = vm_get_field_page(sp, "m_anParam", NULL);
			if (!pa) continue;
			int mhp = ex_arr_int_get(pa, 2, 0);
			if (mhp > 0) ex_arr_int_set(pa, 1, mhp);
		}
	}
	if (mask & (1 << 1)) {
		struct page *sd = vm_get_global_page("g_anSaveData", NULL);
		if (sd && ex_arr_int_get(sd, 3, 0) < 999999)
			ex_arr_int_set(sd, 3, 999999);
	}
	if (mask & (1 << 2)) {
		struct page *mm = vm_get_global_page("g_sMonsterData", NULL);
		struct page *marr = vm_get_field_page(mm, "m_asData", NULL);
		for (int i = 0; i < vm_dd_array_len(marr); i++) {
			struct page *m = vm_safe_get_page(vm_dd_array_elem_slot(marr, i));
			struct page *sp = vm_get_field_page(m, "m_sParam", NULL);
			struct page *pa = vm_get_field_page(sp, "m_anParam", NULL);
			if (pa && ex_arr_int_get(pa, 1, 0) > 1)
				ex_arr_int_set(pa, 1, 1);
		}
	}
}

static void ex_wd_action(cJSON *act)
{
	struct page *cm = vm_get_global_page("g_sCharaData", NULL);
	struct page *arr = vm_get_field_page(cm, "m_asData", NULL);
	int32_t v;
	if (ex_json_int(act, "gold", &v)) {
		struct page *sd = vm_get_global_page("g_anSaveData", NULL);
		if (sd) ex_arr_int_set(sd, 3, vm_dd_clamp(v, 0, 999999999));
	}
	if (ex_json_int(act, "addGold", &v)) {
		struct page *sd = vm_get_global_page("g_anSaveData", NULL);
		if (sd) {
			int64_t next = (int64_t)ex_arr_int_get(sd, 3, 0) + v;
			int32_t clamped = next > 999999999 ? 999999999 : (next < 0 ? 0 : (int32_t)next);
			ex_arr_int_set(sd, 3, clamped);
		}
	}
	if (ex_json_cheat_is(act, "fullHeal")) ex_wd_locks(1 << 0);
	if (ex_json_cheat_is(act, "enemy1Hp")) ex_wd_locks(1 << 2);

	cJSON *ce = cJSON_GetObjectItemCaseSensitive(act, "exCharEdit");
	if (ce && cJSON_IsObject(ce)) {
		int32_t target = -1;
		ex_json_int(ce, "target", &target);
		for (int i = 0; i < vm_dd_array_len(arr); i++) {
			if (target >= 0 && i != target) continue;
			struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
			struct page *pa = vm_get_field_page(vm_get_field_page(c, "m_sParam", NULL), "m_anParam", NULL);
			if (!pa || vm_dd_array_len(pa) < 9) continue;
			if (ex_json_int(ce, "lv", &v)) ex_arr_int_set(pa, 0, vm_dd_clamp(v, 1, 999));
			if (ex_json_int(ce, "hpMax", &v)) ex_arr_int_set(pa, 2, vm_dd_clamp(v, 1, 999999));
			if (ex_json_int(ce, "hp", &v)) ex_arr_int_set(pa, 1, vm_dd_clamp(v, 0, 999999));
			if (ex_json_int(ce, "atk", &v)) ex_arr_int_set(pa, 3, vm_dd_clamp(v, 0, 99999));
			if (ex_json_int(ce, "def", &v)) ex_arr_int_set(pa, 4, vm_dd_clamp(v, 0, 99999));
			if (ex_json_int(ce, "mag", &v)) ex_arr_int_set(pa, 5, vm_dd_clamp(v, 0, 99999));
			if (ex_json_int(ce, "res", &v)) ex_arr_int_set(pa, 6, vm_dd_clamp(v, 0, 99999));
			if (ex_json_int(ce, "hit", &v)) ex_arr_int_set(pa, 7, vm_dd_clamp(v, 0, 99999));
			if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(ce, "fullHeal"))) {
				int mhp = ex_arr_int_get(pa, 2, 0);
				if (mhp > 0) ex_arr_int_set(pa, 1, mhp);
			}
		}
	}
}

/* =========================================================================
 * 10. Sanbiki (sanbiki) — supports both CN GBK and JP SJIS .ain names!
 * ========================================================================= */
#define SB_G_GOLD_CN  "g_n\xbd\xf0"
#define SB_G_GOLD_JP  "g_n\x8b\xe0"
#define SB_G_LUCK_CN  "g_n\xd0\xd2\xdf\x5c"
#define SB_G_LUCK_JP  "g_n\x8d\x4b\x89\x5e"
#define SB_G_BAD_CN   "g_n\xb2\xbb\xdf\x5c"
#define SB_G_BAD_JP   "g_n\x95\x73\x89\x5e"
#define SB_G_CHR_CN   "\xa5\xad\xa5\xe3\xa5\xe9\xa5\xaf\xa5\xbf"
#define SB_G_CHR_JP   "\x83\x4c\x83\x83\x83\x89\x83\x4e\x83\x5e"
#define SB_G_ITEM_CN  "\xb5\xc0\xbe\xdf"
#define SB_G_ITEM_JP  "\x93\xb9\x8b\xef"
#define SB_F_NAME_CN  "g_sz\xc3\xfb\xb3\xc6"
#define SB_F_NAME_JP  "g_sz\x96\xbc\x8f\xcc"
#define SB_F_HP_CN    "g_n\xac\x46\xd4\xda\xcc\xe5\xc1\xa6"
#define SB_F_HP_JP    "g_n\x8c\xbb\x8d\xdd\x91\xcc\x97\xcd"
#define SB_F_HPMAX_CN "g_n\xcc\xe5\xc1\xa6"
#define SB_F_HPMAX_JP "g_n\x91\xcc\x97\xcd"
#define SB_F_ATK_CN   "g_n\xb9\xa5"
#define SB_F_ATK_JP   "g_n\x8d\x55"
#define SB_F_CATK_CN  "g_n\xac\x46\xd4\xda\xb9\xa5"
#define SB_F_CATK_JP  "g_n\x8c\xbb\x8d\xdd\x8d\x55"
#define SB_F_DEF_CN   "g_n\xb7\xc0"
#define SB_F_DEF_JP   "g_n\x96\x68"
#define SB_F_CDEF_CN  "g_n\xac\x46\xd4\xda\xb7\xc0"
#define SB_F_CDEF_JP  "g_n\x8c\xbb\x8d\xdd\x96\x68"
#define SB_F_INT_CN   "g_n\xd6\xaa"
#define SB_F_INT_JP   "g_n\x92\x6d"
#define SB_F_CINT_CN  "g_n\xac\x46\xd4\xda\xd6\xaa"
#define SB_F_CINT_JP  "g_n\x8c\xbb\x8d\xdd\x92\x6d"
#define SB_F_SPD_CN   "g_n\xcb\xd9"
#define SB_F_SPD_JP   "g_n\x91\xac"
#define SB_F_CSPD_CN  "g_n\xac\x46\xd4\xda\xcb\xd9"
#define SB_F_CSPD_JP  "g_n\x8c\xbb\x8d\xdd\x91\xac"
#define SB_F_ICNT_CN  "g_n\xcb\xf9\xb3\xd6\xca\xfd"
#define SB_F_ICNT_JP  "g_n\x8f\x8a\x8e\x9d\x90\x94"
#define SB_F_ILIM_CN  "g_n\xcf\xde\xbd\xe7"
#define SB_F_ILIM_JP  "g_n\x8c\xc0\x8a\x45"

static void ex_sb_populate(cJSON *root)
{
	struct page *chrs = vm_get_global_page(SB_G_CHR_CN, SB_G_CHR_JP);
	int n = vm_dd_array_len(chrs);
	cJSON_AddBoolToObject(root, "inGame", n > 0);
	cJSON_AddBoolToObject(root, "inBattle", false);
	cJSON_AddNumberToObject(root, "gold", ex_get_global_int(SB_G_GOLD_CN, SB_G_GOLD_JP, 0));
	cJSON_AddNumberToObject(root, "luck", ex_get_global_int(SB_G_LUCK_CN, SB_G_LUCK_JP, 0));

	cJSON *chars = cJSON_CreateArray();
	int added = 0;
	for (int i = 0; i < n && added < 32; i++) {
		struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(chrs, i));
		if (!c || vm_get_field_int(c, SB_F_HPMAX_CN, SB_F_HPMAX_JP, 0) <= 0) continue;
		char *nm = vm_dd_get_field_utf8(c, SB_F_NAME_CN, SB_F_NAME_JP);
		if (!nm[0]) { free(nm); continue; }
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddStringToObject(jc, "name", nm);
		free(nm);
		cJSON_AddNumberToObject(jc, "hp", vm_get_field_int(c, SB_F_HP_CN, SB_F_HP_JP, 0));
		cJSON_AddNumberToObject(jc, "hpMax", vm_get_field_int(c, SB_F_HPMAX_CN, SB_F_HPMAX_JP, 0));
		cJSON_AddNumberToObject(jc, "atk", vm_get_field_int(c, SB_F_ATK_CN, SB_F_ATK_JP, 0));
		cJSON_AddNumberToObject(jc, "def", vm_get_field_int(c, SB_F_DEF_CN, SB_F_DEF_JP, 0));
		cJSON_AddNumberToObject(jc, "mag", vm_get_field_int(c, SB_F_INT_CN, SB_F_INT_JP, 0));
		cJSON_AddNumberToObject(jc, "spd", vm_get_field_int(c, SB_F_SPD_CN, SB_F_SPD_JP, 0));
		cJSON_AddItemToArray(chars, jc);
		added++;
	}
	cJSON_AddItemToObject(root, "exCharacters", chars);
}

static void ex_sb_locks(int mask)
{
	if ((mask & (1 << 0)) && ex_get_global_int(SB_G_GOLD_CN, SB_G_GOLD_JP, 0) < 999999)
		ex_set_global_int(SB_G_GOLD_CN, SB_G_GOLD_JP, 999999);
	if (mask & (1 << 1)) {
		struct page *chrs = vm_get_global_page(SB_G_CHR_CN, SB_G_CHR_JP);
		for (int i = 0; i < vm_dd_array_len(chrs); i++) {
			struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(chrs, i));
			int mhp = vm_get_field_int(c, SB_F_HPMAX_CN, SB_F_HPMAX_JP, 0);
			if (c && mhp > 0) vm_set_field_int(c, SB_F_HP_CN, SB_F_HP_JP, mhp);
		}
	}
	if (mask & (1 << 2)) {
		if (ex_get_global_int(SB_G_LUCK_CN, SB_G_LUCK_JP, 0) < 999)
			ex_set_global_int(SB_G_LUCK_CN, SB_G_LUCK_JP, 999);
		ex_set_global_int(SB_G_BAD_CN, SB_G_BAD_JP, 0);
	}
	if (mask & (1 << 3)) {
		struct page *items = vm_get_global_page(SB_G_ITEM_CN, SB_G_ITEM_JP);
		for (int i = 0; i < vm_dd_array_len(items); i++) {
			struct page *it = vm_safe_get_page(vm_dd_array_elem_slot(items, i));
			if (!it) continue;
			int cnt = vm_get_field_int(it, SB_F_ICNT_CN, SB_F_ICNT_JP, 0);
			int lim = vm_get_field_int(it, SB_F_ILIM_CN, SB_F_ILIM_JP, 99);
			if (lim <= 0) lim = 99;
			if (cnt > 0 && cnt < lim) vm_set_field_int(it, SB_F_ICNT_CN, SB_F_ICNT_JP, lim);
		}
	}
}

static void ex_sb_action(cJSON *act)
{
	int32_t v;
	if (ex_json_int(act, "gold", &v)) ex_set_global_int(SB_G_GOLD_CN, SB_G_GOLD_JP, vm_dd_clamp(v, 0, 99999999));
	if (ex_json_int(act, "addGold", &v)) ex_set_global_int(SB_G_GOLD_CN, SB_G_GOLD_JP, vm_dd_clamp(ex_get_global_int(SB_G_GOLD_CN, SB_G_GOLD_JP, 0) + v, 0, 99999999));
	if (ex_json_int(act, "luck", &v)) ex_set_global_int(SB_G_LUCK_CN, SB_G_LUCK_JP, vm_dd_clamp(v, 0, 9999));
	if (ex_json_cheat_is(act, "fullHeal")) ex_sb_locks(1 << 1);
	if (ex_json_cheat_is(act, "maxItems")) ex_sb_locks(1 << 3);

	cJSON *ce = cJSON_GetObjectItemCaseSensitive(act, "exCharEdit");
	if (ce && cJSON_IsObject(ce)) {
		struct page *chrs = vm_get_global_page(SB_G_CHR_CN, SB_G_CHR_JP);
		int32_t target = -1;
		ex_json_int(ce, "target", &target);
		for (int i = 0; i < vm_dd_array_len(chrs); i++) {
			if (target >= 0 && i != target) continue;
			struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(chrs, i));
			if (!c || vm_get_field_int(c, SB_F_HPMAX_CN, SB_F_HPMAX_JP, 0) <= 0) continue;
			if (ex_json_int(ce, "hpMax", &v)) vm_set_field_int(c, SB_F_HPMAX_CN, SB_F_HPMAX_JP, vm_dd_clamp(v, 1, 99999));
			if (ex_json_int(ce, "hp", &v)) vm_set_field_int(c, SB_F_HP_CN, SB_F_HP_JP, vm_dd_clamp(v, 0, 99999));
			if (ex_json_int(ce, "atk", &v)) { vm_set_field_int(c, SB_F_ATK_CN, SB_F_ATK_JP, vm_dd_clamp(v, 0, 9999)); vm_set_field_int(c, SB_F_CATK_CN, SB_F_CATK_JP, vm_dd_clamp(v, 0, 9999)); }
			if (ex_json_int(ce, "def", &v)) { vm_set_field_int(c, SB_F_DEF_CN, SB_F_DEF_JP, vm_dd_clamp(v, 0, 9999)); vm_set_field_int(c, SB_F_CDEF_CN, SB_F_CDEF_JP, vm_dd_clamp(v, 0, 9999)); }
			if (ex_json_int(ce, "mag", &v)) { vm_set_field_int(c, SB_F_INT_CN, SB_F_INT_JP, vm_dd_clamp(v, 0, 9999)); vm_set_field_int(c, SB_F_CINT_CN, SB_F_CINT_JP, vm_dd_clamp(v, 0, 9999)); }
			if (ex_json_int(ce, "spd", &v)) { vm_set_field_int(c, SB_F_SPD_CN, SB_F_SPD_JP, vm_dd_clamp(v, 0, 9999)); vm_set_field_int(c, SB_F_CSPD_CN, SB_F_CSPD_JP, vm_dd_clamp(v, 0, 9999)); }
			if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(ce, "fullHeal"))) {
				int mhp = vm_get_field_int(c, SB_F_HPMAX_CN, SB_F_HPMAX_JP, 0);
				if (mhp > 0) vm_set_field_int(c, SB_F_HP_CN, SB_F_HP_JP, mhp);
			}
		}
	}
}

/* =========================================================================
 * 11. Dorapeko (dorapeko)
 * ========================================================================= */
#define DP_F_NAME "c\x96\xbc\x91\x4f"
#define DP_F_LV   "c\x82\x6b\x82\x75"
#define DP_F_HP   "c\x82\x67\x82\x6f"
#define DP_F_ATK  "c\x8d\x55\x8c\x82\x92\x6c"
#define DP_F_ACT  "c\x8d\x73\x93\xae"
#define DP_F_LOVE "c\x8d\x44\x8a\xb4\x93\x78"
#define DP_F_JOIN "c\x89\xc1\x93\xfc"
#define DP_F_EXP  "c\x82\x64\x82\x77\x82\x6f"

static void ex_dp_populate(cJSON *root)
{
	struct page *cd = vm_get_global_page("CD", NULL);
	int n = vm_dd_array_len(cd);
	cJSON_AddBoolToObject(root, "inGame", n > 0);
	cJSON_AddBoolToObject(root, "inBattle", false);

	cJSON *chars = cJSON_CreateArray();
	for (int i = 0; i < n && i < 32; i++) {
		struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(cd, i));
		if (!c) continue;
		char *nm = vm_dd_get_field_utf8(c, DP_F_NAME, NULL);
		if (!nm[0]) { free(nm); continue; }
		struct page *hp = vm_get_field_page(c, DP_F_HP, NULL);
		struct page *ex = vm_get_field_page(c, DP_F_EXP, NULL);
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddStringToObject(jc, "name", nm);
		free(nm);
		cJSON_AddNumberToObject(jc, "lv", vm_get_field_int(c, DP_F_LV, NULL, 1));
		cJSON_AddNumberToObject(jc, "hp", ex_arr_int_get(hp, 0, 0));
		cJSON_AddNumberToObject(jc, "hpMax", ex_arr_int_get(hp, 1, 0));
		cJSON_AddNumberToObject(jc, "atk", vm_get_field_int(c, DP_F_ATK, NULL, 0));
		cJSON_AddNumberToObject(jc, "love", vm_get_field_int(c, DP_F_LOVE, NULL, 0));
		cJSON_AddNumberToObject(jc, "exp", ex_arr_int_get(ex, 0, 0));
		cJSON_AddItemToArray(chars, jc);
	}
	cJSON_AddItemToObject(root, "exCharacters", chars);
}

static bool ex_dp_char_active(struct page *c)
{
	if (!c)
		return false;
	if (vm_get_field_int(c, DP_F_JOIN, NULL, 0) > 0)
		return true;
	struct page *hp = vm_get_field_page(c, DP_F_HP, NULL);
	if (ex_arr_int_get(hp, 1, 0) > 0)
		return true;
	char *nm = vm_dd_get_field_utf8(c, DP_F_NAME, NULL);
	bool has_name = nm && nm[0] != '\0';
	free(nm);
	return has_name;
}

static void ex_dp_locks(int mask)
{
	struct page *cd = vm_get_global_page("CD", NULL);
	for (int i = 0; i < vm_dd_array_len(cd); i++) {
		struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(cd, i));
		if (!ex_dp_char_active(c)) continue;
		if (mask & (1 << 0)) {
			struct page *hp = vm_get_field_page(c, DP_F_HP, NULL);
			int mhp = ex_arr_int_get(hp, 1, 0);
			if (mhp > 0) ex_arr_int_set(hp, 0, mhp);
		}
		if ((mask & (1 << 1)) && vm_get_field_int(c, DP_F_LOVE, NULL, 0) < 100)
			vm_set_field_int(c, DP_F_LOVE, NULL, 100);
		if (mask & (1 << 2))
			vm_set_field_int(c, DP_F_ACT, NULL, 1);
	}
}

static void ex_dp_action(cJSON *act)
{
	if (ex_json_cheat_is(act, "fullHeal")) ex_dp_locks((1 << 0) | (1 << 2));
	cJSON *ce = cJSON_GetObjectItemCaseSensitive(act, "exCharEdit");
	if (ce && cJSON_IsObject(ce)) {
		struct page *cd = vm_get_global_page("CD", NULL);
		int32_t target = -1, v;
		ex_json_int(ce, "target", &target);
		for (int i = 0; i < vm_dd_array_len(cd); i++) {
			if (target >= 0 && i != target) continue;
			struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(cd, i));
			if (!c || (target < 0 && !ex_dp_char_active(c))) continue;
			struct page *hp = vm_get_field_page(c, DP_F_HP, NULL);
			struct page *ex = vm_get_field_page(c, DP_F_EXP, NULL);
			if (ex_json_int(ce, "lv", &v)) vm_set_field_int(c, DP_F_LV, NULL, vm_dd_clamp(v, 1, 999));
			if (ex_json_int(ce, "hpMax", &v)) ex_arr_int_set(hp, 1, vm_dd_clamp(v, 1, 99999));
			if (ex_json_int(ce, "hp", &v)) ex_arr_int_set(hp, 0, vm_dd_clamp(v, 0, 99999));
			if (ex_json_int(ce, "atk", &v)) vm_set_field_int(c, DP_F_ATK, NULL, vm_dd_clamp(v, 0, 99999));
			if (ex_json_int(ce, "love", &v)) vm_set_field_int(c, DP_F_LOVE, NULL, vm_dd_clamp(v, 0, 999));
			if (ex_json_int(ce, "exp", &v)) ex_arr_int_set(ex, 0, vm_dd_clamp(v, 0, 9999999));
			if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(ce, "fullHeal"))) {
				int mhp = ex_arr_int_get(hp, 1, 0);
				if (mhp > 0) ex_arr_int_set(hp, 0, mhp);
				vm_set_field_int(c, DP_F_ACT, NULL, 1);
			}
		}
	}
}

/* =========================================================================
 * 12. Haruurare (haruurare)
 * ========================================================================= */
static void ex_hu_populate(cJSON *root)
{
	struct page *gc = vm_get_global_page("g_gameContext", NULL);
	struct page *mems = vm_get_field_page(gc, "member", NULL);
	cJSON_AddBoolToObject(root, "inGame", gc != NULL && vm_dd_array_len(mems) > 0);
	cJSON_AddBoolToObject(root, "inBattle", false);
	cJSON_AddNumberToObject(root, "gold", vm_get_field_int(gc, "money", NULL, 0));
	cJSON_AddNumberToObject(root, "turn", vm_get_field_int(gc, "turn", NULL, 0));
	cJSON_AddNumberToObject(root, "credit", vm_get_field_int(gc, "creditPoint", NULL, 0));
	cJSON_AddNumberToObject(root, "quota", vm_get_field_int(gc, "jounou", NULL, 0));

	cJSON *chars = cJSON_CreateArray();
	for (int i = 0; i < vm_dd_array_len(mems) && i < 32; i++) {
		struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(mems, i));
		if (!c) continue;
		char *sei = vm_dd_get_field_utf8(c, "nameSei_", NULL);
		char *mei = vm_dd_get_field_utf8(c, "nameMei_", NULL);
		if (!sei[0] && !mei[0]) { free(sei); free(mei); continue; }
		char buf[128];
		snprintf(buf, sizeof(buf), "%s%s", sei, mei);
		free(sei); free(mei);
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddStringToObject(jc, "name", buf);
		cJSON_AddNumberToObject(jc, "hp", vm_get_field_int(c, "hp_", NULL, 0));
		cJSON_AddNumberToObject(jc, "skill", vm_get_field_int(c, "skill_", NULL, 0));
		cJSON_AddNumberToObject(jc, "visual", vm_get_field_int(c, "visual_", NULL, 0));
		cJSON_AddItemToArray(chars, jc);
	}
	cJSON_AddItemToObject(root, "exCharacters", chars);
}

static void ex_hu_locks(int mask)
{
	struct page *gc = vm_get_global_page("g_gameContext", NULL);
	if (!gc) return;
	if ((mask & (1 << 0)) && vm_get_field_int(gc, "money", NULL, 0) < 999999)
		vm_set_field_int(gc, "money", NULL, 999999);
	if (mask & (1 << 1)) {
		struct page *mems = vm_get_field_page(gc, "member", NULL);
		for (int i = 0; i < vm_dd_array_len(mems); i++) {
			struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(mems, i));
			if (c && vm_get_field_int(c, "hp_", NULL, 0) < 100)
				vm_set_field_int(c, "hp_", NULL, 100);
		}
	}
	if (mask & (1 << 2)) {
		if (vm_get_field_int(gc, "creditLevel", NULL, 0) < 10)
			vm_set_field_int(gc, "creditLevel", NULL, 10);
		if (vm_get_field_int(gc, "creditPoint", NULL, 0) < 9999)
			vm_set_field_int(gc, "creditPoint", NULL, 9999);
	}
}

static void ex_hu_action(cJSON *act)
{
	struct page *gc = vm_get_global_page("g_gameContext", NULL);
	if (!gc) return;
	int32_t v;
	if (ex_json_int(act, "gold", &v)) vm_set_field_int(gc, "money", NULL, vm_dd_clamp(v, 0, 99999999));
	if (ex_json_int(act, "addGold", &v)) vm_set_field_int(gc, "money", NULL, vm_dd_clamp(vm_get_field_int(gc, "money", NULL, 0) + v, 0, 99999999));
	if (ex_json_int(act, "credit", &v)) vm_set_field_int(gc, "creditPoint", NULL, vm_dd_clamp(v, 0, 99999));
	if (ex_json_int(act, "quota", &v)) vm_set_field_int(gc, "jounou", NULL, vm_dd_clamp(v, 0, 99999999));
	if (ex_json_cheat_is(act, "fullHeal")) ex_hu_locks(1 << 1);
	if (ex_json_cheat_is(act, "unlockItems")) {
		struct page *en = vm_get_field_page(gc, "itemEnableBuy", NULL);
		for (int i = 0; i < vm_dd_array_len(en); i++)
			ex_arr_int_set(en, i, 1);
	}
	cJSON *ce = cJSON_GetObjectItemCaseSensitive(act, "exCharEdit");
	if (ce && cJSON_IsObject(ce)) {
		struct page *mems = vm_get_field_page(gc, "member", NULL);
		int32_t target = -1;
		ex_json_int(ce, "target", &target);
		for (int i = 0; i < vm_dd_array_len(mems); i++) {
			if (target >= 0 && i != target) continue;
			struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(mems, i));
			if (!c) continue;
			if (ex_json_int(ce, "hp", &v)) vm_set_field_int(c, "hp_", NULL, vm_dd_clamp(v, 0, 999));
			if (ex_json_int(ce, "skill", &v)) vm_set_field_int(c, "skill_", NULL, vm_dd_clamp(v, 0, 999));
			if (ex_json_int(ce, "visual", &v)) vm_set_field_int(c, "visual_", NULL, vm_dd_clamp(v, 0, 999));
		}
	}
}

/* =========================================================================
 * 13. Oyakorankan (oyakorankan)
 * ========================================================================= */
#define OY_G_CDATA   "\x81\x99\x83\x4c\x83\x83\x83\x89\x83\x66\x81\x5b\x83\x5e"
#define OY_G_EDATA   "\x81\x99\x93\x47\x83\x4c\x83\x83\x83\x89\x83\x66\x81\x5b\x83\x5e"
#define OY_F_NAME    "m_sz\x96\xbc\x91\x4f"
#define OY_F_FERT    "m_n\x8e\xf3\x90\xb8\x83\x51\x81\x5b\x83\x57"
#define OY_F_GUARD   "m_n\x83\x89\x83\x75\x83\x4b\x81\x5b\x83\x68"
#define OY_F_GMAX    "m_n\x83\x89\x83\x75\x83\x4b\x81\x5b\x83\x68\x81\x5e\x8f\xe3\x8c\xc0\x92\x6c"
#define OY_F_STAR    "m_n\x8f\x9f\x82\xbf\x90\xaf"
#define OY_F_SMAX    "m_n\x8f\x9f\x82\xbf\x90\xaf\x81\x5e\x8f\xe3\x8c\xc0\x92\x6c"
#define OY_F_EATK    "m_n\x8d\x55\x8c\x82\x97\xcd"
#define OY_F_ESTAM   "m_n\x90\xb8\x97\xcd"

static void ex_oy_populate(cJSON *root)
{
	struct page *cd = vm_get_global_page(OY_G_CDATA, NULL);
	struct page *arr = vm_get_field_page(cd, "m_acBase", NULL);
	int n = vm_dd_array_len(arr);
	cJSON_AddBoolToObject(root, "inGame", n > 0);
	cJSON_AddBoolToObject(root, "inBattle", false);
	cJSON_AddNumberToObject(root, "day", ex_get_global_int("g_nDay", NULL, 0));

	cJSON *chars = cJSON_CreateArray();
	for (int i = 0; i < n && i < 24; i++) {
		struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
		if (!c) continue;
		char *nm = vm_dd_get_field_utf8(c, OY_F_NAME, NULL);
		if (!nm[0]) { free(nm); continue; }
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddStringToObject(jc, "name", nm);
		free(nm);
		cJSON_AddNumberToObject(jc, "hp", vm_get_field_int(c, OY_F_GUARD, NULL, 0));
		cJSON_AddNumberToObject(jc, "hpMax", vm_get_field_int(c, OY_F_GMAX, NULL, 0));
		cJSON_AddNumberToObject(jc, "star", vm_get_field_int(c, OY_F_STAR, NULL, 0));
		cJSON_AddNumberToObject(jc, "fert", vm_get_field_int(c, OY_F_FERT, NULL, 0));
		cJSON_AddItemToArray(chars, jc);
	}
	cJSON_AddItemToObject(root, "exCharacters", chars);
}

static void ex_oy_locks(int mask)
{
	struct page *cd = vm_get_global_page(OY_G_CDATA, NULL);
	struct page *arr = vm_get_field_page(cd, "m_acBase", NULL);
	for (int i = 0; i < vm_dd_array_len(arr); i++) {
		struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
		if (!c) continue;
		if (mask & (1 << 0)) {
			int gmax = vm_get_field_int(c, OY_F_GMAX, NULL, 0);
			if (gmax > 0) vm_set_field_int(c, OY_F_GUARD, NULL, gmax);
		}
		if (mask & (1 << 1)) {
			int smax = vm_get_field_int(c, OY_F_SMAX, NULL, 0);
			if (smax > 0) vm_set_field_int(c, OY_F_STAR, NULL, smax);
		}
		if (mask & (1 << 2))
			vm_set_field_int(c, OY_F_FERT, NULL, 0);
	}
	if (mask & (1 << 3)) {
		struct page *ed = vm_get_global_page(OY_G_EDATA, NULL);
		struct page *earr = vm_get_field_page(ed, "m_acBase", NULL);
		for (int i = 0; i < vm_dd_array_len(earr); i++) {
			struct page *e = vm_safe_get_page(vm_dd_array_elem_slot(earr, i));
			if (!e) continue;
			vm_set_field_int(e, OY_F_EATK, NULL, 0);
			vm_set_field_int(e, OY_F_ESTAM, NULL, 0);
		}
	}
}

static void ex_oy_action(cJSON *act)
{
	if (ex_json_cheat_is(act, "fullHeal")) ex_oy_locks((1 << 0) | (1 << 1) | (1 << 2));
	if (ex_json_cheat_is(act, "weakenEnemy")) ex_oy_locks(1 << 3);
	cJSON *ce = cJSON_GetObjectItemCaseSensitive(act, "exCharEdit");
	if (ce && cJSON_IsObject(ce)) {
		struct page *cd = vm_get_global_page(OY_G_CDATA, NULL);
		struct page *arr = vm_get_field_page(cd, "m_acBase", NULL);
		int32_t target = -1, v;
		ex_json_int(ce, "target", &target);
		for (int i = 0; i < vm_dd_array_len(arr); i++) {
			if (target >= 0 && i != target) continue;
			struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
			if (!c) continue;
			if (ex_json_int(ce, "hp", &v)) vm_set_field_int(c, OY_F_GUARD, NULL, vm_dd_clamp(v, 0, 99999));
			if (ex_json_int(ce, "star", &v)) vm_set_field_int(c, OY_F_STAR, NULL, vm_dd_clamp(v, 0, 999));
			if (ex_json_int(ce, "fert", &v)) vm_set_field_int(c, OY_F_FERT, NULL, vm_dd_clamp(v, 0, 99999));
		}
	}
}

/* =========================================================================
 * 14. Tsumashibori (tsumashibori)
 * ========================================================================= */
#define TS_G_HERO   "\x81\x99\x8d\x4b\x89\xee" /* ☆幸介 */
#define TS_G_SAKURA "\x81\x99\x8d\xe7\x97\xc7" /* ☆咲良 */
#define TS_G_MADOKA "\x81\x99\x89\x7e"         /* ☆円 */
#define TS_G_RIVAL1 "\x81\x99\x8b\x9e\x96\xed" /* ☆京弥 */
#define TS_G_RIVAL2 "\x81\x99\x97\xc7\x8e\xa1" /* ☆良治 */
#define TS_G_RIVAL3 "\x81\x99\x8e\x4f\x98\x59" /* ☆三郎 */
#define TS_F_HP     "m_n\x91\xcc\x97\xcd"
#define TS_F_HPMAX  "m_n\x91\xcc\x97\xcdMAX"
#define TS_F_SP     "m_n\x90\xb8\x97\xcd"
#define TS_F_SPMAX  "m_n\x90\xb8\x97\xcdMAX"
#define TS_F_GOLD   "m_n\x8f\x8a\x8e\x9d\x8b\xe0"
#define TS_F_LV     "m_n\x83\x8c\x83\x78\x83\x8b"
#define TS_F_EXP    "m_n\x8c\x6f\x8c\xb1\x92\x6c"
#define TS_F_UNLUCK "m_n\x95\x73\x8d\x4b"
#define TS_F_AFFE   "m_n\x88\xa4\x8f\xee\x92\x6c"
#define TS_F_AMAX   "m_n\x88\xa4\x8f\xee\x92\x6cMAX"
#define TS_F_LOVE   "m_n\x8d\x44\x8a\xb4\x93\x78"
#define TS_F_JEAL   "m_n\x8e\xb9\x93\x69\x90\x53"

static void ex_ts_populate(cJSON *root)
{
	struct page *hero = vm_get_global_page(TS_G_HERO, NULL);
	cJSON_AddBoolToObject(root, "inGame", hero != NULL);
	cJSON_AddBoolToObject(root, "inBattle", false);
	cJSON_AddNumberToObject(root, "gold", vm_get_field_int(hero, TS_F_GOLD, NULL, 0));
	cJSON_AddNumberToObject(root, "hp", vm_get_field_int(hero, TS_F_HP, NULL, 0));
	cJSON_AddNumberToObject(root, "hpMax", vm_get_field_int(hero, TS_F_HPMAX, NULL, 0));
	cJSON_AddNumberToObject(root, "sp", vm_get_field_int(hero, TS_F_SP, NULL, 0));
	cJSON_AddNumberToObject(root, "spMax", vm_get_field_int(hero, TS_F_SPMAX, NULL, 0));
	cJSON_AddNumberToObject(root, "lv", vm_get_field_int(hero, TS_F_LV, NULL, 1));
	cJSON_AddNumberToObject(root, "exp", vm_get_field_int(hero, TS_F_EXP, NULL, 0));
}

static void ex_ts_locks(int mask)
{
	struct page *hero = vm_get_global_page(TS_G_HERO, NULL);
	if (!hero) return;
	if ((mask & (1 << 0)) && vm_get_field_int(hero, TS_F_GOLD, NULL, 0) < 999999)
		vm_set_field_int(hero, TS_F_GOLD, NULL, 999999);
	if (mask & (1 << 1)) {
		int mhp = vm_get_field_int(hero, TS_F_HPMAX, NULL, 0);
		int msp = vm_get_field_int(hero, TS_F_SPMAX, NULL, 0);
		if (mhp > 0) vm_set_field_int(hero, TS_F_HP, NULL, mhp);
		if (msp > 0) vm_set_field_int(hero, TS_F_SP, NULL, msp);
	}
	if (mask & (1 << 2)) {
		vm_set_field_int(hero, TS_F_UNLUCK, NULL, 0);
		const char *hs[2] = { TS_G_SAKURA, TS_G_MADOKA };
		for (int i = 0; i < 2; i++) {
			struct page *h = vm_get_global_page(hs[i], NULL);
			if (!h) continue;
			vm_set_field_int(h, TS_F_JEAL, NULL, 0);
			int amax = vm_get_field_int(h, TS_F_AMAX, NULL, 0);
			if (amax > 0) vm_set_field_int(h, TS_F_AFFE, NULL, amax);
		}
	}
	if (mask & (1 << 3)) {
		const char *rs[3] = { TS_G_RIVAL1, TS_G_RIVAL2, TS_G_RIVAL3 };
		for (int i = 0; i < 3; i++) {
			struct page *r = vm_get_global_page(rs[i], NULL);
			if (!r) continue;
			vm_set_field_int(r, TS_F_SP, NULL, 0);
			vm_set_field_int(r, TS_F_AFFE, NULL, 0);
		}
	}
	if (mask & (1 << 4)) {
		struct page *items = vm_get_global_page("ITEM_DATA", NULL);
		for (int i = 0; i < vm_dd_array_len(items); i++) {
			struct page *it = vm_safe_get_page(vm_dd_array_elem_slot(items, i));
			if (it && vm_get_field_int(it, "nCOUNT", NULL, 0) > 0 &&
			    vm_get_field_int(it, "nCOUNT", NULL, 0) < 99)
				vm_set_field_int(it, "nCOUNT", NULL, 99);
		}
	}
}

static void ex_ts_action(cJSON *act)
{
	struct page *hero = vm_get_global_page(TS_G_HERO, NULL);
	if (!hero) return;
	int32_t v;
	if (ex_json_int(act, "gold", &v)) vm_set_field_int(hero, TS_F_GOLD, NULL, vm_dd_clamp(v, 0, 99999999));
	if (ex_json_int(act, "addGold", &v)) vm_set_field_int(hero, TS_F_GOLD, NULL, vm_dd_clamp(vm_get_field_int(hero, TS_F_GOLD, NULL, 0) + v, 0, 99999999));
	if (ex_json_int(act, "hp", &v)) vm_set_field_int(hero, TS_F_HP, NULL, vm_dd_clamp(v, 0, 99999));
	if (ex_json_int(act, "hpMax", &v)) vm_set_field_int(hero, TS_F_HPMAX, NULL, vm_dd_clamp(v, 1, 99999));
	if (ex_json_int(act, "sp", &v)) vm_set_field_int(hero, TS_F_SP, NULL, vm_dd_clamp(v, 0, 99999));
	if (ex_json_int(act, "spMax", &v)) vm_set_field_int(hero, TS_F_SPMAX, NULL, vm_dd_clamp(v, 1, 99999));
	if (ex_json_int(act, "lv", &v)) vm_set_field_int(hero, TS_F_LV, NULL, vm_dd_clamp(v, 1, 99));
	if (ex_json_int(act, "exp", &v)) vm_set_field_int(hero, TS_F_EXP, NULL, vm_dd_clamp(v, 0, 9999999));
	if (ex_json_cheat_is(act, "fullHeal")) ex_ts_locks((1 << 1) | (1 << 2));
	if (ex_json_cheat_is(act, "weakenEnemy")) ex_ts_locks(1 << 3);
	if (ex_json_cheat_is(act, "maxItems")) ex_ts_locks(1 << 4);
}

/* =========================================================================
 * 15. Okusama Junai & NTR (okusamajun / okusamantr)
 * ========================================================================= */
static struct page *ex_ok_charas(void)
{
	struct page *res = vm_get_global_page("g_resources", NULL);
	return vm_get_field_page(res, "Charas", NULL);
}

static struct page *ex_ok_money_page(struct page *cm)
{
	struct page *plist = vm_get_field_page(cm, "m_possessList", NULL);
	if (vm_dd_array_len(plist) <= 0) return NULL;
	struct page *pos0 = vm_safe_get_page(vm_dd_array_elem_slot(plist, 0));
	return vm_get_field_page(pos0, "m_Money", NULL);
}

static void ex_ok_populate(cJSON *root)
{
	struct page *cm = ex_ok_charas();
	struct page *slist = vm_get_field_page(cm, "m_statusList", NULL);
	struct page *arr = vm_get_field_page(slist, "m_list", NULL);
	struct page *mp = ex_ok_money_page(cm);
	int n = vm_dd_array_len(arr);
	cJSON_AddBoolToObject(root, "inGame", cm != NULL && n > 0);
	cJSON_AddBoolToObject(root, "inBattle", false);
	cJSON_AddNumberToObject(root, "gold", vm_get_field_int(mp, "<Value>", NULL, 0));

	cJSON *chars = cJSON_CreateArray();
	for (int i = 0; i < n && i < 24; i++) {
		struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
		if (!c) continue;
		char *nm = vm_dd_get_field_utf8(c, "<IdName>", NULL);
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddStringToObject(jc, "name", nm);
		free(nm);
		cJSON_AddNumberToObject(jc, "lv", vm_get_field_int(c, "<Level>", NULL, 1));
		cJSON_AddNumberToObject(jc, "exp", vm_get_field_int(c, "<ExpNow>", NULL, 0));
		cJSON_AddNumberToObject(jc, "hp", vm_get_field_int(c, "<HpNow>", NULL, 0));
		cJSON_AddNumberToObject(jc, "hpMax", vm_get_field_int(c, "<HpMax>", NULL, 0));
		cJSON_AddNumberToObject(jc, "mp", vm_get_field_int(c, "<MpNow>", NULL, 0));
		cJSON_AddNumberToObject(jc, "mpMax", vm_get_field_int(c, "<MpMax>", NULL, 0));
		cJSON_AddNumberToObject(jc, "atk", vm_get_field_int(c, "<Str>", NULL, 0));
		cJSON_AddNumberToObject(jc, "def", vm_get_field_int(c, "<Def>", NULL, 0));
		cJSON_AddItemToArray(chars, jc);
	}
	cJSON_AddItemToObject(root, "exCharacters", chars);
}

static void ex_ok_locks(int mask)
{
	struct page *cm = ex_ok_charas();
	if (!cm) return;
	if (mask & (1 << 0)) {
		struct page *plist = vm_get_field_page(cm, "m_possessList", NULL);
		for (int i = 0; i < vm_dd_array_len(plist); i++) {
			struct page *pos = vm_safe_get_page(vm_dd_array_elem_slot(plist, i));
			struct page *mp = vm_get_field_page(pos, "m_Money", NULL);
			if (mp && vm_get_field_int(mp, "<Value>", NULL, 0) < 999999)
				vm_set_field_int(mp, "<Value>", NULL, 999999);
		}
	}
	if (mask & (1 << 1)) {
		struct page *slist = vm_get_field_page(cm, "m_statusList", NULL);
		struct page *arr = vm_get_field_page(slist, "m_list", NULL);
		for (int i = 0; i < vm_dd_array_len(arr); i++) {
			struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
			if (!c) continue;
			int mhp = vm_get_field_int(c, "<HpMax>", NULL, 0);
			int mmp = vm_get_field_int(c, "<MpMax>", NULL, 0);
			if (mhp > 0) vm_set_field_int(c, "<HpNow>", NULL, mhp);
			if (mmp > 0) vm_set_field_int(c, "<MpNow>", NULL, mmp);
		}
	}
}

static void ex_ok_action(cJSON *act)
{
	struct page *cm = ex_ok_charas();
	if (!cm) return;
	int32_t v;
	if (ex_json_int(act, "gold", &v)) {
		struct page *plist = vm_get_field_page(cm, "m_possessList", NULL);
		int32_t clamped = vm_dd_clamp(v, 0, 999999999);
		for (int i = 0; i < vm_dd_array_len(plist); i++) {
			struct page *pos = vm_safe_get_page(vm_dd_array_elem_slot(plist, i));
			struct page *mp = vm_get_field_page(pos, "m_Money", NULL);
			if (mp) vm_set_field_int(mp, "<Value>", NULL, clamped);
		}
	}
	if (ex_json_int(act, "addGold", &v)) {
		struct page *plist = vm_get_field_page(cm, "m_possessList", NULL);
		for (int i = 0; i < vm_dd_array_len(plist); i++) {
			struct page *pos = vm_safe_get_page(vm_dd_array_elem_slot(plist, i));
			struct page *mp = vm_get_field_page(pos, "m_Money", NULL);
			if (mp) vm_set_field_int(mp, "<Value>", NULL, vm_dd_clamp(vm_get_field_int(mp, "<Value>", NULL, 0) + v, 0, 999999999));
		}
	}
	if (ex_json_cheat_is(act, "fullHeal")) ex_ok_locks(1 << 1);
	cJSON *ce = cJSON_GetObjectItemCaseSensitive(act, "exCharEdit");
	if (ce && cJSON_IsObject(ce)) {
		struct page *arr = vm_get_field_page(vm_get_field_page(cm, "m_statusList", NULL), "m_list", NULL);
		int32_t target = -1;
		ex_json_int(ce, "target", &target);
		for (int i = 0; i < vm_dd_array_len(arr); i++) {
			if (target >= 0 && i != target) continue;
			struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
			if (!c) continue;
			if (ex_json_int(ce, "lv", &v)) vm_set_field_int(c, "<Level>", NULL, vm_dd_clamp(v, 1, 999));
			if (ex_json_int(ce, "exp", &v)) vm_set_field_int(c, "<ExpNow>", NULL, vm_dd_clamp(v, 0, 9999999));
			if (ex_json_int(ce, "hpMax", &v)) vm_set_field_int(c, "<HpMax>", NULL, vm_dd_clamp(v, 1, 99999));
			if (ex_json_int(ce, "hp", &v)) vm_set_field_int(c, "<HpNow>", NULL, vm_dd_clamp(v, 0, 99999));
			if (ex_json_int(ce, "mpMax", &v)) vm_set_field_int(c, "<MpMax>", NULL, vm_dd_clamp(v, 1, 99999));
			if (ex_json_int(ce, "mp", &v)) vm_set_field_int(c, "<MpNow>", NULL, vm_dd_clamp(v, 0, 99999));
			if (ex_json_int(ce, "atk", &v)) vm_set_field_int(c, "<Str>", NULL, vm_dd_clamp(v, 0, 9999));
			if (ex_json_int(ce, "def", &v)) vm_set_field_int(c, "<Def>", NULL, vm_dd_clamp(v, 0, 9999));
			if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(ce, "fullHeal"))) {
				int mhp = vm_get_field_int(c, "<HpMax>", NULL, 0);
				int mmp = vm_get_field_int(c, "<MpMax>", NULL, 0);
				if (mhp > 0) vm_set_field_int(c, "<HpNow>", NULL, mhp);
				if (mmp > 0) vm_set_field_int(c, "<MpNow>", NULL, mmp);
			}
		}
	}
}

/* =========================================================================
 * 16. Escalayer Reboot (escalayerreboot) — Mituki C_INT
 * ========================================================================= */
static void ex_er_populate(cJSON *root)
{
	struct page *gp = vm_get_global_page("g_cGameParam", NULL);
	struct page *cp = vm_get_global_page("g_cCustomizeParam", NULL);
	cJSON_AddBoolToObject(root, "inGame", gp != NULL);
	cJSON_AddBoolToObject(root, "inBattle", false);
	cJSON_AddNumberToObject(root, "peace", ex_field_cint_read(gp, "m_nPeacePoint", 0));
	cJSON_AddNumberToObject(root, "love", ex_field_cint_read(gp, "m_nLove", 0));
	cJSON_AddNumberToObject(root, "libido", ex_field_cint_read(gp, "m_nLibido", 0));
	cJSON_AddNumberToObject(root, "energy", ex_field_cint_read(gp, "m_nEnergy", 0));
	cJSON_AddNumberToObject(root, "shield", ex_field_cint_read(cp, "m_nShield", 0));
	cJSON_AddNumberToObject(root, "atk", ex_field_cint_read(cp, "m_nAttack", 0));
	cJSON_AddNumberToObject(root, "def", ex_field_cint_read(cp, "m_nDefense", 0));
	cJSON_AddNumberToObject(root, "spd", ex_field_cint_read(cp, "m_nSpeed", 0));
}

static void ex_er_locks(int mask)
{
	struct page *gp = vm_get_global_page("g_cGameParam", NULL);
	struct page *cp = vm_get_global_page("g_cCustomizeParam", NULL);
	if (!gp) return;
	if (mask & (1 << 0)) {
		int llim = ex_field_cint_read(gp, "m_nLibidoLimit", 100);
		if (llim <= 0) llim = 100;
		if (ex_field_cint_read(gp, "m_nLibido", 0) < llim)
			ex_field_cint_write(gp, "m_nLibido", llim, EX_KEY_ESCALAYER_REBOOT);
		if (ex_field_cint_read(gp, "m_nEnergy", 0) < 100)
			ex_field_cint_write(gp, "m_nEnergy", 100, EX_KEY_ESCALAYER_REBOOT);
	}
	if (mask & (1 << 1)) {
		if (ex_field_cint_read(gp, "m_nPeacePoint", 0) < 99999)
			ex_field_cint_write(gp, "m_nPeacePoint", 99999, EX_KEY_ESCALAYER_REBOOT);
		if (ex_field_cint_read(gp, "m_nLove", 0) < 999)
			ex_field_cint_write(gp, "m_nLove", 999, EX_KEY_ESCALAYER_REBOOT);
	}
	if (mask & (1 << 2)) {
		struct page *boost = vm_get_field_page(gp, "m_bBoost", NULL);
		if (boost && boost->type == STRUCT_PAGE && boost->nr_vars >= 1 && boost->values[0].i == 0)
			ex_cbool_write(boost, true, EX_KEY_ESCALAYER_REBOOT);
	}
	if ((mask & (1 << 3)) && cp) {
		if (ex_field_cint_read(cp, "m_nShield", 0) < 10) ex_field_cint_write(cp, "m_nShield", 10, EX_KEY_ESCALAYER_REBOOT);
		if (ex_field_cint_read(cp, "m_nAttack", 0) < 10) ex_field_cint_write(cp, "m_nAttack", 10, EX_KEY_ESCALAYER_REBOOT);
		if (ex_field_cint_read(cp, "m_nDefense", 0) < 10) ex_field_cint_write(cp, "m_nDefense", 10, EX_KEY_ESCALAYER_REBOOT);
		if (ex_field_cint_read(cp, "m_nSpeed", 0) < 10) ex_field_cint_write(cp, "m_nSpeed", 10, EX_KEY_ESCALAYER_REBOOT);
	}
}

static void ex_er_action(cJSON *act)
{
	struct page *gp = vm_get_global_page("g_cGameParam", NULL);
	struct page *cp = vm_get_global_page("g_cCustomizeParam", NULL);
	if (!gp) return;
	int32_t v;
	if (ex_json_int(act, "peace", &v)) ex_field_cint_write(gp, "m_nPeacePoint", vm_dd_clamp(v, 0, 9999999), EX_KEY_ESCALAYER_REBOOT);
	if (ex_json_int(act, "addPeace", &v)) ex_field_cint_write(gp, "m_nPeacePoint", vm_dd_clamp(ex_field_cint_read(gp, "m_nPeacePoint", 0) + v, 0, 9999999), EX_KEY_ESCALAYER_REBOOT);
	if (ex_json_int(act, "love", &v)) ex_field_cint_write(gp, "m_nLove", vm_dd_clamp(v, 0, 9999), EX_KEY_ESCALAYER_REBOOT);
	if (ex_json_int(act, "libido", &v)) ex_field_cint_write(gp, "m_nLibido", vm_dd_clamp(v, 0, 9999), EX_KEY_ESCALAYER_REBOOT);
	if (ex_json_int(act, "energy", &v)) ex_field_cint_write(gp, "m_nEnergy", vm_dd_clamp(v, 0, 9999), EX_KEY_ESCALAYER_REBOOT);
	if (cp) {
		if (ex_json_int(act, "shield", &v)) ex_field_cint_write(cp, "m_nShield", vm_dd_clamp(v, 1, 10), EX_KEY_ESCALAYER_REBOOT);
		if (ex_json_int(act, "atk", &v)) ex_field_cint_write(cp, "m_nAttack", vm_dd_clamp(v, 1, 10), EX_KEY_ESCALAYER_REBOOT);
		if (ex_json_int(act, "def", &v)) ex_field_cint_write(cp, "m_nDefense", vm_dd_clamp(v, 1, 10), EX_KEY_ESCALAYER_REBOOT);
		if (ex_json_int(act, "spd", &v)) ex_field_cint_write(cp, "m_nSpeed", vm_dd_clamp(v, 1, 10), EX_KEY_ESCALAYER_REBOOT);
	}
	if (ex_json_cheat_is(act, "fullHeal")) ex_er_locks((1 << 0) | (1 << 2));
	if (ex_json_cheat_is(act, "boostAll")) ex_er_locks(0x0F);
}

/* =========================================================================
 * 17. Ixseal (ixseal) — Mituki C_INT + faculty xorKey
 * ========================================================================= */
static void ex_ix_write_fac(struct page *fac, const char *val_name, const char *key_name, int32_t val)
{
	if (!fac) return;
	int32_t xk = vm_get_field_int(fac, key_name, NULL, 0);
	ex_cint_write_xor(vm_get_field_page(fac, val_name, NULL), val, EX_KEY_IXSEAL, xk);
}

static void ex_ix_populate(cJSON *root)
{
	struct page *gp = vm_get_global_page("g_cGameParam", NULL);
	struct page *hp = vm_get_global_page("g_cHeroineParam", NULL);
	struct page *arr = vm_get_field_page(hp, "m_acBase", NULL);
	int n = vm_dd_array_len(arr);
	cJSON_AddBoolToObject(root, "inGame", n > 0);
	cJSON_AddBoolToObject(root, "inBattle", false);
	cJSON_AddNumberToObject(root, "gold", ex_field_cint_read(gp, "m_nMagicPlate", 0));
	cJSON_AddNumberToObject(root, "energy", ex_field_cint_read(gp, "m_nEnergy", 0));

	cJSON *chars = cJSON_CreateArray();
	for (int i = 0; i < n && i < 24; i++) {
		struct page *h = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
		if (!h) continue;
		struct page *fac = vm_get_field_page(h, "m_cFaculty", NULL);
		char *nm = vm_dd_get_field_utf8(h, "m_szKey", NULL);
		if (!nm[0]) { free(nm); continue; }
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddStringToObject(jc, "name", nm);
		free(nm);
		cJSON_AddNumberToObject(jc, "hp", ex_field_cint_read(fac, "m_nHitPoint", 0));
		cJSON_AddNumberToObject(jc, "hpMax", ex_field_cint_read(fac, "m_nHitPointLimit", 0));
		cJSON_AddNumberToObject(jc, "atk", ex_field_cint_read(fac, "m_nAttack", 0));
		cJSON_AddNumberToObject(jc, "def", ex_field_cint_read(fac, "m_nDefense", 0));
		cJSON_AddNumberToObject(jc, "spd", ex_field_cint_read(fac, "m_nReaction", 0));
		cJSON_AddNumberToObject(jc, "energy", ex_field_cint_read(h, "m_nEnergy", 0));
		cJSON_AddNumberToObject(jc, "love", ex_field_cint_read(h, "m_nFavorability", 0));
		cJSON_AddNumberToObject(jc, "pollute", ex_field_cint_read(h, "m_cPollution", 0));
		cJSON_AddItemToArray(chars, jc);
	}
	cJSON_AddItemToObject(root, "exCharacters", chars);
}

static void ex_ix_locks(int mask)
{
	struct page *gp = vm_get_global_page("g_cGameParam", NULL);
	struct page *hp = vm_get_global_page("g_cHeroineParam", NULL);
	struct page *arr = vm_get_field_page(hp, "m_acBase", NULL);
	if ((mask & (1 << 0)) && gp) {
		int elim = ex_field_cint_read(gp, "m_nEnergyLimit", 100);
		if (elim <= 0) elim = 100;
		if (ex_field_cint_read(gp, "m_nEnergy", 0) < elim)
			ex_field_cint_write(gp, "m_nEnergy", elim, EX_KEY_IXSEAL);
	}
	for (int i = 0; i < vm_dd_array_len(arr); i++) {
		struct page *h = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
		if (!h) continue;
		struct page *fac = vm_get_field_page(h, "m_cFaculty", NULL);
		if (mask & (1 << 0)) {
			int mhp = ex_field_cint_read(fac, "m_nHitPointLimit", 0);
			if (mhp > 0) ex_ix_write_fac(fac, "m_nHitPoint", "m_nHitPointKey", mhp);
			if (ex_field_cint_read(h, "m_nEnergy", 0) < 100)
				ex_field_cint_write(h, "m_nEnergy", 100, EX_KEY_IXSEAL);
		}
		if (mask & (1 << 1))
			ex_field_cint_write(h, "m_cPollution", 0, EX_KEY_IXSEAL);
		if ((mask & (1 << 2)) && ex_field_cint_read(h, "m_nFavorability", 0) < 999)
			ex_field_cint_write(h, "m_nFavorability", 999, EX_KEY_IXSEAL);
	}
	if ((mask & (1 << 3)) && gp && ex_field_cint_read(gp, "m_nMagicPlate", 0) < 12)
		ex_field_cint_write(gp, "m_nMagicPlate", 12, EX_KEY_IXSEAL);
}

static void ex_ix_action(cJSON *act)
{
	struct page *gp = vm_get_global_page("g_cGameParam", NULL);
	int32_t v;
	if (gp) {
		if (ex_json_int(act, "gold", &v)) ex_field_cint_write(gp, "m_nMagicPlate", vm_dd_clamp(v, 1, 12), EX_KEY_IXSEAL);
		if (ex_json_int(act, "addGold", &v)) ex_field_cint_write(gp, "m_nMagicPlate", vm_dd_clamp(ex_field_cint_read(gp, "m_nMagicPlate", 0) + v, 1, 12), EX_KEY_IXSEAL);
		if (ex_json_int(act, "energy", &v)) ex_field_cint_write(gp, "m_nEnergy", vm_dd_clamp(v, 0, 9999), EX_KEY_IXSEAL);
	}
	if (ex_json_cheat_is(act, "fullHeal")) ex_ix_locks((1 << 0) | (1 << 1));
	if (ex_json_cheat_is(act, "maxItems")) ex_ix_locks(1 << 3);
	cJSON *ce = cJSON_GetObjectItemCaseSensitive(act, "exCharEdit");
	if (ce && cJSON_IsObject(ce)) {
		struct page *arr = vm_get_field_page(vm_get_global_page("g_cHeroineParam", NULL), "m_acBase", NULL);
		int32_t target = -1;
		ex_json_int(ce, "target", &target);
		for (int i = 0; i < vm_dd_array_len(arr); i++) {
			if (target >= 0 && i != target) continue;
			struct page *h = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
			if (!h) continue;
			struct page *fac = vm_get_field_page(h, "m_cFaculty", NULL);
			if (ex_json_int(ce, "hpMax", &v)) ex_ix_write_fac(fac, "m_nHitPointLimit", "m_nHitPointLimitKey", vm_dd_clamp(v, 1, 999));
			if (ex_json_int(ce, "hp", &v)) ex_ix_write_fac(fac, "m_nHitPoint", "m_nHitPointKey", vm_dd_clamp(v, 0, 999));
			if (ex_json_int(ce, "atk", &v)) ex_ix_write_fac(fac, "m_nAttack", "m_nAttackKey", vm_dd_clamp(v, 1, 999));
			if (ex_json_int(ce, "def", &v)) ex_ix_write_fac(fac, "m_nDefense", "m_nDefenseKey", vm_dd_clamp(v, 1, 999));
			if (ex_json_int(ce, "spd", &v)) ex_ix_write_fac(fac, "m_nReaction", "m_nReactionKey", vm_dd_clamp(v, 1, 999));
			if (ex_json_int(ce, "energy", &v)) ex_field_cint_write(h, "m_nEnergy", vm_dd_clamp(v, 0, 999), EX_KEY_IXSEAL);
			if (ex_json_int(ce, "love", &v)) ex_field_cint_write(h, "m_nFavorability", vm_dd_clamp(v, 0, 9999), EX_KEY_IXSEAL);
			if (ex_json_int(ce, "pollute", &v)) ex_field_cint_write(h, "m_cPollution", vm_dd_clamp(v, 0, 999), EX_KEY_IXSEAL);
		}
	}
}

/* =========================================================================
 * 18. Pastel Chime 3 (pascha3) — Mituki C_INT (ValueEncryption passthrough)
 * ========================================================================= */
static struct page *ex_p3_unit_list(void)
{
	struct page *up = vm_get_global_page("g_cUnitParamter", NULL);
	struct page *cls = vm_get_field_page(up, "m_acClass", NULL);
	if (vm_dd_array_len(cls) <= 0) return NULL;
	struct page *c0 = vm_safe_get_page(vm_dd_array_elem_slot(cls, 0));
	return vm_get_field_page(c0, "m_acBase", NULL);
}

static void ex_p3_populate(cJSON *root)
{
	struct page *gp = vm_get_global_page("g_cGameParam", NULL);
	struct page *units = ex_p3_unit_list();
	cJSON_AddBoolToObject(root, "inGame", gp != NULL);
	cJSON_AddBoolToObject(root, "inBattle", false);
	cJSON_AddNumberToObject(root, "gold", ex_field_cint_read(gp, "m_nMoney", 0));
	cJSON_AddNumberToObject(root, "ap", ex_field_cint_read(gp, "m_nActionPoint", 0));
	cJSON_AddNumberToObject(root, "apMax", ex_field_cint_read(gp, "m_nActionPointLimit", 0));

	cJSON *chars = cJSON_CreateArray();
	for (int i = 0; i < vm_dd_array_len(units) && i < 32; i++) {
		struct page *u = vm_safe_get_page(vm_dd_array_elem_slot(units, i));
		if (!u) continue;
		char *nm = ex_get_cstring_utf8(u, "m_szName");
		if (!nm[0]) { free(nm); continue; }
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddStringToObject(jc, "name", nm);
		free(nm);
		cJSON_AddNumberToObject(jc, "lv", ex_field_cint_read(u, "m_nLevel", 1));
		cJSON_AddNumberToObject(jc, "exp", ex_field_cint_read(u, "m_nExp", 0));
		cJSON_AddNumberToObject(jc, "bp", ex_field_cint_read(u, "m_nBp", 0));
		cJSON_AddNumberToObject(jc, "hp", ex_field_cint_read(u, "m_nHp", 0));
		cJSON_AddNumberToObject(jc, "sp", ex_field_cint_read(u, "m_nSp", 0));
		cJSON_AddItemToArray(chars, jc);
	}
	cJSON_AddItemToObject(root, "exCharacters", chars);
}

static void ex_p3_locks(int mask)
{
	struct page *gp = vm_get_global_page("g_cGameParam", NULL);
	if (!gp) return;
	if ((mask & (1 << 0)) && ex_field_cint_read(gp, "m_nMoney", 0) < 999999)
		ex_field_cint_write(gp, "m_nMoney", 999999, EX_KEY_PASCHA3);
	if (mask & (1 << 1)) {
		int apm = ex_field_cint_read(gp, "m_nActionPointLimit", 10);
		if (apm < 10) apm = 10;
		if (ex_field_cint_read(gp, "m_nActionPoint", 0) < apm)
			ex_field_cint_write(gp, "m_nActionPoint", apm, EX_KEY_PASCHA3);
	}
	if (mask & (1 << 2)) {
		struct page *units = ex_p3_unit_list();
		for (int i = 0; i < vm_dd_array_len(units); i++) {
			struct page *u = vm_safe_get_page(vm_dd_array_elem_slot(units, i));
			if (!u) continue;
			char *nm = ex_get_cstring_utf8(u, "m_szName");
			bool empty = !nm[0];
			free(nm);
			if (empty) continue;
			if (ex_field_cint_read(u, "m_nHp", 0) < 9999) ex_field_cint_write(u, "m_nHp", 9999, EX_KEY_PASCHA3);
			if (ex_field_cint_read(u, "m_nSp", 0) < 999) ex_field_cint_write(u, "m_nSp", 999, EX_KEY_PASCHA3);
			if (ex_field_cint_read(u, "m_nTension", 0) < 100) ex_field_cint_write(u, "m_nTension", 100, EX_KEY_PASCHA3);
		}
	}
	if (mask & (1 << 3)) {
		struct page *cp = vm_get_global_page("g_cCharacterParam", NULL);
		struct page *arr = vm_get_field_page(cp, "m_acBase", NULL);
		for (int i = 0; i < vm_dd_array_len(arr); i++) {
			struct page *c = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
			if (!c) continue;
			int fl = ex_field_cint_read(c, "m_nFavorabilityLimit", 100);
			if (fl > 0) ex_field_cint_write(c, "m_nFavorability", fl, EX_KEY_PASCHA3);
		}
	}
	if (mask & (1 << 4)) {
		struct page *ip = vm_get_global_page("g_cItemParam", NULL);
		struct page *arr = vm_get_field_page(ip, "m_acBase", NULL);
		for (int i = 0; i < vm_dd_array_len(arr); i++) {
			struct page *it = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
			if (it && ex_field_cint_read(it, "m_nNumof", 0) > 0 &&
			    ex_field_cint_read(it, "m_nNumof", 0) < 99)
				ex_field_cint_write(it, "m_nNumof", 99, EX_KEY_PASCHA3);
		}
	}
}

static void ex_p3_action(cJSON *act)
{
	struct page *gp = vm_get_global_page("g_cGameParam", NULL);
	if (!gp) return;
	int32_t v;
	if (ex_json_int(act, "gold", &v)) ex_field_cint_write(gp, "m_nMoney", vm_dd_clamp(v, 0, 99999999), EX_KEY_PASCHA3);
	if (ex_json_int(act, "addGold", &v)) ex_field_cint_write(gp, "m_nMoney", vm_dd_clamp(ex_field_cint_read(gp, "m_nMoney", 0) + v, 0, 99999999), EX_KEY_PASCHA3);
	if (ex_json_int(act, "ap", &v)) ex_field_cint_write(gp, "m_nActionPoint", vm_dd_clamp(v, 0, 999), EX_KEY_PASCHA3);
	if (ex_json_cheat_is(act, "fullHeal")) ex_p3_locks((1 << 1) | (1 << 2));
	if (ex_json_cheat_is(act, "maxItems")) ex_p3_locks(1 << 4);
	cJSON *ce = cJSON_GetObjectItemCaseSensitive(act, "exCharEdit");
	if (ce && cJSON_IsObject(ce)) {
		struct page *units = ex_p3_unit_list();
		int32_t target = -1;
		ex_json_int(ce, "target", &target);
		for (int i = 0; i < vm_dd_array_len(units); i++) {
			if (target >= 0 && i != target) continue;
			struct page *u = vm_safe_get_page(vm_dd_array_elem_slot(units, i));
			if (!u) continue;
			if (ex_json_int(ce, "lv", &v)) ex_field_cint_write(u, "m_nLevel", vm_dd_clamp(v, 1, 99), EX_KEY_PASCHA3);
			if (ex_json_int(ce, "exp", &v)) ex_field_cint_write(u, "m_nExp", vm_dd_clamp(v, 0, 9999999), EX_KEY_PASCHA3);
			if (ex_json_int(ce, "bp", &v)) ex_field_cint_write(u, "m_nBp", vm_dd_clamp(v, 0, 9999), EX_KEY_PASCHA3);
			if (ex_json_int(ce, "hp", &v)) ex_field_cint_write(u, "m_nHp", vm_dd_clamp(v, 1, 99999), EX_KEY_PASCHA3);
			if (ex_json_int(ce, "sp", &v)) ex_field_cint_write(u, "m_nSp", vm_dd_clamp(v, 0, 9999), EX_KEY_PASCHA3);
		}
	}
}

/* =========================================================================
 * 19. Haharanman (haharanman) — CEncryptInt + built-in CConfigParam cheats!
 * ========================================================================= */
static void ex_hhr_populate(cJSON *root)
{
	struct page *gp = vm_get_global_page("g_cGameParam", NULL);
	struct page *hp = vm_get_global_page("g_cHeroParam", NULL);
	cJSON_AddBoolToObject(root, "inGame", gp != NULL && hp != NULL);
	cJSON_AddBoolToObject(root, "inBattle", false);
	cJSON_AddNumberToObject(root, "ap", ex_hhr_read(hp, "m_nActionPoint", 0));
	cJSON_AddNumberToObject(root, "lv", ex_hhr_read(hp, "m_nLevel", 1));
	cJSON_AddNumberToObject(root, "exp", ex_hhr_read(hp, "m_nExp", 0));
	cJSON_AddNumberToObject(root, "mental", ex_hhr_read(gp, "m_nMental", 0));
	cJSON_AddNumberToObject(root, "refresh", ex_hhr_read(gp, "m_nRefreshPoint", 0));
	cJSON_AddNumberToObject(root, "days", ex_hhr_read(gp, "m_nLimitOfDays", 0));
}

static struct {
	uint32_t config_seq;
	int active_mask;
	int32_t original[6];
} hhr_assists;

static void ex_hhr_reset_assists(void)
{
	hhr_assists.config_seq = 0;
	hhr_assists.active_mask = 0;
}

static void ex_hhr_config_assists(int mask, bool one_shot)
{
	static const char *const fields[] = {
		"<SkipSpend>", "<SkipUseCount>", "<BoostExp>",
		"<DisableTroll>", "<DisableAbleActionTimezone>", "<DisableCounterCondition>",
	};
	int idx = vm_find_global_idx("g_cConfigParam", NULL);
	if (idx < 0 || idx >= heap[0].page->nr_vars)
		return;
	int slot = global_get(idx).i;
	struct page *cfg = vm_safe_get_page(slot);
	if (!cfg)
		return;
	uint32_t seq = heap_get_seq(slot);
	if (seq != hhr_assists.config_seq) {
		hhr_assists.active_mask = 0;
		hhr_assists.config_seq = seq;
	}
	for (int i = 0; i < 6; i++) {
		int bit = 1 << (2 + i / 3);
		bool was_active = hhr_assists.active_mask & bit;
		if (mask & bit) {
			if (one_shot) {
				if (was_active)
					hhr_assists.original[i] = 1;
			} else if (!was_active) {
				hhr_assists.original[i] = vm_get_field_int(cfg, fields[i], NULL, 0);
			}
			vm_set_field_int(cfg, fields[i], NULL, 1);
		} else if (!one_shot && was_active) {
			vm_set_field_int(cfg, fields[i], NULL, hhr_assists.original[i]);
		}
	}
	if (!one_shot)
		hhr_assists.active_mask = mask & 0x0C;
}

static void ex_hhr_refill(int mask)
{
	struct page *gp = vm_get_global_page("g_cGameParam", NULL);
	struct page *hp = vm_get_global_page("g_cHeroParam", NULL);
	if (!gp || !hp) return;
	if ((mask & (1 << 0)) && ex_hhr_read(hp, "m_nActionPoint", 0) < 99)
		ex_hhr_write(hp, "m_nActionPoint", 99);
	if (mask & (1 << 1)) {
		if (ex_hhr_read(gp, "m_nMental", 0) < 100) ex_hhr_write(gp, "m_nMental", 100);
		if (ex_hhr_read(gp, "m_nRefreshPoint", 0) < 99) ex_hhr_write(gp, "m_nRefreshPoint", 99);
	}
}

static void ex_hhr_action(cJSON *act)
{
	struct page *gp = vm_get_global_page("g_cGameParam", NULL);
	struct page *hp = vm_get_global_page("g_cHeroParam", NULL);
	if (!gp || !hp) return;
	int32_t v;
	if (ex_json_int(act, "ap", &v)) ex_hhr_write(hp, "m_nActionPoint", vm_dd_clamp(v, 0, 999));
	if (ex_json_int(act, "lv", &v)) ex_hhr_write(hp, "m_nLevel", vm_dd_clamp(v, 1, 99));
	if (ex_json_int(act, "exp", &v)) ex_hhr_write(hp, "m_nExp", vm_dd_clamp(v, 0, 999999));
	if (ex_json_int(act, "mental", &v)) ex_hhr_write(gp, "m_nMental", vm_dd_clamp(v, 0, 999));
	if (ex_json_int(act, "refresh", &v)) ex_hhr_write(gp, "m_nRefreshPoint", vm_dd_clamp(v, 0, 999));
	if (ex_json_int(act, "days", &v)) ex_hhr_write(gp, "m_nLimitOfDays", vm_dd_clamp(v, 1, 999));
	if (ex_json_cheat_is(act, "fullHeal") || ex_json_cheat_is(act, "boostAll"))
		ex_hhr_refill((1 << 0) | (1 << 1));
	if (ex_json_cheat_is(act, "boostAll"))
		ex_hhr_config_assists((1 << 2) | (1 << 3), true);
}

/* =========================================================================
 * 20. Tsumamigui 3 (tsumamigui3) — Mituki C_INT
 * ========================================================================= */
static void ex_t3g_populate(cJSON *root)
{
	struct page *gp = vm_get_global_page("g_cGameParam", NULL);
	struct page *hp = vm_get_global_page("g_cHeroineParam", NULL);
	struct page *arr = vm_get_field_page(hp, "m_acBase", NULL);
	cJSON_AddBoolToObject(root, "inGame", gp != NULL);
	cJSON_AddBoolToObject(root, "inBattle", false);
	cJSON_AddNumberToObject(root, "gold", ex_field_cint_read(gp, "m_nMoney", 0));
	cJSON_AddNumberToObject(root, "energy", ex_field_cint_read(gp, "m_nEnergy", 0));
	cJSON_AddNumberToObject(root, "day", ex_field_cint_read(gp, "m_nDate", 0));

	cJSON *chars = cJSON_CreateArray();
	for (int i = 0; i < vm_dd_array_len(arr) && i < 24; i++) {
		struct page *h = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
		if (!h) continue;
		char *nm = vm_dd_get_field_utf8(h, "m_szKey", NULL);
		if (!nm[0]) { free(nm); continue; }
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddStringToObject(jc, "name", nm);
		free(nm);
		cJSON_AddNumberToObject(jc, "lv", ex_field_cint_read(h, "m_nLevel", 0));
		cJSON_AddNumberToObject(jc, "love", ex_field_cint_read(h, "m_nRelation", 0));
		cJSON_AddNumberToObject(jc, "seed", ex_field_cint_read(h, "m_nSeed", 0));
		cJSON_AddNumberToObject(jc, "rival", ex_field_cint_read(h, "m_nEnemySemen", 0));
		cJSON_AddItemToArray(chars, jc);
	}
	cJSON_AddItemToObject(root, "exCharacters", chars);
}

static void ex_t3g_locks(int mask)
{
	struct page *gp = vm_get_global_page("g_cGameParam", NULL);
	if (!gp) return;
	if ((mask & (1 << 0)) && ex_field_cint_read(gp, "m_nMoney", 0) < 999999)
		ex_field_cint_write(gp, "m_nMoney", 999999, EX_KEY_TSUMAMIGUI3);
	if ((mask & (1 << 1)) && ex_field_cint_read(gp, "m_nEnergy", 0) < 20)
		ex_field_cint_write(gp, "m_nEnergy", 20, EX_KEY_TSUMAMIGUI3);
	if (mask & (1 << 2)) {
		struct page *arr = vm_get_field_page(vm_get_global_page("g_cHeroineParam", NULL), "m_acBase", NULL);
		for (int i = 0; i < vm_dd_array_len(arr); i++) {
			struct page *h = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
			if (h) ex_field_cint_write(h, "m_nEnemySemen", 0, EX_KEY_TSUMAMIGUI3);
		}
	}
	if (mask & (1 << 3)) {
		struct page *arr = vm_get_field_page(vm_get_global_page("g_cItemParam", NULL), "m_acBase", NULL);
		for (int i = 0; i < vm_dd_array_len(arr); i++) {
			struct page *it = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
			if (it && ex_field_cint_read(it, "m_nNumof", 0) > 0 &&
			    ex_field_cint_read(it, "m_nNumof", 0) < 99)
				ex_field_cint_write(it, "m_nNumof", 99, EX_KEY_TSUMAMIGUI3);
		}
	}
}

static void ex_t3g_action(cJSON *act)
{
	struct page *gp = vm_get_global_page("g_cGameParam", NULL);
	if (!gp) return;
	int32_t v;
	if (ex_json_int(act, "gold", &v)) ex_field_cint_write(gp, "m_nMoney", vm_dd_clamp(v, 0, 999999), EX_KEY_TSUMAMIGUI3);
	if (ex_json_int(act, "addGold", &v)) ex_field_cint_write(gp, "m_nMoney", vm_dd_clamp(ex_field_cint_read(gp, "m_nMoney", 0) + v, 0, 999999), EX_KEY_TSUMAMIGUI3);
	if (ex_json_int(act, "energy", &v)) ex_field_cint_write(gp, "m_nEnergy", vm_dd_clamp(v, 0, 20), EX_KEY_TSUMAMIGUI3);
	if (ex_json_cheat_is(act, "fullHeal")) ex_t3g_locks((1 << 1) | (1 << 2));
	if (ex_json_cheat_is(act, "maxItems")) ex_t3g_locks(1 << 3);
	cJSON *ce = cJSON_GetObjectItemCaseSensitive(act, "exCharEdit");
	if (ce && cJSON_IsObject(ce)) {
		struct page *arr = vm_get_field_page(vm_get_global_page("g_cHeroineParam", NULL), "m_acBase", NULL);
		int32_t target = -1;
		ex_json_int(ce, "target", &target);
		for (int i = 0; i < vm_dd_array_len(arr); i++) {
			if (target >= 0 && i != target) continue;
			struct page *h = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
			if (!h) continue;
			if (ex_json_int(ce, "lv", &v)) ex_field_cint_write(h, "m_nLevel", vm_dd_clamp(v, 0, 10), EX_KEY_TSUMAMIGUI3);
			if (ex_json_int(ce, "love", &v)) ex_field_cint_write(h, "m_nRelation", vm_dd_clamp(v, 0, 9999), EX_KEY_TSUMAMIGUI3);
			if (ex_json_int(ce, "seed", &v)) ex_field_cint_write(h, "m_nSeed", vm_dd_clamp(v, 0, 9999), EX_KEY_TSUMAMIGUI3);
			if (ex_json_int(ce, "rival", &v)) ex_field_cint_write(h, "m_nEnemySemen", vm_dd_clamp(v, 0, 9999), EX_KEY_TSUMAMIGUI3);
		}
	}
}

/* =========================================================================
 * 21. Maman (maman) — Mituki C_INT
 * ========================================================================= */
static void ex_mm_populate(cJSON *root)
{
	struct page *gp = vm_get_global_page("g_cGameParam", NULL);
	cJSON_AddBoolToObject(root, "inGame", gp != NULL);
	cJSON_AddBoolToObject(root, "inBattle", false);
	cJSON_AddNumberToObject(root, "condom", ex_field_cint_read(gp, "m_nCondom", 0));
	cJSON_AddNumberToObject(root, "wild", ex_field_cint_read(gp, "m_nWild", 0));
	cJSON_AddNumberToObject(root, "mcon", ex_field_cint_read(gp, "m_nMotherComplex", 0));
	cJSON_AddNumberToObject(root, "humor", ex_field_cint_read(gp, "m_nHumor", 0));
}

static void ex_mm_locks(int mask)
{
	struct page *gp = vm_get_global_page("g_cGameParam", NULL);
	if (!gp) return;
	if ((mask & (1 << 0)) && ex_field_cint_read(gp, "m_nCondom", 0) < 5)
		ex_field_cint_write(gp, "m_nCondom", 5, EX_KEY_MAMAN);
	if (mask & (1 << 1)) {
		int wlim = vm_dd_clamp(ex_field_cint_read(gp, "m_nWildLimit", 100), 1, 100);
		if (ex_field_cint_read(gp, "m_nWild", 0) < wlim)
			ex_field_cint_write(gp, "m_nWild", wlim, EX_KEY_MAMAN);
		if (ex_field_cint_read(gp, "m_nHumor", 0) < 5)
			ex_field_cint_write(gp, "m_nHumor", 5, EX_KEY_MAMAN);
	}
	if (mask & (1 << 2)) {
		struct page *fp = vm_get_global_page("g_cFondnessParam", NULL);
		struct page *arr = vm_get_field_page(fp, "m_anValue", NULL);
		for (int i = 0; i < vm_dd_array_len(arr); i++) {
			struct page *ci = vm_safe_get_page(vm_dd_array_elem_slot(arr, i));
			if (ci && ex_cint_read(ci, 0) < 100)
				ex_cint_write_xor(ci, 100, EX_KEY_MAMAN, 0);
		}
	}
}

static void ex_mm_action(cJSON *act)
{
	struct page *gp = vm_get_global_page("g_cGameParam", NULL);
	if (!gp) return;
	int32_t v;
	if (ex_json_int(act, "condom", &v)) ex_field_cint_write(gp, "m_nCondom", vm_dd_clamp(v, 0, 5), EX_KEY_MAMAN);
	if (ex_json_int(act, "wild", &v)) ex_field_cint_write(gp, "m_nWild", vm_dd_clamp(v, 0, 100), EX_KEY_MAMAN);
	if (ex_json_int(act, "mcon", &v)) ex_field_cint_write(gp, "m_nMotherComplex", vm_dd_clamp(v, 0, 100), EX_KEY_MAMAN);
	if (ex_json_int(act, "humor", &v)) ex_field_cint_write(gp, "m_nHumor", vm_dd_clamp(v, 0, 5), EX_KEY_MAMAN);
	if (ex_json_cheat_is(act, "fullHeal")) ex_mm_locks((1 << 0) | (1 << 1));
	if (ex_json_cheat_is(act, "boostAll")) ex_mm_locks(0x07);
}

/* =========================================================================
 * Unified Dispatchers Called From modifier.c
 * ========================================================================= */

static void vm_ex_populate_live_state_json(cJSON *root)
{
	const char *gid = ex_current_game_id();
	cJSON_AddStringToObject(root, "game", gid);
	pthread_mutex_lock(&g_mod_mutex);
	int mask = ex_get_lock_mask_locked(gid);
	pthread_mutex_unlock(&g_mod_mutex);
	cJSON_AddNumberToObject(root, "gameLockMask", mask);

	if (game_rance6) ex_r06_populate(root);
	else if (game_evenicle2 || game_evenicle2_rance) ex_ev2_populate(root);
	else if (game_daibanchou) ex_db_populate(root);
	else if (game_daiteikoku) ex_dt_populate(root);
	else if (game_galzoo) ex_gz_populate(root);
	else if (game_toushin3) ex_t3_populate(root);
	else if (game_bladebriders) ex_bb_populate(root);
	else if (game_night_remaster) ex_yk_populate(root);
	else if (game_waidonyo) ex_wd_populate(root);
	else if (game_sanbiki) ex_sb_populate(root);
	else if (game_dorapeko) ex_dp_populate(root);
	else if (game_haruurare) ex_hu_populate(root);
	else if (game_oyakorankan) ex_oy_populate(root);
	else if (game_tsumashibori) ex_ts_populate(root);
	else if (game_okusama) ex_ok_populate(root);
	else if (game_escalayer_reboot) ex_er_populate(root);
	else if (game_ixseal) ex_ix_populate(root);
	else if (game_pascha3) ex_p3_populate(root);
	else if (game_haharanman) ex_hhr_populate(root);
	else if (game_tsumamigui3) ex_t3g_populate(root);
	else if (game_maman) ex_mm_populate(root);
}

static void vm_ex_enforce_locks(void)
{
	const char *gid = ex_current_game_id();
	if (!gid[0])
		return;
	pthread_mutex_lock(&g_mod_mutex);
	int mask = ex_get_lock_mask_locked(gid);
	pthread_mutex_unlock(&g_mod_mutex);
	if (game_haharanman) {
		if (mask & 0x03)
			ex_hhr_refill(mask);
		if ((mask & 0x0C) || hhr_assists.active_mask)
			ex_hhr_config_assists(mask, false);
		return;
	}
	if (mask == 0)
		return;

	if (game_rance6) ex_r06_locks(mask);
	else if (game_evenicle2 || game_evenicle2_rance) ex_ev2_locks(mask);
	else if (game_daibanchou) ex_db_locks(mask);
	else if (game_daiteikoku) ex_dt_locks(mask);
	else if (game_galzoo) ex_gz_locks(mask);
	else if (game_toushin3) ex_t3_locks(mask);
	else if (game_bladebriders) ex_bb_locks(mask);
	else if (game_night_remaster) ex_yk_locks(mask);
	else if (game_waidonyo) ex_wd_locks(mask);
	else if (game_sanbiki) ex_sb_locks(mask);
	else if (game_dorapeko) ex_dp_locks(mask);
	else if (game_haruurare) ex_hu_locks(mask);
	else if (game_oyakorankan) ex_oy_locks(mask);
	else if (game_tsumashibori) ex_ts_locks(mask);
	else if (game_okusama) ex_ok_locks(mask);
	else if (game_escalayer_reboot) ex_er_locks(mask);
	else if (game_ixseal) ex_ix_locks(mask);
	else if (game_pascha3) ex_p3_locks(mask);
	else if (game_tsumamigui3) ex_t3g_locks(mask);
	else if (game_maman) ex_mm_locks(mask);
}

static void vm_ex_execute_action(const char *json_str)
{
	if (!json_str || !json_str[0])
		return;
	cJSON *act = cJSON_Parse(json_str);
	if (!act)
		return;

	const char *gid = ex_current_game_id();
	bool persist_cfg = false;
	int save_r10_mask = 0;
	int32_t new_mask = 0;
	if (gid[0] && ex_json_int(act, "setGameLockMask", &new_mask)) {
		pthread_mutex_lock(&g_mod_mutex);
		struct ex_game_lock_slot *slot = ex_find_lock_slot(gid);
		if (slot) {
			slot->mask = new_mask;
			slot->init = true;
			persist_cfg = true;
		}
		save_r10_mask = g_live_lock_mask;
		pthread_mutex_unlock(&g_mod_mutex);
	}
	vm_apply_ui_scale_from_json(act, &persist_cfg, &save_r10_mask);
	if (persist_cfg)
		persist_modifier_config_from_native(save_r10_mask);

	if (game_rance6) ex_r06_action(act);
	else if (game_evenicle2 || game_evenicle2_rance) ex_ev2_action(act);
	else if (game_daibanchou) ex_db_action(act);
	else if (game_daiteikoku) ex_dt_action(act);
	else if (game_galzoo) ex_gz_action(act);
	else if (game_toushin3) ex_t3_action(act);
	else if (game_bladebriders) ex_bb_action(act);
	else if (game_night_remaster) ex_yk_action(act);
	else if (game_waidonyo) ex_wd_action(act);
	else if (game_sanbiki) ex_sb_action(act);
	else if (game_dorapeko) ex_dp_action(act);
	else if (game_haruurare) ex_hu_action(act);
	else if (game_oyakorankan) ex_oy_action(act);
	else if (game_tsumashibori) ex_ts_action(act);
	else if (game_okusama) ex_ok_action(act);
	else if (game_escalayer_reboot) ex_er_action(act);
	else if (game_ixseal) ex_ix_action(act);
	else if (game_pascha3) ex_p3_action(act);
	else if (game_haharanman) ex_hhr_action(act);
	else if (game_tsumamigui3) ex_t3g_action(act);
	else if (game_maman) ex_mm_action(act);

	cJSON_Delete(act);
}

#endif /* SYSTEM4_MODIFIER_EXTRA_INC */
