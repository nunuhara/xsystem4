/* Copyright (C) 2021 kichikuou <KichikuouChrome@gmail.com>
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
#include <stdio.h>
#include <string.h>

#include "system4/string.h"

#include "hll.h"
#include "audio.h"
#include "savedata.h"
#include "vm/page.h"
#include "xsystem4.h"

static int BanMisc_SaveStruct(struct page *page, struct string *file_name)
{
	char *path = unix_path(file_name->text);
	FILE *fp = fopen(path, "w");
	if (!fp) {
		WARNING("Failed to open file '%s': %s", path, strerror(errno));
		free(path);
		return 0;
	}
	free(path);

	cJSON *json = page_to_json(page);
	char *str = cJSON_Print(json);
	cJSON_Delete(json);

	bool ok = fputs(str, fp) != EOF;
	free(str);
	fclose(fp);
	if (!ok) {
		WARNING("BanMisc.SaveStruct failed (fputs): %s", strerror(errno));
		return 0;
	}
	return 1;
}

static int BanMisc_LoadStruct(struct page **_page, struct string *file_name)
{
	struct page *page = *_page;
	if (page->type != STRUCT_PAGE) {
		VM_ERROR("BanMisc.LoadStruct of non-struct");
	}

	char *path = unix_path(file_name->text);
	FILE *fp = fopen(path, "r");
	if (!fp) {
		WARNING("Failed to open file '%s': %s", path, strerror(errno));
		free(path);
		return 0;
	}
	free(path);

	fseek(fp, 0, SEEK_END);
	long len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	char *buf = xmalloc(len + 1);
	buf[len] = '\0';
	if (fread(buf, len, 1, fp) != 1) {
		WARNING("BanMisc.LoadStruct failed (fread): %s", strerror(errno));
		free(buf);
		return 0;
	}
	fclose(fp);

	cJSON *json = cJSON_Parse(buf);
	free(buf);
	if (!json) {
		WARNING("BanMisc.LoadStruct failed to parse JSON");
		return 0;
	}
	if (!cJSON_IsArray(json)) {
		WARNING("BanMisc.LoadStruct incorrect type in parsed JSON");
		cJSON_Delete(json);
		return 0;
	}

	json_load_page(page, json, true);
	cJSON_Delete(json);
	return 1;
}

HLL_WARN_UNIMPLEMENTED( , void, BanMisc, Init, void *imainsystem);

static void BanMisc_FadePlay(int ch, int num, int time, int volume, int loop)
{
	bgm_prepare(ch, num - 1);
	bgm_play(ch);
	// TODO: fading
}

static int BanMisc_IsPlay(int ch)
{
	return bgm_is_playing(ch);
}

static void BanMisc_Stop(int ch)
{
	bgm_stop(ch);
}

static void BanMisc_Fade(int ch, int time, int volume, int stopFlag)
{
	bgm_fade(ch, time, volume, stopFlag);
}

HLL_WARN_UNIMPLEMENTED(0, int, BanMisc, IsFade, int ch);

static int BanMisc_SoundLoad(int ch, int num)
{
	return wav_prepare(ch, num - 1);
}

static void BanMisc_SoundPlay(int ch, int loop)
{
	if (loop != 1)
		WARNING("sound loop is not supported");
	wav_play(ch);
}

static void BanMisc_SoundStop(int ch)
{
	wav_stop(ch);
}

static void BanMisc_SoundFade(int ch, int time, int vol, int stop)
{
	wav_fade(ch, time, vol, stop);
}

static void BanMisc_SoundReverseLR(int ch)
{
	wav_reverse_LR(ch);
}

static int BanMisc_SoundIsPlay(int ch)
{
	return wav_is_playing(ch);
}

static int BanMisc_SoundRelease(int ch)
{
	return wav_unprepare(ch);
}

static int BanMisc_IsMute(int ch)
{
	// Returns 1 if ch is muted in the sound config dialog.
	return 0;
}

HLL_LIBRARY(BanMisc,
			HLL_EXPORT(SaveStruct, BanMisc_SaveStruct),
			HLL_EXPORT(LoadStruct, BanMisc_LoadStruct),
			HLL_EXPORT(Init, BanMisc_Init),
			HLL_EXPORT(FadePlay, BanMisc_FadePlay),
			HLL_EXPORT(IsPlay, BanMisc_IsPlay),
			HLL_EXPORT(Stop, BanMisc_Stop),
			HLL_EXPORT(Fade, BanMisc_Fade),
			HLL_EXPORT(IsFade, BanMisc_IsFade),
			HLL_EXPORT(SoundLoad, BanMisc_SoundLoad),
			HLL_EXPORT(SoundPlay, BanMisc_SoundPlay),
			HLL_EXPORT(SoundStop, BanMisc_SoundStop),
			HLL_EXPORT(SoundFade, BanMisc_SoundFade),
			HLL_EXPORT(SoundReverseLR, BanMisc_SoundReverseLR),
			HLL_EXPORT(SoundIsPlay, BanMisc_SoundIsPlay),
			HLL_EXPORT(SoundRelease, BanMisc_SoundRelease),
			HLL_EXPORT(IsMute, BanMisc_IsMute));
