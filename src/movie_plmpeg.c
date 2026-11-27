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
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "system4.h"
#include "system4/file.h"
#include "system4/little_endian.h"

#include "gfx/gfx.h"
#include "kiwi.h"
#include "movie.h"
#include "mixer.h"
#include "sprite.h"
#include "sts_mixer.h"
#include "xsystem4.h"

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#include "apeg_vlc.h"

static Shader movie_shader;

struct apeg_keyframe {
	uint32_t time_ms;
	uint32_t file_offset;
};

struct apeg_context {
	FILE *fp;
	int width;
	int height;
	int end_time_ms;
	uint8_t intra_quant_matrix[64];
	uint8_t non_intra_quant_matrix[64];
	uint32_t keyframe_count;
	struct apeg_keyframe *keyframes;
	uint32_t first_chunk_offset;

	int sound_id;
	int sound_group;
	bool has_audio;
	bool has_sond_chunk;

	plm_buffer_t *bitbuf;
	plm_video_t *vdec;
	uint8_t *chunk_buf;
	size_t chunk_cap;

	bool has_ref;
	bool has_both_refs;
	bool eof;
	bool playing;
	bool has_manual_time;
	bool has_rendered_frame;
	int start_time_ms;
	uint32_t start_ticks_ms;
	int current_time_ms;
	int seeked_time_ms;
};

struct movie_context {
	plm_t *plm;
	struct apeg_context *apeg;
	SDL_mutex *decoder_mutex;

	plm_frame_t *pending_video_frame;
	GLuint textures[3];  // Y, Cb, Cr

	sts_mixer_stream_t sts_stream;
	int voice;
	int volume;

	bool plm_playing;
	bool skipped;
	bool unsupported;

	// Time keeping data. Written by audio handler and referenced by video
	// handler (i.e. we sync the video to the audio).
	double stream_time;  // in seconds
	uint32_t wall_time_ms;
	SDL_mutex *timer_mutex;
};

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

static bool movie_context_is_playing(struct movie_context *mc)
{
	if (!mc || mc->unsupported || mc->skipped)
		return false;
	if (mc->apeg)
		return mc->apeg->playing && !movie_is_end(mc);
	return mc->plm_playing && !movie_is_end(mc);
}

static void apeg_decode_block(plm_video_t *self, int block)
{
	int n = 0;
	uint8_t *quant_matrix;
	int quantizer_scale = self->quantizer_scale;

	if (self->macroblock_intra) {
		int predictor;
		int plane_index = (block > 3) ? (block - 3) : 0;
		predictor = self->dc_predictor[plane_index];

		int size = plm_buffer_read_vlc(self->buffer,
			block < 4 ? APEG_VLC_DC_LUMA : APEG_VLC_DC_CHROMA);
		if (size < 0) {
			memset(self->block_data, 0, sizeof(self->block_data));
			return;
		}

		if (size > 0) {
			int val = plm_buffer_read(self->buffer, size);
			if ((val & (1 << (size - 1))) == 0) {
				val -= (1 << size) - 1;
			}
			predictor += val;
			self->dc_predictor[plane_index] = predictor;
		}
		self->block_data[0] = predictor << (3 + 5);
		quant_matrix = self->intra_quant_matrix;
		n = 1;
	} else {
		quant_matrix = self->non_intra_quant_matrix;
	}

	int coeff = 0;
	while (TRUE) {
		int run_coeff = plm_buffer_read_vlc(self->buffer,
			n == 0 ? APEG_VLC_DCT_FIRST : APEG_VLC_DCT_NEXT);
		if (run_coeff == -3) {
			break;
		}
		if (run_coeff == -4) {
			memset(self->block_data, 0, sizeof(self->block_data));
			return;
		}

		int run;
		if (run_coeff == -2) {
			run = plm_buffer_read(self->buffer, 6);
			coeff = plm_buffer_read(self->buffer, 8);
			if (coeff == 0) {
				coeff = plm_buffer_read(self->buffer, 8);
			} else if (coeff == 128) {
				coeff = plm_buffer_read(self->buffer, 8) - 256;
			} else if (coeff > 128) {
				coeff -= 256;
			}
		} else {
			if (run_coeff == -1) {
				run = 0;
				coeff = 1;
			} else {
				run = run_coeff >> 8;
				coeff = run_coeff & 0xff;
			}
			if (plm_buffer_read(self->buffer, 1)) {
				coeff = -coeff;
			}
		}

		n += run;
		if (n < 0 || n >= 64) {
			break;
		}

		int de_zig_zagged = PLM_VIDEO_ZIG_ZAG[n];
		n++;

		coeff <<= 1;
		if (!self->macroblock_intra) {
			coeff += (coeff < 0) ? -1 : 1;
		}
		coeff = (coeff * quantizer_scale * quant_matrix[de_zig_zagged]) >> 4;
		if ((coeff & 1) == 0) {
			coeff -= coeff > 0 ? 1 : -1;
		}
		if (coeff > 2047) {
			coeff = 2047;
		} else if (coeff < -2048) {
			coeff = -2048;
		}

		self->block_data[de_zig_zagged] = coeff * PLM_VIDEO_PREMULTIPLIER_MATRIX[de_zig_zagged];
	}

	if (n == 1) {
		int val = (self->block_data[0] + 128) >> 8;
		int clamped = plm_clamp(val);
		uint8_t *d;
		int scan;
		if (block < 4) {
			scan = self->luma_width - 8;
			d = self->frame_current.y.data
				+ ((self->mb_row << 4) + ((block & 2) << 2)) * self->luma_width
				+ (self->mb_col << 4) + ((block & 1) << 3);
		} else {
			scan = self->chroma_width - 8;
			uint8_t *plane = (block == 4) ? self->frame_current.cb.data : self->frame_current.cr.data;
			d = plane + (self->mb_row << 3) * self->chroma_width + (self->mb_col << 3);
		}
		for (int sy = 0; sy < 8; sy++) {
			for (int sx = 0; sx < 8; sx++) {
				if (self->macroblock_intra) {
					*d = clamped;
				} else {
					int s = *d + val;
					*d = plm_clamp(s);
				}
				d++;
			}
			d += scan;
		}
		self->block_data[0] = 0;
		return;
	}

	plm_video_idct(self->block_data);

	uint8_t *d;
	int scan;
	if (block < 4) {
		scan = self->luma_width - 8;
		d = self->frame_current.y.data
			+ ((self->mb_row << 4) + ((block & 2) << 2)) * self->luma_width
			+ (self->mb_col << 4) + ((block & 1) << 3);
	} else {
		scan = self->chroma_width - 8;
		uint8_t *plane = (block == 4) ? self->frame_current.cb.data : self->frame_current.cr.data;
		d = plane + (self->mb_row << 3) * self->chroma_width + (self->mb_col << 3);
	}

	int *s = self->block_data;
	if (self->macroblock_intra) {
		for (int sy = 0; sy < 8; sy++) {
			for (int sx = 0; sx < 8; sx++) {
				*d++ = plm_clamp(*s);
				*s++ = 0;
			}
			d += scan;
		}
	} else {
		for (int sy = 0; sy < 8; sy++) {
			for (int sx = 0; sx < 8; sx++) {
				int val = *d + *s;
				*d++ = plm_clamp(val);
				*s++ = 0;
			}
			d += scan;
		}
	}
}

static int apeg_decode_motion_vector(plm_buffer_t *buf, int r_size, int motion)
{
	int m_code = plm_buffer_read_vlc(buf, APEG_VLC_MOTION);
	if (m_code <= 0) {
		return motion;
	}
	int sign = plm_buffer_read(buf, 1);
	int r = (r_size > 0) ? plm_buffer_read(buf, r_size) : 0;
	int d = ((m_code - 1) << r_size) + r + 1;
	if (sign) {
		d = -d;
	}
	motion += d;
	int max_val = (16 << r_size) - 1;
	int min_val = -(16 << r_size);
	int range = 32 << r_size;
	if (motion > max_val)
		motion -= range;
	else if (motion < min_val)
		motion += range;
	return motion;
}

static void apeg_decode_picture(plm_video_t *self, int pic_type)
{
	self->picture_type = pic_type;
	self->quantizer_scale = 1;
	if (pic_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE) {
		self->motion_forward.full_px = plm_buffer_read(self->buffer, 1);
		self->motion_forward.r_size = plm_buffer_read(self->buffer, 3);
		plm_buffer_skip(self->buffer, 4);
	} else if (pic_type == PLM_VIDEO_PICTURE_TYPE_B) {
		self->motion_forward.full_px = plm_buffer_read(self->buffer, 1);
		self->motion_forward.r_size = plm_buffer_read(self->buffer, 3);
		self->motion_backward.full_px = plm_buffer_read(self->buffer, 1);
		self->motion_backward.r_size = plm_buffer_read(self->buffer, 3);
	}

	self->dc_predictor[0] = 128;
	self->dc_predictor[1] = 128;
	self->dc_predictor[2] = 128;
	self->motion_forward.h = 0;
	self->motion_forward.v = 0;
	self->motion_backward.h = 0;
	self->motion_backward.v = 0;
	memset(self->block_data, 0, sizeof(self->block_data));

	if (pic_type == PLM_VIDEO_PICTURE_TYPE_INTRA) {
		for (int ma = 0; ma < self->mb_size; ma++) {
			self->mb_row = ma / self->mb_width;
			self->mb_col = ma % self->mb_width;
			self->macroblock_address = ma;
			self->macroblock_type = 0x01;
			self->macroblock_intra = 1;
			if (plm_buffer_read(self->buffer, 1) != 0) {
				self->quantizer_scale = plm_buffer_read(self->buffer, 5);
			}
			for (int b = 0; b < 6; b++) {
				apeg_decode_block(self, b);
			}
		}
		return;
	}

	int ma = 0;
	while (ma < self->mb_size) {
		self->mb_row = ma / self->mb_width;
		self->mb_col = ma % self->mb_width;
		self->macroblock_address = ma;

		int mb_type = plm_buffer_read_vlc(self->buffer,
			pic_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE ? APEG_VLC_MB_TYPE_P : APEG_VLC_MB_TYPE_B);
		if (mb_type < 0)
			return;

		self->macroblock_type = mb_type;
		self->macroblock_intra = (mb_type & 0x01);

		if (self->macroblock_intra) {
			self->motion_forward.h = 0;
			self->motion_forward.v = 0;
			self->motion_backward.h = 0;
			self->motion_backward.v = 0;
			self->motion_forward.is_set = FALSE;
			self->motion_backward.is_set = FALSE;
		} else {
			self->dc_predictor[0] = 128;
			self->dc_predictor[1] = 128;
			self->dc_predictor[2] = 128;
			self->motion_forward.is_set = (mb_type & 0x08) != 0;
			self->motion_backward.is_set = (mb_type & 0x04) != 0;

			if (self->motion_forward.is_set) {
				self->motion_forward.h = apeg_decode_motion_vector(self->buffer, self->motion_forward.r_size, self->motion_forward.h);
				self->motion_forward.v = apeg_decode_motion_vector(self->buffer, self->motion_forward.r_size, self->motion_forward.v);
			} else if (pic_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE) {
				self->motion_forward.h = 0;
				self->motion_forward.v = 0;
				self->motion_forward.is_set = TRUE;
			}

			if (self->motion_backward.is_set) {
				self->motion_backward.h = apeg_decode_motion_vector(self->buffer, self->motion_backward.r_size, self->motion_backward.h);
				self->motion_backward.v = apeg_decode_motion_vector(self->buffer, self->motion_backward.r_size, self->motion_backward.v);
			}

			plm_video_predict_macroblock(self);
		}

		int cbp = 0;
		if (self->macroblock_intra) {
			cbp = 0x3f;
		} else if (mb_type & 0x02) {
			cbp = plm_buffer_read_vlc(self->buffer, APEG_VLC_CBP);
			if (cbp < 0)
				return;
		}

		if (mb_type & 0x10) {
			self->quantizer_scale = plm_buffer_read(self->buffer, 5);
		}

		int skip = 0;
		while (TRUE) {
			int v = plm_buffer_read_vlc(self->buffer, APEG_VLC_SKIP);
			if (v == -1) {
				skip += 35;
			} else if (v >= 0) {
				skip += v;
				break;
			} else {
				return;
			}
		}

		for (int b = 0, mask = 0x20; b < 6; b++, mask >>= 1) {
			if (cbp & mask) {
				apeg_decode_block(self, b);
			}
		}

		if (skip > 0) {
			self->dc_predictor[0] = 128;
			self->dc_predictor[1] = 128;
			self->dc_predictor[2] = 128;
			if (pic_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE) {
				self->motion_forward.h = 0;
				self->motion_forward.v = 0;
				self->motion_forward.is_set = TRUE;
				self->motion_backward.is_set = FALSE;
			}
			for (int s = 0; s < skip && ma + 1 < self->mb_size; s++) {
				ma++;
				self->mb_row = ma / self->mb_width;
				self->mb_col = ma % self->mb_width;
				self->macroblock_address = ma;
				plm_video_predict_macroblock(self);
			}
		}

		ma++;
	}
}

static plm_frame_t *apeg_decode_next_frame(struct apeg_context *apeg, int skip_b_before_ms)
{
	while (!apeg->eof) {
		uint8_t chdr[8];
		if (fread(chdr, 1, 8, apeg->fp) != 8) {
			apeg->eof = true;
			if (apeg->has_ref) {
				apeg->has_ref = false;
				return &apeg->vdec->frame_backward;
			}
			return NULL;
		}
		uint32_t csz = LittleEndian_getDW(chdr, 4);
		bool is_ipic = !memcmp(chdr, "IPIC", 4);
		bool is_ppic = !memcmp(chdr, "PPIC", 4);
		bool is_bpic = !memcmp(chdr, "BPIC", 4);
		if ((!is_ipic && !is_ppic && !is_bpic) || csz < 4) {
			if (fseek(apeg->fp, csz, SEEK_CUR) != 0)
				apeg->eof = true;
			continue;
		}
		uint8_t ts_buf[4];
		if (fread(ts_buf, 1, 4, apeg->fp) != 4) {
			apeg->eof = true;
			return NULL;
		}
		uint32_t chunk_ts = LittleEndian_getDW(ts_buf, 0);
		uint32_t payload_sz = csz - 4;

		if (is_bpic && (!apeg->has_both_refs || (int)chunk_ts < skip_b_before_ms)) {
			if (fseek(apeg->fp, payload_sz, SEEK_CUR) != 0)
				apeg->eof = true;
			continue;
		}

		size_t needed = (size_t)payload_sz + 8;
		if (needed > apeg->chunk_cap) {
			apeg->chunk_cap = needed * 2;
			apeg->chunk_buf = xrealloc(apeg->chunk_buf, apeg->chunk_cap);
		}
		if (payload_sz > 0 && fread(apeg->chunk_buf, 1, payload_sz, apeg->fp) != payload_sz) {
			apeg->eof = true;
			return NULL;
		}
		memset(apeg->chunk_buf + payload_sz, 0, 8);
		apeg->bitbuf->length = 0;
		apeg->bitbuf->bit_index = 0;
		apeg->bitbuf->has_ended = FALSE;
		plm_buffer_write(apeg->bitbuf, apeg->chunk_buf, needed);

		if (is_ipic || is_ppic) {
			plm_frame_t frame_temp = apeg->vdec->frame_forward;
			apeg->vdec->frame_forward = apeg->vdec->frame_backward;
			apeg_decode_picture(apeg->vdec, is_ipic ? PLM_VIDEO_PICTURE_TYPE_INTRA : PLM_VIDEO_PICTURE_TYPE_PREDICTIVE);
			apeg->vdec->frame_current.time = (double)chunk_ts / 1000.0;
			apeg->vdec->frame_backward = apeg->vdec->frame_current;
			apeg->vdec->frame_current = frame_temp;

			if (apeg->has_ref) {
				apeg->has_both_refs = true;
				return &apeg->vdec->frame_forward;
			}
			apeg->has_ref = true;
		} else {
			apeg_decode_picture(apeg->vdec, PLM_VIDEO_PICTURE_TYPE_B);
			apeg->vdec->frame_current.time = (double)chunk_ts / 1000.0;
			return &apeg->vdec->frame_current;
		}
	}
	return NULL;
}

static void apeg_seek_to(struct movie_context *mc, int time_ms)
{
	struct apeg_context *apeg = mc->apeg;
	if (!apeg)
		return;
	if (time_ms < 0)
		time_ms = 0;
	if (time_ms > apeg->end_time_ms)
		time_ms = apeg->end_time_ms;

	uint32_t offset = apeg->first_chunk_offset;
	for (uint32_t i = 0; i < apeg->keyframe_count; i++) {
		if ((int)apeg->keyframes[i].time_ms <= time_ms) {
			offset = apeg->keyframes[i].file_offset;
		} else {
			break;
		}
	}

	fseek(apeg->fp, offset, SEEK_SET);
	apeg->has_ref = false;
	apeg->has_both_refs = false;
	apeg->eof = false;
	apeg->has_rendered_frame = false;
	apeg->seeked_time_ms = time_ms;

	int skip_b_before_ms = time_ms - 33;
	plm_frame_t *frame = apeg_decode_next_frame(apeg, skip_b_before_ms);
	while (frame && frame->time * 1000.0 + 16.0 < (double)time_ms && !apeg->eof) {
		plm_frame_t *next = apeg_decode_next_frame(apeg, skip_b_before_ms);
		if (!next)
			break;
		frame = next;
	}
	mc->pending_video_frame = frame;
}

static void apeg_update_clock(struct apeg_context *apeg)
{
	if (!apeg || !apeg->playing)
		return;
	uint32_t elapsed = SDL_GetTicks() - apeg->start_ticks_ms;
	int now_ms = apeg->start_time_ms + (int)elapsed;
	if (now_ms >= apeg->end_time_ms) {
		now_ms = apeg->end_time_ms;
	}
	apeg->current_time_ms = now_ms;
}

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
	glActiveTexture(GL_TEXTURE0);
}

static void movie_shader_on_gl_reset(void *user)
{
	(void)user;
	movie_shader.program = 0;
}

static void load_movie_shader(void)
{
	gfx_load_shader(&movie_shader, "shaders/render.v.glsl", "shaders/movie.f.glsl");
	glUseProgram(movie_shader.program);
	glUniform1i(glGetUniformLocation(movie_shader.program, "texture_y"), 0);
	glUniform1i(glGetUniformLocation(movie_shader.program, "texture_cb"), 1);
	glUniform1i(glGetUniformLocation(movie_shader.program, "texture_cr"), 2);
	movie_shader.prepare = prepare_movie_shader;
	gfx_register_reset_callback(movie_shader_on_gl_reset, NULL);
}

static void movie_alloc_yuv_textures(struct movie_context *mc)
{
	glGenTextures(3, mc->textures);
	for (int i = 0; i < 3; i++) {
		glBindTexture(GL_TEXTURE_2D, mc->textures[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
}

static void movie_mc_on_gl_reset(void *user)
{
	struct movie_context *mc = user;
	if (!mc)
		return;
	memset(mc->textures, 0, sizeof(mc->textures));
	movie_alloc_yuv_textures(mc);
}

static void init_movie_textures(struct movie_context *mc)
{
	if (!movie_shader.program)
		load_movie_shader();

	movie_alloc_yuv_textures(mc);
	gfx_register_reset_callback(movie_mc_on_gl_reset, mc);
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
	if (!filename || !*filename)
		return NULL;
	char *path = gamedir_path_icase(filename);
	if (!path) {
		WARNING("%s: file does not exist", filename);
		return NULL;
	}
	if (is_directory(path)) {
		WARNING("%s: path is a directory", path);
		free(path);
		return NULL;
	}
	FILE *fp = file_open_utf8(path, "rb");
	if (!fp) {
		WARNING("%s: %s", path, strerror(errno));
		free(path);
		return NULL;
	}
	struct movie_context *mc = xcalloc(1, sizeof(struct movie_context));
	mc->voice = -1;
	mc->volume = 100;
	mc->plm = plm_create_with_file(fp, TRUE);
	if (!mc->plm || !plm_has_headers(mc->plm) ||
			plm_get_num_video_streams(mc->plm) <= 0) {
		WARNING("%s: unsupported movie format (not MPEG-1 PS); skipping playback", path);
		if (mc->plm) {
			plm_destroy(mc->plm);
			mc->plm = NULL;
		} else {
			fclose(fp);
		}
		free(path);
		mc->unsupported = true;
		mc->skipped = true;
		return mc;
	}
	free(path);

	init_movie_textures(mc);

	mc->decoder_mutex = SDL_CreateMutex();
	mc->timer_mutex = SDL_CreateMutex();
	register_movie(mc);
	return mc;
}

struct movie_context *movie_load_apeg(const char *filename, int sound_id, int sound_group)
{
	if (!filename || !*filename)
		return NULL;

	char *path = gamedir_path_icase(filename);
	if (!path) {
		WARNING("%s: file does not exist", filename);
		return NULL;
	}
	FILE *fp = file_open_utf8(path, "rb");
	if (!fp) {
		WARNING("%s: %s", path, strerror(errno));
		free(path);
		return NULL;
	}

	uint8_t pre[8];
	if (fread(pre, 1, 8, fp) != 8 || memcmp(pre, "APEG", 4) != 0) {
		WARNING("%s: not an APEG file", path);
		fclose(fp);
		free(path);
		return NULL;
	}
	uint32_t hdr_size = LittleEndian_getDW(pre, 4);
	if (hdr_size < 0x90 || hdr_size > 16 * 1024 * 1024) {
		WARNING("%s: invalid APEG header size %u", path, hdr_size);
		fclose(fp);
		free(path);
		return NULL;
	}
	uint8_t *hdr = xmalloc(hdr_size);
	if (fread(hdr, 1, hdr_size, fp) != hdr_size) {
		WARNING("%s: truncated APEG header", path);
		free(hdr);
		fclose(fp);
		free(path);
		return NULL;
	}

	int width = LittleEndian_getW(hdr, 0x04);
	int height = LittleEndian_getW(hdr, 0x06);
	if (width <= 0 || height <= 0) {
		WARNING("%s: invalid APEG dimensions %dx%d", path, width, height);
		free(hdr);
		fclose(fp);
		free(path);
		return NULL;
	}

	struct movie_context *mc = xcalloc(1, sizeof(struct movie_context));
	mc->voice = -1;
	mc->volume = 100;
	struct apeg_context *apeg = xcalloc(1, sizeof(struct apeg_context));
	mc->apeg = apeg;
	apeg->fp = fp;
	apeg->width = width;
	apeg->height = height;
	memcpy(apeg->intra_quant_matrix, hdr + 0x08, 64);
	memcpy(apeg->non_intra_quant_matrix, hdr + 0x48, 64);
	apeg->end_time_ms = (int)LittleEndian_getDW(hdr, 0x88);
	uint32_t kf_count = LittleEndian_getDW(hdr, 0x8c);
	uint32_t max_kf = (hdr_size - 0x90) / 8;
	if (kf_count > max_kf)
		kf_count = max_kf;
	apeg->keyframe_count = kf_count;
	if (kf_count > 0) {
		apeg->keyframes = xcalloc(kf_count, sizeof(struct apeg_keyframe));
		for (uint32_t i = 0; i < kf_count; i++) {
			apeg->keyframes[i].time_ms = LittleEndian_getDW(hdr, 0x90 + i * 8);
			apeg->keyframes[i].file_offset = LittleEndian_getDW(hdr, 0x90 + i * 8 + 4);
		}
	}
	free(hdr);
	free(path);

	apeg->sound_id = sound_id;
	apeg->sound_group = sound_group;

	// Scan initial metadata chunks (TMNL, SOND) prior to video pictures
	while (true) {
		long chunk_pos = ftell(fp);
		uint8_t chdr[8];
		if (fread(chdr, 1, 8, fp) != 8) {
			apeg->first_chunk_offset = (uint32_t)chunk_pos;
			break;
		}
		uint32_t csz = LittleEndian_getDW(chdr, 4);
		if (!memcmp(chdr, "IPIC", 4) || !memcmp(chdr, "PPIC", 4) || !memcmp(chdr, "BPIC", 4)) {
			fseek(fp, chunk_pos, SEEK_SET);
			apeg->first_chunk_offset = (uint32_t)chunk_pos;
			break;
		}
		if (!memcmp(chdr, "SOND", 4) && csz > 0) {
			apeg->has_sond_chunk = true;
			if (sound_id > 0) {
				uint8_t *sond_buf = xmalloc(csz);
				if (fread(sond_buf, 1, csz, fp) == csz) {
					if (kiwi_prepare_from_memory(sound_id, sond_buf, csz, sound_group)) {
						apeg->has_audio = true;
					}
				} else {
					free(sond_buf);
					break;
				}
			} else {
				if (fseek(fp, csz, SEEK_CUR) != 0)
					break;
			}
		} else {
			if (fseek(fp, csz, SEEK_CUR) != 0)
				break;
		}
	}

	apeg->bitbuf = plm_buffer_create_with_capacity(64 * 1024);
	apeg->vdec = plm_video_create_with_buffer(apeg->bitbuf, TRUE);
	apeg->vdec->width = width;
	apeg->vdec->height = height;
	apeg->vdec->mb_width = (width + 15) >> 4;
	apeg->vdec->mb_height = (height + 15) >> 4;
	apeg->vdec->mb_size = apeg->vdec->mb_width * apeg->vdec->mb_height;
	apeg->vdec->luma_width = apeg->vdec->mb_width << 4;
	apeg->vdec->luma_height = apeg->vdec->mb_height << 4;
	apeg->vdec->chroma_width = apeg->vdec->mb_width << 3;
	apeg->vdec->chroma_height = apeg->vdec->mb_height << 3;

	size_t frame_data_size = (size_t)apeg->vdec->mb_size * 384;
	apeg->vdec->frames_data = xcalloc(3, frame_data_size);
	plm_video_init_frame(apeg->vdec, &apeg->vdec->frame_current, apeg->vdec->frames_data);
	plm_video_init_frame(apeg->vdec, &apeg->vdec->frame_forward, apeg->vdec->frames_data + frame_data_size);
	plm_video_init_frame(apeg->vdec, &apeg->vdec->frame_backward, apeg->vdec->frames_data + frame_data_size * 2);
	memcpy(apeg->vdec->intra_quant_matrix, apeg->intra_quant_matrix, 64);
	memcpy(apeg->vdec->non_intra_quant_matrix, apeg->non_intra_quant_matrix, 64);
	apeg->vdec->has_sequence_header = TRUE;

	init_movie_textures(mc);
	mc->decoder_mutex = SDL_CreateMutex();
	mc->timer_mutex = SDL_CreateMutex();

	apeg->seeked_time_ms = -1;
	apeg_seek_to(mc, 0);
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

	if (mc->apeg) {
		struct apeg_context *apeg = mc->apeg;
		if (apeg->has_audio && apeg->sound_id > 0) {
			kiwi_stop_sound(apeg->sound_id);
			kiwi_unprepare_sound(apeg->sound_id);
		}
		if (apeg->vdec)
			plm_video_destroy(apeg->vdec);
		free(apeg->chunk_buf);
		free(apeg->keyframes);
		if (apeg->fp)
			fclose(apeg->fp);
		free(apeg);
	}
	if (mc->plm)
		plm_destroy(mc->plm);
	gfx_unregister_reset_callback(movie_mc_on_gl_reset, mc);
	if (mc->textures[0] && glIsTexture(mc->textures[0]))
		glDeleteTextures(3, mc->textures);
	if (mc->decoder_mutex)
		SDL_DestroyMutex(mc->decoder_mutex);
	if (mc->timer_mutex)
		SDL_DestroyMutex(mc->timer_mutex);
	free(mc);
}

bool movie_play(struct movie_context *mc)
{
	if (!mc)
		return false;
	if (mc->unsupported)
		return true;
	if (mc->apeg)
		return movie_play_at(mc, 0);

	mc->voice = -1;
	mixer_stream_stop_stream(&mc->sts_stream);
	mc->skipped = false;
	mc->plm_playing = true;
	movie_playback_seq++;

	// Start the audio stream.
	mc->stream_time = 0.0;
	mc->wall_time_ms = SDL_GetTicks();
	if (plm_get_num_audio_streams(mc->plm) > 0 && plm_get_samplerate(mc->plm) > 0) {
		mc->sts_stream.userdata = mc;
		mc->sts_stream.callback = audio_callback;
		mc->sts_stream.sample.frequency = plm_get_samplerate(mc->plm);
		mc->sts_stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_FLOAT;
		mc->voice = mixer_stream_play(&mc->sts_stream, mc->volume);
	} else {
		mc->voice = -1;
	}
	return true;
}

bool movie_play_at(struct movie_context *mc, int time_ms)
{
	if (!mc)
		return false;
	if (!mc->apeg)
		return movie_play(mc);

	struct apeg_context *apeg = mc->apeg;
	if (time_ms < 0)
		time_ms = 0;
	if (time_ms > apeg->end_time_ms)
		time_ms = apeg->end_time_ms;

	SDL_LockMutex(mc->decoder_mutex);
	if (apeg->seeked_time_ms != time_ms || apeg->has_rendered_frame) {
		apeg_seek_to(mc, time_ms);
	}
	SDL_UnlockMutex(mc->decoder_mutex);

	if (apeg->has_audio && apeg->sound_id > 0) {
		kiwi_stop_sound(apeg->sound_id);
		kiwi_play_sound(apeg->sound_id);
		if (time_ms > 0)
			kiwi_seek_sound(apeg->sound_id, time_ms);
	}

	mc->skipped = false;
	apeg->start_time_ms = time_ms;
	apeg->start_ticks_ms = SDL_GetTicks();
	apeg->current_time_ms = time_ms;
	apeg->has_manual_time = false;
	apeg->playing = true;
	movie_playback_seq++;
	return true;
}

bool movie_set_time(struct movie_context *mc, int time_ms)
{
	if (!mc || !mc->apeg)
		return false;
	struct apeg_context *apeg = mc->apeg;
	int clamped_ms = time_ms;
	if (clamped_ms < 0)
		clamped_ms = 0;
	if (clamped_ms > apeg->end_time_ms)
		clamped_ms = apeg->end_time_ms;

	if (apeg->has_audio && apeg->sound_id > 0) {
		kiwi_stop_sound(apeg->sound_id);
	}

	SDL_LockMutex(mc->decoder_mutex);
	apeg_seek_to(mc, clamped_ms);
	SDL_UnlockMutex(mc->decoder_mutex);

	mc->skipped = false;
	apeg->has_manual_time = true;
	apeg->playing = false;
	apeg->start_time_ms = clamped_ms;
	apeg->start_ticks_ms = SDL_GetTicks();
	apeg->current_time_ms = time_ms;
	return true;
}

bool movie_draw(struct movie_context *mc, struct sact_sprite *sprite)
{
	if (!mc)
		return false;
	if (mc->unsupported || mc->skipped)
		return sprite != NULL;

	plm_frame_t *frame = NULL;
	if (mc->apeg) {
		struct apeg_context *apeg = mc->apeg;
		apeg_update_clock(apeg);
		double now = apeg->current_time_ms / 1000.0;

		SDL_LockMutex(mc->decoder_mutex);
		frame = mc->pending_video_frame;
		mc->pending_video_frame = NULL;
		if (!frame) {
			frame = apeg_decode_next_frame(apeg, apeg->current_time_ms - 33);
		}
		if (!frame) {
			SDL_UnlockMutex(mc->decoder_mutex);
			return apeg->eof;
		}
		if (frame->time > now && apeg->has_rendered_frame) {
			mc->pending_video_frame = frame;
			SDL_UnlockMutex(mc->decoder_mutex);
			return true;
		}
		while (frame && frame->time + 0.035 < now && !apeg->eof) {
			plm_frame_t *next = apeg_decode_next_frame(apeg, apeg->current_time_ms - 33);
			if (!next)
				break;
			frame = next;
		}
		apeg->has_rendered_frame = true;
		SDL_UnlockMutex(mc->decoder_mutex);
	} else {
		// Decode a frame, unless we already have one.
		frame = mc->pending_video_frame;
		mc->pending_video_frame = NULL;
		if (!frame) {
			SDL_LockMutex(mc->decoder_mutex);
			frame = plm_decode_video(mc->plm);
			bool ended = !frame && plm_video_has_ended(mc->plm->video_decoder);
			SDL_UnlockMutex(mc->decoder_mutex);
			if (!frame)
				return ended;
		}

		// If the frame's timestamp is in the future, save the frame and return.
		SDL_LockMutex(mc->timer_mutex);
		double now = mc->stream_time + (SDL_GetTicks() - mc->wall_time_ms) / 1000.0;
		SDL_UnlockMutex(mc->timer_mutex);
		if (frame->time > now) {
			mc->pending_video_frame = frame;
			return true;
		}
		while (frame && frame->time + 0.04 < now) {
			SDL_LockMutex(mc->decoder_mutex);
			bool ended = plm_video_has_ended(mc->plm->video_decoder);
			plm_frame_t *next = ended ? NULL : plm_decode_video(mc->plm);
			SDL_UnlockMutex(mc->decoder_mutex);
			if (!next)
				break;
			frame = next;
		}
	}

	if (!movie_shader.program)
		load_movie_shader();
	if (!mc->textures[0] || !glIsTexture(mc->textures[0]))
		movie_alloc_yuv_textures(mc);
	// Render the frame.
	update_texture(GL_TEXTURE0, mc->textures[0], &frame->y);
	update_texture(GL_TEXTURE1, mc->textures[1], &frame->cb);
	update_texture(GL_TEXTURE2, mc->textures[2], &frame->cr);
	glActiveTexture(GL_TEXTURE0);

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
	if (!mc || mc->unsupported || mc->skipped)
		return true;
	if (mc->apeg) {
		struct apeg_context *apeg = mc->apeg;
		if (apeg->has_sond_chunk) {
			if (!apeg->playing)
				return true;
			apeg_update_clock(apeg);
			return apeg->current_time_ms >= apeg->end_time_ms;
		}
		int pos = movie_get_position(mc);
		return pos >= 0 && pos >= apeg->end_time_ms;
	}
	SDL_LockMutex(mc->decoder_mutex);
	bool ended = plm_has_ended(mc->plm);
	SDL_UnlockMutex(mc->decoder_mutex);
	return ended;
}

int movie_get_position(struct movie_context *mc)
{
	if (!mc || mc->unsupported)
		return 0;
	if (mc->apeg) {
		struct apeg_context *apeg = mc->apeg;
		if (apeg->has_manual_time)
			return apeg->current_time_ms;
		if (!apeg->playing)
			return apeg->has_sond_chunk ? apeg->end_time_ms : 0;
		apeg_update_clock(apeg);
		return apeg->current_time_ms;
	}
	SDL_LockMutex(mc->timer_mutex);
	int ms = mc->wall_time_ms ? mc->stream_time * 1000 + SDL_GetTicks() - mc->wall_time_ms : 0;
	SDL_UnlockMutex(mc->timer_mutex);
	return ms;
}

int movie_get_end_time(struct movie_context *mc)
{
	if (!mc || !mc->apeg)
		return 0;
	return mc->apeg->end_time_ms;
}

int movie_get_width(struct movie_context *mc)
{
	if (!mc)
		return 0;
	if (mc->apeg)
		return mc->apeg->width;
	return mc->plm ? plm_get_width(mc->plm) : 0;
}

int movie_get_height(struct movie_context *mc)
{
	if (!mc)
		return 0;
	if (mc->apeg)
		return mc->apeg->height;
	return mc->plm ? plm_get_height(mc->plm) : 0;
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

static void movie_skip_one(struct movie_context *mc)
{
	if (!mc || mc->skipped)
		return;
	mc->skipped = true;
	mc->voice = -1;
	mixer_stream_stop_stream(&mc->sts_stream);
	if (mc->apeg) {
		struct apeg_context *apeg = mc->apeg;
		apeg->playing = false;
		apeg->current_time_ms = apeg->end_time_ms;
		if (apeg->has_audio && apeg->sound_id > 0)
			kiwi_stop_sound(apeg->sound_id);
	} else if (mc->plm) {
		mc->plm_playing = false;
	}
}

bool movie_has_active_playback(void)
{
	for (int i = 0; i < nr_active_movies; i++) {
		if (movie_context_is_playing(active_movies[i]))
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
		if (movie_context_is_playing(active_movies[i])) {
			movie_skip_one(active_movies[i]);
			skipped_any = true;
		}
	}
	return skipped_any;
}

void movie_end_session(void)
{
	gfx_unregister_reset_callback(movie_shader_on_gl_reset, NULL);
	memset(&movie_shader, 0, sizeof(movie_shader));
	movie_skip_active();
	if (nr_active_movies == 0) {
		free(active_movies);
		active_movies = NULL;
		cap_active_movies = 0;
	}
}
