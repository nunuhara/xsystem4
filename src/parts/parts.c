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

#include <assert.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/hashtable.h"
#include "system4/string.h"

#include "asset_manager.h"
#include "audio.h"
#include "vm/page.h"
#include "xsystem4.h"
#include "sact.h"
#include "sprite.h"
#include "parts.h"
#include "parts_internal.h"
#include "reign.h"

struct parts_list parts_list = TAILQ_HEAD_INITIALIZER(parts_list);
static struct parts_list dirty_list = TAILQ_HEAD_INITIALIZER(dirty_list);
static struct hash_table *parts_table = NULL;
static Point root_pos = { 0, 0 };

struct parts_controller_stack ctrl_stack;
bool parts_multi_controller;

static void ctrl_stack_init(void);
static void ctrl_stack_fini(void);

#define PARTS_PARAMS_INITIALIZER (struct parts_params) { \
	.z = 1, \
	.pos = { 0, 0 }, \
	.show = true, \
	.alpha = 255, \
	.scale = { 1.0f, 1.0f }, \
	.rotation = { 0.0f, 0.0f, 0.0f }, \
	.add_color = { 0, 0, 0, 0 }, \
	.multiply_color = { 255, 255, 255, 255 } \
}

static void parts_init(struct parts *parts)
{
	parts->sp.z = 1;
	parts->sp.has_pixel = true;
	parts->sp.has_alpha = true;
	parts->sp.render = parts_sprite_render;
	parts->sp.to_json = parts_sprite_to_json;
	parts->local = PARTS_PARAMS_INITIALIZER;
	parts->global = PARTS_PARAMS_INITIALIZER;
	parts->delegate_index = -1;
	parts->want_save = true;
	parts->on_cursor_sound = -1;
	parts->on_click_sound = -1;
	parts->origin_mode = 1;
	parts->pending_parent = -1;
	parts->linked_to = -1;
	parts->linked_from = -1;
	TAILQ_INIT(&parts->children);
	TAILQ_INIT(&parts->motion);
}

static struct parts *parts_alloc(void)
{
	struct parts *parts = xcalloc(1, sizeof(struct parts));
	parts_init(parts);
	return parts;
}

void parts_component_dirty(struct parts *parts)
{
	if (parts->dirty)
		return;
	parts->dirty = true;
	TAILQ_INSERT_TAIL(&dirty_list, parts, dirty_list_entry);
}

static void dirty_list_remove(struct parts *parts)
{
	if (parts->dirty)
		TAILQ_REMOVE(&dirty_list, parts, dirty_list_entry);
}

static int parts_get_sprite_z(struct parts *parts)
{
	if (!parts_multi_controller)
		return parts->global.z;
	// The system overlay controller sorts above any in-stack controller.
	return parts->controller_no;
}

static int parts_get_sprite_z2(struct parts *parts)
{
	if (!parts_multi_controller)
		return 0;
	return parts->global.z;
}

static void parts_list_insert(struct parts *parts)
{
	int z = parts_get_sprite_z(parts);
	int z2 = parts_get_sprite_z2(parts);
	parts->sp.z = z;
	parts->sp.z2 = z2;
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		int pz = parts_get_sprite_z(p);
		int pz2 = parts_get_sprite_z2(p);
		if (pz > z || (pz == z && pz2 > z2)) {
			TAILQ_INSERT_BEFORE(p, parts, parts_list_entry);
			goto done;
		}
	}
	TAILQ_INSERT_TAIL(&parts_list, parts, parts_list_entry);
done:
	parts_engine_dirty();
	scene_register_sprite(&parts->sp);
}

static void parts_list_remove(struct parts *parts)
{
	TAILQ_REMOVE(&parts_list, parts, parts_list_entry);
	scene_unregister_sprite(&parts->sp);
}

void parts_list_resort(struct parts *parts)
{
	// TODO: this could be optimized
	parts_list_remove(parts);
	parts_list_insert(parts);
}

struct parts *parts_try_get(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (slot->value)
		return slot->value;
	return NULL;
}

struct parts *parts_get(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (slot->value)
		return slot->value;

	struct parts *parts = parts_alloc();
	parts->no = parts_no;
	parts->controller_no = ctrl_stack.active;
	slot->value = parts;
	parts_list_insert(parts);
	return parts;
}

bool parts_exists(int parts_no)
{
	return !!ht_get_int(parts_table, parts_no, NULL);
}

static void parts_state_free(struct parts_state *state)
{
	switch (state->type) {
	case PARTS_UNINITIALIZED:
	case PARTS_RECT_DETECTION:
	case PARTS_LAYOUT_BOX:
		break;
	case PARTS_CG:
		gfx_delete_texture(&state->common.texture);
		if (state->cg.name)
			free_string(state->cg.name);
		break;
	case PARTS_TEXT:
		parts_text_free(&state->text);
		break;
	case PARTS_ANIMATION:
		for (unsigned i = 0; i < state->anim.nr_frames; i++) {
			gfx_delete_texture(&state->anim.frames[i]);
		}
		free(state->anim.frames);
		break;
	case PARTS_NUMERAL:
		gfx_delete_texture(&state->common.texture);
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		gfx_delete_texture(&state->common.texture);
		gfx_delete_texture(&state->gauge.cg);
		break;
	case PARTS_CONSTRUCTION_PROCESS:
		gfx_delete_texture(&state->common.texture);
		parts_clear_construction_process(&state->cproc);
		break;
	case PARTS_FLASH:
		parts_flash_free(&state->flash);
		break;
	case PARTS_FLAT:
		parts_flat_free(&state->flat);
		break;
	case PARTS_MOVIE:
		// The texture is owned by the SACT sprite.
		state->common.texture.handle = 0;
		if (state->movie.sprite_no >= 0)
			sact_SP_Delete(state->movie.sprite_no);
		break;
	case PARTS_3DLAYER:
		state->common.texture.handle = 0;
		if (state->layer3d.plugin >= 0)
			ReignEngine_ReleasePlugin(state->layer3d.plugin);
		if (state->layer3d.sprite_no >= 0)
			sact_SP_Delete(state->layer3d.sprite_no);
		break;
	}
	memset(state, 0, sizeof(struct parts_state));
}

static struct text_style default_text_style = {
	.face = FONT_GOTHIC,
	.size = 16.0f,
	.bold_width = 0.0f,
	.weight = FW_NORMAL,
	.edge_left = 0.0f,
	.edge_up = 0.0f,
	.edge_right = 0.0f,
	.edge_down = 0.0f,
	.color = { .r = 255, .g = 255, .b = 255, .a = 255 },
	.edge_color = { .r = 0, .g = 0, .b = 0, .a = 255 },
	.scale_x = 1.0f,
	.space_scale_x = 1.0f,
	.font_spacing = 0.0f,
	.font_size = NULL
};

void parts_state_reset(struct parts_state *state, enum parts_type type)
{
	parts_state_free(state);
	state->type = type;
	switch (type) {
	case PARTS_TEXT:
		state->text.ts = default_text_style;
		break;
	case PARTS_NUMERAL:
		state->num.length = 1;
		state->num.font_no = -1;
		break;
	case PARTS_CONSTRUCTION_PROCESS:
		TAILQ_INIT(&state->cproc.ops);
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		state->gauge.cg_no = -1;
		state->gauge.rate = 0.0f;
		break;
	case PARTS_3DLAYER:
		state->layer3d.plugin = -1;
		state->layer3d.sprite_no = -1;
		break;
	case PARTS_UNINITIALIZED:
	case PARTS_CG:
	case PARTS_ANIMATION:
	case PARTS_FLASH:
	case PARTS_FLAT:
	case PARTS_RECT_DETECTION:
		break;
	case PARTS_MOVIE:
		state->movie.sprite_no = -1;
		break;
	case PARTS_LAYOUT_BOX:
		state->layout_box.layout_type = PARTS_LAYOUT_VERTICAL;
		state->layout_box.align = 1;
		break;
	}
}

struct parts_cg *parts_get_cg(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_CG) {
		parts_state_reset(&parts->states[state], PARTS_CG);
	}
	return &parts->states[state].cg;
}

struct parts_text *parts_get_text(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_TEXT) {
		parts_state_reset(&parts->states[state], PARTS_TEXT);
	}
	return &parts->states[state].text;
}

struct parts_animation *parts_get_animation(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_ANIMATION) {
		parts_state_reset(&parts->states[state], PARTS_ANIMATION);
	}
	return &parts->states[state].anim;
}

struct parts_numeral *parts_get_numeral(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_NUMERAL) {
		parts_state_reset(&parts->states[state], PARTS_NUMERAL);
	}
	return &parts->states[state].num;
}

struct parts_gauge *parts_get_hgauge(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_HGAUGE) {
		parts_state_reset(&parts->states[state], PARTS_HGAUGE);
	}
	return &parts->states[state].gauge;
}

struct parts_gauge *parts_get_vgauge(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_VGAUGE) {
		parts_state_reset(&parts->states[state], PARTS_VGAUGE);
	}
	return &parts->states[state].gauge;
}

struct parts_construction_process *parts_get_construction_process(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_CONSTRUCTION_PROCESS) {
		parts_state_reset(&parts->states[state], PARTS_CONSTRUCTION_PROCESS);
	}
	return &parts->states[state].cproc;
}

struct parts_flash *parts_get_flash(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_FLASH) {
		parts_state_reset(&parts->states[state], PARTS_FLASH);
	}
	return &parts->states[state].flash;
}

struct parts_flat *parts_get_flat(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_FLAT) {
		parts_state_reset(&parts->states[state], PARTS_FLAT);
	}
	return &parts->states[state].flat;
}

struct parts_movie *parts_get_movie(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_MOVIE) {
		parts_state_reset(&parts->states[state], PARTS_MOVIE);
	}
	return &parts->states[state].movie;
}

struct parts_layout_box *parts_get_layout_box(struct parts *parts)
{
	if (parts->states[0].type != PARTS_LAYOUT_BOX) {
		parts_state_reset(&parts->states[0], PARTS_LAYOUT_BOX);
	}
	return &parts->states[0].layout_box;
}

struct parts_3dlayer *parts_get_3dlayer(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_3DLAYER) {
		parts_state_reset(&parts->states[state], PARTS_3DLAYER);
	}
	return &parts->states[state].layer3d;
}

static Point calculate_offset(int mode, int w, int h)
{
	switch (mode) {
	case 1:  return (Point) {    0, 0    }; // top-left
	case 2:  return (Point) { -w/2, 0    }; // top-center
	case 3:  return (Point) {   -w, 0    }; // top-right
	case 4:  return (Point) {    0, -h/2 }; // middle-left
	case 5:  return (Point) { -w/2, -h/2 }; // middle-center
	case 6:  return (Point) {   -w, -h/2 }; // middle-right
	case 7:  return (Point) {    0, -h   }; // bottom-left
	case 8:  return (Point) { -w/2, -h   }; // bottom-center
	case 9:  return (Point) {   -w, -h   }; // bottom-right
	default: return (Point) { mode, (3*h)/4 }; // why...
	}
}

/*
 * Should be called when:
 *   - position (parts->pos) changes
 *   - width or height changes
 *   - origin mode changes
 */
static void parts_common_recalculate_hitbox(struct parts *parts, struct parts_common *common)
{

	if (common->surface_area.w || common->surface_area.h) {
		common->origin_offset = calculate_offset(parts->origin_mode,
				common->surface_area.w, common->surface_area.h);
		Rectangle r = { 0, 0, common->w, common->h };
		SDL_IntersectRect(&r, &common->surface_area, &common->hitbox);
		common->origin_offset.x -= common->surface_area.x;
		common->origin_offset.y -= common->surface_area.y;
		common->hitbox.x += parts->local.pos.x + common->origin_offset.x;
		common->hitbox.y += parts->local.pos.y + common->origin_offset.y;
	} else {
		common->origin_offset = calculate_offset(parts->origin_mode, common->w, common->h);
		common->hitbox = (Rectangle) {
			.x = parts->local.pos.x + common->origin_offset.x,
			.y = parts->local.pos.y + common->origin_offset.y,
			.w = common->w,
			.h = common->h,
		};
	}
}

void parts_recalculate_hitbox(struct parts *parts)
{
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		parts_common_recalculate_hitbox(parts, &parts->states[i].common);
	}
}

static void parts_update_global_pos(struct parts *parts, Point parent_pos)
{
	parts->global.pos = (Point) {
		parent_pos.x + parts->local.pos.x,
		parent_pos.y + parts->local.pos.y
	};

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_pos(child, parts->global.pos);
	}
}

void parts_set_pos(struct parts *parts, Point pos)
{
	parts->local.pos.x = pos.x;
	parts->local.pos.y = pos.y;
	parts_recalculate_hitbox(parts);
	parts_update_global_pos(parts, parts->parent ? parts->parent->global.pos : root_pos);
	parts_dirty(parts);
}

void parts_set_global_pos(Point pos)
{
	root_pos = pos;
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		parts_update_global_pos(parts, root_pos);
	}
	parts_engine_dirty();
}

static void parts_update_global_z(struct parts *parts, int parent_z)
{
	parts->global.z = parent_z + parts->local.z;
	parts_list_resort(parts);

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_z(child, parts->global.z);
	}
}

void parts_set_z(struct parts *parts, int z)
{
	if (parts->local.z == z)
		return;

	parts->local.z = z;
	parts_update_global_z(parts, parts->parent ? parts->parent->global.z : 0);
	parts_dirty(parts);
}

static void parts_update_global_show(struct parts *parts, bool parent_show)
{
	parts->global.show = parent_show && parts->local.show;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_show(child, parts->global.show);
	}
}

void parts_set_show(struct parts *parts, bool show)
{
	if (parts->local.show == show)
		return;

	parts->local.show = show;
	parts_update_global_show(parts, parts->parent ? parts->parent->global.show : true);
	parts_dirty(parts);
}

static void parts_update_global_alpha(struct parts *parts, int parent_alpha)
{
	parts->global.alpha = parent_alpha * (parts->local.alpha / 255.0f);

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_alpha(child, parts->global.alpha);
	}
}

void parts_set_alpha(struct parts *parts, int alpha)
{
	parts->local.alpha = max(0, min(255, alpha));
	parts_update_global_alpha(parts, parts->parent ? parts->parent->global.alpha : 255);
	parts_dirty(parts);
}

static void parts_update_global_add_color(struct parts *parts, SDL_Color parent_color)
{
	parts->global.add_color = (SDL_Color) {
		parent_color.r * (parts->local.add_color.r / 255.0f),
		parent_color.g * (parts->local.add_color.g / 255.0f),
		parent_color.b * (parts->local.add_color.b / 255.0f),
		0
	};

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_add_color(child, parts->global.add_color);
	}
}

void parts_set_add_color(struct parts *parts, SDL_Color color)
{
	parts->local.add_color = color;
	parts_update_global_add_color(parts, parts->parent ? parts->parent->global.add_color
			: (SDL_Color){0,0,0,0});
	parts_dirty(parts);
}

static void parts_update_global_multiply_color(struct parts *parts, SDL_Color parent_color)
{
	parts->global.multiply_color = (SDL_Color) {
		parent_color.r * (parts->local.multiply_color.r / 255.0f),
		parent_color.g * (parts->local.multiply_color.g / 255.0f),
		parent_color.b * (parts->local.multiply_color.b / 255.0f),
		255
	};

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_multiply_color(child, parts->global.multiply_color);
	}
}

void parts_set_multiply_color(struct parts *parts, SDL_Color color)
{
	parts->local.multiply_color = color;
	parts_update_global_multiply_color(parts, parts->parent ? parts->parent->global.multiply_color
			: (SDL_Color){255,255,255,255});
	parts_dirty(parts);
}

void parts_set_origin_mode(struct parts *parts, int origin_mode)
{
	parts->origin_mode = origin_mode;
	parts_recalculate_hitbox(parts);
	parts_dirty(parts);
}

static void parts_update_global_scale_x(struct parts *parts, float parent_scale_x)
{
	parts->global.scale.x = parent_scale_x * parts->local.scale.x;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_scale_x(child, parts->global.scale.x);
	}
}

void parts_set_scale_x(struct parts *parts, float mag)
{
	parts->local.scale.x = mag;
	parts_recalculate_hitbox(parts);
	parts_update_global_scale_x(parts, parts->parent ? parts->parent->global.scale.x : 1.0f);
	parts_dirty(parts);
}

static void parts_update_global_scale_y(struct parts *parts, float parent_scale_y)
{
	parts->global.scale.y = parent_scale_y * parts->local.scale.y;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_scale_y(child, parts->global.scale.y);
	}
}

void parts_set_scale_y(struct parts *parts, float mag)
{
	parts->local.scale.y = mag;
	parts_recalculate_hitbox(parts);
	parts_update_global_scale_y(parts, parts->parent ? parts->parent->global.scale.y : 1.0f);
	parts_dirty(parts);
}

static void parts_update_global_rotate_x(struct parts *parts, float parent_rot_x)
{
	parts->global.rotation.x = parent_rot_x + parts->local.rotation.x;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_rotate_x(child, parts->global.rotation.x);
	}
}

void parts_set_rotation_x(struct parts *parts, float rot)
{
	parts->local.rotation.x = rot;
	parts_update_global_rotate_x(parts, parts->parent ? parts->parent->global.rotation.x : 0.0f);
	parts_dirty(parts);
}

static void parts_update_global_rotate_y(struct parts *parts, float parent_rot_y)
{
	parts->global.rotation.y = parent_rot_y + parts->local.rotation.y;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_rotate_y(child, parts->global.rotation.y);
	}
}

void parts_set_rotation_y(struct parts *parts, float rot)
{
	parts->local.rotation.y = rot;
	parts_update_global_rotate_y(parts, parts->parent ? parts->parent->global.rotation.y : 0.0f);
	parts_dirty(parts);
}

static void parts_update_global_rotate_z(struct parts *parts, float parent_rot_z)
{
	parts->global.rotation.z = parent_rot_z + parts->local.rotation.z;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_rotate_z(child, parts->global.rotation.z);
	}
}

void parts_set_rotation_z(struct parts *parts, float rot)
{
	parts->local.rotation.z = rot;
	parts_update_global_rotate_z(parts, parts->parent ? parts->parent->global.rotation.z : 0.0f);
	parts_dirty(parts);
}

void parts_set_dims(struct parts *parts, struct parts_common *common, int w, int h)
{
	common->w = w;
	common->h = h;
	parts_common_recalculate_hitbox(parts, common);
}

bool _parts_cg_set(struct parts *parts, struct parts_cg *parts_cg, struct cg *cg, int cg_no,
		struct string *name)
{
	if (!cg)
		return false;
	gfx_delete_texture(&parts_cg->common.texture);
	gfx_init_texture_with_cg(&parts_cg->common.texture, cg);
	parts_set_dims(parts, &parts_cg->common, cg->metrics.w, cg->metrics.h);
	parts_cg->no = cg_no;
	if (parts_cg->name)
		free_string(parts_cg->name);
	parts_cg->name = name;
	parts_dirty(parts);
	cg_free(cg);
	return true;
}

bool parts_cg_set_by_index(struct parts *parts, struct parts_cg *cg, int cg_no)
{
	assert(cg_no);
	return _parts_cg_set(parts, cg, asset_cg_load(cg_no), cg_no, NULL);
}

bool parts_cg_set(struct parts *parts, struct parts_cg *parts_cg, struct string *cg_name)
{
	assert(cg_name && *(cg_name->text) != '\0');
	int no;
	struct cg *cg;
	if (!memcmp(cg_name->text, "<save>", 6)) {
		char *path = savedir_path(cg_name->text + 6);
		cg = cg_load_file(path);
		no = 0;
		free(path);
	} else {
		cg = asset_cg_load_by_name(cg_name->text, &no);
	}
	return _parts_cg_set(parts, parts_cg, cg, no, string_dup(cg_name));
}

struct parts_numeral_font *parts_numeral_fonts = NULL;
int parts_nr_numeral_fonts = 0;

void parts_numeral_font_init(struct parts_numeral_font *font)
{
	if (font->type == PARTS_NUMERAL_FONT_SEPARATE) {
		for (int i = 0; i < 12; i++) {
			struct cg *cg = asset_cg_load(font->cg_no + i);
			if (!cg) {
				font->cg[i].handle = 0;
				continue;
			}
			gfx_init_texture_with_cg(&font->cg[i], cg);
			cg_free(cg);
		}
	} else if (font->type == PARTS_NUMERAL_FONT_SEPARATE2) {
		for (int i = 0; i < 12; i++) {
			if (font->width[i] < 0)
				continue;
			struct cg *cg = asset_cg_load(font->width[i]);
			if (!cg) {
				font->cg[i].handle = 0;
				continue;
			}
			gfx_init_texture_with_cg(&font->cg[i], cg);
			cg_free(cg);
		}
	} else if (font->type == PARTS_NUMERAL_FONT_COMBINED) {
		int x = 0;
		Texture t = {0};
		struct cg *cg = asset_cg_load(font->cg_no);
		gfx_init_texture_with_cg(&t, cg);
		for (int i = 0; i < 12; i++) {
			if (font->width[i] <= 0)
				continue;
			gfx_init_texture_blank(&font->cg[i], font->width[i], t.h);
			gfx_copy_with_alpha_map(&font->cg[i], 0, 0, &t, x, 0, font->width[i], t.h);
			x += font->width[i];
		}
		gfx_delete_texture(&t);
		free(cg);
	}
}

static int parts_load_numeral_font_separate(int cg_no)
{
	// find existing font
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		if (parts_numeral_fonts[i].type == PARTS_NUMERAL_FONT_SEPARATE
				&& parts_numeral_fonts[i].cg_no == cg_no)
			return i;
	}

	// load new font
	int font_no = parts_nr_numeral_fonts++;
	parts_numeral_fonts = xrealloc_array(parts_numeral_fonts, font_no, font_no + 1,
			sizeof(struct parts_numeral_font));
	struct parts_numeral_font *font = &parts_numeral_fonts[font_no];
	font->cg_no = cg_no;
	font->type = PARTS_NUMERAL_FONT_SEPARATE;
	parts_numeral_font_init(font);
	return font_no;
}

static int parts_load_numeral_font_separate_string(struct string *cg_name)
{
	// convert name to CG indices
	int indices[12];
	for (int i = 0; i < 12; i++) {
		struct string *name = string_format(cg_name, (union vm_value){.i = i}, STRFMT_INT);
		if (!asset_exists_by_name(ASSET_CG, name->text, &indices[i])) {
			WARNING("numeral cg doesn't exist: %s", display_sjis0(name->text));
			indices[i] = -1;
		}
		free_string(name);
	}

	// find existing font
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		if (font->type != PARTS_NUMERAL_FONT_SEPARATE2)
			continue;
		bool not_match = false;
		for (int d = 0; d < 12; d++) {
			if (font->width[d] != indices[d]) {
				not_match = true;
				break;
			}
		}
		if (not_match)
			continue;
		return i;
	}

	// load new font
	int font_no = parts_nr_numeral_fonts++;
	parts_numeral_fonts = xrealloc_array(parts_numeral_fonts, font_no, font_no + 1,
			sizeof(struct parts_numeral_font));
	struct parts_numeral_font *font = &parts_numeral_fonts[font_no];
	font->cg_no = indices[0];
	font->type = PARTS_NUMERAL_FONT_SEPARATE2;
	memcpy(font->width, indices, sizeof(int) * 12);
	parts_numeral_font_init(font);
	return font_no;
}

static int parts_load_numeral_font_combined(struct cg *cg, int cg_no, int w[12])
{
	// find existing font
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		if (font->type != PARTS_NUMERAL_FONT_COMBINED || font->cg_no != cg_no)
			continue;
		for (int i = 0; i < 12; i++) {
			if (w[i] != font->width[i])
				continue;
		}
		return i;
	}

	// load new font
	int font_no = parts_nr_numeral_fonts++;
	parts_numeral_fonts = xrealloc_array(parts_numeral_fonts, font_no, font_no + 1,
			sizeof(struct parts_numeral_font));
	struct parts_numeral_font *font = &parts_numeral_fonts[font_no];
	font->cg_no = cg_no;
	font->type = PARTS_NUMERAL_FONT_COMBINED;
	memcpy(font->width, w, sizeof(int) * 12);
	parts_numeral_font_init(font);
	return font_no;
}

static bool parts_numeral_update(struct parts *parts, struct parts_numeral *num)
{
	// XXX: don't generate texture if number hasn't been set yet
	if (!num->have_num || num->font_no < 0)
		return true;
	int_least64_t n = num->num;
	bool negative = n < 0;
	if (negative)
		n *= -1;

	// extract digits
	int digits;
	uint8_t d[32];
	for (digits = 0; n; digits++) {
		d[digits] = n % 10;
		n /= 10;
	}

	// encode number as texture indices
	int nr_chars = 0;
	uint8_t chars[32];
	if (negative) {
		chars[nr_chars++] = 10;
	}
	for (int i = 0; i < digits; i++) {
		if (num->show_comma && i > 0 && i % 3 == 0) {
			chars[nr_chars++] = 11;
		}
		chars[nr_chars++] = d[i];
	}

	for (int i = digits; i < num->length; i++) {
		chars[nr_chars++] = 0;
	}

	struct parts_numeral_font *font = &parts_numeral_fonts[num->font_no];

	// determine output dimensions
	int w = 0, h = 0;
	for (int i = nr_chars-1; i >= 0; i--) {
		if (!font->cg[chars[i]].handle)
			continue;
		w += font->cg[chars[i]].w;
		h = max(h, font->cg[chars[i]].h);
	}
	w += (nr_chars-1) * num->space;

	// copy chars to texture
	gfx_delete_texture(&num->common.texture);
	gfx_init_texture_rgba(&num->common.texture, w, h, (SDL_Color){0, 0, 0, 255});

	int x = 0;
	for (int i = nr_chars-1; i>= 0; i--) {
		Texture *ch = &font->cg[chars[i]];
		if (!ch->handle)
			continue;
		gfx_copy_with_alpha_map(&num->common.texture, x, 0, ch, 0, 0, ch->w, ch->h);
		x += ch->w + num->space;
	}

	parts_set_dims(parts, &num->common, w, h);

	parts_dirty(parts);
	return true;

}

bool parts_numeral_set_number(struct parts *parts, struct parts_numeral *num, int n)
{
	num->have_num = true;
	num->num = n;
	return parts_numeral_update(parts, num);
}

void parts_set_state(struct parts *parts, enum parts_state_type state)
{
	if (parts->lock_input_state)
		return;
	while (state > PARTS_STATE_DEFAULT && parts->states[state].type == PARTS_UNINITIALIZED)
		state--;
	if (parts->state != state) {
		parts->state = state;
		parts_dirty(parts);
	}
}

void parts_set_surface_area(struct parts *parts, struct parts_common *common, int x, int y, int w, int h)
{
	if (x < 0 || y < 0)
		return;
	common->surface_area = (Rectangle) { x, y, w, h };
	parts_common_recalculate_hitbox(parts, common);
}

static bool parts_animation_update(struct parts_animation *anim, int passed_time)
{
	if (passed_time <= 0 || !anim->nr_frames)
		return false;

	const unsigned elapsed = anim->elapsed + passed_time;
	const unsigned frame_diff = elapsed / anim->frame_time;
	const unsigned remainder = elapsed % anim->frame_time;

	if (frame_diff > 0) {
		anim->elapsed = remainder;
		anim->current_frame = (anim->current_frame + frame_diff) % anim->nr_frames;
		anim->common.texture = anim->frames[anim->current_frame];
		return true;
	} else {
		anim->elapsed = elapsed;
		return false;
	}
}

static void parts_update_loop(struct parts *parts, int passed_time)
{
	bool dirty = false;
	switch (parts->states[parts->state].type) {
	case PARTS_ANIMATION:
		dirty = parts_animation_update(&parts->states[parts->state].anim, passed_time);
		break;
	case PARTS_FLASH:
		dirty = parts_flash_update(&parts->states[parts->state].flash, passed_time);
		break;
	case PARTS_FLAT:
		dirty = parts_flat_update(&parts->states[parts->state].flat, passed_time);
		break;
	default:
		break;
	}
	if (dirty)
		parts_dirty(parts);
}

static void parts_update_animation(int passed_time)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		parts_update_loop(parts, passed_time);
	}
}

void parts_release(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (!slot->value)
		return;

	struct parts *parts = slot->value;
	parts_input_reset_drag(parts);
	parts_clear_motion(parts);
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		parts_state_free(&parts->states[i]);
	}

	// break parent/child relationships
	while (!TAILQ_EMPTY(&parts->children)) {
		struct parts *child = TAILQ_FIRST(&parts->children);
		TAILQ_REMOVE(&parts->children, child, child_list_entry);
		child->parent = NULL;
	}
	if (parts->parent) {
		TAILQ_REMOVE(&parts->parent->children, parts, child_list_entry);
		parts->parent = NULL;
	}

	parts_list_remove(parts);
	dirty_list_remove(parts);
	free(parts);
	slot->value = NULL;
	parts_engine_dirty();
}

void parts_release_all(void)
{
	while (!TAILQ_EMPTY(&parts_list)) {
		struct parts *parts = TAILQ_FIRST(&parts_list);
		parts_release(parts->no);
	}

	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		for (int i = 0; i < 12; i++) {
			if (font->cg[i].handle)
				gfx_delete_texture(&font->cg[i]);
		}
	}

	free(parts_numeral_fonts);
	parts_numeral_fonts = NULL;
	parts_nr_numeral_fonts = 0;
}

static bool parts_engine_initialized = false;

void PE_enable_multi_controller(void)
{
	if (parts_multi_controller)
		return;
	assert(!parts_engine_initialized);
	parts_multi_controller = true;
}

bool PE_Init(void)
{
	if (parts_engine_initialized)
		return true;
	// XXX: Oyako Rankan doesn't call ChipmunkSpriteEngine.Init
	sact_init(16, CHIPMUNK_SPRITE_ENGINE);
	parts_table = ht_create(1024);
	parts_render_init();
	parts_debug_init();
	ctrl_stack_init();
	parts_engine_initialized = true;
	return true;
}

void PE_Reset(void)
{
	PE_ReleaseAllParts();
	PE_ReleaseMessage();
	ctrl_stack_fini();
	sact_ModuleFini();
}

static bool parts_has_dirty_parent(struct parts *parts)
{
	for (struct parts *parent = parts->parent; parent; parent = parent->parent) {
		if (parent->dirty)
			return true;
	}
	return false;
}

static void parts_combine_params(struct parts_params *parent, struct parts_params *child,
		struct parts_params *out)
{
	out->z = parent->z + child->z;
	out->pos = (Point) { parent->pos.x + child->pos.x, parent->pos.y + child->pos.y };
	out->show = parent->show && child->show;
	out->alpha = parent->alpha * (child->alpha / 255.0f);
	out->scale.x = parent->scale.x * child->scale.x;
	out->scale.y = parent->scale.y * child->scale.y;
	out->rotation.x = parent->rotation.x + child->rotation.x;
	out->rotation.y = parent->rotation.y + child->rotation.y;
	out->rotation.z = parent->rotation.z + child->rotation.z;
	out->add_color.r = parent->add_color.r * (child->add_color.r / 255.0f);
	out->add_color.g = parent->add_color.g * (child->add_color.g / 255.0f);
	out->add_color.b = parent->add_color.b * (child->add_color.b / 255.0f);
	out->multiply_color.r = parent->multiply_color.r * (child->multiply_color.r / 255.0f);
	out->multiply_color.g = parent->multiply_color.g * (child->multiply_color.g / 255.0f);
	out->multiply_color.b = parent->multiply_color.b * (child->multiply_color.b / 255.0f);
}

static void parts_update_component(struct parts *parts)
{
	if (parts->parent) {
		parts_combine_params(&parts->parent->global, &parts->local, &parts->global);
	}
	if (parts_get_sprite_z(parts) != parts->sp.z
			|| parts_get_sprite_z2(parts) != parts->sp.z2) {
		parts_list_resort(parts);
	}

	if (parts->dirty) {
		TAILQ_REMOVE(&dirty_list, parts, dirty_list_entry);
		parts->dirty = false;
	}

	parts_do_layout(parts);

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_component(child);
	}
}

void PE_UpdateComponent(possibly_unused int passed_time)
{
	while (!TAILQ_EMPTY(&dirty_list)) {
		// pop parts object from dirty list
		struct parts *parts = TAILQ_FIRST(&dirty_list);
		TAILQ_REMOVE(&dirty_list, parts, dirty_list_entry);
		parts->dirty = false;

		// update parent
		struct parts *parent;
		if (parts->pending_parent >= 0 && (parent = parts_try_get(parts->pending_parent))) {
			if (parts->parent) {
				TAILQ_REMOVE(&parts->parent->children, parts, child_list_entry);
			}
			parts->parent = parent;
			TAILQ_INSERT_TAIL(&parent->children, parts, child_list_entry);

			// if parent is layout box, mark it dirty so that it can re-layout its children
			if (parent->states[0].type == PARTS_LAYOUT_BOX)
				parts_component_dirty(parent);
		}
		// TODO: should the child be orphaned if it already has a parent and an invalid
		//       parent no is given?
		parts->pending_parent = -1;

		// don't do anything here if parent is dirty
		if (parts_has_dirty_parent(parts))
			continue;

		// update child params
		parts_update_component(parts);
	}
}

bool parts_message_window_show = true;

void PE_Update(int passed_time, bool message_window_show)
{
	parts_message_window_show = message_window_show;
	PE_UpdateComponent(passed_time);
	audio_update();
	parts_update_animation(passed_time);
	PE_UpdateInputState(passed_time);
	parts_render_update(passed_time);
}

void PE_UpdateParts(int passed_time, possibly_unused bool is_skip, bool message_window_show)
{
	parts_message_window_show = message_window_show;
	audio_update();
	parts_update_animation(passed_time);
	parts_render_update(passed_time);
}

void PE_SetDelegateIndex(int parts_no, int delegate_index)
{
	parts_get(parts_no)->delegate_index = delegate_index;
}

int PE_GetDelegateIndex(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? parts->delegate_index : -1;
}

bool PE_SetPartsCG(int parts_no, struct string *cg_name, possibly_unused int sprite_deform, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	if (!cg_name || *(cg_name->text) == '\0') {
		parts_state_reset(&parts->states[state], PARTS_CG);
		parts_dirty(parts);
		return true;
	}

	struct parts_cg *cg = parts_get_cg(parts, state);
	return parts_cg_set(parts, cg, cg_name);
}

bool PE_SetPartsCG_by_index(int parts_no, int cg_no, possibly_unused int sprite_deform, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	if (!cg_no) {
		parts_state_reset(&parts->states[state], PARTS_CG);
		parts_dirty(parts);
		return true;
	}

	struct parts_cg *cg = parts_get_cg(parts, state);
	return parts_cg_set_by_index(parts, cg, cg_no);
}

// XXX: Rance Quest
bool PE_SetPartsCG_by_string_index(int parts_no, struct string *cg_name,
		possibly_unused int sprite_deform, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	if (!cg_name) {
		parts_state_reset(&parts->states[state], PARTS_CG);
		parts_dirty(parts);
		return true;
	}

	struct parts_cg *cg = parts_get_cg(parts, state);
	int cg_no = atoi(cg_name->text);
	if (cg_no) {
		if (!parts_cg_set_by_index(parts, cg, atoi(cg_name->text)))
			return false;
		cg->name = string_ref(cg_name);
		return true;
	} else if (!memcmp(cg_name->text, "<save>SaveData\\", 15)) {
		char *path = savedir_path(cg_name->text + 15);
		bool result = _parts_cg_set(parts, cg, cg_load_file(path), 0, string_ref(cg_name));
		free(path);
		return result;
	} else {
		VM_ERROR("Invalid CG name: %s", display_sjis0(cg_name->text));
	}
}

void PE_GetPartsCGName(int parts_no, struct string **cg_name, int state)
{
	if (!parts_state_valid(--state))
		return;
	struct parts_cg *cg = parts_get_cg(parts_get(parts_no), state);
	if (cg->name) {
		if (*cg_name)
			free_string(*cg_name);
		*cg_name = string_dup(cg->name);
	}
}

bool PE_SetPartsCGSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_cg *cg = parts_get_cg(parts, state);
	parts_set_surface_area(parts, &cg->common, x, y, w, h);
	return true;
}

void PE_GetPartsCGSurfaceArea(int parts_no, int *x, int *y, int *w, int *h, int state)
{
	if (!parts_state_valid(--state))
		return;

	struct parts_cg *cg = parts_get_cg(parts_get(parts_no), state);
	*x = cg->common.surface_area.x;
	*y = cg->common.surface_area.y;
	*w = cg->common.surface_area.w;
	*h = cg->common.surface_area.h;
}

int PE_GetPartsCGNumber(int parts_no, int state)
{
	if (!parts_state_valid(--state)) {
		return false;
	}

	return parts_get_cg(parts_get(parts_no), state)->no;
}

static bool _parts_animation_set_cg(struct parts *parts, struct parts_animation *anim,
		int start_no, int nr_frames, int frame_time,
		struct cg *(*load_cg)(int no, void *data), void *data)
{
	int w = 0, h = 0;
	Texture *frames = xcalloc(nr_frames, sizeof(Texture));
	for (int i = 0; i < nr_frames; i++) {
		struct cg *cg = load_cg(start_no + i, data);
		if (!cg) {
			for (int j = 0; j < i; j++) {
				gfx_delete_texture(&frames[j]);
			}
			free(frames);
			return false;
		}
		gfx_init_texture_with_cg(&frames[i], cg);
		w = max(w, cg->metrics.w);
		h = max(h, cg->metrics.h);
		cg_free(cg);
	}

	parts_set_dims(parts, &anim->common, w, h);
	free(anim->frames);
	anim->start_no = start_no;
	anim->frames = frames;
	anim->nr_frames = nr_frames;
	anim->frame_time = frame_time;
	anim->elapsed = 0;
	anim->current_frame = 0;
	anim->common.texture = frames[0];
	return true;
}

static struct cg *load_loop_cg_by_index(int no, void *_)
{
	return asset_cg_load(no);
}

bool parts_animation_set_cg_by_index(struct parts *parts, struct parts_animation *anim,
		int cg_no, int nr_frames, int frame_time)
{
	return _parts_animation_set_cg(parts, anim, cg_no, nr_frames, frame_time,
			load_loop_cg_by_index, NULL);
}

bool PE_SetLoopCG_by_index(int parts_no, int cg_no, int nr_frames, int frame_time, int state)
{
	if (!parts_state_valid(--state))
		return false;
	if (nr_frames <= 0 || nr_frames > 10000) {
		WARNING("Invalid frame count: %d", nr_frames);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	struct parts_animation *anim = parts_get_animation(parts, state);
	return parts_animation_set_cg_by_index(parts, anim, cg_no, nr_frames, frame_time);
}

static struct cg *load_loop_cg_by_name(int no, void *data)
{
	int unused_no;
	struct string *cg_name = string_format((struct string*)data, (union vm_value){.i=no}, STRFMT_INT);
	struct cg *cg = asset_cg_load_by_name(cg_name->text, &unused_no);
	free_string(cg_name);
	return cg;
}

bool parts_animation_set_cg(struct parts *parts, struct parts_animation *anim,
		struct string *cg_name, int start_no, int nr_frames, int frame_time)
{
	bool r = _parts_animation_set_cg(parts, anim, start_no, nr_frames, frame_time,
			load_loop_cg_by_name, cg_name);
	if (r)
		anim->cg_name = string_dup(cg_name);
	return r;
}

bool PE_SetLoopCG(int parts_no, struct string *cg_name, int start_no, int nr_frames,
	int frame_time, int state)
{
	if (!parts_state_valid(--state))
		return false;
	if (nr_frames <= 0 || nr_frames > 10000) {
		WARNING("Invalid frame count: %d", nr_frames);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	struct parts_animation *anim = parts_get_animation(parts, state);
	return parts_animation_set_cg(parts, anim, cg_name, start_no, nr_frames, frame_time);
}

bool PE_SetLoopCGSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state)) {
		WARNING("Invalid parts state: %d", state);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	struct parts_animation *anim = parts_get_animation(parts, state);
	parts_set_surface_area(parts, &anim->common, x, y, w, h);
	return true;
}

static void _parts_set_gauge_cg(struct parts *parts, struct parts_gauge *g, struct cg *cg)
{
	gfx_init_texture_with_cg(&g->cg, cg);
	gfx_init_texture_with_cg(&g->common.texture, cg);

	gfx_init_texture_rgba(&g->common.texture, g->cg.w, g->cg.h, (SDL_Color){0,0,0,255});
	gfx_copy_with_alpha_map(&g->common.texture, 0, 0, &g->cg, 0, 0, g->cg.w, g->cg.h);

	parts_set_dims(parts, &g->common, g->cg.w, g->cg.h);

	parts_dirty(parts);
}

bool parts_gauge_set_cg(struct parts *parts, struct parts_gauge *g, struct string *cg_name)
{
	int cg_no;
	struct cg *cg = asset_cg_load_by_name(cg_name->text, &cg_no);
	if (!cg)
		return false;
	_parts_set_gauge_cg(parts, g, cg);
	g->cg_no = cg_no;
	return true;
}

bool parts_gauge_set_cg_by_index(struct parts *parts, struct parts_gauge *g, int cg_no)
{
	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return false;
	_parts_set_gauge_cg(parts, g, cg);
	g->cg_no = cg_no;
	return true;
}

bool PE_SetHGaugeCG(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	return parts_gauge_set_cg(parts, g, cg_name);
}

bool PE_SetHGaugeCG_by_index(int parts_no, int cg_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	return parts_gauge_set_cg_by_index(parts, g, cg_no);
}

bool PE_SetVGaugeCG(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	return parts_gauge_set_cg(parts, g, cg_name);
}

bool PE_SetVGaugeCG_by_index(int parts_no, int cg_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	return parts_gauge_set_cg_by_index(parts, g, cg_no);
}

void parts_hgauge_set_rate(struct parts *parts, struct parts_gauge *g, float rate)
{
	if (!g->common.texture.handle) {
		WARNING("HGauge texture uninitialized");
		return;
	}
	int pixels = rate * g->cg.w;
	gfx_copy_with_alpha_map(&g->common.texture, 0, 0, &g->cg, 0, 0, pixels, g->cg.h);
	gfx_fill_amap(&g->common.texture, pixels, 0, g->cg.w - pixels, g->cg.h, 0);
	parts_dirty(parts);
	g->rate = rate;
}

void parts_vgauge_set_rate(struct parts *parts, struct parts_gauge *g, float rate)
{
	if (!g->common.texture.handle) {
		WARNING("VGauge texture uninitialized");
		return;
	}
	int pixels = rate * g->cg.h;
	gfx_copy_with_alpha_map(&g->common.texture, 0, pixels, &g->cg, 0, pixels, g->cg.w, g->cg.h - pixels);
	gfx_fill_amap(&g->common.texture, 0, 0, g->cg.w, pixels, 0);
	parts_dirty(parts);
	g->rate = rate;
}

bool PE_SetHGaugeRate(int parts_no, float numerator, float denominator, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	parts_hgauge_set_rate(parts, g, numerator/denominator);
	return true;
}

bool PE_SetHGaugeRate_int(int parts_no, int numerator, int denominator, int state)
{
	return PE_SetHGaugeRate(parts_no, numerator, denominator, state);
}

bool PE_SetVGaugeRate(int parts_no, float numerator, float denominator, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	parts_vgauge_set_rate(parts, g, (float)numerator/(float)denominator);
	return true;
}

bool PE_SetVGaugeRate_int(int parts_no, int numerator, int denominator, int state)
{
	return PE_SetVGaugeRate(parts_no, numerator, denominator, state);
}

bool PE_SetHGaugeSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	parts_set_surface_area(parts, &g->common, x, y, w, h);
	return true;
}

bool PE_SetVGaugeSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	parts_set_surface_area(parts, &g->common, x, y, w, h);
	return true;
}

bool PE_SetNumeralCG(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts_numeral *n = parts_get_numeral(parts_get(parts_no), state);
	n->font_no = parts_load_numeral_font_separate_string(cg_name);
	return true;
}

bool PE_SetNumeralCG_by_index(int parts_no, int cg_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_numeral *n = parts_get_numeral(parts_get(parts_no), state);
	n->font_no = parts_load_numeral_font_separate(cg_no);
	return true;
}

bool PE_SetNumeralLinkedCGNumberWidthWidthList_by_index(int parts_no, int cg_no,
		int w0, int w1, int w2, int w3, int w4, int w5, int w6, int w7, int w8,
		int w9, int w_minus, int w_comma, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return false;

	int w[12] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w_minus, w_comma };
	struct parts_numeral *n = parts_get_numeral(parts_get(parts_no), state);
	n->font_no = parts_load_numeral_font_combined(cg, cg_no, w);
	return true;
}

bool PE_SetNumeralLinkedCGNumberWidthWidthList(int parts_no, struct string *cg_name,
		int w0, int w1, int w2, int w3, int w4, int w5, int w6, int w7, int w8,
		int w9, int w_minus, int w_comma, int state)
{
	if (!parts_state_valid(--state))
		return false;

	int no;
	struct cg *cg = asset_cg_load_by_name(cg_name->text, &no);
	if (!cg)
		return false;

	int w[12] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w_minus, w_comma };
	struct parts_numeral *n = parts_get_numeral(parts_get(parts_no), state);
	n->font_no = parts_load_numeral_font_combined(cg, no, w);
	return true;
}

bool PE_SetNumeralNumber(int parts_no, int n, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *numeral = parts_get_numeral(parts, state);
	return parts_numeral_set_number(parts, numeral, n);
}

bool PE_SetNumeralShowComma(int parts_no, bool show_comma, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *num = parts_get_numeral(parts, state);
	if (num->show_comma == show_comma)
		return true;

	num->show_comma = show_comma;
	parts_numeral_update(parts, num);
	return true;
}

bool PE_SetNumeralSpace(int parts_no, int space, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *num = parts_get_numeral(parts, state);
	if (num->space == space)
		return true;

	num->space = space;
	parts_numeral_update(parts, num);
	return true;
}

bool PE_SetNumeralLength(int parts_no, int length, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *num = parts_get_numeral(parts, state);
	if (num->length == length)
		return true;

	num->length = max(1, length);
	num->have_num = true;
	parts_numeral_update(parts, num);
	return true;
}

bool PE_SetNumeralSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *n = parts_get_numeral(parts, state);
	parts_set_surface_area(parts, &n->common, x, y, w, h);
	return true;
}

void PE_ReleaseParts(int parts_no)
{
	parts_release(parts_no);
}

void PE_ReleaseAllParts(void)
{
	parts_release_all();
}

void PE_ReleaseAllPartsWithoutSystem(void)
{
	// FIXME: what's the difference?
	parts_release_all();
}

void PE_ReleaseAllWithoutSystem(struct page **erase_number_list)
{
	// Release all parts not belonging to the system overlay controller
	struct parts *parts = TAILQ_FIRST(&parts_list);
	while (parts) {
		struct parts *next = TAILQ_NEXT(parts, parts_list_entry);
		if (parts->controller_no != PARTS_CONTROLLER_SYSTEM_OVERLAY) {
			*erase_number_list = array_pushback(*erase_number_list,
					(union vm_value){.i = parts->no}, AIN_ARRAY_INT, -1);
			parts_release(parts->no);
		}
		parts = next;
	}

	// Drop all normal controllers and add a fresh default one.
	ctrl_stack.nr_controllers = 0;
	PE_AddController(-1);
}

void PE_SetPos(int parts_no, int x, int y)
{
	parts_set_pos(parts_get(parts_no), (Point){ x, y });
}

void PE_SetZ(int parts_no, int z)
{
	parts_set_z(parts_get(parts_no), z);
}

void PE_SetShow(int parts_no, bool show)
{
	parts_set_show(parts_get(parts_no), show);
}

void PE_SetAlpha(int parts_no, int alpha)
{
	parts_set_alpha(parts_get(parts_no), alpha);
}

void PE_SetPartsDrawFilter(int parts_no, int draw_filter)
{
	if (draw_filter && draw_filter != 1)
		UNIMPLEMENTED("(%d, %d)", parts_no, draw_filter);
	parts_get(parts_no)->draw_filter = draw_filter;
}

void PE_SetAddColor(int parts_no, int r, int g, int b)
{
	SDL_Color add_color = {
		min(255, max(0, r)),
		min(255, max(0, g)),
		min(255, max(0, b)),
		255
	};
	parts_set_add_color(parts_get(parts_no), add_color);
}

void PE_SetMultiplyColor(int parts_no, int r, int g, int b)
{
	SDL_Color multiply_color = {
		min(255, max(0, r)),
		min(255, max(0, g)),
		min(255, max(0, b)),
		255
	};
	parts_set_multiply_color(parts_get(parts_no), multiply_color);
}

int PE_GetPartsX(int parts_no)
{
	return parts_get(parts_no)->local.pos.x;
}

int PE_GetPartsY(int parts_no)
{
	return parts_get(parts_no)->local.pos.y;
}

int PE_GetPartsWidth(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	return parts_get(parts_no)->states[state].common.w;
}

int PE_GetPartsHeight(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	return parts_get(parts_no)->states[state].common.h;
}

int PE_GetPartsUpperLeftPosX(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_get(parts_no);
	int x = parts->states[state].common.hitbox.x;
	if (parts->parent)
		x += parts->parent->global.pos.x;
	return x;
}

int PE_GetPartsUpperLeftPosY(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_get(parts_no);
	int y = parts->states[state].common.hitbox.y;
	if (parts->parent)
		y += parts->parent->global.pos.y;
	return y;
}

int PE_GetPartsZ(int parts_no)
{
	return parts_get(parts_no)->local.z;
}

bool PE_GetPartsShow(int parts_no)
{
	return parts_get(parts_no)->local.show;
}

int PE_GetPartsAlpha(int parts_no)
{
	return parts_get(parts_no)->local.alpha;
}

int PE_GetPartsDrawFilter(int parts_no)
{
	return parts_get(parts_no)->draw_filter;
}

void PE_GetAddColor(int parts_no, int *r, int *g, int *b)
{
	struct parts *parts = parts_get(parts_no);
	*r = parts->local.add_color.r;
	*g = parts->local.add_color.g;
	*b = parts->local.add_color.b;
}

void PE_GetMultiplyColor(int parts_no, int *r, int *g, int *b)
{
	struct parts *parts = parts_get(parts_no);
	*r = parts->local.multiply_color.r;
	*g = parts->local.multiply_color.g;
	*b = parts->local.multiply_color.b;
}

void PE_SetPartsOriginPosMode(int parts_no, int origin_pos_mode)
{
	struct parts *parts = parts_get(parts_no);
	parts_set_origin_mode(parts, origin_pos_mode);
	parts_dirty(parts);
}

int PE_GetPartsOriginPosMode(int parts_no)
{
	return parts_get(parts_no)->origin_mode;
}

void PE_SetParentPartsNumber(int parts_no, int parent_parts_no)
{
	struct parts *parts = parts_get(parts_no);
	parts->pending_parent = parent_parts_no;
	parts_component_dirty(parts);
}

int PE_GetParentPartsNumber(int parts_no)
{
	struct parts *parts = parts_get(parts_no);
	if (parts->parent)
		return parts->parent->no;
	return -1;
}

bool PE_SetPartsGroupNumber(possibly_unused int PartsNumber, possibly_unused int GroupNumber)
{
	UNIMPLEMENTED("(%d, %d)", PartsNumber, GroupNumber);
	return true;
}

void PE_SetPartsMessageWindowShowLink(possibly_unused int parts_no, bool message_window_show_link)
{
	struct parts *parts = parts_get(parts_no);
	parts->message_window = message_window_show_link;
}

bool PE_GetPartsMessageWindowShowLink(int parts_no)
{
	return parts_get(parts_no)->message_window;
}

void PE_SetPartsMagX(int parts_no, float scale_x)
{
	struct parts *parts = parts_get(parts_no);
	parts_set_scale_x(parts, scale_x);
}

float PE_GetPartsMagX(int parts_no)
{
	return parts_get(parts_no)->local.scale.x;
}

void PE_SetPartsMagY(int parts_no, float scale_y)
{
	struct parts *parts = parts_get(parts_no);
	parts_set_scale_y(parts, scale_y);
}

float PE_GetPartsMagY(int parts_no)
{
	return parts_get(parts_no)->local.scale.y;
}

void PE_SetPartsRotateX(int parts_no, float rot_x)
{
	UNIMPLEMENTED("(%d, %f)", parts_no, rot_x);
	parts_set_rotation_x(parts_get(parts_no), rot_x);
}

void PE_SetPartsRotateY(int parts_no, float rot_y)
{
	UNIMPLEMENTED("(%d, %f)", parts_no, rot_y);
	parts_set_rotation_y(parts_get(parts_no), rot_y);
}

void PE_SetPartsRotateZ(int parts_no, float rot_z)
{
	parts_set_rotation_z(parts_get(parts_no), rot_z);
}

float PE_GetPartsRotateZ(int parts_no)
{
	return parts_get(parts_no)->local.rotation.z;
}

void PE_SetPartsAlphaClipperPartsNumber(int parts_no, int alpha_clipper_parts_no)
{
	struct parts *parts = parts_get(parts_no);
	parts->alpha_clipper_parts_no = alpha_clipper_parts_no;
	parts_dirty(parts);
}

void PE_SetPartsPixelDecide(int parts_no, bool pixel_decide)
{
	//UNIMPLEMENTED("(%d, %s)", parts_no, pixel_decide ? "true" : "false");
}

bool PE_SetThumbnailReductionSize(int reduction_size)
{
	UNIMPLEMENTED("(%d)", reduction_size);
	return true;
}

bool PE_SetThumbnailMode(bool mode)
{
	UNIMPLEMENTED("(%s)", mode ? "true" : "false");
	return true;
}

void PE_SetInputState(int parts_no, int state)
{
	if (!parts_state_valid(--state)) {
		WARNING("invalid input state: %d", state);
		return;
	}
	parts_set_state(parts_get(parts_no), state);
}

int PE_GetInputState(int parts_no)
{
	return parts_get(parts_no)->state + 1;
}

void PE_SetComponentType(int parts_no, int type, int state)
{
	if (!parts_state_valid(--state))
		return;
	struct parts *parts = parts_get(parts_no);
	enum parts_type pt = PARTS_UNINITIALIZED;
	switch (type) {
	case 8:  pt = PARTS_LAYOUT_BOX; break;
	case 11: pt = PARTS_CG; break;
	case 12: pt = PARTS_ANIMATION; break;
	case 13: pt = PARTS_TEXT; break;
	case 14: pt = PARTS_HGAUGE; break;
	case 15: pt = PARTS_VGAUGE; break;
	case 16: pt = PARTS_NUMERAL; break;
	case 17: pt = PARTS_RECT_DETECTION; break;
	case 18: pt = PARTS_CONSTRUCTION_PROCESS; break;
	case 20: pt = PARTS_FLAT; break;
	case 21: pt = PARTS_3DLAYER; break;
	case 22: pt = PARTS_MOVIE; break;
	default:
		VM_ERROR("unknown component type %d", type);
	}
	if (parts->states[state].type != pt)
		parts_state_reset(&parts->states[state], pt);
}

int PE_GetComponentType(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return -1;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return -1;

	switch (parts->states[state].type) {
	case PARTS_LAYOUT_BOX: return 8;
	case PARTS_UNINITIALIZED:  // defaluts to CG
	case PARTS_CG:
		return 11;
	case PARTS_ANIMATION: return 12;
	case PARTS_TEXT: return 13;
	case PARTS_HGAUGE: return 14;
	case PARTS_VGAUGE: return 15;
	case PARTS_NUMERAL: return 16;
	case PARTS_RECT_DETECTION: return 17;
	case PARTS_CONSTRUCTION_PROCESS: return 18;
	case PARTS_FLAT: return 20;
	case PARTS_3DLAYER: return 21;
	case PARTS_MOVIE: return 22;
	case PARTS_FLASH:
		break;
	}
	VM_ERROR("unsupported component type %d", parts->states[state].type);
}

bool PE_SetPartsRectangleDetectionSize(int parts_no, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return false;
	if (parts->states[state].type != PARTS_RECT_DETECTION)
		parts_state_reset(&parts->states[state], PARTS_RECT_DETECTION);
	parts_set_dims(parts, &parts->states[state].common, w, h);
	return true;
}

bool PE_SetPartsCGDetectionSize(int parts_no, struct string *cg_name, int state)
{
	UNIMPLEMENTED("(%d, %s, %d)", parts_no, display_sjis0(cg_name->text), state);
	return false;
}

int PE_GetFreeNumber(void)
{
	// NOTE: per GUIEngine.dll (Rance 01)
	static int first_free = 1000001000;
	while (PE_IsExist(first_free)) {
		first_free++;
	}
	// XXX: the ID is incremented even if the parts is not created
	return first_free++;
}

bool PE_IsExist(int parts_no)
{
	return !!ht_get_int(parts_table, parts_no, NULL);
}

void PE_SetSpeedupRateByMessageSkip(int parts_no, int rate)
{
	if (rate != 1)
		UNIMPLEMENTED("(%d, %d)");
}

static void ctrl_stack_init(void)
{
	memset(&ctrl_stack, 0, sizeof(ctrl_stack));
	// Add initial default controller
	PE_AddController(-1);
}

static void ctrl_stack_fini(void)
{
	memset(&ctrl_stack, 0, sizeof(ctrl_stack));
}

// Adds a new controller to the stack and makes it active. The `index`
// parameter specifies the position in the stack at which to insert the new
// controller; -1 means "insert directly after the currently active
// controller". In practice the game only ever passes -1, and the active
// controller is always the top of the stack at that point, so this
// degenerates to a simple push.
int PE_AddController(int index)
{
	if (index != -1)
		VM_ERROR("index != -1 not supported (got %d)", index);
	if (ctrl_stack.nr_controllers > 0 &&
			ctrl_stack.active != ctrl_stack.nr_controllers - 1)
		VM_ERROR("active controller is not at the top of the stack");
	if (ctrl_stack.nr_controllers >= PARTS_CONTROLLER_STACK_MAX)
		VM_ERROR("controller stack overflow");

	int no = ctrl_stack.nr_controllers++;
	ctrl_stack.active = no;
	return no;
}

// Removes the controller at position `index` from the stack, releases all
// parts belonging to it, and returns their parts numbers in
// `erase_number_list`. `index == -1` means "remove the currently active
// controller". In practice the game only ever passes -1, and the active
// controller is always the top of the stack at that point, so this
// degenerates to a simple pop.
void PE_RemoveController(struct page **erase_number_list, int index)
{
	if (index != -1)
		VM_ERROR("index != -1 not supported (got %d)", index);
	if (ctrl_stack.nr_controllers == 0 ||
			ctrl_stack.active != ctrl_stack.nr_controllers - 1)
		VM_ERROR("active controller is not at the top of the stack");

	int ctrl_no = ctrl_stack.active;

	// Collect and release parts belonging to this controller
	struct parts *p = TAILQ_FIRST(&parts_list);
	while (p) {
		struct parts *next = TAILQ_NEXT(p, parts_list_entry);
		if (p->controller_no == ctrl_no) {
			*erase_number_list = array_pushback(*erase_number_list,
					(union vm_value){.i = p->no}, AIN_ARRAY_INT, -1);
			parts_release(p->no);
		}
		p = next;
	}

	ctrl_stack.nr_controllers--;
	if (ctrl_stack.nr_controllers == 0) {
		PE_AddController(-1);
	} else {
		ctrl_stack.active = ctrl_stack.nr_controllers - 1;
	}
}

void PE_set_active_controller(int controller_no)
{
	if (controller_no == PARTS_CONTROLLER_SYSTEM_OVERLAY ||
			(controller_no >= 0 && controller_no < ctrl_stack.nr_controllers))
		ctrl_stack.active = controller_no;
	else
		VM_ERROR("Invalid controller number: %d", controller_no);
}

int PE_get_active_controller(void)
{
	return ctrl_stack.active;
}

int PE_get_system_controller(void)
{
	return PARTS_CONTROLLER_SYSTEM_OVERLAY;
}

void PE_parts_set_want_save(int parts_no, bool want_save)
{
	parts_get(parts_no)->want_save = want_save;
}

float PE_parts_get_absolute_x(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? (float)parts->global.pos.x : 0.0f;
}

float PE_parts_get_absolute_y(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? (float)parts->global.pos.y : 0.0f;
}

int PE_parts_get_absolute_z(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? parts->global.z : 0;
}

void PE_parts_set_lock_input_state(int parts_no, bool lock)
{
	parts_get(parts_no)->lock_input_state = lock;
}

bool PE_init_parts_movie(int parts_no, int width, int height, int bg_r, int bg_g, int bg_b, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_movie *movie = parts_get_movie(parts, state);

	int sp_no = sact_SP_GetUnuseNum(0);
	struct sact_sprite *sp = sact_create_sprite(sp_no, width, height, bg_r, bg_g, bg_b, 255);
	if (!sp)
		return false;

	struct texture *tex = sprite_get_texture(sp);
	movie->sprite_no = sp_no;
	movie->common.texture = *tex; // XXX: textures normally shouldn't be copied like this...
	parts_set_dims(parts, &movie->common, width, height);
	parts_dirty(parts);
	return true;
}

int PE_get_movie_sprite(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return -1;

	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_MOVIE)
		return -1;

	return parts->states[state].movie.sprite_no;
}

bool PE_CreateParts3DLayerPluginID(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_get(parts_no);
	struct parts_3dlayer *l = parts_get_3dlayer(parts, state);

	if (l->plugin >= 0)
		return false;

	int handle = ReignEngine_create_plugin(RE_SEAL_PLUGIN);
	if (handle < 0)
		return false;

	int sp_no = sact_SP_GetUnuseNum(0);
	struct sact_sprite *sp = sact_create_sprite(sp_no, 1, 1, 0, 0, 0, 255);
	if (!sp) {
		ReignEngine_ReleasePlugin(handle);
		return false;
	}

	if (!ReignEngine_BindPlugin(handle, sp_no)) {
		sact_SP_Delete(sp_no);
		ReignEngine_ReleasePlugin(handle);
		return false;
	}

	l->plugin = handle;
	l->sprite_no = sp_no;
	return true;
}

int PE_GetParts3DLayerPluginID(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return -1;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_3DLAYER)
		return -1;
	return parts->states[state].layer3d.plugin;
}

bool PE_ReleaseParts3DLayerPluginID(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_3DLAYER)
		return false;
	struct parts_3dlayer *l = &parts->states[state].layer3d;
	if (l->plugin < 0)
		return false;
	ReignEngine_ReleasePlugin(l->plugin);
	l->plugin = -1;
	if (l->sprite_no >= 0) {
		sact_SP_Delete(l->sprite_no);
		l->sprite_no = -1;
	}
	return true;
}
