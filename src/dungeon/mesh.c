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

#include <stdlib.h>
#include <cglm/cglm.h>
#include <GL/glew.h>
#include "system4.h"

#include "dungeon/mesh.h"
#include "gfx/gfx.h"

struct mesh_shader {
	struct shader s;
	// Uniform variable locations
	GLint local_transform;
	GLint proj_transform;
	// Attribute variable locations
	GLint vertex_uv;
	GLint instance_transform;
};
static struct mesh_shader mesh_shader;

struct vertex {
	GLfloat x, y, z, u, v;
};

static const struct vertex wall_vertices[] = {
	{-1.0,  1.0, 0.0,  0.0, 0.0},
	{-1.0, -1.0, 0.0,  0.0, 1.0},
	{ 1.0,  1.0, 0.0,  1.0, 0.0},
	{ 1.0, -1.0, 0.0,  1.0, 1.0},
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

struct geometry {
	GLuint attr_buffer;
	int nr_vertices;
};
static struct geometry *geometries[MESH_TYPE_MAX];

static struct geometry *geometry_create(const struct vertex *vertices, size_t size)
{
	struct geometry *g = xcalloc(1, sizeof(struct geometry));
	g->nr_vertices = size / sizeof(struct vertex);
	glGenBuffers(1, &g->attr_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, g->attr_buffer);
	glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	return g;
}

static void geometry_free(struct geometry *g)
{
	glDeleteBuffers(1, &g->attr_buffer);
	free(g);
}

static void geometry_bind(const struct geometry *g)
{
	glBindBuffer(GL_ARRAY_BUFFER, g->attr_buffer);
	glEnableVertexAttribArray(mesh_shader.s.vertex);
	glVertexAttribPointer(mesh_shader.s.vertex, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (const void *)0);
	glEnableVertexAttribArray(mesh_shader.vertex_uv);
	glVertexAttribPointer(mesh_shader.vertex_uv, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (const void *)(3 * sizeof(GLfloat)));
}

void mesh_init(void)
{
	gfx_load_shader(&mesh_shader.s, "shaders/dungeon.v.glsl", "shaders/dungeon.f.glsl");
	mesh_shader.local_transform = glGetUniformLocation(mesh_shader.s.program, "local_transform");
	mesh_shader.proj_transform = glGetUniformLocation(mesh_shader.s.program, "proj_transform");
	mesh_shader.vertex_uv = glGetAttribLocation(mesh_shader.s.program, "vertex_uv");
	mesh_shader.instance_transform = glGetAttribLocation(mesh_shader.s.program, "instance_transform");

	geometries[MESH_WALL] = geometry_create(wall_vertices, sizeof(wall_vertices));
	geometries[MESH_STAIRS] = geometry_create(stairs_vertices, sizeof(stairs_vertices));
}

void mesh_fini(void)
{
	for (int i = 0; i < MESH_TYPE_MAX; i++)
		geometry_free(geometries[i]);
	glDeleteProgram(mesh_shader.s.program);
}

struct mesh {
	const struct geometry *geometry;
	GLuint vao;
	GLuint texture;
	GLuint instance_buffer;
	mat4 *instances;
	int nr_instances;
	int cap_instances;
	uint32_t *cell_ids;
	bool has_alpha;
	bool dirty;
};

struct mesh *mesh_create(enum mesh_type type, GLuint texture, bool has_alpha)
{
	struct mesh *m = xcalloc(1, sizeof(struct mesh));
	m->geometry = geometries[type];
	m->texture = texture;
	m->has_alpha = has_alpha;
	m->nr_instances = 0;
	m->cap_instances = 16;
	m->instances = xcalloc(m->cap_instances, sizeof(mat4));
	m->cell_ids = xcalloc(m->cap_instances, sizeof(uint32_t));
	m->dirty = false;
	glGenVertexArrays(1, &m->vao);
	glBindVertexArray(m->vao);
	geometry_bind(m->geometry);
	glGenBuffers(1, &m->instance_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, m->instance_buffer);
	for (int i = 0; i < 4; i++) {
		GLuint index = mesh_shader.instance_transform + i;
		glEnableVertexAttribArray(index);
		glVertexAttribPointer(index, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(GLfloat), (const void *)(i * 4 * sizeof(GLfloat)));
		glVertexAttribDivisor(index, 1);
	}
	glBindVertexArray(0);
	return m;
}

void mesh_free(struct mesh *m)
{
	if (!m)
		return;
	glDeleteTextures(1, &m->texture);
	glDeleteBuffers(1, &m->instance_buffer);
	glDeleteVertexArrays(1, &m->vao);
	free(m->instances);
	free(m->cell_ids);
	free(m);
}

bool mesh_is_transparent(struct mesh *m)
{
	return m->has_alpha;
}

void mesh_render(struct mesh *m, const mat4 local_transform, const mat4 view_transform, const mat4 proj_transform)
{
	if (!m || !m->nr_instances)
		return;

	if (m->dirty) {
		glBindBuffer(GL_ARRAY_BUFFER, m->instance_buffer);
		glBufferData(GL_ARRAY_BUFFER, m->nr_instances * sizeof(mat4), m->instances, GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		m->dirty = false;
	}

	glUseProgram(mesh_shader.s.program);
	glUniformMatrix4fv(mesh_shader.local_transform, 1, GL_FALSE, local_transform[0]);
	glUniformMatrix4fv(mesh_shader.s.view_transform, 1, GL_FALSE, view_transform[0]);
	glUniformMatrix4fv(mesh_shader.proj_transform, 1, GL_FALSE, proj_transform[0]);

	glBindVertexArray(m->vao);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m->texture);
	glUniform1i(mesh_shader.s.texture, 0);

	glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, m->geometry->nr_vertices, m->nr_instances);

	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

enum wall_face {
	NORTH,
	SOUTH,
	EAST,
	WEST,
	FLOOR,
	CEILING
};

static inline uint32_t cell_id(int x, int y, int z, int face)
{
	return x << 24 | y << 16 | z << 8 | face;
}

static void mesh_add(struct mesh *m, mat4 instance, uint32_t cell_id)
{
	if (!m)
		return;
	if (m->nr_instances == m->cap_instances) {
		m->cap_instances *= 2;
		m->instances = xrealloc_array(m->instances, m->nr_instances, m->cap_instances, sizeof(mat4));
		m->cell_ids = xrealloc_array(m->cell_ids, m->nr_instances, m->cap_instances, sizeof(uint32_t));
	}
	memcpy(m->instances[m->nr_instances], instance, sizeof(mat4));
	m->cell_ids[m->nr_instances] = cell_id;
	m->nr_instances++;
	m->dirty = true;
}

static void mesh_remove(struct mesh *m, uint32_t cell_id)
{
	if (!m)
		return;
	for (int i = 0; i < m->nr_instances; i++) {
		if (m->cell_ids[i] != cell_id)
			continue;
		if (i != m->nr_instances - 1) {
			memcpy(m->instances[i], m->instances[m->nr_instances - 1], sizeof(mat4));
			m->cell_ids[i] = m->cell_ids[m->nr_instances - 1];
		}
		m->nr_instances--;
		m->dirty = true;
		return;
	}
	ERROR("mesh_remove: cannot find cell id %x", cell_id);
}

void mesh_add_floor(struct mesh *m, int x, int y, int z)
{
	mat4 mat = {
		{   1,     0,    0, 0},
		{   0,     0,   -1, 0},
		{   0,     1,    0, 0},
		{ 2*x, 2*y-1, -2*z, 1}
	};
	mesh_add(m, mat, cell_id(x, y, z, FLOOR));
}

void mesh_add_ceiling(struct mesh *m, int x, int y, int z)
{
	mat4 mat = {
		{ -1,     0,    0, 0},
		{  0,     0,   -1, 0},
		{  0,    -1,    0, 0},
		{2*x, 2*y+1, -2*z, 1}
	};
	mesh_add(m, mat, cell_id(x, y, z, CEILING));
}

void mesh_add_north_wall(struct mesh *m, int x, int y, int z)
{
	mat4 mat = {
		{  1,   0,      0, 0},
		{  0,   1,      0, 0},
		{  0,   0,      1, 0},
		{2*x, 2*y, -2*z-1, 1}
	};
	mesh_add(m, mat, cell_id(x, y, z, NORTH));
}

void mesh_add_south_wall(struct mesh *m, int x, int y, int z)
{
	mat4 mat = {
		{ -1,   0,      0, 0},
		{  0,   1,      0, 0},
		{  0,   0,     -1, 0},
		{2*x, 2*y, -2*z+1, 1}
	};
	mesh_add(m, mat, cell_id(x, y, z, SOUTH));
}

void mesh_add_east_wall(struct mesh *m, int x, int y, int z)
{
	mat4 mat = {
		{    0,   0,    1, 0},
		{    0,   1,    0, 0},
		{   -1,   0,    0, 0},
		{2*x+1, 2*y, -2*z, 1}
	};
	mesh_add(m, mat, cell_id(x, y, z, EAST));
}

void mesh_add_west_wall(struct mesh *m, int x, int y, int z)
{
	mat4 mat = {
		{    0,   0,   -1, 0},
		{    0,   1,    0, 0},
		{    1,   0,    0, 0},
		{2*x-1, 2*y, -2*z, 1}
	};
	mesh_add(m, mat, cell_id(x, y, z, WEST));
}

void mesh_add_stairs(struct mesh *m, int x, int y, int z, int orientation)
{
	mat4 matrices[4] = {
		{{ 1, 0,  0, 0}, {0, 1, 0, 0}, { 0, 0,  1, 0}, {2*x, 2*y, -2*z, 1}},
		{{ 0, 0, -1, 0}, {0, 1, 0, 0}, { 1, 0,  0, 0}, {2*x, 2*y, -2*z, 1}},
		{{-1, 0,  0, 0}, {0, 1, 0, 0}, { 0, 0, -1, 0}, {2*x, 2*y, -2*z, 1}},
		{{ 0, 0,  1, 0}, {0, 1, 0, 0}, {-1, 0,  0, 0}, {2*x, 2*y, -2*z, 1}}
	};
	mesh_add(m, matrices[orientation], 0);
}

void mesh_remove_floor(struct mesh *m, int x, int y, int z)
{
	mesh_remove(m, cell_id(x, y, z, FLOOR));
}

void mesh_remove_ceiling(struct mesh *m, int x, int y, int z)
{
	mesh_remove(m, cell_id(x, y, z, CEILING));
}

void mesh_remove_north_wall(struct mesh *m, int x, int y, int z)
{
	mesh_remove(m, cell_id(x, y, z, NORTH));
}

void mesh_remove_south_wall(struct mesh *m, int x, int y, int z)
{
	mesh_remove(m, cell_id(x, y, z, SOUTH));
}

void mesh_remove_east_wall(struct mesh *m, int x, int y, int z)
{
	mesh_remove(m, cell_id(x, y, z, EAST));
}

void mesh_remove_west_wall(struct mesh *m, int x, int y, int z)
{
	mesh_remove(m, cell_id(x, y, z, WEST));
}
