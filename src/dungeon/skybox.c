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
	int texture_set;
};

static void skybox_load_textures(struct skybox *s, struct dtx *dtx)
{
	static const uint8_t fallback_pixel[4] = { 0, 0, 0, 255 };
	const GLenum targets[6] = {
		GL_TEXTURE_CUBE_MAP_POSITIVE_Z,  // north
		GL_TEXTURE_CUBE_MAP_NEGATIVE_X,  // west
		GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,  // south
		GL_TEXTURE_CUBE_MAP_POSITIVE_X,  // east
		GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,  // bottom
		GL_TEXTURE_CUBE_MAP_POSITIVE_Y,  // top
	};
	struct cg *cgs[6] = {0};
	int fallback_w = 1, fallback_h = 1;
	int base = s->texture_set >= 0 ? s->texture_set * 6 : 0;
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, s->texture);
	for (int i = 0; i < 6; i++) {
		struct cg *cg = dtx ? dtx_create_cg(dtx, DTX_SKYBOX, base + i) : NULL;
		if (!cg || cg->metrics.w <= 0 || cg->metrics.h <= 0 || !cg->pixels) {
			WARNING("Cannot load skybox texture %d", base + i);
			if (cg)
				cg_free(cg);
			continue;
		}
		cgs[i] = cg;
		if (fallback_w == 1 && fallback_h == 1 && cg->metrics.w == cg->metrics.h) {
			fallback_w = cg->metrics.w;
			fallback_h = cg->metrics.h;
		}
	}

	uint8_t *fallback_buf = NULL;
	const uint8_t *fallback_pixels = fallback_pixel;
	for (int i = 0; i < 6; i++) {
		if (cgs[i]) {
			glTexImage2D(targets[i], 0, GL_RGBA, cgs[i]->metrics.w, cgs[i]->metrics.h, 0,
				GL_RGBA, GL_UNSIGNED_BYTE, cgs[i]->pixels);
			cg_free(cgs[i]);
		} else {
			if (!fallback_buf && (fallback_w > 1 || fallback_h > 1)) {
				size_t nr_pixels = (size_t)fallback_w * (size_t)fallback_h;
				fallback_buf = xcalloc(nr_pixels, 4);
				for (size_t p = 0; p < nr_pixels; p++)
					fallback_buf[p * 4 + 3] = 255;
				fallback_pixels = fallback_buf;
			}
			glTexImage2D(targets[i], 0, GL_RGBA, fallback_w, fallback_h, 0,
				GL_RGBA, GL_UNSIGNED_BYTE, fallback_pixels);
		}
	}
	free(fallback_buf);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

static void skybox_init_gl(struct skybox *s, struct dtx *dtx)
{
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
	if (s->shader.vertex_pos >= 0) {
		glEnableVertexAttribArray(s->shader.vertex_pos);
		glVertexAttribPointer(s->shader.vertex_pos, 3, GL_FLOAT, GL_FALSE, 0, 0);
	}
	glGenBuffers(1, &s->index_buffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->index_buffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	s->nr_indices = sizeof(indices) / sizeof(GLushort);

	// Set up a cubemap texture.
	glGenTextures(1, &s->texture);
	skybox_load_textures(s, dtx);
}

void skybox_set_texture_set(struct skybox *s, struct dtx *dtx, int set)
{
	if (!s || set < 0)
		return;
	s->texture_set = set;
	if (s->texture)
		skybox_load_textures(s, dtx);
}

void skybox_reset_gl(struct skybox *s, struct dtx *dtx)
{
	if (!s)
		return;
	s->shader.program = 0;
	s->vao = 0;
	s->attr_buffer = 0;
	s->index_buffer = 0;
	s->texture = 0;
	skybox_init_gl(s, dtx);
}

struct skybox *skybox_create(enum draw_dungeon_version version, struct dtx *dtx)
{
	if (version == DRAW_DUNGEON_2 || version == DRAW_FIELD) {
		// Skybox is not used in these games.
		return NULL;
	};

	struct skybox *s = xcalloc(1, sizeof(struct skybox));
	skybox_init_gl(s, dtx);

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
	if (!s || !glIsVertexArray(s->vao) || !glIsTexture(s->texture))
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
