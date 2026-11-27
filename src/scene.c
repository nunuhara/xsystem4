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

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "system4.h"
#include "system4/cg.h"

#include "asset_manager.h"
#include "cJSON.h"
#include "gfx/gfx.h"
#include "json.h"
#include "queue.h"
#include "scene.h"
#include "parts/parts_internal.h"
#include "xsystem4.h"

static int id_counter = 0;

bool scene_is_dirty = true;

static TAILQ_HEAD(listhead, sprite) sprite_list = TAILQ_HEAD_INITIALIZER(sprite_list);

static Texture wp = {0};
static bool scene_reset_registered = false;

#if defined(ALICEHUB_IOS)
void scene_end_session(void)
{
	while (!TAILQ_EMPTY(&sprite_list))
		scene_unregister_sprite(TAILQ_FIRST(&sprite_list));
	gfx_delete_texture(&wp);
	scene_reset_registered = false;
	id_counter = 0;
	scene_dirty();
}
#endif

static void scene_on_gl_reset(void *user)
{
	(void)user;
	gfx_remap_texture(&wp);
	scene_dirty();
}

void scene_reset(void)
{
	gfx_delete_texture(&wp);
	TAILQ_INIT(&sprite_list);
	id_counter = 0;
	scene_is_dirty = true;
}

void scene_register_sprite(struct sprite *sp)
{
	if (!sp || sp->in_scene)
		return;

	if (!sp->id)
		sp->id = ++id_counter;

	struct sprite *p;
	TAILQ_FOREACH_REVERSE(p, &sprite_list, listhead, entry) {
		if (p->z < sp->z || (p->z == sp->z && p->z2 <= sp->z2)) {
			TAILQ_INSERT_AFTER(&sprite_list, p, sp, entry);
			sp->in_scene = true;
			scene_dirty();
			return;
		}
	}
	TAILQ_INSERT_HEAD(&sprite_list, sp, entry);
	sp->in_scene = true;
	scene_dirty();
}

void scene_unregister_sprite(struct sprite *sp)
{
	if (!sp || !sp->in_scene)
		return;
	TAILQ_REMOVE(&sprite_list, sp, entry);
	sp->in_scene = false;
	scene_dirty();
}

void scene_render(void)
{
	struct sprite *start_sp = NULL;
	struct sprite *sp;
	TAILQ_FOREACH_REVERSE(sp, &sprite_list, listhead, entry) {
		if (sp->render == parts_sprite_render && parts_sprite_is_fullscreen_opaque(sp)) {
			start_sp = sp;
			break;
		}
	}

	if (!start_sp) {
		gfx_clear();
		gfx_remap_texture(&wp);
		if (wp.handle) {
			Rectangle r = RECT(0, 0, wp.w, wp.h);
			gfx_render_texture(&wp, &r);
		}
		start_sp = TAILQ_FIRST(&sprite_list);
	}

	bool in_parts_pass = false;
	struct sprite *next;
	for (sp = start_sp; sp; sp = next) {
		next = TAILQ_NEXT(sp, entry);
		if (sp->render) {
			bool is_parts = (sp->render == parts_sprite_render || sp->render == parts_background_render);
			if (is_parts && !in_parts_pass) {
				parts_render_pass_begin();
				in_parts_pass = true;
			} else if (!is_parts && in_parts_pass) {
				parts_render_pass_end();
				in_parts_pass = false;
			}
			sp->render(sp);
		} else {
			WARNING("sprite in scene without render function");
		}
	}
	if (in_parts_pass)
		parts_render_pass_end();
}

int scene_set_wp(int cg_no) {
	if (!cg_no) {
		gfx_delete_texture(&wp);
		scene_dirty();
		return 1;
	}

	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return 0;

	if (!scene_reset_registered) {
		gfx_register_reset_callback(scene_on_gl_reset, NULL);
		scene_reset_registered = true;
	}
	gfx_delete_texture(&wp);
	gfx_init_texture_with_cg(&wp, cg);
	cg_free(cg);
	scene_dirty();
	return 1;
}

int scene_set_wp_color(int r, int g, int b)
{
	gfx_set_clear_color(r, g, b, 255);
	scene_dirty();
	return 1;
}

int scene_set_wp_texture(Texture *tex)
{
	if (!scene_reset_registered) {
		gfx_register_reset_callback(scene_on_gl_reset, NULL);
		scene_reset_registered = true;
	}
	gfx_delete_texture(&wp);
	if (!tex || !tex->handle || tex->w <= 0 || tex->h <= 0) {
		scene_dirty();
		return 0;
	}
	gfx_init_texture_rgb(&wp, tex->w, tex->h, (SDL_Color){ 0, 0, 0, 255 });
	gfx_copy(&wp, 0, 0, tex, 0, 0, tex->w, tex->h);

	scene_dirty();
	return 1;
}

void scene_set_sprite_z(struct sprite *sp, int z)
{
	if (!sp)
		return;
	sp->z = z;
	if (sp->in_scene) {
		scene_unregister_sprite(sp);
		scene_register_sprite(sp);
	}
}

void scene_set_sprite_z2(struct sprite *sp, int z, int z2)
{
	if (!sp)
		return;
	sp->z = z;
	sp->z2 = z2;
	if (sp->in_scene) {
		scene_unregister_sprite(sp);
		scene_register_sprite(sp);
	}
}

bool scene_sprite_check_z_order(struct sprite *sp, int z, int z2)
{
	if (!sp || !sp->in_scene)
		return false;
	struct sprite *prev = TAILQ_PREV(sp, listhead, entry);
	struct sprite *next = TAILQ_NEXT(sp, entry);
	if (prev && !(prev->z < z || (prev->z == z && prev->z2 <= z2)))
		return false;
	if (next && !(z < next->z || (z == next->z && z2 <= next->z2)))
		return false;
	return true;
}

void scene_print(void)
{
	struct sprite *p;
	TAILQ_FOREACH(p, &sprite_list, entry) {
		cJSON *json = p->to_json ? p->to_json(p, false) : scene_sprite_to_json(p, false);
		char *text = cJSON_Print(json);
		sys_message("%s\n", text);
		free(text);
		cJSON_Delete(json);
	}
}

cJSON *scene_to_json(bool verbose)
{
	cJSON *ar = cJSON_CreateArray();
	struct sprite *p;
	TAILQ_FOREACH(p, &sprite_list, entry) {
		cJSON *sprite = p->to_json ? p->to_json(p, verbose) : scene_sprite_to_json(p, verbose);
		cJSON_AddItemToArray(ar, sprite);
	}
	return ar;
}

struct sprite *scene_get(int id)
{
	struct sprite *p;
	TAILQ_FOREACH(p, &sprite_list, entry) {
		if (p->id == id)
			return p;
	}
	return NULL;
}
