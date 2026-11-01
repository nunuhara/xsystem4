/* Copyright (C) 2026 kichikuou <KichikuouChrome@gmail.com>
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

#ifndef SYSTEM4_AUDIO_META_H
#define SYSTEM4_AUDIO_META_H

enum asset_type;
struct string;

enum audio_meta_field {
	AUDIO_META_GROUP = 1 << 0,
	AUDIO_META_LOOP_COUNT = 1 << 1,
	AUDIO_META_LOOP_RANGE = 1 << 2,
	AUDIO_META_VOLUME = 1 << 3,
};

struct audio_meta {
	unsigned fields;
	int group;
	int loop_count;
	int loop_start;
	int loop_end;
	int volume;
};

void audio_meta_load(void);
void audio_meta_load_timemap(void);
const struct audio_meta *audio_meta_get_by_number(enum asset_type type, int no);
const struct audio_meta *audio_meta_get_by_name(const char *name);
struct string *audio_meta_get_sound_name_by_number(int number);

#endif
