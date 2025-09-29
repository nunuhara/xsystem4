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
#include "dungeon/generator.h"
#include "dungeon/map.h"
#include "dungeon/polyobj.h"
#include "dungeon/renderer.h"
#include "dungeon/tes.h"
#include "gfx/gfx.h"
#include "cJSON.h"
#include "json.h"
#include "sact.h"
#include "sprite.h"
#include "vm.h"
#include "vm/page.h"
#include "xsystem4.h"

static const char plugin_name[] = "DrawDungeon";

static void dungeon_render(struct sact_sprite *sp);
static cJSON *dungeon_to_json(struct sact_sprite *sp, bool verbose);
static void dungeon_context_free(struct draw_plugin *plugin);

struct dungeon_context *dungeon_get_context(int surface)
{
	struct sact_sprite *sp = sact_try_get_sprite(surface);
	if (!sp || !sp->plugin || sp->plugin->name != plugin_name)
		return NULL;
	return (struct dungeon_context *)sp->plugin;
}

struct dungeon_context *dungeon_context_create(enum draw_dungeon_version version, int width, int height)
{
	struct dungeon_context *ctx = xcalloc(1, sizeof(struct dungeon_context));
	ctx->plugin.name = plugin_name;
	ctx->plugin.free = dungeon_context_free;
	ctx->plugin.update = dungeon_render;
	ctx->plugin.to_json = dungeon_to_json;
	ctx->version = version;

	gfx_init_texture_blank(&ctx->texture, width, height);
	glGenRenderbuffers(1, &ctx->depth_buffer);
	glBindRenderbuffer(GL_RENDERBUFFER, ctx->depth_buffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glm_perspective(GLM_PIf / 3.f, (float)width / height, 0.5f, 100.f, ctx->proj_transform);

	ctx->map = dungeon_map_create(version);

	if (version == DRAW_FIELD) {
		ctx->characters = xcalloc(DRAWFIELD_NR_CHARACTERS, sizeof(struct drawfield_character));
	}
	return ctx;
}

static void dungeon_context_free(struct draw_plugin *plugin)
{
	struct dungeon_context *ctx = (struct dungeon_context *)plugin;
	if (ctx->dgn)
		dgn_free(ctx->dgn);
	if (ctx->dtx)
		dtx_free(ctx->dtx);
	if (ctx->tes)
		tes_free(ctx->tes);
	if (ctx->characters)
		free(ctx->characters);

	if (ctx->renderer)
		dungeon_renderer_free(ctx->renderer);

	gfx_delete_texture(&ctx->texture);
	glDeleteRenderbuffers(1, &ctx->depth_buffer);
	if (ctx->map)
		dungeon_map_free(ctx->map);

	free(ctx);
}

static GLuint *load_event_textures(int *nr_textures_out)
{
	char *path = gamedir_path("Data/Event.alk");
	int error = ARCHIVE_SUCCESS;
	struct alk_archive *alk = alk_open(path, MMAP_IF_64BIT, &error);
	if (error == ARCHIVE_FILE_ERROR) {
		WARNING("alk_open(\"%s\"): %s", display_utf0(path), strerror(errno));
	} else if (error == ARCHIVE_BAD_ARCHIVE_ERROR) {
		WARNING("alk_open(\"%s\"): invalid .alk file", display_utf0(path));
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
		struct archive *dlf = (struct archive *)dlf_open(path, MMAP_IF_64BIT, &error);
		if (error == ARCHIVE_FILE_ERROR) {
			WARNING("dlf_open(\"%s\"): %s", display_utf0(path), strerror(errno));
		} else if (error == ARCHIVE_BAD_ARCHIVE_ERROR) {
			WARNING("dlf_open(\"%s\"): invalid .dlf file", display_utf0(path));
		}
		free(path);
		if (!dlf)
			return false;

		struct archive_data *dgn = archive_get(dlf, num * 3);
		if (dgn) {
			ctx->dgn = dgn_parse(dgn->data, dgn->size, false);
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
	} else if (ctx->version == DRAW_DUNGEON_2) {
		ctx->dgn = dgn_generate_drawdungeon2(num);
		char *dtl_path = gamedir_path("Data/Texture.dtl");
		ctx->dtx = dtx_load_from_dtl(dtl_path, (num * 25) | 1);
		free(dtl_path);
	} else if (ctx->version == DRAW_DUNGEON_14) {
		char buf[100];
		sprintf(buf, "Data/map%02d.dgn", num);
		char *path = gamedir_path(buf);

		size_t len;
		uint8_t *dgn = file_read(path, &len);
		if (dgn) {
			ctx->dgn = dgn_parse(dgn, len, false);
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

	ctx->renderer = dungeon_renderer_create(ctx->version, num, ctx->dtx, event_textures, nr_event_textures, polyobj);

	if (polyobj)
		polyobj_free(polyobj);

	dungeon_map_init(ctx);

	ctx->loaded = true;
	return true;
}

bool dungeon_load_dungeon(struct dungeon_context *ctx, const char *filename, int num)
{
	char *path = gamedir_path(filename);
	size_t len;
	uint8_t *dgn = file_read(path, &len);
	if (!dgn) {
		WARNING("Cannot load %s", path);
		free(path);
		return false;
	}
	free(path);
	if (ctx->dgn)
		dgn_free(ctx->dgn);
	ctx->dgn = dgn_parse(dgn, len, ctx->version == DRAW_FIELD);
	free(dgn);
	if (!ctx->dgn)
		return false;

	dungeon_map_init(ctx);

	ctx->loaded = ctx->dgn && ctx->dtx;
	return true;
}

bool dungeon_load_texture(struct dungeon_context *ctx, const char *filename)
{
	// load .dtx
	char *path = gamedir_path(filename);
	size_t dtx_len;
	uint8_t *dtx = file_read(path, &dtx_len);
	if (!dtx) {
		WARNING("Cannot load %s", path);
		free(path);
		return false;
	}
	if (ctx->dtx)
		dtx_free(ctx->dtx);
	ctx->dtx = dtx_parse(dtx, dtx_len);
	free(dtx);
	if (!ctx->dtx) {
		free(path);
		return false;
	}

	// load .tes
	if (ctx->tes)
		tes_free(ctx->tes);
	ctx->tes = NULL;
	size_t path_len = strlen(path);
	if (path_len > 4 && path[path_len - 4] == '.') {
		strcpy(path + path_len - 4, ".tes");  // .dtx -> .tes
		size_t tes_len;
		uint8_t *tes = file_read(path, &tes_len);
		if (tes) {
			ctx->tes = tes_parse(tes, tes_len);
			free(tes);
		}
	}
	free(path);

	if (ctx->renderer)
		dungeon_renderer_free(ctx->renderer);
	ctx->renderer = dungeon_renderer_create(ctx->version, 0, ctx->dtx, NULL, 0, NULL);

	ctx->loaded = ctx->dgn && ctx->dtx;
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
	ctx->camera.angle = -glm_rad(angle);
	ctx->camera.angle_p = glm_rad(angle_p);

	if (ctx->version != DRAW_FIELD) {
		ctx->player_pos[0] = roundf(x / 2.f);
		ctx->player_pos[1] = roundf(y / 2.f);
		ctx->player_pos[2] = roundf(z / 2.f);
	}
}

void dungeon_set_perspective(int surface, int width, int height, float near, float far, float deg)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return;

	float aspect = (float)width / height;
	// XXX: Pastel Chime Continue sets unnecessarily small near (0.1) and large
	//      far (10000), which degrades depth buffer precision.
	near = max(near, 0.4f);
	far = min(far, 500.f);
	glm_perspective(glm_rad(deg), aspect, near, far, ctx->proj_transform);
}

void dungeon_set_player_pos(int surface, int x, int y, int z)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return;
	ctx->player_pos[0] = x;
	ctx->player_pos[1] = y;
	ctx->player_pos[2] = z;
}

static void model_view_matrix(struct dungeon_context *ctx, mat4 mv_out)
{
	struct camera *camera = &ctx->camera;
	float dist = 0.9f;
	vec3 front = {
		dist * sinf(camera->angle) * cosf(camera->angle_p),
		dist * sinf(camera->angle_p),
		dist * -cosf(camera->angle) * cosf(camera->angle_p),
	};
	if (ctx->version != DRAW_FIELD) {
		// The actual camera position is slightly behind camera->pos.
		vec3 eye;
		glm_vec3_sub(camera->pos, front, eye);
		glm_lookat(eye, camera->pos, GLM_YUP, mv_out);
	} else {
		vec3 target;
		glm_vec3_add(camera->pos, front, target);
		glm_lookat(camera->pos, target, GLM_YUP, mv_out);
	}
}

static void dungeon_render(struct sact_sprite *sp)
{
	struct dungeon_context *ctx = (struct dungeon_context *)sp->plugin;
	if (!ctx->loaded || !ctx->draw_enabled)
		return;

	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, &ctx->texture, 0, 0, ctx->texture.w, ctx->texture.h);
	glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, ctx->depth_buffer);
	if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		ERROR("Incomplete framebuffer");

	glClearColor(ctx->dgn->back_color_r / 255.f, ctx->dgn->back_color_g / 255.f, ctx->dgn->back_color_b / 255.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(0.f, 0.f, 0.f, 1.f);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	mat4 view_transform;
	model_view_matrix(ctx, view_transform);

	int dgn_x, dgn_y, dgn_z;
	if (ctx->version == DRAW_FIELD) {
		dgn_x = 0;
		dgn_y = ctx->dgn->size_y - 1;
		dgn_z = 0;
	} else {
		dgn_x = ctx->player_pos[0];
		dgn_y = ctx->player_pos[1];
		dgn_z = ctx->player_pos[2];
	}
	int nr_cells;
	struct dgn_cell **cells = dgn_get_visible_cells(ctx->dgn, dgn_x, dgn_y, dgn_z, &nr_cells);
	dungeon_renderer_render(ctx->renderer, cells, nr_cells, ctx->characters, view_transform, ctx->proj_transform);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);

	dungeon_renderer_run_post_processing(ctx->renderer, &ctx->texture, sprite_get_texture(sp));
	sprite_dirty(sp);
}

static void neighbor_reveal(struct dungeon_context *ctx, int x, int y, int z)
{
	struct dgn_cell *cell = dgn_cell_at(ctx->dgn, x, y, z);
	switch (ctx->version) {
	case DRAW_DUNGEON_1:
		if (cell->floor < 0) {
			cell->walked = 1;
			dungeon_map_reveal(ctx, x, y, z, false);
		}
		break;
	case DRAW_DUNGEON_2:
		if (cell->floor >= 0) {
			cell->walked |= 2;
			dungeon_map_reveal(ctx, x, y, z, false);
		}
		break;
	case DRAW_DUNGEON_14:
	case DRAW_FIELD:
		// do nothing
		break;
	}
}

void dungeon_set_walked(int surface, int x, int y, int z, int flag)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return;
	if (!dgn_is_in_map(ctx->dgn, x, y, z))
		return;  // avoid VM_ERROR in Pastel Chime Continue
	if (flag) {
		dgn_cell_at(ctx->dgn, x, y, z)->walked = ctx->version == DRAW_DUNGEON_2 ? 3 : 1;
		dungeon_map_reveal(ctx, x, y, z, false);
		if (x > 0)
			neighbor_reveal(ctx, x - 1, y, z);
		if (x + 1u < ctx->dgn->size_x)
			neighbor_reveal(ctx, x + 1, y, z);
		if (z > 0)
			neighbor_reveal(ctx, x, y, z - 1);
		if (z + 1u < ctx->dgn->size_z)
			neighbor_reveal(ctx, x, y, z + 1);
	} else {
		dgn_cell_at(ctx->dgn, x, y, z)->walked = 0;
		dungeon_map_hide(ctx, x, y, z);
	}
}

void dungeon_set_walked_all(int surface)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx)
		return;
	int nr_cells = dgn_nr_cells(ctx->dgn);
	for (struct dgn_cell *c = ctx->dgn->cells; c < ctx->dgn->cells + nr_cells; c++) {
		if (ctx->version == DRAW_DUNGEON_2) {
			if (c->floor >= 0) {
				c->walked = 3;
				dungeon_map_reveal(ctx, c->x, c->y, c->z, false);
			}
		} else {
			c->walked = 1;
			dungeon_map_reveal(ctx, c->x, c->y, c->z, false);
		}
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
 * struct DrawDungeon1_WalkData {
 *     int version;  // zero
 *     int nr_maps;
 *     struct {
 *         int map_no;
 *         int size_x, size_y, size_z;
 *         int cells[size_x * size_y * size_z];
 *     } maps[nr_maps];
 * };
 *
 * struct DrawDungeon2_WalkData {
 *     int magic;     // 1977
 *     int reserved;  // zero
 *     int size_x, size_y, size_z;
 *     int cells[size_x * size_y * size_z];
 * };
 */

#define DD2_WALKDATA_MAGIC 1977

enum {
	WALK_DATA_NOT_FOUND = -1,
	WALK_DATA_BROKEN = -2,
};

static int dd1_find_walk_data(struct page *array, int map_no, struct dgn *dgn)
{
	if (!array)
		return WALK_DATA_NOT_FOUND;
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

static bool dd1_load_walk_data(struct dungeon_context *ctx, int map, struct page **page)
{
	struct page *array = *page;

	int offset = dd1_find_walk_data(array, map, ctx->dgn);
	if (offset < 0)
		return offset != WALK_DATA_BROKEN;

	offset += 4;
	int nr_cells = dgn_nr_cells(ctx->dgn);
	for (struct dgn_cell *c = ctx->dgn->cells; c < ctx->dgn->cells + nr_cells; c++) {
		if (array->values[offset].i)
			dungeon_map_reveal(ctx, c->x, c->y, c->z, true);
		c->walked = array->values[offset++].i;
	}

	return true;
}

static bool dd2_load_walk_data(struct dungeon_context *ctx, struct page **page)
{
	int nr_cells = dgn_nr_cells(ctx->dgn);
	struct page *array = *page;
	if (!array || array->nr_vars != 5 + nr_cells) {
		WARNING("DrawDungeon2.LoadWalkData: invalid array size");
		return false;
	}

	int ptr = 0;
	if (array->values[ptr++].i != DD2_WALKDATA_MAGIC)
		return false;
	if (array->values[ptr++].i != 0)
		return false;
	uint32_t size_x = array->values[ptr++].i;
	uint32_t size_y = array->values[ptr++].i;
	uint32_t size_z = array->values[ptr++].i;
	if (size_x != ctx->dgn->size_x || size_y != ctx->dgn->size_y || size_z != ctx->dgn->size_z)
		return false;

	for (struct dgn_cell *c = ctx->dgn->cells; c < ctx->dgn->cells + nr_cells; c++) {
		c->walked = array->values[ptr++].i;
		if (c->walked)
			dungeon_map_reveal(ctx, c->x, c->y, c->z, true);
	}
	return true;
}

bool dungeon_load_walk_data(int surface, int map, struct page **page)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return false;
	switch (ctx->version) {
	case DRAW_DUNGEON_1:
	case DRAW_DUNGEON_14:
		return dd1_load_walk_data(ctx, map, page);
	case DRAW_DUNGEON_2:
		return dd2_load_walk_data(ctx, page);
	case DRAW_FIELD:
		break;  // DrawField does not export LoadWalkData
	}
	return false;
}

static bool dd1_save_walk_data(struct dungeon_context *ctx, int map, struct page **page)
{
	struct page *array = *page;
	int nr_cells = dgn_nr_cells(ctx->dgn);

	int offset = dd1_find_walk_data(array, map, ctx->dgn);
	if (offset == WALK_DATA_NOT_FOUND) {
		offset = array ? array->nr_vars : 0;
		union vm_value dim = { .i = offset + 4 + nr_cells };
		*page = array = realloc_array(array, 1, &dim, AIN_ARRAY_INT, 0, false);
		array->values[1].i += 1;
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

static bool dd2_save_walk_data(struct dungeon_context *ctx, struct page **page)
{
	struct page *array = *page;
	int nr_cells = dgn_nr_cells(ctx->dgn);

	if (!array || array->nr_vars != 5 + nr_cells) {
		WARNING("DrawDungeon2.SaveWalkData: invalid array size");
		return false;
	}

	int ptr = 0;
	array->values[ptr++].i = DD2_WALKDATA_MAGIC;
	array->values[ptr++].i = 0;
	array->values[ptr++].i = ctx->dgn->size_x;
	array->values[ptr++].i = ctx->dgn->size_y;
	array->values[ptr++].i = ctx->dgn->size_z;
	for (int i = 0; i < nr_cells; i++)
		array->values[ptr++].i = ctx->dgn->cells[i].walked;
	return true;
}

bool dungeon_save_walk_data(int surface, int map, struct page **page)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return false;
	switch (ctx->version) {
	case DRAW_DUNGEON_1:
	case DRAW_DUNGEON_14:
		return dd1_save_walk_data(ctx, map, page);
	case DRAW_DUNGEON_2:
		return dd2_save_walk_data(ctx, page);
	case DRAW_FIELD:
		break;  // DrawField does not export SaveWalkData
	}
	return false;
}

void dungeon_paint_step(int surface, int x, int y, int z)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->dgn)
		return;
	dgn_paint_step(ctx->dgn, x, z);
}

void dungeon_set_chara_sprite(int surface, int num, int sprite)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->characters || num < 0 || num >= DRAWFIELD_NR_CHARACTERS)
		return;
	ctx->characters[num].sprite = sprite;
}

void dungeon_set_chara_pos(int surface, int num, float x, float y, float z)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->characters || num < 0 || num >= DRAWFIELD_NR_CHARACTERS)
		return;
	ctx->characters[num].pos[0] = x;
	ctx->characters[num].pos[1] = y;
	ctx->characters[num].pos[2] = -z;
}

void dungeon_set_chara_cg(int surface, int num, int cg)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->characters || num < 0 || num >= DRAWFIELD_NR_CHARACTERS)
		return;
	ctx->characters[num].cg_index = cg;
}

void dungeon_set_chara_cg_info(int surface, int num, int num_chara_x, int num_chara_y)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->characters || num < 0 || num >= DRAWFIELD_NR_CHARACTERS)
		return;
	ctx->characters[num].rows = num_chara_y;
	ctx->characters[num].cols = num_chara_x;
}

void dungeon_set_chara_show(int surface, int num, bool show)
{
	struct dungeon_context *ctx = dungeon_get_context(surface);
	if (!ctx || !ctx->characters || num < 0 || num >= DRAWFIELD_NR_CHARACTERS)
		return;
	ctx->characters[num].show = show;
}

bool dungeon_project_world_to_screen(struct dungeon_context *ctx, vec3 world_pos, Point *screen_pos)
{
	mat4 view_transform;
	model_view_matrix(ctx, view_transform);
	mat4 mvp;
	glm_mat4_mul(ctx->proj_transform, view_transform, mvp);

	vec4 world_pos_h = {
		world_pos[0],
		world_pos[1],
		-world_pos[2],  // Z is negated to match coordinate system
		1.f
	};

	vec4 clip_coords;
	glm_mat4_mulv(mvp, world_pos_h, clip_coords);

	if (clip_coords[3] <= 0.f) {
		return false;  // point is behind the near clipping plane
	}

	vec3 ndc;
	ndc[0] = clip_coords[0] / clip_coords[3];
	ndc[1] = clip_coords[1] / clip_coords[3];
	ndc[2] = clip_coords[2] / clip_coords[3];

	screen_pos->x = roundf(((ndc[0] + 1.f) / 2.f) * ctx->texture.w);
	// Y is inverted from NDC to screen space (NDC is bottom-up, screen is top-down)
	screen_pos->y = roundf(((1.f - ndc[1]) / 2.f) * ctx->texture.h);
	return true;
}

static cJSON *dungeon_to_json(struct sact_sprite *sp, bool verbose)
{
	cJSON *obj, *cam;
	struct dungeon_context *ctx = (struct dungeon_context *)sp->plugin;
	obj = cJSON_CreateObject();
	cJSON_AddStringToObject(obj, "name", ctx->plugin.name);
	cJSON_AddItemToObjectCS(obj, "camera", cam = cJSON_CreateObject());
	cJSON_AddItemToObjectCS(cam, "pos", vec3_point_to_json(ctx->camera.pos, verbose));
	cJSON_AddNumberToObject(cam, "angle", ctx->camera.angle);
	cJSON_AddNumberToObject(cam, "angle_p", ctx->camera.angle_p);

	return obj;
}
