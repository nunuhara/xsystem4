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
#include <zlib.h>

#include "system4/little_endian.h"
#include "system4/buffer.h"
#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "hll.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

enum ts3m_section {
	TS3M_MAP_INFO = 0,
	TS3M_OBJECT_INFO = 1,
	TS3M_BILLBOARD_INFO = 2,
	TS3M_MAP_DATA = 3,
	TS3M_MAP_SOUND = 4,
};

struct ts3m_header {
	uint32_t version;
	uint32_t offsets[5];
};

struct ts3m_info {
	uint32_t count;
	struct buffer buf;
};

static struct ts3m_info *ts3m_map_info;
static struct ts3m_info *ts3m_object_info;
static struct ts3m_info *ts3m_billboard_info;
static struct buffer *ts3m_read_buf;

static FILE *open_and_read_header(struct ts3m_header *header)
{
	char *path = gamedir_path("Data/Toushin3Map.dat");
	FILE *fp = file_open_utf8(path, "rb");
	free(path);

	uint8_t buf[28];
	if (fread(buf, sizeof buf, 1, fp) != 1)
		goto err_exit;
	if (memcmp(buf, "TS3M", 4))
		goto err_exit;
	header->version = LittleEndian_getDW(buf, 4);
	if (header->version != 100)
		goto err_exit;
	for (int i = 0; i < 5; i++)
		header->offsets[i] = LittleEndian_getDW(buf, 8 + i * 4);
	return fp;

 err_exit:
	fclose(fp);
	return NULL;
}

static uint8_t *read_compressed_data(FILE *fp, uint32_t *size)
{
	uint8_t *buf = malloc(*size);
	if (fread(buf, *size, 1, fp) != 1) {
		free(buf);
		return NULL;
	}
	unsigned long raw_size = LittleEndian_getDW(buf, 0);
	void *raw = malloc(raw_size);
	if (uncompress(raw, &raw_size, buf + 4, *size - 4) != Z_OK) {
		WARNING("uncompress failed");
		free(raw);
		free(buf);
		return NULL;
	}
	free(buf);

	*size = raw_size;
	return raw;
}

static struct ts3m_info *load_ts3m_info(enum ts3m_section section)
{
	struct ts3m_header header;
	FILE *fp = open_and_read_header(&header);
	if (!fp)
		return NULL;

	fseek(fp, header.offsets[section], SEEK_SET);

	uint8_t buf[8];
	if (fread(buf, sizeof buf, 1, fp) != 1) {
		fclose(fp);
		return NULL;
	}
	struct ts3m_info *info = xcalloc(1, sizeof(struct ts3m_info));
	info->count = LittleEndian_getDW(buf, 0);
	uint32_t size = LittleEndian_getDW(buf, 4);
	uint8_t *raw = read_compressed_data(fp, &size);
	fclose(fp);
	if (!raw) {
		free(info);
		return NULL;
	}
	buffer_init(&info->buf, raw, size);
	return info;
}

static void free_ts3m_info(struct ts3m_info *info)
{
	free(info->buf.buf);
	free(info);
}

static bool unload_ts3m_data(void)
{
	if (!ts3m_read_buf)
		return false;
	free(ts3m_read_buf->buf);
	free(ts3m_read_buf);
	ts3m_read_buf = NULL;
	return true;
}

static bool load_ts3m_data(enum ts3m_section section, int no)
{
	if (unload_ts3m_data()) {
		WARNING("Previously loaded data wasn't unloaded");
	}

	struct ts3m_header header;
	FILE *fp = open_and_read_header(&header);
	if (!fp)
		return false;

	fseek(fp, header.offsets[section], SEEK_SET);

	uint8_t buf[12];
	if (fread(buf, 4, 1, fp) != 1) {
		fclose(fp);
		return false;
	}
	int nr_data = LittleEndian_getDW(buf, 0);
	for (int i = 0; i < nr_data; i++) {
		if (fread(buf, 12, 1, fp) != 1) {
			fclose(fp);
			return false;
		}
		int data_no = LittleEndian_getDW(buf, 0);
		uint32_t size = LittleEndian_getDW(buf, 4);
		uint32_t offset = LittleEndian_getDW(buf, 8);
		if (data_no == no) {
			fseek(fp, offset, SEEK_SET);
			uint8_t *data = read_compressed_data(fp, &size);
			fclose(fp);
			if (!data)
				return false;
			ts3m_read_buf = xcalloc(1, sizeof(struct buffer));
			buffer_init(ts3m_read_buf, data, size);
			return true;
		}
	}
	WARNING("load_ts3m_data(%d, %d): not found", section, no);
	fclose(fp);
	return true;
}

static int32_t read_cstring(struct buffer *buf)
{
	struct string *s = cstr_to_string(buffer_strdata(buf));
	buffer_skip(buf, s->size + 1);
	return heap_alloc_string(s);
}

static bool Toushin3Loader_LoadMapInfo(void)
{
	if (!ts3m_map_info)
		ts3m_map_info = load_ts3m_info(TS3M_MAP_INFO);
	return !!ts3m_map_info;
}

static bool Toushin3Loader_UnloadMapInfo(void)
{
	if (!ts3m_map_info)
		return false;
	free_ts3m_info(ts3m_map_info);
	ts3m_map_info = NULL;
	return true;
}

static int Toushin3Loader_GetMapInfoCount(void)
{
	if (!ts3m_map_info)
		return -1;
	return ts3m_map_info->count;
}

static bool Toushin3Loader_GetMapInfo(struct page **page)
{
	if (!ts3m_map_info)
		return false;

	union vm_value *out = (*page)->values;  // struct tagMapInfo
	out[0].i = buffer_read_int32(&ts3m_map_info->buf);  // m_nNo
	out[1].i = read_cstring(&ts3m_map_info->buf);       // m_szName
	out[2].i = buffer_read_int32(&ts3m_map_info->buf);  // m_nX
	out[3].i = buffer_read_int32(&ts3m_map_info->buf);  // m_nY
	out[4].i = buffer_read_int32(&ts3m_map_info->buf);  // m_nDir
	out[5].i = read_cstring(&ts3m_map_info->buf);       // m_szName2
	return true;
}

static bool Toushin3Loader_LoadObjectInfo(void)
{
	if (!ts3m_object_info)
		ts3m_object_info = load_ts3m_info(TS3M_OBJECT_INFO);
	return !!ts3m_object_info;
}

static bool Toushin3Loader_UnloadObjectInfo(void)
{
	if (!ts3m_object_info)
		return false;
	free_ts3m_info(ts3m_object_info);
	ts3m_object_info = NULL;
	return true;
}

static int Toushin3Loader_GetObjectInfoCount(void)
{
	if (!ts3m_object_info)
		return -1;
	return ts3m_object_info->count;
}

bool Toushin3Loader_GetObjectInfo(struct page **page)
{
	if (!ts3m_object_info)
		return false;

	union vm_value *out = (*page)->values;  // struct tagObjectInfo
	out[0].i = buffer_read_int32(&ts3m_object_info->buf);  // m_nNo
	out[1].i = read_cstring(&ts3m_object_info->buf);       // m_szName
	out[2].i = buffer_read_int32(&ts3m_object_info->buf);  // m_nEnterFlag
	out[3].i = buffer_read_int32(&ts3m_object_info->buf);  // m_nState
	out[4].i = buffer_read_int32(&ts3m_object_info->buf);  // m_nBegin
	out[5].i = buffer_read_int32(&ts3m_object_info->buf);  // m_nEnd
	out[6].i = buffer_read_int32(&ts3m_object_info->buf);  // m_nLoopBegin
	out[7].i = buffer_read_int32(&ts3m_object_info->buf);  // m_nLoopEnd
	out[8].i = buffer_read_int32(&ts3m_object_info->buf);  // m_nFPS
	return true;
}

static bool Toushin3Loader_LoadBillboardInfo(void)
{
	if (!ts3m_billboard_info)
		ts3m_billboard_info = load_ts3m_info(TS3M_BILLBOARD_INFO);
	return !!ts3m_billboard_info;
}

static bool Toushin3Loader_UnloadBillboardInfo(void)
{
	if (!ts3m_billboard_info)
		return false;
	free_ts3m_info(ts3m_billboard_info);
	ts3m_billboard_info = NULL;
	return true;
}

static int Toushin3Loader_GetBillboardInfoCount(void)
{
	if (!ts3m_billboard_info)
		return -1;
	return ts3m_billboard_info->count;
}

static bool Toushin3Loader_GetBillboardInfo(struct page **page)
{
	if (!ts3m_billboard_info)
		return false;

	union vm_value *out = (*page)->values;  // struct tagBillboardInfo
	for (int i = 0; i < 14; i++)
		out[i].i = buffer_read_int32(&ts3m_billboard_info->buf);
	return true;
}

static bool Toushin3Loader_LoadMapData(int map_no)
{
	return load_ts3m_data(TS3M_MAP_DATA, map_no);
}

static bool Toushin3Loader_LoadMapSound(int map_no)
{
	return load_ts3m_data(TS3M_MAP_SOUND, map_no);
}

static bool Toushin3Loader_GetByte(int *data)
{
	if (!ts3m_read_buf || buffer_remaining(ts3m_read_buf) < 1)
		return false;
	*data = buffer_read_u8(ts3m_read_buf);
	return true;
}

static bool Toushin3Loader_GetInt(int *data)
{
	if (!ts3m_read_buf || buffer_remaining(ts3m_read_buf) < 4)
		return false;
	*data = buffer_read_int32(ts3m_read_buf);
	return true;
}

static bool Toushin3Loader_GetFloat(float *data)
{
	if (!ts3m_read_buf || buffer_remaining(ts3m_read_buf) < 4)
		return false;
	*data = buffer_read_float(ts3m_read_buf);
	return true;
}

//bool Toushin3Loader_GetString(struct string **pIString);  // unused

HLL_LIBRARY(Toushin3Loader,
	    HLL_EXPORT(LoadMapInfo, Toushin3Loader_LoadMapInfo),
	    HLL_EXPORT(UnloadMapInfo, Toushin3Loader_UnloadMapInfo),
	    HLL_EXPORT(GetMapInfoCount, Toushin3Loader_GetMapInfoCount),
	    HLL_EXPORT(GetMapInfo, Toushin3Loader_GetMapInfo),
	    HLL_EXPORT(LoadObjectInfo, Toushin3Loader_LoadObjectInfo),
	    HLL_EXPORT(UnloadObjectInfo, Toushin3Loader_UnloadObjectInfo),
	    HLL_EXPORT(GetObjectInfoCount, Toushin3Loader_GetObjectInfoCount),
	    HLL_EXPORT(GetObjectInfo, Toushin3Loader_GetObjectInfo),
	    HLL_EXPORT(LoadBillboardInfo, Toushin3Loader_LoadBillboardInfo),
	    HLL_EXPORT(UnloadBillboardInfo, Toushin3Loader_UnloadBillboardInfo),
	    HLL_EXPORT(GetBillboardInfoCount, Toushin3Loader_GetBillboardInfoCount),
	    HLL_EXPORT(GetBillboardInfo, Toushin3Loader_GetBillboardInfo),
	    HLL_EXPORT(LoadMapData, Toushin3Loader_LoadMapData),
	    HLL_EXPORT(UnloadMapData, unload_ts3m_data),
	    HLL_EXPORT(LoadMapSound, Toushin3Loader_LoadMapSound),
	    HLL_EXPORT(UnloadMapSound, unload_ts3m_data),
	    HLL_EXPORT(GetByte, Toushin3Loader_GetByte),
	    HLL_EXPORT(GetInt, Toushin3Loader_GetInt),
	    HLL_EXPORT(GetFloat, Toushin3Loader_GetFloat),
	    HLL_TODO_EXPORT(GetString, Toushin3Loader_GetString)
	    );
