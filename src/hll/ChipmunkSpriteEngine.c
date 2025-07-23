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

#include "system4/cg.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "asset_manager.h"
#include "gfx/gfx.h"
#include "gfx/font.h"
#include "vm/page.h"
#include "input.h"
#include "sact.h"
#include "CharSpriteManager.h"
#include "xsystem4.h"
#include "hll.h"
#include "plugin.h"

static void ChipmunkSpriteEngine_PreLink(void);

static void ChipmunkSpriteEngine_ModuleFini(void)
{
	sact_ModuleFini();
	CharSpriteManager_Clear();
}

static int ChipmunkSpriteEngine_Init(possibly_unused void *imain_system)
{
	return sact_init(16, CHIPMUNK_SPRITE_ENGINE);
}

static int ChipmunkSpriteEngine_Init_with_size(possibly_unused void *imain_system, int cg_cache_size)
{
	return sact_init(cg_cache_size, CHIPMUNK_SPRITE_ENGINE);
}

static int ChipmunkSpriteEngine_SP_SetCG(int sp_no, struct string *cg_name)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_set_cg_by_name(sp, cg_name->text);
}

// XXX: Rance Quest
static int ChipmunkSpriteEngine_SP_SetCG_by_string_id(int sp_no, struct string *cg_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_set_cg_from_asset(sp, atoi(cg_no->text));
}

static int ChipmunkSpriteEngine_CG_IsExist(struct string *cg_name)
{
	return asset_exists_by_name(ASSET_CG, cg_name->text, NULL);
}

// XXX: Rance Quest
static int ChipmunkSpriteEngine_CG_IsExist_by_string_id(struct string *cg_no)
{
	return asset_exists(ASSET_CG, atoi(cg_no->text));
}

static void set_cg_metrics(struct cg_metrics *metrics, struct page **page)
{
	union vm_value *cgm = (*page)->values;
	cgm[0].i = metrics->w;
	cgm[1].i = metrics->h;
	cgm[2].i = metrics->bpp;
	cgm[3].i = metrics->has_pixel;
	cgm[4].i = metrics->has_alpha;
	cgm[5].i = metrics->pixel_pitch;
	cgm[6].i = metrics->alpha_pitch;
}

static int ChipmunkSpriteEngine_CG_GetMetrics(struct string *cg_name, struct page **page)
{
	struct cg_metrics metrics;
	if (!asset_cg_get_metrics_by_name(cg_name->text, &metrics))
		return 0;
	set_cg_metrics(&metrics, page);
	return 1;
}

// XXX: Rance Quest
static int ChipmunkSpriteEngine_CG_GetMetrics_by_string_id(struct string *cg_no, struct page **page)
{
	struct cg_metrics metrics;
	if (!asset_cg_get_metrics(atoi(cg_no->text), &metrics))
		return 0;
	set_cg_metrics(&metrics, page);
	return 1;
}

HLL_WARN_UNIMPLEMENTED(0, int,  ChipmunkSpriteEngine, SP_SetBrightness, int sp_no, int brightness);

HLL_WARN_UNIMPLEMENTED(0, int,  ChipmunkSpriteEngine, SP_GetBrightness, int sp_no);

static bool keep_previous_view = true;
HLL_WARN_UNIMPLEMENTED((keep_previous_view = on, true), bool, ChipmunkSpriteEngine, KeepPreviousView_SetMode, bool on);
HLL_WARN_UNIMPLEMENTED(keep_previous_view, bool, ChipmunkSpriteEngine, KeepPreviousView_GetMode);
HLL_QUIET_UNIMPLEMENTED(false, bool, ChipmunkSpriteEngine, KeepPreviousView);

static void ChipmunkSpriteEngine_Sleep(void)
{
	static int prev_time = 0;
	int this_time = vm_time();
	int delta = this_time - prev_time;
	prev_time = this_time;

	if (delta >= 12)
		return;
	SDL_Delay(12 - delta);
}

//static bool ChipmunkSpriteEngine_SP_SetCutCG(int sp_no, int cg_no, int cut_x, int cut_y, int cut_w, int cut_h);

static bool ChipmunkSpriteEngine_SP_SetMultipleColor(int sp_no, int r, int g, int b)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return false;
	sprite_set_multiply_color(sp, r, g, b);
	return true;
}

static bool ChipmunkSpriteEngine_SP_SetAddColor(int sp_no, int r, int g, int b, int a)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return false;
	sprite_set_add_color(sp, r, g, b);
	return true;
}

static bool ChipmunkSpriteEngine_SP_SetSurfaceArea(int sp_no, int x, int y, int w, int h)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return false;
	sprite_set_surface_area(sp, x, y, w, h);
	return true;
}

static bool ChipmunkSpriteEngine_SP_GetSurfaceArea(int sp_no, int *x, int *y, int *w, int *h)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return false;
	*x = sp->surface_area.x;
	*y = sp->surface_area.y;
	*w = sp->surface_area.w;
	*h = sp->surface_area.h;
	return true;
}

static const char transform_plugin_name[] = "ChipmunkSpriteEngine_Transform";

struct transform_plugin {
	struct draw_plugin p;
	struct gfx_vertex vertices[4];
};

static struct transform_plugin *get_transform_plugin(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp || !sp->plugin || sp->plugin->name != transform_plugin_name)
		return NULL;
	return (struct transform_plugin *)sp->plugin;
}

static void transform_plugin_free(struct draw_plugin *plugin)
{
	free(plugin);
}

static void transform_plugin_render(struct sact_sprite *sp)
{
	struct transform_plugin *plugin = (struct transform_plugin *)sp->plugin;
	gfx_render_quadrilateral(&sp->texture, plugin->vertices);
}

static bool ChipmunkSpriteEngine_SP_SetSpriteTransformMode(int sp_no, int transform_mode)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp)
		return false;
	bool current_mode = sp->plugin && sp->plugin->name == transform_plugin_name;
	if (current_mode && !transform_mode) {
		sprite_bind_plugin(sp, NULL);
	} else if (!current_mode && transform_mode) {
		struct transform_plugin *plugin = xcalloc(1, sizeof(struct transform_plugin));
		plugin->p.name = transform_plugin_name;
		plugin->p.free = transform_plugin_free;
		plugin->p.render = transform_plugin_render;
		sprite_bind_plugin(sp, &plugin->p);
	}
	return true;
}

static bool ChipmunkSpriteEngine_SP_SetSpriteTransformPos(
	int sp_no,
	float x0, float y0, float z0, float w0, float u0, float v0,
	float x1, float y1, float z1, float w1, float u1, float v1,
	float x2, float y2, float z2, float w2, float u2, float v2,
	float x3, float y3, float z3, float w3, float u3, float v3)
{
	struct transform_plugin *plugin = get_transform_plugin(sp_no);
	if (!plugin)
		return false;
	plugin->vertices[0] = (struct gfx_vertex){x0, y0, z0, w0, u0, v0};
	plugin->vertices[1] = (struct gfx_vertex){x1, y1, z1, w1, u1, v1};
	plugin->vertices[2] = (struct gfx_vertex){x2, y2, z2, w2, u2, v2};
	plugin->vertices[3] = (struct gfx_vertex){x3, y3, z3, w3, u3, v3};
	sprite_dirty(sact_try_get_sprite(sp_no));
	return true;
}

//static bool ChipmunkSpriteEngine_Sprite_SetTextureFilerType(int sp_no, int texture_filter_type);
//static int ChipmunkSpriteEngine_Sprite_GetTextureFilterType(int sp_no);

static bool ChipmunkSpriteEngine_SP_RenderView(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return false;

	scene_render();
	gfx_copy(sprite_get_texture(sp), 0, 0, gfx_main_surface(), 0, 0,
			sprite_get_width(sp), sprite_get_height(sp));

	return true;
}

//static bool ChipmunkSpriteEngine_SP_GetFontDescent(int *descent);

static bool ChipmunkSpriteEngine_SP_GetFontWidth(struct string *text, int *width)
{
	if (!text || !text->size) {
		*width = 0;
		return true;
	}

	*width = gfx_size_text(&text_sprite_ts, text->text);
	return true;
}

//static bool ChipmunkSpriteEngine_SP_SetAlphaClipperSpriteNumber(int sp_no, int ac_sp_no);
//static int ChipmunkSpriteEngine_SP_GetAlphaClipperSpriteNumber(int sp_no);
//static int CG_GetFormat_by_index(int cg_no);
//static int CG_GetFormat(struct string *cg_name);

static bool ChipmunkSpriteEngine_SP_Suspend(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return false;
	sp->suspended = true;
	return true;
}

static bool ChipmunkSpriteEngine_SP_Resume(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return false;
	sp->suspended = false;
	return true;
}

static bool ChipmunkSpriteEngine_SP_IsSuspend(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	return sp && sp->suspended;
}

//static bool ChipmunkSpriteEngine_Debug_GetVideoMemoryInfo(int *max_size, int *use_size, int *use_max_size);
//static void ChipmunkSpriteEngine_CombineTexture(void);
//static void ChipmunkSpriteEngine_ReleaseCombinedTexture(void);


HLL_LIBRARY(ChipmunkSpriteEngine,
	    HLL_EXPORT(_PreLink, ChipmunkSpriteEngine_PreLink),
	    HLL_EXPORT(_ModuleFini, ChipmunkSpriteEngine_ModuleFini),
	    HLL_EXPORT(Init, ChipmunkSpriteEngine_Init),
	    HLL_TODO_EXPORT(Error, SACT2_Error),
	    HLL_EXPORT(GetScreenWidth, sact_GetScreenWidth),
	    HLL_EXPORT(GetScreenHeight, sact_GetScreenHeight),
	    HLL_EXPORT(Update, sact_Update),
	    HLL_EXPORT(SP_GetUnuseNum, sact_SP_GetUnuseNum),
	    HLL_EXPORT(SP_Count, sact_SP_Count),
	    HLL_EXPORT(SP_Enum, sact_SP_Enum),
	    HLL_EXPORT(SP_SetCG, ChipmunkSpriteEngine_SP_SetCG),
	    HLL_TODO_EXPORT(SP_SetCutCG, ChipmunkSpriteEngine_SP_SetCutCG),
	    HLL_EXPORT(SP_SetCGFromFile, sact_SP_SetCGFromFile),
	    HLL_EXPORT(SP_SaveCG, sact_SP_SaveCG),
	    HLL_EXPORT(SP_Create, sact_SP_Create),
	    HLL_EXPORT(SP_CreatePixelOnly, sact_SP_CreatePixelOnly),
	    HLL_EXPORT(SP_CreateCustom, sact_SP_CreateCustom),
	    HLL_EXPORT(SP_Delete, sact_SP_Delete),
	    HLL_EXPORT(SP_DeleteAll, sact_SP_DeleteAll),
	    HLL_EXPORT(SP_SetPos, sact_SP_SetPos),
	    HLL_EXPORT(SP_SetX, sact_SP_SetX),
	    HLL_EXPORT(SP_SetY, sact_SP_SetY),
	    HLL_EXPORT(SP_SetZ, sact_SP_SetZ),
	    HLL_EXPORT(SP_SetBlendRate, sact_SP_SetBlendRate),
	    HLL_EXPORT(SP_SetMultipleColor, ChipmunkSpriteEngine_SP_SetMultipleColor),
	    HLL_EXPORT(SP_SetAddColor, ChipmunkSpriteEngine_SP_SetAddColor),
	    HLL_EXPORT(SP_SetShow, sact_SP_SetShow),
	    HLL_EXPORT(SP_SetDrawMethod, sact_SP_SetDrawMethod),
	    HLL_EXPORT(SP_SetSurfaceArea, ChipmunkSpriteEngine_SP_SetSurfaceArea),
	    HLL_EXPORT(SP_GetSurfaceArea, ChipmunkSpriteEngine_SP_GetSurfaceArea),
	    HLL_EXPORT(SP_SetSpriteTransformMode, ChipmunkSpriteEngine_SP_SetSpriteTransformMode),
	    HLL_EXPORT(SP_SetSpriteTransformPos, ChipmunkSpriteEngine_SP_SetSpriteTransformPos),
	    HLL_TODO_EXPORT(Sprite_SetTextureFilerType, ChipmunkSpriteEngine_Sprite_SetTextureFilerType),
	    HLL_TODO_EXPORT(Sprite_GetTextureFilterType, ChipmunkSpriteEngine_Sprite_GetTextureFilterType),
	    HLL_EXPORT(SP_IsUsing, sact_SP_IsUsing),
	    HLL_EXPORT(SP_ExistAlpha, sact_SP_ExistsAlpha),
	    HLL_EXPORT(SP_GetPosX, sact_SP_GetPosX),
	    HLL_EXPORT(SP_GetPosY, sact_SP_GetPosY),
	    HLL_EXPORT(SP_GetWidth, sact_SP_GetWidth),
	    HLL_EXPORT(SP_GetHeight, sact_SP_GetHeight),
	    HLL_EXPORT(SP_GetZ, sact_SP_GetZ),
	    HLL_EXPORT(SP_GetBlendRate, sact_SP_GetBlendRate),
	    HLL_EXPORT(SP_GetShow, sact_SP_GetShow),
	    HLL_EXPORT(SP_GetDrawMethod, sact_SP_GetDrawMethod),
	    HLL_EXPORT(SP_RenderView, ChipmunkSpriteEngine_SP_RenderView),
	    HLL_EXPORT(SP_IsPtIn, sact_SP_IsPtIn),
	    HLL_EXPORT(SP_IsPtInRect, sact_SP_IsPtInRect),
	    HLL_EXPORT(SP_SetTextSprite, StoatSpriteEngine_SP_SetTextSprite),
	    HLL_EXPORT(SP_SetTextSpriteType, StoatSpriteEngine_SP_SetTextSpriteType),
	    HLL_EXPORT(SP_SetTextSpriteSize, StoatSpriteEngine_SP_SetTextSpriteSize),
	    HLL_EXPORT(SP_SetTextSpriteColor, StoatSpriteEngine_SP_SetTextSpriteColor),
	    HLL_EXPORT(SP_SetTextSpriteBoldWeight, StoatSpriteEngine_SP_SetTextSpriteBoldWeight),
	    HLL_EXPORT(SP_SetTextSpriteEdgeWeight, StoatSpriteEngine_SP_SetTextSpriteEdgeWeight),
	    HLL_EXPORT(SP_SetTextSpriteEdgeColor, StoatSpriteEngine_SP_SetTextSpriteEdgeColor),
	    HLL_TODO_EXPORT(SP_GetFontDescent, ChipmunkSpriteEngine_SP_GetFontDescent),
	    HLL_EXPORT(SP_GetFontWidth, ChipmunkSpriteEngine_SP_GetFontWidth),
	    HLL_EXPORT(SP_SetDashTextSprite, StoatSpriteEngine_SP_SetDashTextSprite),
	    HLL_TODO_EXPORT(SP_SetAlphaClipperSpriteNumber, ChipmunkSpriteEngine_SP_SetAlphaClipperSpriteNumber),
	    HLL_TODO_EXPORT(SP_GetAlphaClipperSpriteNumber, ChipmunkSpriteEngine_SP_GetAlphaClipperSpriteNumber),
	    HLL_EXPORT(FPS_SetShow, StoatSpriteEngine_FPS_SetShow),
	    HLL_EXPORT(FPS_GetShow, StoatSpriteEngine_FPS_GetShow),
	    HLL_EXPORT(FPS_Get, StoatSpriteEngine_FPS_Get),
	    HLL_EXPORT(GAME_MSG_GetNumof, sact_GAME_MSG_GetNumOf),
	    HLL_TODO_EXPORT(GAME_MSG_Get, SACT2_GAME_MSG_Get),
	    HLL_EXPORT(IntToZenkaku, sact_IntToZenkaku),
	    HLL_EXPORT(IntToHankaku, sact_IntToHankaku),
	    HLL_TODO_EXPORT(StringPopFront, SACT2_StringPopFront),
	    HLL_EXPORT(Mouse_GetPos, sact_Mouse_GetPos),
	    HLL_EXPORT(Mouse_ClearWheel, mouse_clear_wheel),
	    HLL_EXPORT(Mouse_GetWheel, mouse_get_wheel),
	    HLL_EXPORT(Key_ClearFlag, sact_Key_ClearFlag),
	    HLL_EXPORT(Key_IsDown, sact_Key_IsDown),
	    HLL_EXPORT(KEY_GetState, key_is_down),
	    HLL_EXPORT(Timer_Get, vm_time),
	    HLL_EXPORT(CG_IsExist, ChipmunkSpriteEngine_CG_IsExist),
	    HLL_EXPORT(CG_GetMetrics, ChipmunkSpriteEngine_CG_GetMetrics),
	    HLL_TODO_EXPORT(CG_GetFormat, ChipmunkSpriteEngine_CG_GetFormat),
	    HLL_EXPORT(System_GetDate, get_date),
	    HLL_EXPORT(System_GetTime, get_time),
	    HLL_TODO_EXPORT(CG_RotateRGB, SACT2_CG_RotateRGB),
	    HLL_EXPORT(CG_BlendAMapBin, sact_CG_BlendAMapBin),
	    HLL_TODO_EXPORT(Debug_Pause, SACT2_Debug_Pause),
	    HLL_TODO_EXPORT(Debug_GetFuncStack, SACT2_Debug_GetFuncStack),
	    HLL_EXPORT(SP_GetAMapValue, sact_SP_GetAMapValue),
	    HLL_EXPORT(SP_GetPixelValue, sact_SP_GetPixelValue),
	    HLL_EXPORT(SP_SetBrightness, ChipmunkSpriteEngine_SP_SetBrightness),
	    HLL_EXPORT(SP_GetBrightness, ChipmunkSpriteEngine_SP_GetBrightness),
	    HLL_TODO_EXPORT(SP_CreateCopy, SACTDX_SP_CreateCopy),
	    HLL_EXPORT(Key_ClearFlagNoCtrl, sact_Key_ClearFlagNoCtrl),
	    HLL_TODO_EXPORT(Key_ClearFlagOne, SACTDX_Key_ClearFlagOne),
	    HLL_EXPORT(TRANS_Begin, sact_TRANS_Begin),
	    HLL_EXPORT(TRANS_Update, sact_TRANS_Update),
	    HLL_EXPORT(TRANS_End, sact_TRANS_End),
	    HLL_EXPORT(VIEW_SetMode, sact_VIEW_SetMode),
	    HLL_EXPORT(VIEW_GetMode, sact_VIEW_GetMode),
	    HLL_EXPORT(VIEW_SetOffsetPos, gfx_set_view_offset),
	    HLL_EXPORT(DX_GetUsePower2Texture, sact_DX_GetUsePower2Texture),
	    HLL_EXPORT(DX_SetUsePower2Texture, sact_DX_SetUsePower2Texture),
	    HLL_EXPORT(KeepPreviousView_SetMode, ChipmunkSpriteEngine_KeepPreviousView_SetMode),
	    HLL_EXPORT(KeepPreviousView_GetMode, ChipmunkSpriteEngine_KeepPreviousView_GetMode),
	    HLL_EXPORT(KeepPreviousView, ChipmunkSpriteEngine_KeepPreviousView),
	    HLL_EXPORT(Sleep, ChipmunkSpriteEngine_Sleep),
	    HLL_EXPORT(CharSpriteManager_CreateHandle, CharSpriteManager_CreateHandle),
	    HLL_EXPORT(CharSpriteManager_ReleaseHandle, CharSpriteManager_ReleaseHandle),
	    HLL_EXPORT(CharSpriteManager_Clear, CharSpriteManager_Clear),
	    HLL_EXPORT(CharSpriteManager_Rebuild, CharSpriteManager_Rebuild),
	    HLL_EXPORT(CharSpriteManager_Save, CharSpriteManager_Save),
	    HLL_EXPORT(CharSpriteManager_Load, CharSpriteManager_Load),
	    HLL_EXPORT(CharSprite_Release, CharSprite_Release),
	    HLL_EXPORT(CharSprite_SetChar, CharSprite_SetChar),
	    HLL_EXPORT(CharSprite_SetPos, CharSprite_SetPos),
	    HLL_EXPORT(CharSprite_SetZ, CharSprite_SetZ),
	    HLL_EXPORT(CharSprite_SetAlphaRate, CharSprite_SetAlphaRate),
	    HLL_EXPORT(CharSprite_SetShow, CharSprite_SetShow),
	    HLL_EXPORT(CharSprite_GetChar, CharSprite_GetChar),
	    HLL_EXPORT(CharSprite_GetAlphaRate, CharSprite_GetAlphaRate),
	    HLL_EXPORT(SYSTEM_IsResetOnce, StoatSpriteEngine_SYSTEM_IsResetOnce),
	    HLL_EXPORT(SYSTEM_SetConfigOverFrameRateSleep, StoatSpriteEngine_SYSTEM_SetConfigOverFrameRateSleep),
	    HLL_EXPORT(SYSTEM_GetConfigOverFrameRateSleep, StoatSpriteEngine_SYSTEM_GetConfigOverFrameRateSleep),
	    HLL_EXPORT(SYSTEM_SetConfigSleepByInactiveWindow, StoatSpriteEngine_SYSTEM_SetConfigSleepByInactiveWindow),
	    HLL_EXPORT(SYSTEM_GetConfigSleepByInactiveWindow, StoatSpriteEngine_SYSTEM_GetConfigSleepByInactiveWindow),
	    HLL_EXPORT(SYSTEM_SetReadMessageSkipping, StoatSpriteEngine_SYSTEM_SetReadMessageSkipping),
	    HLL_EXPORT(SYSTEM_GetReadMessageSkipping, StoatSpriteEngine_SYSTEM_GetReadMessageSkipping),
	    HLL_EXPORT(SYSTEM_SetConfigFrameSkipWhileMessageSkip, StoatSpriteEngine_SYSTEM_SetConfigFrameSkipWhileMessageSkip),
	    HLL_EXPORT(SYSTEM_GetConfigFrameSkipWhileMessageSkip, StoatSpriteEngine_SYSTEM_GetConfigFrameSkipWhileMessageSkip),
	    HLL_EXPORT(SYSTEM_SetInvalidateFrameSkipWhileMessageSkip, StoatSpriteEngine_SYSTEM_SetInvalidateFrameSkipWhileMessageSkip),
	    HLL_EXPORT(SYSTEM_GetInvalidateFrameSkipWhileMessageSkip, StoatSpriteEngine_SYSTEM_GetInvalidateFrameSkipWhileMessageSkip),
	    HLL_TODO_EXPORT(Debug_GetCurrentAllocatedMemorySize, StoatSpriteEngine_Debug_GetCurrentAllocatedMemorySize),
	    HLL_TODO_EXPORT(Debug_GetMaxAllocatedMemorySize, StoatSpriteEngine_Debug_GetMaxAllocatedMemorySize),
	    HLL_TODO_EXPORT(Debug_GetFillRate, StoatSpriteEngine_Debug_GetFillRate),
	    HLL_TODO_EXPORT(MUSIC_ReloadParam, StoatSpriteEngine_MUSIC_ReloadParam),
	    HLL_EXPORT(SP_Suspend, ChipmunkSpriteEngine_SP_Suspend),
	    HLL_EXPORT(SP_Resume, ChipmunkSpriteEngine_SP_Resume),
	    HLL_EXPORT(SP_IsSuspend, ChipmunkSpriteEngine_SP_IsSuspend),
	    HLL_TODO_EXPORT(Debug_GetVideoMemoryInfo, ChipmunkSpriteEngine_Debug_GetVideoMemoryInfo),
	    HLL_TODO_EXPORT(CombineTexture, ChipmunkSpriteEngine_CombineTexture),
	    HLL_TODO_EXPORT(ReleaseCombinedTexture, ChipmunkSpriteEngine_ReleaseCombinedTexture));

static struct ain_hll_function *get_fun(int libno, const char *name)
{
	int fno = ain_get_library_function(ain, libno, name);
	return fno >= 0 ? &ain->libraries[libno].functions[fno] : NULL;
}

// Daiteikoku and Shaman's Sanctuary have different interface than later versions
static void ChipmunkSpriteEngine_PreLink(void)
{
	struct ain_hll_function *fun;
	int libno = ain_get_library(ain, "ChipmunkSpriteEngine");
	assert(libno >= 0);

	fun = get_fun(libno, "Init");
	if (fun && fun->nr_arguments == 2) {
		static_library_replace(&lib_ChipmunkSpriteEngine, "Init", ChipmunkSpriteEngine_Init_with_size);
	}

	fun = get_fun(libno, "SP_SetCG");
	if (fun && game_rance8) {
		static_library_replace(&lib_ChipmunkSpriteEngine, "SP_SetCG",
				ChipmunkSpriteEngine_SP_SetCG_by_string_id);
	} else if (fun && fun->arguments[1].type.data == AIN_INT) {
		static_library_replace(&lib_ChipmunkSpriteEngine, "SP_SetCG", sact_SP_SetCG);
	}

	// TODO: SP_SetCutCG

	fun = get_fun(libno, "CG_IsExist");
	if (fun && game_rance8) {
		static_library_replace(&lib_ChipmunkSpriteEngine, "CG_IsExist",
				ChipmunkSpriteEngine_CG_IsExist_by_string_id);
	} else if (fun && fun->arguments[0].type.data == AIN_INT) {
		static_library_replace(&lib_ChipmunkSpriteEngine, "CG_IsExist", sact_CG_IsExist);
	}

	fun = get_fun(libno, "CG_GetMetrics");
	if (fun && game_rance8) {
		static_library_replace(&lib_ChipmunkSpriteEngine, "CG_GetMetrics",
				ChipmunkSpriteEngine_CG_GetMetrics_by_string_id);
	} else if (fun && fun->arguments[0].type.data == AIN_INT) {
		static_library_replace(&lib_ChipmunkSpriteEngine, "CG_GetMetrics", sact_CG_GetMetrics);
	}

	// TODO: CG_GetFormat
}
