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

#include <limits.h>
#include <cglm/cglm.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/hashtable.h"

#include "3d_internal.h"
#include "asset_manager.h"
#include "reign.h"
#include "sact.h"

#define BONE_TRANSFORMS_BINDING 0

// TODO: Respect RE_plugin.shadow_map_resolution_level
#define SHADOW_WIDTH 512
#define SHADOW_HEIGHT 512

enum {
	COLOR_TEXTURE_UNIT,
	SPECULAR_TEXTURE_UNIT,
	ALPHA_TEXTURE_UNIT,
	LIGHT_TEXTURE_UNIT,
	NORMAL_TEXTURE_UNIT,
	SHADOW_TEXTURE_UNIT,
};

enum {
	DIFFUSE_EMISSIVE = 0,
	DIFFUSE_NORMAL = 1,
	DIFFUSE_LIGHT_MAP = 2,
	DIFFUSE_ENV_MAP = 3,
};

enum {
	ALPHA_BLEND = 0,
	ALPHA_TEST = 1,
	ALPHA_MAP_BLEND = 2,
	ALPHA_MAP_TEST = 3,
};

enum draw_phase {
	DRAW_OPAQUE,
	DRAW_TRANSPARENT,
};

static GLuint load_shader(const char *vertex_shader_path, const char *fragment_shader_path)
{
	char defines[256];
	snprintf(defines, sizeof(defines),
			 "#define REIGN_ENGINE %d\n"
			 "#define TAPIR_ENGINE %d\n"
			 "#define ENGINE %d\n",
			 RE_REIGN_PLUGIN,
			 RE_TAPIR_PLUGIN,
			 re_plugin_version);
	GLuint program = glCreateProgram();
	GLuint vertex_shader = gfx_load_shader_file(vertex_shader_path, GL_VERTEX_SHADER, defines);
	GLuint fragment_shader = gfx_load_shader_file(fragment_shader_path, GL_FRAGMENT_SHADER, defines);

	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);

	// Bind vertex attributes to fixed locations, so that common VAO can be used
	// by multiple programs.
	glBindAttribLocation(program, VATTR_POS, "vertex_pos");
	glBindAttribLocation(program, VATTR_NORMAL, "vertex_normal");
	glBindAttribLocation(program, VATTR_UV, "vertex_uv");
	glBindAttribLocation(program, VATTR_BONE_INDEX, "vertex_bone_index");
	glBindAttribLocation(program, VATTR_BONE_WEIGHT, "vertex_bone_weight");
	glBindAttribLocation(program, VATTR_LIGHT_UV, "vertex_light_uv");
	glBindAttribLocation(program, VATTR_COLOR, "vertex_color");
	glBindAttribLocation(program, VATTR_TANGENT, "vertex_tangent");

	glLinkProgram(program);

	GLint link_success;
	glGetProgramiv(program, GL_LINK_STATUS, &link_success);
	if (!link_success) {
		GLint len;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
		char *infolog = xmalloc(len + 1);
		glGetProgramInfoLog(program, len, NULL, infolog);
		ERROR("Failed to link shader %s, %s: %s", vertex_shader_path, fragment_shader_path, infolog);
	}
	return program;
}

static void init_shadow_renderer(struct shadow_renderer *sr)
{
	GLint orig_fbo;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &orig_fbo);

	sr->program = load_shader("shaders/reign_shadow.v.glsl", "shaders/reign_shadow.f.glsl");
	sr->local_transform = glGetUniformLocation(sr->program, "local_transform");
	sr->view_transform = glGetUniformLocation(sr->program, "view_transform");
	sr->has_bones = glGetUniformLocation(sr->program, "has_bones");
	GLuint bone_transforms = glGetUniformBlockIndex(sr->program, "BoneTransforms");
	glUniformBlockBinding(sr->program, bone_transforms, BONE_TRANSFORMS_BINDING);

	glGenFramebuffers(1, &sr->fbo);
	glGenTextures(1, &sr->texture);
	glBindTexture(GL_TEXTURE_2D, sr->texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindFramebuffer(GL_FRAMEBUFFER, sr->fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sr->texture, 0);
	glDrawBuffers(0, NULL);
	glReadBuffer(GL_NONE);

	glBindFramebuffer(GL_FRAMEBUFFER, orig_fbo);
}

static void destroy_shadow_renderer(struct shadow_renderer *sr)
{
	glDeleteTextures(1, &sr->texture);
	glDeleteFramebuffers(1, &sr->fbo);
	glDeleteProgram(sr->program);
}

static void init_outline_renderer(struct outline_renderer *or)
{
	if (re_plugin_version < RE_TAPIR_PLUGIN) {
		or->program = 0;
		return;
	}
	or->program = load_shader("shaders/reign_outline.v.glsl", "shaders/reign_outline.f.glsl");
	or->local_transform = glGetUniformLocation(or->program, "local_transform");
	or->view_transform = glGetUniformLocation(or->program, "view_transform");
	or->proj_transform = glGetUniformLocation(or->program, "proj_transform");
	or->normal_transform = glGetUniformLocation(or->program, "normal_transform");
	or->has_bones = glGetUniformLocation(or->program, "has_bones");
	GLuint bone_transforms = glGetUniformBlockIndex(or->program, "BoneTransforms");
	glUniformBlockBinding(or->program, bone_transforms, BONE_TRANSFORMS_BINDING);
	or->outline_color = glGetUniformLocation(or->program, "outline_color");
	or->outline_thickness = glGetUniformLocation(or->program, "outline_thickness");
}

static void destroy_outline_renderer(struct outline_renderer *or)
{
	if (or->program)
		glDeleteProgram(or->program);
}

static void init_billboard_mesh(struct RE_renderer *r)
{
	static const GLfloat vertices[] = {
		// x,    y,   z,    u,   v
		-1.0,  1.0, 0.0,  0.0, 0.0,
		-1.0, -1.0, 0.0,  0.0, 1.0,
		 1.0,  1.0, 0.0,  1.0, 0.0,
		 1.0, -1.0, 0.0,  1.0, 1.0,
	};
	glGenVertexArrays(1, &r->billboard_vao);
	glBindVertexArray(r->billboard_vao);
	glGenBuffers(1, &r->billboard_attr_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, r->billboard_attr_buffer);

	glEnableVertexAttribArray(VATTR_POS);
	glVertexAttribPointer(VATTR_POS, 3, GL_FLOAT, GL_FALSE, 20, (const void *)0);
	glEnableVertexAttribArray(VATTR_UV);
	glVertexAttribPointer(VATTR_UV, 2, GL_FLOAT, GL_FALSE, 20, (const void *)12);
	glDisableVertexAttribArray(VATTR_LIGHT_UV);
	glVertexAttrib2f(VATTR_LIGHT_UV, 0.0, 0.0);
	glDisableVertexAttribArray(VATTR_COLOR);
	glVertexAttrib4f(VATTR_COLOR, 1.0, 1.0, 1.0, 1.0);
	glDisableVertexAttribArray(VATTR_NORMAL);
	glVertexAttrib3f(VATTR_NORMAL, 0.0, 0.0, 1.0);
	glDisableVertexAttribArray(VATTR_TANGENT);
	glVertexAttrib3f(VATTR_TANGENT, 1.0, 0.0, 0.0);
	glDisableVertexAttribArray(VATTR_BONE_INDEX);
	glVertexAttribI4i(VATTR_BONE_INDEX, 0, 0, 0, 0);
	glDisableVertexAttribArray(VATTR_BONE_WEIGHT);
	glVertexAttrib4f(VATTR_BONE_WEIGHT, 0.0, 0.0, 0.0, 0.0);

	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void destroy_billboard_mesh(struct RE_renderer *r)
{
	glDeleteVertexArrays(1, &r->billboard_vao);
	glDeleteBuffers(1, &r->billboard_attr_buffer);
}

struct RE_renderer *RE_renderer_new(void)
{
	struct RE_renderer *r = xcalloc(1, sizeof(struct RE_renderer));

	r->program = load_shader("shaders/reign.v.glsl", "shaders/reign.f.glsl");
	r->view_transform = glGetUniformLocation(r->program, "view_transform");
	r->texture = glGetUniformLocation(r->program, "tex");
	r->local_transform = glGetUniformLocation(r->program, "local_transform");
	r->proj_transform = glGetUniformLocation(r->program, "proj_transform");
	r->normal_transform = glGetUniformLocation(r->program, "normal_transform");
	r->alpha_mod = glGetUniformLocation(r->program, "alpha_mod");
	r->has_bones = glGetUniformLocation(r->program, "has_bones");
	GLuint bone_transforms = glGetUniformBlockIndex(r->program, "BoneTransforms");
	glUniformBlockBinding(r->program, bone_transforms, BONE_TRANSFORMS_BINDING);
	r->global_ambient = glGetUniformLocation(r->program, "global_ambient");
	r->instance_ambient = glGetUniformLocation(r->program, "instance_ambient");
	for (int i = 0; i < NR_DIR_LIGHTS; i++) {
		char buf[64];
		sprintf(buf, "dir_lights[%d].dir", i);
		r->dir_lights[i].dir = glGetUniformLocation(r->program, buf);
		sprintf(buf, "dir_lights[%d].diffuse", i);
		r->dir_lights[i].diffuse = glGetUniformLocation(r->program, buf);
		sprintf(buf, "dir_lights[%d].globe_diffuse", i);
		r->dir_lights[i].globe_diffuse = glGetUniformLocation(r->program, buf);
	}
	r->specular_light_dir = glGetUniformLocation(r->program, "specular_light_dir");
	r->specular_strength = glGetUniformLocation(r->program, "specular_strength");
	r->specular_shininess = glGetUniformLocation(r->program, "specular_shininess");
	r->use_specular_map = glGetUniformLocation(r->program, "use_specular_map");
	r->specular_texture = glGetUniformLocation(r->program, "specular_texture");
	r->rim_exponent = glGetUniformLocation(r->program, "rim_exponent");
	r->rim_color = glGetUniformLocation(r->program, "rim_color");
	r->camera_pos = glGetUniformLocation(r->program, "camera_pos");
	r->diffuse_type = glGetUniformLocation(r->program, "diffuse_type");
	r->light_texture = glGetUniformLocation(r->program, "light_texture");
	r->use_normal_map = glGetUniformLocation(r->program, "use_normal_map");
	r->normal_texture = glGetUniformLocation(r->program, "normal_texture");
	r->shadow_darkness = glGetUniformLocation(r->program, "shadow_darkness");
	r->shadow_transform = glGetUniformLocation(r->program, "shadow_transform");
	r->shadow_texture = glGetUniformLocation(r->program, "shadow_texture");
	r->shadow_bias = glGetUniformLocation(r->program, "shadow_bias");
	r->fog_type = glGetUniformLocation(r->program, "fog_type");
	r->fog_near = glGetUniformLocation(r->program, "fog_near");
	r->fog_far = glGetUniformLocation(r->program, "fog_far");
	r->fog_color = glGetUniformLocation(r->program, "fog_color");
	r->ls_params = glGetUniformLocation(r->program, "ls_params");
	r->ls_light_dir = glGetUniformLocation(r->program, "ls_light_dir");
	r->ls_light_color = glGetUniformLocation(r->program, "ls_light_color");
	r->ls_sun_color = glGetUniformLocation(r->program, "ls_sun_color");
	r->alpha_mode = glGetUniformLocation(r->program, "alpha_mode");
	r->alpha_texture = glGetUniformLocation(r->program, "alpha_texture");
	r->uv_scroll = glGetUniformLocation(r->program, "uv_scroll");

	glGenRenderbuffers(1, &r->depth_buffer);

	init_shadow_renderer(&r->shadow);
	init_outline_renderer(&r->outline);
	init_billboard_mesh(r);
	r->billboard_textures = ht_create(256);
	r->last_frame_timestamp = SDL_GetTicks();
	return r;
}

void RE_renderer_set_viewport_size(struct RE_renderer *r, int width, int height)
{
	r->viewport_width = width;
	r->viewport_height = height;
	glBindRenderbuffer(GL_RENDERBUFFER, r->depth_buffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

bool RE_renderer_load_billboard_texture(struct RE_renderer *r, int cg_no)
{
	if (ht_get_int(r->billboard_textures, cg_no, NULL))
		return true;

	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return false;

	struct billboard_texture *bt = xcalloc(1, sizeof(struct billboard_texture));
	glGenTextures(1, &bt->texture);
	glBindTexture(GL_TEXTURE_2D, bt->texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cg->metrics.w, cg->metrics.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, cg->pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);
	bt->has_alpha = cg->metrics.has_alpha;

	cg_free(cg);
	ht_put_int(r->billboard_textures, cg_no, bt);
	return true;
}

static void free_billboard_texture(void *value)
{
	struct billboard_texture *bt = value;
	glDeleteTextures(1, &bt->texture);
	free(bt);
}

void RE_renderer_free(struct RE_renderer *r)
{
	glDeleteProgram(r->program);
	glDeleteRenderbuffers(1, &r->depth_buffer);
	ht_foreach_value(r->billboard_textures, free_billboard_texture);
	ht_free_int(r->billboard_textures);
	destroy_billboard_mesh(r);
	destroy_shadow_renderer(&r->shadow);
	destroy_outline_renderer(&r->outline);
	free(r);
}

void RE_calc_view_matrix(struct RE_camera *camera, vec3 up, mat4 out)
{
	vec3 front = { 0.0, 0.0, -1.0 };
	vec3 euler = {
		glm_rad(camera->pitch + camera->quake_pitch),
		glm_rad(camera->yaw + camera->quake_yaw),
		glm_rad(camera->roll)
	};
	mat4 rot;
	glm_euler(euler, rot);
	glm_mat4_mulv3(rot, front, 0.0, front);
	glm_look(camera->pos, front, up, out);
}

static bool should_draw_shadow(struct mesh *mesh, struct material *material)
{
	return !(mesh->flags & (MESH_ALPHA | MESH_BOTH | MESH_SPRITE))
	    && !(material->flags & (MATERIAL_ALPHA | MATERIAL_SPRITE));
}

static void render_model(struct RE_instance *inst, struct RE_renderer *r, enum draw_phase phase)
{
	struct model *model = inst->model;
	if (!inst->model)
		return;
	bool all_transparent = inst->alpha < 1.0f;
	bool all_opaque = !model->has_transparent_mesh && inst->alpha >= 1.0f;
	if ((phase == DRAW_OPAQUE && all_transparent) || (phase == DRAW_TRANSPARENT && all_opaque))
		return;

	if (inst->local_transform_needs_update)
		RE_instance_update_local_transform(inst);

	glUniformMatrix4fv(r->local_transform, 1, GL_FALSE, inst->local_transform[0]);
	glUniformMatrix3fv(r->normal_transform, 1, GL_FALSE, inst->normal_transform[0]);
	glUniform1f(r->alpha_mod, inst->alpha);
	glUniform3fv(r->instance_ambient, 1, inst->ambient);

	bool draw_shadow = inst->draw_shadow && inst->plugin->shadow_mode;
	if (draw_shadow) {
		glActiveTexture(GL_TEXTURE0 + SHADOW_TEXTURE_UNIT);
		glBindTexture(GL_TEXTURE_2D, r->shadow.texture);
		glUniform1i(r->shadow_texture, SHADOW_TEXTURE_UNIT);
	}

	for (int i = 0; i < model->nr_meshes; i++) {
		struct mesh *mesh = &model->meshes[i];
		if (mesh->hidden)
			continue;
		struct material *material = &model->materials[mesh->material];
		bool is_transparent = mesh->is_transparent || inst->alpha < 1.0f;
		if (phase != (is_transparent ? DRAW_TRANSPARENT : DRAW_OPAQUE))
			continue;

		if (mesh->flags & MESH_BLEND_ADDITIVE) {
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
		} else {
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
		}

		glUniform1i(r->fog_type, (inst->plugin->fog_mode && !(mesh->flags & MESH_NOLIGHTING))
			? inst->plugin->fog_type : 0);

		GLboolean use_specular_map = GL_FALSE;
		if (inst->plugin->specular_mode && !(mesh->flags & MESH_NOLIGHTING)) {
			glUniform1f(r->specular_strength, material->specular_strength);
			glUniform1f(r->specular_shininess, material->specular_shininess);
			if (material->specular_map) {
				glActiveTexture(GL_TEXTURE0 + SPECULAR_TEXTURE_UNIT);
				glBindTexture(GL_TEXTURE_2D, material->specular_map);
				glUniform1i(r->specular_texture, SPECULAR_TEXTURE_UNIT);
				use_specular_map = GL_TRUE;
			}
		} else {
			glUniform1f(r->specular_strength, 0.0);
			glUniform1f(r->specular_shininess, 0.0);
		}
		glUniform1i(r->use_specular_map, use_specular_map);

		glUniform1f(r->rim_exponent, material->rim_exponent);
		glUniform3fv(r->rim_color, 1, material->rim_color);

		glActiveTexture(GL_TEXTURE0 + COLOR_TEXTURE_UNIT);
		int animation_index = inst->texture_animation_index < material->nr_color_maps
			? inst->texture_animation_index : 0;
		glBindTexture(GL_TEXTURE_2D, material->color_maps[animation_index]);
		glUniform1i(r->texture, COLOR_TEXTURE_UNIT);

		if (draw_shadow && should_draw_shadow(mesh, material))
			glUniform1f(r->shadow_darkness, material->shadow_darkness);
		else
			glUniform1f(r->shadow_darkness, 0.0f);

		if (mesh->flags & MESH_ENVMAP) {
			glUniform1i(r->diffuse_type, DIFFUSE_ENV_MAP);
		} else if (mesh->flags & MESH_NOLIGHTING) {
			glUniform1i(r->diffuse_type, DIFFUSE_EMISSIVE);
		} else if (material->light_map && mesh->flags & MESH_HAS_LIGHT_UV && inst->plugin->light_map_mode) {
			glUniform1i(r->diffuse_type, DIFFUSE_LIGHT_MAP);
			glActiveTexture(GL_TEXTURE0 + LIGHT_TEXTURE_UNIT);
			glBindTexture(GL_TEXTURE_2D, material->light_map);
			glUniform1i(r->light_texture, LIGHT_TEXTURE_UNIT);
		} else {
			glUniform1i(r->diffuse_type, DIFFUSE_NORMAL);
		}

		if (material->normal_map && inst->draw_bump && inst->plugin->bump_mode) {
			glUniform1i(r->use_normal_map, GL_TRUE);
			glActiveTexture(GL_TEXTURE0 + NORMAL_TEXTURE_UNIT);
			glBindTexture(GL_TEXTURE_2D, material->normal_map);
			glUniform1i(r->normal_texture, NORMAL_TEXTURE_UNIT);
		} else {
			glUniform1i(r->use_normal_map, GL_FALSE);
		}

		if (material->alpha_map) {
			glUniform1i(r->alpha_mode, mesh->is_transparent ? ALPHA_MAP_BLEND : ALPHA_MAP_TEST);
			glActiveTexture(GL_TEXTURE0 + ALPHA_TEXTURE_UNIT);
			glBindTexture(GL_TEXTURE_2D, material->alpha_map);
			glUniform1i(r->alpha_texture, ALPHA_TEXTURE_UNIT);
		} else {
			glUniform1i(r->alpha_mode, mesh->is_transparent ? ALPHA_BLEND : ALPHA_TEST);
		}

		vec2 uv_scroll;
		glm_vec2_scale(mesh->uv_scroll, r->last_frame_timestamp / 1000.f, uv_scroll);
		glUniform2fv(r->uv_scroll, 1, uv_scroll);

		glBindVertexArray(mesh->vao);

		if (mesh->flags & MESH_BOTH)
			glDisable(GL_CULL_FACE);

		if (mesh->nr_indices)
			glDrawElements(GL_TRIANGLES, mesh->nr_indices, GL_UNSIGNED_SHORT, NULL);
		else
			glDrawArrays(GL_TRIANGLES, 0, mesh->nr_vertices);

		if (mesh->flags & MESH_BOTH)
			glEnable(GL_CULL_FACE);
		glBindVertexArray(0);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

static void render_static_model(struct RE_instance *inst, struct RE_renderer *r, enum draw_phase phase)
{
	if (!inst->draw)
		return;
	glUniform1i(r->has_bones, GL_FALSE);
	render_model(inst, r, phase);
}

static void render_skinned_model(struct RE_instance *inst, struct RE_renderer *r, enum draw_phase phase)
{
	if (!inst->draw)
		return;
	struct model *model = inst->model;
	if (!model)
		return;

	if (model->nr_bones > 0 && inst->motion) {
		glUniform1i(r->has_bones, GL_TRUE);
		glBindBufferBase(GL_UNIFORM_BUFFER, BONE_TRANSFORMS_BINDING, inst->bone_transforms_ubo);
	} else {
		glUniform1i(r->has_bones, GL_FALSE);
	}
	render_model(inst, r, phase);

	if (inst->shadow_volume_instance && phase == DRAW_TRANSPARENT)
		render_static_model(inst->shadow_volume_instance, r, phase);
}

static void render_billboard(struct RE_instance *inst, struct RE_renderer *r, mat4 view_mat, enum draw_phase phase)
{
	if (!inst->draw)
		return;
	int cg_no = inst->motion->current_frame;
	struct billboard_texture *bt = ht_get_int(r->billboard_textures, cg_no, NULL);
	if (!bt)
		return;
	bool is_transparent = bt->has_alpha || inst->alpha < 1.0f;
	if (phase != (is_transparent ? DRAW_TRANSPARENT : DRAW_OPAQUE))
		return;

	mat4 local_transform;
	glm_translate_make(local_transform, inst->pos);
	mat3 rot;
	glm_mat4_pick3t(view_mat, rot);
	glm_mat4_ins3(rot, local_transform);
	glm_scale(local_transform, inst->scale);
	// Billboard instances are bottomed at y=0.
	glm_translate_y(local_transform, 1.0);
	mat3 normal_transform;
	// This should be safe because billboards do not have non-uniform scaling.
	glm_mat4_pick3(local_transform, normal_transform);

	glUniform3fv(r->instance_ambient, 1, inst->ambient);

	glUniformMatrix4fv(r->local_transform, 1, GL_FALSE, local_transform[0]);
	glUniformMatrix3fv(r->normal_transform, 1, GL_FALSE, normal_transform[0]);
	glUniform1i(r->has_bones, GL_FALSE);
	glUniform1f(r->alpha_mod, inst->alpha);
	glUniform1f(r->specular_strength, 0.0);
	glUniform1f(r->specular_shininess, 0.0);
	glUniform1i(r->use_specular_map, GL_FALSE);
	glUniform1f(r->rim_exponent, 0.0);
	glUniform1i(r->diffuse_type, DIFFUSE_NORMAL);
	glUniform1i(r->use_normal_map, GL_FALSE);
	glUniform1f(r->shadow_darkness, 0.0f);
	glUniform1i(r->alpha_mode, ALPHA_BLEND);
	glUniform1i(r->fog_type, inst->plugin->fog_mode ? inst->plugin->fog_type : 0);
	switch (inst->draw_type) {
	case RE_DRAW_TYPE_NORMAL:
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
		break;
	case RE_DRAW_TYPE_ADDITIVE:
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
		break;
	}

	glDepthFunc(GL_LEQUAL);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, bt->texture);
	glUniform1i(r->texture, 0);
	glBindVertexArray(r->billboard_vao);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glDepthFunc(GL_LESS);
}

static void render_billboard_particles(struct RE_renderer *r, struct RE_instance *inst, struct particle_object *po, float frame)
{
	struct pae_object *pae_obj = po->pae_obj;
	if (pae_obj->nr_textures == 0)
		return;

	glUniform1i(r->diffuse_type, DIFFUSE_EMISSIVE);
	glUniform1i(r->fog_type, 0);

	glActiveTexture(GL_TEXTURE0);
	glUniform1i(r->texture, 0);
	glBindVertexArray(r->billboard_vao);

	for (int index = 0; index < pae_obj->nr_particles; index++) {
		struct particle_instance *pi = &po->instances[index];
		if (frame < pi->begin_frame || pi->end_frame <= frame)
			continue;

		int step = (int)((frame - pi->begin_frame) / pae_obj->texture_anime_frame);
		struct billboard_texture *bt = ht_get(inst->effect->pae->textures, pae_obj->textures[step % pae_obj->nr_textures], NULL);
		if (!bt)
			continue;
		glBindTexture(GL_TEXTURE_2D, bt->texture);

		float alpha = pae_object_calc_alpha(pae_obj, pi, frame);
		if (pae_obj->blend_type == PARTICLE_BLEND_ADDITIVE && !bt->has_alpha)
			alpha *= alpha;  // why...
		glUniform1f(r->alpha_mod, alpha);

		mat4 local_transform;
		particle_object_calc_local_transform(inst, po, pi, frame, local_transform);
		glUniformMatrix4fv(r->local_transform, 1, GL_FALSE, local_transform[0]);
		glUniformMatrix3fv(r->normal_transform, 1, GL_FALSE, local_transform[0]);

		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}

	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

static void render_polygon_particles(struct RE_renderer *r, struct RE_instance *inst, struct particle_object *po, float frame)
{
	struct pae_object *pae_obj = po->pae_obj;
	struct model *model = pae_obj->model;
	if (!model)
		return;

	glUniform1i(r->diffuse_type, DIFFUSE_NORMAL);
	glUniform1i(r->fog_type, inst->plugin->fog_mode ? inst->plugin->fog_type : 0);

	for (int index = 0; index < pae_obj->nr_particles; index++) {
		struct particle_instance *pi = &po->instances[index];
		if (frame < pi->begin_frame || pi->end_frame <= frame)
			continue;

		float alpha = pae_object_calc_alpha(pae_obj, pi, frame);
		glUniform1f(r->alpha_mod, alpha);

		mat4 local_transform;
		particle_object_calc_local_transform(inst, po, pi, frame, local_transform);
		glUniformMatrix4fv(r->local_transform, 1, GL_FALSE, local_transform[0]);
		glUniformMatrix3fv(r->normal_transform, 1, GL_FALSE, local_transform[0]);

		for (int i = 0; i < model->nr_meshes; i++) {
			struct mesh *mesh = &model->meshes[i];
			struct material *material = &model->materials[mesh->material];

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, material->color_maps[0]);
			glUniform1i(r->texture, 0);

			glBindVertexArray(mesh->vao);
			glDrawArrays(GL_TRIANGLES, 0, mesh->nr_vertices);

			glBindVertexArray(0);
			glBindTexture(GL_TEXTURE_2D, 0);
		}
	}
}

static void render_particle_effect(struct RE_instance *inst, struct RE_renderer *r, enum draw_phase phase)
{
	// NOTE: inst->draw flag has no effect for particle effects.
	if (!inst->effect || phase == DRAW_OPAQUE)
		return;

	glUniform3fv(r->instance_ambient, 1, inst->ambient);

	glUniform1i(r->has_bones, GL_FALSE);
	glUniform1f(r->specular_strength, 0.0);
	glUniform1f(r->specular_shininess, 0.0);
	glUniform1i(r->use_specular_map, GL_FALSE);
	glUniform1f(r->rim_exponent, 0.0);
	glUniform1i(r->use_normal_map, GL_FALSE);
	glUniform1f(r->shadow_darkness, 0.0f);
	glUniform1i(r->alpha_mode, ALPHA_BLEND);

	glDepthMask(GL_FALSE);

	for (int i = 0; i < inst->effect->pae->nr_objects; i++) {
		struct particle_object *po = &inst->effect->objects[i];

		switch (po->pae_obj->blend_type) {
		case PARTICLE_BLEND_NORMAL:
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
			break;
		case PARTICLE_BLEND_ADDITIVE:
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
			break;
		}

		switch (po->pae_obj->type) {
		case PARTICLE_TYPE_BILLBOARD:
			render_billboard_particles(r, inst, po, inst->motion->current_frame);
			break;
		case PARTICLE_TYPE_POLYGON_OBJECT:
			render_polygon_particles(r, inst, po, inst->motion->current_frame);
			break;
		case PARTICLE_TYPE_SWORD_BLUR:   // unused
		case PARTICLE_TYPE_CAMERA_QUAKE: // handled in particle_effect_update()
			break;
		}
	}
	glDepthMask(GL_TRUE);
}

static void render_instance(struct RE_instance *inst, struct RE_renderer *r, mat4 view_mat, enum draw_phase phase)
{
	switch (inst->type) {
	case RE_ITYPE_STATIC:
		render_static_model(inst, r, phase);
		break;
	case RE_ITYPE_SKINNED:
		render_skinned_model(inst, r, phase);
		break;
	case RE_ITYPE_BILLBOARD:
		render_billboard(inst, r, view_mat, phase);
		break;
	case RE_ITYPE_PARTICLE_EFFECT:
		render_particle_effect(inst, r, phase);
		break;
	default:
		break;
	}
}

static void merge_spheres(vec4 s1, vec4 s2, vec4 dest)
{
	// glm_sphere_merge() doesn't make the smallest possible sphere, so do it manually.
	float distance = glm_vec3_distance(s1, s2);
	float r1 = s1[3];
	float r2 = s2[3];
	if (distance + r2 <= r1) {
		glm_vec4_copy(s1, dest);
	} else if (distance + r1 <= r2) {
		glm_vec4_copy(s2, dest);
	} else {
		float radius = (distance + r1 + r2) / 2.f;
		vec3 v12;
		glm_vec3_sub(s2, s1, v12);
		glm_vec3_copy(s1, dest);
		glm_vec3_muladds(v12, (radius - r1) / distance, dest);
		dest[3] = radius;
	}
}

static bool calc_shadow_light_transform(struct RE_plugin *plugin, mat4 dest)
{
	// Compute a bounding sphere of shadow casters.
	vec4 bounding_sphere = {};
	for (int i = 0; i < plugin->nr_instances; i++) {
		struct RE_instance *inst = plugin->instances[i];
		if (!inst || !inst->draw || !inst->make_shadow || !inst->model)
			continue;
		if (bounding_sphere[3] > 0.0f)
			merge_spheres(inst->bounding_sphere, bounding_sphere, bounding_sphere);
		else
			glm_vec4_copy(inst->bounding_sphere, bounding_sphere);
	}

	float radius = bounding_sphere[3] * 1.2f;  // Add some padding.
	if (radius <= 0.0f)
		return false;

	// Create a orthographic frustum that contains the bounding sphere.
	mat4 view_matrix;
	glm_look_anyup(bounding_sphere, plugin->shadow_map_light_dir, view_matrix);
	mat4 proj_matrix;
	glm_ortho(-radius, radius, -radius, radius, -radius, radius, proj_matrix);
	glm_mat4_mul(proj_matrix, view_matrix, dest);
	return true;
}

static void render_shadow_map(struct RE_plugin *plugin, mat4 light_space_transform)
{
	if (!calc_shadow_light_transform(plugin, light_space_transform)) {
		// No shadow casters, but proceed anyway to clear the shadow texture.
		glm_mat4_identity(light_space_transform);
	}

	struct RE_renderer *r = plugin->renderer;
	GLint orig_fbo, orig_viewport[4];
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &orig_fbo);
	glGetIntegerv(GL_VIEWPORT, orig_viewport);

	glBindFramebuffer(GL_FRAMEBUFFER, r->shadow.fbo);
	glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
	glClear(GL_DEPTH_BUFFER_BIT);
	glUseProgram(r->shadow.program);

	glUniformMatrix4fv(r->shadow.view_transform, 1, GL_FALSE, light_space_transform[0]);

	glEnable(GL_DEPTH_TEST);

	// Render the shadow casters.
	for (int i = 0; i < plugin->nr_instances; i++) {
		struct RE_instance *inst = plugin->instances[i];
		if (!inst || !inst->draw || !inst->make_shadow || !inst->model)
			continue;
		struct model *model = inst->model;
		if (model->nr_bones > 0) {
			glUniform1i(r->shadow.has_bones, GL_TRUE);
			glBindBufferBase(GL_UNIFORM_BUFFER, BONE_TRANSFORMS_BINDING, inst->bone_transforms_ubo);
		} else {
			glUniform1i(r->shadow.has_bones, GL_FALSE);
		}
		glUniformMatrix4fv(r->shadow.local_transform, 1, GL_FALSE, inst->local_transform[0]);
		for (int j = 0; j < model->nr_meshes; j++) {
			struct mesh *mesh = &model->meshes[j];
			if (mesh->flags & MESH_NOMAKESHADOW)
				continue;
			glBindVertexArray(mesh->vao);
			glDrawArrays(GL_TRIANGLES, 0, mesh->nr_vertices);
		}
		glBindVertexArray(0);
	}

	glDisable(GL_DEPTH_TEST);
	glBindFramebuffer(GL_FRAMEBUFFER, orig_fbo);
	glViewport(orig_viewport[0], orig_viewport[1], orig_viewport[2], orig_viewport[3]);
}

static void render_outlines(struct RE_plugin *plugin, mat4 view_transform)
{
	enum RE_draw_edge_mode mode = plugin->draw_options[RE_DRAW_OPTION_EDGE];
	if (mode == RE_DRAW_EDGE_NONE)
		return;
	struct outline_renderer *or = &plugin->renderer->outline;
	if (!or->program)
		return;
	glUseProgram(or->program);
	glUniformMatrix4fv(or->view_transform, 1, GL_FALSE, view_transform[0]);
	glUniformMatrix4fv(or->proj_transform, 1, GL_FALSE, plugin->proj_transform[0]);

	glCullFace(GL_FRONT);
	for (int i = 0; i < plugin->nr_instances; i++) {
		struct RE_instance *inst = plugin->instances[i];
		if (!inst || !inst->draw)
			continue;
		if (mode == RE_DRAW_EDGE_CHARACTERS_ONLY && inst->type != RE_ITYPE_SKINNED)
			continue;
		struct model *model = inst->model;
		if (inst->model && model->nr_bones > 0) {
			glUniform1i(or->has_bones, GL_TRUE);
			glBindBufferBase(GL_UNIFORM_BUFFER, BONE_TRANSFORMS_BINDING, inst->bone_transforms_ubo);
		} else {
			glUniform1i(or->has_bones, GL_FALSE);
		}
		glUniformMatrix4fv(or->local_transform, 1, GL_FALSE, inst->local_transform[0]);
		glUniformMatrix3fv(or->normal_transform, 1, GL_FALSE, inst->normal_transform[0]);
		for (int j = 0; j < model->nr_meshes; j++) {
			struct mesh *mesh = &model->meshes[j];
			if (mesh->flags & MESH_NO_EDGE)
				continue;
			glUniform3fv(or->outline_color, 1, mesh->outline_color);
			glUniform1f(or->outline_thickness, mesh->outline_thickness);
			glBindVertexArray(mesh->vao);
			glDrawArrays(GL_TRIANGLES, 0, mesh->nr_vertices);
		}
		glBindVertexArray(0);
	}
	glCullFace(GL_BACK);
	glUseProgram(plugin->renderer->program);
}

static void render_back_cg(struct texture *dst, struct RE_back_cg *bcg, struct RE_renderer *r)
{
	if (!bcg->show || !bcg->texture.handle)
		return;
	int sw = bcg->texture.w;
	int sh = bcg->texture.h;
	gfx_copy_stretch_blend_amap_alpha(dst, bcg->x, bcg->y, sw * bcg->mag, sh * bcg->mag, &bcg->texture, 0, 0, sw, sh, bcg->blend_rate * 255);
}

static void setup_lights(struct RE_plugin *plugin)
{
	struct RE_renderer *r = plugin->renderer;
	glUniform3fv(r->camera_pos, 1, plugin->camera.pos);
	glUniform3fv(r->global_ambient, 1, plugin->global_ambient);
	int light_index = 0;
	for (int i = 0; i < plugin->nr_instances; i++) {
		struct RE_instance *inst = plugin->instances[i];
		if (!inst)
			continue;
		switch (inst->type) {
		case RE_ITYPE_DIRECTIONAL_LIGHT:
			if (light_index >= NR_DIR_LIGHTS) {
				WARNING("too many directional lights");
				break;
			}
			glUniform3fv(r->dir_lights[light_index].dir, 1, inst->vec);
			glUniform3fv(r->dir_lights[light_index].diffuse, 1, inst->diffuse);
			glUniform3fv(r->dir_lights[light_index].globe_diffuse, 1, inst->globe_diffuse);
			light_index++;
			break;
		case RE_ITYPE_SPECULAR_LIGHT:
			glUniform3fv(r->specular_light_dir, 1, inst->vec);
			break;
		default:
			break;
		}
	}
	for (; light_index < NR_DIR_LIGHTS; light_index++) {
		const vec3 zero = {0.0, 0.0, 0.0};
		glUniform3fv(r->dir_lights[light_index].dir, 1, zero);
		glUniform3fv(r->dir_lights[light_index].diffuse, 1, zero);
		glUniform3fv(r->dir_lights[light_index].globe_diffuse, 1, zero);
	}
}

static int cmp_instances_by_z(const void *lhs, const void *rhs)
{
	struct RE_instance *l = *(struct RE_instance **)lhs;
	struct RE_instance *r = *(struct RE_instance **)rhs;
	float lz = l ? l->z_from_camera : FLT_MAX;
	float rz = r ? r->z_from_camera : FLT_MAX;
	return (lz > rz) - (lz < rz);  // ascending order.
}

static struct RE_instance **sort_instances(struct RE_plugin *plugin, mat4 view_transform)
{
	struct RE_instance **instances = xmalloc(plugin->nr_instances * sizeof(struct RE_instance *));
	memcpy(instances, plugin->instances, plugin->nr_instances * sizeof(struct RE_instance *));

	for (int i = 0; i < plugin->nr_instances; i++) {
		struct RE_instance *inst = instances[i];
		if (!inst)
			continue;
		vec3 center;
		if (inst->model) {
			glm_aabb_center(inst->model->aabb, center);
			if (inst->local_transform_needs_update)
				RE_instance_update_local_transform(inst);
			glm_mat4_mulv3(inst->local_transform, center, 1.0, center);
		} else {
			glm_vec3_copy(inst->pos, center);
		}
		glm_mat4_mulv3(view_transform, center, 1.0f, center);
		inst->z_from_camera = center[2];
	}

	qsort(instances, plugin->nr_instances, sizeof(struct RE_instance *), cmp_instances_by_z);
	return instances;
}

void RE_render(struct sact_sprite *sp)
{
	struct RE_plugin *plugin = (struct RE_plugin *)sp->plugin;
	struct RE_renderer *r = plugin->renderer;
	if (!r || plugin->suspended)
		return;

	if (re_plugin_version == RE_TAPIR_PLUGIN) {
		uint32_t timestamp = SDL_GetTicks();
		RE_build_model(plugin, timestamp - r->last_frame_timestamp);
		r->last_frame_timestamp = timestamp;
	}

	sprite_dirty(sp);
	struct texture *texture = sprite_get_texture(sp);

	mat4 shadow_transform;
	if (plugin->shadow_mode)
		render_shadow_map(plugin, shadow_transform);
	else
		glm_mat4_identity(shadow_transform);

	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, texture, 0, 0, texture->w, texture->h);
	glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, r->depth_buffer);
	if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		ERROR("Incomplete framebuffer");

	glClearColor(0.f, 0.f, 0.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Draw background CGs.
	glDisable(GL_DEPTH_TEST);
	for (int i = 0; i < RE_NR_BACK_CGS; i++)
		render_back_cg(texture, &plugin->back_cg[i], r);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	mat4 view_transform;
	RE_calc_view_matrix(&plugin->camera, GLM_YUP, view_transform);

	// Tweak the projection transform so that the rendering result is vertically
	// flipped. If we render the scene normally, the resulting image will be
	// bottom-up (the first pixel is at the bottom-left), but we want a top-down
	// image (the first pixel is at the top-left). This changes the coordinate
	// system from right-handed to left-handed, we also need to reverse the
	// winding order.
	plugin->proj_transform[1][1] *= -1;
	glFrontFace(GL_CW);

	glUseProgram(r->program);
	glUniformMatrix4fv(r->view_transform, 1, GL_FALSE, view_transform[0]);
	glUniformMatrix4fv(r->proj_transform, 1, GL_FALSE, plugin->proj_transform[0]);
	glUniformMatrix4fv(r->shadow_transform, 1, GL_FALSE, shadow_transform[0]);
	glUniform1f(r->shadow_bias, plugin->shadow_bias);
	if (plugin->fog_mode) {
		switch (plugin->fog_type) {
		case RE_FOG_NONE:
			break;
		case RE_FOG_LINEAR:
			glUniform1f(r->fog_near, plugin->fog_near);
			glUniform1f(r->fog_far, plugin->fog_far);
			glUniform3fv(r->fog_color, 1, plugin->fog_color);
			break;
		case RE_FOG_LIGHT_SCATTERING:
			glUniform4f(r->ls_params, plugin->ls_beta_r, plugin->ls_beta_m, plugin->ls_g, plugin->ls_distance);
			glUniform3fv(r->ls_light_dir, 1, plugin->ls_light_dir);
			glUniform3fv(r->ls_light_color, 1, plugin->ls_light_color);
			glUniform3fv(r->ls_sun_color, 1, plugin->ls_sun_color);
			break;
		}
	}

	glEnable(GL_BLEND);

	setup_lights(plugin);

	// Sort instances by z-order.
	struct RE_instance **sorted_instances = sort_instances(plugin, view_transform);

	// Render opaque instances, from nearest to farthest.
	for (int i = plugin->nr_instances - 1; i >= 0; i--) {
		struct RE_instance *inst = sorted_instances[i];
		if (!inst)
			continue;
		render_instance(inst, r, view_transform, DRAW_OPAQUE);
	}

	// Render transparent instances, from farthest to nearest.
	for (int i = 0; i < plugin->nr_instances; i++) {
		struct RE_instance *inst = sorted_instances[i];
		if (!inst)
			continue;
		render_instance(inst, r, view_transform, DRAW_TRANSPARENT);
	}

	// Render outlines.
	render_outlines(plugin, view_transform);

	free(sorted_instances);

	plugin->proj_transform[1][1] *= -1;
	glFrontFace(GL_CCW);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glUseProgram(0);
	glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

struct height_detector {
	GLushort *buf;
	vec3 aabb[2];
};

struct height_detector *RE_renderer_create_height_detector(struct RE_renderer *r, struct model *model)
{
	struct height_detector *hd = xcalloc(1, sizeof(struct height_detector));
	vec3 aabb[2];
	memcpy(aabb, model->aabb, sizeof(aabb));
	// Add some Y padding.
	aabb[0][1] -= 1.0;
	aabb[1][1] += 1.0;
	memcpy(hd->aabb, aabb, sizeof(aabb));

	// Set up an orthographic projection matrix looking down the instance from
	// right above.
	mat4 view_matrix, proj_matrix;
	glm_mat4_identity(view_matrix);
	glm_rotate_x(view_matrix, glm_rad(90.0), view_matrix);
	glm_aabb_transform(aabb, view_matrix, aabb);
	glm_ortho_aabb(aabb, proj_matrix);

	GLint orig_fbo, orig_viewport[4];
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &orig_fbo);
	glGetIntegerv(GL_VIEWPORT, orig_viewport);

	// Since OpenGL ES does not allow reading from depth textures, create a
	// color texture and render depth values into it.
	GLuint color_texture;
	glGenTextures(1, &color_texture);
	glBindTexture(GL_TEXTURE_2D, color_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, NULL);

	glBindFramebuffer(GL_FRAMEBUFFER, r->shadow.fbo);
	GLenum draw_buffers[1] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers(1, draw_buffers);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_texture, 0);

	glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
	glClear(GL_DEPTH_BUFFER_BIT);
	glUseProgram(r->shadow.program);
	glUniformMatrix4fv(r->shadow.local_transform, 1, GL_FALSE, view_matrix[0]);
	glUniformMatrix4fv(r->shadow.view_transform, 1, GL_FALSE, proj_matrix[0]);
	glUniform1i(r->shadow.has_bones, GL_FALSE);

	// Bone transforms are not used, but we still need to bind a UBO.
	GLuint bone_transforms_ubo;
	glGenBuffers(1, &bone_transforms_ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, bone_transforms_ubo);
	glBufferData(GL_UNIFORM_BUFFER, MAX_BONES * sizeof(mat3x4), NULL, GL_STATIC_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	glBindBufferBase(GL_UNIFORM_BUFFER, BONE_TRANSFORMS_BINDING, bone_transforms_ubo);

	glEnable(GL_DEPTH_TEST);

	// Render the model.
	for (int i = 0; i < model->nr_meshes; i++) {
		struct mesh *mesh = &model->meshes[i];
		glBindVertexArray(mesh->vao);
		glDrawArrays(GL_TRIANGLES, 0, mesh->nr_vertices);
	}
	glBindVertexArray(0);
	glFinish();

	hd->buf = xmalloc(SHADOW_WIDTH * SHADOW_HEIGHT * sizeof(GLushort));
	glReadPixels(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT, GL_RED_INTEGER, GL_UNSIGNED_SHORT, hd->buf);

	glDisable(GL_DEPTH_TEST);
	glDrawBuffers(0, NULL);
	glReadBuffer(GL_NONE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, orig_fbo);
	glViewport(orig_viewport[0], orig_viewport[1], orig_viewport[2], orig_viewport[3]);

	glDeleteBuffers(1, &bone_transforms_ubo);
	glDeleteTextures(1, &color_texture);
	return hd;
}

void RE_renderer_free_height_detector(struct height_detector *hd)
{
	free(hd->buf);
	free(hd);
}

bool RE_renderer_detect_height(struct height_detector *hd, float x, float z, float *y_out)
{
	float minx = hd->aabb[0][0];
	float maxx = hd->aabb[1][0];
	float miny = hd->aabb[0][1];
	float maxy = hd->aabb[1][1];
	float minz = hd->aabb[0][2];
	float maxz = hd->aabb[1][2];
	if (x < minx || x > maxx || z < minz || z > maxz)
		return false;
	int px = glm_percent(minx, maxx, x) * SHADOW_WIDTH;
	int pz = glm_percent(maxz, minz, z) * SHADOW_HEIGHT;
	GLushort val = hd->buf[pz * SHADOW_WIDTH + px];
	if (!val)
		return false;
	*y_out = glm_lerp(maxy, miny, val / 65535.0f);
	return true;
}
