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

#include "system4.h"
#include "sact.h"
#include "cg.h"
#include "input.h"
#include "sdl_core.h"
#include "vm.h"
#include "vm_string.h"
#include "utfsjis.h"

static struct sact_sprite **sprites = NULL;
static int nr_sprites = 0;

TAILQ_HEAD(listhead, sact_sprite) sprite_list =
	TAILQ_HEAD_INITIALIZER(sprite_list);

struct sact_sprite *sact_get_sprite(int sp)
{
	if (sp < 0 || sp >= nr_sprites)
		return NULL;
	return sprites[sp];
}

struct sact_sprite *get_surface(int no)
{
	struct sact_sprite *sp = sact_get_sprite(no);
	if (!sp)
		ERROR("Invalid sprite number: %d", no);
	if (!sp->cg->s)
		sp->cg->s = sdl_create_surface(sp->rect.w, sp->rect.h, &sp->color);
	if (!sp->cg->t)
		sp->cg->t = sdl_surface_to_texture(sp->cg->s);
	return sp;
}

static struct sact_sprite *alloc_sprite(int sp)
{
	sprites[sp] = xcalloc(1, sizeof(struct sact_sprite));
	sprites[sp]->no = sp;
	return sprites[sp];
}

static void sprite_register(struct sact_sprite *sp)
{
	struct sact_sprite *p;
	TAILQ_FOREACH(p, &sprite_list, entry) {
		if (p->z > sp->z) {
			TAILQ_INSERT_BEFORE(p, sp, entry);
			return;
		}
	}
	TAILQ_INSERT_TAIL(&sprite_list, sp, entry);
}

static void sprite_unregister(struct sact_sprite *sp)
{
	TAILQ_REMOVE(&sprite_list, sp, entry);
}

int sact_Init(void)
{
	sdl_initialize();
	sdl_text_init();
	return 1;
}

int sact_Update(void)
{
	handle_events();

	sdl_render_clear();
	struct sact_sprite *p;
	TAILQ_FOREACH(p, &sprite_list, entry) {
		if (!p->show)
			continue;
		if (!p->cg->s) {
			p->cg->s = sdl_create_surface(p->rect.w, p->rect.h, &p->color);
			p->cg->t = sdl_surface_to_texture(p->cg->s);
		}
		sdl_render_texture(p->cg->t, &p->rect);
		if (p->text.t)
			sdl_render_texture(p->text.t, &p->rect);
	}
	sdl_render_present();
	return 1;
}

enum sact_effect {
	SACT_EFFECT_CROSSFADE              = 1,
	SACT_EFFECT_FADEOUT                = 2,
	SACT_EFFECT_FADEIN                 = 3,
	SACT_EFFECT_WHITEOUT               = 4,
	SACT_EFFECT_WHITEIN                = 5,
	SACT_EFFECT_CROSSFADE_MOSAIC       = 6,
	SACT_EFFECT_BLIND_DOWN             = 7,
	SACT_EFFECT_BLIND_LR               = 8,
	SACT_EFFECT_BLIND_DOWN_LR          = 9,
	SACT_EFFECT_ZOOM_BLEND_BLUR        = 10,
	SACT_EFFECT_LINEAR_BLUR            = 11,
	SACT_EFFECT_UP_DOWN_CROSSFADE      = 12,
	SACT_EFFECT_DOWN_UP_CROSSFADE      = 13,
	SACT_EFFECT_PENTAGRAM_IN_OUT       = 14,
	SACT_EFFECT_PENTAGRAM_OUT_IN       = 15,
	SACT_EFFECT_HEXAGRAM_IN_OUT        = 16,
	SACT_EFFECT_HEXAGRAM_OUT_IN        = 17,
	SACT_EFFECT_AMAP_CROSSFADE         = 18,
	SACT_EFFECT_VERTICAL_BAR_BLUR      = 19,
	SACT_EFFECT_ROTATE_OUT             = 20,
	SACT_EFFECT_ROTATE_IN              = 21,
	SACT_EFFECT_ROTATE_OUT_CW          = 22,
	SACT_EFFECT_ROTATE_IN_CW           = 23,
	SACT_EFFECT_BLOCK_DISSOLVE         = 24,
	SACT_EFFECT_POLYGON_ROTATE_Y       = 25,
	SACT_EFFECT_POLYGON_ROTATE_Y_CW    = 26,
	SACT_EFFECT_OSCILLATE              = 27,
	SACT_EFFECT_POLYGON_ROTATE_X_CW    = 28,
	SACT_EFFECT_POLYGON_ROTATE_X       = 29,
	SACT_EFFECT_ROTATE_ZOOM_BLEND_BLUR = 30,
	SACT_EFFECT_ZIGZAG_CROSSFADE       = 31,
	SACT_EFFECT_TV_SWITCH_OFF          = 32,
	SACT_EFFECT_TV_SWITCH_ON           = 33,
	SACT_EFFECT_POLYGON_EXPLOSION      = 34,
	SACT_EFFECT_NOISE_CROSSFADE        = 35,
	SACT_EFFECT_TURN_PAGE              = 36,
	SACT_EFFECT_SEPIA_NOISE_CROSSFADE  = 37,
	SACT_EFFECT_CRUMPLED_PAPER_PULL    = 38,
	SACT_EFFECT_HORIZONTAL_ZIGZAG      = 39,
	SACT_EFFECT_LINEAR_BLUR_HD         = 40,
	SACT_EFFECT_VERTICAL_BAR_BLUR_HD   = 41,
	SACT_EFFECT_AMAP_CROSSFADE2        = 42,
	SACT_EFFECT_ZOOM_LR                = 43,
	SACT_EFFECT_ZOOR_RL                = 44,
	SACT_EFFECT_CROSSFADE_LR           = 45,
	SACT_EFFECT_CROSSFADE_RL           = 46,
	SACT_EFFECT_PIXEL_EXPLOSION        = 47,
	SACT_EFFECT_ZOOM_IN_CROSSFADE      = 48,
	SACT_EFFECT_PIXEL_DROP             = 49,
	SACT_EFFECT_BLUR_FADEOUT           = 50,
	SACT_EFFECT_BLUR_CROSSFADE         = 51,
	SACT_EFFECT_2ROT_ZOOM_BLEND_BLUR   = 52,
	SACT_NR_EFFECTS
};

static const char *sact_effect_names[SACT_NR_EFFECTS] = {
	[SACT_EFFECT_CROSSFADE]              = "SACT_EFFECT_CROSSFADE",
	[SACT_EFFECT_FADEOUT]                = "SACT_EFFECT_FADEOUT",
	[SACT_EFFECT_FADEIN]                 = "SACT_EFFECT_FADEIN",
	[SACT_EFFECT_WHITEOUT]               = "SACT_EFFECT_WHITEOUT",
	[SACT_EFFECT_WHITEIN]                = "SACT_EFFECT_WHITEIN",
	[SACT_EFFECT_CROSSFADE_MOSAIC]       = "SACT_EFFECT_CROSSFADE_MOSAIC",
	[SACT_EFFECT_BLIND_DOWN]             = "SACT_EFFECT_BLIND_DOWN",
	[SACT_EFFECT_BLIND_LR]               = "SACT_EFFECT_BLIND_LR",
	[SACT_EFFECT_BLIND_DOWN_LR]          = "SACT_EFFECT_BLIND_DOWN_LR",
	[SACT_EFFECT_ZOOM_BLEND_BLUR]        = "SACT_EFFECT_ZOOM_BLEND_BLUR",
	[SACT_EFFECT_LINEAR_BLUR]            = "SACT_EFFECT_LINEAR_BLUR",
	[SACT_EFFECT_UP_DOWN_CROSSFADE]      = "SACT_EFFECT_UP_DOWN_CROSSFADE",
	[SACT_EFFECT_DOWN_UP_CROSSFADE]      = "SACT_EFFECT_DOWN_UP_CROSSFADE",
	[SACT_EFFECT_PENTAGRAM_IN_OUT]       = "SACT_EFFECT_PENTAGRAM_IN_OUT",
	[SACT_EFFECT_PENTAGRAM_OUT_IN]       = "SACT_EFFECT_PENTAGRAM_OUT_IN",
	[SACT_EFFECT_HEXAGRAM_IN_OUT]        = "SACT_EFFECT_HEXAGRAM_IN_OUT",
	[SACT_EFFECT_HEXAGRAM_OUT_IN]        = "SACT_EFFECT_HEXAGRAM_OUT_IN",
	[SACT_EFFECT_AMAP_CROSSFADE]         = "SACT_EFFECT_AMAP_CROSSFADE",
	[SACT_EFFECT_VERTICAL_BAR_BLUR]      = "SACT_EFFECT_VERTICAL_BAR_BLUR",
	[SACT_EFFECT_ROTATE_OUT]             = "SACT_EFFECT_ROTATE_OUT",
	[SACT_EFFECT_ROTATE_IN]              = "SACT_EFFECT_ROTATE_IN",
	[SACT_EFFECT_ROTATE_OUT_CW]          = "SACT_EFFECT_ROTATE_OUT_CW",
	[SACT_EFFECT_ROTATE_IN_CW]           = "SACT_EFFECT_ROTATE_IN_CW",
	[SACT_EFFECT_BLOCK_DISSOLVE]         = "SACT_EFFECT_BLOCK_DISSOLVE",
	[SACT_EFFECT_POLYGON_ROTATE_Y]       = "SACT_EFFECT_POLYGON_ROTATE_Y",
	[SACT_EFFECT_POLYGON_ROTATE_Y_CW]    = "SACT_EFFECT_POLYGON_ROTATE_Y_CW",
	[SACT_EFFECT_OSCILLATE]              = "SACT_EFFECT_OSCILLATE",
	[SACT_EFFECT_POLYGON_ROTATE_X_CW]    = "SACT_EFFECT_POLYGON_ROTATE_X_CW",
	[SACT_EFFECT_POLYGON_ROTATE_X]       = "SACT_EFFECT_POLYGON_ROTATE_X",
	[SACT_EFFECT_ROTATE_ZOOM_BLEND_BLUR] = "SACT_EFFECT_ROTATE_ZOOM_BLEND_BLUR",
	[SACT_EFFECT_ZIGZAG_CROSSFADE]       = "SACT_EFFECT_ZIGZAG_CROSSFADE",
	[SACT_EFFECT_TV_SWITCH_OFF]          = "SACT_EFFECT_TV_SWITCH_OFF",
	[SACT_EFFECT_TV_SWITCH_ON]           = "SACT_EFFECT_TV_SWITCH_ON",
	[SACT_EFFECT_POLYGON_EXPLOSION]      = "SACT_EFFECT_POLYGON_EXPLOSION",
	[SACT_EFFECT_NOISE_CROSSFADE]        = "SACT_EFFECT_NOISE_CROSSFADE",
	[SACT_EFFECT_TURN_PAGE]              = "SACT_EFFECT_TURN_PAGE",
	[SACT_EFFECT_SEPIA_NOISE_CROSSFADE]  = "SACT_EFFECT_SEPIA_NOISE_CROSSFADE",
	[SACT_EFFECT_CRUMPLED_PAPER_PULL]    = "SACT_EFFECT_CRUMPLED_PAPER_PULL",
	[SACT_EFFECT_HORIZONTAL_ZIGZAG]      = "SACT_EFFECT_HORIZONTAL_ZIGZAG",
	[SACT_EFFECT_LINEAR_BLUR_HD]         = "SACT_EFFECT_LINEAR_BLUR_HD",
	[SACT_EFFECT_VERTICAL_BAR_BLUR_HD]   = "SACT_EFFECT_VERTICAL_BAR_BLUR_HD",
	[SACT_EFFECT_AMAP_CROSSFADE2]        = "SACT_EFFECT_AMAP_CROSSFADE2",
	[SACT_EFFECT_ZOOM_LR]                = "SACT_EFFECT_ZOOM_LR",
	[SACT_EFFECT_ZOOR_RL]                = "SACT_EFFECT_ZOOR_RL",
	[SACT_EFFECT_CROSSFADE_LR]           = "SACT_EFFECT_CROSSFADE_LR",
	[SACT_EFFECT_CROSSFADE_RL]           = "SACT_EFFECT_CROSSFADE_RL",
	[SACT_EFFECT_PIXEL_EXPLOSION]        = "SACT_EFFECT_PIXEL_EXPLOSION",
	[SACT_EFFECT_ZOOM_IN_CROSSFADE]      = "SACT_EFFECT_ZOOM_IN_CROSSFADE",
	[SACT_EFFECT_PIXEL_DROP]             = "SACT_EFFECT_PIXEL_DROP",
	[SACT_EFFECT_BLUR_FADEOUT]           = "SACT_EFFECT_BLUR_FADEOUT",
	[SACT_EFFECT_BLUR_CROSSFADE]         = "SACT_EFFECT_BLUR_CROSSFADE",
	[SACT_EFFECT_2ROT_ZOOM_BLEND_BLUR]   = "SACT_EFFECT_2ROT_ZOOM_BLEND_BLUR",
};

int sact_Effect(int type, possibly_unused int time, possibly_unused int key)
{
	switch (type) {
	default:
		if (type < 0 || type >= SACT_NR_EFFECTS)
			WARNING("Invalid or unknown effect: %d", type);
		else
			WARNING("Unimplemented effect: %s", sact_effect_names[type]);
	}
	return 0;
}

int sact_SP_GetUnuseNum(int min)
{
	for (int i = min; i < nr_sprites; i++) {
		if (!sprites[i])
			return i;
	}

	int old_nr_sprites = nr_sprites;
	int n = max(min, nr_sprites);
	nr_sprites = n + 256;
	sprites = xrealloc(sprites, sizeof(struct sact_sprite*) * nr_sprites);
	memset(sprites + old_nr_sprites, 0, sizeof(struct sact_sprite*) * (nr_sprites - old_nr_sprites));
	return n;
}

int sact_SP_GetMaxZ(void)
{
	if (TAILQ_EMPTY(&sprite_list))
		return 0;
	return TAILQ_LAST(&sprite_list, listhead)->z;
}

int sact_SP_SetCG(int sp_no, int cg_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp)
		sact_SP_Create(sp_no, 1, 1, 0, 0, 0, 0);
	if (!(sp = sact_get_sprite(sp_no))) {
		WARNING("Failed to create sprite");
		return 0;
	}
	cg_reinit(sp->cg);
	if (!cg_load(sp->cg, cg_no - 1))
		return 0;
	sp->rect.w = sp->cg->s->w;
	sp->rect.h = sp->cg->s->h;
	return 1;

}

static struct sact_sprite *create_sprite(int sp_no, int width, int height, int r, int g, int b, int a)
{
	struct sact_sprite *sp;
	if (sp_no < 0)
		VM_ERROR("Invalid sprite number: %d", sp_no);
	if (!(sp = sact_get_sprite(sp_no)))
		sp = alloc_sprite(sp_no);
	sp->color = (SDL_Color) { .r = r, .g = g, .b = b, .a = (a >= 0 ? a : 255) };
	sp->rect.w = width;
	sp->rect.h = height;
	if (sp->cg) {
		cg_reinit(sp->cg);
	} else {
		sp->cg = cg_init();
		sprite_register(sp);
	}
	sp->cg->has_alpha = a >= 0;
	return sp;
}

int sact_SP_Create(int sp_no, int width, int height, int r, int g, int b, int a)
{
	create_sprite(sp_no, width, height, r, g, b, a);
	return 1;
}

int sact_SP_CreatePixelOnly(int sp_no, int width, int height)
{
	create_sprite(sp_no, width, height, 0, 0, 0, -1);
	return 1;
}

int sact_SP_Delete(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_unregister(sp);
	cg_free(sp->cg);
	free(sp);
	sprites[sp_no] = NULL;
	return 1;
}

int sact_SP_SetZ(int sp_no, int z)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_unregister(sp);
	sp->z = z;
	sprite_register(sp);
	return 1;

}

int sact_SP_GetBlendRate(int sp_no)
{
	uint8_t rate;
	struct sact_sprite *sp = get_surface(sp_no);
	if (!sp) return 0;
	SDL_GetTextureAlphaMod(sp->cg->t, &rate);
	return rate;
}

int sact_SP_SetBlendRate(int sp_no, int rate)
{
	struct sact_sprite *sp = get_surface(sp_no);
	if (!sp) return 0;
	rate = max(0, min(255, rate));
	SDL_SetTextureAlphaMod(sp->cg->t, rate);
	return 1;
}

enum {
	DRAW_METHOD_NORMAL = 0,
	DRAW_METHOD_SCREEN = 1,
	DRAW_METHOD_MULTIPLY = 2
};

int sact_SP_SetDrawMethod(int sp_no, int method)
{
	struct sact_sprite *sp = get_surface(sp_no);
	SDL_BlendMode mode;
	switch (method) {
	case DRAW_METHOD_NORMAL:
		mode = SDL_BLENDMODE_BLEND;
		break;
	case DRAW_METHOD_SCREEN:
		mode = SDL_BLENDMODE_ADD;
		break;
	case DRAW_METHOD_MULTIPLY:
		mode = SDL_BLENDMODE_MOD;
		break;
	default:
		WARNING("SACT.SP_SetDrawMethod: unknown draw method: %d", method);
		return 0;
	}
	SDL_SetTextureBlendMode(sp->cg->t, mode);
	return 1;
}

int sact_SP_GetDrawMethod(int sp_no)
{
	struct sact_sprite *sp = get_surface(sp_no);
	SDL_BlendMode mode;
	SDL_GetTextureBlendMode(sp->cg->t, &mode);
	switch (mode) {
	case SDL_BLENDMODE_NONE:
	case SDL_BLENDMODE_BLEND:
		return DRAW_METHOD_NORMAL;
	case SDL_BLENDMODE_ADD:
		return DRAW_METHOD_SCREEN;
	case SDL_BLENDMODE_MOD:
		return DRAW_METHOD_MULTIPLY;
	default:
		WARNING("Unknown blend mode: %d", mode);
		return DRAW_METHOD_NORMAL;
	}
}

int sact_SP_ExistsAlpha(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sp->cg->has_alpha;
}

static void init_text_metrics(struct text_metrics *tm, union vm_value *_tm)
{
	*tm = (struct text_metrics) {
		.color = {
			.r = _tm[0].i,
			.g = _tm[1].i,
			.b = _tm[2].i,
			.a = 255
		},
		.outline_color = {
			.r = _tm[10].i,
			.g = _tm[11].i,
			.b = _tm[12].i,
			.a = 255
		},
		.size          = _tm[3].i,
		.weight        = _tm[4].i,
		.face          = _tm[5].i,
		.outline_left  = _tm[6].i,
		.outline_up    = _tm[7].i,
		.outline_right = _tm[8].i,
		.outline_down  = _tm[9].i,
	};
}

int sact_SP_TextDraw(int sp_no, struct string *text, union vm_value *_tm)
{
	struct text_metrics tm;
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;

	init_text_metrics(&tm, _tm);

	if (!sp->text.surf) {
		SDL_Color c;
		if (tm.outline_left || tm.outline_right || tm.outline_up || tm.outline_down)
			c = tm.outline_color;
		else
			c = tm.color;
		c.a = 0;
		sp->text.surf = sdl_create_surface(sp->rect.w, sp->rect.h, &c);
		sp->text.t = sdl_create_texture(sp->rect.w, sp->rect.h);
	}

	sp->text.pos.x += sdl_render_text(sp->text.surf, sp->text.pos, text->text, &tm) + sp->text.char_space;
	sdl_update_texture(sp->text.t, sp->text.surf);
	return 1;
}

static void clear_text(struct sact_sprite *sp)
{
	if (sp->text.surf) {
		SDL_FreeSurface(sp->text.surf);
		SDL_DestroyTexture(sp->text.t);
		sp->text.surf = NULL;
		sp->text.t = NULL;
	}
}

int sact_SP_TextClear(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	clear_text(sp);
	return 1;
}

int sact_SP_TextCopy(int dno, int sno)
{
	struct sact_sprite *dsp = sact_get_sprite(dno);
	struct sact_sprite *ssp = sact_get_sprite(sno);
	if (!ssp)
		return 0;
	if (!dsp) {
		sact_SP_Create(dno, 1, 1, 0, 0, 0, 0);
		if (!(dsp = sact_get_sprite(dno))) {
			WARNING("Failed to create sprite");
			return 0;
		}
	}

	clear_text(dsp);
	if (ssp->text.surf) {
		SDL_Color c = { 0, 0, 0, 0 };
		Rectangle rect = RECT(0, 0, dsp->rect.w, dsp->rect.h);
		dsp->text.surf = sdl_create_surface(dsp->rect.w, dsp->rect.h, &c);
		dsp->text.t = sdl_create_texture(dsp->rect.w, dsp->rect.h);
		SDL_BlitSurface(ssp->text.surf, NULL, dsp->text.surf, &rect);
		sdl_update_texture(dsp->text.t, dsp->text.surf);
	}
	dsp->text.home = ssp->text.home;
	dsp->text.pos = ssp->text.pos;
	dsp->text.char_space = ssp->text.char_space;
	dsp->text.line_space = ssp->text.line_space;
	return 1;
}

int sact_SP_IsPtIn(int sp_no, int x, int y)
{
	bool in = false;
	Point p = POINT(x, y);
	struct sact_sprite *sp = get_surface(sp_no);
	if (SDL_PointInRect(&p, &sp->rect)) {
		uint8_t r, g, b, a;
		x -= sp->rect.x;
		y -= sp->rect.y;
		SDL_LockSurface(sp->cg->s);
		SDL_GetRGBA(*sdl_get_pixel(sp->cg->s, x, y), sp->cg->s->format, &r, &g, &b, &a);
		in = !!a;
		SDL_UnlockSurface(sp->cg->s);
	}
	return in;
}

int sact_CG_GetMetrics(int cg_no, union vm_value *cgm)
{
	struct cg_metrics metrics;
	if (!cg_get_metrics(cg_no, &metrics))
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

//
// --- DrawGraph.dll ---
//

void DrawGraph_Copy(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h)
{
	sdl_copy(get_surface(dno)->cg, dx, dy, get_surface(sno)->cg, sx, sy, w, h);
}

void DrawGraph_CopyBright(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h, int rate)
{
	sdl_copy_bright(get_surface(dno)->cg, dx, dy, get_surface(sno)->cg, sx, sy, w, h, rate);
}

void DrawGraph_CopyAMap(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h)
{
	sdl_copy_amap(get_surface(dno)->cg, dx, dy, get_surface(sno)->cg, sx, sy, w, h);
}

void DrawGraph_CopySprite(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h, int r, int g, int b)
{
	sdl_copy_sprite(get_surface(dno)->cg, dx, dy, get_surface(sno)->cg, sx, sy, w, h, r, g, b);
}

void DrawGraph_CopyUseAMapUnder(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h, int a_threshold)
{
	sdl_copy_use_amap_under(get_surface(dno)->cg, dx, dy, get_surface(sno)->cg, sx, sy, w, h, a_threshold);
}

void DrawGraph_CopyUseAMapBorder(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h, int a_threshold)
{
	sdl_copy_use_amap_border(get_surface(dno)->cg, dx, dy, get_surface(sno)->cg, sx, sy, w, h, a_threshold);
}

void DrawGraph_CopyAMapMax(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h)
{
	sdl_copy_amap_max(get_surface(dno)->cg, dx, dy, get_surface(sno)->cg, sx, sy, w, h);
}

void DrawGraph_CopyAMapMin(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h)
{
	sdl_copy_amap_min(get_surface(dno)->cg, dx, dy, get_surface(sno)->cg, sx, sy, w, h);
}

void DrawGraph_BlendAMap(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h)
{
	sdl_blend_amap(get_surface(dno)->cg, dx, dy, get_surface(sno)->cg, sx, sy, w, h);
}

void DrawGraph_BlendAMapAlpha(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h, int alpha)
{
	sdl_blend_amap_alpha(get_surface(dno)->cg, dx, dy, get_surface(sno)->cg, sx, sy, w, h, alpha);
}

void DrawGraph_Fill(int sp_no, int x, int y, int w, int h, int r, int g, int b)
{
	sdl_fill(get_surface(sp_no)->cg, x, y, w, h, r, g, b);
}

void DrawGraph_FillAlphaColor(int sp_no, int x, int y, int w, int h, int r, int g, int b, int a)
{
	sdl_fill_alpha_color(get_surface(sp_no)->cg, x, y, w, h, r, g, b, a);
}

void DrawGraph_FillAMap(int sp_no, int x, int y, int w, int h, int a)
{
	sdl_fill_amap(get_surface(sp_no)->cg, x, y, w, h, a);
}

void DrawGraph_AddDA_DAxSA(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h)
{
	sdl_add_da_daxsa(get_surface(dno)->cg, dx, dy, get_surface(sno)->cg, sx, sy, w, h);
}

void DrawGraph_CopyStretch(int dst, int dx, int dy, int dw, int dh, int src, int sx, int sy, int sw, int sh)
{
	sdl_copy_stretch(get_surface(dst)->cg, dx, dy, dw, dh, get_surface(src)->cg, sx, sy, sw, sh);
}

void DrawGraph_CopyStretchAMap(int dst, int dx, int dy, int dw, int dh, int src, int sx, int sy, int sw, int sh)
{
	sdl_copy_stretch_amap(get_surface(dst)->cg, dx, dy, dw, dh, get_surface(src)->cg, sx, sy, sw, sh);
}

void DrawGraph_CopyReverseLR(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h)
{
	sdl_copy_reverse_LR(get_surface(dno)->cg, dx, dy, get_surface(sno)->cg, sx, sy, w, h);
}

void DrawGraph_CopyReverseAMapLR(int dno, int dx, int dy, int sno, int sx, int sy, int w, int h)
{
	sdl_copy_reverse_amap_LR(get_surface(dno)->cg, dx, dy, get_surface(sno)->cg, sx, sy, w, h);
}
