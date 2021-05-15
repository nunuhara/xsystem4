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

#include <math.h>
#include <stdlib.h>
#include "system4.h"

#include "dungeon/dgn.h"
#include "dungeon/dungeon.h"
#include "dungeon/map.h"
#include "gfx/gfx.h"
#include "sact.h"
#include "vm.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

#define CS 12  // pixel size of a map cell

enum symbol_type {
	SYMBOL_PLAYER_NORTH = 0,
	SYMBOL_PLAYER_SOUTH = 1,
	SYMBOL_PLAYER_EAST = 2,
	SYMBOL_PLAYER_WEST = 3,
	SYMBOL_CROSS = 4,
	SYMBOL_EXIT = 5,
	SYMBOL_STAIRS_UP = 6,
	SYMBOL_STAIRS_DOWN = 7,
	SYMBOL_EVENT = 8,
	SYMBOL_HEART = 9,
	SYMBOL_WARP = 10,
	SYMBOL_TREASURE = 11,
	SYMBOL_MONSTER = 12,
	SYMBOL_YELLOW_STAR = 13,
	NR_SYMBOLS = 14
};

struct dungeon_map {
	struct texture *textures;
	int nr_textures;
	int symbol_sprites[NR_SYMBOLS];
	int small_map_floor;
	int large_map_floor;
	bool all_visible;
};

struct dungeon_map *dungeon_map_create(void)
{
	struct dungeon_map *map = xcalloc(1, sizeof(struct dungeon_map));
	map->small_map_floor = -1;
	map->large_map_floor = -1;
	return map;
}

void dungeon_map_free(struct dungeon_map *map)
{
	for (int i = 0; i < map->nr_textures; i++)
		gfx_delete_texture(&map->textures[i]);
	free(map->textures);
	free(map);
}

void dungeon_map_init(struct dungeon_context *ctx)
{
	struct dungeon_map *map = ctx->map;
	struct dgn *dgn = ctx->dgn;

	map->textures = xcalloc(dgn->size_y, sizeof(struct texture));
	map->nr_textures = dgn->size_y;
	for (int y = 0; y < dgn->size_y; y++) {
		gfx_init_texture_rgba(&map->textures[y], dgn->size_x * CS, dgn->size_z * CS, (SDL_Color){0, 0, 0, 0});
		for (int z = 0; z < dgn->size_z; z++) {
			for (int x = 0; x < dgn->size_x; x++) {
				dungeon_map_update_cell(ctx, x, y, z);
			}
		}
	}
}

static void reveal_cell(struct dungeon_context *ctx, int dgn_x, int dgn_y, int dgn_z)
{
	struct texture *dst = &ctx->map->textures[dgn_y];
	int x = dgn_x * CS;
	int y = (ctx->dgn->size_z - dgn_z - 1) * CS;
	gfx_fill_amap(dst, x, y, CS, CS, 255);
}

static void draw_hline(struct texture *dst, int x, int y, int w, SDL_Color col)
{
	gfx_fill(dst, x, y, w, 1, col.r, col.g, col.b);
}

static void draw_vline(struct texture *dst, int x, int y, int h, SDL_Color col)
{
	gfx_fill(dst, x, y, 1, h, col.r, col.g, col.b);
}

static void draw_symbol(struct dungeon_context *ctx, struct texture *dst, int x, int y, int type)
{
	struct sact_sprite *sp = sact_get_sprite(ctx->map->symbol_sprites[type]);
	if (!sp || sp->rect.w != CS - 2 || sp->rect.h != CS - 2)
		VM_ERROR("Invalid map cg %d", ctx->map->symbol_sprites[type]);
	struct texture *src = sprite_get_texture(sp);
	gfx_copy(dst, x, y, src, 0, 0, src->w, src->h);
}

void dungeon_map_update_cell(struct dungeon_context *ctx, int dgn_x, int dgn_y, int dgn_z)
{
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, dgn_x, dgn_y, dgn_z);
	struct texture *dst = &ctx->map->textures[dgn_y];
	int x = dgn_x * CS;
	int y = (ctx->dgn->size_z - dgn_z - 1) * CS;

	if (cell->floor >= 0)
		gfx_fill(dst, x, y, CS, CS, 0, 0, 0);
	else
		gfx_fill(dst, x, y, CS, CS, 182, 72, 0);

	const SDL_Color wall_colors[2] = {{255, 255, 255}, {255, 0, 0}};
	if (cell->north_wall >= 0)
		draw_hline(dst, x, y, CS, wall_colors[!!cell->enterable_north]);
	if (cell->south_wall >= 0)
		draw_hline(dst, x, y + CS - 1, CS, wall_colors[!!cell->enterable_south]);
	if (cell->west_wall >= 0)
		draw_vline(dst, x, y, CS, wall_colors[!!cell->enterable_west]);
	if (cell->east_wall >= 0)
		draw_vline(dst, x + CS - 1, y, CS, wall_colors[!!cell->enterable_east]);

	const SDL_Color door_color = {255, 0, 0, 255};
	if (cell->north_door >= 0) {
		draw_hline(dst, x, y, CS / 2 - 1, door_color);
		draw_hline(dst, x + CS / 2 + 1, y, CS / 2 - 1, door_color);
	}
	if (cell->south_door >= 0) {
		draw_hline(dst, x, y + CS - 1, CS / 2 - 1, door_color);
		draw_hline(dst, x + CS / 2 + 1, y + CS - 1, CS / 2 - 1, door_color);
	}
	if (cell->west_door >= 0) {
		draw_vline(dst, x, y, CS / 2 - 1, door_color);
		draw_vline(dst, x, y + CS / 2 + 1, CS / 2 - 1, door_color);
	}
	if (cell->east_door >= 0) {
		draw_vline(dst, x + CS - 1, y, CS / 2 - 1, door_color);
		draw_vline(dst, x + CS - 1, y + CS / 2 + 1, CS / 2 - 1, door_color);
	}

	const SDL_Color event_wall_color = {64, 255, 128, 255};
	if (cell->north_event)
		draw_hline(dst, x, y, CS, event_wall_color);
	if (cell->south_event)
		draw_hline(dst, x, y + CS - 1, CS, event_wall_color);
	if (cell->west_event)
		draw_vline(dst, x, y, CS, event_wall_color);
	if (cell->east_event)
		draw_vline(dst, x + CS - 1, y, CS, event_wall_color);

	int symbol = -1;
	switch (cell->floor_event) {
	case 11: symbol = SYMBOL_TREASURE; break;
	case 12: symbol = SYMBOL_MONSTER; break;
	case 14: symbol = SYMBOL_EVENT; break;
	case 15: symbol = SYMBOL_HEART; break;
	case 16: symbol = SYMBOL_CROSS; break;
	case 17: symbol = SYMBOL_EXIT; break;
	case 18: symbol = SYMBOL_WARP; break;
	case 28: symbol = SYMBOL_YELLOW_STAR; break;
	}
	if (cell->stairs_orientation >= 0)
		symbol = SYMBOL_STAIRS_UP;
	else if (dgn_y && dgn_cell_at(ctx->dgn, dgn_x, dgn_y - 1, dgn_z)->stairs_orientation >= 0)
		symbol = SYMBOL_STAIRS_DOWN;

	if (symbol >= 0)
		draw_symbol(ctx, dst, x + 1, y + 1, symbol);
}

static int player_symbol(struct dungeon_context *ctx)
{
	const int symbols[] = {
		SYMBOL_PLAYER_NORTH,
		SYMBOL_PLAYER_EAST,
		SYMBOL_PLAYER_SOUTH,
		SYMBOL_PLAYER_WEST,
	};
	return symbols[(int)(round(ctx->camera.angle * 2 / M_PI)) & 3];
}

void dungeon_map_draw(int surface, int sprite)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return;
	struct sact_sprite *sp = sact_get_sprite(sprite);
	if (!sp)
		VM_ERROR("DrawDungeon.DrawMap: invalid sprite %d", sprite);
	sprite_dirty(sp);
	struct texture *dst = sprite_get_texture(sp);

	int player_x = round(ctx->camera.pos[0] / 2.0);
	int player_y = round(ctx->camera.pos[1] / 2.0);
	int player_z = round(ctx->camera.pos[2] / -2.0);
	int level = ctx->map->small_map_floor;
	if (level < 0)
		level = player_y;

	struct texture *src = &ctx->map->textures[level];

	int x = player_x * CS - (dst->w - CS) / 2;
	int y = (ctx->dgn->size_z - player_z - 1) * CS - (dst->h - CS) / 2;

	gfx_copy_with_alpha_map(dst, 0, 0, src, x, y, dst->w, dst->h);

	draw_symbol(ctx, dst, (dst->w - CS + 2) / 2, (dst->h - CS + 2) / 2, player_symbol(ctx));

	if (ctx->map->all_visible)
		gfx_fill_amap(dst, 0, 0, dst->w, dst->h, 255);
}

void dungeon_map_draw_lmap(int surface, int sprite)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return;
	struct sact_sprite *sp = sact_get_sprite(sprite);
	if (!sp)
		VM_ERROR("DrawDungeon.DrawMap: invalid sprite %d", sprite);
	sprite_dirty(sp);
	struct texture *dst = sprite_get_texture(sp);

	int player_x = round(ctx->camera.pos[0] / 2.0);
	int player_y = round(ctx->camera.pos[1] / 2.0);
	int player_z = round(ctx->camera.pos[2] / -2.0);
	int level = ctx->map->large_map_floor;
	if (level < 0)
		level = player_y;

	struct texture *src = &ctx->map->textures[level];
	gfx_copy_with_alpha_map(dst, 0, 0, src, 0, 0, src->w, src->h);

	if (ctx->map->all_visible)
		gfx_fill_amap(dst, 0, 0, dst->w, dst->h, 255);

	draw_symbol(ctx, dst, player_x * CS + 1, (ctx->dgn->size_z - player_z - 1) * CS + 1, player_symbol(ctx));
}

void dungeon_map_set_all_view(int surface, int flag)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return;
	ctx->map->all_visible = flag;
}

void dungeon_map_set_cg(int surface, int index, int sprite)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return;
	if (index < 0 || index >= NR_SYMBOLS)
		VM_ERROR("DrawDungeon.SetMapCG: index %d is out of bounds", index);
	ctx->map->symbol_sprites[index] = sprite;
}

void dungeon_map_set_small_map_floor(int surface, int floor)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return;
	ctx->map->small_map_floor = floor;
}

void dungeon_map_set_large_map_floor(int surface, int floor)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return;
	ctx->map->large_map_floor = floor;
}

void dungeon_map_set_walked(int surface, int x, int y, int z, int flag)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return;
	reveal_cell(ctx, x, y, z);
	if (x >= 0 && dgn_cell_at(ctx->dgn, x - 1, y, z)->floor < 0)
		reveal_cell(ctx, x - 1, y, z);
	if (x + 1 < ctx->dgn->size_x && dgn_cell_at(ctx->dgn, x + 1, y, z)->floor < 0)
		reveal_cell(ctx, x + 1, y, z);
	if (z >= 0 && dgn_cell_at(ctx->dgn, x, y, z - 1)->floor < 0)
		reveal_cell(ctx, x, y, z - 1);
	if (z + 1 < ctx->dgn->size_z && dgn_cell_at(ctx->dgn, x, y, z + 1)->floor < 0)
		reveal_cell(ctx, x, y, z + 1);
}
