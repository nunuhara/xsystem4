#ifndef SYSTEM4_MODIFIER_API_H
#define SYSTEM4_MODIFIER_API_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Platform-neutral entry points for the in-game modifier UI (Android JNI, iOS Swift).
 * Strings are UTF-8; returned strings are malloc'd and must be released with free(). */

enum modifier_lock_game {
	MODIFIER_LOCK_RANCE10,
	MODIFIER_LOCK_DOHNADOHNA,
	MODIFIER_LOCK_EVENICLE,
	MODIFIER_LOCK_SENGOKURANCE,
	MODIFIER_LOCK_RANCE01,
	MODIFIER_LOCK_RANCE02,
	MODIFIER_LOCK_RANCE03,
	MODIFIER_LOCK_RANCE08,
	MODIFIER_LOCK_RANCE09,
};

char *modifier_api_read_local_save_summary(const char *path);
bool modifier_api_write_local_save_mod(const char *path, const char *mod_json);
char *modifier_api_read_global_save_summary(const char *global_path, const char *collection_path);
bool modifier_api_write_global_save_mod(const char *global_path, const char *collection_path, const char *mod_json);

/* Running game ("rance10", "dohnadohna", ...), or "" before a game is identified. */
const char *modifier_api_current_game(void);
char *modifier_api_get_live_state_json(void);
void modifier_api_set_config_path(const char *path);
void modifier_api_apply_live_action(const char *action_json);
void modifier_api_set_lock_mask(enum modifier_lock_game game, int mask);
int modifier_api_get_lock_mask(enum modifier_lock_game game);
void modifier_api_set_game_lock_mask(const char *game_id, int mask);
int modifier_api_get_game_lock_mask(const char *game_id);
bool modifier_api_has_streamer_mode(void);

int modifier_api_read_streamer_mode(const char *config_path);
bool modifier_api_write_streamer_mode(const char *config_path, bool enabled);
void modifier_api_set_ctrl_skip(bool enabled);
bool modifier_api_get_ctrl_skip(void);
void modifier_api_set_ui_scale_game_key(const char *game_key);
void modifier_api_set_ui_scale(int font_pct, int scrollbar_pct);
int modifier_api_get_font_scale(void);
int modifier_api_get_scrollbar_scale(void);
bool modifier_api_export_pc_save_file(const char *src_path, const char *dst_path);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM4_MODIFIER_API_H */
