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

#include <stdbool.h>
#include <SDL_mixer.h>
#include "ald.h"
#include "audio.h"
#include "system4.h"

#define AUDIO_SLOT_ALLOC_STEP 256

struct audio_slot {
	Mix_Chunk *chunk;
	int channel;
	int no;
};

static struct {
	struct audio_slot *slots;
	int nr_slots;
} audio[2];

void audio_init(void)
{
	Mix_Init(MIX_INIT_MP3 | MIX_INIT_OGG);
	if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
		WARNING("Audio initialization failed");
	}

	audio[AUDIO_MUSIC].slots = xcalloc(16, sizeof(struct audio_slot));
	audio[AUDIO_MUSIC].nr_slots = 16;

	audio[AUDIO_SOUND].slots = xcalloc(16, sizeof(struct audio_slot));
	audio[AUDIO_SOUND].nr_slots = 16;
}

void audio_fini(void)
{
	Mix_CloseAudio();
	Mix_Quit();
}

static void audio_realloc(enum audio_type type, int new_size)
{
	audio[type].slots = xrealloc(audio[type].slots, new_size * sizeof(struct audio_slot));
	for (int i = audio[type].nr_slots; i < new_size; i++) {
		audio[type].slots[i].chunk = NULL;
		audio[type].slots[i].channel = -1;
	}
	audio[type].nr_slots = new_size;
}

static struct audio_slot *audio_alloc_channel(enum audio_type type, int ch)
{
	if (ch >= audio[type].nr_slots)
		audio_realloc(type, ch + AUDIO_SLOT_ALLOC_STEP);
	return &audio[type].slots[ch];
}

int audio_get_unused_channel(enum audio_type type)
{
	for (int i = 0; i < audio[type].nr_slots; i++) {
		if (!audio[type].slots[i].chunk)
			return i;
	}

	int slot = audio[type].nr_slots;
	audio_realloc(type, slot + AUDIO_SLOT_ALLOC_STEP);
	return slot;
}

static struct audio_slot *get_slot(enum audio_type type, int ch)
{
	if (ch < 0 || ch >= audio[type].nr_slots)
		return NULL;
	return &audio[type].slots[ch];
}

static int audio_type_to_archive_type(enum audio_type type)
{
	switch (type) {
	case AUDIO_MUSIC: return ALDFILE_BGM;
	case AUDIO_SOUND: return ALDFILE_WAVE;
	}
	ERROR("Unknown audio type: %d", type);
}

bool audio_exists(enum audio_type type, int no)
{
	int archive = audio_type_to_archive_type(type);
	return ald[archive] && ald_data_exists(ald[archive], no);
}

static Mix_Chunk *load_chunk(enum audio_type type, int no)
{
	struct archive_data *dfile;
	int archive = audio_type_to_archive_type(type);

	if (!(dfile = ald_get(ald[archive], no))) {
		WARNING("Failed to load %s %d", archive == ALDFILE_BGM ? "BGM" : "WAV", no);
		return NULL;
	}

	Mix_Chunk *chunk = Mix_LoadWAV_RW(SDL_RWFromConstMem(dfile->data, dfile->size), 1);
	ald_free_data(dfile);

	if (!chunk) {
		WARNING("%s %d: not a valid audio file", archive == ALDFILE_BGM ? "BGM" : "WAV", no);
		return NULL;
	}

	return chunk;
}

static void unload_slot(struct audio_slot *slot)
{
	if (slot->chunk) {
		Mix_HaltChannel(slot->channel);
		Mix_FreeChunk(slot->chunk);
		slot->chunk = NULL;
	}
	slot->channel = -1;
}

int audio_prepare(enum audio_type type, int ch, int no)
{
	if (ch < 0)
		return 0;

	struct audio_slot *slot = audio_alloc_channel(type, ch);
	unload_slot(slot);

	audio[type].slots[ch].chunk = load_chunk(type, no);
	audio[type].slots[ch].no = no;
	return !!audio[type].slots[ch].chunk;
}

int audio_unprepare(enum audio_type type, int ch)
{
	struct audio_slot *slot = get_slot(type, ch);
	if (!slot)
		return 0;

	unload_slot(slot);
	return 1;
}

int audio_play(enum audio_type type, int ch)
{
	struct audio_slot *slot = get_slot(type, ch);
	if (!slot || !slot->chunk)
		return 0;

	if (slot->channel >= 0 && Mix_Paused(slot->channel)) {
		Mix_Resume(slot->channel);
		return 1;
	}

	slot->channel = Mix_PlayChannel(-1, slot->chunk, type == AUDIO_MUSIC ? -1 : 0);
	return slot->channel >= 0;
}

int audio_stop(enum audio_type type, int ch)
{
	struct audio_slot *slot = get_slot(type, ch);
	if (!slot)
		return 0;

	if (slot->channel >= 0)
		Mix_HaltChannel(slot->channel);
	return 1;
}

bool audio_is_playing(enum audio_type type, int ch)
{
	struct audio_slot *slot = get_slot(type, ch);
	return slot && slot->channel >= 0 && Mix_Playing(slot->channel);
}

int audio_fade(enum audio_type type, int ch, possibly_unused int time, possibly_unused int volume, bool stop)
{
	// TODO: fade
	if (stop)
		audio_stop(type, ch);
	return 1;
}

int audio_get_time_length(enum audio_type type, int ch)
{
	struct audio_slot *slot = get_slot(type, ch);
	if (!slot || !slot->chunk)
		return 0;

	int freq = 0;
	uint16_t fmt = 0;
	int chans = 0;

	Mix_QuerySpec(&freq, &fmt, &chans);

	uint32_t points = slot->chunk->alen / ((fmt & 0xFF) / 8);
	uint32_t frames = points / chans;
	return (frames * 1000) / freq;
}
