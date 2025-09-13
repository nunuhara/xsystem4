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

static const SDL_Color unenterable_cell_color = {182, 72, 0, 0};

enum dd1_symbol_type {
	DD1_SYMBOL_PLAYER_NORTH = 0,
	DD1_SYMBOL_PLAYER_SOUTH = 1,
	DD1_SYMBOL_PLAYER_EAST = 2,
	DD1_SYMBOL_PLAYER_WEST = 3,
	DD1_SYMBOL_CROSS = 4,
	DD1_SYMBOL_EXIT = 5,
	DD1_SYMBOL_STAIRS_UP = 6,
	DD1_SYMBOL_STAIRS_DOWN = 7,
	DD1_SYMBOL_EVENT = 8,
	DD1_SYMBOL_HEART = 9,
	DD1_SYMBOL_WARP = 10,
	DD1_SYMBOL_TREASURE = 11,
	DD1_SYMBOL_MONSTER = 12,
	DD1_SYMBOL_YELLOW_STAR = 13,
	NR_SYMBOLS = 14
};

enum dd2_symbol_type {
	DD2_SYMBOL_PLAYER_NORTH = 0,
	DD2_SYMBOL_PLAYER_EAST = 1,
	DD2_SYMBOL_PLAYER_WEST = 2,
	DD2_SYMBOL_PLAYER_SOUTH = 3,
	DD2_SYMBOL_START = 4,
	DD2_SYMBOL_EXIT = 5,
};

struct dungeon_map {
	struct texture *textures;
	int nr_textures;
	int symbol_sprites[NR_SYMBOLS];
	int grid_size;
	int small_map_floor;
	int large_map_floor;
	bool radar;
	bool all_visible;
	int offset_x, offset_y;
};

static Point texture_pos(struct dungeon_context *ctx, int x, int z)
{
	return (Point){
		.x = (ctx->map->offset_x + x) * ctx->map->grid_size,
		.y = (ctx->map->offset_y + ctx->dgn->size_z - z - 1) * ctx->map->grid_size,
	};
}

struct dungeon_map *dungeon_map_create(enum draw_dungeon_version version)
{
	if (version == DRAW_DUNGEON_14)
		return NULL;  // GALZOO does not use DrawDungeon's 2D map functions.

	struct dungeon_map *map = xcalloc(1, sizeof(struct dungeon_map));
	switch (version) {
	case DRAW_DUNGEON_1:
	case DRAW_FIELD:
		map->grid_size = 12;
		break;
	case DRAW_DUNGEON_2:
		map->grid_size = 11;
		break;
	case DRAW_DUNGEON_14:
		ERROR("cannot happen");
		break;
	}
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
	if (!map)
		return;

	if (map->textures) {
		for (int i = 0; i < map->nr_textures; i++)
			gfx_delete_texture(&map->textures[i]);
		free(map->textures);
	}
	map->textures = xcalloc(dgn->size_y, sizeof(struct texture));
	map->nr_textures = dgn->size_y;

	int map_width;
	int map_height;
	switch (ctx->version) {
	case DRAW_DUNGEON_1: {
			// Calculate the bounding box on the XZ plane so that map is drawn in the
			// center of the texture.
			uint32_t min_x = dgn->size_x;
			uint32_t min_y = dgn->size_z;
			uint32_t max_x = 0;
			uint32_t max_y = 0;
			for (uint32_t y = 0; y < dgn->size_y; y++) {
				for (uint32_t z = 0; z < dgn->size_z; z++) {
					uint32_t map_y = ctx->dgn->size_z - z - 1;
					for (uint32_t x = 0; x < dgn->size_x; x++) {
						if (dgn_cell_at(ctx->dgn, x, y, z)->floor < 0)
							continue;
						min_x = min(min_x, x);
						min_y = min(min_y, map_y);
						max_x = max(max_x, x);
						max_y = max(max_y, map_y);
					}
				}
			}
			map_width = 32;
			map_height = 32;
			map->offset_x = (map_width - (max_x - min_x + 1)) / 2 - min_x;
			map->offset_y = (map_height - (max_y - min_y)) / 2 - min_y;
		}
		break;
	case DRAW_DUNGEON_2:
		map_width = 19;
		map_height = 19;
		map->offset_x = 0;
		map->offset_y = 1;
		break;
	case DRAW_DUNGEON_14:
		ERROR("cannot happen");
		break;
	case DRAW_FIELD:
		map_width = dgn->size_x;
		map_height = dgn->size_z;
		map->offset_x = 0;
		map->offset_y = 0;
		break;
	}
	// Initial draw
	for (uint32_t y = 0; y < dgn->size_y; y++) {
		gfx_init_texture_rgba(&map->textures[y],
			map_width * map->grid_size,
			map_height * map->grid_size,
			unenterable_cell_color);
		for (uint32_t z = 0; z < dgn->size_z; z++) {
			for (uint32_t x = 0; x < dgn->size_x; x++) {
				dungeon_map_update_cell(ctx, x, y, z);
			}
		}
	}
}

void dungeon_map_reveal(struct dungeon_context *ctx, int x, int y, int z, bool from_walk_data)
{
	if (!ctx->map)
		return;
	struct texture *dst = &ctx->map->textures[y];
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	Point pos = texture_pos(ctx, x, z);
	int alpha;
	switch (ctx->version) {
	case DRAW_DUNGEON_1:
		alpha = from_walk_data ? 128 : 255;
		gfx_fill_amap(dst, pos.x, pos.y, ctx->map->grid_size, ctx->map->grid_size, alpha);
		break;
	case DRAW_DUNGEON_2:
		if ((x == ctx->dgn->start_x && z == ctx->dgn->start_y) || cell->floor_event == 17) {
			// Do not overwrite the start/goal symbol.
			gfx_fill_amap(dst, pos.x, pos.y, ctx->map->grid_size, ctx->map->grid_size, 255);
		} else {
			int rg = (cell->walked & 1) ? 0 : 255;
			gfx_fill_with_alpha(dst, pos.x, pos.y, ctx->map->grid_size, ctx->map->grid_size, rg, rg, 0, 255);
		}
		break;
	case DRAW_FIELD:
		alpha = cell->floor == -1 ? 160 : 255;
		gfx_fill_amap(dst, pos.x, pos.y, ctx->map->grid_size, ctx->map->grid_size, alpha);
	default:
		break;
	}
}

void dungeon_map_hide(struct dungeon_context *ctx, int x, int y, int z)
{
	if (!ctx->map)
		return;
	struct texture *dst = &ctx->map->textures[y];
	Point pos = texture_pos(ctx, x, z);
	gfx_fill_amap(dst, pos.x, pos.y, ctx->map->grid_size, ctx->map->grid_size, 0);
}

static void draw_hline(struct texture *dst, int x, int y, int w, SDL_Color col)
{
	gfx_fill(dst, x, y, w, 1, col.r, col.g, col.b);
}

static void draw_vline(struct texture *dst, int x, int y, int h, SDL_Color col)
{
	gfx_fill(dst, x, y, 1, h, col.r, col.g, col.b);
}

static void draw_symbol(struct dungeon_map *map, struct texture *dst, int x, int y, int type)
{
	struct sact_sprite *sp = sact_get_sprite(map->symbol_sprites[type]);
	if (!sp)
		VM_ERROR("Invalid map cg %d", map->symbol_sprites[type]);
	struct texture *src = sprite_get_texture(sp);
	gfx_copy(dst, x, y, src, 0, 0, src->w, src->h);
}

static void dd1_update_cell(struct dungeon_context *ctx, int dgn_x, int dgn_y, int dgn_z)
{
	if (!ctx->map)
		return;
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, dgn_x, dgn_y, dgn_z);
	struct texture *dst = &ctx->map->textures[dgn_y];
	Point pos = texture_pos(ctx, dgn_x, dgn_z);
	int x = pos.x;
	int y = pos.y;
	const int CS = ctx->map->grid_size;

	if (cell->floor >= 0)
		gfx_fill(dst, x, y, CS, CS, 0, 0, 0);
	else
		gfx_fill(dst, x, y, CS, CS, unenterable_cell_color.r, unenterable_cell_color.g, unenterable_cell_color.b);

	const SDL_Color wall_colors[2] = {{255, 255, 255, 255}, {255, 0, 0, 255}};
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

	bool draw_all_symbols = ctx->map->radar || ctx->map->all_visible;
	int symbol = -1;
	switch (cell->floor_event) {
	case 11: if (draw_all_symbols) symbol = DD1_SYMBOL_TREASURE; break;
	case 12: if (draw_all_symbols) symbol = DD1_SYMBOL_MONSTER; break;
	case 14: case 140:             symbol = DD1_SYMBOL_EVENT; break;
	case 15:                       symbol = DD1_SYMBOL_HEART; break;
	case 16: if (draw_all_symbols) symbol = DD1_SYMBOL_CROSS; break;
	case 17:                       symbol = DD1_SYMBOL_EXIT; break;
	case 18:                       symbol = DD1_SYMBOL_WARP; break;
	case 28: if (draw_all_symbols) symbol = DD1_SYMBOL_YELLOW_STAR; break;
	}
	if (cell->stairs_texture >= 0)
		symbol = DD1_SYMBOL_STAIRS_UP;
	else if (dgn_y && dgn_cell_at(ctx->dgn, dgn_x, dgn_y - 1, dgn_z)->stairs_texture >= 0)
		symbol = DD1_SYMBOL_STAIRS_DOWN;

	if (symbol >= 0)
		draw_symbol(ctx->map, dst, x + 1, y + 1, symbol);
}

static void dd2_update_cell(struct dungeon_context *ctx, int dgn_x, int dgn_y, int dgn_z)
{
	if (!ctx->map)
		return;
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, dgn_x, dgn_y, dgn_z);
	struct texture *dst = &ctx->map->textures[0];
	Point pos = texture_pos(ctx, dgn_x, dgn_z);

	if (dgn_x == ctx->dgn->start_x && dgn_z == ctx->dgn->start_y) {
		draw_symbol(ctx->map, dst, pos.x, pos.y, DD2_SYMBOL_START);
	} else if (cell->floor_event == 17) {
		draw_symbol(ctx->map, dst, pos.x, pos.y, DD2_SYMBOL_EXIT);
	}
}

static void df_update_cell(struct dungeon_context *ctx, int dgn_x, int dgn_y, int dgn_z)
{
	if (!ctx->map)
		return;
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, dgn_x, dgn_y, dgn_z);
	struct texture *dst = &ctx->map->textures[dgn_y];
	Point pos = texture_pos(ctx, dgn_x, dgn_z);
	int x = pos.x;
	int y = pos.y;
	const int CS = ctx->map->grid_size;

	if (cell->floor >= 0)
		gfx_fill(dst, x, y, CS, CS, 128, 64, 32);
	else
		gfx_fill(dst, x, y, CS, CS, 0, 0, 0);

	const SDL_Color wall_color = {255, 255, 255, 255};
	if (cell->north_wall >= 0)
		draw_hline(dst, x, y, CS, wall_color);
	if (cell->south_wall >= 0)
		draw_hline(dst, x, y + CS - 1, CS, wall_color);
	if (cell->west_wall >= 0)
		draw_vline(dst, x, y, CS, wall_color);
	if (cell->east_wall >= 0)
		draw_vline(dst, x + CS - 1, y, CS, wall_color);

	const SDL_Color door_colors[2] = {{0, 255, 0, 255}, {255, 0, 0, 255}};
	if (cell->north_door >= 0) {
		draw_hline(dst, x, y, CS / 2 - 1, door_colors[cell->north_door_lock]);
		draw_hline(dst, x + CS / 2 - 1, y, 2, COLOR(0, 0, 0, 0));
		draw_hline(dst, x + CS / 2 + 1, y, CS / 2 - 1, door_colors[cell->north_door_lock]);
	}
	if (cell->south_door >= 0) {
		draw_hline(dst, x, y + CS - 1, CS / 2 - 1, door_colors[cell->south_door_lock]);
		draw_hline(dst, x + CS / 2 - 1, y + CS - 1, 2, COLOR(0, 0, 0, 0));
		draw_hline(dst, x + CS / 2 + 1, y + CS - 1, CS / 2 - 1, door_colors[cell->south_door_lock]);
	}
	if (cell->west_door >= 0) {
		draw_vline(dst, x, y, CS / 2 - 1, door_colors[cell->west_door_lock]);
		draw_vline(dst, x, y + CS / 2 - 1, 2, COLOR(0, 0, 0, 0));
		draw_vline(dst, x, y + CS / 2 + 1, CS / 2 - 1, door_colors[cell->west_door_lock]);
	}
	if (cell->east_door >= 0) {
		draw_vline(dst, x + CS - 1, y, CS / 2 - 1, door_colors[cell->east_door_lock]);
		draw_vline(dst, x + CS - 1, y + CS / 2 - 1, 2, COLOR(0, 0, 0, 0));
		draw_vline(dst, x + CS - 1, y + CS / 2 + 1, CS / 2 - 1, door_colors[cell->east_door_lock]);
	}

	if (cell->stairs_texture >= 0)
		gfx_fill(dst, x + 2, y + 2, CS - 4, CS - 4, 0, 255, 255);
	else if (dgn_y && dgn_cell_at(ctx->dgn, dgn_x, dgn_y - 1, dgn_z)->stairs_texture >= 0)
		gfx_fill(dst, x + 2, y + 2, CS - 4, CS - 4, 0, 0, 255);
}

void dungeon_map_update_cell(struct dungeon_context *ctx, int dgn_x, int dgn_y, int dgn_z)
{
	switch (ctx->version) {
	case DRAW_DUNGEON_1:
		dd1_update_cell(ctx, dgn_x, dgn_y, dgn_z);
		break;
	case DRAW_DUNGEON_2:
		dd2_update_cell(ctx, dgn_x, dgn_y, dgn_z);
		break;
	case DRAW_DUNGEON_14:
		// unused
		break;
	case DRAW_FIELD:
		df_update_cell(ctx, dgn_x, dgn_y, dgn_z);
	}
}

static int player_symbol(struct dungeon_context *ctx)
{
	const int dd1_symbols[] = {
		DD1_SYMBOL_PLAYER_NORTH,
		DD1_SYMBOL_PLAYER_EAST,
		DD1_SYMBOL_PLAYER_SOUTH,
		DD1_SYMBOL_PLAYER_WEST,
	};
	const int dd2_symbols[] = {
		DD2_SYMBOL_PLAYER_NORTH,
		DD2_SYMBOL_PLAYER_EAST,
		DD2_SYMBOL_PLAYER_SOUTH,
		DD2_SYMBOL_PLAYER_WEST,
	};
	const int *symbols = ctx->version == DRAW_DUNGEON_1 ? dd1_symbols : dd2_symbols;
	return symbols[(int)(roundf(ctx->camera.angle * 2 / GLM_PIf)) & 3];
}

void dungeon_map_draw(int surface, int sprite)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->map)
		return;

	if (ctx->version == DRAW_DUNGEON_2) {
		// Draw the entire map
		dungeon_map_draw_lmap(surface, sprite);
		return;
	}

	struct sact_sprite *sp = sact_get_sprite(sprite);
	if (!sp)
		VM_ERROR("DrawDungeon.DrawMap: invalid sprite %d", sprite);
	sprite_dirty(sp);
	struct texture *dst = sprite_get_texture(sp);

	int level = ctx->map->small_map_floor;
	if (level < 0)
		level = ctx->player_pos[1];

	struct texture *src = &ctx->map->textures[level];

	const int CS = ctx->map->grid_size;
	Point pos = texture_pos(ctx, ctx->player_pos[0], ctx->player_pos[2]);
	int x = pos.x - (dst->w - CS) / 2;
	int y = pos.y - (dst->h - CS) / 2;

	// Clear the texture so that no garbage is left if copy from `src` is clipped.
	gfx_fill_with_alpha(dst, 0, 0, dst->w, dst->h, unenterable_cell_color.r, unenterable_cell_color.g, unenterable_cell_color.b, unenterable_cell_color.a);

	gfx_copy_with_alpha_map(dst, 0, 0, src, x, y, dst->w, dst->h);

	draw_symbol(ctx->map, dst, (dst->w - CS + 2) / 2, (dst->h - CS + 2) / 2, player_symbol(ctx));

	if (ctx->map->all_visible)
		gfx_fill_amap(dst, 0, 0, dst->w, dst->h, 255);
}

void dungeon_map_draw_lmap(int surface, int sprite)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->map)
		return;
	struct sact_sprite *sp = sact_get_sprite(sprite);
	if (!sp)
		VM_ERROR("DrawDungeon.DrawMap: invalid sprite %d", sprite);
	sprite_dirty(sp);
	struct texture *dst = sprite_get_texture(sp);

	int level = ctx->map->large_map_floor;
	if (level < 0)
		level = ctx->player_pos[1];

	struct texture *src = &ctx->map->textures[level];
	gfx_copy_with_alpha_map(dst, 0, 0, src, 0, 0, src->w, src->h);

	if (ctx->map->all_visible)
		gfx_fill_amap(dst, 0, 0, dst->w, dst->h, 255);

	// Draw the player symbol.
	Point pos = texture_pos(ctx, ctx->player_pos[0], ctx->player_pos[2]);
	if (ctx->version == DRAW_FIELD) {
		int ofs = (ctx->map->grid_size - 4) / 2;
		gfx_fill(dst, pos.x + ofs, pos.y + ofs, 4, 4, 255, 192, 255);
	} else {
		draw_symbol(ctx->map, dst, pos.x + 1, pos.y + 1, player_symbol(ctx));
		int size = ctx->map->grid_size - 2;
		gfx_fill_amap(dst, pos.x + 1, pos.y + 1, size, size, level == ctx->player_pos[1] ? 255 : 128);
	}
}

static void redraw_event_symbols(struct dungeon_context *ctx)
{
	int nr_cells = dgn_nr_cells(ctx->dgn);
	for (struct dgn_cell *c = ctx->dgn->cells; c < ctx->dgn->cells + nr_cells; c++) {
		if (c->floor_event)
			dungeon_map_update_cell(ctx, c->x, c->y, c->z);
	}
}

void dungeon_map_set_radar(int surface, int flag)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->map || ctx->map->radar == flag)
		return;
	ctx->map->radar = flag;
	redraw_event_symbols(ctx);
}

void dungeon_map_set_all_view(int surface, int flag)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->map || ctx->map->all_visible == flag)
		return;
	ctx->map->all_visible = flag;
	redraw_event_symbols(ctx);
}

void dungeon_map_set_cg(int surface, int index, int sprite)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->map)
		return;
	if (index < 0 || index >= NR_SYMBOLS)
		VM_ERROR("DrawDungeon.SetMapCG: index %d is out of bounds", index);
	ctx->map->symbol_sprites[index] = sprite;
}

void dungeon_map_set_small_map_floor(int surface, int floor)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->map)
		return;
	ctx->map->small_map_floor = floor;
}

void dungeon_map_set_large_map_floor(int surface, int floor)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->map)
		return;
	ctx->map->large_map_floor = floor;
}
