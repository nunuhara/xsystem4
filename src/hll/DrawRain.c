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

#include <stdlib.h>

#include "cJSON.h"
#include "hll.h"
#include "plugin.h"
#include "sact.h"
#include "sprite.h"
#include "xsystem4.h"

static const char plugin_name[] = "DrawRain";

struct draw_rain_plugin {
	struct draw_plugin p;
	int nr_lines;
	int length;
	float speed;
	float angle;
	uint32_t timestamp;
	bool started;
};

static struct draw_rain_plugin *get_draw_rain_plugin(int surface)
{
	struct sact_sprite *sp = sact_try_get_sprite(surface);
	if (!sp)
		return NULL;
	if (!sp->plugin || sp->plugin->name != plugin_name)
		return NULL;
	return (struct draw_rain_plugin *)sp->plugin;
}

static void DrawRain_free(struct draw_plugin *plugin)
{
	free(plugin);
}

static void DrawRain_update(struct sact_sprite *sp)
{
	struct draw_rain_plugin *plugin = (struct draw_rain_plugin *)sp->plugin;
	if (!plugin->started)
		return;
	uint32_t timestamp = SDL_GetTicks();
	if (timestamp - plugin->timestamp < 16)
		return;
	plugin->timestamp = timestamp;

	// Unlike the AliceSoft's DrawRain, we draw random lines every frame
	// ignoring the speed parameter. The difference is not noticeable unless
	// the speed is very small.
	struct texture *dst = sprite_get_texture(sp);
	gfx_fill_amap(dst, 0, 0, dst->w, dst->h, 0);
	float rad = plugin->angle * GLM_PIf / 180.f;
	for (int i = 0; i < plugin->nr_lines / 10; i++) {
		int x1 = rand() % dst->w;
		int y1 = rand() % dst->h;
		int len = rand() % plugin->length;
		int x2 = x1 + len * sinf(rad);
		int y2 = y1 - len * cosf(rad);
		int alpha = 128 + rand() % 128;
		gfx_draw_line_to_amap(dst, x1, y1, x2, y2, alpha);
	}
	sprite_dirty(sp);
}

static cJSON *DrawRain_to_json(struct sact_sprite *sp, bool verbose)
{
	struct draw_rain_plugin *plugin = (struct draw_rain_plugin *)sp->plugin;
	cJSON *obj = cJSON_CreateObject();
	cJSON_AddStringToObject(obj, "name", plugin->p.name);
	cJSON_AddNumberToObject(obj, "nr_lines", plugin->nr_lines);
	cJSON_AddNumberToObject(obj, "length", plugin->length);
	cJSON_AddNumberToObject(obj, "speed", plugin->speed);
	cJSON_AddNumberToObject(obj, "angle", plugin->angle);
	cJSON_AddBoolToObject(obj, "started", plugin->started);
	return obj;
}

static int DrawRain_Init(int surface)
{
	struct sact_sprite *sp = sact_get_sprite(surface);
	if (!sp) {
		WARNING("DrawRain.Init: invalid surface %d", surface);
		return 0;
	}
	struct draw_rain_plugin *plugin = xcalloc(1, sizeof(struct draw_rain_plugin));
	plugin->p.name = plugin_name;
	plugin->p.free = DrawRain_free;
	plugin->p.update = DrawRain_update;
	plugin->p.to_json = DrawRain_to_json;
	plugin->nr_lines = 1000;
	plugin->length = 300;
	plugin->speed = 0.03f;
	plugin->angle = 5.f;
	sprite_bind_plugin(sp, &plugin->p);
	return 1;
}

static void DrawRain_Release(int surface)
{
	if (!get_draw_rain_plugin(surface))
		return;
	sprite_bind_plugin(sact_get_sprite(surface), NULL);
}

static void DrawRain_SetNumofLine(int surface, int line)
{
	struct draw_rain_plugin *plugin = get_draw_rain_plugin(surface);
	if (!plugin)
		return;
	plugin->nr_lines = line;
}

static void DrawRain_SetLength(int surface, int length)
{
	struct draw_rain_plugin *plugin = get_draw_rain_plugin(surface);
	if (!plugin)
		return;
	plugin->length = length;
}

static void DrawRain_SetSpeed(int surface, float speed)
{
	struct draw_rain_plugin *plugin = get_draw_rain_plugin(surface);
	if (!plugin)
		return;
	plugin->speed = speed;
}

static void DrawRain_SetAngle(int surface, float angle)
{
	struct draw_rain_plugin *plugin = get_draw_rain_plugin(surface);
	if (!plugin)
		return;
	plugin->angle = angle;
}

static void DrawRain_Start(int surface)
{
	struct draw_rain_plugin *plugin = get_draw_rain_plugin(surface);
	if (!plugin)
		return;
	plugin->started = true;
}

HLL_LIBRARY(DrawRain,
	    HLL_EXPORT(Init, DrawRain_Init),
	    HLL_EXPORT(Release, DrawRain_Release),
	    HLL_EXPORT(SetNumofLine, DrawRain_SetNumofLine),
	    HLL_EXPORT(SetLength, DrawRain_SetLength),
	    HLL_EXPORT(SetSpeed, DrawRain_SetSpeed),
	    HLL_EXPORT(SetAngle, DrawRain_SetAngle),
	    HLL_EXPORT(Start, DrawRain_Start)
	    );
