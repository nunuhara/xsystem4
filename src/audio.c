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
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/string.h"

#include "asset_manager.h"
#include "audio.h"
#include "audio_meta.h"
#include "id_pool.h"
#include "mixer.h"
#include "queue.h"

/*
 * Common audio interface for SACT2/KiwiSoundEngine.
 * This is just a wrapper over the generic audio interface in audio_mixer.c.
 */

static struct id_pool wav;
static struct id_pool bgm;
static struct id_pool channels;
static struct id_pool se_players;

struct audio_se_playback {
	LIST_ENTRY(audio_se_playback) entry;
	struct channel *channel;
	struct string *name;
};

struct audio_se_player {
	// Minimum time in milliseconds between two playbacks of the same sound.
	int play_span;
	// If false, a new playback stops the playbacks of the same sound.
	bool multi_play;
	LIST_HEAD(, audio_se_playback) playbacks;
};

static void se_playback_remove(struct audio_se_playback *playback)
{
	LIST_REMOVE(playback, entry);
	channel_close(playback->channel);
	free_string(playback->name);
	free(playback);
}

static void se_stop_all(struct audio_se_player *player)
{
	while (!LIST_EMPTY(&player->playbacks))
		se_playback_remove(LIST_FIRST(&player->playbacks));
}

static void se_cleanup(struct audio_se_player *player)
{
	struct audio_se_playback *p, *next;
	for (p = LIST_FIRST(&player->playbacks); p; p = next) {
		next = LIST_NEXT(p, entry);
		if (!channel_is_playing(p->channel))
			se_playback_remove(p);
	}
}

static void apply_metadata(struct channel *ch, const struct audio_meta *meta)
{
	if (!meta)
		return;
	if (meta->fields & AUDIO_META_GROUP)
		channel_set_mixer(ch, meta->group);
	if (meta->fields & AUDIO_META_LOOP_COUNT)
		channel_set_loop_count(ch, meta->loop_count);
	if (meta->fields & AUDIO_META_LOOP_RANGE) {
		channel_set_loop_start_pos(ch, meta->loop_start);
		channel_set_loop_end_pos(ch, meta->loop_end);
	}
	if (meta->fields & AUDIO_META_VOLUME)
		channel_set_volume(ch, meta->volume);
}

static void apply_numbered_metadata(struct channel *ch, enum asset_type type, int no)
{
	const struct audio_meta *meta = audio_meta_get_by_number(type, no);
	if (!meta) {
		if (type == ASSET_SOUND)
			channel_set_mixer(ch, 1);
		else if (type == ASSET_BGM)
			channel_set_loop_count(ch, 0);
		return;
	}
	apply_metadata(ch, meta);
}

static struct channel *open_asset_channel(enum asset_type type, int no)
{
	struct archive_data *data = asset_get(type, no);
	return data ? channel_open_archive_data(data) : NULL;
}

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
	id_pool_init(&channels, 0);
	id_pool_init(&se_players, 0);

	for (int i = 0; i < NR_ANONYMOUS_CHANNELS; i++) {
		anonymous_channels[i] = -1;
	}

	mixer_init();
	audio_meta_load();
	audio_initialized = true;
}

void audio_reset(void)
{
	for (int i = id_pool_get_first(&channels); i >= 0; i = id_pool_get_next(&channels, i))
		audio_unprepare(i);
	for (int i = id_pool_get_first(&se_players); i >= 0; i = id_pool_get_next(&se_players, i)) {
		struct audio_se_player *player = id_pool_release(&se_players, i);
		se_stop_all(player);
		free(player);
	}
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
	for (int i = id_pool_get_first(&se_players); i >= 0; i = id_pool_get_next(&se_players, i))
		se_cleanup(id_pool_get(&se_players, i));
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
			if (!wav_prepare_from_archive_data(ch, AUDIO_NO_METADATA, dfile))
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

static int pool_prepare(struct id_pool *pool, int id, enum asset_type type, int no)
{
	if (id < 0)
		return 0;
	struct channel *ch = open_asset_channel(type, no);
	if (ch)
		apply_numbered_metadata(ch, type, no);
	struct channel *old = id_pool_set(pool, id, ch);
	if (old)
		channel_close(old);
	return !!ch;
}

int wav_prepare(int id, int no) { return pool_prepare(&wav, id, ASSET_SOUND, no); }
int bgm_prepare(int id, int no) { return pool_prepare(&bgm, id, ASSET_BGM, no); }

static int pool_unprepare(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_release(pool, id);
	if (!ch)
		return 0;
	channel_close(ch);
	return 1;
}

int wav_unprepare(int id) { return pool_unprepare(&wav, id); }
int bgm_unprepare(int id) { return pool_unprepare(&bgm, id); }

static int pool_play(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch) {
		// NOTE: SACT2 brings up error GUI here
		WARNING("Tried to play from unprepared audio channel");
		return 0;
	}
	return channel_play(ch);
}
int wav_play(int id) { return pool_play(&wav, id); }
int bgm_play(int id) { return pool_play(&bgm, id); }

static int pool_stop(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch) {
		return 0;
	}
	return channel_stop(ch);
}
int wav_stop(int id) { return pool_stop(&wav, id); }
int bgm_stop(int id) { return pool_stop(&bgm, id); }

static int pool_is_playing(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	return ch && channel_is_playing(ch);
}
bool wav_is_playing(int id) { return pool_is_playing(&wav, id); }
bool bgm_is_playing(int id) { return pool_is_playing(&bgm, id); }

static int pool_set_loop_count(struct id_pool *pool, int id, int count)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_set_loop_count(ch, count);
}
int wav_set_loop_count(int id, int count) { return pool_set_loop_count(&wav, id, count); }
int bgm_set_loop_count(int id, int count) { return pool_set_loop_count(&bgm, id, count); }

static int pool_get_loop_count(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_loop_count(ch);
}
int wav_get_loop_count(int id) { return pool_get_loop_count(&wav, id); }
int bgm_get_loop_count(int id) { return pool_get_loop_count(&bgm, id); }

static int pool_set_loop_start_pos(struct id_pool *pool, int id, int pos)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_set_loop_start_pos(ch, pos);
}
int wav_set_loop_start_pos(int id, int pos) { return pool_set_loop_start_pos(&wav, id, pos); }
int bgm_set_loop_start_pos(int id, int pos) { return pool_set_loop_start_pos(&bgm, id, pos); }

static int pool_set_loop_end_pos(struct id_pool *pool, int id, int pos)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_set_loop_end_pos(ch, pos);
}
int wav_set_loop_end_pos(int id, int pos) { return pool_set_loop_end_pos(&wav, id, pos); }
int bgm_set_loop_end_pos(int id, int pos) { return pool_set_loop_end_pos(&bgm, id, pos); }

static int pool_fade(struct id_pool *pool, int id, int time, int volume, bool stop)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_fade(ch, time, volume, stop, FADE_CURVE_LINEAR);
}
int wav_fade(int id, int time, int volume, bool stop) { return pool_fade(&wav, id, time, volume, stop); }
int bgm_fade(int id, int time, int volume, bool stop) { return pool_fade(&bgm, id, time, volume, stop); }

static int pool_stop_fade(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_stop_fade(ch);
}
int wav_stop_fade(int id) { return pool_stop_fade(&wav, id); }
int bgm_stop_fade(int id) { return pool_stop_fade(&bgm, id); }

static bool pool_is_fading(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_is_fading(ch);
}
bool wav_is_fading(int id) { return pool_is_fading(&wav, id); }
bool bgm_is_fading(int id) { return pool_is_fading(&bgm, id); }

static int pool_pause(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_pause(ch);
}
int wav_pause(int id) { return pool_pause(&wav, id); }
int bgm_pause(int id) { return pool_pause(&bgm, id); }

static int pool_restart(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_restart(ch);
}
int wav_restart(int id) { return pool_restart(&wav, id); }
int bgm_restart(int id) { return pool_restart(&bgm, id); }

static bool pool_is_paused(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_is_paused(ch);
}
bool wav_is_paused(int id) { return pool_is_paused(&wav, id); }
bool bgm_is_paused(int id) { return pool_is_paused(&bgm, id); }

static int pool_get_pos(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_pos(ch);
}
int wav_get_pos(int id) { return pool_get_pos(&wav, id); }
int bgm_get_pos(int id) { return pool_get_pos(&bgm, id); }

static int pool_get_length(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_length(ch);
}
int wav_get_length(int id) { return pool_get_length(&wav, id); }
int bgm_get_length(int id) { return pool_get_length(&bgm, id); }

static int pool_get_sample_pos(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_sample_pos(ch);
}
int wav_get_sample_pos(int id) { return pool_get_sample_pos(&wav, id); }
int bgm_get_sample_pos(int id) { return pool_get_sample_pos(&bgm, id); }

static int pool_get_sample_length(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_sample_length(ch);
}
int wav_get_sample_length(int id) { return pool_get_sample_length(&wav, id); }
int bgm_get_sample_length(int id) { return pool_get_sample_length(&bgm, id); }

static int pool_seek(struct id_pool *pool, int id, int pos)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_seek(ch, pos);
}
int wav_seek(int id, int pos) { return pool_seek(&wav, id, pos); }
int bgm_seek(int id, int pos) { return pool_seek(&bgm, id, pos); }

int wav_get_unused_channel(void) { return id_pool_get_unused(&wav); }
int bgm_get_unused_channel(void) { return id_pool_get_unused(&bgm); }

static int pool_reverse_LR(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_reverse_LR(ch);
}
int wav_reverse_LR(int id) { return pool_reverse_LR(&wav, id); }
int bgm_reverse_LR(int id) { return pool_reverse_LR(&bgm, id); }

static int pool_get_volume(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_volume(ch);
}
int wav_get_volume(int id) { return pool_get_volume(&wav, id); }
int bgm_get_volume(int id) { return pool_get_volume(&bgm, id); }

static int pool_get_time_length(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return 0;
	return channel_get_time_length(ch);
}
int wav_get_time_length(int id) { return pool_get_time_length(&wav, id); }
int bgm_get_time_length(int id) { return pool_get_time_length(&bgm, id); }

static int get_data_length(enum asset_type type, int no) {
	struct channel *ch = open_asset_channel(type, no);
	if (!ch)
		return 0;
	int len = channel_get_time_length(ch);
	channel_close(ch);
	return len;
}

int wav_get_data_length(int no) { return get_data_length(ASSET_SOUND, no); }
int bgm_get_data_length(int no) { return get_data_length(ASSET_BGM, no); }

static int pool_get_data_no(struct id_pool *pool, int id)
{
	struct channel *ch = id_pool_get(pool, id);
	if (!ch)
		return -1;
	return channel_get_data_no(ch);
}

int wav_get_group_num(int id)
{
	int no = pool_get_data_no(&wav, id);
	if (no < 0)
		return 0;
	return wav_get_group_num_from_data_num(no);
}
//int bgm_get_group_num(int channel);

static int pool_prepare_from_file(struct id_pool *pool, int id, char *filename)
{
	if (id < 0)
		return 0;
	struct channel *ch = channel_open_file(filename);
	struct channel *old = id_pool_set(pool, id, ch);
	if (old)
		channel_close(old);
	return !!ch;
}

int wav_prepare_from_file(int id, char *filename) { return pool_prepare_from_file(&wav, id, filename); }
int bgm_prepare_from_file(int id, char *filename) { return pool_prepare_from_file(&bgm, id, filename); }

static int pool_prepare_from_archive_data(struct id_pool *pool, int id,
					  enum asset_type type, int metadata_no,
					  struct archive_data *dfile)
{
	if (id < 0)
		return 0;
	struct channel *ch = channel_open_archive_data(dfile);
	if (ch && metadata_no >= 0)
		apply_numbered_metadata(ch, type, metadata_no);
	struct channel *old = id_pool_set(pool, id, ch);
	if (old)
		channel_close(old);
	return !!ch;
}

int wav_prepare_from_archive_data(int id, int metadata_no, struct archive_data *dfile)
{
	return pool_prepare_from_archive_data(
		&wav, id, ASSET_SOUND, metadata_no, dfile);
}

int bgm_prepare_from_archive_data(int id, int metadata_no, struct archive_data *dfile)
{
	return pool_prepare_from_archive_data(
		&bgm, id, ASSET_BGM, metadata_no, dfile);
}

int wav_get_group_num_from_data_num(int no)
{
	const struct audio_meta *meta = audio_meta_get_by_number(ASSET_SOUND, no);
	return meta ? meta->group : 0;
}

enum sound_group {
	SOUND_GROUP_BGM = 1,
	SOUND_GROUP_SE = 2,
};

static struct archive_data *audio_get_named_data(const char *name)
{
	struct archive_data *data = asset_get_by_name(ASSET_SOUND, name, NULL);
	return data ? data : asset_get_by_name(ASSET_VOICE, name, NULL);
}

static struct channel *audio_open_named(const char *name, bool streaming)
{
	struct archive_data *data = audio_get_named_data(name);
	if (!data)
		return NULL;
	struct channel *ch = channel_open_archive_data(data);
	if (!ch)
		return NULL;
	const struct audio_meta *meta = audio_meta_get_by_name(name);
	if (!meta)
		channel_set_mixer(ch, streaming ? SOUND_GROUP_BGM : SOUND_GROUP_SE);
	channel_set_loop_count(ch, streaming ? 0 : 1);
	apply_metadata(ch, meta);
	return ch;
}

bool audio_id_exists(int id) { return id_pool_get(&channels, id) != NULL; }

bool audio_prepare(int id, struct string *name, bool streaming)
{
	if (id < 0)
		return false;
	struct channel *ch = audio_open_named(name->text, streaming);
	if (!ch)
		return false;
	struct channel *old = id_pool_set(&channels, id, ch);
	if (old)
		channel_close(old);
	return true;
}

bool audio_unprepare(int id) { return pool_unprepare(&channels, id); }

static struct channel *audio_channel(int id) { return id_pool_get(&channels, id); }

bool audio_play(int id) { struct channel *ch = audio_channel(id); return ch && channel_play(ch); }
bool audio_stop(int id) { struct channel *ch = audio_channel(id); return ch && channel_stop(ch); }
bool audio_is_playing(int id) { struct channel *ch = audio_channel(id); return ch && channel_is_playing(ch); }

bool audio_set_loop_count(int id, int count) { struct channel *ch = audio_channel(id); return ch && channel_set_loop_count(ch, count); }
int audio_get_loop_count(int id) { struct channel *ch = audio_channel(id); return ch ? channel_get_loop_count(ch) : 0; }

bool audio_pause(int id) { struct channel *ch = audio_channel(id); return ch && channel_pause(ch); }
bool audio_restart(int id) { struct channel *ch = audio_channel(id); return ch && channel_restart(ch); }
bool audio_is_paused(int id) { struct channel *ch = audio_channel(id); return ch && channel_is_paused(ch); }

bool audio_fade(int id, int time, float volume, bool stop, int curve)
{
	struct channel *ch = audio_channel(id);
	if (!ch)
		return false;
	if (curve > FADE_CURVE_MAX)
		WARNING("Unknown fade curve %d", curve);
	if (curve < FADE_CURVE_MIN || curve > FADE_CURVE_MAX)
		curve = FADE_CURVE_LINEAR;
	return channel_fade(ch, time > 0 ? time : 0, (int)(volume * 100 + 0.5f), stop, curve);
}

bool audio_stop_fade(int id) { struct channel *ch = audio_channel(id); return ch && channel_stop_fade(ch); }
bool audio_is_fading(int id) { struct channel *ch = audio_channel(id); return ch && channel_is_fading(ch); }
bool audio_seek(int id, int milliseconds) { struct channel *ch = audio_channel(id); return ch && channel_seek(ch, milliseconds); }
int audio_get_pos(int id) { struct channel *ch = audio_channel(id); return ch ? channel_get_pos(ch) : 0; }
int audio_get_length(int id) { struct channel *ch = audio_channel(id); return ch ? channel_get_length(ch) : 0; }
int audio_get_group(int id) { struct channel *ch = audio_channel(id); return ch ? channel_get_mixer(ch) : 0; }

bool audio_se_exists(int se_id) { return id_pool_get(&se_players, se_id) != NULL; }

static struct audio_se_player *se_player_get_or_create(int id)
{
	if (id < 0)
		return NULL;
	struct audio_se_player *player = id_pool_get(&se_players, id);
	if (!player) {
		player = xcalloc(1, sizeof(*player));
		player->multi_play = true;
		id_pool_set(&se_players, id, player);
	}
	return player;
}

bool audio_se_set_params(int se_id, int play_span, bool multi_play)
{
	struct audio_se_player *player = se_player_get_or_create(se_id);
	if (!player)
		return false;
	player->play_span = play_span;
	player->multi_play = multi_play;
	return true;
}

bool audio_se_play(int se_id, struct string *name)
{
	struct audio_se_player *player = se_player_get_or_create(se_id);
	if (!player)
		return false;
	se_cleanup(player);
	// play_span and multi_play apply only to playbacks of the same sound.
	struct audio_se_playback *p;
	LIST_FOREACH(p, &player->playbacks, entry) {
		if (!strcmp(p->name->text, name->text)
				&& channel_get_pos(p->channel) < player->play_span)
			return false;
	}
	if (!player->multi_play) {
		struct audio_se_playback *next;
		for (p = LIST_FIRST(&player->playbacks); p; p = next) {
			next = LIST_NEXT(p, entry);
			if (!strcmp(p->name->text, name->text))
				se_playback_remove(p);
		}
	}
	struct channel *ch = audio_open_named(name->text, false);
	if (!ch)
		return false;
	if (!channel_play(ch)) {
		channel_close(ch);
		return false;
	}
	p = xmalloc(sizeof(*p));
	p->channel = ch;
	p->name = string_ref(name);
	LIST_INSERT_HEAD(&player->playbacks, p, entry);
	return true;
}

bool audio_se_stop(int se_id)
{
	struct audio_se_player *player = id_pool_get(&se_players, se_id);
	if (!player)
		return false;
	se_stop_all(player);
	return true;
}

bool audio_se_is_playing(int se_id)
{
	struct audio_se_player *player = id_pool_get(&se_players, se_id);
	if (!player)
		return false;
	se_cleanup(player);
	return !LIST_EMPTY(&player->playbacks);
}

bool audio_exists_by_name(struct string *name)
{
	return asset_exists_by_name(ASSET_SOUND, name->text, NULL) ||
		asset_exists_by_name(ASSET_VOICE, name->text, NULL);
}

int audio_get_length_by_name(struct string *name)
{
	struct archive_data *data = audio_get_named_data(name->text);
	if (!data)
		return 0;
	struct channel *ch = channel_open_archive_data(data);
	if (!ch)
		return 0;
	int length = channel_get_length(ch);
	channel_close(ch);
	return length;
}

int audio_get_group_by_name(struct string *name)
{
	const struct audio_meta *meta = audio_meta_get_by_name(name->text);
	return meta ? meta->group : SOUND_GROUP_SE;
}

float audio_get_group_volume(int group)
{
	return mixer_get_effective_volume(group);
}
