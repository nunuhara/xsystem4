/* Copyright (C) 2024 kichikuou <KichikuouChrome@gmail.com>
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

#include <math.h>
#include <stdlib.h>
#include <cglm/cglm.h>
#include <SDL.h>

#include "cJSON.h"
#include "gfx/gfx.h"
#include "hll.h"
#include "json.h"
#include "plugin.h"
#include "sact.h"
#include "sprite.h"
#include "xsystem4.h"

static const char plugin_name[] = "DrawSnow";

enum draw_snow_type {
	DRAW_SNOW_SCREEN_BLEND = 0,
	DRAW_SNOW_ALPHA_BLEND = 1,
};

struct snowflake {
	int start_x;
	int start_y;
	int end_x;
	int end_y;
	int total_time;
	float scale;
};

struct draw_snow_plugin {
	struct draw_plugin p;
	enum draw_snow_type type;
	int snow_sprite;
	int max_time;
	int max_amp;
	int nr_particles;
	struct snowflake *particles;
	uint32_t timestamp;
};

static inline float frand(void)
{
	return ((float)rand() / RAND_MAX);
}

static struct draw_snow_plugin *get_draw_snow_plugin(int sprite)
{
	struct sact_sprite *sp = sact_try_get_sprite(sprite);
	if (!sp)
		return NULL;
	if (!sp->plugin || sp->plugin->name != plugin_name)
		return NULL;
	return (struct draw_snow_plugin *)sp->plugin;
}

static void DrawSnow_free(struct draw_plugin *_plugin)
{
	struct draw_snow_plugin *plugin = (struct draw_snow_plugin *)_plugin;
	free(plugin->particles);
	free(plugin);
}

static void DrawSnow_update(struct sact_sprite *sp)
{
	struct draw_snow_plugin *plugin = (struct draw_snow_plugin *)sp->plugin;
	if (!plugin->particles)
		return;
	int elapsed = SDL_GetTicks() - plugin->timestamp;
	if (elapsed >= 16)
		sprite_dirty(sp);
}

static void DrawSnow_render(struct sact_sprite *sp)
{
	struct draw_snow_plugin *plugin = (struct draw_snow_plugin *)sp->plugin;
	if (!plugin->particles)
		return;

	struct texture *src = sprite_get_texture(sact_get_sprite(plugin->snow_sprite));
	struct texture *dst = gfx_main_surface();
	plugin->timestamp = SDL_GetTicks();
	uint32_t timestamp = 30000 + plugin->timestamp;
	for (int i = 0; i < plugin->nr_particles; i++) {
		struct snowflake *p = &plugin->particles[i];
		float rate = (float)(timestamp % p->total_time) / p->total_time;
		int x = p->start_x + (p->end_x - p->start_x) * rate;
		int y = p->start_y + (p->end_y - p->start_y) * rate;
		if (x < 0 || x >= sp->rect.w || y < 0 || y >= sp->rect.h)
			continue;
		int dw = src->w * p->scale;
		int dh = src->h * p->scale;
		int dx = sp->rect.x + x - dw / 2;
		int dy = sp->rect.y + y - dh / 2;
		switch (plugin->type) {
		case DRAW_SNOW_SCREEN_BLEND:
			gfx_copy_stretch_blend_screen(dst, dx, dy, dw, dh, src, 0, 0, src->w, src->h);
			break;
		case DRAW_SNOW_ALPHA_BLEND:
			gfx_copy_stretch_blend_amap(dst, dx, dy, dw, dh, src, 0, 0, src->w, src->h);
			break;
		}
	}
}

static cJSON *DrawSnow_to_json(struct sact_sprite *sp, bool verbose)
{
	struct draw_snow_plugin *plugin = (struct draw_snow_plugin *)sp->plugin;
	cJSON *obj = cJSON_CreateObject();
	cJSON_AddStringToObject(obj, "name", plugin->p.name);
	cJSON_AddNumberToObject(obj, "type", plugin->type);
	cJSON_AddNumberToObject(obj, "max_time", plugin->max_time);
	cJSON_AddNumberToObject(obj, "max_amp", plugin->max_amp);
	cJSON_AddNumberToObject(obj, "nr_particles", plugin->nr_particles);
	cJSON *snow_sprite = sprite_to_json(sact_get_sprite(plugin->snow_sprite), verbose);
	cJSON_AddItemToObjectCS(obj, "snow_sprite", snow_sprite);
	cJSON_AddBoolToObject(obj, "started", !!plugin->particles);
	return obj;
}

static void DrawSnow_Init(int sprite, int width, int height)
{
	struct draw_snow_plugin *plugin = xcalloc(1, sizeof(struct draw_snow_plugin));
	plugin->p.name = plugin_name;
	plugin->p.free = DrawSnow_free;
	plugin->p.update = DrawSnow_update;
	plugin->p.render = DrawSnow_render;
	plugin->p.to_json = DrawSnow_to_json;
	struct sact_sprite *sp = sact_get_sprite(sprite);
	if (!sp)
		VM_ERROR("DrawSnow.Init: invalid sprite %d", sprite);
	sp->rect.w = width;
	sp->rect.h = height;
	sprite_bind_plugin(sp, &plugin->p);
	plugin->max_time = 3000;
	plugin->max_amp = 160;
	plugin->nr_particles = 1024;
}

static void DrawSnow_Release(int sprite)
{
	if (!get_draw_snow_plugin(sprite))
		return;
	sprite_bind_plugin(sact_get_sprite(sprite), NULL);
}

static void DrawSnow_Start(int sprite)
{
	struct draw_snow_plugin *plugin = get_draw_snow_plugin(sprite);
	if (!plugin)
		return;
	if (plugin->particles)
		return;  // already started
	plugin->particles = xcalloc(plugin->nr_particles, sizeof(struct snowflake));
	struct sact_sprite *sp = sact_get_sprite(sprite);
	for (int i = 0; i < plugin->nr_particles; i++) {
		struct snowflake *p = &plugin->particles[i];
		p->start_x = rand() % sp->rect.w;
		p->start_y = -sp->rect.h + rand() % sp->rect.h;
		p->end_x = (p->start_x + rand() % plugin->max_amp) - (plugin->max_amp / 2);
		p->end_y = sp->rect.h + rand() % sp->rect.h;
		p->total_time = glm_lerp(0.8f, 1.2f, frand()) * plugin->max_time;
		p->scale = glm_lerp(0.25f, 1.f, frand());
	}
}

static void DrawSnow_SetType(int sprite, int type)
{
	struct draw_snow_plugin *plugin = get_draw_snow_plugin(sprite);
	if (!plugin)
		return;
	plugin->type = type;
}

static void DrawSnow_SetSurface(int sprite, int surface)
{
	struct draw_snow_plugin *plugin = get_draw_snow_plugin(sprite);
	if (!plugin)
		return;
	plugin->snow_sprite = surface;
}

static void DrawSnow_SetNumof(int sprite, int numof)
{
	struct draw_snow_plugin *plugin = get_draw_snow_plugin(sprite);
	if (!plugin || plugin->particles)
		return;
	plugin->nr_particles = numof;
}

static void DrawSnow_SetMaxAmp(int sprite, int max_amp)
{
	struct draw_snow_plugin *plugin = get_draw_snow_plugin(sprite);
	if (!plugin)
		return;
	plugin->max_amp = max_amp;
}

static void DrawSnow_SetMaxTime(int sprite, int max_time)
{
	struct draw_snow_plugin *plugin = get_draw_snow_plugin(sprite);
	if (!plugin)
		return;
	plugin->max_time = max_time;
}

HLL_LIBRARY(DrawSnow,
	    HLL_EXPORT(Init, DrawSnow_Init),
	    HLL_EXPORT(Release, DrawSnow_Release),
	    HLL_EXPORT(Start, DrawSnow_Start),
	    HLL_EXPORT(SetType, DrawSnow_SetType),
	    HLL_EXPORT(SetSurface, DrawSnow_SetSurface),
	    HLL_EXPORT(SetNumof, DrawSnow_SetNumof),
	    HLL_EXPORT(SetMaxAmp, DrawSnow_SetMaxAmp),
	    HLL_EXPORT(SetMaxTime, DrawSnow_SetMaxTime)
	    );
