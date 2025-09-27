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
	char *vm_name;
	char *game_dir;
	char *save_dir;
	char *home_dir;
	int view_width;
	int view_height;
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
char *savedir_path(const char *path);

void get_date(int *year, int *month, int *mday, int *wday);
void get_time(int *hour, int *min, int *sec, int *ms);

void screenshot_save(void);

#ifndef XSYS4_DATA_DIR
#define XSYS4_DATA_DIR "/usr/local/share/xsystem4"
#endif

#define MMAP_IF_64BIT (sizeof(void*) >= 8 ? ARCHIVE_MMAP : 0)

extern bool game_daibanchou_en;
extern bool game_rance02_mg;
extern bool game_rance6_mg;
extern bool game_rance7_mg;
extern bool game_rance8;
extern bool game_rance8_mg;
extern bool game_dungeons_and_dolls;

#endif /* XSYSTEM4_H */
