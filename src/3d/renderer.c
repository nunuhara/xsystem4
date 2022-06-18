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

#include <assert.h>
#include <cglm/cglm.h>

#include "system4.h"

#include "3d_internal.h"
#include "reign.h"
#include "sact.h"
#include "vm.h"

struct RE_renderer *RE_renderer_new(struct texture *texture)
{
	struct RE_renderer *r = xcalloc(1, sizeof(struct RE_renderer));

	gfx_load_shader(&r->shader, "shaders/reign.v.glsl", "shaders/reign.f.glsl");
	r->local_transform = glGetUniformLocation(r->shader.program, "local_transform");
	r->proj_transform = glGetUniformLocation(r->shader.program, "proj_transform");
	r->alpha_mod = glGetUniformLocation(r->shader.program, "alpha_mod");
	r->has_bones = glGetUniformLocation(r->shader.program, "has_bones");
	r->bone_matrices = glGetUniformLocation(r->shader.program, "bone_matrices");
	r->vertex_normal = glGetAttribLocation(r->shader.program, "vertex_normal");
	r->vertex_uv = glGetAttribLocation(r->shader.program, "vertex_uv");
	r->vertex_bone_index = glGetAttribLocation(r->shader.program, "vertex_bone_index");
	r->vertex_bone_weight = glGetAttribLocation(r->shader.program, "vertex_bone_weight");

	glGenRenderbuffers(1, &r->depth_buffer);
	glBindRenderbuffer(GL_RENDERBUFFER, r->depth_buffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, texture->w, texture->h);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	r->last_frame_timestamp = SDL_GetTicks();
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

static void interpolate_motion_frame(struct mot_frame *m1, struct mot_frame *m2, float t, struct mot_frame *out)
{
	glm_vec3_lerp(m1->pos, m2->pos, t, out->pos);
	glm_quat_nlerp(m1->rotq, m2->rotq, t, out->rotq);
}

static void update_motion(struct motion *m, float delta_frame)
{
	if (!m || m->state == RE_MOTION_STATE_STOP)
		return;
	m->current_frame += delta_frame;
	switch (m->state) {
	case RE_MOTION_STATE_NOLOOP:
		if (m->current_frame > m->frame_end) {
			m->current_frame = m->frame_end;
			m->state = RE_MOTION_STATE_STOP;
		}
		break;
	case RE_MOTION_STATE_LOOP:
		if (m->current_frame > m->loop_frame_end)
			m->current_frame = m->loop_frame_begin + fmodf(m->current_frame - m->loop_frame_begin, m->loop_frame_end - m->loop_frame_begin);
		break;
	default:
		VM_ERROR("Invalid motion state %d", m->state);
	}
}

static void calc_motion_frame(struct motion *m, int bone, struct mot_frame *out)
{
	int cur_frame = m->current_frame;
	int next_frame = ceilf(m->current_frame);
	float t = m->current_frame - cur_frame;
	struct mot_frame *frames = m->mot->motions[bone]->frames;
	interpolate_motion_frame(&frames[cur_frame], &frames[next_frame], t, out);
}

static void animate(struct RE_instance *inst, struct RE_renderer *r, float delta_time)
{
	float delta_frame = delta_time * inst->fps;
	update_motion(inst->motion, delta_frame);
	update_motion(inst->next_motion, delta_frame);

	for (int i = 0; i < inst->model->nr_bones; i++) {
		struct mot_frame mf;
		calc_motion_frame(inst->motion, i, &mf);
		if (inst->motion_blend && inst->next_motion) {
			struct mot_frame next_mf;
			calc_motion_frame(inst->next_motion, i, &next_mf);
			interpolate_motion_frame(&mf, &next_mf, inst->motion_blend_rate, &mf);
		}
		mat4 bone_transform;
		glm_translate_make(bone_transform, mf.pos);
		glm_quat_rotate(bone_transform, mf.rotq, bone_transform);

		if (inst->model->bones[i].parent >= 0) {
			assert(inst->model->bones[i].parent < i);
			glm_mat4_mul(inst->bone_transforms[inst->model->bones[i].parent], bone_transform, bone_transform);
		}
		glm_mat4_copy(bone_transform, inst->bone_transforms[i]);

		glm_mat4_mul(bone_transform, inst->model->bones[i].inverse_bind_matrix, r->bone_transforms[i]);
	}
}

static void render_instance(struct RE_instance *inst, struct RE_renderer *r, mat4 view_transform, mat4 proj_transform, float delta_time)
{
	if (!inst->draw || !inst->model)
		return;

	if (inst->local_transform_needs_update)
		update_local_transform(inst);

	animate(inst, r, delta_time);

	glUseProgram(r->shader.program);
	glUniformMatrix4fv(r->shader.view_transform, 1, GL_FALSE, view_transform[0]);
	glUniformMatrix4fv(r->proj_transform, 1, GL_FALSE, proj_transform[0]);
	glUniformMatrix4fv(r->local_transform, 1, GL_FALSE, inst->local_transform[0]);
	glUniform1f(r->alpha_mod, inst->alpha);

	if (inst->model->nr_bones > 0) {
		glUniform1i(r->has_bones, GL_TRUE);
		glUniformMatrix4fv(r->bone_matrices, inst->model->nr_bones, GL_FALSE, r->bone_transforms[0][0]);
	} else {
		glUniform1i(r->has_bones, GL_FALSE);
	}

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

static void render_back_cg(struct texture *dst, struct RE_back_cg *bcg, struct RE_renderer *r)
{
	if (!bcg->texture.handle)
		return;
	int sw = bcg->texture.w;
	int sh = bcg->texture.h;
	gfx_copy_stretch_blend(dst, bcg->x, bcg->y, sw * bcg->mag, sh * bcg->mag, &bcg->texture, 0, 0, sw, sh, bcg->blend_rate * 255);
}

void RE_render(struct sact_sprite *sp)
{
	uint32_t timestamp = SDL_GetTicks();

	struct RE_plugin *plugin = (struct RE_plugin *)sp->plugin;
	struct RE_renderer *r = plugin->renderer;
	if (!r) {
		r->last_frame_timestamp = timestamp;
		return;
	}
	sprite_dirty(sp);
	struct texture *texture = sprite_get_texture(sp);

	float delta_time = (timestamp - r->last_frame_timestamp) / 1000.0;

	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, texture, 0, 0, texture->w, texture->h);
	glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, r->depth_buffer);
	if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		ERROR("Incomplete framebuffer");

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Draw background CGs.
	glDisable(GL_DEPTH_TEST);
	for (int i = 0; i < RE_NR_BACK_CGS; i++)
		render_back_cg(texture, &plugin->back_cg[i], r);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);

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

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	for (int i = 0; i < RE_MAX_INSTANCES; i++) {
		if (plugin->instances[i])
			render_instance(plugin->instances[i], r, view_transform, plugin->proj_transform, delta_time);
	}

	plugin->proj_transform[1][1] *= -1;
	glFrontFace(GL_CCW);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);

	r->last_frame_timestamp = timestamp;
}
