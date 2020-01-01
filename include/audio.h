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

#ifndef SYSTEM4_AUDIO_H
#define SYSTEM4_AUDIO_H

#include <stdbool.h>

enum audio_type {
	AUDIO_MUSIC,
	AUDIO_SOUND
};

void audio_init(void);
void audio_fini(void);

bool audio_exists(enum audio_type type, int no);
int audio_prepare(enum audio_type type, int channel, int no);
int audio_unprepare(enum audio_type type, int channel);
int audio_play(enum audio_type type, int channel);
int audio_stop(enum audio_type type, int channel);
bool audio_is_playing(enum audio_type type, int channel);
int audio_set_loop_count(enum audio_type type, int channel, int count);
int audio_get_loop_count(enum audio_type type, int channel);
int audio_set_loop_start_pos(enum audio_type type, int channel, int pos);
int audio_set_loop_end_pos(enum audio_type type, int channel, int pos);
int audio_fade(enum audio_type type, int channel, int time, int volume, bool stop);
int audio_stop_fade(enum audio_type type, int channel);
bool audio_is_fading(enum audio_type type, int channel);
int audio_pause(enum audio_type type, int channel);
int audio_restart(enum audio_type type, int channel);
bool audio_is_paused(enum audio_type type, int channel);
int audio_get_pos(enum audio_type type, int channel);
int audio_get_length(enum audio_type type, int channel);
int audio_get_sample_pos(enum audio_type type, int channel);
int audio_get_sample_length(enum audio_type type, int channel);
int audio_seek(enum audio_type type, int channel, int pos);
int audio_get_unused_channel(enum audio_type type);
int audio_reverse_LR(enum audio_type type, int channel);
int audio_get_volume(enum audio_type type, int channel);
int audio_get_time_length(enum audio_type type, int channel);
int audio_get_group_num(enum audio_type type, int channel);
int audio_prepare_from_file(enum audio_type type, int channel, char *filename);

#endif /* SYSTEM4_AUDIO_H */
