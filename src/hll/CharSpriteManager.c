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
#include <math.h>

#include "system4.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "gfx/font.h"
#include "sprite.h"
#include "xsystem4.h"
#include "CharSpriteManager.h"
#include "iarray.h"

/*
 * NOTE: This is not a library on it's own, but rather part of
 *       StoatSpriteEngine/ChipmunkSpriteEngine.
 */

struct charsprite {
	struct sact_sprite sp;
	// NOTE: even though we only render a single character, we need to store an
	//       arbitrary string because CharSprite_GetChar returns it.
	struct string *ch;
	struct text_style ts;
	// NOTE: this is needed because we have to hide all sprites after loading
	//       regardless of their original visibility. When Rebuild is called,
	//       this value is used to correctly set the visibility.
	bool show;
};

struct charsprite_manager {
	unsigned size;
	struct charsprite **sprites;
	unsigned nr_free;
	int *free;
};

static struct charsprite_manager chars = {0};

// XXX: after CharSpriteManager_Load, CharSpriteManager_Rebuild must be called to
//      make the loaded data visible. It is stored here until then.
static struct charsprite_manager load_chars = {0};
static bool loaded = false;

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
	sprite_init(&chars.sprites[handle]->sp);
	chars.sprites[handle]->ts.scale_x = 1.0f;
	return handle;
}

static void charsprite_free(struct charsprite *cs)
{
	sprite_free(&cs->sp);
	if (cs->ch) {
		free_string(cs->ch);
	}
	free(cs);
}

bool CharSpriteManager_ReleaseHandle(int handle)
{
	if (!handle_valid(handle))
		return false;

	charsprite_free(chars.sprites[handle]);
	chars.sprites[handle] = NULL;
	chars.free[chars.nr_free++] = handle;
	return true;
}

static void charsprite_manager_free(struct charsprite_manager *csm)
{
	for (unsigned i = 0; i < csm->size; i++) {
		if (csm->sprites[i])
			charsprite_free(csm->sprites[i]);
	}
	free(csm->sprites);
	free(csm->free);
	memset(csm, 0, sizeof(*csm));
}

void CharSpriteManager_Clear(void)
{
	for (unsigned i = 0; i < chars.size; i++) {
		CharSpriteManager_ReleaseHandle(i);
	}
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

static void charsprite_render(struct charsprite *cs)
{
	char ch[3];
	extract_sjis_char(cs->ch->text, ch);

	// XXX: this is handled here and not in gfx_render_text because of the
	//      width calculation below
	if (ch[0] == '}' && game_rance02_mg) {
		ch[0] = 0x81;
		ch[1] = 0x5c;
		ch[2] = 0;
	}

	int w = ceilf(text_style_width(&cs->ts, ch));
	int h = cs->ts.size + cs->ts.size/2;
	sprite_init_color(&cs->sp, w, h, 0, 0, 0, 0);
	sprite_get_texture(&cs->sp); // XXX: force initialization of texture
	gfx_render_text(&cs->sp.texture, 0.0f, 0, ch, &cs->ts, false);
	sprite_dirty(&cs->sp);
}

void CharSpriteManager_Rebuild(void)
{
	if (!loaded)
		return;

	charsprite_manager_free(&chars);
	chars = load_chars;

	// reset visibility
	for (unsigned i = 0; i < chars.size; i++) {
		if (!chars.sprites[i])
			continue;
		charsprite_render(chars.sprites[i]);
		sprite_set_show(&chars.sprites[i]->sp, chars.sprites[i]->show);
	}

	memset(&load_chars, 0, sizeof(load_chars));
	loaded = false;
}

bool CharSpriteManager_Save(struct page **iarray)
{
	// 'C'
	// 'S'
	// 'M'
	// 0x0
	// 0x0
	// 0x1  <- nr handles (NOT necessary the length of the following list)
	// 0x1  <- ???
	// 0x67 <- 'C' (char text)
	// 0    <- '\0'
	// 0    <- m_nType
	// 64   <- m_nSize
	// ...
	// 0   <- x
	// 0   <- y
	// 0   <- z
	// 255 <- alpha rate
	// 1   <- show
	// 2000 <- handle + 2000

	struct iarray_writer w;
	iarray_init_writer(&w, "CSM");
	iarray_write(&w, 0); // version?
	iarray_write(&w, chars.size);

	for (unsigned i = 0; i < chars.size; i++) {
		if (!chars.sprites[i])
			continue;

		struct charsprite *s = chars.sprites[i];
		iarray_write(&w, 1); // ???
		iarray_write_string(&w, s->ch);
		iarray_write(&w, s->ts.face);
		iarray_write(&w, s->ts.size);
		iarray_write(&w, s->ts.color.r);
		iarray_write(&w, s->ts.color.g);
		iarray_write(&w, s->ts.color.b);
		iarray_write(&w, s->ts.color.a);
		//iarray_write_float(&w, s->ts.weight);
		iarray_write_float(&w, 1.0);
		iarray_write_float(&w, s->ts.edge_left);
		iarray_write(&w, s->ts.edge_color.r);
		iarray_write(&w, s->ts.edge_color.g);
		iarray_write(&w, s->ts.edge_color.b);
		iarray_write(&w, s->ts.edge_color.a);
		iarray_write(&w, sprite_get_pos_x(&s->sp));
		iarray_write(&w, sprite_get_pos_y(&s->sp));
		iarray_write(&w, sprite_get_z(&s->sp));
		iarray_write(&w, sprite_get_blend_rate(&s->sp));
		iarray_write(&w, sprite_get_show(&s->sp));
		iarray_write(&w, 2000 + i);

		// NOTE: Allocated but uninitialized handles are saved with a
		//       handle of -1. These persist even after loading and
		//       re-saving.
		//       I've haven't implemented this as there's no way to know
		//       the original handle number after loading anyways. Seems
		//       like a memory leak in the original implementation.
	}

	if (*iarray) {
		delete_page_vars(*iarray);
		free_page(*iarray);
	}
	*iarray = iarray_to_page(&w);
	iarray_free_writer(&w);
	return true;
}

bool CharSpriteManager_Load(struct page **data)
{
	struct charsprite_manager csm = {0};

	if (!(*data))
		return false;

	struct iarray_reader r;
	iarray_init_reader(&r, *data, "CSM");
	if (iarray_read(&r))
		return false;

	int nr_handles = iarray_read(&r);
	if (nr_handles < 0 || nr_handles > 100000)
		goto error;

	csm.size = nr_handles;
	csm.sprites = xcalloc(nr_handles, sizeof(struct charsprite*));

	// load handles
	while (!iarray_end(&r)) {
		// character data
		struct charsprite *cs = xcalloc(1, sizeof(struct charsprite));
		iarray_read(&r); // ???
		cs->ch = iarray_read_string(&r);
		cs->ts.face = iarray_read(&r);
		cs->ts.size = iarray_read(&r);
		cs->ts.color.r = iarray_read(&r);
		cs->ts.color.g = iarray_read(&r);
		cs->ts.color.b = iarray_read(&r);
		cs->ts.color.a = iarray_read(&r);
		cs->ts.weight = FW_NORMAL; iarray_read_float(&r); // FIXME
		text_style_set_edge_width(&cs->ts, iarray_read_float(&r));
		cs->ts.edge_color.r = iarray_read(&r);
		cs->ts.edge_color.g = iarray_read(&r);
		cs->ts.edge_color.b = iarray_read(&r);
		cs->ts.edge_color.a = iarray_read(&r);
		cs->ts.bold_width = 0.0f;
		cs->ts.scale_x = 1.0f;
		cs->ts.space_scale_x = 1.0f;
		cs->ts.font_spacing = 0.0f;
		cs->ts.font_size = NULL;

		// sprite data
		int x = iarray_read(&r);
		int y = iarray_read(&r);
		sprite_set_show(&cs->sp, false);
		sprite_set_pos(&cs->sp, x, y);
		sprite_set_z(&cs->sp, iarray_read(&r));
		sprite_set_blend_rate(&cs->sp, iarray_read(&r));
		cs->show = iarray_read(&r);

		// insert charsprite into array
		int handle = iarray_read(&r) - 2000;
		if (handle < 0 || (unsigned)handle >= csm.size || csm.sprites[handle]) {
			sprite_free(&cs->sp);
			free(cs);
			goto error;
		}
		csm.sprites[handle] = cs;

		if (r.error)
			goto error;
	}

	// initialize free list
	csm.free = xcalloc(csm.size, sizeof(int));
	for (unsigned i = 0; i < csm.size; i++) {
		if (!csm.sprites[i])
			csm.free[csm.nr_free++] = i;
	}

	if (loaded) {
		charsprite_manager_free(&load_chars);
	}
	load_chars = csm;
	loaded = true;
	return true;
error:
	WARNING("CharSpriteManager_Load failed");
	charsprite_manager_free(&csm);
	return false;
}

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

static void CASCharSpriteProperty_to_text_style(struct text_style *ts, struct page *page)
{
	assert(page);
	assert(page->nr_vars == 6);

	ts->face = page->values[0].i;
	ts->size = page->values[1].i;
	CASColor_to_SDL_Color(&ts->color, page->values[2].i);
	//ts->weight = page->values[3].f;
	ts->weight = FW_NORMAL;
	text_style_set_edge_width(ts, page->values[4].f);
	CASColor_to_SDL_Color(&ts->edge_color, page->values[5].i);
}

void CharSprite_SetChar(int handle, struct string **s, struct page **property)
{
	struct charsprite *sp = charsprite_get(handle);
	CASCharSpriteProperty_to_text_style(&sp->ts, *property);

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
	charsprite_render(sp);
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
