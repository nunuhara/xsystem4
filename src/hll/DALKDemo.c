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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/alk.h"
#include "system4/archive.h"
#include "system4/cg.h"

#include "audio.h"
#include "effect.h"
#include "gfx/gfx.h"
#include "hll.h"
#include "input.h"
#include "vm.h"
#include "xsystem4.h"

#define NR_PARTICLES 30

enum {
	DD_CG_PARTICLE = 12,
	DD_CG_LOGO = 13,
	DD_CG_BACKGROUND = 14,
	DD_CG_BACKGROUND_SHADOWED = 15,
	DD_CG_MAGIC_CIRCLE = 16,
	DD_CG_INITIALIZE_WARNING = 17,
	DD_CG_YES = 18,
	DD_CG_NO = 19,
	DD_CG_GAMESTART = 20,
	DD_CG_GAMESTART_SELECTED = 21,
	DD_CG_INITIALIZE = 22,
	DD_CG_INITIALIZE_SELECTED = 23,
	DD_CG_MAX
};

enum {
	DD_SOUND_SELCHANGE = 451,
	DD_SOUND_CLICK = 454,
};

struct dalkdemo {
	Texture tex[DD_CG_MAX];
	Texture tmp;
	float rotate_deg;
	struct {
		int x;
		int y;
		int speed;
	} particles[NR_PARTICLES];
};
static struct dalkdemo *dd;

static bool dalkdemo_run_effect(int (*update)(float), int time)
{
	uint32_t start = SDL_GetTicks();
	bool prev_keydown = false;
	uint32_t t = SDL_GetTicks() - start;
	while (t < time) {
		update((float)t / (float)time);
		handle_events();
		bool keydown = key_is_down(VK_LBUTTON) || key_is_down(VK_RBUTTON) || key_is_down(VK_RETURN) || key_is_down(VK_SPACE);
		if (prev_keydown && !keydown)
			return true;
		prev_keydown = keydown;

		uint32_t t2 = SDL_GetTicks() - start;
		if (t2 < t + 16) {
			SDL_Delay(t + 16 - t2);
			t += 16;
		} else {
			t = t2;
		}
	}
	return false;
}

static bool dalkdemo_effect_copy(Texture *tex, int x, int y, int w, int h, enum effect effect, int time)
{
	if (!effect_init(effect))
		return true;
	if (tex)
		gfx_copy(gfx_main_surface(), x, y, tex, x, y, w, h);
	else
		gfx_clear();

	bool interrupted = dalkdemo_run_effect(effect_update, time);
	if (!interrupted)
		effect_update(1.0);
	effect_fini();
	return interrupted;
}

static bool dalkdemo_wait(int time)
{
	return dalkdemo_effect_copy(&dd->tex[0], 0, 0, 0, 0, EFFECT_CROSSFADE, time);
}

static void dalkdemo_draw_title(Texture *dst, float circle_mag, float logo_opacity)
{
	Texture *src = &dd->tex[DD_CG_MAGIC_CIRCLE];
	gfx_copy_rot_zoom2(dst, dst->w / 2, dst->h / 2, src, src->w / 2, src->h / 2, dd->rotate_deg, circle_mag);
	dd->rotate_deg -= 0.25f;

	if (logo_opacity <= 0.f) {
		src = &dd->tex[DD_CG_BACKGROUND];
	} else if (logo_opacity >= 1.f) {
		src = &dd->tex[DD_CG_BACKGROUND_SHADOWED];
	} else {
		src = &dd->tex[DD_CG_BACKGROUND];
		gfx_copy(&dd->tmp, 0, 0, src, 0, 0, src->w, src->h);
		src = &dd->tex[DD_CG_BACKGROUND_SHADOWED];
		gfx_blend(&dd->tmp, 0, 0, src, 0, 0, src->w, src->h, logo_opacity * 255);
		src = &dd->tmp;
	}
	gfx_blend_screen(dst, 0, 0, src, 0, 0, src->w, src->h);

	src = &dd->tex[DD_CG_LOGO];
	gfx_blend_amap_alpha(dst, 0, 0, src, 0, 0, src->w, src->h, logo_opacity * 255);

	src = &dd->tex[DD_CG_PARTICLE];
	for (int i = 0; i < NR_PARTICLES; i++) {
		gfx_blend_screen(dst, dd->particles[i].x - src->w / 2, dd->particles[i].y - src->h / 2, src, 0, 0, src->w, src->h);
		dd->particles[i].x = (dd->particles[i].x - dd->particles[i].speed + dst->w) % dst->w;
		dd->particles[i].y = (dd->particles[i].y + dd->particles[i].speed) % dst->h;
	}
}

static int dalkdemo_title_effect(float progress)
{
	float t = progress * 15.f;
	gfx_clear();
	float logo_opacity = max(0.f, min(1.f, (t - 5.f) / 7.f));
	dalkdemo_draw_title(gfx_main_surface(), 1.75f - t * 0.05f, logo_opacity);
	if (t < 5.f) {
		int b = t * 255 / 5;
		gfx_fill_multiply(gfx_main_surface(), 0, 0, 640, 480, b, b, b);
	}
	gfx_swap();
	return 1;
}

static void dalkdemo_intro(void)
{
	int bgm_channel = bgm_get_unused_channel();
	bgm_prepare(bgm_channel, 1);
	bgm_play(bgm_channel);

	gfx_clear();
	if (dalkdemo_effect_copy(&dd->tex[0], 0, 0, 640, 480, EFFECT_CROSSFADE, 1500))
		goto skipped;
	if (dalkdemo_wait(1000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[1], 0, 0, 640, 480, EFFECT_CROSSFADE, 1500))
		goto skipped;
	// FIXME: DALKDemo.dll uses a steeper gradient
	if (dalkdemo_effect_copy(&dd->tex[2], 585, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[2], 545, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[2], 505, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[2], 60, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[2], 20, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;

	if (dalkdemo_effect_copy(&dd->tex[0], 0, 0, 640, 480, EFFECT_CROSSFADE, 4000))
		goto skipped;
	if (dalkdemo_wait(1000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[3], 340, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[3], 300, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[3], 260, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[0], 0, 0, 640, 480, EFFECT_CROSSFADE, 3000))
		goto skipped;

	if (dalkdemo_effect_copy(&dd->tex[4], 0, 0, 640, 480, EFFECT_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[5], 190, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[5], 150, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[5], 110, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[5], 70, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[5], 30, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;

	if (dalkdemo_effect_copy(&dd->tex[0], 0, 0, 640, 480, EFFECT_CROSSFADE, 4000))
		goto skipped;
	if (dalkdemo_wait(1000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[6], 320, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[6], 280, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[0], 0, 0, 640, 480, EFFECT_CROSSFADE, 3000))
		goto skipped;

	if (dalkdemo_effect_copy(&dd->tex[7], 0, 0, 640, 480, EFFECT_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[8], 560, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[8], 520, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[8], 480, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[8], 440, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[8], 400, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;

	if (dalkdemo_effect_copy(&dd->tex[0], 0, 0, 640, 480, EFFECT_CROSSFADE, 4000))
		goto skipped;
	if (dalkdemo_wait(1000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[9], 360, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[9], 320, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[9], 280, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[9], 240, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[0], 0, 0, 640, 480, EFFECT_CROSSFADE, 3000))
		goto skipped;

	if (dalkdemo_effect_copy(&dd->tex[10], 0, 0, 640, 480, EFFECT_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[11], 580, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[11], 540, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(&dd->tex[11], 60, 0, 40, 480, EFFECT_UP_DOWN_CROSSFADE, 3000))
		goto skipped;
	if (dalkdemo_effect_copy(NULL, 0, 0, 640, 480, EFFECT_CROSSFADE, 3000))
		goto skipped;

	dalkdemo_run_effect(dalkdemo_title_effect, 15000);

 skipped:
	bgm_unprepare(bgm_channel);
}

static int dalkdemo_main_menu()
{
	Texture *dst = gfx_main_surface();

	enum {
		START_MENU,
		INITIALIZE_MENU,
	} state = START_MENU;

	bool sel = false;
	bool prev_lbutton = false;
	bool prev_rbutton = false;
	for (;;) {
		// Input handling
		handle_events();
		int mx, my;
		mouse_get_pos(&mx, &my);
		bool next_sel = state == START_MENU ? my >= 430 : mx >= 320;
		if (sel != next_sel) {
			sel = next_sel;
			audio_play_sound(DD_SOUND_SELCHANGE);
		}

		bool lbutton = key_is_down(VK_LBUTTON);
		if (prev_lbutton && !lbutton) {
			audio_play_sound(DD_SOUND_CLICK);
			if (state == START_MENU) {
				if (!sel)
					return 1;
				else
					state = INITIALIZE_MENU;
			} else {
				if (!sel)
					return 2;
				else
					state = START_MENU;
			}
		}
		prev_lbutton = lbutton;
		bool rbutton = key_is_down(VK_RBUTTON);
		if (prev_rbutton && !rbutton && state == INITIALIZE_MENU) {
			state = START_MENU;
		}
		prev_rbutton = rbutton;

		// Draw
		gfx_clear();
		dalkdemo_draw_title(dst, 1.f, 1.f);

		if (state == START_MENU) {
			Texture *src = &dd->tex[!sel ? DD_CG_GAMESTART_SELECTED : DD_CG_GAMESTART];
			gfx_blend_amap(dst, (dst->w - src->w) / 2, 412 - src->h, src, 0, 0, src->w, src->h);
			src = &dd->tex[sel ? DD_CG_INITIALIZE_SELECTED : DD_CG_INITIALIZE];
			gfx_blend_amap(dst, (dst->w - src->w) / 2, 473 - src->h, src, 0, 0, src->w, src->h);
		} else {
			gfx_copy_grayscale(dst, 0, 0, dst, 0, 0, dst->w, dst->h);
			Texture *src = &dd->tex[DD_CG_INITIALIZE_WARNING];
			gfx_blend_amap(dst, 0, dst->h - src->h, src, 0, 0, src->w, src->h);
			if (!sel) {
				src = &dd->tex[DD_CG_YES];
				gfx_blend_amap(dst, 223, dst->h - src->h, src, 0, 0, src->w, src->h);
			} else {
				src = &dd->tex[DD_CG_NO];
				gfx_blend_amap(dst, 330, dst->h - src->h, src, 0, 0, src->w, src->h);
			}
		}

		gfx_swap();
		audio_update();
		vm_sleep(16);
	}
}

static void DALKDemo_Release(void)
{
	for (int i = 0; i < DD_CG_MAX; i++) {
		gfx_delete_texture(&dd->tex[i]);
	}
	gfx_delete_texture(&dd->tmp);
	free(dd);
	dd = NULL;
}

static int DALKDemo_Init(possibly_unused void *imainsystem)
{
	char *alk_path = gamedir_path("daDemo.alk");
	int error = ARCHIVE_SUCCESS;
	struct alk_archive *alk = alk_open(alk_path, MMAP_IF_64BIT, &error);
	if (error == ARCHIVE_FILE_ERROR) {
		WARNING("%s: %s", display_utf0(alk_path), strerror(errno));
	} else if (error == ARCHIVE_BAD_ARCHIVE_ERROR) {
		WARNING("%s: invalid .alk file", display_utf0(alk_path));
	}
	free(alk_path);
	if (!alk)
		return 0;

	dd = xcalloc(1, sizeof(struct dalkdemo));
	for (int i = 0; i < DD_CG_MAX; i++) {
		struct archive_data *dfile = archive_get(&alk->ar, i);
		if (!dfile) {
			WARNING("daDemo.alk: cannot load CG %d", i);
			DALKDemo_Release();
			return 0;
		}
		struct cg *cg = cg_load_data(dfile);
		archive_free_data(dfile);
		if (!cg) {
			WARNING("daDemo.alk: cannot decode CG %d", i);
			DALKDemo_Release();
			return 0;
		}
		gfx_init_texture_with_cg(&dd->tex[i], cg);
		cg_free(cg);
	}
	archive_free(&alk->ar);
	gfx_init_texture_blank(&dd->tmp, 640, 480);

	dd->rotate_deg = 0.f;
	for (int i = 0; i < NR_PARTICLES; i++) {
		dd->particles[i].x = rand() % 640;
		dd->particles[i].y = rand() % 480;
		dd->particles[i].speed = rand() % 2 ? 1 : 2;
	}

	return 1;
}

static int DALKDemo_Run(void)
{
	if (!dd)
		return 0;

	dalkdemo_intro();
	return dalkdemo_main_menu();
}

static int DALKDemo_Run2(void)
{
	if (!dd)
		return 0;

	return dalkdemo_main_menu();
}

HLL_LIBRARY(DALKDemo,
	    HLL_EXPORT(Init, DALKDemo_Init),
	    HLL_EXPORT(Run, DALKDemo_Run),
	    HLL_EXPORT(Run2, DALKDemo_Run2),
	    HLL_EXPORT(Release, DALKDemo_Release)
	    );
