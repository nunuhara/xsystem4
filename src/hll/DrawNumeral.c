/* Copyright (C) 2021 kichikuou <KichikuouChrome@gmail.com>
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

#include "asset_manager.h"
#include "gfx/gfx.h"
#include "sact.h"
#include "sprite.h"
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
		struct cg *cg =asset_cg_load(cg_no + i);
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

struct sp_entry {
	struct cg_entry *cg_set;
	char format[8];
	int value;
	int brightness;
};

static struct hash_table *sp_table;

static struct sp_entry *sp_table_get(int sp_no)
{
	if (!sp_table)
		return NULL;
	return ht_get_int(sp_table, sp_no, NULL);
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

	if (!sp_table)
		sp_table = ht_create(64);
	struct ht_slot *slot = ht_put_int(sp_table, sp_no, NULL);
	if (slot->value)
		return false;  // already registered

	struct sp_entry *entry = xmalloc(sizeof(struct sp_entry));
	entry->cg_set = cg_set;
	entry->cg_set->refcnt++;
	sprintf(entry->format, "%%%s%dd", zero_padding ? "0" : "", width);
	entry->value = 0;
	entry->brightness = 255;

	sprite_init(sp, cg_set->texture[0].w * width, cg_set->texture[0].h, 0, 0, 0, 255);

	slot->value = entry;
	return true;
}

static bool DrawNumeral_Unregister(int sp_no)
{
	struct sp_entry *entry = sp_table_get(sp_no);
	if (!entry)
		return false;
	entry->cg_set->refcnt--;
	free(entry);
	ht_put_int(sp_table, sp_no, NULL)->value = NULL;
	return true;
}

static bool DrawNumeral_SetValue(int sp_no, int value)
{
	struct sp_entry *entry = sp_table_get(sp_no);
	if (!entry)
		return false;
	entry->value = value;

	// Draw.
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp)
		return false;

	struct texture *dst = sprite_get_texture(sp);
	gfx_fill_amap(dst, 0, 0, dst->w, dst->h, 0);

	char buf[10];
	sprintf(buf, entry->format, value);
	for (int i = 0; buf[i]; i++) {
		if (buf[i] == ' ')
			continue;
		int digit = buf[i] - '0';
		Texture *t = &entry->cg_set->texture[digit];
		// TODO: use entry->brightness
		gfx_copy_with_alpha_map(dst, i * t->w, 0, t, 0, 0, t->w, t->h);
	}

	sprite_dirty(sp);
	return true;
}

static int DrawNumeral_GetValue(int sp_no)
{
	struct sp_entry *entry = sp_table_get(sp_no);
	if (!entry)
		return 0;
	return entry->value;
}

static bool DrawNumeral_SetBrightness(int sp_no, int brightness)
{
	struct sp_entry *entry = sp_table_get(sp_no);
	if (!entry)
		return false;
	entry->brightness = brightness;
	return true;
}

static int DrawNumeral_GetSingleWidth(int sp_no)
{
	struct sp_entry *entry = sp_table_get(sp_no);
	if (!entry)
		return 0;
	return entry->cg_set->texture[0].w;
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
