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
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <cglm/cglm.h>
#include "gfx/gl.h"
#include "system4.h"
#include "system4/alk.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/dlf.h"
#include "system4/file.h"

#include "dungeon/dgn.h"
#include "dungeon/dtx.h"
#include "dungeon/dungeon.h"
#include "dungeon/map.h"
#include "dungeon/polyobj.h"
#include "dungeon/renderer.h"
#include "dungeon/tes.h"
#include "gfx/gfx.h"
#include "sact.h"
#include "sprite.h"
#include "vm.h"
#include "vm/page.h"
#include "xsystem4.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

struct dungeon_context *dungeon_context_create(enum draw_dungeon_version version, int surface)
{
	struct dungeon_context *ctx = xcalloc(1, sizeof(struct dungeon_context));
	ctx->version = version;
	ctx->surface = surface;
	struct sact_sprite *sp = sact_get_sprite(ctx->surface);
	if (!sp)
		VM_ERROR("DrawDungeon.Init: invalid surface %d", surface);

	// Dungeon scene will be rendered to this texture.
	struct texture *texture = sprite_get_texture(sp);

	glGenRenderbuffers(1, &ctx->depth_buffer);
	glBindRenderbuffer(GL_RENDERBUFFER, ctx->depth_buffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, texture->w, texture->h);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	ctx->map = dungeon_map_create(version);
	return ctx;
}

void dungeon_context_free(struct dungeon_context *ctx)
{
	if (ctx->dgn)
		dgn_free(ctx->dgn);
	if (ctx->dtx)
		dtx_free(ctx->dtx);
	if (ctx->tes)
		tes_free(ctx->tes);

	if (ctx->renderer)
		dungeon_renderer_free(ctx->renderer);

	glDeleteRenderbuffers(1, &ctx->depth_buffer);
	if (ctx->map)
		dungeon_map_free(ctx->map);

	free(ctx);
}

static GLuint *load_event_textures(int *nr_textures_out)
{
	char *path = gamedir_path("Data/Event.alk");
	int error = ARCHIVE_SUCCESS;
	struct alk_archive *alk = alk_open(path, ARCHIVE_MMAP, &error);
	if (error == ARCHIVE_FILE_ERROR) {
		WARNING("alk_open(\"%s\"): %s", path, strerror(errno));
	} else if (error == ARCHIVE_BAD_ARCHIVE_ERROR) {
		WARNING("alk_open(\"%s\"): invalid .alk file", path);
	}
	free(path);
	if (!alk)
		return NULL;

	GLuint *textures = xcalloc(alk->nr_files, sizeof(GLuint));
	glGenTextures(alk->nr_files, textures);
	for (int i = 0; i < alk->nr_files; i++) {
		struct archive_data *dfile = archive_get(&alk->ar, i);
		if (!dfile)
			continue;
		struct cg *cg = cg_load_data(dfile);
		archive_free_data(dfile);
		if (!cg) {
			WARNING("Event.alk: cannot load cg %d", i);
			continue;
		}
		glBindTexture(GL_TEXTURE_2D, textures[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cg->metrics.w, cg->metrics.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, cg->pixels);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glGenerateMipmap(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, 0);
		cg_free(cg);
	}
	*nr_textures_out = alk->nr_files;
	archive_free(&alk->ar);
	return textures;
}

bool dungeon_load(struct dungeon_context *ctx, int num)
{
	struct polyobj *polyobj = NULL;

	if (ctx->loaded)
		VM_ERROR("Dungeon is already loaded");

	if (ctx->version == DRAW_DUNGEON_1) {
		char *path = gamedir_path("Data/DungeonData.dlf");
		int error = ARCHIVE_SUCCESS;
		struct archive *dlf = (struct archive *)dlf_open(path, ARCHIVE_MMAP, &error);
		if (error == ARCHIVE_FILE_ERROR) {
			WARNING("dlf_open(\"%s\"): %s", path, strerror(errno));
		} else if (error == ARCHIVE_BAD_ARCHIVE_ERROR) {
			WARNING("dlf_open(\"%s\"): invalid .dlf file", path);
		}
		free(path);
		if (!dlf)
			return false;

		struct archive_data *dgn = archive_get(dlf, num * 3);
		if (dgn) {
			ctx->dgn = dgn_parse(dgn->data, dgn->size);
			archive_free_data(dgn);
		}
		struct archive_data *dtx = archive_get(dlf, num * 3 + 1);
		if (dtx) {
			ctx->dtx = dtx_parse(dtx->data, dtx->size);
			archive_free_data(dtx);
		}
		struct archive_data *tes = archive_get(dlf, num * 3 + 2);
		if (tes) {
			ctx->tes = tes_parse(tes->data, tes->size);
			archive_free_data(tes);
		}
		archive_free(dlf);
	} else if (ctx->version == DRAW_DUNGEON_14) {
		char buf[100];
		sprintf(buf, "Data/map%02d.dgn", num);
		char *path = gamedir_path(buf);

		size_t len;
		uint8_t *dgn = file_read(path, &len);
		if (dgn) {
			ctx->dgn = dgn_parse(dgn, len);
			free(dgn);
		}

		strcpy(path + strlen(path) - 3, "dtx");
		uint8_t *dtx = file_read(path, &len);
		if (dtx) {
			ctx->dtx = dtx_parse(dtx, len);
			free(dtx);
		}

		strcpy(path + strlen(path) - 3, "tes");
		uint8_t *tes = file_read(path, &len);
		if (tes) {
			ctx->tes = tes_parse(tes, len);
			free(tes);
		}
		free(path);

		char *polyobj_path = gamedir_path("Data/PolyObj.lin");
		polyobj = polyobj_load(polyobj_path);
		free(polyobj_path);
		if (!polyobj) {
			WARNING("Cannot load Data/PolyObj.lin");
			return false;
		}
	} else {
		ERROR("Invalid DrawDungeon version %d", ctx->version);
	}

	if (!ctx->dgn || !ctx->dtx) {
		WARNING("Cannot load dungeon %d", num);
		return false;
	}

	int nr_event_textures;
	GLuint *event_textures = load_event_textures(&nr_event_textures);
	if (!event_textures)
		return false;

	ctx->renderer = dungeon_renderer_create(ctx->version, ctx->dtx, event_textures, nr_event_textures, polyobj);

	if (polyobj)
		polyobj_free(polyobj);

	dungeon_map_init(ctx);

	ctx->loaded = true;
	return true;
}

void dungeon_set_camera(int surface, float x, float y, float z, float angle, float angle_p)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return;
	ctx->camera.pos[0] = x;
	ctx->camera.pos[1] = y;
	ctx->camera.pos[2] = -z;
	ctx->camera.angle = -angle * M_PI / 180;
	ctx->camera.angle_p = angle_p * M_PI / 180;
}

static void model_view_matrix(struct camera *camera, mat4 out)
{
	// The actual camera position is slightly behind camera->pos.
	float dist = 0.9;
	vec3 d = {
		dist * sin(camera->angle) * cos(camera->angle_p),
		dist * sin(camera->angle_p),
		dist * -cos(camera->angle) * cos(camera->angle_p),
	};
	vec3 eye;
	glm_vec3_sub(camera->pos, d, eye);
	vec3 up = {0.0, 1.0, 0.0};
	glm_lookat(eye, camera->pos, up, out);
}

void dungeon_render(struct dungeon_context *ctx)
{
	struct sact_sprite *sp = sact_get_sprite(ctx->surface);
	sprite_dirty(sp);
	struct texture *texture = sprite_get_texture(sp);

	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, texture, 0, 0, texture->w, texture->h);
	glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, ctx->depth_buffer);
	if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		ERROR("Incomplete framebuffer");

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	mat4 local_transform, view_transform, proj_transform;
	glm_mat4_identity(local_transform);
	model_view_matrix(&ctx->camera, view_transform);
	glm_perspective(M_PI / 3.0, (float)texture->w / texture->h, 0.5, 100.0, proj_transform);

	// Tweak the projection transform so that the rendering result is vertically
	// flipped. If we render the scene normally, the resulting image will be
	// bottom-up (the first pixel is at the bottom-left), but we want a top-down
	// image (the first pixel is at the top-left).
	proj_transform[1][1] *= -1;
	glFrontFace(GL_CW);

	int dgn_x = round(ctx->camera.pos[0] / 2.0);
	int dgn_y = round(ctx->camera.pos[1] / 2.0);
	int dgn_z = round(ctx->camera.pos[2] / -2.0);
	int nr_cells;
	struct dgn_cell **cells = dgn_get_visible_cells(ctx->dgn, dgn_x, dgn_y, dgn_z, &nr_cells);
	dungeon_renderer_render(ctx->renderer, cells, nr_cells, view_transform, proj_transform);

	glFrontFace(GL_CCW);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
}

static void neighbor_reveal(struct dungeon_context *ctx, int x, int y, int z)
{
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	if (cell->floor < 0) {
		cell->walked = 1;
		dungeon_map_reveal(ctx, x, y, z, false);
	}
}

void dungeon_set_walked(int surface, int x, int y, int z, int flag)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return;
	dgn_cell_at(ctx->dgn, x, y, z)->walked = 1;
	dungeon_map_reveal(ctx, x, y, z, false);
	if (ctx->version == DRAW_DUNGEON_1) {
		if (x > 0)
			neighbor_reveal(ctx, x - 1, y, z);
		if (x + 1u < ctx->dgn->size_x)
			neighbor_reveal(ctx, x + 1, y, z);
		if (z > 0)
			neighbor_reveal(ctx, x, y, z - 1);
		if (z + 1u < ctx->dgn->size_z)
			neighbor_reveal(ctx, x, y, z + 1);
	}
}

int dungeon_get_walked(int surface, int x, int y, int z)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return 0;
	return dgn_cell_at(ctx->dgn, x, y, z)->walked;
}

int dungeon_calc_conquer(int surface)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return 0;
	return dgn_calc_conquer(ctx->dgn);
}

/*
 * WalkData serialization format:
 *
 * struct WalkData {
 *     int version;  // zero
 *     int nr_maps;
 *     struct {
 *         int map_no;
 *         int size_x, size_y, size_z;
 *         int cells[size_x * size_y * size_z];
 *     } maps[nr_maps];
 * };
 */

enum {
	WALK_DATA_NOT_FOUND = -1,
	WALK_DATA_BROKEN = -2,
};

static int find_walk_data(struct page *array, int map_no, struct dgn *dgn)
{
	if (!array)
		return WALK_DATA_BROKEN;
	if (array->type != ARRAY_PAGE || array->a_type != AIN_ARRAY_INT || array->array.rank != 1)
		VM_ERROR("Not a flat integer array");

	int ptr = 0;
	if (array->nr_vars < 2 || array->values[ptr++].i != 0)
		return WALK_DATA_BROKEN;
	int nr_maps = array->values[ptr++].i;

	for (int i = 0; i < nr_maps; i++) {
		if (array->nr_vars < ptr + 4)
			return WALK_DATA_BROKEN;
		int no = array->values[ptr].i;
		uint32_t size_x = array->values[ptr + 1].i;
		uint32_t size_y = array->values[ptr + 2].i;
		uint32_t size_z = array->values[ptr + 3].i;
		int nr_cells = size_x * size_y * size_z;
		if (no == map_no) {
			if (size_x != dgn->size_x || size_y != dgn->size_y || size_z != dgn->size_z)
				return WALK_DATA_BROKEN;
			if (ptr + 4 + nr_cells > array->nr_vars)
				return WALK_DATA_BROKEN;
			return ptr;
		}
		ptr += 4 + nr_cells;
	}
	return WALK_DATA_NOT_FOUND;
}

bool dungeon_load_walk_data(int surface, int map, struct page **page)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return false;
	struct page *array = *page;

	int offset = find_walk_data(array, map, ctx->dgn);
	if (offset < 0)
		return false;

	offset += 4;
	int nr_cells = dgn_nr_cells(ctx->dgn);
	for (struct dgn_cell *c = ctx->dgn->cells; c < ctx->dgn->cells + nr_cells; c++) {
		if (array->values[offset].i)
			dungeon_map_reveal(ctx, c->x, c->y, c->z, true);
		c->walked = array->values[offset++].i;
	}

	return true;
}

bool dungeon_save_walk_data(int surface, int map, struct page **page)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return false;
	struct page *array = *page;
	int nr_cells = dgn_nr_cells(ctx->dgn);

	int offset = find_walk_data(array, map, ctx->dgn);
	if (offset == WALK_DATA_NOT_FOUND) {
		offset = array->nr_vars;
		array->values[1].i += 1;
		union vm_value dim = { .i = array->nr_vars + 4 + nr_cells };
		*page = array = realloc_array(array, 1, &dim, AIN_ARRAY_INT, 0, false);
	} else if (offset == WALK_DATA_BROKEN) {
		if (array)
			WARNING("Discarding broken walk data");
		union vm_value dim = { .i = 2 + 4 + nr_cells };
		*page = array = realloc_array(array, 1, &dim, AIN_ARRAY_INT, 0, false);
		array->values[0].i = 0;
		array->values[1].i = 1;
		offset = 2;
	}
	array->values[offset++].i = map;
	array->values[offset++].i = ctx->dgn->size_x;
	array->values[offset++].i = ctx->dgn->size_y;
	array->values[offset++].i = ctx->dgn->size_z;
	for (int i = 0; i < nr_cells; i++)
		array->values[offset++].i = ctx->dgn->cells[i].walked;
	return true;
}
