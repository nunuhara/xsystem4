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

#include <stdlib.h>
#include <math.h>
#include <SDL.h>
#include "effect.h"
#include "gfx/gfx.h"
#include "sact.h"
#include "system4.h"
#include "xsystem4.h"

const char *effect_names[NR_EFFECTS] = {
	[EFFECT_CROSSFADE]              = "EFFECT_CROSSFADE",
	[EFFECT_FADEOUT]                = "EFFECT_FADEOUT",
	[EFFECT_FADEIN]                 = "EFFECT_FADEIN",
	[EFFECT_WHITEOUT]               = "EFFECT_WHITEOUT",
	[EFFECT_WHITEIN]                = "EFFECT_WHITEIN",
	[EFFECT_CROSSFADE_MOSAIC]       = "EFFECT_CROSSFADE_MOSAIC",
	[EFFECT_BLIND_DOWN]             = "EFFECT_BLIND_DOWN",
	[EFFECT_BLIND_LR]               = "EFFECT_BLIND_LR",
	[EFFECT_BLIND_DOWN_LR]          = "EFFECT_BLIND_DOWN_LR",
	[EFFECT_ZOOM_BLEND_BLUR]        = "EFFECT_ZOOM_BLEND_BLUR",
	[EFFECT_LINEAR_BLUR]            = "EFFECT_LINEAR_BLUR",
	[EFFECT_UP_DOWN_CROSSFADE]      = "EFFECT_UP_DOWN_CROSSFADE",
	[EFFECT_DOWN_UP_CROSSFADE]      = "EFFECT_DOWN_UP_CROSSFADE",
	[EFFECT_PENTAGRAM_IN_OUT]       = "EFFECT_PENTAGRAM_IN_OUT",
	[EFFECT_PENTAGRAM_OUT_IN]       = "EFFECT_PENTAGRAM_OUT_IN",
	[EFFECT_HEXAGRAM_IN_OUT]        = "EFFECT_HEXAGRAM_IN_OUT",
	[EFFECT_HEXAGRAM_OUT_IN]        = "EFFECT_HEXAGRAM_OUT_IN",
	[EFFECT_AMAP_CROSSFADE]         = "EFFECT_AMAP_CROSSFADE",
	[EFFECT_VERTICAL_BAR_BLUR]      = "EFFECT_VERTICAL_BAR_BLUR",
	[EFFECT_ROTATE_OUT]             = "EFFECT_ROTATE_OUT",
	[EFFECT_ROTATE_IN]              = "EFFECT_ROTATE_IN",
	[EFFECT_ROTATE_OUT_CW]          = "EFFECT_ROTATE_OUT_CW",
	[EFFECT_ROTATE_IN_CW]           = "EFFECT_ROTATE_IN_CW",
	[EFFECT_BLOCK_DISSOLVE]         = "EFFECT_BLOCK_DISSOLVE",
	[EFFECT_POLYGON_ROTATE_Y]       = "EFFECT_POLYGON_ROTATE_Y",
	[EFFECT_POLYGON_ROTATE_Y_CW]    = "EFFECT_POLYGON_ROTATE_Y_CW",
	[EFFECT_OSCILLATE]              = "EFFECT_OSCILLATE",
	[EFFECT_POLYGON_ROTATE_X_CW]    = "EFFECT_POLYGON_ROTATE_X_CW",
	[EFFECT_POLYGON_ROTATE_X]       = "EFFECT_POLYGON_ROTATE_X",
	[EFFECT_ROTATE_ZOOM_BLEND_BLUR] = "EFFECT_ROTATE_ZOOM_BLEND_BLUR",
	[EFFECT_ZIGZAG_CROSSFADE]       = "EFFECT_ZIGZAG_CROSSFADE",
	[EFFECT_TV_SWITCH_OFF]          = "EFFECT_TV_SWITCH_OFF",
	[EFFECT_TV_SWITCH_ON]           = "EFFECT_TV_SWITCH_ON",
	[EFFECT_POLYGON_EXPLOSION]      = "EFFECT_POLYGON_EXPLOSION",
	[EFFECT_NOISE_CROSSFADE]        = "EFFECT_NOISE_CROSSFADE",
	[EFFECT_TURN_PAGE]              = "EFFECT_TURN_PAGE",
	[EFFECT_SEPIA_NOISE_CROSSFADE]  = "EFFECT_SEPIA_NOISE_CROSSFADE",
	[EFFECT_CRUMPLED_PAPER_PULL]    = "EFFECT_CRUMPLED_PAPER_PULL",
	[EFFECT_HORIZONTAL_ZIGZAG]      = "EFFECT_HORIZONTAL_ZIGZAG",
	[EFFECT_LINEAR_BLUR_HD]         = "EFFECT_LINEAR_BLUR_HD",
	[EFFECT_VERTICAL_BAR_BLUR_HD]   = "EFFECT_VERTICAL_BAR_BLUR_HD",
	[EFFECT_AMAP_CROSSFADE2]        = "EFFECT_AMAP_CROSSFADE2",
	[EFFECT_ZOOM_LR]                = "EFFECT_ZOOM_LR",
	[EFFECT_ZOOM_RL]                = "EFFECT_ZOOM_RL",
	[EFFECT_CROSSFADE_LR]           = "EFFECT_CROSSFADE_LR",
	[EFFECT_CROSSFADE_RL]           = "EFFECT_CROSSFADE_RL",
	[EFFECT_PIXEL_EXPLOSION]        = "EFFECT_PIXEL_EXPLOSION",
	[EFFECT_ZOOM_IN_CROSSFADE]      = "EFFECT_ZOOM_IN_CROSSFADE",
	[EFFECT_PIXEL_DROP]             = "EFFECT_PIXEL_DROP",
	[EFFECT_BLUR_FADEOUT]           = "EFFECT_BLUR_FADEOUT",
	[EFFECT_BLUR_CROSSFADE]         = "EFFECT_BLUR_CROSSFADE",
	[EFFECT_2ROT_ZOOM_BLEND_BLUR]   = "EFFECT_2ROT_ZOOM_BLEND_BLUR",
};

struct effect_shader {
	Shader s;
	GLint old;
	GLint resolution;
	GLint progress;
	const char *f_path;
};

struct effect_data {
	Texture *old;
	Texture *new;
	float progress;
};

static void prepare_effect_shader(struct gfx_render_job *job, void *data)
{
	struct effect_shader *s = (struct effect_shader*)job->shader;
	struct effect_data *d = (struct effect_data*)data;

	// set uniforms
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, d->old->handle);
	glUniform1i(s->old, 1);

	glUniform2f(s->resolution, config.view_width, config.view_height);
	glUniform1f(s->progress, d->progress);
}

static void load_effect_shader(struct effect_shader *s)
{
	gfx_load_shader(&s->s, "shaders/render.v.glsl", s->f_path);
	s->old = glGetUniformLocation(s->s.program, "old");
	s->resolution = glGetUniformLocation(s->s.program, "resolution");
	s->progress = glGetUniformLocation(s->s.program, "progress");
	s->s.prepare = prepare_effect_shader;
}

static void render_effect_shader(struct effect_shader *shader, Texture *old, Texture *new, float progress)
{
	struct effect_data data = { .old = old, .new = new, .progress = progress };
	mat4 wv_transform = WV_TRANSFORM(config.view_width, config.view_height);
	struct gfx_render_job job = {
		.shader = &shader->s,
		.texture = new->handle,
		.world_transform = new->world_transform[0],
		.view_transform = wv_transform[0],
		.data = &data
	};
	gfx_render(&job);
}

#define EFFECT_SHADER(path) { .s = { .prepare = prepare_effect_shader }, .f_path = path}
static struct effect_shader crossfade_shader = EFFECT_SHADER("shaders/effects/crossfade.f.glsl");
static struct effect_shader crossfade_lr_shader = EFFECT_SHADER("shaders/effects/crossfade_lr.f.glsl");
static struct effect_shader crossfade_up_down_shader = EFFECT_SHADER("shaders/effects/crossfade_up_down.f.glsl");
static struct effect_shader mosaic_shader = EFFECT_SHADER("shaders/effects/mosaic.f.glsl");
static struct effect_shader blind_down_shader = EFFECT_SHADER("shaders/effects/blind_down.f.glsl");
static struct effect_shader blind_lr_shader = EFFECT_SHADER("shaders/effects/blind_lr.f.glsl");
static struct effect_shader linear_blur_shader = EFFECT_SHADER("shaders/effects/linear_blur.f.glsl");
static struct effect_shader zigzag_crossfade_shader = EFFECT_SHADER("shaders/effects/zigzag_crossfade.f.glsl");
static struct effect_shader noise_crossfade_shader = EFFECT_SHADER("shaders/effects/noise_crossfade.f.glsl");
static struct effect_shader turn_page_shader = EFFECT_SHADER("shaders/effects/turn_page.f.glsl");
static struct effect_shader sepia_noise_crossfade_shader = EFFECT_SHADER("shaders/effects/sepia_noise_crossfade.f.glsl");
static struct effect_shader blur_fadeout_shader = EFFECT_SHADER("shaders/effects/blur_fadeout.f.glsl");
static struct effect_shader blur_crossfade_shader = EFFECT_SHADER("shaders/effects/blur_crossfade.f.glsl");

extern GLuint main_surface_fb;

static struct effect_shader *effect_shaders[NR_EFFECTS] = {
	[EFFECT_CROSSFADE] = &crossfade_shader,
	[EFFECT_CROSSFADE_LR] = &crossfade_lr_shader,
	[EFFECT_UP_DOWN_CROSSFADE] = &crossfade_up_down_shader,
	[EFFECT_CROSSFADE_MOSAIC] = &mosaic_shader,
	[EFFECT_BLIND_DOWN] = &blind_down_shader,
	[EFFECT_BLIND_LR] = &blind_lr_shader,
	[EFFECT_LINEAR_BLUR] = &linear_blur_shader,
	[EFFECT_ZIGZAG_CROSSFADE] = &zigzag_crossfade_shader,
	[EFFECT_NOISE_CROSSFADE] = &noise_crossfade_shader,
	[EFFECT_TURN_PAGE] = &turn_page_shader,
	[EFFECT_SEPIA_NOISE_CROSSFADE] = &sepia_noise_crossfade_shader,
	[EFFECT_BLUR_FADEOUT] = &blur_fadeout_shader,
	[EFFECT_BLUR_CROSSFADE] = &blur_crossfade_shader,
};

static struct {
	bool on;
	int type;
	Texture old;
	Texture new;
} effect = {0};

static void effect_fadeout(float rate)
{
	gfx_copy_bright(gfx_main_surface(), 0, 0, &effect.old, 0, 0,
			effect.old.w, effect.old.h, (1.0f - rate) * 255);
}

static void effect_fadein(float rate)
{
	gfx_copy_bright(gfx_main_surface(), 0, 0, &effect.new, 0, 0,
			effect.new.w, effect.new.h, rate * 255);
}

static void effect_whiteout(float rate)
{
	Texture *dst = gfx_main_surface();
	gfx_fill(dst, 0, 0, dst->w, dst->h, 255, 255, 255);
	gfx_blend(dst, 0, 0, &effect.old, 0, 0, dst->w, dst->h, (1.0f - rate) * 255);
}

static void effect_whitein(float rate)
{
	Texture *dst = gfx_main_surface();
	gfx_fill(dst, 0, 0, dst->w, dst->h, 255, 255, 255);
	gfx_blend(dst, 0, 0, &effect.new, 0, 0, dst->w, dst->h, rate * 255);
}

static void effect_zoom_lr(float rate)
{
	Texture *dst = gfx_main_surface();
	unsigned x_pos = roundf(dst->w * rate);
	gfx_copy_stretch(dst, x_pos, 0, dst->w-x_pos, dst->h, &effect.old, 0, 0, effect.old.w, effect.old.h);
	gfx_copy_stretch(dst, 0, 0, x_pos, dst->h, &effect.new, 0, 0, effect.new.w, effect.new.h);
}

static void effect_zoom_rl(float rate)
{
	Texture *dst = gfx_main_surface();
	unsigned x_pos = dst->w - roundf(dst->w * rate);
	gfx_copy_stretch(dst, x_pos, 0, dst->w-x_pos, dst->h, &effect.new, 0, 0, effect.new.w, effect.new.h);
	gfx_copy_stretch(dst, 0, 0, x_pos, dst->h, &effect.old, 0, 0, effect.old.w, effect.old.h);
}

static void effect_oscillate(float rate)
{
	Texture *dst = gfx_main_surface();
	int delta_x = (rand() % (dst->w / 10) - (dst->w / 20)) * (1.0f - rate);
	int delta_y = (rand() % (dst->h / 10) - (dst->h / 20)) * (1.0f - rate);;

	gfx_copy(dst, 0, 0, &effect.old, 0, 0, dst->w, dst->h);
	gfx_copy(dst, delta_x, delta_y, &effect.new, 0, 0, dst->w, dst->h);
}

static void effect_tv_switch(float rate, Texture *src)
{
	Texture *dst = gfx_main_surface();
	if (rate < 0.5f) {
		int h = max(1, (int)(dst->h * (1.0f - rate * 2.0f)));
		gfx_copy_stretch(dst, 0, dst->h * rate, dst->w, h,
				 src, 0, 0, src->w, src->h);
	} else {
		rate -= 0.5f;
		int w = dst->w * (1.0f - rate * 2.0f);
		gfx_copy_stretch(dst, dst->w * rate, dst->h / 2, w, 1,
				 src, 0, 0, src->w, src->h);
	}
}

static void effect_tv_switch_off(float rate)
{
	effect_tv_switch(rate, &effect.old);
}

static void effect_tv_switch_on(float rate)
{
	effect_tv_switch(1.0f - rate, &effect.new);
}

static void effect_zoom_in_crossfade(float rate)
{
	Texture *dst = gfx_main_surface();
	float scale = 1.0f + powf(rate, 4.0f) * 16.0f;
	int w = roundf(dst->w * scale);
	int h = roundf(dst->h * scale);
	int x = -((w - dst->w)/2);
	int y = -((h - dst->h)/2);
	int a = (1.0f - rate) * 255;

	gfx_copy(dst, 0, 0, &effect.new, 0, 0, dst->w, dst->h);
	gfx_copy_stretch_blend(dst, x, y, w, h, &effect.old, 0, 0, dst->w, dst->h, a);
}

typedef void (*effect_fun)(float rate);
static effect_fun effect_functions[NR_EFFECTS] = {
	[EFFECT_FADEOUT]           = effect_fadeout,
	[EFFECT_FADEIN]            = effect_fadein,
	[EFFECT_WHITEOUT]          = effect_whiteout,
	[EFFECT_WHITEIN]           = effect_whitein,
	[EFFECT_OSCILLATE]         = effect_oscillate,
	[EFFECT_TV_SWITCH_OFF]     = effect_tv_switch_off,
	[EFFECT_TV_SWITCH_ON]      = effect_tv_switch_on,
	[EFFECT_ZOOM_LR]           = effect_zoom_lr,
	[EFFECT_ZOOM_RL]           = effect_zoom_rl,
	[EFFECT_ZOOM_IN_CROSSFADE] = effect_zoom_in_crossfade,
};

int sact_TRANS_Begin(int type)
{
	if (type <= 0 || type >= NR_EFFECTS) {
		WARNING("Invalid or unknown effect: %d", type);
		return 0;
	}

	if (effect_functions[type]) {
		// nothing to do
	}
	else {
		struct effect_shader *s = effect_shaders[type];
		if (!s) {
			WARNING("Unimplemented effect: %s", effect_names[type]);
			type = EFFECT_BLUR_CROSSFADE;
			s = effect_shaders[type];
		}
		// load shader lazily
		if (!s->s.program) {
			load_effect_shader(s);
		}
	}

	effect.on = true;
	effect.type = type;
	gfx_copy_main_surface(&effect.old);
	scene_render();
	gfx_copy_main_surface(&effect.new);
	return 1;
}

int sact_TRANS_Update(float rate)
{
	if (!effect.on) {
		return 0;
	}
	gfx_clear();
	if (effect_functions[effect.type]) {
		effect_functions[effect.type](rate);
	} else {
		render_effect_shader(effect_shaders[effect.type], &effect.old, &effect.new, rate);
	}
	gfx_swap();
	return 1;
}

int sact_TRANS_End(void)
{
	effect.on = false;
	gfx_delete_texture(&effect.old);
	gfx_delete_texture(&effect.new);
	return 1;
}

int sact_Effect(int type, int time, possibly_unused int key)
{
	uint32_t start = SDL_GetTicks();
	if (!sact_TRANS_Begin(type))
		return 0;

	uint32_t t = SDL_GetTicks() - start;
	while (t < time) {
		sact_TRANS_Update((float)t / (float)time);
		uint32_t t2 = SDL_GetTicks() - start;
		if (t2 < t + 16) {
			SDL_Delay(t + 16 - t2);
			t += 16;
		} else {
			t = t2;
		}
	}

	sact_TRANS_End();
	return 1;
}
