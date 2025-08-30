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
#include "gfx/gl.h"
#include "system4.h"
#include "system4/cg.h"

#include "dungeon/dtx.h"
#include "dungeon/dungeon.h"
#include "dungeon/skybox.h"
#include "gfx/gfx.h"

struct skybox {
	struct shader shader;
	GLint proj_transform;
	GLuint vao;
	GLuint attr_buffer;
	GLuint index_buffer;
	GLuint texture;
	int nr_indices;
};

struct skybox *skybox_create(enum draw_dungeon_version version, struct dtx *dtx)
{
	if (version == DRAW_DUNGEON_2 || version == DRAW_FIELD) {
		// Skybox is not used in these games.
		return NULL;
	};

	struct skybox *s = xcalloc(1, sizeof(struct skybox));
	gfx_load_shader(&s->shader, "shaders/dungeon_skybox.v.glsl", "shaders/dungeon_skybox.f.glsl");
	s->proj_transform = glGetUniformLocation(s->shader.program, "proj_transform");

	// Set up a vertex array.
	const GLfloat vertices[] = {
		-1.0,  1.0, -1.0,
		-1.0, -1.0, -1.0,
		 1.0, -1.0, -1.0,
		 1.0,  1.0, -1.0,
		-1.0, -1.0,  1.0,
		-1.0,  1.0,  1.0,
		 1.0, -1.0,  1.0,
		 1.0,  1.0,  1.0,
	};
	const GLushort indices[] = {
		0, 1, 2,  2, 3, 0,
		4, 1, 0,  0, 5, 4,
		2, 6, 7,  7, 3, 2,
		4, 5, 7,  7, 6, 4,
		0, 3, 7,  7, 5, 0,
		1, 4, 2,  2, 4, 6,
	};
	glGenVertexArrays(1, &s->vao);
	glBindVertexArray(s->vao);
	glGenBuffers(1, &s->attr_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, s->attr_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	glEnableVertexAttribArray(s->shader.vertex_pos);
	glVertexAttribPointer(s->shader.vertex_pos, 3, GL_FLOAT, GL_FALSE, 0, 0);
	glGenBuffers(1, &s->index_buffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->index_buffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
	glBindVertexArray(0);
	s->nr_indices = sizeof(indices) / sizeof(GLushort);

	// Set up a cubemap texture.
	glGenTextures(1, &s->texture);
	glBindTexture(GL_TEXTURE_CUBE_MAP, s->texture);
	const GLenum targets[6] = {
		GL_TEXTURE_CUBE_MAP_POSITIVE_Z,  // north
		GL_TEXTURE_CUBE_MAP_NEGATIVE_X,  // west
		GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,  // south
		GL_TEXTURE_CUBE_MAP_POSITIVE_X,  // east
		GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,  // bottom
		GL_TEXTURE_CUBE_MAP_POSITIVE_Y,  // top
	};
	for (int i = 0; i < 6; i++) {
		struct cg *cg = dtx_create_cg(dtx, DTX_SKYBOX, i);
		if (!cg) {
			WARNING("Cannot load skybox texture %d", i);
			continue;
		}
		glTexImage2D(targets[i], 0, GL_RGBA, cg->metrics.w, cg->metrics.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, cg->pixels);
		cg_free(cg);
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

	return s;
}

void skybox_free(struct skybox *s)
{
	if (!s)
		return;
	glDeleteVertexArrays(1, &s->vao);
	glDeleteBuffers(1, &s->attr_buffer);
	glDeleteBuffers(1, &s->index_buffer);
	glDeleteTextures(1, &s->texture);
	glDeleteProgram(s->shader.program);
	free(s);
}

void skybox_render(struct skybox *s, const mat4 view_transform, const mat4 proj_transform)
{
	if (!s)
		return;
	glDepthFunc(GL_LEQUAL);
	glUseProgram(s->shader.program);

	glUniformMatrix4fv(s->shader.view_transform, 1, GL_FALSE, view_transform[0]);
	glUniformMatrix4fv(s->proj_transform, 1, GL_FALSE, proj_transform[0]);

	glBindVertexArray(s->vao);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, s->texture);
	glUniform1i(s->shader.texture, 0);

	glDrawElements(GL_TRIANGLES, s->nr_indices, GL_UNSIGNED_SHORT, NULL);

	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
	glDepthFunc(GL_LESS);
}
