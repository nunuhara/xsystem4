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

#ifndef SYSTEM4_MIXER_H
#define SYSTEM4_MIXER_H

#include <stdbool.h>

struct bgi {
	int no;
	int loop_count;
	int loop_start;
	int loop_end;
	int channel;
	int volume;
};

struct wai {
	//int uk1;
	//int uk2;
	int channel;
	//int uk3; // version 4+
};

void bgi_read(const char *path);
struct bgi *bgi_get(int no);
void wai_load(const char *path);
struct wai *wai_get(int no);

void mixer_init(void);
int mixer_get_numof(void);
const char *mixer_get_name(int n);
int mixer_set_name(int n, const char *name);
int mixer_get_volume(int n, int *volume);
int mixer_set_volume(int n, int volume);
int mixer_get_mute(int n, int *mute);
int mixer_set_mute(int n, int mute);

struct sts_mixer_stream_t;
int mixer_stream_play(struct sts_mixer_stream_t* stream, int volume);
bool mixer_stream_set_volume(int voice, int volume);
void mixer_stream_stop(int voice);

struct archive_data;
struct channel;
enum asset_type;

struct channel *channel_open(enum asset_type type, int no);
// Takes ownership of dfile.
struct channel *channel_open_archive_data(struct archive_data *dfile);
struct channel *channel_open_file(const char *path);
void channel_close(struct channel *ch);
int channel_play(struct channel *ch);
int channel_stop(struct channel *ch);
int channel_is_playing(struct channel *ch);
int channel_set_loop_count(struct channel *ch, int count);
int channel_get_loop_count(struct channel *ch);
int channel_set_loop_start_pos(struct channel *ch, int pos);
int channel_set_loop_end_pos(struct channel *ch, int pos);
int channel_fade(struct channel *ch, int time, int volume, bool stop);
int channel_stop_fade(struct channel *ch);
int channel_is_fading(struct channel *ch);
int channel_pause(struct channel *ch);
int channel_restart(struct channel *ch);
int channel_is_paused(struct channel *ch);
int channel_get_pos(struct channel *ch);
int channel_get_length(struct channel *ch);
int channel_get_sample_pos(struct channel *ch);
int channel_get_sample_length(struct channel *ch);
int channel_seek(struct channel *ch, int pos);
int channel_reverse_LR(struct channel *ch);
int channel_get_volume(struct channel *ch);
int channel_get_time_length(struct channel *ch);
int channel_get_data_no(struct channel *ch);

#endif /* SYSTEM4_MIXER_H */
