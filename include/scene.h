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

#ifndef SYSTEM4_SCENE_H
#define SYSTEM4_SCENE_H

#include <stdbool.h>
#include "queue.h"

typedef struct cJSON cJSON;
struct texture;

struct sprite {
	TAILQ_ENTRY(sprite) entry;
	// The Z-layer of the sprite within the scene
	int z;
	// The secondary Z-layer (for GoatGUIEngine)
	int z2;
	// If a sprite does not have pixel data, it will not be added to the scene.
	bool has_pixel;
	bool has_alpha;
	// When a sprite is hidden, it is removed from the scene.
	bool hidden;
	// This flag indicates whether or not the sprite is included in the
	// the current scene. A sprite should be in the scene if there is pixel
	// or text data attached to it and it is not hidden.
	bool in_scene;
	// Unique ID
	int id;
	// The rendering function.
	void (*render)(struct sprite*);
	// Debug printing function
	cJSON *(*to_json)(struct sprite*, bool);
};

extern bool scene_is_dirty;

void scene_register_sprite(struct sprite *sp);
void scene_unregister_sprite(struct sprite *sp);
void scene_render(void);
int scene_set_wp(int cg_no);
int scene_set_wp_color(int r, int g, int b);
int scene_set_wp_texture(struct texture *tex);
void scene_set_sprite_z(struct sprite *sp, int z);
void scene_set_sprite_z2(struct sprite *sp, int z, int z2);

void scene_print(void);
cJSON *scene_to_json(bool verbose);
struct sprite *scene_get(int id);

static inline void scene_sprite_dirty(struct sprite *sp)
{
	if (!sp)
		return;
	scene_is_dirty = true;
	if (sp->hidden) {
		scene_unregister_sprite(sp);
	} else if (sp->has_pixel) {
		scene_register_sprite(sp);
	}
}

static inline void scene_dirty(void)
{
	scene_is_dirty = true;
}

static inline void scene_set_sprite_show(struct sprite *sp, bool show)
{
	sp->hidden = !show;
	scene_sprite_dirty(sp);
}

static inline bool scene_get_sprite_show(struct sprite *sp)
{
	return !sp->hidden;
}

#endif /* SYSTEM4_SCENE_H */
