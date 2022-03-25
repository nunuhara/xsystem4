/* Copyright (C) 2022 kichikuou <KichikuouChrome@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/hashtable.h"
#include "system4/mt19937int.h"
#include "system4/string.h"
#include "little_endian.h"

#include "audio.h"
#include "sact.h"
#include "xsystem4.h"

#include "hll.h"

/*
 * struct ChrHeader {
 *   char magic[4];  // "CHR\0"
 *   // the below are encrypted
 *   char type[4];  // "Mine" or "Enmy"
 * };  // Followed by a MineIndex or an EnmyIndex
 *
 * struct MineIndex {
 *   uint32_t info_offset;
 *   uint32_t cg_offset[4];
 *   uint32_t cg_size[4];
 *   uint32_t sound_offset;
 *   uint32_t sound_size;
 *   uint32_t string_offset[48];
 * };
 *
 * struct MineInfo {
 *   uint32_t unknown[6];
 *   uint32_t status[24];
 *   char name[32];
 * };
 *
 * struct EnmyIndex {
 *   uint32_t info_offset;
 *   uint32_t cg_offset[3];
 *   uint32_t cg_size[3];
 *   uint32_t sound_offset;
 *   uint32_t sound_size;
 * };
 *
 * struct EnmyInfo {
 *   uint32_t unknown[5];
 *   uint32_t status[17];
 *   char name[32];
 * };
 */

static struct hash_table *chr_table;  // Caches decrypted .chr file data

static bool is_enemy(uint8_t *data)
{
	return !memcmp(data + 4, "Enmy", 4);
}

static uint8_t *chr_get(int id)
{
	struct ht_slot *slot = ht_put_int(chr_table, id, NULL);
	if (slot->value)
		return slot->value;

	char buf[32];
	sprintf(buf, "CHR/%d.chr", id);
	char *path = gamedir_path(buf);
	size_t size;
	uint8_t *data = file_read(path, &size);
	if (!data) {
		// WARNING("Cannot load %s", path);
	} else if (memcmp(data, "CHR", 4)) {
		WARNING("%s: Wrong magic bytes", path);
		free(data);
		data = NULL;
	} else {
		struct mt19937 mt;
		mt19937_init(&mt, 12753);
		for (size_t i = 4; i < size; i++)
			data[i] ^= mt19937_genrand(&mt);
		slot->value = data;
	}
	free(path);
	return data;
}

static bool ChrLoader_Init(void)
{
	chr_table = ht_create(512);
	return true;
}

static bool ChrLoader_LoadCG(int sprite, int x, int y, int id, int type)
{
	uint8_t *data = chr_get(id);
	if (!data)
		return false;

	struct sact_sprite *sp = sact_get_sprite(sprite);
	if (!sp)
		return false;

	int index = -1;
	switch (type) {
	case 0: // Unit
		index = is_enemy(data) ? 0 : 1;
		break;
	case 1: // Attack
		index = is_enemy(data) ? 1 : 2;
		break;
	case 2: // Damage
		index = is_enemy(data) ? 2 : 3;
		break;
	case 3: // Face
		index = is_enemy(data) ? -1 : 0;
		break;
	}
	if (index < 0)
		return false;

	size_t offset = LittleEndian_getDW(data, 12 + index * 4);
	size_t size = LittleEndian_getDW(data, (is_enemy(data) ? 24 : 28) + index * 4);
	struct cg *cg = cg_load_buffer(data + offset, size);
	if (!cg)
		return false;
	sprite_set_cg(sp, cg);
	cg_free(cg);
	return true;
}

static void chr_archive_free_data(struct archive_data *data) { /* no-op */ }

static struct archive_ops chr_archive_ops = {
	.free_data = chr_archive_free_data
};

static struct archive chr_archive = { .mmapped = false, .ops = &chr_archive_ops };

static bool ChrLoader_LoadWavFile(int sound_channel, int id)
{
	uint8_t *data = chr_get(id);
	if (!data)
		return false;

	size_t offset = LittleEndian_getDW(data, is_enemy(data) ? 36 : 44);
	size_t size = LittleEndian_getDW(data, is_enemy(data) ? 40 : 48);

	struct archive_data *dfile = xcalloc(1, sizeof(struct archive_data));
	dfile->size = size;
	dfile->data = data + offset;
	dfile->no = id;
	dfile->archive = &chr_archive;

	if (!wav_prepare_from_archive_data(sound_channel, dfile)) {
		WARNING("ChrLoader.LoadWavFile %d failed", id);
		return false;
	}
	return true;
}

static bool ChrLoader_LoadSentence(int id, int type, struct string **msg)
{
	uint8_t *data = chr_get(id);
	if (!data || is_enemy(data) || (unsigned)type >= 48)
		return false;

	uint32_t offset = LittleEndian_getDW(data, 52 + type * 4);
	if (!offset)
		return false;
	*msg = cstr_to_string((char *)data + offset);
	return true;
}

static bool ChrLoader_GetName(int id, struct string **name)
{
	uint8_t *data = chr_get(id);
	if (!data)
		return false;

	uint32_t offset = LittleEndian_getDW(data, 8);
	offset += is_enemy(data) ? 88 : 120;
	*name = cstr_to_string((char *)data + offset);
	return true;
}

static int ChrLoader_GetStatus(int id, int type)
{
	uint8_t *data = chr_get(id);
	if (!data)
		return false;

	const int mine_indices[26] = {
		 6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
		19, 20, 23, 24, 21, 22, -1, -1, 25, 26, 27, 28, 29
	};
	const int enmy_indices[26] = {
		 5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, 17, 18, 19, 20, 21
	};
	int index = (unsigned)type >= 26 ? -1
		: is_enemy(data) ? enmy_indices[type] : mine_indices[type];
	if (index < 0) {
		WARNING("nType Error nType==%d", type);
		return false;
	}

	uint32_t offset = LittleEndian_getDW(data, 8) + index * 4;
	return LittleEndian_getDW(data, offset);
}

static bool ChrLoader_IsExist(int id)
{
	return chr_get(id) != NULL;
}

HLL_LIBRARY(ChrLoader,
	    HLL_EXPORT(Init, ChrLoader_Init),
	    HLL_EXPORT(LoadCG, ChrLoader_LoadCG),
	    HLL_EXPORT(LoadWavFile, ChrLoader_LoadWavFile),
	    HLL_EXPORT(LoadSentence, ChrLoader_LoadSentence),
	    HLL_EXPORT(GetName, ChrLoader_GetName),
	    HLL_EXPORT(GetStatus, ChrLoader_GetStatus),
	    HLL_EXPORT(IsExist, ChrLoader_IsExist)
	    );
