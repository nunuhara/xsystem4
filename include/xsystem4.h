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

struct config {
	char *game_name;
	char *ain_filename;
	char *game_dir;
	char *save_dir;
	char *home_dir;
	int view_width;
	int view_height;
	size_t mixer_nr_channels;
	char **mixer_channels;

	char *bgi_path;
	char *wai_path;
};

extern struct config config;

char *unix_path(const char *path);
char *gamedir_path(const char *path);
char *savedir_path(const char *path);

#ifndef XSYS4_DATA_DIR
#define XSYS4_DATA_DIR "/usr/local/share/xsystem4"
#endif

#endif /* XSYSTEM4_H */
