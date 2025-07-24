/* Copyright (C) 2025 kichikuou <KichikuouChrome@gmail.com>
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

#include "system4/cg.h"
#include "system4/hashtable.h"

#include "cJSON.h"
#include "asset_manager.h"
#include "gfx/gfx.h"
#include "sact.h"
#include "sprite.h"
#include "plugin.h"
#include "hll.h"

struct cg_entry {
	int refcnt;
	Texture texture[10];
};

static struct hash_table *cg_table;

static struct cg_entry *cg_table_get(int cg_no)
{
	if (!cg_table)
		return NULL;
	return ht_get_int(cg_table, cg_no, NULL);
}

static bool DrawNumeral_LoadCG(int cg_no)
{
	if (!cg_table)
		cg_table = ht_create(16);
	struct ht_slot *slot = ht_put_int(cg_table, cg_no, NULL);
	if (slot->value)
		return true;

	struct cg_entry *entry = xmalloc(sizeof(struct cg_entry));
	entry->refcnt = 0;
	for (int i = 0; i < 10; i++) {
		struct cg *cg = asset_cg_load(cg_no + i);
		if (!cg) {
			while (--i >= 0)
				gfx_delete_texture(&entry->texture[i]);
			free(entry);
			return false;
		}
		gfx_init_texture_with_cg(&entry->texture[i], cg);
		cg_free(cg);
	}
	slot->value = entry;
	return true;
}

static bool DrawNumeral_UnloadCG(int cg_no)
{
	struct cg_entry *entry = cg_table_get(cg_no);
	if (!entry || entry->refcnt > 0)
		return false;

	for (int i = 0; i < 10; i++)
		gfx_delete_texture(&entry->texture[i]);
	free(entry);
	ht_put_int(cg_table, cg_no, NULL)->value = NULL;
	return true;
}

static const char plugin_name[] = "DrawNumeral";

struct draw_numeral_plugin {
	struct draw_plugin p;
	struct cg_entry *cg_set;
	char format[8];
	int value;
	int brightness;
};

static struct draw_numeral_plugin *get_draw_numeral_plugin(int surface)
{
	struct sact_sprite *sp = sact_try_get_sprite(surface);
	if (!sp)
		return NULL;
	if (!sp->plugin || sp->plugin->name != plugin_name)
		return NULL;
	return (struct draw_numeral_plugin *)sp->plugin;
}

static void DrawNumeral_free(struct draw_plugin *plugin_)
{
	struct draw_numeral_plugin *plugin = (struct draw_numeral_plugin *)plugin_;
	plugin->cg_set->refcnt--;
	free(plugin);
}

static void DrawNumeral_render(struct sact_sprite *sp)
{
	struct draw_numeral_plugin *plugin = (struct draw_numeral_plugin *)sp->plugin;

	struct texture *dst = gfx_main_surface();

	char buf[10];
	snprintf(buf, sizeof(buf), plugin->format, plugin->value);
	for (int i = 0; buf[i]; i++) {
		if (buf[i] == ' ')
			continue;
		int digit = buf[i] - '0';
		Texture *t = &plugin->cg_set->texture[digit];
		int x = sp->rect.x + i * t->w;
		int y = sp->rect.y;
		gfx_blend_amap_bright(dst, x, y, t, 0, 0, t->w, t->h, plugin->brightness);
	}
}

static cJSON *DrawNumeral_to_json(struct sact_sprite *sp, bool verbose)
{
	struct draw_numeral_plugin *plugin = (struct draw_numeral_plugin *)sp->plugin;
	cJSON *obj = cJSON_CreateObject();
	cJSON_AddStringToObject(obj, "name", plugin->p.name);
	cJSON_AddStringToObject(obj, "format", plugin->format);
	cJSON_AddNumberToObject(obj, "value", plugin->value);
	cJSON_AddNumberToObject(obj, "brightness", plugin->brightness);
	return obj;
}

static bool DrawNumeral_Register(int sp_no, int cg_no, int width, bool zero_padding)
{
	if (width < 0 || width > 9)
		return false;  // invalid arguments

	struct cg_entry *cg_set = cg_table_get(cg_no);
	if (!cg_set)
		return false;

	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp)
		return false;

	cg_set->refcnt++;
	struct draw_numeral_plugin *plugin = xcalloc(1, sizeof(struct draw_numeral_plugin));
	plugin->p.name = plugin_name;
	plugin->p.free = DrawNumeral_free;
	plugin->p.render = DrawNumeral_render;
	plugin->p.to_json = DrawNumeral_to_json;
	plugin->cg_set = cg_set;
	snprintf(plugin->format, sizeof(plugin->format), "%%%s%dd", zero_padding ? "0" : "", width);
	plugin->value = 0;
	plugin->brightness = 255;
	sprite_bind_plugin(sp, &plugin->p);
	sp->sp.has_pixel = true;
	return true;
}

static bool DrawNumeral_Unregister(int sp_no)
{
	if (!get_draw_numeral_plugin(sp_no))
		return false;
	sprite_bind_plugin(sact_get_sprite(sp_no), NULL);
	return true;
}

static bool DrawNumeral_SetValue(int sp_no, int value)
{
	struct draw_numeral_plugin *plugin = get_draw_numeral_plugin(sp_no);
	if (!plugin)
		return false;

	if (plugin->value != value) {
		plugin->value = value;
		sprite_dirty(sact_try_get_sprite(sp_no));
	}
	return true;
}

static int DrawNumeral_GetValue(int sp_no)
{
	struct draw_numeral_plugin *plugin = get_draw_numeral_plugin(sp_no);
	if (!plugin)
		return 0;
	return plugin->value;
}

static bool DrawNumeral_SetBrightness(int sp_no, int brightness)
{
	struct draw_numeral_plugin *plugin = get_draw_numeral_plugin(sp_no);
	if (!plugin)
		return false;

	if (plugin->brightness != brightness) {
		plugin->brightness = brightness;
		sprite_dirty(sact_try_get_sprite(sp_no));
	}
	return true;
}

static int DrawNumeral_GetSingleWidth(int sp_no)
{
	struct draw_numeral_plugin *plugin = get_draw_numeral_plugin(sp_no);
	if (!plugin)
		return 0;
	return plugin->cg_set->texture[0].w;
}

HLL_LIBRARY(DrawNumeral,
	    HLL_EXPORT(LoadCG, DrawNumeral_LoadCG),
	    HLL_EXPORT(UnloadCG, DrawNumeral_UnloadCG),
	    HLL_EXPORT(Register, DrawNumeral_Register),
	    HLL_EXPORT(Unregister, DrawNumeral_Unregister),
	    HLL_EXPORT(SetValue, DrawNumeral_SetValue),
	    HLL_EXPORT(GetValue, DrawNumeral_GetValue),
	    HLL_EXPORT(SetBrightness, DrawNumeral_SetBrightness),
	    HLL_EXPORT(GetSingleWidth, DrawNumeral_GetSingleWidth)
	    );
