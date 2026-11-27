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
#include <math.h>
#include <cglm/cglm.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/hashtable.h"

#include "3d_internal.h"
#include "asset_manager.h"
#include "reign.h"
#include "sact.h"

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
	BLEND_TEXTURE_UNIT,
	BONE_TEXTURE_UNIT,
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

static GLuint load_shader(const char *vertex_shader_path, const char *fragment_shader_path, enum RE_plugin_version version)
{
	char defines[256];
	snprintf(defines, sizeof(defines),
			 "#define REIGN_ENGINE %d\n"
			 "#define TAPIR_ENGINE %d\n"
			 "#define SEAL_ENGINE %d\n"
			 "#define ENGINE %d\n",
			 RE_REIGN_PLUGIN,
			 RE_TAPIR_PLUGIN,
			 RE_SEAL_PLUGIN,
			 version);
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
	glBindAttribLocation(program, VATTR_BLEND_WEIGHT, "vertex_blend_weight");
	glBindAttribLocation(program, VATTR_BLEND_UV, "vertex_blend_uv");

	glLinkProgram(program);
	glDetachShader(program, vertex_shader);
	glDetachShader(program, fragment_shader);
	glDeleteShader(vertex_shader);
	glDeleteShader(fragment_shader);

	GLint link_success;
	glGetProgramiv(program, GL_LINK_STATUS, &link_success);
	if (!link_success) {
		GLint len;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
		char *infolog = xmalloc(len + 1);
		glGetProgramInfoLog(program, len, NULL, infolog);
		ERROR("Failed to link shader %s, %s: %s", vertex_shader_path, fragment_shader_path, infolog);
	}
	GLint bone_texture = glGetUniformLocation(program, "bone_texture");
	if (bone_texture >= 0) {
		glUseProgram(program);
		glUniform1i(bone_texture, BONE_TEXTURE_UNIT);
		glUseProgram(0);
	}
	return program;
}

static void bind_bone_texture(GLuint texture)
{
	glActiveTexture(GL_TEXTURE0 + BONE_TEXTURE_UNIT);
	glBindTexture(GL_TEXTURE_2D, texture);
	glActiveTexture(GL_TEXTURE0);
}

static void init_shadow_renderer(struct shadow_renderer *sr, enum RE_plugin_version version)
{
	GLint orig_draw_fbo = 0, orig_read_fbo = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &orig_draw_fbo);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &orig_read_fbo);

	sr->program = load_shader("shaders/reign_shadow.v.glsl", "shaders/reign_shadow.f.glsl", version);
	sr->local_transform = glGetUniformLocation(sr->program, "local_transform");
	sr->view_transform = glGetUniformLocation(sr->program, "view_transform");
	sr->has_bones = glGetUniformLocation(sr->program, "has_bones");

	glGenFramebuffers(1, &sr->fbo);
	glGenTextures(1, &sr->texture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, sr->texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glBindTexture(GL_TEXTURE_2D, 0);

	glBindFramebuffer(GL_FRAMEBUFFER, sr->fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sr->texture, 0);
	GLenum none = GL_NONE;
	glDrawBuffers(1, &none);
	glReadBuffer(GL_NONE);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)orig_draw_fbo);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)orig_read_fbo);
}

static void destroy_shadow_renderer(struct shadow_renderer *sr)
{
	glDeleteTextures(1, &sr->texture);
	glDeleteFramebuffers(1, &sr->fbo);
	glDeleteProgram(sr->program);
}

static void init_outline_renderer(struct outline_renderer *or, enum RE_plugin_version version)
{
	if (version < RE_TAPIR_PLUGIN) {
		or->program = 0;
		return;
	}
	or->program = load_shader("shaders/reign_outline.v.glsl", "shaders/reign_outline.f.glsl", version);
	or->local_transform = glGetUniformLocation(or->program, "local_transform");
	or->view_transform = glGetUniformLocation(or->program, "view_transform");
	or->proj_transform = glGetUniformLocation(or->program, "proj_transform");
	or->normal_transform = glGetUniformLocation(or->program, "normal_transform");
	or->has_bones = glGetUniformLocation(or->program, "has_bones");
	or->outline_color = glGetUniformLocation(or->program, "outline_color");
	or->outline_thickness = glGetUniformLocation(or->program, "outline_thickness");
}

static void destroy_outline_renderer(struct outline_renderer *or)
{
	if (or->program)
		glDeleteProgram(or->program);
}

static void init_billboard_unused_vertex_attrs(void)
{
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
	glGenBuffers(1, &r->billboard_particle_attr_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, r->billboard_particle_attr_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	glGenBuffers(1, &r->billboard_instance_attr_buffer);

	glGenVertexArrays(1, &r->billboard_particle_vao);
	glBindVertexArray(r->billboard_particle_vao);
	glBindBuffer(GL_ARRAY_BUFFER, r->billboard_particle_attr_buffer);
	glEnableVertexAttribArray(VATTR_POS);
	glVertexAttribPointer(VATTR_POS, 3, GL_FLOAT, GL_FALSE, 20, (const void *)0);
	glEnableVertexAttribArray(VATTR_UV);
	glVertexAttribPointer(VATTR_UV, 2, GL_FLOAT, GL_FALSE, 20, (const void *)12);
	init_billboard_unused_vertex_attrs();

	glGenVertexArrays(1, &r->billboard_instance_vao);
	glBindVertexArray(r->billboard_instance_vao);
	glBindBuffer(GL_ARRAY_BUFFER, r->billboard_instance_attr_buffer);
	glEnableVertexAttribArray(VATTR_POS);
	glVertexAttribPointer(VATTR_POS, 3, GL_FLOAT, GL_FALSE, 20, (const void *)0);
	glEnableVertexAttribArray(VATTR_UV);
	glVertexAttribPointer(VATTR_UV, 2, GL_FLOAT, GL_FALSE, 20, (const void *)12);
	init_billboard_unused_vertex_attrs();

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void destroy_billboard_mesh(struct RE_renderer *r)
{
	glDeleteVertexArrays(1, &r->billboard_particle_vao);
	glDeleteVertexArrays(1, &r->billboard_instance_vao);
	glDeleteBuffers(1, &r->billboard_particle_attr_buffer);
	glDeleteBuffers(1, &r->billboard_instance_attr_buffer);
}

static void init_renderer_gl_resources(struct RE_renderer *r)
{
	r->program = load_shader("shaders/reign.v.glsl", "shaders/reign.f.glsl", r->version);
	r->view_transform = glGetUniformLocation(r->program, "view_transform");
	r->texture = glGetUniformLocation(r->program, "tex");
	r->local_transform = glGetUniformLocation(r->program, "local_transform");
	r->proj_transform = glGetUniformLocation(r->program, "proj_transform");
	r->normal_transform = glGetUniformLocation(r->program, "normal_transform");
	r->alpha_mod = glGetUniformLocation(r->program, "alpha_mod");
	r->has_bones = glGetUniformLocation(r->program, "has_bones");
	r->global_ambient = glGetUniformLocation(r->program, "global_ambient");
	r->instance_ambient = glGetUniformLocation(r->program, "instance_ambient");
	r->diffuse_mod = glGetUniformLocation(r->program, "diffuse_mod");
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
	r->specular_color = glGetUniformLocation(r->program, "specular_color");
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
	r->hemi_light_dir = glGetUniformLocation(r->program, "hemi_light_dir");
	r->hemi_sky_color = glGetUniformLocation(r->program, "hemi_sky_color");
	r->hemi_mid_color = glGetUniformLocation(r->program, "hemi_mid_color");
	r->hemi_ground_color = glGetUniformLocation(r->program, "hemi_ground_color");
	r->tonemap_param = glGetUniformLocation(r->program, "tonemap_param");
	r->tonemap_param2 = glGetUniformLocation(r->program, "tonemap_param2");
	r->nolighting = glGetUniformLocation(r->program, "nolighting");
	r->grayscale_rate = glGetUniformLocation(r->program, "grayscale_rate");
	r->alpha_mode = glGetUniformLocation(r->program, "alpha_mode");
	r->alpha_texture = glGetUniformLocation(r->program, "alpha_texture");
	r->uv_scroll = glGetUniformLocation(r->program, "uv_scroll");
	r->uv_tiling = glGetUniformLocation(r->program, "uv_tiling");
	r->blend_tex = glGetUniformLocation(r->program, "blend_tex");
	r->use_blend_texture = glGetUniformLocation(r->program, "use_blend_texture");

	glGenRenderbuffers(1, &r->depth_buffer);

	init_shadow_renderer(&r->shadow, r->version);
	init_outline_renderer(&r->outline, r->version);
	init_billboard_mesh(r);
}

GLuint RE_create_texture_from_cg(struct cg *cg, GLint wrap, bool *has_alpha_out)
{
	GLuint texture;
	glGenTextures(1, &texture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cg->metrics.w, cg->metrics.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, cg->pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);
	if (has_alpha_out)
		*has_alpha_out = cg->metrics.has_alpha;
	return texture;
}

static struct billboard_texture *create_billboard_texture(struct cg *cg);

static void reload_billboard_texture_no_cb(struct ht_slot *slot, void *data)
{
	(void)data;
	struct billboard_texture *bt = slot->value;
	if (!bt)
		return;
	bt->texture = 0;
	struct cg *cg = asset_cg_load(slot->ikey);
	if (!cg)
		return;
	bt->texture = RE_create_texture_from_cg(cg, GL_CLAMP_TO_EDGE, &bt->has_alpha);
	cg_free(cg);
}

static void reload_billboard_texture_path_cb(struct ht_slot *slot, void *data)
{
	struct archive *aar = data;
	struct billboard_texture *bt = slot->value;
	if (!bt)
		return;
	bt->texture = 0;
	if (!aar || !slot->key)
		return;
	struct archive_data *dfile = archive_get_by_name(aar, slot->key);
	if (!dfile)
		return;
	struct cg *cg = cg_load_data(dfile);
	archive_free_data(dfile);
	if (!cg)
		return;
	bt->texture = RE_create_texture_from_cg(cg, GL_CLAMP_TO_EDGE, &bt->has_alpha);
	cg_free(cg);
}

void RE_renderer_reload_billboard_textures_by_path(struct RE_renderer *r, struct archive *aar)
{
	if (!r || !r->billboard_textures_by_path)
		return;
	ht_foreach(r->billboard_textures_by_path, reload_billboard_texture_path_cb, aar);
}

static void re_renderer_on_gl_reset(void *user)
{
	struct RE_renderer *r = user;
	if (!r)
		return;
	r->program = 0;
	r->depth_buffer = 0;
	memset(&r->shadow, 0, sizeof(r->shadow));
	memset(&r->outline, 0, sizeof(r->outline));
	r->billboard_particle_vao = 0;
	r->billboard_particle_attr_buffer = 0;
	r->billboard_instance_vao = 0;
	r->billboard_instance_attr_buffer = 0;
	init_renderer_gl_resources(r);
	if (r->viewport_width > 0 && r->viewport_height > 0)
		RE_renderer_set_viewport_size(r, r->viewport_width, r->viewport_height);
	if (r->billboard_textures_by_no)
		ht_foreach(r->billboard_textures_by_no, reload_billboard_texture_no_cb, NULL);
}

struct RE_renderer *RE_renderer_new(enum RE_plugin_version version)
{
	struct RE_renderer *r = xcalloc(1, sizeof(struct RE_renderer));
	r->version = version;
	init_renderer_gl_resources(r);
	r->billboard_textures_by_no = ht_create(256);
	r->billboard_textures_by_path = ht_create(256);
	r->last_frame_timestamp = SDL_GetTicks();
	gfx_register_reset_callback(re_renderer_on_gl_reset, r);
	return r;
}

void RE_renderer_set_viewport_size(struct RE_renderer *r, int width, int height)
{
	r->viewport_width = width;
	r->viewport_height = height;
	glBindRenderbuffer(GL_RENDERBUFFER, r->depth_buffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

static struct billboard_texture *create_billboard_texture(struct cg *cg)
{
	struct billboard_texture *bt = xcalloc(1, sizeof(struct billboard_texture));
	bt->texture = RE_create_texture_from_cg(cg, GL_CLAMP_TO_EDGE, &bt->has_alpha);
	return bt;
}

bool RE_renderer_load_billboard_texture_by_no(struct RE_renderer *r, int cg_no)
{
	if (ht_get_int(r->billboard_textures_by_no, cg_no, NULL))
		return true;

	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return false;

	ht_put_int(r->billboard_textures_by_no, cg_no, create_billboard_texture(cg));
	cg_free(cg);
	return true;
}

struct billboard_texture *RE_renderer_load_billboard_texture_by_path(struct RE_renderer *r, struct archive *aar, const char *path)
{
	struct billboard_texture *bt = ht_get(r->billboard_textures_by_path, path, NULL);
	if (bt)
		return bt;

	struct archive_data *dfile = archive_get_by_name(aar, path);
	if (!dfile)
		return NULL;
	struct cg *cg = cg_load_data(dfile);
	archive_free_data(dfile);
	if (!cg) {
		WARNING("cg_load_data failed: %s", path);
		return NULL;
	}

	bt = create_billboard_texture(cg);
	cg_free(cg);
	ht_put(r->billboard_textures_by_path, path, bt);
	return bt;
}

static void free_billboard_texture(void *value)
{
	struct billboard_texture *bt = value;
	glDeleteTextures(1, &bt->texture);
	free(bt);
}

void RE_renderer_free(struct RE_renderer *r)
{
	gfx_unregister_reset_callback(re_renderer_on_gl_reset, r);
	glDeleteProgram(r->program);
	glDeleteRenderbuffers(1, &r->depth_buffer);
	ht_foreach_value(r->billboard_textures_by_no, free_billboard_texture);
	ht_free_int(r->billboard_textures_by_no);
	ht_foreach_value(r->billboard_textures_by_path, free_billboard_texture);
	ht_free(r->billboard_textures_by_path);
	destroy_billboard_mesh(r);
	destroy_shadow_renderer(&r->shadow);
	destroy_outline_renderer(&r->outline);
	free(r);
}

void RE_calc_view_matrix(struct RE_camera *camera, vec3 up, mat4 out)
{
	vec3 front = { 0.0, 0.0, -1.0 };
	vec3 euler;
	float *pos;
	if (camera->override_active) {
		euler[0] = glm_rad(camera->override_pitch);
		euler[1] = glm_rad(camera->override_yaw);
		euler[2] = glm_rad(camera->override_roll);
		pos = camera->override_pos;
	} else {
		euler[0] = glm_rad(camera->pitch + camera->quake_pitch);
		euler[1] = glm_rad(camera->yaw + camera->quake_yaw);
		euler[2] = glm_rad(camera->roll);
		pos = camera->pos;
	}
	mat4 rot;
	glm_euler_yxz(euler, rot);
	glm_mat4_mulv3(rot, front, 0.0, front);
	vec3 up_v;
	if (up) {
		glm_vec3_copy(up, up_v);  // Use the provided up vector.
	} else {
		// Derive the camera's up by rotating world-Y with the same orientation.
		glm_mat4_mulv3(rot, GLM_YUP, 0.0, up_v);
	}
	glm_look(pos, front, up_v, out);
}

static bool should_draw_shadow(struct mesh *mesh, struct material *material)
{
	return !(mesh->flags & (MESH_ALPHA | MESH_BOTH | MESH_SPRITE | MESH_NO_DRAWSHADOW))
	    && !(material->flags & (MATERIAL_ALPHA | MATERIAL_SPRITE));
}

static bool lighting_disabled(struct RE_plugin *plugin)
{
	return plugin->draw_options[RE_DRAW_OPTION_LIGHTING] <= 0;
}

static bool is_nolighting(struct RE_plugin *plugin, struct mesh *mesh)
{
	return lighting_disabled(plugin) || (mesh->flags & MESH_NOLIGHTING);
}

static void render_model(struct RE_instance *inst, struct RE_renderer *r, enum draw_phase phase)
{
	struct model *model = inst->model;
	if (!inst->model)
		return;

	// NOTE: next_motion's .mpr is not sampled.
	const struct mpr *mpr = (inst->motion && inst->motion->mot) ? inst->motion->mot->mpr : NULL;
	float frame = inst->motion ? inst->motion->current_frame : 0.f;
	struct mpr_modulation obj_mod;
	mpr_evaluate_object(mpr, frame, inst, &obj_mod);

	bool all_transparent = obj_mod.alpha < 1.0f;
	bool all_opaque = !model->has_transparent_mesh && obj_mod.alpha >= 1.0f
		&& !(mpr && mpr->has_mesh_alpha);
	if ((phase == DRAW_OPAQUE && all_transparent) || (phase == DRAW_TRANSPARENT && all_opaque))
		return;

	int mat_tex_index[model->nr_materials > 0 ? model->nr_materials : 1];
	if (model->nr_materials > 0)
		mpr_build_mat_tex_index(model, inst, mpr, frame, mat_tex_index);

	if (inst->local_transform_needs_update)
		RE_instance_update_local_transform(inst);

	glUniformMatrix4fv(r->local_transform, 1, GL_FALSE, inst->local_transform[0]);
	glUniformMatrix3fv(r->normal_transform, 1, GL_FALSE, inst->normal_transform[0]);

	bool draw_shadow = inst->draw_shadow && inst->plugin->shadow_mode;
	if (draw_shadow) {
		glActiveTexture(GL_TEXTURE0 + SHADOW_TEXTURE_UNIT);
		glBindTexture(GL_TEXTURE_2D, r->shadow.texture);
		glUniform1i(r->shadow_texture, SHADOW_TEXTURE_UNIT);
	}

	for (int i = 0; i < model->nr_meshes; i++) {
		struct mesh *mesh = &model->meshes[i];
		if (mesh->hidden || RE_instance_is_mesh_hidden(inst, i))
			continue;
		if (mesh->material < 0 || mesh->material >= model->nr_materials)
			continue;
		struct material *material = &model->materials[mesh->material];

		const struct mpr_track_set *mt = (mpr && i < mpr->nr_meshes) ? mpr->mesh_tracks[i] : NULL;
		struct mpr_modulation mesh_mod;
		mpr_evaluate_mesh(mt, frame, &obj_mod, &mesh_mod);
		if (mesh_mod.alpha <= 0.0f)
			continue;

		bool is_transparent = mesh->is_transparent || mesh_mod.alpha < 1.0f;
		if (phase != (is_transparent ? DRAW_TRANSPARENT : DRAW_OPAQUE))
			continue;

		glUniform1f(r->alpha_mod, mesh_mod.alpha);
		glUniform3fv(r->instance_ambient, 1, mesh_mod.ambient);
		glUniform3fv(r->diffuse_mod, 1, mesh_mod.diffuse);

		if (mesh->flags & MESH_BLEND_ADDITIVE) {
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
		} else {
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
		}

		bool nolighting = is_nolighting(inst->plugin, mesh);
		int fog_type = inst->plugin->fog_type;
		if (!inst->plugin->fog_mode || (inst->plugin->version == RE_REIGN_PLUGIN && nolighting)) {
			fog_type = RE_FOG_NONE;
		}
		glUniform1i(r->fog_type, fog_type);
		glUniform1i(r->nolighting, nolighting);

		GLboolean use_specular_map = GL_FALSE;
		float shininess = (mesh->flags & MESH_HAS_SPECULAR_POWER) ? mesh->specular_power : material->specular_shininess;
		if (inst->plugin->specular_mode && !nolighting && shininess > 0.0f) {
			if (mesh->flags & MESH_HAS_SPECULAR_COLOR) {
				glUniform3fv(r->specular_color, 1, mesh->specular_color);
			} else {
				glUniform3f(r->specular_color, material->specular_strength, material->specular_strength, material->specular_strength);
			}
			glUniform1f(r->specular_shininess, shininess);
			if (material->specular_map) {
				glActiveTexture(GL_TEXTURE0 + SPECULAR_TEXTURE_UNIT);
				glBindTexture(GL_TEXTURE_2D, material->specular_map);
				glUniform1i(r->specular_texture, SPECULAR_TEXTURE_UNIT);
				use_specular_map = GL_TRUE;
			}
		} else {
			glUniform3f(r->specular_color, 0.0f, 0.0f, 0.0f);
			glUniform1f(r->specular_shininess, 0.0f);
		}
		glUniform1i(r->use_specular_map, use_specular_map);

		glUniform1f(r->rim_exponent, material->rim_exponent);
		glUniform3fv(r->rim_color, 1, material->rim_color);

		glActiveTexture(GL_TEXTURE0 + COLOR_TEXTURE_UNIT);
		int tex_index = mat_tex_index[mesh->material];
		int animation_index = (tex_index >= 0 && tex_index < material->nr_color_maps) ? tex_index : 0;
		GLuint color_tex = (material->color_maps && material->nr_color_maps > 0) ? material->color_maps[animation_index] : 0;
		glBindTexture(GL_TEXTURE_2D, color_tex);
		glUniform1i(r->texture, COLOR_TEXTURE_UNIT);

		if (draw_shadow && should_draw_shadow(mesh, material))
			glUniform1f(r->shadow_darkness, material->shadow_darkness);
		else
			glUniform1f(r->shadow_darkness, 0.0f);

		if (mesh->flags & MESH_ENVMAP) {
			glUniform1i(r->diffuse_type, DIFFUSE_ENV_MAP);
		} else if (nolighting) {
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
			glUniform1i(r->alpha_mode, is_transparent ? ALPHA_MAP_BLEND : ALPHA_MAP_TEST);
			glActiveTexture(GL_TEXTURE0 + ALPHA_TEXTURE_UNIT);
			glBindTexture(GL_TEXTURE_2D, material->alpha_map);
			glUniform1i(r->alpha_texture, ALPHA_TEXTURE_UNIT);
		} else {
			glUniform1i(r->alpha_mode, is_transparent ? ALPHA_BLEND : ALPHA_TEST);
		}

		if (material->blend_texture) {
			glUniform1i(r->use_blend_texture, GL_TRUE);
			glActiveTexture(GL_TEXTURE0 + BLEND_TEXTURE_UNIT);
			glBindTexture(GL_TEXTURE_2D, material->blend_texture);
			glUniform1i(r->blend_tex, BLEND_TEXTURE_UNIT);
		} else {
			glUniform1i(r->use_blend_texture, GL_FALSE);
		}

		vec2 uv_scroll;
		glm_vec2_scale(mesh->uv_scroll, r->last_frame_timestamp / 1000.f, uv_scroll);
		glUniform2fv(r->uv_scroll, 1, uv_scroll);
		glUniform4f(r->uv_tiling,
			material->uv_tiling[0], material->uv_tiling[1],
			material->blend_uv_tiling[0], material->blend_uv_tiling[1]);

		glBindVertexArray(mesh->vao);

		if (mesh->flags & MESH_BOTH)
			glDisable(GL_CULL_FACE);
		if (mesh->flags & MESH_NO_ZWRITE)
			glDepthMask(GL_FALSE);

		if (mesh->nr_indices)
			glDrawElements(GL_TRIANGLES, mesh->nr_indices, GL_UNSIGNED_SHORT, NULL);
		else
			glDrawArrays(GL_TRIANGLES, 0, mesh->nr_vertices);

		if (mesh->flags & MESH_NO_ZWRITE)
			glDepthMask(GL_TRUE);
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
		bind_bone_texture(inst->bone_transforms_texture);
	} else {
		glUniform1i(r->has_bones, GL_FALSE);
	}
	render_model(inst, r, phase);

	if (inst->shadow_volume_instance && phase == DRAW_TRANSPARENT)
		render_static_model(inst->shadow_volume_instance, r, phase);
}

static void reset_draw_uniforms(struct RE_renderer *r, struct RE_plugin *plugin)
{
	bool nolighting = lighting_disabled(plugin);
	glUniform1f(r->alpha_mod, 1.0f);
	glUniform3f(r->diffuse_mod, 1.0f, 1.0f, 1.0f);
	glUniform2f(r->uv_scroll, 0.0f, 0.0f);
	glUniform4f(r->uv_tiling, 1.0f, 1.0f, 1.0f, 1.0f);
	glUniform1i(r->has_bones, GL_FALSE);
	glUniform3f(r->specular_color, 0.0f, 0.0f, 0.0f);
	glUniform1f(r->specular_shininess, 0.0f);
	glUniform1i(r->use_specular_map, GL_FALSE);
	glUniform1f(r->rim_exponent, 0.0f);
	glUniform3f(r->rim_color, 0.0f, 0.0f, 0.0f);
	glUniform1i(r->use_normal_map, GL_FALSE);
	glUniform1i(r->use_blend_texture, GL_FALSE);
	glUniform1f(r->shadow_darkness, 0.0f);
	glUniform1i(r->alpha_mode, ALPHA_BLEND);
	glUniform1i(r->fog_type, 0);
	glUniform1i(r->nolighting, nolighting);
	glUniform1i(r->diffuse_type, nolighting ? DIFFUSE_EMISSIVE : DIFFUSE_NORMAL);
}

static void render_billboard(struct RE_instance *inst, struct RE_renderer *r, mat4 view_mat, enum draw_phase phase)
{
	if (!inst->draw || !inst->motion)
		return;
	int frame = inst->motion->current_frame;
	struct billboard_texture *bt;
	if (inst->nr_billboard_frames > 0) {
		if (frame < 0 || frame >= inst->nr_billboard_frames)
			return;
		bt = inst->billboard_frames[frame];
	} else {
		bt = ht_get_int(r->billboard_textures_by_no, frame, NULL);
	}
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
	mat3 normal_transform;
	// This should be safe because billboards do not have non-uniform scaling.
	glm_mat4_pick3(local_transform, normal_transform);

	glUniform3fv(r->instance_ambient, 1, inst->ambient);
	glUniformMatrix4fv(r->local_transform, 1, GL_FALSE, local_transform[0]);
	glUniformMatrix3fv(r->normal_transform, 1, GL_FALSE, normal_transform[0]);

	reset_draw_uniforms(r, inst->plugin);
	glUniform1f(r->alpha_mod, inst->alpha);
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
	GLfloat vertices[4][5];
	for (int i = 0; i < 4; i++) {
		glm_vec3_copy(inst->vertex_pos[i], vertices[i]);
		glm_vec2_copy(inst->vertex_uv[i], vertices[i] + 3);
	}
	glBindVertexArray(r->billboard_instance_vao);
	glBindBuffer(GL_ARRAY_BUFFER, r->billboard_instance_attr_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glDepthFunc(GL_LESS);
}

static void render_billboard_particles(struct RE_renderer *r, struct RE_instance *inst, struct particle_object *po, float frame)
{
	struct pae_object *pae_obj = po->pae_obj;
	if (pae_obj->nr_textures <= 0)
		return;

	glUniform1i(r->diffuse_type, DIFFUSE_EMISSIVE);
	glUniform1i(r->fog_type, 0);

	glActiveTexture(GL_TEXTURE0);
	glUniform1i(r->texture, 0);
	glBindVertexArray(r->billboard_particle_vao);

	for (int index = 0; index < pae_obj->nr_particles; index++) {
		struct particle_instance *pi = &po->instances[index];
		if (frame < pi->begin_frame || pi->end_frame <= frame)
			continue;

		int step = pae_obj->texture_anime_frame > 0.0f
			? (int)((frame - pi->begin_frame) / pae_obj->texture_anime_frame)
			: 0;
		if (step < 0)
			step = 0;
		struct billboard_texture *bt = ht_get(inst->effect->pae->textures, pae_obj->textures[step % pae_obj->nr_textures], NULL);
		if (!bt)
			continue;
		glBindTexture(GL_TEXTURE_2D, bt->texture);

		float alpha = pae_object_calc_alpha(pae_obj, pi, frame);
		if (pae_obj->blend_type == PARTICLE_BLEND_ADDITIVE && !bt->has_alpha)
			alpha *= alpha;  // why...
		glUniform1f(r->alpha_mod, alpha);

		mat4 local_transform;
		mat3 normal_transform;
		particle_object_calc_local_transform(inst, po, pi, frame, local_transform);
		glm_mat4_pick3(local_transform, normal_transform);
		glUniformMatrix4fv(r->local_transform, 1, GL_FALSE, local_transform[0]);
		glUniformMatrix3fv(r->normal_transform, 1, GL_FALSE, normal_transform[0]);

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

	glUniform1i(r->diffuse_type,
		lighting_disabled(inst->plugin) ? DIFFUSE_EMISSIVE : DIFFUSE_NORMAL);
	glUniform1i(r->fog_type, inst->plugin->fog_mode ? inst->plugin->fog_type : 0);

	for (int index = 0; index < pae_obj->nr_particles; index++) {
		struct particle_instance *pi = &po->instances[index];
		if (frame < pi->begin_frame || pi->end_frame <= frame)
			continue;

		float alpha = pae_object_calc_alpha(pae_obj, pi, frame);
		glUniform1f(r->alpha_mod, alpha);

		mat4 local_transform;
		mat3 normal_transform;
		particle_object_calc_local_transform(inst, po, pi, frame, local_transform);
		glm_mat4_pick3(local_transform, normal_transform);
		glUniformMatrix4fv(r->local_transform, 1, GL_FALSE, local_transform[0]);
		glUniformMatrix3fv(r->normal_transform, 1, GL_FALSE, normal_transform[0]);

		for (int i = 0; i < model->nr_meshes; i++) {
			struct mesh *mesh = &model->meshes[i];
			if (mesh->hidden || mesh->material < 0 || mesh->material >= model->nr_materials)
				continue;
			struct material *material = &model->materials[mesh->material];
			glUniform4f(r->uv_tiling,
				material->uv_tiling[0], material->uv_tiling[1],
				material->blend_uv_tiling[0], material->blend_uv_tiling[1]);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, (material->color_maps && material->nr_color_maps > 0) ? material->color_maps[0] : 0);
			glUniform1i(r->texture, 0);

			glBindVertexArray(mesh->vao);
			glDrawArrays(GL_TRIANGLES, 0, mesh->nr_vertices);

			glBindVertexArray(0);
			glBindTexture(GL_TEXTURE_2D, 0);
		}
	}
}

static void render_s3de_billboard_particles(struct RE_renderer *r, struct RE_instance *inst,
					    struct s3de_object *obj, struct s3de_object_state *st,
					    mat3 camera_rot, float frame)
{
	if (!obj->texture || obj->particle_count == 0)
		return;

	struct billboard_texture *bt = ht_get(
		inst->s3de_effect->s3de->textures, obj->texture, NULL);
	if (!bt)
		return;

	glUniform1i(r->diffuse_type, DIFFUSE_EMISSIVE);
	switch (obj->blend_type) {
	case S3DE_BLEND_NORMAL:
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
		break;
	case S3DE_BLEND_ADDITIVE:
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
		break;
	}
	glActiveTexture(GL_TEXTURE0);
	glUniform1i(r->texture, 0);
	glBindVertexArray(r->billboard_particle_vao);
	glDisable(GL_CULL_FACE);
	glBindTexture(GL_TEXTURE_2D, bt->texture);

	for (int i = 0; i < obj->particle_count; i++) {
		struct s3de_particle *p = &st->particles[i];
		float alpha;
		if (!s3de_particle_alpha(st, p, frame, &alpha))
			continue;

		mat4 world;
		if (!s3de_billboard_world_transform(inst, obj, st, p, frame, camera_rot, world))
			continue;

		mat3 normal_transform;
		glm_mat4_pick3(world, normal_transform);
		glUniform1f(r->alpha_mod, alpha);
		glUniformMatrix4fv(r->local_transform, 1, GL_FALSE, world[0]);
		glUniformMatrix3fv(r->normal_transform, 1, GL_FALSE, normal_transform[0]);

		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}

	glEnable(GL_CULL_FACE);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

static void render_s3de_polygon_particles(struct RE_renderer *r, struct RE_instance *inst,
					  struct s3de_object *obj, struct s3de_object_state *st,
					  mat3 camera_rot, vec3 camera_pos, float frame,
					  enum draw_phase phase)
{
	struct model *model = obj->model;
	if (!model || obj->particle_count == 0)
		return;

	if (phase == DRAW_OPAQUE && st->emitter_alpha < 1.0f)
		return;

	glUniform1i(r->diffuse_type,
		lighting_disabled(inst->plugin) ? DIFFUSE_EMISSIVE : DIFFUSE_NORMAL);
	// The .3de blend_type is ignored for polygon objects. Per-mesh additive
	// blend modes are not yet handled.
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);

	for (int i = 0; i < obj->particle_count; i++) {
		struct s3de_particle *p = &st->particles[i];
		float alpha;
		if (!s3de_particle_alpha(st, p, frame, &alpha))
			continue;
		if (phase == DRAW_OPAQUE && alpha < 1.0f)
			continue;

		mat4 world;
		if (!s3de_mesh_world_transform(inst, obj, st, p, frame, camera_rot, camera_pos, world))
			continue;

		mat3 normal_transform;
		glm_mat4_pick3(world, normal_transform);
		glUniform1f(r->alpha_mod, alpha);
		glUniformMatrix4fv(r->local_transform, 1, GL_FALSE, world[0]);
		glUniformMatrix3fv(r->normal_transform, 1, GL_FALSE, normal_transform[0]);

		for (int j = 0; j < model->nr_meshes; j++) {
			struct mesh *mesh = &model->meshes[j];
			if (mesh->hidden || mesh->material < 0 || mesh->material >= model->nr_materials)
				continue;
			bool is_transparent = mesh->is_transparent || alpha < 1.0f;
			if (phase != (is_transparent ? DRAW_TRANSPARENT : DRAW_OPAQUE))
				continue;
			struct material *material = &model->materials[mesh->material];
			glUniform4f(r->uv_tiling,
				material->uv_tiling[0], material->uv_tiling[1],
				material->blend_uv_tiling[0], material->blend_uv_tiling[1]);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, (material->color_maps && material->nr_color_maps > 0) ? material->color_maps[0] : 0);
			glUniform1i(r->texture, 0);
			glBindVertexArray(mesh->vao);

			if (mesh->flags & MESH_BOTH)
				glDisable(GL_CULL_FACE);
			glDrawArrays(GL_TRIANGLES, 0, mesh->nr_vertices);
			if (mesh->flags & MESH_BOTH)
				glEnable(GL_CULL_FACE);

			glBindVertexArray(0);
			glBindTexture(GL_TEXTURE_2D, 0);
		}
	}
}

// Sort entry for per-object ordering.
struct s3de_obj_sort_key {
	float key;
	int idx;
};

static int cmp_s3de_obj(const void *lhs, const void *rhs)
{
	const struct s3de_obj_sort_key *l = lhs;
	const struct s3de_obj_sort_key *r = rhs;
	if (l->key != r->key)
		return (l->key > r->key) - (l->key < r->key);
	// Fall back to declaration order to keep the sort stable.
	return (l->idx > r->idx) - (l->idx < r->idx);
}

static void render_s3de_effect(struct RE_instance *inst, struct RE_renderer *r, enum draw_phase phase)
{
	if (!inst->s3de_effect || !inst->draw || !inst->motion)
		return;

	struct s3de *s = inst->s3de_effect->s3de;
	if (!s || s->nr_objects <= 0)
		return;

	if (inst->local_transform_needs_update)
		RE_instance_update_local_transform(inst);

	glUniform3fv(r->instance_ambient, 1, inst->ambient);
	reset_draw_uniforms(r, inst->plugin);

	if (phase == DRAW_TRANSPARENT)
		glDepthMask(GL_FALSE);

	mat4 view_mat;
	RE_calc_view_matrix(&inst->plugin->camera, NULL, view_mat);
	mat3 camera_rot;
	glm_mat4_pick3t(view_mat, camera_rot);

	// Sort objects by their emitter's world-space distance to the camera.
	// The transparent phase draws back-to-front, the opaque phase draws
	// front-to-back. Particles inside one object are not sorted; they stay
	// in spawn order.
	float *camera_pos = inst->plugin->camera.override_active
		? inst->plugin->camera.override_pos : inst->plugin->camera.pos;
	float key_sign = phase == DRAW_OPAQUE ? 1.0f : -1.0f;
	struct s3de_obj_sort_key *keys = xmalloc(s->nr_objects * sizeof(*keys));
	for (int i = 0; i < s->nr_objects; i++) {
		keys[i].idx = i;
		vec3 emitter_world;
		glm_mat4_mulv3(inst->local_transform,
			inst->s3de_effect->objects[i].emitter_pos, 1.0f,
			emitter_world);
		vec3 d;
		glm_vec3_sub(emitter_world, camera_pos, d);
		keys[i].key = key_sign * glm_vec3_norm2(d);
	}
	qsort(keys, s->nr_objects, sizeof(*keys), cmp_s3de_obj);

	float frame = inst->motion->current_frame;

	for (int k = 0; k < s->nr_objects; k++) {
		int i = keys[k].idx;
		struct s3de_object *obj = &s->objects[i];
		struct s3de_object_state *st = &inst->s3de_effect->objects[i];

		vec3 ambient;
		glm_vec3_add(inst->ambient, st->additive_color, ambient);
		glUniform3fv(r->instance_ambient, 1, ambient);
		glUniform3fv(r->diffuse_mod, 1, st->multiply_color);

		switch (obj->type) {
		case S3DE_OBJ_BILLBOARD:
			// Billboards are transparent-phase only.
			if (phase == DRAW_TRANSPARENT)
				render_s3de_billboard_particles(r, inst, obj, st, camera_rot, frame);
			break;
		case S3DE_OBJ_POLYGON:
			render_s3de_polygon_particles(r, inst, obj, st, camera_rot, camera_pos, frame, phase);
			break;
		case S3DE_OBJ_CAMERA:
			break;
		}
	}
	free(keys);

	if (phase == DRAW_TRANSPARENT)
		glDepthMask(GL_TRUE);
}

static void render_particle_effect(struct RE_instance *inst, struct RE_renderer *r, enum draw_phase phase)
{
	// NOTE: inst->draw flag has no effect for particle effects.
	if (!inst->effect || !inst->motion || phase == DRAW_OPAQUE)
		return;

	glUniform3fv(r->instance_ambient, 1, inst->ambient);
	reset_draw_uniforms(r, inst->plugin);

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
	glUniform1f(r->grayscale_rate, inst->grayscale_rate);

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
		if (inst->s3de_effect)
			render_s3de_effect(inst, r, phase);
		else
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
		vec4 sphere;
		if (inst->bounding_sphere[3] > 0.0f && isfinite(inst->bounding_sphere[3])) {
			glm_vec4_copy(inst->bounding_sphere, sphere);
		} else {
			vec3 aabb[2];
			memcpy(aabb, inst->model->aabb, sizeof(aabb));
			if (aabb[0][0] > aabb[1][0] || aabb[0][1] > aabb[1][1] || aabb[0][2] > aabb[1][2])
				continue;
			if (inst->local_transform_needs_update)
				RE_instance_update_local_transform(inst);
			glm_aabb_transform(aabb, inst->local_transform, aabb);
			vec3 center;
			glm_aabb_center(aabb, center);
			float r = glm_aabb_radius(aabb);
			if (r <= 0.0f || !isfinite(r))
				continue;
			glm_vec3_copy(center, sphere);
			sphere[3] = r;
		}
		if (bounding_sphere[3] > 0.0f)
			merge_spheres(sphere, bounding_sphere, bounding_sphere);
		else
			glm_vec4_copy(sphere, bounding_sphere);
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
	GLint orig_draw_fbo = 0, orig_read_fbo = 0, orig_viewport[4];
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &orig_draw_fbo);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &orig_read_fbo);
	glGetIntegerv(GL_VIEWPORT, orig_viewport);

	glBindFramebuffer(GL_FRAMEBUFFER, r->shadow.fbo);
	glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
	glDepthMask(GL_TRUE);
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
		if (inst->type == RE_ITYPE_SKINNED && model->nr_bones > 0 && inst->motion) {
			glUniform1i(r->shadow.has_bones, GL_TRUE);
			bind_bone_texture(inst->bone_transforms_texture);
		} else {
			glUniform1i(r->shadow.has_bones, GL_FALSE);
		}
		glUniformMatrix4fv(r->shadow.local_transform, 1, GL_FALSE, inst->local_transform[0]);
		for (int j = 0; j < model->nr_meshes; j++) {
			struct mesh *mesh = &model->meshes[j];
			if ((mesh->flags & MESH_NOMAKESHADOW) || mesh->hidden || RE_instance_is_mesh_hidden(inst, j))
				continue;
			glBindVertexArray(mesh->vao);
			if (mesh->nr_indices > 0)
				glDrawElements(GL_TRIANGLES, mesh->nr_indices, GL_UNSIGNED_SHORT, NULL);
			else
				glDrawArrays(GL_TRIANGLES, 0, mesh->nr_vertices);
		}
		glBindVertexArray(0);
	}

	glDisable(GL_DEPTH_TEST);
	glUseProgram(0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)orig_draw_fbo);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)orig_read_fbo);
	glViewport(orig_viewport[0], orig_viewport[1], orig_viewport[2], orig_viewport[3]);
}

static void render_outlines(struct RE_plugin *plugin, mat4 view_transform)
{
	if (plugin->draw_options[RE_DRAW_OPTION_EDGE] == 0)
		return;
	struct outline_renderer *or = &plugin->renderer->outline;
	if (!or->program)
		return;
	glUseProgram(or->program);
	glUniformMatrix4fv(or->view_transform, 1, GL_FALSE, view_transform[0]);
	glUniformMatrix4fv(or->proj_transform, 1, GL_FALSE, plugin->proj_transform[0]);

	// SealEngine ignores the per-mesh edge parameters from .opr and uses
	// a single plugin-wide edge length and color.
	bool seal_edge = plugin->version >= RE_SEAL_PLUGIN;
	if (seal_edge) {
		glUniform3fv(or->outline_color, 1, plugin->edge_color);
		float thickness = plugin->edge_length * (1.f - plugin->edge_reduction_rate);
		glUniform1f(or->outline_thickness, thickness);
	}

	glCullFace(GL_FRONT);
	for (int i = 0; i < plugin->nr_instances; i++) {
		struct RE_instance *inst = plugin->instances[i];
		if (!inst || !inst->draw || !inst->draw_edge || !inst->model)
			continue;
		struct model *model = inst->model;
		if (inst->type == RE_ITYPE_SKINNED && model->nr_bones > 0 && inst->motion) {
			glUniform1i(or->has_bones, GL_TRUE);
			bind_bone_texture(inst->bone_transforms_texture);
		} else {
			glUniform1i(or->has_bones, GL_FALSE);
		}
		glUniformMatrix4fv(or->local_transform, 1, GL_FALSE, inst->local_transform[0]);
		glUniformMatrix3fv(or->normal_transform, 1, GL_FALSE, inst->normal_transform[0]);
		for (int j = 0; j < model->nr_meshes; j++) {
			struct mesh *mesh = &model->meshes[j];
			if ((mesh->flags & MESH_NO_EDGE) || mesh->hidden || RE_instance_is_mesh_hidden(inst, j))
				continue;
			if (!seal_edge) {
				glUniform3fv(or->outline_color, 1, mesh->outline_color);
				glUniform1f(or->outline_thickness, mesh->outline_thickness);
			}
			glBindVertexArray(mesh->vao);
			if (mesh->nr_indices > 0)
				glDrawElements(GL_TRIANGLES, mesh->nr_indices, GL_UNSIGNED_SHORT, NULL);
			else
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

static void setup_light_scattering(struct RE_plugin *plugin)
{
	struct RE_renderer *r = plugin->renderer;
	if (plugin->version >= RE_SEAL_PLUGIN) {
		const float *lp = plugin->light_params;
		glUniform4f(r->ls_params, lp[SEAL_LP_LS_BETA_R], lp[SEAL_LP_LS_BETA_M],
			lp[SEAL_LP_LS_G], lp[SEAL_LP_LS_DISTANCE]);
		glUniform3f(r->ls_light_dir, lp[SEAL_LP_LS_LIGHT_VEC],
			lp[SEAL_LP_LS_LIGHT_VEC + 1], -lp[SEAL_LP_LS_LIGHT_VEC + 2]);
		glUniform3fv(r->ls_light_color, 1, &lp[SEAL_LP_LS_LIGHT_COLOR]);
		glUniform3fv(r->ls_sun_color, 1, &lp[SEAL_LP_LS_SUN_COLOR]);
	} else {
		glUniform4f(r->ls_params, plugin->ls_beta_r, plugin->ls_beta_m, plugin->ls_g, plugin->ls_distance);
		glUniform3fv(r->ls_light_dir, 1, plugin->ls_light_dir);
		glUniform3fv(r->ls_light_color, 1, plugin->ls_light_color);
		glUniform3fv(r->ls_sun_color, 1, plugin->ls_sun_color);
	}
}

static void setup_hemisphere_light(struct RE_plugin *plugin)
{
	struct RE_renderer *r = plugin->renderer;
	const float *lp = plugin->light_params;
	vec3 hemi_dir = {
		-lp[SEAL_LP_HEMI_VEC], -lp[SEAL_LP_HEMI_VEC + 1], lp[SEAL_LP_HEMI_VEC + 2]
	};
	if (glm_vec3_norm2(hemi_dir) > 1e-12f)
		glm_vec3_normalize(hemi_dir);
	else
		glm_vec3_copy((vec3){ 0.0f, 1.0f, 0.0f }, hemi_dir);
	glUniform3fv(r->hemi_light_dir, 1, hemi_dir);
	glUniform3fv(r->hemi_sky_color, 1, &lp[SEAL_LP_HEMI_SKY_COLOR]);
	glUniform3fv(r->hemi_mid_color, 1, &lp[SEAL_LP_HEMI_MID_COLOR]);
	glUniform3fv(r->hemi_ground_color, 1, &lp[SEAL_LP_HEMI_GROUND_COLOR]);
	// SealEngine's specular uses the hemisphere light vector. hemi_dir points
	// toward the light, but the shader's specular formula expects the light
	// propagation direction.
	vec3 spec_dir;
	glm_vec3_negate_to(hemi_dir, spec_dir);
	glUniform3fv(r->specular_light_dir, 1, spec_dir);
}

static void setup_tone_mapping(struct RE_plugin *plugin)
{
	struct RE_renderer *r = plugin->renderer;
	const float *lp = plugin->light_params;
	glUniform4fv(r->tonemap_param, 1, &lp[SEAL_LP_TONEMAP_EXPOSURE_BIAS]);
	glUniform4fv(r->tonemap_param2, 1, &lp[SEAL_LP_TONEMAP_C]);
}

static void setup_lights(struct RE_plugin *plugin)
{
	struct RE_renderer *r = plugin->renderer;
	glUniform3fv(r->camera_pos, 1, plugin->camera.override_active
			? plugin->camera.override_pos : plugin->camera.pos);
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

	if (plugin->version >= RE_SEAL_PLUGIN)
		setup_hemisphere_light(plugin);
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

void RE_update_model(struct RE_plugin *plugin)
{
	struct RE_renderer *r = plugin->renderer;
	if (!r || plugin->suspended)
		return;
	uint32_t timestamp = SDL_GetTicks();
	RE_build_model(plugin, timestamp - r->last_frame_timestamp);
	r->last_frame_timestamp = timestamp;
}

static void reset_default_vertex_attribs(void)
{
	glVertexAttrib3f(VATTR_NORMAL, 0.0f, 0.0f, 1.0f);
	glVertexAttrib2f(VATTR_UV, 0.0f, 0.0f);
	glVertexAttribI4i(VATTR_BONE_INDEX, -1, -1, -1, -1);
	glVertexAttrib4f(VATTR_BONE_WEIGHT, 0.0f, 0.0f, 0.0f, 0.0f);
	glVertexAttrib2f(VATTR_LIGHT_UV, 0.0f, 0.0f);
	glVertexAttrib4f(VATTR_COLOR, 1.0f, 1.0f, 1.0f, 1.0f);
	glVertexAttrib4f(VATTR_TANGENT, 1.0f, 0.0f, 0.0f, 1.0f);
	glVertexAttrib1f(VATTR_BLEND_WEIGHT, 0.0f);
	glVertexAttrib2f(VATTR_BLEND_UV, 0.0f, 0.0f);
}

void RE_render(struct sact_sprite *sp)
{
	struct RE_plugin *plugin = (struct RE_plugin *)sp->plugin;
	struct RE_renderer *r = plugin->renderer;
	if (!r || plugin->suspended)
		return;

	reset_default_vertex_attribs();

	for (int i = 0; i < plugin->nr_instances; i++) {
		struct RE_instance *inst = plugin->instances[i];
		if (!inst)
			continue;
		if (inst->local_transform_needs_update)
			RE_instance_update_local_transform(inst);
		if (inst->type == RE_ITYPE_SKINNED && inst->motion && inst->bones_dirty)
			RE_instance_sync_bones(inst);
	}

	sprite_dirty(sp);
	struct texture *texture = sprite_get_texture(sp);
	if (!texture || texture->w <= 0 || texture->h <= 0 || !texture->handle || !r->depth_buffer) {
		WARNING("Skipping 3D render with incomplete target texture/depth buffer");
		return;
	}

	mat4 shadow_transform;
	if (plugin->shadow_mode)
		render_shadow_map(plugin, shadow_transform);
	else
		glm_mat4_identity(shadow_transform);

	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, texture, 0, 0, texture->w, texture->h);
	glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, r->depth_buffer);
	GLenum fbo_status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
	if (fbo_status != GL_FRAMEBUFFER_COMPLETE) {
		WARNING("Incomplete 3D framebuffer: 0x%x", fbo_status);
		glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
		gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
		return;
	}

	glClearColor(0.f, 0.f, 0.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Draw background CGs.
	glDisable(GL_DEPTH_TEST);
	for (int i = 0; i < RE_NR_BACK_CGS; i++)
		render_back_cg(texture, &plugin->back_cg[i], r);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	glViewport(0, 0, texture->w, texture->h);
	reset_default_vertex_attribs();

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	mat4 view_transform;
	RE_calc_view_matrix(&plugin->camera, NULL, view_transform);

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
			setup_light_scattering(plugin);
			break;
		}
	}

	glEnable(GL_BLEND);

	setup_lights(plugin);
	if (plugin->version >= RE_SEAL_PLUGIN)
		setup_tone_mapping(plugin);

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
	glActiveTexture(GL_TEXTURE0);
}

struct height_detector {
	GLushort *buf;
	vec3 aabb[2];
};

struct height_detector *RE_renderer_create_height_detector(struct RE_renderer *r, struct model *model)
{
	if (!r || !model)
		return NULL;
	vec3 aabb[2];
	memcpy(aabb, model->aabb, sizeof(aabb));
	if (aabb[0][0] > aabb[1][0] || aabb[0][1] > aabb[1][1] || aabb[0][2] > aabb[1][2])
		return NULL;

	struct height_detector *hd = xcalloc(1, sizeof(struct height_detector));
	// Add some Y padding.
	aabb[0][1] -= 1.0f;
	aabb[1][1] += 1.0f;
	memcpy(hd->aabb, aabb, sizeof(aabb));

	// Set up an orthographic projection matrix looking down the instance from
	// right above.
	mat4 view_matrix, proj_matrix;
	glm_mat4_identity(view_matrix);
	glm_rotate_x(view_matrix, glm_rad(90.0f), view_matrix);
	glm_aabb_transform(aabb, view_matrix, aabb);
	glm_ortho_aabb(aabb, proj_matrix);

	GLint orig_draw_fbo = 0, orig_read_fbo = 0, orig_viewport[4];
	GLint orig_active_tex = GL_TEXTURE0, orig_program = 0, orig_depth_func = GL_LESS;
	GLboolean orig_cull = glIsEnabled(GL_CULL_FACE);
	GLboolean orig_scissor = glIsEnabled(GL_SCISSOR_TEST);
	GLboolean orig_blend = glIsEnabled(GL_BLEND);
	GLboolean orig_depth_test = glIsEnabled(GL_DEPTH_TEST);
	GLboolean orig_depth_mask = GL_TRUE;
	GLboolean orig_color_mask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &orig_draw_fbo);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &orig_read_fbo);
	glGetIntegerv(GL_VIEWPORT, orig_viewport);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &orig_active_tex);
	glGetIntegerv(GL_CURRENT_PROGRAM, &orig_program);
	glGetIntegerv(GL_DEPTH_FUNC, &orig_depth_func);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &orig_depth_mask);
	glGetBooleanv(GL_COLOR_WRITEMASK, orig_color_mask);

	// Since OpenGL ES does not allow reading from depth textures, create an
	// unsigned integer color texture and render quantized depth values into it.
	GLuint color_texture = 0;
	glGenTextures(1, &color_texture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, color_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glBindTexture(GL_TEXTURE_2D, 0);

	glBindFramebuffer(GL_FRAMEBUFFER, r->shadow.fbo);
	GLenum draw_buffers[1] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers(1, draw_buffers);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_texture, 0);

	GLenum fbo_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (fbo_status != GL_FRAMEBUFFER_COMPLETE) {
		WARNING("Incomplete height detector framebuffer: 0x%x", fbo_status);
		free(hd);
		hd = NULL;
		goto restore;
	}

	glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	GLuint zero[4] = { 0, 0, 0, 0 };
	glClearBufferuiv(GL_COLOR, 0, zero);
	glClear(GL_DEPTH_BUFFER_BIT);
	glUseProgram(r->shadow.program);
	glUniformMatrix4fv(r->shadow.local_transform, 1, GL_FALSE, view_matrix[0]);
	glUniformMatrix4fv(r->shadow.view_transform, 1, GL_FALSE, proj_matrix[0]);
	glUniform1i(r->shadow.has_bones, GL_FALSE);

	// Render the model.
	for (int i = 0; i < model->nr_meshes; i++) {
		struct mesh *mesh = &model->meshes[i];
		if ((mesh->flags & MESH_NO_HEIGHT_DETECTION) || mesh->hidden)
			continue;
		glBindVertexArray(mesh->vao);
		if (mesh->nr_indices > 0)
			glDrawElements(GL_TRIANGLES, mesh->nr_indices, GL_UNSIGNED_SHORT, NULL);
		else
			glDrawArrays(GL_TRIANGLES, 0, mesh->nr_vertices);
	}
	glBindVertexArray(0);
	glFinish();

	// OpenGL ES 3.0.6 Table 4.5 only guarantees (GL_RGBA_INTEGER, GL_UNSIGNED_INT)
	// for unsigned-integer color buffer readback; unpack the R channel to uint16.
	size_t nr_pixels = (size_t)SHADOW_WIDTH * SHADOW_HEIGHT;
	hd->buf = xcalloc(nr_pixels, sizeof(GLushort));
	GLuint *rgba_buf = xcalloc(nr_pixels * 4, sizeof(GLuint));
	glReadPixels(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT, GL_RGBA_INTEGER, GL_UNSIGNED_INT, rgba_buf);
	for (size_t i = 0; i < nr_pixels; i++)
		hd->buf[i] = (GLushort)rgba_buf[i * 4];
	free(rgba_buf);

restore:
	if (orig_cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (orig_scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
	if (orig_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (orig_depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	glDepthMask(orig_depth_mask);
	glDepthFunc((GLenum)orig_depth_func);
	glColorMask(orig_color_mask[0], orig_color_mask[1], orig_color_mask[2], orig_color_mask[3]);
	glUseProgram((GLuint)orig_program);
	GLenum none = GL_NONE;
	glDrawBuffers(1, &none);
	glReadBuffer(GL_NONE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)orig_draw_fbo);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)orig_read_fbo);
	glViewport(orig_viewport[0], orig_viewport[1], orig_viewport[2], orig_viewport[3]);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glDeleteTextures(1, &color_texture);
	glActiveTexture((GLenum)orig_active_tex);
	return hd;
}

void RE_renderer_free_height_detector(struct height_detector *hd)
{
	if (!hd)
		return;
	free(hd->buf);
	free(hd);
}

bool RE_renderer_detect_height(struct height_detector *hd, float x, float z, float *y_out)
{
	if (!hd || !hd->buf)
		return false;
	float minx = hd->aabb[0][0];
	float maxx = hd->aabb[1][0];
	float miny = hd->aabb[0][1];
	float maxy = hd->aabb[1][1];
	float minz = hd->aabb[0][2];
	float maxz = hd->aabb[1][2];
	if (maxx <= minx || maxz <= minz)
		return false;
	if (x < minx || x > maxx || z < minz || z > maxz)
		return false;
	int px = glm_percent(minx, maxx, x) * SHADOW_WIDTH;
	int pz = glm_percent(maxz, minz, z) * SHADOW_HEIGHT;
	if (px < 0)
		px = 0;
	else if (px >= SHADOW_WIDTH)
		px = SHADOW_WIDTH - 1;
	if (pz < 0)
		pz = 0;
	else if (pz >= SHADOW_HEIGHT)
		pz = SHADOW_HEIGHT - 1;
	GLushort val = hd->buf[pz * SHADOW_WIDTH + px];
	if (!val)
		return false;
	*y_out = glm_lerp(maxy, miny, val / 65535.0f);
	return true;
}
