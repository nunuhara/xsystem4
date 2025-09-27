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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include "system4.h"
#include "system4/string.h"

#include "dungeon/dgn.h"
#include "dungeon/dungeon.h"
#include "dungeon/map.h"
#include "dungeon/renderer.h"
#include "dungeon/tes.h"
#include "sact.h"
#include "hll.h"

static int dungeon_init(enum draw_dungeon_version version, int surface)
{
	struct sact_sprite *sp = sact_get_sprite(surface);
	if (!sp)
		return 0;

	struct dungeon_context *ctx = dungeon_context_create(version, sp->rect.w, sp->rect.h);
	sprite_bind_plugin(sp, &ctx->plugin);
	return 1;
}

static struct dgn_cell *dungeon_get_cell(int surface, int x, int y, int z)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn || !dgn_is_in_map(ctx->dgn, x, y, z))
		return NULL;
	return dgn_cell_at(ctx->dgn, x, y, z);
}

static int DrawDungeon_Init(int surface)
{
	return dungeon_init(DRAW_DUNGEON_1, surface);
}

static int DrawDungeon2_Init(int surface)
{
	return dungeon_init(DRAW_DUNGEON_2, surface);
}

static int DrawDungeon14_Init(int surface)
{
	return dungeon_init(DRAW_DUNGEON_14, surface);
}

static int DrawField_Init(int surface)
{
	return dungeon_init(DRAW_FIELD, surface);
}

static void DrawDungeon_Release(int surface)
{
	if (!dungeon_get_context(surface))
		return;
	sprite_bind_plugin(sact_get_sprite(surface), NULL);
}

static void DrawDungeon_SetDrawFlag(int surface, int flag)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return;
	ctx->draw_enabled = flag;
}

static bool DrawDungeon_GetDrawFlag(int surface)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return false;
	return ctx->draw_enabled;
}

static bool DrawDungeon_BeginLoad(int surface, int num)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return false;
	return dungeon_load(ctx, num);
}

static bool DrawDungeon_IsLoading(int surface)
{
	return false;  // We don't use background loading.
}

static bool DrawDungeon_IsSucceededLoading(int surface)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return false;
	return ctx->loaded;
}

//float DrawDungeon_GetLoadingPercent(int surface);

static int DrawDungeon_LoadFromFile(int surface, struct string *file_name, int num)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return false;
	return dungeon_load_dungeon(ctx, file_name->text, num);
}

static int DrawDungeon_LoadTexture(int surface, struct string *file_name)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return false;
	return dungeon_load_texture(ctx, file_name->text);
}

//int DrawDungeon_LoadEventTexture(int surface, struct string *file_name);

static int DrawDungeon_GetMapX(int surface)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return -1;
	return ctx->dgn->size_x;
}

static int DrawDungeon_GetMapY(int surface)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return -1;
	return ctx->dgn->size_y;
}

static int DrawDungeon_GetMapZ(int surface)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return -1;
	return ctx->dgn->size_z;
}

static int DrawDungeon_IsInMap(int surface, int x, int y, int z)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return -1;
	return dgn_is_in_map(ctx->dgn, x, y, z);
}

#define CELL_GETTER(name, defval, expr) \
	static int name(int surface, int x, int y, int z) \
	{ \
		struct dgn_cell *cell = dungeon_get_cell(surface, x, y, z); \
		if (!cell) \
			return defval; \
		return expr; \
	}

CELL_GETTER(DrawDungeon_IsFloor, 0, cell->floor != -1);
CELL_GETTER(DrawDungeon_IsStair, 0, cell->stairs_texture != -1);
CELL_GETTER(DrawDungeon_IsStairN, 0, cell->stairs_texture != -1 && cell->stairs_orientation == 0);
CELL_GETTER(DrawDungeon_IsStairW, 0, cell->stairs_texture != -1 && cell->stairs_orientation == 1);
CELL_GETTER(DrawDungeon_IsStairS, 0, cell->stairs_texture != -1 && cell->stairs_orientation == 2);
CELL_GETTER(DrawDungeon_IsStairE, 0, cell->stairs_texture != -1 && cell->stairs_orientation == 3);
CELL_GETTER(DrawDungeon_IsWallN, 0, cell->north_wall != -1);
CELL_GETTER(DrawDungeon_IsWallW, 0, cell->west_wall != -1);
CELL_GETTER(DrawDungeon_IsWallS, 0, cell->south_wall != -1);
CELL_GETTER(DrawDungeon_IsWallE, 0, cell->east_wall != -1);
CELL_GETTER(DrawDungeon_IsDoorN, 0, cell->north_door != -1);
CELL_GETTER(DrawDungeon_IsDoorW, 0, cell->west_door != -1);
CELL_GETTER(DrawDungeon_IsDoorS, 0, cell->south_door != -1);
CELL_GETTER(DrawDungeon_IsDoorE, 0, cell->east_door != -1);
CELL_GETTER(DrawDungeon_IsEnter, 0, cell->enterable);
CELL_GETTER(DrawDungeon_IsEnterN, 0, cell->enterable_north);
CELL_GETTER(DrawDungeon_IsEnterW, 0, cell->enterable_west);
CELL_GETTER(DrawDungeon_IsEnterS, 0, cell->enterable_south);
CELL_GETTER(DrawDungeon_IsEnterE, 0, cell->enterable_east);
CELL_GETTER(DrawDungeon_GetEvFloor, 0, cell->floor_event);
CELL_GETTER(DrawDungeon_GetEvWallN, 0, cell->north_event);
CELL_GETTER(DrawDungeon_GetEvWallW, 0, cell->west_event);
CELL_GETTER(DrawDungeon_GetEvWallS, 0, cell->south_event);
CELL_GETTER(DrawDungeon_GetEvWallE, 0, cell->east_event);
CELL_GETTER(DrawDungeon_GetEvFloor2, 0, cell->floor_event2);
CELL_GETTER(DrawDungeon_GetEvWallN2, 0, cell->north_event2);
CELL_GETTER(DrawDungeon_GetEvWallW2, 0, cell->west_event2);
CELL_GETTER(DrawDungeon_GetEvWallS2, 0, cell->south_event2);
CELL_GETTER(DrawDungeon_GetEvWallE2, 0, cell->east_event2);
CELL_GETTER(DrawDungeon_GetTexFloor, -1, cell->floor);
CELL_GETTER(DrawDungeon_GetTexCeiling, -1, cell->ceiling);
CELL_GETTER(DrawDungeon_GetTexWallN, -1, cell->north_wall);
CELL_GETTER(DrawDungeon_GetTexWallW, -1, cell->west_wall);
CELL_GETTER(DrawDungeon_GetTexWallS, -1, cell->south_wall);
CELL_GETTER(DrawDungeon_GetTexWallE, -1, cell->east_wall);
CELL_GETTER(DrawDungeon_GetTexDoorN, -1, cell->north_door);
CELL_GETTER(DrawDungeon_GetTexDoorW, -1, cell->west_door);
CELL_GETTER(DrawDungeon_GetTexDoorS, -1, cell->south_door);
CELL_GETTER(DrawDungeon_GetTexDoorE, -1, cell->east_door);
CELL_GETTER(DrawDungeon_GetTexStair, -1, cell->stairs_texture);
CELL_GETTER(DrawDungeon_GetBattleBack, -1, cell->battle_background);

static bool DrawDungeon_IsFloorFillTexture(int surface, int x, int y, int z)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->renderer || !ctx->dgn || !dgn_is_in_map(ctx->dgn, x, y, z))
		return false;

	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	return dungeon_renderer_is_floor_opaque(ctx->renderer, cell);
}

#define CELL_SETTER(name, type, expr, update_map) \
	static void name(int surface, int x, int y, int z, type value) \
	{ \
		struct dungeon_context *ctx = dungeon_get_context(surface); \
		if (!ctx || !ctx->dgn || !dgn_is_in_map(ctx->dgn, x, y, z)) \
			return; \
		struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z); \
		expr = value; \
		if (update_map) \
			dungeon_map_update_cell(ctx, x, y, z);	\
	}

CELL_SETTER(DrawDungeon_SetDoorNAngle, float, cell->north_door_angle, false);
CELL_SETTER(DrawDungeon_SetDoorWAngle, float, cell->west_door_angle, false);
CELL_SETTER(DrawDungeon_SetDoorSAngle, float, cell->south_door_angle, false);
CELL_SETTER(DrawDungeon_SetDoorEAngle, float, cell->east_door_angle, false);
CELL_SETTER(DrawDungeon_SetEvFloor, int, cell->floor_event, true);
CELL_SETTER(DrawDungeon_SetEvWallN, int, cell->north_event, true);
CELL_SETTER(DrawDungeon_SetEvWallW, int, cell->west_event, true);
CELL_SETTER(DrawDungeon_SetEvWallS, int, cell->south_event, true);
CELL_SETTER(DrawDungeon_SetEvWallE, int, cell->east_event, true);
CELL_SETTER(DrawDungeon_SetEvFloor2, int, cell->floor_event2, false);
CELL_SETTER(DrawDungeon_SetEvWallN2, int, cell->north_event2, false);
CELL_SETTER(DrawDungeon_SetEvWallW2, int, cell->west_event2, false);
CELL_SETTER(DrawDungeon_SetEvWallS2, int, cell->south_event2, false);
CELL_SETTER(DrawDungeon_SetEvWallE2, int, cell->east_event2, false);
//void DrawDungeon_SetEvMag(int surface, int x, int y, int z, float mag);
CELL_SETTER(DrawDungeon_SetEvRate, int, cell->event_blend_rate, false);
CELL_SETTER(DrawDungeon_SetEnter, int, cell->enterable, false);
CELL_SETTER(DrawDungeon_SetEnterN, int, cell->enterable_north, true);
CELL_SETTER(DrawDungeon_SetEnterW, int, cell->enterable_west, true);
CELL_SETTER(DrawDungeon_SetEnterS, int, cell->enterable_south, true);
CELL_SETTER(DrawDungeon_SetEnterE, int, cell->enterable_east, true);
CELL_SETTER(DrawDungeon_SetTexFloor, int, cell->floor, true);
CELL_SETTER(DrawDungeon_SetTexCeiling, int, cell->ceiling, false);
CELL_SETTER(DrawDungeon_SetTexWallN, int, cell->north_wall, true);
CELL_SETTER(DrawDungeon_SetTexWallW, int, cell->west_wall, true);
CELL_SETTER(DrawDungeon_SetTexWallS, int, cell->south_wall, true);
CELL_SETTER(DrawDungeon_SetTexWallE, int, cell->east_wall, true);
CELL_SETTER(DrawDungeon_SetTexDoorN, int, cell->north_door, true);
CELL_SETTER(DrawDungeon_SetTexDoorW, int, cell->west_door, true);
CELL_SETTER(DrawDungeon_SetTexDoorS, int, cell->south_door, true);
CELL_SETTER(DrawDungeon_SetTexDoorE, int, cell->east_door, true);
//void DrawDungeon_SetSkyTextureSet(int surface, int set);

HLL_QUIET_UNIMPLEMENTED( , void, DrawDungeon, SetDrawHalfFlag, int surface, int flag);
HLL_QUIET_UNIMPLEMENTED(0, int,  DrawDungeon, GetDrawHalfFlag, int surface);
//int DrawDungeon_CalcNumofFloor(int surface);
//int DrawDungeon_CalcNumofWalk(int surface);
//int DrawDungeon_CalcNumofWalk2(int surface);
HLL_QUIET_UNIMPLEMENTED( , void, DrawDungeon, SetInterlaceMode, int surface, int flag);
HLL_QUIET_UNIMPLEMENTED(1, bool, DrawDungeon, SetDirect3DMode, int surface, int flag);
HLL_QUIET_UNIMPLEMENTED(1, bool, DrawDungeon, GetDirect3DMode, int surface);
HLL_QUIET_UNIMPLEMENTED( , void, DrawDungeon, SaveDrawSettingFlag, int direct3D, int interlace, int half);
HLL_QUIET_UNIMPLEMENTED( , void, DrawDungeon, GetDrawSettingFlag, int *direct3D, int *half);
HLL_QUIET_UNIMPLEMENTED(1, bool, DrawDungeon, IsDrawSettingFile, void);
HLL_QUIET_UNIMPLEMENTED( , void, DrawDungeon, DeleteDrawSettingFile, void);
HLL_QUIET_UNIMPLEMENTED(1, bool, DrawDungeon, IsSSEFlag, void);
HLL_QUIET_UNIMPLEMENTED( , void, DrawDungeon, SetSSEFlag, bool flag);

static bool DrawDungeon_IsPVSData(int surface)
{
	return false;
}

static int DrawDungeon_GetTexSound(int surface, int type, int num)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->tes)
		return -1;
	return tes_get(ctx->tes, type, num);
}

HLL_QUIET_UNIMPLEMENTED( , void, DrawDungeon, StopTimer, void);
HLL_QUIET_UNIMPLEMENTED( , void, DrawDungeon, RestartTimer, void);

#define DRAW_DUNGEON_EXPORTS \
	HLL_EXPORT(Release, DrawDungeon_Release), \
	HLL_EXPORT(SetDrawFlag, DrawDungeon_SetDrawFlag), \
	HLL_EXPORT(BeginLoad, DrawDungeon_BeginLoad), \
	HLL_EXPORT(IsLoading, DrawDungeon_IsLoading), \
	HLL_EXPORT(IsSucceededLoading, DrawDungeon_IsSucceededLoading), \
	HLL_TODO_EXPORT(GetLoadingPercent, DrawDungeon_GetLoadingPercent), \
	HLL_EXPORT(LoadFromFile, DrawDungeon_LoadFromFile), \
	HLL_EXPORT(LoadTexture, DrawDungeon_LoadTexture), \
	HLL_TODO_EXPORT(LoadEventTexture, DrawDungeon_LoadEventTexture), \
	HLL_EXPORT(GetMapX, DrawDungeon_GetMapX), \
	HLL_EXPORT(GetMapY, DrawDungeon_GetMapY), \
	HLL_EXPORT(GetMapZ, DrawDungeon_GetMapZ), \
	HLL_EXPORT(SetCamera, dungeon_set_camera), \
	HLL_EXPORT(IsInMap, DrawDungeon_IsInMap), \
	HLL_EXPORT(IsFloor, DrawDungeon_IsFloor), \
	HLL_EXPORT(IsFloorFillTexture, DrawDungeon_IsFloorFillTexture), \
	HLL_EXPORT(IsStair, DrawDungeon_IsStair), \
	HLL_EXPORT(IsStairN, DrawDungeon_IsStairN), \
	HLL_EXPORT(IsStairW, DrawDungeon_IsStairW), \
	HLL_EXPORT(IsStairS, DrawDungeon_IsStairS), \
	HLL_EXPORT(IsStairE, DrawDungeon_IsStairE), \
	HLL_EXPORT(IsWallN, DrawDungeon_IsWallN), \
	HLL_EXPORT(IsWallW, DrawDungeon_IsWallW), \
	HLL_EXPORT(IsWallS, DrawDungeon_IsWallS), \
	HLL_EXPORT(IsWallE, DrawDungeon_IsWallE), \
	HLL_EXPORT(IsDoorN, DrawDungeon_IsDoorN), \
	HLL_EXPORT(IsDoorW, DrawDungeon_IsDoorW), \
	HLL_EXPORT(IsDoorS, DrawDungeon_IsDoorS), \
	HLL_EXPORT(IsDoorE, DrawDungeon_IsDoorE), \
	HLL_EXPORT(IsEnter, DrawDungeon_IsEnter), \
	HLL_EXPORT(IsEnterN, DrawDungeon_IsEnterN), \
	HLL_EXPORT(IsEnterW, DrawDungeon_IsEnterW), \
	HLL_EXPORT(IsEnterS, DrawDungeon_IsEnterS), \
	HLL_EXPORT(IsEnterE, DrawDungeon_IsEnterE), \
	HLL_EXPORT(GetEvFloor, DrawDungeon_GetEvFloor), \
	HLL_EXPORT(GetEvWallN, DrawDungeon_GetEvWallN), \
	HLL_EXPORT(GetEvWallW, DrawDungeon_GetEvWallW), \
	HLL_EXPORT(GetEvWallS, DrawDungeon_GetEvWallS), \
	HLL_EXPORT(GetEvWallE, DrawDungeon_GetEvWallE), \
	HLL_EXPORT(GetEvFloor2, DrawDungeon_GetEvFloor2), \
	HLL_EXPORT(GetEvWallN2, DrawDungeon_GetEvWallN2), \
	HLL_EXPORT(GetEvWallW2, DrawDungeon_GetEvWallW2), \
	HLL_EXPORT(GetEvWallS2, DrawDungeon_GetEvWallS2), \
	HLL_EXPORT(GetEvWallE2, DrawDungeon_GetEvWallE2), \
	HLL_EXPORT(GetTexFloor, DrawDungeon_GetTexFloor), \
	HLL_EXPORT(GetTexCeiling, DrawDungeon_GetTexCeiling), \
	HLL_EXPORT(GetTexWallN, DrawDungeon_GetTexWallN), \
	HLL_EXPORT(GetTexWallW, DrawDungeon_GetTexWallW), \
	HLL_EXPORT(GetTexWallS, DrawDungeon_GetTexWallS), \
	HLL_EXPORT(GetTexWallE, DrawDungeon_GetTexWallE), \
	HLL_EXPORT(GetTexDoorN, DrawDungeon_GetTexDoorN), \
	HLL_EXPORT(GetTexDoorW, DrawDungeon_GetTexDoorW), \
	HLL_EXPORT(GetTexDoorS, DrawDungeon_GetTexDoorS), \
	HLL_EXPORT(GetTexDoorE, DrawDungeon_GetTexDoorE), \
	HLL_EXPORT(GetTexStair, DrawDungeon_GetTexStair), \
	HLL_EXPORT(GetBattleBack, DrawDungeon_GetBattleBack), \
	HLL_EXPORT(SetDoorNAngle, DrawDungeon_SetDoorNAngle), \
	HLL_EXPORT(SetDoorWAngle, DrawDungeon_SetDoorWAngle), \
	HLL_EXPORT(SetDoorSAngle, DrawDungeon_SetDoorSAngle), \
	HLL_EXPORT(SetDoorEAngle, DrawDungeon_SetDoorEAngle), \
	HLL_EXPORT(SetEvFloor, DrawDungeon_SetEvFloor), \
	HLL_EXPORT(SetEvWallN, DrawDungeon_SetEvWallN), \
	HLL_EXPORT(SetEvWallW, DrawDungeon_SetEvWallW), \
	HLL_EXPORT(SetEvWallS, DrawDungeon_SetEvWallS), \
	HLL_EXPORT(SetEvWallE, DrawDungeon_SetEvWallE), \
	HLL_EXPORT(SetEvFloor2, DrawDungeon_SetEvFloor2), \
	HLL_EXPORT(SetEvWallN2, DrawDungeon_SetEvWallN2), \
	HLL_EXPORT(SetEvWallW2, DrawDungeon_SetEvWallW2), \
	HLL_EXPORT(SetEvWallS2, DrawDungeon_SetEvWallS2), \
	HLL_EXPORT(SetEvWallE2, DrawDungeon_SetEvWallE2), \
	HLL_TODO_EXPORT(SetEvMag, DrawDungeon_SetEvMag), \
	HLL_EXPORT(SetEvRate, DrawDungeon_SetEvRate), \
	HLL_EXPORT(SetEnter, DrawDungeon_SetEnter), \
	HLL_EXPORT(SetEnterN, DrawDungeon_SetEnterN), \
	HLL_EXPORT(SetEnterW, DrawDungeon_SetEnterW), \
	HLL_EXPORT(SetEnterS, DrawDungeon_SetEnterS), \
	HLL_EXPORT(SetEnterE, DrawDungeon_SetEnterE), \
	HLL_EXPORT(SetTexFloor, DrawDungeon_SetTexFloor), \
	HLL_EXPORT(SetTexCeiling, DrawDungeon_SetTexCeiling), \
	HLL_EXPORT(SetTexWallN, DrawDungeon_SetTexWallN), \
	HLL_EXPORT(SetTexWallW, DrawDungeon_SetTexWallW), \
	HLL_EXPORT(SetTexWallS, DrawDungeon_SetTexWallS), \
	HLL_EXPORT(SetTexWallE, DrawDungeon_SetTexWallE), \
	HLL_EXPORT(SetTexDoorN, DrawDungeon_SetTexDoorN), \
	HLL_EXPORT(SetTexDoorW, DrawDungeon_SetTexDoorW), \
	HLL_EXPORT(SetTexDoorS, DrawDungeon_SetTexDoorS), \
	HLL_EXPORT(SetTexDoorE, DrawDungeon_SetTexDoorE), \
	HLL_TODO_EXPORT(SetSkyTextureSet, DrawDungeon_SetSkyTextureSet), \
	HLL_EXPORT(DrawMap, dungeon_map_draw), \
	HLL_EXPORT(SetMapAllViewFlag, dungeon_map_set_all_view), \
	HLL_EXPORT(SetDrawMapFloor, dungeon_map_set_small_map_floor), \
	HLL_EXPORT(DrawLMap, dungeon_map_draw_lmap), \
	HLL_EXPORT(SetMapCG, dungeon_map_set_cg), \
	HLL_EXPORT(SetDrawLMapFloor, dungeon_map_set_large_map_floor), \
	HLL_EXPORT(SetDrawMapRadar, dungeon_map_set_radar), \
	HLL_EXPORT(SetDrawHalfFlag, DrawDungeon_SetDrawHalfFlag), \
	HLL_EXPORT(GetDrawHalfFlag, DrawDungeon_GetDrawHalfFlag), \
	HLL_EXPORT(SetWalked, dungeon_set_walked), \
	HLL_EXPORT(SetWalkedAll, dungeon_set_walked_all), \
	HLL_TODO_EXPORT(CalcNumofFloor, DrawDungeon_CalcNumofFloor), \
	HLL_TODO_EXPORT(CalcNumofWalk, DrawDungeon_CalcNumofWalk), \
	HLL_TODO_EXPORT(CalcNumofWalk2, DrawDungeon_CalcNumofWalk2), \
	HLL_EXPORT(CalcConquer, dungeon_calc_conquer), \
	HLL_EXPORT(SetInterlaceMode, DrawDungeon_SetInterlaceMode), \
	HLL_EXPORT(SetDirect3DMode, DrawDungeon_SetDirect3DMode), \
	HLL_EXPORT(SaveDrawSettingFlag, DrawDungeon_SaveDrawSettingFlag), \
	HLL_EXPORT(GetDrawSettingFlag, DrawDungeon_GetDrawSettingFlag), \
	HLL_EXPORT(IsDrawSettingFile, DrawDungeon_IsDrawSettingFile), \
	HLL_EXPORT(DeleteDrawSettingFile, DrawDungeon_DeleteDrawSettingFile), \
	HLL_EXPORT(IsSSEFlag, DrawDungeon_IsSSEFlag), \
	HLL_EXPORT(SetSSEFlag, DrawDungeon_SetSSEFlag), \
	HLL_EXPORT(IsPVSData, DrawDungeon_IsPVSData), \
	HLL_EXPORT(GetTexSound, DrawDungeon_GetTexSound), \
	HLL_EXPORT(StopTimer, DrawDungeon_StopTimer), \
	HLL_EXPORT(RestartTimer, DrawDungeon_RestartTimer), \
	HLL_EXPORT(LoadWalkData, dungeon_load_walk_data), \
	HLL_EXPORT(SaveWalkData, dungeon_save_walk_data)

HLL_LIBRARY(DrawDungeon,
	    HLL_EXPORT(Init, DrawDungeon_Init),
	    DRAW_DUNGEON_EXPORTS
	    );

static void DrawDungeon2_GetPlayerStartPos(int surface, int *x, int *y)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return;
	*x = ctx->dgn->start_x;
	*y = ctx->dgn->start_y;
}

static void DrawDungeon2_GetExitPos(int surface, int *x, int *y)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return;
	*x = ctx->dgn->exit_x;
	*y = ctx->dgn->exit_y;
}

CELL_GETTER(DrawDungeon2_GetMapStep, -1, cell->pathfinding_cost);

HLL_LIBRARY(DrawDungeon2,
	    HLL_EXPORT(Init, DrawDungeon2_Init),
	    DRAW_DUNGEON_EXPORTS,
	    HLL_EXPORT(GetPlayerStartPos, DrawDungeon2_GetPlayerStartPos),
	    HLL_EXPORT(GetExitPos, DrawDungeon2_GetExitPos),
	    HLL_EXPORT(PaintStep, dungeon_paint_step),
	    HLL_EXPORT(GetMapStep, DrawDungeon2_GetMapStep)
	    );

static void DrawDungeon14_SetRasterScroll(int surface, int type)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->renderer)
		return;
	return dungeon_renderer_set_raster_scroll(ctx->renderer, type);
}

static void DrawDungeon14_SetRasterAmp(int surface, float amp)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->renderer)
		return;
	return dungeon_renderer_set_raster_amp(ctx->renderer, amp);
}

// unused
HLL_WARN_UNIMPLEMENTED( , void, DrawDungeon14, SetLapFilter, int surface, int type, int r, int g, int b);

static bool DrawDungeon14_GetDrawMapObjectFlag(int surface)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->renderer)
		return true;
	return dungeon_renderer_event_markers_enabled(ctx->renderer);
}

static void DrawDungeon14_SetDrawMapObjectFlag(int surface, bool flag)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->renderer)
		return;
	dungeon_renderer_enable_event_markers(ctx->renderer, flag);
}

HLL_LIBRARY(DrawDungeon14,
	    HLL_EXPORT(Init, DrawDungeon14_Init),
	    DRAW_DUNGEON_EXPORTS,
	    HLL_EXPORT(GetWalked, dungeon_get_walked),
	    HLL_EXPORT(SetRasterScroll, DrawDungeon14_SetRasterScroll),
	    HLL_EXPORT(SetRasterAmp, DrawDungeon14_SetRasterAmp),
	    HLL_EXPORT(SetLapFilter, DrawDungeon14_SetLapFilter),
	    HLL_EXPORT(GetDrawMapObjectFlag, DrawDungeon14_GetDrawMapObjectFlag),
	    HLL_EXPORT(SetDrawMapObjectFlag, DrawDungeon14_SetDrawMapObjectFlag)
	    );

//bool DrawField_BeginLoad(int nSurface, struct string *szFileName, int nNum);
//bool DrawField_IsLoadEnd(int nSurface);
//bool DrawField_StopLoad(int nSurface);
//float DrawField_GetLoadPercent(int nSurface);
//bool DrawField_IsLoadSucceeded(int nSurface);
//bool DrawField_BeginLoadTexture(int nSurface, struct string *szFileName);
//bool DrawField_IsLoadTextureEnd(int nSurface);
//bool DrawField_StopLoadTexture(int nSurface);
//float DrawField_GetLoadTexturePercent(int nSurface);
//bool DrawField_IsLoadTextureSucceeded(int nSurface);
//void DrawField_UpdateTexture(int nSurface);
//bool DrawField_GetDoorNAngle(int nSurface, int nX, int nY, int nZ, float *pfAngle);
//bool DrawField_GetDoorWAngle(int nSurface, int nX, int nY, int nZ, float *pfAngle);
//bool DrawField_GetDoorSAngle(int nSurface, int nX, int nY, int nZ, float *pfAngle);
//bool DrawField_GetDoorEAngle(int nSurface, int nX, int nY, int nZ, float *pfAngle);
//bool DrawField_SetDoorNAngle(int nSurface, int nX, int nY, int nZ, float fAngle);
//bool DrawField_SetDoorWAngle(int nSurface, int nX, int nY, int nZ, float fAngle);
//bool DrawField_SetDoorSAngle(int nSurface, int nX, int nY, int nZ, float fAngle);
//bool DrawField_SetDoorEAngle(int nSurface, int nX, int nY, int nZ, float fAngle);

static void DrawField_SetDoorNLock(int surface, int x, int y, int z, int value)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn || !dgn_is_in_map(ctx->dgn, x, y, z))
		return;
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	if (cell->north_door != -1) {
		cell->north_door_lock = value;
		dungeon_map_update_cell(ctx, x, y, z);
	}
}

static void DrawField_SetDoorWLock(int surface, int x, int y, int z, int value)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn || !dgn_is_in_map(ctx->dgn, x, y, z))
		return;
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	if (cell->west_door != -1) {
		cell->west_door_lock = value;
		dungeon_map_update_cell(ctx, x, y, z);
	}
}

static void DrawField_SetDoorSLock(int surface, int x, int y, int z, int value)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn || !dgn_is_in_map(ctx->dgn, x, y, z))
		return;
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	if (cell->south_door != -1) {
		cell->south_door_lock = value;
		dungeon_map_update_cell(ctx, x, y, z);
	}
}

static void DrawField_SetDoorELock(int surface, int x, int y, int z, int value)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn || !dgn_is_in_map(ctx->dgn, x, y, z))
		return;
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	if (cell->east_door != -1) {
		cell->east_door_lock = value;
		dungeon_map_update_cell(ctx, x, y, z);
	}
}

static bool DrawField_GetDoorNLock(int surface, int x, int y, int z, int *lock)
{
	struct dgn_cell *cell = dungeon_get_cell(surface, x, y, z);
	if (!cell || cell->north_door == -1)
		return false;
	*lock = cell->north_door_lock;
	return true;
}

static bool DrawField_GetDoorWLock(int surface, int x, int y, int z, int *lock)
{
	struct dgn_cell *cell = dungeon_get_cell(surface, x, y, z);
	if (!cell || cell->west_door == -1)
		return false;
	*lock = cell->west_door_lock;
	return true;
}

static bool DrawField_GetDoorSLock(int surface, int x, int y, int z, int *lock)
{
	struct dgn_cell *cell = dungeon_get_cell(surface, x, y, z);
	if (!cell || cell->south_door == -1)
		return false;
	*lock = cell->south_door_lock;
	return true;
}

static bool DrawField_GetDoorELock(int surface, int x, int y, int z, int *lock)
{
	struct dgn_cell *cell = dungeon_get_cell(surface, x, y, z);
	if (!cell || cell->east_door == -1)
		return false;
	*lock = cell->east_door_lock;
	return true;
}

//void DrawField_SetTexStair(int nSurface, int nX, int nY, int nZ, int nTexture, int nType);
//void DrawField_SetShadowTexFloor(int nSurface, int nX, int nY, int nZ, int nTexture);
//void DrawField_SetShadowTexCeiling(int nSurface, int nX, int nY, int nZ, int nTexture);
//void DrawField_SetShadowTexWallN(int nSurface, int nX, int nY, int nZ, int nTexture);
//void DrawField_SetShadowTexWallW(int nSurface, int nX, int nY, int nZ, int nTexture);
//void DrawField_SetShadowTexWallS(int nSurface, int nX, int nY, int nZ, int nTexture);
//void DrawField_SetShadowTexWallE(int nSurface, int nX, int nY, int nZ, int nTexture);
//void DrawField_SetShadowTexDoorN(int nSurface, int nX, int nY, int nZ, int nTexture);
//void DrawField_SetShadowTexDoorW(int nSurface, int nX, int nY, int nZ, int nTexture);
//void DrawField_SetShadowTexDoorS(int nSurface, int nX, int nY, int nZ, int nTexture);
//void DrawField_SetShadowTexDoorE(int nSurface, int nX, int nY, int nZ, int nTexture);

static void DrawField_CalcShadowMap(int surface)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return;
	return dgn_calc_lightmap(ctx->dgn);
}

HLL_QUIET_UNIMPLEMENTED(, void, DrawField, SetLooked, int surface, int x, int y, int z, bool flag);
HLL_WARN_UNIMPLEMENTED(false, bool, DrawField, SetHideDrawMapFloor, int surface, int floor, bool hide);
//bool DrawField_SetHideDrawMapWall(int nSurface, int nWall, bool bHide);

static void DrawField_SetDrawShadowMap(int surface, bool draw)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->renderer)
		return;
	dungeon_renderer_enable_lightmap(ctx->renderer, draw);
}

static float DrawField_CosDeg(float deg)
{
	return cosf(glm_rad(deg));
}

static float DrawField_SinDeg(float deg)
{
	return sinf(glm_rad(deg));
}

static float DrawField_TanDeg(float deg)
{
	return tanf(glm_rad(deg));
}

static float DrawField_Atan2(float y, float x)
{
	return glm_deg(atan2f(y, x));
}

//bool DrawField_TransPos2DToPos3DOnPlane(int surface, int nScreenX, int nScreenY, float fPlaneY, float *pfX, float *pfY, float *pfZ);
//bool DrawField_TransPos3DToPos2D(int nSurace, float fX, float fY, float fZ, int *pnScreenX, int *pnScreenY);

static int DrawField_GetCharaNumMax(int surface)
{
	return DRAWFIELD_NR_CHARACTERS;
}

//void DrawField_SetCharaZBias(int surface, float fZBias0, float fZBias1, float fZBias2, float fZBias3);

HLL_QUIET_UNIMPLEMENTED(, void, DrawField, SetCenterOffsetY, int surface, float y);
//void DrawField_SetBuilBoard(int surface, int num, int sprite);

static void DrawField_SetSphereTheta(int surface, float x, float y, float z)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return;
	ctx->dgn->sphere_theta[0] = x;
	ctx->dgn->sphere_theta[1] = y;
	ctx->dgn->sphere_theta[2] = z;
}

static void DrawField_SetSphereColor(int surface, float top, float bottom)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return;
	ctx->dgn->sphere_color_top = top;
	ctx->dgn->sphere_color_bottom = bottom;
}

static bool DrawField_GetSphereTheta(int surface, float *x, float *y, float *z)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return false;
	*x = ctx->dgn->sphere_theta[0];
	*y = ctx->dgn->sphere_theta[1];
	*z = ctx->dgn->sphere_theta[2];
	return true;
}

static bool DrawField_GetSphereColor(int surface, float *top, float *bottom)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return false;
	*top = ctx->dgn->sphere_color_top;
	*bottom = ctx->dgn->sphere_color_bottom;
	return true;
}

static bool DrawField_GetBackColor(int surface, int *r, int *g, int *b)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return false;
	*r = ctx->dgn->back_color_r;
	*g = ctx->dgn->back_color_g;
	*b = ctx->dgn->back_color_b;
	return true;
}

static void DrawField_SetBackColor(int surface, int r, int g, int b)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return;
	ctx->dgn->back_color_r = r;
	ctx->dgn->back_color_g = g;
	ctx->dgn->back_color_b = b;
}

//int DrawField_GetPolyObj(int surface, int nX, int nY, int nZ);
//float DrawField_GetPolyObjMag(int surface, int nX, int nY, int nZ);
//float DrawField_GetPolyObjRotateH(int surface, int nX, int nY, int nZ);
//float DrawField_GetPolyObjRotateP(int surface, int nX, int nY, int nZ);
//float DrawField_GetPolyObjRotateB(int surface, int nX, int nY, int nZ);
//float DrawField_GetPolyObjOffsetX(int surface, int nX, int nY, int nZ);
//float DrawField_GetPolyObjOffsetY(int surface, int nX, int nY, int nZ);
//float DrawField_GetPolyObjOffsetZ(int surface, int nX, int nY, int nZ);
//void DrawField_SetPolyObj(int surface, int nX, int nY, int nZ, int nPolyObj);
//void DrawField_SetPolyObjMag(int surface, int nX, int nY, int nZ, float fMag);
//void DrawField_SetPolyObjRotateH(int surface, int nX, int nY, int nZ, float fRotateH);
//void DrawField_SetPolyObjRotateP(int surface, int nX, int nY, int nZ, float fRotateP);
//void DrawField_SetPolyObjRotateB(int surface, int nX, int nY, int nZ, float fRotateB);
//void DrawField_SetPolyObjOffsetX(int surface, int nX, int nY, int nZ, float fOffsetX);
//void DrawField_SetPolyObjOffsetY(int surface, int nX, int nY, int nZ, float fOffsetY);
//void DrawField_SetPolyObjOffsetZ(int surface, int nX, int nY, int nZ, float fOffsetZ);

static void DrawField_SetDrawObjFlag(int surface, int type, bool flag)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->renderer)
		return;
	dungeon_renderer_set_draw_obj_flag(ctx->renderer, type, flag);
}

static bool DrawField_GetDrawObjFlag(int surface, int type)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->renderer)
		return false;
	return dungeon_renderer_get_draw_obj_flag(ctx->renderer, type);
}

//int DrawField_GetNumofTexSound(int nSurface, int nType);

HLL_LIBRARY(DrawField,
	    HLL_EXPORT(Init, DrawField_Init),
	    HLL_EXPORT(Release, DrawDungeon_Release),
	    HLL_EXPORT(SetDrawFlag, DrawDungeon_SetDrawFlag),
	    HLL_EXPORT(GetDrawFlag, DrawDungeon_GetDrawFlag),
	    HLL_EXPORT(LoadFromFile, DrawDungeon_LoadFromFile),
	    HLL_EXPORT(LoadTexture, DrawDungeon_LoadTexture),
	    HLL_TODO_EXPORT(LoadEventTexture, DrawDungeon_LoadEventTexture),
	    HLL_TODO_EXPORT(BeginLoad, DrawField_BeginLoad),
	    HLL_TODO_EXPORT(IsLoadEnd, DrawField_IsLoadEnd),
	    HLL_TODO_EXPORT(StopLoad, DrawField_StopLoad),
	    HLL_TODO_EXPORT(GetLoadPercent, DrawField_GetLoadPercent),
	    HLL_TODO_EXPORT(IsLoadSucceeded, DrawField_IsLoadSucceeded),
	    HLL_TODO_EXPORT(BeginLoadTexture, DrawField_BeginLoadTexture),
	    HLL_TODO_EXPORT(IsLoadTextureEnd, DrawField_IsLoadTextureEnd),
	    HLL_TODO_EXPORT(StopLoadTexture, DrawField_StopLoadTexture),
	    HLL_TODO_EXPORT(GetLoadTexturePercent, DrawField_GetLoadTexturePercent),
	    HLL_TODO_EXPORT(IsLoadTextureSucceeded, DrawField_IsLoadTextureSucceeded),
	    HLL_TODO_EXPORT(UpdateTexture, DrawField_UpdateTexture),
	    HLL_EXPORT(SetCamera, dungeon_set_camera),
	    HLL_EXPORT(GetMapX, DrawDungeon_GetMapX),
	    HLL_EXPORT(GetMapY, DrawDungeon_GetMapY),
	    HLL_EXPORT(GetMapZ, DrawDungeon_GetMapZ),
	    HLL_EXPORT(IsInMap, DrawDungeon_IsInMap),
	    HLL_EXPORT(IsFloor, DrawDungeon_IsFloor),
	    HLL_EXPORT(IsStair, DrawDungeon_IsStair),
	    HLL_EXPORT(IsStairN, DrawDungeon_IsStairN),
	    HLL_EXPORT(IsStairW, DrawDungeon_IsStairW),
	    HLL_EXPORT(IsStairS, DrawDungeon_IsStairS),
	    HLL_EXPORT(IsStairE, DrawDungeon_IsStairE),
	    HLL_EXPORT(IsWallN, DrawDungeon_IsWallN),
	    HLL_EXPORT(IsWallW, DrawDungeon_IsWallW),
	    HLL_EXPORT(IsWallS, DrawDungeon_IsWallS),
	    HLL_EXPORT(IsWallE, DrawDungeon_IsWallE),
	    HLL_EXPORT(IsDoorN, DrawDungeon_IsDoorN),
	    HLL_EXPORT(IsDoorW, DrawDungeon_IsDoorW),
	    HLL_EXPORT(IsDoorS, DrawDungeon_IsDoorS),
	    HLL_EXPORT(IsDoorE, DrawDungeon_IsDoorE),
	    HLL_EXPORT(IsEnter, DrawDungeon_IsEnter),
	    HLL_EXPORT(IsEnterN, DrawDungeon_IsEnterN),
	    HLL_EXPORT(IsEnterW, DrawDungeon_IsEnterW),
	    HLL_EXPORT(IsEnterS, DrawDungeon_IsEnterS),
	    HLL_EXPORT(IsEnterE, DrawDungeon_IsEnterE),
	    HLL_EXPORT(GetEvFloor, DrawDungeon_GetEvFloor),
	    HLL_EXPORT(GetEvWallN, DrawDungeon_GetEvWallN),
	    HLL_EXPORT(GetEvWallW, DrawDungeon_GetEvWallW),
	    HLL_EXPORT(GetEvWallS, DrawDungeon_GetEvWallS),
	    HLL_EXPORT(GetEvWallE, DrawDungeon_GetEvWallE),
	    HLL_EXPORT(GetEvFloor2, DrawDungeon_GetEvFloor2),
	    HLL_EXPORT(GetEvWallN2, DrawDungeon_GetEvWallN2),
	    HLL_EXPORT(GetEvWallW2, DrawDungeon_GetEvWallW2),
	    HLL_EXPORT(GetEvWallS2, DrawDungeon_GetEvWallS2),
	    HLL_EXPORT(GetEvWallE2, DrawDungeon_GetEvWallE2),
	    HLL_EXPORT(GetTexFloor, DrawDungeon_GetTexFloor),
	    HLL_EXPORT(GetTexCeiling, DrawDungeon_GetTexCeiling),
	    HLL_EXPORT(GetTexWallN, DrawDungeon_GetTexWallN),
	    HLL_EXPORT(GetTexWallW, DrawDungeon_GetTexWallW),
	    HLL_EXPORT(GetTexWallS, DrawDungeon_GetTexWallS),
	    HLL_EXPORT(GetTexWallE, DrawDungeon_GetTexWallE),
	    HLL_EXPORT(GetTexDoorN, DrawDungeon_GetTexDoorN),
	    HLL_EXPORT(GetTexDoorW, DrawDungeon_GetTexDoorW),
	    HLL_EXPORT(GetTexDoorS, DrawDungeon_GetTexDoorS),
	    HLL_EXPORT(GetTexDoorE, DrawDungeon_GetTexDoorE),
	    HLL_TODO_EXPORT(GetDoorNAngle, DrawField_GetDoorNAngle),
	    HLL_TODO_EXPORT(GetDoorWAngle, DrawField_GetDoorWAngle),
	    HLL_TODO_EXPORT(GetDoorSAngle, DrawField_GetDoorSAngle),
	    HLL_TODO_EXPORT(GetDoorEAngle, DrawField_GetDoorEAngle),
	    HLL_TODO_EXPORT(SetDoorNAngle, DrawField_SetDoorNAngle),
	    HLL_TODO_EXPORT(SetDoorWAngle, DrawField_SetDoorWAngle),
	    HLL_TODO_EXPORT(SetDoorSAngle, DrawField_SetDoorSAngle),
	    HLL_TODO_EXPORT(SetDoorEAngle, DrawField_SetDoorEAngle),
	    HLL_EXPORT(SetEvFloor, DrawDungeon_SetEvFloor),
	    HLL_EXPORT(SetEvWallN, DrawDungeon_SetEvWallN),
	    HLL_EXPORT(SetEvWallW, DrawDungeon_SetEvWallW),
	    HLL_EXPORT(SetEvWallS, DrawDungeon_SetEvWallS),
	    HLL_EXPORT(SetEvWallE, DrawDungeon_SetEvWallE),
	    HLL_EXPORT(SetEvFloor2, DrawDungeon_SetEvFloor2),
	    HLL_EXPORT(SetEvWallN2, DrawDungeon_SetEvWallN2),
	    HLL_EXPORT(SetEvWallW2, DrawDungeon_SetEvWallW2),
	    HLL_EXPORT(SetEvWallS2, DrawDungeon_SetEvWallS2),
	    HLL_EXPORT(SetEvWallE2, DrawDungeon_SetEvWallE2),
	    HLL_TODO_EXPORT(SetEvMag, DrawDungeon_SetEvMag),
	    HLL_EXPORT(SetEvRate, DrawDungeon_SetEvRate),
	    HLL_EXPORT(SetEnter, DrawDungeon_SetEnter),
	    HLL_EXPORT(SetEnterN, DrawDungeon_SetEnterN),
	    HLL_EXPORT(SetEnterW, DrawDungeon_SetEnterW),
	    HLL_EXPORT(SetEnterS, DrawDungeon_SetEnterS),
	    HLL_EXPORT(SetEnterE, DrawDungeon_SetEnterE),
	    HLL_EXPORT(SetDoorNLock, DrawField_SetDoorNLock),
	    HLL_EXPORT(SetDoorWLock, DrawField_SetDoorWLock),
	    HLL_EXPORT(SetDoorSLock, DrawField_SetDoorSLock),
	    HLL_EXPORT(SetDoorELock, DrawField_SetDoorELock),
	    HLL_EXPORT(GetDoorNLock, DrawField_GetDoorNLock),
	    HLL_EXPORT(GetDoorWLock, DrawField_GetDoorWLock),
	    HLL_EXPORT(GetDoorSLock, DrawField_GetDoorSLock),
	    HLL_EXPORT(GetDoorELock, DrawField_GetDoorELock),
	    HLL_EXPORT(SetTexFloor, DrawDungeon_SetTexFloor),
	    HLL_EXPORT(SetTexCeiling, DrawDungeon_SetTexCeiling),
	    HLL_EXPORT(SetTexWallN, DrawDungeon_SetTexWallN),
	    HLL_EXPORT(SetTexWallW, DrawDungeon_SetTexWallW),
	    HLL_EXPORT(SetTexWallS, DrawDungeon_SetTexWallS),
	    HLL_EXPORT(SetTexWallE, DrawDungeon_SetTexWallE),
	    HLL_EXPORT(SetTexDoorN, DrawDungeon_SetTexDoorN),
	    HLL_EXPORT(SetTexDoorW, DrawDungeon_SetTexDoorW),
	    HLL_EXPORT(SetTexDoorS, DrawDungeon_SetTexDoorS),
	    HLL_EXPORT(SetTexDoorE, DrawDungeon_SetTexDoorE),
	    HLL_TODO_EXPORT(SetTexStair, DrawField_SetTexStair),
	    HLL_TODO_EXPORT(SetShadowTexFloor, DrawField_SetShadowTexFloor),
	    HLL_TODO_EXPORT(SetShadowTexCeiling, DrawField_SetShadowTexCeiling),
	    HLL_TODO_EXPORT(SetShadowTexWallN, DrawField_SetShadowTexWallN),
	    HLL_TODO_EXPORT(SetShadowTexWallW, DrawField_SetShadowTexWallW),
	    HLL_TODO_EXPORT(SetShadowTexWallS, DrawField_SetShadowTexWallS),
	    HLL_TODO_EXPORT(SetShadowTexWallE, DrawField_SetShadowTexWallE),
	    HLL_TODO_EXPORT(SetShadowTexDoorN, DrawField_SetShadowTexDoorN),
	    HLL_TODO_EXPORT(SetShadowTexDoorW, DrawField_SetShadowTexDoorW),
	    HLL_TODO_EXPORT(SetShadowTexDoorS, DrawField_SetShadowTexDoorS),
	    HLL_TODO_EXPORT(SetShadowTexDoorE, DrawField_SetShadowTexDoorE),
	    HLL_EXPORT(CalcShadowMap, DrawField_CalcShadowMap),
	    HLL_EXPORT(DrawMap, dungeon_map_draw),
	    HLL_EXPORT(SetMapAllViewFlag, dungeon_map_set_all_view),
	    HLL_EXPORT(SetDrawMapFloor, dungeon_map_set_small_map_floor),
	    HLL_EXPORT(DrawLMap, dungeon_map_draw_lmap),
	    HLL_EXPORT(SetMapCG, dungeon_map_set_cg),
	    HLL_EXPORT(SetDrawLMapFloor, dungeon_map_set_large_map_floor),
	    HLL_EXPORT(SetPlayerPos, dungeon_set_player_pos),
	    HLL_EXPORT(SetWalked, dungeon_set_walked),
	    HLL_EXPORT(SetLooked, DrawField_SetLooked),
	    HLL_EXPORT(SetHideDrawMapFloor, DrawField_SetHideDrawMapFloor),
	    HLL_TODO_EXPORT(SetHideDrawMapWall, DrawField_SetHideDrawMapWall),
	    HLL_EXPORT(SetDrawHalfFlag, DrawDungeon_SetDrawHalfFlag),
	    HLL_EXPORT(GetDrawHalfFlag, DrawDungeon_GetDrawHalfFlag),
	    HLL_EXPORT(SetInterlaceMode, DrawDungeon_SetInterlaceMode),
	    HLL_EXPORT(SetDirect3DMode, DrawDungeon_SetDirect3DMode),
	    HLL_EXPORT(GetDirect3DMode, DrawDungeon_GetDirect3DMode),
	    HLL_EXPORT(SaveDrawSettingFlag, DrawDungeon_SaveDrawSettingFlag),
	    HLL_EXPORT(SetPerspective, dungeon_set_perspective),
	    HLL_EXPORT(SetDrawShadowMap, DrawField_SetDrawShadowMap),
	    HLL_TODO_EXPORT(CalcNumofFloor, DrawField_CalcNumofFloor),
	    HLL_TODO_EXPORT(CalcNumofWalk, DrawField_CalcNumofWalk),
	    HLL_TODO_EXPORT(CalcNumofWalk2, DrawField_CalcNumofWalk2),
	    HLL_EXPORT(IsPVSData, DrawDungeon_IsPVSData),
	    HLL_EXPORT(CosDeg, DrawField_CosDeg),
	    HLL_EXPORT(SinDeg, DrawField_SinDeg),
	    HLL_EXPORT(TanDeg, DrawField_TanDeg),
	    HLL_EXPORT(Sqrt, sqrtf),
	    HLL_EXPORT(Atan2, DrawField_Atan2),
	    HLL_TODO_EXPORT(TransPos2DToPos3DOnPlane, DrawField_TransPos2DToPos3DOnPlane),
	    HLL_TODO_EXPORT(TransPos3DToPos2D, DrawField_TransPos3DToPos2D),
	    HLL_EXPORT(GetCharaNumMax, DrawField_GetCharaNumMax),
	    HLL_EXPORT(SetCharaSprite, dungeon_set_chara_sprite),
	    HLL_EXPORT(SetCharaPos, dungeon_set_chara_pos),
	    HLL_EXPORT(SetCharaCG, dungeon_set_chara_cg),
	    HLL_EXPORT(SetCharaCGInfo, dungeon_set_chara_cg_info),
	    HLL_TODO_EXPORT(SetCharaZBias, DrawField_SetCharaZBias),
	    HLL_EXPORT(SetCharaShow, dungeon_set_chara_show),
	    HLL_EXPORT(SetCenterOffsetY, DrawField_SetCenterOffsetY),
	    HLL_TODO_EXPORT(SetBuilBoard, DrawField_SetBuilBoard),
	    HLL_EXPORT(SetSphereTheta, DrawField_SetSphereTheta),
	    HLL_EXPORT(SetSphereColor, DrawField_SetSphereColor),
	    HLL_EXPORT(GetSphereTheta, DrawField_GetSphereTheta),
	    HLL_EXPORT(GetSphereColor, DrawField_GetSphereColor),
	    HLL_EXPORT(GetBackColor, DrawField_GetBackColor),
	    HLL_EXPORT(SetBackColor, DrawField_SetBackColor),
	    HLL_TODO_EXPORT(GetPolyObj, DrawField_GetPolyObj),
	    HLL_TODO_EXPORT(GetPolyObjMag, DrawField_GetPolyObjMag),
	    HLL_TODO_EXPORT(GetPolyObjRotateH, DrawField_GetPolyObjRotateH),
	    HLL_TODO_EXPORT(GetPolyObjRotateP, DrawField_GetPolyObjRotateP),
	    HLL_TODO_EXPORT(GetPolyObjRotateB, DrawField_GetPolyObjRotateB),
	    HLL_TODO_EXPORT(GetPolyObjOffsetX, DrawField_GetPolyObjOffsetX),
	    HLL_TODO_EXPORT(GetPolyObjOffsetY, DrawField_GetPolyObjOffsetY),
	    HLL_TODO_EXPORT(GetPolyObjOffsetZ, DrawField_GetPolyObjOffsetZ),
	    HLL_TODO_EXPORT(SetPolyObj, DrawField_SetPolyObj),
	    HLL_TODO_EXPORT(SetPolyObjMag, DrawField_SetPolyObjMag),
	    HLL_TODO_EXPORT(SetPolyObjRotateH, DrawField_SetPolyObjRotateH),
	    HLL_TODO_EXPORT(SetPolyObjRotateP, DrawField_SetPolyObjRotateP),
	    HLL_TODO_EXPORT(SetPolyObjRotateB, DrawField_SetPolyObjRotateB),
	    HLL_TODO_EXPORT(SetPolyObjOffsetX, DrawField_SetPolyObjOffsetX),
	    HLL_TODO_EXPORT(SetPolyObjOffsetY, DrawField_SetPolyObjOffsetY),
	    HLL_TODO_EXPORT(SetPolyObjOffsetZ, DrawField_SetPolyObjOffsetZ),
	    HLL_EXPORT(SetDrawObjFlag, DrawField_SetDrawObjFlag),
	    HLL_EXPORT(GetDrawObjFlag, DrawField_GetDrawObjFlag),
	    HLL_EXPORT(GetTexSound, DrawDungeon_GetTexSound),
	    HLL_TODO_EXPORT(GetNumofTexSound, DrawField_GetNumofTexSound)
	    );
