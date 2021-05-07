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
#include <assert.h>

#include "system4.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "sprite.h"
#include "CharSpriteManager.h"

/*
 * NOTE: This is not a library on it's own, but rather part of
 *       StoatSpriteEngine/ChipmunkSpriteEngine.
 */

struct charsprite {
	struct sact_sprite sp;
	// NOTE: even though we only render a single character, we need to store an
	//       arbitrary string because CharSprite_GetChar returns it.
	struct string *ch;
	struct text_metrics tm;
};

static struct {
	unsigned size;
	struct charsprite **sprites;
	unsigned nr_free;
	int *free;
} chars = {0};

static void grow_chars(unsigned new_size) {
	assert(new_size > chars.size);
	chars.sprites = xrealloc_array(chars.sprites, chars.size, new_size, sizeof(struct charsprite*));
	chars.free = xrealloc_array(chars.free, chars.size, new_size, sizeof(int));

	for (unsigned i = chars.size; i < new_size; i++) {
		chars.free[chars.nr_free++] = i;
	}

	chars.size = new_size;
}

static int get_handle(void)
{
	if (!chars.nr_free) {
		grow_chars(chars.size ? chars.size*2 : 256);
	}
	return chars.free[--chars.nr_free];
}

static bool handle_valid(int handle)
{
	return handle >= 0 && (unsigned)handle < chars.size && chars.sprites[handle];
}

static void check_handle(int handle)
{
	if (!handle_valid(handle))
		VM_ERROR("Invalid CharSprite handle: %d", handle);
}

static struct charsprite *charsprite_get(int handle)
{
	check_handle(handle);
	return chars.sprites[handle];
}

static bool charsprite_allocated(struct charsprite *cs)
{
	return cs->ch != NULL;
}

int CharSpriteManager_CreateHandle(void)
{
	int handle = get_handle();
	assert(!chars.sprites[handle]);
	chars.sprites[handle] = xcalloc(1, sizeof(struct charsprite));
	return handle;
}

bool CharSpriteManager_ReleaseHandle(int handle)
{
	if (!handle_valid(handle))
		return false;

	sprite_free(&chars.sprites[handle]->sp);
	if (chars.sprites[handle]->ch) {
		free_string(chars.sprites[handle]->ch);
	}
	free(chars.sprites[handle]);
	chars.sprites[handle] = NULL;
	chars.free[chars.nr_free++] = handle;
	return true;
}

void CharSpriteManager_Clear(void)
{
	for (unsigned i = 0; i < chars.size; i++) {
		CharSpriteManager_ReleaseHandle(i);
	}
}

//static void CharSpriteManager_Rebuild(void);
//static bool CharSpriteManager_Save(struct page **iarray);
//static bool CharSpriteManager_Load(struct page **iarray);

void CharSprite_Release(int handle)
{
	struct charsprite *sp = charsprite_get(handle);
	sprite_free(&sp->sp);
}

static void CASColor_to_SDL_Color(SDL_Color *color, int page_no)
{
	struct page *page = heap_get_page(page_no);
	assert(page);
	assert(page->nr_vars == 4);
	color->r = page->values[0].i;
	color->g = page->values[1].i;
	color->b = page->values[2].i;
	color->a = page->values[3].i;
}

static void CASCharSpriteProperty_to_text_metrics(struct text_metrics *tm, struct page *page)
{
	assert(page);
	assert(page->nr_vars == 6);

	tm->face = page->values[0].i;
	tm->size = page->values[1].i;
	CASColor_to_SDL_Color(&tm->color, page->values[2].i);
	//tm->weight = page->values[3].f;
	tm->weight = FW_NORMAL;
	tm->outline_left = page->values[4].f;
	tm->outline_up = tm->outline_left;
	tm->outline_right = tm->outline_left;
	tm->outline_down = tm->outline_left;
	CASColor_to_SDL_Color(&tm->outline_color, page->values[5].i);
}

static int extract_sjis_char(char *src, char *dst)
{
	if (SJIS_2BYTE(*src)) {
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = '\0';
		return 2;
	}
	dst[0] = src[0];
	dst[1] = '\0';
	return 1;
}

void CharSprite_SetChar(int handle, struct string **s, struct page **property)
{
	struct charsprite *sp = charsprite_get(handle);
	CASCharSpriteProperty_to_text_metrics(&sp->tm, *property);

	if (charsprite_allocated(sp)) {
		sprite_free(&sp->sp);
	}
	if (sp->ch) {
		free_string(sp->ch);
		sp->ch = NULL;
	}
	if (!(*s)->size) {
		return;
	}

	sp->ch = string_ref(*s);

	char ch[3];
	extract_sjis_char((*s)->text, ch);

	int w = sp->tm.size;
	int h = sp->tm.size;
	if (isascii(ch[0]))
		w /= 2;

	sprite_init(&sp->sp, w, h, 0, 0, 0, 0);
	sprite_get_texture(&sp->sp); // XXX: force initialization of texture
	gfx_render_text(&sp->sp.texture, (Point) { .x=0, .y = 0 }, ch, &sp->tm, 0);
	sprite_dirty(&sp->sp);
}

void CharSprite_SetPos(int handle, int x, int y)
{
	struct charsprite *sp = charsprite_get(handle);
	sprite_set_pos(&sp->sp, x, y);
}

void CharSprite_SetZ(int handle, int z)
{
	struct charsprite *sp = charsprite_get(handle);
	sprite_set_z(&sp->sp, z);
}

void CharSprite_SetAlphaRate(int handle, int rate)
{
	struct charsprite *sp = charsprite_get(handle);
	sprite_set_blend_rate(&sp->sp, rate);
}

void CharSprite_SetShow(int handle, bool show)
{
	struct charsprite *sp = charsprite_get(handle);
	sprite_set_show(&sp->sp, show);
}

void CharSprite_GetChar(int handle, struct string **ch)
{
	struct charsprite *sp = charsprite_get(handle);
	if (*ch)
		free_string(*ch);
	if (sp->ch)
		*ch = string_ref(sp->ch);
	else
		*ch = string_ref(&EMPTY_STRING);
}

int CharSprite_GetAlphaRate(int handle)
{
	struct charsprite *sp = charsprite_get(handle);
	return sprite_get_blend_rate(&sp->sp);
}
