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
#include <limits.h>
#include <math.h>
#include <cglm/cglm.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/hashtable.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "asset_manager.h"
#include "audio.h"
#include "movie.h"
#include "vm/page.h"
#include "xsystem4.h"
#include "sact.h"
#include "sprite.h"
#include "parts.h"
#include "parts_activity.h"
#include "parts_internal.h"
#include "reign.h"

static struct parts_context live_context = {
	.list = TAILQ_HEAD_INITIALIZER(live_context.list),
	.dirty = TAILQ_HEAD_INITIALIZER(live_context.dirty),
	.next_free_no = 1000010000,
};
struct parts_context *parts_current = &live_context;
#define dirty_list (parts_current->dirty)
#define parts_table (parts_current->table)
#define root_pos (parts_current->origin)

bool parts_multi_controller;
bool parts_v2 = false;
bool parts_hierarchical_matrix = true;

static void ctrl_stack_init(void);
static void parts_change_controller(struct parts *parts, int index);

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
	if (parts_v2) {
		parts->local.z = 0;
		parts->global.z = 0;
	}
	parts->delegate_index = -1;
	parts->comment = string_ref(&EMPTY_STRING);
	parts->user_component_name = string_ref(&EMPTY_STRING);
	parts->text_box_text = string_ref(&EMPTY_STRING);
	parts->want_save = true;
	parts->enable_input = true;
	parts->enable_speed_up = true;
	parts->delegate_index = -1;
	parts->event_unique_id = -1;
	parts->on_cursor_sound = -1;
	parts->on_click_sound = -1;
	for (int i = 0; i < PARTS_NR_STATES; i++)
		parts->sound_names[i] = string_ref(&EMPTY_STRING);
	parts->origin_mode = 1;
	parts->pending_parent = -1;
	parts->linked_to = -1;
	parts->linked_from = -1;
	TAILQ_INIT(&parts->children);
	TAILQ_INIT(&parts->motion);
	glm_mat4_identity(parts->committed_matrix);
}

static struct parts *parts_alloc(void)
{
	struct parts *parts = xcalloc(1, sizeof(struct parts));
	parts_init(parts);
	parts->world_matrix_self = parts;
	return parts;
}

void parts_component_dirty(struct parts *parts)
{
	parts_render_gen++;
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
	PARTS_LIST_FOREACH_REVERSE(p) {
		int pz = parts_get_sprite_z(p);
		int pz2 = parts_get_sprite_z2(p);
		if (pz < z || (pz == z && pz2 <= z2)) {
			TAILQ_INSERT_AFTER(&parts_list, p, parts, parts_list_entry);
			goto done;
		}
	}
	TAILQ_INSERT_HEAD(&parts_list, parts, parts_list_entry);
done:
	parts_engine_dirty();
	if (!parts_current->background)
		scene_register_sprite(&parts->sp);
}

static void parts_list_remove(struct parts *parts)
{
	TAILQ_REMOVE(&parts_list, parts, parts_list_entry);
	scene_unregister_sprite(&parts->sp);
}

void parts_list_resort(struct parts *parts)
{
	int z = parts_get_sprite_z(parts);
	int z2 = parts_get_sprite_z2(parts);
	struct parts *prev = TAILQ_PREV(parts, parts_list_head, parts_list_entry);
	struct parts *next = TAILQ_NEXT(parts, parts_list_entry);
	bool ok_prev = !prev || prev->sp.z < z || (prev->sp.z == z && prev->sp.z2 <= z2);
	bool ok_next = !next || next->sp.z > z || (next->sp.z == z && next->sp.z2 >= z2);
	if (ok_prev && ok_next &&
			(parts_current->background || scene_sprite_check_z_order(&parts->sp, z, z2))) {
		parts->sp.z = z;
		parts->sp.z2 = z2;
		parts_engine_dirty();
		return;
	}
	parts_list_remove(parts);
	parts_list_insert(parts);
}

struct parts *parts_try_get(int parts_no)
{
	return ht_get_int(parts_table, parts_no, NULL);
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
	if (parts_v2) {
		int root = ctrl_stack.active == PARTS_CONTROLLER_SYSTEM_OVERLAY
			? ctrl_stack.overlay_id : ctrl_stack.ids[ctrl_stack.active];
		struct parts *parent = root != parts_no ? parts_try_get(root) : NULL;
		if (parent) {
			parts->parent = parent;
			TAILQ_INSERT_TAIL(&parent->children, parts, child_list_entry);
			parts_component_dirty(parts);
		}
	}
	return parts;
}

bool parts_exists(int parts_no)
{
	return !!ht_get_int(parts_table, parts_no, NULL);
}

void parts_common_free_alpha_mask(struct parts_common *common)
{
	free(common->alpha_mask);
	common->alpha_mask = NULL;
	common->alpha_w = 0;
	common->alpha_h = 0;
}

static void build_alpha_mask_bits(const uint8_t *rgba, int w, int h, bool has_alpha,
		uint8_t **mask_out, int *aw_out, int *ah_out)
{
	*mask_out = NULL;
	*aw_out = 0;
	*ah_out = 0;
	if (w <= 0 || h <= 0)
		return;
	if (!has_alpha || !rgba) {
		*aw_out = -1;
		*ah_out = -1;
		return;
	}
	size_t n = (size_t)w * (size_t)h;
	size_t first_zero = n;
	for (size_t i = 0; i < n; i++) {
		if (rgba[i * 4 + 3] == 0) {
			first_zero = i;
			break;
		}
	}
	if (first_zero == n) {
		*aw_out = -1;
		*ah_out = -1;
		return;
	}
	size_t nr_bytes = (n + 7) >> 3;
	uint8_t *mask = xmalloc(nr_bytes);
	size_t full_bytes = first_zero >> 3;
	if (full_bytes > 0)
		memset(mask, 0xff, full_bytes);
	for (size_t b = full_bytes; b < nr_bytes; b++) {
		size_t base = b << 3;
		uint8_t bits = 0;
		if (base + 8 <= n) {
			const uint8_t *p = rgba + base * 4 + 3;
			if (p[0])  bits |= 1u << 0;
			if (p[4])  bits |= 1u << 1;
			if (p[8])  bits |= 1u << 2;
			if (p[12]) bits |= 1u << 3;
			if (p[16]) bits |= 1u << 4;
			if (p[20]) bits |= 1u << 5;
			if (p[24]) bits |= 1u << 6;
			if (p[28]) bits |= 1u << 7;
		} else {
			for (size_t k = 0; base + k < n; k++) {
				if (rgba[(base + k) * 4 + 3] != 0)
					bits |= (uint8_t)(1u << k);
			}
		}
		mask[b] = bits;
	}
	*mask_out = mask;
	*aw_out = w;
	*ah_out = h;
}

static void parts_common_build_alpha_mask(struct parts_common *common,
		const uint8_t *rgba, int w, int h, bool has_alpha)
{
	parts_common_free_alpha_mask(common);
	build_alpha_mask_bits(rgba, w, h, has_alpha,
			&common->alpha_mask, &common->alpha_w, &common->alpha_h);
}

void parts_common_set_alpha_from_cg(struct parts_common *common, const struct cg *cg)
{
	if (!cg) {
		parts_common_free_alpha_mask(common);
		return;
	}
	parts_common_build_alpha_mask(common, cg->pixels, cg->metrics.w, cg->metrics.h,
			cg->metrics.has_alpha);
}

#define PARTS_CG_HASH_BUCKETS 2048

#ifndef PARTS_CG_IDLE_VRAM_BUDGET
#if defined(__ANDROID__) || defined(ALICEHUB_IOS) || defined(__SWITCH__)
#define PARTS_CG_IDLE_VRAM_BUDGET (96u * 1024u * 1024u)
#else
#define PARTS_CG_IDLE_VRAM_BUDGET (192u * 1024u * 1024u)
#endif
#endif

#ifndef PARTS_CG_IDLE_MAX_COUNT
#if defined(__ANDROID__) || defined(ALICEHUB_IOS) || defined(__SWITCH__)
#define PARTS_CG_IDLE_MAX_COUNT 256
#else
#define PARTS_CG_IDLE_MAX_COUNT 512
#endif
#endif

struct parts_cg_asset {
	char *name;
	int cg_no;
	uint64_t archive_gen;
	Texture texture;
	int w, h;
	bool has_alpha;
	int refcount;
	size_t vram_bytes;
	uint8_t *alpha_mask;
	int alpha_w, alpha_h;
	bool alpha_mask_built;
	bool in_idle_lru;
	bool in_name_table;
	bool in_id_table;
	TAILQ_ENTRY(parts_cg_asset) all_entry;
	TAILQ_ENTRY(parts_cg_asset) idle_lru_entry;
	struct parts_cg_asset *name_next;
	struct parts_cg_asset *id_next;
};

static TAILQ_HEAD(parts_cg_asset_list, parts_cg_asset) cg_asset_all =
	TAILQ_HEAD_INITIALIZER(cg_asset_all);
static TAILQ_HEAD(parts_cg_asset_lru, parts_cg_asset) cg_asset_idle_lru =
	TAILQ_HEAD_INITIALIZER(cg_asset_idle_lru);
static struct parts_cg_asset *cg_asset_by_name[PARTS_CG_HASH_BUCKETS];
static struct parts_cg_asset *cg_asset_by_id[PARTS_CG_HASH_BUCKETS];
static int cg_asset_idle_count = 0;
static size_t cg_asset_idle_vram_bytes = 0;
static size_t parts_cg_idle_vram_budget = PARTS_CG_IDLE_VRAM_BUDGET;
static int parts_cg_idle_max_count = PARTS_CG_IDLE_MAX_COUNT;
static uint64_t next_cg_load_gen = 1;
static uint64_t cg_cache_archive_gen = 0;

static unsigned cg_hash_str(const char *s)
{
	unsigned h = 5381u;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++)
		h = ((h << 5) + h) + *p;
	return h;
}

static unsigned cg_hash_int(int k)
{
	uint32_t x = (uint32_t)k;
	x = ((x >> 16) ^ x) * 0x45d9f3bu;
	x = ((x >> 16) ^ x) * 0x45d9f3bu;
	x = (x >> 16) ^ x;
	return (unsigned)x;
}

static void parts_cg_asset_insert_tables(struct parts_cg_asset *asset)
{
	if (asset->name && asset->name[0]) {
		unsigned b = cg_hash_str(asset->name) & (PARTS_CG_HASH_BUCKETS - 1);
		asset->name_next = cg_asset_by_name[b];
		cg_asset_by_name[b] = asset;
		asset->in_name_table = true;
	}
	if (asset->cg_no > 0) {
		unsigned b = cg_hash_int(asset->cg_no) & (PARTS_CG_HASH_BUCKETS - 1);
		bool duplicate = false;
		for (struct parts_cg_asset *e = cg_asset_by_id[b]; e; e = e->id_next) {
			if (e->cg_no == asset->cg_no) {
				duplicate = true;
				break;
			}
		}
		if (!duplicate) {
			asset->id_next = cg_asset_by_id[b];
			cg_asset_by_id[b] = asset;
			asset->in_id_table = true;
		}
	}
}

static void parts_cg_asset_remove_tables(struct parts_cg_asset *asset)
{
	if (asset->in_name_table && asset->name) {
		unsigned b = cg_hash_str(asset->name) & (PARTS_CG_HASH_BUCKETS - 1);
		for (struct parts_cg_asset **pp = &cg_asset_by_name[b]; *pp; pp = &(*pp)->name_next) {
			if (*pp == asset) {
				*pp = asset->name_next;
				break;
			}
		}
		asset->name_next = NULL;
		asset->in_name_table = false;
	}
	if (asset->in_id_table) {
		unsigned b = cg_hash_int(asset->cg_no) & (PARTS_CG_HASH_BUCKETS - 1);
		for (struct parts_cg_asset **pp = &cg_asset_by_id[b]; *pp; pp = &(*pp)->id_next) {
			if (*pp == asset) {
				*pp = asset->id_next;
				break;
			}
		}
		asset->id_next = NULL;
		asset->in_id_table = false;
	}
}

static void parts_cg_asset_destroy(struct parts_cg_asset *asset)
{
	if (asset->in_idle_lru) {
		TAILQ_REMOVE(&cg_asset_idle_lru, asset, idle_lru_entry);
		asset->in_idle_lru = false;
		cg_asset_idle_count--;
		cg_asset_idle_vram_bytes -= asset->vram_bytes;
	}
	parts_cg_asset_remove_tables(asset);
	TAILQ_REMOVE(&cg_asset_all, asset, all_entry);
	gfx_delete_texture(&asset->texture);
	free(asset->alpha_mask);
	free(asset->name);
	free(asset);
}

static void parts_cg_asset_evict_idle(size_t max_bytes, int max_count)
{
	while (!TAILQ_EMPTY(&cg_asset_idle_lru) &&
			(cg_asset_idle_vram_bytes > max_bytes || cg_asset_idle_count > max_count)) {
		struct parts_cg_asset *victim = TAILQ_FIRST(&cg_asset_idle_lru);
		parts_cg_asset_destroy(victim);
	}
}

static void parts_cg_cache_sync_archive_gen(void)
{
	uint64_t gen = asset_cg_archive_gen();
	if (gen == cg_cache_archive_gen)
		return;
	cg_cache_archive_gen = gen;
	parts_cg_asset_evict_idle(0, 0);
	struct parts_cg_asset *asset;
	TAILQ_FOREACH(asset, &cg_asset_all, all_entry) {
		parts_cg_asset_remove_tables(asset);
	}
}

static void parts_cg_asset_ref(struct parts_cg_asset *asset)
{
	if (!asset)
		return;
	if (asset->refcount == 0 && asset->in_idle_lru) {
		TAILQ_REMOVE(&cg_asset_idle_lru, asset, idle_lru_entry);
		asset->in_idle_lru = false;
		cg_asset_idle_count--;
		cg_asset_idle_vram_bytes -= asset->vram_bytes;
	}
	asset->refcount++;
}

static void parts_cg_asset_unref(struct parts_cg_asset *asset)
{
	if (!asset)
		return;
	assert(asset->refcount > 0);
	asset->refcount--;
	if (asset->refcount == 0) {
		if (!asset->texture.handle || (!asset->in_name_table && !asset->in_id_table)) {
			parts_cg_asset_destroy(asset);
			return;
		}
		if (!asset->in_idle_lru) {
			TAILQ_INSERT_TAIL(&cg_asset_idle_lru, asset, idle_lru_entry);
			asset->in_idle_lru = true;
			cg_asset_idle_count++;
			cg_asset_idle_vram_bytes += asset->vram_bytes;
		}
		parts_cg_asset_evict_idle(parts_cg_idle_vram_budget, parts_cg_idle_max_count);
	}
}

static struct parts_cg_asset *parts_cg_asset_find_by_name(const char *name)
{
	if (!name || !name[0])
		return NULL;
	unsigned b = cg_hash_str(name) & (PARTS_CG_HASH_BUCKETS - 1);
	for (struct parts_cg_asset *e = cg_asset_by_name[b]; e; e = e->name_next) {
		if (e->name && !strcmp(e->name, name)) {
			if (e->texture.handle && gfx_remap_texture(&e->texture) && e->texture.handle)
				return e;
			if (e->refcount == 0)
				parts_cg_asset_destroy(e);
			return NULL;
		}
	}
	return NULL;
}

static struct parts_cg_asset *parts_cg_asset_find_by_id(int cg_no)
{
	if (cg_no <= 0)
		return NULL;
	unsigned b = cg_hash_int(cg_no) & (PARTS_CG_HASH_BUCKETS - 1);
	for (struct parts_cg_asset *e = cg_asset_by_id[b]; e; e = e->id_next) {
		if (e->cg_no == cg_no) {
			if (e->texture.handle && gfx_remap_texture(&e->texture) && e->texture.handle)
				return e;
			if (e->refcount == 0)
				parts_cg_asset_destroy(e);
			return NULL;
		}
	}
	return NULL;
}

static struct parts_cg_asset *parts_cg_asset_create_from_cg_with_mask(struct cg *cg, int cg_no,
		const char *name, uint64_t archive_gen, bool insert_tables,
		bool has_prebuilt_mask, uint8_t *alpha_mask, int alpha_w, int alpha_h)
{
	struct parts_cg_asset *asset = xcalloc(1, sizeof(*asset));
	gfx_init_texture_with_cg(&asset->texture, cg);
	asset->w = cg->metrics.w;
	asset->h = cg->metrics.h;
	asset->has_alpha = cg->metrics.has_alpha;
	asset->cg_no = cg_no;
	asset->archive_gen = archive_gen;
	if (name && name[0])
		asset->name = xstrdup(name);
	asset->vram_bytes = (asset->w > 0 && asset->h > 0)
		? (size_t)asset->w * (size_t)asset->h * 4u : 0u;
	asset->alpha_mask_built = true;
	if (has_prebuilt_mask) {
		asset->alpha_mask = alpha_mask;
		asset->alpha_w = alpha_w;
		asset->alpha_h = alpha_h;
	} else {
		build_alpha_mask_bits(cg->pixels, cg->metrics.w, cg->metrics.h,
				cg->metrics.has_alpha, &asset->alpha_mask,
				&asset->alpha_w, &asset->alpha_h);
	}
	asset->refcount = 1;
	TAILQ_INSERT_TAIL(&cg_asset_all, asset, all_entry);
	if (insert_tables)
		parts_cg_asset_insert_tables(asset);
	return asset;
}

static struct parts_cg_asset *parts_cg_asset_create_from_cg(struct cg *cg, int cg_no,
		const char *name, uint64_t archive_gen, bool insert_tables)
{
	return parts_cg_asset_create_from_cg_with_mask(cg, cg_no, name, archive_gen,
			insert_tables, false, NULL, 0, 0);
}

static void parts_cg_asset_ensure_alpha_mask(struct parts_cg_asset *asset)
{
	if (asset->alpha_mask_built)
		return;
	asset->alpha_mask_built = true;
	if (asset->w <= 0 || asset->h <= 0)
		return;
	if (!asset->has_alpha || asset->texture.opaque) {
		asset->alpha_w = -1;
		asset->alpha_h = -1;
		return;
	}
	if (asset->texture.handle != 0 && gfx_remap_texture(&asset->texture)
			&& asset->texture.handle != 0
			&& asset->texture.w > 0 && asset->texture.h > 0) {
		uint8_t *pixels = gfx_get_pixels(&asset->texture);
		if (pixels) {
			build_alpha_mask_bits(pixels, asset->texture.w, asset->texture.h, true,
					&asset->alpha_mask, &asset->alpha_w, &asset->alpha_h);
			free(pixels);
			return;
		}
	}
	struct cg *cg = NULL;
	if (asset->name && asset->name[0]) {
		int unused_no = 0;
		cg = asset_cg_load_by_name(asset->name, &unused_no);
	}
	if (!cg && asset->cg_no > 0)
		cg = asset_cg_load(asset->cg_no);
	if (cg) {
		build_alpha_mask_bits(cg->pixels, cg->metrics.w, cg->metrics.h, cg->metrics.has_alpha,
				&asset->alpha_mask, &asset->alpha_w, &asset->alpha_h);
		cg_free(cg);
	} else {
		asset->alpha_w = -1;
		asset->alpha_h = -1;
	}
}

bool parts_common_check_alpha(struct parts_state *state, int px, int py)
{
	struct parts_common *common = &state->common;
	if (px < 0 || py < 0 || px >= common->w || py >= common->h)
		return false;
	if (state->type == PARTS_CG && state->cg.cg_asset) {
		struct parts_cg_asset *asset = state->cg.cg_asset;
		if (!asset->alpha_mask_built)
			parts_cg_asset_ensure_alpha_mask(asset);
		if (asset->alpha_w == -1)
			return true;
		if (!asset->alpha_mask || px >= asset->alpha_w || py >= asset->alpha_h)
			return false;
		size_t bit = (size_t)py * (size_t)asset->alpha_w + (size_t)px;
		return (asset->alpha_mask[bit >> 3] & (1u << (bit & 7))) != 0;
	}
	if (common->alpha_w == -1)
		return true;
	if (common->alpha_w == 0) {
		if (state->type == PARTS_RECT_DETECTION && state->rect.is_cg_detection
				&& state->rect.cg_name && state->rect.cg_name->size > 0) {
			int cg_no;
			struct cg *cg = asset_cg_load_by_name(state->rect.cg_name->text, &cg_no);
			if (cg) {
				parts_common_set_alpha_from_cg(common, cg);
				cg_free(cg);
			} else {
				common->alpha_w = -1;
				common->alpha_h = -1;
			}
		} else if (common->texture.handle != 0) {
			if (common->texture.opaque) {
				common->alpha_w = -1;
				common->alpha_h = -1;
			} else if (common->texture.w > 0 && common->texture.h > 0) {
				uint8_t *pixels = gfx_get_pixels(&common->texture);
				parts_common_build_alpha_mask(common, pixels,
						common->texture.w, common->texture.h, true);
				free(pixels);
			}
		} else {
			return true;
		}
	}
	if (common->alpha_w == -1)
		return true;
	if (!common->alpha_mask || px >= common->alpha_w || py >= common->alpha_h)
		return false;
	size_t bit = (size_t)py * (size_t)common->alpha_w + (size_t)px;
	return (common->alpha_mask[bit >> 3] & (1u << (bit & 7))) != 0;
}

static void parts_cg_detach(struct parts_cg *cg)
{
	if (cg->cg_asset) {
		parts_cg_asset_unref(cg->cg_asset);
		cg->cg_asset = NULL;
		cg->common.texture = (Texture){0};
	} else {
		gfx_delete_texture(&cg->common.texture);
	}
	parts_common_free_alpha_mask(&cg->common);
	if (cg->name) {
		free_string(cg->name);
		cg->name = NULL;
	}
	cg->no = 0;
	cg->suspended_w = 0;
	cg->suspended_h = 0;
}

static inline bool is_save_cg_name(const char *s)
{
	return s && !strncmp(s, "<save>", 6);
}

static void parts_cg_resume_update_dims(struct parts *parts, struct parts_cg *cg,
		struct parts_cg_asset *asset)
{
	bool intrinsic_changed = cg->suspended_w > 0 && cg->suspended_h > 0
		&& (asset->w != cg->suspended_w || asset->h != cg->suspended_h);
	bool dims_match_intrinsic = cg->common.w == cg->suspended_w && cg->common.h == cg->suspended_h;
	if (intrinsic_changed && dims_match_intrinsic) {
		if (parts)
			parts_set_dims(parts, &cg->common, asset->w, asset->h);
		else {
			cg->common.w = asset->w;
			cg->common.h = asset->h;
		}
	}
	cg->suspended_w = 0;
	cg->suspended_h = 0;
}

void parts_cg_suspend(struct parts_cg *cg)
{
	if (!cg || !cg->cg_asset)
		return;
	cg->suspended_w = cg->cg_asset->w;
	cg->suspended_h = cg->cg_asset->h;
	parts_cg_asset_unref(cg->cg_asset);
	cg->cg_asset = NULL;
	cg->common.texture = (Texture){0};
}

void parts_cg_resume(struct parts *parts, struct parts_cg *cg)
{
	if (!cg || cg->cg_asset || cg->common.texture.handle)
		return;
	const char *name = (cg->name && cg->name->size > 0) ? cg->name->text : NULL;
	if (is_save_cg_name(name))
		return;
	parts_cg_cache_sync_archive_gen();
	struct parts_cg_asset *asset = name ? parts_cg_asset_find_by_name(name) : NULL;
	if (!asset && cg->no > 0)
		asset = parts_cg_asset_find_by_id(cg->no);
	if (asset) {
		parts_cg_asset_ref(asset);
		cg->cg_asset = asset;
		cg->common.texture = asset->texture;
		parts_cg_resume_update_dims(parts, cg, asset);
		return;
	}
	struct cg *loaded = NULL;
	int cg_no = cg->no;
	const char *cache_name = NULL;
	if (name && asset_cg_supports_name()) {
		loaded = asset_cg_load_by_name(name, &cg_no);
		if (loaded)
			cache_name = name;
	}
	if (!loaded && cg->no > 0) {
		cg_no = cg->no;
		loaded = asset_cg_load(cg->no);
		cache_name = NULL;
	}
	if (!loaded)
		return;
	asset = parts_cg_asset_create_from_cg(loaded, cg_no, cache_name, cg_cache_archive_gen, true);
	cg_free(loaded);
	cg->cg_asset = asset;
	cg->common.texture = asset->texture;
	if (cg_no > 0)
		cg->no = cg_no;
	parts_cg_resume_update_dims(parts, cg, asset);
}

static void parts_cg_cache_clear(void)
{
	parts_cg_asset_evict_idle(0, 0);
}

/*
 * Portable SDL2 background CG decode worker pool for PartsEngine.SetPartsCGThread.
 * Workers run lock-protected archive raw reads + lock-free parallel CPU decode;
 * GL texture upload and part attachment run on the render thread with a frame budget.
 */
#define PARTS_CG_ASYNC_BUCKETS 256
#define PARTS_CG_MAX_WORKERS 4
#define PARTS_CG_UPLOAD_BUDGET_MS 6

enum parts_cg_job_state {
	CG_JOB_QUEUED = 0,
	CG_JOB_DECODING = 1,
	CG_JOB_DONE = 2,
};

struct parts_cg_async_target {
	int parts_no;
	int state;
	uint64_t gen;
	struct parts_cg_async_target *next;
};

struct parts_cg_async_job {
	char *cg_name;
	uint64_t archive_gen;
	enum parts_cg_job_state state;
	struct cg *cg;
	int cg_no;
	uint8_t *alpha_mask;
	int alpha_w, alpha_h;
	bool alpha_mask_built;
	bool sync_claimed;
	struct parts_cg_async_target *targets;
	struct parts_cg_async_target *targets_tail;
	TAILQ_ENTRY(parts_cg_async_job) queue_entry;
	struct parts_cg_async_job *hash_next;
};

static struct {
	SDL_mutex *mutex;
	SDL_cond *work_cond;
	SDL_cond *done_cond;
	SDL_Thread *workers[PARTS_CG_MAX_WORKERS];
	int nr_workers;
	bool initialized;
	bool shutdown;
	int nr_queued;
	int nr_decoding;
	int nr_ready;
	TAILQ_HEAD(, parts_cg_async_job) pending_q;
	TAILQ_HEAD(, parts_cg_async_job) ready_q;
	struct parts_cg_async_job *active_buckets[PARTS_CG_ASYNC_BUCKETS];
} cg_async;

static void parts_cg_attach_asset(struct parts *parts, struct parts_cg *parts_cg,
		struct parts_cg_asset *asset, struct string *name);

static struct parts_cg_async_job *cg_async_find_job_locked(const char *cg_name, uint64_t archive_gen)
{
	unsigned b = cg_hash_str(cg_name) & (PARTS_CG_ASYNC_BUCKETS - 1);
	for (struct parts_cg_async_job *j = cg_async.active_buckets[b]; j; j = j->hash_next) {
		if (j->archive_gen == archive_gen && !strcmp(j->cg_name, cg_name))
			return j;
	}
	return NULL;
}

static void cg_async_insert_job_locked(struct parts_cg_async_job *job)
{
	unsigned b = cg_hash_str(job->cg_name) & (PARTS_CG_ASYNC_BUCKETS - 1);
	job->hash_next = cg_async.active_buckets[b];
	cg_async.active_buckets[b] = job;
}

static void cg_async_remove_job_locked(struct parts_cg_async_job *job)
{
	unsigned b = cg_hash_str(job->cg_name) & (PARTS_CG_ASYNC_BUCKETS - 1);
	for (struct parts_cg_async_job **pp = &cg_async.active_buckets[b]; *pp; pp = &(*pp)->hash_next) {
		if (*pp == job) {
			*pp = job->hash_next;
			break;
		}
	}
	job->hash_next = NULL;
}

static void parts_cg_async_free_job(struct parts_cg_async_job *job)
{
	if (!job)
		return;
	if (job->cg)
		cg_free(job->cg);
	free(job->alpha_mask);
	struct parts_cg_async_target *t = job->targets;
	while (t) {
		struct parts_cg_async_target *next = t->next;
		free(t);
		t = next;
	}
	free(job->cg_name);
	free(job);
}

static int parts_cg_worker_main(void *unused)
{
	(void)unused;
	SDL_LockMutex(cg_async.mutex);
	for (;;) {
		while (!cg_async.shutdown && TAILQ_EMPTY(&cg_async.pending_q)) {
			SDL_CondWait(cg_async.work_cond, cg_async.mutex);
		}
		if (cg_async.shutdown)
			break;
		struct parts_cg_async_job *job = TAILQ_FIRST(&cg_async.pending_q);
		TAILQ_REMOVE(&cg_async.pending_q, job, queue_entry);
		cg_async.nr_queued--;
		job->state = CG_JOB_DECODING;
		cg_async.nr_decoding++;
		const char *name = job->cg_name;
		SDL_UnlockMutex(cg_async.mutex);

		int cg_no = 0;
		struct cg *cg = asset_cg_load_by_name(name, &cg_no);
		uint8_t *alpha_mask = NULL;
		int alpha_w = 0, alpha_h = 0;
		bool alpha_built = false;
		if (cg) {
			build_alpha_mask_bits(cg->pixels, cg->metrics.w, cg->metrics.h,
					cg->metrics.has_alpha, &alpha_mask, &alpha_w, &alpha_h);
			alpha_built = true;
		}

		SDL_LockMutex(cg_async.mutex);
		job->cg = cg;
		job->cg_no = cg_no;
		job->alpha_mask = alpha_mask;
		job->alpha_w = alpha_w;
		job->alpha_h = alpha_h;
		job->alpha_mask_built = alpha_built;
		job->state = CG_JOB_DONE;
		cg_async.nr_decoding--;
		if (!job->sync_claimed) {
			TAILQ_INSERT_TAIL(&cg_async.ready_q, job, queue_entry);
			cg_async.nr_ready++;
		}
		SDL_CondBroadcast(cg_async.done_cond);
	}
	SDL_UnlockMutex(cg_async.mutex);
	return 0;
}

static bool parts_cg_async_ensure_started(void)
{
	if (cg_async.initialized)
		return true;
	cg_async.mutex = SDL_CreateMutex();
	cg_async.work_cond = SDL_CreateCond();
	cg_async.done_cond = SDL_CreateCond();
	if (!cg_async.mutex || !cg_async.work_cond || !cg_async.done_cond) {
		if (cg_async.done_cond) SDL_DestroyCond(cg_async.done_cond);
		if (cg_async.work_cond) SDL_DestroyCond(cg_async.work_cond);
		if (cg_async.mutex) SDL_DestroyMutex(cg_async.mutex);
		cg_async.mutex = NULL;
		cg_async.work_cond = NULL;
		cg_async.done_cond = NULL;
		return false;
	}
	TAILQ_INIT(&cg_async.pending_q);
	TAILQ_INIT(&cg_async.ready_q);
	memset(cg_async.active_buckets, 0, sizeof(cg_async.active_buckets));
	cg_async.shutdown = false;
	cg_async.nr_queued = 0;
	cg_async.nr_decoding = 0;
	cg_async.nr_ready = 0;

	int cpus = SDL_GetCPUCount();
	int n = cpus / 2;
	if (n < 2)
		n = 2;
	if (n > PARTS_CG_MAX_WORKERS)
		n = PARTS_CG_MAX_WORKERS;

	cg_async.nr_workers = 0;
	for (int i = 0; i < n; i++) {
		SDL_Thread *th = SDL_CreateThread(parts_cg_worker_main, "parts-cg", NULL);
		if (th)
			cg_async.workers[cg_async.nr_workers++] = th;
	}
	if (cg_async.nr_workers == 0) {
		SDL_DestroyCond(cg_async.done_cond);
		SDL_DestroyCond(cg_async.work_cond);
		SDL_DestroyMutex(cg_async.mutex);
		cg_async.done_cond = NULL;
		cg_async.work_cond = NULL;
		cg_async.mutex = NULL;
		return false;
	}
	cg_async.initialized = true;
	return true;
}

static void parts_cg_async_shutdown(void)
{
	if (!cg_async.initialized)
		return;
	SDL_LockMutex(cg_async.mutex);
	cg_async.shutdown = true;
	SDL_CondBroadcast(cg_async.work_cond);
	SDL_UnlockMutex(cg_async.mutex);

	for (int i = 0; i < cg_async.nr_workers; i++) {
		if (cg_async.workers[i]) {
			SDL_WaitThread(cg_async.workers[i], NULL);
			cg_async.workers[i] = NULL;
		}
	}
	cg_async.nr_workers = 0;

	while (!TAILQ_EMPTY(&cg_async.pending_q)) {
		struct parts_cg_async_job *job = TAILQ_FIRST(&cg_async.pending_q);
		TAILQ_REMOVE(&cg_async.pending_q, job, queue_entry);
		parts_cg_async_free_job(job);
	}
	while (!TAILQ_EMPTY(&cg_async.ready_q)) {
		struct parts_cg_async_job *job = TAILQ_FIRST(&cg_async.ready_q);
		TAILQ_REMOVE(&cg_async.ready_q, job, queue_entry);
		parts_cg_async_free_job(job);
	}
	memset(cg_async.active_buckets, 0, sizeof(cg_async.active_buckets));
	cg_async.nr_queued = 0;
	cg_async.nr_decoding = 0;
	cg_async.nr_ready = 0;

	SDL_DestroyCond(cg_async.done_cond);
	SDL_DestroyCond(cg_async.work_cond);
	SDL_DestroyMutex(cg_async.mutex);
	cg_async.done_cond = NULL;
	cg_async.work_cond = NULL;
	cg_async.mutex = NULL;
	cg_async.shutdown = false;
	cg_async.initialized = false;
}

static struct parts_cg_asset *parts_cg_async_finish_job(struct parts_cg_async_job *job, bool retain_asset)
{
	parts_cg_cache_sync_archive_gen();
	bool current_gen = (job->archive_gen == cg_cache_archive_gen);
	bool any_live = false;
	for (struct parts_cg_async_target *t = job->targets; t; t = t->next) {
		struct parts *p = parts_try_get(t->parts_no);
		if (p && p->states[t->state].type == PARTS_CG
				&& p->states[t->state].cg.load_gen == t->gen) {
			any_live = true;
			break;
		}
	}

	struct parts_cg_asset *asset = NULL;
	if (job->cg && (any_live || retain_asset)) {
		if (current_gen) {
			asset = parts_cg_asset_find_by_name(job->cg_name);
			if (asset) {
				parts_cg_asset_ref(asset);
			} else {
				asset = parts_cg_asset_create_from_cg_with_mask(job->cg, job->cg_no,
						job->cg_name, job->archive_gen, true,
						job->alpha_mask_built, job->alpha_mask,
						job->alpha_w, job->alpha_h);
				if (job->alpha_mask_built) {
					job->alpha_mask = NULL;
					job->alpha_mask_built = false;
				}
				parts_frame_did_heavy_load = true;
			}
		} else {
			asset = parts_cg_asset_create_from_cg_with_mask(job->cg, job->cg_no,
					job->cg_name, job->archive_gen, false,
					job->alpha_mask_built, job->alpha_mask,
					job->alpha_w, job->alpha_h);
			if (job->alpha_mask_built) {
				job->alpha_mask = NULL;
				job->alpha_mask_built = false;
			}
			parts_frame_did_heavy_load = true;
		}
	}

	size_t name_len = strlen(job->cg_name);
	for (struct parts_cg_async_target *t = job->targets; t; t = t->next) {
		struct parts *p = parts_try_get(t->parts_no);
		if (!p || p->states[t->state].type != PARTS_CG
				|| p->states[t->state].cg.load_gen != t->gen)
			continue;
		p->states[t->state].cg.async_gen = 0;
		if (asset) {
			struct parts_cg *pcg = parts_get_cg(p, t->state);
			parts_cg_attach_asset(p, pcg, asset, make_string(job->cg_name, name_len));
			if (p->textures_suspended)
				parts_cg_suspend(pcg);
		}
	}

	parts_cg_async_free_job(job);
	if (asset && !retain_asset) {
		parts_cg_asset_unref(asset);
		return NULL;
	}
	return asset;
}

static bool parts_has_pending_async_cg(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return false;
	for (int st = 0; st < PARTS_NR_STATES; st++) {
		if (parts->states[st].type == PARTS_CG && parts->states[st].cg.async_gen != 0)
			return true;
	}
	return false;
}

static bool cg_async_job_targets_live_part(const struct parts_cg_async_job *job, int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return false;
	for (const struct parts_cg_async_target *t = job->targets; t; t = t->next) {
		if (t->parts_no == parts_no && parts->states[t->state].type == PARTS_CG
				&& parts->states[t->state].cg.load_gen == t->gen)
			return true;
	}
	return false;
}

void parts_cg_async_flush_part(int parts_no)
{
	if (!cg_async.initialized || parts_current != &live_context)
		return;
	if (!parts_has_pending_async_cg(parts_no))
		return;
	SDL_LockMutex(cg_async.mutex);
	while (parts_has_pending_async_cg(parts_no)) {
		if (!TAILQ_EMPTY(&cg_async.ready_q)) {
			struct parts_cg_async_job *job = TAILQ_FIRST(&cg_async.ready_q);
			TAILQ_REMOVE(&cg_async.ready_q, job, queue_entry);
			cg_async.nr_ready--;
			cg_async_remove_job_locked(job);
			SDL_UnlockMutex(cg_async.mutex);

			parts_cg_async_finish_job(job, false);

			SDL_LockMutex(cg_async.mutex);
			continue;
		}
		struct parts_cg_async_job *queued = NULL;
		TAILQ_FOREACH(queued, &cg_async.pending_q, queue_entry) {
			if (cg_async_job_targets_live_part(queued, parts_no))
				break;
		}
		if (queued) {
			TAILQ_REMOVE(&cg_async.pending_q, queued, queue_entry);
			cg_async.nr_queued--;
			cg_async_remove_job_locked(queued);
			SDL_UnlockMutex(cg_async.mutex);

			queued->archive_gen = asset_cg_archive_gen();
			int cg_no = 0;
			queued->cg = asset_cg_load_by_name(queued->cg_name, &cg_no);
			queued->cg_no = cg_no;
			parts_cg_async_finish_job(queued, false);

			SDL_LockMutex(cg_async.mutex);
			continue;
		}
		if (cg_async.shutdown || (cg_async.nr_queued == 0 && cg_async.nr_decoding == 0))
			break;
		SDL_CondWait(cg_async.done_cond, cg_async.mutex);
	}
	SDL_UnlockMutex(cg_async.mutex);
}

void parts_cg_async_flush_all(void)
{
	if (!cg_async.initialized || parts_current != &live_context)
		return;
	SDL_LockMutex(cg_async.mutex);
	for (;;) {
		while (!TAILQ_EMPTY(&cg_async.ready_q)) {
			struct parts_cg_async_job *job = TAILQ_FIRST(&cg_async.ready_q);
			TAILQ_REMOVE(&cg_async.ready_q, job, queue_entry);
			cg_async.nr_ready--;
			cg_async_remove_job_locked(job);
			SDL_UnlockMutex(cg_async.mutex);

			parts_cg_async_finish_job(job, false);

			SDL_LockMutex(cg_async.mutex);
		}
		if (cg_async.shutdown || (cg_async.nr_queued == 0 && cg_async.nr_decoding == 0))
			break;
		SDL_CondWait(cg_async.done_cond, cg_async.mutex);
	}
	SDL_UnlockMutex(cg_async.mutex);
}

static void parts_cg_async_drain_ready(int budget_ms)
{
	if (!cg_async.initialized || parts_current != &live_context)
		return;
	SDL_LockMutex(cg_async.mutex);
	if (TAILQ_EMPTY(&cg_async.ready_q)) {
		SDL_UnlockMutex(cg_async.mutex);
		return;
	}
	uint64_t freq = SDL_GetPerformanceFrequency();
	uint64_t budget_ticks = (budget_ms > 0 && freq)
		? (freq * (uint64_t)budget_ms) / 1000u : 0u;
	uint64_t t_start = SDL_GetPerformanceCounter();
	int processed = 0;

	while (!TAILQ_EMPTY(&cg_async.ready_q)) {
		struct parts_cg_async_job *job = TAILQ_FIRST(&cg_async.ready_q);
		TAILQ_REMOVE(&cg_async.ready_q, job, queue_entry);
		cg_async.nr_ready--;
		cg_async_remove_job_locked(job);
		SDL_UnlockMutex(cg_async.mutex);

		parts_cg_async_finish_job(job, false);
		processed++;

		SDL_LockMutex(cg_async.mutex);
		if (budget_ticks && processed >= 1
				&& (SDL_GetPerformanceCounter() - t_start) >= budget_ticks)
			break;
	}
	SDL_UnlockMutex(cg_async.mutex);
}

static bool parts_cg_async_claim_sync(const char *cg_name, struct parts_cg_asset **asset_out)
{
	*asset_out = NULL;
	if (!cg_async.initialized || parts_current != &live_context)
		return false;
	parts_cg_cache_sync_archive_gen();
	uint64_t archive_gen = cg_cache_archive_gen;
	SDL_LockMutex(cg_async.mutex);
	struct parts_cg_async_job *job = cg_async_find_job_locked(cg_name, archive_gen);
	if (!job) {
		SDL_UnlockMutex(cg_async.mutex);
		return false;
	}
	if (job->state == CG_JOB_QUEUED) {
		TAILQ_REMOVE(&cg_async.pending_q, job, queue_entry);
		cg_async.nr_queued--;
		cg_async_remove_job_locked(job);
		SDL_UnlockMutex(cg_async.mutex);

		int cg_no = 0;
		job->cg = asset_cg_load_by_name(job->cg_name, &cg_no);
		job->cg_no = cg_no;
		*asset_out = parts_cg_async_finish_job(job, true);
		return true;
	}
	if (job->state == CG_JOB_DONE) {
		TAILQ_REMOVE(&cg_async.ready_q, job, queue_entry);
		cg_async.nr_ready--;
		cg_async_remove_job_locked(job);
		SDL_UnlockMutex(cg_async.mutex);

		*asset_out = parts_cg_async_finish_job(job, true);
		return true;
	}
	// CG_JOB_DECODING: wait for worker currently decoding this exact CG.
	job->sync_claimed = true;
	while (job->state == CG_JOB_DECODING) {
		SDL_CondWait(cg_async.done_cond, cg_async.mutex);
	}
	cg_async_remove_job_locked(job);
	SDL_UnlockMutex(cg_async.mutex);

	*asset_out = parts_cg_async_finish_job(job, true);
	return true;
}

void PE_SetEventID(int parts_no, int delegate_index, int unique_id)
{
	struct parts *parts = parts_get(parts_no);
	parts->delegate_index = delegate_index;
	parts->event_unique_id = unique_id;
}

void PE_SetComponentSubColorMode(int parts_no, bool enable)
{
	struct parts *parts = parts_get(parts_no);
	if (parts->sub_color_mode == enable)
		return;
	parts->sub_color_mode = enable;
	parts_dirty(parts);
}

bool PE_IsComponentSubColorMode(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? parts->sub_color_mode : false;
}

void PE_FlushCGCache(void)
{
	parts_cg_cache_clear();
	parts_cproc_cache_clear();
	parts_flat_cache_clear();
}

static void parts_state_free(struct parts_state *state)
{
	parts_state_discard_texture_backups(state);
	switch (state->type) {
	case PARTS_UNINITIALIZED:
	case PARTS_LAYOUT_BOX:
		break;
	case PARTS_CG:
		parts_cg_detach(&state->cg);
		break;
	case PARTS_TEXT:
		parts_text_free(&state->text);
		break;
	case PARTS_ANIMATION:
		for (unsigned i = 0; i < state->anim.nr_frames; i++) {
			gfx_delete_texture(&state->anim.frames[i]);
		}
		free(state->anim.frames);
		if (state->anim.cg_name)
			free_string(state->anim.cg_name);
		break;
	case PARTS_NUMERAL:
		gfx_delete_texture(&state->common.texture);
		if (state->num.cg_name) free_string(state->num.cg_name);
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		gfx_delete_texture(&state->common.texture);
		gfx_delete_texture(&state->gauge.cg);
		if (state->gauge.cg_name)
			free_string(state->gauge.cg_name);
		break;
	case PARTS_RECT_DETECTION:
		if (state->rect.cg_name)
			free_string(state->rect.cg_name);
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
		if (state->movie.mc)
			movie_free(state->movie.mc);
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
	parts_common_free_alpha_mask(&state->common);
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
		state->text.ruby_ts = default_text_style;
		state->text.ruby_ts.size = 10.0f;
		break;
	case PARTS_NUMERAL:
		state->num.length = parts_v2 ? 0 : 1;
		state->num.font_no = -1;
		state->num.show_padding = true;
		state->num.ts = default_text_style;
		state->num.cg_name = string_ref(&EMPTY_STRING);
		break;
	case PARTS_CONSTRUCTION_PROCESS:
		TAILQ_INIT(&state->cproc.ops);
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		state->gauge.cg_no = -1;
		state->gauge.cg_name = string_ref(&EMPTY_STRING);
		state->gauge.rate = 1.0f;
		state->gauge.numerator = 100.0f;
		state->gauge.denominator = 100.0f;
		break;
	case PARTS_3DLAYER:
		state->layer3d.plugin = -1;
		state->layer3d.sprite_no = -1;
		break;
	case PARTS_UNINITIALIZED:
	case PARTS_CG:
	case PARTS_ANIMATION:
	case PARTS_FLASH:
	case PARTS_RECT_DETECTION:
		break;
	case PARTS_FLAT:
		state->flat.fps_rate = 1.0f;
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
	parts_state_resume_textures(parts, &parts->states[state]);
	if (parts->states[state].type != PARTS_CG) {
		parts_state_reset(&parts->states[state], PARTS_CG);
	}
	return &parts->states[state].cg;
}

struct parts_text *parts_get_text(struct parts *parts, int state)
{
	parts_state_resume_textures(parts, &parts->states[state]);
	if (parts->states[state].type != PARTS_TEXT) {
		parts_state_reset(&parts->states[state], PARTS_TEXT);
	}
	return &parts->states[state].text;
}

struct parts_animation *parts_get_animation(struct parts *parts, int state)
{
	parts_state_resume_textures(parts, &parts->states[state]);
	if (parts->states[state].type != PARTS_ANIMATION) {
		parts_state_reset(&parts->states[state], PARTS_ANIMATION);
	}
	return &parts->states[state].anim;
}

struct parts_numeral *parts_get_numeral(struct parts *parts, int state)
{
	parts_state_resume_textures(parts, &parts->states[state]);
	if (parts->states[state].type != PARTS_NUMERAL) {
		parts_state_reset(&parts->states[state], PARTS_NUMERAL);
	}
	return &parts->states[state].num;
}

struct parts_gauge *parts_get_hgauge(struct parts *parts, int state)
{
	parts_state_resume_textures(parts, &parts->states[state]);
	if (parts->states[state].type != PARTS_HGAUGE) {
		parts_state_reset(&parts->states[state], PARTS_HGAUGE);
	}
	return &parts->states[state].gauge;
}

struct parts_gauge *parts_get_vgauge(struct parts *parts, int state)
{
	parts_state_resume_textures(parts, &parts->states[state]);
	if (parts->states[state].type != PARTS_VGAUGE) {
		parts_state_reset(&parts->states[state], PARTS_VGAUGE);
	}
	return &parts->states[state].gauge;
}

struct parts_construction_process *parts_get_construction_process(struct parts *parts, int state)
{
	parts_state_resume_textures(parts, &parts->states[state]);
	if (parts->states[state].type != PARTS_CONSTRUCTION_PROCESS) {
		parts_state_reset(&parts->states[state], PARTS_CONSTRUCTION_PROCESS);
	}
	return &parts->states[state].cproc;
}

struct parts_flash *parts_get_flash(struct parts *parts, int state)
{
	parts_state_resume_textures(parts, &parts->states[state]);
	if (parts->states[state].type != PARTS_FLASH) {
		parts_state_reset(&parts->states[state], PARTS_FLASH);
	}
	return &parts->states[state].flash;
}

struct parts_flat *parts_get_flat(struct parts *parts, int state)
{
	parts_state_resume_textures(parts, &parts->states[state]);
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
	case 0:  return (Point) {    0, 0    }; // unset -> top-left
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
	if (parts->states[0].type == PARTS_LAYOUT_BOX)
		parts_render_gen++;
	// v12 hitboxes stay in component space; transforms retain subpixel positions.
	float x = parts_v2 ? 0 : parts->local.pos.x;
	float y = parts_v2 ? 0 : parts->local.pos.y;
	if (parts->component_type == 1 && parts->widget && common != &parts->widget->label.common) {
		int width, height;
		parts_widget_get_size(parts, &width, &height);
		common->origin_offset = calculate_offset(parts->origin_mode, width, height);
		common->hitbox = (Rectangle){common->origin_offset.x, common->origin_offset.y, width, height};
		return;
	}
	if (common->surface_area.w || common->surface_area.h) {
		common->origin_offset = calculate_offset(parts->origin_mode,
				common->surface_area.w, common->surface_area.h);
		Rectangle r = { 0, 0, common->w, common->h };
		SDL_IntersectRect(&r, &common->surface_area, &common->hitbox);
		common->origin_offset.x -= common->surface_area.x;
		common->origin_offset.y -= common->surface_area.y;
		common->hitbox.x += x + common->origin_offset.x;
		common->hitbox.y += y + common->origin_offset.y;
	} else {
		common->origin_offset = calculate_offset(parts->origin_mode, common->w, common->h);
		common->hitbox = (Rectangle) {
			.x = x + common->origin_offset.x,
			.y = y + common->origin_offset.y,
			.w = common->w,
			.h = common->h,
		};
	}
}

void parts_recalculate_hitbox(struct parts *parts)
{
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		parts_common_recalculate_hitbox(parts, &parts->states[i].common);
		if (parts->states[i].type == PARTS_RECT_DETECTION && parts->states[i].rect.custom) {
			struct parts_rect *rect = &parts->states[i].rect;
			int x = rect->points[0].x, y = rect->points[0].y;
			for (int j = 1; j < 4; j++) { x = min(x, rect->points[j].x); y = min(y, rect->points[j].y); }
			rect->common.hitbox.x += x;
			rect->common.hitbox.y += y;
		}
	}
}

SDL_FPoint parts_local_position(struct parts *parts)
{
	SDL_FPoint pos = parts->local.pos;
	if (parts->position_is_ratio) {
		pos.x *= config.view_width;
		pos.y *= config.view_height;
	}
	return pos;
}

static void parts_apply_local_matrix(struct parts *parts, mat4 out)
{
	float tx = 0.0f, ty = 0.0f;
	if (parts->parent && (parts->parent->component_type == 15 ||
			(parts->parent->states[0].type == PARTS_LAYOUT_BOX &&
			parts->parent->states[0].layout_box.layout_type == PARTS_LAYOUT_FREE))) {
		Point off = parts->parent->states[0].common.origin_offset;
		tx += off.x;
		ty += off.y;
	}
	SDL_FPoint pos = parts_local_position(parts);
	for (int axis = 0; axis < 2; axis++) {
		int link_no = parts->scroll_links[axis];
		if (link_no > 0) {
			struct parts *link = parts_try_get(link_no);
			if (link && link->scrollbar) {
				if (axis) pos.y -= link->scrollbar->pos;
				else pos.x -= link->scrollbar->pos;
			}
		}
	}
	tx += pos.x;
	ty += pos.y;
	// Component reversal is independent of magnification. Apply it here so
	// CG/FLAT rendering, child transforms and hit testing use the same axes.
	float scale_x = parts->local.scale.x * (parts->reverse_lr ? -1.0f : 1.0f);
	float scale_y = parts->local.scale.y * (parts->reverse_tb ? -1.0f : 1.0f);
	if (parts->local.rotation.x == 0.0f && parts->local.rotation.y == 0.0f &&
			parts->local.rotation.z == 0.0f) {
		float sx = scale_x;
		float sy = scale_y;
		if (tx != 0.0f || ty != 0.0f || sx != 1.0f || sy != 1.0f) {
			for (int r = 0; r < 4; r++) {
				float m0 = out[0][r];
				float m1 = out[1][r];
				out[3][r] += tx * m0 + ty * m1;
				out[0][r] = sx * m0;
				out[1][r] = sy * m1;
			}
		}
		return;
	}
	if (tx != 0.0f || ty != 0.0f)
		glm_translate(out, (vec3){tx, ty, 0});
	if (parts->local.rotation.z != 0.0f)
		glm_rotate_z(out, glm_rad(parts->local.rotation.z), out);
	if (parts->local.rotation.x != 0.0f)
		glm_rotate_x(out, glm_rad(-parts->local.rotation.x), out);
	if (parts->local.rotation.y != 0.0f)
		glm_rotate_y(out, glm_rad(parts->local.rotation.y), out);
	if (scale_x != 1.0f || scale_y != 1.0f)
		glm_scale(out, (vec3){scale_x, scale_y, 1});
}

static void parts_apply_local_transform(struct parts *parts, mat4 out)
{
	if (parts_hierarchical_matrix) {
		parts_apply_local_matrix(parts, out);
		return;
	}
	float tx = 0.0f, ty = 0.0f;
	if (parts->parent && parts->parent->states[0].type == PARTS_LAYOUT_BOX &&
			parts->parent->states[0].layout_box.layout_type == PARTS_LAYOUT_FREE) {
		Point off = parts->parent->states[0].common.origin_offset;
		tx += off.x;
		ty += off.y;
	}
	SDL_FPoint pos = parts_local_position(parts);
	for (int axis = 0; axis < 2; axis++) {
		int link_no = parts->scroll_links[axis];
		if (link_no > 0) {
			struct parts *link = parts_try_get(link_no);
			if (link && link->scrollbar) {
				if (axis) pos.y -= link->scrollbar->pos;
				else pos.x -= link->scrollbar->pos;
			}
		}
	}
	tx += pos.x;
	ty += pos.y;
	// Pre-UpdateMatrix PartsEngine (AIN v6.1: Rance 03, Rance 9, Evenicle,
	// Escalayer Reboot) applies child positions additively in world space
	// rather than transforming them through the parent's scale/rotation.
	out[3][0] += tx;
	out[3][1] += ty;
	float scale_x = parts->local.scale.x * (parts->reverse_lr ? -1.0f : 1.0f);
	float scale_y = parts->local.scale.y * (parts->reverse_tb ? -1.0f : 1.0f);
	if (parts->local.rotation.x == 0.0f && parts->local.rotation.y == 0.0f &&
			parts->local.rotation.z == 0.0f) {
		if (scale_x != 1.0f || scale_y != 1.0f) {
			for (int r = 0; r < 4; r++) {
				out[0][r] *= scale_x;
				out[1][r] *= scale_y;
			}
		}
		return;
	}
	if (parts->local.rotation.z != 0.0f)
		glm_rotate_z(out, glm_rad(parts->local.rotation.z), out);
	if (parts->local.rotation.x != 0.0f)
		glm_rotate_x(out, glm_rad(-parts->local.rotation.x), out);
	if (parts->local.rotation.y != 0.0f)
		glm_rotate_y(out, glm_rad(parts->local.rotation.y), out);
	if (scale_x != 1.0f || scale_y != 1.0f)
		glm_scale(out, (vec3){scale_x, scale_y, 1});
}

static void parts_update_global_pos_mat(struct parts *parts, SDL_FPoint parent_pos, mat4 parent_matrix)
{
	parts->global.pos = (SDL_FPoint) {
		parent_pos.x + parts->local.pos.x,
		parent_pos.y + parts->local.pos.y
	};
	mat4 matrix;
	if (parts_v2) {
		glm_mat4_copy(parent_matrix, matrix);
		parts_apply_local_transform(parts, matrix);
		parts->global.pos = (SDL_FPoint){matrix[3][0], matrix[3][1]};
		if (parts->world_matrix_self == parts) {
			glm_mat4_copy(matrix, parts->world_matrix);
			parts->world_matrix_gen = parts_render_gen;
		}
	}

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_pos_mat(child, parts->global.pos, matrix);
	}
}

static void parts_update_global_pos(struct parts *parts, SDL_FPoint parent_pos)
{
	mat4 parent_matrix = GLM_MAT4_IDENTITY_INIT;
	if (parts_v2) {
		if (parts->parent) {
			parts_world_matrix(parts->parent, parent_matrix);
		} else if (root_pos.x != 0 || root_pos.y != 0) {
			glm_translate(parent_matrix, (vec3){root_pos.x, root_pos.y, 0});
		}
	}
	parts_update_global_pos_mat(parts, parent_pos, parent_matrix);
}

void parts_set_pos(struct parts *parts, Point pos)
{
	parts_set_posf(parts, (SDL_FPoint){pos.x, pos.y});
}

void parts_set_posf(struct parts *parts, SDL_FPoint pos)
{
	parts->local.pos.x = pos.x;
	parts->local.pos.y = pos.y;
	parts_recalculate_hitbox(parts);
	parts_dirty(parts);
	parts_update_global_pos(parts, parts->parent ? parts->parent->global.pos
			: (SDL_FPoint){root_pos.x, root_pos.y});
}

void parts_set_global_pos(Point pos)
{
	root_pos = pos;
	parts_engine_dirty();
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (!parts->parent)
			parts_update_global_pos(parts, (SDL_FPoint){root_pos.x, root_pos.y});
	}
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

bool parts_is_visible(struct parts *parts)
{
	if (!parts || !parts->global.show || parts->global.alpha <= 0)
		return false;
	float rot_x = 0.0f;
	float rot_y = 0.0f;
	for (struct parts *p = parts; p; p = p->parent) {
		if (!p->local.show || p->local.alpha <= 0)
			return false;
		if (p->message_window && !parts_message_window_show)
			return false;
		if (p->linked_to > 0) {
			struct parts *link_parts = parts_try_get(p->linked_to);
			if (!link_parts || !link_parts->is_hovered)
				return false;
		}
		if (p->checkbox_show_link > 0) {
			struct parts *cb = parts_try_get(p->checkbox_show_link);
			if (!cb || (cb->widget && !cb->widget->checked))
				return false;
		}
		rot_x += p->local.rotation.x;
		rot_y += p->local.rotation.y;
	}
	if ((rot_x != 0.0f || rot_y != 0.0f) &&
			cosf(glm_rad(rot_x)) * cosf(glm_rad(rot_y)) <= 1e-6f)
		return false;
	return true;
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
	uint8_t clamped = max(0, min(255, alpha));
	if (parts->local.alpha == clamped)
		return;
	parts->local.alpha = clamped;
	parts_update_global_alpha(parts, parts->parent ? parts->parent->global.alpha : 255);
	parts_dirty(parts);
}

static void parts_update_global_add_color(struct parts *parts, SDL_Color parent_color)
{
	parts->global.add_color = (SDL_Color) {
		min(255, parent_color.r + parts->local.add_color.r),
		min(255, parent_color.g + parts->local.add_color.g),
		min(255, parent_color.b + parts->local.add_color.b),
		0
	};

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_add_color(child, parts->global.add_color);
	}
}

void parts_set_add_color(struct parts *parts, SDL_Color color)
{
	if (!memcmp(&parts->local.add_color, &color, sizeof(SDL_Color)))
		return;
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
	if (!memcmp(&parts->local.multiply_color, &color, sizeof(SDL_Color)))
		return;
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
	parts_update_global_pos(parts, parts->parent ? parts->parent->global.pos
			: (SDL_FPoint){root_pos.x, root_pos.y});
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

static void parts_cg_attach_asset(struct parts *parts, struct parts_cg *parts_cg,
		struct parts_cg_asset *asset, struct string *name)
{
	parts_cg_asset_ref(asset);
	parts_cg_detach(parts_cg);
	parts_cg->cg_asset = asset;
	parts_cg->common.texture = asset->texture;
	parts_set_dims(parts, &parts_cg->common, asset->w, asset->h);
	parts_cg->no = asset->cg_no;
	parts_cg->name = name;
	parts_dirty(parts);
}

bool _parts_cg_set(struct parts *parts, struct parts_cg *parts_cg, struct cg *cg, int cg_no,
		struct string *name)
{
	if (!cg) {
		if (name)
			free_string(name);
		return false;
	}
	bool cacheable = !(name && is_save_cg_name(name->text))
		&& ((name && name->size > 0) || cg_no > 0);
	if (cacheable) {
		parts_cg_cache_sync_archive_gen();
		struct parts_cg_asset *asset = parts_cg_asset_create_from_cg(
				cg, cg_no, name ? name->text : NULL, cg_cache_archive_gen, true);
		parts_cg_attach_asset(parts, parts_cg, asset, name);
		parts_cg_asset_unref(asset);
		parts_frame_did_heavy_load = true;
		cg_free(cg);
		return true;
	}
	parts_cg_detach(parts_cg);
	gfx_init_texture_with_cg(&parts_cg->common.texture, cg);
	parts_common_set_alpha_from_cg(&parts_cg->common, cg);
	parts_set_dims(parts, &parts_cg->common, cg->metrics.w, cg->metrics.h);
	parts_cg->no = cg_no;
	parts_cg->name = name;
	parts_frame_did_heavy_load = true;
	parts_dirty(parts);
	cg_free(cg);
	return true;
}

bool parts_cg_set_by_index(struct parts *parts, struct parts_cg *cg, int cg_no)
{
	assert(cg_no);
	parts_cg_cache_sync_archive_gen();
	cg->load_gen = ++next_cg_load_gen;
	cg->async_gen = 0;
	if (cg->cg_asset && cg->cg_asset->archive_gen == cg_cache_archive_gen
			&& cg->no == cg_no && cg->common.texture.handle) {
		if (cg->name) {
			free_string(cg->name);
			cg->name = NULL;
		}
		parts_set_dims(parts, &cg->common, cg->common.texture.w, cg->common.texture.h);
		parts_dirty(parts);
		return true;
	}
	struct parts_cg_asset *cached = parts_cg_asset_find_by_id(cg_no);
	if (cached) {
		parts_cg_attach_asset(parts, cg, cached, NULL);
		return true;
	}
	return _parts_cg_set(parts, cg, asset_cg_load(cg_no), cg_no, NULL);
}

bool parts_cg_set(struct parts *parts, struct parts_cg *parts_cg, struct string *cg_name)
{
	assert(cg_name && *(cg_name->text) != '\0');
	parts_cg->load_gen = ++next_cg_load_gen;
	parts_cg->async_gen = 0;
	int no = 0;
	struct cg *cg;
	if (is_save_cg_name(cg_name->text)) {
		cg = asset_cg_load_by_name(cg_name->text, &no);
	} else {
		parts_cg_cache_sync_archive_gen();
		if (parts_cg->cg_asset && parts_cg->cg_asset->archive_gen == cg_cache_archive_gen
				&& parts_cg->common.texture.handle
				&& parts_cg->name && !strcmp(parts_cg->name->text, cg_name->text)) {
			parts_set_dims(parts, &parts_cg->common,
					parts_cg->common.texture.w, parts_cg->common.texture.h);
			parts_dirty(parts);
			return true;
		}
		struct parts_cg_asset *cached = parts_cg_asset_find_by_name(cg_name->text);
		if (cached) {
			parts_cg_attach_asset(parts, parts_cg, cached, string_dup(cg_name));
			return true;
		}
		struct parts_cg_asset *claimed = NULL;
		if (parts_cg_async_claim_sync(cg_name->text, &claimed)) {
			if (!claimed)
				return false;
			parts_cg_attach_asset(parts, parts_cg, claimed, string_dup(cg_name));
			parts_cg_asset_unref(claimed);
			return true;
		}
		cg = asset_cg_load_by_name(cg_name->text, &no);
	}
	return _parts_cg_set(parts, parts_cg, cg, no, string_dup(cg_name));
}


void parts_numeral_font_init(struct parts_numeral_font *font)
{
	for (int i = 0; i < 12; i++) {
		if (font->cg[i].handle)
			gfx_delete_texture(&font->cg[i]);
		font->cg[i] = (Texture){0};
	}

	if (font->type == PARTS_NUMERAL_FONT_SEPARATE) {
		for (int i = 0; i < 12; i++) {
			struct cg *cg = asset_cg_load(font->cg_no + i);
			if (!cg)
				continue;
			gfx_init_texture_with_cg(&font->cg[i], cg);
			cg_free(cg);
		}
	} else if (font->type == PARTS_NUMERAL_FONT_SEPARATE2) {
		if (font->cg_name && font->cg_name->size > 0) {
			for (int i = 0; i < 12; i++) {
				struct string *name = string_format(font->cg_name, (union vm_value){.i = i}, STRFMT_INT);
				int no = -1;
				struct cg *cg = asset_cg_load_by_name(name->text, &no);
				free_string(name);
				if (!cg) {
					font->width[i] = -1;
					continue;
				}
				font->width[i] = no;
				gfx_init_texture_with_cg(&font->cg[i], cg);
				cg_free(cg);
			}
			font->cg_no = font->width[0];
		} else {
			for (int i = 0; i < 12; i++) {
				if (font->width[i] < 0)
					continue;
				struct cg *cg = asset_cg_load(font->width[i]);
				if (!cg)
					continue;
				gfx_init_texture_with_cg(&font->cg[i], cg);
				cg_free(cg);
			}
		}
	} else if (font->type == PARTS_NUMERAL_FONT_COMBINED) {
		struct cg *cg = NULL;
		if (font->cg_name && font->cg_name->size > 0) {
			int no = font->cg_no;
			cg = asset_cg_load_by_name(font->cg_name->text, &no);
			if (cg)
				font->cg_no = no;
		}
		if (!cg && font->cg_no > 0)
			cg = asset_cg_load(font->cg_no);
		if (!cg)
			return;
		if (!cg->pixels || cg->metrics.w <= 0 || cg->metrics.h <= 0) {
			cg_free(cg);
			return;
		}
		int src_w = cg->metrics.w;
		int src_h = cg->metrics.h;
		const uint8_t *src_px = (const uint8_t *)cg->pixels;
		int64_t x = 0;
		for (int i = 0; i < 12; i++) {
			int gw = font->width[i];
			if (gw <= 0)
				continue;
			if ((size_t)gw > SIZE_MAX / 4 / (size_t)src_h) {
				x += gw;
				continue;
			}
			uint8_t *buf = xcalloc((size_t)gw * (size_t)src_h, 4);
			if (x >= 0 && x < src_w) {
				int copy_w = src_w - (int)x;
				if (copy_w > gw)
					copy_w = gw;
				for (int y = 0; y < src_h; y++) {
					memcpy(buf + ((size_t)y * (size_t)gw) * 4,
							src_px + ((size_t)y * (size_t)src_w + (size_t)x) * 4,
							(size_t)copy_w * 4);
				}
			}
			gfx_init_texture_with_pixels(&font->cg[i], gw, src_h, buf);
			free(buf);
			x += gw;
		}
		cg_free(cg);
	}
}

static bool parts_numeral_font_has_texture(const struct parts_numeral_font *font)
{
	for (int i = 0; i < 12; i++) {
		if (font->cg[i].handle)
			return true;
	}
	return false;
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
	memset(font, 0, sizeof(*font));
	font->cg_no = cg_no;
	font->type = PARTS_NUMERAL_FONT_SEPARATE;
	parts_numeral_font_init(font);
	return font_no;
}

int parts_load_numeral_font_separate_string(struct string *cg_name)
{
	if (!cg_name || !cg_name->size)
		return -1;

	// find existing font by name first
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		if (font->type == PARTS_NUMERAL_FONT_SEPARATE2
				&& font->cg_name && !strcmp(font->cg_name->text, cg_name->text))
			return i;
	}

	// convert name to CG indices
	int indices[12];
	bool any_exists = false;
	for (int i = 0; i < 12; i++) {
		struct string *name = string_format(cg_name, (union vm_value){.i = i}, STRFMT_INT);
		if (asset_exists_by_name(ASSET_CG, name->text, &indices[i])) {
			any_exists = true;
		} else {
			WARNING("numeral cg doesn't exist: %s", display_sjis0(name->text));
			indices[i] = -1;
		}
		free_string(name);
	}
	if (!any_exists)
		return -1;

	// match legacy unnamed font by indices and upgrade it with cg_name
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		if (font->type != PARTS_NUMERAL_FONT_SEPARATE2 || font->cg_name)
			continue;
		if (!memcmp(font->width, indices, sizeof(indices))) {
			font->cg_name = string_dup(cg_name);
			if (!parts_numeral_font_has_texture(font))
				parts_numeral_font_init(font);
			return i;
		}
	}

	// load new font
	int font_no = parts_nr_numeral_fonts++;
	parts_numeral_fonts = xrealloc_array(parts_numeral_fonts, font_no, font_no + 1,
			sizeof(struct parts_numeral_font));
	struct parts_numeral_font *font = &parts_numeral_fonts[font_no];
	memset(font, 0, sizeof(*font));
	font->cg_no = indices[0];
	font->type = PARTS_NUMERAL_FONT_SEPARATE2;
	font->cg_name = string_dup(cg_name);
	memcpy(font->width, indices, sizeof(int) * 12);
	parts_numeral_font_init(font);
	return font_no;
}

static int parts_load_numeral_font_combined(int cg_no, int w[12])
{
	// find existing font
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		if (font->type != PARTS_NUMERAL_FONT_COMBINED || font->cg_name || font->cg_no != cg_no)
			continue;
		if (!memcmp(w, font->width, sizeof(font->width)))
			return i;
	}

	// load new font
	int font_no = parts_nr_numeral_fonts++;
	parts_numeral_fonts = xrealloc_array(parts_numeral_fonts, font_no, font_no + 1,
			sizeof(struct parts_numeral_font));
	struct parts_numeral_font *font = &parts_numeral_fonts[font_no];
	memset(font, 0, sizeof(*font));
	font->cg_no = cg_no;
	font->type = PARTS_NUMERAL_FONT_COMBINED;
	memcpy(font->width, w, sizeof(int) * 12);
	parts_numeral_font_init(font);
	return font_no;
}

int parts_load_numeral_font_combined_by_name(struct string *cg_name, int w[12])
{
	if (!cg_name || !cg_name->size)
		return -1;
	int no = 0;
	if (!asset_exists_by_name(ASSET_CG, cg_name->text, &no))
		return -1;

	// find existing font by name, or upgrade matching legacy unnamed font
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		if (font->type != PARTS_NUMERAL_FONT_COMBINED)
			continue;
		if (memcmp(w, font->width, sizeof(font->width)))
			continue;
		if (font->cg_name && !strcmp(font->cg_name->text, cg_name->text))
			return i;
		if (!font->cg_name && font->cg_no == no) {
			font->cg_name = string_dup(cg_name);
			if (!parts_numeral_font_has_texture(font))
				parts_numeral_font_init(font);
			return i;
		}
	}

	// load new font
	int font_no = parts_nr_numeral_fonts++;
	parts_numeral_fonts = xrealloc_array(parts_numeral_fonts, font_no, font_no + 1,
			sizeof(struct parts_numeral_font));
	struct parts_numeral_font *font = &parts_numeral_fonts[font_no];
	memset(font, 0, sizeof(*font));
	font->cg_no = no;
	font->type = PARTS_NUMERAL_FONT_COMBINED;
	font->cg_name = string_dup(cg_name);
	memcpy(font->width, w, sizeof(int) * 12);
	parts_numeral_font_init(font);
	return font_no;
}

static bool parts_numeral_update(struct parts *parts, struct parts_numeral *num)
{
	if (!num->have_num || (num->show_type != 2 && num->font_no < 0))
		return true;
	if (num->show_type != 2 && num->font_no >= parts_nr_numeral_fonts)
		return false;
	// Length counts digits, excluding the sign and inserted grouping separators.
	// Hidden padding still occupies the width of its zero glyph.
	char digits[24];
	int64_t value = num->num;
	bool negative = value < 0;
	if (negative) value = -value;
	int count = snprintf(digits, sizeof(digits), "%lld", (long long)value);
	int total = max(count, min(num->length, 128));
	int padding = total - count;
	char characters[192];
	bool hidden[192] = {0};
	int length = 0;
	if (negative) characters[length++] = '-';
	for (int i = 0; i < total; i++) {
		if (num->show_comma && i && (total - i) % 3 == 0)
			characters[length++] = ',';
		hidden[length] = i < padding && !num->show_padding;
		characters[length++] = i < padding ? '0' : digits[i - padding];
	}
	characters[length] = 0;

	float advances[192];
	char glyphs[192][4];
	struct parts_numeral_font *font = num->show_type != 2
		? &parts_numeral_fonts[num->font_no] : NULL;
	struct text_style eff_ts = parts_get_effective_text_style(&num->ts);
	int height = 0;
	float width = 0;
	for (int i = 0; i < length; i++) {
		if (num->show_type == 2) {
			glyphs[i][0] = characters[i];
			glyphs[i][1] = 0;
			if (num->full_pitch) {
				// Convert through the selected game encoding (GBK or Shift-JIS).
				unsigned cp = 0xff00 + (unsigned char)characters[i] - 0x20;
				char utf8[4] = {0xe0 | (cp >> 12), 0x80 | ((cp >> 6) & 0x3f),
					0x80 | (cp & 0x3f), 0};
				char *encoded = utf2sjis(utf8, 3);
				snprintf(glyphs[i], sizeof(glyphs[i]), "%s", encoded);
				free(encoded);
			}
			advances[i] = gfx_size_char(&eff_ts, glyphs[i]);
			height = ceilf(text_style_height(&eff_ts));
		} else {
			int index = characters[i] == '-' ? 10 : characters[i] == ',' ? 11 : characters[i] - '0';
			advances[i] = font->cg[index].w;
			height = max(height, font->cg[index].h);
		}
		width += advances[i];
	}
	width += (length - 1) * num->space;
	int w = max(0, (int)ceilf(width));
	gfx_delete_texture(&num->common.texture);
	if (w > 0 && height > 0) {
		gfx_init_texture_rgba(&num->common.texture, w, height, (SDL_Color){0, 0, 0, 0});
		gfx_begin_draw_target(&num->common.texture);
		float x = 0;
		for (int i = 0; i < length; i++) {
			if (!hidden[i]) {
				if (num->show_type == 2) {
					gfx_render_textf(&num->common.texture, x, 0, glyphs[i], &eff_ts, false);
				} else {
					int index = characters[i] == '-' ? 10 : characters[i] == ',' ? 11 : characters[i] - '0';
					Texture *cg = &font->cg[index];
					if (cg->handle) gfx_copy_with_alpha_map(&num->common.texture, x, 0, cg, 0, 0, cg->w, cg->h);
				}
			}
			x += advances[i] + num->space;
		}
		gfx_end_draw_target();
	}
	parts_set_dims(parts, &num->common, w, height);
	parts_dirty(parts);
	return true;
}

bool parts_numeral_set_number(struct parts *parts, struct parts_numeral *num, int n)
{
	if (num->have_num && num->num == n && num->common.texture.handle)
		return true;
	num->have_num = true;
	num->num = n;
	return parts_numeral_update(parts, num);
}

void parts_set_state(struct parts *parts, enum parts_state_type state)
{
	if (parts->lock_input_state)
		return;
	while (state > PARTS_STATE_DEFAULT) {
		struct parts_state *s = &parts->states[state];
		if (s->type != PARTS_UNINITIALIZED && !(parts_v2
				&& s->type == PARTS_CG && !s->common.texture.handle)) break;
		state--;
	}
	if (parts->state != state) {
		parts->state = state;
		parts_dirty(parts);
	}
}

void parts_set_surface_area(struct parts *parts, struct parts_common *common, int x, int y, int w, int h)
{
	if (x < 0 || y < 0)
		return;
	Rectangle area = { x, y, w, h };
	bool changed = common->surface_area.x != area.x || common->surface_area.y != area.y ||
		common->surface_area.w != area.w || common->surface_area.h != area.h;
	common->surface_area = area;
	parts_common_recalculate_hitbox(parts, common);
	if (changed)
		parts_dirty(parts);
}

static bool parts_animation_update(struct parts_animation *anim, int passed_time)
{
	if (passed_time <= 0 || !anim->nr_frames || !anim->frame_time)
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

bool parts_state_update(struct parts_state *state, int passed_time)
{
	bool dirty = false;
	switch (state->type) {
	case PARTS_ANIMATION:
		dirty = parts_animation_update(&state->anim, passed_time);
		break;
	case PARTS_FLASH:
		dirty = parts_flash_update(&state->flash, passed_time);
		break;
	case PARTS_FLAT:
		dirty = parts_flat_update(&state->flat, passed_time);
		break;
	case PARTS_MOVIE:
		if (state->movie.mc && state->movie.sprite_no >= 0) {
			movie_draw(state->movie.mc, sact_try_get_sprite(state->movie.sprite_no));
			dirty = true;
		}
		break;
	default:
		break;
	}
	return dirty;
}

static void parts_update_loop(struct parts *parts, int passed_time, bool occluded)
{
	bool visible = !occluded && parts_is_visible(parts);
	struct parts_state *display = parts_display_state(parts);
	bool state_dirty = parts_state_update(display, passed_time);
	if (display != &parts->states[PARTS_STATE_DEFAULT]
			&& parts->states[PARTS_STATE_DEFAULT].type == PARTS_FLAT
			&& parts->states[PARTS_STATE_DEFAULT].flat.needs_advance) {
		state_dirty |= parts_state_update(&parts->states[PARTS_STATE_DEFAULT], 0);
	}
	bool ui_dirty = false;
	if (parts->scrollbar) ui_dirty |= parts_scrollbar_update(parts, passed_time);
	if (parts->message_box) ui_dirty |= parts_message_window_update(parts, passed_time);
	if ((state_dirty && visible) || (ui_dirty && !occluded))
		parts_dirty(parts);
}

static void parts_update_animation(int passed_time, int scaled_time)
{
	struct parts *cover = NULL;
	struct parts *p;
	PARTS_LIST_FOREACH_REVERSE(p) {
		if (parts_sprite_is_fullscreen_opaque(&p->sp)) {
			cover = p;
			break;
		}
	}
	bool occluded = (cover != NULL);
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (parts == cover)
			occluded = false;
		parts_update_loop(parts, parts->enable_speed_up ? scaled_time : passed_time, occluded);
	}
}

void parts_release(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (!slot->value)
		return;

	struct parts *parts = slot->value;
	struct parts *root = parts;
	while (root->parent) root = root->parent;
	if (root->active_part == parts_no) root->active_part = 0;
	parts_input_reset_part(parts);
	if (parts->text_box_label) {
		parts_state_reset(parts->text_box_label, PARTS_UNINITIALIZED);
		free(parts->text_box_label);
	}
	parts_widget_free(parts);
	parts_panel_free(parts);
	parts_scrollbar_free(parts);
	parts_message_window_free(parts);
	parts_clear_motion(parts);
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		parts_state_free(&parts->states[i]);
		free_string(parts->sound_names[i]);
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
	free_string(parts->comment);
	free_string(parts->user_component_name);
	if (parts->text_box_text)
		free_string(parts->text_box_text);
	free(parts->radio_children);
	while (parts->user_values) {
		struct parts_user_value *v = parts->user_values;
		parts->user_values = v->next;
		free_string(v->key);
		free_string(v->value);
		free(v);
	}
	free(parts);
	slot->value = NULL;
	if (parts_no >= 1000010000 && parts_no < parts_current->next_free_no)
		parts_current->next_free_no = parts_no;
	parts_engine_dirty();
}

void parts_release_all(void)
{
	while (!TAILQ_EMPTY(&parts_list)) {
		struct parts *parts = TAILQ_FIRST(&parts_list);
		parts_release(parts->no);
	}
	parts_current->next_free_no = 1000010000;

	if (!parts_current->background) {
		for (int i = 0; i < parts_nr_numeral_fonts; i++) {
			struct parts_numeral_font *font = &parts_numeral_fonts[i];
			for (int j = 0; j < 12; j++) {
				if (font->cg[j].handle)
					gfx_delete_texture(&font->cg[j]);
			}
			if (font->cg_name)
				free_string(font->cg_name);
		}

		free(parts_numeral_fonts);
		parts_numeral_fonts = NULL;
		parts_nr_numeral_fonts = 0;
	}
}

struct parts_context *parts_context_select(struct parts_context *context)
{
	struct parts_context *previous = parts_current;
	parts_current = context;
	parts_render_gen++;
	return previous;
}

void parts_background_render(struct sprite *sprite)
{
	struct parts_context *context = (void*)((char*)sprite - offsetof(struct parts_context, sprite));
	struct parts_context *previous = parts_context_select(context);
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) parts_render(parts);
	parts_context_select(previous);
}

struct parts_context *parts_context_new_background(void)
{
	struct parts_context *context = xcalloc(1, sizeof(*context));
	TAILQ_INIT(&context->list);
	TAILQ_INIT(&context->dirty);
	context->table = ht_create(16384);
	context->next_free_no = 1000010000;
	context->origin = root_pos;
	context->background = true;
	// BackScene is one layer below the live graph, regardless of component Z.
	context->sprite.z = INT_MIN;
	context->sprite.z2 = INT_MIN;
	context->sprite.has_pixel = true;
	context->sprite.has_alpha = true;
	context->sprite.render = parts_background_render;
	return context;
}

void parts_context_free(struct parts_context *context)
{
	if (!context) return;
	assert(context != parts_current);
	struct parts_context *previous = parts_context_select(context);
	parts_release_all();
	ht_free_int(context->table);
	scene_unregister_sprite(&context->sprite);
	parts_context_select(previous);
	free(context);
}

static bool parts_engine_initialized = false;

void PE_enable_multi_controller(void)
{
	if (parts_multi_controller)
		return;
	assert(!parts_engine_initialized);
	parts_multi_controller = true;
}

static void parts_on_gl_reset(void *user);

bool PE_Init(void)
{
	if (parts_engine_initialized)
		return true;
	// XXX: Oyako Rankan doesn't call ChipmunkSpriteEngine.Init
	sact_init(16, CHIPMUNK_SPRITE_ENGINE);
	parts_table = ht_create(16384);
	parts_current->next_free_no = 1000010000;
	parts_message_window_show = true;
	parts_message_window_mul_color_rate = 1.0f;
	parts_message_window_alpha_rate = 1.0f;
	parts_render_init();
	parts_debug_init();
	ctrl_stack_init();
	gfx_register_reset_callback(parts_on_gl_reset, NULL);
	parts_engine_initialized = true;
	return true;
}

static void parts_clear(void)
{
	parts_cg_async_shutdown();
	PE_ClearBackScene();
	parts_input_reset();
	PE_ReleaseActivities();
	PE_ReleaseAllParts();
	parts_cg_cache_clear();
	cg_cache_archive_gen = 0;
	parts_cproc_cache_clear();
	parts_flat_cache_clear();
	PE_ReleaseMessage();
}

void PE_Reset(void)
{
	parts_clear();
	parts_message_window_show = true;
	parts_message_window_mul_color_rate = 1.0f;
	parts_message_window_alpha_rate = 1.0f;
	ctrl_stack_init();
	sact_ModuleFini();
}

void parts_end_session(void)
{
	if (parts_engine_initialized) {
		parts_clear();
		parts_render_fini();
		ht_free_int(parts_table);
		parts_table = NULL;
		root_pos = (Point){0};
		parts_engine_initialized = false;
	}
	parts_multi_controller = false;
	parts_v2 = false;
	parts_hierarchical_matrix = true;
	parts_message_window_show = true;
	parts_message_window_mul_color_rate = 1.0f;
	parts_message_window_alpha_rate = 1.0f;
	parts_activity_end_session();
	parts_motion_end_session();
	parts_scrollbar_end_session();
	parts_text_end_session();
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
	out->pos = (SDL_FPoint) { parent->pos.x + child->pos.x, parent->pos.y + child->pos.y };
	out->show = parent->show && child->show;
	out->alpha = parent->alpha * (child->alpha / 255.0f);
	out->scale.x = parent->scale.x * child->scale.x;
	out->scale.y = parent->scale.y * child->scale.y;
	out->rotation.x = parent->rotation.x + child->rotation.x;
	out->rotation.y = parent->rotation.y + child->rotation.y;
	out->rotation.z = parent->rotation.z + child->rotation.z;
	out->add_color.r = min(255, parent->add_color.r + child->add_color.r);
	out->add_color.g = min(255, parent->add_color.g + child->add_color.g);
	out->add_color.b = min(255, parent->add_color.b + child->add_color.b);
	out->multiply_color.r = parent->multiply_color.r * (child->multiply_color.r / 255.0f);
	out->multiply_color.g = parent->multiply_color.g * (child->multiply_color.g / 255.0f);
	out->multiply_color.b = parent->multiply_color.b * (child->multiply_color.b / 255.0f);
}

static void parts_update_component_mat(struct parts *parts, mat4 parent_matrix)
{
	if (parts->parent) {
		parts_combine_params(&parts->parent->global, &parts->local, &parts->global);
	}
	mat4 matrix;
	if (parts_v2) {
		glm_mat4_copy(parent_matrix, matrix);
		parts_apply_local_transform(parts, matrix);
		parts->global.pos = (SDL_FPoint){matrix[3][0], matrix[3][1]};
		glm_mat4_copy(matrix, parts->committed_matrix);
		if (parts->world_matrix_self == parts) {
			glm_mat4_copy(matrix, parts->world_matrix);
			parts->world_matrix_gen = parts_render_gen;
		}
	}
	if (parts_get_sprite_z(parts) != parts->sp.z
			|| parts_get_sprite_z2(parts) != parts->sp.z2) {
		parts_list_resort(parts);
	}

	if (parts->dirty) {
		TAILQ_REMOVE(&dirty_list, parts, dirty_list_entry);
		parts->dirty = false;
	}

	if (!parts_v2) parts_do_layout(parts);

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_component_mat(child, matrix);
	}
}

static void parts_update_component(struct parts *parts)
{
	mat4 parent_matrix = GLM_MAT4_IDENTITY_INIT;
	if (parts_v2) {
		if (parts->parent) {
			parts_world_matrix(parts->parent, parent_matrix);
		} else if (root_pos.x != 0 || root_pos.y != 0) {
			glm_translate(parent_matrix, (vec3){root_pos.x, root_pos.y, 0});
		}
	}
	parts_update_component_mat(parts, parent_matrix);
}

void PE_UpdateComponent(possibly_unused int passed_time)
{
	parts_cg_async_drain_ready(PARTS_CG_UPLOAD_BUDGET_MS);
	// After loading a save, the game script may compute a negative time delta
	// because it stores an absolute system.GetTime() value in the save data,
	// which is meaningless across process restarts.
	if (passed_time < 0)
		passed_time = 0;
	while (!TAILQ_EMPTY(&dirty_list)) {
		// pop parts object from dirty list
		struct parts *parts = TAILQ_FIRST(&dirty_list);
		TAILQ_REMOVE(&dirty_list, parts, dirty_list_entry);
		parts->dirty = false;

		// update parent
		struct parts *parent;
		if (parts->pending_parent >= 0 && (parent = parts_try_get(parts->pending_parent))) {
			bool creates_cycle = (parent == parts);
			for (struct parts *anc = parent; !creates_cycle && anc; anc = anc->parent) {
				if (anc == parts)
					creates_cycle = true;
			}
			if (!creates_cycle) {
				if (parts->parent) {
					TAILQ_REMOVE(&parts->parent->children, parts, child_list_entry);
				}
				parts->parent = parent;
				TAILQ_INSERT_TAIL(&parent->children, parts, child_list_entry);

				// if parent is layout box, mark it dirty so that it can re-layout its children
				if (parent->states[0].type == PARTS_LAYOUT_BOX)
					parts_component_dirty(parent);
			} else {
				WARNING("Ignoring cyclic parent %d for parts %d", parts->pending_parent, parts->no);
			}
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
	if (parts_v2) {
		// Resolve all pending parent links first, then measure children before parents.
		struct parts *parts;
		PARTS_LIST_FOREACH(parts) {
			if (!parts->parent) {
				parts_update_layout_tree(parts);
			}
		}
		parts_scrollbar_update_links();
		uint64_t gen_start = ++parts_render_gen;
		PARTS_LIST_FOREACH(parts) {
			if (!parts->parent) {
				parts_update_component(parts);
			}
		}
		if (parts_render_gen != gen_start) {
			PARTS_LIST_FOREACH(parts) {
				if (parts->world_matrix_self == parts && parts->world_matrix_gen >= gen_start)
					parts->world_matrix_gen = parts_render_gen;
			}
		}
	}
}

static void parts_prewarm_offscreen_cproc(void)
{
	uint64_t freq = SDL_GetPerformanceFrequency();
	uint64_t budget = freq ? (freq * 4) / 1000 : 0;
	uint64_t t_start = SDL_GetPerformanceCounter();
	int built = 0;
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (parts->textures_suspended || !parts_is_visible(parts))
			continue;
		struct parts_state *state = parts_display_state(parts);
		if (state->type != PARTS_CONSTRUCTION_PROCESS || !state->cproc.needs_build)
			continue;
		if (parts_cg_on_screen(parts, &state->common))
			continue;
		parts_build_construction_process(parts, &state->cproc);
		built++;
		if (built >= 4 || (budget && SDL_GetPerformanceCounter() - t_start >= budget))
			break;
	}
}

bool parts_message_window_show = true;
bool parts_frame_did_heavy_load = false;

static float clamp_message_window_rate(float rate)
{
	if (!isfinite(rate))
		return 1.0f;
	if (rate < 0.0f)
		return 0.0f;
	if (rate > 1.0f)
		return 1.0f;
	return rate;
}

void PE_SetMessageWindowShow(bool show)
{
	if (parts_message_window_show != show) {
		parts_message_window_show = show;
		parts_engine_dirty();
	}
}

void PE_SetMessageWindowEffectRates(float multiply_rate, float alpha_rate)
{
	float mul = clamp_message_window_rate(multiply_rate);
	float alpha = clamp_message_window_rate(alpha_rate);
	if (parts_message_window_mul_color_rate != mul ||
			parts_message_window_alpha_rate != alpha) {
		parts_message_window_mul_color_rate = mul;
		parts_message_window_alpha_rate = alpha;
		parts_engine_dirty();
	}
}

void PE_Update(int passed_time, bool message_window_show)
{
	PE_UpdateWithTimeScale(passed_time, passed_time, message_window_show);
}

void PE_UpdateWithTimeScale(int passed_time, int scaled_time, bool message_window_show)
{
	PE_SetMessageWindowShow(message_window_show);
	PE_UpdateComponent(passed_time);
	audio_update();
	parts_update_animation(passed_time, scaled_time);
	PE_UpdateInputState(passed_time);
	parts_scrollbar_notify_changes();
	if (!parts_frame_did_heavy_load && !parts_input_is_busy()) {
		bool was_dirty = parts_is_dirty();
		parts_prewarm_offscreen_cproc();
		if (!was_dirty)
			parts_engine_clean();
	}
	parts_frame_did_heavy_load = false;
	parts_render_update();
}

void PE_UpdateParts(int passed_time, possibly_unused bool is_skip, bool message_window_show)
{
	parts_cg_async_drain_ready(PARTS_CG_UPLOAD_BUDGET_MS);
	PE_SetMessageWindowShow(message_window_show);
	audio_update();
	parts_update_animation(passed_time, passed_time);
	parts_render_update();
}

void PE_SetEnableSpeedUp(int parts_no, bool enable)
{
	parts_get(parts_no)->enable_speed_up = enable;
}

bool PE_IsEnableSpeedUp(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? parts->enable_speed_up : true;
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

bool PE_SetPartsCG(int parts_no, struct string *cg_name, int sprite_deform, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	parts->sprite_deform = sprite_deform;
	if (!cg_name || *(cg_name->text) == '\0') {
		parts_state_reset(&parts->states[state], PARTS_CG);
		parts_dirty(parts);
		return true;
	}

	struct parts_cg *cg = parts_get_cg(parts, state);
	return parts_cg_set(parts, cg, cg_name);
}

bool PE_SetPartsCGThread(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	parts->sprite_deform = 0;
	if (!cg_name || *(cg_name->text) == '\0') {
		parts_state_reset(&parts->states[state], PARTS_CG);
		parts_dirty(parts);
		return true;
	}
	if (is_save_cg_name(cg_name->text) || parts_current != &live_context) {
		struct parts_cg *cg = parts_get_cg(parts, state);
		return parts_cg_set(parts, cg, cg_name);
	}

	parts_cg_cache_sync_archive_gen();
	uint64_t archive_gen = cg_cache_archive_gen;

	struct parts_cg *pcg = parts_get_cg(parts, state);
	if (pcg->cg_asset && pcg->cg_asset->archive_gen == archive_gen
			&& pcg->common.texture.handle
			&& pcg->name && !strcmp(pcg->name->text, cg_name->text)) {
		pcg->load_gen = ++next_cg_load_gen;
		pcg->async_gen = 0;
		parts_set_dims(parts, &pcg->common, pcg->common.texture.w, pcg->common.texture.h);
		parts_dirty(parts);
		return true;
	}
	struct parts_cg_asset *cached = parts_cg_asset_find_by_name(cg_name->text);
	if (cached) {
		pcg->load_gen = ++next_cg_load_gen;
		pcg->async_gen = 0;
		parts_cg_attach_asset(parts, pcg, cached, string_dup(cg_name));
		return true;
	}
	if (!parts_cg_async_ensure_started())
		return parts_cg_set(parts, pcg, cg_name);

	uint64_t gen = ++next_cg_load_gen;
	pcg->load_gen = gen;
	pcg->async_gen = gen;

	struct parts_cg_async_target *target = xcalloc(1, sizeof(*target));
	target->parts_no = parts_no;
	target->state = state;
	target->gen = gen;

	SDL_LockMutex(cg_async.mutex);
	struct parts_cg_async_job *job = cg_async_find_job_locked(cg_name->text, archive_gen);
	if (job) {
		if (job->targets_tail)
			job->targets_tail->next = target;
		else
			job->targets = target;
		job->targets_tail = target;
	} else {
		job = xcalloc(1, sizeof(*job));
		job->cg_name = xstrdup(cg_name->text);
		job->archive_gen = archive_gen;
		job->state = CG_JOB_QUEUED;
		job->targets = job->targets_tail = target;
		cg_async_insert_job_locked(job);
		TAILQ_INSERT_TAIL(&cg_async.pending_q, job, queue_entry);
		cg_async.nr_queued++;
		SDL_CondSignal(cg_async.work_cond);
	}
	SDL_UnlockMutex(cg_async.mutex);
	return true;
}

bool PE_IsThreadLoading(int parts_no, int state)
{
	parts_cg_async_drain_ready(PARTS_CG_UPLOAD_BUDGET_MS);
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_CG)
		return false;
	return parts->states[state].cg.async_gen != 0;
}

bool PE_IsExistWorkerThread(void)
{
	parts_cg_async_drain_ready(PARTS_CG_UPLOAD_BUDGET_MS);
	if (!cg_async.initialized)
		return false;
	SDL_LockMutex(cg_async.mutex);
	bool busy = (cg_async.nr_queued + cg_async.nr_decoding + cg_async.nr_ready) > 0;
	SDL_UnlockMutex(cg_async.mutex);
	return busy;
}

bool PE_SetPartsCG_by_index(int parts_no, int cg_no, int sprite_deform, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	parts->sprite_deform = sprite_deform;
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
		int sprite_deform, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	parts->sprite_deform = sprite_deform;
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
	} else if (!strncmp(cg_name->text, "<save>SaveData\\", 15)) {
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
	if (!parts_state_valid(--state) || !cg_name)
		return;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_CG)
		return;
	struct parts_cg *cg = &parts->states[state].cg;
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
	*x = *y = *w = *h = 0;
	if (!parts_state_valid(--state))
		return;

	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_CG)
		return;
	struct parts_cg *cg = &parts->states[state].cg;
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

	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_CG)
		return 0;
	return parts->states[state].cg.no;
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
	for (unsigned i = 0; i < anim->nr_frames; i++) {
		gfx_delete_texture(&anim->frames[i]);
	}
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
	bool r = _parts_animation_set_cg(parts, anim, cg_no, nr_frames, frame_time,
			load_loop_cg_by_index, NULL);
	if (r && anim->cg_name) {
		free_string(anim->cg_name);
		anim->cg_name = NULL;
	}
	return r;
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
	if (r) {
		if (anim->cg_name)
			free_string(anim->cg_name);
		anim->cg_name = string_dup(cg_name);
	}
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
	gfx_delete_texture(&g->cg);
	gfx_delete_texture(&g->common.texture);
	gfx_init_texture_with_cg(&g->cg, cg);

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
	cg_free(cg);
	g->cg_no = cg_no;
	if (g->cg_name)
		free_string(g->cg_name);
	g->cg_name = string_dup(cg_name);
	return true;
}

bool parts_gauge_set_cg_by_index(struct parts *parts, struct parts_gauge *g, int cg_no)
{
	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return false;
	_parts_set_gauge_cg(parts, g, cg);
	cg_free(cg);
	g->cg_no = cg_no;
	if (g->cg_name) {
		free_string(g->cg_name);
		g->cg_name = NULL;
	}
	return true;
}

bool PE_SetHGaugeCG(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	if (!parts_gauge_set_cg(parts, g, cg_name))
		return false;
	if (g->rate != 1.0f)
		parts_hgauge_set_rate(parts, g, g->rate);
	return true;
}

bool PE_SetHGaugeCG_by_index(int parts_no, int cg_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	if (!parts_gauge_set_cg_by_index(parts, g, cg_no))
		return false;
	if (g->rate != 1.0f)
		parts_hgauge_set_rate(parts, g, g->rate);
	return true;
}

bool PE_SetVGaugeCG(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	if (!parts_gauge_set_cg(parts, g, cg_name))
		return false;
	if (g->rate != 1.0f)
		parts_vgauge_set_rate(parts, g, g->rate);
	return true;
}

bool PE_SetVGaugeCG_by_index(int parts_no, int cg_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	if (!parts_gauge_set_cg_by_index(parts, g, cg_no))
		return false;
	if (g->rate != 1.0f)
		parts_vgauge_set_rate(parts, g, g->rate);
	return true;
}

void parts_hgauge_set_rate(struct parts *parts, struct parts_gauge *g, float rate)
{
	if (!g->common.texture.handle) {
		WARNING("HGauge texture uninitialized");
		return;
	}
	int w = g->cg.w;
	int h = g->cg.h;
	int pixels = max(0, min(w, (int)(rate * w)));
	if (!g->reverse) {
		if (pixels > 0)
			gfx_copy_with_alpha_map(&g->common.texture, 0, 0, &g->cg, 0, 0, pixels, h);
		if (w - pixels > 0)
			gfx_fill_amap(&g->common.texture, pixels, 0, w - pixels, h, 0);
	} else {
		if (w - pixels > 0)
			gfx_fill_amap(&g->common.texture, 0, 0, w - pixels, h, 0);
		if (pixels > 0)
			gfx_copy_with_alpha_map(&g->common.texture, w - pixels, 0, &g->cg, w - pixels, 0, pixels, h);
	}
	parts_dirty(parts);
	g->rate = rate;
}

void parts_vgauge_set_rate(struct parts *parts, struct parts_gauge *g, float rate)
{
	if (!g->common.texture.handle) {
		WARNING("VGauge texture uninitialized");
		return;
	}
	int w = g->cg.w;
	int h = g->cg.h;
	int pixels = max(0, min(h, (int)(rate * h)));
	if (!g->reverse) {
		if (h - pixels > 0)
			gfx_fill_amap(&g->common.texture, 0, 0, w, h - pixels, 0);
		if (pixels > 0)
			gfx_copy_with_alpha_map(&g->common.texture, 0, h - pixels, &g->cg, 0, h - pixels, w, pixels);
	} else {
		if (pixels > 0)
			gfx_copy_with_alpha_map(&g->common.texture, 0, 0, &g->cg, 0, 0, w, pixels);
		if (h - pixels > 0)
			gfx_fill_amap(&g->common.texture, 0, pixels, w, h - pixels, 0);
	}
	parts_dirty(parts);
	g->rate = rate;
}

void PE_SetHGaugeReverse(int parts_no, bool reverse, int state)
{
	if (!parts_state_valid(--state))
		return;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	if (g->reverse == reverse)
		return;
	g->reverse = reverse;
	if (g->common.texture.handle)
		parts_hgauge_set_rate(parts, g, g->rate);
}

void PE_SetVGaugeReverse(int parts_no, bool reverse, int state)
{
	if (!parts_state_valid(--state))
		return;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	if (g->reverse == reverse)
		return;
	g->reverse = reverse;
	if (g->common.texture.handle)
		parts_vgauge_set_rate(parts, g, g->rate);
}

bool PE_SetHGaugeRate(int parts_no, float numerator, float denominator, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	g->numerator = numerator;
	g->denominator = denominator;
	float rate = denominator != 0.0f ? max(0.0f, min(1.0f, numerator / denominator)) : 0.0f;
	g->rate = rate;
	if (g->common.texture.handle)
		parts_hgauge_set_rate(parts, g, rate);
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
	g->numerator = numerator;
	g->denominator = denominator;
	float rate = denominator != 0.0f ? max(0.0f, min(1.0f, numerator / denominator)) : 0.0f;
	g->rate = rate;
	if (g->common.texture.handle)
		parts_vgauge_set_rate(parts, g, rate);
	return true;
}

bool PE_SetVGaugeRate_int(int parts_no, int numerator, int denominator, int state)
{
	return PE_SetVGaugeRate(parts_no, numerator, denominator, state);
}

float PE_GetHGaugeNumerator(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0.0f;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_HGAUGE)
		return 0.0f;
	return parts->states[state].gauge.numerator;
}

float PE_GetHGaugeDenominator(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 1.0f;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_HGAUGE)
		return 1.0f;
	return parts->states[state].gauge.denominator;
}

float PE_GetVGaugeNumerator(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0.0f;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_VGAUGE)
		return 0.0f;
	return parts->states[state].gauge.numerator;
}

float PE_GetVGaugeDenominator(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 1.0f;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_VGAUGE)
		return 1.0f;
	return parts->states[state].gauge.denominator;
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
	struct parts *p = parts_get(parts_no);
	struct parts_numeral *n = parts_get_numeral(p, state);
	n->font_no = parts_load_numeral_font_separate_string(cg_name);
	n->show_type = 0;
	if (n->cg_name)
		free_string(n->cg_name);
	n->cg_name = cg_name ? string_dup(cg_name) : string_ref(&EMPTY_STRING);
	return parts_numeral_update(p, n);
}

bool PE_SetNumeralCG_by_index(int parts_no, int cg_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *p = parts_get(parts_no);
	struct parts_numeral *n = parts_get_numeral(p, state);
	n->font_no = parts_load_numeral_font_separate(cg_no);
	n->show_type = 0;
	if (n->cg_name)
		free_string(n->cg_name);
	n->cg_name = string_ref(&EMPTY_STRING);
	return parts_numeral_update(p, n);
}

bool PE_SetNumeralLinkedCGNumberWidthWidthList_by_index(int parts_no, int cg_no,
		int w0, int w1, int w2, int w3, int w4, int w5, int w6, int w7, int w8,
		int w9, int w_minus, int w_comma, int state)
{
	if (!parts_state_valid(--state))
		return false;

	if (!asset_exists(ASSET_CG, cg_no))
		return false;

	int w[12] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w_minus, w_comma };
	struct parts *p = parts_get(parts_no);
	struct parts_numeral *n = parts_get_numeral(p, state);
	n->font_no = parts_load_numeral_font_combined(cg_no, w);
	n->show_type = 1;
	if (n->cg_name)
		free_string(n->cg_name);
	n->cg_name = string_ref(&EMPTY_STRING);
	return parts_numeral_update(p, n);
}

bool PE_SetNumeralLinkedCGNumberWidthWidthList(int parts_no, struct string *cg_name,
		int w0, int w1, int w2, int w3, int w4, int w5, int w6, int w7, int w8,
		int w9, int w_minus, int w_comma, int state)
{
	if (!parts_state_valid(--state))
		return false;
	if (!cg_name || !cg_name->size)
		return false;

	int w[12] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w_minus, w_comma };
	int font_no = parts_load_numeral_font_combined_by_name(cg_name, w);
	if (font_no < 0)
		return false;

	struct parts *p = parts_get(parts_no);
	struct parts_numeral *n = parts_get_numeral(p, state);
	n->font_no = font_no;
	n->show_type = 1;
	if (n->cg_name)
		free_string(n->cg_name);
	n->cg_name = string_dup(cg_name);
	return parts_numeral_update(p, n);
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

	num->length = max(parts_v2 ? 0 : 1, length);
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

static struct parts_numeral *try_get_numeral(int no, int state)
{
	if (!parts_state_valid(--state)) return NULL;
	struct parts *p = parts_try_get(no);
	return p && p->states[state].type == PARTS_NUMERAL ? &p->states[state].num : NULL;
}

static bool parts_set_numeral_font(int no, int face, int size, int r, int g, int b, float bold,
		int er, int eg, int eb, float edge, int state, const bool *full_pitch)
{
	if (!parts_state_valid(--state)) return false;
	struct parts *p = parts_get(no);
	struct parts_numeral *n = parts_get_numeral(p, state);
	n->show_type = 2;
	if (full_pitch)
		n->full_pitch = *full_pitch;
	n->ts.face = face;
	n->ts.size = size;
	n->ts.font_size = NULL;
	n->ts.color = (SDL_Color){r, g, b, 255};
	n->ts.bold_width = bold;
	n->ts.weight = bold * 1000;
	n->ts.edge_color = (SDL_Color){er, eg, eb, 255};
	text_style_set_edge_width(&n->ts, edge);
	return parts_numeral_update(p, n);
}

void PE_SetNumeralFont(int no, int face, int size, int r, int g, int b, float bold,
		int er, int eg, int eb, float edge, int state)
{
	parts_set_numeral_font(no, face, size, r, g, b, bold, er, eg, eb, edge, state, NULL);
}

bool PE_SetNumeralFontWithPitch(int no, int face, int size, int r, int g, int b, float bold,
		int er, int eg, int eb, float edge, bool full_pitch, int state)
{
	return parts_set_numeral_font(no, face, size, r, g, b, bold, er, eg, eb, edge, state, &full_pitch);
}

void PE_GetNumeralFont(int no, int *face, int *size, int *r, int *g, int *b, float *bold,
		int *er, int *eg, int *eb, float *edge, int state)
{
	struct parts_numeral *n = try_get_numeral(no, state);
	struct text_style *ts = n ? &n->ts : &default_text_style;
	*face = ts->face;
	*size = ts->size;
	*r = ts->color.r; *g = ts->color.g; *b = ts->color.b;
	*bold = ts->bold_width;
	*er = ts->edge_color.r; *eg = ts->edge_color.g; *eb = ts->edge_color.b;
	*edge = ts->edge_left;
}

void PE_SetNumeralFullPitch(int no, bool full_pitch, int state)
{
	if (!parts_state_valid(--state)) return;
	struct parts *p = parts_get(no);
	struct parts_numeral *n = parts_get_numeral(p, state);
	n->full_pitch = full_pitch;
	parts_numeral_update(p, n);
}

void PE_SetNumeralShowPadding(int no, bool show, int state)
{
	if (!parts_state_valid(--state)) return;
	struct parts *p = parts_get(no);
	struct parts_numeral *n = parts_get_numeral(p, state);
	n->show_padding = show;
	parts_numeral_update(p, n);
}

void PE_SetNumeralShowType(int no, int type, int state)
{
	if (!parts_state_valid(--state) || type < 0 || type > 2) return;
	struct parts *p = parts_get(no);
	struct parts_numeral *n = parts_get_numeral(p, state);
	n->show_type = type;
	parts_numeral_update(p, n);
}

#define NUMERAL_GETTER(type, name, member, fallback) \
	type PE_##name(int no, int state) { \
		struct parts_numeral *n = try_get_numeral(no, state); \
		return n ? n->member : fallback; \
	}
NUMERAL_GETTER(bool, IsNumeralShowPadding, show_padding, true)
NUMERAL_GETTER(int, GetNumeralCGType, show_type, 0)
NUMERAL_GETTER(int, GetNumeralNumber, num, 0)
NUMERAL_GETTER(bool, IsNumeralShowComma, show_comma, false)
NUMERAL_GETTER(int, GetNumeralSpace, space, 0)
NUMERAL_GETTER(int, GetNumeralLength, length, 0)
#undef NUMERAL_GETTER

struct string *PE_GetNumeralCGName(int no, int state)
{
	struct parts_numeral *n = try_get_numeral(no, state);
	return string_ref(n && n->cg_name ? n->cg_name : &EMPTY_STRING);
}

void PE_GetNumeralCGNumberWidthList(int no, int *w0, int *w1, int *w2, int *w3,
		int *w4, int *w5, int *w6, int *w7, int *w8, int *w9, int *minus, int *comma, int state)
{
	struct parts_numeral *n = try_get_numeral(no, state);
	struct parts_numeral_font *font = n && n->font_no >= 0 && n->font_no < parts_nr_numeral_fonts
		? &parts_numeral_fonts[n->font_no] : NULL;
	int *widths[12] = {w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, minus, comma};
	for (int i = 0; i < 12; i++)
		*widths[i] = font && font->type == PARTS_NUMERAL_FONT_COMBINED ? font->width[i] : 0;
}

void PE_GetNumeralSurfaceArea(int no, int *x, int *y, int *w, int *h, int state)
{
	struct parts_numeral *n = try_get_numeral(no, state);
	Rectangle area = n ? n->common.surface_area : (Rectangle){0};
	*x = area.x; *y = area.y; *w = area.w; *h = area.h;
}

void PE_ReleaseParts(int parts_no)
{
	parts_release(parts_no);
}

void parts_world_matrix(struct parts *parts, mat4 out)
{
	if (!parts_v2) {
		glm_mat4_identity(out);
		glm_translate(out, (vec3){parts->global.pos.x, parts->global.pos.y, 0});
		glm_rotate_z(out, glm_rad(parts->local.rotation.z), out);
		if (parts->local.rotation.x != 0.0f)
			glm_rotate_x(out, glm_rad(-parts->local.rotation.x), out);
		if (parts->local.rotation.y != 0.0f)
			glm_rotate_y(out, glm_rad(parts->local.rotation.y), out);
		glm_scale(out, (vec3){parts->global.scale.x, parts->global.scale.y, 1});
		return;
	}
	if (parts->world_matrix_self == parts && parts->world_matrix_gen == parts_render_gen) {
		glm_mat4_copy(parts->world_matrix, out);
		return;
	}
	// Child positions live in their parent's transformed coordinate system.
	// Multiplying full matrices also retains shear from nonuniform scaling.
	if (parts->parent) {
		parts_world_matrix(parts->parent, out);
	} else {
		glm_mat4_identity(out);
		if (root_pos.x != 0 || root_pos.y != 0)
			glm_translate(out, (vec3){root_pos.x, root_pos.y, 0});
	}
	parts_apply_local_transform(parts, out);
	if (parts->world_matrix_self == parts) {
		glm_mat4_copy(out, parts->world_matrix);
		parts->world_matrix_gen = parts_render_gen;
	}
}

void PE_GetComponentAbsoluteSquarePos(int no, float *x0, float *y0,
		float *x1, float *y1, float *x2, float *y2, float *x3, float *y3, int state)
{
	*x0 = *y0 = *x1 = *y1 = *x2 = *y2 = *x3 = *y3 = 0;
	if (!parts_state_valid(--state)) return;
	struct parts *parts = parts_try_get(no);
	if (!parts) return;
	struct parts_common *c = &parts->states[state].common;
	mat4 matrix;
	parts_world_matrix(parts, matrix);
	float x = c->hitbox.x - (!parts_v2 ? parts->local.pos.x : 0);
	float y = c->hitbox.y - (!parts_v2 ? parts->local.pos.y : 0);
	float *xs[] = {x0, x1, x2, x3}, *ys[] = {y0, y1, y2, y3};
	for (int i = 0; i < 4; i++) {
		vec4 v = {x + (i & 1 ? c->hitbox.w : 0), y + (i & 2 ? c->hitbox.h : 0), 0, 1};
		if (parts->states[state].type == PARTS_RECT_DETECTION && parts->states[state].rect.custom) {
			v[0] = parts->states[state].rect.points[i].x + c->origin_offset.x;
			v[1] = parts->states[state].rect.points[i].y + c->origin_offset.y;
		}
		glm_mat4_mulv(matrix, v, v);
		*xs[i] = v[0]; *ys[i] = v[1];
	}
}

void PE_SetComment(int parts_no, struct string *comment)
{
	struct parts *parts = parts_get(parts_no);
	struct string *copy = string_ref(comment);
	free_string(parts->comment);
	parts->comment = copy;
}

struct string *PE_GetComment(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return string_ref(parts ? parts->comment : &EMPTY_STRING);
}

void PE_ReleaseAllParts(void)
{
	parts_release_all();
}

void PE_ReleaseAll_list(struct page **erase_number_list)
{
	if (erase_number_list) {
		struct parts *parts;
		PARTS_LIST_FOREACH(parts) {
			*erase_number_list = array_pushback(*erase_number_list,
					(union vm_value){.i = parts->no}, AIN_ARRAY_INT, -1);
		}
	}
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

void PE_SetComponentPos(int no, float x, float y)
{
	parts_set_posf(parts_get(no), (SDL_FPoint){x, y});
}

float PE_GetComponentPosX(int no)
{
	return parts_get(no)->local.pos.x;
}

float PE_GetComponentPosY(int no)
{
	return parts_get(no)->local.pos.y;
}

void PE_GetComponentAbsolutePos(int no, float *x, float *y)
{
	struct parts *parts = parts_try_get(no);
	*x = parts ? parts->global.pos.x : 0;
	*y = parts ? parts->global.pos.y : 0;
}

float PE_GetComponentAbsolutePosX(int no)
{
	float x, y;
	PE_GetComponentAbsolutePos(no, &x, &y);
	return x;
}

float PE_GetComponentAbsolutePosY(int no)
{
	float x, y;
	PE_GetComponentAbsolutePos(no, &x, &y);
	return y;
}

int PE_GetPartsWidth(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_get(parts_no);
	if (parts_v2 && ((ain->version >= 11 && parts->component_type == 15) || parts->widget
			|| parts->states[state].type == PARTS_LAYOUT_BOX)) {
		int w, h;
		parts_get_layout_size(parts, &w, &h);
		return w;
	}
	return parts->states[state].common.w;
}

int PE_GetPartsHeight(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_get(parts_no);
	if (parts_v2 && ((ain->version >= 11 && parts->component_type == 15) || parts->widget
			|| parts->states[state].type == PARTS_LAYOUT_BOX)) {
		int w, h;
		parts_get_layout_size(parts, &w, &h);
		return h;
	}
	return parts->states[state].common.h;
}

int PE_GetPartsUpperLeftPosX(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_get(parts_no);
	int x = parts->states[state].common.hitbox.x;
	if (parts_v2) {
		if (parts_hierarchical_matrix) {
			vec4 pos = {x, parts->states[state].common.hitbox.y, 0, 1};
			glm_mat4_mulv(parts->committed_matrix, pos, pos);
			return pos[0];
		}
		mat4 matrix;
		parts_world_matrix(parts, matrix);
		return matrix[3][0] + x;
	}
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
	if (parts_v2) {
		if (parts_hierarchical_matrix) {
			vec4 pos = {parts->states[state].common.hitbox.x, y, 0, 1};
			glm_mat4_mulv(parts->committed_matrix, pos, pos);
			return pos[1];
		}
		mat4 matrix;
		parts_world_matrix(parts, matrix);
		return matrix[3][1] + y;
	}
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
	if (parts_v2) {
		if (parent_parts_no <= 0)
			parent_parts_no = parts->controller_no == PARTS_CONTROLLER_SYSTEM_OVERLAY
				? ctrl_stack.overlay_id : ctrl_stack.ids[parts->controller_no];
		struct parts *parent = parent_parts_no == parts_no ? NULL : parts_get(parent_parts_no);
		for (struct parts *p = parent; p; p = p->parent)
			if (p == parts) VM_ERROR("Cyclic parts parent relationship");
		if (parts->parent == parent) return;
		if (parts->parent) {
			parts_component_dirty(parts->parent);
			TAILQ_REMOVE(&parts->parent->children, parts, child_list_entry);
		}
		parts->parent = parent;
		if (parent) {
			TAILQ_INSERT_TAIL(&parent->children, parts, child_list_entry);
			parts_change_controller(parts, parent->controller_no);
			parts_component_dirty(parent);
		} else parts->global = parts->local;
		parts->pending_parent = -1;
		parts_component_dirty(parts);
		return;
	}
	parts->pending_parent = parent_parts_no;
	parts_component_dirty(parts);
}

int PE_GetParentPartsNumber(int parts_no)
{
	if (parts_v2 && parts_no <= 0)
		return 0;
	struct parts *parts = parts_get(parts_no);
	if (parts->parent)
		return parts->parent->no;
	return parts_v2 ? 0 : -1;
}

int PE_NumofChild(int no)
{
	struct parts *parent = parts_get(no), *p;
	int count = 0;
	PARTS_FOREACH_CHILD(p, parent) count++;
	return count;
}

int PE_GetChildIndex(int no, int child)
{
	struct parts *parent = parts_get(no), *p;
	int index = 0;
	PARTS_FOREACH_CHILD(p, parent) {
		if (p->no == child) return index;
		index++;
	}
	return -1;
}

int PE_GetChild(int no, int index)
{
	struct parts *parent = parts_get(no), *p;
	if (index >= 0) PARTS_FOREACH_CHILD(p, parent) if (!index--) return p->no;
	return 0;
}

bool PE_IsExistChild(int no, int child) { return PE_GetChildIndex(no, child) >= 0; }
void PE_AddChild(int no, int child) { PE_SetParentPartsNumber(child, no); }

void PE_InsertChild(int no, int index, int child)
{
	if (!parts_v2 || child == no)
		return;
	PE_SetParentPartsNumber(child, no);
	struct parts *parent = parts_get(no), *p = parts_get(child), *at;
	if (p->parent != parent)
		return;
	TAILQ_REMOVE(&parent->children, p, child_list_entry);
	PARTS_FOREACH_CHILD(at, parent) {
		if (index--) continue;
		TAILQ_INSERT_BEFORE(at, p, child_list_entry);
		parts_component_dirty(parent);
		return;
	}
	TAILQ_INSERT_TAIL(&parent->children, p, child_list_entry);
	parts_component_dirty(parent);
}

void PE_RemoveChild(int no, int child)
{
	struct parts *p = parts_try_get(child);
	if (p && p->parent && p->parent->no == no) PE_SetParentPartsNumber(child, -1);
}

void PE_ClearChild(int no)
{
	struct parts *parent = parts_get(no), *p = TAILQ_FIRST(&parent->children);
	while (p) {
		struct parts *next = TAILQ_NEXT(p, child_list_entry);
		PE_RemoveChild(no, p->no);
		p = next;
	}
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
	parts_set_rotation_x(parts_get(parts_no), rot_x);
}

void PE_SetPartsRotateY(int parts_no, float rot_y)
{
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
	parts_get(parts_no)->pixel_decide = pixel_decide;
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

bool PE_save_thumbnail(struct string *filename, int reduction_factor)
{
	if (reduction_factor < 1)
		reduction_factor = 1;

	Texture *src = gfx_main_surface();
	int w, h;
	if (ain->version >= 12 || reduction_factor > 16) {
		w = reduction_factor;
		h = src->w > 0 ? (int)((int64_t)src->h * w / src->w) : 1;
		if (h < 1)
			h = 1;
	} else {
		w = src->w / reduction_factor;
		h = src->h / reduction_factor;
	}

	// Downscale by repeatedly halving until we are within a factor of two of
	// the target size, then do the final stretch. This avoids the aliasing
	// that a single bilinear minification would otherwise produce.
	Texture tmp, *cur = src;
	bool have_tmp = false;
	while (cur->w / 2 > w && cur->h / 2 > h) {
		Texture next;
		gfx_init_texture_blank(&next, cur->w / 2, cur->h / 2);
		gfx_copy_stretch_with_alpha_map(&next, 0, 0, next.w, next.h, cur, 0, 0, cur->w, cur->h);
		if (have_tmp)
			gfx_delete_texture(&tmp);
		tmp = next;
		cur = &tmp;
		have_tmp = true;
	}

	Texture dst;
	gfx_init_texture_blank(&dst, w, h);
	gfx_copy_stretch_with_alpha_map(&dst, 0, 0, w, h, cur, 0, 0, cur->w, cur->h);
	if (have_tmp)
		gfx_delete_texture(&tmp);

	char *path = savedir_path(filename->text);
	int r = gfx_save_texture(&dst, path, ALCG_QNT);
	free(path);
	gfx_delete_texture(&dst);
	return !!r;
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
	if ((type >= 0 && type <= 7) || type == 9) {
		PE_SetSpecialComponentType(parts_no, type);
		return;
	}
	if (!parts_state_valid(--state))
		return;
	struct parts *parts = parts_get(parts_no);
	parts_widget_free(parts);
	parts_panel_free(parts);
	parts_scrollbar_free(parts);
	parts_message_window_free(parts);
	parts->component_type = 0;
	enum parts_type pt = PARTS_UNINITIALIZED;
	switch (type) {
	case 8:  pt = PARTS_LAYOUT_BOX; break;
	case 10: pt = PARTS_UNINITIALIZED; break;
	case 11: pt = PARTS_CG; break;
	case 12: pt = PARTS_ANIMATION; break;
	case 13: pt = PARTS_TEXT; break;
	case 14: pt = PARTS_HGAUGE; break;
	case 15: pt = PARTS_VGAUGE; break;
	case 16: pt = PARTS_NUMERAL; break;
	case 17:
	case 19:
		pt = PARTS_RECT_DETECTION;
		break;
	case 18: pt = PARTS_CONSTRUCTION_PROCESS; break;
	case 20: pt = PARTS_FLAT; break;
	case 21: pt = PARTS_3DLAYER; break;
	case 22: pt = PARTS_MOVIE; break;
	default:
		VM_ERROR("unknown component type %d", type);
	}
	if (parts->states[state].type != pt)
		parts_state_reset(&parts->states[state], pt);
	if (type == 19)
		parts->states[state].rect.is_cg_detection = true;
}

int PE_GetComponentType(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return -1;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return -1;
	if (!ain || ain->version < 12) {
		int special = PE_GetSpecialComponentType(parts_no);
		if (special >= 0)
			return special;
	}

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
	case PARTS_RECT_DETECTION:
		return parts->states[state].rect.is_cg_detection ? 19 : 17;
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
	parts->states[state].rect.custom = false;
	parts->states[state].rect.is_cg_detection = false;
	parts_common_free_alpha_mask(&parts->states[state].common);
	parts_set_dims(parts, &parts->states[state].common, w, h);
	return true;
}

void PE_SetRectangleDetectionPoint(int no, int x0, int y0, int x1, int y1,
		int x2, int y2, int x3, int y3, int state)
{
	if (!parts_state_valid(--state)) return;
	struct parts *parts = parts_get(no);
	if (parts->states[state].type != PARTS_RECT_DETECTION)
		parts_state_reset(&parts->states[state], PARTS_RECT_DETECTION);
	struct parts_rect *rect = &parts->states[state].rect;
	rect->custom = true;
	rect->is_cg_detection = false;
	parts_common_free_alpha_mask(&rect->common);
	rect->points[0] = (Point){x0, y0}; rect->points[1] = (Point){x1, y1};
	rect->points[2] = (Point){x2, y2}; rect->points[3] = (Point){x3, y3};
	rect->common.w = max(max(x0, x1), max(x2, x3)) - min(min(x0, x1), min(x2, x3));
	rect->common.h = max(max(y0, y1), max(y2, y3)) - min(min(y0, y1), min(y2, y3));
	parts_recalculate_hitbox(parts);
	parts_component_dirty(parts);
}

void PE_GetRectangleDetectionPoint(int no, int *x0, int *y0, int *x1, int *y1,
		int *x2, int *y2, int *x3, int *y3, int state)
{
	*x0 = *y0 = *x1 = *y1 = *x2 = *y2 = *x3 = *y3 = 0;
	if (!parts_state_valid(--state)) return;
	struct parts *parts = parts_try_get(no);
	if (!parts || parts->states[state].type != PARTS_RECT_DETECTION) return;
	struct parts_rect *rect = &parts->states[state].rect;
	if (!rect->custom) { *x1 = *x3 = rect->common.w; *y2 = *y3 = rect->common.h; return; }
	*x0 = rect->points[0].x; *y0 = rect->points[0].y;
	*x1 = rect->points[1].x; *y1 = rect->points[1].y;
	*x2 = rect->points[2].x; *y2 = rect->points[2].y;
	*x3 = rect->points[3].x; *y3 = rect->points[3].y;
}

bool PE_SetPartsCGDetectionSize(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_get(parts_no);
	if (parts->states[state].type != PARTS_RECT_DETECTION)
		parts_state_reset(&parts->states[state], PARTS_RECT_DETECTION);
	struct parts_rect *rect = &parts->states[state].rect;
	rect->custom = false;
	rect->is_cg_detection = true;
	parts_common_free_alpha_mask(&rect->common);
	if (rect->cg_name)
		free_string(rect->cg_name);
	rect->cg_name = cg_name ? string_dup(cg_name) : string_ref(&EMPTY_STRING);
	if (cg_name && cg_name->size > 0) {
		int cg_no;
		struct cg *cg = asset_cg_load_by_name(cg_name->text, &cg_no);
		if (cg) {
			parts_common_set_alpha_from_cg(&rect->common, cg);
			parts_set_dims(parts, &rect->common, cg->metrics.w, cg->metrics.h);
			cg_free(cg);
			return true;
		}
	}
	return false;
}

int PE_GetFreeNumber(void)
{
	if (parts_v2) {
		if (parts_current->next_free_no < 1000010000)
			parts_current->next_free_no = 1000010000;
		int no = parts_current->next_free_no;
		while (PE_IsExist(no)) no++;
		parts_current->next_free_no = no;
		return no;
	}
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

static void ctrl_stack_init(void)
{
	memset(&ctrl_stack, 0, sizeof(ctrl_stack));
	if (parts_v2) {
		ctrl_stack.active = PARTS_CONTROLLER_SYSTEM_OVERLAY;
		ctrl_stack.overlay_id = PE_GetFreeNumber();
		parts_get_layout_box(parts_get(ctrl_stack.overlay_id))->layout_type = PARTS_LAYOUT_FREE;
	}
	// Add initial default controller
	PE_AddController(-1);
}

static void parts_change_controller(struct parts *parts, int index)
{
	if (parts->controller_no != index) {
		parts->controller_no = index;
		parts_list_resort(parts);
	}
	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) parts_change_controller(child, index);
}

static void ctrl_stack_reindex(void)
{
	for (int i = 0; i < ctrl_stack.nr_controllers; i++) {
		struct parts *root = parts_try_get(ctrl_stack.ids[i]);
		if (root) parts_change_controller(root, i);
	}
}

// Adds a new controller to the stack and makes it active. Before v12, `index`
// parameter specifies the position in the stack at which to insert the new
// controller; -1 means "insert directly after the currently active
// controller". In practice the game only ever passes -1, and the active
// controller is always the top of the stack at that point, so this
// degenerates to a simple push.
int PE_AddController(int index)
{
	if (parts_v2) {
		if (ctrl_stack.nr_controllers >= PARTS_CONTROLLER_STACK_MAX)
			VM_ERROR("controller stack overflow");
		// v12's misleadingly named Index is the ID to insert after.
		// An unknown ID (including -1) means after the active controller.
		int after = PE_GetControllerIndex(index);
		if (after < 0) after = ctrl_stack.active == PARTS_CONTROLLER_SYSTEM_OVERLAY
			? ctrl_stack.nr_controllers - 1 : ctrl_stack.active;
		int pos = min(after + 1, ctrl_stack.nr_controllers);
		memmove(&ctrl_stack.ids[pos + 1], &ctrl_stack.ids[pos],
				(ctrl_stack.nr_controllers - pos) * sizeof(*ctrl_stack.ids));
		ctrl_stack.nr_controllers++;
		ctrl_stack.active = pos;
		int id = ctrl_stack.ids[pos] = PE_GetFreeNumber();
		parts_get_layout_box(parts_get(id))->layout_type = PARTS_LAYOUT_FREE;
		ctrl_stack_reindex();
		return id;
	}
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

static void parts_release_controller_children(struct parts *root, struct page **erased)
{
	while (!TAILQ_EMPTY(&root->children)) {
		struct parts *child = TAILQ_FIRST(&root->children);
		*erased = array_pushback(*erased, vm_int(child->delegate_index), AIN_ARRAY_INT, -1);
		parts_release_controller_children(child, erased);
		parts_release(child->no);
	}
}

static void parts_clear_int_output(struct page **output)
{
	if (!*output) return;
	struct page *old = *output;
	if (old->type != ARRAY_PAGE || (old->a_type != AIN_ARRAY_INT
			&& (old->a_type != AIN_ARRAY || old->array.element_type.data != AIN_INT)))
		VM_ERROR("Expected integer array output");
	*output = alloc_page(ARRAY_PAGE, old->a_type, 0);
	(*output)->array = old->array;
	delete_page_vars(old);
	free_page(old);
}

// Before v12, removes the controller at position `index`, releases all
// parts belonging to it, and returns their parts numbers in
// `erase_number_list`. `index == -1` means "remove the currently active
// controller". In practice the game only ever passes -1, and the active
// controller is always the top of the stack at that point, so this
// degenerates to a simple pop.
void PE_RemoveController(struct page **erase_number_list, int index)
{
	if (parts_v2) {
		// The original retains its last controller and omits the controller
		// root itself from the list of erased delegates.
		if (ctrl_stack.nr_controllers <= 1) return;
		int pos = PE_GetControllerIndex(index);
		if (pos < 0) pos = ctrl_stack.active == PARTS_CONTROLLER_SYSTEM_OVERLAY
			? ctrl_stack.nr_controllers - 1 : ctrl_stack.active;
		struct parts *root = parts_try_get(ctrl_stack.ids[pos]);
		if (root) {
			// The v12 HLL leaves the caller's array unchanged for an empty result.
			if (!TAILQ_EMPTY(&root->children)) parts_clear_int_output(erase_number_list);
			parts_release_controller_children(root, erase_number_list);
			parts_release(root->no);
		}
		ctrl_stack.nr_controllers--;
		memmove(&ctrl_stack.ids[pos], &ctrl_stack.ids[pos + 1],
				(ctrl_stack.nr_controllers - pos) * sizeof(*ctrl_stack.ids));
		if (ctrl_stack.active == pos) ctrl_stack.active = max(0, pos - 1);
		else if (ctrl_stack.active != PARTS_CONTROLLER_SYSTEM_OVERLAY && ctrl_stack.active > pos)
			ctrl_stack.active--;
		ctrl_stack_reindex();
		return;
	}
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
					(union vm_value){.i = p->delegate_index}, AIN_ARRAY_INT, -1);
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

void PE_MoveController(int id, int index)
{
	int from = PE_GetControllerIndex(id);
	if (from < 0) return;
	int active = PE_get_active_controller();
	// Index is an insertion position in the original order, unlike Add/Remove.
	int to = max(0, min(index, ctrl_stack.nr_controllers));
	if (from < to) to--;
	if (from == to) return;
	if (from < to)
		memmove(&ctrl_stack.ids[from], &ctrl_stack.ids[from + 1], (to - from) * sizeof(*ctrl_stack.ids));
	else
		memmove(&ctrl_stack.ids[to + 1], &ctrl_stack.ids[to], (from - to) * sizeof(*ctrl_stack.ids));
	ctrl_stack.ids[to] = id;
	ctrl_stack.active = active == ctrl_stack.overlay_id ? PARTS_CONTROLLER_SYSTEM_OVERLAY
		: PE_GetControllerIndex(active);
	ctrl_stack_reindex();
}

void PE_set_active_controller(int controller_no)
{
	if (parts_v2) {
		if (controller_no == ctrl_stack.overlay_id) {
			ctrl_stack.active = PARTS_CONTROLLER_SYSTEM_OVERLAY;
			return;
		}
		int index = PE_GetControllerIndex(controller_no);
		if (index < 0) {
			WARNING("Invalid controller ID: %d", controller_no);
			return;
		}
		ctrl_stack.active = index;
		return;
	}
	if (controller_no == PARTS_CONTROLLER_SYSTEM_OVERLAY ||
			(controller_no >= 0 && controller_no < ctrl_stack.nr_controllers))
		ctrl_stack.active = controller_no;
	else
		VM_ERROR("Invalid controller number: %d", controller_no);
}

int PE_get_active_controller(void)
{
	if (parts_v2)
		return ctrl_stack.active == PARTS_CONTROLLER_SYSTEM_OVERLAY
			? ctrl_stack.overlay_id : ctrl_stack.ids[ctrl_stack.active];
	return ctrl_stack.active;
}

int PE_get_system_controller(void)
{
	if (parts_v2) return ctrl_stack.overlay_id;
	return PARTS_CONTROLLER_SYSTEM_OVERLAY;
}

int PE_GetControllerIndex(int id)
{
	for (int i = 0; i < ctrl_stack.nr_controllers; i++) if (ctrl_stack.ids[i] == id) return i;
	return -1;
}

int PE_GetControllerID(int index)
{
	if (!ctrl_stack.nr_controllers) return -1;
	if (index < 0) index = ctrl_stack.active == PARTS_CONTROLLER_SYSTEM_OVERLAY
		? ctrl_stack.nr_controllers - 1 : ctrl_stack.active;
	return ctrl_stack.ids[min(index, ctrl_stack.nr_controllers - 1)];
}

int PE_GetControllerLength(void) { return ctrl_stack.nr_controllers; }

void PE_parts_set_want_save(int parts_no, bool want_save)
{
	parts_get(parts_no)->want_save = want_save;
}

bool PE_parts_get_want_save(int parts_no)
{
	return parts_get(parts_no)->want_save;
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
	parts_state_reset(&parts->states[state], PARTS_MOVIE);
	struct parts_movie *movie = &parts->states[state].movie;

	int sp_no = sact_SP_GetUnuseNum(0);
	struct sact_sprite *sp = sact_create_sprite(sp_no, width, height, bg_r, bg_g, bg_b, 255);
	if (!sp)
		return false;
	// The movie frames are composited by the parts engine via parts_render(),
	// so hide the bound sprite from the scene to avoid double-drawing.
	sprite_set_show(sp, false);

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
	// The 3D content is composited by the parts engine via parts_render(), so
	// hide the bound sprite from the scene to avoid double-drawing.
	sprite_set_show(sp, false);

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

static bool cproc_is_replayable_with_text(struct parts_construction_process *cproc)
{
	struct parts_cp_op *first = TAILQ_FIRST(&cproc->ops);
	if (!first)
		return false;
	if (first->type != PARTS_CP_CREATE &&
	    first->type != PARTS_CP_CREATE_PIXEL_ONLY &&
	    first->type != PARTS_CP_CG)
		return false;
	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &cproc->ops, entry) {
		if (op->type == PARTS_CP_DRAW_TEXT ||
		    op->type == PARTS_CP_COPY_TEXT ||
		    op->type == PARTS_CP_ALPHA_BLEND_TEXT ||
		    op->type == PARTS_CP_ONLY_ALPHA_TEXT)
			return true;
	}
	return false;
}

static void refresh_font_in_current_context(void)
{
	if (!parts_engine_initialized || !parts_table)
		return;
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		if (p->textures_suspended)
			continue;
		if (p->message_box) {
			parts_message_window_rebuild_text(p);
			parts_component_dirty(p);
		}
		if (p->widget && p->widget->text && p->widget->text->size > 0) {
			parts_widget_rebuild_label(p);
		}
		for (int st = 0; st < PARTS_NR_STATES; st++) {
			struct parts_state *s = &p->states[st];
			if (s->type == PARTS_TEXT) {
				parts_text_rerender_all(p, &s->text);
				parts_component_dirty(p);
			} else if (s->type == PARTS_NUMERAL && s->num.show_type == 2 && s->num.have_num) {
				parts_numeral_update(p, &s->num);
				parts_component_dirty(p);
			} else if (s->type == PARTS_CONSTRUCTION_PROCESS &&
			           cproc_is_replayable_with_text(&s->cproc)) {
				s->cproc.needs_build = true;
				parts_dirty(p);
			}
		}
	}
	PE_UpdateComponent(0);
	parts_engine_dirty();
}

void parts_refresh_live_font_scale(void)
{
	if (!parts_engine_initialized)
		return;
	refresh_font_in_current_context();
	struct parts_context *bs = parts_get_back_scene();
	if (bs && bs != parts_current) {
		struct parts_context *prev = parts_context_select(bs);
		refresh_font_in_current_context();
		parts_context_select(prev);
	}
	scene_dirty();
}

static void remap_flash_bitmap(void *val)
{
	Texture *t = val;
	if (t && t->handle)
		gfx_remap_texture(t);
}

static void recover_cg_state(struct parts_cg *state, bool suspended)
{
	if (state->common.texture.handle)
		gfx_remap_texture(&state->common.texture);
	if (state->common.texture.handle || suspended)
		return;
	struct cg *cg = NULL;
	if (state->name && state->name->size > 0) {
		int no = 0;
		cg = asset_cg_load_by_name(state->name->text, &no);
		if (cg && no > 0)
			state->no = no;
	}
	if (!cg && state->no > 0) {
		cg = asset_cg_load(state->no);
	}
	if (cg) {
		gfx_init_texture_with_cg(&state->common.texture, cg);
		cg_free(cg);
	}
}

static void recover_widget_skin(struct parts *p, struct parts_state *state, int index, bool checked)
{
	if (state->type == PARTS_CG) {
		recover_cg_state(&state->cg, p->textures_suspended);
		if (!p->textures_suspended && !state->common.texture.handle &&
		    state->common.texture.w > 0 && !state->cg.no && !state->cg.name)
			parts_widget_restore_plain_skin(p, state, index, checked);
	} else if (state->type == PARTS_FLAT) {
		parts_flat_recover_widget_skin(&state->flat, p->textures_suspended);
	}
}

static bool recover_text_state(struct parts_text *text)
{
	if (text->common.texture.handle)
		gfx_remap_texture(&text->common.texture);
	bool missing_texture = false;
	for (unsigned li = 0; li < text->nr_lines; li++) {
		struct parts_text_line *line = &text->lines[li];
		for (int ci = 0; ci < line->nr_chars; ci++) {
			struct parts_text_char *ch = &line->chars[ci];
			if (ch->t.handle && !gfx_remap_texture(&ch->t))
				missing_texture = true;
			if (ch->t_edge.handle && !gfx_remap_texture(&ch->t_edge))
				missing_texture = true;
		}
	}
	return missing_texture;
}

static void recover_widget_states(struct parts *p)
{
	struct parts_widget *w = p->widget;
	if (!w)
		return;
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		recover_widget_skin(p, &p->states[i], i, false);
		recover_widget_skin(p, &w->checked_states[i], i, true);
	}
	recover_widget_skin(p, &w->disabled, PARTS_NR_STATES, false);
	recover_widget_skin(p, &w->checked_disabled, PARTS_NR_STATES, true);
	if (recover_text_state(&w->label.text) && !p->textures_suspended)
		parts_widget_rebuild_label(p);
}

static void recover_parts_state(struct parts *p, struct parts_state *s)
{
	switch (s->type) {
	case PARTS_CG:
		recover_cg_state(&s->cg, p->textures_suspended);
		break;
	case PARTS_TEXT:
		if (recover_text_state(&s->text) && !p->textures_suspended)
			parts_text_rerender_all(p, &s->text);
		break;
	case PARTS_ANIMATION: {
		bool missing_frame = false;
		if (s->common.texture.handle)
			gfx_remap_texture(&s->common.texture);
		for (unsigned fi = 0; s->anim.frames && fi < s->anim.nr_frames; fi++) {
			if (s->anim.frames[fi].handle) {
				if (!gfx_remap_texture(&s->anim.frames[fi]))
					missing_frame = true;
			} else if (s->anim.frames[fi].w > 0) {
				missing_frame = true;
			}
		}
		if (missing_frame && !p->textures_suspended && s->anim.nr_frames > 0) {
			unsigned saved_elapsed = s->anim.elapsed;
			unsigned saved_frame = s->anim.current_frame;
			bool ok = false;
			if (s->anim.cg_name && s->anim.cg_name->size > 0) {
				struct string *name_copy = string_dup(s->anim.cg_name);
				ok = parts_animation_set_cg(p, &s->anim, name_copy,
						(int)s->anim.start_no, (int)s->anim.nr_frames, (int)s->anim.frame_time);
				free_string(name_copy);
			} else if (s->anim.start_no > 0) {
				ok = parts_animation_set_cg_by_index(p, &s->anim,
						(int)s->anim.start_no, (int)s->anim.nr_frames, (int)s->anim.frame_time);
			}
			if (ok && s->anim.nr_frames > 0) {
				s->anim.elapsed = saved_elapsed;
				s->anim.current_frame = saved_frame < s->anim.nr_frames ? saved_frame : 0;
			}
		}
		if (s->anim.frames && s->anim.nr_frames > 0) {
			if (s->anim.current_frame >= s->anim.nr_frames)
				s->anim.current_frame = 0;
			s->common.texture = s->anim.frames[s->anim.current_frame];
		}
		break;
	}
	case PARTS_NUMERAL:
		if (s->common.texture.handle)
			gfx_remap_texture(&s->common.texture);
		if (!s->common.texture.handle && s->num.have_num && !p->textures_suspended)
			parts_numeral_update(p, &s->num);
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE: {
		bool cg_ok = !s->gauge.cg.handle || gfx_remap_texture(&s->gauge.cg);
		if (s->common.texture.handle)
			gfx_remap_texture(&s->common.texture);
		if (!p->textures_suspended) {
			bool need_cg = !s->gauge.cg.handle &&
				((s->gauge.cg_name && s->gauge.cg_name->size > 0) || s->gauge.cg_no >= 0 || !cg_ok);
			if (need_cg) {
				float saved_rate = s->gauge.rate;
				bool loaded = false;
				if (s->gauge.cg_name && s->gauge.cg_name->size > 0) {
					struct string *name_copy = string_dup(s->gauge.cg_name);
					loaded = parts_gauge_set_cg(p, &s->gauge, name_copy);
					free_string(name_copy);
				}
				if (!loaded && s->gauge.cg_no >= 0)
					loaded = parts_gauge_set_cg_by_index(p, &s->gauge, s->gauge.cg_no);
				if (loaded) {
					if (s->type == PARTS_VGAUGE)
						parts_vgauge_set_rate(p, &s->gauge, saved_rate);
					else
						parts_hgauge_set_rate(p, &s->gauge, saved_rate);
				}
			} else if (!s->common.texture.handle && s->gauge.cg.handle) {
				gfx_init_texture_rgba(&s->common.texture, s->gauge.cg.w, s->gauge.cg.h,
						(SDL_Color){0, 0, 0, 255});
				if (s->type == PARTS_VGAUGE)
					parts_vgauge_set_rate(p, &s->gauge, s->gauge.rate);
				else
					parts_hgauge_set_rate(p, &s->gauge, s->gauge.rate);
			}
		}
		break;
	}
	case PARTS_CONSTRUCTION_PROCESS:
		if (s->common.texture.handle)
			gfx_remap_texture(&s->common.texture);
		if (!s->common.texture.handle && !p->textures_suspended &&
		    !TAILQ_EMPTY(&s->cproc.ops)) {
			s->cproc.needs_build = true;
		}
		break;
	case PARTS_FLASH:
		if (s->common.texture.handle)
			gfx_remap_texture(&s->common.texture);
		if (s->flash.bitmaps)
			ht_foreach_value(s->flash.bitmaps, remap_flash_bitmap);
		break;
	case PARTS_FLAT:
		parts_flat_recover_widget_skin(&s->flat, p->textures_suspended);
		break;
	default:
		if (s->common.texture.handle)
			gfx_remap_texture(&s->common.texture);
		break;
	}
}

static void recover_parts_in_current_context(void)
{
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		bool needs_reinit = false;
		for (int j = 0; j < 12; j++) {
			if (font->cg[j].handle) {
				if (!gfx_remap_texture(&font->cg[j]))
					needs_reinit = true;
			} else if (font->cg[j].w > 0) {
				needs_reinit = true;
			}
		}
		if (needs_reinit)
			parts_numeral_font_init(font);
	}
	if (!parts_table)
		return;
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		for (int st = 0; st < PARTS_NR_STATES; st++) {
			struct parts_state *s = &p->states[st];
			if (p->widget && (s->type == PARTS_CG || s->type == PARTS_FLAT))
				continue;
			recover_parts_state(p, s);
		}
		recover_widget_states(p);
		if (p->panel && !p->textures_suspended && !p->states[0].common.texture.handle &&
		    p->panel->width > 0 && p->panel->height > 0) {
			parts_panel_rebuild(p);
		}
		if (p->scrollbar) {
			struct parts_scrollbar *sb = p->scrollbar;
			bool needs_rebuild = false;
			for (int piece = 0; piece < 4; piece++) {
				for (int st = 0; st < 4; st++) {
					struct parts_state *sk = &sb->skins[piece][st];
					if (sk->type == PARTS_CG) {
						recover_cg_state(&sk->cg, p->textures_suspended);
						if (!sk->common.texture.handle && sk->common.w > 0)
							needs_rebuild = true;
					} else if (sk->type == PARTS_FLAT) {
						parts_flat_recover_widget_skin(&sk->flat, p->textures_suspended);
						for (size_t li = 0; sk->flat.textures && li < sk->flat.nr_libraries; li++) {
							if (!sk->flat.textures[li].handle && sk->flat.textures[li].w > 0)
								needs_rebuild = true;
						}
					} else if (sk->common.texture.handle) {
						if (!gfx_remap_texture(&sk->common.texture))
							needs_rebuild = true;
					}
				}
			}
			if (needs_rebuild && !p->textures_suspended)
				parts_scrollbar_rebuild(p);
		}
		if (p->message_box) {
			struct parts_message_window *m = p->message_box;
			if (recover_text_state(&m->label.text) && !p->textures_suspended) {
				int saved_elapsed = m->elapsed;
				bool saved_fixed = m->fixed;
				parts_message_window_rebuild_text(p);
				m->elapsed = saved_elapsed;
				m->fixed = saved_fixed;
			}
			recover_parts_state(p, &m->key_wait);
		}
		if (p->text_box_label && p->text_box_label->type == PARTS_TEXT) {
			if (recover_text_state(&p->text_box_label->text) && !p->textures_suspended)
				parts_text_rerender_all(p, &p->text_box_label->text);
		}
		parts_component_dirty(p);
		parts_dirty(p);
	}
	parts_engine_dirty();
}

static void parts_cg_asset_on_gl_reset(void)
{
	parts_cg_cache_sync_archive_gen();
	struct parts_cg_asset *asset = TAILQ_FIRST(&cg_asset_all);
	while (asset) {
		struct parts_cg_asset *next = TAILQ_NEXT(asset, all_entry);
		if (asset->texture.handle && gfx_remap_texture(&asset->texture) && asset->texture.handle) {
			asset = next;
			continue;
		}
		if (asset->refcount == 0) {
			parts_cg_asset_destroy(asset);
			asset = next;
			continue;
		}
		struct cg *cg = NULL;
		int no = asset->cg_no;
		if (asset->name && asset->name[0])
			cg = asset_cg_load_by_name(asset->name, &no);
		if (!cg && asset->cg_no > 0)
			cg = asset_cg_load(asset->cg_no);
		if (cg) {
			gfx_init_texture_with_cg(&asset->texture, cg);
			cg_free(cg);
		} else {
			asset->texture = (Texture){0};
		}
		asset = next;
	}
}

static void parts_cg_sync_state_after_gl_reset(struct parts_state *s, bool suspended)
{
	if (s->type != PARTS_CG || !s->cg.cg_asset)
		return;
	if (!suspended && s->cg.cg_asset->texture.handle) {
		s->common.texture = s->cg.cg_asset->texture;
	} else {
		parts_cg_asset_unref(s->cg.cg_asset);
		s->cg.cg_asset = NULL;
		s->common.texture = (Texture){0};
	}
}

static void parts_cg_sync_context_after_gl_reset(void)
{
	if (!parts_table)
		return;
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		for (int st = 0; st < PARTS_NR_STATES; st++)
			parts_cg_sync_state_after_gl_reset(&p->states[st], p->textures_suspended);
		if (p->widget) {
			for (int i = 0; i < PARTS_NR_STATES; i++)
				parts_cg_sync_state_after_gl_reset(&p->widget->checked_states[i], p->textures_suspended);
			parts_cg_sync_state_after_gl_reset(&p->widget->disabled, p->textures_suspended);
			parts_cg_sync_state_after_gl_reset(&p->widget->checked_disabled, p->textures_suspended);
		}
		if (p->scrollbar) {
			for (int piece = 0; piece < 4; piece++) {
				for (int st = 0; st < 4; st++)
					parts_cg_sync_state_after_gl_reset(&p->scrollbar->skins[piece][st], p->textures_suspended);
			}
		}
		if (p->message_box)
			parts_cg_sync_state_after_gl_reset(&p->message_box->key_wait, p->textures_suspended);
	}
}

static void parts_on_gl_reset(void *user)
{
	(void)user;
	if (!parts_engine_initialized)
		return;
	parts_cg_asset_on_gl_reset();
	parts_cg_sync_context_after_gl_reset();
	recover_parts_in_current_context();
	struct parts_context *bs = parts_get_back_scene();
	if (bs && bs != parts_current) {
		struct parts_context *prev = parts_context_select(bs);
		parts_cg_sync_context_after_gl_reset();
		recover_parts_in_current_context();
		parts_context_select(prev);
	}
	scene_dirty();
}

static void refresh_scrollbar_in_current_context(void)
{
	if (!parts_engine_initialized || !parts_table)
		return;
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		if (p->scrollbar) {
			parts_recalculate_hitbox(p);
			parts_dirty(p);
		}
	}
	parts_render_gen++;
	parts_engine_dirty();
}

void parts_refresh_live_scrollbar_scale(void)
{
	if (!parts_engine_initialized)
		return;
	refresh_scrollbar_in_current_context();
	struct parts_context *bs = parts_get_back_scene();
	if (bs && bs != parts_current) {
		struct parts_context *prev = parts_context_select(bs);
		refresh_scrollbar_in_current_context();
		parts_context_select(prev);
	}
	scene_dirty();
}
