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

#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/cg.h"
#include "system4/string.h"

#include "hll.h"
#include "asset_manager.h"
#include "audio.h"
#include "effect.h"
#include "input.h"
#include "queue.h"
#include "gfx/gfx.h"
#include "gfx/font.h"
#include "parts.h"
#include "sact.h"
#include "scene.h"
#include "vm/page.h"
#include "dungeon/dungeon.h"
#include "xsystem4.h"

static enum sprite_engine_type engine_type = UNINITIALIZED_SPRITE_ENGINE;
static struct sact_sprite **sprites = NULL;
static int nr_sprites = 0;
static int view_mode;
static bool use_power2_texture;
static int next_z2 = 0;

static struct sact_sprite *sact_alloc_sprite(int sp)
{
	sprites[sp] = xcalloc(1, sizeof(struct sact_sprite));
	sprites[sp]->no = sp;
	sprites[sp]->sp.z2 = engine_type == SACT2_SPRITE_ENGINE ? sp : ++next_z2;
	sprite_init(sprites[sp]);
	sprite_dirty(sprites[sp]);
	return sprites[sp];
}

static void sact_free_sprite(struct sact_sprite *sp)
{
	if (sprites[sp->no] == NULL)
		VM_ERROR("Double free of sact_sprite");
	sprites[sp->no] = NULL;
	sprite_free(sp);
	free(sp);
}

static void realloc_sprite_table(int n)
{
	int old_nr_sprites = nr_sprites;
	nr_sprites = n;

	struct sact_sprite **tmp = xrealloc(sprites-1, sizeof(struct sact_sprite*) * (nr_sprites+1));
	sprites = tmp + 1;

	memset(sprites + old_nr_sprites, 0, sizeof(struct sact_sprite*) * (nr_sprites - old_nr_sprites));
}

// NOTE: Used externally by DrawGraph, SengokuRanceFont and dungeon.c
struct sact_sprite *sact_get_sprite(int sp)
{
	if (sp < -1) {
		WARNING("Invalid sprite number: %d", sp);
		return NULL;
	}
	if (sp >= nr_sprites)
		realloc_sprite_table(sp+256);
	if (!sprites[sp]) {
		return sact_alloc_sprite(sp);
	}
	return sprites[sp];
}

struct sact_sprite *sact_try_get_sprite(int sp)
{
	if (sp < -1 || sp >= nr_sprites)
		return NULL;
	return sprites[sp];
}

int sact_init(possibly_unused int cg_cache_size, enum sprite_engine_type engine)
{
	if (engine_type != UNINITIALIZED_SPRITE_ENGINE) {
		if (engine_type != engine)
			VM_ERROR("sact_init() called with different engine type: %d != %d", engine_type, engine);
		// already initialized
		return 1;
	}
	engine_type = engine;

	gfx_init();
	gfx_font_init();
	audio_init();

	nr_sprites = 256;
	sprites = xmalloc(sizeof(struct sact_sprite*) * 257);
	memset(sprites, 0, sizeof(struct sact_sprite*) * 257);

	// create sprite for main_surface
	Texture *t = gfx_main_surface();
	struct sact_sprite *sp = sact_create_sprite(0, t->w, t->h, 0, 0, 0, 255);
	sp->texture = *t; // XXX: textures normally shouldn't be copied like this...
	sprite_set_show(sp, false);

	sprites++;

	// initialize sprite renderer
	if (engine == CHIPMUNK_SPRITE_ENGINE)
		sprite_init_chipmunk();
	else
		sprite_init_sact();

	return 1;
}

static int SACT2_Init(possibly_unused void *imain_system, int cg_cache_size)
{
	return sact_init(cg_cache_size, SACT2_SPRITE_ENGINE);
}

static int SACTDX_Init(possibly_unused void *imain_system, int cg_cache_size)
{
	return sact_init(cg_cache_size, SACTDX_SPRITE_ENGINE);
}

void sact_ModuleFini(void)
{
	sact_SP_DeleteAll();
	audio_reset();
}

//int SACT2_Error(struct string *err);
HLL_WARN_UNIMPLEMENTED(0, int, SACT2, WP_GetSP, int sp);

int sact_WP_SetSP(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp)
		return 0;
	if (!sp->texture.handle)
		return 0;
	return scene_set_wp_texture(&sp->texture);
}

int sact_GetScreenWidth(void)
{
	return config.view_width;
}

int sact_GetScreenHeight(void)
{
	return config.view_height;
}

int sact_GetMainSurfaceNumber(void)
{
	return -1;
}

int sact_Update(void)
{
	handle_events();
	sprite_call_plugins();
	if (scene_is_dirty) {
		scene_render();
		gfx_swap();
		scene_is_dirty = false;
	}
	return 1;
}

int sact_Effect(int type, int time, possibly_unused int key)
{
	uint32_t start = SDL_GetTicks();
	if (!effect_init(type))
		return 0;
	scene_render();

	uint32_t t = SDL_GetTicks() - start;
	while (t < time) {
		effect_update((float)t / (float)time);
		uint32_t t2 = SDL_GetTicks() - start;
		if (t2 < t + 16) {
			SDL_Delay(t + 16 - t2);
			t += 16;
		} else {
			t = t2;
		}
	}

	effect_fini();
	return 1;
}

HLL_WARN_UNIMPLEMENTED(1, int, SACT2, EffectSetMask, int cg);
//int SACT2_EffectSetMaskSP(int sp);

void sact_QuakeScreen(int amp_x, int amp_y, int time, int key)
{
	Texture tex;
	scene_render();
	gfx_copy_main_surface(&tex);

	Texture *dst = gfx_main_surface();

	for (int i = 0; i < time; i+= 16) {
		float rate = 1.0f - ((float)i / (float)time);
		int delta_x = amp_x ? (rand() % amp_x - amp_x/2) * rate : 0;
		int delta_y = amp_y ? (rand() % amp_y - amp_y/2) * rate : 0;
		gfx_clear();
		gfx_copy(dst, delta_x, delta_y, &tex, 0, 0, dst->w, dst->h);
		gfx_swap();
		SDL_Delay(16);
	}
}

//void SACT2_QUAKE_SET_CROSS(int amp_x, int amp_y);
//void SACT2_QUAKE_SET_ROTATION(int amp, int cycle);

int sact_SP_GetUnuseNum(int min)
{
	for (int i = min; i < nr_sprites; i++) {
		if (!sprites[i])
			return i;
	}

	int n = max(min, nr_sprites);
	realloc_sprite_table(n + 256);
	return n;
}

int sact_SP_Count(void)
{
	int count = 0;
	for (int i = 0; i < nr_sprites; i++) {
		if (sprites[i])
			count++;
	}
	return count;
}

int sact_SP_Enum(struct page **array)
{
	int count = 0;
	int size = (*array)->nr_vars;
	for (int i = 0; i < nr_sprites && count < size; i++) {
		if (sprites[i]) {
			(*array)->values[count++].i = i;
		}
	}
	return count;
}

int sact_SP_GetMaxZ(void)
{
	int max_z = 0;
	for (int i = 0; i < nr_sprites; i++) {
		if (sprites[i] && max_z < sprites[i]->sp.z)
			max_z = sprites[i]->sp.z;
	}
	return max_z;
}

int sact_SP_SetCG(int sp_no, int cg_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_set_cg_from_asset(sp, cg_no);
}

int sact_SP_SetCG2X(int sp_no, int cg_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_set_cg_2x_from_asset(sp, cg_no);
}

int sact_SP_SetCGFromFile(int sp_no, struct string *filename)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	char *path = gamedir_path(filename->text);
	int r = sprite_set_cg_from_file(sp, path);
	free(path);
	return r;
}

int sact_SP_SaveCG(int sp_no, struct string *filename)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	char *path = gamedir_path(filename->text);
	int r = sprite_save_cg(sp, path);
	free(path);
	return r;
}

struct sact_sprite *sact_create_sprite(int sp_no, int width, int height, int r, int g, int b, int a)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return NULL;
	sprite_init_color(sp, width, height, r, g, b, a);
	return sp;
}

int sact_SP_Create(int sp_no, int width, int height, int r, int g, int b, int a)
{
	return !!sact_create_sprite(sp_no, width, height, r, g, b, a);
}

int sact_SP_CreatePixelOnly(int sp_no, int width, int height)
{
	return !!sact_create_sprite(sp_no, width, height, 0, 0, 0, -1);
}

int sact_SP_CreateCustom(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_init_custom(sp);
	return true;
}

int sact_SP_Delete(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	sact_free_sprite(sp);
	return 1;
}

int sact_SP_DeleteAll(void)
{
	for (int i = 0; i < nr_sprites; i++) {
		if (sprites[i]) {
			sact_free_sprite(sprites[i]);
		}
	}
	next_z2 = 0;
	return 1;
}

int sact_SP_SetPos(int sp_no, int x, int y)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_set_pos(sp, x, y);
	return 1;
}

int sact_SP_SetX(int sp_no, int x)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_set_x(sp, x);
	return 1;
}

int sact_SP_SetY(int sp_no, int y)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_set_y(sp, y);
	return 1;
}

int sact_SP_SetZ(int sp_no, int z)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_set_z(sp, z);
	return 1;
}

int sact_SP_GetBlendRate(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_get_blend_rate(sp);
}

int sact_SP_SetBlendRate(int sp_no, int rate)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_set_blend_rate(sp, rate);
	return 1;
}

int sact_SP_SetShow(int sp_no, int show)
{
	if (sp_no < 0)
		VM_ERROR("Invalid sprite number: %d", sp_no);
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_set_show(sp, !!show);
	return 1;
}

int sact_SP_SetDrawMethod(int sp_no, int method)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	switch (method) {
	case 0: return sprite_set_draw_method(sp, DRAW_METHOD_NORMAL);
	case 1: return sprite_set_draw_method(sp, DRAW_METHOD_SCREEN);
	case 2: return sprite_set_draw_method(sp, DRAW_METHOD_MULTIPLY);
	default:
		WARNING("unknown draw method: %d", method);
		return 0;
	}
}

int sact_SP_GetDrawMethod(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return DRAW_METHOD_NORMAL;
	int method = sprite_get_draw_method(sp);
	switch (method) {
	case DRAW_METHOD_NORMAL: return 0;
	case DRAW_METHOD_SCREEN: return 1;
	case DRAW_METHOD_MULTIPLY: return 2;
	default:
		WARNING("unknown draw method: %d", method);
		return 0;
	}
}

int sact_SP_IsUsing(int sp_no)
{
	return sact_try_get_sprite(sp_no) != NULL;
}

int sact_SP_ExistsAlpha(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_exists_alpha(sp);
}

int sact_SP_GetPosX(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_get_pos_x(sp);
}

int sact_SP_GetPosY(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_get_pos_y(sp);
}

int sact_SP_GetWidth(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_get_width(sp);
}

int sact_SP_GetHeight(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_get_height(sp);
}

int sact_SP_GetZ(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_get_z(sp);
}

int sact_SP_GetShow(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_get_show(sp);
}

int sact_SP_SetTextHome(int sp_no, int x, int y)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_set_text_home(sp, x, y);
	return 1;
}

int sact_SP_SetTextLineSpace(int sp_no, int px)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_set_text_line_space(sp, px);
	return 1;
}

int sact_SP_SetTextCharSpace(int sp_no, int px)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_set_text_char_space(sp, px);
	return 1;
}

int sact_SP_SetTextPos(int sp_no, int x, int y)
{
	// XXX: In AliceSoft's implementation, this function succeeds even with
	//      a negative sp_no...
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_set_text_pos(sp, x, y);
	return 1;
}

static void sact_text_metrics_to_text_style(struct text_style *ts, union vm_value *_tm)
{
	*ts = (struct text_style) {
		.color = {
			.r = _tm[0].i,
			.g = _tm[1].i,
			.b = _tm[2].i,
			.a = 255
		},
		.edge_color = {
			.r = _tm[10].i,
			.g = _tm[11].i,
			.b = _tm[12].i,
			.a = 255
		},
		.size       = _tm[3].i,
		.weight     = _tm[4].i,
		.face       = _tm[5].i,
		.edge_left  = _tm[6].i,
		.edge_up    = _tm[7].i,
		.edge_right = _tm[8].i,
		.edge_down  = _tm[9].i,
		.bold_width = 0.0f,
		.scale_x = 1.0f,
		.space_scale_x = 1.0f,
		.font_spacing = 0.0f,
		.font_size = NULL
	};
}

int _sact_SP_TextDraw(int sp_no, struct string *text, struct text_style *ts)
{
	// XXX: In AliceSoft's implementation, this function succeeds even with
	//      a negative sp_no...
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp || !sp->rect.w || !sp->rect.h) return 0;
	sprite_text_draw(sp, text, ts);
	return 1;
}

int sact_SP_TextDraw(int sp_no, struct string *text, struct page *_tm)
{
	struct text_style ts;
	sact_text_metrics_to_text_style(&ts, _tm->values);
	_sact_SP_TextDraw(sp_no, text, &ts);
	return 1;
}

int sact_SP_TextClear(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 1; // always returns 1
	sprite_text_clear(sp);
	return 1;
}

int sact_SP_TextHome(int sp_no, int size)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_text_home(sp, size);
	return 1;
}

int sact_SP_TextNewLine(int sp_no, int size)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_text_new_line(sp, size);
	return 1;
}

//int SACT2_SP_TextBackSpace(int sp_no);

int sact_SP_TextCopy(int dno, int sno)
{
	// XXX: the dest sprite gets allocated even if this function returns
	//      0 because the source sprite doesn't exist
	struct sact_sprite *dsp = sact_try_get_sprite(dno);
	if (!dsp) {
		// FIXME? SP_GetWidth/SP_GetHeight return 0 in this case
		dsp = sact_create_sprite(dno, 1, 1, 0, 0, 0, 0);
		if (!dsp) {
			WARNING("Failed to create sprite: %d", dno);
			return 0;
		}
	}

	struct sact_sprite *ssp = sact_try_get_sprite(sno);
	if (!ssp)
		return 0;

	sprite_text_copy(dsp, ssp);
	return 1;
}

int sact_SP_GetTextHomeX(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_get_text_home_x(sp);
}

int sact_SP_GetTextHomeY(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_get_text_home_y(sp);
}

int sact_SP_GetTextCharSpace(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_get_text_char_space(sp);
}

int sact_SP_GetTextPosX(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_get_text_pos_x(sp);
}

int sact_SP_GetTextPosY(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_get_text_pos_y(sp);
}

int sact_SP_GetTextLineSpace(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_get_text_line_space(sp);
}

int sact_SP_IsPtIn(int sp_no, int x, int y)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_is_point_in(sp, x, y);
}

int sact_SP_IsPtInRect(int sp_no, int x, int y)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	return sprite_is_point_in_rect(sp, x, y);
}

int sact_CG_GetMetrics(int cg_no, struct page **page)
{
	union vm_value *cgm = (*page)->values;
	struct cg_metrics metrics;
	if (!asset_cg_get_metrics(cg_no, &metrics))
		return 0;
	cgm[0].i = metrics.w;
	cgm[1].i = metrics.h;
	cgm[2].i = metrics.bpp;
	cgm[3].i = metrics.has_pixel;
	cgm[4].i = metrics.has_alpha;
	cgm[5].i = metrics.pixel_pitch;
	cgm[6].i = metrics.alpha_pitch;
	return 1;
}

int sact_SP_GetAMapValue(int sp_no, int x, int y)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return -1;
	if (x < 0 || x >= sp->rect.w || y < 0 || y >= sp->rect.h)
		return -1;
	if (!sp->sp.has_alpha)
		return -1;
	return sprite_get_amap_value(sp, x, y);
}

int sact_SP_GetPixelValue(int sp_no, int x, int y, int *r, int *g, int *b)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_get_pixel_value(sp, x, y, r, g, b);
	return 1;
}

int sact_GAME_MSG_GetNumOf(void)
{
	return ain->nr_messages;
}

//void SACT2_GAME_MSG_Get(int index, struct string **text);

void sact_IntToZenkaku(struct string **s, int value, int figures, int zero_pad)
{
	int i;
	char buf[512];

	string_clear(*s);
	i = int_to_cstr(buf, 512, value, figures, zero_pad, true);
	string_append_cstr(s, buf, i);
}

void sact_IntToHankaku(struct string **s, int value, int figures, int zero_pad)
{
	int i;
	char buf[512];

	string_clear(*s);
	i = int_to_cstr(buf, 512, value, figures, zero_pad, false);
	string_append_cstr(s, buf, i);
}

//int SACT2_StringPopFront(struct string **dst, struct string **src);

int sact_Mouse_GetPos(int *x, int *y)
{
	handle_events();
	mouse_get_pos(x, y);
	return mouse_focus && keyboard_focus;
}

int sact_Mouse_SetPos(int x, int y)
{
	handle_events();
	mouse_set_pos(x, y);
	return mouse_focus && keyboard_focus;
}

void sact_Joypad_ClearKeyDownFlag(int n)
{
	joy_clear_flag();
}

int sact_Joypad_IsKeyDown(int num, int key)
{
	handle_events();
	return joy_key_is_down(key);
}

//int SACT2_Joypad_GetNumof(void);
HLL_WARN_UNIMPLEMENTED( , void, SACT2, JoypadQuake_Set, int num, int type, int magnitude);

bool sact_Joypad_GetAnalogStickStatus(int num, int type, float *degree, float *power)
{
	return joy_get_stick_status(num, type, degree, power);
}

bool sact_Joypad_GetDigitalStickStatus(int num, int type, bool *left, bool *right, bool *up, bool *down)
{
	//hll_unimplemented_warning(SACT2, Joypad_GetDigitalStickStatus);
	*left  = false;
	*right = false;
	*up    = false;
	*down  = false;
	return 1;
}

int sact_Key_ClearFlag(void)
{
	key_clear_flag(false);
	return 1;
}

void sact_Key_ClearFlagNoCtrl(void)
{
	key_clear_flag(true);
}

int sact_Key_IsDown(int keycode)
{
	handle_events();
	return key_is_down(keycode);
}

int sact_CG_IsExist(int cg_no)
{
	return asset_exists(ASSET_CG, cg_no);
}

//int  SACT2_CSV_Load(struct string *filename);
//int  SACT2_CSV_Save(void);
//int  SACT2_CSV_SaveAs(struct string *filename);
//int  SACT2_CSV_CountLines(void);
//int  SACT2_CSV_CountColumns(void);
//void SACT2_CSV_Get(struct string **s, int line, int column);
//int  SACT2_CSV_Set(int line, int column, struct string *data);
//int  SACT2_CSV_GetInt(int line, int column);
//void SACT2_CSV_SetInt(int line, int column, int data);
//void SACT2_CSV_Realloc(int lines, int columns);

//void SACT2_CG_RotateRGB(int dst, int dx, int dy, int w, int h, int rotate_type);

void sact_CG_BlendAMapBin(int dst, int dx, int dy, int src, int sx, int sy, int w, int h, int border)
{
	struct sact_sprite *dsp = sact_get_sprite(dst);
	struct sact_sprite *ssp = sact_get_sprite(src);
	if (!dsp) return;
	struct texture *dtx = sprite_get_texture(dsp);
	struct texture *stx = ssp ? sprite_get_texture(ssp) : NULL;
	gfx_copy_use_amap_border(dtx, dx, dy, stx, sx, sy, w, h, border);
	sprite_dirty(dsp);
}

//void SACT2_Debug_Pause(void);
//void SACT2_Debug_GetFuncStack(struct string **s, int nest);

int sact_SP_SetBrightness(int sp_no, int brightness)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_set_multiply_color(sp, brightness, brightness, brightness);
	sprite_dirty(sp);
	return 1;
}

int sact_SP_GetBrightness(int sp_no)
{
	struct sact_sprite *sp = sact_try_get_sprite(sp_no);
	if (!sp) return 0;
	return sp->multiply_color.r;
}

HLL_WARN_UNIMPLEMENTED(0, int, SACT2, Music_GetSampleData, int nCh, struct page **anDst, int nSamplePos);
HLL_WARN_UNIMPLEMENTED(0, int, SACT2, Music_MillisecondsToSamples, int nMillisec, int nSamplesPerSec);
HLL_WARN_UNIMPLEMENTED(0, int, SACT2, Music_GetFormat, int nCh, int *pnSamplesPerSec, int *pnBitsPerSample, int *pnChannels);
HLL_WARN_UNIMPLEMENTED(, void, SACT2, FFT_rdft, struct page **a);
HLL_WARN_UNIMPLEMENTED(, void, SACT2, FFT_hanning_window, struct page **a);
HLL_WARN_UNIMPLEMENTED(0, int, SACT2, Music_AnalyzeSampleData, struct page **l, struct page **r, struct page **src, int chns, int bps);

#define SACT_EXPORTS \
	    HLL_EXPORT(_ModuleFini, sact_ModuleFini), \
	    HLL_TODO_EXPORT(Error, SACT2_Error), \
	    HLL_EXPORT(SetWP, sact_SetWP), \
	    HLL_EXPORT(SetWP_Color, sact_SetWP_Color), \
	    HLL_EXPORT(WP_GetSP, SACT2_WP_GetSP), \
	    HLL_EXPORT(WP_SetSP, sact_WP_SetSP), \
	    HLL_EXPORT(GetScreenWidth, sact_GetScreenWidth), \
	    HLL_EXPORT(GetScreenHeight, sact_GetScreenHeight), \
	    HLL_EXPORT(GetMainSurfaceNumber, sact_GetMainSurfaceNumber), \
	    HLL_EXPORT(Update, sact_Update), \
	    HLL_EXPORT(Effect, sact_Effect), \
	    HLL_EXPORT(EffectSetMask, SACT2_EffectSetMask), \
	    HLL_TODO_EXPORT(EffectSetMaskSP, SACT2_EffectSetMaskSP), \
	    HLL_EXPORT(QuakeScreen, sact_QuakeScreen), \
	    HLL_TODO_EXPORT(QUAKE_SET_CROSS, SACT2_QUAKE_SET_CROSS), \
	    HLL_TODO_EXPORT(QUAKE_SET_ROTATION, SACT2_QUAKE_SET_ROTATION), \
	    HLL_EXPORT(SP_GetUnuseNum, sact_SP_GetUnuseNum), \
	    HLL_EXPORT(SP_Count, sact_SP_Count), \
	    HLL_EXPORT(SP_Enum, sact_SP_Enum), \
	    HLL_EXPORT(SP_GetMaxZ, sact_SP_GetMaxZ), \
	    HLL_EXPORT(SP_SetCG, sact_SP_SetCG), \
	    HLL_EXPORT(SP_SetCGFromFile, sact_SP_SetCGFromFile), \
	    HLL_EXPORT(SP_SaveCG, sact_SP_SaveCG), \
	    HLL_EXPORT(SP_Create, sact_SP_Create), \
	    HLL_EXPORT(SP_CreatePixelOnly, sact_SP_CreatePixelOnly), \
	    HLL_EXPORT(SP_CreateCustom, sact_SP_CreateCustom), \
	    HLL_EXPORT(SP_Delete, sact_SP_Delete), \
	    HLL_EXPORT(SP_SetPos, sact_SP_SetPos), \
	    HLL_EXPORT(SP_SetX, sact_SP_SetX), \
	    HLL_EXPORT(SP_SetY, sact_SP_SetY), \
	    HLL_EXPORT(SP_SetZ, sact_SP_SetZ), \
	    HLL_EXPORT(SP_SetBlendRate, sact_SP_SetBlendRate), \
	    HLL_EXPORT(SP_SetShow, sact_SP_SetShow), \
	    HLL_EXPORT(SP_SetDrawMethod, sact_SP_SetDrawMethod), \
	    HLL_EXPORT(SP_IsUsing, sact_SP_IsUsing), \
	    HLL_EXPORT(SP_ExistAlpha, sact_SP_ExistsAlpha), \
	    HLL_EXPORT(SP_GetPosX, sact_SP_GetPosX), \
	    HLL_EXPORT(SP_GetPosY, sact_SP_GetPosY), \
	    HLL_EXPORT(SP_GetWidth, sact_SP_GetWidth), \
	    HLL_EXPORT(SP_GetHeight, sact_SP_GetHeight), \
	    HLL_EXPORT(SP_GetZ, sact_SP_GetZ), \
	    HLL_EXPORT(SP_GetBlendRate, sact_SP_GetBlendRate), \
	    HLL_EXPORT(SP_GetShow, sact_SP_GetShow), \
	    HLL_EXPORT(SP_GetDrawMethod, sact_SP_GetDrawMethod), \
	    HLL_EXPORT(SP_SetTextHome, sact_SP_SetTextHome), \
	    HLL_EXPORT(SP_SetTextLineSpace, sact_SP_SetTextLineSpace), \
	    HLL_EXPORT(SP_SetTextCharSpace, sact_SP_SetTextCharSpace), \
	    HLL_EXPORT(SP_SetTextPos, sact_SP_SetTextPos), \
	    HLL_EXPORT(SP_TextDraw, sact_SP_TextDraw), \
	    HLL_EXPORT(SP_TextClear, sact_SP_TextClear), \
	    HLL_EXPORT(SP_TextHome, sact_SP_TextHome), \
	    HLL_EXPORT(SP_TextNewLine, sact_SP_TextNewLine), \
	    HLL_TODO_EXPORT(SP_TextBackSpace, SACT2_SP_TextBackSpace), \
	    HLL_EXPORT(SP_TextCopy, sact_SP_TextCopy), \
	    HLL_EXPORT(SP_GetTextHomeX, sact_SP_GetTextHomeX), \
	    HLL_EXPORT(SP_GetTextHomeY, sact_SP_GetTextHomeY), \
	    HLL_EXPORT(SP_GetTextCharSpace, sact_SP_GetTextCharSpace), \
	    HLL_EXPORT(SP_GetTextPosX, sact_SP_GetTextPosX), \
	    HLL_EXPORT(SP_GetTextPosY, sact_SP_GetTextPosY), \
	    HLL_EXPORT(SP_GetTextLineSpace, sact_SP_GetTextLineSpace), \
	    HLL_EXPORT(SP_IsPtIn, sact_SP_IsPtIn), \
	    HLL_EXPORT(SP_IsPtInRect, sact_SP_IsPtInRect), \
	    HLL_EXPORT(GAME_MSG_GetNumof, sact_GAME_MSG_GetNumOf), \
	    HLL_TODO_EXPORT(GAME_MSG_Get, SACT2_GAME_MSG_Get), \
	    HLL_EXPORT(IntToZenkaku, sact_IntToZenkaku), \
	    HLL_EXPORT(IntToHankaku, sact_IntToHankaku), \
	    HLL_TODO_EXPORT(StringPopFront, SACT2_StringPopFront), \
	    HLL_EXPORT(Mouse_GetPos, sact_Mouse_GetPos), \
	    HLL_EXPORT(Mouse_SetPos, sact_Mouse_SetPos), \
	    HLL_EXPORT(Mouse_ClearWheel, mouse_clear_wheel), \
	    HLL_EXPORT(Mouse_GetWheel, mouse_get_wheel), \
	    HLL_EXPORT(Joypad_ClearKeyDownFlag, sact_Joypad_ClearKeyDownFlag), \
	    HLL_EXPORT(Joypad_IsKeyDown, sact_Joypad_IsKeyDown), \
	    HLL_TODO_EXPORT(Joypad_GetNumof, SACT2_Joypad_GetNumof), \
	    HLL_EXPORT(JoypadQuake_Set, SACT2_JoypadQuake_Set), \
	    HLL_EXPORT(Joypad_GetAnalogStickStatus, sact_Joypad_GetAnalogStickStatus), \
	    HLL_EXPORT(Joypad_GetDigitalStickStatus, sact_Joypad_GetDigitalStickStatus), \
	    HLL_EXPORT(Key_ClearFlag, sact_Key_ClearFlag), \
	    HLL_EXPORT(Key_IsDown, sact_Key_IsDown), \
	    HLL_EXPORT(Timer_Get, vm_time), \
	    HLL_EXPORT(CG_IsExist, sact_CG_IsExist), \
	    HLL_EXPORT(CG_GetMetrics, sact_CG_GetMetrics), \
	    HLL_TODO_EXPORT(CSV_Load, SACT2_CSV_Load), \
	    HLL_TODO_EXPORT(CSV_Save, SACT2_CSV_Save), \
	    HLL_TODO_EXPORT(CSV_SaveAs, SACT2_CSV_SaveAs), \
	    HLL_TODO_EXPORT(CSV_CountLines, SACT2_CSV_CountLines), \
	    HLL_TODO_EXPORT(CSV_CountColumns, SACT2_CSV_CountColumns), \
	    HLL_TODO_EXPORT(CSV_Get, SACT2_CSV_Get), \
	    HLL_TODO_EXPORT(CSV_Set, SACT2_CSV_Set), \
	    HLL_TODO_EXPORT(CSV_GetInt, SACT2_CSV_GetInt), \
	    HLL_TODO_EXPORT(CSV_SetInt, SACT2_CSV_SetInt), \
	    HLL_TODO_EXPORT(CSV_Realloc, SACT2_CSV_Realloc), \
	    HLL_EXPORT(Music_IsExist, bgm_exists), \
	    HLL_EXPORT(Music_Prepare, bgm_prepare), \
	    HLL_EXPORT(Music_Unprepare, bgm_unprepare), \
	    HLL_EXPORT(Music_Play, bgm_play), \
	    HLL_EXPORT(Music_Stop, bgm_stop), \
	    HLL_EXPORT(Music_IsPlay, bgm_is_playing), \
	    HLL_EXPORT(Music_SetLoopCount, bgm_set_loop_count), \
	    HLL_EXPORT(Music_GetLoopCount, bgm_get_loop_count), \
	    HLL_EXPORT(Music_SetLoopStartPos, bgm_set_loop_start_pos), \
	    HLL_EXPORT(Music_SetLoopEndPos, bgm_set_loop_end_pos), \
	    HLL_EXPORT(Music_Fade, bgm_fade), \
	    HLL_EXPORT(Music_StopFade, bgm_stop_fade), \
	    HLL_EXPORT(Music_IsFade, bgm_is_fading), \
	    HLL_EXPORT(Music_Pause, bgm_pause), \
	    HLL_EXPORT(Music_Restart, bgm_restart), \
	    HLL_EXPORT(Music_IsPause, bgm_is_paused), \
	    HLL_EXPORT(Music_GetPos, bgm_get_pos), \
	    HLL_EXPORT(Music_GetLength, bgm_get_length), \
	    HLL_EXPORT(Music_GetSamplePos, bgm_get_sample_pos), \
	    HLL_EXPORT(Music_GetSampleLength, bgm_get_sample_length), \
	    HLL_EXPORT(Music_Seek, bgm_seek), \
	    HLL_EXPORT(Music_GetSampleData, SACT2_Music_GetSampleData), \
	    HLL_EXPORT(Music_MillisecondsToSamples, SACT2_Music_MillisecondsToSamples), \
	    HLL_EXPORT(Music_GetFormat, SACT2_Music_GetFormat), \
	    HLL_EXPORT(FFT_rdft, SACT2_FFT_rdft), \
	    HLL_EXPORT(FFT_hanning_window, SACT2_FFT_hanning_window), \
	    HLL_EXPORT(Music_AnalyzeSampleData, SACT2_Music_AnalyzeSampleData), \
	    HLL_EXPORT(Sound_IsExist, wav_exists), \
	    HLL_EXPORT(Sound_GetUnuseChannel, wav_get_unused_channel), \
	    HLL_EXPORT(Sound_Prepare, wav_prepare), \
	    HLL_EXPORT(Sound_Unprepare, wav_unprepare), \
	    HLL_EXPORT(Sound_Play, wav_play), \
	    HLL_EXPORT(Sound_Stop, wav_stop), \
	    HLL_EXPORT(Sound_IsPlay, wav_is_playing), \
	    HLL_EXPORT(Sound_SetLoopCount, wav_set_loop_count), \
	    HLL_EXPORT(Sound_GetLoopCount, wav_get_loop_count), \
	    HLL_EXPORT(Sound_Fade, wav_fade), \
	    HLL_EXPORT(Sound_StopFade, wav_stop_fade), \
	    HLL_EXPORT(Sound_IsFade, wav_is_fading), \
	    HLL_EXPORT(Sound_GetPos, wav_get_pos), \
	    HLL_EXPORT(Sound_GetLength, wav_get_length), \
	    HLL_EXPORT(Sound_ReverseLR, wav_reverse_LR), \
	    HLL_EXPORT(Sound_GetVolume, wav_get_volume), \
	    HLL_EXPORT(Sound_GetTimeLength, wav_get_time_length), \
	    HLL_EXPORT(Sound_GetGroupNum, wav_get_group_num), \
	    HLL_TODO_EXPORT(Sound_PrepareFromFile, SACT2_Sound_PrepareFromFile), \
	    HLL_EXPORT(System_GetDate, get_date), \
	    HLL_EXPORT(System_GetTime, get_time), \
	    HLL_TODO_EXPORT(CG_RotateRGB, SACT2_CG_RotateRGB), \
	    HLL_EXPORT(CG_BlendAMapBin, sact_CG_BlendAMapBin), \
	    HLL_TODO_EXPORT(Debug_Pause, SACT2_Debug_Pause), \
	    HLL_TODO_EXPORT(Debug_GetFuncStack, SACT2_Debug_GetFuncStack), \
	    HLL_EXPORT(SP_GetAMapValue, sact_SP_GetAMapValue), \
	    HLL_EXPORT(SP_GetPixelValue, sact_SP_GetPixelValue), \
	    HLL_EXPORT(SP_SetBrightness, sact_SP_SetBrightness), \
	    HLL_EXPORT(SP_GetBrightness, sact_SP_GetBrightness)

HLL_WARN_UNIMPLEMENTED( , void, SACTDX, SetVolumeMixerMasterGroupNum, int n);
HLL_WARN_UNIMPLEMENTED( , void, SACTDX, SetVolumeMixerSEGroupNum, int n);
HLL_WARN_UNIMPLEMENTED( , void, SACTDX, SetVolumeMixerBGMGroupNum, int n);
//static int SACTDX_SP_CreateCopy(int nSP, int nSrcSp);
//static bool SACTDX_Joypad_GetAnalogStickStatus(int nNum, int nType, ref float pfDegree, ref float pfPower);
//static bool SACTDX_Joypad_GetDigitalStickStatus(int nNum, int nType, ref bool pbLeft, ref bool pbRight, ref bool pbUp, ref bool pbDown);
//static void SACTDX_Key_ClearFlagOne(int nKeyCode);

int sact_TRANS_Begin(int type)
{
	if (!effect_init(type))
		return 0;
	return 1;
}

int sact_TRANS_Update(float rate)
{
	if (scene_is_dirty)
		scene_render();
	return effect_update(rate);
}

int sact_TRANS_End(void)
{
	return effect_fini();
}

bool sact_VIEW_SetMode(int mode)
{
	view_mode = mode;
	return true;
}

int sact_VIEW_GetMode(void)
{
	return view_mode;
}

bool sact_DX_GetUsePower2Texture(void)
{
	return use_power2_texture;
}

void sact_DX_SetUsePower2Texture(bool use)
{
	use_power2_texture = use;
}

#define SACT2_EXPORTS \
	HLL_EXPORT(Init, SACT2_Init), \
	HLL_EXPORT(Key_ClearFlagNoCtrl, sact_Key_ClearFlagNoCtrl)

#define SACTDX_EXPORTS \
	HLL_EXPORT(Init, SACTDX_Init), \
	HLL_EXPORT(SetVolumeMixerMasterGroupNum, SACTDX_SetVolumeMixerMasterGroupNum), \
	HLL_EXPORT(SetVolumeMixerSEGroupNum, SACTDX_SetVolumeMixerSEGroupNum), \
	HLL_EXPORT(SetVolumeMixerBGMGroupNum, SACTDX_SetVolumeMixerBGMGroupNum), \
	HLL_EXPORT(Sound_GetGroupNumFromDataNum, wav_get_group_num_from_data_num), \
	HLL_EXPORT(SP_DeleteAll, sact_SP_DeleteAll),	\
	HLL_TODO_EXPORT(SP_CreateCopy, SACTDX_SP_CreateCopy),	\
	HLL_TODO_EXPORT(Joypad_GetAnalogStickStatus, SACTDX_Joypad_GetAnalogStickStatus), \
	HLL_TODO_EXPORT(GetDigitalStickStatus, SACTDX_GetDigitalStickStatus), \
	HLL_EXPORT(Key_ClearFlagNoCtrl, sact_Key_ClearFlagNoCtrl), \
	HLL_TODO_EXPORT(Key_ClearFlagOne, SACTDX_Key_ClearFlagOne), \
	HLL_EXPORT(TRANS_Begin, sact_TRANS_Begin),	    \
	HLL_EXPORT(TRANS_Update, sact_TRANS_Update),	    \
	HLL_EXPORT(TRANS_End, sact_TRANS_End),	\
	HLL_EXPORT(VIEW_SetMode, sact_VIEW_SetMode),	\
	HLL_EXPORT(VIEW_GetMode, sact_VIEW_GetMode),	\
	HLL_EXPORT(DX_GetUsePower2Texture, sact_DX_GetUsePower2Texture),	\
	HLL_EXPORT(DX_SetUsePower2Texture, sact_DX_SetUsePower2Texture)

HLL_LIBRARY(SACT2, SACT_EXPORTS, SACT2_EXPORTS);
HLL_LIBRARY(SACTDX, SACT_EXPORTS, SACTDX_EXPORTS);
