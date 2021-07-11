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
	[EFFECT_ZOOR_RL]                = "EFFECT_ZOOR_RL",
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
static struct effect_shader mosaic_shader = EFFECT_SHADER("shaders/effects/mosaic.f.glsl");
static struct effect_shader blind_lr_shader = EFFECT_SHADER("shaders/effects/blind_lr.f.glsl");
static struct effect_shader zigzag_crossfade_shader = EFFECT_SHADER("shaders/effects/zigzag_crossfade.f.glsl");
static struct effect_shader blur_crossfade_shader = EFFECT_SHADER("shaders/effects/blur_crossfade.f.glsl");

extern GLuint main_surface_fb;

static struct effect_shader *effect_shaders[NR_EFFECTS] = {
	[EFFECT_CROSSFADE] = &crossfade_shader,
	[EFFECT_CROSSFADE_MOSAIC] = &mosaic_shader,
	[EFFECT_BLIND_LR] = &blind_lr_shader,
	[EFFECT_ZIGZAG_CROSSFADE] = &zigzag_crossfade_shader,
	[EFFECT_BLUR_CROSSFADE] = &blur_crossfade_shader,
};

int sact_Effect(int type, possibly_unused int time, possibly_unused int key)
{
	if (type <= 0 || type >= NR_EFFECTS) {
		WARNING("Invalid or unknown effect: %d", type);
		return 0;
	}
	struct effect_shader *s = effect_shaders[type];
	if (!s) {
		WARNING("Unimplemented effect: %s", effect_names[type]);
		return 0;
	}
	// load shader lazily
	if (!s->s.program) {
		load_effect_shader(s);
	}

	// get old & new scene textures
	Texture old, new;
	gfx_copy_main_surface(&old);
	scene_render();
	gfx_copy_main_surface(&new);

	//effect_callback effect = effects[type];
	for (int i = 0; i < time; i += 16) {
		gfx_clear();
		render_effect_shader(s, &old, &new, (float)i / (float)time);
		gfx_swap();
		SDL_Delay(16);
	}

	gfx_delete_texture(&old);
	gfx_delete_texture(&new);

	return 1;
}

static struct {
	bool on;
	int type;
	Texture old;
	Texture new;
} trans = {0};

int sact_TRANS_Begin(int type)
{
	if (type <= 0 || type >= NR_EFFECTS) {
		WARNING("Invalid or unknown effect: %d", type);
		return 0;
	}
	struct effect_shader *s = effect_shaders[type];
	if (!s) {
		WARNING("Unimplemented effect: %s", effect_names[type]);
		return 0;
	}
	// load shader lazily
	if (!s->s.program) {
		load_effect_shader(s);
	}

	trans.on = true;
	trans.type = type;
	gfx_copy_main_surface(&trans.old);
	scene_render();
	gfx_copy_main_surface(&trans.new);
	return 1;
}

int sact_TRANS_Update(float rate)
{
	if (!trans.on) {
		return 0;
	}
	gfx_clear();
	render_effect_shader(effect_shaders[trans.type], &trans.old, &trans.new, rate);
	gfx_swap();
	return 1;
}

int sact_TRANS_End(void)
{
	trans.on = false;
	gfx_delete_texture(&trans.old);
	gfx_delete_texture(&trans.new);
	return 1;
}
