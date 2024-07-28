/* Copyright (C) 2024 kichikuou <KichikuouChrome@gmail.com>
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
#include "system4/string.h"

#include "gfx/font.h"
#include "hll.h"
#include "id_pool.h"
#include "vmSurface.h"
#include "xsystem4.h"

enum {
	SURFACE_DROP_SHADOW,
	SURFACE_BOLD,
	SURFACE_TEXT,
	NR_SURFACES
};

struct style {
	struct text_style ts;
	int drop_shadow;
	SDL_Color drop_shadow_color;
	int bold;
	SDL_Color bold_color;
};

struct vm_drawmsg {
	int w;
	int h;
	int surfaces[NR_SURFACES];
	struct string *text;
	Point pos;
	Point initial_pos;
	struct style style;
	struct style initial_style;
};

static struct id_pool pool;

static void vmDrawMsg_New(possibly_unused void *imainsystem)
{
	if (pool.slots)
		return;  // already initialized
	gfx_font_init();
	id_pool_init(&pool, ID_POOL_HANDLE_BASE);
}

static int vmDrawMsg_EnumHandle(void)
{
	return id_pool_count(&pool);
}

static int vmDrawMsg_Open(int width, int height)
{
	int no = id_pool_get_unused(&pool);
	struct vm_drawmsg *dm = xcalloc(1, sizeof(struct vm_drawmsg));
	id_pool_set(&pool, no, dm);
	dm->w = width;
	dm->h = height;
	for (int i = 0; i < NR_SURFACES; i++) {
		struct vm_surface *sf = vm_surface_create(width, height);
		dm->surfaces[i] = sf->no;
		gfx_init_texture_rgba(&sf->texture, sf->w, sf->h, COLOR(0, 0, 0, 0));
	}
	dm->text = string_dup(&EMPTY_STRING);

	dm->initial_style.ts.face = FONT_GOTHIC;
	dm->initial_style.ts.size = 24.0f;
	dm->initial_style.ts.weight = FW_BOLD;
	dm->initial_style.ts.color = COLOR(255, 255, 255, 255);
	dm->initial_style.ts.scale_x = 1.0;
	dm->initial_style.ts.space_scale_x = 1.0f;
	dm->style = dm->initial_style;

	return no;
}

static void vmDrawMsg_Close(int handle)
{
	struct vm_drawmsg *dm = id_pool_release(&pool, handle);
	if (!dm)
		return;
	free_string(dm->text);
	for (int i = 0; i < NR_SURFACES; i++) {
		vm_surface_delete(dm->surfaces[i]);
	}
	free(dm);
}

static int vmDrawMsg_Release(void)
{
	for (int i = id_pool_get_first(&pool); i >= 0; i = id_pool_get_next(&pool, i))
		vmDrawMsg_Close(i);
	id_pool_delete(&pool);
	return 1;
}

static void vmDrawMsg_ModuleFini(void)
{
	vmDrawMsg_Release();
}

static int vmDrawMsg_GetNumofSurface(int handle)
{
	struct vm_drawmsg *dm = id_pool_get(&pool, handle);
	return dm ? NR_SURFACES : 0;
}

static int vmDrawMsg_GetSurface(int handle, int index)
{
	struct vm_drawmsg *dm = id_pool_get(&pool, handle);
	if (!dm || index < 0 || index >= NR_SURFACES)
		return 0;
	return dm->surfaces[index];
}

static int vmDrawMsg_AddMsg(int handle, struct string *s)
{
	struct vm_drawmsg *dm = id_pool_get(&pool, handle);
	if (!dm)
		return 0;
	string_append(&dm->text, s);
	return 1;
}

static int vmDrawMsg_Draw(int handle)
{
	struct vm_drawmsg *dm = id_pool_get(&pool, handle);
	if (!dm)
		return 0;

	char *s = dm->text->text;
	const int line_space = 2;
	while (*s) {
		int skiplen = 0, a1, a2, a3, a4, a5, a6;
		if (*s != '<') {
			char *lb = strchr(s, '<');
			if (lb) {
				skiplen = lb - s;
				*lb = '\0';
			} else {
				skiplen = strlen(s);
			}
			if (dm->style.drop_shadow) {
				struct text_style ts = dm->style.ts;
				ts.color = dm->style.drop_shadow_color;
				struct vm_surface *sf = vm_surface_get(dm->surfaces[SURFACE_DROP_SHADOW]);
				int x = dm->pos.x + dm->style.drop_shadow;
				int y = dm->pos.y + dm->style.drop_shadow;
				gfx_render_text(&sf->texture, x, y, s, &ts, false);
			}
			if (dm->style.bold) {
				struct text_style ts = dm->style.ts;
				ts.bold_width = dm->style.bold;
				ts.color = dm->style.bold_color;
				struct vm_surface *sf = vm_surface_get(dm->surfaces[SURFACE_BOLD]);
				gfx_render_text(&sf->texture, dm->pos.x, dm->pos.y, s, &ts, false);
			}
			struct vm_surface *sf = vm_surface_get(dm->surfaces[SURFACE_TEXT]);
			dm->pos.x += gfx_render_text(&sf->texture, dm->pos.x, dm->pos.y, s, &dm->style.ts, false);
			if (lb)
				*lb = '<';
		} else if (sscanf(s, "<@C%d,%d,%d,%d,%d,%d>%n", &a1, &a2, &a3, &a4, &a5, &a6, &skiplen) == 6) {
			// FIXME: This tag specifies a vertical gradient from (a1, a2, a3)
			//        to (a4, a5, a6), but we just use the average color.
			dm->initial_style.ts.color = dm->style.ts.color =
				COLOR((a1 + a4) / 2, (a2 + a5) / 2, (a3 + a6) / 2, 255);
		} else if (sscanf(s, "<C%d,%d,%d>%n", &a1, &a2, &a3, &skiplen) == 3) {
			dm->style.ts.color = COLOR(a1, a2, a3, 255);
		} else if (sscanf(s, "<@C%d,%d,%d>%n", &a1, &a2, &a3, &skiplen) == 3) {
			dm->initial_style.ts.color = dm->style.ts.color = COLOR(a1, a2, a3, 255);
		} else if (!strncmp(s, "</C>", 4)) {
			skiplen = 4;
			dm->style.ts.color = dm->initial_style.ts.color;
		} else if (sscanf(s, "<B%d>%n", &a1, &skiplen) == 1) {
			dm->style.ts.weight = a1 ? FW_HEAVY : FW_BOLD;
		} else if (sscanf(s, "<F%d>%n", &a1, &skiplen) == 1) {
			dm->style.ts.font_size = NULL;
			dm->style.ts.face = a1 ? FONT_MINCHO : FONT_GOTHIC;
		} else if (sscanf(s, "<E%d>%n", &a1, &skiplen) == 1) {
			dm->style.bold = a1;
		} else if (sscanf(s, "<@E%d>%n", &a1, &skiplen) == 1) {
			dm->initial_style.bold = dm->style.bold = a1;
		} else if (sscanf(s, "<EC%d,%d,%d>%n", &a1, &a2, &a3, &skiplen) == 3) {
			dm->style.bold_color = COLOR(a1, a2, a3, 255);
		} else if (sscanf(s, "<@EC%d,%d,%d>%n", &a1, &a2, &a3, &skiplen) == 3) {
			dm->initial_style.bold_color = dm->style.bold_color = COLOR(a1, a2, a3, 255);
		} else if (sscanf(s, "<K%d>%n", &a1, &skiplen) == 1) {
			dm->style.drop_shadow = a1;
		} else if (sscanf(s, "<@K%d>%n", &a1, &skiplen) == 1) {
			dm->initial_style.drop_shadow = dm->style.drop_shadow = a1;
		} else if (sscanf(s, "<KC%d,%d,%d>%n", &a1, &a2, &a3, &skiplen) == 3) {
			dm->style.drop_shadow_color = COLOR(a1, a2, a3, 255);
		} else if (sscanf(s, "<@KC%d,%d,%d>%n", &a1, &a2, &a3, &skiplen) == 3) {
			dm->initial_style.drop_shadow_color = dm->style.drop_shadow_color = COLOR(a1, a2, a3, 255);
		} else if (sscanf(s, "<S%d>%n", &a1, &skiplen) == 1) {
			dm->style.ts.size = a1;
		} else if (sscanf(s, "<@S%d>%n", &a1, &skiplen) == 1) {
			dm->initial_style.ts.size = dm->style.ts.size = a1;
		} else if (sscanf(s, "<P%d,%d>%n", &a1, &a2, &skiplen) == 2) {
			dm->pos = POINT(a1, a2);
		} else if (sscanf(s, "<@P%d,%d>%n", &a1, &a2, &skiplen) == 2) {
			dm->initial_pos = dm->pos = POINT(a1, a2);
		} else if (!strncmp(s, "<WCR>", 5)) {
			skiplen = 5;
			dm->pos = dm->initial_pos;
			for (int i = 0; i < NR_SURFACES; i++) {
				struct vm_surface *dst = vm_surface_get(dm->surfaces[i]);
				gfx_fill_amap(&dst->texture, 0, 0, dm->w, dm->h, 0);
			}
		} else if (!strncmp(s, "<CR>", 4)) {
			skiplen = 4;
			dm->pos = dm->initial_pos;
			dm->style = dm->initial_style;
		} else if (!strncmp(s, "<WA1>", 5)) {
			// no-op?
			skiplen = 5;
		} else if (!strncmp(s, "<R>", 3)) {
			skiplen = 3;
			dm->pos.x = dm->initial_pos.x;
			dm->pos.y += dm->style.ts.size + line_space;
		} else {
			char *rb = strchr(s, '>');
			if (rb) {
				WARNING("Unknown command %.*s", ++rb - s, s);
				skiplen = rb - s;
			} else {
				WARNING("Unfinished command in message '%s'", dm->text->text);
				break;
			}
		}
		s += skiplen;
	}
	string_clear(dm->text);
	return 0;
}

static int vmDrawMsg_RenewalRect(int handle, int *x, int *y, int *width, int *height)
{
	struct vm_drawmsg *dm = id_pool_get(&pool, handle);
	if (!dm)
		return 0;
	*x = 0;
	*y = 0;
	*width = dm->w;
	*height = dm->h;
	return 1;
}

HLL_LIBRARY(vmDrawMsg,
	    HLL_EXPORT(_ModuleFini, vmDrawMsg_ModuleFini),
	    HLL_EXPORT(New, vmDrawMsg_New),
	    HLL_EXPORT(Release, vmDrawMsg_Release),
	    HLL_EXPORT(EnumHandle, vmDrawMsg_EnumHandle),
	    HLL_EXPORT(Open, vmDrawMsg_Open),
	    HLL_EXPORT(Close, vmDrawMsg_Close),
	    HLL_EXPORT(GetNumofSurface, vmDrawMsg_GetNumofSurface),
	    HLL_EXPORT(GetSurface, vmDrawMsg_GetSurface),
	    HLL_EXPORT(AddMsg, vmDrawMsg_AddMsg),
	    HLL_EXPORT(Draw, vmDrawMsg_Draw),
	    HLL_EXPORT(RenewalRect, vmDrawMsg_RenewalRect)
	    );
