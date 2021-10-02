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

#include <stdatomic.h>
#include <assert.h>

#include <sndfile.h>
#include <SDL.h>

#include "system4.h"
#include "system4/archive.h"

#include "asset_manager.h"
#include "audio.h"
#include "mixer.h"
#include "xsystem4.h"

/*
 * The actual mixer implementation is contained in this header.
 * The rest of this file implements a high level interface for managing mixer
 * channels (loading audio, starting/stopping, etc.)
 */
#define STS_MIXER_IMPLEMENTATION
#include "sts_mixer.h"

#define CHUNK_SIZE 1024

struct fade {
	atomic_bool fading;
	bool stop;
	uint_least32_t start_pos;
	uint_least32_t end_pos;
	float start_volume;
	float end_volume;
};

struct channel {
	// archive data
	struct archive_data *dfile;
	int no;

	// audio file data
	SNDFILE *file;
	SF_INFO info;
	sf_count_t offset;

	// stream data
	atomic_int voice;
	sts_mixer_stream_t stream;
	float data[CHUNK_SIZE * 2];

	// main thread read-only
	atomic_uint_least32_t frame;
	atomic_uint volume;

	atomic_bool swapped;
	uint_least32_t loop_start;
	uint_least32_t loop_end;
	atomic_uint loop_count;
	struct fade fade;
};

static SDL_AudioDeviceID audio_device = 0;
static sts_mixer_t mixer;

/*
 * The SDL2 audio callback.
 */
static void audio_callback(possibly_unused void *data, Uint8 *stream, int len)
{
	sts_mixer_mix_audio(&mixer, stream, len / (sizeof(float) * 2));
}

/*
 * Seek to the specified position in the stream.
 * Returns true if the seek succeeded, otherwise returns false.
 */
static bool cb_seek(struct channel *ch, uint_least32_t pos)
{
	if (pos > ch->info.frames) {
		pos = ch->info.frames;
	}
	sf_count_t r = sf_seek(ch->file, pos, SEEK_SET);
	if (r < 0) {
		WARNING("sf_seek failed");
		return false;
	}
	ch->frame = r;
	return true;
}

/*
 * Seek to loop start, if the stream should loop.
 * Returns true if the stream should loop, false if it should stop.
 */
static bool cb_loop(struct channel *ch)
{
	if (!cb_seek(ch, ch->loop_start) || ch->loop_count == 1) {
		ch->voice = -1;
		return false;
	}
	if (ch->loop_count > 1) {
		ch->loop_count--;
	}
	return true;
}

/*
 * Callback to refill the stream's audio data.
 * Called from sys_mixer_mix_audio.
 */
//static int refill_stream(sts_mixer_sample_t *sample, void *data)
static int cb_read_frames(struct channel *ch, float *out, sf_count_t frame_count)
{
	sf_count_t num_read = 0;

	// handle case where chunk crosses loop point (seamless)
	// NOTE: it's assumed that the length of the loop is greater than the chunk length
	if (ch->frame + frame_count >= ch->loop_end) {
		// read frames up to loop_end
		num_read = sf_readf_float(ch->file, out, ch->loop_end - ch->frame);
		// adjust parameters for later
		ch->frame += num_read;
		out += num_read;
		frame_count -= num_read;
		// seek to loop_start
		if (!cb_loop(ch))
			return STS_STREAM_COMPLETE;
	} else if (ch->frame >= ch->loop_end) {
		// seek to loop_start
		if (!cb_loop(ch))
			return STS_STREAM_COMPLETE;
	}

	// read remaining data
	sf_count_t n = sf_readf_float(ch->file, out, frame_count);
	num_read += n;
	ch->frame += n;
	out += num_read;
	frame_count -= num_read;

	// XXX: This *shouldn't* be necessary, but sometimes libsndfile seems to stop reading
	//      just before the end of file (i.e. ch->frame + frame_count is < ch->info.frames,
	//      yet sf_readf_float returns less that the requested amount of frames).
	//      Not sure if this is a bug in xsystem4 or libsndfile
	if (frame_count > 0) {
		if (!cb_loop(ch))
			return STS_STREAM_COMPLETE;
		num_read += sf_readf_float(ch->file, out, frame_count);
	}

	return STS_STREAM_CONTINUE;
}

/*
 * Calculate the gain for a fade at a given sample.
 */
static float cb_calc_fade(struct fade *fade, uint_least32_t sample)
{
	if (sample >= fade->end_pos) {
		return fade->stop ? 0.0 : fade->end_volume;
	}

	float progress = (float)(sample - fade->start_pos) / (float)(fade->end_pos - fade->start_pos);
	float delta_v = fade->end_volume - fade->start_volume;
	float gain = fade->start_volume + delta_v * progress;
	if (gain < 0.0) return 0.0;
	if (gain > 1.0) return 1.0;
	return gain;
}

static int refill_stream(sts_mixer_sample_t *sample, void *data)
{
	struct channel *ch = data;
	memset(ch->data, 0, sizeof(float) * sample->length);

	// read audio data from file
	int r = cb_read_frames(ch, ch->data, CHUNK_SIZE);

	// convert mono to stereo
	if (ch->info.channels == 1) {
		for (int i = CHUNK_SIZE-1; i >= 0; i--) {
			ch->data[i*2+1] = ch->data[i];
			ch->data[i*2] = ch->data[i];
		}
	}

	// reverse LR channels
	else if (ch->swapped) {
		for (int i = 0; i < CHUNK_SIZE; i++) {
			float tmp = ch->data[i*2];
			ch->data[i*2] = ch->data[i*2+1];
			ch->data[i*2+1] = tmp;
		}
	}

	// set gain for fade
	if (ch->fade.fading) {
		float gain = cb_calc_fade(&ch->fade, ch->frame);
		mixer.voices[ch->voice].gain = gain;
		ch->volume = gain * 100.0;

		if (ch->frame >= ch->fade.end_pos) {
			ch->fade.fading = false;
			if (ch->fade.stop) {
				r = STS_STREAM_COMPLETE;
			}
		}
	}

	return r;
}

int channel_play(struct channel *ch)
{
	SDL_LockAudioDevice(audio_device);
	if (ch->voice >= 0) {
		SDL_UnlockAudioDevice(audio_device);
		return 0;
	}
	ch->voice = sts_mixer_play_stream(&mixer, &ch->stream, 1.0f);
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_stop(struct channel *ch)
{
	SDL_LockAudioDevice(audio_device);
	if (ch->voice < 0) {
		SDL_UnlockAudioDevice(audio_device);
		return 0;
	}
	sts_mixer_stop_voice(&mixer, ch->voice);
	ch->voice = -1;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_is_playing(struct channel *ch)
{
	return ch->voice >= 0;
}

int channel_set_loop_count(struct channel *ch, int count)
{
	SDL_LockAudioDevice(audio_device);
	ch->loop_count = count;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_get_loop_count(struct channel *ch)
{
	return ch->loop_count;
}

int channel_set_loop_start_pos(struct channel *ch, int pos)
{
	SDL_LockAudioDevice(audio_device);
	ch->loop_start = pos;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_set_loop_end_pos(struct channel *ch, int pos)
{
	SDL_LockAudioDevice(audio_device);
	ch->loop_end = pos;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_fade(struct channel *ch, int time, int volume, bool stop)
{
	SDL_LockAudioDevice(audio_device);
	ch->fade.fading = true;
	ch->fade.stop = stop;
	ch->fade.start_pos = ch->frame;
	ch->fade.start_volume = (float)ch->volume / 100.0;
	ch->fade.end_pos = ch->fade.start_pos + ((time * ch->info.samplerate) / 1000);
	ch->fade.end_volume = min(1.0, max(0.0, (float)volume / 100.0));
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_stop_fade(struct channel *ch)
{
	SDL_LockAudioDevice(audio_device);
	// XXX: we need to set the volume to end_volume and potentially stop the
	//      stream here; better to let the callback do it
	ch->fade.end_pos = ch->frame;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_is_fading(struct channel *ch)
{
	return ch->fade.fading;
}

int channel_pause(possibly_unused struct channel *ch)
{
	// NOTE: not implemented in Sengoku Rance's SACT2.dll
	WARNING("channel_pause not implemented");
	return 0;
}

int channel_restart(possibly_unused struct channel *ch)
{
	// NOTE: not implemented in Sengoku Rance's SACT2.dll
	WARNING("channel_restart not implemented");
	return 0;
}

int channel_is_paused(possibly_unused struct channel *ch)
{
	return 0;
}

int channel_get_pos(struct channel *ch)
{
	return (ch->frame * 1000) / ch->info.samplerate;
}

int channel_get_length(struct channel *ch)
{
	// FIXME: how is this different than channel_get_time_length?
	return (ch->info.frames * 1000) / ch->info.samplerate;
}

int channel_get_sample_pos(struct channel *ch)
{
	return ch->frame;
}

int channel_get_sample_length(struct channel *ch)
{
	return ch->info.frames;
}

int channel_seek(struct channel *ch, int pos)
{
	// NOTE: SACT2.Music_Seek doesn't seem to do anything in Sengoku Rance...
	SDL_LockAudioDevice(audio_device);
	int r = cb_seek(ch, (pos * ch->info.samplerate) / 1000);
	SDL_UnlockAudioDevice(audio_device);
	return r;
}

int channel_reverse_LR(struct channel *ch)
{
	ch->swapped = !ch->swapped;
	return 1;
}

int channel_get_volume(struct channel *ch)
{
	return ch->volume;
}

int channel_get_time_length(struct channel *ch)
{
	return (ch->info.frames * 1000) / ch->info.samplerate;
}

static sf_count_t channel_vio_get_filelen(void *data)
{
	return ((struct channel*)data)->dfile->size;
}

static sf_count_t channel_vio_seek(sf_count_t offset, int whence, void *data)
{
	struct channel *ch = data;
	switch (whence) {
	case SEEK_CUR:
		ch->offset += offset;
		break;
	case SEEK_SET:
		ch->offset = offset;
		break;
	case SEEK_END:
		ch->offset = ch->dfile->size + offset;
		break;
	}
	ch->offset = min((sf_count_t)ch->dfile->size, max(0, ch->offset));
	return ch->offset;
}

static sf_count_t channel_vio_read(void *ptr, sf_count_t count, void *data)
{
	struct channel *ch = data;
	sf_count_t c = min(count, (sf_count_t)ch->dfile->size - ch->offset);
	memcpy(ptr, ch->dfile->data + ch->offset, c);
	ch->offset += c;
	return c;
}

static sf_count_t channel_vio_write(possibly_unused const void *ptr,
				    possibly_unused sf_count_t count,
				    possibly_unused void *user_data)
{
	ERROR("sndfile vio write not supported");
}

static sf_count_t channel_vio_tell(void *data)
{
	return ((struct channel*)data)->offset;
}

static SF_VIRTUAL_IO channel_vio = {
	.get_filelen = channel_vio_get_filelen,
	.seek = channel_vio_seek,
	.read = channel_vio_read,
	.write = channel_vio_write,
	.tell = channel_vio_tell
};

struct channel *channel_open(enum asset_type type, int no)
{
	struct channel *ch = xcalloc(1, sizeof(struct channel));

	// get file from archive
	ch->dfile = asset_get(type, no-1);
	if (!ch->dfile) {
		WARNING("Failed to load %s %d", type == ASSET_SOUND ? "WAV" : "BGM", no);
		goto error;
	}

	// open file
	ch->file = sf_open_virtual(&channel_vio, SFM_READ, &ch->info, ch);
	if (sf_error(ch->file) != SF_ERR_NO_ERROR) {
		WARNING("Failed to open %s %d: %s", type == ASSET_SOUND ? "WAV" : "BGM", no, sf_strerror(ch->file));
		goto error;
	}
	if (ch->info.channels > 2) {
		WARNING("Audio file has more than 2 channels: %s %d", type == ASSET_SOUND ? "WAV" : "BGM", no);
		goto error;
	}

	// create stream
	ch->stream.userdata = ch;
	ch->stream.callback = refill_stream;
	ch->stream.sample.frequency = ch->info.samplerate;
	ch->stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_FLOAT;
	ch->stream.sample.length = CHUNK_SIZE * 2;
	ch->stream.sample.data = ch->data;
	ch->voice = -1;

	struct bgi *bgi = bgi_get(no);
	if (bgi) {
		ch->volume = min(100, max(0, bgi->volume));
		ch->loop_start = min(ch->info.frames, max(0, bgi->loop_start));
		ch->loop_end = min(ch->info.frames, max(0, bgi->loop_end));
		ch->loop_count = max(0, bgi->loop_count);
	} else {
		ch->volume = 100;
		ch->loop_start = 0;
		ch->loop_end = ch->info.frames;
		ch->loop_count = type == ASSET_SOUND ? 1 : 0;
	}
	ch->no = no;

	return ch;

error:
	if (ch->dfile)
		archive_free_data(ch->dfile);
	free(ch);
	return NULL;
}

void channel_close(struct channel *ch)
{
	channel_stop(ch);
	sf_close(ch->file);
	archive_free_data(ch->dfile);
	free(ch);
}

void mixer_init(void)
{
	SDL_AudioSpec have;
	SDL_AudioSpec want = {
		.format = AUDIO_F32,
		.freq = 44100,
		.channels = 2,
		.samples = CHUNK_SIZE,
		.callback = audio_callback,
	};
	audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	sts_mixer_init(&mixer, 44100, STS_MIXER_SAMPLE_FORMAT_FLOAT);
	if (config.bgi_path)
		bgi_read(config.bgi_path);

	SDL_PauseAudioDevice(audio_device, 0);
}
