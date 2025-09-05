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

#ifndef SYSTEM4_DUNGEON_H
#define SYSTEM4_DUNGEON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <cglm/cglm.h>
#include "gfx/gfx.h"
#include "plugin.h"

#define DRAWFIELD_NR_CHARACTERS 256

struct page;
struct dgn;
struct dtx;
struct tes;
struct dungeon_renderer;
struct dungeon_map;
struct drawfield_character;

enum draw_dungeon_version {
	DRAW_DUNGEON_1,    // Rance VI
	DRAW_DUNGEON_2,    // Dungeons & Dolls
	DRAW_DUNGEON_14,   // GALZOO Island
	DRAW_FIELD         // Pastel Chime Continue
};

struct camera {
	float pos[3];
	float angle;
	float angle_p;
};

/*
 * Dungeon rendering context.
 *
 * A dungeon cell at (x, y, z) is rendered in a 2x2x2 cube centered at
 * (2*x, 2*y, -2*z) in the world coordinates. Note that Z is flipped, because
 * OpenGL uses a right-handed coordinate system while dungeon data is left-
 * handed.
 */

struct dungeon_context {
	struct draw_plugin plugin;
	enum draw_dungeon_version version;
	int surface;
	bool loaded;
	bool draw_enabled;
	struct dungeon_map *map;
	struct camera camera;
	struct dgn *dgn;
	struct dtx *dtx;
	struct tes *tes;
	ivec3 player_pos;
	struct drawfield_character *characters;
	mat4 proj_transform;
	struct dungeon_renderer *renderer;
	struct texture texture;
	GLuint depth_buffer;
};

struct dungeon_context *dungeon_context_create(enum draw_dungeon_version version, int width, int height);
bool dungeon_load(struct dungeon_context *ctx, int num);
bool dungeon_load_dungeon(struct dungeon_context *ctx, const char *filename, int num);
bool dungeon_load_texture(struct dungeon_context *ctx, const char *filename);
void dungeon_set_camera(int surface, float x, float y, float z, float angle, float angle_p);
void dungeon_set_perspective(int surface, int width, int height, float near, float far, float deg);
void dungeon_set_player_pos(int surface, int x, int y, int z);
void dungeon_set_walked(int surface, int x, int y, int z, int flag);
int dungeon_get_walked(int surface, int x, int y, int z);
void dungeon_set_walked_all(int surface);
int dungeon_calc_conquer(int surface);
bool dungeon_load_walk_data(int surface, int map, struct page **page);
bool dungeon_save_walk_data(int surface, int map, struct page **page);
void dungeon_paint_step(int surface, int x, int y, int z);
void dungeon_set_chara_sprite(int surface, int num, int sprite);
void dungeon_set_chara_pos(int surface, int num, float x, float y, float z);
void dungeon_set_chara_cg(int surface, int num, int cg);
void dungeon_set_chara_cg_info(int surface, int num, int num_chara_x, int num_chara_y);
void dungeon_set_chara_show(int surface, int num, bool show);
bool dungeon_project_world_to_screen(struct dungeon_context *ctx, vec3 world_pos, Point *screen_pos);

struct dungeon_context *dungeon_get_context(int surface);

#endif /* SYSTEM4_DUNGEON_H */
