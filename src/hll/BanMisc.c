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
#include <zlib.h>

#include "system4/file.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/mt19937int.h"

#include "hll.h"
#include "audio.h"
#include "iarray.h"
#include "savedata.h"
#include "vm/page.h"
#include "xsystem4.h"

#define MD11_ENCRYPT_KEY 0x18ec03
#define MD11_HEADER_SIZE 8

static const uint8_t md11_signature[] = { 'M', 'D', 0x01, 0x01 };

static int BanMisc_SaveStruct(struct page *page, struct string *file_name)
{
	// serialize
	struct iarray_writer out;
	iarray_init_writer(&out, NULL);
	iarray_write_struct(&out, page, true);
	size_t raw_size;
	uint8_t *raw_buf = iarray_to_buffer(&out, &raw_size);
	iarray_free_writer(&out);

	// compress
	unsigned long compressed_size = compressBound(raw_size);
	uint8_t *buf = xmalloc(MD11_HEADER_SIZE + compressed_size);
	memcpy(buf, md11_signature, sizeof(md11_signature));
	LittleEndian_putDW(buf, 4, raw_size);
	uint8_t *compressed = buf + MD11_HEADER_SIZE;
	int r = compress2(compressed, &compressed_size, raw_buf, raw_size, Z_DEFAULT_COMPRESSION);
	free(raw_buf);
	if (r != Z_OK) {
		free(buf);
		return 0;
	}

	// encrypt
	mt19937_xorcode(compressed, compressed_size, MD11_ENCRYPT_KEY);

	// write
	char *path = unix_path(file_name->text);
	bool ok = file_write(path, buf, MD11_HEADER_SIZE + compressed_size);
	if (!ok) {
		WARNING("Failed to write file '%s': %s", display_utf0(path), strerror(errno));
	}
	free(buf);
	free(path);
	return ok;
}

static struct page *load_md11(int struct_type, uint8_t *buf, size_t len)
{
	if (len < MD11_HEADER_SIZE) {
		WARNING("BanMisc.LoadStruct file too short");
		return NULL;
	}
	unsigned long raw_size = LittleEndian_getDW(buf, 4);
	if (raw_size % 4 != 0) {
		WARNING("BanMisc.LoadStruct invalid file format");
		return NULL;
	}
	buf += MD11_HEADER_SIZE;
	len -= MD11_HEADER_SIZE;

	// decrypt
	mt19937_xorcode(buf, len, MD11_ENCRYPT_KEY);

	// decompress
	uint8_t *raw_buf = xmalloc(raw_size);
	int r = uncompress(raw_buf, &raw_size, buf, len);
	if (r != Z_OK) {
		WARNING("BanMisc.LoadStruct uncompress failed: %d", r);
		free(raw_buf);
		return NULL;
	}

	// deserialize
	int nr_vars = raw_size / 4;
	struct page *tmp_page = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, nr_vars);
	tmp_page->array.rank = 1;
	for (int i = 0; i < nr_vars; i++) {
		tmp_page->values[i].i = LittleEndian_getDW(raw_buf, i * 4);
	}
	struct iarray_reader in;
	iarray_init_reader(&in, tmp_page, NULL);
	struct page *page = iarray_read_struct(&in, struct_type, true);
	free_page(tmp_page);
	free(raw_buf);
	return page;
}

static int BanMisc_LoadStruct(struct page **_page, struct string *file_name)
{
	struct page *page = *_page;
	if (page->type != STRUCT_PAGE) {
		VM_ERROR("BanMisc.LoadStruct of non-struct");
	}

	char *path = unix_path(file_name->text);
	size_t len;
	uint8_t *buf = file_read(path, &len);
	free(path);
	if (!buf) {
		return 0;
	}

	if (!memcmp(buf, md11_signature, sizeof(md11_signature))) {
		page = load_md11(page->index, buf, len);
		free(buf);
		if (!page)
			return 0;
		if (*_page) {
			delete_page_vars(*_page);
			free_page(*_page);
		}
		*_page = page;
		return 1;
	}
	// load from JSON (for backward compatibility)
	cJSON *json = cJSON_Parse((char *)buf);
	free(buf);
	if (!json) {
		WARNING("BanMisc.LoadStruct invalid file format");
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
	bgm_prepare(ch, num);
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
	return wav_prepare(ch, num);
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
