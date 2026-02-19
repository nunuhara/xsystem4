/* Copyright (C) 2026 kichikuou <KichikuouChrome@gmail.com>
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

#include "system4/little_endian.h"
#include "system4/file.h"
#include "system4/string.h"
#include "system4/cg.h"

#include "hll.h"
#include "id_pool.h"
#include "xsystem4.h"
#include "gfx/gfx.h"
#include "sact.h"

// A MAD file consists of this header, followed by `nr_images` QNT images, each
// prefixed with its size.
struct MADHeader {
	char magic[4];  // "MAD\0"
	unsigned char unknown1[8];
	int width;
	int height;
	int total_time;
	int nr_images;
	int draw_method;
	int is_loop;
	unsigned char unknown2[8];
	int keep_last_frame;
};

struct MAD {
	struct MADHeader header;
	Texture textures[];
};

static struct id_pool pool;

static void MADLoader_ModuleInit(void)
{
	id_pool_init(&pool, 0);
}

static void MADLoader_Close(int handle)
{
	struct MAD *mad = id_pool_release(&pool, handle);
	if (!mad)
		return;
	for (int i = 0; i < mad->header.nr_images; i++) {
		if (mad->textures[i].handle)
			gfx_delete_texture(&mad->textures[i]);
	}
	free(mad);
}

static void MADLoader_Delete(void)
{
	for (int i = id_pool_get_first(&pool); i >= 0; i = id_pool_get_next(&pool, i))
		MADLoader_Close(i);
}

static void MADLoader_ModuleFini(void)
{
	MADLoader_Delete();
	id_pool_delete(&pool);
}

static FILE *mad_open(struct string *filename, struct MADHeader *header)
{
	char *path = gamedir_path(filename->text);
	FILE *fp = file_open_utf8(path, "rb");
	free(path);
	if (!fp)
		return NULL;

	uint8_t buf[48];
	if (fread(buf, sizeof buf, 1, fp) != 1 || memcmp(buf, "MAD", 4)) {
		fclose(fp);
		return NULL;
	}

	memcpy(header->magic, buf, 4);
	memcpy(header->unknown1, buf + 4, 8);
	header->width = LittleEndian_getDW(buf, 12);
	header->height = LittleEndian_getDW(buf, 16);
	header->total_time = LittleEndian_getDW(buf, 20);
	header->nr_images = LittleEndian_getDW(buf, 24);
	header->draw_method = LittleEndian_getDW(buf, 28);
	header->is_loop = LittleEndian_getDW(buf, 32);
	memcpy(header->unknown2, buf + 36, 8);
	header->keep_last_frame = LittleEndian_getDW(buf, 44);

	return fp;
}

static bool mad_read_header(struct string *filename, struct MADHeader *header)
{
	FILE *fp = mad_open(filename, header);
	fclose(fp);
	return fp != NULL;
}

static int MADLoader_Open(struct string *filename)
{
	struct MADHeader header;
	FILE *fp = mad_open(filename, &header);
	if (!fp)
		return -1;

	struct MAD *mad = xcalloc(1, sizeof(struct MAD) + header.nr_images * sizeof(Texture));
	mad->header = header;
	for (int i = 0; i < mad->header.nr_images; i++) {
		uint8_t buf[4];
		if (fread(buf, 4, 1, fp) != 1)
			VM_ERROR("MADLoader: failed to read image size %d of %s", i, display_sjis0(filename->text));
		uint32_t image_size = LittleEndian_getDW(buf, 0);
		uint8_t *image_data = xmalloc(image_size);
		if (fread(image_data, image_size, 1, fp) != 1)
			VM_ERROR("MADLoader: failed to read image data %d of %s", i, display_sjis0(filename->text));
		struct cg *cg = cg_load_buffer(image_data, image_size);
		if (!cg)
			VM_ERROR("MADLoader: failed to load cg %d of %s", i, display_sjis0(filename->text));
		gfx_init_texture_with_cg(&mad->textures[i], cg);
		cg_free(cg);
		free(image_data);
	}
	fclose(fp);

	int handle = id_pool_get_unused(&pool);
	id_pool_set(&pool, handle, mad);
	return handle;
}

static bool MADLoader_GetSize(int handle, int *width, int *height)
{
	struct MAD *mad = id_pool_get(&pool, handle);
	if (!mad)
		return false;
	*width = mad->header.width;
	*height = mad->header.height;
	return true;
}

static int MADLoader_GetNumof(int handle)
{
	struct MAD *mad = id_pool_get(&pool, handle);
	return mad ? mad->header.nr_images : -1;
}

static int MADLoader_GetTotalTime(int handle)
{
	struct MAD *mad = id_pool_get(&pool, handle);
	return mad ? mad->header.total_time : -1;
}

static int MADLoader_GetDrawMethod(int handle)
{
	struct MAD *mad = id_pool_get(&pool, handle);
	return mad ? mad->header.draw_method : -1;
}

static bool MADLoader_IsLoop(int handle)
{
	struct MAD *mad = id_pool_get(&pool, handle);
	return mad && mad->header.is_loop == 1;
}

static bool MADLoader_IsKeepLastFrame(int handle)
{
	struct MAD *mad = id_pool_get(&pool, handle);
	return mad && mad->header.keep_last_frame == 1;
}

static bool MADLoader_IsLoading(int handle)
{
	// We don't support background loading.
	return false;
}

static int MADLoader_GetLoadNumof(int handle)
{
	struct MAD *mad = id_pool_get(&pool, handle);
	return mad ? mad->header.nr_images : 0;
}

static bool MADLoader_GetSizeFromFile(struct string *filename, int *width, int *height)
{
	struct MADHeader header;
	if (!mad_read_header(filename, &header))
		return false;
	*width = header.width;
	*height = header.height;
	return true;
}

static int MADLoader_GetNumofFromFile(struct string *filename)
{
	struct MADHeader header;
	if (!mad_read_header(filename, &header))
		return -1;
	return header.nr_images;
}

static int MADLoader_GetTotalTimeFromFile(struct string *filename)
{
	struct MADHeader header;
	if (!mad_read_header(filename, &header))
		return -1;
	return header.total_time;
}

static int MADLoader_GetDrawMethodFromFile(struct string *filename)
{
	struct MADHeader header;
	if (!mad_read_header(filename, &header))
		return -1;
	return header.draw_method;
}

static bool MADLoader_IsLoopFromFile(struct string *filename)
{
	struct MADHeader header;
	if (!mad_read_header(filename, &header))
		return false;
	return header.is_loop == 1;
}

static bool MADLoader_IsKeepLastFrameFromFile(struct string *filename)
{
	struct MADHeader header;
	if (!mad_read_header(filename, &header))
		return false;
	return header.keep_last_frame == 1;
}

static Texture *get_mad_texture(int handle, int index)
{
	struct MAD *mad = id_pool_get(&pool, handle);
	if (!mad || index < 0 || index >= mad->header.nr_images)
		return NULL;
	return &mad->textures[index];
}

static Texture *get_dest_texture(int sp_no)
{
	struct sact_sprite *sp = sact_get_sprite(sp_no);
	if (!sp)
		return NULL;
	sprite_dirty(sp);
	return sprite_get_texture(sp);
}

static bool MADLoader_Copy(int handle, int sprite, int index)
{
	Texture *src = get_mad_texture(handle, index);
	Texture *dst = get_dest_texture(sprite);
	if (!src || !dst)
		return false;

	gfx_copy_with_alpha_map(dst, 0, 0, src, 0, 0, src->w, src->h);
	return true;
}

static bool MADLoader_CopyReverseLR(int handle, int sprite, int index)
{
	Texture *src = get_mad_texture(handle, index);
	Texture *dst = get_dest_texture(sprite);
	if (!src || !dst)
		return false;

	gfx_copy_reverse_LR_with_alpha_map(dst, 0, 0, src, 0, 0, src->w, src->h);
	return true;
}

static bool MADLoader_CopyGrayscale(int handle, int sprite, int index)
{
	Texture *src = get_mad_texture(handle, index);
	Texture *dst = get_dest_texture(sprite);
	if (!src || !dst)
		return false;

	gfx_copy_grayscale_with_alpha_map(dst, 0, 0, src, 0, 0, src->w, src->h);
	return true;
}

static bool MADLoader_CopyGrayscaleReverseLR(int handle, int sprite, int index)
{
	Texture *src = get_mad_texture(handle, index);
	Texture *dst = get_dest_texture(sprite);
	if (!src || !dst)
		return false;

	gfx_copy_grayscale_reverse_LR_with_alpha_map(dst, 0, 0, src, 0, 0, src->w, src->h);
	return true;
}

HLL_LIBRARY(MADLoader,
	    HLL_EXPORT(_ModuleInit, MADLoader_ModuleInit),
	    HLL_EXPORT(_ModuleFini, MADLoader_ModuleFini),
	    HLL_EXPORT(Delete, MADLoader_Delete),
	    HLL_EXPORT(Open, MADLoader_Open),
	    HLL_EXPORT(Close, MADLoader_Close),
	    HLL_EXPORT(GetSize, MADLoader_GetSize),
	    HLL_EXPORT(GetNumof, MADLoader_GetNumof),
	    HLL_EXPORT(GetTotalTime, MADLoader_GetTotalTime),
	    HLL_EXPORT(GetDrawMethod, MADLoader_GetDrawMethod),
	    HLL_EXPORT(IsLoop, MADLoader_IsLoop),
	    HLL_EXPORT(IsKeepLastFrame, MADLoader_IsKeepLastFrame),
	    HLL_EXPORT(IsLoading, MADLoader_IsLoading),
	    HLL_EXPORT(GetLoadNumof, MADLoader_GetLoadNumof),
	    HLL_EXPORT(GetSizeFromFile, MADLoader_GetSizeFromFile),
	    HLL_EXPORT(GetNumofFromFile, MADLoader_GetNumofFromFile),
	    HLL_EXPORT(GetTotalTimeFromFile, MADLoader_GetTotalTimeFromFile),
	    HLL_EXPORT(GetDrawMethodFromFile, MADLoader_GetDrawMethodFromFile),
	    HLL_EXPORT(IsLoopFromFile, MADLoader_IsLoopFromFile),
	    HLL_EXPORT(IsKeepLastFrameFromFile, MADLoader_IsKeepLastFrameFromFile),
	    HLL_EXPORT(Copy, MADLoader_Copy),
	    HLL_EXPORT(CopyReverseLR, MADLoader_CopyReverseLR),
	    HLL_EXPORT(CopyGrayscale, MADLoader_CopyGrayscale),
	    HLL_EXPORT(CopyGrayscaleReverseLR, MADLoader_CopyGrayscaleReverseLR)
	    );
