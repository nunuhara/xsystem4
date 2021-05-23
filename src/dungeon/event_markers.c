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
#include <stdlib.h>
#include <cglm/cglm.h>
#include <GL/glew.h>
#include "system4.h"
#include "system4/alk.h"
#include "system4/cg.h"

#include "dungeon/event_markers.h"
#include "dungeon/mesh.h"
#include "gfx/gfx.h"
#include "xsystem4.h"

enum floor_marker_type {
	MAGIC_CIRCLE,
	RED_OCTAGRAM,
	NR_FLOOR_MARKER_TYPE
};

struct marker_info {
	int event_type;
	int texture_index;
	int texture_count;
	int floor_marker_type;
};

static const struct marker_info rance6_marker_info[] = {
	{ 11, 41,  3, MAGIC_CIRCLE},  // treasure chest
	{ 12, 51,  7, RED_OCTAGRAM},  // enemy
	{ 14,  8,  5, MAGIC_CIRCLE},  // green star
	{ 15, 21, 10, MAGIC_CIRCLE},  // heart
	{ 16, 45,  5, MAGIC_CIRCLE},  // BP cross
	{ 17, 58,  5, MAGIC_CIRCLE},  // exit
	{ 18, 31, 10, MAGIC_CIRCLE},  // teleporter
	{140, 13,  5, RED_OCTAGRAM},  // red star
	{0},
};

const struct marker_info *get_marker_info(int event_type)
{
	for (const struct marker_info *info = rance6_marker_info; info->event_type; info++) {
		if (info->event_type == event_type)
			return info;
	}
	return NULL;
}

static inline uint32_t cell_id(int x, int y, int z) {
	return x << 16 | y << 8 | z;
}

struct event_sprite {
	uint32_t cell_id;
	int event_type;
	GLfloat pos[3];
	int alpha_mod;  // 0-255
};

struct event_markers {
	GLuint *event_textures;
	int nr_event_textures;
	struct mesh *floor_markers[NR_FLOOR_MARKER_TYPE];

	// For rendering sprites
	struct shader sprite_shader;
	GLint proj_transform;
	GLuint alpha_mod;
	struct event_sprite *sprites;
	int nr_sprites;
	int cap_sprites;
	GLuint vao;
	GLuint pos_buffer;
};

static int find_event_sprite(struct event_markers *em, int x, int y, int z)
{
	uint32_t id = cell_id(x, y, z);
	for (int i = 0; i < em->nr_sprites; i++) {
		if (em->sprites[i].cell_id == id)
			return i;
	}
	return -1;
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

struct event_markers *event_markers_create(void)
{
	struct event_markers *em = xcalloc(1, sizeof(struct event_markers));
	gfx_load_shader(&em->sprite_shader, "shaders/dungeon_sprite.v.glsl", "shaders/dungeon_sprite.f.glsl");
	em->proj_transform = glGetUniformLocation(em->sprite_shader.program, "proj_transform");
	em->alpha_mod = glGetUniformLocation(em->sprite_shader.program, "alpha_mod");

	em->event_textures = load_event_textures(&em->nr_event_textures);
	if (!em->event_textures)
		return em;

	em->floor_markers[MAGIC_CIRCLE] = mesh_create(MESH_WALL, em->event_textures[1], true);
	em->floor_markers[RED_OCTAGRAM] = mesh_create(MESH_WALL, em->event_textures[63], true);

	em->nr_sprites = 0;
	em->cap_sprites = 16;
	em->sprites = xcalloc(em->cap_sprites, sizeof(struct event_sprite));
	glGenVertexArrays(1, &em->vao);
	glBindVertexArray(em->vao);
	glGenBuffers(1, &em->pos_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, em->pos_buffer);
	glEnableVertexAttribArray(em->sprite_shader.vertex);
	glVertexAttribPointer(em->sprite_shader.vertex, 3, GL_FLOAT, GL_FALSE, 0, 0);
	glBindVertexArray(0);
	return em;
}

void event_markers_free(struct event_markers *em)
{
	for (int i = 0; i < NR_FLOOR_MARKER_TYPE; i++)
		mesh_free(em->floor_markers[i]);
	glDeleteVertexArrays(1, &em->vao);
	glDeleteBuffers(1, &em->pos_buffer);
	glDeleteProgram(em->sprite_shader.program);
	glDeleteTextures(em->nr_event_textures, em->event_textures);
	free(em->event_textures);
	free(em->sprites);
	free(em);
}


void event_markers_render(struct event_markers *em, const mat4 view_transform, const mat4 proj_transform)
{
	uint32_t t = SDL_GetTicks();

	// Draw floor markers
	mat4 local_transform;
	glm_mat4_identity(local_transform);
	float rad = t / 400.0;
	local_transform[0][0] = cos(rad);
	local_transform[0][1] = sin(rad);
	local_transform[1][0] = -sin(rad);
	local_transform[1][1] = cos(rad);
	local_transform[3][3] /= 0.75;

	glDepthFunc(GL_LEQUAL);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(-1.0, -2.0);
	for (int i = 0; i < NR_FLOOR_MARKER_TYPE; i++)
		mesh_render(em->floor_markers[i], local_transform, view_transform, proj_transform);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glDepthFunc(GL_LESS);

	// Draw sprites
	glUseProgram(em->sprite_shader.program);
	glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
	glEnable(GL_POINT_SPRITE);
	glUniformMatrix4fv(em->sprite_shader.view_transform, 1, GL_FALSE, view_transform[0]);
	glUniformMatrix4fv(em->proj_transform, 1, GL_FALSE, proj_transform[0]);

	glBindVertexArray(em->vao);
	glActiveTexture(GL_TEXTURE0);
	glUniform1i(em->sprite_shader.texture, 0);
	glBindBuffer(GL_ARRAY_BUFFER, em->pos_buffer);

	int frame = t / 100;   // 100ms per frame
	struct event_sprite *sp = em->sprites;
	for (int i = 0; i < em->nr_sprites; i++, sp++) {
		const struct marker_info *info = get_marker_info(sp->event_type);
		int texture = info->texture_index + frame % info->texture_count;
		glUniform1f(em->alpha_mod, sp->alpha_mod / 255.0);
		glBindTexture(GL_TEXTURE_2D, em->event_textures[texture]);
		glBufferData(GL_ARRAY_BUFFER, 3 * sizeof(GLfloat), sp->pos, GL_STREAM_DRAW);
		glDrawArrays(GL_POINTS, 0, 1);
	}

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_VERTEX_PROGRAM_POINT_SIZE);
	glDisable(GL_POINT_SPRITE);
}

void event_markers_set(struct event_markers *em, int x, int y, int z, int event_type)
{
	int i = find_event_sprite(em, x, y, z);
	if (i >= 0) {
		if (event_type == em->sprites[i].event_type)
			return;
		// Remove old marker
		const struct marker_info *old_info = get_marker_info(em->sprites[i].event_type);
		mesh_remove_floor(em->floor_markers[old_info->floor_marker_type], x, y, z);
		if (i != em->nr_sprites - 1)
			em->sprites[i] = em->sprites[em->nr_sprites - 1];
		em->nr_sprites--;
	}

	const struct marker_info *info = get_marker_info(event_type);
	if (info) {
		// Add a new marker
		if (em->nr_sprites == em->cap_sprites) {
			em->cap_sprites *= 2;
			em->sprites = xrealloc_array(em->sprites, em->nr_sprites, em->cap_sprites, sizeof(struct event_sprite));
		}
		struct event_sprite *sp = &em->sprites[em->nr_sprites++];
		sp->cell_id = cell_id(x, y, z);
		sp->event_type = event_type;
		sp->pos[0] = x * 2.0;
		sp->pos[1] = y * 2.0;
		sp->pos[2] = z * -2.0;
		sp->alpha_mod = 255;
		mesh_add_floor(em->floor_markers[info->floor_marker_type], x, y, z);
	}
}

void event_markers_set_blend_rate(struct event_markers *em, int x, int y, int z, int alpha_mod)
{
	int i = find_event_sprite(em, x, y, z);
	if (i < 0)
		return;
	em->sprites[i].alpha_mod = alpha_mod;
}
