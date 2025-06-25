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

const float PI = 3.14159265358979323846;

#if ENGINE == REIGN_ENGINE

#define NR_DIR_LIGHTS 3
#define FOG_LIGHT_SCATTERING 2

struct dir_light {
	vec3 dir;
	vec3 diffuse;
	vec3 globe_diffuse;
};

uniform dir_light dir_lights[NR_DIR_LIGHTS];
uniform vec3 specular_light_dir;
uniform int fog_type;
uniform vec4 ls_params;  // (beta_r, beta_m, g, distance)
uniform vec3 ls_light_dir;
uniform vec3 ls_light_color;
uniform vec3 ls_sun_color;
out vec3 light_dir[NR_DIR_LIGHTS];
out vec3 specular_dir;
out vec3 ls_ex;
out vec3 ls_in;

const vec2 uv_scroll = vec2(0.0);

#else // ENGINE == REIGN_ENGINE

uniform vec2 uv_scroll;

#endif // ENGINE == REIGN_ENGINE

uniform mat4 local_transform;
uniform mat4 view_transform;
uniform mat4 proj_transform;
uniform mat3 normal_transform;

const int MAX_BONES = 308;  // see 3d_internal.h
const int NR_WEIGHTS = 4;
uniform bool has_bones;
layout(std140) uniform BoneTransforms {
	mat3x4 bone_matrices[MAX_BONES];  // transposed
};

uniform bool use_normal_map;

uniform vec3 camera_pos;
uniform mat4 shadow_transform;

in vec3 vertex_pos;
in vec3 vertex_normal;
in vec2 vertex_uv;
in vec2 vertex_light_uv;
in vec4 vertex_color;
in vec4 vertex_tangent;
in ivec4 vertex_bone_index;
in vec4 vertex_bone_weight;

out vec2 tex_coord;
out vec2 light_tex_coord;
out vec4 color_mod;
out vec3 frag_pos;
out vec4 shadow_frag_pos;
out float dist;
out vec3 eye;
out vec3 normal;

void main() {
	mat4 local_bone_transform = local_transform;
	mat3 normal_bone_transform = normal_transform;
	if (has_bones) {
		mat3x4 bone_transform = mat3x4(0.0);
		for (int i = 0; i < NR_WEIGHTS; i++) {
			if (vertex_bone_index[i] >= 0) {
				bone_transform += bone_matrices[vertex_bone_index[i]] * vertex_bone_weight[i];
			}
		}
		local_bone_transform *= mat4(
			vec4(bone_transform[0].x, bone_transform[1].x, bone_transform[2].x, 0.0),
			vec4(bone_transform[0].y, bone_transform[1].y, bone_transform[2].y, 0.0),
			vec4(bone_transform[0].z, bone_transform[1].z, bone_transform[2].z, 0.0),
			vec4(bone_transform[0].w, bone_transform[1].w, bone_transform[2].w, 1.0));
		normal_bone_transform *= mat3(bone_transform);
	}

	// World-space normal vector.
	normal = normalize(normal_bone_transform * vertex_normal);

	mat3 TBN = mat3(1.0f);
	if (use_normal_map) {
		vec3 tangent = normalize(normal_bone_transform * vertex_tangent.xyz);
		vec3 bitangent = cross(normal, tangent) * vertex_tangent.w;
		TBN = transpose(mat3(tangent, bitangent, normal));
	}

	vec4 world_pos = local_bone_transform * vec4(vertex_pos, 1.0);
	vec4 view_pos = view_transform * world_pos;
	gl_Position = proj_transform * view_pos;

	tex_coord = vertex_uv + uv_scroll;
	light_tex_coord = vertex_light_uv + uv_scroll;
	color_mod = vertex_color;
	dist = -view_pos.z;
	shadow_frag_pos = shadow_transform * world_pos;

	// These are in tangent-space if use_normal_map is true, in world-space
	// otherwise.
	frag_pos = TBN * vec3(world_pos);
	eye = TBN * camera_pos;

#if ENGINE == REIGN_ENGINE
	light_dir[0] = TBN * dir_lights[0].dir;
	light_dir[1] = TBN * dir_lights[1].dir;
	light_dir[2] = TBN * dir_lights[2].dir;
	specular_dir = TBN * specular_light_dir;

	if (fog_type == FOG_LIGHT_SCATTERING) {
		float beta_r = ls_params.x;
		float beta_m = ls_params.y;
		float g = ls_params.z;
		float distance = dist / ls_params.w;

		vec3 view_dir = normalize(camera_pos - vec3(world_pos));
		float cos_theta = dot(view_dir, normalize(ls_light_dir));
		// Note: `* PI` in the two assignments below should be `/ PI` (see the
		// definitions of Rayleigh / Mie phase functions in [1]), but this is
		// how TT3's shader works.
		// [1] http://amd-dev.wpengine.netdna-cdn.com/wordpress/media/2012/10/ATI-LightScattering.pdf
		float phase_r = 3.0 / 16.0 * PI * (1.0 + cos_theta * cos_theta);
		float phase_m = 1.0 / 4.0 * PI * (1.0 - g) * (1.0 - g) / pow(1.0 + g * g - 2.0 * g * cos_theta, 1.5);
		float f_ex = exp((beta_r + beta_m) * -distance);
		ls_in = (phase_r * beta_r + phase_m * beta_m) / (beta_r + beta_m) * (1.0 - f_ex) * ls_sun_color;
		ls_ex = ls_light_color * f_ex;
	}
#endif
}
