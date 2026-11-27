/* Copyright (C) 2023 kichikuou <KichikuouChrome@gmail.com>
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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/fifo.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include "system4.h"

#include "gfx/gfx.h"
#include "movie.h"
#include "mixer.h"
#include "sprite.h"
#include "sts_mixer.h"
#include "xsystem4.h"

#define PRELOAD_PACKETS 10

struct decoder {
	AVStream *stream;
	AVCodecContext *ctx;
	AVFrame *frame;
	AVFifo *queue;
	bool finished;
};

struct movie_context {
	AVFormatContext *format_ctx;
	bool format_eof;
	SDL_mutex *format_mutex;  // also protects video.queue and audio.queue

	struct decoder video;
	struct decoder audio;

	struct texture temp_surface;

	bool has_pending_video_frame;
	struct SwsContext *sws_ctx;
	AVFrame *sws_frame;
	void *sws_buf;
	int sws_w;
	int sws_h;

	sts_mixer_stream_t sts_stream;
	int bytes_per_sample;
	int voice;
	int volume;
	bool playing;
	bool skipped;

	// Time keeping data. Written by audio handler and referenced by video
	// handler (i.e. we sync the video to the audio).
	double stream_time;  // in seconds
	uint32_t wall_time_ms;
	SDL_mutex *timer_mutex;
};

static void free_decoder(struct decoder *dec)
{
	if (dec->ctx)
		avcodec_free_context(&dec->ctx);
	if (dec->frame)
		av_frame_free(&dec->frame);
	if (dec->queue) {
		while (av_fifo_can_read(dec->queue) > 0) {
			AVPacket *packet;
			av_fifo_read(dec->queue, &packet, 1);
			av_packet_free(&packet);
		}
		av_fifo_freep2(&dec->queue);
	}
}

static bool init_decoder(struct decoder *dec, AVStream *stream)
{
	if (!stream)
		return false;
	dec->stream = stream;
	const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
	if (!codec)
		return false;
	dec->ctx = avcodec_alloc_context3(codec);
	if (avcodec_parameters_to_context(dec->ctx, stream->codecpar) < 0)
		return false;
	if (avcodec_open2(dec->ctx, codec, NULL) != 0)
		return false;
	dec->frame = av_frame_alloc();
	dec->queue = av_fifo_alloc2(0, sizeof(AVPacket*), AV_FIFO_FLAG_AUTO_GROW);
	return true;
}

static void read_packet(struct movie_context *mc)
{
	if (mc->format_eof)
		return;

	AVPacket *packet = av_packet_alloc();
	if (!packet)
		return;

	while (av_read_frame(mc->format_ctx, packet) == 0) {
		if (mc->video.stream && packet->stream_index == mc->video.stream->index) {
			av_fifo_write(mc->video.queue, &packet, 1);
			return;
		} else if (mc->audio.stream && packet->stream_index == mc->audio.stream->index) {
			av_fifo_write(mc->audio.queue, &packet, 1);
			return;
		}
		av_packet_unref(packet);
	}
	av_packet_free(&packet);
	packet = NULL;
	mc->format_eof = true;
	if (mc->video.queue)
		av_fifo_write(mc->video.queue, &packet, 1);
	if (mc->audio.queue)
		av_fifo_write(mc->audio.queue, &packet, 1);
}

static void preload_packets(struct movie_context *mc)
{
	SDL_LockMutex(mc->format_mutex);
	while (!mc->format_eof) {
		size_t queued = (mc->video.queue ? av_fifo_can_read(mc->video.queue) : 0)
			+ (mc->audio.queue ? av_fifo_can_read(mc->audio.queue) : 0);
		if (queued >= PRELOAD_PACKETS)
			break;
		read_packet(mc);
	}
	SDL_UnlockMutex(mc->format_mutex);
}

static bool decode_frame(struct decoder *dec, struct movie_context *mc)
{
	if (!dec->ctx || !dec->queue) {
		dec->finished = true;
		return false;
	}
	int ret;
	while ((ret = avcodec_receive_frame(dec->ctx, dec->frame)) == AVERROR(EAGAIN)) {
		SDL_LockMutex(mc->format_mutex);
		AVPacket *packet = NULL;
		while (av_fifo_can_read(dec->queue) == 0 && !mc->format_eof)
			read_packet(mc);
		if (av_fifo_can_read(dec->queue) == 0) {
			SDL_UnlockMutex(mc->format_mutex);
			dec->finished = true;
			return false;
		}
		av_fifo_read(dec->queue, &packet, 1);
		SDL_UnlockMutex(mc->format_mutex);

		if ((ret = avcodec_send_packet(dec->ctx, packet)) != 0) {
			if (packet)
				av_packet_free(&packet);
			WARNING("avcodec_send_packet failed: %d", ret);
			return false;
		}
		if (packet)
			av_packet_free(&packet);
	}
	if (ret == AVERROR_EOF)
		dec->finished = true;
	else if (ret)
		WARNING("avcodec_receive_frame failed: %d", ret);
	return ret == 0;
}

static int audio_callback(sts_mixer_sample_t *sample, void *data)
{
	struct movie_context *mc = data;
	assert(sample == &mc->sts_stream.sample);

	if (!mc->audio.ctx || mc->audio.ctx->sample_rate <= 0 || !decode_frame(&mc->audio, mc)) {
		free(sample->data);
		sample->length = 0;
		sample->data = NULL;
		mc->voice = -1;
		return STS_STREAM_COMPLETE;
	}
	const int out_channels = 2;
	int in_channels = mc->audio.ctx->ch_layout.nb_channels;
	if (in_channels <= 0) {
		free(sample->data);
		sample->length = 0;
		sample->data = NULL;
		mc->voice = -1;
		return STS_STREAM_COMPLETE;
	}
	unsigned samples = mc->audio.frame->nb_samples;
	if (samples == 0) {
		sample->length = 0;
		return STS_STREAM_COMPLETE;
	}
	if (samples * out_channels != sample->length) {
		free(sample->data);
		sample->length = samples * out_channels;
		sample->data = xmalloc((size_t)sample->length * mc->bytes_per_sample);
	}

	enum AVSampleFormat fmt = mc->audio.ctx->sample_fmt;
	uint8_t *out = sample->data;
	if (fmt == AV_SAMPLE_FMT_U8 || fmt == AV_SAMPLE_FMT_U8P) {
		int16_t *out16 = (int16_t *)out;
		if (av_sample_fmt_is_planar(fmt)) {
			const uint8_t *l = mc->audio.frame->data[0];
			const uint8_t *r = mc->audio.frame->data[in_channels > 1 ? 1 : 0];
			for (unsigned i = 0; i < samples; i++) {
				out16[i * 2 + 0] = (int16_t)(((int)l[i] - 128) << 8);
				out16[i * 2 + 1] = (int16_t)(((int)r[i] - 128) << 8);
			}
		} else {
			const uint8_t *in = mc->audio.frame->data[0];
			int r_ch = in_channels > 1 ? 1 : 0;
			for (unsigned i = 0; i < samples; i++) {
				out16[i * 2 + 0] = (int16_t)(((int)in[i * in_channels + 0] - 128) << 8);
				out16[i * 2 + 1] = (int16_t)(((int)in[i * in_channels + r_ch] - 128) << 8);
			}
		}
	} else if (fmt == AV_SAMPLE_FMT_DBL || fmt == AV_SAMPLE_FMT_DBLP) {
		float *outf = (float *)out;
		if (av_sample_fmt_is_planar(fmt)) {
			const double *l = (const double *)mc->audio.frame->data[0];
			const double *r = (const double *)mc->audio.frame->data[in_channels > 1 ? 1 : 0];
			for (unsigned i = 0; i < samples; i++) {
				outf[i * 2 + 0] = (float)l[i];
				outf[i * 2 + 1] = (float)r[i];
			}
		} else {
			const double *in = (const double *)mc->audio.frame->data[0];
			int r_ch = in_channels > 1 ? 1 : 0;
			for (unsigned i = 0; i < samples; i++) {
				outf[i * 2 + 0] = (float)in[i * in_channels + 0];
				outf[i * 2 + 1] = (float)in[i * in_channels + r_ch];
			}
		}
	} else if (av_sample_fmt_is_planar(fmt)) {
		const uint8_t *l = mc->audio.frame->data[0];
		const uint8_t *r = mc->audio.frame->data[in_channels > 1 ? 1 : 0];
		for (unsigned i = 0; i < samples; i++) {
			memcpy(out, l, mc->bytes_per_sample);
			out += mc->bytes_per_sample;
			l += mc->bytes_per_sample;
			memcpy(out, r, mc->bytes_per_sample);
			out += mc->bytes_per_sample;
			r += mc->bytes_per_sample;
		}
	} else {
		const uint8_t *in = mc->audio.frame->data[0];
		if (in_channels == 2) {
			memcpy(out, in, (size_t)samples * 2 * mc->bytes_per_sample);
		} else {
			int r_ch = in_channels > 1 ? 1 : 0;
			size_t frame_stride = (size_t)in_channels * mc->bytes_per_sample;
			size_t r_off = (size_t)r_ch * mc->bytes_per_sample;
			for (unsigned i = 0; i < samples; i++) {
				memcpy(out, in, mc->bytes_per_sample);
				out += mc->bytes_per_sample;
				memcpy(out, in + r_off, mc->bytes_per_sample);
				out += mc->bytes_per_sample;
				in += frame_stride;
			}
		}
	}

	// Update the timestamp.
	SDL_LockMutex(mc->timer_mutex);
	mc->stream_time += (double)samples / mc->audio.ctx->sample_rate;
	mc->wall_time_ms = SDL_GetTicks();
	SDL_UnlockMutex(mc->timer_mutex);
	return STS_STREAM_CONTINUE;
}

static struct movie_context **active_movies = NULL;
static int nr_active_movies = 0;
static int cap_active_movies = 0;
static uint32_t movie_playback_seq = 0;

static void register_movie(struct movie_context *mc)
{
	if (!mc)
		return;
	for (int i = 0; i < nr_active_movies; i++) {
		if (active_movies[i] == mc)
			return;
	}
	if (nr_active_movies == cap_active_movies) {
		cap_active_movies = cap_active_movies ? cap_active_movies * 2 : 4;
		active_movies = xrealloc(active_movies, sizeof(struct movie_context*) * cap_active_movies);
	}
	active_movies[nr_active_movies++] = mc;
}

static void unregister_movie(struct movie_context *mc)
{
	for (int i = 0; i < nr_active_movies; i++) {
		if (active_movies[i] == mc) {
			active_movies[i] = active_movies[--nr_active_movies];
			return;
		}
	}
}

struct movie_context *movie_load(const char *filename)
{
	if (!filename)
		return NULL;
	struct movie_context *mc = xcalloc(1, sizeof(struct movie_context));
	mc->voice = -1;
	char *path = gamedir_path_icase(filename);
	if (!path) {
		WARNING("%s: file does not exist", filename);
		movie_free(mc);
		return NULL;
	}
	int ret;
	if ((ret = avformat_open_input(&mc->format_ctx, path, NULL, NULL)) != 0) {
		WARNING("%s: avformat_open_input failed: %d", path, ret);
		free(path);
		movie_free(mc);
		return NULL;
	}
	free(path);

	if ((ret = avformat_find_stream_info(mc->format_ctx, NULL)) < 0) {
		WARNING("avformat_find_stream_info failed: %d", ret);
		movie_free(mc);
		return NULL;
	}

	AVStream *video_stream = NULL;
	AVStream *audio_stream = NULL;
	for (int i = 0; i < (int)mc->format_ctx->nb_streams; ++i) {
		switch (mc->format_ctx->streams[i]->codecpar->codec_type) {
		case AVMEDIA_TYPE_VIDEO:
			if (!video_stream)
				video_stream = mc->format_ctx->streams[i];
			break;
		case AVMEDIA_TYPE_AUDIO:
			if (!audio_stream)
				audio_stream = mc->format_ctx->streams[i];
			break;
		default:
			break;
		}
	}
	if (!init_decoder(&mc->video, video_stream)) {
		WARNING("Cannot initialize video decoder");
		movie_free(mc);
		return NULL;
	}
	if (audio_stream) {
		if (!init_decoder(&mc->audio, audio_stream)) {
			WARNING("Cannot initialize audio decoder; continuing without audio");
			free_decoder(&mc->audio);
			memset(&mc->audio, 0, sizeof(mc->audio));
			mc->audio.finished = true;
		}
	} else {
		mc->audio.finished = true;
	}

	NOTICE("video: %d x %d, %s", mc->video.ctx->width, mc->video.ctx->height, av_get_pix_fmt_name(mc->video.ctx->pix_fmt));
	if (mc->audio.ctx) {
		NOTICE("audio: %d hz, %d channels, %s", mc->audio.ctx->sample_rate, mc->audio.ctx->ch_layout.nb_channels, av_get_sample_fmt_name(mc->audio.ctx->sample_fmt));
	} else {
		NOTICE("audio: none");
	}

	mc->format_mutex = SDL_CreateMutex();
	mc->timer_mutex = SDL_CreateMutex();
	mc->volume = 100;

	preload_packets(mc);
	register_movie(mc);
	return mc;
}

void movie_free(struct movie_context *mc)
{
	if (!mc)
		return;
	unregister_movie(mc);
	mc->voice = -1;
	mixer_stream_stop_stream(&mc->sts_stream);
	if (mc->sts_stream.sample.data)
		free(mc->sts_stream.sample.data);

	if (mc->format_ctx)
		avformat_close_input(&mc->format_ctx);
	if (mc->format_mutex)
		SDL_DestroyMutex(mc->format_mutex);
	if (mc->timer_mutex)
		SDL_DestroyMutex(mc->timer_mutex);

	free_decoder(&mc->video);
	free_decoder(&mc->audio);

	if (mc->temp_surface.handle)
		gfx_delete_texture(&mc->temp_surface);
	if (mc->sws_ctx)
		sws_freeContext(mc->sws_ctx);
	if (mc->sws_frame)
		av_frame_free(&mc->sws_frame);
	if (mc->sws_buf)
		av_free(mc->sws_buf);

	free(mc);
}

bool movie_play(struct movie_context *mc)
{
	if (!mc)
		return false;
	mc->voice = -1;
	mixer_stream_stop_stream(&mc->sts_stream);
	mc->skipped = false;
	mc->playing = true;
	movie_playback_seq++;
	mc->stream_time = 0.0;
	mc->wall_time_ms = SDL_GetTicks();
	if (!mc->audio.ctx || mc->audio.ctx->sample_rate <= 0 || mc->audio.ctx->ch_layout.nb_channels <= 0) {
		mc->audio.finished = true;
		mc->voice = -1;
		return true;
	}
	// Start the audio stream.
	mc->sts_stream.userdata = mc;
	mc->sts_stream.callback = audio_callback;
	mc->sts_stream.sample.frequency = mc->audio.ctx->sample_rate;
	switch (mc->audio.ctx->sample_fmt) {
	case AV_SAMPLE_FMT_U8:
	case AV_SAMPLE_FMT_U8P:
	case AV_SAMPLE_FMT_S16:
	case AV_SAMPLE_FMT_S16P:
		mc->sts_stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_16;
		mc->bytes_per_sample = 2;
		break;
	case AV_SAMPLE_FMT_S32:
	case AV_SAMPLE_FMT_S32P:
		mc->sts_stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_32;
		mc->bytes_per_sample = 4;
		break;
	case AV_SAMPLE_FMT_FLT:
	case AV_SAMPLE_FMT_FLTP:
	case AV_SAMPLE_FMT_DBL:
	case AV_SAMPLE_FMT_DBLP:
		mc->sts_stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_FLOAT;
		mc->bytes_per_sample = 4;
		break;
	default:
		WARNING("Unsupported audio format %d; playing silently", mc->audio.ctx->sample_fmt);
		mc->audio.finished = true;
		mc->voice = -1;
		return true;
	}
	mc->voice = mixer_stream_play(&mc->sts_stream, mc->volume);
	return true;
}

bool movie_draw(struct movie_context *mc, struct sact_sprite *sprite)
{
	if (!mc)
		return false;
	if (mc->skipped)
		return sprite != NULL;
	struct texture *texture;
	if (sprite) {
		texture = sprite_get_texture(sprite);
	} else {
		if (!mc->temp_surface.handle)
			gfx_init_texture_blank(&mc->temp_surface, config.view_width, config.view_height);
		texture = &mc->temp_surface;
	}

	if (!mc->video.ctx || mc->video.ctx->width <= 0 || mc->video.ctx->height <= 0 ||
	    !texture || texture->w <= 0 || texture->h <= 0)
		return false;
	if (!mc->sws_ctx || !mc->sws_frame || !mc->sws_buf ||
	    texture->w != mc->sws_w || texture->h != mc->sws_h) {
		if (mc->sws_ctx) {
			sws_freeContext(mc->sws_ctx);
			mc->sws_ctx = NULL;
		}
		if (mc->sws_frame)
			av_frame_free(&mc->sws_frame);
		if (mc->sws_buf) {
			av_free(mc->sws_buf);
			mc->sws_buf = NULL;
		}
		mc->sws_w = 0;
		mc->sws_h = 0;

		mc->sws_ctx = sws_getContext(
			mc->video.ctx->width, mc->video.ctx->height, mc->video.ctx->pix_fmt,
			texture->w, texture->h, AV_PIX_FMT_RGBA, SWS_BICUBIC, NULL, NULL, NULL);
		if (!mc->sws_ctx)
			return false;
		mc->sws_frame = av_frame_alloc();
		int size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, texture->w, texture->h, 1);
		if (!mc->sws_frame || size <= 0) {
			sws_freeContext(mc->sws_ctx);
			mc->sws_ctx = NULL;
			if (mc->sws_frame)
				av_frame_free(&mc->sws_frame);
			return false;
		}
		mc->sws_buf = av_malloc(size);
		if (!mc->sws_buf) {
			sws_freeContext(mc->sws_ctx);
			mc->sws_ctx = NULL;
			av_frame_free(&mc->sws_frame);
			return false;
		}
		av_image_fill_arrays(
			mc->sws_frame->data, mc->sws_frame->linesize,
			mc->sws_buf, AV_PIX_FMT_RGBA, texture->w, texture->h, 1);
		mc->sws_w = texture->w;
		mc->sws_h = texture->h;
	}

	// Decode a frame, unless we already have one.
	if (!mc->has_pending_video_frame && !decode_frame(&mc->video, mc))
		return mc->video.finished;

	// If the frame's timestamp is in the future, save the frame and return.
	int64_t ts = mc->video.frame->best_effort_timestamp;
	double pts = (ts != AV_NOPTS_VALUE) ? av_q2d(mc->video.stream->time_base) * (double)ts : 0.0;
	SDL_LockMutex(mc->timer_mutex);
	double now = mc->stream_time + (SDL_GetTicks() - mc->wall_time_ms) / 1000.0;
	SDL_UnlockMutex(mc->timer_mutex);
	if (pts > now) {
		mc->has_pending_video_frame = true;
		// We have spare time, let's buffer some packets.
		preload_packets(mc);
		return true;
	}

	// Convert to RGB and update the texture.
	sws_scale(mc->sws_ctx, (const uint8_t **)mc->video.frame->data, mc->video.frame->linesize,
		 0, mc->video.ctx->height, mc->sws_frame->data, mc->sws_frame->linesize);
	gfx_update_texture_with_pixels(texture, mc->sws_buf);
	mc->has_pending_video_frame = false;

	if (sprite) {
		sprite_dirty(sprite);
	} else {
		// Draw directly to the main framebuffer.
		gfx_clear();
		gfx_render_texture(&mc->temp_surface, NULL);
		gfx_swap();
	}

	return true;
}

bool movie_is_end(struct movie_context *mc)
{
	if (!mc || mc->skipped)
		return true;
	return mc->video.finished && mc->audio.finished;
}

int movie_get_position(struct movie_context *mc)
{
	if (!mc)
		return 0;
	SDL_LockMutex(mc->timer_mutex);
	int ms = mc->wall_time_ms ? mc->stream_time * 1000 + SDL_GetTicks() - mc->wall_time_ms : 0;
	SDL_UnlockMutex(mc->timer_mutex);
	return ms;
}

bool movie_set_volume(struct movie_context *mc, int volume)
{
	if (!mc)
		return false;
	if (volume < 0)
		volume = 0;
	else if (volume > 100)
		volume = 100;
	mc->volume = volume;
	mixer_stream_set_volume_stream(&mc->sts_stream, volume);
	return true;
}


struct movie_context *movie_load_apeg(const char *filename, int sound_id, int sound_group)
{
	(void)filename; (void)sound_id; (void)sound_group;
	return NULL;
}

bool movie_play_at(struct movie_context *mc, int time_ms)
{
	(void)time_ms;
	return mc ? movie_play(mc) : false;
}

bool movie_set_time(struct movie_context *mc, int time_ms)
{
	(void)mc; (void)time_ms;
	return false;
}

int movie_get_end_time(struct movie_context *mc)
{
	(void)mc;
	return 0;
}

int movie_get_width(struct movie_context *mc)
{
	return mc && mc->video.ctx ? mc->video.ctx->width : 0;
}

int movie_get_height(struct movie_context *mc)
{
	return mc && mc->video.ctx ? mc->video.ctx->height : 0;
}

bool movie_has_active_playback(void)
{
	for (int i = 0; i < nr_active_movies; i++) {
		struct movie_context *mc = active_movies[i];
		if (mc && mc->playing && !mc->skipped && !movie_is_end(mc))
			return true;
	}
	return false;
}

uint32_t movie_get_playback_seq(void)
{
	return movie_playback_seq;
}

bool movie_skip_active(void)
{
	bool skipped_any = false;
	for (int i = 0; i < nr_active_movies; i++) {
		struct movie_context *mc = active_movies[i];
		if (mc && mc->playing && !mc->skipped && !movie_is_end(mc)) {
			mc->skipped = true;
			mc->playing = false;
			mc->voice = -1;
			mixer_stream_stop_stream(&mc->sts_stream);
			skipped_any = true;
		}
	}
	return skipped_any;
}

void movie_end_session(void)
{
	movie_skip_active();
	if (nr_active_movies == 0) {
		free(active_movies);
		active_movies = NULL;
		cap_active_movies = 0;
	}
}
