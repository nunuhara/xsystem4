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

void audio_init(void);
void audio_fini(void);

bool wav_exists(int no);
bool bgm_exists(int no);
int wav_prepare(int ch, int no);
int bgm_prepare(int ch, int no);
int wav_unprepare(int ch);
int bgm_unprepare(int ch);
int wav_play(int ch);
int bgm_play(int ch);
int wav_stop(int ch);
int bgm_stop(int ch);
bool wav_is_playing(int ch);
bool bgm_is_playing(int ch);
//int audio_set_loop_count(enum audio_type type, int channel, int count);
//int audio_get_loop_count(enum audio_type type, int channel);
//int audio_set_loop_start_pos(enum audio_type type, int channel, int pos);
//int audio_set_loop_end_pos(enum audio_type type, int channel, int pos);
int wav_fade(int ch, int time, int volume, bool stop);
int bgm_fade(int ch, int time, int volume, bool stop);
//int audio_stop_fade(enum audio_type type, int channel);
//bool audio_is_fading(enum audio_type type, int channel);
//int audio_pause(enum audio_type type, int channel);
//int audio_restart(enum audio_type type, int channel);
//bool audio_is_paused(enum audio_type type, int channel);
//int audio_get_pos(enum audio_type type, int channel);
//int audio_get_length(enum audio_type type, int channel);
//int audio_get_sample_pos(enum audio_type type, int channel);
//int audio_get_sample_length(enum audio_type type, int channel);
//int audio_seek(enum audio_type type, int channel, int pos);
int wav_get_unused_channel(void);
int bgm_get_unused_channel(void);
//int audio_reverse_LR(enum audio_type type, int channel);
//int audio_get_volume(enum audio_type type, int channel);
int wav_get_time_length(int ch);
//int audio_get_group_num(enum audio_type type, int channel);
//int audio_prepare_from_file(enum audio_type type, int channel, char *filename);

#endif /* SYSTEM4_AUDIO_H */
