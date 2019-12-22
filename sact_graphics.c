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
#include "gfx_core.h"
#include "vm.h"
#include "vm_string.h"
#include "utfsjis.h"

static struct sact_sprite *create_sprite(int sp_no, int width, int height, int r, int g, int b, int a);

static struct sact_sprite **sprites = NULL;
static int nr_sprites = 0;

// wallpaper
static struct sact_sprite wp;

TAILQ_HEAD(listhead, sact_sprite) sprite_list =
	TAILQ_HEAD_INITIALIZER(sprite_list);

struct sact_sprite *sact_get_sprite(int sp)
{
	if (sp < -1 || sp >= nr_sprites)
		return NULL;
	return sprites[sp];
}

struct texture *sact_get_texture(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp)
		return NULL;
	if (!sp->texture.handle) {
		gfx_init_texture_with_color(&sp->texture, sp->rect.w, sp->rect.h, sp->color);
	}
	return &sp->texture;
}

static struct sact_sprite *alloc_sprite(int sp)
{
	sprites[sp] = xcalloc(1, sizeof(struct sact_sprite));
	sprites[sp]->no = sp;
	return sprites[sp];
}

static void free_sprite(struct sact_sprite *sp)
{
	if (sprites[sp->no] == NULL)
		VM_ERROR("Double free of sact_sprite");
	sprites[sp->no] = NULL;
	gfx_delete_texture(&sp->texture);
	gfx_delete_texture(&sp->text.texture);
	free(sp);
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

static void realloc_sprite_table(int n)
{
	int old_nr_sprites = nr_sprites;
	nr_sprites = n;

	struct sact_sprite **tmp = xrealloc(sprites-1, sizeof(struct sact_sprite*) * (nr_sprites+1));
	sprites = tmp + 1;

	memset(sprites + old_nr_sprites, 0, sizeof(struct sact_sprite*) * (nr_sprites - old_nr_sprites));
}

int sact_Init(void)
{
	gfx_init();
	gfx_font_init();

	nr_sprites = 256;
	sprites = xmalloc(sizeof(struct sact_sprite*) * 257);
	memset(sprites, 0, sizeof(struct sact_sprite*) * 257);

	// create sprite for main_surface
	Texture *t = gfx_main_surface();
	struct sact_sprite *sp = create_sprite(0, t->w, t->h, 0, 0, 0, 255);
	sp->texture = *t; // XXX: textures normally shouldn't be copied like this...

	sprites++;
	return 1;
}

int sact_SetWP(int cg_no)
{
	if (!cg_no) {
		gfx_delete_texture(&wp.texture);
		return 1;
	}

	struct cg *cg = cg_load(cg_no - 1);
	if (!cg)
		return 0;

	gfx_delete_texture(&wp.texture);
	gfx_init_texture_with_cg(&wp.texture, cg);
	cg_free(cg);
	return 1;
}

int sact_Update(void)
{
	handle_events();

	gfx_clear();
	if (wp.texture.handle) {
		Rectangle r = RECT(0, 0, wp.texture.w, wp.texture.h);
		gfx_render_texture(&wp.texture, &r);
	}
	struct sact_sprite *p;
	TAILQ_FOREACH(p, &sprite_list, entry) {
		if (!p->texture.handle) {
			gfx_init_texture_with_color(&p->texture, p->rect.w, p->rect.h, p->color);
		}
		gfx_render_texture(&p->texture, &p->rect);
		if (p->text.texture.handle) {
			gfx_render_texture(&p->text.texture, &p->rect);
		}
	}
	gfx_swap();
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

int sact_SP_Enum(union vm_value *array, int size)
{
	int count = 0;
	for (int i = 0; i < nr_sprites && count < size; i++) {
		if (sprites[i]) {
			array[count++].i = i;
		}
	}
	return count;
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
		sact_SP_Create(sp_no, 1, 1, 0, 0, 0, 255);
	if (!(sp = sact_get_sprite(sp_no))) {
		WARNING("Failed to create sprite");
		return 0;
	}
	struct cg *cg = cg_load(cg_no - 1);
	if (!cg)
		return 0;
	gfx_init_texture_with_cg(&sp->texture, cg);
	sp->rect.w = cg->metrics.w;
	sp->rect.h = cg->metrics.h;
	sp->cg_no = cg_no;
	cg_free(cg);
	return 1;
}

static struct sact_sprite *create_sprite(int sp_no, int width, int height, int r, int g, int b, int a)
{
	struct sact_sprite *sp;
	if (sp_no < 0 || sp_no >= nr_sprites)
		VM_ERROR("Invalid sprite number: %d", sp_no);
	if (!(sp = sact_get_sprite(sp_no)))
		sp = alloc_sprite(sp_no);
	sp->color = (SDL_Color) { .r = r, .g = g, .b = b, .a = (a >= 0 ? a : 255) };
	sp->rect.w = width;
	sp->rect.h = height;
	if (!sp->show) {
		sp->show = true;
		sprite_register(sp);
	}
	gfx_delete_texture(&sp->texture);
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
	if (sp->show)
		sprite_unregister(sp);
	free_sprite(sp);
	return 1;
}

int sact_SP_SetZ(int sp_no, int z)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sp->z = z;
	if (sp->show) {
		sprite_unregister(sp);
		sprite_register(sp);
	}
	return 1;

}

int sact_SP_GetBlendRate(int sp_no)
{
	struct texture *t = sact_get_texture(sp_no);
	return t ? t->alpha_mod : 0;
}

int sact_SP_SetBlendRate(int sp_no, int rate)
{
	struct texture *t = sact_get_texture(sp_no);
	if (!t) return 0;
	t->alpha_mod = max(0, min(255, rate));
	return 1;
}

int sact_SP_SetShow(int sp_no, bool show)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	if (show == sp->show)
		return 1;
	sp->show = show;
	if (show)
		sprite_register(sp);
	else
		sprite_unregister(sp);
	return 1;
}

int sact_SP_SetDrawMethod(int sp_no, int method)
{
	struct texture *t = sact_get_texture(sp_no);
	if (!t) return 0;
	if (method < 0 || method >= NR_DRAW_METHODS) {
		WARNING("unknown draw method: %d", method);
		return 0;
	}
	t->draw_method = method;
	return 1;
}

int sact_SP_GetDrawMethod(int sp_no)
{
	struct texture *t = sact_get_texture(sp_no);
	if (!t) return DRAW_METHOD_NORMAL;
	return t->draw_method;
}

int sact_SP_ExistsAlpha(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sp->texture.has_alpha;
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

	if (!sp->text.texture.handle) {
		SDL_Color c;
		if (tm.outline_left || tm.outline_right || tm.outline_up || tm.outline_down)
			c = tm.outline_color;
		else
			c = tm.color;
		c.a = 0;
		gfx_init_texture_with_color(&sp->text.texture, sp->rect.w, sp->rect.h, c);
	}

	sp->text.pos.x += gfx_render_text(&sp->text.texture, sp->text.pos, text->text, &tm);
	return 1;
}

static void clear_text(struct sact_sprite *sp)
{
	gfx_delete_texture(&sp->text.texture);
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
	if (ssp->text.texture.handle) {
		SDL_Color c = { 0, 0, 0, 0 };
		gfx_init_texture_with_color(&dsp->text.texture, dsp->rect.w, dsp->rect.h, c);
		gfx_copy(&dsp->text.texture, 0, 0, &ssp->text.texture, 0, 0, ssp->text.texture.w, ssp->text.texture.h);
		gfx_copy_amap(&dsp->text.texture, 0, 0, &ssp->text.texture, 0, 0, ssp->text.texture.w, ssp->text.texture.h);
	}
	dsp->text.home = ssp->text.home;
	dsp->text.pos = ssp->text.pos;
	dsp->text.char_space = ssp->text.char_space;
	dsp->text.line_space = ssp->text.line_space;
	return 1;
}

int sact_SP_IsPtIn(int sp_no, int x, int y)
{
	Point p = POINT(x, y);
	struct sact_sprite *sp = sact_get_sprite(sp_no);

	if (!sp || !SDL_PointInRect(&p, &sp->rect))
		return 0;

	Texture *t = sact_get_texture(sp_no);
	SDL_Color c = gfx_get_pixel(t, x - sp->rect.x, y - sp->rect.y);
	return !!c.a;
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

int sact_SP_GetAMapValue(int sp_no, int x, int y)
{
	Texture *t = sact_get_texture(sp_no);
	SDL_Color c = gfx_get_pixel(t, x, y);
	return c.a;
}

int sact_SP_GetPixelValue(int sp_no, int x, int y, int *r, int *g, int *b)
{
	Texture *t = sact_get_texture(sp_no);
	SDL_Color c = gfx_get_pixel(t, x, y);
	*r = c.r;
	*g = c.g;
	*b = c.b;
	return 1;
}
