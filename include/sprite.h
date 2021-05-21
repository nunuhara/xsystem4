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

#ifndef SYSTEM4_SPRITE_H
#define SYSTEM4_SPRITE_H

#include <stdbool.h>
#include <SDL.h>
#include "queue.h"
#include "gfx/gfx.h"

struct string;
struct page;
union vm_value;

struct sact_sprite {
	TAILQ_ENTRY(sact_sprite) entry;
	// The sprite's texture (CG or solid color). Initialized lazily.
	struct texture texture;
	// If no CG is attached to the sprite, the solid color to fill with.
	SDL_Color color;
	// The position and dimensions of the sprite.
	Rectangle rect;
	// Arbitrary text can be attached to the sprite. This is rendered on a
	// separate texture and overlayed.
	struct {
		struct string *str;
		struct texture texture;
		Point home;
		Point pos;
		int char_space;
		int line_space;
	} text;
	// The Z-layer of the sprite within the scene.
	int z;
	// The secondary Z-layer (for GoatGUIEngine)
	int z2;
	// This flag indicates that pixel data has been attached to a sprite; it
	// does NOT guarantee that the texture is initialized.
	bool has_pixel;
	bool has_alpha;
	// This flag indicates whether or not the sprite is included in the
	// the current scene. A sprite should be in the scene if there is pixel
	// or text data attached to it and it is not hidden.
	bool in_scene;
	// When a sprite is hidden, it is removed from the scene. Attaching new
	// pixel data does not alter the state of this flag.
	bool hidden;
	// The sprite handle
	int no;
	// The CG number attached to the sprite.
	int cg_no;
	// The rendering function
	void (*render)(struct sact_sprite*);
};

extern bool sact_dirty;

void sprite_register(struct sact_sprite *sp);
void sprite_unregister(struct sact_sprite *sp);

static inline void sprite_dirty(struct sact_sprite *sp)
{
	if (!sp)
		return;
	sact_dirty = true;
	if (sp->hidden) {
		sprite_unregister(sp);
	} else if (sp->has_pixel || sp->text.texture.handle) {
		sprite_register(sp);
	}
}

static inline void scene_dirty(void)
{
	sact_dirty = true;
}

void sprite_free(struct sact_sprite *sp);
void sprite_render_scene(void);
void sprite_flip(void);
struct texture *sprite_get_texture(struct sact_sprite *sp);
int sprite_set_wp(int cg_no);
int sprite_set_wp_color(int r, int g, int b);
int sprite_set_cg(struct sact_sprite *sp, int cg_no);
int sprite_set_cg_from_file(struct sact_sprite *sp, const char *path);
void sprite_init(struct sact_sprite *sp, int w, int h, int r, int g, int b, int a);
int sprite_get_max_z(void);
void sprite_set_pos(struct sact_sprite *sp, int x, int y);
void sprite_set_x(struct sact_sprite *sp, int x);
void sprite_set_y(struct sact_sprite *sp, int y);
void sprite_set_z(struct sact_sprite *sp, int z);
void sprite_set_z2(struct sact_sprite *sp, int z, int z2);
int sprite_get_blend_rate(struct sact_sprite *sp);
void sprite_set_blend_rate(struct sact_sprite *sp, int rate);
void sprite_set_show(struct sact_sprite *sp, bool show);
int sprite_set_draw_method(struct sact_sprite *sp, int method);
int sprite_get_draw_method(struct sact_sprite *sp);
int sprite_exists_alpha(struct sact_sprite *sp);
static inline int sprite_get_pos_x(struct sact_sprite *sp) { return sp->rect.x; }
static inline int sprite_get_pos_y(struct sact_sprite *sp) { return sp->rect.y; }
static inline int sprite_get_width(struct sact_sprite *sp) { return sp->rect.w; }
static inline int sprite_get_height(struct sact_sprite *sp) { return sp->rect.h; }
static inline int sprite_get_z(struct sact_sprite *sp) { return sp->z; }
static inline int sprite_get_z2(struct sact_sprite *sp) { return sp->z2; }
static inline int sprite_get_show(struct sact_sprite *sp) { return !sp->hidden; }
void sprite_set_text_home(struct sact_sprite *sp, int x, int y);
void sprite_set_text_line_space(struct sact_sprite *sp, int px);
void sprite_set_text_char_space(struct sact_sprite *sp, int px);
void sprite_set_text_pos(struct sact_sprite *sp, int x, int y);
void sprite_text_draw(struct sact_sprite *sp, struct string *text, struct text_metrics *tm);
void sprite_text_clear(struct sact_sprite *sp);
void sprite_text_home(struct sact_sprite *sp, int size);
void sprite_text_new_line(struct sact_sprite *sp, int size);
void sprite_text_copy(struct sact_sprite *dsp, struct sact_sprite *ssp);
static inline int sprite_get_text_home_x(struct sact_sprite *sp) { return sp->text.home.x; }
static inline int sprite_get_text_home_y(struct sact_sprite *sp) { return sp->text.home.y; }
static inline int sprite_get_text_char_space(struct sact_sprite *sp) { return sp->text.char_space; }
static inline int sprite_get_text_pos_x(struct sact_sprite *sp) { return sp->text.pos.x; }
static inline int sprite_get_text_pos_y(struct sact_sprite *sp) { return sp->text.pos.y; }
static inline int sprite_get_text_line_space(struct sact_sprite *sp) { return sp->text.line_space; }
bool sprite_is_point_in(struct sact_sprite *sp, int x, int y);
bool sprite_is_point_in_rect(struct sact_sprite *sp, int x, int y);
int sprite_get_amap_value(struct sact_sprite *sp, int x, int y);
void sprite_get_pixel_value(struct sact_sprite *sp, int x, int y, int *r, int *g, int *b);

#endif /* SYSTEM4_SPRITE_H */
