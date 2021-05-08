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
#include <GL/glew.h>
#include "system4.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/dlf.h"

#include "dungeon/dgn.h"
#include "dungeon/dtx.h"
#include "dungeon/dungeon.h"
#include "dungeon/event_markers.h"
#include "dungeon/mesh.h"
#include "dungeon/skybox.h"
#include "dungeon/tes.h"
#include "gfx/gfx.h"
#include "sact.h"
#include "sprite.h"
#include "vm.h"
#include "xsystem4.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

struct dungeon_context *dungeon_context_create(int surface)
{
	struct dungeon_context *ctx = xcalloc(1, sizeof(struct dungeon_context));
	ctx->surface = surface;
	struct sact_sprite *sp = sact_get_sprite(ctx->surface);
	if (!sp)
		VM_ERROR("DrawDungeon.Init: invalid surface %d", surface);

	// Dungeon scene will be rendered to this texture. Unlike other textures,
	// its (0,0) is at the bottom-left, so it needs to be flipped vertically
	// when displayed on the screen.
	struct texture *texture = sprite_get_texture(sp);
	texture->flip_y = true;

	glGenRenderbuffers(1, &ctx->depth_buffer);
	glBindRenderbuffer(GL_RENDERBUFFER, ctx->depth_buffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, texture->w, texture->h);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	mesh_init();
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

	for (int i = 0; i < ctx->nr_meshes; i++)
		mesh_free(ctx->meshes[i]);
	free(ctx->meshes);
	skybox_free(ctx->skybox);
	event_markers_free(ctx->events);

	glDeleteRenderbuffers(1, &ctx->depth_buffer);
	mesh_fini();

	struct sact_sprite *sp = sact_get_sprite(ctx->surface);
	if (sp)
		sprite_get_texture(sp)->flip_y = false;

	free(ctx);
}

static struct mesh **create_meshes(struct dtx *dtx, int *nr_meshes)
{
	const int nr_types = DTX_DOOR + 1;
	struct mesh **meshes = xcalloc(nr_types * dtx->nr_columns, sizeof(struct mesh *));
	for (int type = 0; type < nr_types; type++) {
		enum mesh_type mtype = type == DTX_STAIRS ? MESH_STAIRS : MESH_WALL;
		for (int i = 0; i < dtx->nr_columns; i++) {
			struct cg *cg = dtx_create_cg(dtx, type, i);
			if (!cg)
				continue;
			GLuint texture;
			glGenTextures(1, &texture);
			glBindTexture(GL_TEXTURE_2D, texture);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cg->metrics.w, cg->metrics.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, cg->pixels);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glGenerateMipmap(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, 0);
			meshes[type * dtx->nr_columns + i] = mesh_create(mtype, texture, cg->metrics.has_alpha);
			cg_free(cg);
		}
	}
	*nr_meshes = nr_types * dtx->nr_columns;
	return meshes;
}

static inline struct mesh *ctx_mesh(struct dungeon_context *ctx, int type, int index)
{
	return ctx->meshes[type * ctx->dtx->nr_columns + index];
}

static void populate_meshes(struct dungeon_context *ctx)
{
	struct dgn_cell *cell = ctx->dgn->cells;
	for (uint32_t z = 0; z < ctx->dgn->size_z; z++) {
		for (uint32_t y = 0; y < ctx->dgn->size_y; y++) {
			for (uint32_t x = 0; x < ctx->dgn->size_x; x++, cell++) {
				if (cell->floor >= 0)
					mesh_add_floor(ctx_mesh(ctx, DTX_FLOOR, cell->floor), x, y, z);
				if (cell->ceiling >= 0)
					mesh_add_ceiling(ctx_mesh(ctx, DTX_CEILING, cell->ceiling), x, y, z);
				if (cell->north_wall >= 0)
					mesh_add_north_wall(ctx_mesh(ctx, DTX_WALL, cell->north_wall), x, y, z);
				if (cell->south_wall >= 0)
					mesh_add_south_wall(ctx_mesh(ctx, DTX_WALL, cell->south_wall), x, y, z);
				if (cell->east_wall >= 0)
					mesh_add_east_wall(ctx_mesh(ctx, DTX_WALL, cell->east_wall), x, y, z);
				if (cell->west_wall >= 0)
					mesh_add_west_wall(ctx_mesh(ctx, DTX_WALL, cell->west_wall), x, y, z);
				if (cell->north_door >= 0)
					mesh_add_north_wall(ctx_mesh(ctx, DTX_DOOR, cell->north_door), x, y, z);
				if (cell->south_door >= 0)
					mesh_add_south_wall(ctx_mesh(ctx, DTX_DOOR, cell->south_door), x, y, z);
				if (cell->east_door >= 0)
					mesh_add_east_wall(ctx_mesh(ctx, DTX_DOOR, cell->east_door), x, y, z);
				if (cell->west_door >= 0)
					mesh_add_west_wall(ctx_mesh(ctx, DTX_DOOR, cell->west_door), x, y, z);
				if (cell->stairs_texture >= 0)
					mesh_add_stairs(ctx_mesh(ctx, DTX_STAIRS, cell->stairs_texture), x, y, z, cell->stairs_orientation);
				if (cell->floor_event)
					event_markers_set(ctx->events, x, y, z, cell->floor_event);
			}
		}
	}
}

bool dungeon_load(struct dungeon_context *ctx, int num)
{
	if (ctx->loaded)
		VM_ERROR("Dungeon is already loaded");

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
	if (!ctx->dgn || !ctx->dtx) {
		WARNING("Cannot load dungeon %d", num);
		return false;
	}

	ctx->meshes = create_meshes(ctx->dtx, &ctx->nr_meshes);
	ctx->skybox = skybox_create(ctx->dtx);
	ctx->events = event_markers_create();

	populate_meshes(ctx);

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

	// Render opaque objects.
	glDisable(GL_BLEND);
	for (int i = 0; i < ctx->nr_meshes; i++) {
		struct mesh *m = ctx->meshes[i];
		if (m && !mesh_is_transparent(m))
			mesh_render(m, local_transform[0], view_transform[0], proj_transform[0]);
	}

	// Render the skybox.
	skybox_render(ctx->skybox, view_transform[0], proj_transform[0]);

	// Render transparent objects.
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	for (int i = 0; i < ctx->nr_meshes; i++) {
		struct mesh *m = ctx->meshes[i];
		if (m && mesh_is_transparent(m))
			mesh_render(m, local_transform[0], view_transform[0], proj_transform[0]);
	}

	// Render event markers.
	event_markers_render(ctx->events, view_transform[0], proj_transform[0]);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
}

void dungeon_set_event_floor(int surface, int x, int y, int z, int event)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return;
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	cell->floor_event = event;
	event_markers_set(ctx->events, x, y, z, cell->floor_event);
}

void dungeon_set_event_blend_rate(int surface, int x, int y, int z, int rate)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return;
	event_markers_set_blend_rate(ctx->events, x, y, z, rate);
}

void dungeon_set_texture_floor(int surface, int x, int y, int z, int texture)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return;
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	if (cell->floor == texture)
		return;
	if (cell->floor >= 0)
		mesh_remove_floor(ctx_mesh(ctx, DTX_FLOOR, cell->floor), x, y, z);
	cell->floor = texture;
	mesh_add_floor(ctx_mesh(ctx, DTX_FLOOR, cell->floor), x, y, z);
}

void dungeon_set_texture_ceiling(int surface, int x, int y, int z, int texture)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return;
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	if (cell->ceiling == texture)
		return;
	if (cell->ceiling >= 0)
		mesh_remove_ceiling(ctx_mesh(ctx, DTX_CEILING, cell->ceiling), x, y, z);
	cell->ceiling = texture;
	mesh_add_ceiling(ctx_mesh(ctx, DTX_CEILING, cell->ceiling), x, y, z);
}

void dungeon_set_texture_north(int surface, int x, int y, int z, int texture)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return;
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	if (cell->north_wall == texture)
		return;
	if (cell->north_wall >= 0)
		mesh_remove_north_wall(ctx_mesh(ctx, DTX_WALL, cell->north_wall), x, y, z);
	cell->north_wall = texture;
	mesh_add_north_wall(ctx_mesh(ctx, DTX_WALL, cell->north_wall), x, y, z);
}

void dungeon_set_texture_south(int surface, int x, int y, int z, int texture)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return;
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	if (cell->south_wall == texture)
		return;
	if (cell->south_wall >= 0)
		mesh_remove_south_wall(ctx_mesh(ctx, DTX_WALL, cell->south_wall), x, y, z);
	cell->south_wall = texture;
	mesh_add_south_wall(ctx_mesh(ctx, DTX_WALL, cell->south_wall), x, y, z);
}

void dungeon_set_texture_east(int surface, int x, int y, int z, int texture)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return;
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	if (cell->east_wall == texture)
		return;
	if (cell->east_wall >= 0)
		mesh_remove_east_wall(ctx_mesh(ctx, DTX_WALL, cell->east_wall), x, y, z);
	cell->east_wall = texture;
	mesh_add_east_wall(ctx_mesh(ctx, DTX_WALL, cell->east_wall), x, y, z);
}

void dungeon_set_texture_west(int surface, int x, int y, int z, int texture)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return;
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	if (cell->west_wall == texture)
		return;
	if (cell->west_wall >= 0)
		mesh_remove_west_wall(ctx_mesh(ctx, DTX_WALL, cell->west_wall), x, y, z);
	cell->west_wall = texture;
	mesh_add_west_wall(ctx_mesh(ctx, DTX_WALL, cell->west_wall), x, y, z);
}

void dungeon_set_door_north(int surface, int x, int y, int z, int texture)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return;
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	if (cell->north_door == texture)
		return;
	if (cell->north_door >= 0)
		mesh_remove_north_wall(ctx_mesh(ctx, DTX_DOOR, cell->north_door), x, y, z);
	cell->north_door = texture;
	mesh_add_north_wall(ctx_mesh(ctx, DTX_DOOR, cell->north_door), x, y, z);
}

void dungeon_set_door_south(int surface, int x, int y, int z, int texture)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return;
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	if (cell->south_door == texture)
		return;
	if (cell->south_door >= 0)
		mesh_remove_south_wall(ctx_mesh(ctx, DTX_DOOR, cell->south_door), x, y, z);
	cell->south_door = texture;
	mesh_add_south_wall(ctx_mesh(ctx, DTX_DOOR, cell->south_door), x, y, z);
}

void dungeon_set_door_east(int surface, int x, int y, int z, int texture)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return;
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	if (cell->east_door == texture)
		return;
	if (cell->east_door >= 0)
		mesh_remove_east_wall(ctx_mesh(ctx, DTX_DOOR, cell->east_door), x, y, z);
	cell->east_door = texture;
	mesh_add_east_wall(ctx_mesh(ctx, DTX_DOOR, cell->east_door), x, y, z);
}

void dungeon_set_door_west(int surface, int x, int y, int z, int texture)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return;
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	if (cell->west_door == texture)
		return;
	if (cell->west_door >= 0)
		mesh_remove_west_wall(ctx_mesh(ctx, DTX_DOOR, cell->west_door), x, y, z);
	cell->west_door = texture;
	mesh_add_west_wall(ctx_mesh(ctx, DTX_DOOR, cell->west_door), x, y, z);
}
