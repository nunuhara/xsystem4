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
#include <math.h>

#include <sndfile.h>
#include <SDL.h>
#ifdef __ANDROID__
#include <sys/prctl.h>
#include <sys/resource.h>
#endif

#include "system4.h"
#include "system4/archive.h"
#include "system4/utfsjis.h"

#include "asset_manager.h"
#include "audio.h"
#include "audio_curve.h"
#include "mixer.h"
#include "xsystem4.h"

#define clamp(min_value, max_value, value) min(max_value, max(min_value, value))
#define muldiv(x, y, denom) ((int64_t)(x) * (int64_t)(y) / (int64_t)(denom))

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
	bool pending;
	bool stop;
	uint_least32_t start_pos;
	uint_least32_t frames;
	uint_least32_t elapsed;
	float start_volume;
	float end_volume;
	int curve;
};

struct channel {
	// archive data
	struct archive_data *dfile;
	int no;
	int mixer_no;

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
	atomic_bool paused;
	uint_least32_t loop_start;
	uint_least32_t loop_end;
	atomic_uint loop_count;
	struct fade fade;
};

struct mixer {
	sts_mixer_t mixer;
	sts_mixer_stream_t stream;
	int voice;
	atomic_bool muted;
	float data[CHUNK_SIZE * 2];
	char *name;

	struct mixer *parent;
	struct mixer **children;
	int nr_children;
};

static struct mixer *master = NULL;
static struct mixer *mixers = NULL;
static int nr_mixers = 0;

#ifdef ALICEHUB_IOS
static _Atomic SDL_AudioDeviceID audio_device;
static SDL_mutex *audio_mutex;
static SDL_Thread *audio_clock_thread;
static SDL_Thread *audio_device_thread;
static atomic_bool audio_stopping;
static atomic_bool audio_device_stopped;
static atomic_bool retry_device, remove_device;
static atomic_bool output_paused;
static atomic_uint_least64_t last_callback;
static Uint64 clock_tick;
static unsigned clock_fraction;
#else
static SDL_AudioDeviceID audio_device = 0;
#endif
static bool global_focus = true;
static bool window_focused = true;
static bool app_backgrounded = false;

static inline void mixer_sync_pause_state(void)
{
#ifdef ALICEHUB_IOS
	if (audio_mutex)
		SDL_LockMutex(audio_mutex);
	bool paused = app_backgrounded || (!global_focus && !window_focused);
	if (atomic_exchange(&output_paused, paused) != paused) {
		clock_tick = SDL_GetTicks64();
		clock_fraction = 0;
	}
	if (audio_mutex)
		SDL_UnlockMutex(audio_mutex);
#else
	if (audio_device) {
		bool should_pause = app_backgrounded || (!global_focus && !window_focused);
		SDL_PauseAudioDevice(audio_device, should_pause ? 1 : 0);
	}
#endif
}

static inline void audio_lock(void)
{
#ifdef ALICEHUB_IOS
	if (audio_mutex)
		SDL_LockMutex(audio_mutex);
#else
	if (audio_device)
		SDL_LockAudioDevice(audio_device);
#endif
}

static inline void audio_unlock(void)
{
#ifdef ALICEHUB_IOS
	if (audio_mutex)
		SDL_UnlockMutex(audio_mutex);
#else
	if (audio_device)
		SDL_UnlockAudioDevice(audio_device);
#endif
}

static bool audio_clock_available(void)
{
#ifdef ALICEHUB_IOS
	return audio_clock_thread != NULL;
#else
	return audio_device != 0;
#endif
}

void mixer_set_global_focus(bool enabled)
{
	global_focus = enabled;
	mixer_sync_pause_state();
}

void mixer_set_window_focus(bool focused)
{
	window_focused = focused;
	mixer_sync_pause_state();
}

void mixer_set_app_backgrounded(bool backgrounded)
{
	app_backgrounded = backgrounded;
	mixer_sync_pause_state();
}

/*
 * The SDL2 audio callback.
 */
static void audio_callback(possibly_unused void *data, Uint8 *stream, int len)
{
#ifdef ALICEHUB_IOS
	audio_lock();
	if (atomic_load(&audio_stopping)) {
		memset(stream, 0, len);
		audio_unlock();
		return;
	}
	clock_tick = SDL_GetTicks64();
	atomic_store(&last_callback, clock_tick);
	clock_fraction = 0;
	if (atomic_load(&output_paused)) {
		memset(stream, 0, len);
		audio_unlock();
		return;
	}
#endif
#ifdef __ANDROID__
	static _Thread_local bool s_audio_prio_set = false;
	if (!s_audio_prio_set) {
		s_audio_prio_set = true;
		/* ANDROID_PRIORITY_URGENT_AUDIO (-16) prevents mixer underruns during loads */
		setpriority(PRIO_PROCESS, 0, -16);
		prctl(PR_SET_TIMERSLACK, 50000UL, 0, 0, 0);
	}
#endif
	sts_mixer_mix_audio(&master->mixer, stream, len / (sizeof(float) * 2));
	if (master->muted) {
		memset(stream, 0, len);
	}
#ifdef ALICEHUB_IOS
	audio_unlock();
#endif
}

#ifdef ALICEHUB_IOS
static void advance_audio_clock_locked(Uint64 now)
{
	if (now < clock_tick)
		return;
	if (atomic_load(&output_paused)) {
		clock_tick = now;
		clock_fraction = 0;
	} else if (!audio_device || (now >= atomic_load(&last_callback) && now - atomic_load(&last_callback) > 100)) {
		Uint64 elapsed = min(now - clock_tick, 250);
		unsigned frames = (elapsed * 44100 + clock_fraction) / 1000;
		clock_fraction = (elapsed * 44100 + clock_fraction) % 1000;
		clock_tick = now;
		float discard[CHUNK_SIZE * 2];
		while (frames) {
			unsigned count = min(frames, CHUNK_SIZE);
			sts_mixer_mix_audio(&master->mixer, discard, count);
			frames -= count;
		}
	} else {
		clock_tick = now;
		clock_fraction = 0;
	}
}

static void advance_audio_clock(Uint64 now)
{
	audio_lock();
	advance_audio_clock_locked(now);
	audio_unlock();
}

static int audio_clock_main(void *unused)
{
	while (!atomic_load(&audio_stopping)) {
		advance_audio_clock(SDL_GetTicks64());
		SDL_Delay(10);
	}
	return 0;
}
#endif

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
	if (ch->loop_count == 1 || ch->loop_end <= ch->loop_start) {
		return false;
	}
	if (!cb_seek(ch, ch->loop_start)) {
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
static int cb_read_frames(struct channel *ch, float *out, sf_count_t frame_count, uint_least32_t *num_read)
{
	*num_read = 0;

	while (frame_count > 0) {
		bool looping = (ch->loop_count != 1) && (ch->loop_end > ch->loop_start);
		if (looping) {
			if (ch->frame >= ch->loop_end) {
				if (!cb_loop(ch))
					return STS_STREAM_COMPLETE;
				looping = (ch->loop_count != 1) && (ch->loop_end > ch->loop_start);
			}
			if (looping && ch->frame + (uint64_t)frame_count >= ch->loop_end) {
				sf_count_t to_read = (sf_count_t)(ch->loop_end - ch->frame);
				sf_count_t n = to_read > 0 ? sf_readf_float(ch->file, out, to_read) : 0;
				if (n < 0)
					n = 0;
				*num_read += (uint_least32_t)n;
				ch->frame += (uint_least32_t)n;
				out += n * ch->info.channels;
				frame_count -= n;
				if (n < to_read) {
					if (n == 0 && !(ch->frame > ch->loop_start && cb_loop(ch)))
						return STS_STREAM_COMPLETE;
					continue;
				}
				if (!cb_loop(ch))
					return STS_STREAM_COMPLETE;
				continue;
			}
		}

		sf_count_t n = sf_readf_float(ch->file, out, frame_count);
		if (n < 0)
			n = 0;
		*num_read += (uint_least32_t)n;
		ch->frame += (uint_least32_t)n;
		out += n * ch->info.channels;
		frame_count -= n;

		if (n == 0) {
			if (looping && ch->frame > ch->loop_start && cb_loop(ch))
				continue;
			return STS_STREAM_COMPLETE;
		}
	}

	return STS_STREAM_CONTINUE;
}

/*
 * Calculate the gain for a fade at a given sample.
 */
static float cb_calc_fade(struct fade *fade)
{
	if (fade->frames == 0 || fade->elapsed >= fade->frames)
		return fade->stop ? 0.0f : fade->end_volume;

	float progress = audio_fade_curve(fade->curve, (float)fade->elapsed / (float)fade->frames);
	float delta_v = fade->end_volume - fade->start_volume;
	float gain = fade->start_volume + delta_v * progress;
	if (gain < 0.0f) return 0.0f;
	if (gain > 1.0f) return 1.0f;
	return gain;
}

static int refill_stream(sts_mixer_sample_t *sample, void *data)
{
	struct channel *ch = data;
	uint_least32_t frames_read = 0;
	sample->length = CHUNK_SIZE * 2;
	memset(ch->data, 0, sizeof(ch->data));
	if (ch->paused) return STS_STREAM_CONTINUE;
	if (nr_mixers <= 0 || ch->mixer_no < 0 || ch->mixer_no >= nr_mixers || ch->voice < 0) {
		ch->voice = -1;
		return STS_STREAM_COMPLETE;
	}

	// read audio data from file
	int r = cb_read_frames(ch, ch->data, CHUNK_SIZE, &frames_read);

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

	bool fade_stopped = false;
	// set gain for fade
	if (ch->fade.fading) {
		float gain = cb_calc_fade(&ch->fade);
		mixers[ch->mixer_no].mixer.voices[ch->voice].gain = gain;
		ch->volume = gain * 100.0;

		ch->fade.elapsed += frames_read;
		if (ch->fade.elapsed >= ch->fade.frames) {
			ch->fade.fading = false;
			ch->volume = ch->fade.end_volume * 100.0;
			if (ch->fade.stop) {
				cb_seek(ch, 0);
				r = STS_STREAM_COMPLETE;
				fade_stopped = true;
			}
		}
	} else {
		float gain = ch->volume / 100.0;
		mixers[ch->mixer_no].mixer.voices[ch->voice].gain = gain;
	}

	if (r == STS_STREAM_COMPLETE) {
		if (!fade_stopped && frames_read > 0) {
			sample->length = frames_read * 2;
			return STS_STREAM_CONTINUE;
		}
		ch->fade.fading = false;
		ch->fade.pending = false;
		ch->voice = -1;
	}

	return r;
}

static int refill_mixer(sts_mixer_sample_t *sample, void *data)
{
	struct mixer *mixer = data;
	sts_mixer_mix_audio(&mixer->mixer, &mixer->data, CHUNK_SIZE);
	if (mixer->muted) {
		memset(mixer->data, 0, sizeof(float) * sample->length);
	}

	return STS_STREAM_CONTINUE;
}

int channel_play(struct channel *ch)
{
	if (!ch || nr_mixers <= 0 || ch->mixer_no < 0 || ch->mixer_no >= nr_mixers)
		return 0;
	audio_lock();
#ifdef ALICEHUB_IOS
	advance_audio_clock_locked(SDL_GetTicks64());
#endif
	ch->paused = false;
	if (ch->voice >= 0) {
		if (ch->fade.pending && audio_clock_available()) {
			ch->fade.fading = true;
			ch->fade.pending = false;
		}
		audio_unlock();
		return 1;
	}
	ch->stream.sample.length = CHUNK_SIZE * 2;
	memset(ch->data, 0, sizeof(ch->data));
	ch->voice = sts_mixer_play_stream(&mixers[ch->mixer_no].mixer, &ch->stream, (float)ch->volume / 100.0f);
	if (ch->fade.pending) {
		ch->fade.pending = false;
		if (ch->voice >= 0 && audio_clock_available()) {
			ch->fade.start_pos = ch->frame;
			ch->fade.fading = true;
		} else {
			ch->fade.fading = false;
			ch->volume = ch->fade.end_volume * 100.0f;
		}
	}
	bool playing = ch->voice >= 0;
	audio_unlock();
	if (!playing) {
		WARNING("Failed to allocate audio stream in mixer %d", ch->mixer_no);
		return 0;
	}
	return 1;
}

int channel_stop(struct channel *ch)
{
	if (!ch)
		return 0;
	audio_lock();
	ch->paused = false;
	ch->fade.fading = false;
	ch->fade.pending = false;
	if (ch->voice < 0) {
		audio_unlock();
		return 1;
	}
	cb_seek(ch, 0);
	if (nr_mixers > 0 && ch->mixer_no >= 0 && ch->mixer_no < nr_mixers)
		sts_mixer_stop_voice(&mixers[ch->mixer_no].mixer, ch->voice);
	ch->voice = -1;
	audio_unlock();
	return 1;
}

int channel_is_playing(struct channel *ch)
{
	return ch->voice >= 0;
}

int channel_set_loop_count(struct channel *ch, int count)
{
	audio_lock();
	ch->loop_count = count;
	audio_unlock();
	return 1;
}

int channel_get_loop_count(struct channel *ch)
{
	return ch->loop_count;
}

int channel_set_loop_start_pos(struct channel *ch, int pos)
{
	audio_lock();
	sf_count_t max_pos = max((sf_count_t)0, ch->info.frames);
	sf_count_t clamped = min(max_pos, max((sf_count_t)0, (sf_count_t)pos));
	ch->loop_start = (uint_least32_t)clamped;
	audio_unlock();
	return 1;
}

int channel_set_loop_end_pos(struct channel *ch, int pos)
{
	audio_lock();
	sf_count_t max_pos = max((sf_count_t)0, ch->info.frames);
	sf_count_t clamped = min(max_pos, max((sf_count_t)0, (sf_count_t)pos));
	ch->loop_end = (uint_least32_t)clamped;
	audio_unlock();
	return 1;
}

int channel_fade_with_curve(struct channel *ch, int time, int volume, bool stop, int curve)
{
	if (!ch)
		return 0;
	if (time <= 0 && stop)
		return channel_stop(ch);

	audio_lock();
#ifdef ALICEHUB_IOS
	advance_audio_clock_locked(SDL_GetTicks64());
#endif
	if (time <= 0 || ch->info.samplerate <= 0) {
		// XXX: Fade with time=0 is used to set volume. This needs to
		//      take effect immediately, not via the audio callback
		//      because the stream isn't necessarily playing yet.
		//      E.g. the below call sequence should fade from 0 -> 33:
		//
		//          SACT2.Music_Fade(ch, 0, 0, 0);
		//          SACT2.Music_Fade(ch, 33, 4000, 0);
		//          SACT2.Music_Play(ch);
		ch->fade.fading = false;
		ch->fade.pending = false;
		ch->volume = max(0, min(100, volume));
		if (ch->voice >= 0 && nr_mixers > 0 && ch->mixer_no >= 0 && ch->mixer_no < nr_mixers) {
			mixers[ch->mixer_no].mixer.voices[ch->voice].gain = (float)ch->volume / 100.0f;
		}
	} else {
		ch->fade.stop = stop;
		ch->fade.start_pos = ch->frame;
		ch->fade.start_volume = (float)ch->volume / 100.0f;
		ch->fade.frames = muldiv(time, ch->info.samplerate, 1000);
		ch->fade.elapsed = 0;
		ch->fade.end_volume = clamp(0.0f, 1.0f, (float)volume / 100.0f);
		ch->fade.curve = curve;
		if (ch->voice >= 0 && !ch->paused && audio_clock_available() && ch->fade.frames > 0) {
			ch->fade.fading = true;
			ch->fade.pending = false;
		} else if (!stop && ch->voice < 0 && audio_clock_available() && ch->fade.frames > 0) {
			// Armed pre-play fade-in: starts when channel_play() is called,
			// without reporting fading=true while idle.
			ch->fade.fading = false;
			ch->fade.pending = true;
		} else {
			ch->fade.fading = false;
			ch->fade.pending = false;
			ch->volume = max(0, min(100, volume));
			if (stop) {
				if (ch->voice >= 0 && nr_mixers > 0 && ch->mixer_no >= 0 && ch->mixer_no < nr_mixers)
					sts_mixer_stop_voice(&mixers[ch->mixer_no].mixer, ch->voice);
				ch->voice = -1;
				ch->paused = false;
				cb_seek(ch, 0);
			}
		}
	}
	audio_unlock();
	return 1;
}

int channel_fade(struct channel *ch, int time, int volume, bool stop)
{
	return channel_fade_with_curve(ch, time, volume, stop, 0);
}

int channel_stop_fade(struct channel *ch)
{
	if (!ch)
		return 0;
	audio_lock();
	ch->fade.pending = false;
	if (ch->fade.fading) {
		ch->fade.fading = false;
		ch->fade.elapsed = ch->fade.frames;
		ch->volume = ch->fade.end_volume * 100.0f;
		if (ch->fade.stop) {
			cb_seek(ch, 0);
			if (ch->voice >= 0 && nr_mixers > 0 && ch->mixer_no >= 0 && ch->mixer_no < nr_mixers)
				sts_mixer_stop_voice(&mixers[ch->mixer_no].mixer, ch->voice);
			ch->voice = -1;
			ch->paused = false;
		} else if (ch->voice >= 0 && nr_mixers > 0 && ch->mixer_no >= 0 && ch->mixer_no < nr_mixers) {
			mixers[ch->mixer_no].mixer.voices[ch->voice].gain = ch->fade.end_volume;
		}
	}
	audio_unlock();
	return 1;
}

int channel_is_fading(struct channel *ch)
{
	return ch->fade.fading;
}

int channel_pause(struct channel *ch)
{
	audio_lock();
	ch->paused = ch->voice >= 0;
	audio_unlock();
	return ch->paused;
}

int channel_restart(struct channel *ch)
{
	ch->paused = false;
	return ch->voice >= 0;
}

int channel_is_paused(struct channel *ch)
{
	return ch->paused;
}

int channel_get_group(struct channel *ch) { return ch->mixer_no; }

bool channel_set_group(struct channel *ch, int group)
{
	if (group < 0 || group >= nr_mixers || ch->voice >= 0 ||
			(config.mixer_nr_channels && (unsigned)group >= config.mixer_nr_channels))
		return false;
	ch->mixer_no = group;
	return true;
}

int channel_get_pos(struct channel *ch)
{
	if (!ch || ch->info.samplerate <= 0)
		return 0;
	return muldiv(ch->frame, 1000, ch->info.samplerate);
}

int channel_get_length(struct channel *ch)
{
	if (!ch || ch->info.samplerate <= 0)
		return 0;
	// FIXME: how is this different than channel_get_time_length?
	return muldiv(ch->info.frames, 1000, ch->info.samplerate);
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
	if (!ch || ch->info.samplerate <= 0)
		return 0;
	// NOTE: SACT2.Music_Seek doesn't seem to do anything in Sengoku Rance...
	audio_lock();
	int r = cb_seek(ch, muldiv(max(0, pos), ch->info.samplerate, 1000));
	audio_unlock();
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
	if (!ch || ch->info.samplerate <= 0)
		return 0;
	return muldiv(ch->info.frames, 1000, ch->info.samplerate);
}

int channel_get_data_no(struct channel *ch)
{
	return ch->dfile ? ch->dfile->no : -1;
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
	ch->offset = clamp(0, (sf_count_t)ch->dfile->size, ch->offset);
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
	// get file from archive
	struct archive_data *dfile = asset_get(type, no);
	if (!dfile) {
		WARNING("Failed to load %s %d", type == ASSET_SOUND ? "WAV" : "BGM", no);
		return NULL;
	}

	struct channel *ch = channel_open_archive_data(dfile, type, no);
	if (!ch) {
		WARNING("Failed to open %s %d", type == ASSET_SOUND ? "WAV" : "BGM", no);
		return NULL;
	}

	return ch;
}

static bool init_channel(struct channel *ch)
{
	if (!ch->file || sf_error(ch->file) != SF_ERR_NO_ERROR) {
		WARNING("sf_open_virtual failed: %s", sf_strerror(ch->file));
		return false;
	}
	if (ch->info.samplerate <= 0 || ch->info.channels <= 0 || ch->info.channels > 2) {
		WARNING("Invalid audio format (samplerate=%d, channels=%d)", ch->info.samplerate, ch->info.channels);
		return false;
	}

	// create stream
	ch->stream.userdata = ch;
	ch->stream.callback = refill_stream;
	ch->stream.sample.frequency = ch->info.samplerate;
	ch->stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_FLOAT;
	ch->stream.sample.length = CHUNK_SIZE * 2;
	ch->stream.sample.data = ch->data;
	ch->voice = -1;

	ch->volume = 100;
	ch->loop_start = 0;
	ch->loop_end = max(0, ch->info.frames);
	ch->loop_count = 1;
	ch->mixer_no = 0;

	ch->no = -1;
	return true;
}

struct channel *channel_open_archive_data(struct archive_data *dfile,
					  enum asset_type type, int metadata_no)
{
	struct channel *ch = xcalloc(1, sizeof(struct channel));
	ch->dfile = dfile;

	// open file
	ch->file = sf_open_virtual(&channel_vio, SFM_READ, &ch->info, ch);

	if (!init_channel(ch)) {
		archive_free_data(dfile);
		if (ch->file)
			sf_close(ch->file);
		free(ch);
		return NULL;
	}
	int max_mixer = nr_mixers > 0 ? nr_mixers - 1 : 0;
	if (metadata_no >= 0) {
		if (type == ASSET_SOUND) {
			struct wai *wai = wai_get(metadata_no);
			ch->mixer_no = clamp(0, max_mixer, wai ? wai->channel : 1);
		} else if (type == ASSET_BGM) {
			struct bgi *bgi = bgi_get(metadata_no);
			if (bgi) {
				ch->volume = clamp(0, 100, bgi->volume);
				ch->loop_start = clamp(0, max(0, ch->info.frames), bgi->loop_start);
				ch->loop_end = clamp(0, max(0, ch->info.frames), bgi->loop_end);
				ch->loop_count = max(0, bgi->loop_count);
				ch->mixer_no = clamp(0, max_mixer, bgi->channel);
			} else {
				ch->loop_count = 0;
				ch->mixer_no = clamp(0, max_mixer, 0);
			}
		}
		ch->no = metadata_no;
	}
	return ch;
}

struct channel *channel_open_file(const char *path)
{
	struct channel *ch = xcalloc(1, sizeof(struct channel));
#ifdef _WIN32
	wchar_t *wpath = utf8_to_wchar(path);
	ch->file = sf_wchar_open(wpath, SFM_READ, &ch->info);
	free(wpath);
#else
	ch->file = sf_open(path, SFM_READ, &ch->info);
#endif

	if (!init_channel(ch)) {
		if (ch->file)
			sf_close(ch->file);
		free(ch);
		return NULL;
	}
	return ch;
}

void channel_close(struct channel *ch)
{
	channel_stop(ch);
	sf_close(ch->file);
	if (ch->dfile)
		archive_free_data(ch->dfile);
	free(ch);
}

#define SJIS_MASTER "\x83\x7d\x83\x58\x83\x5e\x81\x5b"
#define SJIS_VOICE  "\x89\xb9\x90\xba"

static void open_default_audio_device(void)
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
	if (audio_device == 0) {
#ifdef ALICEHUB_IOS
		const char *error = SDL_GetError();
		const char *driver = SDL_GetCurrentAudioDriver();
		WARNING("SDL_OpenAudioDevice failed (driver=%s, device=default): %s",
			driver ? driver : "none", error);
#else
		WARNING("SDL_OpenAudioDevice failed: %s", SDL_GetError());
#endif
		return;
	}
#ifdef ALICEHUB_IOS
	const char *driver = SDL_GetCurrentAudioDriver();
	NOTICE("Audio opened (driver=%s, device=default, id=%u, %d Hz, format=0x%x, channels=%u)",
		driver ? driver : "none", audio_device, have.freq, have.format, have.channels);
#endif
#ifdef ALICEHUB_IOS
	SDL_PauseAudioDevice(audio_device, 0);
#else
	SDL_PauseAudioDevice(audio_device, (app_backgrounded || (!global_focus && !window_focused)) ? 1 : 0);
#endif
}

#ifdef ALICEHUB_IOS
static void update_audio_device(void)
{
	if (atomic_exchange(&remove_device, false) && audio_device) {
		SDL_AudioDeviceID dev = atomic_exchange(&audio_device, 0);
		SDL_CloseAudioDevice(dev);
	}
	if (!atomic_exchange(&retry_device, false))
		return;
	Uint64 callback_time = atomic_load(&last_callback);
	if (audio_device && SDL_GetTicks64() - callback_time > 500) {
		SDL_AudioDeviceID dev = atomic_exchange(&audio_device, 0);
		SDL_CloseAudioDevice(dev);
	}
	if (!audio_device)
		open_default_audio_device();
}

static void service_audio_device(void)
{
	if (!atomic_load(&audio_stopping))
		update_audio_device();
}

static int audio_device_main(void *unused)
{
	while (!atomic_load(&audio_stopping)) {
		service_audio_device();
		SDL_Delay(20);
	}
	SDL_AudioDeviceID dev = atomic_exchange(&audio_device, 0);
	if (dev)
		SDL_CloseAudioDevice(dev);
	atomic_store(&audio_device_stopped, true);
	return 0;
}

void mixer_retry_audio_device(void)
{
	if (audio_clock_thread && !atomic_load(&audio_stopping))
		atomic_store(&retry_device, true);
}

void audio_metadata_reset(void);

bool mixer_fini(void)
{
	atomic_store(&audio_stopping, true);
	if (audio_clock_thread) {
		SDL_WaitThread(audio_clock_thread, NULL);
		audio_clock_thread = NULL;
	}
	// Drain a callback that entered before audio_stopping was set.
	audio_lock();
	audio_unlock();
	for (int i = 0; i < nr_mixers; i++) {
		free(mixers[i].name);
		free(mixers[i].children);
	}
	free(mixers);
	mixers = master = NULL;
	nr_mixers = 0;
	atomic_store(&retry_device, false);
	atomic_store(&remove_device, false);
	audio_metadata_reset();
	if (!audio_device_thread) {
		SDL_DestroyMutex(audio_mutex);
		audio_mutex = NULL;
	}
	return audio_device_thread == NULL;
}

bool alicehub_ios_finish_audio_shutdown(void)
{
	if (!audio_device_thread)
		return true;
	if (!atomic_load(&audio_device_stopped))
		return false;
	SDL_WaitThread(audio_device_thread, NULL);
	audio_device_thread = NULL;
	SDL_DestroyMutex(audio_mutex);
	audio_mutex = NULL;
	SDL_Quit();
	return true;
}
#endif


void mixer_handle_device_removed(uint32_t dev)
{
	if (!master)
		return;
#ifdef ALICEHUB_IOS
	if (audio_device && dev == audio_device) {
		atomic_store(&remove_device, true);
		atomic_store(&retry_device, true);
	}
#else
	if (audio_device != 0 && dev == (uint32_t)audio_device) {
		SDL_CloseAudioDevice(audio_device);
		audio_device = 0;
		open_default_audio_device();
	}
#endif
}

void mixer_init(void)
{
	// initialize mixer naming
	if (!config.mixer_nr_channels) {
		nr_mixers = 3;
		mixers = xcalloc(nr_mixers, sizeof(struct mixer));
		mixers[0].name = strdup("Music");
		mixers[1].name = strdup("Sound");
		mixers[2].name = strdup("Master");
		master = &mixers[2];
	} else {
		// NOTE: Older games don't have an explicit master channel.
		//       We add an extra mixer in this case for consistency.
		int need_master = 1;
		static const char gbk_master[] = "\xd6\xf7\xd2\xf4\xc1\xbf"; /* 主音量 */
		if (game_night_remaster
				|| !strcmp(config.mixer_channels[0], "Master")
				|| !strcmp(config.mixer_channels[0], SJIS_MASTER)
				|| !strcmp(config.mixer_channels[0], gbk_master)) {
			need_master = 0;
		}
		nr_mixers = config.mixer_nr_channels + need_master;
		mixers = xcalloc(nr_mixers, sizeof(struct mixer));
		for (unsigned i = 0; i < config.mixer_nr_channels; i++) {
			mixers[i].name = strdup(config.mixer_channels[i]);
		}
		if (need_master) {
			mixers[nr_mixers-1].name = strdup("Master");
			master = &mixers[nr_mixers-1];
		} else {
			master = &mixers[0];
		}
	}

	// initialize mixer hierarchy
	// NOTE: "Master" and "Voice" are special nodes; all channels following
	//       them in the list become their children. This appears to be
	//       hardcoded behavior in system 4.
	struct mixer *parent = master;
	bool split_voice_mixers = game_escalayer_reboot || game_tsumamigui3;
	int first_back_voice = game_escalayer_reboot ? 12 : 10;
	for (int i = 0; i < nr_mixers; i++) {
		if (&mixers[i] == master)
			continue;
		struct mixer *node_parent = parent;
		if (split_voice_mixers) {
			// Groups 3 and 5 independently control foreground and background voices.
			node_parent = i >= first_back_voice ? &mixers[5] : i >= 6 ? &mixers[3] : master;
		} else if (game_rance03) {
			// Rance 03 CGroupTree::GetParentGroup: groups 1..5 attach to Master (0),
			// while VoiceMale (6) and VoiceFemale (7) attach to Voice (3).
			node_parent = (i >= 6 && nr_mixers > 3) ? &mixers[3] : master;
		}
		mixers[i].parent = node_parent;
		node_parent->children = xrealloc_array(node_parent->children, node_parent->nr_children, node_parent->nr_children+1, sizeof(struct mixer*));
		node_parent->children[node_parent->nr_children++] = &mixers[i];
		if (!split_voice_mixers && !game_rance10 && !game_dohnadohna && !game_evenicle2 && !game_ixseal && !game_rance03 && !game_okusama && !game_haharanman && !game_night_remaster && !game_tsumamigui3_jun && !game_tsumamigui3_ntr && !game_maman && (!strcmp(mixers[i].name, "Voice") || !strcmp(mixers[i].name, SJIS_VOICE))) {
			parent = &mixers[i];
		}
	}

	// initialize mixers
	for (int i = 0; i < nr_mixers; i++) {
		sts_mixer_init(&mixers[i].mixer, 44100, STS_MIXER_SAMPLE_FORMAT_FLOAT);
		int volume = i < (int)config.mixer_nr_channels ? config.mixer_volumes[i] : config.default_volume;
		mixers[i].mixer.gain = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	}

	// initialize mixer streams
	for (int i = 0; i < nr_mixers; i++) {
		if (&mixers[i] == master)
			continue;
		mixers[i].stream.userdata = &mixers[i];
		mixers[i].stream.callback = refill_mixer;
		mixers[i].stream.sample.frequency = 44100;
		mixers[i].stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_FLOAT;
		mixers[i].stream.sample.length = CHUNK_SIZE * 2;
		mixers[i].stream.sample.data = mixers[i].data;
		mixers[i].voice = sts_mixer_play_stream(&mixers[i].parent->mixer, &mixers[i].stream, 1.0f);
		if (mixers[i].voice < 0)
			ERROR("Failed to link audio mixer %d (%s) to parent %td (%s): stream capacity %d exhausted",
				i, mixers[i].name, mixers[i].parent - mixers, mixers[i].parent->name, STS_MIXER_VOICES);
	}

	// read audio metadata
	if (config.bgi_path)
		bgi_read(config.bgi_path);
	if (config.wai_path)
		wai_load(config.wai_path);

	// initialize SDL audio
#ifdef ALICEHUB_IOS
	atomic_store(&audio_stopping, false);
	app_backgrounded = false;
	window_focused = true;
	global_focus = true;
	clock_fraction = 0;
	if (!(audio_mutex = SDL_CreateMutex()))
		ERROR("SDL_CreateMutex: %s", SDL_GetError());
	clock_tick = SDL_GetTicks64();
	atomic_store(&last_callback, clock_tick);
	mixer_sync_pause_state();
	audio_clock_thread = SDL_CreateThread(audio_clock_main, "mixer-clock", NULL);
	if (!audio_clock_thread)
		ERROR("SDL_CreateThread(mixer-clock): %s", SDL_GetError());
	atomic_store(&retry_device, true);
	atomic_store(&audio_device_stopped, false);
	audio_device_thread = SDL_CreateThread(audio_device_main, "mixer-device", NULL);
	if (!audio_device_thread)
		ERROR("SDL_CreateThread(mixer-device): %s", SDL_GetError());
	NOTICE("Audio software clock ready (44100 Hz); opening output asynchronously");
#else
	open_default_audio_device();
#endif
}

int mixer_get_stream_capacity(void)
{
	return STS_MIXER_VOICES;
}

int mixer_get_numof(void)
{
	// Return the number of mixers specified in System40.ini, even if
	// xsystem4 added more mixers.
	return config.mixer_nr_channels;
}

const char *mixer_get_name(int n)
{
	if (!mixers || n < 0 || n >= (int)config.mixer_nr_channels || n >= nr_mixers)
		return NULL;
	return mixers[n].name;
}

int mixer_set_name(int n, const char *name)
{
	if (!mixers || !name || n < 0 || n >= (int)config.mixer_nr_channels || n >= nr_mixers)
		return 0;
	free(mixers[n].name);
	mixers[n].name = strdup(name);
	return 1;
}

int mixer_get_volume(int n, int *volume)
{
	if (!mixers || !volume || n < 0 || n >= (int)config.mixer_nr_channels || n >= nr_mixers) {
		return 0;
	}
	audio_lock();
	*volume = clamp(0, 100, (int)lroundf(mixers[n].mixer.gain * 100.0f));
	audio_unlock();
	return 1;
}

int mixer_set_volume(int n, int volume)
{
	if (!mixers || n < 0 || n >= (int)config.mixer_nr_channels || n >= nr_mixers)
		return 0;
	audio_lock();
	mixers[n].mixer.gain = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	audio_unlock();
	return 1;
}
int mixer_get_mute(int n, int *mute)
{
	if (!mixers || !mute || n < 0 || n >= (int)config.mixer_nr_channels || n >= nr_mixers)
		return 0;
	*mute = mixers[n].muted;
	return 1;
}

int mixer_set_mute(int n, int mute)
{
	if (!mixers || n < 0 || n >= (int)config.mixer_nr_channels || n >= nr_mixers)
		return 0;
	mixers[n].muted = !!mute;
	return 1;
}

int mixer_stream_play(sts_mixer_stream_t* stream, int volume)
{
	if (!master || !stream)
		return -1;
	audio_lock();
	float gain = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	int voice = sts_mixer_play_stream(&master->mixer, stream, gain);
	audio_unlock();
	return voice;
}

bool mixer_stream_set_volume(int voice, int volume)
{
	if (!master || voice < 0 || voice >= STS_MIXER_VOICES)
		return false;
	audio_lock();
	master->mixer.voices[voice].gain = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	audio_unlock();
	return true;
}

bool mixer_stream_set_volume_stream(sts_mixer_stream_t *stream, int volume)
{
	if (!master || !stream)
		return false;
	float gain = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	bool updated = false;
	audio_lock();
	for (int i = 0; i < STS_MIXER_VOICES; i++) {
		if (master->mixer.voices[i].state == STS_MIXER_VOICE_STREAMING &&
		    master->mixer.voices[i].stream == stream) {
			master->mixer.voices[i].gain = gain;
			updated = true;
		}
	}
	audio_unlock();
	return updated;
}

void mixer_stream_stop(int voice)
{
	if (!master || voice < 0 || voice >= STS_MIXER_VOICES)
		return;
	audio_lock();
	sts_mixer_stop_voice(&master->mixer, voice);
	audio_unlock();
}

void mixer_stream_stop_stream(sts_mixer_stream_t *stream)
{
	if (!master || !stream)
		return;
	audio_lock();
	sts_mixer_stop_stream(&master->mixer, stream);
	audio_unlock();
}
