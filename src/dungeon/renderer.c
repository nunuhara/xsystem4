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
#include <stdbool.h>
#include <stdlib.h>
#include <cglm/cglm.h>
#include <GL/glew.h>
#include "system4.h"
#include "system4/cg.h"

#include "dungeon/dungeon.h"
#include "dungeon/dgn.h"
#include "dungeon/dtx.h"
#include "dungeon/renderer.h"
#include "dungeon/skybox.h"
#include "gfx/gfx.h"

struct marker_info {
	int event_type;
	int texture_index;
	int texture_count;
	int floor_marker;
};

enum floor_marker {
	// Rance VI
	R6_MAGIC_CIRCLE = 1,
	R6_RED_OCTAGRAM = 63,
	// GALZOO Island
	GZ_MAGIC_CIRCLE = 0,
};

static const struct marker_info rance6_marker_info[] = {
	{ 11, 41,  3, R6_MAGIC_CIRCLE},  // treasure chest
	{ 12, 51,  7, R6_RED_OCTAGRAM},  // enemy
	{ 14,  8,  5, R6_MAGIC_CIRCLE},  // green star
	{ 15, 21, 10, R6_MAGIC_CIRCLE},  // heart
	{ 16, 45,  5, R6_MAGIC_CIRCLE},  // BP cross
	{ 17, 58,  5, R6_MAGIC_CIRCLE},  // exit
	{ 18, 31, 10, R6_MAGIC_CIRCLE},  // teleporter
	{140, 13,  5, R6_RED_OCTAGRAM},  // red star
	{0},
};

static const struct marker_info galzoo_marker_info[] = {
	{11, 1, 0, GZ_MAGIC_CIRCLE},  // item
	{12, 2, 0, GZ_MAGIC_CIRCLE},  // enemy
	{14, 3, 0, GZ_MAGIC_CIRCLE},  // star
	{15, 4, 0, GZ_MAGIC_CIRCLE},  // heart
	{17, 5, 0, GZ_MAGIC_CIRCLE},  // exit
	{18, 6, 0, GZ_MAGIC_CIRCLE},  // teleporter
	{0},
};

static const struct marker_info *marker_tables[] = {
	[DRAW_DUNGEON_1] = rance6_marker_info,
	[DRAW_DUNGEON_14] = galzoo_marker_info
};

struct geometry;
struct material;

struct dungeon_renderer {
	struct shader shader;
	// Uniform variable locations
	GLint local_transform;
	GLint proj_transform;
	GLuint alpha_mod;
	// Attribute variable locations
	GLint vertex_uv;

	struct geometry *wall_geometry;
	struct geometry *door_left_geometry;
	struct geometry *door_right_geometry;
	struct geometry *roof_geometry;
	struct geometry *stairs_geometry;
	struct geometry *floating_marker_geometry;

	struct material *materials;
	int nr_materials;
	int nr_dtx_columns;

	GLuint *event_textures;
	int nr_event_textures;
	const struct marker_info *marker_table;
	bool draw_event_markers;

	struct skybox *skybox;
};

static const struct marker_info *get_marker_info(struct dungeon_renderer *r, int event_type)
{
	for (const struct marker_info *info = r->marker_table; info->event_type; info++) {
		if (info->event_type == event_type)
			return info;
	}
	return NULL;
}

struct vertex {
	GLfloat x, y, z, u, v;
};

static const struct vertex wall_vertices[] = {
	{-1.0,  1.0, 0.0,  0.0, 0.0},
	{-1.0, -1.0, 0.0,  0.0, 1.0},
	{ 1.0,  1.0, 0.0,  1.0, 0.0},
	{ 1.0, -1.0, 0.0,  1.0, 1.0},
};

static const struct vertex door_left_vertices[] = {
	{ 0.0,  1.0, 0.0,  0.0, 0.0},
	{ 0.0, -1.0, 0.0,  0.0, 1.0},
	{ 1.0,  1.0, 0.0,  0.5, 0.0},
	{ 1.0, -1.0, 0.0,  0.5, 1.0},
};

static const struct vertex door_right_vertices[] = {
	{-1.0,  1.0, 0.0,  0.5, 0.0},
	{-1.0, -1.0, 0.0,  0.5, 1.0},
	{ 0.0,  1.0, 0.0,  1.0, 0.0},
	{ 0.0, -1.0, 0.0,  1.0, 1.0},
};

static const struct vertex roof_vertices[] = {
	{-1.0,  1.0, -1.0,  0.0, 0.0},
	{-1.0, -1.0,  1.0,  0.0, 1.0},
	{ 1.0,  1.0, -1.0,  1.0, 0.0},
	{ 1.0, -1.0,  1.0,  1.0, 1.0},
};

static const struct vertex stairs_vertices[] = {
	{ 1,  3/3.f, -3/3.f, 1,  0/12.f},
	{-1,  3/3.f, -3/3.f, 0,  0/12.f},
	{ 1,  3/3.f, -2/3.f, 1,  1/12.f},
	{-1,  3/3.f, -2/3.f, 0,  1/12.f},
	{ 1,  2/3.f, -2/3.f, 1,  2/12.f},
	{-1,  2/3.f, -2/3.f, 0,  2/12.f},
	{ 1,  2/3.f, -1/3.f, 1,  3/12.f},
	{-1,  2/3.f, -1/3.f, 0,  3/12.f},
	{ 1,  1/3.f, -1/3.f, 1,  4/12.f},
	{-1,  1/3.f, -1/3.f, 0,  4/12.f},
	{ 1,  1/3.f,  0/3.f, 1,  5/12.f},
	{-1,  1/3.f,  0/3.f, 0,  5/12.f},
	{ 1,  0/3.f,  0/3.f, 1,  6/12.f},
	{-1,  0/3.f,  0/3.f, 0,  6/12.f},
	{ 1,  0/3.f,  1/3.f, 1,  7/12.f},
	{-1,  0/3.f,  1/3.f, 0,  7/12.f},
	{ 1, -1/3.f,  1/3.f, 1,  8/12.f},
	{-1, -1/3.f,  1/3.f, 0,  8/12.f},
	{ 1, -1/3.f,  2/3.f, 1,  9/12.f},
	{-1, -1/3.f,  2/3.f, 0,  9/12.f},
	{ 1, -2/3.f,  2/3.f, 1, 10/12.f},
	{-1, -2/3.f,  2/3.f, 0, 10/12.f},
	{ 1, -2/3.f,  3/3.f, 1, 11/12.f},
	{-1, -2/3.f,  3/3.f, 0, 11/12.f},
	{ 1, -3/3.f,  3/3.f, 1, 12/12.f},
	{-1, -3/3.f,  3/3.f, 0, 12/12.f},
};

static const struct vertex floating_marker_vertices[] = {
	{-0.9,  0.9, 0.0,  0.0, 0.0},
	{-0.9, -0.9, 0.0,  0.0, 1.0},
	{ 0.9,  0.9, 0.0,  1.0, 0.0},
	{ 0.9, -0.9, 0.0,  1.0, 1.0},
	{ 0.9, -0.9, 0.0,  1.0, 1.0}, // degenerate triangle
	{ 0.9,  0.9, 0.0,  0.0, 0.0}, // degenerate triangle
	{ 0.9,  0.9, 0.0,  0.0, 0.0},
	{ 0.9, -0.9, 0.0,  0.0, 1.0},
	{-0.9,  0.9, 0.0,  1.0, 0.0},
	{-0.9, -0.9, 0.0,  1.0, 1.0},
};

struct geometry {
	GLuint vao;
	GLuint attr_buffer;
	int nr_vertices;
};

static struct geometry *geometry_create(struct dungeon_renderer *r, const struct vertex *vertices, size_t size)
{
	struct geometry *g = xcalloc(1, sizeof(struct geometry));
	g->nr_vertices = size / sizeof(struct vertex);

	glGenVertexArrays(1, &g->vao);
	glBindVertexArray(g->vao);
	glGenBuffers(1, &g->attr_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, g->attr_buffer);

	glEnableVertexAttribArray(r->shader.vertex);
	glVertexAttribPointer(r->shader.vertex, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (const void *)0);
	glEnableVertexAttribArray(r->vertex_uv);
	glVertexAttribPointer(r->vertex_uv, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (const void *)(3 * sizeof(GLfloat)));
	glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	return g;
}

static void geometry_free(struct geometry *g)
{
	glDeleteVertexArrays(1, &g->vao);
	glDeleteBuffers(1, &g->attr_buffer);
	free(g);
}

struct material {
	GLuint texture;
	bool opaque;
};

static void init_material(struct material *m, struct dtx *dtx, int type, int index)
{
	struct cg *cg = dtx_create_cg(dtx, type, index);
	if (!cg)
		return;
	glGenTextures(1, &m->texture);
	glBindTexture(GL_TEXTURE_2D, m->texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cg->metrics.w, cg->metrics.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, cg->pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);
	m->opaque = !cg->metrics.has_alpha;
	cg_free(cg);
}

static void delete_material(struct material *m)
{
	if (m->texture)
		glDeleteTextures(1, &m->texture);
}

static struct material *get_material(struct dungeon_renderer *r, int type, int index)
{
	struct material *m = &r->materials[type * r->nr_dtx_columns + index];
	if (!m->texture)
		return NULL;
	return m;
}

struct dungeon_renderer *dungeon_renderer_create(enum draw_dungeon_version version, struct dtx *dtx, GLuint *event_textures, int nr_event_textures)
{
	struct dungeon_renderer *r = xcalloc(1, sizeof(struct dungeon_renderer));

	gfx_load_shader(&r->shader, "shaders/dungeon.v.glsl", "shaders/dungeon.f.glsl");
	r->local_transform = glGetUniformLocation(r->shader.program, "local_transform");
	r->proj_transform = glGetUniformLocation(r->shader.program, "proj_transform");
	r->alpha_mod = glGetUniformLocation(r->shader.program, "alpha_mod");
	r->vertex_uv = glGetAttribLocation(r->shader.program, "vertex_uv");

	r->wall_geometry = geometry_create(r, wall_vertices, sizeof(wall_vertices));
	r->door_left_geometry = geometry_create(r, door_left_vertices, sizeof(door_left_vertices));
	r->door_right_geometry = geometry_create(r, door_right_vertices, sizeof(door_right_vertices));
	r->roof_geometry = geometry_create(r, roof_vertices, sizeof(roof_vertices));
	r->stairs_geometry = geometry_create(r, stairs_vertices, sizeof(stairs_vertices));
	r->floating_marker_geometry = geometry_create(r, floating_marker_vertices, sizeof(floating_marker_vertices));

	const int nr_types = DTX_DOOR + 1;
	r->materials = xcalloc(nr_types * dtx->nr_columns, sizeof(struct material));
	for (int type = 0; type < nr_types; type++) {
		for (int i = 0; i < dtx->nr_columns; i++) {
			init_material(&r->materials[type * dtx->nr_columns + i], dtx, type, i);
		}
	}
	r->nr_dtx_columns = dtx->nr_columns;
	r->nr_materials = nr_types * dtx->nr_columns;

	r->event_textures = event_textures;
	r->nr_event_textures = nr_event_textures;
	r->marker_table = marker_tables[version];
	r->draw_event_markers = true;

	r->skybox = skybox_create(dtx);

	return r;
}

void dungeon_renderer_free(struct dungeon_renderer *r)
{
	glDeleteProgram(r->shader.program);

	geometry_free(r->wall_geometry);
	geometry_free(r->door_left_geometry);
	geometry_free(r->door_right_geometry);
	geometry_free(r->roof_geometry);
	geometry_free(r->stairs_geometry);
	geometry_free(r->floating_marker_geometry);

	for (int i = 0; i < r->nr_materials; i++)
		delete_material(&r->materials[i]);
	free(r->materials);

	glDeleteTextures(r->nr_event_textures, r->event_textures);
	free(r->event_textures);

	skybox_free(r->skybox);

	free(r);
}

static void draw(struct dungeon_renderer *r, struct geometry *geometry, GLuint texture, mat4 transform)
{
	glUseProgram(r->shader.program);
	glUniformMatrix4fv(r->local_transform, 1, GL_FALSE, transform[0]);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glUniform1i(r->shader.texture, 0);

	glBindVertexArray(geometry->vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, geometry->nr_vertices);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

static void draw_door(struct dungeon_renderer *r, GLuint texture, float angle, mat4 transform, float dx, float dz)
{
	if (angle == 0.0) {
		draw(r, r->wall_geometry, texture, transform);
		return;
	}
	mat4 m;
	glm_mat4_copy(transform, m);
	m[3][0] += dx;
	m[3][2] += dz;
	glm_rotate_y(m, angle * (M_PI/180), m);
	draw(r, r->door_left_geometry, texture, m);

	glm_mat4_copy(transform, m);
	m[3][0] -= dx;
	m[3][2] -= dz;
	glm_rotate_y(m, angle * -(M_PI/180), m);
	draw(r, r->door_right_geometry, texture, m);
}

static void draw_floor_marker(struct dungeon_renderer *r, const struct marker_info *info, float x, float y, float z, uint32_t t)
{
	float rad = t / 400.0;
	mat4 rot;
	glm_euler((vec3){0, 0, rad}, rot);
	rot[3][3] /= 0.75;  // scale
	mat4 trans = MAT4(
		1,  0,  0,  x,
		0,  0,  1,  y-1,
		0, -1,  0,  z,
		0,  0,  0,  1);
	glm_mul(trans, rot, trans);

	glDepthFunc(GL_LEQUAL);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(-1.0, -2.0);
	draw(r, r->wall_geometry, r->event_textures[info->floor_marker], trans);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glDepthFunc(GL_LESS);
}

static void draw_floating_marker(struct dungeon_renderer *r, const struct marker_info *info, float x, float y, float z, int blend_rate, uint32_t t, mat4 view_mat)
{
	int texture = info->texture_index;
	mat4 m = MAT4(
		1,  0,  0,  x,
		0,  1,  0,  y,
		0,  0,  1,  z,
		0,  0,  0,  1);
	if (info->texture_count) {
		int frame = t / 100;   // 100ms per frame
		texture += frame % info->texture_count;
		// Make a billboard matrix
		mat3 r;
		glm_mat4_pick3t(view_mat, r);
		glm_mat4_ins3(r, m);
	} else {
		float angle = t / -(M_PI * 100.0);
		glm_rotate_y(m, angle, m);
	}

	glUniform1f(r->alpha_mod, blend_rate / 255.0);
	draw(r, r->floating_marker_geometry, r->event_textures[texture], m);
	glUniform1f(r->alpha_mod, 1.0);
}

static void stairs_matrix(mat4 dst, float x, float y, float z, int orientation)
{
	vec3 pos = {x, y, z};
	glm_translate_make(dst, pos);
	const float cos[4] = {1, 0, -1, 0};
	const float sin[4] = {0, 1, 0, -1};
	dst[0][0] =  cos[orientation % 4];
	dst[0][2] = -sin[orientation % 4];
	dst[2][0] =  sin[orientation % 4];
	dst[2][2] =  cos[orientation % 4];
}

static void draw_cell(struct dungeon_renderer *r, struct dgn_cell *cell, bool render_opaque, mat4 view_transform)
{
	float x =  2.0 * cell->x;
	float y =  2.0 * cell->y;
	float z = -2.0 * cell->z;

	if (cell->floor >= 0) {
		struct material *material = get_material(r, DTX_FLOOR, cell->floor);
		if (material && material->opaque == render_opaque) {
			mat4 m = MAT4(
				1,  0,  0,  x,
				0,  0,  1,  y-1,
				0, -1,  0,  z,
				0,  0,  0,  1);
			draw(r, r->wall_geometry, material->texture, m);
		}
	}
	if (cell->ceiling >= 0) {
		struct material *material = get_material(r, DTX_CEILING, cell->ceiling);
		if (material && material->opaque == render_opaque) {
			mat4 m = MAT4(
				-1,  0,  0,  x,
				 0,  0, -1,  y+1,
				 0, -1,  0,  z,
				 0,  0,  0,  1);
			draw(r, r->wall_geometry, material->texture, m);
		}
	}
	if (cell->north_wall >= 0) {
		struct material *material = get_material(r, DTX_WALL, cell->north_wall);
		if (material && material->opaque == render_opaque) {
			mat4 m = MAT4(
				 1,  0,  0,  x,
				 0,  1,  0,  y,
				 0,  0,  1,  z-1,
				 0,  0,  0,  1);
			draw(r, r->wall_geometry, material->texture, m);
		}
	}
	if (cell->south_wall >= 0) {
		struct material *material = get_material(r, DTX_WALL, cell->south_wall);
		if (material && material->opaque == render_opaque) {
			mat4 m = MAT4(
				-1,  0,  0,  x,
				 0,  1,  0,  y,
				 0,  0, -1,  z+1,
				 0,  0,  0,  1);
			draw(r, r->wall_geometry, material->texture, m);
		}
	}
	if (cell->east_wall >= 0) {
		struct material *material = get_material(r, DTX_WALL, cell->east_wall);
		if (material && material->opaque == render_opaque) {
			mat4 m = MAT4(
				 0,  0, -1,  x+1,
				 0,  1,  0,  y,
				 1,  0,  0,  z,
				 0,  0,  0,  1);
			draw(r, r->wall_geometry, material->texture, m);
		}
	}
	if (cell->west_wall >= 0) {
		struct material *material = get_material(r, DTX_WALL, cell->west_wall);
		if (material && material->opaque == render_opaque) {
			mat4 m = MAT4(
				 0,  0,  1,  x-1,
				 0,  1,  0,  y,
				-1,  0,  0,  z,
				 0,  0,  0,  1);
			draw(r, r->wall_geometry, material->texture, m);
		}
	}
	if (cell->north_door >= 0) {
		struct material *material = get_material(r, DTX_DOOR, cell->north_door);
		if (material && material->opaque == render_opaque) {
			mat4 m = MAT4(
				 1,  0,  0,  x,
				 0,  1,  0,  y,
				 0,  0,  1,  z-1,
				 0,  0,  0,  1);
			draw_door(r, material->texture, cell->north_door_angle, m, -1, 0);
		}
	}
	if (cell->south_door >= 0) {
		struct material *material = get_material(r, DTX_DOOR, cell->south_door);
		if (material && material->opaque == render_opaque) {
			mat4 m = MAT4(
				-1,  0,  0,  x,
				 0,  1,  0,  y,
				 0,  0, -1,  z+1,
				 0,  0,  0,  1);
			draw_door(r, material->texture, cell->south_door_angle, m, 1, 0);
		}
	}
	if (cell->east_door >= 0) {
		struct material *material = get_material(r, DTX_DOOR, cell->east_door);
		if (material && material->opaque == render_opaque) {
			mat4 m = MAT4(
				 0,  0, -1,  x+1,
				 0,  1,  0,  y,
				 1,  0,  0,  z,
				 0,  0,  0,  1);
			draw_door(r, material->texture, cell->east_door_angle, m, 0, -1);
		}
	}
	if (cell->west_door >= 0) {
		struct material *material = get_material(r, DTX_DOOR, cell->west_door);
		if (material && material->opaque == render_opaque) {
			mat4 m = MAT4(
				 0,  0,  1,  x-1,
				 0,  1,  0,  y,
				-1,  0,  0,  z,
				 0,  0,  0,  1);
			draw_door(r, material->texture, cell->west_door_angle, m, 0, 1);
		}
	}
	if (cell->stairs_texture >= 0) {
		struct material *material = get_material(r, DTX_STAIRS, cell->stairs_texture);
		if (material && material->opaque == render_opaque) {
			mat4 m;
			stairs_matrix(m, x, y, z, cell->stairs_orientation);
			draw(r, r->stairs_geometry, material->texture, m);
		}
	}
	if (cell->roof_texture >= 0) {
		struct material *material = get_material(r, DTX_STAIRS, cell->roof_texture);
		if (material && material->opaque == render_opaque) {
			mat4 m;
			stairs_matrix(m, x, y, z, cell->roof_orientation);
			draw(r, r->roof_geometry, material->texture, m);
		}
	}
	if (cell->roof_underside_texture >= 0) {
		struct material *material = get_material(r, DTX_STAIRS, cell->roof_underside_texture);
		if (material && material->opaque == render_opaque) {
			mat4 m;
			stairs_matrix(m, x, y, z, cell->roof_orientation);
			glCullFace(GL_FRONT);
			draw(r, r->roof_geometry, material->texture, m);
			glCullFace(GL_BACK);
		}
	}
	if (r->draw_event_markers && cell->floor_event && !render_opaque) {
		const struct marker_info *info = get_marker_info(r, cell->floor_event);
		if (info) {
			uint32_t t = SDL_GetTicks();
			draw_floor_marker(r, info, x, y, z, t);
			draw_floating_marker(r, info, x, y, z, cell->event_blend_rate, t, view_transform);
		}
	}
}

void dungeon_renderer_render(struct dungeon_renderer *r, struct dgn_cell **cells, int nr_cells, mat4 view_transform, mat4 proj_transform)
{
	glUseProgram(r->shader.program);
	glUniformMatrix4fv(r->shader.view_transform, 1, GL_FALSE, view_transform[0]);
	glUniformMatrix4fv(r->proj_transform, 1, GL_FALSE, proj_transform[0]);
	glUniform1f(r->alpha_mod, 1.0);

	// Render opaque objects, from near to far.
	glDisable(GL_BLEND);
	for (int i = 0; i < nr_cells; i++)
		draw_cell(r, cells[i], true, view_transform);

	// Render the skybox.
	skybox_render(r->skybox, view_transform, proj_transform);

	glUseProgram(r->shader.program);
	// Render transparent objects, from far to near.
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	for (int i = nr_cells - 1; i >= 0; i--)
		draw_cell(r, cells[i], false, view_transform);

	glUseProgram(0);
}

void dungeon_renderer_enable_event_markers(struct dungeon_renderer *r, bool enable)
{
	r->draw_event_markers = enable;
}

bool dungeon_renderer_event_markers_enabled(struct dungeon_renderer *r)
{
	return r->draw_event_markers;
}
