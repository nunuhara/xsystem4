#define VM_PRIVATE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#include <unistd.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <zlib.h>

#ifdef __ANDROID__
#include <jni.h>
#endif

#include "cJSON.h"
#include "system4.h"
#include "system4/ain.h"
#include "system4/ex.h"
#include "system4/file.h"
#include "system4/instructions.h"
#include "system4/savefile.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "modifier_api.h"
#include "savedata.h"

/* GBK / SJIS names for special Rance 10 globals */
#define GBK_BATTLE_COUNT  "\xa1\xf4\xd5\xbd\xb6\xb7\xbb\xd8\xca\xfd"
#define SJIS_BATTLE_COUNT "\x81\x9f\x90\xed\x93\xac\x89\xf1\x90\x94"
#define GBK_DIFFICULTY    "\xa1\xf4\xc4\xd1\xb6\xc8"
#define SJIS_DIFFICULTY   "\x81\x9f\x93\xef\x88\xd5\x93\x78"

#ifndef MODIFIER_OFFLINE_ONLY
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "input.h"
#include "modifier.h"
#include "parts.h"
#include "sact.h"
#include "sprite.h"
#include "xsystem4.h"

static int s_pulse_key0_frames = 0;
static char *s_pending_cheat_gbk = NULL;
static int s_cheat_wait_frames = 0;
static bool s_pending_is_streamer_cheat = false;
static bool s_wait_post_rcs_streamer_refresh = false;
static bool s_pending_clear_card_cache = false;
static bool s_pending_live_ui_refresh = false;
static bool s_in_modifier_frame = false;
static bool s_game_config_ready = false;
static bool s_game_config_loaded = false;
static int s_last_observed_summary_mode = -1;

/* Lock bitmask flags (Rance 10) */
#define LOCK_PARTY_MAX_HP       (1 << 0)
#define LOCK_INFINITE_AP        (1 << 1)
#define LOCK_BATTLE_COMBO       (1 << 2)
#define LOCK_BATTLE_ROUND_1     (1 << 3)
#define LOCK_ONE_HIT_KILL       (1 << 4)
#define LOCK_ENEMY_ZERO_ATK     (1 << 5)
#define LOCK_LEADER_BOOST_CRIT  (1 << 6)
#define LOCK_INFINITE_SKILL_USE (1 << 7)
#define LOCK_INFINITE_ZIPPO     (1 << 8)
#define LOCK_MAX_RESOURCES      (1 << 9)
#define LOCK_TREASURE_100       (1 << 10)
#define LOCK_DISABLE_STREAMER_MODE (1 << 11)

/* Lock bitmask flags (Dohna Dohna) */
#define LOCK_DD_ALLY_MAX_HP       (1 << 0)
#define LOCK_DD_ALLY_MAX_MP       (1 << 1)
#define LOCK_DD_ALLY_MAX_VP       (1 << 2)
#define LOCK_DD_ONE_HIT_KILL      (1 << 3)
#define LOCK_DD_INFINITE_MONEY    (1 << 4)
#define LOCK_DD_INFINITE_TP       (1 << 5)
#define LOCK_DD_WORKER_BEST_COND  (1 << 6)
#define LOCK_DD_WORK_FREEZE_TIMER (1 << 7)

/* Lock bitmask flags (Evenicle) */
#define LOCK_EV_NO_ENCOUNT        (1 << 0)
#define LOCK_EV_ALLY_FULL_HP      (1 << 1)
#define LOCK_EV_INF_SKILL_P       (1 << 2)
#define LOCK_EV_INF_BP            (1 << 3)
#define LOCK_EV_ONE_HIT_KILL      (1 << 4)
#define LOCK_EV_INFINITE_MONEY    (1 << 5)

/* Lock bitmask flags (Sengoku Rance) */
#define LOCK_SR_INF_GOLD          (1 << 0)
#define LOCK_SR_INF_AP            (1 << 1)
#define LOCK_SR_MAX_SAT           (1 << 2)
#define LOCK_SR_ALLY_FULL_HP      (1 << 3)
#define LOCK_SR_ONE_HIT_KILL      (1 << 4)
#define LOCK_SR_UNLIM_ACTION      (1 << 5)

/* Lock bitmask flags (Rance 03) */
#define LOCK_R03_HP               (1 << 0)
#define LOCK_R03_NO_CD            (1 << 1)
#define LOCK_R03_INF_ITEMS        (1 << 2)
#define LOCK_R03_DROP_100         (1 << 3)
#define LOCK_R03_ONE_HIT_KILL     (1 << 4)

/* Lock bitmask flags (Rance 08 / Quest) */
#define LOCK_R08_ALLY_FULL_HP     (1 << 0)
#define LOCK_R08_ONE_HIT_KILL     (1 << 1)
#define LOCK_R08_INF_SKILLS       (1 << 2)
#define LOCK_R08_MAGNUM_MAX       (1 << 3)
#define LOCK_R08_INF_GOLD         (1 << 4)

/* Lock bitmask flags (Rance 09 / Helman) */
#define LOCK_R09_ALLY_GOD         (1 << 0)
#define LOCK_R09_ONE_HIT_KILL     (1 << 1)
#define LOCK_R09_INF_ACTION       (1 << 2)
#define LOCK_R09_DROP_100         (1 << 3)
#define LOCK_R09_INF_RESOURCES    (1 << 4)

/* Lock bitmask flags (Rance 01 / Quest for Hikari) */
#define LOCK_R01_FULL_HP          (1 << 0)
#define LOCK_R01_ONE_HIT_KILL     (1 << 1)
#define LOCK_R01_INF_GOLD         (1 << 2)
#define LOCK_R01_DROP_100         (1 << 3)
#define LOCK_R01_NO_DEBUFF        (1 << 4)

/* Lock bitmask flags (Rance 02 / Rebellious Maidens Kai) */
#define LOCK_R02_ALLY_FULL_HP_SP  (1 << 0)
#define LOCK_R02_ONE_HIT_KILL     (1 << 1)
#define LOCK_R02_INF_GOLD         (1 << 2)
#define LOCK_R02_INF_ITEMS        (1 << 3)

extern const struct ex *main_ex_get_file(void);

void vm_call_method_1(int fno, int struct_page, int value);

/* Shift-JIS global/member names for Evenicle (Evenicle.ain v6) */
/* NOTE: split hex escapes before [0-9a-fA-F] so C doesn't fold them into >0xFF. */
#define EV_G_CHARA_DATA   "g_a\x83L\x83\x83\x83\x89\x83" "f\x81[\x83^"
#define EV_F_NAME         "\x96\xbc\x91O"
#define EV_F_PARTY_SET    "\x83p\x81[\x83" "e\x83" "B\x81[\x90\xdd\x92\xe8"
#define EV_F_CHARA_ID     "\x83L\x83\x83\x83\x89\x8e\xaf\x95\xcaID"
#define EV_F_LV           "\x82k\x82u"
#define EV_F_HP           "\x82g\x82o"
#define EV_F_MAX_HP       "\x8d\xc5\x91\xe5\x82g\x82o"
#define EV_F_ATK          "\x8dU\x8c\x82\x97\xcd"
#define EV_F_MAG          "\x96\x82\x96@\x97\xcd"
#define EV_F_DEF          "\x96h\x8c\xe4\x97\xcd"
#define EV_F_SPD          "\x91" "f\x91\x81\x82\xb3"
#define EV_F_EXP          "\x8co\x8c\xb1\x92l"
#define EV_F_MAX_EXP      "\x8d\xc5\x91\xe5\x8co\x8c\xb1\x92l"
#define EV_F_SKILL_P      "\x83X\x83L\x83\x8b\x82o"
#define EV_F_BP           "\x82" "a\x82o"
#define EV_F_BASE_HP      "\x8a\xee\x91" "b\x82g\x82o"
#define EV_F_BASE_ATK     "\x8a\xee\x91" "b\x8dU\x8c\x82\x97\xcd"
#define EV_F_BASE_DEF     "\x8a\xee\x91" "b\x96h\x8c\xe4\x97\xcd"
#define EV_F_BASE_MAG     "\x8a\xee\x91" "b\x96\x82\x96@\x97\xcd"
#define EV_F_BASE_SPD     "\x8a\xee\x91" "b\x91" "f\x91\x81\x82\xb3"
#define EV_SJIS_TITLE_FN  "\x83^\x83" "C\x83g\x83\x8b\x89\xe6\x96\xca"

/* GBK global/member names for Chinese Evenicle (Evenicle.ain v6) */
#define EV_G_CHARA_DATA_CN "g_a\xa5\xad\xa5\xe3\xa5\xe9\xa5\xc7\xa9`\xa5\xbf"
#define EV_F_NAME_CN       "\xc3\xfb\xc7\xb0"
#define EV_F_PARTY_SET_CN  "\xa5\xd1\xa9`\xa5\xc6\xa5\xa3\xa9`\xd4O\xb6\xa8"
#define EV_F_CHARA_ID_CN   "\xa5\xad\xa5\xe3\xa5\xe9\xca\xb6\x84" "eID"
#define EV_F_LV_CN         "\xa3\xcc\xa3\xd6"
#define EV_F_HP_CN         "\xa3\xc8\xa3\xd0"
#define EV_F_MAX_HP_CN     "\xd7\xee\xb4\xf3\xa3\xc8\xa3\xd0"
#define EV_F_ATK_CN        "\xb9\xa5\xbb\xf7\xc1\xa6"
#define EV_F_MAG_CN        "\xc4\xa7\xb7\xa8\xc1\xa6"
#define EV_F_DEF_CN        "\xb7\xc0\xd3\xf9\xc1\xa6"
#define EV_F_SPD_CN        "\xcb\xd9\xb6\xc8"
#define EV_F_EXP_CN        "\xbdU\xf2Y\x82\x8e"
#define EV_F_MAX_EXP_CN    "\xd7\xee\xb4\xf3\xbdU\xf2Y\x82\x8e"
#define EV_F_SKILL_P_CN    "\xa5\xb9\xa5\xad\xa5\xeb\xa3\xd0"
#define EV_F_BP_CN         "\xa3\xc2\xa3\xd0"
#define EV_F_BASE_HP_CN    "\xbb\xf9\xb5" "A\xa3\xc8\xa3\xd0"
#define EV_F_BASE_ATK_CN   "\xbb\xf9\xb5" "A\xb9\xa5\xbb\xf7\xc1\xa6"
#define EV_F_BASE_DEF_CN   "\xbb\xf9\xb5" "A\xb7\xc0\xd3\xf9\xc1\xa6"
#define EV_F_BASE_MAG_CN   "\xbb\xf9\xb5" "A\xc4\xa7\xb7\xa8\xc1\xa6"
#define EV_F_BASE_SPD_CN   "\xbb\xf9\xb5" "A\xcb\xd8\xd4\xe7\xa4\xb5"
#define EV_CN_TITLE_FN     "\xa5\xbf\xa5\xa4\xa5\xc8\xa5\xeb\xbb\xad\xc3\xe6"

/* GBK byte sequences for MOD streamer-mode detection & in-game dialog */
#define GBK_STREAMER_PASSPHRASE "\xb9\xe0\xd7\xa2\x45\x69\x6e\x73\xb3\xc2\xd0\xbb\xd0\xbb\xdf\xf7"
#define GBK_CARD_STREAMER       "\xbf\xa8\xc5\xc6\xa3\xaf\xd6\xf7\xb2\xa5"
#define GBK_STREAMER_MODE       "\xd6\xf7\xb2\xa5\xc4\xa3\xca\xbd"
#define GBK_XIEXIEMIAO          "\xd0\xbb\xd0\xbb\xdf\xf7"
#define GBK_ZHUBO               "\xd6\xf7\xb2\xa5"
#define GBK_CHEAT_INVALID_TITLE "\xd7\xf7\xb1\xd7\xc2\xeb\xce\xde\xd0\xa7"
#define GBK_CHEAT_INVALID_MSG   "\xce\xb4\xd5\xd2\xb5\xbd\xb6\xd4\xd3\xa6\xd7\xf7\xb1\xd7\xc2\xeb\xa3\xa1"
#define GBK_CHEAT_OK_TITLE      "\xd7\xf7\xb1\xd7\xc2\xeb\xc9\xfa\xd0\xa7\xa3\xa1"
#define GBK_STREAMER_OFF_MSG    "\xd6\xf7\xb2\xa5\xc4\xa3\xca\xbd\xd2\xd1\xb9\xd8\xb1\xd5\xa3\xa1"

static pthread_mutex_t g_mod_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_mod_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_cfg_io_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_cfg_tmp_seq = 0;
static bool g_vm_ready_cached = false;
static int g_live_lock_mask = 0;
static bool g_has_lock_mask_init = false;
static int g_dohna_lock_mask = 0;
static bool g_has_dohna_lock_mask_init = false;
static int g_evenicle_lock_mask = 0;
static bool g_has_evenicle_lock_mask_init = false;
static int g_sr_lock_mask = 0;
static bool g_has_sr_lock_mask_init = false;
static int g_r03_lock_mask = 0;
static bool g_has_r03_lock_mask_init = false;
static int g_r08_lock_mask = 0;
static bool g_has_r08_lock_mask_init = false;
static int g_r09_lock_mask = 0;
static bool g_has_r09_lock_mask_init = false;
static int g_r01_lock_mask = 0;
static bool g_has_r01_lock_mask_init = false;
static int g_r02_lock_mask = 0;
static bool g_has_r02_lock_mask_init = false;
#define EX_MAX_GAMES 32
static void ex_snapshot_locks_locked(int *out_masks, bool *out_init);
static bool ex_all_locks_initialized_locked(void);
static void ex_load_locks_from_json_locked(cJSON *root);
static bool ex_locks_differ_from_json(cJSON *root, const int *snap_masks, const bool *snap_init);
static void ex_save_locks_to_json(cJSON *root, const int *snap_masks, const bool *snap_init);
static void ex_reset_locks_locked(void);
static inline bool vm_is_evenicle_family(void)
{
	return game_evenicle || game_evenicle_rance || game_evenicle_gaiden;
}
static int g_font_scale_pct = 100;
static int g_scrollbar_scale_pct = 100;
static bool g_has_ui_scale_init = false;
static char *g_ui_scale_game_key = NULL;
static int s_applied_font_scale_pct = 100;
static int s_applied_scrollbar_scale_pct = 100;
static int g_pending_lock_streamer_mode = -1;

static inline int clamp_font_scale_pct(int pct)
{
	if (pct < 75) return 75;
	if (pct > 160) return 160;
	return pct;
}

static inline int clamp_scrollbar_scale_pct(int pct)
{
	if (pct < 75) return 75;
	if (pct > 300) return 300;
	return pct;
}

static int cjson_get_per_game_scale(cJSON *root, const char *map_key, const char *game_key,
                                    int default_pct, int (*clamp_fn)(int))
{
	if (!root || !map_key || !game_key || !game_key[0])
		return default_pct;
	cJSON *map = cJSON_GetObjectItemCaseSensitive(root, map_key);
	if (!map || !cJSON_IsObject(map))
		return default_pct;
	cJSON *entry = cJSON_GetObjectItemCaseSensitive(map, game_key);
	if (!entry || !cJSON_IsNumber(entry))
		return default_pct;
	return clamp_fn ? clamp_fn(entry->valueint) : entry->valueint;
}

static void cjson_set_per_game_scale(cJSON *root, const char *map_key, const char *game_key,
                                     int pct, int default_pct)
{
	if (!root || !map_key || !game_key || !game_key[0])
		return;
	cJSON *map = cJSON_GetObjectItemCaseSensitive(root, map_key);
	if (map && !cJSON_IsObject(map)) {
		cJSON_DeleteItemFromObjectCaseSensitive(root, map_key);
		map = NULL;
	}
	if (!map) {
		if (pct == default_pct)
			return;
		map = cJSON_CreateObject();
		if (!map)
			return;
		cJSON_AddItemToObject(root, map_key, map);
	}
	cJSON_DeleteItemFromObjectCaseSensitive(map, game_key);
	if (pct != default_pct)
		cJSON_AddNumberToObject(map, game_key, pct);
}
static char *g_pending_action_json = NULL;
static char *g_cached_live_state_json = NULL;
static char *g_modifier_config_path = NULL;
static uint32_t g_live_state_req_seq = 0;
static uint32_t g_live_state_built_seq = 0;
#endif

/* Validate UTF-8 and replace any invalid/truncated/overlong/surrogate byte
 * sequence with U+FFFD (EF BF BD) so cJSON and JNI never see invalid UTF-8. */
static char *sanitize_utf8_alloc(const char *src, size_t len)
{
	if (!src || len == 0)
		return strdup("");
	const unsigned char *s = (const unsigned char *)src;
	char *out = malloc(len * 3u + 1u);
	if (!out)
		return strdup("");
	size_t o = 0;
	size_t i = 0;
	while (i < len && s[i] != 0) {
		unsigned char b0 = s[i];
		if (b0 < 0x80u) {
			out[o++] = (char)b0;
			i++;
		} else if (b0 >= 0xC2u && b0 <= 0xDFu) {
			if (i + 1 < len && (s[i + 1] & 0xC0u) == 0x80u) {
				out[o++] = (char)b0;
				out[o++] = (char)s[i + 1];
				i += 2;
			} else {
				out[o++] = (char)0xEF;
				out[o++] = (char)0xBF;
				out[o++] = (char)0xBD;
				i++;
			}
		} else if (b0 >= 0xE0u && b0 <= 0xEFu) {
			bool b1_ok = (i + 1 < len) && (
				(b0 == 0xE0u) ? (s[i + 1] >= 0xA0u && s[i + 1] <= 0xBFu) :
				(b0 == 0xEDu) ? (s[i + 1] >= 0x80u && s[i + 1] <= 0x9Fu) :
				((s[i + 1] & 0xC0u) == 0x80u));
			if (b1_ok && i + 2 < len && (s[i + 2] & 0xC0u) == 0x80u) {
				out[o++] = (char)b0;
				out[o++] = (char)s[i + 1];
				out[o++] = (char)s[i + 2];
				i += 3;
			} else {
				out[o++] = (char)0xEF;
				out[o++] = (char)0xBF;
				out[o++] = (char)0xBD;
				i += b1_ok ? 2 : 1;
			}
		} else if (b0 >= 0xF0u && b0 <= 0xF4u) {
			bool b1_ok = (i + 1 < len) && (
				(b0 == 0xF0u) ? (s[i + 1] >= 0x90u && s[i + 1] <= 0xBFu) :
				(b0 == 0xF4u) ? (s[i + 1] >= 0x80u && s[i + 1] <= 0x8Fu) :
				((s[i + 1] & 0xC0u) == 0x80u));
			bool b2_ok = b1_ok && (i + 2 < len) && ((s[i + 2] & 0xC0u) == 0x80u);
			if (b2_ok && i + 3 < len && (s[i + 3] & 0xC0u) == 0x80u) {
				out[o++] = (char)b0;
				out[o++] = (char)s[i + 1];
				out[o++] = (char)s[i + 2];
				out[o++] = (char)s[i + 3];
				i += 4;
			} else {
				out[o++] = (char)0xEF;
				out[o++] = (char)0xBF;
				out[o++] = (char)0xBD;
				i += b2_ok ? 3 : (b1_ok ? 2 : 1);
			}
		} else {
			out[o++] = (char)0xEF;
			out[o++] = (char)0xBF;
			out[o++] = (char)0xBD;
			i++;
		}
	}
	out[o] = '\0';
	return out;
}

/* Convert Shift-JIS / GBK VM text to guaranteed-valid UTF-8 without ever
 * falling back to raw non-ASCII bytes. */
static char *mod_sjis2utf_safe(const char *src, size_t len)
{
	if (!src || len == 0 || !src[0])
		return strdup("");
	char *u = sjis2utf(src, len);
	if (u) {
		char *clean = sanitize_utf8_alloc(u, strlen(u));
		free(u);
		return clean;
	}
	return sanitize_utf8_alloc(src, len);
}

/* =========================================================================
 * Cross-Platform & Out-of-Game gsave (.asd) Helpers
 * ========================================================================= */

static struct gsave_global *gs_find_global(struct gsave *gs, const char *name1, const char *name2)
{
	if (!gs || !gs->globals) return NULL;
	for (int i = 0; i < gs->nr_globals; i++) {
		const char *gn = gs->globals[i].name;
		if (!gn) continue;
		if (name1 && !strcmp(gn, name1)) return &gs->globals[i];
		if (name2 && !strcmp(gn, name2)) return &gs->globals[i];
	}
	return NULL;
}

static struct gsave_keyval *gs_get_record_kv(struct gsave *gs, int32_t rec_idx, const char *name1, const char *name2)
{
	if (!gs || rec_idx < 0 || rec_idx >= gs->nr_records) return NULL;
	struct gsave_record *rec = &gs->records[rec_idx];
	struct gsave_struct_def *sd = (gs->version >= 7 && rec->struct_index >= 0 && rec->struct_index < gs->nr_struct_defs)
		? &gs->struct_defs[rec->struct_index] : NULL;
	for (int i = 0; i < rec->nr_indices; i++) {
		int32_t kv_idx = rec->indices[i];
		if (kv_idx < 0 || kv_idx >= gs->nr_keyvals) continue;
		struct gsave_keyval *kv = &gs->keyvals[kv_idx];
		const char *fname = (sd && i < sd->nr_fields) ? sd->fields[i].name : kv->name;
		if (!fname) continue;
		if (name1 && !strcmp(fname, name1)) return kv;
		if (name2 && !strcmp(fname, name2)) return kv;
	}
	return NULL;
}

#ifndef MODIFIER_OFFLINE_ONLY
static bool gs_ensure_adv_shortcut_5_unlocked(struct gsave *gs, int32_t cfg_rec, bool *out_modified)
{
	struct gsave_keyval *kv_adv = (cfg_rec >= 0) ? gs_get_record_kv(gs, cfg_rec, "m_isShowAdvShortcut", NULL) : NULL;
	if (!kv_adv || kv_adv->value < 0 || kv_adv->value >= gs->nr_arrays)
		return false;
	struct gsave_array *ga = &gs->arrays[kv_adv->value];
	if (ga->rank < 1) {
		free(ga->dimensions);
		free(ga->flat_arrays);
		ga->rank = 1;
		ga->dimensions = xcalloc(1, sizeof(int32_t));
		ga->dimensions[0] = 7;
		ga->nr_flat_arrays = 1;
		ga->flat_arrays = xcalloc(1, sizeof(struct gsave_flat_array));
		ga->flat_arrays[0].type = AIN_BOOL;
		ga->flat_arrays[0].nr_values = 7;
		ga->flat_arrays[0].values = xcalloc(7, sizeof(struct gsave_array_value));
		for (int i = 0; i < 7; i++)
			ga->flat_arrays[0].values[i].type = AIN_BOOL;
		ga->flat_arrays[0].values[5].value = 1;
		if (out_modified)
			*out_modified = true;
		return true;
	}
	if (ga->rank != 1 || ga->nr_flat_arrays < 1 || !ga->flat_arrays)
		return false;
	struct gsave_flat_array *fa = &ga->flat_arrays[0];
	enum ain_data_type et = fa->type ? fa->type : AIN_BOOL;
	if (fa->nr_values < 6) {
		int old_n = fa->nr_values;
		int new_n = 7;
		fa->values = xrealloc_array(fa->values, old_n, new_n, sizeof(struct gsave_array_value));
		for (int i = old_n; i < new_n; i++) {
			fa->values[i].type = et;
			fa->values[i].value = 0;
		}
		fa->nr_values = new_n;
		fa->type = et;
		if (ga->dimensions)
			ga->dimensions[0] = new_n;
		if (out_modified)
			*out_modified = true;
	}
	if (fa->values[5].value != 1) {
		fa->values[5].type = et;
		fa->values[5].value = 1;
		if (out_modified)
			*out_modified = true;
	}
	return true;
}

static bool write_game_config_streamer_mode(const char *config_path, bool streamer_mode_enabled)
{
	if (!config_path || !config_path[0] || !file_exists(config_path))
		return false;
	bool prev_gbk = sys4_text_is_gbk;
	if (!ain)
		sys4_set_text_gbk(true);
	enum savefile_error serr;
	struct gsave *gs = gsave_read(config_path, &serr);
	if (!gs) {
		if (!ain)
			sys4_set_text_gbk(prev_gbk);
		return false;
	}
	struct gsave_global *g_cfg = gs_find_global(gs, "g_gameConfig", NULL);
	int32_t cfg_rec = g_cfg ? g_cfg->value : -1;
	struct gsave_keyval *kv = (cfg_rec >= 0) ? gs_get_record_kv(gs, cfg_rec, "m_isSummaryMode", NULL) : NULL;
	if (!kv) {
		gsave_free(gs);
		if (!ain)
			sys4_set_text_gbk(prev_gbk);
		return false;
	}
	int32_t target = streamer_mode_enabled ? 1 : 0;
	bool modified = false;
	if (target == 0) {
		gs_ensure_adv_shortcut_5_unlocked(gs, cfg_rec, &modified);
	}
	if (kv->value != target) {
		kv->value = target;
		modified = true;
	}
	if (!modified) {
		gsave_free(gs);
		if (!ain)
			sys4_set_text_gbk(prev_gbk);
		return true;
	}

	char *tmp_path = NULL;
	FILE *fp = atomic_save_open(config_path, "wb", &tmp_path);
	bool ok = false;
	if (fp) {
		if (gsave_write(gs, fp, false, 1) == SAVEFILE_SUCCESS)
			ok = atomic_save_commit(fp, config_path, tmp_path, true);
		else
			atomic_save_abort(fp, tmp_path);
	}
	gsave_free(gs);
	if (!ain)
		sys4_set_text_gbk(prev_gbk);
	return ok;
}

static void persist_streamer_mode_to_savedir(bool streamer_mode_enabled)
{
	if (!config.save_dir || !config.save_dir[0])
		return;
	char *cfg_path = savedir_path("GameConfig.asd");
	if (!cfg_path)
		return;
	write_game_config_streamer_mode(cfg_path, streamer_mode_enabled);
	free(cfg_path);
}

static bool write_config_atomic_unique(const char *cfg_path, const char *data, size_t len, uint32_t seq)
{
	if (!cfg_path || !cfg_path[0] || !data)
		return false;
#ifdef _WIN32
	unsigned long pid_v = 0;
#else
	unsigned long pid_v = (unsigned long)getpid();
#endif
	unsigned long tid_v = (unsigned long)(uintptr_t)pthread_self();
	size_t cap = strlen(cfg_path) + 64;
	char *tmp_path = malloc(cap);
	if (!tmp_path)
		return false;
	snprintf(tmp_path, cap, "%s.%lu.%lu.%u.tmp", cfg_path, pid_v, tid_v, seq);

	FILE *fp = fopen(tmp_path, "wb");
	if (!fp) {
		free(tmp_path);
		return false;
	}
	bool ok = (len == 0 || fwrite(data, 1, len, fp) == len) && fflush(fp) == 0;
#ifndef _WIN32
	if (ok) {
		int fd = fileno(fp);
		if (fd >= 0)
			(void)fsync(fd);
	}
#endif
	if (fclose(fp) != 0)
		ok = false;
	if (ok) {
		if (rename(tmp_path, cfg_path) != 0)
			ok = false;
		else
			file_note_fs_mutation();
	}
	if (!ok)
		remove(tmp_path);
	free(tmp_path);
	return ok;
}

static void persist_modifier_config_from_native(int lock_mask)
{
	char *cfg_path = NULL;
	char *ui_game_key = NULL;
	int cur_font_pct = 100;
	int cur_sb_pct = 100;
	bool has_ui_init = false;
	int cur_dd_mask = 0;
	int cur_ev_mask = 0;
	int cur_sr_mask = 0;
	int cur_r03_mask = 0;
	int cur_r08_mask = 0;
	int cur_r09_mask = 0;
	int cur_r01_mask = 0;
	int cur_r02_mask = 0;
	bool has_dd_init = false;
	bool has_ev_init = false;
	bool has_sr_init = false;
	bool has_r03_init = false;
	bool has_r08_init = false;
	bool has_r09_init = false;
	bool has_r01_init = false;
	bool has_r02_init = false;
	int cur_ex_masks[EX_MAX_GAMES];
	bool has_ex_init[EX_MAX_GAMES];

	pthread_mutex_lock(&g_mod_mutex);
	if (g_modifier_config_path && g_modifier_config_path[0])
		cfg_path = strdup(g_modifier_config_path);
	if (g_ui_scale_game_key && g_ui_scale_game_key[0])
		ui_game_key = strdup(g_ui_scale_game_key);
	has_ui_init = g_has_ui_scale_init;
	cur_font_pct = g_font_scale_pct;
	cur_sb_pct = g_scrollbar_scale_pct;
	cur_dd_mask = g_dohna_lock_mask;
	cur_ev_mask = g_evenicle_lock_mask;
	cur_sr_mask = g_sr_lock_mask;
	cur_r03_mask = g_r03_lock_mask;
	cur_r08_mask = g_r08_lock_mask;
	cur_r09_mask = g_r09_lock_mask;
	cur_r01_mask = g_r01_lock_mask;
	cur_r02_mask = g_r02_lock_mask;
	has_dd_init = g_has_dohna_lock_mask_init;
	has_ev_init = g_has_evenicle_lock_mask_init;
	has_sr_init = g_has_sr_lock_mask_init;
	has_r03_init = g_has_r03_lock_mask_init;
	has_r08_init = g_has_r08_lock_mask_init;
	has_r09_init = g_has_r09_lock_mask_init;
	has_r01_init = g_has_r01_lock_mask_init;
	has_r02_init = g_has_r02_lock_mask_init;
	ex_snapshot_locks_locked(cur_ex_masks, has_ex_init);
	pthread_mutex_unlock(&g_mod_mutex);

#ifndef __ANDROID__
	if (!cfg_path) {
		const char *env_cfg = getenv("XSYS4_MOD_CONFIG_FILE");
		if (env_cfg && env_cfg[0])
			cfg_path = strdup(env_cfg);
	}
	if (!ui_game_key) {
		const char *env_key = getenv("XSYS4_MOD_GAME_KEY");
		if (env_key && env_key[0])
			ui_game_key = strdup(env_key);
	}
#endif
	if (!cfg_path) {
		free(ui_game_key);
		return;
	}

	pthread_mutex_lock(&g_cfg_io_mutex);
	uint32_t tmp_seq = ++g_cfg_tmp_seq;

	cJSON *root = NULL;
	if (file_exists(cfg_path)) {
		size_t sz = 0;
		char *raw = file_read(cfg_path, &sz);
		if (raw) {
			root = cJSON_Parse(raw);
			free(raw);
		}
	}
	if (!root || !cJSON_IsObject(root)) {
		cJSON_Delete(root);
		root = cJSON_CreateObject();
		cJSON_AddBoolToObject(root, "floating_enabled", true);
		cJSON_AddBoolToObject(root, "floating_default_on_v1", true);
		cJSON_AddBoolToObject(root, "ctrl_overlay_enabled", false);
		cJSON_AddBoolToObject(root, "ctrl_default_on_v1", true);
		cJSON_AddBoolToObject(root, "ctrl_default_off_v2", true);
		cJSON_AddBoolToObject(root, "ctrl_overlay_visible", false);
	}

	bool want_dis = (lock_mask & LOCK_DISABLE_STREAMER_MODE) != 0;
	cJSON *j_dis = cJSON_GetObjectItem(root, "disable_streamer_mode");
	cJSON *j_mask = cJSON_GetObjectItem(root, "lock_mask");
	cJSON *j_dd_mask = cJSON_GetObjectItem(root, "dohna_lock_mask");
	cJSON *j_ev_mask = cJSON_GetObjectItem(root, "evenicle_lock_mask");
	cJSON *j_sr_mask = cJSON_GetObjectItem(root, "sengokurance_lock_mask");
	cJSON *j_r03_mask = cJSON_GetObjectItem(root, "rance03_lock_mask");
	cJSON *j_r08_mask = cJSON_GetObjectItem(root, "rance08_lock_mask");
	cJSON *j_r09_mask = cJSON_GetObjectItem(root, "rance09_lock_mask");
	cJSON *j_r01_mask = cJSON_GetObjectItem(root, "rance01_lock_mask");
	cJSON *j_r02_mask = cJSON_GetObjectItem(root, "rance02_lock_mask");
	bool cur_dis = j_dis ? cJSON_IsTrue(j_dis) : false;
	int cur_mask = (j_mask && cJSON_IsNumber(j_mask)) ? j_mask->valueint : -1;
	int disk_dd_mask = (j_dd_mask && cJSON_IsNumber(j_dd_mask)) ? j_dd_mask->valueint : 0;
	int disk_ev_mask = (j_ev_mask && cJSON_IsNumber(j_ev_mask)) ? j_ev_mask->valueint : 0;
	int disk_sr_mask = (j_sr_mask && cJSON_IsNumber(j_sr_mask)) ? j_sr_mask->valueint : 0;
	int disk_r03_mask = (j_r03_mask && cJSON_IsNumber(j_r03_mask)) ? j_r03_mask->valueint : 0;
	int disk_r08_mask = (j_r08_mask && cJSON_IsNumber(j_r08_mask)) ? j_r08_mask->valueint : 0;
	int disk_r09_mask = (j_r09_mask && cJSON_IsNumber(j_r09_mask)) ? j_r09_mask->valueint : 0;
	int disk_r01_mask = (j_r01_mask && cJSON_IsNumber(j_r01_mask)) ? j_r01_mask->valueint : 0;
	int disk_r02_mask = (j_r02_mask && cJSON_IsNumber(j_r02_mask)) ? j_r02_mask->valueint : 0;
	bool persist_per_game_ui = has_ui_init && ui_game_key && ui_game_key[0];
	int disk_font = persist_per_game_ui
		? cjson_get_per_game_scale(root, "game_font_scale_pct", ui_game_key, 100, clamp_font_scale_pct)
		: cur_font_pct;
	int disk_sb = persist_per_game_ui
		? cjson_get_per_game_scale(root, "game_scrollbar_scale_pct", ui_game_key, 100, clamp_scrollbar_scale_pct)
		: cur_sb_pct;
	if (j_dis && j_mask && cur_dis == want_dis && cur_mask == lock_mask &&
	    (!has_dd_init || (j_dd_mask && disk_dd_mask == cur_dd_mask)) &&
	    (!has_ev_init || (j_ev_mask && disk_ev_mask == cur_ev_mask)) &&
	    (!has_sr_init || (j_sr_mask && disk_sr_mask == cur_sr_mask)) &&
	    (!has_r03_init || (j_r03_mask && disk_r03_mask == cur_r03_mask)) &&
	    (!has_r08_init || (j_r08_mask && disk_r08_mask == cur_r08_mask)) &&
	    (!has_r09_init || (j_r09_mask && disk_r09_mask == cur_r09_mask)) &&
	    (!has_r01_init || (j_r01_mask && disk_r01_mask == cur_r01_mask)) &&
	    (!has_r02_init || (j_r02_mask && disk_r02_mask == cur_r02_mask)) &&
	    !ex_locks_differ_from_json(root, cur_ex_masks, has_ex_init) &&
	    (!persist_per_game_ui || (disk_font == cur_font_pct && disk_sb == cur_sb_pct))) {
		cJSON_Delete(root);
		pthread_mutex_unlock(&g_cfg_io_mutex);
		free(cfg_path);
		free(ui_game_key);
		return;
	}

	cJSON *j_seq = cJSON_GetObjectItem(root, "config_seq");
	int next_seq = ((j_seq && cJSON_IsNumber(j_seq)) ? j_seq->valueint : 0) + 1;

	cJSON_DeleteItemFromObject(root, "disable_streamer_mode");
	cJSON_AddBoolToObject(root, "disable_streamer_mode", want_dis);
	cJSON_DeleteItemFromObject(root, "lock_mask");
	cJSON_AddNumberToObject(root, "lock_mask", lock_mask);
	if (has_dd_init || j_dd_mask) {
		cJSON_DeleteItemFromObject(root, "dohna_lock_mask");
		cJSON_AddNumberToObject(root, "dohna_lock_mask", has_dd_init ? cur_dd_mask : disk_dd_mask);
	}
	if (has_ev_init || j_ev_mask) {
		cJSON_DeleteItemFromObject(root, "evenicle_lock_mask");
		cJSON_AddNumberToObject(root, "evenicle_lock_mask", has_ev_init ? cur_ev_mask : disk_ev_mask);
	}
	if (has_sr_init || j_sr_mask) {
		cJSON_DeleteItemFromObject(root, "sengokurance_lock_mask");
		cJSON_AddNumberToObject(root, "sengokurance_lock_mask", has_sr_init ? cur_sr_mask : disk_sr_mask);
	}
	if (has_r03_init || j_r03_mask) {
		cJSON_DeleteItemFromObject(root, "rance03_lock_mask");
		cJSON_AddNumberToObject(root, "rance03_lock_mask", has_r03_init ? cur_r03_mask : disk_r03_mask);
	}
	if (has_r08_init || j_r08_mask) {
		cJSON_DeleteItemFromObject(root, "rance08_lock_mask");
		cJSON_AddNumberToObject(root, "rance08_lock_mask", has_r08_init ? cur_r08_mask : disk_r08_mask);
	}
	if (has_r09_init || j_r09_mask) {
		cJSON_DeleteItemFromObject(root, "rance09_lock_mask");
		cJSON_AddNumberToObject(root, "rance09_lock_mask", has_r09_init ? cur_r09_mask : disk_r09_mask);
	}
	if (has_r01_init || j_r01_mask) {
		cJSON_DeleteItemFromObject(root, "rance01_lock_mask");
		cJSON_AddNumberToObject(root, "rance01_lock_mask", has_r01_init ? cur_r01_mask : disk_r01_mask);
	}
	if (has_r02_init || j_r02_mask) {
		cJSON_DeleteItemFromObject(root, "rance02_lock_mask");
		cJSON_AddNumberToObject(root, "rance02_lock_mask", has_r02_init ? cur_r02_mask : disk_r02_mask);
	}
	ex_save_locks_to_json(root, cur_ex_masks, has_ex_init);
	cJSON_DeleteItemFromObject(root, "font_scale_pct");
	cJSON_DeleteItemFromObject(root, "scrollbar_scale_pct");
	if (persist_per_game_ui) {
		cjson_set_per_game_scale(root, "game_font_scale_pct", ui_game_key, cur_font_pct, 100);
		cjson_set_per_game_scale(root, "game_scrollbar_scale_pct", ui_game_key, cur_sb_pct, 100);
	}
	cJSON_DeleteItemFromObject(root, "config_seq");
	cJSON_AddNumberToObject(root, "config_seq", next_seq);
	if (!cJSON_GetObjectItem(root, "floating_default_on_v1"))
		cJSON_AddBoolToObject(root, "floating_default_on_v1", true);
	if (!cJSON_GetObjectItem(root, "floating_enabled"))
		cJSON_AddBoolToObject(root, "floating_enabled", true);
	if (!cJSON_GetObjectItem(root, "ctrl_default_on_v1"))
		cJSON_AddBoolToObject(root, "ctrl_default_on_v1", true);
	if (!cJSON_GetObjectItem(root, "ctrl_overlay_enabled"))
		cJSON_AddBoolToObject(root, "ctrl_overlay_enabled", false);
	if (!cJSON_GetObjectItem(root, "ctrl_overlay_visible")) {
		cJSON *j_ctrl_en = cJSON_GetObjectItem(root, "ctrl_overlay_enabled");
		bool ctrl_en = j_ctrl_en ? cJSON_IsTrue(j_ctrl_en) : false;
		cJSON_AddBoolToObject(root, "ctrl_overlay_visible", ctrl_en);
	}

	char *out = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (out) {
		write_config_atomic_unique(cfg_path, out, strlen(out), tmp_seq);
		free(out);
	}
	pthread_mutex_unlock(&g_cfg_io_mutex);
	free(cfg_path);
	free(ui_game_key);
}

#endif

#if defined(__ANDROID__) || defined(ALICEHUB_IOS) || defined(MODIFIER_STANDALONE_TEST) || defined(MODIFIER_OFFLINE_ONLY)

static int32_t gs_get_field_int(struct gsave *gs, int32_t rec_idx, const char *name1, const char *name2, int32_t def_val)
{
	struct gsave_keyval *kv = gs_get_record_kv(gs, rec_idx, name1, name2);
	return kv ? kv->value : def_val;
}

static bool gs_set_field_int(struct gsave *gs, int32_t rec_idx, const char *name1, const char *name2, int32_t val)
{
	struct gsave_keyval *kv = gs_get_record_kv(gs, rec_idx, name1, name2);
	if (kv) kv->value = val;
	return kv != NULL;
}

static struct gsave_flat_array *gs_get_flat_array(struct gsave *gs, int32_t arr_idx)
{
	if (!gs || arr_idx < 0 || arr_idx >= gs->nr_arrays) return NULL;
	struct gsave_array *a = &gs->arrays[arr_idx];
	if (a->rank < 1 || a->nr_flat_arrays < 1 || !a->flat_arrays) return NULL;
	return &a->flat_arrays[0];
}

static int32_t gs_get_tt(struct gsave *gs, int idx, int32_t def_val)
{
	struct gsave_global *g = gs_find_global(gs, "tt", NULL);
	if (!g) return def_val;
	struct gsave_flat_array *fa = gs_get_flat_array(gs, g->value);
	if (!fa || idx < 0 || idx >= fa->nr_values) return def_val;
	return fa->values[idx].value;
}

static void gs_set_tt(struct gsave *gs, int idx, int32_t val)
{
	struct gsave_global *g = gs_find_global(gs, "tt", NULL);
	if (!g) return;
	struct gsave_flat_array *fa = gs_get_flat_array(gs, g->value);
	if (!fa || idx < 0 || idx >= fa->nr_values) return;
	fa->values[idx].value = val;
}

static void gs_boost_card_array(struct gsave *gs, int32_t arr_idx, int set_count, int add_count, int atk_bonus, int hp_bonus, int *total_cards)
{
	struct gsave_flat_array *fa = gs_get_flat_array(gs, arr_idx);
	if (!fa) return;
	for (int i = 0; i < fa->nr_values; i++) {
		int32_t c_rec = fa->values[i].value;
		if (c_rec < 0 || c_rec >= gs->nr_records) continue;
		if (total_cards) (*total_cards)++;
		if (set_count > 0) {
			gs_set_field_int(gs, c_rec, "m_count", NULL, set_count);
			gs_set_field_int(gs, c_rec, "m_lastCount", NULL, set_count);
		} else if (add_count > 0) {
			int32_t cur = gs_get_field_int(gs, c_rec, "m_count", NULL, 1);
			gs_set_field_int(gs, c_rec, "m_count", NULL, cur + add_count);
			gs_set_field_int(gs, c_rec, "m_lastCount", NULL, cur + add_count);
		}
		if (atk_bonus > 0) {
			int32_t cur_atk = gs_get_field_int(gs, c_rec, "m_totalAtk", NULL, 100);
			if (cur_atk < atk_bonus)
				gs_set_field_int(gs, c_rec, "m_totalAtk", NULL, atk_bonus);
		}
		if (hp_bonus > 0) {
			int32_t cur_hp = gs_get_field_int(gs, c_rec, "m_totalHp", NULL, 100);
			if (cur_hp < hp_bonus)
				gs_set_field_int(gs, c_rec, "m_totalHp", NULL, hp_bonus);
		}
	}
}

static int gs_process_all_cards(struct gsave *gs, int set_count, int add_count, int atk_bonus, int hp_bonus)
{
	int total = 0;
	struct gsave_global *g_pc = gs_find_global(gs, "g_playerCard", NULL);
	if (!g_pc) return 0;
	int32_t pc_rec = g_pc->value;
	int32_t org_arr = gs_get_field_int(gs, pc_rec, "m_org", NULL, -1);
	struct gsave_flat_array *fa_org = gs_get_flat_array(gs, org_arr);
	if (fa_org) {
		for (int i = 0; i < fa_org->nr_values; i++) {
			int32_t org_rec = fa_org->values[i].value;
			int32_t card_arr = gs_get_field_int(gs, org_rec, "m_card", NULL, -1);
			gs_boost_card_array(gs, card_arr, set_count, add_count, atk_bonus, hp_bonus, &total);
		}
	}
	int32_t stock_arr = gs_get_field_int(gs, pc_rec, "m_stock", NULL, -1);
	gs_boost_card_array(gs, stock_arr, set_count, add_count, atk_bonus, hp_bonus, &total);
	int32_t extra_arr = gs_get_field_int(gs, pc_rec, "m_extraCard", NULL, -1);
	gs_boost_card_array(gs, extra_arr, set_count, add_count, atk_bonus, hp_bonus, &total);
	int32_t old_arr = gs_get_field_int(gs, pc_rec, "m_card", "m_oldCard", -1);
	gs_boost_card_array(gs, old_arr, set_count, add_count, atk_bonus, hp_bonus, NULL);
	return total;
}

static int gs_process_all_characters(struct gsave *gs, int set_star, int set_exp)
{
	struct gsave_global *g_ch = gs_find_global(gs, "g_character", NULL);
	if (!g_ch) return 0;
	int32_t ch_rec = g_ch->value;
	int32_t arr_idx = gs_get_field_int(gs, ch_rec, "m_character", NULL, -1);
	struct gsave_flat_array *fa = gs_get_flat_array(gs, arr_idx);
	if (!fa) return 0;
	for (int i = 0; i < fa->nr_values; i++) {
		int32_t c_rec = fa->values[i].value;
		if (set_star > 0) {
			int32_t cur = gs_get_field_int(gs, c_rec, "m_star", NULL, 0);
			if (cur < set_star)
				gs_set_field_int(gs, c_rec, "m_star", NULL, set_star);
		}
		if (set_exp > 0) {
			gs_set_field_int(gs, c_rec, "<Exp>", NULL, set_exp);
		}
	}
	return fa->nr_values;
}

static void json_read_common_status_gs(cJSON *root, struct gsave *gs, int32_t cs_rec)
{
	if (cs_rec < 0) return;
	cJSON_AddNumberToObject(root, "partyAtk", gs_get_field_int(gs, cs_rec, "<Atk>", NULL, 0));
	cJSON_AddNumberToObject(root, "atkPerPhys", gs_get_field_int(gs, cs_rec, "m_AtkPerPhysical", NULL, 0));
	cJSON_AddNumberToObject(root, "atkPerMag", gs_get_field_int(gs, cs_rec, "m_AtkPerMagical", NULL, 0));
	cJSON_AddNumberToObject(root, "defPerPhys", gs_get_field_int(gs, cs_rec, "m_DefPerPhysical", NULL, 0));
	cJSON_AddNumberToObject(root, "defPerMag", gs_get_field_int(gs, cs_rec, "m_DefPerMagical", NULL, 0));
	cJSON_AddNumberToObject(root, "perAvoid", gs_get_field_int(gs, cs_rec, "m_PerAvoidance", NULL, 0));
	cJSON_AddNumberToObject(root, "decBadCond", gs_get_field_int(gs, cs_rec, "m_DecPerBadCondition", NULL, 0));
	cJSON_AddNumberToObject(root, "perAutoAtk", gs_get_field_int(gs, cs_rec, "m_perAutoBattleAtk", NULL, 0));
	cJSON_AddNumberToObject(root, "perCrit", gs_get_field_int(gs, cs_rec, "m_PerCritical", NULL, 0));
	cJSON_AddNumberToObject(root, "perMulti", gs_get_field_int(gs, cs_rec, "m_PerMultiAttack", NULL, 0));
	cJSON_AddNumberToObject(root, "perExpUp", gs_get_field_int(gs, cs_rec, "m_PerExpUp", NULL, 0));
	cJSON_AddNumberToObject(root, "perHpUp", gs_get_field_int(gs, cs_rec, "m_PerHpUp", NULL, 0));
	cJSON_AddNumberToObject(root, "perAutoHeal", gs_get_field_int(gs, cs_rec, "m_PerAutoHeal", NULL, 0));
	cJSON_AddNumberToObject(root, "perFindTreasure", gs_get_field_int(gs, cs_rec, "m_PerFindTreasure", NULL, 0));
	cJSON_AddNumberToObject(root, "perInjection", gs_get_field_int(gs, cs_rec, "m_PerInjection", NULL, 0));
	cJSON_AddNumberToObject(root, "addPerHit", gs_get_field_int(gs, cs_rec, "m_AddPerHit", NULL, 0));
	cJSON_AddNumberToObject(root, "statusMaxPer", gs_get_field_int(gs, cs_rec, "StatusMaxPer", NULL, 0));
}

static bool json_write_int_if_present(cJSON *root, const char *key, struct gsave *gs, int32_t rec_idx, const char *f1, const char *f2)
{
	cJSON *item = cJSON_GetObjectItem(root, key);
	if (!item) return true;
	return cJSON_IsNumber(item) && gs_set_field_int(gs, rec_idx, f1, f2, item->valueint);
}

static char *read_local_save_summary_str(const char *path)
{
	sys4_set_text_gbk(true);
	enum savefile_error serr;
	struct gsave *gs = gsave_read(path, &serr);
	cJSON *root = cJSON_CreateObject();
	if (!gs) {
		cJSON_AddBoolToObject(root, "valid", false);
		cJSON_AddStringToObject(root, "error", savefile_strerror(serr));
		char *out = cJSON_PrintUnformatted(root);
		cJSON_Delete(root);
		return out;
	}
	cJSON_AddBoolToObject(root, "valid", true);
	cJSON_AddBoolToObject(root, "ok", true);
	if (gs->nr_comments > 0 && gs->comments[0] && gs->comments[0]->size > 0) {
		char *utf = mod_sjis2utf_safe(gs->comments[0]->text, (size_t)gs->comments[0]->size);
		cJSON_AddStringToObject(root, "comment", utf ? utf : "");
		free(utf);
	} else {
		cJSON_AddStringToObject(root, "comment", "");
	}

	struct gsave_global *g_gc = gs_find_global(gs, "g_gameContext", NULL);
	int32_t gc_rec = g_gc ? g_gc->value : -1;
	cJSON_AddNumberToObject(root, "turn", gs_get_field_int(gs, gc_rec, "m_turn", NULL, 0));
	cJSON_AddNumberToObject(root, "chapter", gs_get_field_int(gs, gc_rec, "m_chapter", NULL, 0));

	struct gsave_global *g_diff = gs_find_global(gs, GBK_DIFFICULTY, SJIS_DIFFICULTY);
	struct gsave_global *g_bc = gs_find_global(gs, GBK_BATTLE_COUNT, SJIS_BATTLE_COUNT);
	cJSON_AddNumberToObject(root, "difficulty", g_diff ? g_diff->value : 1);
	cJSON_AddNumberToObject(root, "battleCount", g_bc ? g_bc->value : 0);

	struct gsave_global *g_pcp = gs_find_global(gs, "g_playerCommonParam", NULL);
	int32_t pcp_rec = g_pcp ? g_pcp->value : -1;
	cJSON_AddNumberToObject(root, "foodTicket", gs_get_field_int(gs, pcp_rec, "m_foodTicket", NULL, 0));
	cJSON_AddNumberToObject(root, "honor", gs_get_field_int(gs, pcp_rec, "m_honor", NULL, 0));
	cJSON_AddNumberToObject(root, "friendPoint", gs_get_field_int(gs, pcp_rec, "m_friendPoint", NULL, 0));
	cJSON_AddNumberToObject(root, "totalExp", gs_get_field_int(gs, pcp_rec, "<TotalExp>", NULL, 0));
	cJSON_AddNumberToObject(root, "partyRank", gs_get_field_int(gs, pcp_rec, "<PartyRank>", NULL, 0));
	cJSON_AddNumberToObject(root, "treasureNotGetCount", gs_get_field_int(gs, pcp_rec, "<TreasureNotGetCount>", NULL, 0));
	cJSON_AddNumberToObject(root, "itemStar", gs_get_field_int(gs, pcp_rec, "<ItemStar>", NULL, 0));
	cJSON_AddNumberToObject(root, "isUsedHannyZippo", gs_get_field_int(gs, pcp_rec, "<IsUsedHannyZippo>", NULL, 0));

	struct gsave_global *g_pb = gs_find_global(gs, "g_partyBonus", NULL);
	int32_t pb_rec = g_pb ? g_pb->value : -1;
	cJSON_AddNumberToObject(root, "partyBonusMaxPoint", gs_get_field_int(gs, pb_rec, "m_maxPoint", NULL, 0));

	struct gsave_global *g_party = gs_find_global(gs, "g_party", NULL);
	int32_t party_rec = g_party ? g_party->value : -1;
	int32_t ps_rec = gs_get_field_int(gs, party_rec, "m_status", NULL, -1);
	int32_t cs_rec = gs_get_field_int(gs, ps_rec, "m_status", NULL, -1);
	cJSON_AddNumberToObject(root, "partyHp", gs_get_field_int(gs, party_rec, "m_hp", NULL, 0));
	cJSON_AddNumberToObject(root, "partyMaxHp", gs_get_field_int(gs, ps_rec, "m_totalHp", NULL, 0));
	cJSON_AddNumberToObject(root, "partyAvailCount", gs_get_field_int(gs, party_rec, "m_availableCount", NULL, 4));
	cJSON_AddNumberToObject(root, "partyIsFixStatus", gs_get_field_int(gs, party_rec, "<IsFixStatus>", NULL, 0));
	json_read_common_status_gs(root, gs, cs_rec);

	cJSON_AddNumberToObject(root, "humanCount", gs_get_tt(gs, 33, 300000));
	cJSON_AddNumberToObject(root, "humanDead", gs_get_tt(gs, 34, 0));
	cJSON_AddNumberToObject(root, "humanAlivePer", gs_get_tt(gs, 35, 100));
	cJSON_AddNumberToObject(root, "humanDeadPer", gs_get_tt(gs, 36, 0));
	cJSON_AddNumberToObject(root, "curseTurn", gs_get_tt(gs, 41, 0));
	cJSON_AddNumberToObject(root, "rareLuck", gs_get_tt(gs, 42, 0));
	cJSON_AddNumberToObject(root, "hannyZippo", gs_get_tt(gs, 101, 0));
	cJSON_AddNumberToObject(root, "seaPlayerArmy", gs_get_tt(gs, 66, 0));
	cJSON_AddNumberToObject(root, "seaEnemyArmy", gs_get_tt(gs, 68, 0));
	cJSON_AddNumberToObject(root, "leazasState", gs_get_tt(gs, 13, 0));
	cJSON_AddNumberToObject(root, "helmanState", gs_get_tt(gs, 14, 0));
	cJSON_AddNumberToObject(root, "zethState", gs_get_tt(gs, 15, 0));
	cJSON_AddNumberToObject(root, "freeCitiesState", gs_get_tt(gs, 16, 0));
	cJSON_AddNumberToObject(root, "leazasPArmy", gs_get_tt(gs, 25, 300));
	cJSON_AddNumberToObject(root, "helmanPArmy", gs_get_tt(gs, 26, 300));
	cJSON_AddNumberToObject(root, "zethPArmy", gs_get_tt(gs, 27, 300));
	cJSON_AddNumberToObject(root, "freeCitiesPArmy", gs_get_tt(gs, 28, 300));
	cJSON_AddNumberToObject(root, "leazasEArmy", gs_get_tt(gs, 29, 500));
	cJSON_AddNumberToObject(root, "helmanEArmy", gs_get_tt(gs, 30, 500));
	cJSON_AddNumberToObject(root, "zethEArmy", gs_get_tt(gs, 31, 500));
	cJSON_AddNumberToObject(root, "freeCitiesEArmy", gs_get_tt(gs, 32, 500));
	cJSON_AddNumberToObject(root, "subRemnant", gs_get_tt(gs, 37, 0));
	cJSON_AddNumberToObject(root, "subDwarf", gs_get_tt(gs, 38, 0));
	cJSON_AddNumberToObject(root, "subGeneral", gs_get_tt(gs, 39, 0));
	cJSON_AddNumberToObject(root, "subHanny", gs_get_tt(gs, 40, 0));

	cJSON *c_state = cJSON_CreateArray();
	cJSON *c_parmy = cJSON_CreateArray();
	cJSON *c_earmy = cJSON_CreateArray();
	cJSON *c_sub = cJSON_CreateArray();
	for (int i = 0; i < 4; i++) {
		cJSON_AddItemToArray(c_state, cJSON_CreateNumber(gs_get_tt(gs, 13 + i, 3)));
		cJSON_AddItemToArray(c_parmy, cJSON_CreateNumber(gs_get_tt(gs, 25 + i, 300)));
		cJSON_AddItemToArray(c_earmy, cJSON_CreateNumber(gs_get_tt(gs, 29 + i, 500)));
		cJSON_AddItemToArray(c_sub, cJSON_CreateNumber(gs_get_tt(gs, 37 + i, 0)));
	}
	cJSON_AddItemToObject(root, "countryState", c_state);
	cJSON_AddItemToObject(root, "countryPlayerArmy", c_parmy);
	cJSON_AddItemToObject(root, "countryEnemyArmy", c_earmy);
	cJSON_AddItemToObject(root, "subArmy", c_sub);

	int card_count = gs_process_all_cards(gs, 0, 0, 0, 0);
	int char_count = gs_process_all_characters(gs, 0, 0);
	cJSON_AddNumberToObject(root, "cardCount", card_count);
	cJSON_AddNumberToObject(root, "charCount", char_count);

	gsave_free(gs);
	char *out = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	return out;
}

static bool write_local_save_mod_str(const char *path, const char *mod_json_str)
{
	sys4_set_text_gbk(true);
	cJSON *mod = cJSON_Parse(mod_json_str);
	if (!mod) return false;

	enum savefile_error serr;
	struct gsave *gs = gsave_read(path, &serr);
	if (!gs) {
		cJSON_Delete(mod);
		return false;
	}

	struct gsave_global *g_gc = gs_find_global(gs, "g_gameContext", NULL);
	int32_t gc_rec = g_gc ? g_gc->value : -1;
	json_write_int_if_present(mod, "turn", gs, gc_rec, "m_turn", NULL);
	json_write_int_if_present(mod, "chapter", gs, gc_rec, "m_chapter", NULL);

	cJSON *j_diff = cJSON_GetObjectItem(mod, "difficulty");
	if (j_diff && cJSON_IsNumber(j_diff)) {
		struct gsave_global *g_diff = gs_find_global(gs, GBK_DIFFICULTY, SJIS_DIFFICULTY);
		if (g_diff) g_diff->value = j_diff->valueint;
	}
	cJSON *j_bc = cJSON_GetObjectItem(mod, "battleCount");
	if (j_bc && cJSON_IsNumber(j_bc)) {
		struct gsave_global *g_bc = gs_find_global(gs, GBK_BATTLE_COUNT, SJIS_BATTLE_COUNT);
		if (g_bc) g_bc->value = j_bc->valueint;
	}

	struct gsave_global *g_pcp = gs_find_global(gs, "g_playerCommonParam", NULL);
	int32_t pcp_rec = g_pcp ? g_pcp->value : -1;
	json_write_int_if_present(mod, "foodTicket", gs, pcp_rec, "m_foodTicket", NULL);
	json_write_int_if_present(mod, "honor", gs, pcp_rec, "m_honor", NULL);
	json_write_int_if_present(mod, "friendPoint", gs, pcp_rec, "m_friendPoint", NULL);
	json_write_int_if_present(mod, "totalExp", gs, pcp_rec, "<TotalExp>", NULL);
	json_write_int_if_present(mod, "partyRank", gs, pcp_rec, "<PartyRank>", NULL);
	json_write_int_if_present(mod, "treasureNotGetCount", gs, pcp_rec, "<TreasureNotGetCount>", NULL);
	json_write_int_if_present(mod, "itemStar", gs, pcp_rec, "<ItemStar>", NULL);
	json_write_int_if_present(mod, "isUsedHannyZippo", gs, pcp_rec, "<IsUsedHannyZippo>", NULL);

	struct gsave_global *g_pb = gs_find_global(gs, "g_partyBonus", NULL);
	int32_t pb_rec = g_pb ? g_pb->value : -1;
	json_write_int_if_present(mod, "partyBonusMaxPoint", gs, pb_rec, "m_maxPoint", NULL);

	struct gsave_global *g_party = gs_find_global(gs, "g_party", NULL);
	int32_t party_rec = g_party ? g_party->value : -1;
	int32_t ps_rec = gs_get_field_int(gs, party_rec, "m_status", NULL, -1);
	int32_t cs_rec = gs_get_field_int(gs, ps_rec, "m_status", NULL, -1);
	json_write_int_if_present(mod, "partyHp", gs, party_rec, "m_hp", NULL);
	json_write_int_if_present(mod, "partyMaxHp", gs, ps_rec, "m_totalHp", NULL);
	json_write_int_if_present(mod, "partyAvailCount", gs, party_rec, "m_availableCount", NULL);
	json_write_int_if_present(mod, "partyIsFixStatus", gs, party_rec, "<IsFixStatus>", NULL);
	{
		int32_t cur_hp = gs_get_field_int(gs, party_rec, "m_hp", NULL, 1);
		int32_t max_hp = gs_get_field_int(gs, ps_rec, "m_totalHp", NULL, 1);
		if (max_hp > 0) {
			union { float f; int32_t i; } u;
			u.f = (float)cur_hp / (float)max_hp;
			if (u.f > 1.0f) u.f = 1.0f;
			gs_set_field_int(gs, party_rec, "m_hpRatio", NULL, u.i);
		}
	}
	cJSON *it;
	if ((it = cJSON_GetObjectItem(mod, "allStatusPer")) && cJSON_IsNumber(it) && cs_rec >= 0) {
		int32_t v = it->valueint;
		gs_set_field_int(gs, cs_rec, "m_AtkPerPhysical", NULL, v);
		gs_set_field_int(gs, cs_rec, "m_AtkPerMagical", NULL, v);
		gs_set_field_int(gs, cs_rec, "m_DefPerPhysical", NULL, v);
		gs_set_field_int(gs, cs_rec, "m_DefPerMagical", NULL, v);
		gs_set_field_int(gs, cs_rec, "m_PerAvoidance", NULL, v);
		gs_set_field_int(gs, cs_rec, "m_DecPerBadCondition", NULL, v);
		gs_set_field_int(gs, cs_rec, "m_perAutoBattleAtk", NULL, v);
		gs_set_field_int(gs, cs_rec, "m_PerCritical", NULL, v);
		gs_set_field_int(gs, cs_rec, "m_PerMultiAttack", NULL, v);
		gs_set_field_int(gs, cs_rec, "m_PerExpUp", NULL, v);
		gs_set_field_int(gs, cs_rec, "m_PerHpUp", NULL, v);
		gs_set_field_int(gs, cs_rec, "m_PerAutoHeal", NULL, v);
		gs_set_field_int(gs, cs_rec, "m_PerFindTreasure", NULL, v);
		gs_set_field_int(gs, cs_rec, "m_PerInjection", NULL, v);
		gs_set_field_int(gs, cs_rec, "m_AddPerHit", NULL, v);
		gs_set_field_int(gs, cs_rec, "StatusMaxPer", NULL, v);
	}
	json_write_int_if_present(mod, "partyAtk", gs, cs_rec, "<Atk>", NULL);
	json_write_int_if_present(mod, "atkPerPhys", gs, cs_rec, "m_AtkPerPhysical", NULL);
	json_write_int_if_present(mod, "atkPerMag", gs, cs_rec, "m_AtkPerMagical", NULL);
	json_write_int_if_present(mod, "defPerPhys", gs, cs_rec, "m_DefPerPhysical", NULL);
	json_write_int_if_present(mod, "defPerMag", gs, cs_rec, "m_DefPerMagical", NULL);
	json_write_int_if_present(mod, "perAvoid", gs, cs_rec, "m_PerAvoidance", NULL);
	json_write_int_if_present(mod, "decBadCond", gs, cs_rec, "m_DecPerBadCondition", NULL);
	json_write_int_if_present(mod, "perAutoAtk", gs, cs_rec, "m_perAutoBattleAtk", NULL);
	json_write_int_if_present(mod, "perCrit", gs, cs_rec, "m_PerCritical", NULL);
	json_write_int_if_present(mod, "perMulti", gs, cs_rec, "m_PerMultiAttack", NULL);
	json_write_int_if_present(mod, "perExpUp", gs, cs_rec, "m_PerExpUp", NULL);
	json_write_int_if_present(mod, "perHpUp", gs, cs_rec, "m_PerHpUp", NULL);
	json_write_int_if_present(mod, "perAutoHeal", gs, cs_rec, "m_PerAutoHeal", NULL);
	json_write_int_if_present(mod, "perFindTreasure", gs, cs_rec, "m_PerFindTreasure", NULL);
	json_write_int_if_present(mod, "perInjection", gs, cs_rec, "m_PerInjection", NULL);
	json_write_int_if_present(mod, "addPerHit", gs, cs_rec, "m_AddPerHit", NULL);
	json_write_int_if_present(mod, "statusMaxPer", gs, cs_rec, "StatusMaxPer", NULL);
	if ((it = cJSON_GetObjectItem(mod, "humanCount")) && cJSON_IsNumber(it)) gs_set_tt(gs, 33, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "humanDead")) && cJSON_IsNumber(it)) {
		gs_set_tt(gs, 34, it->valueint);
		gs_set_field_int(gs, pcp_rec, "m_lastHumanDead", NULL, it->valueint);
	}
	if ((it = cJSON_GetObjectItem(mod, "humanAlivePer")) && cJSON_IsNumber(it)) {
		gs_set_tt(gs, 35, it->valueint);
		gs_set_field_int(gs, pcp_rec, "m_lastHumanAlivePer", NULL, it->valueint);
	}
	if ((it = cJSON_GetObjectItem(mod, "humanDeadPer")) && cJSON_IsNumber(it)) gs_set_tt(gs, 36, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "curseTurn")) && cJSON_IsNumber(it)) gs_set_tt(gs, 41, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "rareLuck")) && cJSON_IsNumber(it)) gs_set_tt(gs, 42, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "hannyZippo")) && cJSON_IsNumber(it)) gs_set_tt(gs, 101, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "seaPlayerArmy")) && cJSON_IsNumber(it)) gs_set_tt(gs, 66, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "seaEnemyArmy")) && cJSON_IsNumber(it)) gs_set_tt(gs, 68, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "leazasState")) && cJSON_IsNumber(it)) gs_set_tt(gs, 13, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "helmanState")) && cJSON_IsNumber(it)) gs_set_tt(gs, 14, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "zethState")) && cJSON_IsNumber(it)) gs_set_tt(gs, 15, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "freeCitiesState")) && cJSON_IsNumber(it)) gs_set_tt(gs, 16, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "leazasPArmy")) && cJSON_IsNumber(it)) gs_set_tt(gs, 25, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "helmanPArmy")) && cJSON_IsNumber(it)) gs_set_tt(gs, 26, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "zethPArmy")) && cJSON_IsNumber(it)) gs_set_tt(gs, 27, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "freeCitiesPArmy")) && cJSON_IsNumber(it)) gs_set_tt(gs, 28, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "leazasEArmy")) && cJSON_IsNumber(it)) gs_set_tt(gs, 29, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "helmanEArmy")) && cJSON_IsNumber(it)) gs_set_tt(gs, 30, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "zethEArmy")) && cJSON_IsNumber(it)) gs_set_tt(gs, 31, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "freeCitiesEArmy")) && cJSON_IsNumber(it)) gs_set_tt(gs, 32, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "subRemnant")) && cJSON_IsNumber(it)) gs_set_tt(gs, 37, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "subDwarf")) && cJSON_IsNumber(it)) gs_set_tt(gs, 38, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "subGeneral")) && cJSON_IsNumber(it)) gs_set_tt(gs, 39, it->valueint);
	if ((it = cJSON_GetObjectItem(mod, "subHanny")) && cJSON_IsNumber(it)) gs_set_tt(gs, 40, it->valueint);

	cJSON *arr;
	if ((arr = cJSON_GetObjectItem(mod, "countryState")) && cJSON_IsArray(arr)) {
		for (int i = 0; i < 4 && i < cJSON_GetArraySize(arr); i++)
			gs_set_tt(gs, 13 + i, cJSON_GetArrayItem(arr, i)->valueint);
	}
	if ((arr = cJSON_GetObjectItem(mod, "countryPlayerArmy")) && cJSON_IsArray(arr)) {
		for (int i = 0; i < 4 && i < cJSON_GetArraySize(arr); i++)
			gs_set_tt(gs, 25 + i, cJSON_GetArrayItem(arr, i)->valueint);
	}
	if ((arr = cJSON_GetObjectItem(mod, "countryEnemyArmy")) && cJSON_IsArray(arr)) {
		for (int i = 0; i < 4 && i < cJSON_GetArraySize(arr); i++)
			gs_set_tt(gs, 29 + i, cJSON_GetArrayItem(arr, i)->valueint);
	}
	if ((arr = cJSON_GetObjectItem(mod, "subArmy")) && cJSON_IsArray(arr)) {
		for (int i = 0; i < 4 && i < cJSON_GetArraySize(arr); i++)
			gs_set_tt(gs, 37 + i, cJSON_GetArrayItem(arr, i)->valueint);
	}

	int set_card_count = (it = cJSON_GetObjectItem(mod, "setCardCount")) && cJSON_IsNumber(it) ? it->valueint : 0;
	int add_card_count = (it = cJSON_GetObjectItem(mod, "addCardCount")) && cJSON_IsNumber(it) ? it->valueint : 0;
	int card_atk_bonus = (it = cJSON_GetObjectItem(mod, "cardAtkBonus")) && cJSON_IsNumber(it) ? it->valueint : 0;
	int card_hp_bonus  = (it = cJSON_GetObjectItem(mod, "cardHpBonus")) && cJSON_IsNumber(it) ? it->valueint : 0;
	if (set_card_count > 0 || add_card_count > 0 || card_atk_bonus > 0 || card_hp_bonus > 0) {
		gs_process_all_cards(gs, set_card_count, add_card_count, card_atk_bonus, card_hp_bonus);
	}

	int set_char_star = (it = cJSON_GetObjectItem(mod, "setCharStar")) && cJSON_IsNumber(it) ? it->valueint : 0;
	int set_char_exp  = (it = cJSON_GetObjectItem(mod, "setCharExp")) && cJSON_IsNumber(it) ? it->valueint : 0;
	if (set_char_star > 0 || set_char_exp > 0) {
		gs_process_all_characters(gs, set_char_star, set_char_exp);
	}

	char *tmp_path = NULL;
	FILE *fp = atomic_save_open(path, "wb", &tmp_path);
	bool ok = false;
	if (fp) {
		if (gsave_write(gs, fp, false, 1) == SAVEFILE_SUCCESS)
			ok = atomic_save_commit(fp, path, tmp_path, false);
		else
			atomic_save_abort(fp, tmp_path);
	}
	gsave_free(gs);
	cJSON_Delete(mod);
	return ok;
}

enum csd_value_type {
	CSD_INT    = 1,
	CSD_FLOAT  = 2,
	CSD_STRING = 3,
	CSD_BOOL   = 4,
};

struct csd_entry {
	char *key;
	int32_t type;
	int32_t i_val;
	float f_val;
	char *s_val;
};

struct csd_table {
	struct csd_entry *entries;
	size_t count;
	size_t cap;
};

struct csd_buf {
	uint8_t *data;
	size_t len;
	size_t cap;
};

static const struct {
	const char *key;
	bool default_bool;
} s_csd_r10_keys[] = {
	{ "totalClearPoint",    false },
	{ "clearBPointMax",     false },
	{ "TotalClearCount",    false },
	{ "Clear_A_Count",      false },
	{ "Clear_B_Count",      false },
	{ "Clear_C_Count",      false },
	{ "AutoBattleMedal",    false },
	{ "AchievementPoint",   false },
	{ "BestScore",          false },
	{ "LeaderSmallestTurn", false },
	{ "RealSmallestTurn",   false },
	{ "CollectionClearB",   true  },
	{ "OpenMonsterpedia",   true  },
	{ "TrophyComplete",     true  },
	{ "CardComplete",       true  },
};

#define NR_CSD_R10_KEYS (sizeof(s_csd_r10_keys) / sizeof(s_csd_r10_keys[0]))

static uint32_t csd_read_u32_le(const uint8_t *p)
{
	return (uint32_t)p[0] |
	       ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static int32_t csd_read_i32_le(const uint8_t *p)
{
	uint32_t u = csd_read_u32_le(p);
	int32_t v;
	memcpy(&v, &u, sizeof(v));
	return v;
}

static float csd_read_f32_le(const uint8_t *p)
{
	uint32_t u = csd_read_u32_le(p);
	float f;
	memcpy(&f, &u, sizeof(f));
	return f;
}

static bool csd_buf_append(struct csd_buf *b, const void *src, size_t n)
{
	if (n == 0)
		return true;
	if (b->len + n > b->cap) {
		size_t new_cap = b->cap ? b->cap * 2 : 256;
		while (new_cap < b->len + n)
			new_cap *= 2;
		uint8_t *nd = realloc(b->data, new_cap);
		if (!nd)
			return false;
		b->data = nd;
		b->cap = new_cap;
	}
	memcpy(b->data + b->len, src, n);
	b->len += n;
	return true;
}

static bool csd_buf_append_u32_le(struct csd_buf *b, uint32_t u)
{
	uint8_t tmp[4] = {
		(uint8_t)(u & 0xFFu),
		(uint8_t)((u >> 8) & 0xFFu),
		(uint8_t)((u >> 16) & 0xFFu),
		(uint8_t)((u >> 24) & 0xFFu),
	};
	return csd_buf_append(b, tmp, 4);
}

static bool csd_buf_append_i32_le(struct csd_buf *b, int32_t v)
{
	uint32_t u;
	memcpy(&u, &v, sizeof(u));
	return csd_buf_append_u32_le(b, u);
}

static bool csd_buf_append_f32_le(struct csd_buf *b, float f)
{
	uint32_t u;
	memcpy(&u, &f, sizeof(u));
	return csd_buf_append_u32_le(b, u);
}

static void csd_table_free(struct csd_table *tbl)
{
	if (!tbl)
		return;
	for (size_t i = 0; i < tbl->count; i++) {
		free(tbl->entries[i].key);
		free(tbl->entries[i].s_val);
	}
	free(tbl->entries);
	tbl->entries = NULL;
	tbl->count = 0;
	tbl->cap = 0;
}

static struct csd_entry *csd_table_find(struct csd_table *tbl, const char *key)
{
	if (!tbl || !key)
		return NULL;
	for (size_t i = 0; i < tbl->count; i++) {
		if (tbl->entries[i].key && strcmp(tbl->entries[i].key, key) == 0)
			return &tbl->entries[i];
	}
	return NULL;
}

static bool csd_table_set_int_or_bool(struct csd_table *tbl, const char *key, int32_t val, bool default_bool)
{
	if (default_bool && val != 0 && val != 1)
		return false;
	struct csd_entry *existing = csd_table_find(tbl, key);
	if (existing) {
		free(existing->s_val);
		existing->s_val = NULL;
		existing->f_val = 0.0f;
		if (default_bool || (existing->type == CSD_BOOL && (val == 0 || val == 1))) {
			existing->type = CSD_BOOL;
			existing->i_val = val ? 1 : 0;
		} else {
			existing->type = CSD_INT;
			existing->i_val = val;
		}
		return true;
	}
	if (tbl->count == tbl->cap) {
		size_t new_cap = tbl->cap ? tbl->cap * 2 : 32;
		struct csd_entry *ne = realloc(tbl->entries, new_cap * sizeof(struct csd_entry));
		if (!ne)
			return false;
		tbl->entries = ne;
		tbl->cap = new_cap;
	}
	char *kcopy = strdup(key);
	if (!kcopy)
		return false;
	struct csd_entry *e = &tbl->entries[tbl->count++];
	memset(e, 0, sizeof(*e));
	e->key = kcopy;
	if (default_bool) {
		e->type = CSD_BOOL;
		e->i_val = val ? 1 : 0;
	} else {
		e->type = CSD_INT;
		e->i_val = val;
	}
	return true;
}

static int csd_entry_cmp(const void *a, const void *b)
{
	const struct csd_entry *ea = (const struct csd_entry *)a;
	const struct csd_entry *eb = (const struct csd_entry *)b;
	return strcmp(ea->key ? ea->key : "", eb->key ? eb->key : "");
}

static bool csd_table_load_file(const char *path, struct csd_table *tbl)
{
	memset(tbl, 0, sizeof(*tbl));
	if (!path || !path[0] || !file_exists(path))
		return false;
	size_t fsize = 0;
	uint8_t *fbuf = (uint8_t *)file_read(path, &fsize);
	if (!fbuf)
		return false;
	if (fsize < 16 || memcmp(fbuf, "CSD\0", 4) != 0) {
		free(fbuf);
		return false;
	}
	uint32_t version = csd_read_u32_le(fbuf + 4);
	uint32_t raw_size = csd_read_u32_le(fbuf + 8);
	uint32_t comp_size = csd_read_u32_le(fbuf + 12);
	if (version != 0 || (size_t)comp_size > fsize - 16u || raw_size > 64u * 1024u * 1024u) {
		free(fbuf);
		return false;
	}
	if (raw_size == 0) {
		free(fbuf);
		return true;
	}
	uint8_t *raw_buf = malloc(raw_size);
	if (!raw_buf) {
		free(fbuf);
		return false;
	}
	uLongf dest_len = (uLongf)raw_size;
	int zrc = uncompress(raw_buf, &dest_len, fbuf + 16, (uLong)comp_size);
	free(fbuf);
	if (zrc != Z_OK || dest_len != (uLongf)raw_size || raw_size < 4) {
		free(raw_buf);
		return false;
	}

	int32_t count = csd_read_i32_le(raw_buf);
	if (count < 0 || (size_t)count > raw_size / 6u) {
		free(raw_buf);
		return false;
	}
	if (count > 0) {
		tbl->entries = calloc((size_t)count, sizeof(struct csd_entry));
		if (!tbl->entries) {
			free(raw_buf);
			return false;
		}
		tbl->cap = (size_t)count;
	}

	size_t pos = 4;
	for (int32_t idx = 0; idx < count; idx++) {
		if (pos >= raw_size) {
			csd_table_free(tbl);
			free(raw_buf);
			return false;
		}
		const uint8_t *knul = memchr(raw_buf + pos, 0, raw_size - pos);
		if (!knul) {
			csd_table_free(tbl);
			free(raw_buf);
			return false;
		}
		size_t klen = (size_t)(knul - (raw_buf + pos));
		char *key = malloc(klen + 1);
		if (!key) {
			csd_table_free(tbl);
			free(raw_buf);
			return false;
		}
		memcpy(key, raw_buf + pos, klen + 1);
		pos += klen + 1;

		if (pos + 4 > raw_size) {
			free(key);
			csd_table_free(tbl);
			free(raw_buf);
			return false;
		}
		int32_t type = csd_read_i32_le(raw_buf + pos);
		pos += 4;

		struct csd_entry *e = &tbl->entries[tbl->count];
		e->key = key;
		e->type = type;

		switch (type) {
		case CSD_INT:
			if (pos + 4 > raw_size) {
				tbl->count++;
				csd_table_free(tbl);
				free(raw_buf);
				return false;
			}
			e->i_val = csd_read_i32_le(raw_buf + pos);
			pos += 4;
			break;
		case CSD_FLOAT:
			if (pos + 4 > raw_size) {
				tbl->count++;
				csd_table_free(tbl);
				free(raw_buf);
				return false;
			}
			e->f_val = csd_read_f32_le(raw_buf + pos);
			pos += 4;
			break;
		case CSD_STRING: {
			const uint8_t *snul = memchr(raw_buf + pos, 0, raw_size - pos);
			if (!snul) {
				tbl->count++;
				csd_table_free(tbl);
				free(raw_buf);
				return false;
			}
			size_t slen = (size_t)(snul - (raw_buf + pos));
			e->s_val = malloc(slen + 1);
			if (!e->s_val) {
				tbl->count++;
				csd_table_free(tbl);
				free(raw_buf);
				return false;
			}
			memcpy(e->s_val, raw_buf + pos, slen + 1);
			pos += slen + 1;
			break;
		}
		case CSD_BOOL:
			if (pos + 4 > raw_size) {
				tbl->count++;
				csd_table_free(tbl);
				free(raw_buf);
				return false;
			}
			e->i_val = csd_read_i32_le(raw_buf + pos) != 0 ? 1 : 0;
			pos += 4;
			break;
		default:
			tbl->count++;
			csd_table_free(tbl);
			free(raw_buf);
			return false;
		}
		tbl->count++;
	}

	free(raw_buf);
	return true;
}

static bool csd_table_save_file(const char *path, struct csd_table *tbl)
{
	if (!path || !path[0] || !tbl)
		return false;
	if (tbl->count > 1)
		qsort(tbl->entries, tbl->count, sizeof(struct csd_entry), csd_entry_cmp);

	struct csd_buf raw = { NULL, 0, 0 };
	uint8_t *comp_buf = NULL;
	uint32_t raw_size = 0;
	uint32_t comp_size = 0;

	if (tbl->count > 0) {
		if (!csd_buf_append_i32_le(&raw, (int32_t)tbl->count))
			goto fail;
		for (size_t i = 0; i < tbl->count; i++) {
			const struct csd_entry *e = &tbl->entries[i];
			const char *k = e->key ? e->key : "";
			if (!csd_buf_append(&raw, k, strlen(k) + 1))
				goto fail;
			if (!csd_buf_append_i32_le(&raw, e->type))
				goto fail;
			switch (e->type) {
			case CSD_INT:
				if (!csd_buf_append_i32_le(&raw, e->i_val))
					goto fail;
				break;
			case CSD_FLOAT:
				if (!csd_buf_append_f32_le(&raw, e->f_val))
					goto fail;
				break;
			case CSD_STRING: {
				const char *sv = e->s_val ? e->s_val : "";
				if (!csd_buf_append(&raw, sv, strlen(sv) + 1))
					goto fail;
				break;
			}
			case CSD_BOOL:
				if (!csd_buf_append_i32_le(&raw, e->i_val != 0 ? 1 : 0))
					goto fail;
				break;
			default:
				goto fail;
			}
		}
		raw_size = (uint32_t)raw.len;
		uLongf bound = compressBound((uLong)raw.len);
		comp_buf = malloc((size_t)bound);
		if (!comp_buf)
			goto fail;
		uLongf dest_len = bound;
		if (compress2(comp_buf, &dest_len, raw.data, (uLong)raw.len, Z_DEFAULT_COMPRESSION) != Z_OK)
			goto fail;
		comp_size = (uint32_t)dest_len;
	}

	uint8_t hdr[16];
	hdr[0] = 'C';
	hdr[1] = 'S';
	hdr[2] = 'D';
	hdr[3] = '\0';
	hdr[4] = 0; hdr[5] = 0; hdr[6] = 0; hdr[7] = 0;
	hdr[8]  = (uint8_t)(raw_size & 0xFFu);
	hdr[9]  = (uint8_t)((raw_size >> 8) & 0xFFu);
	hdr[10] = (uint8_t)((raw_size >> 16) & 0xFFu);
	hdr[11] = (uint8_t)((raw_size >> 24) & 0xFFu);
	hdr[12] = (uint8_t)(comp_size & 0xFFu);
	hdr[13] = (uint8_t)((comp_size >> 8) & 0xFFu);
	hdr[14] = (uint8_t)((comp_size >> 16) & 0xFFu);
	hdr[15] = (uint8_t)((comp_size >> 24) & 0xFFu);

	char *tmp_path = NULL;
	FILE *fp = atomic_save_open(path, "wb", &tmp_path);
	bool ok = false;
	if (fp) {
		bool wrote = (fwrite(hdr, 1, 16, fp) == 16) &&
			(comp_size == 0 || fwrite(comp_buf, 1, comp_size, fp) == comp_size);
		if (wrote)
			ok = atomic_save_commit(fp, path, tmp_path, true);
		else
			atomic_save_abort(fp, tmp_path);
	}
	free(comp_buf);
	free(raw.data);
	return ok;

fail:
	free(comp_buf);
	free(raw.data);
	return false;
}

static char *csd_join_path(const char *dir, size_t dir_len, const char *rel)
{
	size_t rlen = strlen(rel);
	char *out = malloc(dir_len + 1 + rlen + 1);
	if (!out)
		return NULL;
	if (dir_len > 0) {
		memcpy(out, dir, dir_len);
		out[dir_len] = '/';
		memcpy(out + dir_len + 1, rel, rlen + 1);
	} else {
		memcpy(out, rel, rlen + 1);
	}
	return out;
}

static char *csd_resolve_path(const char *global_path, const char *collection_path, bool for_write)
{
	const char *anchor = (global_path && global_path[0]) ? global_path :
		((collection_path && collection_path[0]) ? collection_path : NULL);
	if (!anchor)
		return NULL;

	const char *slash = strrchr(anchor, '/');
	const char *bslash = strrchr(anchor, '\\');
	if (bslash && (!slash || bslash > slash))
		slash = bslash;
	size_t dir_len = slash ? (size_t)(slash - anchor) : 0;

	char *cand1 = csd_join_path(anchor, dir_len, "CommonSystemData.sav");
	if (cand1 && file_exists(cand1))
		return cand1;

	char *cand2 = csd_join_path(anchor, dir_len, "SaveData/CommonSystemData.sav");
	if (cand2 && file_exists(cand2)) {
		free(cand1);
		return cand2;
	}
	free(cand2);

	/* Also check parent directory if anchor is inside .../SaveData/... */
	if (dir_len >= 8) {
		const char *tail = anchor + dir_len - 8;
		if ((dir_len == 8 || tail[-1] == '/' || tail[-1] == '\\') &&
		    strncasecmp(tail, "SaveData", 8) == 0) {
			size_t p_len = (dir_len > 8) ? (dir_len - 9) : 0;
			char *cand3 = csd_join_path(anchor, p_len, "CommonSystemData.sav");
			if (cand3 && file_exists(cand3)) {
				free(cand1);
				return cand3;
			}
			free(cand3);
		}
	}

	if (for_write)
		return cand1;
	free(cand1);
	return NULL;
}

static char *read_global_save_summary_str(const char *global_path, const char *collection_path)
{
	sys4_set_text_gbk(true);
	cJSON *root = cJSON_CreateObject();
	enum savefile_error serr;
	bool has_global = false;
	int32_t game_play_count = 0;

	struct gsave *gs_g = (global_path && global_path[0] && file_exists(global_path))
		? gsave_read(global_path, &serr) : NULL;
	if (gs_g) {
		has_global = true;
		cJSON_AddBoolToObject(root, "hasGlobal", true);
		struct gsave_global *g_gc = gs_find_global(gs_g, "g_globalContext", NULL);
		int32_t gc_rec = g_gc ? g_gc->value : -1;
		game_play_count = gs_get_field_int(gs_g, gc_rec, "m_gamePlayCount", NULL, 0);
		cJSON_AddNumberToObject(root, "gamePlayCount", game_play_count);
		cJSON_AddNumberToObject(root, "itemStarRank", gs_get_field_int(gs_g, gc_rec, "m_itemStarRank", NULL, 0));
		struct gsave_flat_array *fa_cards = gs_get_flat_array(gs_g, gs_get_field_int(gs_g, gc_rec, "m_cardIds", NULL, -1));
		struct gsave_flat_array *fa_ranks = gs_get_flat_array(gs_g, gs_get_field_int(gs_g, gc_rec, "m_characterRank", NULL, -1));
		struct gsave_flat_array *fa_info  = gs_get_flat_array(gs_g, gs_get_field_int(gs_g, gc_rec, "m_cardInfo", NULL, -1));
		cJSON_AddNumberToObject(root, "unlockedCardCount", fa_cards ? fa_cards->nr_values : 0);
		cJSON_AddNumberToObject(root, "charRankCount", fa_ranks ? fa_ranks->nr_values : 0);
		cJSON_AddNumberToObject(root, "takeOverCardCount", fa_info ? fa_info->nr_values : 0);
		gsave_free(gs_g);
	} else {
		cJSON_AddBoolToObject(root, "hasGlobal", false);
	}

	struct gsave *gs_c = (collection_path && collection_path[0] && file_exists(collection_path))
		? gsave_read(collection_path, &serr) : NULL;
	if (gs_c) {
		cJSON_AddBoolToObject(root, "hasCollection", true);
		struct gsave_global *g_ct = gs_find_global(gs_c, "g_completedTrophy", NULL);
		int32_t ct_rec = g_ct ? g_ct->value : -1;
		struct gsave_flat_array *fa_tr = gs_get_flat_array(gs_c, gs_get_field_int(gs_c, ct_rec, "m_trophyInfo", NULL, -1));
		int valid_trophies = 0;
		if (fa_tr) {
			for (int i = 0; i < fa_tr->nr_values; i++) {
				int32_t tr_rec = fa_tr->values[i].value;
				int32_t s_idx = gs_get_field_int(gs_c, tr_rec, "Id", "<Id>", GSAVE7_EMPTY_STRING);
				if (s_idx != GSAVE7_EMPTY_STRING && s_idx >= 0 && s_idx < gs_c->nr_strings && gs_c->strings[s_idx]->size > 0)
					valid_trophies++;
			}
		}
		cJSON_AddNumberToObject(root, "trophyCount", valid_trophies);
		union { int32_t i; float f; } u;
		u.i = gs_get_field_int(gs_c, ct_rec, "<LeaderAtkRatio>", NULL, 0);
		cJSON_AddNumberToObject(root, "leaderAtkRatio", u.f);
		int32_t cs_rec = gs_get_field_int(gs_c, ct_rec, "m_status", NULL, -1);
		cJSON_AddNumberToObject(root, "trophyExpBonus", gs_get_field_int(gs_c, cs_rec, "m_PerExpUp", NULL, 0));
		cJSON_AddNumberToObject(root, "trophyStatusMaxPer", gs_get_field_int(gs_c, cs_rec, "StatusMaxPer", NULL, 0));
		gsave_free(gs_c);
	} else {
		cJSON_AddBoolToObject(root, "hasCollection", false);
	}

	struct csd_table csd_tbl;
	memset(&csd_tbl, 0, sizeof(csd_tbl));
	char *csd_path = csd_resolve_path(global_path, collection_path, false);
	bool has_csd = csd_path && csd_table_load_file(csd_path, &csd_tbl);
	free(csd_path);
	cJSON_AddBoolToObject(root, "hasCommonSystemData", has_csd);

	for (size_t k = 0; k < NR_CSD_R10_KEYS; k++) {
		const char *kname = s_csd_r10_keys[k].key;
		struct csd_entry *e = has_csd ? csd_table_find(&csd_tbl, kname) : NULL;
		int32_t val = 0;
		if (e && (e->type == CSD_INT || e->type == CSD_BOOL)) {
			val = e->i_val;
		} else if (has_global && strcmp(kname, "TotalClearCount") == 0) {
			val = game_play_count;
		}
		cJSON_AddNumberToObject(root, kname, val);
	}
	csd_table_free(&csd_tbl);

	char *out = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	return out;
}

static bool json_get_int_or_bool_item(cJSON *obj, const char *key, int32_t *out_val)
{
	cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
	if (!it)
		return false;
	if (cJSON_IsNumber(it)) {
		*out_val = it->valueint;
		return true;
	}
	if (cJSON_IsBool(it)) {
		*out_val = cJSON_IsTrue(it) ? 1 : 0;
		return true;
	}
	return false;
}

static bool write_global_save_mod_str(const char *global_path, const char *collection_path, const char *mod_json_str)
{
	sys4_set_text_gbk(true);
	cJSON *mod = cJSON_Parse(mod_json_str);
	if (!mod) return false;
	bool all_ok = true;
	enum savefile_error serr;
	cJSON *rank = cJSON_GetObjectItemCaseSensitive(mod, "setAllCharRank");
	if (cJSON_IsNumber(rank) && rank->valueint < 0)
		cJSON_DeleteItemFromObjectCaseSensitive(mod, "setAllCharRank");
	cJSON *cards = cJSON_GetObjectItemCaseSensitive(mod, "setTakeOverCardCount");
	if (cJSON_IsNumber(cards) && cards->valueint < 0)
		cJSON_DeleteItemFromObjectCaseSensitive(mod, "setTakeOverCardCount");

	int32_t total_clear_val = 0;
	bool has_total_clear_mod = json_get_int_or_bool_item(mod, "TotalClearCount", &total_clear_val);
	bool has_game_play_mod = cJSON_GetObjectItemCaseSensitive(mod, "gamePlayCount") != NULL;
	bool want_global = has_game_play_mod ||
		(has_total_clear_mod && global_path && global_path[0] && file_exists(global_path)) ||
		cJSON_GetObjectItemCaseSensitive(mod, "itemStarRank") ||
		cJSON_GetObjectItemCaseSensitive(mod, "setAllCharRank") ||
		cJSON_GetObjectItemCaseSensitive(mod, "setTakeOverCardCount");
	bool want_collection = cJSON_GetObjectItemCaseSensitive(mod, "leaderAtkRatio") ||
		cJSON_GetObjectItemCaseSensitive(mod, "trophyExpBonus") ||
		cJSON_GetObjectItemCaseSensitive(mod, "trophyStatusMaxPer");
	bool want_csd_write = false;
	for (size_t k = 0; k < NR_CSD_R10_KEYS; k++) {
		const char *key = s_csd_r10_keys[k].key;
		cJSON *item = cJSON_GetObjectItemCaseSensitive(mod, key);
		if (!item)
			continue;
		if (s_csd_r10_keys[k].default_bool && !cJSON_IsBool(item) &&
		    !(cJSON_IsNumber(item) && (item->valuedouble == 0 || item->valuedouble == 1))) {
			cJSON_Delete(mod);
			return false;
		}
		want_csd_write = true;
	}

	if (want_global) {
		bool file_ok = false;
		struct gsave *gs_g = (global_path && global_path[0] && file_exists(global_path))
			? gsave_read(global_path, &serr) : NULL;
		if (gs_g) {
			bool modified = true;
			struct gsave_global *g_gc = gs_find_global(gs_g, "g_globalContext", NULL);
			int32_t gc_rec = g_gc ? g_gc->value : -1;
			if (has_game_play_mod) {
				modified &= json_write_int_if_present(mod, "gamePlayCount", gs_g, gc_rec, "m_gamePlayCount", NULL);
			} else if (has_total_clear_mod) {
				modified &= gs_set_field_int(gs_g, gc_rec, "m_gamePlayCount", NULL, total_clear_val);
			}
			modified &= json_write_int_if_present(mod, "itemStarRank", gs_g, gc_rec, "m_itemStarRank", NULL);

			cJSON *j_rank = cJSON_GetObjectItemCaseSensitive(mod, "setAllCharRank");
			if (j_rank) {
				struct gsave_flat_array *fa_ranks = gs_get_flat_array(gs_g, gs_get_field_int(gs_g, gc_rec, "m_characterRank", NULL, -1));
				if (cJSON_IsNumber(j_rank) && j_rank->valueint >= 0 && fa_ranks) {
					for (int i = 0; i < fa_ranks->nr_values; i++) {
						int32_t r_rec = fa_ranks->values[i].value;
						modified &= gs_set_field_int(gs_g, r_rec, "rank", "Rank", j_rank->valueint);
						modified &= gs_set_field_int(gs_g, r_rec, "eventStage", "EventStage", j_rank->valueint);
					}
				} else {
					modified = false;
				}
			}
			cJSON *j_to = cJSON_GetObjectItemCaseSensitive(mod, "setTakeOverCardCount");
			if (j_to) {
				struct gsave_flat_array *fa_info = gs_get_flat_array(gs_g, gs_get_field_int(gs_g, gc_rec, "m_cardInfo", NULL, -1));
				if (cJSON_IsNumber(j_to) && j_to->valueint >= 0 && fa_info) {
					for (int i = 0; i < fa_info->nr_values; i++) {
						int32_t i_rec = fa_info->values[i].value;
						modified &= gs_set_field_int(gs_g, i_rec, "m_count", NULL, j_to->valueint);
					}
				} else {
					modified = false;
				}
			}
			char *tmp_path = NULL;
			FILE *fp = modified ? atomic_save_open(global_path, "wb", &tmp_path) : NULL;
			if (fp) {
				if (gsave_write(gs_g, fp, false, 1) == SAVEFILE_SUCCESS) {
					file_ok = atomic_save_commit(fp, global_path, tmp_path, true);
				} else {
					atomic_save_abort(fp, tmp_path);
				}
			}
			gsave_free(gs_g);
		}
		all_ok &= file_ok;
	}

	if (want_collection) {
		bool file_ok = false;
		struct gsave *gs_c = (collection_path && collection_path[0] && file_exists(collection_path))
			? gsave_read(collection_path, &serr) : NULL;
		if (gs_c) {
			bool modified = true;
			struct gsave_global *g_ct = gs_find_global(gs_c, "g_completedTrophy", NULL);
			int32_t ct_rec = g_ct ? g_ct->value : -1;
			cJSON *j_lar = cJSON_GetObjectItemCaseSensitive(mod, "leaderAtkRatio");
			if (j_lar && cJSON_IsNumber(j_lar)) {
				union { float f; int32_t i; } u;
				u.f = (float)j_lar->valuedouble;
				modified &= gs_set_field_int(gs_c, ct_rec, "<LeaderAtkRatio>", NULL, u.i);
			} else if (j_lar) {
				modified = false;
			}
			int32_t cs_rec = gs_get_field_int(gs_c, ct_rec, "m_status", NULL, -1);
			modified &= json_write_int_if_present(mod, "trophyExpBonus", gs_c, cs_rec, "m_PerExpUp", NULL);
			modified &= json_write_int_if_present(mod, "trophyStatusMaxPer", gs_c, cs_rec, "StatusMaxPer", NULL);
			char *tmp_path = NULL;
			FILE *fp = modified ? atomic_save_open(collection_path, "wb", &tmp_path) : NULL;
			if (fp) {
				if (gsave_write(gs_c, fp, false, 1) == SAVEFILE_SUCCESS) {
					file_ok = atomic_save_commit(fp, collection_path, tmp_path, true);
				} else {
					atomic_save_abort(fp, tmp_path);
				}
			}
			gsave_free(gs_c);
		}
		all_ok &= file_ok;
	}

	if (want_csd_write) {
		bool file_ok = false;
		char *csd_path = csd_resolve_path(global_path, collection_path, true);
		if (csd_path) {
			struct csd_table tbl;
			memset(&tbl, 0, sizeof(tbl));
			bool load_ok = !file_exists(csd_path) || csd_table_load_file(csd_path, &tbl);
			if (load_ok) {
				bool set_ok = true;
				for (size_t k = 0; k < NR_CSD_R10_KEYS; k++) {
					int32_t val = 0;
					if (cJSON_GetObjectItemCaseSensitive(mod, s_csd_r10_keys[k].key)) {
						if (!json_get_int_or_bool_item(mod, s_csd_r10_keys[k].key, &val) ||
						    !csd_table_set_int_or_bool(&tbl, s_csd_r10_keys[k].key, val, s_csd_r10_keys[k].default_bool)) {
							set_ok = false;
							break;
						}
					}
				}
				if (set_ok)
					file_ok = csd_table_save_file(csd_path, &tbl);
			}
			csd_table_free(&tbl);
			free(csd_path);
		}
		all_ok &= file_ok;
	}

	cJSON_Delete(mod);
	return (want_global || want_collection || want_csd_write) && all_ok;
}

#ifndef MODIFIER_OFFLINE_ONLY
static int read_game_config_streamer_mode(const char *config_path)
{
	if (!config_path || !config_path[0] || !file_exists(config_path)) return -1;
	bool prev_gbk = sys4_text_is_gbk;
	if (!ain) sys4_set_text_gbk(true);
	enum savefile_error serr;
	struct gsave *gs = gsave_read(config_path, &serr);
	if (!gs) {
		if (!ain) sys4_set_text_gbk(prev_gbk);
		return -1;
	}
	struct gsave_global *g_cfg = gs_find_global(gs, "g_gameConfig", NULL);
	int32_t cfg_rec = g_cfg ? g_cfg->value : -1;
	int32_t val = gs_get_field_int(gs, cfg_rec, "m_isSummaryMode", NULL, -1);
	gsave_free(gs);
	if (!ain) sys4_set_text_gbk(prev_gbk);
	if (val < 0) return -1;
	return val != 0 ? 1 : 0;
}

#endif
#endif

#ifndef MODIFIER_OFFLINE_ONLY

/* =========================================================================
 * Live In-Game VM Memory Helpers (Executed on VM Thread in modifier_on_frame)
 * ========================================================================= */

static bool vm_globals_ready(void)
{
	return ain != NULL && heap != NULL && heap_size > 0 &&
	       heap[0].ref > 0 && heap[0].page != NULL &&
	       heap[0].page->type == GLOBAL_PAGE;
}

static bool vm_is_ready(void)
{
	return vm_globals_ready() && call_stack_ptr > 0;
}

static struct page *vm_safe_get_page(int slot)
{
	if (slot <= 0 || !heap || !page_index_valid(slot))
		return NULL;
	return heap[slot].page;
}

static int vm_find_global_idx(const char *name1, const char *name2)
{
	if (!vm_globals_ready() || !ain->globals) return -1;
	for (int i = 0; i < ain->nr_globals; i++) {
		const char *n1 = ain->globals[i].name;
		const char *n2 = ain->globals[i].name2;
		if (name1 && ((n1 && !strcmp(n1, name1)) || (n2 && !strcmp(n2, name1)))) return i;
		if (name2 && ((n1 && !strcmp(n1, name2)) || (n2 && !strcmp(n2, name2)))) return i;
	}
	return -1;
}

static struct page *vm_get_global_page(const char *name1, const char *name2)
{
	int idx = vm_find_global_idx(name1, name2);
	if (idx < 0 || idx >= heap[0].page->nr_vars) return NULL;
	union vm_value v = global_get(idx);
	return vm_safe_get_page(v.i);
}

static union vm_value *vm_get_struct_member(struct page *page, const char *name1, const char *name2)
{
	if (!ain || !page || page->type != STRUCT_PAGE) return NULL;
	if (page->index < 0 || page->index >= ain->nr_structures) return NULL;
	struct ain_struct *st = &ain->structures[page->index];
	for (int i = 0; i < st->nr_members && i < page->nr_vars; i++) {
		const char *n1 = st->members[i].name;
		const char *n2 = st->members[i].name2;
		if (name1 && ((n1 && !strcmp(n1, name1)) || (n2 && n2[0] && !strcmp(n2, name1))))
			return &page->values[i];
		if (name2 && ((n1 && !strcmp(n1, name2)) || (n2 && n2[0] && !strcmp(n2, name2))))
			return &page->values[i];
	}
	return NULL;
}

static int32_t vm_get_field_int(struct page *page, const char *name1, const char *name2, int32_t def_val)
{
	union vm_value *v = vm_get_struct_member(page, name1, name2);
	return v ? v->i : def_val;
}

static void vm_set_field_int(struct page *page, const char *name1, const char *name2, int32_t val)
{
	union vm_value *v = vm_get_struct_member(page, name1, name2);
	if (v) v->i = val;
}

static struct page *vm_get_field_page(struct page *page, const char *name1, const char *name2)
{
	union vm_value *v = vm_get_struct_member(page, name1, name2);
	if (!v) return NULL;
	return vm_safe_get_page(v->i);
}

static void vm_set_string_slot(int slot, const char *cstr)
{
	if (!cstr || slot <= 0 || !string_index_valid(slot)) return;
	struct string *s = make_string(cstr, strlen(cstr));
	heap_string_assign(slot, s);
	free_string(s);
}

static void vm_set_player_info_name(struct page *gc, const char *gbk_str)
{
	if (!gc || !gbk_str) return;
	struct page *c2p = vm_get_field_page(gc, "PlayerInfo", "m_chapter2PlayerInfo");
	if (!c2p) c2p = vm_get_field_page(gc, "<Chapter2PlayerInfo>", NULL);
	if (c2p && c2p->type == STRUCT_PAGE) {
		union vm_value *nv = vm_get_struct_member(c2p, "m_playerName", "<Name>");
		if (!nv) nv = vm_get_struct_member(c2p, "m_name", NULL);
		if (!nv && c2p->nr_vars > 0) nv = &c2p->values[0];
		if (nv) vm_set_string_slot(nv->i, gbk_str);
	}
}

static int vm_clear_array_slot(int slot)
{
	struct page *old = vm_safe_get_page(slot);
	if (!old || old->type != ARRAY_PAGE || old->nr_vars <= 0)
		return 0;
	int cleared = old->nr_vars;
	struct page *empty = NULL;
	if (ain && ain->version >= 11) {
		empty = alloc_page(ARRAY_PAGE, old->a_type, 0);
		empty->array = old->array;
	}
	for (int i = 0; i < old->nr_vars; i++) {
		variable_fini(old->values[i], variable_type(old, i, NULL, NULL), false);
	}
	free_page(old);
	heap_set_page(slot, empty);
	return cleared;
}

static int vm_clear_kvmap_field(struct page *cache_sub, const char *map_field)
{
	if (!cache_sub || cache_sub->type != STRUCT_PAGE)
		return 0;
	struct page *kvmap = vm_get_field_page(cache_sub, map_field, NULL);
	if (!kvmap || kvmap->type != STRUCT_PAGE)
		return 0;
	union vm_value *m_data = vm_get_struct_member(kvmap, "m_data", NULL);
	if (!m_data)
		return 0;
	return vm_clear_array_slot(m_data->i);
}

#define FNO_FLAG_CARD_CACHE     (1u << 0)
#define FNO_FLAG_CARD_UI        (1u << 1)
#define FNO_FLAG_GAMEPLAY_SCENE (1u << 2)

static struct ain *s_inspected_ain = NULL;
static uint8_t *s_fno_flags = NULL;
static int s_fno_flags_len = 0;
static bool s_is_mod_streamer = false;
static bool s_is_summary_gated_by_adv5 = false;

static int s_fno_run_char_setting = -1;
static int s_fno_scene_char_setting_run = -1;
static int s_fno_scene_stack_join = -1;
static int s_fno_run_info_dialog = -1;
static int s_fno_is_summary_get = -1;
static int s_fno_is_summary_set = -1;
static int s_fno_pcfv_load = -1;
static int s_fno_qmmcv_update = -1;
static int s_fno_cm_set_checkbox = -1;

static int s_struc_pcfv = -1;
static int s_struc_qmmcv = -1;
static int s_struc_common_menu = -1;
static int s_struc_scene_common_menu = -1;
static int s_struc_game_config = -1;

/* Cached hot global indices */
static int s_glob_game_config = -1;
static int s_glob_card_cache = -1;
static int s_glob_game_context = -1;
static int s_glob_pcp = -1;
static int s_glob_party_bonus = -1;
static int s_glob_party = -1;
static int s_glob_battle_ctx = -1;
static int s_glob_enemy = -1;
static int s_glob_tt = -1;
static int s_glob_diff = -1;
static int s_glob_battle_count = -1;
static int s_glob_end_busy = -1;
static int s_glob_end_busy_num = -1;

/* Cached GameConfig & CardCache member offsets */
static int s_mem_cfg_adv_shortcut = -1;
static int s_mem_cfg_summary_mode = -1;
static int s_mem_cc_front = -1;
static int s_mem_front_proc_org = -1;
static int s_card_org_mdata_slot = -1;
static int s_card_org_pair0_slot = -1;

static char *s_rcs_streamer_cheat_gbk = NULL;
static bool s_rcs_streamer_has_tt0_req = false;
static int32_t s_rcs_streamer_req_tt0 = 0;
static int s_rcs_invalid_title_idx = -1;
static int s_rcs_invalid_msg_idx = -1;

static bool s_saved_tt0_active = false;
static int32_t s_saved_tt0 = 0;
static bool s_saved_tt6655_active = false;
static int32_t s_saved_tt6655 = 0;
static bool s_rcs_popup_override_active = false;
static struct string *s_orig_invalid_title_str = NULL;
static struct string *s_orig_invalid_msg_str = NULL;
static struct string *s_override_ok_title_str = NULL;
static struct string *s_override_off_msg_str = NULL;

static int ain_find_struct_member_idx(int struc_idx, const char *name1, const char *name2)
{
	if (!ain || !ain->structures || struc_idx < 0 || struc_idx >= ain->nr_structures)
		return -1;
	struct ain_struct *st = &ain->structures[struc_idx];
	for (int i = 0; i < st->nr_members; i++) {
		const char *n1 = st->members[i].name;
		const char *n2 = st->members[i].name2;
		if (name1 && ((n1 && !strcmp(n1, name1)) || (n2 && n2[0] && !strcmp(n2, name1))))
			return i;
		if (name2 && ((n1 && !strcmp(n1, name2)) || (n2 && n2[0] && !strcmp(n2, name2))))
			return i;
	}
	return -1;
}

static inline struct page *vm_get_global_page_idx(int glob_idx)
{
	if (!vm_globals_ready() || glob_idx < 0 || glob_idx >= heap[0].page->nr_vars)
		return NULL;
	return vm_safe_get_page(global_get(glob_idx).i);
}

static inline union vm_value *vm_get_member_at(struct page *page, int mem_idx)
{
	if (!page || page->type != STRUCT_PAGE || mem_idx < 0 || mem_idx >= page->nr_vars)
		return NULL;
	return &page->values[mem_idx];
}

static inline uint16_t mod_le16(const uint8_t *buf, size_t off)
{
	return (uint16_t)buf[off] | ((uint16_t)buf[off + 1] << 8);
}

static inline int32_t mod_le32(const uint8_t *buf, size_t off)
{
	uint32_t v = (uint32_t)buf[off] |
	             ((uint32_t)buf[off + 1] << 8) |
	             ((uint32_t)buf[off + 2] << 16) |
	             ((uint32_t)buf[off + 3] << 24);
	return (int32_t)v;
}

static size_t mod_func_end_addr(int fno)
{
	if (!ain || fno < 0 || fno >= ain->nr_functions)
		return 0;
	size_t start = (size_t)ain->functions[fno].address;
	size_t best = ain->code_size;
	for (int i = 0; i < ain->nr_functions; i++) {
		if (i == fno || !ain->functions[i].name)
			continue;
		if (!strncmp(ain->functions[i].name, "<lambda", 7))
			continue;
		size_t addr = (size_t)ain->functions[i].address;
		if (addr > start && addr < best)
			best = addr;
	}
	return best;
}

static bool mod_is_streamer_cheat_str(const char *s)
{
	if (!s || !s[0])
		return false;
	return strstr(s, "Eins") != NULL ||
	       strstr(s, "eins") != NULL ||
	       strstr(s, GBK_XIEXIEMIAO) != NULL ||
	       strstr(s, GBK_ZHUBO) != NULL;
}

static bool mod_scan_tt0_req_at(size_t start_ip, size_t limit_addr, int tt_glob_idx, int32_t *out_req)
{
	if (!ain || !ain->code || start_ip >= limit_addr || limit_addr > ain->code_size)
		return false;
	size_t ip = start_ip;
	int hist_op[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
	int32_t hist_arg[8] = { 0 };
	for (int steps = 0; steps < 32 && ip + 2 <= limit_addr; steps++) {
		enum opcode op = mod_le16(ain->code, ip) & ~OPTYPE_MASK;
		if (op >= NR_OPCODES)
			break;
		int32_t w = instruction_width(op);
		if (w <= 0 || ip + (size_t)w > limit_addr)
			break;
		int32_t arg0 = (instructions[op].nr_args >= 1) ? mod_le32(ain->code, ip + 2) : 0;

		if (op == RETURN || op == ENDFUNC)
			break;
		if (op == CALLFUNC && arg0 == s_fno_run_info_dialog)
			break;

		/* Match: PUSH <tt>; REF; PUSH 0; REF; PUSH <req>; (NOTE + IFNZ | EQUALE + IFZ) */
		if ((op == IFNZ && hist_op[0] == NOTE) || (op == IFZ && hist_op[0] == EQUALE)) {
			if (hist_op[1] == PUSH &&
			    hist_op[2] == REF &&
			    hist_op[3] == PUSH && hist_arg[3] == 0 &&
			    hist_op[4] == REF &&
			    hist_op[5] == PUSH && (tt_glob_idx < 0 || hist_arg[5] == tt_glob_idx)) {
				*out_req = hist_arg[1];
				return true;
			}
		}

		for (int k = 7; k > 0; k--) {
			hist_op[k] = hist_op[k - 1];
			hist_arg[k] = hist_arg[k - 1];
		}
		hist_op[0] = (int)op;
		hist_arg[0] = arg0;
		ip += (size_t)w;
	}
	return false;
}

static void mod_scan_func_for_streamer_cheat(int fno, int tt_glob_idx, int depth)
{
	if (!ain || !ain->code || fno < 0 || fno >= ain->nr_functions || depth > 1)
		return;
	size_t ip = (size_t)ain->functions[fno].address;
	size_t end_addr = mod_func_end_addr(fno);
	if (end_addr > ain->code_size)
		end_addr = ain->code_size;

	while (ip + 2 <= end_addr) {
		enum opcode op = mod_le16(ain->code, ip) & ~OPTYPE_MASK;
		if (op >= NR_OPCODES) {
			ip += 2;
			continue;
		}
		int32_t w = instruction_width(op);
		if (w <= 0 || ip + (size_t)w > end_addr)
			break;

		/* 1. Fallback invalid-cheat dialog string slots in RunCharacterSetting */
		if (depth == 0 && op == S_PUSH && ip + 18 <= end_addr) {
			int32_t s1 = mod_le32(ain->code, ip + 2);
			enum opcode op2 = mod_le16(ain->code, ip + 6) & ~OPTYPE_MASK;
			enum opcode op3 = mod_le16(ain->code, ip + 12) & ~OPTYPE_MASK;
			if (op2 == S_PUSH && op3 == CALLFUNC) {
				int32_t s2 = mod_le32(ain->code, ip + 8);
				int32_t callee = mod_le32(ain->code, ip + 14);
				if (callee == s_fno_run_info_dialog &&
				    s1 >= 0 && s1 < ain->nr_strings && ain->strings[s1] &&
				    s2 >= 0 && s2 < ain->nr_strings && ain->strings[s2] &&
				    !strcmp(ain->strings[s1]->text, GBK_CHEAT_INVALID_TITLE) &&
				    !strcmp(ain->strings[s2]->text, GBK_CHEAT_INVALID_MSG)) {
					s_rcs_invalid_title_idx = s1;
					s_rcs_invalid_msg_idx = s2;
				}
			}
		}

		/* 2. Direct S_PUSH + (S_EQUALE / S_NOTE / String.Contains) cheat check */
		if (!s_rcs_streamer_cheat_gbk && op == S_PUSH && ip + 8 <= end_addr) {
			int32_t sidx = mod_le32(ain->code, ip + 2);
			if (sidx >= 0 && sidx < ain->nr_strings && ain->strings[sidx] &&
			    mod_is_streamer_cheat_str(ain->strings[sidx]->text)) {
				size_t ip2 = ip + 6;
				enum opcode op2 = mod_le16(ain->code, ip2) & ~OPTYPE_MASK;
				if (op2 < NR_OPCODES && (op2 == S_EQUALE || op2 == S_NOTE || op2 == CALLHLL)) {
					size_t ip3 = ip2 + (size_t)instruction_width(op2);
					size_t handler_ip = 0;
					bool matched = false;
					if (ip3 + 6 <= end_addr) {
						enum opcode op3 = mod_le16(ain->code, ip3) & ~OPTYPE_MASK;
						int32_t target = mod_le32(ain->code, ip3 + 2);
						if ((op2 == S_EQUALE || op2 == CALLHLL) && op3 == IFNZ && target > 0) {
							handler_ip = (size_t)target;
							matched = true;
						} else if ((op2 == S_EQUALE || op2 == CALLHLL) && op3 == IFZ) {
							handler_ip = ip3 + 6;
							matched = true;
						} else if (op2 == S_NOTE && op3 == IFNZ) {
							handler_ip = ip3 + 6;
							matched = true;
						} else if (op2 == S_NOTE && op3 == IFZ && target > 0) {
							handler_ip = (size_t)target;
							matched = true;
						}
					}
					if (matched) {
						s_rcs_streamer_cheat_gbk = strdup(ain->strings[sidx]->text);
						int32_t req = 0;
						if (handler_ip > 0 && mod_scan_tt0_req_at(handler_ip, end_addr, tt_glob_idx, &req)) {
							s_rcs_streamer_has_tt0_req = true;
							s_rcs_streamer_req_tt0 = req;
						}
					}
				}
			}
		}

		/* 3. String SWITCH / STRSWITCH cheat dispatch */
		if (!s_rcs_streamer_cheat_gbk && (op == SWITCH || op == STRSWITCH)) {
			int32_t sw_idx = mod_le32(ain->code, ip + 2);
			if (sw_idx >= 0 && sw_idx < ain->nr_switches) {
				struct ain_switch *sw = &ain->switches[sw_idx];
				if (sw->case_type == AIN_SWITCH_STRING && sw->cases) {
					for (int c = 0; c < sw->nr_cases; c++) {
						int32_t sidx = sw->cases[c].value;
						if (sidx >= 0 && sidx < ain->nr_strings && ain->strings[sidx] &&
						    mod_is_streamer_cheat_str(ain->strings[sidx]->text)) {
							s_rcs_streamer_cheat_gbk = strdup(ain->strings[sidx]->text);
							int32_t req = 0;
							if (sw->cases[c].address > 0 &&
							    mod_scan_tt0_req_at((size_t)sw->cases[c].address, end_addr, tt_glob_idx, &req)) {
								s_rcs_streamer_has_tt0_req = true;
								s_rcs_streamer_req_tt0 = req;
							}
							break;
						}
					}
				}
			}
		}

		/* 4. 1-level helper CALLFUNC inside RunCharacterSetting */
		if (!s_rcs_streamer_cheat_gbk && depth == 0 && op == CALLFUNC) {
			int32_t callee = mod_le32(ain->code, ip + 2);
			if (callee >= 0 && callee < ain->nr_functions &&
			    callee != fno && callee != s_fno_run_info_dialog &&
			    ain->functions[callee].name &&
			    strncmp(ain->functions[callee].name, "Scenes::RunIScene", 17) != 0) {
				mod_scan_func_for_streamer_cheat(callee, tt_glob_idx, depth + 1);
			}
		}

		if (op == ENDFUNC && mod_le32(ain->code, ip + 2) == fno)
			break;
		ip += (size_t)w;
	}
}

static bool mod_func_reads_struct_member(int fno, int mem_idx)
{
	if (!ain || !ain->code || fno < 0 || fno >= ain->nr_functions || mem_idx < 0)
		return false;
	size_t ip = (size_t)ain->functions[fno].address;
	size_t end_addr = mod_func_end_addr(fno);
	if (end_addr > ain->code_size)
		end_addr = ain->code_size;
	bool prev_struct_page = false;
	while (ip + 2 <= end_addr) {
		enum opcode op = mod_le16(ain->code, ip) & ~OPTYPE_MASK;
		if (op >= NR_OPCODES) {
			prev_struct_page = false;
			ip += 2;
			continue;
		}
		int32_t w = instruction_width(op);
		if (w <= 0 || ip + (size_t)w > end_addr)
			break;
		if (prev_struct_page && op == PUSH && mod_le32(ain->code, ip + 2) == mem_idx)
			return true;
		if (op == SH_STRUCTREF && mod_le32(ain->code, ip + 2) == mem_idx)
			return true;
		prev_struct_page = (op == PUSHSTRUCTPAGE);
		if (op == ENDFUNC && mod_le32(ain->code, ip + 2) == fno)
			break;
		ip += (size_t)w;
	}
	return false;
}

static void vm_inspect_mod_streamer_ain(void)
{
	if (!ain)
		return;
	if (s_inspected_ain == ain)
		return;

	s_inspected_ain = ain;
	s_game_config_ready = false;
	s_game_config_loaded = false;
	s_last_observed_summary_mode = -1;
	s_card_org_mdata_slot = -1;
	s_card_org_pair0_slot = -1;

	s_is_mod_streamer = false;
	s_is_summary_gated_by_adv5 = false;
	s_fno_run_char_setting = -1;
	s_fno_scene_char_setting_run = -1;
	s_fno_scene_stack_join = -1;
	s_fno_run_info_dialog = -1;
	s_fno_is_summary_get = -1;
	s_fno_is_summary_set = -1;
	s_fno_pcfv_load = -1;
	s_fno_qmmcv_update = -1;
	s_fno_cm_set_checkbox = -1;

	s_struc_pcfv = -1;
	s_struc_qmmcv = -1;
	s_struc_common_menu = -1;
	s_struc_scene_common_menu = -1;
	s_struc_game_config = -1;

	s_glob_game_config = -1;
	s_glob_card_cache = -1;
	s_glob_game_context = -1;
	s_glob_pcp = -1;
	s_glob_party_bonus = -1;
	s_glob_party = -1;
	s_glob_battle_ctx = -1;
	s_glob_enemy = -1;
	s_glob_tt = -1;
	s_glob_diff = -1;
	s_glob_battle_count = -1;
	s_glob_end_busy = -1;
	s_glob_end_busy_num = -1;

	s_mem_cfg_adv_shortcut = -1;
	s_mem_cfg_summary_mode = -1;
	s_mem_cc_front = -1;
	s_mem_front_proc_org = -1;

	free(s_rcs_streamer_cheat_gbk);
	s_rcs_streamer_cheat_gbk = NULL;
	s_rcs_streamer_has_tt0_req = false;
	s_rcs_streamer_req_tt0 = 0;
	s_rcs_invalid_title_idx = -1;
	s_rcs_invalid_msg_idx = -1;

	free(s_fno_flags);
	s_fno_flags = NULL;
	s_fno_flags_len = 0;

	if (ain->strings) {
		for (int i = 0; i < ain->nr_strings; i++) {
			if (!ain->strings[i])
				continue;
			const char *t = ain->strings[i]->text;
			if (strstr(t, GBK_CARD_STREAMER) ||
			    strstr(t, GBK_STREAMER_MODE) ||
			    strstr(t, "Eins")) {
				s_is_mod_streamer = true;
				break;
			}
		}
	}

	int struc_card_cache = -1;
	int struc_cc_front = -1;
	if (ain->structures) {
		for (int i = 0; i < ain->nr_structures; i++) {
			const char *sn = ain->structures[i].name;
			if (!sn)
				continue;
			if (!strcmp(sn, "PlayerCardFlipView"))
				s_struc_pcfv = i;
			else if (!strcmp(sn, "QuestMapMiniCardView"))
				s_struc_qmmcv = i;
			else if (!strcmp(sn, "CommonMenu"))
				s_struc_common_menu = i;
			else if (!strcmp(sn, "SceneCommonMenu"))
				s_struc_scene_common_menu = i;
			else if (!strcmp(sn, "GameConfig"))
				s_struc_game_config = i;
			else if (!strcmp(sn, "CardCache"))
				struc_card_cache = i;
			else if (!strcmp(sn, "CardConstructProcessCacheFront"))
				struc_cc_front = i;
		}
	}

	if (s_struc_game_config >= 0) {
		s_mem_cfg_adv_shortcut = ain_find_struct_member_idx(s_struc_game_config, "m_isShowAdvShortcut", NULL);
		s_mem_cfg_summary_mode = ain_find_struct_member_idx(s_struc_game_config, "m_isSummaryMode", NULL);
	}
	if (struc_card_cache >= 0)
		s_mem_cc_front = ain_find_struct_member_idx(struc_card_cache, "Front", NULL);
	if (struc_cc_front >= 0)
		s_mem_front_proc_org = ain_find_struct_member_idx(struc_cc_front, "m_procOrg", NULL);

	if (ain->globals) {
		for (int i = 0; i < ain->nr_globals; i++) {
			const char *gn = ain->globals[i].name;
			const char *gn2 = ain->globals[i].name2;
			if (!gn)
				continue;
			if (!strcmp(gn, "g_gameConfig"))
				s_glob_game_config = i;
			else if (!strcmp(gn, "g_cardCache"))
				s_glob_card_cache = i;
			else if (!strcmp(gn, "g_gameContext"))
				s_glob_game_context = i;
			else if (!strcmp(gn, "g_playerCommonParam"))
				s_glob_pcp = i;
			else if (!strcmp(gn, "g_partyBonus"))
				s_glob_party_bonus = i;
			else if (!strcmp(gn, "g_party"))
				s_glob_party = i;
			else if (!strcmp(gn, "g_battleContext"))
				s_glob_battle_ctx = i;
			else if (!strcmp(gn, "g_enemy"))
				s_glob_enemy = i;
			else if (!strcmp(gn, "tt"))
				s_glob_tt = i;
			else if (!strcmp(gn, GBK_DIFFICULTY) || !strcmp(gn, SJIS_DIFFICULTY) ||
			         (gn2 && (!strcmp(gn2, GBK_DIFFICULTY) || !strcmp(gn2, SJIS_DIFFICULTY))))
				s_glob_diff = i;
			else if (!strcmp(gn, GBK_BATTLE_COUNT) || !strcmp(gn, SJIS_BATTLE_COUNT) ||
			         (gn2 && (!strcmp(gn2, GBK_BATTLE_COUNT) || !strcmp(gn2, SJIS_BATTLE_COUNT))))
				s_glob_battle_count = i;
			else if (!strcmp(gn, "parts::detail::g_EndPartsBusyLoop"))
				s_glob_end_busy = i;
			else if (!strcmp(gn, "parts::detail::g_EndPartsBusyLoopNumber"))
				s_glob_end_busy_num = i;
		}
	}

	if (ain->functions && ain->nr_functions > 0) {
		s_fno_flags = xcalloc((size_t)ain->nr_functions, sizeof(uint8_t));
		s_fno_flags_len = ain->nr_functions;
		for (int i = 0; i < ain->nr_functions; i++) {
			const char *fn = ain->functions[i].name;
			if (!fn)
				continue;
			uint8_t fl = 0;
			if (!strncmp(fn, "CardCache@", 10) ||
			    !strncmp(fn, "CardConstruct", 13) ||
			    !strncmp(fn, "KeyValueMap<", 12)) {
				fl |= FNO_FLAG_CARD_CACHE | FNO_FLAG_CARD_UI;
			}
			if (!strncmp(fn, "PlayerCardFlipView", 18) ||
			    !strncmp(fn, "QuestMapMiniCardView", 20) ||
			    !strncmp(fn, "PartyCard@", 10) ||
			    !strncmp(fn, "FormationView@", 14) ||
			    !strncmp(fn, "CommonMenu@", 11) ||
			    !strncmp(fn, "SceneCharacterSetting@", 22)) {
				fl |= FNO_FLAG_CARD_UI;
			}
			if (!strncmp(fn, "SceneTitle@", 11) ||
			    !strncmp(fn, "SceneMap@", 9) ||
			    !strncmp(fn, "SceneQuestMap@", 14) ||
			    !strncmp(fn, "SceneAdv@", 9) ||
			    !strncmp(fn, "SceneParty@", 11) ||
			    !strncmp(fn, "SceneBattle@", 12) ||
			    !strncmp(fn, "SceneCommonMenu@", 16) ||
			    !strncmp(fn, "CommonMenu@", 11)) {
				fl |= FNO_FLAG_GAMEPLAY_SCENE;
			}

			if (!strcmp(fn, "Scenes::RunCharacterSetting")) {
				s_fno_run_char_setting = i;
				fl |= FNO_FLAG_CARD_UI;
			} else if (!strcmp(fn, "SceneCharacterSetting@Run")) {
				s_fno_scene_char_setting_run = i;
			} else if (!strcmp(fn, "SceneStack@Join")) {
				s_fno_scene_stack_join = i;
			} else if (!strcmp(fn, "DialogScenes::RunInformationDialog")) {
				s_fno_run_info_dialog = i;
			} else if (!strcmp(fn, "GameConfig@IsSummaryMode::get") &&
			           s_fno_is_summary_get < 0 &&
			           ain->functions[i].nr_args == 0 &&
			           ain->functions[i].return_type.data != AIN_VOID) {
				s_fno_is_summary_get = i;
			} else if (!strcmp(fn, "GameConfig@IsSummaryMode::set") &&
			           s_fno_is_summary_set < 0 &&
			           ain->functions[i].nr_args == 1 &&
			           ain->functions[i].return_type.data == AIN_VOID) {
				s_fno_is_summary_set = i;
			} else if (!strcmp(fn, "PlayerCardFlipView@Load") &&
			           ain->functions[i].nr_args == 0 &&
			           ain->functions[i].return_type.data == AIN_VOID) {
				s_fno_pcfv_load = i;
			} else if (!strcmp(fn, "QuestMapMiniCardView@UpdateCard") &&
			           ain->functions[i].nr_args == 0 &&
			           ain->functions[i].return_type.data == AIN_VOID) {
				s_fno_qmmcv_update = i;
			} else if (!strcmp(fn, "CommonMenu@SetCheckBoxState") &&
			           ain->functions[i].nr_args == 0 &&
			           ain->functions[i].return_type.data == AIN_VOID) {
				s_fno_cm_set_checkbox = i;
			}
			s_fno_flags[i] = fl;
		}
	}

	if (s_fno_is_summary_get >= 0 && s_mem_cfg_adv_shortcut >= 0) {
		s_is_summary_gated_by_adv5 = mod_func_reads_struct_member(s_fno_is_summary_get, s_mem_cfg_adv_shortcut);
	}

	if (s_fno_run_char_setting >= 0) {
		mod_scan_func_for_streamer_cheat(s_fno_run_char_setting, s_glob_tt, 0);
	}
}

static inline bool vm_has_mod_streamer_mode(void)
{
	if (s_inspected_ain != ain)
		vm_inspect_mod_streamer_ain();
	return s_is_mod_streamer;
}

bool modifier_api_has_streamer_mode(void)
{
	return vm_has_mod_streamer_mode();
}

static void vm_activate_rcs_popup_override(void)
{
	vm_inspect_mod_streamer_ain();
	if (s_rcs_popup_override_active || !ain || !ain->strings)
		return;
	if (s_rcs_invalid_title_idx < 0 || s_rcs_invalid_title_idx >= ain->nr_strings ||
	    s_rcs_invalid_msg_idx < 0 || s_rcs_invalid_msg_idx >= ain->nr_strings)
		return;
	if (!s_override_ok_title_str)
		s_override_ok_title_str = make_string(GBK_CHEAT_OK_TITLE, strlen(GBK_CHEAT_OK_TITLE));
	if (!s_override_off_msg_str)
		s_override_off_msg_str = make_string(GBK_STREAMER_OFF_MSG, strlen(GBK_STREAMER_OFF_MSG));
	s_orig_invalid_title_str = ain->strings[s_rcs_invalid_title_idx];
	s_orig_invalid_msg_str = ain->strings[s_rcs_invalid_msg_idx];
	ain->strings[s_rcs_invalid_title_idx] = s_override_ok_title_str;
	ain->strings[s_rcs_invalid_msg_idx] = s_override_off_msg_str;
	s_rcs_popup_override_active = true;
}

static inline bool vm_callstack_has_fno(int target_fno)
{
	if (target_fno < 0 || call_stack_ptr <= 0)
		return false;
	for (int i = 0; i < call_stack_ptr; i++) {
		if (call_stack[i].fno == target_fno)
			return true;
	}
	return false;
}

static int vm_find_hud_setter(char *name)
{
	int fno = ain_get_function(ain, name);
	if (fno < 0)
		return -1;
	const struct ain_function *f = &ain->functions[fno];
	return f->return_type.data == AIN_VOID && f->nr_args == 2
		&& f->vars[0].type.data == AIN_INT && f->vars[1].type.data == AIN_INT ? fno : -1;
}

static void vm_call_hud_setter(int fno, int slot, int first, int second)
{
	if (fno < 0 || vm_callstack_has_fno(fno))
		return;
	bool saved_dg = dg_callbegin_pending;
	dg_callbegin_pending = false;
	stack_push(first);
	stack_push(second);
	vm_call(fno, slot);
	dg_callbegin_pending = saved_dg;
}

static inline bool vm_callstack_any_flag(uint8_t flag_mask)
{
	if (!s_fno_flags || call_stack_ptr <= 0)
		return false;
	for (int i = 0; i < call_stack_ptr; i++) {
		int32_t fno = call_stack[i].fno;
		if (fno >= 0 && fno < s_fno_flags_len && (s_fno_flags[fno] & flag_mask))
			return true;
	}
	return false;
}

static bool vm_is_common_menu_on_callstack(void)
{
	if (call_stack_ptr <= 0 || (s_struc_common_menu < 0 && s_struc_scene_common_menu < 0))
		return false;
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		struct page *sp = vm_safe_get_page(call_stack[i].struct_page);
		if (!sp || sp->type != STRUCT_PAGE)
			continue;
		if (sp->index == s_struc_common_menu || sp->index == s_struc_scene_common_menu)
			return true;
	}
	return false;
}

static int32_t vm_get_tt(int idx, int32_t def_val)
{
	vm_inspect_mod_streamer_ain();
	struct page *tt = s_glob_tt >= 0 ? vm_get_global_page_idx(s_glob_tt) : vm_get_global_page("tt", NULL);
	if (!tt || tt->type != ARRAY_PAGE || idx < 0 || idx >= tt->nr_vars) return def_val;
	return tt->values[idx].i;
}

static void vm_set_tt(int idx, int32_t val)
{
	vm_inspect_mod_streamer_ain();
	struct page *tt = s_glob_tt >= 0 ? vm_get_global_page_idx(s_glob_tt) : vm_get_global_page("tt", NULL);
	if (!tt || tt->type != ARRAY_PAGE || idx < 0 || idx >= tt->nr_vars) return;
	tt->values[idx].i = val;
}

static void vm_restore_temp_rcs_overrides(bool force)
{
	if (!s_rcs_popup_override_active && !s_saved_tt0_active && !s_saved_tt6655_active)
		return;
	vm_inspect_mod_streamer_ain();
	bool in_rcs = vm_callstack_has_fno(s_fno_run_char_setting) ||
	              vm_callstack_has_fno(s_fno_scene_char_setting_run);
	bool in_info = vm_callstack_has_fno(s_fno_run_info_dialog);

	if (s_rcs_popup_override_active && ain && ain->strings) {
		if (force || in_info || !in_rcs) {
			if (s_rcs_invalid_title_idx >= 0 && s_rcs_invalid_title_idx < ain->nr_strings && s_orig_invalid_title_str)
				ain->strings[s_rcs_invalid_title_idx] = s_orig_invalid_title_str;
			if (s_rcs_invalid_msg_idx >= 0 && s_rcs_invalid_msg_idx < ain->nr_strings && s_orig_invalid_msg_str)
				ain->strings[s_rcs_invalid_msg_idx] = s_orig_invalid_msg_str;
			s_rcs_popup_override_active = false;
		}
	}
	if (s_saved_tt0_active) {
		if (force || in_info || !in_rcs) {
			vm_set_tt(0, s_saved_tt0);
			s_saved_tt0_active = false;
		}
	}
	if (s_saved_tt6655_active) {
		/* MOD 0.49 assigns tt[6655] = -99999 AFTER RunInformationDialog returns */
		if (force || !in_rcs) {
			vm_set_tt(6655, s_saved_tt6655);
			s_saved_tt6655_active = false;
		}
	}
}

static inline bool vm_is_in_card_cache_callstack(void)
{
	vm_inspect_mod_streamer_ain();
	return vm_callstack_any_flag(FNO_FLAG_CARD_CACHE);
}

static inline bool vm_is_in_card_ui_callstack(void)
{
	vm_inspect_mod_streamer_ain();
	return vm_callstack_any_flag(FNO_FLAG_CARD_CACHE | FNO_FLAG_CARD_UI);
}

/*
 * g_cardCache (global[286], group = -1) caches constructed CAS process arrays
 * across the entire process lifetime. Only clear the 5 lazily-populated
 * per-card KeyValueMap<string, array<CASConstructionProcess>> tables that
 * depend on m_isSummaryMode ("卡牌／主播" / "卡牌／物品／主播").
 * NEVER clear Front.m_procOrg: it is populated once in
 * CardConstructProcessCacheFront@0 for org IDs 1..10 and is never recreated!
 */
static int vm_clear_card_cache(void)
{
	vm_inspect_mod_streamer_ain();
	struct page *cc = s_glob_card_cache >= 0
		? vm_get_global_page_idx(s_glob_card_cache)
		: vm_get_global_page("g_cardCache", NULL);
	if (!cc || cc->type != STRUCT_PAGE)
		return 0;
	if (vm_is_in_card_cache_callstack()) {
		s_pending_clear_card_cache = true;
		return 0;
	}
	s_pending_clear_card_cache = false;
	int total = 0;
	struct page *front = vm_get_field_page(cc, "Front", NULL);
	total += vm_clear_kvmap_field(front, "m_procCardBasic");
	total += vm_clear_kvmap_field(vm_get_field_page(cc, "FrontItem", NULL), "m_proc");
	total += vm_clear_kvmap_field(vm_get_field_page(cc, "BackSkill", NULL), "m_proc");
	total += vm_clear_kvmap_field(vm_get_field_page(cc, "BackCard", NULL), "m_proc");
	total += vm_clear_kvmap_field(vm_get_field_page(cc, "BackCardListInfo", NULL), "m_proc");
	return total;
}

static bool vm_is_live_pcfv_page(struct page *p)
{
	if (!p || p->type != STRUCT_PAGE || p->index != s_struc_pcfv)
		return false;
	struct page *pc = vm_get_field_page(p, "m_playerCard", NULL);
	if (!pc || pc->type != STRUCT_PAGE)
		return false;
	struct page *par = vm_get_field_page(p, "m_parent", NULL);
	if (!par || par->type != STRUCT_PAGE || vm_get_field_int(par, "m_partsNumber", NULL, 0) <= 0)
		return false;
	struct page *cv = vm_get_field_page(p, "m_cardView", NULL);
	if (!cv || cv->type != ARRAY_PAGE || cv->nr_vars <= 0)
		return false;
	struct page *cv0 = vm_safe_get_page(cv->values[0].i);
	if (!cv0 || cv0->type != STRUCT_PAGE || vm_get_field_int(cv0, "m_partsNumber", NULL, 0) <= 0)
		return false;
	return true;
}

static bool vm_is_live_qmmcv_page(struct page *p)
{
	if (!p || p->type != STRUCT_PAGE || p->index != s_struc_qmmcv)
		return false;
	struct page *par = vm_get_field_page(p, "m_parent", NULL);
	if (!par || par->type != STRUCT_PAGE || vm_get_field_int(par, "m_partsNumber", NULL, 0) <= 0)
		return false;
	struct page *cids = vm_get_field_page(p, "m_cardId", NULL);
	if (!cids || cids->type != ARRAY_PAGE || cids->nr_vars <= 0)
		return false;
	return true;
}

/*
 * Rebuilds already-instantiated card views (SceneParty's PlayerCardFlipView and
 * QuestMap's QuestMapMiniCardView) and CommonMenu's SummaryCheckBox in-place
 * after Streamer Mode changes, so white-curtain cards reload immediately
 * without requiring the player to leave the current screen.
 */
static void vm_refresh_live_streamer_ui(void)
{
	if (!vm_is_ready())
		return;
	vm_inspect_mod_streamer_ain();
	if (vm_is_in_card_ui_callstack()) {
		s_pending_live_ui_refresh = true;
		return;
	}
	s_pending_live_ui_refresh = false;
	if (s_pending_clear_card_cache)
		vm_clear_card_cache();

	bool saved_dg = dg_callbegin_pending;
	dg_callbegin_pending = false;

	if (s_fno_pcfv_load >= 0 && s_struc_pcfv >= 0) {
		size_t cap = 64, nr = 0;
		int *slots = malloc(cap * sizeof(int));
		if (slots) {
			for (size_t s = 1; s < heap_size; s++) {
				if (heap[s].ref > 0 && vm_is_live_pcfv_page(heap[s].page)) {
					if (nr == cap) {
						cap *= 2;
						int *ns = realloc(slots, cap * sizeof(int));
						if (!ns) break;
						slots = ns;
					}
					slots[nr++] = (int)s;
				}
			}
			for (size_t i = 0; i < nr; i++) {
				int slot = slots[i];
				if (page_index_valid(slot) && heap[slot].ref > 0 &&
				    vm_is_live_pcfv_page(heap[slot].page)) {
					vm_call(s_fno_pcfv_load, slot);
				}
			}
			free(slots);
		}
	}

	if (s_fno_qmmcv_update >= 0 && s_struc_qmmcv >= 0) {
		size_t cap = 16, nr = 0;
		int *slots = malloc(cap * sizeof(int));
		if (slots) {
			for (size_t s = 1; s < heap_size; s++) {
				if (heap[s].ref > 0 && vm_is_live_qmmcv_page(heap[s].page)) {
					if (nr == cap) {
						cap *= 2;
						int *ns = realloc(slots, cap * sizeof(int));
						if (!ns) break;
						slots = ns;
					}
					slots[nr++] = (int)s;
				}
			}
			for (size_t i = 0; i < nr; i++) {
				int slot = slots[i];
				if (page_index_valid(slot) && heap[slot].ref > 0 &&
				    vm_is_live_qmmcv_page(heap[slot].page)) {
					vm_call(s_fno_qmmcv_update, slot);
				}
			}
			free(slots);
		}
	}

	if (s_fno_cm_set_checkbox >= 0 && s_struc_common_menu >= 0) {
		int cm_slot = -1;
		for (int i = call_stack_ptr - 1; i >= 0; i--) {
			struct page *sp = vm_safe_get_page(call_stack[i].struct_page);
			if (!sp || sp->type != STRUCT_PAGE)
				continue;
			if (sp->index == s_struc_common_menu) {
				cm_slot = call_stack[i].struct_page;
				break;
			}
			if (sp->index == s_struc_scene_common_menu) {
				union vm_value *mv = vm_get_struct_member(sp, "m_menu", NULL);
				if (mv && vm_safe_get_page(mv->i)) {
					cm_slot = mv->i;
					break;
				}
			}
		}
		struct page *cm_page = vm_safe_get_page(cm_slot);
		if (cm_page && cm_page->type == STRUCT_PAGE &&
		    cm_page->index == s_struc_common_menu &&
		    vm_get_field_page(cm_page, "m_act", NULL) != NULL) {
			vm_call(s_fno_cm_set_checkbox, cm_slot);
		}
	}

	dg_callbegin_pending = saved_dg;
}

/*
 * MOD 0.49+ gates GameConfig@IsSummaryMode::get and IsSummaryMode::set behind
 * m_isShowAdvShortcut[5] == true (the permanent unlock flag set by
 * SetEnableAdvShortCut(5, true) in "灌注Eins陈谢谢喵"). Without it,
 * IsSummaryMode::get always returns true AND IsSummaryMode::set(false) forces
 * m_isSummaryMode back to true and saves GameConfig.asd!
 */
static bool vm_ensure_streamer_unlock_gate(struct page *cfg)
{
	if (!cfg || cfg->type != STRUCT_PAGE)
		return false;
	union vm_value *adv_mem = (s_mem_cfg_adv_shortcut >= 0)
		? vm_get_member_at(cfg, s_mem_cfg_adv_shortcut)
		: vm_get_struct_member(cfg, "m_isShowAdvShortcut", NULL);
	if (!adv_mem)
		return false;

	bool changed = false;
	int adv_slot = adv_mem->i;
	if (adv_slot <= 0 || !page_index_valid(adv_slot)) {
		adv_slot = heap_alloc_slot(VM_PAGE);
		adv_mem->i = adv_slot;
		union vm_value dims = { .i = 7 };
		struct page *arr = alloc_array(1, &dims, AIN_ARRAY_BOOL, -1, false);
		heap_set_page(adv_slot, arr);
		if (arr && arr->nr_vars > 5)
			arr->values[5].i = 1;
		return true;
	}

	struct page *arr = heap[adv_slot].page;
	if (!arr || arr->type != ARRAY_PAGE || arr->nr_vars < 6) {
		union vm_value dims = { .i = 7 };
		arr = realloc_array(arr, 1, &dims, AIN_ARRAY_BOOL, -1, false);
		heap_set_page(adv_slot, arr);
		changed = true;
	}
	if (arr && arr->type == ARRAY_PAGE && arr->nr_vars > 5 && arr->values[5].i != 1) {
		arr->values[5].i = 1;
		changed = true;
	}
	return changed;
}

static bool vm_persist_game_config_asd(void)
{
	if (!vm_globals_ready() || !config.save_dir || !config.save_dir[0])
		return false;
	vm_inspect_mod_streamer_ain();
	int cfg_idx = s_glob_game_config >= 0 ? s_glob_game_config : vm_find_global_idx("g_gameConfig", NULL);
	if (cfg_idx < 0 || cfg_idx >= ain->nr_globals)
		return false;
	/* Before the game's own GroupLoad, g_gameConfig still holds defaults;
	 * a GroupSave now would wipe the user's speed settings and unlock flags. */
	if (!s_game_config_loaded)
		return false;
	int gidx = ain->globals[cfg_idx].group_index;
	if (gidx >= 0 && gidx < ain->nr_global_groups &&
	    ain->global_group_names && ain->global_group_names[gidx] && ain->global_group_names[gidx][0]) {
		return save_globals("rance10", "GameConfig.asd", ain->global_group_names[gidx], NULL) != 0;
	}
	return false;
}

static int vm_get_live_streamer_mode(bool allow_vm_call)
{
	if (!vm_globals_ready() || !vm_has_mod_streamer_mode())
		return -1;
	int cfg_idx = s_glob_game_config >= 0 ? s_glob_game_config : vm_find_global_idx("g_gameConfig", NULL);
	if (cfg_idx < 0 || cfg_idx >= heap[0].page->nr_vars)
		return -1;
	int cfg_slot = global_get(cfg_idx).i;
	struct page *cfg = vm_safe_get_page(cfg_slot);
	if (!cfg || cfg->type != STRUCT_PAGE)
		return -1;
	if (allow_vm_call && call_stack_ptr > 0 && s_fno_is_summary_get >= 0) {
		bool saved_dg = dg_callbegin_pending;
		dg_callbegin_pending = false;
		vm_call(s_fno_is_summary_get, cfg_slot);
		int res = stack_pop().i;
		dg_callbegin_pending = saved_dg;
		return res != 0 ? 1 : 0;
	}
	if (s_is_summary_gated_by_adv5) {
		union vm_value *adv_mem = s_mem_cfg_adv_shortcut >= 0
			? vm_get_member_at(cfg, s_mem_cfg_adv_shortcut)
			: vm_get_struct_member(cfg, "m_isShowAdvShortcut", NULL);
		struct page *adv_arr = adv_mem ? vm_safe_get_page(adv_mem->i) : NULL;
		if (!adv_arr || adv_arr->type != ARRAY_PAGE || adv_arr->nr_vars < 6 || adv_arr->values[5].i == 0)
			return 1;
	}
	union vm_value *sm_mem = s_mem_cfg_summary_mode >= 0
		? vm_get_member_at(cfg, s_mem_cfg_summary_mode)
		: vm_get_struct_member(cfg, "m_isSummaryMode", NULL);
	if (!sm_mem)
		return -1;
	return sm_mem->i != 0 ? 1 : 0;
}

static void vm_set_streamer_mode(int enabled, bool persist_disk, bool force_clear_cache)
{
	if (!vm_globals_ready() || !vm_has_mod_streamer_mode())
		return;
	int target = enabled ? 1 : 0;
	int cfg_idx = s_glob_game_config >= 0 ? s_glob_game_config : vm_find_global_idx("g_gameConfig", NULL);
	int cfg_slot = (cfg_idx >= 0 && cfg_idx < heap[0].page->nr_vars) ? global_get(cfg_idx).i : -1;
	struct page *cfg = vm_safe_get_page(cfg_slot);
	if (!cfg || cfg->type != STRUCT_PAGE)
		return;

	bool changed = false;
	if (target == 0) {
		if (vm_ensure_streamer_unlock_gate(cfg))
			changed = true;
	}
	union vm_value *sm_mem = s_mem_cfg_summary_mode >= 0
		? vm_get_member_at(cfg, s_mem_cfg_summary_mode)
		: vm_get_struct_member(cfg, "m_isSummaryMode", NULL);
	if (sm_mem && sm_mem->i != target) {
		sm_mem->i = target;
		changed = true;
	}
	if (changed || force_clear_cache) {
		vm_clear_card_cache();
		s_pending_live_ui_refresh = true;
	}
	if (persist_disk) {
		if (!vm_persist_game_config_asd())
			persist_streamer_mode_to_savedir(target != 0);
	}
	s_last_observed_summary_mode = target;
}

/*
 * CardConstructProcessCacheFront@0 populates Front.m_procOrg only for
 * organization IDs 1..10. When a save contains cross-version MOD cards absent
 * from Rance10EX.ex ("卡牌数据"), PlayerCard@OrganizationId::get returns 0,
 * causing m_procOrg.Get(0) to return null (no 208x312 CreateBase canvas and
 * no card frame/star/skill base ops -> 0x0 "空卡牌"). Clone org ID 7 ("其他")
 * into key 0 once so OrganizationId == 0 cards build a full 208x312 base.
 */
static void vm_ensure_card_org_fallback_proc(void)
{
	if (!vm_globals_ready())
		return;
	vm_inspect_mod_streamer_ain();

	struct page *cc = s_glob_card_cache >= 0
		? vm_get_global_page_idx(s_glob_card_cache)
		: vm_get_global_page("g_cardCache", NULL);
	if (!cc || cc->type != STRUCT_PAGE)
		return;
	union vm_value *front_v = s_mem_cc_front >= 0
		? vm_get_member_at(cc, s_mem_cc_front)
		: vm_get_struct_member(cc, "Front", NULL);
	struct page *front = front_v ? vm_safe_get_page(front_v->i) : NULL;
	if (!front || front->type != STRUCT_PAGE)
		return;
	union vm_value *po_v = s_mem_front_proc_org >= 0
		? vm_get_member_at(front, s_mem_front_proc_org)
		: vm_get_struct_member(front, "m_procOrg", NULL);
	struct page *proc_org = po_v ? vm_safe_get_page(po_v->i) : NULL;
	if (!proc_org || proc_org->type != STRUCT_PAGE || proc_org->nr_vars <= 0)
		return;

	/* Fast O(1) verification once key 0 has already been injected */
	if (s_card_org_mdata_slot > 0 && s_card_org_pair0_slot > 0 &&
	    proc_org->values[0].i == s_card_org_mdata_slot) {
		struct page *md_fast = vm_safe_get_page(s_card_org_mdata_slot);
		if (md_fast && md_fast->type == ARRAY_PAGE && md_fast->nr_vars > 0 &&
		    md_fast->values[0].i == s_card_org_pair0_slot) {
			return;
		}
	}

	if (vm_is_in_card_cache_callstack())
		return;

	union vm_value *m_data_mem = vm_get_struct_member(proc_org, "m_data", NULL);
	if (!m_data_mem)
		return;
	int m_data_slot = m_data_mem->i;
	struct page *m_data = vm_safe_get_page(m_data_slot);
	if (!m_data || m_data->type != ARRAY_PAGE || m_data->nr_vars <= 0)
		return;

	int src_pair_slot = -1;
	int fallback_pair_slot = -1;
	int insert_pos = m_data->nr_vars;
	for (int i = 0; i < m_data->nr_vars; i++) {
		int pslot = m_data->values[i].i;
		struct page *pair = vm_safe_get_page(pslot);
		if (!pair || pair->type != STRUCT_PAGE)
			continue;
		union vm_value *kv = vm_get_struct_member(pair, "key", NULL);
		if (!kv)
			continue;
		struct page *val_arr = vm_get_field_page(pair, "val", NULL);
		bool valid_val = (val_arr && val_arr->type == ARRAY_PAGE && val_arr->nr_vars > 0);
		if (kv->i == 0) {
			if (valid_val) {
				s_card_org_mdata_slot = m_data_slot;
				s_card_org_pair0_slot = pslot;
				return;
			}
			insert_pos = -1 - i;
		} else if (kv->i > 0 && insert_pos == m_data->nr_vars) {
			insert_pos = i;
		}
		if (valid_val) {
			if (kv->i == 7)
				src_pair_slot = pslot;
			if (fallback_pair_slot < 0)
				fallback_pair_slot = pslot;
		}
	}

	if (src_pair_slot < 0)
		src_pair_slot = fallback_pair_slot;
	if (src_pair_slot < 0)
		return;

	struct page *src_pair = vm_safe_get_page(src_pair_slot);
	if (!src_pair)
		return;
	struct page *cloned_pair = copy_page(src_pair);
	if (!cloned_pair)
		return;
	vm_set_field_int(cloned_pair, "key", NULL, 0);
	int new_pair_slot = heap_alloc_page(cloned_pair);

	m_data = vm_safe_get_page(m_data_slot);
	if (!m_data || m_data->type != ARRAY_PAGE) {
		heap_unref(new_pair_slot);
		return;
	}
	if (insert_pos < 0) {
		int existing_idx = -1 - insert_pos;
		if (existing_idx >= 0 && existing_idx < m_data->nr_vars) {
			variable_set(m_data, existing_idx, AIN_STRUCT, vm_int(new_pair_slot));
		} else {
			heap_unref(new_pair_slot);
			return;
		}
	} else {
		int old_nr = m_data->nr_vars;
		if (insert_pos > old_nr)
			insert_pos = old_nr;
		struct page *next = alloc_page(ARRAY_PAGE, m_data->a_type, old_nr + 1);
		next->array = m_data->array;
		if (insert_pos > 0)
			memcpy(next->values, m_data->values, (size_t)insert_pos * sizeof(*next->values));
		next->values[insert_pos].i = new_pair_slot;
		if (insert_pos < old_nr)
			memcpy(next->values + insert_pos + 1, m_data->values + insert_pos,
			       (size_t)(old_nr - insert_pos) * sizeof(*next->values));
		free_page(m_data);
		heap_set_page(m_data_slot, next);
	}

	s_card_org_mdata_slot = m_data_slot;
	if (insert_pos == 0 || insert_pos == -1)
		s_card_org_pair0_slot = new_pair_slot;

	vm_clear_card_cache();
	s_pending_live_ui_refresh = true;
}

static void maybe_load_initial_modifier_config(void)
{
	char *cfg_path = NULL;
	char *ui_game_key = NULL;
	pthread_mutex_lock(&g_mod_mutex);
	if (g_has_lock_mask_init && g_has_dohna_lock_mask_init && g_has_evenicle_lock_mask_init &&
	    g_has_sr_lock_mask_init && g_has_r01_lock_mask_init && g_has_r02_lock_mask_init &&
	    g_has_r03_lock_mask_init && g_has_r08_lock_mask_init && g_has_r09_lock_mask_init &&
	    g_has_ui_scale_init && ex_all_locks_initialized_locked()) {
		pthread_mutex_unlock(&g_mod_mutex);
		return;
	}
	if (g_modifier_config_path && g_modifier_config_path[0])
		cfg_path = strdup(g_modifier_config_path);
	if (g_ui_scale_game_key && g_ui_scale_game_key[0])
		ui_game_key = strdup(g_ui_scale_game_key);
	pthread_mutex_unlock(&g_mod_mutex);

#ifndef __ANDROID__
	if (!cfg_path) {
		const char *env_cfg = getenv("XSYS4_MOD_CONFIG_FILE");
		if (env_cfg && env_cfg[0])
			cfg_path = strdup(env_cfg);
	}
	if (!ui_game_key) {
		const char *env_key = getenv("XSYS4_MOD_GAME_KEY");
		if (env_key && env_key[0])
			ui_game_key = strdup(env_key);
	}
#endif
	if (!cfg_path) {
		free(ui_game_key);
		return;
	}
	if (!file_exists(cfg_path)) {
		free(cfg_path);
		free(ui_game_key);
		return;
	}
	size_t sz = 0;
	char *raw = file_read(cfg_path, &sz);
	free(cfg_path);
	if (!raw) {
		free(ui_game_key);
		return;
	}
	cJSON *root = cJSON_Parse(raw);
	free(raw);
	if (!root || !cJSON_IsObject(root)) {
		cJSON_Delete(root);
		free(ui_game_key);
		return;
	}
	cJSON *j_mask = cJSON_GetObjectItem(root, "lock_mask");
	cJSON *j_dd   = cJSON_GetObjectItem(root, "dohna_lock_mask");
	cJSON *j_ev   = cJSON_GetObjectItem(root, "evenicle_lock_mask");
	cJSON *j_sr   = cJSON_GetObjectItem(root, "sengokurance_lock_mask");
	cJSON *j_r03  = cJSON_GetObjectItem(root, "rance03_lock_mask");
	cJSON *j_r08  = cJSON_GetObjectItem(root, "rance08_lock_mask");
	cJSON *j_r09  = cJSON_GetObjectItem(root, "rance09_lock_mask");
	cJSON *j_r01  = cJSON_GetObjectItem(root, "rance01_lock_mask");
	cJSON *j_r02  = cJSON_GetObjectItem(root, "rance02_lock_mask");
	cJSON *j_dis  = cJSON_GetObjectItem(root, "disable_streamer_mode");
	int mask = (j_mask && cJSON_IsNumber(j_mask)) ? j_mask->valueint : 0;
	int dd_mask = (j_dd && cJSON_IsNumber(j_dd)) ? j_dd->valueint : 0;
	int ev_mask = (j_ev && cJSON_IsNumber(j_ev)) ? j_ev->valueint : 0;
	int sr_mask = (j_sr && cJSON_IsNumber(j_sr)) ? j_sr->valueint : 0;
	int r03_mask = (j_r03 && cJSON_IsNumber(j_r03)) ? j_r03->valueint : 0;
	int r08_mask = (j_r08 && cJSON_IsNumber(j_r08)) ? j_r08->valueint : 0;
	int r09_mask = (j_r09 && cJSON_IsNumber(j_r09)) ? j_r09->valueint : 0;
	int r01_mask = (j_r01 && cJSON_IsNumber(j_r01)) ? j_r01->valueint : 0;
	int r02_mask = (j_r02 && cJSON_IsNumber(j_r02)) ? j_r02->valueint : 0;
	if (j_dis && cJSON_IsTrue(j_dis))
		mask |= LOCK_DISABLE_STREAMER_MODE;
	int font_pct = cjson_get_per_game_scale(root, "game_font_scale_pct", ui_game_key, 100, clamp_font_scale_pct);
	int sb_pct   = cjson_get_per_game_scale(root, "game_scrollbar_scale_pct", ui_game_key, 100, clamp_scrollbar_scale_pct);
	free(ui_game_key);

	bool apply_initial_scale = false;
	pthread_mutex_lock(&g_mod_mutex);
	ex_load_locks_from_json_locked(root);
	cJSON_Delete(root);
	if (!g_has_lock_mask_init) {
		g_has_lock_mask_init = true;
		g_live_lock_mask = mask;
		if (mask & LOCK_DISABLE_STREAMER_MODE)
			g_pending_lock_streamer_mode = 0;
	}
	if (!g_has_dohna_lock_mask_init) {
		g_has_dohna_lock_mask_init = true;
		g_dohna_lock_mask = dd_mask;
	}
	if (!g_has_evenicle_lock_mask_init) {
		g_has_evenicle_lock_mask_init = true;
		g_evenicle_lock_mask = ev_mask;
	}
	if (!g_has_sr_lock_mask_init) {
		g_has_sr_lock_mask_init = true;
		g_sr_lock_mask = sr_mask;
	}
	if (!g_has_r03_lock_mask_init) {
		g_has_r03_lock_mask_init = true;
		g_r03_lock_mask = r03_mask;
	}
	if (!g_has_r08_lock_mask_init) {
		g_has_r08_lock_mask_init = true;
		g_r08_lock_mask = r08_mask;
	}
	if (!g_has_r09_lock_mask_init) {
		g_has_r09_lock_mask_init = true;
		g_r09_lock_mask = r09_mask;
	}
	if (!g_has_r01_lock_mask_init) {
		g_has_r01_lock_mask_init = true;
		g_r01_lock_mask = r01_mask;
	}
	if (!g_has_r02_lock_mask_init) {
		g_has_r02_lock_mask_init = true;
		g_r02_lock_mask = r02_mask;
	}
	if (!g_has_ui_scale_init) {
		g_has_ui_scale_init = true;
		g_font_scale_pct = font_pct;
		g_scrollbar_scale_pct = sb_pct;
		apply_initial_scale = true;
	}
	pthread_mutex_unlock(&g_mod_mutex);
	if (apply_initial_scale) {
		s_applied_font_scale_pct = font_pct;
		s_applied_scrollbar_scale_pct = sb_pct;
		parts_set_modifier_font_scale(font_pct);
		parts_set_modifier_scrollbar_scale(sb_pct);
	}
}

static void modifier_mark_config_ready(void)
{
	if (!vm_globals_ready())
		return;
	maybe_load_initial_modifier_config();
	if (!game_rance10) {
		s_game_config_ready = true;
		return;
	}
	vm_inspect_mod_streamer_ain();
	s_game_config_ready = true;
	if (!s_is_mod_streamer)
		return;

	pthread_mutex_lock(&g_mod_mutex);
	int mask = g_live_lock_mask;
	int pending_sm = g_pending_lock_streamer_mode;
	g_pending_lock_streamer_mode = -1;
	pthread_mutex_unlock(&g_mod_mutex);

	if ((mask & LOCK_DISABLE_STREAMER_MODE) || pending_sm == 0) {
		pthread_mutex_lock(&g_mod_mutex);
		g_live_lock_mask |= LOCK_DISABLE_STREAMER_MODE;
		mask = g_live_lock_mask;
		pthread_mutex_unlock(&g_mod_mutex);
		vm_set_streamer_mode(0, true, true);
		persist_modifier_config_from_native(mask);
	} else if (pending_sm == 1) {
		vm_set_streamer_mode(1, true, true);
		persist_modifier_config_from_native(mask);
	} else {
		int cur_sm = vm_get_live_streamer_mode(false);
		s_last_observed_summary_mode = cur_sm;
		if (cur_sm == 0) {
			pthread_mutex_lock(&g_mod_mutex);
			g_live_lock_mask |= LOCK_DISABLE_STREAMER_MODE;
			mask = g_live_lock_mask;
			pthread_mutex_unlock(&g_mod_mutex);
			vm_clear_card_cache();
			s_pending_live_ui_refresh = true;
			persist_modifier_config_from_native(mask);
		}
	}
}

void modifier_on_config_loaded(void)
{
	if (!vm_globals_ready())
		return;
	vm_inspect_mod_streamer_ain();
	s_game_config_loaded = true;
	modifier_mark_config_ready();
}

void modifier_on_globals_loaded(void)
{
	if (!game_rance10 || !vm_globals_ready())
		return;
	vm_ensure_card_org_fallback_proc();
	if (!vm_has_mod_streamer_mode() || !s_game_config_ready)
		return;
	pthread_mutex_lock(&g_mod_mutex);
	int mask = g_live_lock_mask;
	pthread_mutex_unlock(&g_mod_mutex);
	if (mask & LOCK_DISABLE_STREAMER_MODE) {
		/* Always purge g_cardCache on save/resume load in case resume.bin had cached white curtains */
		vm_set_streamer_mode(0, true, true);
	} else {
		s_last_observed_summary_mode = vm_get_live_streamer_mode(false);
	}
}

/*
 * In 日不落 MOD, Scenes::RunCharacterSetting gates almost all cheat codes behind
 * tt[1224] == 1 (which challenge modes / base phase A reset to 0, showing
 * "作弊码禁用！当前状态或模式下无法使用！"). Setting tt[1224] = 1 unlocks the gate
 * cleanly without wiping the player's tt[6655] bonus score to -99999.
 * Also ensure tt[6699] (the VK_0 re-entrancy lock) is cleared if RunCharacterSetting
 * is not currently on call_stack.
 */
static void vm_unlock_mod_cheat_gate(void)
{
	vm_inspect_mod_streamer_ain();
	struct page *tt = s_glob_tt >= 0 ? vm_get_global_page_idx(s_glob_tt) : vm_get_global_page("tt", NULL);
	if (!tt || tt->type != ARRAY_PAGE)
		return;
	if (tt->nr_vars > 1224)
		tt->values[1224].i = 1;
	if (tt->nr_vars > 6699 && !vm_callstack_has_fno(s_fno_run_char_setting))
		tt->values[6699].i = 0;
}

static void vm_trigger_streamer_unlock_popup(void)
{
	vm_inspect_mod_streamer_ain();
	pthread_mutex_lock(&g_mod_mutex);
	g_live_lock_mask |= LOCK_DISABLE_STREAMER_MODE;
	int mask = g_live_lock_mask;
	pthread_mutex_unlock(&g_mod_mutex);

	vm_set_streamer_mode(0, true, true);
	persist_modifier_config_from_native(mask);
	if (!s_is_mod_streamer)
		return;

	vm_unlock_mod_cheat_gate();
	const char *cheat_gbk = s_rcs_streamer_cheat_gbk ? s_rcs_streamer_cheat_gbk : GBK_STREAMER_PASSPHRASE;
	free(s_pending_cheat_gbk);
	s_pending_cheat_gbk = strdup(cheat_gbk);
	s_cheat_wait_frames = 180;
	s_pending_is_streamer_cheat = true;
}

static void vm_queue_cheat_code_utf8(const char *utf8_str)
{
	if (!utf8_str || !utf8_str[0]) return;
	if (!strcmp(utf8_str, "关闭主播模式") ||
	    !strcmp(utf8_str, "灌注Eins陈谢谢喵") ||
	    !strcmp(utf8_str, "关注Eins陈谢谢喵")) {
		vm_trigger_streamer_unlock_popup();
		return;
	}
	if (!strcmp(utf8_str, "1123029976")) {
		utf8_str = "界徐盛";
	}
	vm_unlock_mod_cheat_gate();
	if (sys4_text_is_gbk)
		sys4_set_text_gbk(true);
	char *gbk = utf2sjis(utf8_str, 0);
	if (gbk) {
		if (gbk[0]) {
			free(s_pending_cheat_gbk);
			s_pending_cheat_gbk = gbk;
			s_cheat_wait_frames = 180;
			s_pending_is_streamer_cheat = false;
		} else {
			free(gbk);
		}
	}
}

static void vm_step_pending_cheat(void)
{
	if (!s_pending_cheat_gbk)
		return;
	if (!ain || !ain->functions || call_stack_ptr <= 0)
		return;
	vm_inspect_mod_streamer_ain();

	bool in_run_char_setting = false;
	int scs_frame = -1;
	struct page *scs_page = NULL;

	for (int i = 0; i < call_stack_ptr; i++) {
		int32_t fno = call_stack[i].fno;
		if (fno < 0 || fno >= ain->nr_functions)
			continue;
		if (fno == s_fno_run_char_setting) {
			in_run_char_setting = true;
		} else if (fno == s_fno_scene_char_setting_run) {
			in_run_char_setting = true;
			scs_frame = i;
			scs_page = vm_safe_get_page(call_stack[i].struct_page);
		}
	}

	if (scs_page && scs_frame >= 0) {
		struct page *scs_ss = vm_get_field_page(scs_page, "m_sceneStack", NULL);
		bool in_join = false;
		for (int i = scs_frame + 1; i < call_stack_ptr; i++) {
			int32_t fno = call_stack[i].fno;
			if (fno == s_fno_scene_stack_join) {
				struct page *join_ss = vm_safe_get_page(call_stack[i].struct_page);
				if (!scs_ss || join_ss == scs_ss) {
					if (!scs_ss) scs_ss = join_ss;
					in_join = true;
					break;
				}
			}
		}
		if (in_join && scs_ss) {
			key_set_injected(VK_0, false);
			s_pulse_key0_frames = 0;
			if (ain->globals) {
				struct page *tt = s_glob_tt >= 0 ? vm_get_global_page_idx(s_glob_tt) : vm_get_global_page("tt", NULL);
				if (tt && tt->type == ARRAY_PAGE && tt->nr_vars > 1224)
					tt->values[1224].i = 1;
			}
			union vm_value *cur_v = vm_get_struct_member(scs_page, "m_current", NULL);
			if (cur_v) {
				vm_set_string_slot(cur_v->i, s_pending_cheat_gbk);
			}
			struct page *gc = s_glob_game_context >= 0 ? vm_get_global_page_idx(s_glob_game_context) : vm_get_global_page("g_gameContext", NULL);
			vm_set_player_info_name(gc, s_pending_cheat_gbk);

			if (s_pending_is_streamer_cheat) {
				if (s_rcs_streamer_cheat_gbk != NULL) {
					if (s_rcs_streamer_has_tt0_req) {
						s_saved_tt0 = vm_get_tt(0, 0);
						s_saved_tt0_active = true;
						vm_set_tt(0, s_rcs_streamer_req_tt0);
					}
					s_saved_tt6655 = vm_get_tt(6655, 0);
					s_saved_tt6655_active = true;
				} else {
					vm_activate_rcs_popup_override();
				}
				s_wait_post_rcs_streamer_refresh = true;
				s_pending_is_streamer_cheat = false;
			}

			vm_set_field_int(scs_ss, "<IsFinish>", "m_isFinish", 1);
			int idx_end_busy = s_glob_end_busy >= 0 ? s_glob_end_busy : vm_find_global_idx("parts::detail::g_EndPartsBusyLoop", NULL);
			if (idx_end_busy >= 0)
				global_set(idx_end_busy, vm_int(1), false);
			int idx_end_num = s_glob_end_busy_num >= 0 ? s_glob_end_busy_num : vm_find_global_idx("parts::detail::g_EndPartsBusyLoopNumber", NULL);
			if (idx_end_num >= 0)
				global_set(idx_end_num, vm_int(0), false);
			PE_ReleaseMessage();
			free(s_pending_cheat_gbk);
			s_pending_cheat_gbk = NULL;
			s_cheat_wait_frames = 0;
			return;
		}
	}

	if (in_run_char_setting) {
		key_set_injected(VK_0, false);
	} else {
		key_set_injected(VK_0, true);
	}

	if (--s_cheat_wait_frames <= 0) {
		key_set_injected(VK_0, false);
		free(s_pending_cheat_gbk);
		s_pending_cheat_gbk = NULL;
		s_cheat_wait_frames = 0;
		s_pending_is_streamer_cheat = false;
		vm_restore_temp_rcs_overrides(true);
	}
}

static void vm_boost_card_array(struct page *arr, int set_count, int add_count, int atk_bonus, int hp_bonus, int *total_cards)
{
	if (!arr || arr->type != ARRAY_PAGE) return;
	for (int i = 0; i < arr->nr_vars; i++) {
		struct page *c_page = vm_safe_get_page(arr->values[i].i);
		if (!c_page || c_page->type != STRUCT_PAGE) continue;
		if (total_cards) (*total_cards)++;
		if (set_count > 0) {
			vm_set_field_int(c_page, "m_count", NULL, set_count);
			vm_set_field_int(c_page, "m_lastCount", NULL, set_count);
		} else if (add_count > 0) {
			int32_t cur = vm_get_field_int(c_page, "m_count", NULL, 1);
			vm_set_field_int(c_page, "m_count", NULL, cur + add_count);
			vm_set_field_int(c_page, "m_lastCount", NULL, cur + add_count);
		}
		if (atk_bonus > 0) {
			int32_t cur_atk = vm_get_field_int(c_page, "m_totalAtk", NULL, 100);
			if (cur_atk < atk_bonus)
				vm_set_field_int(c_page, "m_totalAtk", NULL, atk_bonus);
		}
		if (hp_bonus > 0) {
			int32_t cur_hp = vm_get_field_int(c_page, "m_totalHp", NULL, 100);
			if (cur_hp < hp_bonus)
				vm_set_field_int(c_page, "m_totalHp", NULL, hp_bonus);
		}
	}
}

static int vm_process_all_cards(int set_count, int add_count, int atk_bonus, int hp_bonus)
{
	int total = 0;
	struct page *pc = vm_get_global_page("g_playerCard", NULL);
	if (!pc) return 0;
	struct page *org_arr = vm_get_field_page(pc, "m_org", NULL);
	if (org_arr && org_arr->type == ARRAY_PAGE) {
		for (int i = 0; i < org_arr->nr_vars; i++) {
			struct page *org_page = vm_safe_get_page(org_arr->values[i].i);
			struct page *card_arr = vm_get_field_page(org_page, "m_card", NULL);
			vm_boost_card_array(card_arr, set_count, add_count, atk_bonus, hp_bonus, &total);
		}
	}
	vm_boost_card_array(vm_get_field_page(pc, "m_stock", NULL), set_count, add_count, atk_bonus, hp_bonus, &total);
	vm_boost_card_array(vm_get_field_page(pc, "m_extraCard", NULL), set_count, add_count, atk_bonus, hp_bonus, &total);
	vm_boost_card_array(vm_get_field_page(pc, "m_oldCard", "m_card"), set_count, add_count, atk_bonus, hp_bonus, NULL);
	return total;
}

static int vm_process_all_characters(int set_star, int set_exp)
{
	struct page *ch = vm_get_global_page("g_character", NULL);
	if (!ch) return 0;
	struct page *arr = vm_get_field_page(ch, "m_character", NULL);
	if (!arr || arr->type != ARRAY_PAGE) return 0;
	for (int i = 0; i < arr->nr_vars; i++) {
		struct page *c_page = vm_safe_get_page(arr->values[i].i);
		if (!c_page || c_page->type != STRUCT_PAGE) continue;
		if (set_star > 0) {
			int32_t cur = vm_get_field_int(c_page, "m_star", NULL, 0);
			if (cur < set_star) vm_set_field_int(c_page, "m_star", NULL, set_star);
		}
		if (set_exp > 0) {
			vm_set_field_int(c_page, "<Exp>", NULL, set_exp);
		}
	}
	return arr->nr_vars;
}

static void vm_cure_and_boost_leaders(bool boost_crit, bool reset_skills)
{
	struct page *party = vm_get_global_page("g_party", NULL);
	if (!party) return;
	struct page *leaders_col = vm_get_field_page(party, "m_leaders", NULL);
	struct page *leaders_arr = vm_get_field_page(leaders_col, "m_leaders", NULL);
	if (!leaders_arr || leaders_arr->type != ARRAY_PAGE) return;

	for (int i = 0; i < leaders_arr->nr_vars; i++) {
		struct page *lc = vm_safe_get_page(leaders_arr->values[i].i);
		if (!lc || lc->type != STRUCT_PAGE) continue;
		if (boost_crit) {
			struct page *ls = vm_get_field_page(lc, "m_state", NULL);
			if (ls) {
				int32_t st = vm_get_field_int(ls, "m_state", NULL, -1);
				if (st >= 1 || st == -1) vm_set_field_int(ls, "m_state", NULL, 0); /* 0 = Boost & Actionable */
				vm_set_field_int(ls, "IsDamageSacrifice", NULL, 0);
				vm_set_field_int(ls, "IsBadConditionSacrifice", NULL, 0);
				vm_set_field_int(ls, "IsCritical", NULL, 1);
				vm_set_field_int(ls, "IsMultiAttack", NULL, 1);
			}
		}
		if (reset_skills) {
			struct page *sc_col = vm_get_field_page(lc, "m_skillCounter", NULL);
			struct page *sc_arr = vm_get_field_page(sc_col, "m_counter", NULL);
			if (sc_arr && sc_arr->type == ARRAY_PAGE) {
				for (int k = 0; k < sc_arr->nr_vars; k++) {
					struct page *scv = vm_safe_get_page(sc_arr->values[k].i);
					if (scv && scv->type == STRUCT_PAGE) {
						vm_set_field_int(scv, "IsUsed", NULL, 0);
						vm_set_field_int(scv, "IsLastUsed", NULL, 0);
						vm_set_field_int(scv, "UsedCount", NULL, 0);
					}
				}
			}
		}
	}
}

static void vm_json_write_if_present(cJSON *root, const char *key, struct page *page, const char *f1, const char *f2)
{
	cJSON *it = cJSON_GetObjectItem(root, key);
	if (it && cJSON_IsNumber(it)) {
		vm_set_field_int(page, f1, f2, it->valueint);
	}
}

static void vm_execute_pending_action(const char *json_str)
{
	cJSON *act = cJSON_Parse(json_str);
	if (!act) return;

	vm_inspect_mod_streamer_ain();
	struct page *gc    = s_glob_game_context >= 0 ? vm_get_global_page_idx(s_glob_game_context) : vm_get_global_page("g_gameContext", NULL);
	struct page *pcp   = s_glob_pcp >= 0 ? vm_get_global_page_idx(s_glob_pcp) : vm_get_global_page("g_playerCommonParam", NULL);
	struct page *pb    = s_glob_party_bonus >= 0 ? vm_get_global_page_idx(s_glob_party_bonus) : vm_get_global_page("g_partyBonus", NULL);
	struct page *party = s_glob_party >= 0 ? vm_get_global_page_idx(s_glob_party) : vm_get_global_page("g_party", NULL);
	struct page *ps    = vm_get_field_page(party, "m_status", NULL);
	struct page *cs    = vm_get_field_page(ps, "m_status", NULL);
	struct page *bc    = s_glob_battle_ctx >= 0 ? vm_get_global_page_idx(s_glob_battle_ctx) : vm_get_global_page("g_battleContext", NULL);
	struct page *enemy = s_glob_enemy >= 0 ? vm_get_global_page_idx(s_glob_enemy) : vm_get_global_page("g_enemy", NULL);

	cJSON *it;
	/* Instant battle actions */
	if ((it = cJSON_GetObjectItem(act, "killEnemy")) && cJSON_IsTrue(it) && enemy) {
		vm_set_field_int(enemy, "m_hp", NULL, 1);
	}
	if ((it = cJSON_GetObjectItem(act, "weakenEnemy")) && cJSON_IsTrue(it) && enemy) {
		struct page *ecs = vm_get_field_page(enemy, "m_status", NULL);
		if (ecs) {
			vm_set_field_int(ecs, "<Atk>", NULL, 0);
			vm_set_field_int(ecs, "m_AtkPerPhysical", NULL, 0);
			vm_set_field_int(ecs, "m_AtkPerMagical", NULL, 0);
			vm_set_field_int(ecs, "m_DefPerPhysical", NULL, 0);
			vm_set_field_int(ecs, "m_DefPerMagical", NULL, 0);
			vm_set_field_int(ecs, "m_PerAvoidance", NULL, 0);
			vm_set_field_int(ecs, "m_DecPerBadCondition", NULL, 0);
		}
	}
	if ((it = cJSON_GetObjectItem(act, "refillPartyHp")) && cJSON_IsTrue(it) && party) {
		int32_t max_hp = vm_get_field_int(ps, "m_totalHp", NULL, 99999);
		if (max_hp < 1) max_hp = 99999;
		vm_set_field_int(party, "m_hp", NULL, max_hp);
		union vm_value *hr = vm_get_struct_member(party, "m_hpRatio", NULL);
		if (hr) hr->f = 1.0f;
	}
	if ((it = cJSON_GetObjectItem(act, "battleAp")) && cJSON_IsNumber(it) && bc) {
		struct page *ap_p = vm_get_field_page(bc, "<Ap>", NULL);
		vm_set_field_int(ap_p, "m_ap", NULL, it->valueint);
	}
	if ((it = cJSON_GetObjectItem(act, "battleCombo")) && cJSON_IsNumber(it) && bc) {
		struct page *cb_p = vm_get_field_page(bc, "<Combo>", NULL);
		vm_set_field_int(cb_p, "m_count", NULL, it->valueint);
	}
	if ((it = cJSON_GetObjectItem(act, "battleRound")) && cJSON_IsNumber(it) && bc) {
		struct page *rd_p = vm_get_field_page(bc, "<Round>", NULL);
		vm_set_field_int(rd_p, "m_round", NULL, it->valueint);
		int32_t lim = vm_get_field_int(rd_p, "Limit", NULL, 30);
		if (lim < 99) vm_set_field_int(rd_p, "Limit", NULL, 99);
	}
	if ((it = cJSON_GetObjectItem(act, "cureAndBoostLeaders")) && cJSON_IsTrue(it)) {
		vm_cure_and_boost_leaders(true, true);
	}

	/* Resources & Progress */
	vm_json_write_if_present(act, "turn", gc, "m_turn", NULL);
	vm_json_write_if_present(act, "chapter", gc, "m_chapter", NULL);
	if ((it = cJSON_GetObjectItem(act, "difficulty")) && cJSON_IsNumber(it)) {
		int idx = s_glob_diff >= 0 ? s_glob_diff : vm_find_global_idx(GBK_DIFFICULTY, SJIS_DIFFICULTY);
		if (idx >= 0) global_set(idx, vm_int(it->valueint), false);
	}
	if ((it = cJSON_GetObjectItem(act, "battleCount")) && cJSON_IsNumber(it)) {
		int idx = s_glob_battle_count >= 0 ? s_glob_battle_count : vm_find_global_idx(GBK_BATTLE_COUNT, SJIS_BATTLE_COUNT);
		if (idx >= 0) global_set(idx, vm_int(it->valueint), false);
	}
	vm_json_write_if_present(act, "foodTicket", pcp, "m_foodTicket", NULL);
	vm_json_write_if_present(act, "honor", pcp, "m_honor", NULL);
	vm_json_write_if_present(act, "friendPoint", pcp, "m_friendPoint", NULL);
	vm_json_write_if_present(act, "totalExp", pcp, "<TotalExp>", NULL);
	vm_json_write_if_present(act, "partyRank", pcp, "<PartyRank>", NULL);
	vm_json_write_if_present(act, "treasureNotGetCount", pcp, "<TreasureNotGetCount>", NULL);
	vm_json_write_if_present(act, "itemStar", pcp, "<ItemStar>", NULL);
	vm_json_write_if_present(act, "isUsedHannyZippo", pcp, "<IsUsedHannyZippo>", NULL);
	vm_json_write_if_present(act, "partyBonusMaxPoint", pb, "m_maxPoint", NULL);

	/* Party & CommonStatus */
	vm_json_write_if_present(act, "partyHp", party, "m_hp", NULL);
	vm_json_write_if_present(act, "partyMaxHp", ps, "m_totalHp", NULL);
	vm_json_write_if_present(act, "partyAvailCount", party, "m_availableCount", NULL);
	vm_json_write_if_present(act, "partyIsFixStatus", party, "<IsFixStatus>", NULL);
	if (party && ps) {
		int32_t cur_hp = vm_get_field_int(party, "m_hp", NULL, 1);
		int32_t max_hp = vm_get_field_int(ps, "m_totalHp", NULL, 1);
		union vm_value *hr = vm_get_struct_member(party, "m_hpRatio", NULL);
		if (hr && max_hp > 0) {
			hr->f = (float)cur_hp / (float)max_hp;
			if (hr->f > 1.0f) hr->f = 1.0f;
		}
	}
	if ((it = cJSON_GetObjectItem(act, "allStatusPer")) && cJSON_IsNumber(it) && cs) {
		int v = it->valueint;
		vm_set_field_int(cs, "m_AtkPerPhysical", NULL, v);
		vm_set_field_int(cs, "m_AtkPerMagical", NULL, v);
		vm_set_field_int(cs, "m_DefPerPhysical", NULL, v);
		vm_set_field_int(cs, "m_DefPerMagical", NULL, v);
		vm_set_field_int(cs, "m_PerAvoidance", NULL, v);
		vm_set_field_int(cs, "m_DecPerBadCondition", NULL, v);
		vm_set_field_int(cs, "m_perAutoBattleAtk", NULL, v);
		vm_set_field_int(cs, "m_PerCritical", NULL, v);
		vm_set_field_int(cs, "m_PerMultiAttack", NULL, v);
		vm_set_field_int(cs, "m_PerExpUp", NULL, v);
		vm_set_field_int(cs, "m_PerHpUp", NULL, v);
		vm_set_field_int(cs, "m_PerAutoHeal", NULL, v);
		vm_set_field_int(cs, "m_PerFindTreasure", NULL, v);
		vm_set_field_int(cs, "m_PerInjection", NULL, v);
		vm_set_field_int(cs, "m_AddPerHit", NULL, v);
		vm_set_field_int(cs, "StatusMaxPer", NULL, v);
	}
	vm_json_write_if_present(act, "partyAtk", cs, "<Atk>", NULL);
	vm_json_write_if_present(act, "atkPerPhys", cs, "m_AtkPerPhysical", NULL);
	vm_json_write_if_present(act, "atkPerMag", cs, "m_AtkPerMagical", NULL);
	vm_json_write_if_present(act, "defPerPhys", cs, "m_DefPerPhysical", NULL);
	vm_json_write_if_present(act, "defPerMag", cs, "m_DefPerMagical", NULL);
	vm_json_write_if_present(act, "perAvoid", cs, "m_PerAvoidance", NULL);
	vm_json_write_if_present(act, "decBadCond", cs, "m_DecPerBadCondition", NULL);
	vm_json_write_if_present(act, "perAutoAtk", cs, "m_perAutoBattleAtk", NULL);
	vm_json_write_if_present(act, "perCrit", cs, "m_PerCritical", NULL);
	vm_json_write_if_present(act, "perMulti", cs, "m_PerMultiAttack", NULL);
	vm_json_write_if_present(act, "perExpUp", cs, "m_PerExpUp", NULL);
	vm_json_write_if_present(act, "perHpUp", cs, "m_PerHpUp", NULL);
	vm_json_write_if_present(act, "perAutoHeal", cs, "m_PerAutoHeal", NULL);
	vm_json_write_if_present(act, "perFindTreasure", cs, "m_PerFindTreasure", NULL);
	vm_json_write_if_present(act, "perInjection", cs, "m_PerInjection", NULL);
	vm_json_write_if_present(act, "addPerHit", cs, "m_AddPerHit", NULL);
	vm_json_write_if_present(act, "statusMaxPer", cs, "StatusMaxPer", NULL);

	/* World & Country State */
	if ((it = cJSON_GetObjectItem(act, "humanCount")) && cJSON_IsNumber(it)) vm_set_tt(33, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "humanDead")) && cJSON_IsNumber(it)) {
		vm_set_tt(34, it->valueint);
		vm_set_field_int(pcp, "m_lastHumanDead", NULL, it->valueint);
	}
	if ((it = cJSON_GetObjectItem(act, "humanAlivePer")) && cJSON_IsNumber(it)) {
		vm_set_tt(35, it->valueint);
		vm_set_field_int(pcp, "m_lastHumanAlivePer", NULL, it->valueint);
	}
	if ((it = cJSON_GetObjectItem(act, "humanDeadPer")) && cJSON_IsNumber(it)) vm_set_tt(36, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "curseTurn")) && cJSON_IsNumber(it)) vm_set_tt(41, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "rareLuck")) && cJSON_IsNumber(it)) vm_set_tt(42, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "hannyZippo")) && cJSON_IsNumber(it)) {
		vm_set_tt(101, it->valueint);
		if (bc) vm_set_field_int(bc, "m_hannyZippoCount", NULL, it->valueint);
	}
	if ((it = cJSON_GetObjectItem(act, "seaPlayerArmy")) && cJSON_IsNumber(it)) vm_set_tt(66, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "seaEnemyArmy")) && cJSON_IsNumber(it)) vm_set_tt(68, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "leazasState")) && cJSON_IsNumber(it)) vm_set_tt(13, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "helmanState")) && cJSON_IsNumber(it)) vm_set_tt(14, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "zethState")) && cJSON_IsNumber(it)) vm_set_tt(15, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "freeCitiesState")) && cJSON_IsNumber(it)) vm_set_tt(16, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "leazasPArmy")) && cJSON_IsNumber(it)) vm_set_tt(25, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "helmanPArmy")) && cJSON_IsNumber(it)) vm_set_tt(26, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "zethPArmy")) && cJSON_IsNumber(it)) vm_set_tt(27, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "freeCitiesPArmy")) && cJSON_IsNumber(it)) vm_set_tt(28, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "leazasEArmy")) && cJSON_IsNumber(it)) vm_set_tt(29, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "helmanEArmy")) && cJSON_IsNumber(it)) vm_set_tt(30, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "zethEArmy")) && cJSON_IsNumber(it)) vm_set_tt(31, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "freeCitiesEArmy")) && cJSON_IsNumber(it)) vm_set_tt(32, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "subRemnant")) && cJSON_IsNumber(it)) vm_set_tt(37, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "subDwarf")) && cJSON_IsNumber(it)) vm_set_tt(38, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "subGeneral")) && cJSON_IsNumber(it)) vm_set_tt(39, it->valueint);
	if ((it = cJSON_GetObjectItem(act, "subHanny")) && cJSON_IsNumber(it)) vm_set_tt(40, it->valueint);

	cJSON *arr;
	if ((arr = cJSON_GetObjectItem(act, "countryState")) && cJSON_IsArray(arr)) {
		for (int i = 0; i < 4 && i < cJSON_GetArraySize(arr); i++)
			vm_set_tt(13 + i, cJSON_GetArrayItem(arr, i)->valueint);
	}
	if ((arr = cJSON_GetObjectItem(act, "countryPlayerArmy")) && cJSON_IsArray(arr)) {
		for (int i = 0; i < 4 && i < cJSON_GetArraySize(arr); i++)
			vm_set_tt(25 + i, cJSON_GetArrayItem(arr, i)->valueint);
	}
	if ((arr = cJSON_GetObjectItem(act, "countryEnemyArmy")) && cJSON_IsArray(arr)) {
		for (int i = 0; i < 4 && i < cJSON_GetArraySize(arr); i++)
			vm_set_tt(29 + i, cJSON_GetArrayItem(arr, i)->valueint);
	}
	if ((arr = cJSON_GetObjectItem(act, "subArmy")) && cJSON_IsArray(arr)) {
		for (int i = 0; i < 4 && i < cJSON_GetArraySize(arr); i++)
			vm_set_tt(37 + i, cJSON_GetArrayItem(arr, i)->valueint);
	}

	/* Batch Cards & Characters */
	int set_card_count = (it = cJSON_GetObjectItem(act, "setCardCount")) && cJSON_IsNumber(it) ? it->valueint : 0;
	int add_card_count = (it = cJSON_GetObjectItem(act, "addCardCount")) && cJSON_IsNumber(it) ? it->valueint : 0;
	int card_atk_bonus = (it = cJSON_GetObjectItem(act, "cardAtkBonus")) && cJSON_IsNumber(it) ? it->valueint : 0;
	int card_hp_bonus  = (it = cJSON_GetObjectItem(act, "cardHpBonus")) && cJSON_IsNumber(it) ? it->valueint : 0;
	if (set_card_count > 0 || add_card_count > 0 || card_atk_bonus > 0 || card_hp_bonus > 0) {
		vm_process_all_cards(set_card_count, add_card_count, card_atk_bonus, card_hp_bonus);
	}
	int set_char_star = (it = cJSON_GetObjectItem(act, "setCharStar")) && cJSON_IsNumber(it) ? it->valueint : 0;
	int set_char_exp  = (it = cJSON_GetObjectItem(act, "setCharExp")) && cJSON_IsNumber(it) ? it->valueint : 0;
	if (set_char_star > 0 || set_char_exp > 0) {
		vm_process_all_characters(set_char_star, set_char_exp);
	}

	bool want_streamer_popup = false;
	if ((it = cJSON_GetObjectItem(act, "streamerPopup")) &&
	    (cJSON_IsTrue(it) || (cJSON_IsNumber(it) && it->valueint != 0))) {
		want_streamer_popup = true;
	}
	if ((it = cJSON_GetObjectItem(act, "streamerMode")) && cJSON_IsNumber(it)) {
		int en = it->valueint != 0 ? 1 : 0;
		int new_mask;
		pthread_mutex_lock(&g_mod_mutex);
		g_has_lock_mask_init = true;
		if (en == 0)
			g_live_lock_mask |= LOCK_DISABLE_STREAMER_MODE;
		else
			g_live_lock_mask &= ~LOCK_DISABLE_STREAMER_MODE;
		new_mask = g_live_lock_mask;
		pthread_mutex_unlock(&g_mod_mutex);
		s_game_config_ready = true;
		vm_set_streamer_mode(en, true, true);
		persist_modifier_config_from_native(new_mask);
	}
	if (want_streamer_popup) {
		s_game_config_ready = true;
		vm_trigger_streamer_unlock_popup();
	} else if ((it = cJSON_GetObjectItem(act, "cheatCode")) && cJSON_IsString(it) &&
	           it->valuestring && it->valuestring[0]) {
		vm_queue_cheat_code_utf8(it->valuestring);
	} else if ((it = cJSON_GetObjectItem(act, "fillStreamerPassphrase")) &&
	           (cJSON_IsTrue(it) || (cJSON_IsNumber(it) && it->valueint != 0))) {
		s_game_config_ready = true;
		vm_trigger_streamer_unlock_popup();
	}
	if (!s_pending_cheat_gbk &&
	    (it = cJSON_GetObjectItem(act, "triggerKey0")) &&
	    (cJSON_IsTrue(it) || (cJSON_IsNumber(it) && it->valueint != 0))) {
		vm_unlock_mod_cheat_gate();
		s_pulse_key0_frames = 8;
		key_set_injected(VK_0, true);
	}
	cJSON *tt_idx = cJSON_GetObjectItem(act, "ttFlagIdx");
	cJSON *tt_val = cJSON_GetObjectItem(act, "ttFlagVal");
	if (tt_idx && cJSON_IsNumber(tt_idx) && tt_val && cJSON_IsNumber(tt_val)) {
		vm_set_tt(tt_idx->valueint, tt_val->valueint);
	}

	bool scale_changed = false;
	int save_mask = 0;
	pthread_mutex_lock(&g_mod_mutex);
	if ((it = cJSON_GetObjectItem(act, "fontScale")) && cJSON_IsNumber(it)) {
		int fp = clamp_font_scale_pct(it->valueint);
		if (g_font_scale_pct != fp || !g_has_ui_scale_init) {
			g_font_scale_pct = fp;
			g_has_ui_scale_init = true;
			parts_set_modifier_font_scale(fp);
			scale_changed = true;
		}
	}
	if ((it = cJSON_GetObjectItem(act, "scrollbarScale")) && cJSON_IsNumber(it)) {
		int sp = clamp_scrollbar_scale_pct(it->valueint);
		if (g_scrollbar_scale_pct != sp || !g_has_ui_scale_init) {
			g_scrollbar_scale_pct = sp;
			g_has_ui_scale_init = true;
			parts_set_modifier_scrollbar_scale(sp);
			scale_changed = true;
		}
	}
	save_mask = g_live_lock_mask;
	pthread_mutex_unlock(&g_mod_mutex);
	if (scale_changed) {
		persist_modifier_config_from_native(save_mask);
	}

	cJSON_Delete(act);
}

static void vm_enforce_locks(int mask)
{
	const int active_locks = mask & ~LOCK_DISABLE_STREAMER_MODE;
	if (!active_locks || !ain) return;

	vm_inspect_mod_streamer_ain();
	struct page *pcp   = s_glob_pcp >= 0 ? vm_get_global_page_idx(s_glob_pcp) : vm_get_global_page("g_playerCommonParam", NULL);
	struct page *party = s_glob_party >= 0 ? vm_get_global_page_idx(s_glob_party) : vm_get_global_page("g_party", NULL);
	struct page *ps    = party ? vm_get_field_page(party, "m_status", NULL) : NULL;
	struct page *cs    = ps ? vm_get_field_page(ps, "m_status", NULL) : NULL;
	struct page *bc    = s_glob_battle_ctx >= 0 ? vm_get_global_page_idx(s_glob_battle_ctx) : vm_get_global_page("g_battleContext", NULL);
	struct page *enemy = s_glob_enemy >= 0 ? vm_get_global_page_idx(s_glob_enemy) : vm_get_global_page("g_enemy", NULL);

	if ((active_locks & LOCK_PARTY_MAX_HP) && party && ps) {
		int32_t max_hp = vm_get_field_int(ps, "m_totalHp", NULL, 9999);
		if (max_hp < 1) max_hp = 9999;
		if (vm_get_field_int(party, "m_hp", NULL, 0) < max_hp) {
			vm_set_field_int(party, "m_hp", NULL, max_hp);
			union vm_value *hr = vm_get_struct_member(party, "m_hpRatio", NULL);
			if (hr) hr->f = 1.0f;
		}
	}
	if ((active_locks & LOCK_INFINITE_AP) && bc) {
		struct page *ap_p = vm_get_field_page(bc, "<Ap>", NULL);
		if (ap_p && vm_get_field_int(ap_p, "m_ap", NULL, 0) < 99) {
			vm_set_field_int(ap_p, "m_ap", NULL, 99);
		}
	}
	if ((active_locks & LOCK_BATTLE_COMBO) && bc) {
		struct page *cb_p = vm_get_field_page(bc, "<Combo>", NULL);
		if (cb_p && vm_get_field_int(cb_p, "m_count", NULL, 0) < 999) {
			vm_set_field_int(cb_p, "m_count", NULL, 999);
		}
	}
	if ((active_locks & LOCK_BATTLE_ROUND_1) && bc) {
		struct page *rd_p = vm_get_field_page(bc, "<Round>", NULL);
		if (rd_p) {
			if (vm_get_field_int(rd_p, "m_round", NULL, 1) > 1)
				vm_set_field_int(rd_p, "m_round", NULL, 1);
			if (vm_get_field_int(rd_p, "Limit", NULL, 30) < 99)
				vm_set_field_int(rd_p, "Limit", NULL, 99);
		}
	}
	if ((active_locks & LOCK_ONE_HIT_KILL) && enemy) {
		if (vm_get_field_int(enemy, "m_hp", NULL, 0) > 1) {
			vm_set_field_int(enemy, "m_hp", NULL, 1);
		}
	}
	if ((active_locks & LOCK_ENEMY_ZERO_ATK) && enemy) {
		struct page *ecs = vm_get_field_page(enemy, "m_status", NULL);
		if (ecs) {
			vm_set_field_int(ecs, "<Atk>", NULL, 0);
			vm_set_field_int(ecs, "m_AtkPerPhysical", NULL, 0);
			vm_set_field_int(ecs, "m_AtkPerMagical", NULL, 0);
			vm_set_field_int(ecs, "m_DefPerPhysical", NULL, 0);
			vm_set_field_int(ecs, "m_DefPerMagical", NULL, 0);
			vm_set_field_int(ecs, "m_DecPerBadCondition", NULL, 0);
		}
	}
	if ((active_locks & LOCK_LEADER_BOOST_CRIT) || (active_locks & LOCK_INFINITE_SKILL_USE)) {
		vm_cure_and_boost_leaders((active_locks & LOCK_LEADER_BOOST_CRIT) != 0,
		                          (active_locks & LOCK_INFINITE_SKILL_USE) != 0);
	}
	if (active_locks & LOCK_INFINITE_ZIPPO) {
		if (pcp) vm_set_field_int(pcp, "<IsUsedHannyZippo>", NULL, 0);
		if (bc) {
			if (vm_get_field_int(bc, "m_hannyZippoCount", NULL, 0) < 9)
				vm_set_field_int(bc, "m_hannyZippoCount", NULL, 9);
			vm_set_field_int(bc, "m_curseTurn", NULL, 0);
		}
	}
	if ((active_locks & LOCK_MAX_RESOURCES) && pcp) {
		if (vm_get_field_int(pcp, "m_foodTicket", NULL, 0) < 999)
			vm_set_field_int(pcp, "m_foodTicket", NULL, 999);
		if (vm_get_field_int(pcp, "m_honor", NULL, 0) < 999)
			vm_set_field_int(pcp, "m_honor", NULL, 999);
		if (vm_get_field_int(pcp, "m_friendPoint", NULL, 0) < 999)
			vm_set_field_int(pcp, "m_friendPoint", NULL, 999);
	}
	if (active_locks & LOCK_TREASURE_100) {
		if (cs && vm_get_field_int(cs, "m_PerFindTreasure", NULL, 0) < 999)
			vm_set_field_int(cs, "m_PerFindTreasure", NULL, 999);
		if (pcp && vm_get_field_int(pcp, "<TreasureNotGetCount>", NULL, 0) < 99)
			vm_set_field_int(pcp, "<TreasureNotGetCount>", NULL, 99);
		if (vm_get_tt(42, 0) < 999)
			vm_set_tt(42, 999);
	}
}

/* =========================================================================
 * Dohna Dohna (AIN v14) Live VM Modifier & Cheats
 * ========================================================================= */

static struct ain *s_dd_inspected_ain = NULL;
static int s_dd_glob_static_game = -1;
static int s_dd_glob_static_res  = -1;
static int s_dd_glob_battle_stage = -1;
static int s_dd_glob_pill_max    = -1;

static int s_dd_struc_decision_timer = -1;
static int s_dd_struc_player         = -1;
static int s_dd_struc_enemy          = -1;
static int s_dd_struc_boss_bp        = -1;

static int s_dd_fno_gc_money_set    = -1;
static int s_dd_fno_gc_tp_set       = -1;
static int s_dd_fno_gc_tp_max_set   = -1;
static int s_dd_fno_pp_val_set      = -1;
static int s_dd_fno_pl_name_get     = -1;
static int s_dd_fno_pl_pow_get      = -1;
static int s_dd_fno_pl_tec_get      = -1;
static int s_dd_fno_pl_spd_get      = -1;
static int s_dd_fno_pl_debug_lv_set = -1;
static int s_dd_fno_pl_up_level     = -1;
static int s_dd_fno_pl_recalc       = -1;
static int s_dd_fno_pl_weapon_set   = -1;
static int s_dd_fno_wc_set_limit    = -1;
static int s_dd_fno_stock_add       = -1;
static int s_dd_fno_buff_clear      = -1;
static int s_dd_fno_dt_restart      = -1;
static int s_dd_fno_item_type_get   = -1;

static void vm_inspect_dohna_ain(void)
{
	if (!ain || s_dd_inspected_ain == ain)
		return;
	s_dd_inspected_ain = ain;

	s_dd_glob_static_game  = -1;
	s_dd_glob_static_res   = -1;
	s_dd_glob_battle_stage = -1;
	s_dd_glob_pill_max     = -1;

	s_dd_struc_decision_timer = -1;
	s_dd_struc_player         = -1;
	s_dd_struc_enemy          = -1;
	s_dd_struc_boss_bp        = -1;

	s_dd_fno_gc_money_set    = -1;
	s_dd_fno_gc_tp_set       = -1;
	s_dd_fno_gc_tp_max_set   = -1;
	s_dd_fno_pp_val_set      = -1;
	s_dd_fno_pl_name_get     = -1;
	s_dd_fno_pl_pow_get      = -1;
	s_dd_fno_pl_tec_get      = -1;
	s_dd_fno_pl_spd_get      = -1;
	s_dd_fno_pl_debug_lv_set = -1;
	s_dd_fno_pl_up_level     = -1;
	s_dd_fno_pl_recalc       = -1;
	s_dd_fno_pl_weapon_set   = -1;
	s_dd_fno_wc_set_limit    = -1;
	s_dd_fno_stock_add       = -1;
	s_dd_fno_buff_clear      = -1;
	s_dd_fno_dt_restart      = -1;
	s_dd_fno_item_type_get   = -1;

	if (ain->globals) {
		for (int i = 0; i < ain->nr_globals; i++) {
			const char *gn = ain->globals[i].name;
			if (!gn) continue;
			if (!strcmp(gn, "DohnaDohna::m_staticGame"))
				s_dd_glob_static_game = i;
			else if (!strcmp(gn, "DohnaDohna::m_staticResources"))
				s_dd_glob_static_res = i;
			else if (!strcmp(gn, "BattleStage::m_stage"))
				s_dd_glob_battle_stage = i;
			else if (!strcmp(gn, "Worker::PillMax"))
				s_dd_glob_pill_max = i;
		}
	}

	if (ain->structures) {
		for (int i = 0; i < ain->nr_structures; i++) {
			const char *sn = ain->structures[i].name;
			if (!sn) continue;
			if (!strcmp(sn, "DecisionTimer"))
				s_dd_struc_decision_timer = i;
			else if (!strcmp(sn, "Player"))
				s_dd_struc_player = i;
			else if (!strcmp(sn, "Enemy"))
				s_dd_struc_enemy = i;
			else if (!strcmp(sn, "BossBattlePlayer"))
				s_dd_struc_boss_bp = i;
		}
	}

	if (ain->functions) {
		for (int i = 0; i < ain->nr_functions; i++) {
			const char *fn = ain->functions[i].name;
			if (!fn) continue;
			int nargs = ain->functions[i].nr_args;
			enum ain_data_type rt = ain->functions[i].return_type.data;

			if (nargs == 1 && rt == AIN_VOID) {
				if (!strcmp(fn, "GameContext@Money::set") && s_dd_fno_gc_money_set < 0)
					s_dd_fno_gc_money_set = i;
				else if (!strcmp(fn, "GameContext@TrainingPoint::set") && s_dd_fno_gc_tp_set < 0)
					s_dd_fno_gc_tp_set = i;
				else if (!strcmp(fn, "GameContext@TrainingPointMax::set") && s_dd_fno_gc_tp_max_set < 0)
					s_dd_fno_gc_tp_max_set = i;
				else if (!strcmp(fn, "PlayerParam@Value::set") && s_dd_fno_pp_val_set < 0)
					s_dd_fno_pp_val_set = i;
				else if (!strcmp(fn, "Player@DebugLevel::set") && s_dd_fno_pl_debug_lv_set < 0)
					s_dd_fno_pl_debug_lv_set = i;
				else if (!strcmp(fn, "Player@RecalcParam") && s_dd_fno_pl_recalc < 0)
					s_dd_fno_pl_recalc = i;
				else if (!strcmp(fn, "Player@WeaponId::set") && s_dd_fno_pl_weapon_set < 0)
					s_dd_fno_pl_weapon_set = i;
				else if (!strcmp(fn, "WorkerCollection@SetLimit") && s_dd_fno_wc_set_limit < 0)
					s_dd_fno_wc_set_limit = i;
			} else if (nargs == 1 && rt != AIN_VOID) {
				if (!strcmp(fn, "Player@UpLevel") && s_dd_fno_pl_up_level < 0)
					s_dd_fno_pl_up_level = i;
			} else if (nargs == 2 && rt == AIN_VOID) {
				if (!strcmp(fn, "Stock@Add") && s_dd_fno_stock_add < 0)
					s_dd_fno_stock_add = i;
			} else if (nargs == 0 && rt == AIN_VOID) {
				if (!strcmp(fn, "BuffEffectCollection@Clear") && s_dd_fno_buff_clear < 0)
					s_dd_fno_buff_clear = i;
				else if (!strcmp(fn, "DecisionTimer@Restart") && s_dd_fno_dt_restart < 0)
					s_dd_fno_dt_restart = i;
			} else if (nargs == 0 && rt != AIN_VOID) {
				if (!strcmp(fn, "Player@Name::get") && s_dd_fno_pl_name_get < 0)
					s_dd_fno_pl_name_get = i;
				else if (!strcmp(fn, "Player@Power::get") && s_dd_fno_pl_pow_get < 0)
					s_dd_fno_pl_pow_get = i;
				else if (!strcmp(fn, "Player@Technic::get") && s_dd_fno_pl_tec_get < 0)
					s_dd_fno_pl_tec_get = i;
				else if (!strcmp(fn, "Player@Speed::get") && s_dd_fno_pl_spd_get < 0)
					s_dd_fno_pl_spd_get = i;
				else if (!strcmp(fn, "Item@Type::get") && s_dd_fno_item_type_get < 0)
					s_dd_fno_item_type_get = i;
			}
		}
	}
}

static inline int vm_dd_clamp(int val, int lo, int hi)
{
	if (val < lo) return lo;
	if (val > hi) return hi;
	return val;
}

static int vm_dd_get_option_global_slot(int glob_idx)
{
	if (!vm_globals_ready() || glob_idx < 0 || glob_idx + 1 >= heap[0].page->nr_vars)
		return -1;
	/* In AIN v14, AIN_OPTION flag slot == 0 means Some, != 0 means None */
	if (global_get(glob_idx + 1).i != 0)
		return -1;
	int slot = global_get(glob_idx).i;
	return page_index_valid(slot) ? slot : -1;
}

static struct page *vm_dd_get_option_global_page(int glob_idx)
{
	int slot = vm_dd_get_option_global_slot(glob_idx);
	return slot > 0 ? vm_safe_get_page(slot) : NULL;
}

static int vm_dd_get_field_slot(struct page *page, const char *name1, const char *name2)
{
	union vm_value *v = vm_get_struct_member(page, name1, name2);
	if (!v) return -1;
	return page_index_valid(v->i) ? v->i : -1;
}

static int vm_dd_get_option_field_slot(struct page *page, const char *name, int *out_int, bool *out_has)
{
	if (out_has) *out_has = false;
	if (!ain || !page || page->type != STRUCT_PAGE)
		return -1;
	int midx = ain_find_struct_member_idx(page->index, name, NULL);
	if (midx < 0)
		return -1;
	int w = ain_type_slot_width(&ain->structures[page->index].members[midx].type);
	if (w < 2 || midx + w - 1 >= page->nr_vars)
		return -1;
	if (page->values[midx + w - 1].i != 0)
		return -1;
	if (out_has) *out_has = true;
	if (out_int) *out_int = page->values[midx].i;
	return page->values[midx].i;
}

static bool vm_dd_set_option_int_field(struct page *page, const char *name, int32_t val)
{
	if (!ain || !page || page->type != STRUCT_PAGE)
		return false;
	int midx = ain_find_struct_member_idx(page->index, name, NULL);
	if (midx < 0)
		return false;
	int w = ain_type_slot_width(&ain->structures[page->index].members[midx].type);
	if (w < 2 || midx + w - 1 >= page->nr_vars)
		return false;
	page->values[midx].i = val;
	page->values[midx + w - 1].i = 0; /* 0 = Some in AIN v14 option */
	return true;
}

static int vm_dd_array_stride(struct page *arr)
{
	if (!arr || arr->type != ARRAY_PAGE)
		return 1;
	if (arr->a_type == AIN_ARRAY) {
		int s = ain_type_slot_width(&arr->array.element_type);
		return s > 0 ? s : 1;
	}
	return 1;
}

static int vm_dd_array_len(struct page *arr)
{
	if (!arr || arr->type != ARRAY_PAGE || arr->nr_vars <= 0)
		return 0;
	int stride = vm_dd_array_stride(arr);
	return arr->nr_vars / stride;
}

static int vm_dd_array_elem_slot(struct page *arr, int idx)
{
	if (!arr || arr->type != ARRAY_PAGE || idx < 0)
		return -1;
	int stride = vm_dd_array_stride(arr);
	int off = idx * stride;
	if (off < 0 || off >= arr->nr_vars)
		return -1;
	int slot = arr->values[off].i;
	return page_index_valid(slot) ? slot : -1;
}

static char *vm_dd_read_utf8(int str_slot)
{
	if (str_slot <= 0 || !string_index_valid(str_slot))
		return strdup("");
	struct string *s = heap_get_string(str_slot);
	if (!s || s->size <= 0)
		return strdup("");
	return mod_sjis2utf_safe(s->text, (size_t)s->size);
}

static char *vm_dd_get_field_utf8(struct page *page, const char *name1, const char *name2)
{
	union vm_value *v = vm_get_struct_member(page, name1, name2);
	if (!v) return strdup("");
	return vm_dd_read_utf8(v->i);
}

static void vm_dd_call_void_0(int fno, int struct_slot)
{
	if (fno < 0 || !page_index_valid(struct_slot))
		return;
	bool saved_dg = dg_callbegin_pending;
	dg_callbegin_pending = false;
	vm_call(fno, struct_slot);
	dg_callbegin_pending = saved_dg;
}

static void vm_dd_call_void_1i(int fno, int struct_slot, int32_t arg0)
{
	if (fno < 0 || !page_index_valid(struct_slot))
		return;
	bool saved_dg = dg_callbegin_pending;
	dg_callbegin_pending = false;
	stack_push(arg0);
	vm_call(fno, struct_slot);
	dg_callbegin_pending = saved_dg;
}

static int32_t vm_dd_call_int_0(int fno, int struct_slot, int32_t def_val)
{
	if (fno < 0 || !page_index_valid(struct_slot))
		return def_val;
	bool saved_dg = dg_callbegin_pending;
	dg_callbegin_pending = false;
	vm_call(fno, struct_slot);
	int32_t res = stack_pop().i;
	dg_callbegin_pending = saved_dg;
	return res;
}

static int32_t vm_dd_call_int_1i(int fno, int struct_slot, int32_t arg0, int32_t def_val)
{
	if (fno < 0 || !page_index_valid(struct_slot))
		return def_val;
	bool saved_dg = dg_callbegin_pending;
	dg_callbegin_pending = false;
	stack_push(arg0);
	vm_call(fno, struct_slot);
	int32_t res = stack_pop().i;
	dg_callbegin_pending = saved_dg;
	return res;
}

static char *vm_dd_call_str_0_utf8(int fno, int struct_slot)
{
	if (fno < 0 || !page_index_valid(struct_slot))
		return strdup("");
	bool saved_dg = dg_callbegin_pending;
	dg_callbegin_pending = false;
	vm_call(fno, struct_slot);
	int ss = stack_pop().i;
	dg_callbegin_pending = saved_dg;
	char *u = vm_dd_read_utf8(ss);
	if (ss > 0 && string_index_valid(ss))
		heap_unref(ss);
	return u;
}

static void vm_dd_call_void_1s_gbk(int fno, int struct_slot, const char *gbk_str)
{
	if (fno < 0 || !page_index_valid(struct_slot) || !gbk_str)
		return;
	bool saved_dg = dg_callbegin_pending;
	dg_callbegin_pending = false;
	int ss = heap_alloc_string(make_string(gbk_str, strlen(gbk_str)));
	stack_push((int32_t)ss);
	vm_call(fno, struct_slot);
	dg_callbegin_pending = saved_dg;
}

static void vm_dd_call_void_si_gbk(int fno, int struct_slot, const char *gbk_str, int32_t arg1)
{
	if (fno < 0 || !page_index_valid(struct_slot) || !gbk_str)
		return;
	bool saved_dg = dg_callbegin_pending;
	dg_callbegin_pending = false;
	int ss = heap_alloc_string(make_string(gbk_str, strlen(gbk_str)));
	stack_push((int32_t)ss);
	stack_push(arg1);
	vm_call(fno, struct_slot);
	dg_callbegin_pending = saved_dg;
}

static void vm_dd_set_player_param(int pp_slot, int32_t target_val)
{
	struct page *pp = vm_safe_get_page(pp_slot);
	if (!pp || pp->type != STRUCT_PAGE)
		return;
	int32_t pmin = vm_get_field_int(pp, "m_min", NULL, 0);
	int32_t pmax = vm_get_field_int(pp, "m_max", NULL, 0);
	int32_t cur  = vm_get_field_int(pp, "m_current", NULL, 0);
	if (pmax < pmin)
		pmax = pmin;
	int32_t clamped = vm_dd_clamp(target_val, pmin, pmax);
	if (cur == clamped)
		return;
	if (s_dd_fno_pp_val_set >= 0) {
		vm_dd_call_void_1i(s_dd_fno_pp_val_set, pp_slot, clamped);
	} else {
		vm_set_field_int(pp, "m_current", NULL, clamped);
	}
}

static void vm_dd_fill_player_param_max(int pp_slot)
{
	struct page *pp = vm_safe_get_page(pp_slot);
	if (!pp || pp->type != STRUCT_PAGE)
		return;
	int32_t pmax = vm_get_field_int(pp, "m_max", NULL, 0);
	vm_dd_set_player_param(pp_slot, pmax);
}

static void vm_dd_set_money(int gc_slot, int32_t money)
{
	struct page *gc = vm_safe_get_page(gc_slot);
	if (!gc) return;
	int32_t clamped = vm_dd_clamp(money, 0, 999999999);
	if (vm_get_field_int(gc, "m_money", NULL, -1) == clamped)
		return;
	if (s_dd_fno_gc_money_set >= 0)
		vm_dd_call_void_1i(s_dd_fno_gc_money_set, gc_slot, clamped);
	else
		vm_set_field_int(gc, "m_money", NULL, clamped);
}

static void vm_dd_set_tp(int gc_slot, int32_t tp)
{
	struct page *gc = vm_safe_get_page(gc_slot);
	if (!gc) return;
	int32_t tp_max = vm_get_field_int(gc, "<TrainingPointMax>", NULL, 7);
	if (tp_max < 1) tp_max = 1;
	int32_t clamped = vm_dd_clamp(tp, 0, tp_max);
	if (vm_get_field_int(gc, "m_trainingPoint", NULL, -1) == clamped)
		return;
	if (s_dd_fno_gc_tp_set >= 0)
		vm_dd_call_void_1i(s_dd_fno_gc_tp_set, gc_slot, clamped);
	else
		vm_set_field_int(gc, "m_trainingPoint", NULL, clamped);
}

static void vm_dd_set_tp_max(int gc_slot, int32_t tp_max)
{
	struct page *gc = vm_safe_get_page(gc_slot);
	if (!gc) return;
	int32_t clamped = vm_dd_clamp(tp_max, 1, 99);
	if (s_dd_fno_gc_tp_max_set >= 0)
		vm_dd_call_void_1i(s_dd_fno_gc_tp_max_set, gc_slot, clamped);
	else
		vm_set_field_int(gc, "<TrainingPointMax>", NULL, clamped);
}

static int vm_dd_get_pill_max(void)
{
	if (vm_globals_ready() && s_dd_glob_pill_max >= 0 && s_dd_glob_pill_max < heap[0].page->nr_vars) {
		int v = global_get(s_dd_glob_pill_max).i;
		if (v > 0) return v;
	}
	return 255;
}

static int vm_dd_get_battle_context_slot(struct page *lg)
{
	if (lg) {
		int bc_slot = vm_dd_get_option_field_slot(lg, "m_battleContext", NULL, NULL);
		if (bc_slot > 0 && page_index_valid(bc_slot))
			return bc_slot;
	}
	struct page *stage = vm_dd_get_option_global_page(s_dd_glob_battle_stage);
	if (stage) {
		int bc_slot = vm_dd_get_field_slot(stage, "m_context", NULL);
		if (bc_slot > 0)
			return bc_slot;
	}
	return -1;
}

static struct page *vm_dd_get_worker_list_array(struct page *wc)
{
	struct page *id_arr = vm_get_field_page(wc, "m_workers", NULL);
	return vm_get_field_page(id_arr, "m_list", NULL);
}

static bool vm_dd_is_forbidden_item_utf8(const char *id_utf8)
{
	if (!id_utf8 || !id_utf8[0])
		return true;
	/* Never inject key items: 成员1..10 (sell party members), 证A..G, 钥匙* */
	if (!strncmp(id_utf8, "成员", strlen("成员")))
		return true;
	if (!strncmp(id_utf8, "证", strlen("证")))
		return true;
	if (!strncmp(id_utf8, "钥匙", strlen("钥匙")))
		return true;
	return false;
}

static void vm_dd_stock_ensure_count(int stock_slot, const char *id_gbk, int32_t min_count)
{
	struct page *stock = vm_safe_get_page(stock_slot);
	if (!stock || !id_gbk || !id_gbk[0] || min_count <= 0)
		return;
	int32_t max_val = vm_get_field_int(stock, "m_max", NULL, 9999);
	if (min_count > max_val)
		min_count = max_val;
	struct page *id_arr = vm_get_field_page(stock, "m_count", NULL);
	struct page *list   = vm_get_field_page(id_arr, "m_list", NULL);
	int n = vm_dd_array_len(list);
	for (int i = 0; i < n; i++) {
		struct page *sc = vm_safe_get_page(vm_dd_array_elem_slot(list, i));
		if (!sc) continue;
		union vm_value *id_v = vm_get_struct_member(sc, "Id", NULL);
		if (!id_v || !string_index_valid(id_v->i)) continue;
		struct string *s = heap_get_string(id_v->i);
		if (s && !strcmp(s->text, id_gbk)) {
			int32_t cur = vm_get_field_int(sc, "Count", NULL, 0);
			if (cur < min_count)
				vm_set_field_int(sc, "Count", NULL, min_count);
			return;
		}
	}
	if (s_dd_fno_stock_add >= 0) {
		vm_dd_call_void_si_gbk(s_dd_fno_stock_add, stock_slot, id_gbk, min_count);
	}
}

static void vm_dd_apply_battle_side(struct page *bc, int side,
                                    bool ally_hp, bool ally_mp, bool ally_vp,
                                    bool ally_cleanse, bool ally_boost,
                                    bool enemy_1hp, bool enemy_weaken)
{
	if (!bc) return;
	const char *arr_fields[2] = { "m_players", "m_stocks" };
	for (int a = 0; a < 2; a++) {
		struct page *side_arr = vm_get_field_page(bc, arr_fields[a], NULL);
		int bpc_slot = vm_dd_array_elem_slot(side_arr, side);
		struct page *bpc = vm_safe_get_page(bpc_slot);
		struct page *bp_arr = vm_get_field_page(bpc, "m_player", NULL);
		int n = vm_dd_array_len(bp_arr);
		for (int i = 0; i < n; i++) {
			int bp_slot = vm_dd_array_elem_slot(bp_arr, i);
			struct page *bp = vm_safe_get_page(bp_slot);
			if (!bp) continue;
			/* m_player is 2-slot wrap<iface_wrap<IBattlePlayer>>; slot 0 is concrete page */
			union vm_value *mp_v = vm_get_struct_member(bp, "m_player", NULL);
			struct page *ent = mp_v ? vm_safe_get_page(mp_v->i) : NULL;
			if (!ent) continue;

			int hp_slot = vm_dd_get_field_slot(ent, "m_hp", NULL);
			int mp_slot = vm_dd_get_field_slot(ent, "m_mp", NULL);
			int vp_slot = vm_dd_get_field_slot(ent, "m_voltage", NULL);

			if (side == 0) {
				if (ally_hp && hp_slot > 0) vm_dd_fill_player_param_max(hp_slot);
				if (ally_mp && mp_slot > 0) vm_dd_fill_player_param_max(mp_slot);
				if (ally_vp && vp_slot > 0) vm_dd_fill_player_param_max(vp_slot);
				if (ally_cleanse || ally_boost) {
					struct page *bec = vm_get_field_page(bp, "m_buffEffects", NULL);
					struct page *barr = vm_get_field_page(bec, "m_buff", NULL);
					int bn = vm_dd_array_len(barr);
					for (int b = 0; b < bn; b++) {
						struct page *bf = vm_safe_get_page(vm_dd_array_elem_slot(barr, b));
						if (!bf) continue;
						union vm_value *pv = vm_get_struct_member(bf, "Param", NULL);
						if (pv && pv->f < 0.0f)
							pv->f = -pv->f;
						if (ally_boost && vm_get_field_int(bf, "<Turn>", NULL, 0) < 9)
							vm_set_field_int(bf, "<Turn>", NULL, 9);
					}
				}
				if (ally_boost) {
					if (vm_get_field_int(bp, "<AlterTechnicPer>", NULL, 0) < 200)
						vm_set_field_int(bp, "<AlterTechnicPer>", NULL, 200);
				}
			} else {
				if (enemy_1hp && hp_slot > 0) {
					struct page *hpp = vm_safe_get_page(hp_slot);
					if (hpp && vm_get_field_int(hpp, "m_current", NULL, 0) > 1)
						vm_dd_set_player_param(hp_slot, 1);
				}
				if (enemy_weaken) {
					if (mp_slot > 0) {
						struct page *mpp = vm_safe_get_page(mp_slot);
						int32_t mmin = mpp ? vm_get_field_int(mpp, "m_min", NULL, 0) : 0;
						vm_dd_set_player_param(mp_slot, mmin);
					}
					if (vp_slot > 0) {
						vm_dd_set_player_param(vp_slot, 0);
					}
					vm_set_field_int(bp, "<AlterTechnicPer>", NULL, -100);
					int bec_slot = vm_dd_get_field_slot(bp, "m_buffEffects", NULL);
					struct page *bec = vm_safe_get_page(bec_slot);
					struct page *barr = vm_get_field_page(bec, "m_buff", NULL);
					if (bec_slot > 0 && vm_dd_array_len(barr) > 0 && s_dd_fno_buff_clear >= 0) {
						vm_dd_call_void_0(s_dd_fno_buff_clear, bec_slot);
					}
				}
			}
		}
	}
}

static bool vm_dd_is_on_title_screen(void)
{
	if (!ain || !ain->functions)
		return false;
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		int fno = call_stack[i].fno;
		if (fno >= 0 && fno < ain->nr_functions && ain->functions[fno].name) {
			const char *fn = ain->functions[fno].name;
			if (!strncmp(fn, "LocalGame@", 10) ||
			    !strncmp(fn, "BattleSceneSet@", 15) ||
			    !strncmp(fn, "DungeonSceneSet@", 16) ||
			    !strncmp(fn, "WorkSceneSet@", 13))
				return false;
			if (!strncmp(fn, "SceneLogo@", 10) ||
			    !strncmp(fn, "SceneTitle@", 11) ||
			    !strncmp(fn, "TitleSceneSet@", 14))
				return true;
		}
	}
	return false;
}

static void vm_enforce_dohna_locks(int dd_mask, uint32_t frame_tick)
{
	if (!dd_mask || !ain || vm_dd_is_on_title_screen())
		return;
	vm_inspect_dohna_ain();

	struct page *lg = vm_dd_get_option_global_page(s_dd_glob_static_game);
	if (!lg)
		return;
	int gc_slot = vm_dd_get_field_slot(lg, "m_context", NULL);
	struct page *gc = vm_safe_get_page(gc_slot);
	if (!gc)
		return;

	if (dd_mask & LOCK_DD_INFINITE_MONEY) {
		if (vm_get_field_int(gc, "m_money", NULL, 0) < 99999999)
			vm_dd_set_money(gc_slot, 99999999);
	}
	if (dd_mask & LOCK_DD_INFINITE_TP) {
		int32_t tp_max = vm_get_field_int(gc, "<TrainingPointMax>", NULL, 7);
		if (tp_max < 7) {
			tp_max = 7;
			vm_dd_set_tp_max(gc_slot, tp_max);
		}
		if (vm_get_field_int(gc, "m_trainingPoint", NULL, 0) < tp_max)
			vm_dd_set_tp(gc_slot, tp_max);
	}

	bool lock_hp = (dd_mask & LOCK_DD_ALLY_MAX_HP) != 0;
	bool lock_mp = (dd_mask & LOCK_DD_ALLY_MAX_MP) != 0;
	bool lock_vp = (dd_mask & LOCK_DD_ALLY_MAX_VP) != 0;
	if (lock_hp || lock_mp || lock_vp) {
		struct page *pc = vm_get_field_page(lg, "m_players", NULL);
		struct page *parr = vm_get_field_page(pc, "m_players", NULL);
		int pn = vm_dd_array_len(parr);
		for (int i = 0; i < pn; i++) {
			struct page *pl = vm_safe_get_page(vm_dd_array_elem_slot(parr, i));
			if (!pl || !vm_get_field_int(pl, "<IsJoin>", NULL, 0))
				continue;
			if (lock_hp) vm_dd_fill_player_param_max(vm_dd_get_field_slot(pl, "m_hp", NULL));
			if (lock_mp) vm_dd_fill_player_param_max(vm_dd_get_field_slot(pl, "m_mp", NULL));
			if (lock_vp) vm_dd_fill_player_param_max(vm_dd_get_field_slot(pl, "m_voltage", NULL));
		}
	}

	int bc_slot = vm_dd_get_battle_context_slot(lg);
	struct page *bc = bc_slot > 0 ? vm_safe_get_page(bc_slot) : NULL;
	if (bc) {
		if (lock_hp || lock_mp || lock_vp) {
			vm_dd_apply_battle_side(bc, 0, lock_hp, lock_mp, lock_vp, false, false, false, false);
		}
		if (dd_mask & LOCK_DD_ONE_HIT_KILL) {
			vm_dd_apply_battle_side(bc, 1, false, false, false, false, false, true, false);
		}
	}

	if (dd_mask & LOCK_DD_WORKER_BEST_COND) {
		int pill_max = vm_dd_get_pill_max();
		const char *wfields[2] = { "m_worker", "m_garage" };
		for (int s = 0; s < 2; s++) {
			struct page *wc = vm_get_field_page(lg, wfields[s], NULL);
			struct page *warr = vm_dd_get_worker_list_array(wc);
			int wn = vm_dd_array_len(warr);
			for (int i = 0; i < wn; i++) {
				struct page *w = vm_safe_get_page(vm_dd_array_elem_slot(warr, i));
				if (!w) continue;
				if (vm_get_field_int(w, "m_mental", NULL, 0) < 9999)
					vm_set_field_int(w, "m_mental", NULL, 9999);
				if (vm_get_field_int(w, "m_pill", NULL, 0) < pill_max)
					vm_set_field_int(w, "m_pill", NULL, pill_max);
				if (vm_get_field_int(w, "<IsInsane>", NULL, 0) != 0)
					vm_set_field_int(w, "<IsInsane>", NULL, 0);
				if (vm_get_field_int(w, "<IsPregnance>", NULL, 0) != 0)
					vm_set_field_int(w, "<IsPregnance>", NULL, 0);
			}
		}
	}

	if ((dd_mask & LOCK_DD_WORK_FREEZE_TIMER) && (frame_tick % 10 == 1) &&
	    s_dd_struc_decision_timer >= 0 && s_dd_fno_dt_restart >= 0 && !bc) {
		for (size_t s = 1; s < heap_size; s++) {
			if (heap[s].ref <= 0 || heap[s].type != VM_PAGE)
				continue;
			struct page *p = heap[s].page;
			if (!p || p->type != STRUCT_PAGE || p->index != s_dd_struc_decision_timer)
				continue;
			/* DecisionTimer::State: Running == 1 */
			if (vm_get_field_int(p, "m_state", NULL, 0) == 1) {
				union vm_value *rem = vm_get_struct_member(p, "m_remain", NULL);
				if (!rem || rem->f < 0.98f) {
					vm_dd_call_void_0(s_dd_fno_dt_restart, (int)s);
					if (rem) rem->f = 1.0f;
				}
			}
		}
	}
}

static bool vm_dd_weapon_exists(struct page *res, const char *weapon_gbk)
{
	if (!res || !weapon_gbk || !weapon_gbk[0])
		return false;
	struct page *wc = vm_get_field_page(res, "m_weapons", NULL);
	struct page *id_arr = vm_get_field_page(wc, "m_weapon", NULL);
	struct page *wlist = vm_get_field_page(id_arr, "m_list", NULL);
	int n = vm_dd_array_len(wlist);
	for (int i = 0; i < n; i++) {
		struct page *wp = vm_safe_get_page(vm_dd_array_elem_slot(wlist, i));
		if (!wp) continue;
		union vm_value *id_v = vm_get_struct_member(wp, "Id", NULL);
		if (!id_v || !string_index_valid(id_v->i)) continue;
		struct string *s = heap_get_string(id_v->i);
		if (s && !strcmp(s->text, weapon_gbk))
			return true;
	}
	return false;
}

static bool vm_dd_is_unjoined_guest_player(struct page *pl)
{
	if (!pl)
		return false;
	if (vm_get_field_int(pl, "<IsJoin>", NULL, 0) != 0)
		return false;
	struct page *pex = vm_get_field_page(pl, "m_param", NULL);
	union vm_value *id_v = vm_get_struct_member(pex, "m_id", NULL);
	if (!id_v || !string_index_valid(id_v->i))
		return false;
	struct string *s = heap_get_string(id_v->i);
	return s && s->text[0] == 'G';
}

static void vm_dd_apply_player_edit(int pl_slot, cJSON *pe)
{
	struct page *pl = vm_safe_get_page(pl_slot);
	if (!pl || !pe)
		return;
	cJSON *it;
	bool need_recalc = false;

	if ((it = cJSON_GetObjectItem(pe, "joined")) && cJSON_IsNumber(it)) {
		vm_set_field_int(pl, "<IsJoin>", NULL, it->valueint ? 1 : 0);
	}
	if ((it = cJSON_GetObjectItem(pe, "level")) && cJSON_IsNumber(it)) {
		int lv = vm_dd_clamp(it->valueint, 1, 99);
		if (lv != vm_get_field_int(pl, "m_level", NULL, 1)) {
			if (s_dd_fno_pl_debug_lv_set >= 0) {
				vm_dd_call_void_1i(s_dd_fno_pl_debug_lv_set, pl_slot, lv);
			} else {
				vm_set_field_int(pl, "m_level", NULL, lv);
				need_recalc = true;
			}
		}
	}
	if ((it = cJSON_GetObjectItem(pe, "addExp")) && cJSON_IsNumber(it) && it->valueint > 0) {
		if (s_dd_fno_pl_up_level >= 0)
			vm_dd_call_int_1i(s_dd_fno_pl_up_level, pl_slot, it->valueint, 0);
		else
			vm_set_field_int(pl, "m_exp", NULL, vm_get_field_int(pl, "m_exp", NULL, 0) + it->valueint);
	}
	if ((it = cJSON_GetObjectItem(pe, "exp")) && cJSON_IsNumber(it)) {
		vm_set_field_int(pl, "m_exp", NULL, vm_dd_clamp(it->valueint, 0, 999999999));
	}

	if ((it = cJSON_GetObjectItem(pe, "alterPow")) && cJSON_IsNumber(it)) {
		vm_set_field_int(pl, "<AlterPower>", NULL, vm_dd_clamp(it->valueint, 0, 99999));
	}
	if ((it = cJSON_GetObjectItem(pe, "alterTec")) && cJSON_IsNumber(it)) {
		vm_set_field_int(pl, "<AlterTechnic>", NULL, vm_dd_clamp(it->valueint, 0, 99999));
	}
	if ((it = cJSON_GetObjectItem(pe, "alterSpd")) && cJSON_IsNumber(it)) {
		vm_set_field_int(pl, "<AlterSpeed>", NULL, vm_dd_clamp(it->valueint, 0, 99999));
	}
	if ((it = cJSON_GetObjectItem(pe, "alterHp")) && cJSON_IsNumber(it)) {
		vm_set_field_int(pl, "<AlterHp>", NULL, vm_dd_clamp(it->valueint, 0, 999999));
		need_recalc = true;
	}
	if ((it = cJSON_GetObjectItem(pe, "alterMp")) && cJSON_IsNumber(it)) {
		vm_set_field_int(pl, "<AlterMp>", NULL, vm_dd_clamp(it->valueint, 0, 9999));
		need_recalc = true;
	}
	if ((it = cJSON_GetObjectItem(pe, "alterVp")) && cJSON_IsNumber(it)) {
		vm_set_field_int(pl, "<AlterVoltage>", NULL, vm_dd_clamp(it->valueint, 0, 9999));
		need_recalc = true;
	}
	if (need_recalc && s_dd_fno_pl_recalc >= 0) {
		vm_dd_call_void_1i(s_dd_fno_pl_recalc, pl_slot, 0);
	}

	if ((it = cJSON_GetObjectItem(pe, "hp")) && cJSON_IsNumber(it)) {
		vm_dd_set_player_param(vm_dd_get_field_slot(pl, "m_hp", NULL), it->valueint);
	}
	if ((it = cJSON_GetObjectItem(pe, "mp")) && cJSON_IsNumber(it)) {
		vm_dd_set_player_param(vm_dd_get_field_slot(pl, "m_mp", NULL), it->valueint);
	}
	if ((it = cJSON_GetObjectItem(pe, "vp")) && cJSON_IsNumber(it)) {
		vm_dd_set_player_param(vm_dd_get_field_slot(pl, "m_voltage", NULL), it->valueint);
	}
	if ((it = cJSON_GetObjectItem(pe, "feel")) && cJSON_IsNumber(it)) {
		struct page *fe = vm_get_field_page(pl, "m_feelEvent", NULL);
		if (fe)
			vm_set_field_int(fe, "m_feel", NULL, vm_dd_clamp(it->valueint, 0, 999999));
	}
}

static void vm_dd_apply_worker_edit(struct page *w, cJSON *we)
{
	if (!w || !we)
		return;
	cJSON *it;
	int pill_max = vm_dd_get_pill_max();
	if ((it = cJSON_GetObjectItem(we, "rank")) && cJSON_IsNumber(it))
		vm_set_field_int(w, "m_rank", NULL, vm_dd_clamp(it->valueint, 0, 9));
	if ((it = cJSON_GetObjectItem(we, "rarity")) && cJSON_IsNumber(it))
		vm_set_field_int(w, "<Rarity>", NULL, vm_dd_clamp(it->valueint, 0, 5));
	if ((it = cJSON_GetObjectItem(we, "looks")) && cJSON_IsNumber(it))
		vm_set_field_int(w, "m_looks", NULL, vm_dd_clamp(it->valueint, 0, 9999));
	if ((it = cJSON_GetObjectItem(we, "technic")) && cJSON_IsNumber(it))
		vm_set_field_int(w, "m_technic", NULL, vm_dd_clamp(it->valueint, 0, 9999));
	if ((it = cJSON_GetObjectItem(we, "mental")) && cJSON_IsNumber(it))
		vm_set_field_int(w, "m_mental", NULL, vm_dd_clamp(it->valueint, 0, 9999));
	if ((it = cJSON_GetObjectItem(we, "pill")) && cJSON_IsNumber(it))
		vm_set_field_int(w, "m_pill", NULL, vm_dd_clamp(it->valueint, 0, pill_max));
	if ((it = cJSON_GetObjectItem(we, "virgin")) && cJSON_IsNumber(it))
		vm_set_field_int(w, "<IsVirgin>", NULL, it->valueint ? 1 : 0);
	if ((it = cJSON_GetObjectItem(we, "pregnance")) && cJSON_IsNumber(it))
		vm_set_field_int(w, "<IsPregnance>", NULL, it->valueint ? 1 : 0);
	if ((it = cJSON_GetObjectItem(we, "insane")) && cJSON_IsNumber(it))
		vm_set_field_int(w, "<IsInsane>", NULL, it->valueint ? 1 : 0);
}

static void vm_apply_ui_scale_from_json(cJSON *act, bool *persist_cfg, int *save_r10_mask)
{
	cJSON *it;
	pthread_mutex_lock(&g_mod_mutex);
	if ((it = cJSON_GetObjectItem(act, "fontScale")) && cJSON_IsNumber(it)) {
		int fp = clamp_font_scale_pct(it->valueint);
		if (g_font_scale_pct != fp || !g_has_ui_scale_init) {
			g_font_scale_pct = fp;
			g_has_ui_scale_init = true;
			parts_set_modifier_font_scale(fp);
			if (persist_cfg) *persist_cfg = true;
		}
	}
	if ((it = cJSON_GetObjectItem(act, "scrollbarScale")) && cJSON_IsNumber(it)) {
		int sp = clamp_scrollbar_scale_pct(it->valueint);
		if (g_scrollbar_scale_pct != sp || !g_has_ui_scale_init) {
			g_scrollbar_scale_pct = sp;
			g_has_ui_scale_init = true;
			parts_set_modifier_scrollbar_scale(sp);
			if (persist_cfg) *persist_cfg = true;
		}
	}
	if (save_r10_mask)
		*save_r10_mask = g_live_lock_mask;
	pthread_mutex_unlock(&g_mod_mutex);
}

static void vm_execute_dohna_action(const char *json_str)
{
	if (!json_str || !json_str[0])
		return;
	cJSON *act = cJSON_Parse(json_str);
	if (!act)
		return;

	vm_inspect_dohna_ain();
	cJSON *it;

	/* Optional UI scale & Dohna lock mask updates */
	bool persist_cfg = false;
	int save_r10_mask = 0;
	pthread_mutex_lock(&g_mod_mutex);
	if ((it = cJSON_GetObjectItem(act, "setDohnaLockMask")) && cJSON_IsNumber(it)) {
		g_dohna_lock_mask = it->valueint;
		g_has_dohna_lock_mask_init = true;
		persist_cfg = true;
	}
	pthread_mutex_unlock(&g_mod_mutex);
	vm_apply_ui_scale_from_json(act, &persist_cfg, &save_r10_mask);
	if (persist_cfg)
		persist_modifier_config_from_native(save_r10_mask);

	struct page *lg = vm_dd_get_option_global_page(s_dd_glob_static_game);
	struct page *res = vm_dd_get_option_global_page(s_dd_glob_static_res);
	if (!lg) {
		cJSON_Delete(act);
		return;
	}
	int gc_slot = vm_dd_get_field_slot(lg, "m_context", NULL);
	struct page *gc = vm_safe_get_page(gc_slot);
	int bc_slot = vm_dd_get_battle_context_slot(lg);
	struct page *bc = bc_slot > 0 ? vm_safe_get_page(bc_slot) : NULL;

	/* 1. Battle quick commands */
	if ((it = cJSON_GetObjectItem(act, "ddBattle1HpEnemies")) && cJSON_IsTrue(it) && bc) {
		vm_dd_apply_battle_side(bc, 1, false, false, false, false, false, true, false);
	}
	if ((it = cJSON_GetObjectItem(act, "ddBattleWeakenEnemies")) && cJSON_IsTrue(it) && bc) {
		vm_dd_apply_battle_side(bc, 1, false, false, false, false, false, false, true);
	}
	if ((it = cJSON_GetObjectItem(act, "ddBattleHealAllies")) && cJSON_IsTrue(it)) {
		if (bc)
			vm_dd_apply_battle_side(bc, 0, true, true, true, true, false, false, false);
		struct page *parr = vm_get_field_page(vm_get_field_page(lg, "m_players", NULL), "m_players", NULL);
		int pn = vm_dd_array_len(parr);
		for (int i = 0; i < pn; i++) {
			struct page *pl = vm_safe_get_page(vm_dd_array_elem_slot(parr, i));
			if (!pl || !vm_get_field_int(pl, "<IsJoin>", NULL, 0)) continue;
			vm_dd_fill_player_param_max(vm_dd_get_field_slot(pl, "m_hp", NULL));
			vm_dd_fill_player_param_max(vm_dd_get_field_slot(pl, "m_mp", NULL));
			vm_dd_fill_player_param_max(vm_dd_get_field_slot(pl, "m_voltage", NULL));
		}
	}
	if ((it = cJSON_GetObjectItem(act, "ddBattleBoostAllies")) && cJSON_IsTrue(it)) {
		if (bc)
			vm_dd_apply_battle_side(bc, 0, true, true, true, true, true, false, false);
	}

	/* 2. Common one-click cheats */
	if ((it = cJSON_GetObjectItem(act, "ddCheatAddMoney")) && cJSON_IsNumber(it) && gc) {
		int64_t cur = vm_get_field_int(gc, "m_money", NULL, 0);
		int64_t next = cur + (int64_t)it->valueint;
		if (next > 999999999LL) next = 999999999LL;
		if (next < 0) next = 0;
		vm_dd_set_money(gc_slot, (int32_t)next);
	}
	if ((it = cJSON_GetObjectItem(act, "ddCheatMaxMoney")) && cJSON_IsTrue(it) && gc) {
		vm_dd_set_money(gc_slot, 999999999);
		int32_t tp_max = vm_get_field_int(gc, "<TrainingPointMax>", NULL, 7);
		if (tp_max < 7) {
			tp_max = 7;
			vm_dd_set_tp_max(gc_slot, tp_max);
		}
		vm_dd_set_tp(gc_slot, tp_max);
	}
	if ((it = cJSON_GetObjectItem(act, "ddCheatExpandMansion")) && cJSON_IsTrue(it) && gc) {
		int wc_slot = vm_dd_get_field_slot(lg, "m_worker", NULL);
		if (wc_slot > 0 && s_dd_fno_wc_set_limit >= 0) {
			vm_dd_call_void_1i(s_dd_fno_wc_set_limit, wc_slot, 10);
		}
		int32_t tp_max = vm_get_field_int(gc, "<TrainingPointMax>", NULL, 7);
		if (tp_max < 10) tp_max = 10;
		vm_dd_set_tp_max(gc_slot, tp_max);
		vm_dd_set_tp(gc_slot, tp_max);
	}
	if ((it = cJSON_GetObjectItem(act, "ddCheatHeldItems99")) && cJSON_IsTrue(it)) {
		struct page *is_p = vm_get_field_page(lg, "m_itemStock", NULL);
		struct page *st_i = vm_get_field_page(is_p, "m_list", NULL);
		struct page *il   = vm_get_field_page(vm_get_field_page(st_i, "m_count", NULL), "m_list", NULL);
		int ni = vm_dd_array_len(il);
		for (int i = 0; i < ni; i++) {
			struct page *sc = vm_safe_get_page(vm_dd_array_elem_slot(il, i));
			if (!sc) continue;
			char *id_u = vm_dd_get_field_utf8(sc, "Id", NULL);
			bool skip = vm_dd_is_forbidden_item_utf8(id_u);
			free(id_u);
			if (skip) continue;
			if (vm_get_field_int(sc, "Count", NULL, 0) < 99)
				vm_set_field_int(sc, "Count", NULL, 99);
		}
		struct page *bs_p = vm_get_field_page(lg, "m_badgeStock", NULL);
		struct page *st_b = vm_get_field_page(bs_p, "m_badge", NULL);
		struct page *bl   = vm_get_field_page(vm_get_field_page(st_b, "m_count", NULL), "m_list", NULL);
		int nb = vm_dd_array_len(bl);
		for (int i = 0; i < nb; i++) {
			struct page *sc = vm_safe_get_page(vm_dd_array_elem_slot(bl, i));
			if (sc && vm_get_field_int(sc, "Count", NULL, 0) < 99)
				vm_set_field_int(sc, "Count", NULL, 99);
		}
	}
	if ((it = cJSON_GetObjectItem(act, "ddCheatAllItemsBadges99")) && cJSON_IsTrue(it) && res) {
		struct page *is_p = vm_get_field_page(lg, "m_itemStock", NULL);
		int istock_slot = vm_dd_get_field_slot(is_p, "m_list", NULL);
		struct page *ic = vm_get_field_page(res, "m_items", NULL);
		struct page *iarr = vm_get_field_page(ic, "m_items", NULL);
		int ni = vm_dd_array_len(iarr);
		for (int i = 0; i < ni; i++) {
			int item_slot = vm_dd_array_elem_slot(iarr, i);
			struct page *item = vm_safe_get_page(item_slot);
			if (!item) continue;
			union vm_value *id_v = vm_get_struct_member(item, "<Id>", NULL);
			if (!id_v || !string_index_valid(id_v->i)) continue;
			struct string *id_s = heap_get_string(id_v->i);
			if (!id_s || !id_s->text[0]) continue;
			char *id_u = vm_dd_read_utf8(id_v->i);
			bool allow = false;
			if (!vm_dd_is_forbidden_item_utf8(id_u)) {
				if (!strcmp(id_u, "重新挑战")) {
					allow = true;
				} else if (s_dd_fno_item_type_get >= 0) {
					int itype = vm_dd_call_int_0(s_dd_fno_item_type_get, item_slot, -1);
					allow = (itype >= 0 && itype <= 5);
				} else {
					allow = true;
				}
			}
			free(id_u);
			if (allow && istock_slot > 0) {
				/* Copy GBK text because vm_call may grow heap */
				char *gbk_copy = strdup(id_s->text);
				if (gbk_copy) {
					vm_dd_stock_ensure_count(istock_slot, gbk_copy, 99);
					free(gbk_copy);
				}
			}
		}

		struct page *bs_p = vm_get_field_page(lg, "m_badgeStock", NULL);
		int bstock_slot = vm_dd_get_field_slot(bs_p, "m_badge", NULL);
		struct page *bc_res = vm_get_field_page(res, "m_badges", NULL);
		struct page *barr = vm_get_field_page(bc_res, "m_badge", NULL);
		int nb = vm_dd_array_len(barr);
		for (int i = 0; i < nb; i++) {
			struct page *badge = vm_safe_get_page(vm_dd_array_elem_slot(barr, i));
			if (!badge) continue;
			union vm_value *id_v = vm_get_struct_member(badge, "m_id", NULL);
			if (!id_v || !string_index_valid(id_v->i)) continue;
			struct string *id_s = heap_get_string(id_v->i);
			if (!id_s || !id_s->text[0]) continue;
			/* Skip unused dummy badge HP6 ("最大HP提升nnnnn") */
			if (!strcmp(id_s->text, "HP6"))
				continue;
			if (bstock_slot > 0) {
				char *gbk_copy = strdup(id_s->text);
				if (gbk_copy) {
					vm_dd_stock_ensure_count(bstock_slot, gbk_copy, 99);
					free(gbk_copy);
				}
			}
		}
	}
	if ((it = cJSON_GetObjectItem(act, "ddCheatPartyFullHeal")) && cJSON_IsTrue(it)) {
		struct page *parr = vm_get_field_page(vm_get_field_page(lg, "m_players", NULL), "m_players", NULL);
		int pn = vm_dd_array_len(parr);
		for (int i = 0; i < pn; i++) {
			struct page *pl = vm_safe_get_page(vm_dd_array_elem_slot(parr, i));
			if (!pl) continue;
			vm_dd_fill_player_param_max(vm_dd_get_field_slot(pl, "m_hp", NULL));
			vm_dd_fill_player_param_max(vm_dd_get_field_slot(pl, "m_mp", NULL));
			vm_dd_fill_player_param_max(vm_dd_get_field_slot(pl, "m_voltage", NULL));
		}
		if (bc)
			vm_dd_apply_battle_side(bc, 0, true, true, true, true, false, false, false);
	}
	if ((it = cJSON_GetObjectItem(act, "ddCheatPartyLv99")) && cJSON_IsTrue(it)) {
		struct page *parr = vm_get_field_page(vm_get_field_page(lg, "m_players", NULL), "m_players", NULL);
		int pn = vm_dd_array_len(parr);
		for (int i = 0; i < pn; i++) {
			int pl_slot = vm_dd_array_elem_slot(parr, i);
			struct page *pl = vm_safe_get_page(pl_slot);
			if (!pl || !vm_get_field_int(pl, "<IsJoin>", NULL, 0))
				continue;
			if (s_dd_fno_pl_debug_lv_set >= 0)
				vm_dd_call_void_1i(s_dd_fno_pl_debug_lv_set, pl_slot, 99);
			else
				vm_set_field_int(pl, "m_level", NULL, 99);
			vm_dd_fill_player_param_max(vm_dd_get_field_slot(pl, "m_hp", NULL));
			vm_dd_fill_player_param_max(vm_dd_get_field_slot(pl, "m_mp", NULL));
			vm_dd_fill_player_param_max(vm_dd_get_field_slot(pl, "m_voltage", NULL));
		}
	}
	if ((it = cJSON_GetObjectItem(act, "ddCheatPartySuperBoost")) && cJSON_IsTrue(it)) {
		struct page *parr = vm_get_field_page(vm_get_field_page(lg, "m_players", NULL), "m_players", NULL);
		int pn = vm_dd_array_len(parr);
		for (int i = 0; i < pn; i++) {
			int pl_slot = vm_dd_array_elem_slot(parr, i);
			struct page *pl = vm_safe_get_page(pl_slot);
			if (!pl || !vm_get_field_int(pl, "<IsJoin>", NULL, 0))
				continue;
			if (vm_get_field_int(pl, "<AlterPower>", NULL, 0) < 9999)
				vm_set_field_int(pl, "<AlterPower>", NULL, 9999);
			if (vm_get_field_int(pl, "<AlterTechnic>", NULL, 0) < 9999)
				vm_set_field_int(pl, "<AlterTechnic>", NULL, 9999);
			if (vm_get_field_int(pl, "<AlterSpeed>", NULL, 0) < 3000)
				vm_set_field_int(pl, "<AlterSpeed>", NULL, 3000);
			if (vm_get_field_int(pl, "<AlterHp>", NULL, 0) < 20000)
				vm_set_field_int(pl, "<AlterHp>", NULL, 20000);
			if (vm_get_field_int(pl, "<AlterMp>", NULL, 0) < 100)
				vm_set_field_int(pl, "<AlterMp>", NULL, 100);
			if (vm_get_field_int(pl, "<AlterVoltage>", NULL, 0) < 100)
				vm_set_field_int(pl, "<AlterVoltage>", NULL, 100);
			struct page *fe = vm_get_field_page(pl, "m_feelEvent", NULL);
			if (fe && vm_get_field_int(fe, "m_feel", NULL, 0) < 9999)
				vm_set_field_int(fe, "m_feel", NULL, 9999);
			if (s_dd_fno_pl_recalc >= 0)
				vm_dd_call_void_1i(s_dd_fno_pl_recalc, pl_slot, 0);
			vm_dd_fill_player_param_max(vm_dd_get_field_slot(pl, "m_hp", NULL));
			vm_dd_fill_player_param_max(vm_dd_get_field_slot(pl, "m_mp", NULL));
			vm_dd_fill_player_param_max(vm_dd_get_field_slot(pl, "m_voltage", NULL));
		}
	}
	if ((it = cJSON_GetObjectItem(act, "ddCheatPartyRank6Weapons")) && cJSON_IsTrue(it) && res) {
		struct page *parr = vm_get_field_page(vm_get_field_page(lg, "m_players", NULL), "m_players", NULL);
		int pn = vm_dd_array_len(parr);
		for (int i = 0; i < pn; i++) {
			int pl_slot = vm_dd_array_elem_slot(parr, i);
			struct page *pl = vm_safe_get_page(pl_slot);
			if (!pl) continue;
			struct page *pex = vm_get_field_page(pl, "m_param", NULL);
			union vm_value *id_v = vm_get_struct_member(pex, "m_id", NULL);
			if (!id_v || !string_index_valid(id_v->i)) continue;
			struct string *id_s = heap_get_string(id_v->i);
			if (!id_s || !id_s->text[0]) continue;
			char wbuf[128];
			snprintf(wbuf, sizeof(wbuf), "%s6", id_s->text);
			if (vm_dd_weapon_exists(res, wbuf)) {
				if (s_dd_fno_pl_weapon_set >= 0) {
					vm_dd_call_void_1s_gbk(s_dd_fno_pl_weapon_set, pl_slot, wbuf);
				} else {
					union vm_value *wv = vm_get_struct_member(pl, "m_weaponId", NULL);
					if (wv) vm_set_string_slot(wv->i, wbuf);
				}
				if (s_dd_fno_pl_recalc >= 0)
					vm_dd_call_void_1i(s_dd_fno_pl_recalc, pl_slot, 0);
			}
		}
	}
	if ((it = cJSON_GetObjectItem(act, "ddCheatWorkersHeal")) && cJSON_IsTrue(it)) {
		int pill_max = vm_dd_get_pill_max();
		const char *wfields[2] = { "m_worker", "m_garage" };
		for (int s = 0; s < 2; s++) {
			struct page *warr = vm_dd_get_worker_list_array(vm_get_field_page(lg, wfields[s], NULL));
			int wn = vm_dd_array_len(warr);
			for (int i = 0; i < wn; i++) {
				struct page *w = vm_safe_get_page(vm_dd_array_elem_slot(warr, i));
				if (!w) continue;
				vm_set_field_int(w, "m_mental", NULL, 9999);
				vm_set_field_int(w, "m_pill", NULL, pill_max);
				vm_set_field_int(w, "<IsInsane>", NULL, 0);
				vm_set_field_int(w, "<IsPregnance>", NULL, 0);
			}
		}
	}
	if ((it = cJSON_GetObjectItem(act, "ddCheatWorkersMaxStats")) && cJSON_IsTrue(it)) {
		int pill_max = vm_dd_get_pill_max();
		const char *wfields[2] = { "m_worker", "m_garage" };
		for (int s = 0; s < 2; s++) {
			struct page *warr = vm_dd_get_worker_list_array(vm_get_field_page(lg, wfields[s], NULL));
			int wn = vm_dd_array_len(warr);
			for (int i = 0; i < wn; i++) {
				struct page *w = vm_safe_get_page(vm_dd_array_elem_slot(warr, i));
				if (!w) continue;
				vm_set_field_int(w, "m_rank", NULL, 9);
				vm_set_field_int(w, "<Rarity>", NULL, 5);
				vm_set_field_int(w, "m_looks", NULL, 9999);
				vm_set_field_int(w, "m_technic", NULL, 9999);
				vm_set_field_int(w, "m_mental", NULL, 9999);
				vm_set_field_int(w, "m_pill", NULL, pill_max);
				vm_set_field_int(w, "<IsInsane>", NULL, 0);
				vm_set_field_int(w, "<IsPregnance>", NULL, 0);
			}
		}
	}

	/* 3. Granular context / management edits */
	if (gc) {
		if ((it = cJSON_GetObjectItem(act, "ddMoney")) && cJSON_IsNumber(it))
			vm_dd_set_money(gc_slot, it->valueint);
		if ((it = cJSON_GetObjectItem(act, "ddTurn")) && cJSON_IsNumber(it))
			vm_set_field_int(gc, "m_turn", NULL, vm_dd_clamp(it->valueint, 1, 99999));
		if ((it = cJSON_GetObjectItem(act, "ddTpMax")) && cJSON_IsNumber(it))
			vm_dd_set_tp_max(gc_slot, it->valueint);
		if ((it = cJSON_GetObjectItem(act, "ddTp")) && cJSON_IsNumber(it))
			vm_dd_set_tp(gc_slot, it->valueint);
	}
	if ((it = cJSON_GetObjectItem(act, "ddWorkerLimit")) && cJSON_IsNumber(it)) {
		int wc_slot = vm_dd_get_field_slot(lg, "m_worker", NULL);
		struct page *wc = vm_safe_get_page(wc_slot);
		int min_lim = vm_dd_array_len(vm_dd_get_worker_list_array(wc));
		if (min_lim < 1) min_lim = 1;
		if (min_lim > 10) min_lim = 10;
		int lim = vm_dd_clamp(it->valueint, min_lim, 10);
		if (wc && !vm_dd_set_option_int_field(wc, "m_limit", lim) && s_dd_fno_wc_set_limit >= 0)
			vm_dd_call_void_1i(s_dd_fno_wc_set_limit, wc_slot, lim);
	}

	/* 4. Granular combat member edits */
	cJSON *pe = cJSON_GetObjectItem(act, "ddPlayerEdit");
	if (pe && cJSON_IsObject(pe)) {
		cJSON *jt = cJSON_GetObjectItem(pe, "target");
		int target = (jt && cJSON_IsNumber(jt)) ? jt->valueint : -1;
		struct page *parr = vm_get_field_page(vm_get_field_page(lg, "m_players", NULL), "m_players", NULL);
		int pn = vm_dd_array_len(parr);
		if (target >= 0 && target < pn) {
			vm_dd_apply_player_edit(vm_dd_array_elem_slot(parr, target), pe);
		} else if (target == -1 || target == -2) {
			for (int i = 0; i < pn; i++) {
				int pl_slot = vm_dd_array_elem_slot(parr, i);
				struct page *pl = vm_safe_get_page(pl_slot);
				if (!pl || vm_dd_is_unjoined_guest_player(pl)) continue;
				if (target == -1 && !vm_get_field_int(pl, "<IsJoin>", NULL, 0))
					continue;
				vm_dd_apply_player_edit(pl_slot, pe);
			}
		}
	}

	/* 5. Granular brothel worker edits */
	cJSON *we = cJSON_GetObjectItem(act, "ddWorkerEdit");
	if (we && cJSON_IsObject(we)) {
		cJSON *js = cJSON_GetObjectItem(we, "scope");
		cJSON *ji = cJSON_GetObjectItem(we, "index");
		int scope = (js && cJSON_IsNumber(js)) ? js->valueint : 0;
		int idx   = (ji && cJSON_IsNumber(ji)) ? ji->valueint : -1;
		const char *wfields[2] = { "m_worker", "m_garage" };
		for (int s = 0; s < 2; s++) {
			if (scope != 2 && scope != s)
				continue;
			struct page *warr = vm_dd_get_worker_list_array(vm_get_field_page(lg, wfields[s], NULL));
			int wn = vm_dd_array_len(warr);
			if (idx >= 0 && idx < wn && scope != 2) {
				vm_dd_apply_worker_edit(vm_safe_get_page(vm_dd_array_elem_slot(warr, idx)), we);
			} else if (idx == -1) {
				for (int i = 0; i < wn; i++)
					vm_dd_apply_worker_edit(vm_safe_get_page(vm_dd_array_elem_slot(warr, i)), we);
			}
		}
	}

	cJSON_Delete(act);
}

static void vm_dd_populate_live_state_json(cJSON *root)
{
	vm_inspect_dohna_ain();
	struct page *lg = vm_dd_get_option_global_page(s_dd_glob_static_game);
	struct page *gc = lg ? vm_get_field_page(lg, "m_context", NULL) : NULL;
	if (!ain || !lg || !gc || vm_dd_is_on_title_screen()) {
		cJSON_AddBoolToObject(root, "inGame", false);
		cJSON_AddBoolToObject(root, "inBattle", false);
		return;
	}

	int bc_slot = vm_dd_get_battle_context_slot(lg);
	struct page *bc = bc_slot > 0 ? vm_safe_get_page(bc_slot) : NULL;

	cJSON_AddBoolToObject(root, "inGame", true);
	cJSON_AddBoolToObject(root, "inBattle", bc != NULL);
	cJSON_AddNumberToObject(root, "money", vm_get_field_int(gc, "m_money", NULL, 0));
	cJSON_AddNumberToObject(root, "turn", vm_get_field_int(gc, "m_turn", NULL, 1));
	cJSON_AddNumberToObject(root, "phase", vm_get_field_int(gc, "m_phase", NULL, 0));
	cJSON_AddNumberToObject(root, "tp", vm_get_field_int(gc, "m_trainingPoint", NULL, 0));
	cJSON_AddNumberToObject(root, "tpMax", vm_get_field_int(gc, "<TrainingPointMax>", NULL, 3));

	struct page *wc_shop   = vm_get_field_page(lg, "m_worker", NULL);
	struct page *wc_garage = vm_get_field_page(lg, "m_garage", NULL);
	int wlimit = 5;
	bool has_wlim = false;
	vm_dd_get_option_field_slot(wc_shop, "m_limit", &wlimit, &has_wlim);
	cJSON_AddNumberToObject(root, "workerLimit", has_wlim ? wlimit : 5);

	struct page *shop_warr   = vm_dd_get_worker_list_array(wc_shop);
	struct page *garage_warr = vm_dd_get_worker_list_array(wc_garage);
	int shop_wcnt   = vm_dd_array_len(shop_warr);
	int garage_wcnt = vm_dd_array_len(garage_warr);
	cJSON_AddNumberToObject(root, "workerCount", shop_wcnt);
	cJSON_AddNumberToObject(root, "garageCount", garage_wcnt);

	struct page *is_list = vm_get_field_page(vm_get_field_page(vm_get_field_page(lg, "m_itemStock", NULL), "m_list", NULL), "m_count", NULL);
	struct page *bs_list = vm_get_field_page(vm_get_field_page(vm_get_field_page(lg, "m_badgeStock", NULL), "m_badge", NULL), "m_count", NULL);
	cJSON_AddNumberToObject(root, "heldItemKinds", vm_dd_array_len(vm_get_field_page(is_list, "m_list", NULL)));
	cJSON_AddNumberToObject(root, "heldBadgeKinds", vm_dd_array_len(vm_get_field_page(bs_list, "m_list", NULL)));

	if (bc) {
		cJSON_AddNumberToObject(root, "battleTurn", vm_get_field_int(bc, "<Turn>", NULL, 1));
		struct page *side_arr = vm_get_field_page(bc, "m_players", NULL);
		struct page *ally_bpc = vm_safe_get_page(vm_dd_array_elem_slot(side_arr, 0));
		struct page *enem_bpc = vm_safe_get_page(vm_dd_array_elem_slot(side_arr, 1));
		struct page *ally_arr = vm_get_field_page(ally_bpc, "m_player", NULL);
		struct page *enem_arr = vm_get_field_page(enem_bpc, "m_player", NULL);
		int ally_cnt = vm_dd_array_len(ally_arr);
		int enem_cnt = vm_dd_array_len(enem_arr);
		int64_t enem_hp_sum = 0;
		for (int i = 0; i < enem_cnt; i++) {
			struct page *bp = vm_safe_get_page(vm_dd_array_elem_slot(enem_arr, i));
			union vm_value *mp_v = vm_get_struct_member(bp, "m_player", NULL);
			struct page *ent = mp_v ? vm_safe_get_page(mp_v->i) : NULL;
			struct page *hpp = vm_get_field_page(ent, "m_hp", NULL);
			int32_t h = vm_get_field_int(hpp, "m_current", NULL, 0);
			if (h > 0) enem_hp_sum += h;
		}
		cJSON_AddNumberToObject(root, "allyCount", ally_cnt);
		cJSON_AddNumberToObject(root, "enemyCount", enem_cnt);
		cJSON_AddNumberToObject(root, "enemyTotalHp", (double)enem_hp_sum);
	}

	cJSON *j_players = cJSON_CreateArray();
	struct page *parr = vm_get_field_page(vm_get_field_page(lg, "m_players", NULL), "m_players", NULL);
	int pn = vm_dd_array_len(parr);
	for (int i = 0; i < pn; i++) {
		int pl_slot = vm_dd_array_elem_slot(parr, i);
		struct page *pl = vm_safe_get_page(pl_slot);
		if (!pl || vm_dd_is_unjoined_guest_player(pl)) continue;
		cJSON *jp = cJSON_CreateObject();
		cJSON_AddNumberToObject(jp, "index", i);
		char *id_u = vm_dd_get_field_utf8(vm_get_field_page(pl, "m_param", NULL), "m_id", NULL);
		char *name_u = (s_dd_fno_pl_name_get >= 0) ? vm_dd_call_str_0_utf8(s_dd_fno_pl_name_get, pl_slot) : strdup(id_u);
		/* Re-fetch pointer after vm_call in case heap grew */
		pl = vm_safe_get_page(pl_slot);
		char *wp_u = vm_dd_get_field_utf8(pl, "m_weaponId", NULL);
		cJSON_AddStringToObject(jp, "id", id_u);
		const char *disp_name = (name_u && name_u[0]) ? name_u : id_u;
		if (!strcmp(id_u, "G暗影"))
			disp_name = "暗影";
		cJSON_AddStringToObject(jp, "name", disp_name);
		cJSON_AddStringToObject(jp, "weaponId", wp_u);
		free(id_u);
		free(name_u);
		free(wp_u);

		cJSON_AddBoolToObject(jp, "joined", vm_get_field_int(pl, "<IsJoin>", NULL, 0) != 0);
		cJSON_AddNumberToObject(jp, "level", vm_get_field_int(pl, "m_level", NULL, 1));
		cJSON_AddNumberToObject(jp, "exp", vm_get_field_int(pl, "m_exp", NULL, 0));

		struct page *hpp = vm_get_field_page(pl, "m_hp", NULL);
		struct page *mpp = vm_get_field_page(pl, "m_mp", NULL);
		struct page *vpp = vm_get_field_page(pl, "m_voltage", NULL);
		cJSON_AddNumberToObject(jp, "hp", vm_get_field_int(hpp, "m_current", NULL, 0));
		cJSON_AddNumberToObject(jp, "hpMax", vm_get_field_int(hpp, "m_max", NULL, 0));
		cJSON_AddNumberToObject(jp, "mp", vm_get_field_int(mpp, "m_current", NULL, 0));
		cJSON_AddNumberToObject(jp, "mpMin", vm_get_field_int(mpp, "m_min", NULL, 0));
		cJSON_AddNumberToObject(jp, "mpMax", vm_get_field_int(mpp, "m_max", NULL, 0));
		cJSON_AddNumberToObject(jp, "vp", vm_get_field_int(vpp, "m_current", NULL, 0));
		cJSON_AddNumberToObject(jp, "vpMax", vm_get_field_int(vpp, "m_max", NULL, 0));

		cJSON_AddNumberToObject(jp, "alterPow", vm_get_field_int(pl, "<AlterPower>", NULL, 0));
		cJSON_AddNumberToObject(jp, "alterTec", vm_get_field_int(pl, "<AlterTechnic>", NULL, 0));
		cJSON_AddNumberToObject(jp, "alterSpd", vm_get_field_int(pl, "<AlterSpeed>", NULL, 0));
		cJSON_AddNumberToObject(jp, "alterHp", vm_get_field_int(pl, "<AlterHp>", NULL, 0));
		cJSON_AddNumberToObject(jp, "alterMp", vm_get_field_int(pl, "<AlterMp>", NULL, 0));
		cJSON_AddNumberToObject(jp, "alterVp", vm_get_field_int(pl, "<AlterVoltage>", NULL, 0));

		struct page *fe = vm_get_field_page(pl, "m_feelEvent", NULL);
		cJSON_AddNumberToObject(jp, "feel", vm_get_field_int(fe, "m_feel", NULL, 0));

		int pow_v = (s_dd_fno_pl_pow_get >= 0) ? vm_dd_call_int_0(s_dd_fno_pl_pow_get, pl_slot, 0) : 0;
		int tec_v = (s_dd_fno_pl_tec_get >= 0) ? vm_dd_call_int_0(s_dd_fno_pl_tec_get, pl_slot, 0) : 0;
		int spd_v = (s_dd_fno_pl_spd_get >= 0) ? vm_dd_call_int_0(s_dd_fno_pl_spd_get, pl_slot, 0) : 0;
		cJSON_AddNumberToObject(jp, "pow", pow_v);
		cJSON_AddNumberToObject(jp, "tec", tec_v);
		cJSON_AddNumberToObject(jp, "spd", spd_v);

		cJSON_AddItemToArray(j_players, jp);
	}
	cJSON_AddItemToObject(root, "ddPlayers", j_players);

	cJSON *j_workers = cJSON_CreateArray();
	struct page *warrs[2] = { shop_warr, garage_warr };
	int added_workers = 0;
	for (int s = 0; s < 2 && added_workers < 80; s++) {
		int wn = vm_dd_array_len(warrs[s]);
		for (int i = 0; i < wn && added_workers < 80; i++) {
			struct page *w = vm_safe_get_page(vm_dd_array_elem_slot(warrs[s], i));
			if (!w) continue;
			cJSON *jw = cJSON_CreateObject();
			cJSON_AddNumberToObject(jw, "scope", s);
			cJSON_AddNumberToObject(jw, "index", i);
			char *wn_u = vm_dd_get_field_utf8(w, "<Name>", NULL);
			cJSON_AddStringToObject(jw, "name", wn_u);
			free(wn_u);
			cJSON_AddNumberToObject(jw, "rank", vm_get_field_int(w, "m_rank", NULL, 0));
			cJSON_AddNumberToObject(jw, "rarity", vm_get_field_int(w, "<Rarity>", NULL, 0));
			cJSON_AddNumberToObject(jw, "looks", vm_get_field_int(w, "m_looks", NULL, 0));
			cJSON_AddNumberToObject(jw, "technic", vm_get_field_int(w, "m_technic", NULL, 0));
			cJSON_AddNumberToObject(jw, "mental", vm_get_field_int(w, "m_mental", NULL, 0));
			cJSON_AddNumberToObject(jw, "pill", vm_get_field_int(w, "m_pill", NULL, 0));
			cJSON_AddBoolToObject(jw, "virgin", vm_get_field_int(w, "<IsVirgin>", NULL, 0) != 0);
			cJSON_AddBoolToObject(jw, "pregnance", vm_get_field_int(w, "<IsPregnance>", NULL, 0) != 0);
			cJSON_AddBoolToObject(jw, "insane", vm_get_field_int(w, "<IsInsane>", NULL, 0) != 0);
			cJSON_AddItemToArray(j_workers, jw);
			added_workers++;
		}
	}
	cJSON_AddItemToObject(root, "ddWorkers", j_workers);
}

/* =========================================================================
 * Evenicle Live In-Game Modifier Support
 * ========================================================================= */

#define EV_FN_SET_MONEY     "\x8f\x8a\x8e\x9d\x8b\xe0\x90\xdd\x92\xe8"
#define EV_FN_SET_MONEY_CN  "\xcb\xf9\xb3\xd6\xbd\xf0\xd4O\xb6\xa8"

static struct ain *s_ev_inspected_ain = NULL;
static int s_ev_glob_money = -1;
static int s_ev_glob_status = -1;
static int s_ev_glob_chara = -1;
static int s_ev_glob_game = -1;
static int s_ev_glob_battle = -1;
static int s_ev_glob_party_menu = -1;
static int s_ev_fno_set_money = -1;

static void vm_inspect_evenicle_ain(void)
{
	if (!vm_globals_ready() || ain == s_ev_inspected_ain)
		return;
	s_ev_inspected_ain = ain;
	s_ev_glob_money      = vm_find_global_idx("g_nPartyMoney", NULL);
	s_ev_glob_status     = vm_find_global_idx("g_arfStatus", NULL);
	s_ev_glob_chara      = vm_find_global_idx(EV_G_CHARA_DATA_CN, EV_G_CHARA_DATA);
	s_ev_glob_game       = vm_find_global_idx("g_pfGame", NULL);
	s_ev_glob_battle     = vm_find_global_idx("g_ptBattle", NULL);
	s_ev_glob_party_menu = vm_find_global_idx("g_ptPartyMenu", NULL);
	s_ev_fno_set_money   = ain_get_function(ain, EV_FN_SET_MONEY);
	if (s_ev_fno_set_money < 0)
		s_ev_fno_set_money = ain_get_function(ain, EV_FN_SET_MONEY_CN);
	if (s_ev_fno_set_money >= 0 && ain->functions[s_ev_fno_set_money].nr_args != 2)
		s_ev_fno_set_money = -1;
}

static bool vm_ev_is_on_title_screen(void)
{
	if (!ain || !ain->functions)
		return false;
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		int fno = call_stack[i].fno;
		if (fno >= 0 && fno < ain->nr_functions && ain->functions[fno].name) {
			const char *fn = ain->functions[fno].name;
			if (!strncmp(fn, "CGame@", 6) ||
			    !strncmp(fn, "T_Battle@", 9) ||
			    !strncmp(fn, "CSmallMap@", 10) ||
			    !strncmp(fn, "CBigMap@", 8))
				return false;
			if (strstr(fn, EV_SJIS_TITLE_FN) != NULL ||
			    strstr(fn, EV_CN_TITLE_FN) != NULL)
				return true;
		}
	}
	return false;
}

static struct page *vm_ev_get_status_param_at(struct page *status_arr, int idx)
{
	struct page *ref = vm_safe_get_page(vm_dd_array_elem_slot(status_arr, idx));
	if (!ref || ref->type != STRUCT_PAGE)
		return NULL;
	return vm_get_field_page(ref, "rf", NULL);
}

static char *vm_ev_find_chara_name_utf8(struct page *cdata_arr, int chara_no, int fallback_idx)
{
	int n = vm_dd_array_len(cdata_arr);
	if (n <= 0)
		return strdup("");
	// g_aキャラデータ is indexed directly by chara_no; EV_F_CHARA_ID is 戦闘敵識別ID
	// set dynamically on battle enemy slots (e.g. 24+) and collides with ally IDs.
	int try_indices[2] = { chara_no, fallback_idx };
	for (int t = 0; t < 2; t++) {
		int idx = try_indices[t];
		if (idx >= 0 && idx < n) {
			struct page *cp = vm_safe_get_page(vm_dd_array_elem_slot(cdata_arr, idx));
			if (cp) {
				char *nm = vm_dd_get_field_utf8(cp, EV_F_NAME_CN, EV_F_NAME);
				if (nm && nm[0]) return nm;
				free(nm);
			}
		}
	}
	return strdup("");
}

static bool vm_ev_is_playable_chara(struct page *cdata_arr, int32_t chara_no, struct page *st)
{
	if (!st || chara_no <= 0 || chara_no >= 21)
		return false;
	if (cdata_arr) {
		int n = vm_dd_array_len(cdata_arr);
		if (chara_no >= n)
			return false;
		struct page *cp = vm_safe_get_page(vm_dd_array_elem_slot(cdata_arr, chara_no));
		if (cp) {
			if (vm_get_field_int(cp, EV_F_CHARA_ID_CN, EV_F_CHARA_ID, 0) > 0)
				return false;
			if (vm_get_field_int(cp, EV_F_PARTY_SET_CN, EV_F_PARTY_SET, 0) <= 1)
				return false;
		}
	}
	return true;
}

static int32_t vm_ev_get_money(void)
{
	if (!vm_globals_ready() || s_ev_glob_money < 0 || s_ev_glob_money >= heap[0].page->nr_vars)
		return 0;
	return global_get(s_ev_glob_money).i;
}

/*
 * Check whether chara_no occupies one of the 5 active party plates in
 * g_ptPartyMenu.m_anCharaList. Falls back to bIsUse only before party plates
 * are initialized, and always rejects placeholder chara_no <= 0 ("NULL").
 */
static bool vm_ev_is_in_active_party(int32_t chara_no, struct page *st)
{
	if (chara_no <= 0)
		return false;
	struct page *pm = s_ev_glob_party_menu >= 0 ? vm_get_global_page_idx(s_ev_glob_party_menu) : NULL;
	struct page *plist = pm ? vm_get_field_page(pm, "m_anCharaList", NULL) : NULL;
	if (plist && plist->type == ARRAY_PAGE && plist->nr_vars > 0) {
		bool has_any = false;
		for (int p = 0; p < plist->nr_vars && p < 8; p++) {
			int32_t pid = plist->values[p].i;
			if (pid > 0)
				has_any = true;
			if (pid == chara_no)
				return true;
		}
		if (has_any)
			return false;
	}
	return st ? (vm_get_field_int(st, "bIsUse", NULL, 0) != 0) : false;
}

static void vm_ev_set_money(int32_t val)
{
	if (!vm_globals_ready() || s_ev_glob_money < 0 || s_ev_glob_money >= heap[0].page->nr_vars)
		return;
	int32_t clamped = vm_dd_clamp(val, 0, 99999999);
	heap[0].page->values[s_ev_glob_money].i = clamped;
	/* Also update the HUD money plate widget when in a live VM call frame. */
	if (call_stack_ptr > 0 && s_ev_fno_set_money >= 0 &&
	    s_ev_glob_party_menu >= 0 && vm_get_global_page_idx(s_ev_glob_party_menu)) {
		stack_push(clamped);
		stack_push(0);
		vm_call(s_ev_fno_set_money, -1);
	}
}

static void vm_ev_set_stat_with_base(struct page *st, const char *eff_f1, const char *eff_f2,
		const char *base_f1, const char *base_f2, int32_t target, int32_t min_v, int32_t max_v)
{
	if (!st) return;
	int32_t clamped = vm_dd_clamp(target, min_v, max_v);
	int32_t cur = vm_get_field_int(st, eff_f1, eff_f2, 0);
	int32_t delta = clamped - cur;
	vm_set_field_int(st, eff_f1, eff_f2, clamped);
	if ((base_f1 || base_f2) && delta != 0) {
		int32_t bcur = vm_get_field_int(st, base_f1, base_f2, 0);
		int32_t bnext = vm_dd_clamp(bcur + delta, min_v, max_v);
		vm_set_field_int(st, base_f1, base_f2, bnext);
	}
}

static void vm_ev_apply_enemy_1hp(struct page *bt)
{
	if (!bt || !vm_get_field_int(bt, "m_bIsBattle", NULL, 0))
		return;
	struct page *binfo = vm_get_field_page(bt, "m_BattleInitInfo", NULL);
	struct page *earr  = vm_get_field_page(binfo, "aEnemyInfo", NULL);
	int en = vm_dd_array_len(earr);
	for (int i = 0; i < en; i++) {
		struct page *ep = vm_safe_get_page(vm_dd_array_elem_slot(earr, i));
		if (!ep || !vm_get_field_int(ep, "isExist", NULL, 0)) continue;
		struct page *est = vm_get_field_page(ep, "status", NULL);
		if (est && vm_get_field_int(est, EV_F_HP_CN, EV_F_HP, 0) > 1)
			vm_set_field_int(est, EV_F_HP_CN, EV_F_HP, 1);
	}
	struct page *dinfo = vm_get_field_page(bt, "m_EnemyInfo", NULL);
	struct page *darr  = vm_get_field_page(dinfo, "m_anEnemyInfoList", NULL);
	int dn = vm_dd_array_len(darr);
	for (int i = 0; i < dn; i++) {
		struct page *ref = vm_safe_get_page(vm_dd_array_elem_slot(darr, i));
		struct page *ep  = ref ? vm_get_field_page(ref, "rf", NULL) : NULL;
		if (!ep || !vm_get_field_int(ep, "isExist", NULL, 0)) continue;
		struct page *est = vm_get_field_page(ep, "status", NULL);
		if (est && vm_get_field_int(est, EV_F_HP_CN, EV_F_HP, 0) > 1)
			vm_set_field_int(est, EV_F_HP_CN, EV_F_HP, 1);
	}
}

static void vm_ev_apply_char_edit(struct page *st, cJSON *ce)
{
	if (!st || !ce) return;
	cJSON *it;
	if ((it = cJSON_GetObjectItem(ce, "lv")) && cJSON_IsNumber(it))
		vm_set_field_int(st, EV_F_LV_CN, EV_F_LV, vm_dd_clamp(it->valueint, 1, 999));
	if ((it = cJSON_GetObjectItem(ce, "hpMax")) && cJSON_IsNumber(it)) {
		int32_t old_max = vm_get_field_int(st, EV_F_MAX_HP_CN, EV_F_MAX_HP, 1);
		int32_t new_max = vm_dd_clamp(it->valueint, 1, 999999);
		vm_ev_set_stat_with_base(st, EV_F_MAX_HP_CN, EV_F_MAX_HP, EV_F_BASE_HP_CN, EV_F_BASE_HP, new_max, 1, 999999);
		int32_t cur_hp = vm_get_field_int(st, EV_F_HP_CN, EV_F_HP, new_max);
		if (new_max > old_max && cur_hp >= old_max)
			vm_set_field_int(st, EV_F_HP_CN, EV_F_HP, new_max);
		else if (cur_hp > new_max)
			vm_set_field_int(st, EV_F_HP_CN, EV_F_HP, new_max);
	}
	if ((it = cJSON_GetObjectItem(ce, "hp")) && cJSON_IsNumber(it)) {
		int32_t max_hp = vm_get_field_int(st, EV_F_MAX_HP_CN, EV_F_MAX_HP, 999999);
		if (max_hp < 1) max_hp = 1;
		vm_set_field_int(st, EV_F_HP_CN, EV_F_HP, vm_dd_clamp(it->valueint, 0, max_hp));
	}
	if ((it = cJSON_GetObjectItem(ce, "atk")) && cJSON_IsNumber(it))
		vm_ev_set_stat_with_base(st, EV_F_ATK_CN, EV_F_ATK, EV_F_BASE_ATK_CN, EV_F_BASE_ATK, it->valueint, 0, 99999);
	if ((it = cJSON_GetObjectItem(ce, "mag")) && cJSON_IsNumber(it))
		vm_ev_set_stat_with_base(st, EV_F_MAG_CN, EV_F_MAG, EV_F_BASE_MAG_CN, EV_F_BASE_MAG, it->valueint, 0, 99999);
	if ((it = cJSON_GetObjectItem(ce, "def")) && cJSON_IsNumber(it))
		vm_ev_set_stat_with_base(st, EV_F_DEF_CN, EV_F_DEF, EV_F_BASE_DEF_CN, EV_F_BASE_DEF, it->valueint, 0, 99999);
	if ((it = cJSON_GetObjectItem(ce, "spd")) && cJSON_IsNumber(it))
		vm_ev_set_stat_with_base(st, EV_F_SPD_CN, EV_F_SPD, EV_F_BASE_SPD_CN, EV_F_BASE_SPD, it->valueint, 0, 99999);
	if ((it = cJSON_GetObjectItem(ce, "skillP")) && cJSON_IsNumber(it))
		vm_set_field_int(st, EV_F_SKILL_P_CN, EV_F_SKILL_P, vm_dd_clamp(it->valueint, 0, 99999));
	if ((it = cJSON_GetObjectItem(ce, "bp")) && cJSON_IsNumber(it))
		vm_set_field_int(st, EV_F_BP_CN, EV_F_BP, vm_dd_clamp(it->valueint, 0, 5));
	if ((it = cJSON_GetObjectItem(ce, "exp")) && cJSON_IsNumber(it))
		vm_set_field_int(st, EV_F_EXP_CN, EV_F_EXP, vm_dd_clamp(it->valueint, 0, 99999999));
}

static void vm_enforce_evenicle_locks(int ev_mask)
{
	if (!ev_mask || !ain || vm_ev_is_on_title_screen())
		return;
	vm_inspect_evenicle_ain();

	struct page *pf = s_ev_glob_game >= 0 ? vm_get_global_page_idx(s_ev_glob_game) : NULL;
	struct page *status_arr = s_ev_glob_status >= 0 ? vm_get_global_page_idx(s_ev_glob_status) : NULL;
	if (!pf || !status_arr || vm_dd_array_len(status_arr) <= 0)
		return;

	if (ev_mask & LOCK_EV_INFINITE_MONEY) {
		if (vm_ev_get_money() < 9999999)
			vm_ev_set_money(9999999);
	}

	if (ev_mask & LOCK_EV_NO_ENCOUNT) {
		struct page *enc = vm_get_field_page(pf, "m_clEncountData", NULL);
		if (enc) {
			union vm_value *vmax = vm_get_struct_member(enc, "m_fMaxBarCount", NULL);
			union vm_value *vnow = vm_get_struct_member(enc, "m_fNowBarCount", NULL);
			if (vmax && vnow && vmax->f > 0.0f && vnow->f < vmax->f)
				vnow->f = vmax->f;
		}
	}

	bool lock_hp = (ev_mask & LOCK_EV_ALLY_FULL_HP) != 0;
	bool lock_sp = (ev_mask & LOCK_EV_INF_SKILL_P) != 0;
	bool lock_bp = (ev_mask & LOCK_EV_INF_BP) != 0;
	if (lock_hp || lock_sp || lock_bp) {
		struct page *cdata_arr = s_ev_glob_chara >= 0 ? vm_get_global_page_idx(s_ev_glob_chara) : NULL;
		int sn = vm_dd_array_len(status_arr);
		for (int i = 0; i < sn; i++) {
			struct page *st = vm_ev_get_status_param_at(status_arr, i);
			if (!st) continue;
			int32_t cno = vm_get_field_int(st, "charaNo", NULL, i);
			if (!vm_ev_is_playable_chara(cdata_arr, cno, st))
				continue;
			if (!vm_get_field_int(st, "bIsUse", NULL, 0) || vm_get_field_int(st, EV_F_LV_CN, EV_F_LV, 0) <= 0)
				continue;
			if (lock_hp) {
				int32_t mhp = vm_get_field_int(st, EV_F_MAX_HP_CN, EV_F_MAX_HP, 0);
				if (mhp > 0 && vm_get_field_int(st, EV_F_HP_CN, EV_F_HP, 0) != mhp)
					vm_set_field_int(st, EV_F_HP_CN, EV_F_HP, mhp);
			}
			if (lock_sp && vm_get_field_int(st, EV_F_SKILL_P_CN, EV_F_SKILL_P, 0) < 99)
				vm_set_field_int(st, EV_F_SKILL_P_CN, EV_F_SKILL_P, 99);
			if (lock_bp && vm_get_field_int(st, EV_F_BP_CN, EV_F_BP, 0) < 5)
				vm_set_field_int(st, EV_F_BP_CN, EV_F_BP, 5);
		}
	}

	if (ev_mask & LOCK_EV_ONE_HIT_KILL) {
		struct page *bt = s_ev_glob_battle >= 0 ? vm_get_global_page_idx(s_ev_glob_battle) : NULL;
		if (bt)
			vm_ev_apply_enemy_1hp(bt);
	}
}

static void vm_execute_evenicle_action(const char *json_str)
{
	if (!json_str || !json_str[0])
		return;
	cJSON *act = cJSON_Parse(json_str);
	if (!act)
		return;

	vm_inspect_evenicle_ain();
	cJSON *it;

	bool persist_cfg = false;
	int save_r10_mask = 0;
	pthread_mutex_lock(&g_mod_mutex);
	if ((it = cJSON_GetObjectItem(act, "setEvenicleLockMask")) && cJSON_IsNumber(it)) {
		g_evenicle_lock_mask = it->valueint;
		g_has_evenicle_lock_mask_init = true;
		persist_cfg = true;
	}
	pthread_mutex_unlock(&g_mod_mutex);
	vm_apply_ui_scale_from_json(act, &persist_cfg, &save_r10_mask);
	if (persist_cfg)
		persist_modifier_config_from_native(save_r10_mask);

	struct page *pf = s_ev_glob_game >= 0 ? vm_get_global_page_idx(s_ev_glob_game) : NULL;
	struct page *status_arr = s_ev_glob_status >= 0 ? vm_get_global_page_idx(s_ev_glob_status) : NULL;
	struct page *bt = s_ev_glob_battle >= 0 ? vm_get_global_page_idx(s_ev_glob_battle) : NULL;
	if (!pf || !status_arr || vm_ev_is_on_title_screen()) {
		cJSON_Delete(act);
		return;
	}

	if ((it = cJSON_GetObjectItem(act, "evBattle1HpEnemies")) && cJSON_IsTrue(it) && bt)
		vm_ev_apply_enemy_1hp(bt);

	if ((it = cJSON_GetObjectItem(act, "evBattleHealAllies")) && cJSON_IsTrue(it)) {
		struct page *cdata_arr = s_ev_glob_chara >= 0 ? vm_get_global_page_idx(s_ev_glob_chara) : NULL;
		int sn = vm_dd_array_len(status_arr);
		for (int i = 0; i < sn; i++) {
			struct page *st = vm_ev_get_status_param_at(status_arr, i);
			int32_t cno = st ? vm_get_field_int(st, "charaNo", NULL, i) : 0;
			if (!st || !vm_ev_is_playable_chara(cdata_arr, cno, st))
				continue;
			if (!vm_get_field_int(st, "bIsUse", NULL, 0) || vm_get_field_int(st, EV_F_LV_CN, EV_F_LV, 0) <= 0)
				continue;
			int32_t mhp = vm_get_field_int(st, EV_F_MAX_HP_CN, EV_F_MAX_HP, 0);
			if (mhp > 0) vm_set_field_int(st, EV_F_HP_CN, EV_F_HP, mhp);
			vm_set_field_int(st, EV_F_BP_CN, EV_F_BP, 5);
		}
	}

	if ((it = cJSON_GetObjectItem(act, "evCheatAddMoney")) && cJSON_IsNumber(it)) {
		int64_t next = (int64_t)vm_ev_get_money() + (int64_t)it->valueint;
		vm_ev_set_money((int32_t)(next > 99999999LL ? 99999999LL : (next < 0 ? 0 : next)));
	}
	if ((it = cJSON_GetObjectItem(act, "evCheatMaxMoney")) && cJSON_IsTrue(it))
		vm_ev_set_money(99999999);
	if ((it = cJSON_GetObjectItem(act, "evMoney")) && cJSON_IsNumber(it))
		vm_ev_set_money(it->valueint);

	if ((it = cJSON_GetObjectItem(act, "evCheatTriggerEncounter")) && cJSON_IsTrue(it)) {
		struct page *enc = vm_get_field_page(pf, "m_clEncountData", NULL);
		if (enc) {
			union vm_value *vnow = vm_get_struct_member(enc, "m_fNowBarCount", NULL);
			union vm_value *vcnt = vm_get_struct_member(enc, "m_fNowCulcCount", NULL);
			if (vnow) vnow->f = 0.0f;
			if (vcnt) vcnt->f = 0.0f;
			vm_set_field_int(enc, "m_nEncountRand", NULL, 1);
		}
	}
	if ((it = cJSON_GetObjectItem(act, "evCheatClearEncounter")) && cJSON_IsTrue(it)) {
		struct page *enc = vm_get_field_page(pf, "m_clEncountData", NULL);
		if (enc) {
			union vm_value *vmax = vm_get_struct_member(enc, "m_fMaxBarCount", NULL);
			union vm_value *vnow = vm_get_struct_member(enc, "m_fNowBarCount", NULL);
			if (vmax && vnow && vmax->f > 0.0f) vnow->f = vmax->f;
		}
	}

	if ((it = cJSON_GetObjectItem(act, "evCheatPartyFullHeal")) && cJSON_IsTrue(it)) {
		int sn = vm_dd_array_len(status_arr);
		for (int i = 0; i < sn; i++) {
			struct page *st = vm_ev_get_status_param_at(status_arr, i);
			int32_t cno = st ? vm_get_field_int(st, "charaNo", NULL, i) : 0;
			if (!st || cno <= 0 || !vm_ev_is_in_active_party(cno, st))
				continue;
			int32_t mhp = vm_get_field_int(st, EV_F_MAX_HP_CN, EV_F_MAX_HP, 0);
			if (mhp > 0) vm_set_field_int(st, EV_F_HP_CN, EV_F_HP, mhp);
			vm_set_field_int(st, EV_F_BP_CN, EV_F_BP, 5);
		}
	}
	if ((it = cJSON_GetObjectItem(act, "evCheatPartySkillP999")) && cJSON_IsTrue(it)) {
		int sn = vm_dd_array_len(status_arr);
		for (int i = 0; i < sn; i++) {
			struct page *st = vm_ev_get_status_param_at(status_arr, i);
			int32_t cno = st ? vm_get_field_int(st, "charaNo", NULL, i) : 0;
			if (!st || cno <= 0 || !vm_ev_is_in_active_party(cno, st))
				continue;
			if (vm_get_field_int(st, EV_F_SKILL_P_CN, EV_F_SKILL_P, 0) < 999)
				vm_set_field_int(st, EV_F_SKILL_P_CN, EV_F_SKILL_P, 999);
		}
	}
	if ((it = cJSON_GetObjectItem(act, "evCheatPartyBoost")) && cJSON_IsTrue(it)) {
		int sn = vm_dd_array_len(status_arr);
		for (int i = 0; i < sn; i++) {
			struct page *st = vm_ev_get_status_param_at(status_arr, i);
			int32_t cno = st ? vm_get_field_int(st, "charaNo", NULL, i) : 0;
			if (!st || cno <= 0 || !vm_ev_is_in_active_party(cno, st))
				continue;
			if (vm_get_field_int(st, EV_F_MAX_HP_CN, EV_F_MAX_HP, 0) < 9999)
				vm_ev_set_stat_with_base(st, EV_F_MAX_HP_CN, EV_F_MAX_HP, EV_F_BASE_HP_CN, EV_F_BASE_HP, 9999, 1, 999999);
			vm_set_field_int(st, EV_F_HP_CN, EV_F_HP, vm_get_field_int(st, EV_F_MAX_HP_CN, EV_F_MAX_HP, 9999));
			if (vm_get_field_int(st, EV_F_ATK_CN, EV_F_ATK, 0) < 999)
				vm_ev_set_stat_with_base(st, EV_F_ATK_CN, EV_F_ATK, EV_F_BASE_ATK_CN, EV_F_BASE_ATK, 999, 0, 99999);
			if (vm_get_field_int(st, EV_F_MAG_CN, EV_F_MAG, 0) < 999)
				vm_ev_set_stat_with_base(st, EV_F_MAG_CN, EV_F_MAG, EV_F_BASE_MAG_CN, EV_F_BASE_MAG, 999, 0, 99999);
			if (vm_get_field_int(st, EV_F_DEF_CN, EV_F_DEF, 0) < 999)
				vm_ev_set_stat_with_base(st, EV_F_DEF_CN, EV_F_DEF, EV_F_BASE_DEF_CN, EV_F_BASE_DEF, 999, 0, 99999);
			if (vm_get_field_int(st, EV_F_SPD_CN, EV_F_SPD, 0) < 999)
				vm_ev_set_stat_with_base(st, EV_F_SPD_CN, EV_F_SPD, EV_F_BASE_SPD_CN, EV_F_BASE_SPD, 999, 0, 99999);
			if (vm_get_field_int(st, EV_F_SKILL_P_CN, EV_F_SKILL_P, 0) < 999)
				vm_set_field_int(st, EV_F_SKILL_P_CN, EV_F_SKILL_P, 999);
			vm_set_field_int(st, EV_F_BP_CN, EV_F_BP, 5);
		}
	}

	cJSON *ce = cJSON_GetObjectItem(act, "evCharEdit");
	if (ce && cJSON_IsObject(ce)) {
		cJSON *jt = cJSON_GetObjectItem(ce, "target");
		int target = (jt && cJSON_IsNumber(jt)) ? jt->valueint : -1;
		int sn = vm_dd_array_len(status_arr);
		if (target >= 0 && target < sn) {
			vm_ev_apply_char_edit(vm_ev_get_status_param_at(status_arr, target), ce);
		} else if (target == -1 || target == -2) {
			struct page *cdata_arr = s_ev_glob_chara >= 0 ? vm_get_global_page_idx(s_ev_glob_chara) : NULL;
			for (int i = 0; i < sn; i++) {
				struct page *st = vm_ev_get_status_param_at(status_arr, i);
				int32_t cno = st ? vm_get_field_int(st, "charaNo", NULL, i) : 0;
				if (!st || !vm_ev_is_playable_chara(cdata_arr, cno, st) || vm_get_field_int(st, EV_F_LV_CN, EV_F_LV, 0) <= 0) continue;
				if (target == -1 && !vm_ev_is_in_active_party(cno, st))
					continue;
				vm_ev_apply_char_edit(st, ce);
			}
		}
	}

	cJSON_Delete(act);
}

static void vm_ev_populate_live_state_json(cJSON *root)
{
	vm_inspect_evenicle_ain();
	struct page *pf = s_ev_glob_game >= 0 ? vm_get_global_page_idx(s_ev_glob_game) : NULL;
	struct page *status_arr = s_ev_glob_status >= 0 ? vm_get_global_page_idx(s_ev_glob_status) : NULL;
	int sn = vm_dd_array_len(status_arr);
	if (!ain || !pf || !status_arr || sn <= 0 || vm_ev_is_on_title_screen()) {
		cJSON_AddBoolToObject(root, "inGame", false);
		cJSON_AddBoolToObject(root, "inBattle", false);
		return;
	}

	struct page *bt = s_ev_glob_battle >= 0 ? vm_get_global_page_idx(s_ev_glob_battle) : NULL;
	bool in_battle = bt && vm_get_field_int(bt, "m_bIsBattle", NULL, 0) != 0;
	cJSON_AddBoolToObject(root, "inGame", true);
	cJSON_AddBoolToObject(root, "inBattle", in_battle);
	cJSON_AddNumberToObject(root, "money", vm_ev_get_money());

	struct page *enc = vm_get_field_page(pf, "m_clEncountData", NULL);
	if (enc) {
		union vm_value *vmax = vm_get_struct_member(enc, "m_fMaxBarCount", NULL);
		union vm_value *vnow = vm_get_struct_member(enc, "m_fNowBarCount", NULL);
		cJSON_AddNumberToObject(root, "encountMax", vmax ? (double)vmax->f : 0.0);
		cJSON_AddNumberToObject(root, "encountNow", vnow ? (double)vnow->f : 0.0);
	}

	if (in_battle) {
		struct page *binfo = vm_get_field_page(bt, "m_BattleInitInfo", NULL);
		struct page *earr  = vm_get_field_page(binfo, "aEnemyInfo", NULL);
		int en = vm_dd_array_len(earr);
		int alive_cnt = 0;
		int64_t hp_sum = 0;
		for (int i = 0; i < en; i++) {
			struct page *ep = vm_safe_get_page(vm_dd_array_elem_slot(earr, i));
			if (!ep || !vm_get_field_int(ep, "isExist", NULL, 0)) continue;
			struct page *est = vm_get_field_page(ep, "status", NULL);
			int32_t hp = est ? vm_get_field_int(est, EV_F_HP_CN, EV_F_HP, 0) : 0;
			if (hp > 0) {
				alive_cnt++;
				hp_sum += hp;
			}
		}
		cJSON_AddNumberToObject(root, "enemyCount", alive_cnt);
		cJSON_AddNumberToObject(root, "enemyTotalHp", (double)hp_sum);
	}

	struct page *cdata_arr = s_ev_glob_chara >= 0 ? vm_get_global_page_idx(s_ev_glob_chara) : NULL;
	cJSON *j_chars = cJSON_CreateArray();
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < sn && i < 64; i++) {
			struct page *st = vm_ev_get_status_param_at(status_arr, i);
			if (!st) continue;
			int32_t chara_no = vm_get_field_int(st, "charaNo", NULL, i);
			if (!vm_ev_is_playable_chara(cdata_arr, chara_no, st))
				continue;
			bool in_party = vm_ev_is_in_active_party(chara_no, st);
			if ((pass == 0) != in_party)
				continue;
			int32_t lv = vm_get_field_int(st, EV_F_LV_CN, EV_F_LV, 0);
			int32_t hpMax = vm_get_field_int(st, EV_F_MAX_HP_CN, EV_F_MAX_HP, 0);
			bool is_use = vm_get_field_int(st, "bIsUse", NULL, 0) != 0;
			if (!in_party && (!is_use || (lv <= 0 && hpMax <= 0)))
				continue;
			char *name_u = vm_ev_find_chara_name_utf8(cdata_arr, chara_no, i);
			if (!name_u || !name_u[0]) {
				free(name_u);
				continue;
			}
			cJSON *jc = cJSON_CreateObject();
			cJSON_AddNumberToObject(jc, "index", i);
			cJSON_AddNumberToObject(jc, "charaNo", chara_no);
			cJSON_AddStringToObject(jc, "name", name_u);
			free(name_u);
			cJSON_AddBoolToObject(jc, "inParty", in_party);
			cJSON_AddNumberToObject(jc, "lv", lv);
			cJSON_AddNumberToObject(jc, "hp", vm_get_field_int(st, EV_F_HP_CN, EV_F_HP, 0));
			cJSON_AddNumberToObject(jc, "hpMax", hpMax);
			cJSON_AddNumberToObject(jc, "atk", vm_get_field_int(st, EV_F_ATK_CN, EV_F_ATK, 0));
			cJSON_AddNumberToObject(jc, "mag", vm_get_field_int(st, EV_F_MAG_CN, EV_F_MAG, 0));
			cJSON_AddNumberToObject(jc, "def", vm_get_field_int(st, EV_F_DEF_CN, EV_F_DEF, 0));
			cJSON_AddNumberToObject(jc, "spd", vm_get_field_int(st, EV_F_SPD_CN, EV_F_SPD, 0));
			cJSON_AddNumberToObject(jc, "exp", vm_get_field_int(st, EV_F_EXP_CN, EV_F_EXP, 0));
			cJSON_AddNumberToObject(jc, "expMax", vm_get_field_int(st, EV_F_MAX_EXP_CN, EV_F_MAX_EXP, 0));
			cJSON_AddNumberToObject(jc, "skillP", vm_get_field_int(st, EV_F_SKILL_P_CN, EV_F_SKILL_P, 0));
			cJSON_AddNumberToObject(jc, "bp", vm_get_field_int(st, EV_F_BP_CN, EV_F_BP, 0));
			cJSON_AddItemToArray(j_chars, jc);
		}
	}
	cJSON_AddItemToObject(root, "evCharacters", j_chars);
}

/* =========================================================================
 * Sengoku Rance Live In-Game Modifier Support (AliceCN.ain / Rance7 v5)
 * ========================================================================= */

#define SR_G_SEIRYOKU     "\x90\xa8\x97\xcd"
#define SR_G_TURN         "\x81\x9f\x83^\x81[\x83\x93"
#define SR_G_SATISFACTION "\x81\x9f\x96\x9e\x91\xab\x93x"
#define SR_G_GOLD         "\x81\x9f\x8b\xe0"
#define SR_G_TROOP_LIMIT  "\x81\x9f\x95\xba\x90\x94\x90\xa7\x8c\xc0"
#define SR_G_AP           "\x81\x9f\x83R\x83}\x83\x93\x83h\x8e\xc0\x8ds\x90\x94"
#define SR_G_AP_MAX       "\x81\x9f\x83R\x83}\x83\x93\x83h\x8e\xc0\x8ds\x90\x94\x8d\xc5\x91\xe5"

static struct ain *s_sr_inspected_ain = NULL;
static bool s_sr_compatible = false;
static int s_sr_glob_seiryoku = -1;
static int s_sr_glob_gs = -1;
static int s_sr_glob_turn = -1;
static int s_sr_glob_sat = -1;
static int s_sr_glob_gold = -1;
static int s_sr_glob_troop_lim = -1;
static int s_sr_glob_ap = -1;
static int s_sr_glob_ap_max = -1;
static int s_sr_glob_battle = -1;
static int s_sr_glob_system_bar = -1, s_sr_glob_battle_place = -1;
static int s_sr_fn_set_number = -1, s_sr_fn_set_battle_hp = -1, s_sr_fn_get_busho_no = -1;
static int s_sr_st_chara = -1;
static int s_sr_st_seiryoku = -1;

static int sr_find_member_idx(int st_idx, const char *sjis_name, int fallback)
{
	if (!ain || st_idx < 0 || st_idx >= ain->nr_structures)
		return fallback;
	struct ain_struct *st = &ain->structures[st_idx];
	if (sjis_name) {
		for (int i = 0; i < st->nr_members; i++) {
			if (st->members[i].name && !strcmp(st->members[i].name, sjis_name))
				return i;
		}
	}
	return (fallback >= 0 && fallback < st->nr_members) ? fallback : -1;
}

static void vm_inspect_sr_ain(void)
{
	if (!vm_globals_ready() || ain == s_sr_inspected_ain)
		return;
	s_sr_inspected_ain = ain;
	s_sr_compatible = false;
	if (!game_sengokurance)
		return;
	s_sr_glob_seiryoku = vm_find_global_idx(SR_G_SEIRYOKU, NULL);
	s_sr_glob_gs = vm_find_global_idx("gs", NULL);
	s_sr_glob_turn = vm_find_global_idx(SR_G_TURN, NULL);
	s_sr_glob_sat = vm_find_global_idx(SR_G_SATISFACTION, NULL);
	s_sr_glob_gold = vm_find_global_idx(SR_G_GOLD, NULL);
	s_sr_glob_troop_lim = vm_find_global_idx(SR_G_TROOP_LIMIT, NULL);
	s_sr_glob_ap = vm_find_global_idx(SR_G_AP, NULL);
	s_sr_glob_ap_max = vm_find_global_idx(SR_G_AP_MAX, NULL);
	s_sr_glob_battle = vm_find_global_idx("g_sBattle", NULL);
	s_sr_glob_system_bar = vm_find_global_idx("g_sSystemBar", NULL);
	s_sr_glob_battle_place = vm_find_global_idx("g_sPlace", NULL);
	s_sr_fn_set_number = vm_find_hud_setter("tagNumber@setValue");
	s_sr_fn_set_battle_hp = vm_find_hud_setter("tagBattleHp@set");
	s_sr_fn_get_busho_no = ain_get_function(ain, "tagBattleBusho@getBushoNo");
	if (s_sr_fn_get_busho_no >= 0) {
		const struct ain_function *f = &ain->functions[s_sr_fn_get_busho_no];
		if (f->nr_args != 0 || f->return_type.data != AIN_INT)
			s_sr_fn_get_busho_no = -1;
	}
	s_sr_st_chara = ain_get_struct(ain, "s\x83L\x83\x83\x83\x89");
	s_sr_st_seiryoku = ain_get_struct(ain, "s\x90\xa8\x97\xcd");
	if (s_sr_glob_gs >= 0 && s_sr_glob_gold >= 0 && s_sr_glob_ap >= 0 && s_sr_glob_ap_max >= 0
			&& s_sr_st_chara >= 0 && sr_find_member_idx(s_sr_st_chara, "\x95\xba\x90\x94", 21) == 21
			&& sr_find_member_idx(s_sr_st_chara, "\x95\xba\x90\x94\x8d\x87\x90\xed", 23) == 23) {
		s_sr_compatible = true;
	}
}

static struct page *sr_faction_char_array(int faction)
{
	struct page *gs = s_sr_glob_gs >= 0 ? vm_get_global_page_idx(s_sr_glob_gs) : NULL;
	if (!gs || gs->type != ARRAY_PAGE || gs->array.rank != 2)
		return NULL;
	if (faction < 0 || faction >= gs->nr_vars)
		return NULL;
	struct page *row = vm_safe_get_page(gs->values[faction].i);
	return (row && row->type == ARRAY_PAGE && row->array.rank == 1) ? row : NULL;
}

static struct page *sr_get_chara_page(int faction, int char_idx)
{
	struct page *row = sr_faction_char_array(faction);
	if (!row || char_idx < 1 || char_idx >= row->nr_vars)
		return NULL;
	struct page *ch = vm_safe_get_page(row->values[char_idx].i);
	if (!ch || ch->type != STRUCT_PAGE || ch->index != s_sr_st_chara || ch->nr_vars < 70)
		return NULL;
	if (ch->values[1].i <= 0)
		return NULL;
	return ch;
}

static void sr_refresh_number(struct page *bar, const char *field, int global)
{
	if (global < 0)
		return;
	union vm_value *widget = vm_get_struct_member(bar, field, NULL);
	struct page *number = widget ? vm_safe_get_page(widget->i) : NULL;
	if (!vm_get_field_int(number, "m_bActive", NULL, 0))
		return;
	int value = global_get(global).i;
	if (value != vm_get_field_int(number, "m_nValue", NULL, -1))
		vm_call_hud_setter(s_sr_fn_set_number, widget->i, value, 0);
}

static struct page *sr_battle_widgets(bool allies)
{
	struct page *place = vm_get_global_page_idx(s_sr_glob_battle_place);
	struct page *widgets = vm_get_field_page(place, allies ? "m_asMikaBusho" : "m_asTekiBusho", NULL);
	return widgets && widgets->type == ARRAY_PAGE && widgets->array.rank == 1 ? widgets : NULL;
}

static bool sr_is_active_busho(struct page *busho)
{
	return vm_get_field_int(busho, "m_bBattle", NULL, 0)
		&& !vm_get_field_int(busho, "m_bDestroy", NULL, 0);
}

static int sr_get_busho_index(int slot, struct page *busho)
{
	struct page *id = vm_get_field_page(busho, "m_sBushoNo", NULL);
	/* m_nCharaNo holds a serial number, not an index into gs. */
	if (vm_get_field_int(id, "m_nCharaNo", NULL, 0) <= 0 || s_sr_fn_get_busho_no < 0
			|| vm_callstack_has_fno(s_sr_fn_get_busho_no))
		return -1;
	return vm_dd_call_int_0(s_sr_fn_get_busho_no, slot, -1);
}

static void sr_refresh_battle_hp(bool allies)
{
	struct page *widgets = sr_battle_widgets(allies);
	if (!widgets)
		return;
	for (int i = 0; i < widgets->nr_vars; i++) {
		int slot = widgets->values[i].i;
		struct page *busho = vm_safe_get_page(slot);
		if (!sr_is_active_busho(busho) || !vm_get_field_int(busho, "m_bShow", NULL, 0))
			continue;
		union vm_value *hp = vm_get_struct_member(busho, "m_sHp", NULL);
		if (!hp || !vm_get_field_int(vm_safe_get_page(hp->i), "m_bShow", NULL, 0))
			continue;
		struct page *id = vm_get_field_page(busho, "m_sBushoNo", NULL);
		int faction = vm_get_field_int(id, "m_nSeiryokuNo", NULL, -1);
		int character = sr_get_busho_index(slot, busho);
		if (sr_get_chara_page(faction, character))
			vm_call_hud_setter(s_sr_fn_set_battle_hp, hp->i, faction, character);
	}
}

static int sr_detect_player_faction(void)
{
	struct page *bt = s_sr_glob_battle >= 0 ? vm_get_global_page_idx(s_sr_glob_battle) : NULL;
	if (bt && vm_get_field_int(bt, "m_nEnable", NULL, 0) != 0) {
		int m = vm_get_field_int(bt, "m_nMikataNo", NULL, 1);
		if (m > 0 && sr_faction_char_array(m))
			return m;
	}
	if (sr_faction_char_array(1))
		return 1;
	return 1;
}

static bool sr_has_active_roster_unit(bool allies)
{
	struct page *arr = sr_battle_widgets(allies);
	if (!arr)
		return false;
	for (int i = 0; i < arr->nr_vars; i++) {
		if (sr_is_active_busho(vm_safe_get_page(arr->values[i].i)))
			return true;
	}
	return false;
}

static bool sr_is_in_battle(void)
{
	struct page *bt = s_sr_glob_battle >= 0 ? vm_get_global_page_idx(s_sr_glob_battle) : NULL;
	if (!bt || vm_get_field_int(bt, "m_nEnable", NULL, 0) <= 0)
		return false;
	return sr_has_active_roster_unit(false) || sr_has_active_roster_unit(true);
}

static void sr_refresh_hud(bool refresh_battle_hp)
{
	if (!s_sr_compatible)
		return;
	struct page *bar = vm_get_global_page_idx(s_sr_glob_system_bar);
	sr_refresh_number(bar, "m_sGold", s_sr_glob_gold);
	sr_refresh_number(bar, "m_sManzoku", s_sr_glob_sat);
	if (refresh_battle_hp && sr_is_in_battle()) {
		sr_refresh_battle_hp(true);
		sr_refresh_battle_hp(false);
	}
}

static void sr_iterate_battle_roster(bool allies, bool heal_allies, bool drain_enemies,
		int *out_cnt, int *out_total_troops)
{
	struct page *arr = sr_battle_widgets(allies);
	if (!arr)
		return;
	int cnt = 0, total = 0;
	for (int i = 0; i < arr->nr_vars; i++) {
		int slot = arr->values[i].i;
		struct page *bb = vm_safe_get_page(slot);
		if (!sr_is_active_busho(bb))
			continue;
		struct page *bno = vm_get_field_page(bb, "m_sBushoNo", NULL);
		if (!bno)
			continue;
		int f = vm_get_field_int(bno, "m_nSeiryokuNo", NULL, -1);
		int c = sr_get_busho_index(slot, bb);
		struct page *ch = sr_get_chara_page(f, c);
		if (!ch)
			continue;
		if (heal_allies) {
			int32_t mx = ch->values[22].i > 0 ? ch->values[22].i : 1;
			if (ch->values[21].i < mx)
				ch->values[21].i = mx;
			if (ch->values[23].i < mx)
				ch->values[23].i = mx;
			if (ch->values[30].i < ch->values[29].i)
				ch->values[30].i = ch->values[29].i;
		}
		if (drain_enemies && ch->values[23].i > 1)
			ch->values[23].i = 1;
		cnt++;
		total += ch->values[23].i;
	}
	if (out_cnt) *out_cnt = cnt;
	if (out_total_troops) *out_total_troops = total;
}

static void vm_enforce_sr_locks(int sr_mask)
{
	vm_inspect_sr_ain();
	if (!s_sr_compatible || !sr_mask)
		return;
	int turn = s_sr_glob_turn >= 0 ? global_get(s_sr_glob_turn).i : 0;
	int pf = sr_detect_player_faction();
	struct page *row = sr_faction_char_array(pf);
	if (turn <= 0 && !row)
		return;

	if ((sr_mask & LOCK_SR_INF_GOLD) && s_sr_glob_gold >= 0) {
		if (global_get(s_sr_glob_gold).i < 999999)
			heap[0].page->values[s_sr_glob_gold].i = 999999;
	}
	if ((sr_mask & LOCK_SR_INF_AP) && s_sr_glob_ap >= 0 && s_sr_glob_ap_max >= 0) {
		int32_t ap_max = global_get(s_sr_glob_ap_max).i;
		if (ap_max > 0 && global_get(s_sr_glob_ap).i < ap_max)
			heap[0].page->values[s_sr_glob_ap].i = ap_max;
	}
	if ((sr_mask & LOCK_SR_MAX_SAT) && s_sr_glob_sat >= 0) {
		if (global_get(s_sr_glob_sat).i < 999)
			heap[0].page->values[s_sr_glob_sat].i = 999;
	}
	if (row && (sr_mask & (LOCK_SR_ALLY_FULL_HP | LOCK_SR_UNLIM_ACTION))) {
		for (int c = 1; c < row->nr_vars; c++) {
			struct page *ch = sr_get_chara_page(pf, c);
			if (!ch)
				continue;
			if (sr_mask & LOCK_SR_ALLY_FULL_HP) {
				int32_t mx = ch->values[22].i;
				if (mx > 0 && ch->values[21].i < mx)
					ch->values[21].i = mx;
				if (ch->values[30].i < ch->values[29].i)
					ch->values[30].i = ch->values[29].i;
			}
			if (sr_mask & LOCK_SR_UNLIM_ACTION)
				ch->values[8].i = 0;
		}
	}
	if (sr_is_in_battle()) {
		if (sr_mask & LOCK_SR_ALLY_FULL_HP)
			sr_iterate_battle_roster(true, true, false, NULL, NULL);
		if (sr_mask & LOCK_SR_ONE_HIT_KILL)
			sr_iterate_battle_roster(false, false, true, NULL, NULL);
	}
}

static void sr_apply_char_edit(struct page *ch, cJSON *ce)
{
	if (!ch || !ce)
		return;
	cJSON *it;
	if ((it = cJSON_GetObjectItem(ce, "troopsMax")) && cJSON_IsNumber(it))
		ch->values[22].i = vm_dd_clamp(it->valueint, 1, 99999);
	if ((it = cJSON_GetObjectItem(ce, "troops")) && cJSON_IsNumber(it)) {
		int32_t tv = vm_dd_clamp(it->valueint, 0, 99999);
		if (tv > ch->values[22].i)
			ch->values[22].i = tv;
		ch->values[21].i = tv;
		if (sr_is_in_battle())
			ch->values[23].i = tv;
	}
	if ((it = cJSON_GetObjectItem(ce, "atk")) && cJSON_IsNumber(it))
		ch->values[32].i = vm_dd_clamp(it->valueint, 0, 999);
	if ((it = cJSON_GetObjectItem(ce, "def")) && cJSON_IsNumber(it))
		ch->values[33].i = vm_dd_clamp(it->valueint, 0, 999);
	if ((it = cJSON_GetObjectItem(ce, "intel")) && cJSON_IsNumber(it))
		ch->values[34].i = vm_dd_clamp(it->valueint, 0, 999);
	if ((it = cJSON_GetObjectItem(ce, "spd")) && cJSON_IsNumber(it))
		ch->values[35].i = vm_dd_clamp(it->valueint, 0, 999);
	if ((it = cJSON_GetObjectItem(ce, "love")) && cJSON_IsNumber(it))
		ch->values[11].i = vm_dd_clamp(it->valueint, 0, 99999);
	if ((it = cJSON_GetObjectItem(ce, "wazaMax")) && cJSON_IsNumber(it))
		ch->values[29].i = vm_dd_clamp(it->valueint, 0, 99);
	if ((it = cJSON_GetObjectItem(ce, "wazaNow")) && cJSON_IsNumber(it))
		ch->values[30].i = vm_dd_clamp(it->valueint, 0, 99);
	if ((it = cJSON_GetObjectItem(ce, "resetAction")) &&
	    (cJSON_IsTrue(it) || (cJSON_IsNumber(it) && it->valueint != 0)))
		ch->values[8].i = 0;
}

static void vm_execute_sr_action(const char *json_str)
{
	if (!json_str || !json_str[0])
		return;
	cJSON *act = cJSON_Parse(json_str);
	if (!act)
		return;

	vm_inspect_sr_ain();
	cJSON *it;
	bool persist_cfg = false;
	int save_r10_mask = 0;
	pthread_mutex_lock(&g_mod_mutex);
	if ((it = cJSON_GetObjectItem(act, "setSrLockMask")) && cJSON_IsNumber(it)) {
		g_sr_lock_mask = it->valueint;
		g_has_sr_lock_mask_init = true;
		persist_cfg = true;
	}
	save_r10_mask = g_live_lock_mask;
	pthread_mutex_unlock(&g_mod_mutex);
	vm_apply_ui_scale_from_json(act, &persist_cfg, &save_r10_mask);
	if (persist_cfg)
		persist_modifier_config_from_native(save_r10_mask);

	if (!s_sr_compatible) {
		cJSON_Delete(act);
		return;
	}
	int pf = sr_detect_player_faction();
	struct page *row = sr_faction_char_array(pf);

	if ((it = cJSON_GetObjectItem(act, "srAddGold")) && cJSON_IsNumber(it) && s_sr_glob_gold >= 0) {
		int64_t next = (int64_t)global_get(s_sr_glob_gold).i + (int64_t)it->valueint;
		heap[0].page->values[s_sr_glob_gold].i = vm_dd_clamp((int32_t)(next > 99999999LL ? 99999999LL : next), 0, 99999999);
	}
	if ((it = cJSON_GetObjectItem(act, "srGold")) && cJSON_IsNumber(it) && s_sr_glob_gold >= 0)
		heap[0].page->values[s_sr_glob_gold].i = vm_dd_clamp(it->valueint, 0, 99999999);
	if ((it = cJSON_GetObjectItem(act, "srAp")) && cJSON_IsNumber(it) && s_sr_glob_ap >= 0)
		heap[0].page->values[s_sr_glob_ap].i = vm_dd_clamp(it->valueint, 0, 999);
	if ((it = cJSON_GetObjectItem(act, "srApMax")) && cJSON_IsNumber(it) && s_sr_glob_ap_max >= 0)
		heap[0].page->values[s_sr_glob_ap_max].i = vm_dd_clamp(it->valueint, 1, 99);
	if ((it = cJSON_GetObjectItem(act, "srRefillAp")) && cJSON_IsTrue(it) && s_sr_glob_ap >= 0 && s_sr_glob_ap_max >= 0)
		heap[0].page->values[s_sr_glob_ap].i = global_get(s_sr_glob_ap_max).i;
	if ((it = cJSON_GetObjectItem(act, "srSatisfaction")) && cJSON_IsNumber(it) && s_sr_glob_sat >= 0)
		heap[0].page->values[s_sr_glob_sat].i = vm_dd_clamp(it->valueint, 0, 9999);
	if ((it = cJSON_GetObjectItem(act, "srTroopLimit")) && cJSON_IsNumber(it) && s_sr_glob_troop_lim >= 0)
		heap[0].page->values[s_sr_glob_troop_lim].i = vm_dd_clamp(it->valueint, 0, 99999);

	if ((it = cJSON_GetObjectItem(act, "srResetAllActions")) && cJSON_IsTrue(it) && row) {
		for (int c = 1; c < row->nr_vars; c++) {
			struct page *ch = sr_get_chara_page(pf, c);
			if (ch) ch->values[8].i = 0;
		}
	}
	if ((it = cJSON_GetObjectItem(act, "srHealAllTroops")) && cJSON_IsTrue(it) && row) {
		for (int c = 1; c < row->nr_vars; c++) {
			struct page *ch = sr_get_chara_page(pf, c);
			if (!ch) continue;
			if (ch->values[22].i > 0) {
				ch->values[21].i = ch->values[22].i;
				if (sr_is_in_battle()) ch->values[23].i = ch->values[22].i;
			}
			if (ch->values[29].i > 0)
				ch->values[30].i = ch->values[29].i;
		}
		if (sr_is_in_battle())
			sr_iterate_battle_roster(true, true, false, NULL, NULL);
	}
	if ((it = cJSON_GetObjectItem(act, "srBoostAllParty")) && cJSON_IsTrue(it) && row) {
		for (int c = 1; c < row->nr_vars; c++) {
			struct page *ch = sr_get_chara_page(pf, c);
			if (!ch) continue;
			if (ch->values[22].i < 9999) ch->values[22].i = 9999;
			ch->values[21].i = ch->values[22].i;
			if (sr_is_in_battle()) ch->values[23].i = ch->values[22].i;
			for (int st_m = 32; st_m <= 35; st_m++) {
				if (ch->values[st_m].i < 99) ch->values[st_m].i = 99;
			}
			if (ch->values[29].i > 0) ch->values[30].i = ch->values[29].i;
			if (ch->values[11].i < 99) ch->values[11].i = 99;
		}
	}
	if ((it = cJSON_GetObjectItem(act, "srBattle1HpEnemies")) && cJSON_IsTrue(it) && sr_is_in_battle()) {
		sr_iterate_battle_roster(false, false, true, NULL, NULL);
	}

	cJSON *ce = cJSON_GetObjectItem(act, "srCharEdit");
	if (ce && cJSON_IsObject(ce) && row) {
		cJSON *jc = cJSON_GetObjectItem(ce, "charIdx");
		cJSON *jf = cJSON_GetObjectItem(ce, "factionIdx");
		int target_f = (jf && cJSON_IsNumber(jf)) ? jf->valueint : pf;
		int target_c = (jc && cJSON_IsNumber(jc)) ? jc->valueint : -1;
		if (target_c >= 1) {
			sr_apply_char_edit(sr_get_chara_page(target_f, target_c), ce);
		} else {
			struct page *frow = sr_faction_char_array(target_f);
			if (frow) {
				for (int c = 1; c < frow->nr_vars; c++)
					sr_apply_char_edit(sr_get_chara_page(target_f, c), ce);
			}
		}
	}

	cJSON_Delete(act);
}

static void vm_sr_populate_live_state_json(cJSON *root)
{
	vm_inspect_sr_ain();
	int pf = sr_detect_player_faction();
	struct page *row = sr_faction_char_array(pf);
	int turn = s_sr_glob_turn >= 0 ? global_get(s_sr_glob_turn).i : 0;
	bool in_game = s_sr_compatible && row != NULL && turn >= 0;
	if (!in_game) {
		cJSON_AddBoolToObject(root, "inGame", false);
		return;
	}
	bool in_battle = sr_is_in_battle();
	cJSON_AddBoolToObject(root, "inGame", true);
	cJSON_AddBoolToObject(root, "inBattle", in_battle);
	cJSON_AddNumberToObject(root, "turn", turn);
	cJSON_AddNumberToObject(root, "gold", s_sr_glob_gold >= 0 ? global_get(s_sr_glob_gold).i : 0);
	cJSON_AddNumberToObject(root, "ap", s_sr_glob_ap >= 0 ? global_get(s_sr_glob_ap).i : 0);
	cJSON_AddNumberToObject(root, "apMax", s_sr_glob_ap_max >= 0 ? global_get(s_sr_glob_ap_max).i : 0);
	cJSON_AddNumberToObject(root, "satisfaction", s_sr_glob_sat >= 0 ? global_get(s_sr_glob_sat).i : 0);
	cJSON_AddNumberToObject(root, "troopLimit", s_sr_glob_troop_lim >= 0 ? global_get(s_sr_glob_troop_lim).i : 0);
	if (in_battle) {
		int ec = 0, et = 0;
		sr_iterate_battle_roster(false, false, false, &ec, &et);
		cJSON_AddNumberToObject(root, "enemyCount", ec);
		cJSON_AddNumberToObject(root, "enemyTotalTroops", et);
	}

	cJSON *j_chars = cJSON_CreateArray();
	int added = 0;
	for (int c = 1; c < row->nr_vars && added < 80; c++) {
		struct page *ch = sr_get_chara_page(pf, c);
		if (!ch || !string_index_valid(ch->values[0].i))
			continue;
		struct string *ns = heap_get_string(ch->values[0].i);
		char *name_u = (ns && ns->size > 0 && ns->text[0]) ? mod_sjis2utf_safe(ns->text, (size_t)ns->size) : NULL;
		if (!name_u || !name_u[0]) {
			free(name_u);
			continue;
		}
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "factionIdx", pf);
		cJSON_AddNumberToObject(jc, "charIdx", c);
		cJSON_AddStringToObject(jc, "name", name_u);
		free(name_u);
		cJSON_AddNumberToObject(jc, "troops", ch->values[21].i);
		cJSON_AddNumberToObject(jc, "troopsMax", ch->values[22].i);
		cJSON_AddNumberToObject(jc, "battleTroops", ch->values[23].i);
		cJSON_AddNumberToObject(jc, "atk", ch->values[32].i);
		cJSON_AddNumberToObject(jc, "def", ch->values[33].i);
		cJSON_AddNumberToObject(jc, "intel", ch->values[34].i);
		cJSON_AddNumberToObject(jc, "spd", ch->values[35].i);
		cJSON_AddNumberToObject(jc, "love", ch->values[11].i);
		cJSON_AddNumberToObject(jc, "actionDone", ch->values[8].i);
		cJSON_AddNumberToObject(jc, "wazaMax", ch->values[29].i);
		cJSON_AddNumberToObject(jc, "wazaNow", ch->values[30].i);
		cJSON_AddItemToArray(j_chars, jc);
		added++;
	}
	cJSON_AddItemToObject(root, "srCharacters", j_chars);
}

/* =========================================================================
 * Rance 03 Live In-Game Modifier Support (Rance03.ain v6)
 * ========================================================================= */

enum {
	R03_FN_HP = 0, R03_FN_SET_CD, R03_FN_OVER_CD, R03_FN_GET_CD,
	R03_FN_PARTY_CD, R03_FN_GET_PARTY_CD, R03_FN_SET_EXP, R03_FN_ADD_EXP,
	R03_FN_SET_GOLD, R03_FN_ADD_GOLD, R03_FN_USE_ITEM, R03_FN_DEC_ITEM,
	R03_FN_CAMP_LION, R03_FN_DROP, R03_FN_MON_SET_HP, R03_FN_DEC_MON_HP,
	R03_HOOK_COUNT
};

static const struct { const char *name; int32_t crc; int args; } s_r03_sigs[R03_HOOK_COUNT] = {
	{ "Player@setHp", -1617599902, 1 },
	{ "Player@setCoolTime", -729587211, 1 },
	{ "Player@setOverCoolTime", -1464921706, 2 },
	{ "Player@getCoolTime", -1471100255, 1 },
	{ "PlayerCommonParam@setPartyCoolTime", 1629503482, 1 },
	{ "PlayerCommonParam@getPartyCoolTime", 1212459943, 0 },
	{ "Player@setExp", -534610734, 1 },
	{ "Player@addExp", -2065683727, 1 },
	{ "PlayerCommonParam@setGold", 1119748456, 1 },
	{ "PlayerCommonParam@addGold", 549543388, 1 },
	{ "PlayerCommonParam@useItem", -310161395, 1 },
	{ "PlayerCommonParam@decItem", -1220549445, 1 },
	{ "SceneCamp@onClickSaveLion", -36293420, 1 },
	{ "BattleResult@isGetItemByLot", -181551202, 0 },
	{ "MonsterInstance@setHp", 675961301, 1 },
	{ "BattleModel@decMonsterHp", -1430746262, 0 },
};

static struct ain *s_r03_inspected_ain = NULL;
static bool s_r03_compatible = false;
static bool s_r03_applying = false;
static int s_r03_player_type = -1, s_r03_common_type = -1, s_r03_result_type = -1;
static int s_r03_item_type = -1, s_r03_count_type = -1, s_r03_mon_type = -1;
static int s_r03_players_glob = -1, s_r03_common_glob = -1, s_r03_items_glob = -1;
static int s_r03_funcs[R03_HOOK_COUNT];

static struct page *r03_checked_page(int slot, enum page_type kind, int type, int min_vars)
{
	if (!page_index_valid(slot))
		return NULL;
	struct page *p = heap[slot].page;
	if (!p || p->type != kind || p->nr_vars < min_vars || (type >= 0 && p->index != type))
		return NULL;
	return p;
}

static bool r03_member_is(int st_idx, int m_idx, const char *name, enum ain_data_type dt)
{
	if (!ain || st_idx < 0 || st_idx >= ain->nr_structures || m_idx < 0)
		return false;
	struct ain_struct *s = &ain->structures[st_idx];
	return m_idx < s->nr_members && !strcmp(s->members[m_idx].name, name) && s->members[m_idx].type.data == dt;
}

/* =========================================================================
 * Rance 08 (Rance Quest Magnum) Live In-Game Modifier Support
 * ========================================================================= */

static bool s_r08_compatible = false;
static bool s_r08_applying = false;
static int s_r08_sc_type = -1, s_r08_skill_type = -1;
static int s_r08_c_glob = -1, s_r08_pta_glob = -1, s_r08_ptb_glob = -1;
static int s_r08_gold_glob = -1, s_r08_fame_glob = -1, s_r08_famemax_glob = -1;
static int s_r08_mag_glob = -1, s_r08_magmax_glob = -1, s_r08_dm_glob = -1;
static int s_r08_fn_c_hp = -1, s_r08_fn_c_skill_cnt = -1;
static int s_r08_battle_chara_glob = -1, s_r08_battle_chara_type = -1;
static int s_r08_fn_update_battle_card = -1;

static void r08_on_ain_init(void)
{
	s_r08_compatible = false;
	s_r08_applying = false;
	if (!ain || !game_rance8)
		return;
	s_r08_sc_type      = ain_get_struct(ain, "s\x82\x62");
	s_r08_skill_type   = ain_get_struct(ain, "s\x83\x58\x83\x4c\x83\x8b");
	s_r08_c_glob       = ain_get_global(ain, "\x82\x62");
	s_r08_pta_glob     = ain_get_global(ain, "PTA");
	s_r08_ptb_glob     = ain_get_global(ain, "PTB");
	s_r08_gold_glob    = ain_get_global(ain, "\x81\x9f\x8b\xe0");
	s_r08_fame_glob    = ain_get_global(ain, "\x81\x9f\x96\xbc\x90\xba");
	s_r08_famemax_glob = ain_get_global(ain, "\x81\x9f\x96\xbc\x90\xba\x8d\xc5\x91\xe5");
	s_r08_mag_glob     = ain_get_global(ain, "\x81\x9f\x83\x7d\x83\x4f\x83\x69\x83\x80");
	s_r08_magmax_glob  = ain_get_global(ain, "\x81\x9f\x83\x7d\x83\x4f\x83\x69\x83\x80\x8d\xc5\x91\xe5");
	s_r08_dm_glob      = ain_get_global(ain, "\x81\x9f\x90\xed\x93\xac\x81\x5e\x82\x63\x82\x6c");
	s_r08_fn_c_hp      = ain_get_function(ain, "\x82\x62\x91\xcc\x97\xcd");
	s_r08_fn_c_skill_cnt = ain_get_function(ain, "\x82\x62\x83\x58\x83\x4c\x83\x8b\x8e\x67\x97\x70\x89\xf1\x90\x94");
	s_r08_battle_chara_glob = ain_get_global(ain, "g_sBattleChara");
	s_r08_battle_chara_type = ain_get_struct(ain, "T_BattleChara");
	s_r08_fn_update_battle_card = ain_get_function(ain, "T_BattleChara@updateCard");
	if (s_r08_fn_update_battle_card >= 0) {
		const struct ain_function *f = &ain->functions[s_r08_fn_update_battle_card];
		if (f->nr_args != 0 || f->return_type.data != AIN_BOOL)
			s_r08_fn_update_battle_card = -1;
	}
	if (s_r08_sc_type < 0 || s_r08_skill_type < 0 || s_r08_c_glob < 0
			|| s_r08_pta_glob < 0 || s_r08_ptb_glob < 0 || s_r08_gold_glob < 0)
		return;
	struct ain_struct *sc = &ain->structures[s_r08_sc_type];
	struct ain_struct *ss = &ain->structures[s_r08_skill_type];
	if (sc->nr_members < 45 || ss->nr_members < 12)
		return;
	s_r08_compatible = true;
}

static struct page *r08_get_c_array(void)
{
	if (!s_r08_compatible || !vm_globals_ready() || s_r08_c_glob < 0)
		return NULL;
	struct page *p = r03_checked_page(heap[0].page->values[s_r08_c_glob].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0);
	return (p && p->array.rank == 1 && p->array.struct_type == s_r08_sc_type && p->nr_vars <= 4096) ? p : NULL;
}

static struct page *r08_get_pt_array(int glob_idx)
{
	if (!s_r08_compatible || !vm_globals_ready() || glob_idx < 0)
		return NULL;
	struct page *p = r03_checked_page(heap[0].page->values[glob_idx].i, ARRAY_PAGE, AIN_ARRAY_INT, 0);
	return (p && p->array.rank == 1 && p->nr_vars <= 64) ? p : NULL;
}

static bool r08_index_in_pt(int glob_idx, int c_idx)
{
	struct page *pt = r08_get_pt_array(glob_idx);
	if (!pt || c_idx <= 0)
		return false;
	for (int i = 1; i < pt->nr_vars; i++) {
		if (pt->values[i].i == c_idx)
			return true;
	}
	return false;
}

static bool r08_is_ally_c(int c_idx)
{
	if (c_idx <= 0 || r08_index_in_pt(s_r08_ptb_glob, c_idx))
		return false;
	if (r08_index_in_pt(s_r08_pta_glob, c_idx))
		return true;
	struct page *carr = r08_get_c_array();
	if (!carr || c_idx >= carr->nr_vars)
		return false;
	struct page *c = r03_checked_page(carr->values[c_idx].i, STRUCT_PAGE, s_r08_sc_type, 45);
	return c && c->values[1].i > 0;
}

static bool r08_is_enemy_c(int c_idx)
{
	return c_idx > 0 && r08_index_in_pt(s_r08_ptb_glob, c_idx);
}

static void r08_replenish_chara_skills(struct page *c)
{
	struct page *skills = r03_checked_page(c->values[44].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0);
	if (!skills || skills->array.struct_type != s_r08_skill_type || skills->nr_vars > 64)
		return;
	for (int j = 0; j < skills->nr_vars; j++) {
		struct page *sk = r03_checked_page(skills->values[j].i, STRUCT_PAGE, s_r08_skill_type, 12);
		if (!sk) continue;
		if (sk->values[6].i > 0 && sk->values[5].i < sk->values[6].i)
			sk->values[5].i = sk->values[6].i;
		sk->values[8].i = 0;
		sk->values[11].i = 0;
	}
}

/* =========================================================================
 * Rance 09 (The Helman Revolution) Live In-Game Modifier Support
 * ========================================================================= */

static bool s_r09_compatible = false;
static bool s_r09_applying = false;
static int s_r09_cmgr_type = -1, s_r09_chara_type = -1, s_r09_param_type = -1;
static int s_r09_arrval_type = -1, s_r09_arrflg_type = -1, s_r09_dmg_type = -1;
static int s_r09_chara_glob = -1, s_r09_save_glob = -1;
static int s_r09_fn_ally_dmg = -1, s_r09_fn_mon_dmg = -1, s_r09_fn_obs_dmg = -1;
static int s_r09_fn_set_pass = -1, s_r09_fn_gold_drop = -1;
static int s_r09_party_glob = -1, s_r09_party_card_type = -1, s_r09_fn_update_party_card = -1;
static int s_r09_system_bar_delegate = -1, s_r09_fn_update_system_bar = -1;

static void r09_on_ain_init(void)
{
	s_r09_compatible = false;
	s_r09_applying = false;
	if (!ain || !game_rance9)
		return;
	s_r09_cmgr_type   = ain_get_struct(ain, "T_CharaDataManager");
	s_r09_chara_type  = ain_get_struct(ain, "T_CharaData");
	s_r09_param_type  = ain_get_struct(ain, "T_Parameter");
	s_r09_arrval_type = ain_get_struct(ain, "T_ArrayValue");
	s_r09_arrflg_type = ain_get_struct(ain, "T_ArrayFlag");
	s_r09_dmg_type    = ain_get_struct(ain, "T_DamageValue");
	s_r09_chara_glob  = ain_get_global(ain, "g_sCharaData");
	s_r09_save_glob   = ain_get_global(ain, "g_sSaveData");
	s_r09_fn_ally_dmg = ain_get_function(ain, "\x8e\xa9\x8c\x52\x5f\x83\x5f\x83\x81\x81\x5b\x83\x57\x8f\x88\x97\x9d");
	s_r09_fn_mon_dmg  = ain_get_function(ain, "\x93\x47\x8c\x52\x5f\x83\x5f\x83\x81\x81\x5b\x83\x57\x8f\x88\x97\x9d");
	s_r09_fn_obs_dmg  = ain_get_function(ain, "\x8f\xe1\x8a\x51\x95\xa8\x5f\x83\x5f\x83\x81\x81\x5b\x83\x57\x8f\x88\x97\x9d");
	s_r09_fn_set_pass = ain_get_function(ain, "T_CharaData@setPass");
	s_r09_fn_gold_drop = ain_get_function(ain, "\x90\xed\x93\xac\x5f\x82\x66\x82\x6e\x82\x6b\x82\x63\x83\x68\x83\x8d\x83\x62\x83\x76\x8a\x6d\x94\x46");
	s_r09_party_glob = ain_get_global(ain, "g_sParty");
	s_r09_party_card_type = ain_get_struct(ain, "T_PartyList");
	s_r09_fn_update_party_card = ain_get_function(ain, "T_PartyList@update");
	if (s_r09_fn_update_party_card >= 0) {
		const struct ain_function *f = &ain->functions[s_r09_fn_update_party_card];
		if (f->nr_args != 0 || f->return_type.data != AIN_VOID)
			s_r09_fn_update_party_card = -1;
	}
	s_r09_system_bar_delegate = ain_get_global(ain, "g_dgSystemBarUpdate");
	s_r09_fn_update_system_bar = vm_find_hud_setter("\x83\x56\x83\x58\x83\x65\x83\x80\x83\x6f\x81\x5b\x5f\x8d\x58\x90\x56");
	if (s_r09_cmgr_type < 0 || s_r09_chara_type < 0 || s_r09_param_type < 0
			|| s_r09_arrval_type < 0 || s_r09_arrflg_type < 0 || s_r09_dmg_type < 0
			|| s_r09_chara_glob < 0 || s_r09_save_glob < 0)
		return;
	s_r09_compatible = true;
}

static struct page *r09_unwrap_arr_int(int arrval_slot)
{
	struct page *av = r03_checked_page(arrval_slot, STRUCT_PAGE, s_r09_arrval_type, 1);
	return av ? r03_checked_page(av->values[0].i, ARRAY_PAGE, AIN_ARRAY_INT, 0) : NULL;
}

static struct page *r09_unwrap_arr_bool(int arrflg_slot)
{
	struct page *af = r03_checked_page(arrflg_slot, STRUCT_PAGE, s_r09_arrflg_type, 1);
	return af ? r03_checked_page(af->values[0].i, ARRAY_PAGE, AIN_ARRAY_BOOL, 0) : NULL;
}

static struct page *r09_get_save_data_arr(void)
{
	if (!s_r09_compatible || !vm_globals_ready() || s_r09_save_glob < 0)
		return NULL;
	return r09_unwrap_arr_int(heap[0].page->values[s_r09_save_glob].i);
}

static struct page *r09_get_chara_array(void)
{
	if (!s_r09_compatible || !vm_globals_ready() || s_r09_chara_glob < 0)
		return NULL;
	struct page *mgr = r03_checked_page(heap[0].page->values[s_r09_chara_glob].i, STRUCT_PAGE, s_r09_cmgr_type, 1);
	struct page *arr = mgr ? r03_checked_page(mgr->values[0].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0) : NULL;
	return (arr && arr->array.rank == 1 && arr->array.struct_type == s_r09_chara_type && arr->nr_vars <= 256) ? arr : NULL;
}

static struct page *r09_get_chara_params(struct page *c)
{
	struct page *prm = c ? r03_checked_page(c->values[0].i, STRUCT_PAGE, s_r09_param_type, 2) : NULL;
	return prm ? r09_unwrap_arr_int(prm->values[0].i) : NULL;
}

static char *r09_lookup_chara_name(int chara_id)
{
	const struct ex *ex = main_ex_get_file();
	if (!ex || chara_id <= 0)
		return NULL;
	for (unsigned i = 0; i < ex->nr_blocks; i++) {
		const struct ex_block *b = &ex->blocks[i];
		if (!b->name || strcmp(b->name->text, "\x8e\xa9\x8c\x52\x83\x66\x81\x5b\x83\x5e") != 0)
			continue;
		if (b->val.type != EX_TABLE || !b->val.t)
			return NULL;
		struct ex_table *t = b->val.t;
		if (t->nr_columns < 2)
			return NULL;
		for (unsigned r = 0; r < t->nr_rows; r++) {
			if (t->fields[0].type == EX_INT && t->rows[r][0].i == chara_id
					&& t->fields[1].type == EX_STRING && t->rows[r][1].s) {
				return mod_sjis2utf_safe(t->rows[r][1].s->text, (size_t)t->rows[r][1].s->size);
			}
		}
		return NULL;
	}
	return NULL;
}

static bool s_r01_compatible = false;
static bool s_r01_applying = false;
static int s_r01_player_type = -1, s_r01_mon_type = -1, s_r01_sb_type = -1, s_r01_dmg_type = -1;
static int s_r01_chipdep_type = -1, s_r01_cgmgr_type = -1, s_r01_cg_type = -1;
static int s_r01_repmgr_type = -1, s_r01_rep_type = -1, s_r01_time_type = -1;
static int s_r01_player_glob = -1, s_r01_chipdep_glob = -1, s_r01_time_glob = -1;
static int s_r01_cgmgr_glob = -1, s_r01_repmgr_glob = -1, s_r01_repfunc_glob = -1;
static int s_r01_fn_dec_hp = -1, s_r01_fn_calc_drop = -1;
static int s_r01_fn_set_level = -1, s_r01_fn_set_max_level = -1, s_r01_fn_set_exp = -1;
static int s_r01_fn_set_hp = -1, s_r01_fn_set_max_hp = -1, s_r01_fn_set_luck = -1;
static int s_r01_fn_set_gold = -1, s_r01_fn_set_max_chip = -1, s_r01_fn_set_max_dep = -1;
static int s_r01_fn_clear_bad = -1, s_r01_fn_reg_cg = -1, s_r01_fn_add_replay = -1;
static int s_r01_fn_gameflow_run = -1;

static void r01_on_ain_init(void)
{
	s_r01_compatible = false;
	s_r01_applying = false;
	if (!ain || !game_rance01)
		return;
	s_r01_player_type  = ain_get_struct(ain, "Player");
	s_r01_mon_type     = ain_get_struct(ain, "Monster");
	s_r01_sb_type      = ain_get_struct(ain, "SceneBattle");
	s_r01_dmg_type     = ain_get_struct(ain, "DamageInfomation");
	s_r01_chipdep_type = ain_get_struct(ain, "ChipDepository");
	s_r01_cgmgr_type   = ain_get_struct(ain, "CgListManager");
	s_r01_cg_type      = ain_get_struct(ain, "CgList");
	s_r01_repmgr_type  = ain_get_struct(ain, "ReplayListManager");
	s_r01_rep_type     = ain_get_struct(ain, "ReplayList");
	s_r01_time_type    = ain_get_struct(ain, "GameTime");
	s_r01_player_glob  = ain_get_global(ain, "g_player");
	s_r01_chipdep_glob = ain_get_global(ain, "g_chipDepository");
	s_r01_time_glob    = ain_get_global(ain, "g_gameTime");
	s_r01_cgmgr_glob   = ain_get_global(ain, "g_cgListManager");
	s_r01_repmgr_glob  = ain_get_global(ain, "g_replayListManager");
	s_r01_repfunc_glob = ain_get_global(ain, "g_replayFuncList");
	s_r01_fn_dec_hp        = ain_get_function(ain, "SceneBattle@decHp");
	s_r01_fn_calc_drop     = ain_get_function(ain, "SceneBattle@calcDropItem");
	s_r01_fn_set_level     = ain_get_function(ain, "Player@setLevel");
	s_r01_fn_set_max_level = ain_get_function(ain, "Player@setMaxLevel");
	s_r01_fn_set_exp       = ain_get_function(ain, "Player@setExp");
	s_r01_fn_set_hp        = ain_get_function(ain, "Player@setHp");
	s_r01_fn_set_max_hp    = ain_get_function(ain, "Player@setMaxHp");
	s_r01_fn_set_luck      = ain_get_function(ain, "Player@setLuck");
	s_r01_fn_set_gold      = ain_get_function(ain, "Player@setGold");
	s_r01_fn_set_max_chip  = ain_get_function(ain, "Player@setMaxChipCount");
	s_r01_fn_set_max_dep   = ain_get_function(ain, "Player@setMaxChipCountDepository");
	s_r01_fn_clear_bad     = ain_get_function(ain, "Player@clearBadCondition");
	s_r01_fn_reg_cg        = ain_get_function(ain, "\x82\x62\x82\x66\x83\x82\x81[\x83\x68\x93\x6f\x98\x5e");
	s_r01_fn_add_replay    = ain_get_function(ain, "addReplayModeFuncList");
	s_r01_fn_gameflow_run  = ain_get_function(ain, "GameFlowManager@run");
	if (s_r01_player_type < 0 || s_r01_mon_type < 0 || s_r01_sb_type < 0 || s_r01_dmg_type < 0
			|| s_r01_chipdep_type < 0 || s_r01_player_glob < 0)
		return;
	struct ain_struct *sp = &ain->structures[s_r01_player_type];
	if (sp->nr_members < 17)
		return;
	s_r01_compatible = true;
}

static struct page *r01_get_player(int *out_slot)
{
	if (!s_r01_compatible || !vm_globals_ready() || s_r01_player_glob < 0)
		return NULL;
	int slot = heap[0].page->values[s_r01_player_glob].i;
	struct page *p = r03_checked_page(slot, STRUCT_PAGE, s_r01_player_type, 17);
	if (out_slot) *out_slot = p ? slot : -1;
	return p;
}

static void r01_clear_all_bad_conditions(int p_slot, struct page *p)
{
	if (!p || s_r01_fn_clear_bad < 0)
		return;
	for (int iter = 0; iter < 32; iter++) {
		struct page *bc = r03_checked_page(p->values[12].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0);
		if (!bc || bc->nr_vars <= 0)
			break;
		struct page *entry = r03_checked_page(bc->values[0].i, STRUCT_PAGE, -1, 1);
		if (!entry || !string_index_valid(entry->values[0].i))
			break;
		struct string *id_s = heap_get_string(entry->values[0].i);
		if (!id_s) break;
		int prev_n = bc->nr_vars;
		bool saved_dg = dg_callbegin_pending;
		dg_callbegin_pending = false;
		int ss = heap_alloc_string(make_string(id_s->text, id_s->size));
		stack_push(ss);
		vm_call(s_r01_fn_clear_bad, p_slot);
		dg_callbegin_pending = saved_dg;
		p = r01_get_player(NULL);
		struct page *bc_after = p ? r03_checked_page(p->values[12].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0) : NULL;
		if (!bc_after || bc_after->nr_vars >= prev_n)
			break;
	}
}

static bool s_r02_compatible = false;
static bool s_r02_applying = false;
static int s_r02_chara_type = -1, s_r02_enemy_type = -1, s_r02_cgdoc_type = -1, s_r02_cgitem_type = -1;
static int s_r02_cd_glob = -1, s_r02_gold_glob = -1, s_r02_karma_glob = -1;
static int s_r02_item_world = -1, s_r02_item_tree = -1, s_r02_item_potion = -1;
static int s_r02_item_candy = -1, s_r02_item_light = -1, s_r02_item_rope = -1;
static int s_r02_battle_glob = -1, s_r02_cgdoc_glob = -1;
static int s_r02_stat_sprites_glob = -1;
static int s_r02_fn_enemy_dmg = -1, s_r02_fn_ally_dmg = -1, s_r02_fn_heal_all = -1;
static int s_r02_fn_disp_stat = -1, s_r02_fn_reg_cg = -1, s_r02_fn_reg_rem = -1;

static void r02_on_ain_init(void)
{
	s_r02_compatible = false;
	s_r02_applying = false;
	if (!ain || !game_rance02)
		return;
	s_r02_chara_type  = ain_get_struct(ain, "\x83\x4c\x83\x83\x83\x89\x83\x66\x81[\x83\x5e");
	s_r02_enemy_type  = ain_get_struct(ain, "\x93\x47\x83\x66\x81[\x83\x5e");
	s_r02_cgdoc_type  = ain_get_struct(ain, "_CCgModeDocument");
	s_r02_cgitem_type = ain_get_struct(ain, "_CCgModeDocumentItem");
	s_r02_cd_glob     = ain_get_global(ain, "CD");
	s_r02_gold_glob   = ain_get_global(ain, "GOLD");
	s_r02_karma_glob  = ain_get_global(ain, "\xb6\xd9\xcf");
	s_r02_item_world  = ain_get_global(ain, "\x90\xa2\x90\x46\x8a\xe0");
	s_r02_item_tree   = ain_get_global(ain, "\x8b\x41\x82\xe8\x96\xd8");
	s_r02_item_potion = ain_get_global(ain, "\x8c\xb3\x8b\x43\x96\xf2");
	s_r02_item_candy  = ain_get_global(ain, "\x82\xa8\x89\xd9\x8e\x71");
	s_r02_item_light  = ain_get_global(ain, "\xd7\xb2\xc4");
	s_r02_item_rope   = ain_get_global(ain, "\xdb\xb0\xcc\xdf");
	s_r02_battle_glob = ain_get_global(ain, "\x90\xed\x93\xac\x92\x86");
	s_r02_cgdoc_glob  = ain_get_global(ain, "_g_CgModeDocument");
	s_r02_fn_enemy_dmg = ain_get_function(ain, "\x93\x47\x83\x5f\x83\x81\x81[\x83\x57\x8f\x88\x97\x9d");
	s_r02_fn_ally_dmg  = ain_get_function(ain, "\x96\xa1\x95\xfb\x83\x5f\x83\x81\x81[\x83\x57\x8f\x88\x97\x9d");
	s_r02_fn_heal_all  = ain_get_function(ain, "\x91\x53\x89\xf1\x95\x9c");
	s_r02_fn_disp_stat = vm_find_hud_setter("\x94\x5c\x97\xcd\x92\x6c\x95\x5c\x8e\xa6");
	s_r02_stat_sprites_glob = ain_get_global(ain, "sp\x82\x6d");
	s_r02_fn_reg_cg    = ain_get_function(ain, "\x82\x62\x82\x66\x83\x82\x81[\x83\x68\x93\x6f\x98\x5e");
	s_r02_fn_reg_rem   = ain_get_function(ain, "\x89\xf1\x91\x7a\x93\x6f\x98\x5e");
	if (s_r02_chara_type < 0 || s_r02_enemy_type < 0 || s_r02_cd_glob < 0 || s_r02_gold_glob < 0)
		return;
	struct ain_struct *sc = &ain->structures[s_r02_chara_type];
	struct ain_struct *se = &ain->structures[s_r02_enemy_type];
	if (sc->nr_members < 15 || se->nr_members < 16)
		return;
	s_r02_compatible = true;
}

static struct page *r02_get_cd_array(void)
{
	if (!s_r02_compatible || !vm_globals_ready() || s_r02_cd_glob < 0)
		return NULL;
	struct page *p = r03_checked_page(heap[0].page->values[s_r02_cd_glob].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0);
	return (p && p->array.rank == 1 && p->array.struct_type == s_r02_chara_type && p->nr_vars >= 7) ? p : NULL;
}

static void ex_hhr_reset_assists(void);

void modifier_on_ain_init(void)
{
	pthread_mutex_lock(&g_mod_mutex);
	g_vm_ready_cached = false;
	pthread_mutex_unlock(&g_mod_mutex);
	/* Also runs after vm_reset() with the same ain: globals are defaults again. */
	s_game_config_ready = false;
	s_game_config_loaded = false;
	ex_hhr_reset_assists();
	s_r03_compatible = false;
	s_r03_applying = false;
	s_r03_inspected_ain = ain;
	r08_on_ain_init();
	r09_on_ain_init();
	r01_on_ain_init();
	r02_on_ain_init();
	if (!ain || !game_rance03)
		return;
	s_r03_player_type = ain_get_struct(ain, "Player");
	s_r03_common_type = ain_get_struct(ain, "PlayerCommonParam");
	s_r03_result_type = ain_get_struct(ain, "BattleResult");
	s_r03_item_type   = ain_get_struct(ain, "Item");
	s_r03_count_type  = ain_get_struct(ain, "ItemCount");
	s_r03_mon_type    = ain_get_struct(ain, "MonsterInstance");
	s_r03_players_glob = ain_get_global(ain, "g_player");
	s_r03_common_glob  = ain_get_global(ain, "g_playerCommonParam");
	s_r03_items_glob   = ain_get_global(ain, "g_item");
	if (s_r03_players_glob < 0 || s_r03_common_glob < 0 || s_r03_items_glob < 0
			|| !r03_member_is(s_r03_player_type, 10, "hp_", AIN_INT)
			|| !r03_member_is(s_r03_player_type, 11, "maxHp_", AIN_INT)
			|| !r03_member_is(s_r03_player_type, 14, "exp_", AIN_INT)
			|| !r03_member_is(s_r03_player_type, 24, "isJoin_", AIN_BOOL)
			|| !r03_member_is(s_r03_player_type, 27, "coolTime_", AIN_ARRAY_INT)
			|| !r03_member_is(s_r03_common_type, 0, "gold_", AIN_INT)
			|| !r03_member_is(s_r03_common_type, 2, "itemCount_", AIN_ARRAY_STRUCT)
			|| !r03_member_is(s_r03_common_type, 6, "partyCoolTime_", AIN_INT)
			|| !r03_member_is(s_r03_item_type, 0, "id_", AIN_STRING)
			|| !r03_member_is(s_r03_count_type, 1, "count", AIN_INT)
			|| !r03_member_is(s_r03_result_type, 5, "itemId_", AIN_STRING)
			|| !r03_member_is(s_r03_mon_type, 1, "hp_", AIN_INT))
		return;
	for (unsigned i = 0; i < R03_HOOK_COUNT; i++) {
		s_r03_funcs[i] = -1;
		int f = ain_get_function(ain, (char *)s_r03_sigs[i].name);
		if (f < 0 || ain->functions[f].nr_args != s_r03_sigs[i].args || ain->functions[f].crc != s_r03_sigs[i].crc)
			return;
		s_r03_funcs[i] = f;
	}
	s_r03_compatible = true;
}

void modifier_end_session(void)
{
	s_in_modifier_frame = false;
	pthread_mutex_lock(&g_mod_mutex);
	g_vm_ready_cached = false;
	free(g_cached_live_state_json);
	g_cached_live_state_json = NULL;
	free(g_pending_action_json);
	g_pending_action_json = NULL;
	g_live_state_req_seq = 0;
	g_live_state_built_seq = 0;
	g_has_ui_scale_init = false;
	g_font_scale_pct = 100;
	g_scrollbar_scale_pct = 100;
	g_pending_lock_streamer_mode = -1;
	g_has_lock_mask_init = false;
	g_live_lock_mask = 0;
	g_has_dohna_lock_mask_init = false;
	g_dohna_lock_mask = 0;
	g_has_evenicle_lock_mask_init = false;
	g_evenicle_lock_mask = 0;
	g_has_sr_lock_mask_init = false;
	g_sr_lock_mask = 0;
	g_has_r03_lock_mask_init = false;
	g_r03_lock_mask = 0;
	g_has_r08_lock_mask_init = false;
	g_r08_lock_mask = 0;
	g_has_r09_lock_mask_init = false;
	g_r09_lock_mask = 0;
	g_has_r01_lock_mask_init = false;
	g_r01_lock_mask = 0;
	g_has_r02_lock_mask_init = false;
	g_r02_lock_mask = 0;
	ex_reset_locks_locked();
	pthread_cond_broadcast(&g_mod_cond);
	pthread_mutex_unlock(&g_mod_mutex);

	s_applied_font_scale_pct = 100;
	s_applied_scrollbar_scale_pct = 100;
	s_game_config_ready = false;
	s_game_config_loaded = false;
	s_inspected_ain = NULL;
	s_dd_inspected_ain = NULL;
	s_ev_inspected_ain = NULL;
	s_sr_inspected_ain = NULL;
	s_r03_inspected_ain = NULL;
	s_card_org_mdata_slot = -1;
	s_card_org_pair0_slot = -1;
	s_r03_compatible = false;
	s_r03_applying = false;
	ex_hhr_reset_assists();
	r08_on_ain_init();
	r09_on_ain_init();
	r01_on_ain_init();
	r02_on_ain_init();
}

static inline int32_t r03_clamp_sum(int32_t old, int32_t delta)
{
	int64_t sum = (int64_t)old + delta;
	return sum < 0 ? 0 : (sum > INT32_MAX ? INT32_MAX : (int32_t)sum);
}

static int r03_current_mask_ptr(const int *ptr)
{
	pthread_mutex_lock(&g_mod_mutex);
	int mask = *ptr;
	pthread_mutex_unlock(&g_mod_mutex);
	return mask;
}

static int r03_current_mask(void)
{
	return r03_current_mask_ptr(&g_r03_lock_mask);
}

void modifier_before_method(void)
{
	if (call_stack_ptr <= 0)
		return;
	if (s_r01_compatible && !s_r01_applying) {
		struct function_call *f = &call_stack[call_stack_ptr - 1];
		int enabled = r03_current_mask_ptr(&g_r01_lock_mask);
		if (f->fno == s_r01_fn_dec_hp && (enabled & (LOCK_R01_FULL_HP | LOCK_R01_ONE_HIT_KILL))) {
			struct page *sb = r03_checked_page(f->struct_page, STRUCT_PAGE, s_r01_sb_type, 22);
			struct page *dmg_arr = sb ? r03_checked_page(sb->values[9].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0) : NULL;
			if (dmg_arr && dmg_arr->nr_vars >= 2) {
				if (enabled & LOCK_R01_ONE_HIT_KILL) {
					struct page *d0 = r03_checked_page(dmg_arr->values[0].i, STRUCT_PAGE, s_r01_dmg_type, 2);
					struct page *mon = r03_checked_page(sb->values[2].i, STRUCT_PAGE, s_r01_mon_type, 13);
					if (d0 && mon && mon->values[3].i > 0) {
						int32_t kill_dmg = mon->values[3].i + 9999;
						if (d0->values[0].i < kill_dmg)
							d0->values[0].i = kill_dmg;
					}
				}
				if (enabled & LOCK_R01_FULL_HP) {
					struct page *d1 = r03_checked_page(dmg_arr->values[1].i, STRUCT_PAGE, s_r01_dmg_type, 2);
					if (d1) {
						d1->values[0].i = 0;
						d1->values[1].i = 0;
					}
				}
			}
		}
		return;
	}
	if (s_r02_compatible && !s_r02_applying) {
		struct function_call *f = &call_stack[call_stack_ptr - 1];
		int enabled = r03_current_mask_ptr(&g_r02_lock_mask);
		struct page *local = r03_checked_page(f->page_slot, LOCAL_PAGE, f->fno, 4);
		if (!local)
			return;
		if ((enabled & LOCK_R02_ONE_HIT_KILL) && f->fno == s_r02_fn_enemy_dmg && local->nr_vars >= 4) {
			struct page *en = r03_checked_page(local->values[3].i, STRUCT_PAGE, s_r02_enemy_type, 16);
			if (en) {
				int32_t ehp = en->values[6].i > 0 ? en->values[6].i : 99999;
				if (local->values[0].i < ehp)
					local->values[0].i = ehp;
			}
		} else if ((enabled & LOCK_R02_ALLY_FULL_HP_SP) && f->fno == s_r02_fn_ally_dmg && local->nr_vars >= 6) {
			local->values[1].i = 0;
			local->values[5].i = 0;
		}
		return;
	}
	if (s_r08_compatible && !s_r08_applying) {
		struct function_call *f = &call_stack[call_stack_ptr - 1];
		struct page *local = r03_checked_page(f->page_slot, LOCAL_PAGE, f->fno, 2);
		int enabled = r03_current_mask_ptr(&g_r08_lock_mask);
		if (local && f->fno == s_r08_fn_c_hp && local->values[1].i >= 0) {
			int c_idx = local->values[0].i;
			struct page *carr = r08_get_c_array();
			struct page *c = (carr && c_idx > 0 && c_idx < carr->nr_vars)
				? r03_checked_page(carr->values[c_idx].i, STRUCT_PAGE, s_r08_sc_type, 45) : NULL;
			if (c) {
				if ((enabled & LOCK_R08_ALLY_FULL_HP) && r08_is_ally_c(c_idx) && c->values[20].i > 0)
					local->values[1].i = c->values[20].i;
				else if ((enabled & LOCK_R08_ONE_HIT_KILL) && r08_is_enemy_c(c_idx) && local->values[1].i < c->values[19].i)
					local->values[1].i = 0;
			}
		} else if (local && local->nr_vars >= 3 && f->fno == s_r08_fn_c_skill_cnt && local->values[2].i >= 0 && (enabled & LOCK_R08_INF_SKILLS)) {
			int c_idx = local->values[0].i;
			int sk_idx = local->values[1].i;
			struct page *carr = r08_is_ally_c(c_idx) ? r08_get_c_array() : NULL;
			struct page *c = (carr && c_idx > 0 && c_idx < carr->nr_vars)
				? r03_checked_page(carr->values[c_idx].i, STRUCT_PAGE, s_r08_sc_type, 45) : NULL;
			struct page *skills = c ? r03_checked_page(c->values[44].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0) : NULL;
			struct page *sk = (skills && sk_idx >= 0 && sk_idx < skills->nr_vars)
				? r03_checked_page(skills->values[sk_idx].i, STRUCT_PAGE, s_r08_skill_type, 12) : NULL;
			if (sk && sk->values[6].i > 0 && local->values[2].i < sk->values[6].i)
				local->values[2].i = sk->values[6].i;
		}
		return;
	}
	if (s_r09_compatible && !s_r09_applying) {
		struct function_call *f = &call_stack[call_stack_ptr - 1];
		int enabled = r03_current_mask_ptr(&g_r09_lock_mask);
		struct page *local = r03_checked_page(f->page_slot, LOCAL_PAGE, f->fno, 1);
		if (!local)
			return;
		if ((enabled & LOCK_R09_ALLY_GOD) && f->fno == s_r09_fn_ally_dmg && local->nr_vars >= 2) {
			struct page *dv = r03_checked_page(local->values[1].i, STRUCT_PAGE, s_r09_dmg_type, 14);
			if (dv) {
				dv->values[0].i = 0;
				dv->values[2].i = 0;
				dv->values[3].i = 0;
				dv->values[4].i = 0;
				dv->values[5].i = 0;
				dv->values[8].i = 0;
				dv->values[12].i = 1;
				dv->values[13].i = 0;
			}
		} else if ((enabled & LOCK_R09_ONE_HIT_KILL)
				&& (f->fno == s_r09_fn_mon_dmg || f->fno == s_r09_fn_obs_dmg)
				&& local->nr_vars >= 2) {
			struct page *dv = r03_checked_page(local->values[1].i, STRUCT_PAGE, s_r09_dmg_type, 14);
			if (dv && !dv->values[12].i && (dv->values[0].i > 0 || dv->values[2].i > 0
					|| dv->values[3].i > 0 || dv->values[4].i > 0 || dv->values[5].i > 0)) {
				dv->values[0].i = 9999999;
				dv->values[5].i = 9999999;
				dv->values[8].i = 1;
				dv->values[9].i = 0;
				dv->values[13].i = 1;
			}
		} else if ((enabled & LOCK_R09_INF_ACTION) && f->fno == s_r09_fn_set_pass && local->values[0].i != 0) {
			struct page *ch = r03_checked_page(f->struct_page, STRUCT_PAGE, s_r09_chara_type, 12);
			if (ch)
				local->values[0].i = 0;
		}
		return;
	}
	if (!s_r03_compatible || s_r03_applying)
		return;
	struct function_call *f = &call_stack[call_stack_ptr - 1];
	struct page *local = r03_checked_page(f->page_slot, LOCAL_PAGE, f->fno, 1);
	if (!local)
		return;
	int enabled = r03_current_mask();

	struct page *p = r03_checked_page(f->struct_page, STRUCT_PAGE, s_r03_player_type, 31);
	if (p && f->fno == s_r03_funcs[R03_FN_HP] && (enabled & LOCK_R03_HP) && local->values[0].i < p->values[10].i)
		local->values[0].i = p->values[10].i;
	if (p && f->fno == s_r03_funcs[R03_FN_ADD_EXP] && p->values[14].i >= 0)
		local->values[0].i = r03_clamp_sum(p->values[14].i, local->values[0].i) - p->values[14].i;

	struct page *c = r03_checked_page(f->struct_page, STRUCT_PAGE, s_r03_common_type, 16);
	if (c && f->fno == s_r03_funcs[R03_FN_ADD_GOLD] && c->values[0].i >= 0)
		local->values[0].i = r03_clamp_sum(c->values[0].i, local->values[0].i) - c->values[0].i;

	if ((enabled & LOCK_R03_ONE_HIT_KILL) && f->fno == s_r03_funcs[R03_FN_MON_SET_HP]
			&& call_stack_ptr >= 2 && call_stack[call_stack_ptr - 2].fno == s_r03_funcs[R03_FN_DEC_MON_HP]) {
		struct page *m = r03_checked_page(f->struct_page, STRUCT_PAGE, s_r03_mon_type, 4);
		if (m && local->values[0].i > 0)
			local->values[0].i = 0;
	}
}

static void r03_clear_cooldowns(struct page *p)
{
	struct page *a = r03_checked_page(p->values[27].i, ARRAY_PAGE, AIN_ARRAY_INT, 0);
	if (a && a->array.rank == 1 && a->nr_vars <= 4096) {
		for (int i = 0; i < a->nr_vars; i++)
			a->values[i].i = 0;
	}
}

static bool r03_valid_drop(struct page *result)
{
	int id = result->values[5].i;
	if (!string_index_valid(id) || !heap[id].s->size)
		return false;
	struct page *items = (s_r03_items_glob >= 0 && vm_globals_ready())
		? r03_checked_page(heap[0].page->values[s_r03_items_glob].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0) : NULL;
	if (!items || items->array.rank != 1 || items->array.struct_type != s_r03_item_type)
		return false;
	for (int i = 0; i < items->nr_vars; i++) {
		struct page *it = r03_checked_page(items->values[i].i, STRUCT_PAGE, s_r03_item_type, 12);
		if (it && string_index_valid(it->values[0].i) && !strcmp(heap[it->values[0].i].s->text, heap[id].s->text))
			return true;
	}
	return false;
}

void modifier_after_method_return(void)
{
	if (call_stack_ptr <= 0)
		return;
	if (s_r01_compatible && !s_r01_applying) {
		struct function_call *f = &call_stack[call_stack_ptr - 1];
		int enabled = r03_current_mask_ptr(&g_r01_lock_mask);
		if ((enabled & LOCK_R01_DROP_100) && f->fno == s_r01_fn_calc_drop) {
			struct page *sb = r03_checked_page(f->struct_page, STRUCT_PAGE, s_r01_sb_type, 22);
			struct page *mon = sb ? r03_checked_page(sb->values[2].i, STRUCT_PAGE, s_r01_mon_type, 13) : NULL;
			if (sb && mon && string_index_valid(mon->values[7].i) && heap[mon->values[7].i].s->size > 0
					&& string_index_valid(sb->values[1].i)) {
				heap_string_assign(sb->values[1].i, heap[mon->values[7].i].s);
			}
		}
		return;
	}
	if (s_r02_compatible)
		return;
	if (s_r09_compatible && !s_r09_applying) {
		struct function_call *f = &call_stack[call_stack_ptr - 1];
		int enabled = r03_current_mask_ptr(&g_r09_lock_mask);
		if ((enabled & LOCK_R09_DROP_100) && f->fno == s_r09_fn_gold_drop && stack_ptr > 0)
			stack_set(0, 1);
		return;
	}
	if (!s_r03_compatible || s_r03_applying)
		return;
	struct function_call *f = &call_stack[call_stack_ptr - 1];
	int enabled = r03_current_mask();
	if (enabled & LOCK_R03_NO_CD) {
		struct page *p = r03_checked_page(f->struct_page, STRUCT_PAGE, s_r03_player_type, 31);
		if (p && (f->fno == s_r03_funcs[R03_FN_SET_CD] || f->fno == s_r03_funcs[R03_FN_OVER_CD]))
			r03_clear_cooldowns(p);
		if (p && f->fno == s_r03_funcs[R03_FN_GET_CD] && stack_ptr > 0)
			stack_set(0, 0);
		struct page *c = r03_checked_page(f->struct_page, STRUCT_PAGE, s_r03_common_type, 16);
		if (c && f->fno == s_r03_funcs[R03_FN_PARTY_CD])
			c->values[6].i = 0;
		if (c && f->fno == s_r03_funcs[R03_FN_GET_PARTY_CD] && stack_ptr > 0)
			stack_set(0, 0);
	}
	if ((enabled & LOCK_R03_DROP_100) && f->fno == s_r03_funcs[R03_FN_DROP] && stack_ptr > 0) {
		struct page *r = r03_checked_page(f->struct_page, STRUCT_PAGE, s_r03_result_type, 10);
		if (r && r03_valid_drop(r))
			stack_set(0, 1);
	}
}

bool modifier_keep_dec_item(union vm_value *value)
{
	if (!s_r03_compatible || !(r03_current_mask() & LOCK_R03_INF_ITEMS) || call_stack_ptr <= 0)
		return false;
	struct function_call *f = &call_stack[call_stack_ptr - 1];
	bool normal_use = (f->fno == s_r03_funcs[R03_FN_USE_ITEM]);
	bool quick_lion = (f->fno == s_r03_funcs[R03_FN_DEC_ITEM] && call_stack_ptr >= 2
		&& call_stack[call_stack_ptr - 2].fno == s_r03_funcs[R03_FN_CAMP_LION]);
	if (!normal_use && !quick_lion)
		return false;
	struct page *c = r03_checked_page(f->struct_page, STRUCT_PAGE, s_r03_common_type, 16);
	struct page *local = r03_checked_page(f->page_slot, LOCAL_PAGE, f->fno, 3);
	struct page *counts = c ? r03_checked_page(c->values[2].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0) : NULL;
	int index = local ? local->values[1].i : -1;
	if (!counts || counts->array.rank != 1 || counts->array.struct_type != s_r03_count_type
			|| index < 0 || index >= counts->nr_vars)
		return false;
	struct page *item = r03_checked_page(counts->values[index].i, STRUCT_PAGE, s_r03_count_type, 2);
	return item && value == &item->values[1] && value->i > 0;
}

static struct page *r03_get_players_array(void)
{
	if (!s_r03_compatible || !vm_globals_ready() || s_r03_players_glob < 0)
		return NULL;
	struct page *p = r03_checked_page(heap[0].page->values[s_r03_players_glob].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0);
	return (p && p->array.rank == 1 && p->array.struct_type == s_r03_player_type && p->nr_vars <= 4096) ? p : NULL;
}

static struct page *r03_get_common(int *out_slot)
{
	if (!s_r03_compatible || !vm_globals_ready() || s_r03_common_glob < 0)
		return NULL;
	int slot = heap[0].page->values[s_r03_common_glob].i;
	struct page *c = r03_checked_page(slot, STRUCT_PAGE, s_r03_common_type, 16);
	if (out_slot) *out_slot = c ? slot : -1;
	return c;
}

static void vm_enforce_r03_locks(int r03_mask)
{
	if (!s_r03_compatible || s_r03_applying || !(r03_mask & LOCK_R03_NO_CD))
		return;
	struct page *players = r03_get_players_array();
	struct page *common = r03_get_common(NULL);
	if (!players || !common)
		return;
	for (int i = 0; i < players->nr_vars; i++) {
		struct page *p = r03_checked_page(players->values[i].i, STRUCT_PAGE, s_r03_player_type, 31);
		if (p) r03_clear_cooldowns(p);
	}
	common->values[6].i = 0;
}

static void vm_execute_r03_action(const char *json_str)
{
	if (!json_str || !json_str[0])
		return;
	cJSON *act = cJSON_Parse(json_str);
	if (!act)
		return;

	cJSON *it;
	bool persist_cfg = false;
	int save_r10_mask = 0;
	pthread_mutex_lock(&g_mod_mutex);
	if ((it = cJSON_GetObjectItem(act, "setRance03LockMask")) && cJSON_IsNumber(it)) {
		g_r03_lock_mask = it->valueint;
		g_has_r03_lock_mask_init = true;
		persist_cfg = true;
	}
	save_r10_mask = g_live_lock_mask;
	pthread_mutex_unlock(&g_mod_mutex);
	vm_apply_ui_scale_from_json(act, &persist_cfg, &save_r10_mask);
	if (persist_cfg)
		persist_modifier_config_from_native(save_r10_mask);

	int common_slot = -1;
	struct page *players = r03_get_players_array();
	struct page *common = r03_get_common(&common_slot);
	if (!s_r03_compatible || !players || !common || s_r03_applying) {
		cJSON_Delete(act);
		return;
	}

	s_r03_applying = true;
	if ((it = cJSON_GetObjectItem(act, "r03AddGold")) && cJSON_IsNumber(it)) {
		int32_t next = r03_clamp_sum(common->values[0].i, it->valueint);
		vm_call_method_1(s_r03_funcs[R03_FN_SET_GOLD], common_slot, next);
	}
	if ((it = cJSON_GetObjectItem(act, "r03Gold")) && cJSON_IsNumber(it)) {
		vm_call_method_1(s_r03_funcs[R03_FN_SET_GOLD], common_slot, vm_dd_clamp(it->valueint, 0, INT32_MAX));
	}
	if ((it = cJSON_GetObjectItem(act, "r03MaxAllExp")) && cJSON_IsTrue(it)) {
		for (int i = 0; ; i++) {
			players = r03_get_players_array();
			if (!players || i >= players->nr_vars) break;
			struct page *p = r03_checked_page(players->values[i].i, STRUCT_PAGE, s_r03_player_type, 31);
			if (p && p->values[24].i)
				vm_call_method_1(s_r03_funcs[R03_FN_SET_EXP], players->values[i].i, INT32_MAX);
		}
	}
	if ((it = cJSON_GetObjectItem(act, "r03HealAllParty")) && cJSON_IsTrue(it)) {
		for (int i = 0; ; i++) {
			players = r03_get_players_array();
			if (!players || i >= players->nr_vars) break;
			struct page *p = r03_checked_page(players->values[i].i, STRUCT_PAGE, s_r03_player_type, 31);
			if (p && p->values[24].i) {
				r03_clear_cooldowns(p);
				int32_t mhp = p->values[11].i > 0 ? p->values[11].i : 1;
				vm_call_method_1(s_r03_funcs[R03_FN_HP], players->values[i].i, mhp);
			}
		}
		common = r03_get_common(&common_slot);
		if (common) common->values[6].i = 0;
	}
	cJSON *ce = cJSON_GetObjectItem(act, "r03CharEdit");
	if (ce && cJSON_IsObject(ce)) {
		cJSON *jt = cJSON_GetObjectItem(ce, "target");
		int target = (jt && cJSON_IsNumber(jt)) ? jt->valueint : -1;
		for (int i = 0; ; i++) {
			players = r03_get_players_array();
			if (!players || i >= players->nr_vars) break;
			if (target >= 0 && i != target) continue;
			struct page *p = r03_checked_page(players->values[i].i, STRUCT_PAGE, s_r03_player_type, 31);
			if (!p || (target < 0 && !p->values[24].i)) continue;
			cJSON *f;
			if ((f = cJSON_GetObjectItem(ce, "hpMax")) && cJSON_IsNumber(f))
				p->values[11].i = vm_dd_clamp(f->valueint, 1, 999999);
			if ((f = cJSON_GetObjectItem(ce, "lv")) && cJSON_IsNumber(f))
				p->values[12].i = vm_dd_clamp(f->valueint, 1, 9999);
			if ((f = cJSON_GetObjectItem(ce, "str")) && cJSON_IsNumber(f))
				p->values[16].i = vm_dd_clamp(f->valueint, 0, 99999);
			if ((f = cJSON_GetObjectItem(ce, "mind")) && cJSON_IsNumber(f))
				p->values[17].i = vm_dd_clamp(f->valueint, 0, 99999);
			if ((f = cJSON_GetObjectItem(ce, "dex")) && cJSON_IsNumber(f))
				p->values[15].i = vm_dd_clamp(f->valueint, 0, 99999);
			if ((f = cJSON_GetObjectItem(ce, "exp")) && cJSON_IsNumber(f))
				vm_call_method_1(s_r03_funcs[R03_FN_SET_EXP], players->values[i].i, vm_dd_clamp(f->valueint, 0, INT32_MAX));
			if ((f = cJSON_GetObjectItem(ce, "hp")) && cJSON_IsNumber(f)) {
				players = r03_get_players_array();
				int slot = (players && i < players->nr_vars) ? players->values[i].i : -1;
				if (slot >= 0)
					vm_call_method_1(s_r03_funcs[R03_FN_HP], slot, vm_dd_clamp(f->valueint, 1, 999999));
			}
			if (target >= 0) break;
		}
	}
	s_r03_applying = false;
	cJSON_Delete(act);
}

static void vm_r03_populate_live_state_json(cJSON *root)
{
	struct page *players = r03_get_players_array();
	struct page *common = r03_get_common(NULL);
	bool joined = false;
	if (players) {
		for (int i = 0; i < players->nr_vars; i++) {
			struct page *p = r03_checked_page(players->values[i].i, STRUCT_PAGE, s_r03_player_type, 31);
			if (p && p->values[24].i) {
				joined = true;
				break;
			}
		}
	}
	if (!s_r03_compatible || !joined || !common) {
		cJSON_AddBoolToObject(root, "inGame", false);
		return;
	}
	cJSON_AddBoolToObject(root, "inGame", true);
	cJSON_AddNumberToObject(root, "gold", common->values[0].i);
	cJSON_AddNumberToObject(root, "partyCoolTime", common->values[6].i);

	cJSON *j_chars = cJSON_CreateArray();
	int joined_cnt = 0;
	for (int i = 0; i < players->nr_vars && joined_cnt < 64; i++) {
		struct page *p = r03_checked_page(players->values[i].i, STRUCT_PAGE, s_r03_player_type, 31);
		if (!p || !p->values[24].i)
			continue;
		char *name_u = string_index_valid(p->values[1].i) ? mod_sjis2utf_safe(heap[p->values[1].i].s->text, (size_t)heap[p->values[1].i].s->size) : NULL;
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddStringToObject(jc, "name", (name_u && name_u[0]) ? name_u : "?");
		free(name_u);
		cJSON_AddNumberToObject(jc, "lv", p->values[12].i);
		cJSON_AddNumberToObject(jc, "lvMax", p->values[13].i);
		cJSON_AddNumberToObject(jc, "hp", p->values[10].i);
		cJSON_AddNumberToObject(jc, "hpMax", p->values[11].i);
		cJSON_AddNumberToObject(jc, "exp", p->values[14].i);
		cJSON_AddNumberToObject(jc, "str", p->values[16].i);
		cJSON_AddNumberToObject(jc, "mind", p->values[17].i);
		cJSON_AddNumberToObject(jc, "dex", p->values[15].i);
		cJSON_AddItemToArray(j_chars, jc);
		joined_cnt++;
	}
	cJSON_AddNumberToObject(root, "r03JoinedCount", joined_cnt);
	cJSON_AddItemToObject(root, "r03Characters", j_chars);
}

/* =========================================================================
 * Rance 08 State / Actions / Frame Locks
 * ========================================================================= */

static bool vm_enforce_r08_locks(int r08_mask)
{
	if (!s_r08_compatible || s_r08_applying || !vm_globals_ready())
		return false;
	if ((r08_mask & LOCK_R08_INF_GOLD) && s_r08_gold_glob >= 0) {
		if (heap[0].page->values[s_r08_gold_glob].i < 999999)
			heap[0].page->values[s_r08_gold_glob].i = 999999;
	}
	if ((r08_mask & LOCK_R08_MAGNUM_MAX) && s_r08_mag_glob >= 0 && s_r08_magmax_glob >= 0) {
		int mag_max = heap[0].page->values[s_r08_magmax_glob].i;
		if (mag_max <= 0) mag_max = 100;
		heap[0].page->values[s_r08_mag_glob].i = mag_max;
	}
	if (!(r08_mask & (LOCK_R08_ALLY_FULL_HP | LOCK_R08_INF_SKILLS)))
		return false;
	struct page *carr = r08_get_c_array();
	struct page *pta = r08_get_pt_array(s_r08_pta_glob);
	if (!carr || !pta)
		return false;
	bool refresh_cards = false;
	for (int p = 1; p < pta->nr_vars; p++) {
		int c_idx = pta->values[p].i;
		if (c_idx <= 0 || c_idx >= carr->nr_vars) continue;
		struct page *c = r03_checked_page(carr->values[c_idx].i, STRUCT_PAGE, s_r08_sc_type, 45);
		if (!c) continue;
		if ((r08_mask & LOCK_R08_ALLY_FULL_HP) && c->values[20].i > 0
				&& c->values[19].i != c->values[20].i) {
			c->values[19].i = c->values[20].i;
			refresh_cards = true;
		}
		if (r08_mask & LOCK_R08_INF_SKILLS)
			r08_replenish_chara_skills(c);
	}
	return refresh_cards;
}

static void r08_refresh_battle_cards(void)
{
	if (s_r08_battle_chara_type < 0 || s_r08_fn_update_battle_card < 0
			|| vm_callstack_has_fno(s_r08_fn_update_battle_card))
		return;
	struct page *manager = vm_get_global_page_idx(s_r08_battle_chara_glob);
	struct page *cards = vm_get_field_page(manager, "m_asChara", NULL);
	if (!cards || cards->type != ARRAY_PAGE || cards->array.rank != 1)
		return;
	for (int i = 0; i < cards->nr_vars; i++) {
		int slot = cards->values[i].i;
		struct page *unit = r03_checked_page(slot, STRUCT_PAGE, s_r08_battle_chara_type, 1);
		if (vm_get_field_int(unit, "m_nNo", NULL, -1) <= 0)
			continue;
		struct page *card = vm_get_field_page(unit, "m_sCard", NULL);
		struct page *bar = vm_get_field_page(card, "m_sHpBar", NULL);
		if (vm_get_field_int(bar, "m_bShow", NULL, 0) && vm_get_field_int(bar, "m_nMax", NULL, 0) > 0)
			vm_dd_call_int_0(s_r08_fn_update_battle_card, slot, 0);
	}
}

static bool vm_execute_r08_action(const char *json_str)
{
	if (!json_str || !json_str[0])
		return false;
	cJSON *act = cJSON_Parse(json_str);
	if (!act)
		return false;

	cJSON *it;
	bool persist_cfg = false;
	int save_r10_mask = 0;
	pthread_mutex_lock(&g_mod_mutex);
	if ((it = cJSON_GetObjectItem(act, "setRance08LockMask")) && cJSON_IsNumber(it)) {
		g_r08_lock_mask = it->valueint;
		g_has_r08_lock_mask_init = true;
		persist_cfg = true;
	}
	save_r10_mask = g_live_lock_mask;
	pthread_mutex_unlock(&g_mod_mutex);
	vm_apply_ui_scale_from_json(act, &persist_cfg, &save_r10_mask);
	if (persist_cfg)
		persist_modifier_config_from_native(save_r10_mask);

	if (!s_r08_compatible || !vm_globals_ready() || s_r08_applying) {
		cJSON_Delete(act);
		return false;
	}
	s_r08_applying = true;
	if ((it = cJSON_GetObjectItem(act, "r08AddGold")) && cJSON_IsNumber(it) && s_r08_gold_glob >= 0) {
		int32_t cur = heap[0].page->values[s_r08_gold_glob].i;
		heap[0].page->values[s_r08_gold_glob].i = vm_dd_clamp((double)cur + it->valuedouble, 0, 999999999);
	}
	if ((it = cJSON_GetObjectItem(act, "r08Gold")) && cJSON_IsNumber(it) && s_r08_gold_glob >= 0)
		heap[0].page->values[s_r08_gold_glob].i = vm_dd_clamp(it->valuedouble, 0, 999999999);
	if ((it = cJSON_GetObjectItem(act, "r08FameMax")) && cJSON_IsNumber(it) && s_r08_famemax_glob >= 0)
		heap[0].page->values[s_r08_famemax_glob].i = vm_dd_clamp(it->valueint, 0, 999999);
	if ((it = cJSON_GetObjectItem(act, "r08Fame")) && cJSON_IsNumber(it) && s_r08_fame_glob >= 0)
		heap[0].page->values[s_r08_fame_glob].i = vm_dd_clamp(it->valueint, 0, 999999);
	if ((it = cJSON_GetObjectItem(act, "r08MagnumMax")) && cJSON_IsTrue(it) && s_r08_mag_glob >= 0 && s_r08_magmax_glob >= 0) {
		int mag_max = heap[0].page->values[s_r08_magmax_glob].i;
		if (mag_max <= 0) mag_max = 100;
		heap[0].page->values[s_r08_mag_glob].i = mag_max;
	}
	struct page *carr = r08_get_c_array();
	bool refresh_cards = false;
	if (carr) {
		if ((it = cJSON_GetObjectItem(act, "r08HealAllParty")) && cJSON_IsTrue(it)) {
			for (int i = 1; i < carr->nr_vars; i++) {
				if (!r08_is_ally_c(i)) continue;
				struct page *c = r03_checked_page(carr->values[i].i, STRUCT_PAGE, s_r08_sc_type, 60);
				if (!c) continue;
				if (c->values[20].i > 0) c->values[19].i = c->values[20].i;
				c->values[59].i = 0;
				r08_replenish_chara_skills(c);
				refresh_cards = true;
			}
		}
		cJSON *ce = cJSON_GetObjectItem(act, "r08CharEdit");
		if (ce && cJSON_IsObject(ce)) {
			cJSON *jt = cJSON_GetObjectItem(ce, "target");
			int target = (jt && cJSON_IsNumber(jt)) ? jt->valueint : -1;
			for (int i = 1; i < carr->nr_vars; i++) {
				if (target >= 0 && i != target) continue;
				if (target < 0 && !r08_is_ally_c(i)) continue;
				struct page *c = r03_checked_page(carr->values[i].i, STRUCT_PAGE, s_r08_sc_type, 45);
				if (!c) continue;
				cJSON *f;
				if ((f = cJSON_GetObjectItem(ce, "lv")) && cJSON_IsNumber(f))
					c->values[11].i = vm_dd_clamp(f->valueint, 1, 9999);
				if ((f = cJSON_GetObjectItem(ce, "lvMax")) && cJSON_IsNumber(f))
					c->values[12].i = vm_dd_clamp(f->valueint, 1, 9999);
				if ((f = cJSON_GetObjectItem(ce, "exp")) && cJSON_IsNumber(f))
					c->values[13].i = vm_dd_clamp(f->valueint, 0, 99999999);
				if ((f = cJSON_GetObjectItem(ce, "hpMax")) && cJSON_IsNumber(f))
					c->values[20].i = vm_dd_clamp(f->valueint, 1, 999999);
				if ((f = cJSON_GetObjectItem(ce, "hp")) && cJSON_IsNumber(f))
					c->values[19].i = vm_dd_clamp(f->valueint, 1, 999999);
				if ((f = cJSON_GetObjectItem(ce, "spd")) && cJSON_IsNumber(f))
					c->values[21].i = vm_dd_clamp(f->valueint, 0, 99999);
				if ((f = cJSON_GetObjectItem(ce, "atk")) && cJSON_IsNumber(f))
					c->values[22].i = vm_dd_clamp(f->valueint, 0, 999999);
				if ((f = cJSON_GetObjectItem(ce, "def")) && cJSON_IsNumber(f))
					c->values[23].i = vm_dd_clamp(f->valueint, 0, 999999);
				if ((f = cJSON_GetObjectItem(ce, "hit")) && cJSON_IsNumber(f))
					c->values[24].i = vm_dd_clamp(f->valueint, 0, 9999);
				if ((f = cJSON_GetObjectItem(ce, "eva")) && cJSON_IsNumber(f))
					c->values[25].i = vm_dd_clamp(f->valueint, 0, 9999);
				if ((f = cJSON_GetObjectItem(ce, "crt")) && cJSON_IsNumber(f))
					c->values[26].i = vm_dd_clamp(f->valueint, 0, 9999);
				if ((f = cJSON_GetObjectItem(ce, "mag")) && cJSON_IsNumber(f))
					c->values[29].i = vm_dd_clamp(f->valueint, 0, 999999);
				if ((f = cJSON_GetObjectItem(ce, "res")) && cJSON_IsNumber(f))
					c->values[30].i = vm_dd_clamp(f->valueint, 0, 999999);
				if ((f = cJSON_GetObjectItem(ce, "skillPts")) && cJSON_IsNumber(f))
					c->values[41].i = vm_dd_clamp(f->valueint, 0, 9999);
				if ((f = cJSON_GetObjectItem(ce, "fullHeal")) && cJSON_IsTrue(f)) {
					if (c->values[20].i > 0) c->values[19].i = c->values[20].i;
					r08_replenish_chara_skills(c);
				}
				refresh_cards = true;
				if (target >= 0) break;
			}
		}
	}
	s_r08_applying = false;
	cJSON_Delete(act);
	return refresh_cards;
}

static void vm_r08_populate_live_state_json(cJSON *root)
{
	struct page *carr = r08_get_c_array();
	struct page *pta = r08_get_pt_array(s_r08_pta_glob);
	bool has_party = false;
	if (carr && pta) {
		for (int i = 1; i < pta->nr_vars; i++) {
			if (pta->values[i].i > 0) { has_party = true; break; }
		}
	}
	if (!s_r08_compatible || !carr || !has_party) {
		cJSON_AddBoolToObject(root, "inGame", false);
		return;
	}
	cJSON_AddBoolToObject(root, "inGame", true);
	cJSON_AddNumberToObject(root, "gold", s_r08_gold_glob >= 0 ? heap[0].page->values[s_r08_gold_glob].i : 0);
	cJSON_AddNumberToObject(root, "fame", s_r08_fame_glob >= 0 ? heap[0].page->values[s_r08_fame_glob].i : 0);
	cJSON_AddNumberToObject(root, "fameMax", s_r08_famemax_glob >= 0 ? heap[0].page->values[s_r08_famemax_glob].i : 0);
	cJSON_AddNumberToObject(root, "magnum", s_r08_mag_glob >= 0 ? heap[0].page->values[s_r08_mag_glob].i : 0);
	cJSON_AddNumberToObject(root, "magnumMax", s_r08_magmax_glob >= 0 ? heap[0].page->values[s_r08_magmax_glob].i : 0);

	cJSON *j_chars = cJSON_CreateArray();
	int cnt = 0;
	for (int i = 1; i < carr->nr_vars && cnt < 80; i++) {
		if (!r08_is_ally_c(i)) continue;
		struct page *c = r03_checked_page(carr->values[i].i, STRUCT_PAGE, s_r08_sc_type, 45);
		if (!c || c->values[0].i <= 0) continue;
		bool in_pt = r08_index_in_pt(s_r08_pta_glob, i);
		char *name_u = string_index_valid(c->values[4].i) ? mod_sjis2utf_safe(heap[c->values[4].i].s->text, (size_t)heap[c->values[4].i].s->size) : NULL;
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddNumberToObject(jc, "charaNo", c->values[0].i);
		cJSON_AddBoolToObject(jc, "inParty", in_pt);
		cJSON_AddStringToObject(jc, "name", (name_u && name_u[0]) ? name_u : "?");
		free(name_u);
		cJSON_AddNumberToObject(jc, "lv", c->values[11].i);
		cJSON_AddNumberToObject(jc, "lvMax", c->values[12].i);
		cJSON_AddNumberToObject(jc, "exp", c->values[13].i);
		cJSON_AddNumberToObject(jc, "hp", c->values[19].i);
		cJSON_AddNumberToObject(jc, "hpMax", c->values[20].i);
		cJSON_AddNumberToObject(jc, "spd", c->values[21].i);
		cJSON_AddNumberToObject(jc, "atk", c->values[22].i);
		cJSON_AddNumberToObject(jc, "def", c->values[23].i);
		cJSON_AddNumberToObject(jc, "hit", c->values[24].i);
		cJSON_AddNumberToObject(jc, "eva", c->values[25].i);
		cJSON_AddNumberToObject(jc, "crt", c->values[26].i);
		cJSON_AddNumberToObject(jc, "mag", c->values[29].i);
		cJSON_AddNumberToObject(jc, "res", c->values[30].i);
		cJSON_AddNumberToObject(jc, "skillPts", c->values[41].i);
		cJSON_AddItemToArray(j_chars, jc);
		cnt++;
	}
	cJSON_AddNumberToObject(root, "r08CharaCount", cnt);
	cJSON_AddItemToObject(root, "r08Characters", j_chars);
}

/* =========================================================================
 * Rance 09 State / Actions / Frame Locks
 * ========================================================================= */

enum { R09_REFRESH_CARDS = 1, R09_REFRESH_SYSTEM_BAR = 2 };

static int vm_enforce_r09_locks(int r09_mask)
{
	if (!s_r09_compatible || s_r09_applying || !vm_globals_ready())
		return 0;
	int refresh = 0;
	if (r09_mask & LOCK_R09_INF_RESOURCES) {
		struct page *sd = r09_get_save_data_arr();
		if (sd && sd->nr_vars >= 4) {
			static const int minimum[] = { 999999, 9999, 999 };
			for (int i = 1; i <= 3; i++) {
				if (sd->values[i].i >= minimum[i - 1])
					continue;
				sd->values[i].i = minimum[i - 1];
				refresh |= R09_REFRESH_SYSTEM_BAR;
			}
		}
	}
	if (!(r09_mask & (LOCK_R09_ALLY_GOD | LOCK_R09_INF_ACTION)))
		return refresh;
	struct page *carr = r09_get_chara_array();
	if (!carr)
		return refresh;
	for (int i = 0; i < carr->nr_vars; i++) {
		struct page *c = r03_checked_page(carr->values[i].i, STRUCT_PAGE, s_r09_chara_type, 12);
		if (!c) continue;
		if (r09_mask & LOCK_R09_ALLY_GOD) {
			struct page *pv = r09_get_chara_params(c);
			if (pv && pv->nr_vars >= 22) {
				for (int p = 10; p <= 18; p += 2) {
					if (pv->values[p + 1].i <= 0 || pv->values[p].i == pv->values[p + 1].i)
						continue;
					pv->values[p].i = pv->values[p + 1].i;
					refresh |= R09_REFRESH_CARDS;
				}
			}
		}
		if (r09_mask & LOCK_R09_INF_ACTION) {
			struct page *bf = r09_unwrap_arr_bool(c->values[6].i);
			if (bf && bf->nr_vars > 1)
				bf->values[1].i = 0;
		}
	}
	return refresh;
}

static void r09_refresh_system_bar(void)
{
	struct page *update = vm_get_global_page_idx(s_r09_system_bar_delegate);
	if (update && update->type == DELEGATE_PAGE && update->nr_vars > 0)
		vm_call_hud_setter(s_r09_fn_update_system_bar, -1, 0, 0);
}

static void r09_refresh_party_cards(void)
{
	if (s_r09_party_card_type < 0 || s_r09_fn_update_party_card < 0
			|| vm_callstack_has_fno(s_r09_fn_update_party_card))
		return;
	struct page *party = vm_get_global_page_idx(s_r09_party_glob);
	struct page *list = vm_get_field_page(party, "m_sList", NULL);
	struct page *cards = vm_get_field_page(list, "m_asCard", NULL);
	if (!cards || cards->type != ARRAY_PAGE || cards->array.rank != 1
			|| !vm_get_field_int(list, "m_bShow", NULL, 0))
		return;
	for (int i = 0; i < cards->nr_vars; i++) {
		int slot = cards->values[i].i;
		struct page *card = r03_checked_page(slot, STRUCT_PAGE, s_r09_party_card_type, 1);
		if (!vm_get_field_int(card, "m_bShow", NULL, 0))
			continue;
		struct page *status = vm_get_field_page(card, "m_sCard", NULL);
		if (vm_get_field_int(status, "m_nUnitNo", NULL, -1) > 0)
			vm_dd_call_void_0(s_r09_fn_update_party_card, slot);
	}
}

static int vm_execute_r09_action(const char *json_str)
{
	if (!json_str || !json_str[0])
		return 0;
	cJSON *act = cJSON_Parse(json_str);
	if (!act)
		return 0;

	cJSON *it;
	bool persist_cfg = false;
	int save_r10_mask = 0;
	pthread_mutex_lock(&g_mod_mutex);
	if ((it = cJSON_GetObjectItem(act, "setRance09LockMask")) && cJSON_IsNumber(it)) {
		g_r09_lock_mask = it->valueint;
		g_has_r09_lock_mask_init = true;
		persist_cfg = true;
	}
	save_r10_mask = g_live_lock_mask;
	pthread_mutex_unlock(&g_mod_mutex);
	vm_apply_ui_scale_from_json(act, &persist_cfg, &save_r10_mask);
	if (persist_cfg)
		persist_modifier_config_from_native(save_r10_mask);

	if (!s_r09_compatible || !vm_globals_ready() || s_r09_applying) {
		cJSON_Delete(act);
		return 0;
	}
	s_r09_applying = true;
	struct page *sd = r09_get_save_data_arr();
	bool refresh_system_bar = false;
	if (sd && sd->nr_vars >= 4) {
		int gold = sd->values[1].i, points = sd->values[2].i, stones = sd->values[3].i;
		if ((it = cJSON_GetObjectItem(act, "r09AddGold")) && cJSON_IsNumber(it))
			sd->values[1].i = vm_dd_clamp((double)sd->values[1].i + it->valuedouble, 0, INT32_MAX);
		if ((it = cJSON_GetObjectItem(act, "r09Gold")) && cJSON_IsNumber(it))
			sd->values[1].i = vm_dd_clamp(it->valuedouble, 0, INT32_MAX);
		if ((it = cJSON_GetObjectItem(act, "r09AddSeimaPoint")) && cJSON_IsNumber(it))
			sd->values[2].i = vm_dd_clamp((double)sd->values[2].i + it->valuedouble, 0, INT32_MAX);
		if ((it = cJSON_GetObjectItem(act, "r09SeimaPoint")) && cJSON_IsNumber(it))
			sd->values[2].i = vm_dd_clamp(it->valuedouble, 0, INT32_MAX);
		if ((it = cJSON_GetObjectItem(act, "r09SeimaStone")) && cJSON_IsNumber(it))
			sd->values[3].i = vm_dd_clamp(it->valueint, 0, 99999999);
		refresh_system_bar = gold != sd->values[1].i || points != sd->values[2].i || stones != sd->values[3].i;
	}
	struct page *carr = r09_get_chara_array();
	bool refresh_cards = false;
	if (carr) {
		if ((it = cJSON_GetObjectItem(act, "r09HealAllParty")) && cJSON_IsTrue(it)) {
			for (int i = 0; i < carr->nr_vars; i++) {
				struct page *c = r03_checked_page(carr->values[i].i, STRUCT_PAGE, s_r09_chara_type, 12);
				struct page *pv = r09_get_chara_params(c);
				if (!pv || pv->nr_vars < 22) continue;
				if (pv->values[11].i > 0) pv->values[10].i = pv->values[11].i;
				if (pv->values[13].i > 0) pv->values[12].i = pv->values[13].i;
				if (pv->values[15].i > 0) pv->values[14].i = pv->values[15].i;
				if (pv->values[17].i > 0) pv->values[16].i = pv->values[17].i;
				if (pv->values[19].i > 0) pv->values[18].i = pv->values[19].i;
				struct page *bf = r09_unwrap_arr_bool(c->values[6].i);
				if (bf && bf->nr_vars > 1) bf->values[1].i = 0;
				refresh_cards = true;
			}
		}
		if ((it = cJSON_GetObjectItem(act, "r09MaxAllSkillUp")) && cJSON_IsTrue(it)) {
			for (int i = 0; i < carr->nr_vars; i++) {
				struct page *c = r03_checked_page(carr->values[i].i, STRUCT_PAGE, s_r09_chara_type, 12);
				struct page *sp = c ? r09_unwrap_arr_int(c->values[3].i) : NULL;
				if (sp && sp->nr_vars >= 20) {
					sp->values[19].i = 9999;
					refresh_cards = true;
				}
			}
		}
		cJSON *ce = cJSON_GetObjectItem(act, "r09CharEdit");
		if (ce && cJSON_IsObject(ce)) {
			cJSON *jt = cJSON_GetObjectItem(ce, "target");
			int target = (jt && cJSON_IsNumber(jt)) ? jt->valueint : -1;
			for (int i = 0; i < carr->nr_vars; i++) {
				if (target >= 0 && i != target) continue;
				struct page *c = r03_checked_page(carr->values[i].i, STRUCT_PAGE, s_r09_chara_type, 12);
				struct page *pv = r09_get_chara_params(c);
				struct page *sp = c ? r09_unwrap_arr_int(c->values[3].i) : NULL;
				if (!pv || pv->nr_vars < 100) continue;
				cJSON *f;
				if ((f = cJSON_GetObjectItem(ce, "hpMax")) && cJSON_IsNumber(f))
					pv->values[17].i = vm_dd_clamp(f->valueint, 1, 999999);
				if ((f = cJSON_GetObjectItem(ce, "hp")) && cJSON_IsNumber(f))
					pv->values[16].i = vm_dd_clamp(f->valueint, 1, pv->values[17].i > 0 ? pv->values[17].i : 999999);
				if ((f = cJSON_GetObjectItem(ce, "atkMin")) && cJSON_IsNumber(f))
					pv->values[30].i = vm_dd_clamp(f->valueint, 0, 999999);
				if ((f = cJSON_GetObjectItem(ce, "atkMax")) && cJSON_IsNumber(f))
					pv->values[31].i = vm_dd_clamp(f->valueint, 0, 999999);
				if ((f = cJSON_GetObjectItem(ce, "magic")) && cJSON_IsNumber(f))
					pv->values[32].i = vm_dd_clamp(f->valueint, 0, 999999);
				if ((f = cJSON_GetObjectItem(ce, "resist")) && cJSON_IsNumber(f))
					pv->values[33].i = vm_dd_clamp(f->valueint, 0, 9999);
				if ((f = cJSON_GetObjectItem(ce, "move")) && cJSON_IsNumber(f))
					pv->values[34].i = vm_dd_clamp(f->valueint, 1, 99);
				if ((f = cJSON_GetObjectItem(ce, "range")) && cJSON_IsNumber(f))
					pv->values[35].i = vm_dd_clamp(f->valueint, 1, 99);
				if ((f = cJSON_GetObjectItem(ce, "critical")) && cJSON_IsNumber(f))
					pv->values[36].i = vm_dd_clamp(f->valueint, 0, 100);
				if ((f = cJSON_GetObjectItem(ce, "speed")) && cJSON_IsNumber(f))
					pv->values[37].i = vm_dd_clamp(f->valueint, 0, 999);
				if ((f = cJSON_GetObjectItem(ce, "evadeMax")) && cJSON_IsNumber(f))
					pv->values[11].i = vm_dd_clamp(f->valueint, 0, 9999);
				if ((f = cJSON_GetObjectItem(ce, "fendMax")) && cJSON_IsNumber(f))
					pv->values[13].i = vm_dd_clamp(f->valueint, 0, 9999);
				if ((f = cJSON_GetObjectItem(ce, "armoredMax")) && cJSON_IsNumber(f))
					pv->values[15].i = vm_dd_clamp(f->valueint, 0, 9999);
				if (sp && sp->nr_vars >= 20 && (f = cJSON_GetObjectItem(ce, "skillBonus")) && cJSON_IsNumber(f))
					sp->values[19].i = vm_dd_clamp(f->valueint, 0, 999999);
				if ((f = cJSON_GetObjectItem(ce, "fullHeal")) && cJSON_IsTrue(f)) {
					if (pv->values[11].i > 0) pv->values[10].i = pv->values[11].i;
					if (pv->values[13].i > 0) pv->values[12].i = pv->values[13].i;
					if (pv->values[15].i > 0) pv->values[14].i = pv->values[15].i;
					if (pv->values[17].i > 0) pv->values[16].i = pv->values[17].i;
					if (pv->values[19].i > 0) pv->values[18].i = pv->values[19].i;
				}
				refresh_cards = true;
				if (target >= 0) break;
			}
		}
	}
	s_r09_applying = false;
	cJSON_Delete(act);
	return (refresh_cards ? R09_REFRESH_CARDS : 0) | (refresh_system_bar ? R09_REFRESH_SYSTEM_BAR : 0);
}

static void vm_r09_populate_live_state_json(cJSON *root)
{
	struct page *carr = r09_get_chara_array();
	struct page *sd = r09_get_save_data_arr();
	if (!s_r09_compatible || !carr || carr->nr_vars <= 0 || !sd || sd->nr_vars < 4) {
		cJSON_AddBoolToObject(root, "inGame", false);
		return;
	}
	cJSON_AddBoolToObject(root, "inGame", true);
	cJSON_AddNumberToObject(root, "gold", sd->values[1].i);
	cJSON_AddNumberToObject(root, "seimaPoint", sd->values[2].i);
	cJSON_AddNumberToObject(root, "seimaStone", sd->values[3].i);

	cJSON *j_chars = cJSON_CreateArray();
	int cnt = 0;
	for (int i = 0; i < carr->nr_vars && cnt < 64; i++) {
		struct page *c = r03_checked_page(carr->values[i].i, STRUCT_PAGE, s_r09_chara_type, 12);
		struct page *pv = r09_get_chara_params(c);
		struct page *sp = c ? r09_unwrap_arr_int(c->values[3].i) : NULL;
		if (!pv || pv->nr_vars < 100) continue;
		int chara_id = pv->values[0].i;
		if (chara_id <= 0) continue;
		char *name_u = r09_lookup_chara_name(chara_id);
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddNumberToObject(jc, "charaId", chara_id);
		cJSON_AddStringToObject(jc, "name", (name_u && name_u[0]) ? name_u : "?");
		free(name_u);
		cJSON_AddNumberToObject(jc, "hp", pv->values[16].i);
		cJSON_AddNumberToObject(jc, "hpMax", pv->values[17].i);
		cJSON_AddNumberToObject(jc, "atkMin", pv->values[30].i);
		cJSON_AddNumberToObject(jc, "atkMax", pv->values[31].i);
		cJSON_AddNumberToObject(jc, "magic", pv->values[32].i);
		cJSON_AddNumberToObject(jc, "resist", pv->values[33].i);
		cJSON_AddNumberToObject(jc, "move", pv->values[34].i);
		cJSON_AddNumberToObject(jc, "range", pv->values[35].i);
		cJSON_AddNumberToObject(jc, "critical", pv->values[36].i);
		cJSON_AddNumberToObject(jc, "speed", pv->values[37].i);
		cJSON_AddNumberToObject(jc, "evade", pv->values[10].i);
		cJSON_AddNumberToObject(jc, "evadeMax", pv->values[11].i);
		cJSON_AddNumberToObject(jc, "fend", pv->values[12].i);
		cJSON_AddNumberToObject(jc, "fendMax", pv->values[13].i);
		cJSON_AddNumberToObject(jc, "armored", pv->values[14].i);
		cJSON_AddNumberToObject(jc, "armoredMax", pv->values[15].i);
		cJSON_AddNumberToObject(jc, "skillBonus", (sp && sp->nr_vars >= 20) ? sp->values[19].i : 0);
		cJSON_AddItemToArray(j_chars, jc);
		cnt++;
	}
	cJSON_AddNumberToObject(root, "r09CharaCount", cnt);
	cJSON_AddItemToObject(root, "r09Characters", j_chars);
}

/* =========================================================================
 * Rance 01 (Quest for Hikari Remake) Live State / Actions / Frame Locks
 * ========================================================================= */

static void vm_enforce_r01_locks(int r01_mask)
{
	if (!s_r01_compatible || s_r01_applying || !vm_globals_ready())
		return;
	int p_slot = -1;
	struct page *p = r01_get_player(&p_slot);
	if (!p)
		return;
	if (!vm_callstack_has_fno(s_r01_fn_gameflow_run))
		return;
	s_r01_applying = true;
	if ((r01_mask & LOCK_R01_FULL_HP) && p->values[4].i > 0 && p->values[3].i < p->values[4].i) {
		if (s_r01_fn_set_hp >= 0)
			vm_call_method_1(s_r01_fn_set_hp, p_slot, p->values[4].i);
		else
			p->values[3].i = p->values[4].i;
		p = r01_get_player(&p_slot);
	}
	if ((r01_mask & LOCK_R01_INF_GOLD) && p && p->values[10].i < 999999) {
		if (s_r01_fn_set_gold >= 0)
			vm_call_method_1(s_r01_fn_set_gold, p_slot, 999999);
		else
			p->values[10].i = 999999;
		p = r01_get_player(&p_slot);
	}
	if ((r01_mask & LOCK_R01_NO_DEBUFF) && p) {
		struct page *bc = r03_checked_page(p->values[12].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0);
		if (bc && bc->nr_vars > 0)
			r01_clear_all_bad_conditions(p_slot, p);
	}
	s_r01_applying = false;
}

static void r01_unlock_all_gallery(void)
{
	if (!vm_globals_ready())
		return;
	if (s_r01_cgmgr_glob >= 0 && s_r01_fn_reg_cg >= 0) {
		struct page *mgr = r03_checked_page(heap[0].page->values[s_r01_cgmgr_glob].i, STRUCT_PAGE, s_r01_cgmgr_type, 1);
		struct page *list = mgr ? r03_checked_page(mgr->values[0].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0) : NULL;
		if (list) {
			bool saved_dg = dg_callbegin_pending;
			dg_callbegin_pending = false;
			for (int i = 0; i < list->nr_vars; i++) {
				struct page *item = r03_checked_page(list->values[i].i, STRUCT_PAGE, s_r01_cg_type, 2);
				struct page *cgs = item ? r03_checked_page(item->values[1].i, ARRAY_PAGE, AIN_ARRAY_STRING, 0) : NULL;
				if (!cgs) continue;
				for (int j = 0; j < cgs->nr_vars; j++) {
					int str_slot = cgs->values[j].i;
					if (!string_index_valid(str_slot)) continue;
					struct string *st = heap_get_string(str_slot);
					if (!st || !st->size) continue;
					int copy = heap_alloc_string(make_string(st->text, st->size));
					stack_push(copy);
					vm_call(s_r01_fn_reg_cg, -1);
				}
			}
			dg_callbegin_pending = saved_dg;
		}
	}
	if (s_r01_repmgr_glob >= 0 && s_r01_fn_add_replay >= 0) {
		struct page *mgr = r03_checked_page(heap[0].page->values[s_r01_repmgr_glob].i, STRUCT_PAGE, s_r01_repmgr_type, 1);
		struct page *list = mgr ? r03_checked_page(mgr->values[0].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0) : NULL;
		if (list) {
			bool saved_dg = dg_callbegin_pending;
			dg_callbegin_pending = false;
			for (int i = 0; i < list->nr_vars; i++) {
				struct page *item = r03_checked_page(list->values[i].i, STRUCT_PAGE, s_r01_rep_type, 2);
				int fn_slot = item ? item->values[1].i : 0;
				if (!string_index_valid(fn_slot)) continue;
				struct string *st = heap_get_string(fn_slot);
				if (!st || !st->size) continue;
				int copy = heap_alloc_string(make_string(st->text, st->size));
				stack_push(copy);
				vm_call(s_r01_fn_add_replay, -1);
			}
			dg_callbegin_pending = saved_dg;
		}
	}
}

static void vm_execute_r01_action(const char *json_str)
{
	if (!json_str || !json_str[0])
		return;
	cJSON *act = cJSON_Parse(json_str);
	if (!act)
		return;

	cJSON *it;
	bool persist_cfg = false;
	int save_r10_mask = 0;
	pthread_mutex_lock(&g_mod_mutex);
	if ((it = cJSON_GetObjectItem(act, "setRance01LockMask")) && cJSON_IsNumber(it)) {
		g_r01_lock_mask = it->valueint;
		g_has_r01_lock_mask_init = true;
		persist_cfg = true;
	}
	save_r10_mask = g_live_lock_mask;
	pthread_mutex_unlock(&g_mod_mutex);
	vm_apply_ui_scale_from_json(act, &persist_cfg, &save_r10_mask);
	if (persist_cfg)
		persist_modifier_config_from_native(save_r10_mask);

	int p_slot = -1;
	struct page *p = r01_get_player(&p_slot);
	if (!s_r01_compatible || !p || s_r01_applying) {
		cJSON_Delete(act);
		return;
	}

	s_r01_applying = true;
	if ((it = cJSON_GetObjectItem(act, "r01UnlockGallery")) && cJSON_IsTrue(it)) {
		r01_unlock_all_gallery();
		p = r01_get_player(&p_slot);
	}
	if (p) {
		if ((it = cJSON_GetObjectItem(act, "r01AddGold")) && cJSON_IsNumber(it)) {
			int32_t next = r03_clamp_sum(p->values[10].i, it->valueint);
			if (s_r01_fn_set_gold >= 0) vm_call_method_1(s_r01_fn_set_gold, p_slot, next);
			else p->values[10].i = next;
			p = r01_get_player(&p_slot);
		}
		if (p && (it = cJSON_GetObjectItem(act, "r01Gold")) && cJSON_IsNumber(it)) {
			int32_t next = vm_dd_clamp(it->valuedouble, 0, INT32_MAX);
			if (s_r01_fn_set_gold >= 0) vm_call_method_1(s_r01_fn_set_gold, p_slot, next);
			else p->values[10].i = next;
			p = r01_get_player(&p_slot);
		}
		if (p && (it = cJSON_GetObjectItem(act, "r01MaxChipCap")) && cJSON_IsTrue(it)) {
			if (s_r01_fn_set_max_chip >= 0) vm_call_method_1(s_r01_fn_set_max_chip, p_slot, 99);
			if (s_r01_fn_set_max_dep >= 0) vm_call_method_1(s_r01_fn_set_max_dep, p_slot, 999);
			p = r01_get_player(&p_slot);
		}
		if (p && (it = cJSON_GetObjectItem(act, "r01HealFull")) && cJSON_IsTrue(it)) {
			int32_t mhp = p->values[4].i > 0 ? p->values[4].i : 1;
			if (s_r01_fn_set_hp >= 0) vm_call_method_1(s_r01_fn_set_hp, p_slot, mhp);
			else p->values[3].i = mhp;
			p = r01_get_player(&p_slot);
			if (p) r01_clear_all_bad_conditions(p_slot, p);
			p = r01_get_player(&p_slot);
		}
		if (p && (it = cJSON_GetObjectItem(act, "r01MaxLevelCap")) && cJSON_IsNumber(it)) {
			int32_t cap = vm_dd_clamp(it->valueint, 1, 999);
			if (s_r01_fn_set_max_level >= 0) vm_call_method_1(s_r01_fn_set_max_level, p_slot, cap);
			else p->values[1].i = cap;
			p = r01_get_player(&p_slot);
		}
		if (p && (it = cJSON_GetObjectItem(act, "r01Level")) && cJSON_IsNumber(it)) {
			int32_t lv = vm_dd_clamp(it->valueint, 1, 999);
			if (lv > p->values[1].i) {
				if (s_r01_fn_set_max_level >= 0) vm_call_method_1(s_r01_fn_set_max_level, p_slot, lv);
				else p->values[1].i = lv;
				p = r01_get_player(&p_slot);
			}
			if (p) {
				if (s_r01_fn_set_level >= 0) vm_call_method_1(s_r01_fn_set_level, p_slot, lv);
				else p->values[0].i = lv;
				p = r01_get_player(&p_slot);
			}
		}
		if (p && (it = cJSON_GetObjectItem(act, "r01MaxHp")) && cJSON_IsNumber(it)) {
			int32_t mhp = vm_dd_clamp(it->valueint, 1, 999999);
			if (s_r01_fn_set_max_hp >= 0) vm_call_method_1(s_r01_fn_set_max_hp, p_slot, mhp);
			else p->values[4].i = mhp;
			p = r01_get_player(&p_slot);
			if (p) {
				if (s_r01_fn_set_hp >= 0) vm_call_method_1(s_r01_fn_set_hp, p_slot, mhp);
				else p->values[3].i = mhp;
				p = r01_get_player(&p_slot);
			}
		}
		if (p && (it = cJSON_GetObjectItem(act, "r01Hp")) && cJSON_IsNumber(it)) {
			int32_t hp = vm_dd_clamp(it->valueint, 1, p->values[4].i > 0 ? p->values[4].i : 999999);
			if (s_r01_fn_set_hp >= 0) vm_call_method_1(s_r01_fn_set_hp, p_slot, hp);
			else p->values[3].i = hp;
			p = r01_get_player(&p_slot);
		}
		if (p && (it = cJSON_GetObjectItem(act, "r01Exp")) && cJSON_IsNumber(it)) {
			int32_t exp = vm_dd_clamp(it->valuedouble, 0, INT32_MAX);
			if (s_r01_fn_set_exp >= 0) vm_call_method_1(s_r01_fn_set_exp, p_slot, exp);
			else p->values[2].i = exp;
			p = r01_get_player(&p_slot);
		}
		if (p && (it = cJSON_GetObjectItem(act, "r01Luck")) && cJSON_IsNumber(it)) {
			int32_t luck = vm_dd_clamp(it->valueint, 0, 9999);
			if (s_r01_fn_set_luck >= 0) vm_call_method_1(s_r01_fn_set_luck, p_slot, luck);
			else p->values[9].i = luck;
		}
	}
	s_r01_applying = false;
	cJSON_Delete(act);
}

static void vm_r01_populate_live_state_json(cJSON *root)
{
	struct page *p = r01_get_player(NULL);
	bool active = s_r01_compatible && p && vm_callstack_has_fno(s_r01_fn_gameflow_run);
	if (!active) {
		cJSON_AddBoolToObject(root, "inGame", false);
		return;
	}
	cJSON_AddBoolToObject(root, "inGame", true);
	cJSON_AddNumberToObject(root, "level", p->values[0].i);
	cJSON_AddNumberToObject(root, "maxLevel", p->values[1].i);
	cJSON_AddNumberToObject(root, "exp", p->values[2].i);
	cJSON_AddNumberToObject(root, "hp", p->values[3].i);
	cJSON_AddNumberToObject(root, "maxHp", p->values[4].i);
	cJSON_AddNumberToObject(root, "luck", p->values[9].i);
	cJSON_AddNumberToObject(root, "gold", p->values[10].i);
	struct page *gt = (s_r01_time_glob >= 0 && vm_globals_ready())
		? r03_checked_page(heap[0].page->values[s_r01_time_glob].i, STRUCT_PAGE, s_r01_time_type, 2) : NULL;
	cJSON_AddNumberToObject(root, "day", gt ? gt->values[0].i : 1);
	cJSON_AddNumberToObject(root, "timeBlock", gt ? gt->values[1].i : 0);
	struct page *deps = (s_r01_chipdep_glob >= 0 && vm_globals_ready())
		? r03_checked_page(heap[0].page->values[s_r01_chipdep_glob].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0) : NULL;
	int hand_cap = 0, hand_cnt = 0, store_cap = 0, store_cnt = 0;
	if (deps && deps->nr_vars >= 2) {
		struct page *d0 = r03_checked_page(deps->values[0].i, STRUCT_PAGE, s_r01_chipdep_type, 3);
		struct page *d1 = r03_checked_page(deps->values[1].i, STRUCT_PAGE, s_r01_chipdep_type, 3);
		if (d0) {
			struct page *a0 = r03_checked_page(d0->values[1].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0);
			hand_cnt = a0 ? a0->nr_vars : 0;
			hand_cap = d0->values[2].i;
		}
		if (d1) {
			struct page *a1 = r03_checked_page(d1->values[1].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0);
			store_cnt = a1 ? a1->nr_vars : 0;
			store_cap = d1->values[2].i;
		}
	}
	cJSON_AddNumberToObject(root, "handChips", hand_cnt);
	cJSON_AddNumberToObject(root, "handChipCap", hand_cap);
	cJSON_AddNumberToObject(root, "storageChips", store_cnt);
	cJSON_AddNumberToObject(root, "storageChipCap", store_cap);
}

/* =========================================================================
 * Rance 02 (Rebellious Maidens Kai) Live State / Actions / Frame Locks
 * ========================================================================= */

static bool r02_is_in_game(struct page *cd)
{
	if (!cd || cd->nr_vars < 2)
		return false;
	struct page *c1 = r03_checked_page(cd->values[1].i, STRUCT_PAGE, s_r02_chara_type, 15);
	struct page *hp = c1 ? r03_checked_page(c1->values[5].i, ARRAY_PAGE, AIN_ARRAY_INT, 0) : NULL;
	return c1 && c1->values[4].i && hp && hp->nr_vars >= 3 && hp->values[2].i > 0;
}

static void r02_refresh_hud(void)
{
	struct page *sprites = vm_get_global_page_idx(s_r02_stat_sprites_glob);
	if (!sprites || sprites->type != ARRAY_PAGE || sprites->array.rank != 1 || sprites->nr_vars < 2)
		return;
	/* The first HP number exists and is visible only when the status panel is drawn. */
	struct sact_sprite *hp = sact_try_get_sprite(sprites->values[1].i);
	if (hp && sprite_get_show(hp))
		vm_call_hud_setter(s_r02_fn_disp_stat, -1, 1, 0);
}

static bool vm_enforce_r02_locks(int r02_mask)
{
	if (!s_r02_compatible || s_r02_applying || !vm_globals_ready())
		return false;
	struct page *cd = r02_get_cd_array();
	if (!r02_is_in_game(cd))
		return false;
	bool refresh_hud = false;
	if ((r02_mask & LOCK_R02_INF_GOLD) && s_r02_gold_glob >= 0) {
		if (heap[0].page->values[s_r02_gold_glob].i < 999999)
			heap[0].page->values[s_r02_gold_glob].i = 999999;
	}
	if (r02_mask & LOCK_R02_INF_ITEMS) {
		int item_globs[] = { s_r02_item_world, s_r02_item_tree, s_r02_item_potion, s_r02_item_candy };
		for (size_t k = 0; k < sizeof(item_globs) / sizeof(item_globs[0]); k++) {
			if (item_globs[k] >= 0 && heap[0].page->values[item_globs[k]].i < 99)
				heap[0].page->values[item_globs[k]].i = 99;
		}
		if (s_r02_item_light >= 0 && heap[0].page->values[s_r02_item_light].i < 9)
			heap[0].page->values[s_r02_item_light].i = 9;
		if (s_r02_item_rope >= 0 && heap[0].page->values[s_r02_item_rope].i < 9)
			heap[0].page->values[s_r02_item_rope].i = 9;
	}
	if (r02_mask & LOCK_R02_ALLY_FULL_HP_SP) {
		for (int i = 1; i <= 6 && i < cd->nr_vars; i++) {
			struct page *c = r03_checked_page(cd->values[i].i, STRUCT_PAGE, s_r02_chara_type, 15);
			if (!c || !c->values[4].i) continue;
			struct page *hp = r03_checked_page(c->values[5].i, ARRAY_PAGE, AIN_ARRAY_INT, 0);
			struct page *st = r03_checked_page(c->values[6].i, ARRAY_PAGE, AIN_ARRAY_INT, 0);
			if (hp && hp->nr_vars >= 3 && hp->values[2].i > 0 && hp->values[1].i < hp->values[2].i) {
				hp->values[1].i = hp->values[2].i;
				refresh_hud = true;
			}
			if (st && st->nr_vars >= 3 && st->values[2].i > 0 && st->values[1].i < st->values[2].i) {
				st->values[1].i = st->values[2].i;
				refresh_hud = true;
			}
		}
	}
	return refresh_hud;
}

static void r02_unlock_all_gallery(void)
{
	if (!vm_globals_ready())
		return;
	bool saved_dg = dg_callbegin_pending;
	dg_callbegin_pending = false;
	bool did_cg = false;
	if (s_r02_fn_reg_cg >= 0 && s_r02_cgdoc_glob >= 0) {
		struct page *doc = r03_checked_page(heap[0].page->values[s_r02_cgdoc_glob].i, STRUCT_PAGE, s_r02_cgdoc_type, 21);
		struct page *items = doc ? r03_checked_page(doc->values[9].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0) : NULL;
		if (items && items->array.struct_type == s_r02_cgitem_type) {
			for (int i = 0; i < items->nr_vars; i++) {
				struct page *item = r03_checked_page(items->values[i].i, STRUCT_PAGE, s_r02_cgitem_type, 5);
				struct page *list = item ? r03_checked_page(item->values[4].i, ARRAY_PAGE, AIN_ARRAY_INT, 0) : NULL;
				if (!list) continue;
				for (int j = 0; j < list->nr_vars; j++) {
					int cg = list->values[j].i;
					if (cg > 0) {
						vm_dd_call_void_1i(s_r02_fn_reg_cg, -1, cg);
						did_cg = true;
					}
				}
			}
		}
	}
	if (s_r02_fn_reg_cg >= 0 && !did_cg) {
		for (int cg = 10001; cg <= 10821; cg += 10)
			vm_dd_call_void_1i(s_r02_fn_reg_cg, -1, cg);
		vm_dd_call_void_1i(s_r02_fn_reg_cg, -1, 10182);
	}
	if (s_r02_fn_reg_rem >= 0) {
		vm_dd_call_void_1i(s_r02_fn_reg_rem, -1, 1);
		vm_dd_call_void_1i(s_r02_fn_reg_rem, -1, 2);
	}
	dg_callbegin_pending = saved_dg;
}

static bool vm_execute_r02_action(const char *json_str)
{
	if (!json_str || !json_str[0])
		return false;
	cJSON *act = cJSON_Parse(json_str);
	if (!act)
		return false;

	cJSON *it;
	bool persist_cfg = false;
	int save_r10_mask = 0;
	pthread_mutex_lock(&g_mod_mutex);
	if ((it = cJSON_GetObjectItem(act, "setRance02LockMask")) && cJSON_IsNumber(it)) {
		g_r02_lock_mask = it->valueint;
		g_has_r02_lock_mask_init = true;
		persist_cfg = true;
	}
	save_r10_mask = g_live_lock_mask;
	pthread_mutex_unlock(&g_mod_mutex);
	vm_apply_ui_scale_from_json(act, &persist_cfg, &save_r10_mask);
	if (persist_cfg)
		persist_modifier_config_from_native(save_r10_mask);

	if (!s_r02_compatible || !vm_globals_ready() || s_r02_applying) {
		cJSON_Delete(act);
		return false;
	}

	s_r02_applying = true;
	if ((it = cJSON_GetObjectItem(act, "r02UnlockGallery")) && cJSON_IsTrue(it))
		r02_unlock_all_gallery();

	struct page *cd = r02_get_cd_array();
	bool refresh_hud = false;
	if (r02_is_in_game(cd)) {
		if ((it = cJSON_GetObjectItem(act, "r02AddGold")) && cJSON_IsNumber(it) && s_r02_gold_glob >= 0) {
			int32_t cur = heap[0].page->values[s_r02_gold_glob].i;
			heap[0].page->values[s_r02_gold_glob].i = vm_dd_clamp((double)cur + it->valuedouble, 0, 99999999);
		}
		if ((it = cJSON_GetObjectItem(act, "r02Gold")) && cJSON_IsNumber(it) && s_r02_gold_glob >= 0)
			heap[0].page->values[s_r02_gold_glob].i = vm_dd_clamp(it->valuedouble, 0, 99999999);
		if ((it = cJSON_GetObjectItem(act, "r02Karma")) && cJSON_IsNumber(it) && s_r02_karma_glob >= 0)
			heap[0].page->values[s_r02_karma_glob].i = vm_dd_clamp(it->valueint, -9999, 9999);
		if ((it = cJSON_GetObjectItem(act, "r02SupplyItems")) && cJSON_IsTrue(it)) {
			if (s_r02_item_world >= 0) heap[0].page->values[s_r02_item_world].i = 99;
			if (s_r02_item_tree >= 0) heap[0].page->values[s_r02_item_tree].i = 99;
			if (s_r02_item_potion >= 0) heap[0].page->values[s_r02_item_potion].i = 99;
			if (s_r02_item_candy >= 0) heap[0].page->values[s_r02_item_candy].i = 99;
			if (s_r02_item_light >= 0) heap[0].page->values[s_r02_item_light].i = 9;
			if (s_r02_item_rope >= 0) heap[0].page->values[s_r02_item_rope].i = 9;
		}
		if ((it = cJSON_GetObjectItem(act, "r02HealAll")) && cJSON_IsTrue(it)) {
			refresh_hud = true;
			for (int i = 1; i <= 6 && i < cd->nr_vars; i++) {
				struct page *c = r03_checked_page(cd->values[i].i, STRUCT_PAGE, s_r02_chara_type, 15);
				if (!c || !c->values[4].i) continue;
				struct page *hp = r03_checked_page(c->values[5].i, ARRAY_PAGE, AIN_ARRAY_INT, 0);
				struct page *st = r03_checked_page(c->values[6].i, ARRAY_PAGE, AIN_ARRAY_INT, 0);
				if (hp && hp->nr_vars >= 3 && hp->values[2].i > 0) hp->values[1].i = hp->values[2].i;
				if (st && st->nr_vars >= 3 && st->values[2].i > 0) st->values[1].i = st->values[2].i;
			}
		}
		cJSON *ce = cJSON_GetObjectItem(act, "r02CharEdit");
		if (ce && cJSON_IsObject(ce)) {
			cJSON *jt = cJSON_GetObjectItem(ce, "target");
			int target = (jt && cJSON_IsNumber(jt)) ? jt->valueint : -1;
			for (int i = 1; i <= 6 && i < cd->nr_vars; i++) {
				if (target >= 0 && i != target) continue;
				struct page *c = r03_checked_page(cd->values[i].i, STRUCT_PAGE, s_r02_chara_type, 15);
				if (!c || !c->values[4].i) continue;
				struct page *hp = r03_checked_page(c->values[5].i, ARRAY_PAGE, AIN_ARRAY_INT, 0);
				struct page *st = r03_checked_page(c->values[6].i, ARRAY_PAGE, AIN_ARRAY_INT, 0);
				struct page *ab = r03_checked_page(c->values[8].i, ARRAY_PAGE, AIN_ARRAY_INT, 0);
				cJSON *f;
				if ((f = cJSON_GetObjectItem(ce, "lv")) && cJSON_IsNumber(f))
					c->values[1].i = vm_dd_clamp(f->valueint, 1, 999);
				if ((f = cJSON_GetObjectItem(ce, "exp")) && cJSON_IsNumber(f))
					c->values[2].i = vm_dd_clamp(f->valuedouble, 0, 9999999);
				if (hp && hp->nr_vars >= 3) {
					if ((f = cJSON_GetObjectItem(ce, "hpMax")) && cJSON_IsNumber(f))
						hp->values[2].i = vm_dd_clamp(f->valueint, 1, 99999);
					if ((f = cJSON_GetObjectItem(ce, "hp")) && cJSON_IsNumber(f))
						hp->values[1].i = vm_dd_clamp(f->valueint, 1, 99999);
				}
				if (st && st->nr_vars >= 3) {
					if ((f = cJSON_GetObjectItem(ce, "spMax")) && cJSON_IsNumber(f))
						st->values[2].i = vm_dd_clamp(f->valueint, 1, 99999);
					if ((f = cJSON_GetObjectItem(ce, "sp")) && cJSON_IsNumber(f))
						st->values[1].i = vm_dd_clamp(f->valueint, 0, 99999);
				}
				refresh_hud = true;
				if (ab && ab->nr_vars >= 7) {
					if ((f = cJSON_GetObjectItem(ce, "hit")) && cJSON_IsNumber(f))
						ab->values[1].i = vm_dd_clamp(f->valueint, 0, 9999);
					if ((f = cJSON_GetObjectItem(ce, "eva")) && cJSON_IsNumber(f))
						ab->values[2].i = vm_dd_clamp(f->valueint, 0, 9999);
					if ((f = cJSON_GetObjectItem(ce, "atk")) && cJSON_IsNumber(f))
						ab->values[3].i = vm_dd_clamp(f->valueint, 0, 99999);
					if ((f = cJSON_GetObjectItem(ce, "def")) && cJSON_IsNumber(f))
						ab->values[4].i = vm_dd_clamp(f->valueint, 0, 99999);
					if ((f = cJSON_GetObjectItem(ce, "know")) && cJSON_IsNumber(f))
						ab->values[5].i = vm_dd_clamp(f->valueint, 0, 9999);
					if ((f = cJSON_GetObjectItem(ce, "luck")) && cJSON_IsNumber(f))
						ab->values[6].i = vm_dd_clamp(f->valueint, 0, 9999);
				}
				if ((f = cJSON_GetObjectItem(ce, "fullHeal")) && cJSON_IsTrue(f)) {
					if (hp && hp->nr_vars >= 3 && hp->values[2].i > 0) hp->values[1].i = hp->values[2].i;
					if (st && st->nr_vars >= 3 && st->values[2].i > 0) st->values[1].i = st->values[2].i;
				}
				if (target >= 0) break;
			}
		}
	}
	s_r02_applying = false;
	cJSON_Delete(act);
	return refresh_hud;
}

static void vm_r02_populate_live_state_json(cJSON *root)
{
	struct page *cd = r02_get_cd_array();
	if (!s_r02_compatible || !r02_is_in_game(cd)) {
		cJSON_AddBoolToObject(root, "inGame", false);
		return;
	}
	cJSON_AddBoolToObject(root, "inGame", true);
	cJSON_AddNumberToObject(root, "gold", s_r02_gold_glob >= 0 ? heap[0].page->values[s_r02_gold_glob].i : 0);
	cJSON_AddNumberToObject(root, "karma", s_r02_karma_glob >= 0 ? heap[0].page->values[s_r02_karma_glob].i : 0);
	cJSON_AddNumberToObject(root, "worldDrug", s_r02_item_world >= 0 ? heap[0].page->values[s_r02_item_world].i : 0);
	cJSON_AddNumberToObject(root, "returnTree", s_r02_item_tree >= 0 ? heap[0].page->values[s_r02_item_tree].i : 0);
	cJSON_AddNumberToObject(root, "potion", s_r02_item_potion >= 0 ? heap[0].page->values[s_r02_item_potion].i : 0);
	cJSON_AddNumberToObject(root, "candy", s_r02_item_candy >= 0 ? heap[0].page->values[s_r02_item_candy].i : 0);
	cJSON_AddNumberToObject(root, "light", s_r02_item_light >= 0 ? heap[0].page->values[s_r02_item_light].i : 0);
	cJSON_AddNumberToObject(root, "rope", s_r02_item_rope >= 0 ? heap[0].page->values[s_r02_item_rope].i : 0);
	cJSON_AddBoolToObject(root, "inBattle", s_r02_battle_glob >= 0 && heap[0].page->values[s_r02_battle_glob].i != 0);

	cJSON *j_chars = cJSON_CreateArray();
	int cnt = 0;
	for (int i = 1; i <= 6 && i < cd->nr_vars; i++) {
		struct page *c = r03_checked_page(cd->values[i].i, STRUCT_PAGE, s_r02_chara_type, 15);
		if (!c || !c->values[4].i) continue;
		struct page *hp = r03_checked_page(c->values[5].i, ARRAY_PAGE, AIN_ARRAY_INT, 0);
		struct page *st = r03_checked_page(c->values[6].i, ARRAY_PAGE, AIN_ARRAY_INT, 0);
		struct page *ab = r03_checked_page(c->values[8].i, ARRAY_PAGE, AIN_ARRAY_INT, 0);
		char *name_u = vm_dd_read_utf8(c->values[0].i);
		cJSON *jc = cJSON_CreateObject();
		cJSON_AddNumberToObject(jc, "index", i);
		cJSON_AddStringToObject(jc, "name", (name_u && name_u[0]) ? name_u : "?");
		free(name_u);
		cJSON_AddNumberToObject(jc, "lv", c->values[1].i);
		cJSON_AddNumberToObject(jc, "exp", c->values[2].i);
		cJSON_AddNumberToObject(jc, "hp", (hp && hp->nr_vars >= 3) ? hp->values[1].i : 0);
		cJSON_AddNumberToObject(jc, "hpMax", (hp && hp->nr_vars >= 3) ? hp->values[2].i : 0);
		cJSON_AddNumberToObject(jc, "sp", (st && st->nr_vars >= 3) ? st->values[1].i : 0);
		cJSON_AddNumberToObject(jc, "spMax", (st && st->nr_vars >= 3) ? st->values[2].i : 0);
		cJSON_AddNumberToObject(jc, "hit", (ab && ab->nr_vars >= 7) ? ab->values[1].i : 0);
		cJSON_AddNumberToObject(jc, "eva", (ab && ab->nr_vars >= 7) ? ab->values[2].i : 0);
		cJSON_AddNumberToObject(jc, "atk", (ab && ab->nr_vars >= 7) ? ab->values[3].i : 0);
		cJSON_AddNumberToObject(jc, "def", (ab && ab->nr_vars >= 7) ? ab->values[4].i : 0);
		cJSON_AddNumberToObject(jc, "know", (ab && ab->nr_vars >= 7) ? ab->values[5].i : 0);
		cJSON_AddNumberToObject(jc, "luck", (ab && ab->nr_vars >= 7) ? ab->values[6].i : 0);
		cJSON_AddItemToArray(j_chars, jc);
		cnt++;
	}
	cJSON_AddNumberToObject(root, "r02CharaCount", cnt);
	cJSON_AddItemToObject(root, "r02Characters", j_chars);
}

#include "modifier_extra.inc"

static char *vm_build_live_state_json(int lock_mask)
{
	cJSON *root = cJSON_CreateObject();
	cJSON_AddBoolToObject(root, "isRance10", game_rance10);
	cJSON_AddBoolToObject(root, "isDohnaDohna", game_dohnadohna);
	cJSON_AddBoolToObject(root, "isEvenicle", vm_is_evenicle_family());
	cJSON_AddBoolToObject(root, "isSengokuRance", game_sengokurance);
	cJSON_AddBoolToObject(root, "isRance01", game_rance01);
	cJSON_AddBoolToObject(root, "isRance02", game_rance02);
	cJSON_AddBoolToObject(root, "isRance03", game_rance03);
	cJSON_AddBoolToObject(root, "isRance08", game_rance8);
	cJSON_AddBoolToObject(root, "isRance09", game_rance9);
	cJSON_AddNumberToObject(root, "lockMask", lock_mask);
	pthread_mutex_lock(&g_mod_mutex);
	int cur_dd_mask = g_dohna_lock_mask;
	int cur_ev_mask = g_evenicle_lock_mask;
	int cur_sr_mask = g_sr_lock_mask;
	int cur_r01_mask = g_r01_lock_mask;
	int cur_r02_mask = g_r02_lock_mask;
	int cur_r03_mask = g_r03_lock_mask;
	int cur_r08_mask = g_r08_lock_mask;
	int cur_r09_mask = g_r09_lock_mask;
	int cur_font_pct = g_font_scale_pct;
	int cur_sb_pct = g_scrollbar_scale_pct;
	pthread_mutex_unlock(&g_mod_mutex);
	cJSON_AddNumberToObject(root, "dohnaLockMask", cur_dd_mask);
	cJSON_AddNumberToObject(root, "evenicleLockMask", cur_ev_mask);
	cJSON_AddNumberToObject(root, "srLockMask", cur_sr_mask);
	cJSON_AddNumberToObject(root, "r01LockMask", cur_r01_mask);
	cJSON_AddNumberToObject(root, "r02LockMask", cur_r02_mask);
	cJSON_AddNumberToObject(root, "r03LockMask", cur_r03_mask);
	cJSON_AddNumberToObject(root, "r08LockMask", cur_r08_mask);
	cJSON_AddNumberToObject(root, "r09LockMask", cur_r09_mask);
	cJSON_AddNumberToObject(root, "fontScalePct", cur_font_pct);
	cJSON_AddNumberToObject(root, "scrollbarScalePct", cur_sb_pct);

	if (game_dohnadohna) {
		vm_dd_populate_live_state_json(root);
		char *out = cJSON_PrintUnformatted(root);
		cJSON_Delete(root);
		return out;
	}

	if (vm_is_evenicle_family()) {
		vm_ev_populate_live_state_json(root);
		char *out = cJSON_PrintUnformatted(root);
		cJSON_Delete(root);
		return out;
	}

	if (game_sengokurance) {
		vm_sr_populate_live_state_json(root);
		char *out = cJSON_PrintUnformatted(root);
		cJSON_Delete(root);
		return out;
	}

	if (game_rance01) {
		vm_r01_populate_live_state_json(root);
		char *out = cJSON_PrintUnformatted(root);
		cJSON_Delete(root);
		return out;
	}

	if (game_rance02) {
		vm_r02_populate_live_state_json(root);
		char *out = cJSON_PrintUnformatted(root);
		cJSON_Delete(root);
		return out;
	}

	if (game_rance03) {
		vm_r03_populate_live_state_json(root);
		char *out = cJSON_PrintUnformatted(root);
		cJSON_Delete(root);
		return out;
	}

	if (game_rance8) {
		vm_r08_populate_live_state_json(root);
		char *out = cJSON_PrintUnformatted(root);
		cJSON_Delete(root);
		return out;
	}

	if (game_rance9) {
		vm_r09_populate_live_state_json(root);
		char *out = cJSON_PrintUnformatted(root);
		cJSON_Delete(root);
		return out;
	}

	if (ex_is_active_extra_game()) {
		vm_ex_populate_live_state_json(root);
		char *out = cJSON_PrintUnformatted(root);
		cJSON_Delete(root);
		return out;
	}

	if (!game_rance10) {
		cJSON_AddBoolToObject(root, "inGame", ain != NULL);
		char *out = cJSON_PrintUnformatted(root);
		cJSON_Delete(root);
		return out;
	}

	vm_inspect_mod_streamer_ain();
	struct page *gc    = s_glob_game_context >= 0 ? vm_get_global_page_idx(s_glob_game_context) : vm_get_global_page("g_gameContext", NULL);
	struct page *cfg   = s_glob_game_config >= 0 ? vm_get_global_page_idx(s_glob_game_config) : vm_get_global_page("g_gameConfig", NULL);
	struct page *pcp   = s_glob_pcp >= 0 ? vm_get_global_page_idx(s_glob_pcp) : vm_get_global_page("g_playerCommonParam", NULL);
	struct page *pb    = s_glob_party_bonus >= 0 ? vm_get_global_page_idx(s_glob_party_bonus) : vm_get_global_page("g_partyBonus", NULL);
	struct page *party = s_glob_party >= 0 ? vm_get_global_page_idx(s_glob_party) : vm_get_global_page("g_party", NULL);
	struct page *ps    = vm_get_field_page(party, "m_status", NULL);
	struct page *cs    = vm_get_field_page(ps, "m_status", NULL);
	struct page *bc    = s_glob_battle_ctx >= 0 ? vm_get_global_page_idx(s_glob_battle_ctx) : vm_get_global_page("g_battleContext", NULL);
	struct page *enemy = s_glob_enemy >= 0 ? vm_get_global_page_idx(s_glob_enemy) : vm_get_global_page("g_enemy", NULL);

	bool streamer_mode_supported = vm_has_mod_streamer_mode();
	cJSON_AddBoolToObject(root, "streamerModeSupported", streamer_mode_supported);
	if (streamer_mode_supported) {
		int sm = vm_get_live_streamer_mode(!vm_is_in_card_ui_callstack());
		if (sm >= 0)
			cJSON_AddNumberToObject(root, "streamerMode", sm);
	} else if (cfg) {
		cJSON_AddNumberToObject(root, "streamerMode", vm_get_field_int(cfg, "m_isSummaryMode", NULL, 0));
	}

	if (!ain || !gc || !pcp) {
		cJSON_AddBoolToObject(root, "inGame", false);
		char *out = cJSON_PrintUnformatted(root);
		cJSON_Delete(root);
		return out;
	}
	cJSON_AddBoolToObject(root, "inGame", true);
	cJSON_AddBoolToObject(root, "inBattle", (bc != NULL && enemy != NULL));

	cJSON_AddNumberToObject(root, "turn", vm_get_field_int(gc, "m_turn", NULL, 0));
	cJSON_AddNumberToObject(root, "chapter", vm_get_field_int(gc, "m_chapter", NULL, 0));

	int idx_diff = s_glob_diff >= 0 ? s_glob_diff : vm_find_global_idx(GBK_DIFFICULTY, SJIS_DIFFICULTY);
	int idx_bc   = s_glob_battle_count >= 0 ? s_glob_battle_count : vm_find_global_idx(GBK_BATTLE_COUNT, SJIS_BATTLE_COUNT);
	cJSON_AddNumberToObject(root, "difficulty", idx_diff >= 0 ? global_get(idx_diff).i : 1);
	cJSON_AddNumberToObject(root, "battleCount", idx_bc >= 0 ? global_get(idx_bc).i : 0);

	cJSON_AddNumberToObject(root, "foodTicket", vm_get_field_int(pcp, "m_foodTicket", NULL, 0));
	cJSON_AddNumberToObject(root, "honor", vm_get_field_int(pcp, "m_honor", NULL, 0));
	cJSON_AddNumberToObject(root, "friendPoint", vm_get_field_int(pcp, "m_friendPoint", NULL, 0));
	cJSON_AddNumberToObject(root, "totalExp", vm_get_field_int(pcp, "<TotalExp>", NULL, 0));
	cJSON_AddNumberToObject(root, "partyRank", vm_get_field_int(pcp, "<PartyRank>", NULL, 0));
	cJSON_AddNumberToObject(root, "treasureNotGetCount", vm_get_field_int(pcp, "<TreasureNotGetCount>", NULL, 0));
	cJSON_AddNumberToObject(root, "itemStar", vm_get_field_int(pcp, "<ItemStar>", NULL, 0));
	cJSON_AddNumberToObject(root, "isUsedHannyZippo", vm_get_field_int(pcp, "<IsUsedHannyZippo>", NULL, 0));
	cJSON_AddNumberToObject(root, "partyBonusMaxPoint", vm_get_field_int(pb, "m_maxPoint", NULL, 0));

	cJSON_AddNumberToObject(root, "partyHp", vm_get_field_int(party, "m_hp", NULL, 0));
	cJSON_AddNumberToObject(root, "partyMaxHp", vm_get_field_int(ps, "m_totalHp", NULL, 0));
	cJSON_AddNumberToObject(root, "partyAvailCount", vm_get_field_int(party, "m_availableCount", NULL, 4));
	cJSON_AddNumberToObject(root, "partyIsFixStatus", vm_get_field_int(party, "<IsFixStatus>", NULL, 0));
	cJSON_AddNumberToObject(root, "partyAtk", vm_get_field_int(cs, "<Atk>", NULL, 0));
	cJSON_AddNumberToObject(root, "atkPerPhys", vm_get_field_int(cs, "m_AtkPerPhysical", NULL, 0));
	cJSON_AddNumberToObject(root, "atkPerMag", vm_get_field_int(cs, "m_AtkPerMagical", NULL, 0));
	cJSON_AddNumberToObject(root, "defPerPhys", vm_get_field_int(cs, "m_DefPerPhysical", NULL, 0));
	cJSON_AddNumberToObject(root, "defPerMag", vm_get_field_int(cs, "m_DefPerMagical", NULL, 0));
	cJSON_AddNumberToObject(root, "perAvoid", vm_get_field_int(cs, "m_PerAvoidance", NULL, 0));
	cJSON_AddNumberToObject(root, "decBadCond", vm_get_field_int(cs, "m_DecPerBadCondition", NULL, 0));
	cJSON_AddNumberToObject(root, "perAutoAtk", vm_get_field_int(cs, "m_perAutoBattleAtk", NULL, 0));
	cJSON_AddNumberToObject(root, "perCrit", vm_get_field_int(cs, "m_PerCritical", NULL, 0));
	cJSON_AddNumberToObject(root, "perMulti", vm_get_field_int(cs, "m_PerMultiAttack", NULL, 0));
	cJSON_AddNumberToObject(root, "perExpUp", vm_get_field_int(cs, "m_PerExpUp", NULL, 0));
	cJSON_AddNumberToObject(root, "perHpUp", vm_get_field_int(cs, "m_PerHpUp", NULL, 0));
	cJSON_AddNumberToObject(root, "perAutoHeal", vm_get_field_int(cs, "m_PerAutoHeal", NULL, 0));
	cJSON_AddNumberToObject(root, "perFindTreasure", vm_get_field_int(cs, "m_PerFindTreasure", NULL, 0));
	cJSON_AddNumberToObject(root, "perInjection", vm_get_field_int(cs, "m_PerInjection", NULL, 0));
	cJSON_AddNumberToObject(root, "addPerHit", vm_get_field_int(cs, "m_AddPerHit", NULL, 0));
	cJSON_AddNumberToObject(root, "statusMaxPer", vm_get_field_int(cs, "StatusMaxPer", NULL, 0));

	if (bc) {
		struct page *ap_p = vm_get_field_page(bc, "<Ap>", NULL);
		struct page *cb_p = vm_get_field_page(bc, "<Combo>", NULL);
		struct page *rd_p = vm_get_field_page(bc, "<Round>", NULL);
		cJSON_AddNumberToObject(root, "battleAp", vm_get_field_int(ap_p, "m_ap", NULL, 0));
		cJSON_AddNumberToObject(root, "battleCombo", vm_get_field_int(cb_p, "m_count", NULL, 0));
		cJSON_AddNumberToObject(root, "battleRound", vm_get_field_int(rd_p, "m_round", NULL, 1));
		cJSON_AddNumberToObject(root, "battleRoundLimit", vm_get_field_int(rd_p, "Limit", NULL, 30));
		cJSON_AddNumberToObject(root, "hannyZippoCount", vm_get_field_int(bc, "m_hannyZippoCount", NULL, 0));
	}
	if (enemy) {
		struct page *ecs = vm_get_field_page(enemy, "m_status", NULL);
		cJSON_AddNumberToObject(root, "enemyHp", vm_get_field_int(enemy, "m_hp", NULL, 0));
		cJSON_AddNumberToObject(root, "enemyMaxHp", vm_get_field_int(enemy, "<HpMax>", NULL, 0));
		cJSON_AddNumberToObject(root, "enemyAtk", vm_get_field_int(ecs, "<Atk>", NULL, 0));
	}

	cJSON_AddNumberToObject(root, "humanCount", vm_get_tt(33, 300000));
	cJSON_AddNumberToObject(root, "humanDead", vm_get_tt(34, 0));
	cJSON_AddNumberToObject(root, "humanAlivePer", vm_get_tt(35, 100));
	cJSON_AddNumberToObject(root, "humanDeadPer", vm_get_tt(36, 0));
	cJSON_AddNumberToObject(root, "curseTurn", vm_get_tt(41, 0));
	cJSON_AddNumberToObject(root, "rareLuck", vm_get_tt(42, 0));
	cJSON_AddNumberToObject(root, "hannyZippo", vm_get_tt(101, 0));
	cJSON_AddNumberToObject(root, "seaPlayerArmy", vm_get_tt(66, 0));
	cJSON_AddNumberToObject(root, "seaEnemyArmy", vm_get_tt(68, 0));
	cJSON_AddNumberToObject(root, "leazasState", vm_get_tt(13, 0));
	cJSON_AddNumberToObject(root, "helmanState", vm_get_tt(14, 0));
	cJSON_AddNumberToObject(root, "zethState", vm_get_tt(15, 0));
	cJSON_AddNumberToObject(root, "freeCitiesState", vm_get_tt(16, 0));
	cJSON_AddNumberToObject(root, "leazasPArmy", vm_get_tt(25, 300));
	cJSON_AddNumberToObject(root, "helmanPArmy", vm_get_tt(26, 300));
	cJSON_AddNumberToObject(root, "zethPArmy", vm_get_tt(27, 300));
	cJSON_AddNumberToObject(root, "freeCitiesPArmy", vm_get_tt(28, 300));
	cJSON_AddNumberToObject(root, "leazasEArmy", vm_get_tt(29, 500));
	cJSON_AddNumberToObject(root, "helmanEArmy", vm_get_tt(30, 500));
	cJSON_AddNumberToObject(root, "zethEArmy", vm_get_tt(31, 500));
	cJSON_AddNumberToObject(root, "freeCitiesEArmy", vm_get_tt(32, 500));
	cJSON_AddNumberToObject(root, "subRemnant", vm_get_tt(37, 0));
	cJSON_AddNumberToObject(root, "subDwarf", vm_get_tt(38, 0));
	cJSON_AddNumberToObject(root, "subGeneral", vm_get_tt(39, 0));
	cJSON_AddNumberToObject(root, "subHanny", vm_get_tt(40, 0));

	cJSON *c_state = cJSON_CreateArray();
	cJSON *c_parmy = cJSON_CreateArray();
	cJSON *c_earmy = cJSON_CreateArray();
	cJSON *c_sub   = cJSON_CreateArray();
	for (int i = 0; i < 4; i++) {
		cJSON_AddItemToArray(c_state, cJSON_CreateNumber(vm_get_tt(13 + i, 3)));
		cJSON_AddItemToArray(c_parmy, cJSON_CreateNumber(vm_get_tt(25 + i, 300)));
		cJSON_AddItemToArray(c_earmy, cJSON_CreateNumber(vm_get_tt(29 + i, 500)));
		cJSON_AddItemToArray(c_sub,   cJSON_CreateNumber(vm_get_tt(37 + i, 0)));
	}
	cJSON_AddItemToObject(root, "countryState", c_state);
	cJSON_AddItemToObject(root, "countryPlayerArmy", c_parmy);
	cJSON_AddItemToObject(root, "countryEnemyArmy", c_earmy);
	cJSON_AddItemToObject(root, "subArmy", c_sub);

	char *out = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	return out;
}

void modifier_on_frame(void)
{
	if (!vm_is_ready() || s_in_modifier_frame)
		return;
	s_in_modifier_frame = true;

	pthread_mutex_lock(&g_mod_mutex);
	g_vm_ready_cached = true;
	pthread_mutex_unlock(&g_mod_mutex);

	static uint32_t s_frame_tick = 0;
	s_frame_tick++;

	maybe_load_initial_modifier_config();

#ifndef __ANDROID__
	static bool s_env_checked = false;
	static const char *s_action_file = NULL;
	static const char *s_state_file = NULL;
	if (!s_env_checked) {
		s_action_file = getenv("XSYS4_MOD_ACTION_FILE");
		s_state_file  = getenv("XSYS4_MOD_STATE_FILE");
		s_env_checked = true;
	}
	if (s_action_file && s_action_file[0] && (s_frame_tick % 10 == 1)) {
		FILE *afp = fopen(s_action_file, "rb");
		if (afp) {
			if (fseek(afp, 0, SEEK_END) == 0) {
				long sz = ftell(afp);
				if (sz > 0 && sz < 65536 && fseek(afp, 0, SEEK_SET) == 0) {
					char *abuf = malloc((size_t)sz + 1);
					if (abuf) {
						size_t rd = fread(abuf, 1, (size_t)sz, afp);
						abuf[rd] = '\0';
						pthread_mutex_lock(&g_mod_mutex);
						free(g_pending_action_json);
						g_pending_action_json = abuf;
						pthread_mutex_unlock(&g_mod_mutex);
					}
				}
			}
			fclose(afp);
			remove(s_action_file);
		}
	}
#endif

	if (!game_rance10) {
		char *pending = NULL;
		pthread_mutex_lock(&g_mod_mutex);
		pending = g_pending_action_json;
		g_pending_action_json = NULL;
		pthread_mutex_unlock(&g_mod_mutex);

		bool action_applied = false;
		bool refresh_r02_hud = false;
		bool refresh_r08_cards = false;
		int refresh_r09_hud = 0;
		if (pending) {
			if (game_dohnadohna) {
				vm_execute_dohna_action(pending);
				action_applied = true;
			} else if (vm_is_evenicle_family()) {
				vm_execute_evenicle_action(pending);
				action_applied = true;
			} else if (game_sengokurance) {
				vm_execute_sr_action(pending);
				action_applied = true;
			} else if (game_rance01) {
				vm_execute_r01_action(pending);
				action_applied = true;
			} else if (game_rance02) {
				refresh_r02_hud = vm_execute_r02_action(pending);
				action_applied = true;
			} else if (game_rance03) {
				vm_execute_r03_action(pending);
				action_applied = true;
			} else if (game_rance8) {
				refresh_r08_cards = vm_execute_r08_action(pending);
				action_applied = true;
			} else if (game_rance9) {
				refresh_r09_hud = vm_execute_r09_action(pending);
				action_applied = true;
			} else if (ex_is_active_extra_game()) {
				vm_ex_execute_action(pending);
				action_applied = true;
			}
			free(pending);
		}

		int desired_font_pct = 100;
		int desired_sb_pct = 100;
		int dd_lock_mask = 0;
		int ev_lock_mask = 0;
		int sr_lock_mask = 0;
		int r01_lock_mask = 0;
		int r02_lock_mask = 0;
		int r03_lock_mask = 0;
		int r08_lock_mask = 0;
		int r09_lock_mask = 0;
		pthread_mutex_lock(&g_mod_mutex);
		desired_font_pct = g_font_scale_pct;
		desired_sb_pct = g_scrollbar_scale_pct;
		dd_lock_mask = g_dohna_lock_mask;
		ev_lock_mask = g_evenicle_lock_mask;
		sr_lock_mask = g_sr_lock_mask;
		r01_lock_mask = g_r01_lock_mask;
		r02_lock_mask = g_r02_lock_mask;
		r03_lock_mask = g_r03_lock_mask;
		r08_lock_mask = g_r08_lock_mask;
		r09_lock_mask = g_r09_lock_mask;
		pthread_mutex_unlock(&g_mod_mutex);

		bool ui_changed = action_applied;
		if (s_applied_font_scale_pct != desired_font_pct) {
			s_applied_font_scale_pct = desired_font_pct;
			parts_set_modifier_font_scale(desired_font_pct);
			parts_refresh_live_font_scale();
			ui_changed = true;
		}
		if (s_applied_scrollbar_scale_pct != desired_sb_pct) {
			s_applied_scrollbar_scale_pct = desired_sb_pct;
			parts_set_modifier_scrollbar_scale(desired_sb_pct);
			parts_refresh_live_scrollbar_scale();
			ui_changed = true;
		}

		if (game_dohnadohna && dd_lock_mask != 0) {
			vm_enforce_dohna_locks(dd_lock_mask, s_frame_tick);
		} else if (vm_is_evenicle_family() && ev_lock_mask != 0) {
			vm_enforce_evenicle_locks(ev_lock_mask);
		} else if (game_sengokurance && sr_lock_mask != 0) {
			vm_enforce_sr_locks(sr_lock_mask);
		} else if (game_rance01 && r01_lock_mask != 0) {
			vm_enforce_r01_locks(r01_lock_mask);
		} else if (game_rance02 && r02_lock_mask != 0) {
			refresh_r02_hud |= vm_enforce_r02_locks(r02_lock_mask);
		} else if (game_rance03 && r03_lock_mask != 0) {
			vm_enforce_r03_locks(r03_lock_mask);
		} else if (game_rance8 && r08_lock_mask != 0) {
			refresh_r08_cards |= vm_enforce_r08_locks(r08_lock_mask);
		} else if (game_rance9 && r09_lock_mask != 0) {
			refresh_r09_hud |= vm_enforce_r09_locks(r09_lock_mask);
		} else if (ex_is_active_extra_game()) {
			vm_ex_enforce_locks();
		}
		if (game_sengokurance && (action_applied || (sr_lock_mask & (LOCK_SR_INF_GOLD | LOCK_SR_MAX_SAT))))
			sr_refresh_hud(action_applied);
		if (refresh_r02_hud) {
			s_r02_applying = true;
			r02_refresh_hud();
			s_r02_applying = false;
		}
		if (refresh_r08_cards) {
			s_r08_applying = true;
			r08_refresh_battle_cards();
			s_r08_applying = false;
		}
		if (refresh_r09_hud) {
			s_r09_applying = true;
			if (refresh_r09_hud & R09_REFRESH_CARDS)
				r09_refresh_party_cards();
			if (refresh_r09_hud & R09_REFRESH_SYSTEM_BAR)
				r09_refresh_system_bar();
			s_r09_applying = false;
		}

		pthread_mutex_lock(&g_mod_mutex);
		int lock_mask = g_live_lock_mask;
		uint32_t req_seq = g_live_state_req_seq;
		bool need_snap = (req_seq != g_live_state_built_seq) || ui_changed;
		pthread_mutex_unlock(&g_mod_mutex);

#ifndef __ANDROID__
		if (s_state_file && s_state_file[0] && (ui_changed || (s_frame_tick % 15 == 1))) {
			need_snap = true;
		}
#endif
		if (need_snap) {
			char *snap = vm_build_live_state_json(lock_mask);
#ifndef __ANDROID__
			if (s_state_file && s_state_file[0] && snap) {
				char *tmp_path = NULL;
				FILE *sfp = atomic_save_open(s_state_file, "wb", &tmp_path);
				if (sfp) {
					size_t slen = strlen(snap);
					if (fwrite(snap, 1, slen, sfp) == slen)
						atomic_save_commit(sfp, s_state_file, tmp_path, false);
					else
						atomic_save_abort(sfp, tmp_path);
				}
			}
#endif
			pthread_mutex_lock(&g_mod_mutex);
			free(g_cached_live_state_json);
			g_cached_live_state_json = snap;
			g_live_state_built_seq = req_seq;
			pthread_cond_broadcast(&g_mod_cond);
			pthread_mutex_unlock(&g_mod_mutex);
		}

		s_in_modifier_frame = false;
		return;
	}

	vm_inspect_mod_streamer_ain();
	if (!s_game_config_ready) {
		if (vm_callstack_any_flag(FNO_FLAG_GAMEPLAY_SCENE) || s_frame_tick >= 60) {
			modifier_mark_config_ready();
		}
	}

	vm_restore_temp_rcs_overrides(false);
	vm_ensure_card_org_fallback_proc();

	pthread_mutex_lock(&g_mod_mutex);
	char *pending = g_pending_action_json;
	g_pending_action_json = NULL;
	int pending_lock_sm = -1;
	if (s_game_config_ready) {
		pending_lock_sm = g_pending_lock_streamer_mode;
		g_pending_lock_streamer_mode = -1;
	}
	pthread_mutex_unlock(&g_mod_mutex);

	bool did_streamer_sync = false;

	if (s_pending_clear_card_cache) {
		vm_clear_card_cache();
	}
	if (pending_lock_sm == 0 || pending_lock_sm == 1) {
		int cur_mask;
		pthread_mutex_lock(&g_mod_mutex);
		cur_mask = g_live_lock_mask;
		pthread_mutex_unlock(&g_mod_mutex);
		vm_set_streamer_mode(pending_lock_sm, true, true);
		persist_modifier_config_from_native(cur_mask);
		did_streamer_sync = true;
	}
	if (pending) {
		vm_execute_pending_action(pending);
		free(pending);
		did_streamer_sync = true;
	}

	int desired_font_pct = 100;
	int desired_sb_pct = 100;
	pthread_mutex_lock(&g_mod_mutex);
	desired_font_pct = g_font_scale_pct;
	desired_sb_pct = g_scrollbar_scale_pct;
	pthread_mutex_unlock(&g_mod_mutex);

	if (s_applied_font_scale_pct != desired_font_pct) {
		s_applied_font_scale_pct = desired_font_pct;
		parts_set_modifier_font_scale(desired_font_pct);
		parts_refresh_live_font_scale();
		vm_clear_card_cache();
		s_pending_live_ui_refresh = true;
		did_streamer_sync = true;
	}
	if (s_applied_scrollbar_scale_pct != desired_sb_pct) {
		s_applied_scrollbar_scale_pct = desired_sb_pct;
		parts_set_modifier_scrollbar_scale(desired_sb_pct);
		parts_refresh_live_scrollbar_scale();
		did_streamer_sync = true;
	}
	if (s_pending_cheat_gbk) {
		vm_step_pending_cheat();
	} else if (s_pulse_key0_frames > 0) {
		key_set_injected(VK_0, true);
		if (--s_pulse_key0_frames == 0)
			key_set_injected(VK_0, false);
	}

	/*
	 * In Scenes::RunCharacterSetting, MOD cheat handlers call
	 * DialogScenes::RunInformationDialog(...) FIRST and write the unlock flag /
	 * achievement AFTER the user dismisses the popup. Once RunCharacterSetting
	 * leaves call_stack, enforce m_isShowAdvShortcut[5] == 1 + m_isSummaryMode == 0,
	 * persist GameConfig.asd & modifier_config.json, and rebuild live card views!
	 */
	bool in_rcs = vm_callstack_has_fno(s_fno_run_char_setting) ||
	              vm_callstack_has_fno(s_fno_scene_char_setting_run);
	if (s_wait_post_rcs_streamer_refresh && !s_pending_cheat_gbk && !in_rcs) {
		s_wait_post_rcs_streamer_refresh = false;
		int cur_mask;
		pthread_mutex_lock(&g_mod_mutex);
		g_live_lock_mask |= LOCK_DISABLE_STREAMER_MODE;
		cur_mask = g_live_lock_mask;
		pthread_mutex_unlock(&g_mod_mutex);
		vm_set_streamer_mode(0, true, true);
		persist_modifier_config_from_native(cur_mask);
		did_streamer_sync = true;
	}

	/*
	 * Observe in-game MENU ("SummaryCheckBox") changes to Streamer Mode after
	 * GameConfig.asd has been loaded, while protecting user-disabled Streamer
	 * Mode against non-MENU resets.
	 */
	if (s_game_config_ready &&
	    vm_has_mod_streamer_mode() &&
	    !s_pending_cheat_gbk &&
	    !s_wait_post_rcs_streamer_refresh &&
	    !in_rcs) {
		int cur_sm = vm_get_live_streamer_mode(false);
		if (cur_sm == 0 || cur_sm == 1) {
			int cur_mask;
			pthread_mutex_lock(&g_mod_mutex);
			cur_mask = g_live_lock_mask;
			pthread_mutex_unlock(&g_mod_mutex);

			if (cur_sm == 0) {
				if (s_last_observed_summary_mode != 0 ||
				    !(cur_mask & LOCK_DISABLE_STREAMER_MODE)) {
					int new_mask;
					pthread_mutex_lock(&g_mod_mutex);
					g_live_lock_mask |= LOCK_DISABLE_STREAMER_MODE;
					new_mask = g_live_lock_mask;
					pthread_mutex_unlock(&g_mod_mutex);
					vm_set_streamer_mode(0, true, true);
					persist_modifier_config_from_native(new_mask);
					did_streamer_sync = true;
				}
			} else {
				/* cur_sm == 1 */
				if ((cur_mask & LOCK_DISABLE_STREAMER_MODE) &&
				    !vm_is_common_menu_on_callstack()) {
					vm_set_streamer_mode(0, true, true);
					did_streamer_sync = true;
				} else if (s_last_observed_summary_mode != 1) {
					if (cur_mask & LOCK_DISABLE_STREAMER_MODE) {
						/* User explicitly re-checked SummaryCheckBox in CommonMenu */
						int new_mask;
						pthread_mutex_lock(&g_mod_mutex);
						g_live_lock_mask &= ~LOCK_DISABLE_STREAMER_MODE;
						new_mask = g_live_lock_mask;
						pthread_mutex_unlock(&g_mod_mutex);
						vm_set_streamer_mode(1, true, true);
						persist_modifier_config_from_native(new_mask);
						did_streamer_sync = true;
					} else {
						s_last_observed_summary_mode = 1;
					}
				}
			}
		}
	}

	if (s_pending_live_ui_refresh) {
		vm_refresh_live_streamer_ui();
	}

	pthread_mutex_lock(&g_mod_mutex);
	int lock_mask = g_live_lock_mask;
	uint32_t req_seq = g_live_state_req_seq;
	bool need_snap = (req_seq != g_live_state_built_seq) || did_streamer_sync;
	pthread_mutex_unlock(&g_mod_mutex);

	if (lock_mask & ~LOCK_DISABLE_STREAMER_MODE) {
		vm_enforce_locks(lock_mask);
	}

#ifndef __ANDROID__
	if (s_state_file && s_state_file[0] && (did_streamer_sync || (s_frame_tick % 15 == 1))) {
		need_snap = true;
	}
#endif

	/* Build live state JSON lazily on demand (when UI requests it or state synced) */
	if (need_snap) {
		char *snap = vm_build_live_state_json(lock_mask);
#ifndef __ANDROID__
		if (s_state_file && s_state_file[0] && snap) {
			char *tmp_path = NULL;
			FILE *sfp = atomic_save_open(s_state_file, "wb", &tmp_path);
			if (sfp) {
				size_t slen = strlen(snap);
				if (fwrite(snap, 1, slen, sfp) == slen)
					atomic_save_commit(sfp, s_state_file, tmp_path, false);
				else
					atomic_save_abort(sfp, tmp_path);
			}
		}
#endif
		pthread_mutex_lock(&g_mod_mutex);
		free(g_cached_live_state_json);
		g_cached_live_state_json = snap;
		g_live_state_built_seq = req_seq;
		pthread_cond_broadcast(&g_mod_cond);
		pthread_mutex_unlock(&g_mod_mutex);
	}

	s_in_modifier_frame = false;
}

/* Native runtime UI entry points. */
#if defined(__ANDROID__) || defined(ALICEHUB_IOS) || defined(__SWITCH__)

static void set_game_lock_mask(bool *init_flag, int *target, int mask)
{
	int save_r10_mask;
	pthread_mutex_lock(&g_mod_mutex);
	*init_flag = true;
	*target = mask;
	save_r10_mask = g_live_lock_mask;
	pthread_mutex_unlock(&g_mod_mutex);
	persist_modifier_config_from_native(save_r10_mask);
}

static int get_game_lock_mask(const int *target)
{
	pthread_mutex_lock(&g_mod_mutex);
	int mask = *target;
	pthread_mutex_unlock(&g_mod_mutex);
	return mask;
}
#endif
#endif

#if defined(__ANDROID__) || defined(ALICEHUB_IOS) || defined(MODIFIER_OFFLINE_ONLY)
char *modifier_api_read_local_save_summary(const char *path)
{
	if (!path)
		return NULL;
	bool previous_gbk = sys4_text_is_gbk;
	char *summary = read_local_save_summary_str(path);
	sys4_set_text_gbk(previous_gbk);
	return summary;
}

bool modifier_api_write_local_save_mod(const char *path, const char *mod_json)
{
	if (!path || !mod_json)
		return false;
	bool previous_gbk = sys4_text_is_gbk;
	bool written = write_local_save_mod_str(path, mod_json);
	sys4_set_text_gbk(previous_gbk);
	return written;
}

char *modifier_api_read_global_save_summary(const char *global_path, const char *collection_path)
{
	bool previous_gbk = sys4_text_is_gbk;
	char *summary = read_global_save_summary_str(global_path ? global_path : "", collection_path ? collection_path : "");
	sys4_set_text_gbk(previous_gbk);
	return summary;
}

bool modifier_api_write_global_save_mod(const char *global_path, const char *collection_path, const char *mod_json)
{
	if (!mod_json)
		return false;
	bool previous_gbk = sys4_text_is_gbk;
	bool written = write_global_save_mod_str(global_path ? global_path : "", collection_path ? collection_path : "", mod_json);
	sys4_set_text_gbk(previous_gbk);
	return written;
}
#endif

#if defined(__ANDROID__) || defined(ALICEHUB_IOS) || (defined(__SWITCH__) && !defined(MODIFIER_OFFLINE_ONLY))
const char *modifier_api_current_game(void)
{
	if (!vm_is_ready())
		return "";
	if (game_rance10) return "rance10";
	if (game_dohnadohna) return "dohnadohna";
	if (game_evenicle_rance) return "eveniclerance";
	if (game_evenicle_gaiden) return "eveniclegaiden";
	if (game_evenicle) return "evenicle";
	if (game_sengokurance) return "sengokurance";
	if (game_rance01) return "rance01";
	if (game_rance02) return "rance02";
	if (game_rance03) return "rance03";
	if (game_rance8) return "rance08";
	if (game_rance9) return "rance09";
	return ex_current_game_id();
}


char *modifier_api_get_live_state_json(void)
{
	pthread_mutex_lock(&g_mod_mutex);
	if (g_vm_ready_cached) {
		uint32_t target_seq = ++g_live_state_req_seq;
		struct timespec ts;
		if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
			ts.tv_nsec += 25000000L; /* 25ms max wait (~1.5 frames at 60fps) */
			if (ts.tv_nsec >= 1000000000L) {
				ts.tv_sec += 1;
				ts.tv_nsec -= 1000000000L;
			}
			while (g_live_state_built_seq != target_seq) {
				if (pthread_cond_timedwait(&g_mod_cond, &g_mod_mutex, &ts) == ETIMEDOUT)
					break;
			}
		}
	}
	char *ret = strdup(g_cached_live_state_json ? g_cached_live_state_json : "{\"inGame\":false}");
	pthread_mutex_unlock(&g_mod_mutex);
	return ret;
}

void modifier_api_set_config_path(const char *path)
{
	if (!path || !path[0])
		return;
	pthread_mutex_lock(&g_mod_mutex);
	free(g_modifier_config_path);
	g_modifier_config_path = strdup(path);
	pthread_mutex_unlock(&g_mod_mutex);
}

void modifier_api_apply_live_action(const char *action_json)
{
	if (!action_json)
		return;
	pthread_mutex_lock(&g_mod_mutex);
	free(g_pending_action_json);
	g_pending_action_json = strdup(action_json);
	pthread_mutex_unlock(&g_mod_mutex);
}

static void set_rance10_lock_mask(int mask)
{
	pthread_mutex_lock(&g_mod_mutex);
	int prev = g_live_lock_mask;
	bool first_init = !g_has_lock_mask_init;
	g_has_lock_mask_init = true;
	g_live_lock_mask = mask;
	bool prev_dis = (prev & LOCK_DISABLE_STREAMER_MODE) != 0;
	bool next_dis = (mask & LOCK_DISABLE_STREAMER_MODE) != 0;
	if (first_init) {
		if (next_dis)
			g_pending_lock_streamer_mode = 0;
	} else if (prev_dis != next_dis) {
		g_pending_lock_streamer_mode = next_dis ? 0 : 1;
	}
	pthread_mutex_unlock(&g_mod_mutex);
}

void modifier_api_set_lock_mask(enum modifier_lock_game game, int mask)
{
	switch (game) {
	case MODIFIER_LOCK_RANCE10:
		set_rance10_lock_mask(mask);
		persist_modifier_config_from_native(mask);
		break;
	case MODIFIER_LOCK_DOHNADOHNA: set_game_lock_mask(&g_has_dohna_lock_mask_init, &g_dohna_lock_mask, mask); break;
	case MODIFIER_LOCK_EVENICLE: set_game_lock_mask(&g_has_evenicle_lock_mask_init, &g_evenicle_lock_mask, mask); break;
	case MODIFIER_LOCK_SENGOKURANCE: set_game_lock_mask(&g_has_sr_lock_mask_init, &g_sr_lock_mask, mask); break;
	case MODIFIER_LOCK_RANCE01: set_game_lock_mask(&g_has_r01_lock_mask_init, &g_r01_lock_mask, mask); break;
	case MODIFIER_LOCK_RANCE02: set_game_lock_mask(&g_has_r02_lock_mask_init, &g_r02_lock_mask, mask); break;
	case MODIFIER_LOCK_RANCE03: set_game_lock_mask(&g_has_r03_lock_mask_init, &g_r03_lock_mask, mask); break;
	case MODIFIER_LOCK_RANCE08: set_game_lock_mask(&g_has_r08_lock_mask_init, &g_r08_lock_mask, mask); break;
	case MODIFIER_LOCK_RANCE09: set_game_lock_mask(&g_has_r09_lock_mask_init, &g_r09_lock_mask, mask); break;
	}
}

int modifier_api_get_lock_mask(enum modifier_lock_game game)
{
	switch (game) {
	case MODIFIER_LOCK_RANCE10: return get_game_lock_mask(&g_live_lock_mask);
	case MODIFIER_LOCK_DOHNADOHNA: return get_game_lock_mask(&g_dohna_lock_mask);
	case MODIFIER_LOCK_EVENICLE: return get_game_lock_mask(&g_evenicle_lock_mask);
	case MODIFIER_LOCK_SENGOKURANCE: return get_game_lock_mask(&g_sr_lock_mask);
	case MODIFIER_LOCK_RANCE01: return get_game_lock_mask(&g_r01_lock_mask);
	case MODIFIER_LOCK_RANCE02: return get_game_lock_mask(&g_r02_lock_mask);
	case MODIFIER_LOCK_RANCE03: return get_game_lock_mask(&g_r03_lock_mask);
	case MODIFIER_LOCK_RANCE08: return get_game_lock_mask(&g_r08_lock_mask);
	case MODIFIER_LOCK_RANCE09: return get_game_lock_mask(&g_r09_lock_mask);
	}
	return 0;
}

void modifier_api_set_game_lock_mask(const char *game_id, int mask)
{
	if (!game_id || !game_id[0])
		return;
	if (!strcmp(game_id, "rance10")) { modifier_api_set_lock_mask(MODIFIER_LOCK_RANCE10, mask); return; }
	if (!strcmp(game_id, "dohnadohna")) { modifier_api_set_lock_mask(MODIFIER_LOCK_DOHNADOHNA, mask); return; }
	if (!strcmp(game_id, "evenicle") || !strcmp(game_id, "eveniclerance") || !strcmp(game_id, "eveniclegaiden")) {
		modifier_api_set_lock_mask(MODIFIER_LOCK_EVENICLE, mask);
		return;
	}
	if (!strcmp(game_id, "sengokurance")) { modifier_api_set_lock_mask(MODIFIER_LOCK_SENGOKURANCE, mask); return; }
	if (!strcmp(game_id, "rance01")) { modifier_api_set_lock_mask(MODIFIER_LOCK_RANCE01, mask); return; }
	if (!strcmp(game_id, "rance02")) { modifier_api_set_lock_mask(MODIFIER_LOCK_RANCE02, mask); return; }
	if (!strcmp(game_id, "rance03")) { modifier_api_set_lock_mask(MODIFIER_LOCK_RANCE03, mask); return; }
	if (!strcmp(game_id, "rance08")) { modifier_api_set_lock_mask(MODIFIER_LOCK_RANCE08, mask); return; }
	if (!strcmp(game_id, "rance09")) { modifier_api_set_lock_mask(MODIFIER_LOCK_RANCE09, mask); return; }
	if (!strcmp(game_id, "escalayer_reboot")) game_id = "escalayerreboot";

	int save_r10_mask = 0;
	bool updated = false;
	pthread_mutex_lock(&g_mod_mutex);
	struct ex_game_lock_slot *slot = ex_find_lock_slot(game_id);
	if (slot) {
		slot->mask = mask;
		slot->init = true;
		updated = true;
	}
	save_r10_mask = g_live_lock_mask;
	pthread_mutex_unlock(&g_mod_mutex);
	if (updated)
		persist_modifier_config_from_native(save_r10_mask);
}

int modifier_api_get_game_lock_mask(const char *game_id)
{
	if (!game_id || !game_id[0])
		return 0;
	if (!strcmp(game_id, "rance10")) return modifier_api_get_lock_mask(MODIFIER_LOCK_RANCE10);
	if (!strcmp(game_id, "dohnadohna")) return modifier_api_get_lock_mask(MODIFIER_LOCK_DOHNADOHNA);
	if (!strcmp(game_id, "evenicle") || !strcmp(game_id, "eveniclerance") || !strcmp(game_id, "eveniclegaiden"))
		return modifier_api_get_lock_mask(MODIFIER_LOCK_EVENICLE);
	if (!strcmp(game_id, "sengokurance")) return modifier_api_get_lock_mask(MODIFIER_LOCK_SENGOKURANCE);
	if (!strcmp(game_id, "rance01")) return modifier_api_get_lock_mask(MODIFIER_LOCK_RANCE01);
	if (!strcmp(game_id, "rance02")) return modifier_api_get_lock_mask(MODIFIER_LOCK_RANCE02);
	if (!strcmp(game_id, "rance03")) return modifier_api_get_lock_mask(MODIFIER_LOCK_RANCE03);
	if (!strcmp(game_id, "rance08")) return modifier_api_get_lock_mask(MODIFIER_LOCK_RANCE08);
	if (!strcmp(game_id, "rance09")) return modifier_api_get_lock_mask(MODIFIER_LOCK_RANCE09);
	if (!strcmp(game_id, "escalayer_reboot")) game_id = "escalayerreboot";

	pthread_mutex_lock(&g_mod_mutex);
	int mask = ex_get_lock_mask_locked(game_id);
	pthread_mutex_unlock(&g_mod_mutex);
	return mask;
}

#if defined(__ANDROID__) || defined(ALICEHUB_IOS)
int modifier_api_read_streamer_mode(const char *config_path)
{
	return config_path ? read_game_config_streamer_mode(config_path) : -1;
}

bool modifier_api_write_streamer_mode(const char *config_path, bool enabled)
{
	return config_path ? write_game_config_streamer_mode(config_path, enabled) : false;
}
#endif

void modifier_api_set_ctrl_skip(bool enabled)
{
	input_set_touch_ctrl_skip(enabled);
}

bool modifier_api_get_ctrl_skip(void)
{
	return input_get_touch_ctrl_skip();
}

void modifier_api_set_ui_scale_game_key(const char *game_key)
{
	pthread_mutex_lock(&g_mod_mutex);
	free(g_ui_scale_game_key);
	g_ui_scale_game_key = (game_key && game_key[0]) ? strdup(game_key) : NULL;
	pthread_mutex_unlock(&g_mod_mutex);
}

void modifier_api_set_ui_scale(int font_pct, int scrollbar_pct)
{
	int fp = clamp_font_scale_pct(font_pct);
	int sp = clamp_scrollbar_scale_pct(scrollbar_pct);
	pthread_mutex_lock(&g_mod_mutex);
	g_has_ui_scale_init = true;
	g_font_scale_pct = fp;
	g_scrollbar_scale_pct = sp;
	pthread_mutex_unlock(&g_mod_mutex);
	/* Actual parts_set_modifier_* + live refresh run on SDLThread in modifier_on_frame. */
}

int modifier_api_get_font_scale(void)
{
	pthread_mutex_lock(&g_mod_mutex);
	int fp = g_font_scale_pct;
	pthread_mutex_unlock(&g_mod_mutex);
	return fp;
}

int modifier_api_get_scrollbar_scale(void)
{
	pthread_mutex_lock(&g_mod_mutex);
	int sp = g_scrollbar_scale_pct;
	pthread_mutex_unlock(&g_mod_mutex);
	return sp;
}

bool modifier_api_export_pc_save_file(const char *src_path, const char *dst_path)
{
	return (src_path && dst_path) ? savedata_export_pc_file(src_path, dst_path) : false;
}

#endif /* __ANDROID__ || ALICEHUB_IOS || __SWITCH__ */

#if defined(__ANDROID__)

static jstring jni_new_string_utf8_safe(JNIEnv *env, const char *utf8)
{
	if (!env)
		return NULL;
	if (!utf8 || !utf8[0]) {
		jchar empty = 0;
		return (*env)->NewString(env, &empty, 0);
	}
	size_t len = strlen(utf8);
	jchar stack_buf[256];
	jchar *u16 = (len <= 256) ? stack_buf : (jchar *)malloc(len * sizeof(jchar));
	if (!u16) {
		jchar empty = 0;
		return (*env)->NewString(env, &empty, 0);
	}
	size_t out = 0;
	const unsigned char *s = (const unsigned char *)utf8;
	size_t i = 0;
	while (i < len) {
		unsigned char b0 = s[i];
		if (b0 < 0x80u) {
			u16[out++] = (jchar)b0;
			i++;
		} else if (b0 >= 0xC2u && b0 <= 0xDFu) {
			if (i + 1 < len && (s[i + 1] & 0xC0u) == 0x80u) {
				uint32_t cp = ((uint32_t)(b0 & 0x1Fu) << 6) | (uint32_t)(s[i + 1] & 0x3Fu);
				u16[out++] = (jchar)cp;
				i += 2;
			} else {
				u16[out++] = (jchar)0xFFFDu;
				i++;
			}
		} else if (b0 >= 0xE0u && b0 <= 0xEFu) {
			bool b1_ok = (i + 1 < len) && (
				(b0 == 0xE0u) ? (s[i + 1] >= 0xA0u && s[i + 1] <= 0xBFu) :
				(b0 == 0xEDu) ? (s[i + 1] >= 0x80u && s[i + 1] <= 0x9Fu) :
				((s[i + 1] & 0xC0u) == 0x80u));
			if (b1_ok && i + 2 < len && (s[i + 2] & 0xC0u) == 0x80u) {
				uint32_t cp = ((uint32_t)(b0 & 0x0Fu) << 12) |
				              ((uint32_t)(s[i + 1] & 0x3Fu) << 6) |
				              (uint32_t)(s[i + 2] & 0x3Fu);
				u16[out++] = (jchar)cp;
				i += 3;
			} else {
				u16[out++] = (jchar)0xFFFDu;
				i += b1_ok ? 2 : 1;
			}
		} else if (b0 >= 0xF0u && b0 <= 0xF4u) {
			bool b1_ok = (i + 1 < len) && (
				(b0 == 0xF0u) ? (s[i + 1] >= 0x90u && s[i + 1] <= 0xBFu) :
				(b0 == 0xF4u) ? (s[i + 1] >= 0x80u && s[i + 1] <= 0x8Fu) :
				((s[i + 1] & 0xC0u) == 0x80u));
			bool b2_ok = b1_ok && (i + 2 < len) && ((s[i + 2] & 0xC0u) == 0x80u);
			if (b2_ok && i + 3 < len && (s[i + 3] & 0xC0u) == 0x80u) {
				uint32_t cp = ((uint32_t)(b0 & 0x07u) << 18) |
				              ((uint32_t)(s[i + 1] & 0x3Fu) << 12) |
				              ((uint32_t)(s[i + 2] & 0x3Fu) << 6) |
				              (uint32_t)(s[i + 3] & 0x3Fu);
				uint32_t v = cp - 0x10000u;
				u16[out++] = (jchar)(0xD800u | (v >> 10));
				u16[out++] = (jchar)(0xDC00u | (v & 0x3FFu));
				i += 4;
			} else {
				u16[out++] = (jchar)0xFFFDu;
				i += b2_ok ? 3 : (b1_ok ? 2 : 1);
			}
		} else {
			u16[out++] = (jchar)0xFFFDu;
			i++;
		}
	}
	jstring ret = (*env)->NewString(env, u16, (jsize)out);
	if (u16 != stack_buf)
		free(u16);
	return ret;
}

JNIEXPORT jstring JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeReadLocalSaveSummary(
	JNIEnv *env, jclass clazz, jstring jpath)
{
	(void)clazz;
	if (!jpath)
		return jni_new_string_utf8_safe(env, "{}");
	const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
	if (!path)
		return NULL;
	char *json = modifier_api_read_local_save_summary(path);
	(*env)->ReleaseStringUTFChars(env, jpath, path);
	jstring ret = jni_new_string_utf8_safe(env, json ? json : "{}");
	free(json);
	return ret;
}

JNIEXPORT jboolean JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeWriteLocalSaveMod(
	JNIEnv *env, jclass clazz, jstring jpath, jstring jmod)
{
	(void)clazz;
	if (!jpath || !jmod)
		return JNI_FALSE;
	const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
	if (!path)
		return JNI_FALSE;
	const char *mod = (*env)->GetStringUTFChars(env, jmod, NULL);
	if (!mod) {
		(*env)->ReleaseStringUTFChars(env, jpath, path);
		return JNI_FALSE;
	}
	bool ok = modifier_api_write_local_save_mod(path, mod);
	(*env)->ReleaseStringUTFChars(env, jpath, path);
	(*env)->ReleaseStringUTFChars(env, jmod, mod);
	return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeReadGlobalSaveSummary(
	JNIEnv *env, jclass clazz, jstring jglobal_path, jstring jcollection_path)
{
	(void)clazz;
	const char *gp = NULL;
	if (jglobal_path) {
		gp = (*env)->GetStringUTFChars(env, jglobal_path, NULL);
		if (!gp)
			return NULL;
	}
	const char *cp = NULL;
	if (jcollection_path) {
		cp = (*env)->GetStringUTFChars(env, jcollection_path, NULL);
		if (!cp) {
			if (gp) (*env)->ReleaseStringUTFChars(env, jglobal_path, gp);
			return NULL;
		}
	}
	char *json = modifier_api_read_global_save_summary(gp, cp);
	if (gp) (*env)->ReleaseStringUTFChars(env, jglobal_path, gp);
	if (cp) (*env)->ReleaseStringUTFChars(env, jcollection_path, cp);
	jstring ret = jni_new_string_utf8_safe(env, json ? json : "{}");
	free(json);
	return ret;
}

JNIEXPORT jboolean JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeWriteGlobalSaveMod(
	JNIEnv *env, jclass clazz, jstring jglobal_path, jstring jcollection_path, jstring jmod)
{
	(void)clazz;
	if (!jmod)
		return JNI_FALSE;
	const char *gp = NULL;
	if (jglobal_path) {
		gp = (*env)->GetStringUTFChars(env, jglobal_path, NULL);
		if (!gp)
			return JNI_FALSE;
	}
	const char *cp = NULL;
	if (jcollection_path) {
		cp = (*env)->GetStringUTFChars(env, jcollection_path, NULL);
		if (!cp) {
			if (gp) (*env)->ReleaseStringUTFChars(env, jglobal_path, gp);
			return JNI_FALSE;
		}
	}
	const char *mod = (*env)->GetStringUTFChars(env, jmod, NULL);
	if (!mod) {
		if (gp) (*env)->ReleaseStringUTFChars(env, jglobal_path, gp);
		if (cp) (*env)->ReleaseStringUTFChars(env, jcollection_path, cp);
		return JNI_FALSE;
	}
	bool ok = modifier_api_write_global_save_mod(gp, cp, mod);
	if (gp) (*env)->ReleaseStringUTFChars(env, jglobal_path, gp);
	if (cp) (*env)->ReleaseStringUTFChars(env, jcollection_path, cp);
	(*env)->ReleaseStringUTFChars(env, jmod, mod);
	return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeGetLiveStateJson(
	JNIEnv *env, jclass clazz)
{
	(void)clazz;
	char *json = modifier_api_get_live_state_json();
	jstring ret = jni_new_string_utf8_safe(env, json ? json : "{\"inGame\":false}");
	free(json);
	return ret;
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeSetConfigPath(
	JNIEnv *env, jclass clazz, jstring jpath)
{
	(void)clazz;
	if (!jpath)
		return;
	const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
	if (!path)
		return;
	modifier_api_set_config_path(path);
	(*env)->ReleaseStringUTFChars(env, jpath, path);
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeSetLiveLocks(
	JNIEnv *env, jclass clazz, jint mask)
{
	(void)env;
	(void)clazz;
	set_rance10_lock_mask((int)mask);
}

JNIEXPORT jint JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeGetLiveLocks(
	JNIEnv *env, jclass clazz)
{
	(void)env;
	(void)clazz;
	pthread_mutex_lock(&g_mod_mutex);
	int mask = g_live_lock_mask;
	pthread_mutex_unlock(&g_mod_mutex);
	return (jint)mask;
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeSetDohnaLiveLocks(
	JNIEnv *env, jclass clazz, jint mask)
{
	(void)env;
	(void)clazz;
	set_game_lock_mask(&g_has_dohna_lock_mask_init, &g_dohna_lock_mask, (int)mask);
}

JNIEXPORT jint JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeGetDohnaLiveLocks(
	JNIEnv *env, jclass clazz)
{
	(void)env;
	(void)clazz;
	return (jint)get_game_lock_mask(&g_dohna_lock_mask);
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeSetEvenicleLiveLocks(
	JNIEnv *env, jclass clazz, jint mask)
{
	(void)env;
	(void)clazz;
	set_game_lock_mask(&g_has_evenicle_lock_mask_init, &g_evenicle_lock_mask, (int)mask);
}

JNIEXPORT jint JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeGetEvenicleLiveLocks(
	JNIEnv *env, jclass clazz)
{
	(void)env;
	(void)clazz;
	return (jint)get_game_lock_mask(&g_evenicle_lock_mask);
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeSetSrLiveLocks(
	JNIEnv *env, jclass clazz, jint mask)
{
	(void)env;
	(void)clazz;
	set_game_lock_mask(&g_has_sr_lock_mask_init, &g_sr_lock_mask, (int)mask);
}

JNIEXPORT jint JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeGetSrLiveLocks(
	JNIEnv *env, jclass clazz)
{
	(void)env;
	(void)clazz;
	return (jint)get_game_lock_mask(&g_sr_lock_mask);
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeSetR03LiveLocks(
	JNIEnv *env, jclass clazz, jint mask)
{
	(void)env;
	(void)clazz;
	set_game_lock_mask(&g_has_r03_lock_mask_init, &g_r03_lock_mask, (int)mask);
}

JNIEXPORT jint JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeGetR03LiveLocks(
	JNIEnv *env, jclass clazz)
{
	(void)env;
	(void)clazz;
	return (jint)get_game_lock_mask(&g_r03_lock_mask);
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeSetR08LiveLocks(
	JNIEnv *env, jclass clazz, jint mask)
{
	(void)env;
	(void)clazz;
	set_game_lock_mask(&g_has_r08_lock_mask_init, &g_r08_lock_mask, (int)mask);
}

JNIEXPORT jint JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeGetR08LiveLocks(
	JNIEnv *env, jclass clazz)
{
	(void)env;
	(void)clazz;
	return (jint)get_game_lock_mask(&g_r08_lock_mask);
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeSetR09LiveLocks(
	JNIEnv *env, jclass clazz, jint mask)
{
	(void)env;
	(void)clazz;
	set_game_lock_mask(&g_has_r09_lock_mask_init, &g_r09_lock_mask, (int)mask);
}

JNIEXPORT jint JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeGetR09LiveLocks(
	JNIEnv *env, jclass clazz)
{
	(void)env;
	(void)clazz;
	return (jint)get_game_lock_mask(&g_r09_lock_mask);
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeSetR01LiveLocks(
	JNIEnv *env, jclass clazz, jint mask)
{
	(void)env;
	(void)clazz;
	set_game_lock_mask(&g_has_r01_lock_mask_init, &g_r01_lock_mask, (int)mask);
}

JNIEXPORT jint JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeGetR01LiveLocks(
	JNIEnv *env, jclass clazz)
{
	(void)env;
	(void)clazz;
	return (jint)get_game_lock_mask(&g_r01_lock_mask);
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeSetR02LiveLocks(
	JNIEnv *env, jclass clazz, jint mask)
{
	(void)env;
	(void)clazz;
	set_game_lock_mask(&g_has_r02_lock_mask_init, &g_r02_lock_mask, (int)mask);
}

JNIEXPORT jint JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeGetR02LiveLocks(
	JNIEnv *env, jclass clazz)
{
	(void)env;
	(void)clazz;
	return (jint)get_game_lock_mask(&g_r02_lock_mask);
}

JNIEXPORT jstring JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeGetCurrentGame(
	JNIEnv *env, jclass clazz)
{
	(void)clazz;
	const char *gid = modifier_api_current_game();
	return jni_new_string_utf8_safe(env, gid ? gid : "");
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeSetGameLiveLocks(
	JNIEnv *env, jclass clazz, jstring jgameId, jint mask)
{
	(void)clazz;
	if (!jgameId)
		return;
	const char *gid = (*env)->GetStringUTFChars(env, jgameId, NULL);
	if (gid) {
		modifier_api_set_game_lock_mask(gid, (int)mask);
		(*env)->ReleaseStringUTFChars(env, jgameId, gid);
	}
}

JNIEXPORT jint JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeGetGameLiveLocks(
	JNIEnv *env, jclass clazz, jstring jgameId)
{
	(void)clazz;
	if (!jgameId)
		return 0;
	const char *gid = (*env)->GetStringUTFChars(env, jgameId, NULL);
	int mask = gid ? modifier_api_get_game_lock_mask(gid) : 0;
	if (gid)
		(*env)->ReleaseStringUTFChars(env, jgameId, gid);
	return (jint)mask;
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeApplyLiveAction(
	JNIEnv *env, jclass clazz, jstring jaction)
{
	(void)clazz;
	if (!jaction)
		return;
	const char *act = (*env)->GetStringUTFChars(env, jaction, NULL);
	if (act) {
		modifier_api_apply_live_action(act);
		(*env)->ReleaseStringUTFChars(env, jaction, act);
	}
}

JNIEXPORT jint JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeReadStreamerMode(
	JNIEnv *env, jclass clazz, jstring jpath)
{
	(void)clazz;
	if (!jpath) return -1;
	const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
	int res = path ? read_game_config_streamer_mode(path) : -1;
	if (path) (*env)->ReleaseStringUTFChars(env, jpath, path);
	return (jint)res;
}

JNIEXPORT jboolean JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeWriteStreamerMode(
	JNIEnv *env, jclass clazz, jstring jpath, jboolean jenabled)
{
	(void)clazz;
	if (!jpath) return JNI_FALSE;
	const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
	bool ok = path ? write_game_config_streamer_mode(path, jenabled == JNI_TRUE) : false;
	if (path) (*env)->ReleaseStringUTFChars(env, jpath, path);
	return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeSetCtrlSkip(
	JNIEnv *env, jclass clazz, jboolean jenabled)
{
	(void)env;
	(void)clazz;
	input_set_touch_ctrl_skip(jenabled == JNI_TRUE);
}

JNIEXPORT jboolean JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeGetCtrlSkip(
	JNIEnv *env, jclass clazz)
{
	(void)env;
	(void)clazz;
	return input_get_touch_ctrl_skip() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeSetUiScaleGameKey(
	JNIEnv *env, jclass clazz, jstring jgameKey)
{
	(void)clazz;
	if (!jgameKey) {
		modifier_api_set_ui_scale_game_key(NULL);
		return;
	}
	const char *key = (*env)->GetStringUTFChars(env, jgameKey, NULL);
	if (!key)
		return;
	modifier_api_set_ui_scale_game_key(key);
	(*env)->ReleaseStringUTFChars(env, jgameKey, key);
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeSetUiScale(
	JNIEnv *env, jclass clazz, jint font_pct, jint scrollbar_pct)
{
	(void)env;
	(void)clazz;
	modifier_api_set_ui_scale((int)font_pct, (int)scrollbar_pct);
}

JNIEXPORT jint JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeGetFontScale(
	JNIEnv *env, jclass clazz)
{
	(void)env;
	(void)clazz;
	pthread_mutex_lock(&g_mod_mutex);
	int fp = g_font_scale_pct;
	pthread_mutex_unlock(&g_mod_mutex);
	return (jint)fp;
}

JNIEXPORT jint JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeGetScrollbarScale(
	JNIEnv *env, jclass clazz)
{
	(void)env;
	(void)clazz;
	pthread_mutex_lock(&g_mod_mutex);
	int sp = g_scrollbar_scale_pct;
	pthread_mutex_unlock(&g_mod_mutex);
	return (jint)sp;
}

JNIEXPORT jboolean JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeExportPcSaveFile(
	JNIEnv *env, jclass clazz, jstring jsrcPath, jstring jdstPath)
{
	(void)clazz;
	if (!jsrcPath || !jdstPath) return JNI_FALSE;
	const char *src_path = (*env)->GetStringUTFChars(env, jsrcPath, NULL);
	if (!src_path)
		return JNI_FALSE;
	const char *dst_path = (*env)->GetStringUTFChars(env, jdstPath, NULL);
	if (!dst_path) {
		(*env)->ReleaseStringUTFChars(env, jsrcPath, src_path);
		return JNI_FALSE;
	}
	bool ok = savedata_export_pc_file(src_path, dst_path);
	(*env)->ReleaseStringUTFChars(env, jsrcPath, src_path);
	(*env)->ReleaseStringUTFChars(env, jdstPath, dst_path);
	return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeExportPcSaveFileWithAin(
	JNIEnv *env, jclass clazz, jstring jsrcPath, jstring jdstPath, jstring jainPath)
{
	(void)clazz;
	if (!jsrcPath || !jdstPath) return JNI_FALSE;
	const char *src_path = (*env)->GetStringUTFChars(env, jsrcPath, NULL);
	if (!src_path)
		return JNI_FALSE;
	const char *dst_path = (*env)->GetStringUTFChars(env, jdstPath, NULL);
	if (!dst_path) {
		(*env)->ReleaseStringUTFChars(env, jsrcPath, src_path);
		return JNI_FALSE;
	}
	const char *ain_path = NULL;
	if (jainPath) {
		ain_path = (*env)->GetStringUTFChars(env, jainPath, NULL);
		if (!ain_path) {
			(*env)->ReleaseStringUTFChars(env, jsrcPath, src_path);
			(*env)->ReleaseStringUTFChars(env, jdstPath, dst_path);
			return JNI_FALSE;
		}
	}
	const char *effective_ain = (ain_path && ain_path[0]) ? ain_path : NULL;
	bool ok = savedata_export_pc_file_with_ain(src_path, dst_path, effective_ain);
	(*env)->ReleaseStringUTFChars(env, jsrcPath, src_path);
	(*env)->ReleaseStringUTFChars(env, jdstPath, dst_path);
	if (ain_path) (*env)->ReleaseStringUTFChars(env, jainPath, ain_path);
	return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_io_github_kichikuou_xsystem4_GameModifierBridge_nativeSaveFileHasXSystem4OnlyState(
	JNIEnv *env, jclass clazz, jstring jsrcPath)
{
	(void)clazz;
	if (!jsrcPath) return JNI_FALSE;
	const char *src_path = (*env)->GetStringUTFChars(env, jsrcPath, NULL);
	if (!src_path) return JNI_FALSE;
	bool has_only_state = src_path[0] ? savedata_file_has_xsystem4_only_state(src_path) : false;
	(*env)->ReleaseStringUTFChars(env, jsrcPath, src_path);
	return has_only_state ? JNI_TRUE : JNI_FALSE;
}

#endif /* __ANDROID__ */
