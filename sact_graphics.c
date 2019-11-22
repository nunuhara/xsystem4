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

static struct sact_sprite *alloc_sprite(int sp)
{
	sprites[sp] = xcalloc(1, sizeof(struct sact_sprite));
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
	return 1;
}

int sact_Update(void)
{
	handle_events();
	struct sact_sprite *p;
	TAILQ_FOREACH(p, &sprite_list, entry) {
		if (!p->show)
			continue;
		if (!p->cg->s)
			p->cg->s = sdl_make_rectangle(p->rect.w, p->rect.h, &p->color);
		sdl_draw_cg(p->cg, &p->rect);
	}
	sdl_update_screen();
	return 1;
}

int sact_SP_GetUnuseNum(int min)
{
	for (int i = min; i < nr_sprites; i++) {
		if (!sprites[i])
			return i;
	}

	int n = max(min, nr_sprites);
	nr_sprites = n + 256;
	sprites = xrealloc(sprites, sizeof(struct sact_sprite*) * nr_sprites);
	for (int i = n; i < nr_sprites; i++)
		sprites[i] = NULL;
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
	if (!sp || !cg_load(sp->cg, cg_no - 1))
		return 0;
	sp->rect.w = sp->cg->s->w;
	sp->rect.h = sp->cg->s->h;
	return 1;

}

int sact_SP_Create(int sp_no, int width, int height, int r, int g, int b, int a)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);

	if (!sp) {
		sp = alloc_sprite(sp_no);
	}

	sp->color = (SDL_Color) { .r = r, .g = g, .b = b, .a = a };
	sp->rect.w = width;
	sp->rect.h = height;
	if (sp->cg) {
		cg_reinit(sp->cg);
	} else {
		sp->cg = cg_init();
		sprite_register(sp);
	}
	return 1;
}

int sact_SP_Delete(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp) return 0;
	sprite_unregister(sp);
	cg_free(sp->cg);
	sp->cg = NULL;
	return 1;
}

int sact_SP_SetZ(int sp_no, int z)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp || !sp->cg) return 0;
	sprite_unregister(sp);
	sp->z = z;
	sprite_register(sp);
	return 1;

}

