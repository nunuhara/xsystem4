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
//int wav_set_loop_count(int channel, int count);
//int bgm_set_loop_count(int channel, int count);
//int wav_get_loop_count(int channel);
//int bgm_get_loop_count(int channel);
//int wav_set_loop_start_pos(int channel, int pos);
//int bgm_set_loop_start_pos(int channel, int pos);
//int wav_set_loop_end_pos(int channel, int pos);
//int bgm_set_loop_end_pos(int channel, int pos);
int wav_fade(int ch, int time, int volume, bool stop);
int bgm_fade(int ch, int time, int volume, bool stop);
//int wav_stop_fade(int channel);
//int bgm_stop_fade(int channel);
//bool wav_is_fading(int channel);
//bool bgm_is_fading(int channel);
//int wav_pause(int channel);
//int bgm_pause(int channel);
//int wav_restart(int channel);
//int bgm_restart(int channel);
//bool wav_is_paused(int channel);
//bool bgm_is_paused(int channel);
//int wav_get_pos(int channel);
//int bgm_get_pos(int channel);
//int wav_get_length(int channel);
//int bgm_get_length(int channel);
//int wav_get_sample_pos(int channel);
//int bgm_get_sample_pos(int channel);
//int wav_get_sample_length(int channel);
//int bgm_get_sample_length(int channel);
//int wav_seek(int channel, int pos);
//int bgm_seek(int channel, int pos);
int wav_get_unused_channel(void);
int bgm_get_unused_channel(void);
//int wav_reverse_LR(int channel);
//int bgm_reverse_LR(int channel);
//int wav_get_volume(int channel);
//int bgm_get_volume(int channel);
int wav_get_time_length(int ch);
//int wav_get_group_num(int channel);
//int bgm_get_group_num(int channel);
//int wav_prepare_from_file(int channel, char *filename);
//int bgm_prepare_from_file(int channel, char *filename);

#endif /* SYSTEM4_AUDIO_H */
