/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#ifndef XSYSTEM4_H
#define XSYSTEM4_H

#include <stddef.h>
#include <stdbool.h>

enum resume_save_format {
	SAVE_FORMAT_JSON,
	SAVE_FORMAT_RSM,
};

struct config {
	char *game_name;
	char *boot_name;
	char *ain_filename;
	char *ain_basename_utf8;
	char *vm_name;
	char *game_dir;
	char *save_dir;
	char *home_dir;
	int view_width;
	int view_height;
	bool view_width_set;
	bool view_height_set;
	size_t mixer_nr_channels;
	char **mixer_channels;
	int *mixer_volumes;
	int default_volume;

	char *bgi_path;
	char *wai_path;
	char *ex_path;
	char *fnl_path;
	char *font_paths[2];

	bool joypad;
	bool echo;
	float text_x_scale;
	bool manual_text_x_scale;
	bool proportional_text;
	enum resume_save_format save_format;
	int msgskip_delay;
};

extern struct config config;

// Convenience functions to avoid cumbersome memory management when passing
// text to printf-like functions
const char *display_sjis0(const char *sjis);
const char *display_sjis1(const char *sjis);
const char *display_sjis2(const char *sjis);
const char *display_utf0(const char *utf);
const char *display_utf1(const char *utf);
const char *display_utf2(const char *utf);

void indent_message(int indent, const char *fmt, ...);

void log_message(const char *log, const char *fmt, ...);

#define UNIMPLEMENTED(fmt, ...) \
	sys_warning("unimplemented: %s" fmt "\n", __func__, ##__VA_ARGS__)

bool is_absolute_path(const char *path);
char *unix_path(const char *path);
char *gamedir_path(const char *path);
char *gamedir_path_icase(const char *path);
#define SAVEDIR_MEMORY_PREFIX ".alicehub-mem."

char *savedir_path(const char *path);
bool savedir_file_exists(const char *path);
void savedir_cache_invalidate(void);
void savedir_begin_memory_session(void);
void savedir_cleanup_memory_saves(void);
int savedir_memory_find(const char *pattern, int *count, char **match_out);

static inline bool savedir_is_memory_save_path(const char *path)
{
	if (!path)
		return false;
	const char *base = path;
	for (const char *p = path; *p; p++) {
		if (*p == '/' || *p == '\\')
			base = p + 1;
	}
	const char *prefix = SAVEDIR_MEMORY_PREFIX;
	for (size_t i = 0; prefix[i]; i++) {
		if (base[i] != prefix[i])
			return false;
	}
	return true;
}

void get_date(int *year, int *month, int *mday, int *wday);
void get_time(int *hour, int *min, int *sec, int *ms);

void screenshot_save(void);

#ifndef XSYS4_DATA_DIR
#define XSYS4_DATA_DIR "/usr/local/share/xsystem4"
#endif

#ifdef __SWITCH__
#define MMAP_IF_64BIT 0
#else
#define MMAP_IF_64BIT (sizeof(void*) >= 8 ? ARCHIVE_MMAP : 0)
#endif

extern bool game_daibanchou;
extern bool game_daibanchou_en;
extern bool game_daiteikoku;
extern bool game_haruurare;
extern bool game_rance01;
extern bool game_rance02;
extern bool game_rance02_mg;
extern bool game_rance6;
extern bool game_rance6_mg;
extern bool game_rance7_mg;
extern bool game_sengokurance;
extern bool game_rance8;
extern bool game_rance8_mg;
extern bool game_rance9;
extern bool game_rance10;
extern bool game_dohnadohna;
extern bool game_evenicle;
extern bool game_evenicle_rance;
extern bool game_evenicle_rance_cn;
extern bool game_evenicle_gaiden;
extern bool game_bladebriders;
extern bool game_evenicle2;
extern bool game_evenicle2_rance;
extern bool game_haharanman;
extern bool game_okusama;
extern bool game_okusama_ntr;
extern bool game_night_remaster;
extern bool game_oyakorankan;
extern bool game_toushin3;
extern bool game_rance03;
extern bool game_dungeons_and_dolls;
extern bool game_escalayer_reboot;
extern bool game_galzoo;
extern bool game_ixseal;
extern bool game_haruka;
extern bool game_tsumashibori;
extern bool game_tsumamigui3;
extern bool game_tsumamigui3_jun;
extern bool game_tsumamigui3_ntr;
extern bool game_maman;
extern bool game_waidonyo;
extern bool game_dorapeko;
extern bool game_pascha3;
extern bool game_sanbiki;
extern bool game_haniwapetit;

bool sys_is_sdl_main_thread(void);

#endif /* XSYSTEM4_H */
