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

#include <SDL_mixer.h>

#include "system4.h"
#include "system4/archive.h"
#include "system4/file.h"

#include "asset_manager.h"
#include "audio.h"
#include "xsystem4.h"

#define AUDIO_SLOT_ALLOC_STEP 256

struct wav_slot {
	Mix_Chunk *chunk;
	struct archive_data *dfile;
	int channel;
	int no;
	bool reverse;
};

struct bgm_slot {
	Mix_Music *music;
	struct archive_data *dfile;
	int no;
};

struct {
	struct wav_slot *slots;
	int nr_slots;
} wav;

struct {
	struct bgm_slot *slots;
	int nr_slots;
	int playing;
} bgm;

// Anonymous channels for playing sounds in-engine. audio_update must be called
// periodically to clean up channels that have finished playing.
#define NR_ANONYMOUS_CHANNELS 64
static int anonymous_channels[NR_ANONYMOUS_CHANNELS];

#define BGI_MAX 100

struct bgi {
	int no;
	int loopno;
	int looptop;
	int len;
};

static int bgi_nfile;
static struct bgi bgi_data[BGI_MAX];

static char *bgi_gets(char *buf, int n, FILE *fp) {
	char *s = buf;
	int c;
	while (--n > 0 && (c = fgetc(fp)) != EOF) {
		c = c >> 4 | (c & 0xf) << 4;  // decrypt
		*s++ = c;
		if (c == '\n')
			break;
	}
	if (s == buf && c == EOF)
		return NULL;
	*s = '\0';
	return buf;
}

static void bgi_read(const char *path)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		WARNING("Failed to open bgi file: %s", path);
		return;
	}

	char buf[100];
	while (bgi_nfile < BGI_MAX && bgi_gets(buf, sizeof(buf), fp)) {
		int terminator;
		if (sscanf(buf, " %d, %d, %d, %d, %d",
				   &bgi_data[bgi_nfile].no,
				   &bgi_data[bgi_nfile].loopno,
				   &bgi_data[bgi_nfile].looptop,
				   &bgi_data[bgi_nfile].len,
				   &terminator) != 5
			|| terminator != -1) {
			continue;
		}
		bgi_nfile++;
	}
}

possibly_unused static struct bgi *bgi_find(int no) {
	for (int i = 0; i < bgi_nfile; i++) {
		if (bgi_data[i].no == no)
			return &bgi_data[i];
	}
	return NULL;
}

void audio_init(void)
{
	Mix_Init(MIX_INIT_MP3 | MIX_INIT_OGG);
	if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
		WARNING("Audio initialization failed");
	}

	wav.slots = xcalloc(16, sizeof(struct wav_slot));
	wav.nr_slots = 16;

	bgm.slots = xcalloc(16, sizeof(struct bgm_slot));
	bgm.nr_slots = 16;
	bgm.playing = -1;

	if (config.bgi_path)
		bgi_read(config.bgi_path);

	for (int i = 0; i < NR_ANONYMOUS_CHANNELS; i++) {
		anonymous_channels[i] = -1;
	}
}

void audio_fini(void)
{
	Mix_CloseAudio();
	Mix_Quit();
}

void audio_update(void)
{
	for (int i = 0; i < NR_ANONYMOUS_CHANNELS; i++) {
		if (anonymous_channels[i] >= 0) {
			if (!wav_is_playing(anonymous_channels[i])) {
				NOTICE("FREEING CHANNEL");
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

static void wav_realloc(int new_size)
{
	wav.slots = xrealloc(wav.slots, new_size * sizeof(struct wav_slot));
	for (int i = wav.nr_slots; i < new_size; i++) {
		wav.slots[i].chunk = NULL;
		wav.slots[i].channel = -1;
		wav.slots[i].reverse = false;
	}
	wav.nr_slots = new_size;
}

static void bgm_realloc(int new_size)
{
	bgm.slots = xrealloc(bgm.slots, new_size * sizeof(struct bgm_slot));
	for (int i = bgm.nr_slots; i < new_size; i++) {
		bgm.slots[i].music = NULL;
	}
	bgm.nr_slots = new_size;
}

static struct wav_slot *wav_alloc_channel(int ch)
{
	if (ch >= wav.nr_slots)
		wav_realloc(ch + AUDIO_SLOT_ALLOC_STEP);
	return &wav.slots[ch];
}

static struct bgm_slot *bgm_alloc_channel(int ch)
{
	if (ch >= bgm.nr_slots)
		bgm_realloc(ch + AUDIO_SLOT_ALLOC_STEP);
	return &bgm.slots[ch];
}

int wav_get_unused_channel(void)
{
	for (int i = 0; i < wav.nr_slots; i++) {
		if (!wav.slots[i].chunk)
			return i;
	}

	int slot = wav.nr_slots;
	wav_realloc(slot + AUDIO_SLOT_ALLOC_STEP);
	return slot;
}

int bgm_get_unused_channel(void)
{
	for (int i = 0; i < bgm.nr_slots; i++) {
		if (!bgm.slots[i].music)
			return i;
	}

	int slot = bgm.nr_slots;
	bgm_realloc(slot + AUDIO_SLOT_ALLOC_STEP);
	return slot;
}

static struct wav_slot *wav_get_slot(int ch)
{
	if (ch < 0 || ch >= wav.nr_slots)
		return NULL;
	return &wav.slots[ch];
}

static struct bgm_slot *bgm_get_slot(int ch)
{
	if (ch < 0 || ch >= bgm.nr_slots)
		return NULL;
	return &bgm.slots[ch];
}

bool wav_exists(int no)
{
	return asset_exists(ASSET_SOUND, no);
}

bool bgm_exists(int no)
{
	return asset_exists(ASSET_BGM, no);
}

static bool wav_load_chunk(int no, struct wav_slot *slot)
{
	struct archive_data *dfile = asset_get(ASSET_SOUND, no);
	if (!dfile) {
		WARNING("Failed to load WAV %d", no);
		return false;
	}

	Mix_Chunk *chunk = Mix_LoadWAV_RW(SDL_RWFromConstMem(dfile->data, dfile->size), 1);
	if (!chunk) {
		WARNING("WAV %d: not a valid audio file", no);
		return false;
	}

	slot->chunk = chunk;
	slot->dfile = dfile;
	slot->no = no;
	return true;
}

static bool bgm_load_music(int no, struct bgm_slot *slot)
{
	struct archive_data *dfile = asset_get(ASSET_BGM, no);
	if (!dfile) {
		WARNING("Failed to load WAV %d", no);
		return false;
	}

	Mix_MusicType type = MUS_WAV;
	const char *ext = file_extension(dfile->name);
	if (!strcmp(ext, "ogg")) {
		type = MUS_OGG;
	}

	Mix_Music *music = Mix_LoadMUSType_RW(SDL_RWFromConstMem(dfile->data, dfile->size), type, SDL_TRUE);
	if (!music) {
		WARNING("BGM %d: not a valid audio file", no);
		return false;
	}

	slot->music = music;
	slot->dfile = dfile;
	slot->no = no;
	return true;
}

static void wav_unload_slot(struct wav_slot *slot)
{
	if (slot->chunk) {
		if (slot->channel >= 0)
			Mix_HaltChannel(slot->channel);
		Mix_FreeChunk(slot->chunk);
		slot->chunk = NULL;
		archive_free_data(slot->dfile);
		slot->dfile = NULL;
	}
	slot->channel = -1;
}

static void bgm_unload_slot(struct bgm_slot *slot)
{
	if (slot->music) {
		Mix_FreeMusic(slot->music);
		slot->music = NULL;
		archive_free_data(slot->dfile);
		slot->dfile = NULL;
	}
}

int wav_prepare(int ch, int no)
{
	if (ch < 0)
		return 0;

	struct wav_slot *slot = wav_alloc_channel(ch);
	wav_unload_slot(slot);
	return wav_load_chunk(no, slot);
}

int bgm_prepare(int ch, int no)
{
	if (ch < 0)
		return 0;

	struct bgm_slot *slot = bgm_alloc_channel(ch);
	bgm_unload_slot(slot);
	return bgm_load_music(no, slot);
}

int wav_unprepare(int ch)
{
	struct wav_slot *slot = wav_get_slot(ch);
	if (!slot)
		return 0;

	wav_unload_slot(slot);
	return 1;
}

int bgm_unprepare(int ch)
{
	struct bgm_slot *slot = bgm_get_slot(ch);
	if (!slot)
		return 0;

	bgm_unload_slot(slot);
	return 1;
}

int wav_play(int ch)
{
	struct wav_slot *slot = wav_get_slot(ch);
	if (!slot || !slot->chunk)
		return 0;

	if (slot->channel >= 0 && Mix_Paused(slot->channel)) {
		Mix_Resume(slot->channel);
		return 1;
	}
	slot->channel = Mix_PlayChannel(-1, slot->chunk, 0);
	if (slot->channel < 0)
		return 0;

	Mix_SetReverseStereo(slot->channel, slot->reverse);
	return 1;
}

int bgm_play(int ch)
{
	struct bgm_slot *slot = bgm_get_slot(ch);
	if (!slot || !slot->music)
		return 0;

	bgm.playing = slot - bgm.slots;
	return !Mix_PlayMusic(slot->music, -1);
}

int wav_stop(int ch)
{
	struct wav_slot *slot = wav_get_slot(ch);
	if (!slot)
		return 0;

	if (slot->channel >= 0)
		Mix_HaltChannel(slot->channel);
	return 1;
}

int bgm_stop(int ch)
{
	// FIXME: what if channel isn't playing?
	if (bgm.playing == ch) {
		Mix_FadeOutMusic(10);
		bgm.playing = -1;
	}
	return 1;
}

bool wav_is_playing(int ch)
{
	struct wav_slot *slot = wav_get_slot(ch);
	return slot && slot->channel >= 0 && Mix_Playing(slot->channel);
}

bool bgm_is_playing(int ch)
{
	return bgm.playing == ch && Mix_PlayingMusic();
}

int wav_fade(int ch, possibly_unused int time, possibly_unused int volume, bool stop)
{
	// TODO: fade
	if (stop)
		wav_stop(ch);
	return 1;
}

int bgm_fade(int ch, possibly_unused int time, possibly_unused int volume, bool stop)
{
	// TODO: fade
	if (stop)
		bgm_stop(ch);
	return 1;
}

int wav_reverse_LR(int ch)
{
	struct wav_slot *slot = wav_get_slot(ch);
	if (!slot)
		return 0;
	slot->reverse = true;
	if (slot->channel >= 0) {
		Mix_SetReverseStereo(slot->channel, 1);
	}
	return 1;
}

int wav_get_time_length(int ch)
{
	struct wav_slot *slot = wav_get_slot(ch);
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
