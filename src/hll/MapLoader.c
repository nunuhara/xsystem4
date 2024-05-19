/* Copyright (C) 2022 kichikuou <KichikuouChrome@gmail.com>
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
#include <string.h>

#include "system4/cg.h"
#include "system4/file.h"
#include "system4/mt19937int.h"
#include "system4/little_endian.h"

#include "hll.h"
#include "sact.h"
#include "xsystem4.h"

#define MAP_WIDTH 20
#define MAP_HEIGHT 9

/*
 * struct MapHeader {
 *   char magic[4];  // "MAP\0"
 *   // the below are encrypted
 *   uint32_t map_no;
 *   struct Location cg[2];
 *   struct Location move_table;  // bool[MAP_HEIGHT][MAP_WIDTH]
 *   struct Location figures;     // struct Figure[]
 *   uint32_t level;
 *   uint32_t turn;
 * };
 *
 * struct Location {
 *   uint32_t offset;
 *   uint32_t length;
 * };
 *
 * struct Figure {
 *   uint32_t x;
 *   uint32_t y;
 *   uint32_t unit;
 * };
 */

static uint8_t *map_data;
static size_t map_size;

static char *mapfile_path(int map_no)
{
	char buf[32];
	sprintf(buf, "Map/%04d.map", map_no);
	return gamedir_path(buf);
}

static bool map_load_cg(int sprite, int index)
{
	if (!map_data)
		return false;

	struct sact_sprite *sp = sact_get_sprite(sprite);
	if (!sp)
		return false;
	size_t offset = LittleEndian_getDW(map_data, 8 + index * 8);
	size_t length = LittleEndian_getDW(map_data, 8 + index * 8 + 4);
	struct cg *cg = cg_load_buffer(map_data + offset, length);
	if (!cg)
		return false;
	sprite_set_cg(sp, cg);
	cg_free(cg);
	return true;
}

static bool MapLoader_Load(int map_no)
{
	if (map_data)
		free(map_data);

	char *path = mapfile_path(map_no);
	map_data = file_read(path, &map_size);
	if (!map_data) {
		WARNING("Cannot read %s", path);
	} else if (memcmp(map_data, "MAP", 4)) {
		WARNING("%s: Wrong magic bytes", path);
		free(map_data);
		map_data = NULL;
	} else {
		mt19937_xorcode(map_data + 4, map_size - 4, 12753);
	}
	free(path);
	return !!map_data;
}

void MapLoader_Release(void)
{
	if (map_data) {
		free(map_data);
		map_data = NULL;
	}
}

static bool MapLoader_IsExist(int map_no)
{
	char *path = mapfile_path(map_no);
	bool result = file_exists(path);
	free(path);
	return result;
}

static bool MapLoader_LoadCG(int sp)
{
	return map_load_cg(sp, 0);
}

static bool MapLoader_LoadUnitBackCG(int sp)
{
	return map_load_cg(sp, 1);
}

static bool MapLoader_IsMove(int x, int y)
{
	if (!map_data || (unsigned)x >= MAP_WIDTH || (unsigned)y >= MAP_HEIGHT)
		return false;
	uint8_t *table = map_data + LittleEndian_getDW(map_data, 24);
	return table[y * MAP_WIDTH + x];
}

static int MapLoader_GetFigure(int x, int y)
{
	if (!map_data)
		return 0;

	uint8_t *p = map_data + LittleEndian_getDW(map_data, 32);
	uint8_t *end = p + LittleEndian_getDW(map_data, 36);

	while (p < end) {
		if (x == LittleEndian_getDW(p, 0) && y == LittleEndian_getDW(p, 4))
			return LittleEndian_getDW(p, 8);
		p += 12;
	}
	return 0;
}

static int MapLoader_GetLevel(void)
{
	if (!map_data)
		return -1;
	return LittleEndian_getDW(map_data, 40);
}

static int MapLoader_GetTurn(void)
{
	if (!map_data)
		return -1;
	return LittleEndian_getDW(map_data, 44);
}

HLL_LIBRARY(MapLoader,
	    HLL_EXPORT(Load, MapLoader_Load),
	    HLL_EXPORT(Release, MapLoader_Release),
	    HLL_EXPORT(IsExist, MapLoader_IsExist),
	    HLL_EXPORT(LoadCG, MapLoader_LoadCG),
	    HLL_EXPORT(LoadUnitBackCG, MapLoader_LoadUnitBackCG),
	    HLL_EXPORT(IsMove, MapLoader_IsMove),
	    HLL_EXPORT(GetFigure, MapLoader_GetFigure),
	    HLL_EXPORT(GetLevel, MapLoader_GetLevel),
	    HLL_EXPORT(GetTurn, MapLoader_GetTurn)
	    );
