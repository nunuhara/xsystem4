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

#include <assert.h>

#include "system4.h"

#include "asset_manager.h"
#include "audio.h"
#include "id_pool.h"
#include "mixer.h"
#include "xsystem4.h"

/*
 * Common audio interface for SACT2/KiwiSoundEngine.
 * This is just a wrapper over the generic audio interface in audio_mixer.c.
 */

static struct id_pool wav;
static struct id_pool bgm;

// Anonymous channels for playing sounds in-engine. audio_update must be called
// periodically to clean up channels that have finished playing.
#define NR_ANONYMOUS_CHANNELS 64
static int anonymous_channels[NR_ANONYMOUS_CHANNELS];

void audio_init(void)
{
	static bool audio_initialized = false;
	if (audio_initialized)
		return;

	id_pool_init(&wav, 0);
	id_pool_init(&bgm, 0);

	for (int i = 0; i < NR_ANONYMOUS_CHANNELS; i++) {
		anonymous_channels[i] = -1;
	}

	mixer_init();
	audio_initialized = true;
}

void audio_reset(void)
{
	for (int i = id_pool_get_first(&wav); i >= 0; i = id_pool_get_next(&wav, i)) {
		wav_unprepare(i);
	}
	for (int i = id_pool_get_first(&bgm); i >= 0; i = id_pool_get_next(&bgm, i)) {
		bgm_unprepare(i);
	}
	for (int i = 0; i < NR_ANONYMOUS_CHANNELS; i++) {
		anonymous_channels[i] = -1;
	}
}

void audio_update(void)
{
	for (int i = 0; i < NR_ANONYMOUS_CHANNELS; i++) {
		if (anonymous_channels[i] >= 0) {
			if (!wav_is_playing(anonymous_channels[i])) {
				wav_unprepare(anonymous_channels[i]);
				anonymous_channels[i] = -1;
			}
		}
	}
}

bool audio_play_sound(int sound_no)
{
	for (int i = 0; i < NR_ANONYMOUS_CHANNELS; i++) {
		if (anonymous_channels[i] == -1) {
			int ch = wav_get_unused_channel();
			if (!wav_prepare(ch, sound_no))
				return false;
			if (!wav_play(ch)) {
				wav_unprepare(ch);
				return false;
			}
			anonymous_channels[i] = ch;
			return true;
		}
	}
	WARNING("Failed to allocate anonymous audio channel");
	return false;
}

bool audio_play_archive_data(struct archive_data *dfile)
{
	for (int i = 0; i < NR_ANONYMOUS_CHANNELS; i++) {
		if (anonymous_channels[i] == -1) {
			int ch = wav_get_unused_channel();
			if (!wav_prepare_from_archive_data(ch, dfile))
				return false;
			if (!wav_play(ch)) {
				wav_unprepare(ch);
				return false;
			}
			anonymous_channels[i] = ch;
			return true;
		}
	}
	WARNING("Failed to allocate anonymous audio channel");
	return false;
}

bool wav_exists(int no) { return asset_exists(ASSET_SOUND, no); }
bool bgm_exists(int no) { return asset_exists(ASSET_BGM, no); }

static int audio_prepare(struct id_pool *pool, int id, enum asset_type type, int no)
{
	if (id < 0)
		return 0;
	struct channel *ch = channel_open(type, no);
	struct channel *old = id_pool_set(pool, id, ch);
	if (old)
		channel_close(old);
	return !!ch;
}

int wav_prepare(int id, int no) { return audio_prepare(&wav, id, ASSET_SOUND, no); }
int bgm_prepare(int id, int no) { return audio_prepare(&bgm, id, ASSET_BGM, no); }

static int audio_unprepare(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_release(pool, id);
	if (!ch)
		return 0;
	channel_close(ch);
	return 1;
}

int wav_unprepare(int id) { return audio_unprepare(&wav, id); }
int bgm_unprepare(int id) { return audio_unprepare(&bgm, id); }

static int audio_play(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch) {
		// NOTE: SACT2 brings up error GUI here
		WARNING("Tried to play from unprepared audio channel");
		return 0;
	}
	return channel_play(ch);
}
int wav_play(int id) { return audio_play(&wav, id); }
int bgm_play(int id) { return audio_play(&bgm, id); }

static int audio_stop(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch) {
		return 0;
	}
	return channel_stop(ch);
}
int wav_stop(int id) { return audio_stop(&wav, id); }
int bgm_stop(int id) { return audio_stop(&bgm, id); }

static int audio_is_playing(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	return ch && channel_is_playing(ch);
}
bool wav_is_playing(int id) { return audio_is_playing(&wav, id); }
bool bgm_is_playing(int id) { return audio_is_playing(&bgm, id); }

static int audio_set_loop_count(struct id_pool *pool, int id, int count)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_set_loop_count(ch, count);
}
int wav_set_loop_count(int id, int count) { return audio_set_loop_count(&wav, id, count); }
int bgm_set_loop_count(int id, int count) { return audio_set_loop_count(&bgm, id, count); }

static int audio_get_loop_count(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_loop_count(ch);
}
int wav_get_loop_count(int id) { return audio_get_loop_count(&wav, id); }
int bgm_get_loop_count(int id) { return audio_get_loop_count(&bgm, id); }

static int audio_set_loop_start_pos(struct id_pool *pool, int id, int pos)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_set_loop_start_pos(ch, pos);
}
int wav_set_loop_start_pos(int id, int pos) { return audio_set_loop_start_pos(&wav, id, pos); }
int bgm_set_loop_start_pos(int id, int pos) { return audio_set_loop_start_pos(&bgm, id, pos); }

static int audio_set_loop_end_pos(struct id_pool *pool, int id, int pos)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_set_loop_end_pos(ch, pos);
}
int wav_set_loop_end_pos(int id, int pos) { return audio_set_loop_end_pos(&wav, id, pos); }
int bgm_set_loop_end_pos(int id, int pos) { return audio_set_loop_end_pos(&bgm, id, pos); }

static int audio_fade(struct id_pool *pool, int id, int time, int volume, bool stop)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_fade(ch, time, volume, stop);
}
int wav_fade(int id, int time, int volume, bool stop) { return audio_fade(&wav, id, time, volume, stop); }
int bgm_fade(int id, int time, int volume, bool stop) { return audio_fade(&bgm, id, time, volume, stop); }

static int audio_stop_fade(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_stop_fade(ch);
}
int wav_stop_fade(int id) { return audio_stop_fade(&wav, id); }
int bgm_stop_fade(int id) { return audio_stop_fade(&bgm, id); }

static bool audio_is_fading(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_is_fading(ch);
}
bool wav_is_fading(int id) { return audio_is_fading(&wav, id); }
bool bgm_is_fading(int id) { return audio_is_fading(&bgm, id); }

static int audio_pause(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_pause(ch);
}
int wav_pause(int id) { return audio_pause(&wav, id); }
int bgm_pause(int id) { return audio_pause(&bgm, id); }

static int audio_restart(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_restart(ch);
}
int wav_restart(int id) { return audio_restart(&wav, id); }
int bgm_restart(int id) { return audio_restart(&bgm, id); }

static bool audio_is_paused(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_is_paused(ch);
}
bool wav_is_paused(int id) { return audio_is_paused(&wav, id); }
bool bgm_is_paused(int id) { return audio_is_paused(&bgm, id); }

static int audio_get_pos(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_pos(ch);
}
int wav_get_pos(int id) { return audio_get_pos(&wav, id); }
int bgm_get_pos(int id) { return audio_get_pos(&bgm, id); }

static int audio_get_length(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_length(ch);
}
int wav_get_length(int id) { return audio_get_length(&wav, id); }
int bgm_get_length(int id) { return audio_get_length(&bgm, id); }

static int audio_get_sample_pos(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_sample_pos(ch);
}
int wav_get_sample_pos(int id) { return audio_get_sample_pos(&wav, id); }
int bgm_get_sample_pos(int id) { return audio_get_sample_pos(&bgm, id); }

static int audio_get_sample_length(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_sample_length(ch);
}
int wav_get_sample_length(int id) { return audio_get_sample_length(&wav, id); }
int bgm_get_sample_length(int id) { return audio_get_sample_length(&bgm, id); }

static int audio_seek(struct id_pool *pool, int id, int pos)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_seek(ch, pos);
}
int wav_seek(int id, int pos) { return audio_seek(&wav, id, pos); }
int bgm_seek(int id, int pos) { return audio_seek(&bgm, id, pos); }

int wav_get_unused_channel(void) { return id_pool_get_unused(&wav); }
int bgm_get_unused_channel(void) { return id_pool_get_unused(&bgm); }

static int audio_reverse_LR(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_reverse_LR(ch);
}
int wav_reverse_LR(int id) { return audio_reverse_LR(&wav, id); }
int bgm_reverse_LR(int id) { return audio_reverse_LR(&bgm, id); }

static int audio_get_volume(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_volume(ch);
}
int wav_get_volume(int id) { return audio_get_volume(&wav, id); }
int bgm_get_volume(int id) { return audio_get_volume(&bgm, id); }

static int audio_get_time_length(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_time_length(ch);
}
int wav_get_time_length(int id) { return audio_get_time_length(&wav, id); }
int bgm_get_time_length(int id) { return audio_get_time_length(&bgm, id); }

static int audio_get_data_no(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return -1;
	return channel_get_data_no(ch);
}

int wav_get_group_num(int id)
{
	int no = audio_get_data_no(&wav, id);
	if (no < 0)
		return 0;
	return wav_get_group_num_from_data_num(no);
}
//int bgm_get_group_num(int channel);

static int audio_prepare_from_file(struct id_pool *pool, int id, char *filename)
{
	if (id < 0)
		return 0;
	struct channel *ch = channel_open_file(filename);
	struct channel *old = id_pool_set(pool, id, ch);
	if (old)
		channel_close(old);
	return !!ch;
}

int wav_prepare_from_file(int id, char *filename) { return audio_prepare_from_file(&wav, id, filename); }
int bgm_prepare_from_file(int id, char *filename) { return audio_prepare_from_file(&bgm, id, filename); }

static int audio_prepare_from_archive_data(struct id_pool *pool, int id, struct archive_data *dfile)
{
	if (id < 0)
		return 0;
	struct channel *ch = channel_open_archive_data(dfile);
	struct channel *old = id_pool_set(pool, id, ch);
	if (old)
		channel_close(old);
	return !!ch;
}

int wav_prepare_from_archive_data(int id, struct archive_data *dfile) {
	return audio_prepare_from_archive_data(&wav, id, dfile);
}

int wav_get_group_num_from_data_num(int no)
{
	struct wai *wai = wai_get(no);
	return wai ? wai->channel : 0;
}
