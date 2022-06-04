/* Copyright (C) 2022 kichikuou <KichikuouChrome@gmail.com>
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

#include <cglm/cglm.h>

#include "system4.h"

#include "3d_internal.h"
#include "reign.h"
#include "sact.h"

struct RE_renderer *RE_renderer_new(struct texture *texture)
{
	struct RE_renderer *r = xcalloc(1, sizeof(struct RE_renderer));

	gfx_load_shader(&r->shader, "shaders/reign.v.glsl", "shaders/reign.f.glsl");
	r->local_transform = glGetUniformLocation(r->shader.program, "local_transform");
	r->proj_transform = glGetUniformLocation(r->shader.program, "proj_transform");
	r->vertex_normal = glGetAttribLocation(r->shader.program, "vertex_normal");
	r->vertex_uv = glGetAttribLocation(r->shader.program, "vertex_uv");

	glGenRenderbuffers(1, &r->depth_buffer);
	glBindRenderbuffer(GL_RENDERBUFFER, r->depth_buffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, texture->w, texture->h);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	return r;
}

void RE_renderer_free(struct RE_renderer *r)
{
	glDeleteProgram(r->shader.program);
	glDeleteRenderbuffers(1, &r->depth_buffer);
	free(r);
}

static void calc_view_matrix(struct RE_camera *camera, mat4 out)
{
	vec3 front = { 0.0, 0.0, -1.0 };
	vec3 euler = {
		glm_rad(camera->pitch),
		glm_rad(camera->yaw),
		glm_rad(camera->roll)
	};
	mat4 rot;
	glm_euler(euler, rot);
	glm_mat4_mulv3(rot, front, 0.0, front);
	vec3 up = {0.0, 1.0, 0.0};
	glm_look(camera->pos, front, up, out);
}

static void update_local_transform(struct RE_instance *inst)
{
	vec3 euler = {
		glm_rad(inst->pitch),
		glm_rad(inst->yaw),
		glm_rad(inst->roll)
	};
	mat4 rot;
	glm_euler(euler, rot);

	glm_translate_make(inst->local_transform, inst->pos);
	glm_mat4_mul(inst->local_transform, rot, inst->local_transform);
	glm_scale(inst->local_transform, inst->scale);

	inst->local_transform_needs_update = false;
}

static void render_instance(struct RE_instance *inst, struct RE_renderer *r, mat4 view_transform, mat4 proj_transform)
{
	if (!inst->draw || !inst->model)
		return;

	if (inst->local_transform_needs_update)
		update_local_transform(inst);

	glUseProgram(r->shader.program);
	glUniformMatrix4fv(r->shader.view_transform, 1, GL_FALSE, view_transform[0]);
	glUniformMatrix4fv(r->proj_transform, 1, GL_FALSE, proj_transform[0]);
	glUniformMatrix4fv(r->local_transform, 1, GL_FALSE, inst->local_transform[0]);

	struct model *model = inst->model;
	for (int i = 0; i < model->nr_meshes; i++) {
		struct mesh *mesh = &model->meshes[i];
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, model->materials[mesh->material].color_map);
		glUniform1i(r->shader.texture, 0);

		glBindVertexArray(mesh->vao);
		glDrawArrays(GL_TRIANGLES, 0, mesh->nr_vertices);
		glBindVertexArray(0);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	glUseProgram(0);
}

void RE_render(struct sact_sprite *sp)
{
	struct RE_plugin *plugin = (struct RE_plugin *)sp->plugin;
	struct RE_renderer *r = plugin->renderer;
	if (!r)
		return;
	sprite_dirty(sp);
	struct texture *texture = sprite_get_texture(sp);

	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, texture, 0, 0, texture->w, texture->h);
	glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, r->depth_buffer);
	if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		ERROR("Incomplete framebuffer");

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	mat4 view_transform;
	calc_view_matrix(&plugin->camera, view_transform);

	// Tweak the projection transform so that the rendering result is vertically
	// flipped. If we render the scene normally, the resulting image will be
	// bottom-up (the first pixel is at the bottom-left), but we want a top-down
	// image (the first pixel is at the top-left). This changes the coordinate
	// system from right-handed to left-handed, we also need to reverse the
	// winding order.
	plugin->proj_transform[1][1] *= -1;
	glFrontFace(GL_CW);

	for (int i = 0; i < RE_MAX_INSTANCES; i++) {
		if (plugin->instances[i])
			render_instance(plugin->instances[i], r, view_transform, plugin->proj_transform);
	}

	plugin->proj_transform[1][1] *= -1;
	glFrontFace(GL_CCW);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
}
