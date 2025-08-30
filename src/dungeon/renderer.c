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
#include "gfx/gl.h"
#include "system4.h"
#include "system4/buffer.h"
#include "system4/cg.h"

#include "dungeon/dungeon.h"
#include "dungeon/dgn.h"
#include "dungeon/dtx.h"
#include "dungeon/polyobj.h"
#include "dungeon/renderer.h"
#include "dungeon/skybox.h"
#include "gfx/gfx.h"
#include "sact.h"

enum {
	COLOR_TEXTURE_UNIT,
	LIGHT_TEXTURE_UNIT,
};

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
	// Dungeons & Dolls
	DD_MAGIC_CIRCLE = 40,
	DD_RED_OCTAGRAM = 41,
	DD_WHITE_OCTAGRAM = 42,
	// GALZOO Island
	GZ_MAGIC_CIRCLE = 0,
};

static const struct marker_info empty_marker_info[] = {
	{0},
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

static const struct marker_info dolls_marker_info[] = {
	{11, 43,  3, DD_WHITE_OCTAGRAM}, // treasure chest
	{17,  0, 20, DD_MAGIC_CIRCLE},   // goal
	{0},
};

static const struct marker_info dolls_boss_marker_info[] = {
	{11, 43,  3, DD_WHITE_OCTAGRAM}, // treasure chest
	{17, 20, 20, DD_RED_OCTAGRAM},   // boss
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
	[DRAW_DUNGEON_2] = dolls_marker_info,
	[DRAW_DUNGEON_14] = galzoo_marker_info,
	[DRAW_FIELD] = empty_marker_info
};

struct geometry;
struct material;
struct object3d;

struct raster_shader {
	struct shader s;
	GLint amp;
	GLint t;
};

struct dungeon_renderer {
	enum draw_dungeon_version version;
	struct shader shader;
	// Uniform variable locations
	GLint local_transform;
	GLint proj_transform;
	GLint alpha_mod;
	GLint use_lightmap;
	GLint light_texture;
	GLint use_fog;
	GLint uv_offset;
	GLint uv_scale;

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

	int nr_polyobjs;
	struct object3d *polyobjs;
	int nr_polyobj_materials;
	struct material *polyobj_materials;

	struct skybox *skybox;

	struct raster_shader raster_shader;
	bool raster_scroll;
	float raster_amp;

	bool enable_lightmap;
	uint32_t draw_obj_flags;
};

enum draw_obj_flag_index {
	DRAW_POLYOBJ = 0,
	DRAW_FLOOR   = 1,
	DRAW_WALL    = 2,
	DRAW_DOOR    = 3,
	DRAW_CEILING = 4,
	DRAW_STAIRS  = 5,
	NR_DRAW_OBJ_FLAGS
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
	GLenum mode;
};

static struct geometry *geometry_create(struct dungeon_renderer *r, const struct vertex *vertices, size_t size, GLenum mode)
{
	struct geometry *g = xcalloc(1, sizeof(struct geometry));
	g->nr_vertices = size / sizeof(struct vertex);
	g->mode = mode;

	glGenVertexArrays(1, &g->vao);
	glBindVertexArray(g->vao);
	glGenBuffers(1, &g->attr_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, g->attr_buffer);

	glEnableVertexAttribArray(r->shader.vertex_pos);
	glVertexAttribPointer(r->shader.vertex_pos, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (const void *)0);
	glEnableVertexAttribArray(r->shader.vertex_uv);
	glVertexAttribPointer(r->shader.vertex_uv, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (const void *)(3 * sizeof(GLfloat)));
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

static void init_material(struct material *m, struct cg *cg)
{
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

struct mesh {
	struct geometry *geometry;
	int material;
};

struct object3d {
	int nr_meshes;
	struct mesh *meshes;
};

static void init_object3d(struct object3d *o3d, struct dungeon_renderer *r, const struct polyobj_object *obj)
{
	o3d->nr_meshes = obj->nr_materials;
	o3d->meshes = xcalloc(obj->nr_materials, sizeof(struct mesh));
	for (int i = 0; i < obj->nr_materials; i++)
		o3d->meshes[i].material = obj->materials[i];

	struct buffer *bufs = xcalloc(obj->nr_materials, sizeof(struct buffer));
	for (int i = 0; i < obj->nr_parts; i++) {
		const struct polyobj_part *part = &obj->parts[i];
		for (int j = 0; j < part->nr_triangles; j++) {
			const struct polyobj_triangle *triangle = &part->triangles[j];
			for (int k = 0; k < 3; k++) {
				const int ltor[] = {0, 2, 1};  // left->right handed system
				const vec3 *pos = &part->vertices[triangle->index[ltor[k]]];
				const vec2 *uv = &triangle->uv[ltor[k]];
				buffer_write_float(&bufs[triangle->material], (*pos)[0]);  // x
				buffer_write_float(&bufs[triangle->material], (*pos)[1]);  // y
				buffer_write_float(&bufs[triangle->material], -(*pos)[2]); // z
				buffer_write_float(&bufs[triangle->material], (*uv)[0]);   // u
				buffer_write_float(&bufs[triangle->material], (*uv)[1]);   // v
			}
		}
	}

	for (int i = 0; i < obj->nr_materials; i++) {
		o3d->meshes[i].geometry = geometry_create(r, (const struct vertex *)bufs[i].buf, bufs[i].index, GL_TRIANGLES);
		free(bufs[i].buf);
	}
	free(bufs);
}

static void delete_object3d(struct object3d *o3d)
{
	for (int i = 0; i < o3d->nr_meshes; i++)
		geometry_free(o3d->meshes[i].geometry);
	free(o3d->meshes);
}

static void init_polyobjs(struct dungeon_renderer *r, struct polyobj *po)
{
	r->nr_polyobj_materials = po->nr_textures;
	r->polyobj_materials = xcalloc(po->nr_textures, sizeof(struct material));
	for (int i = 0; i < po->nr_textures; i++) {
		struct cg *cg = polyobj_get_texture(po, i);
		init_material(&r->polyobj_materials[i], cg);
		cg_free(cg);
	}

	r->nr_polyobjs = po->nr_objects;
	r->polyobjs = xcalloc(po->nr_objects, sizeof(struct object3d));
	for (int i = 0; i < po->nr_objects; i++) {
		struct polyobj_object *obj = polyobj_get_object(po, i);
		if (!obj)
			continue;
		init_object3d(&r->polyobjs[i], r, obj);
		polyobj_object_free(obj);
	}
}

static bool is_dolls_boss_floor(int num)
{
	switch (num) {
	case 20:
	case 40:
	case 60:
	case 80:
	case 100:
	case 120:
	case 140:
	case 150:
	case 160:
	case 180:
	case 200:
	case 300:
	case 500:
		return true;
	default:
		return false;
	}
}

struct dungeon_renderer *dungeon_renderer_create(enum draw_dungeon_version version, int num, struct dtx *dtx, GLuint *event_textures, int nr_event_textures, struct polyobj *po)
{
	struct dungeon_renderer *r = xcalloc(1, sizeof(struct dungeon_renderer));
	r->version = version;
	gfx_load_shader(&r->shader, "shaders/dungeon.v.glsl", "shaders/dungeon.f.glsl");
	r->local_transform = glGetUniformLocation(r->shader.program, "local_transform");
	r->proj_transform = glGetUniformLocation(r->shader.program, "proj_transform");
	r->alpha_mod = glGetUniformLocation(r->shader.program, "alpha_mod");
	r->use_lightmap = glGetUniformLocation(r->shader.program, "use_lightmap");
	r->light_texture = glGetUniformLocation(r->shader.program, "light_texture");
	r->use_fog = glGetUniformLocation(r->shader.program, "use_fog");
	r->uv_offset = glGetUniformLocation(r->shader.program, "uv_offset");
	r->uv_scale = glGetUniformLocation(r->shader.program, "uv_scale");
	glUseProgram(r->shader.program);
	glUniform1i(r->use_fog, version != DRAW_FIELD);
	glUniform2f(r->uv_offset, 0.0f, 0.0f);
	glUniform2f(r->uv_scale, 1.0f, 1.0f);
	glUseProgram(0);

	r->wall_geometry = geometry_create(r, wall_vertices, sizeof(wall_vertices), GL_TRIANGLE_STRIP);
	r->door_left_geometry = geometry_create(r, door_left_vertices, sizeof(door_left_vertices), GL_TRIANGLE_STRIP);
	r->door_right_geometry = geometry_create(r, door_right_vertices, sizeof(door_right_vertices), GL_TRIANGLE_STRIP);
	r->roof_geometry = geometry_create(r, roof_vertices, sizeof(roof_vertices), GL_TRIANGLE_STRIP);
	r->stairs_geometry = geometry_create(r, stairs_vertices, sizeof(stairs_vertices), GL_TRIANGLE_STRIP);
	r->floating_marker_geometry = geometry_create(r, floating_marker_vertices, sizeof(floating_marker_vertices), GL_TRIANGLE_STRIP);

	r->materials = xcalloc(DTX_NR_CELL_TEXTURE_TYPES * dtx->nr_columns, sizeof(struct material));
	for (int type = 0; type < DTX_NR_CELL_TEXTURE_TYPES; type++) {
		for (int i = 0; i < dtx->nr_columns; i++) {
			struct cg *cg = dtx_create_cg(dtx, type, i);
			if (!cg)
				continue;
			init_material(&r->materials[type * dtx->nr_columns + i], cg);
			cg_free(cg);
		}
	}
	r->nr_dtx_columns = dtx->nr_columns;
	r->nr_materials = DTX_NR_CELL_TEXTURE_TYPES * dtx->nr_columns;

	r->event_textures = event_textures;
	r->nr_event_textures = nr_event_textures;
	r->draw_event_markers = true;

	if (version == DRAW_DUNGEON_2 && is_dolls_boss_floor(num)) {
		// Use special marker table for Dungeons & Dolls boss floor
		r->marker_table = dolls_boss_marker_info;
	} else {
		r->marker_table = marker_tables[version];
	}

	if (po)
		init_polyobjs(r, po);

	r->skybox = skybox_create(version, dtx);

	r->enable_lightmap = true;
	r->draw_obj_flags = (1 << NR_DRAW_OBJ_FLAGS) - 1;
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

	for (int i = 0; i < r->nr_polyobjs; i++)
		delete_object3d(&r->polyobjs[i]);
	free(r->polyobjs);
	for (int i = 0; i < r->nr_polyobj_materials; i++)
		delete_material(&r->polyobj_materials[i]);
	free(r->polyobj_materials);

	skybox_free(r->skybox);
	if (r->raster_shader.s.program)
		glDeleteProgram(r->raster_shader.s.program);

	free(r);
}

static void set_lightmap_texture(struct dungeon_renderer *r, int texture_index)
{
	struct material *material = (r->enable_lightmap && texture_index >= 0)
		? get_material(r, DTX_LIGHTMAP, texture_index) : NULL;
	glUniform1i(r->use_lightmap, !!material);
	if (material) {
		glActiveTexture(GL_TEXTURE0 + LIGHT_TEXTURE_UNIT);
		glBindTexture(GL_TEXTURE_2D, material->texture);
		glUniform1i(r->light_texture, LIGHT_TEXTURE_UNIT);
	}
}

static void draw(struct dungeon_renderer *r, struct geometry *geometry, GLuint texture, mat4 transform)
{
	glUseProgram(r->shader.program);
	glUniformMatrix4fv(r->local_transform, 1, GL_FALSE, transform[0]);

	glActiveTexture(GL_TEXTURE0 + COLOR_TEXTURE_UNIT);
	glBindTexture(GL_TEXTURE_2D, texture);
	glUniform1i(r->shader.texture, COLOR_TEXTURE_UNIT);

	glBindVertexArray(geometry->vao);
	glDrawArrays(geometry->mode, 0, geometry->nr_vertices);
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
	glm_rotate_y(m, glm_rad(angle), m);
	draw(r, r->door_left_geometry, texture, m);

	glm_mat4_copy(transform, m);
	m[3][0] -= dx;
	m[3][2] -= dz;
	glm_rotate_y(m, -glm_rad(angle), m);
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
		float angle = t / -(GLM_PIf * 100.f);
		glm_rotate_y(m, angle, m);
	}

	glUniform1f(r->alpha_mod, blend_rate / 255.0);
	draw(r, r->floating_marker_geometry, r->event_textures[texture], m);
	glUniform1f(r->alpha_mod, 1.0);
}

static void draw_object3d(struct dungeon_renderer *r, struct object3d *o3d, bool render_opaque, vec3 pos, float scale, float rotation_y)
{
	mat4 m;
	glm_translate_make(m, pos);
	glm_rotate_y(m, glm_rad(rotation_y), m);
	glm_scale_uni(m, scale);

	for (int i = 0; i < o3d->nr_meshes; i++) {
		struct mesh *mesh = &o3d->meshes[i];
		struct material *material = &r->polyobj_materials[mesh->material];
		if (material->opaque != render_opaque)
			continue;
		draw(r, mesh->geometry, material->texture, m);
	}
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

	if (cell->floor >= 0 && r->draw_obj_flags & (1 << DRAW_FLOOR)) {
		struct material *material = get_material(r, DTX_FLOOR, cell->floor);
		if (material && material->opaque == render_opaque) {
			mat4 m = MAT4(
				1,  0,  0,  x,
				0,  0,  1,  y-1,
				0, -1,  0,  z,
				0,  0,  0,  1);
			set_lightmap_texture(r, cell->lightmap_floor);
			draw(r, r->wall_geometry, material->texture, m);
			set_lightmap_texture(r, -1);
		}
	}
	if (cell->ceiling >= 0 && r->draw_obj_flags & (1 << DRAW_CEILING)) {
		struct material *material = get_material(r, DTX_CEILING, cell->ceiling);
		if (material && material->opaque == render_opaque) {
			if (r->version == DRAW_FIELD) {
				// Draw as a billboard.
				mat4 m;
				vec3 pos = {x, y, z};
				glm_translate_make(m, pos);
				mat3 r_bill;
				glm_mat4_pick3t(view_transform, r_bill);
				glm_mat4_ins3(r_bill, m);
				glm_scale_uni(m, 1.7f);
				draw(r, r->wall_geometry, material->texture, m);
			} else {
				mat4 m = MAT4(
					-1,  0,  0,  x,
					 0,  0, -1,  y+1,
					 0, -1,  0,  z,
					 0,  0,  0,  1);
				set_lightmap_texture(r, cell->lightmap_ceiling);
				draw(r, r->wall_geometry, material->texture, m);
				set_lightmap_texture(r, -1);
			}
		}
	}
	if (cell->north_wall >= 0 && r->draw_obj_flags & (1 << DRAW_WALL)) {
		struct material *material = get_material(r, DTX_WALL, cell->north_wall);
		if (material && material->opaque == render_opaque) {
			mat4 m = MAT4(
				 1,  0,  0,  x,
				 0,  1,  0,  y,
				 0,  0,  1,  z-1,
				 0,  0,  0,  1);
			set_lightmap_texture(r, cell->lightmap_north);
			draw(r, r->wall_geometry, material->texture, m);
			set_lightmap_texture(r, -1);
		}
	}
	if (cell->south_wall >= 0 && r->draw_obj_flags & (1 << DRAW_WALL)) {
		struct material *material = get_material(r, DTX_WALL, cell->south_wall);
		if (material && material->opaque == render_opaque) {
			mat4 m = MAT4(
				-1,  0,  0,  x,
				 0,  1,  0,  y,
				 0,  0, -1,  z+1,
				 0,  0,  0,  1);
			set_lightmap_texture(r, cell->lightmap_south);
			draw(r, r->wall_geometry, material->texture, m);
			set_lightmap_texture(r, -1);
		}
	}
	if (cell->east_wall >= 0 && r->draw_obj_flags & (1 << DRAW_WALL)) {
		struct material *material = get_material(r, DTX_WALL, cell->east_wall);
		if (material && material->opaque == render_opaque) {
			mat4 m = MAT4(
				 0,  0, -1,  x+1,
				 0,  1,  0,  y,
				 1,  0,  0,  z,
				 0,  0,  0,  1);
			set_lightmap_texture(r, cell->lightmap_east);
			draw(r, r->wall_geometry, material->texture, m);
			set_lightmap_texture(r, -1);
		}
	}
	if (cell->west_wall >= 0 && r->draw_obj_flags & (1 << DRAW_WALL)) {
		struct material *material = get_material(r, DTX_WALL, cell->west_wall);
		if (material && material->opaque == render_opaque) {
			mat4 m = MAT4(
				 0,  0,  1,  x-1,
				 0,  1,  0,  y,
				-1,  0,  0,  z,
				 0,  0,  0,  1);
			set_lightmap_texture(r, cell->lightmap_west);
			draw(r, r->wall_geometry, material->texture, m);
			set_lightmap_texture(r, -1);
		}
	}
	if (cell->north_door >= 0 && r->draw_obj_flags & (1 << DRAW_DOOR)) {
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
	if (cell->south_door >= 0 && r->draw_obj_flags & (1 << DRAW_DOOR)) {
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
	if (cell->east_door >= 0 && r->draw_obj_flags & (1 << DRAW_DOOR)) {
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
	if (cell->west_door >= 0 && r->draw_obj_flags & (1 << DRAW_DOOR)) {
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
	if (cell->stairs_texture >= 0 && r->draw_obj_flags & (1 << DRAW_STAIRS)) {
		struct material *material = get_material(r, DTX_STAIRS, cell->stairs_texture);
		if (material && material->opaque == render_opaque) {
			mat4 m;
			stairs_matrix(m, x, y, z, cell->stairs_orientation);
			glDisable(GL_CULL_FACE);
			draw(r, r->stairs_geometry, material->texture, m);
			glEnable(GL_CULL_FACE);
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
	if (cell->polyobj_index >= 0 && r->draw_obj_flags & (1 << DRAW_POLYOBJ)) {
		vec3 pos = {
			x + cell->polyobj_position_x,
			y + cell->polyobj_position_y - 1,
			z - cell->polyobj_position_z
		};
		draw_object3d(r, &r->polyobjs[cell->polyobj_index], render_opaque, pos, cell->polyobj_scale, cell->polyobj_rotation_y);
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

static void draw_character(struct dungeon_renderer *r, struct drawfield_character *chara, mat4 view_mat)
{
	if (!chara->show || chara->sprite <= 0)
		return;
	struct sact_sprite *sp = sact_try_get_sprite(chara->sprite);
	if (!sp || !sp->texture.handle)
		return;

	if (chara->cols == 0) chara->cols = 1;
	if (chara->rows == 0) chara->rows = 1;
	glUseProgram(r->shader.program);
	glUniform2f(r->uv_offset, (float)(chara->cg_index % chara->cols) / chara->cols, (float)(chara->cg_index / chara->cols) / chara->rows);
	glUniform2f(r->uv_scale, 1.0f / chara->cols, 1.0f / chara->rows);

	mat4 m;
	glm_translate_make(m, chara->pos);

	// Make it a billboard of 2.0 x 2.0
	mat3 r_bill;
	glm_mat4_pick3t(view_mat, r_bill);
	glm_mat4_ins3(r_bill, m);

	draw(r, r->wall_geometry, sp->texture.handle, m);
}

struct character_z {
	struct drawfield_character *chara;
	float z;
};

static int compare_character_z(const void *a, const void *b)
{
	const struct character_z *cha = a;
	const struct character_z *chb = b;
	if (cha->z < chb->z)
		return -1;
	if (cha->z > chb->z)
		return 1;
	return 0;
}

static void draw_characters(struct dungeon_renderer *r, struct drawfield_character *characters, mat4 view_transform)
{
	if (!characters)
		return;

	struct character_z sorted_characters[DRAWFIELD_NR_CHARACTERS];
	int n = 0;
	for (int i = 0; i < DRAWFIELD_NR_CHARACTERS; i++) {
		struct drawfield_character *chara = characters + i;
		if (!chara->show || chara->sprite <= 0)
			continue;
		sorted_characters[n].chara = chara;
		vec4 pos;
		glm_mat4_mulv(view_transform, (vec4){chara->pos[0], chara->pos[1], chara->pos[2], 1}, pos);
		sorted_characters[n].z = pos[2];
		n++;
	}

	qsort(sorted_characters, n, sizeof(struct character_z), compare_character_z);

	for (int i = 0; i < n; i++)
		draw_character(r, sorted_characters[i].chara, view_transform);

	glUniform2f(r->uv_offset, 0.0f, 0.0f);
	glUniform2f(r->uv_scale, 1.0f, 1.0f);
}

void dungeon_renderer_render(struct dungeon_renderer *r, struct dgn_cell **cells, int nr_cells, struct drawfield_character *characters, mat4 view_transform, mat4 proj_transform)
{
	glUseProgram(r->shader.program);
	glUniformMatrix4fv(r->shader.view_transform, 1, GL_FALSE, view_transform[0]);
	glUniformMatrix4fv(r->proj_transform, 1, GL_FALSE, proj_transform[0]);
	glUniform1f(r->alpha_mod, 1.0);
	glUniform1i(r->use_lightmap, 0);

	// Render opaque objects, from near to far.
	glDisable(GL_BLEND);
	for (int i = 0; i < nr_cells; i++)
		draw_cell(r, cells[i], true, view_transform);

	// Render the skybox.
	skybox_render(r->skybox, view_transform, proj_transform);
	glUseProgram(r->shader.program);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// DrawField: draw characters.
	draw_characters(r, characters, view_transform);

	// Render transparent objects, from far to near.
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

bool dungeon_renderer_is_floor_opaque(struct dungeon_renderer *r, struct dgn_cell *cell)
{
	if (cell->floor < 0)
		return false;
	struct material *material = get_material(r, DTX_FLOOR, cell->floor);
	return material && material->opaque;
}

static void prepare_raster_shader(struct gfx_render_job *job, void *data)
{
	struct dungeon_renderer *r = data;
	float width = job->world_transform[0];
	glUniform1f(r->raster_shader.amp, r->raster_amp / width);
	glUniform1f(r->raster_shader.t, SDL_GetTicks() / 1000.f);
}

static void load_raster_shader(struct raster_shader *s)
{
	gfx_load_shader(&s->s, "shaders/render.v.glsl", "shaders/dungeon_raster.f.glsl");
	s->amp = glGetUniformLocation(s->s.program, "amp");
	s->t = glGetUniformLocation(s->s.program, "t");
	s->s.prepare = prepare_raster_shader;
}

void dungeon_renderer_run_post_processing(struct dungeon_renderer *r, struct texture *src, struct texture *dst)
{
	// This function flips the image upside down, because the image in `src`
	// (rendered by dungeon_renderer_render) is bottom-up (the first pixel is at
	// the bottom-left), but sprite textures are top-down (the first pixel is at
	// the top-left).

	if (r->raster_scroll) {
		mat4 mw_transform = MAT4(
			src->w, 0,       0, 0,
			0,      -src->h, 0, src->h,  // flip vertically
			0,      0,       1, 0,
			0,      0,       0, 1);
		mat4 wv_transform = WV_TRANSFORM(src->w, src->h);

		struct gfx_render_job job = {
			.shader = &r->raster_shader.s,
			.shape = GFX_RECTANGLE,
			.texture = src->handle,
			.world_transform = mw_transform[0],
			.view_transform = wv_transform[0],
			.data = r
		};
		GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, dst, 0, 0, dst->w, dst->h);
		gfx_render(&job);
		gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	} else {
		gfx_copy_reverse_UD(dst, 0, 0, src, 0, 0, src->w, src->h);
	}
}

void dungeon_renderer_set_raster_scroll(struct dungeon_renderer *r, int type)
{
	r->raster_scroll = !!type;
	if (r->raster_scroll && !r->raster_shader.s.program) {
		load_raster_shader(&r->raster_shader);
	}
}

void dungeon_renderer_set_raster_amp(struct dungeon_renderer *r, float amp)
{
	r->raster_amp = amp;
}

void dungeon_renderer_enable_lightmap(struct dungeon_renderer *r, bool enable)
{
	r->enable_lightmap = enable;
}

bool dungeon_renderer_get_draw_obj_flag(struct dungeon_renderer *r, int type)
{
	if (type < 0 || type >= NR_DRAW_OBJ_FLAGS)
		return false;
	return r->draw_obj_flags & (1 << type);
}

void dungeon_renderer_set_draw_obj_flag(struct dungeon_renderer *r, int type, bool flag)
{
	if (type < 0 || type >= NR_DRAW_OBJ_FLAGS)
		return;
	if (flag)
		r->draw_obj_flags |= (1 << type);
	else
		r->draw_obj_flags &= ~(1 << type);
}
