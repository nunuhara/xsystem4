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

#include "audio.h"
#include "input.h"
#include "gfx/gfx.h"
#include "sact.h"
#include "system4.h"
#include "system4/ald.h"
#include "system4/cg.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm.h"
#include "vm/page.h"

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

int sact_Init(possibly_unused void *_, possibly_unused int cg_cache_size)
{
	gfx_init();
	gfx_font_init();
	audio_init();

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

	struct cg *cg = cg_load(ald[ALDFILE_CG], cg_no - 1);
	if (!cg)
		return 0;

	gfx_delete_texture(&wp.texture);
	gfx_init_texture_with_cg(&wp.texture, cg);
	cg_free(cg);
	return 1;
}

int sact_SetWP_Color(int r, int g, int b)
{
	gfx_set_clear_color(r, g, b, 255);
	return 1;
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

void sact_render_scene(void)
{
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
}

int sact_Update(void)
{
	handle_events();
	sact_render_scene();
	gfx_swap();
	return 1;
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
	struct cg *cg = cg_load(ald[ALDFILE_CG], cg_no - 1);
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

int sact_SP_SetPos(int sp_no, int x, int y)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 1;
	sp->rect.x = x;
	sp->rect.y = y;
	return 1;
}

int sact_SP_SetX(int sp_no, int x)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sp->rect.x = x;
	return 1;
}

int sact_SP_SetY(int sp_no, int y)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sp->rect.y = y;
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

int sact_SP_IsUsing(int sp_no)
{
	return sact_get_sprite(sp_no) != NULL;
}

int sact_SP_ExistsAlpha(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sp->texture.has_alpha;
}

int sact_SP_GetPosX(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sp->rect.x;
}

int sact_SP_GetPosY(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sp->rect.y;
}

int sact_SP_GetWidth(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sp->rect.w;
}

int sact_SP_GetHeight(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sp->rect.h;
}

int sact_SP_GetZ(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sp->z;
}

int sact_SP_GetShow(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sp->show;
}

int sact_SP_SetTextHome(int sp_no, int x, int y)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sp->text.home = (Point) { .x = x, .y = y };
	return 1;
}

int sact_SP_SetTextLineSpace(int sp_no, int px)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 1;
	sp->text.line_space = px;
	return 1;
}

int sact_SP_SetTextCharSpace(int sp_no, int px)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 1;
	sp->text.char_space = px;
	return 1;
}

int sact_SP_SetTextPos(int sp_no, int x, int y)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sp->text.pos = (Point) { .x = x, .y = y };
	return 1;
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

int sact_SP_TextDraw(int sp_no, struct string *text, struct page *_tm)
{

	struct text_metrics tm;
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;

	init_text_metrics(&tm, _tm->values);

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

int sact_SP_TextHome(int sp_no, possibly_unused int size)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	// FIXME: do something with nTextSize
	sp->text.pos = sp->text.home;
	return 1;
}

int sact_SP_TextNewLine(int sp_no, int size)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sp->text.pos = POINT(sp->text.home.x, sp->text.home.y + size);
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

int sact_SP_GetTextHomeX(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sp->text.home.x;
}

int sact_SP_GetTextHomeY(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sp->text.home.y;
}

int sact_SP_GetTextCharSpace(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sp->text.char_space;
}

int sact_SP_GetTextPosX(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sp->text.pos.x;
}

int sact_SP_GetTextPosY(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sp->text.pos.y;
}

int sact_SP_GetTextLineSpace(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	return sp->text.line_space;
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

int sact_SP_IsPtInRect(int sp_no, int x, int y)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	Point p = { .x = x, .y = y };
	if (!sp) return 0;
	return !!SDL_PointInRect(&p, &sp->rect);
}

int sact_CG_GetMetrics(int cg_no, struct page **page)
{
	union vm_value *cgm = (*page)->values;
	struct cg_metrics metrics;
	if (!cg_get_metrics(ald[ALDFILE_CG], cg_no - 1, &metrics))
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
