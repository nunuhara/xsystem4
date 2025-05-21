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
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>

#include "system4.h"
#include "system4/file.h"

#include "gfx/gfx.h"
#include "movie.h"
#include "mixer.h"
#include "sprite.h"
#include "sts_mixer.h"
#include "xsystem4.h"

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

static Shader movie_shader;

struct movie_context {
	plm_t *plm;
	SDL_mutex *decoder_mutex;

	plm_frame_t *pending_video_frame;
	GLuint textures[3];  // Y, Cb, Cr

	sts_mixer_stream_t sts_stream;
	int voice;
	int volume;

	// Time keeping data. Written by audio handler and referenced by video
	// handler (i.e. we sync the video to the audio).
	double stream_time;  // in seconds
	uint32_t wall_time_ms;
	SDL_mutex *timer_mutex;
};

static int audio_callback(sts_mixer_sample_t *sample, void *data)
{
	struct movie_context *mc = data;
	assert(sample == &mc->sts_stream.sample);

	SDL_LockMutex(mc->decoder_mutex);
	plm_samples_t *frame = plm_decode_audio(mc->plm);
	SDL_UnlockMutex(mc->decoder_mutex);
	if (!frame) {
		sample->length = 0;
		sample->data = NULL;
		mc->voice = -1;
		return STS_STREAM_COMPLETE;
	}
	sample->length = frame->count * 2;
	sample->data = frame->interleaved;

	// Update the timestamp.
	SDL_LockMutex(mc->timer_mutex);
	mc->stream_time = frame->time;
	mc->wall_time_ms = SDL_GetTicks();
	SDL_UnlockMutex(mc->timer_mutex);
	return STS_STREAM_CONTINUE;
}

static void prepare_movie_shader(struct gfx_render_job *job, void *data)
{
	struct movie_context *mc = data;
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mc->textures[0]);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, mc->textures[1]);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, mc->textures[2]);
}

static void load_movie_shader()
{
	gfx_load_shader(&movie_shader, "shaders/render.v.glsl", "shaders/movie.f.glsl");
	glUseProgram(movie_shader.program);
	glUniform1i(glGetUniformLocation(movie_shader.program, "texture_y"), 0);
	glUniform1i(glGetUniformLocation(movie_shader.program, "texture_cb"), 1);
	glUniform1i(glGetUniformLocation(movie_shader.program, "texture_cr"), 2);
	movie_shader.prepare = prepare_movie_shader;
}

static void update_texture(GLuint unit, GLuint texture, plm_plane_t *plane)
{
	glActiveTexture(unit);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, plane->width, plane->height, 0,
	             GL_RED, GL_UNSIGNED_BYTE, plane->data);
}

struct movie_context *movie_load(const char *filename)
{
	struct movie_context *mc = xcalloc(1, sizeof(struct movie_context));
	mc->voice = -1;
	char *path = gamedir_path_icase(filename);
	if (!path) {
		WARNING("%s: file does not exist", filename);
		movie_free(mc);
		return NULL;
	}
	FILE *fp = file_open_utf8(path, "rb");
	if (!fp) {
		WARNING("%s: %s", path, strerror(errno));
		free(path);
		movie_free(mc);
		return NULL;
	}
	mc->plm = plm_create_with_file(fp, TRUE);
	if (!plm_has_headers(mc->plm)) {
		WARNING("%s: not a MPEG-PS file", path);
		free(path);
		movie_free(mc);
		return NULL;
	}
	free(path);

	if (!movie_shader.program)
		load_movie_shader();

	glGenTextures(3, mc->textures);
	for (int i = 0; i < 3; i++) {
		glBindTexture(GL_TEXTURE_2D, mc->textures[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	mc->decoder_mutex = SDL_CreateMutex();
	mc->timer_mutex = SDL_CreateMutex();
	mc->volume = 100;
	return mc;
}

void movie_free(struct movie_context *mc)
{
	if (mc->voice >= 0)
		mixer_stream_stop(mc->voice);

	if (mc->plm)
		plm_destroy(mc->plm);
	if (mc->textures[0])
		glDeleteTextures(3, mc->textures);
	if (mc->decoder_mutex)
		SDL_DestroyMutex(mc->decoder_mutex);
	if (mc->timer_mutex)
		SDL_DestroyMutex(mc->timer_mutex);
	free(mc);
}

bool movie_play(struct movie_context *mc)
{
	// Start the audio stream.
	mc->stream_time = 0.0;
	mc->wall_time_ms = SDL_GetTicks();
	mc->sts_stream.userdata = mc;
	mc->sts_stream.callback = audio_callback;
	mc->sts_stream.sample.frequency = plm_get_samplerate(mc->plm);
	mc->sts_stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_FLOAT;
	mc->voice = mixer_stream_play(&mc->sts_stream, mc->volume);
	return true;
}

bool movie_draw(struct movie_context *mc, struct sact_sprite *sprite)
{
	// Decode a frame, unless we already have one.
	plm_frame_t *frame = mc->pending_video_frame;
	mc->pending_video_frame = NULL;
	if (!frame) {
		SDL_LockMutex(mc->decoder_mutex);
		frame = plm_decode_video(mc->plm);
		SDL_UnlockMutex(mc->decoder_mutex);
		if (!frame)
			return plm_video_has_ended(mc->plm->video_decoder);
	}

	// If the frame's timestamp is in the future, save the frame and return.
	SDL_LockMutex(mc->timer_mutex);
	double now = mc->stream_time + (SDL_GetTicks() - mc->wall_time_ms) / 1000.0;
	SDL_UnlockMutex(mc->timer_mutex);
	if (frame->time > now) {
		mc->pending_video_frame = frame;
		return true;
	}

	// Render the frame.
	update_texture(GL_TEXTURE0, mc->textures[0], &frame->y);
	update_texture(GL_TEXTURE1, mc->textures[1], &frame->cb);
	update_texture(GL_TEXTURE2, mc->textures[2], &frame->cr);

	float w, h;
	GLuint fbo;
	if (sprite) {
		struct texture *texture = sprite_get_texture(sprite);
		w = texture->w;
		h = texture->h;
		fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, texture, 0, 0, w, h);
	} else {
		// Draw directly to the main framebuffer.
		w = config.view_width;
		h = config.view_height;
		gfx_clear();
	}

	mat4 world_transform = WORLD_TRANSFORM(w, h, 0, 0);
	mat4 wv_transform = WV_TRANSFORM(w, h);

	struct gfx_render_job job = {
		.shader = &movie_shader,
		.shape = GFX_RECTANGLE,
		.texture = 0,
		.world_transform = world_transform[0],
		.view_transform = wv_transform[0],
		.data = mc
	};
	gfx_render(&job);

	if (sprite) {
		gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
		sprite_dirty(sprite);
	} else {
		gfx_swap();
	}

	return true;
}

bool movie_is_end(struct movie_context *mc)
{
	return plm_has_ended(mc->plm);
}

int movie_get_position(struct movie_context *mc)
{
	SDL_LockMutex(mc->timer_mutex);
	int ms = mc->wall_time_ms ? mc->stream_time * 1000 + SDL_GetTicks() - mc->wall_time_ms : 0;
	SDL_UnlockMutex(mc->timer_mutex);
	return ms;
}

bool movie_set_volume(struct movie_context *mc, int volume)
{
	mc->volume = volume;
	if (mc->voice >= 0)
		mixer_stream_set_volume(mc->voice, volume);
	return true;
}
