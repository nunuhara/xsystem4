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

struct archive_data;

void audio_init(void);
void audio_reset(void);
void audio_update(void);
bool audio_play_sound(int sound_no);
// Takes ownership of dfile.
bool audio_play_archive_data(struct archive_data *dfile);

bool wav_exists(int no);
bool bgm_exists(int no);
int wav_prepare(int id, int no);
int bgm_prepare(int id, int no);
int wav_unprepare(int id);
int bgm_unprepare(int id);
int wav_play(int id);
int bgm_play(int id);
int wav_stop(int id);
int bgm_stop(int id);
bool wav_is_playing(int id);
bool bgm_is_playing(int id);
int wav_set_loop_count(int id, int count);
int bgm_set_loop_count(int id, int count);
int wav_get_loop_count(int id);
int bgm_get_loop_count(int id);
int wav_set_loop_start_pos(int id, int pos);
int bgm_set_loop_start_pos(int id, int pos);
int wav_set_loop_end_pos(int id, int pos);
int bgm_set_loop_end_pos(int id, int pos);
int wav_fade(int id, int time, int volume, bool stop);
int bgm_fade(int id, int time, int volume, bool stop);
int wav_stop_fade(int id);
int bgm_stop_fade(int id);
bool wav_is_fading(int id);
bool bgm_is_fading(int id);
int wav_pause(int id);
int bgm_pause(int id);
int wav_restart(int id);
int bgm_restart(int id);
bool wav_is_paused(int id);
bool bgm_is_paused(int id);
int wav_get_pos(int id);
int bgm_get_pos(int id);
int wav_get_length(int id);
int bgm_get_length(int id);
int wav_get_sample_pos(int id);
int bgm_get_sample_pos(int id);
int wav_get_sample_length(int id);
int bgm_get_sample_length(int id);
int wav_seek(int id, int pos);
int bgm_seek(int id, int pos);
int wav_get_unused_channel(void);
int bgm_get_unused_channel(void);
int wav_reverse_LR(int id);
int bgm_reverse_LR(int id);
int wav_get_volume(int id);
int bgm_get_volume(int id);
int wav_get_time_length(int id);
int bgm_get_time_length(int id);
int wav_get_group_num(int id);
//int bgm_get_group_num(int id);
int wav_prepare_from_file(int id, char *filename);
int bgm_prepare_from_file(int id, char *filename);

int wav_get_group_num_from_data_num(int no);

// Takes ownership of dfile.
int wav_prepare_from_archive_data(int id, struct archive_data *dfile);

#endif /* SYSTEM4_AUDIO_H */
