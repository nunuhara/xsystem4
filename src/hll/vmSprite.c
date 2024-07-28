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

#include <SDL.h>

#include "cJSON.h"
#include "effect.h"
#include "gfx/gfx.h"
#include "hll.h"
#include "id_pool.h"
#include "input.h"
#include "json.h"
#include "sact.h"
#include "scene.h"
#include "vm/page.h"
#include "vmSurface.h"

struct child_surface {
	int surface;
	int type;
	Point pos;
};

struct animation {
	int frame_s;
	int frame_e;
	int nr_frames;
	int current_frame;
	uint64_t time_origin;
	int total_frame_time;
	int frame_times[];
};

enum findability {
	NOT_FINDABLE = 0,
	FINDABLE_WHEN_VISIBLE = 1,
	ALWAYS_FINDABLE = 2
};

struct vm_sprite {
	struct sprite sp;
	int surface;
	int type;
	Rectangle rect;
	int current;
	int id;
	enum findability findability;
	struct animation *anime;
	struct id_pool children;
};

static struct id_pool pool;
static int default_layer = 200000;
static SDL_Rect dirty_rect;

static void check_array(struct page *array)
{
	if (array->type != ARRAY_PAGE || array->a_type != AIN_ARRAY_INT || array->array.rank != 1)
		VM_ERROR("Not a flat integer array");
}

static void vm_sprite_dirty(struct vm_sprite *sp)
{
	scene_sprite_dirty(&sp->sp);
	SDL_UnionRect(&dirty_rect, &sp->rect, &dirty_rect);
}

static void update_animation(void)
{
	uint64_t now = SDL_GetTicks64();

	for (int id = id_pool_get_first(&pool); id >= 0; id = id_pool_get_next(&pool, id)) {
		struct vm_sprite *sp = id_pool_get(&pool, id);
		if (!sp || !sp->anime)
			continue;
		if (sp->current < sp->anime->frame_s || sp->current > sp->anime->frame_e) {
			sp->anime->current_frame = 0;
			continue;
		}
		int t = (now - sp->anime->time_origin) % sp->anime->total_frame_time;
		int frame;
		for (frame = 0; t >= sp->anime->frame_times[frame]; frame++)
			t -= sp->anime->frame_times[frame];
		if (frame == sp->anime->current_frame)
			continue;
		sp->anime->current_frame = frame;
		vm_sprite_dirty(sp);
	}
}

static void render_surface(int surface, int type, int current, Rectangle *rect, Point *pos)
{
	struct vm_surface *sf = vm_surface_get(surface);
	Texture *src = &sf->texture;
	Texture *dst = gfx_main_surface();
	int sx = 0;
	int sy = 0;
	int dx = rect->x + (pos ? pos->x : 0);
	int dy = rect->y + (pos ? pos->y : 0);
	int w = pos ? src->w : rect->w;
	int h = pos ? src->h : rect->h;

	if (current) {
		int col = sf->w / w;
		sx = current % col * w;
		sy = current / col * h;
	}

	switch (type) {
	case 0:
		gfx_copy(dst, dx, dy, src, sx, sy, w, h);
		break;
	case 1:
		gfx_blend_amap(dst, dx, dy, src, sx, sy, w, h);
		break;
	case 2:
		gfx_copy_sprite(dst, dx, dy, src, sx, sy, w, h, COLOR(255, 0, 255, 0));
		break;
	case 3:
		gfx_blend_add_satur(dst, dx, dy, src, sx, sy, w, h);
		break;
	default:
		WARNING("unknown vmSprite type %d", type);
		break;
	}
}

static void vm_sprite_render(struct sprite *_sp)
{
	struct vm_sprite *sp = (struct vm_sprite*)_sp;
	if (sp->current >= 0) {
		int current = sp->current + (sp->anime ? sp->anime->current_frame : 0);
		render_surface(sp->surface, sp->type, current, &sp->rect, NULL);
	}

	for (int id = id_pool_get_first(&sp->children); id >= 0; id = id_pool_get_next(&sp->children, id)) {
		struct child_surface *child = id_pool_get(&sp->children, id);
		if (!child)
			continue;
		render_surface(child->surface, child->type, 0, &sp->rect, &child->pos);
	}
}

static cJSON *vm_sprite_to_json(struct sprite *_sp, bool verbose)
{
	struct vm_sprite* sp = (struct vm_sprite*)_sp;
	cJSON *ent, *sprite, *children;

	ent = scene_sprite_to_json(_sp, verbose);
	cJSON_AddItemToObjectCS(ent, "vmSprite", sprite = cJSON_CreateObject());

	cJSON_AddNumberToObject(sprite, "surface", sp->surface);
	struct vm_surface *sf = vm_surface_get(sp->surface);
	if (sf && sf->cg_no) {
		cJSON_AddNumberToObject(sprite, "cg_no", sf->cg_no);
	}

	cJSON_AddNumberToObject(sprite, "type", sp->type);
	cJSON_AddItemToObjectCS(sprite, "rect", rectangle_to_json(&sp->rect, verbose));
	cJSON_AddNumberToObject(sprite, "current", sp->current);
	cJSON_AddNumberToObject(sprite, "findability", sp->findability);

	cJSON_AddItemToObjectCS(sprite, "children", children = cJSON_CreateArray());
	for (int id = id_pool_get_first(&sp->children); id >= 0; id = id_pool_get_next(&sp->children, id)) {
		struct child_surface *child = id_pool_get(&sp->children, id);
		if (!child)
			continue;
		cJSON *c;
		cJSON_AddItemToArray(children, c = cJSON_CreateObject());
		cJSON_AddNumberToObject(c, "surface", child->surface);
		cJSON_AddNumberToObject(c, "type", child->type);
		cJSON_AddItemToObjectCS(c, "pos", point_to_json(&child->pos, verbose));
	}

	return ent;
}

static void vm_sprite_delete(struct vm_sprite *sp)
{
	if (!sp)
		return;
	scene_unregister_sprite(&sp->sp);
	for (int id = id_pool_get_first(&sp->children); id >= 0; id = id_pool_get_next(&sp->children, id)) {
		struct child_surface *child = id_pool_get(&sp->children, id);
		if (child)
			free(child);
	}
	if (sp->anime)
		free(sp->anime);
	id_pool_delete(&sp->children);
	free(sp);
}

static void vmSprite_New(possibly_unused void *imainsystem)
{
	if (pool.slots)
		return;  // already initialized
	id_pool_init(&pool, ID_POOL_HANDLE_BASE);
}

static int vmSprite_EnumHandle(void)
{
	return id_pool_count(&pool);
}

static int vmSprite_Open(int surface, int type, int width, int height)
{
	int no = id_pool_get_unused(&pool);
	struct vm_sprite *sp = xcalloc(1, sizeof(struct vm_sprite));
	id_pool_set(&pool, no, sp);
	sp->sp.z = ++default_layer;
	sp->sp.has_pixel = true;
	sp->sp.hidden = true;
	sp->sp.id = no;
	sp->sp.render = vm_sprite_render;
	sp->sp.to_json = vm_sprite_to_json;
	sp->surface = surface;
	sp->type = type;
	sp->rect.w = width;
	sp->rect.h = height;
	sp->id = -1;
	return no;
}

static int vmSprite_Reopen(int sprite, int surface, int type, int width, int height)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	if (!sp)
		return 0;
	sp->surface = surface;
	sp->type = type;
	sp->rect.w = width;
	sp->rect.h = height;
	return sprite;
}

static void vmSprite_Close(int sprite)
{
	vm_sprite_delete(id_pool_release(&pool, sprite));
}

static int vmSprite_Release(void)
{
	for (int i = id_pool_get_first(&pool); i >= 0; i = id_pool_get_next(&pool, i))
		vmSprite_Close(i);
	id_pool_delete(&pool);
	return 1;
}

static void vmSprite_ModuleFini(void)
{
	vmSprite_Release();
}

static int vmSprite_SetLayer(int sprite, int layer)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	if (!sp)
		return 0;
	scene_set_sprite_z(&sp->sp, layer);
	return 1;
}

static void vmSprite_SetPos(int sprite, int x, int y)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	if (sp) {
		vm_sprite_dirty(sp);
		sp->rect.x = x;
		sp->rect.y = y;
		vm_sprite_dirty(sp);
	}
}

static int vmSprite_SetCurrent(int sprite, int current)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	if (!sp)
		return 0;
	if (sp->current != current) {
		sp->current = current;
		if (sp->anime) {
			sp->anime->time_origin = SDL_GetTicks64();
		}
		vm_sprite_dirty(sp);
	}
	return 1;
}

static int vmSprite_SetCurrentArray1(struct page *array, int current)
{
	if (!array)
		return 0;
	check_array(array);

	for (int i = 0; i < array->nr_vars; i++) {
		vmSprite_SetCurrent(array->values[i].i, current);
	}
	return 1;
}

static int vmSprite_SetCurrentArray2(struct page *sprite_array, struct page *current_array)
{
	if (!sprite_array || !current_array)
		return 0;
	check_array(sprite_array);
	check_array(current_array);
	if (sprite_array->nr_vars != current_array->nr_vars)
		return 0;
	for (int i = 0; i < sprite_array->nr_vars; i++) {
		vmSprite_SetCurrent(sprite_array->values[i].i, current_array->values[i].i);
	}
	return 1;
}

static int vmSprite_SetShow(int sprite, int show)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	if (!sp)
		return 0;
	scene_set_sprite_show(&sp->sp, show);
	vm_sprite_dirty(sp);
	return 1;
}

static int vmSprite_SetShowArray1(struct page *array, int show)
{
	if (!array)
		return 0;
	check_array(array);

	for (int i = 0; i < array->nr_vars; i++) {
		vmSprite_SetShow(array->values[i].i, show);
	}
	return 1;
}

static int vmSprite_SetShowArray2(struct page *sprite_array, struct page *show_array)
{
	if (!sprite_array || !show_array)
		return 0;
	check_array(sprite_array);
	check_array(show_array);
	if (sprite_array->nr_vars != show_array->nr_vars)
		return 0;
	for (int i = 0; i < sprite_array->nr_vars; i++) {
		vmSprite_SetShow(sprite_array->values[i].i, show_array->values[i].i);
	}
	return 1;
}

static void vmSprite_SetRenewal(int sprite)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	if (!sp)
		return;
	vm_sprite_dirty(sp);
}

static void vmSprite_SetFind(int sprite, int find)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	if (sp)
		sp->findability = find;
}

static void vmSprite_SetID(int sprite, int id)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	if (sp)
		sp->id = id;
}

static int vmSprite_SetAnime(int sprite, int frame_s, int frame_e, struct page *array)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	if (!sp)
		return 0;
	check_array(array);
	if (sp->anime)
		free(sp->anime);
	int nr_frames = array->nr_vars;
	struct animation *a = xmalloc(sizeof(struct animation) + nr_frames * sizeof(int));
	a->frame_s = frame_s;
	a->frame_e = frame_e;
	a->nr_frames = nr_frames;
	a->current_frame = 0;
	a->time_origin = SDL_GetTicks64();
	a->total_frame_time = 0;
	for (int i = 0; i < a->nr_frames; i++) {
		a->total_frame_time += a->frame_times[i] = array->values[i].i;
	}
	sp->anime = a;
	return 1;
}

static void vmSprite_ResetAnime(int sprite)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	if (!sp)
		return;
	free(sp->anime);
	sp->anime = NULL;
}

static int vmSprite_SetChildSurface(int sprite, int num, int surface, int type, int x, int y)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	if (!sp)
		return 0;

	struct child_surface *child = id_pool_get(&sp->children, num);
	if (!child) {
		child = xcalloc(1, sizeof(struct child_surface));
		id_pool_set(&sp->children, num, child);
	}
	child->surface = surface;
	child->type = type;
	child->pos.x = x;
	child->pos.y = y;
	return 1;
}

static int vmSprite_ResetChildSurface(int sprite, int num)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	if (!sp)
		return 0;

	struct child_surface *child = id_pool_release(&sp->children, num);
	if (child)
		free(child);
	return 1;
}

static void vmSprite_Draw(int update)
{
	handle_events();
	update_animation();
	if (scene_is_dirty) {
		scene_render();
		if (update)
			gfx_swap();
		scene_is_dirty = false;
		dirty_rect = RECT(0, 0, 0, 0);
	}
}

static void vmSprite_DrawEffect(int effect, int time)
{
	// XXX: This function should only apply the effect to `dirty_rect`, while
	// sact_Effect applies the effect to the whole screen. In most cases it
	// doesn't make a difference because (1) the effect is no-op for pixels
	// where the old and new colors are the same (e.g. crossfade), or (2) the
	// whole screen is dirty. In Mamanyonyo, the exception is
	// EFFECT_ROTATE_OUT_CW, which must be applied to `dirty_rect` correctly.
	if (effect != EFFECT_ROTATE_OUT_CW) {
		sact_Effect(effect, time, 0);
		return;
	}

	if (SDL_RectEmpty(&dirty_rect))
		return;
	uint64_t start = SDL_GetTicks64();
	Texture *screen = gfx_main_surface();
	Texture base, src;
	gfx_init_texture_blank(&src, dirty_rect.w, dirty_rect.h);
	gfx_copy(&src, 0, 0, screen, dirty_rect.x, dirty_rect.y, dirty_rect.w, dirty_rect.h);
	scene_render();
	gfx_copy_main_surface(&base);

	uint64_t t = SDL_GetTicks64() - start;
	float cx = dirty_rect.x + dirty_rect.w / 2.0f;
	float cy = dirty_rect.y + dirty_rect.h / 2.0f;
	while (t < time) {
		float rate = (float)t / time;

		gfx_clear();
		gfx_copy(screen, 0, 0, &base, 0, 0, screen->w, screen->h);
		gfx_copy_rot_zoom2(screen, cx, cy, &src, src.w / 2.0f, src.h / 2.0f, rate * -360, 1.0f - rate);
		gfx_swap();

		uint64_t t2 = SDL_GetTicks64() - start;
		if (t2 < t + 16) {
			SDL_Delay(t + 16 - t2);
			t += 16;
		} else {
			t = t2;
		}
	}
	gfx_delete_texture(&base);
	gfx_delete_texture(&src);
}

static int vmSprite_AddRect(int x, int y, int width, int height)
{
	SDL_Rect r = RECT(x, y, width, height);
	SDL_UnionRect(&dirty_rect, &r, &dirty_rect);
	scene_dirty();
	return 1;
}

//void vmSprite_SetSurface(int hSurface);

static int vmSprite_FindPos(int x, int y)
{
	int found = 0;
	int z = 0;
	Point p = { .x = x, .y = y };
	for (int id = id_pool_get_first(&pool); id >= 0; id = id_pool_get_next(&pool, id)) {
		struct vm_sprite *sp = id_pool_get(&pool, id);
		if (!sp || sp->findability == NOT_FINDABLE)
			continue;
		if (sp->findability == FINDABLE_WHEN_VISIBLE && sp->sp.hidden)
			continue;
		if (!SDL_PointInRect(&p, &sp->rect))
			continue;
		if (found && sp->sp.z < z)
			continue;
		found = id;
		z = sp->sp.z;
	}
	return found;
}

//int vmSprite_FindSprite(int sprite);

static int vmSprite_GetSurface(int sprite)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	return sp ? sp->surface : 0;
}

static int vmSprite_GetType(int sprite)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	return sp ? sp->type : 0;
}

static int vmSprite_GetWidth(int sprite)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	return sp ? sp->rect.w : 0;
}

static int vmSprite_GetHeight(int sprite)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	return sp ? sp->rect.h : 0;
}

static int vmSprite_GetLayer(int sprite)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	return sp ? sp->sp.z : 0;
}

static int vmSprite_GetX(int sprite)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	return sp ? sp->rect.x : 0;
}

static int vmSprite_GetY(int sprite)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	return sp ? sp->rect.y : 0;
}

static int vmSprite_GetCurrent(int sprite)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	return sp ? sp->current : 0;
}

static int vmSprite_GetShow(int sprite)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	return sp ? scene_get_sprite_show(&sp->sp) : 0;
}

//int vmSprite_GetRenewal(int sprite);

static int vmSprite_GetFind(int sprite)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	return sp ? sp->findability : 0;
}

static int vmSprite_GetID(int sprite)
{
	struct vm_sprite *sp = id_pool_get(&pool, sprite);
	return sp ? sp->id : -1;
}

HLL_LIBRARY(vmSprite,
	    HLL_EXPORT(_ModuleFini, vmSprite_ModuleFini),
	    HLL_EXPORT(New, vmSprite_New),
	    HLL_EXPORT(Release, vmSprite_Release),
	    HLL_EXPORT(EnumHandle, vmSprite_EnumHandle),
	    HLL_EXPORT(Open, vmSprite_Open),
	    HLL_EXPORT(Reopen, vmSprite_Reopen),
	    HLL_EXPORT(Close, vmSprite_Close),
	    HLL_EXPORT(SetLayer, vmSprite_SetLayer),
	    HLL_EXPORT(SetPos, vmSprite_SetPos),
	    HLL_EXPORT(SetCurrent, vmSprite_SetCurrent),
	    HLL_EXPORT(SetCurrentArray1, vmSprite_SetCurrentArray1),
	    HLL_EXPORT(SetCurrentArray2, vmSprite_SetCurrentArray2),
	    HLL_EXPORT(SetShow, vmSprite_SetShow),
	    HLL_EXPORT(SetShowArray1, vmSprite_SetShowArray1),
	    HLL_EXPORT(SetShowArray2, vmSprite_SetShowArray2),
	    HLL_EXPORT(SetRenewal, vmSprite_SetRenewal),
	    HLL_EXPORT(SetFind, vmSprite_SetFind),
	    HLL_EXPORT(SetID, vmSprite_SetID),
	    HLL_EXPORT(SetAnime, vmSprite_SetAnime),
	    HLL_EXPORT(ResetAnime, vmSprite_ResetAnime),
	    HLL_EXPORT(SetChildSurface, vmSprite_SetChildSurface),
	    HLL_EXPORT(ResetChildSurface, vmSprite_ResetChildSurface),
	    HLL_EXPORT(Draw, vmSprite_Draw),
	    HLL_EXPORT(DrawEffect, vmSprite_DrawEffect),
	    HLL_EXPORT(AddRect, vmSprite_AddRect),
	    HLL_TODO_EXPORT(SetSurface, vmSprite_SetSurface),
	    HLL_EXPORT(FindPos, vmSprite_FindPos),
	    HLL_TODO_EXPORT(FindSprite, vmSprite_FindSprite),
	    HLL_EXPORT(GetSurface, vmSprite_GetSurface),
	    HLL_EXPORT(GetType, vmSprite_GetType),
	    HLL_EXPORT(GetWidth, vmSprite_GetWidth),
	    HLL_EXPORT(GetHeight, vmSprite_GetHeight),
	    HLL_EXPORT(GetLayer, vmSprite_GetLayer),
	    HLL_EXPORT(GetX, vmSprite_GetX),
	    HLL_EXPORT(GetY, vmSprite_GetY),
	    HLL_EXPORT(GetCurrent, vmSprite_GetCurrent),
	    HLL_EXPORT(GetShow, vmSprite_GetShow),
	    HLL_TODO_EXPORT(GetRenewal, vmSprite_GetRenewal),
	    HLL_EXPORT(GetFind, vmSprite_GetFind),
	    HLL_EXPORT(GetID, vmSprite_GetID)
	    );
