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

#define NR_DIR_LIGHTS 3

struct dir_light {
	vec3 dir;
	vec3 diffuse;
	vec3 globe_diffuse;
};

uniform mat4 local_transform;
uniform mat4 view_transform;
uniform mat4 proj_transform;
uniform mat3 normal_transform;

const int MAX_BONES = 211;  // see 3d_internal.h
const int NR_WEIGHTS = 4;
uniform bool has_bones;
uniform mat4 bone_matrices[MAX_BONES];

uniform bool use_normal_map;

uniform vec3 view_pos;
uniform dir_light dir_lights[NR_DIR_LIGHTS];
uniform vec3 specular_light_dir;

in vec3 vertex_pos;
in vec3 vertex_normal;
in vec2 vertex_uv;
in vec2 vertex_light_uv;
in vec4 vertex_tangent;
in ivec4 vertex_bone_index;
in vec4 vertex_bone_weight;

out vec2 tex_coord;
out vec2 light_tex_coord;
out vec3 frag_pos;
out vec3 eye;
out vec3 normal;
out vec3 light_dir[NR_DIR_LIGHTS];
out vec3 specular_dir;

void main() {
	mat4 local_bone_transform = local_transform;
	mat3 normal_bone_transform = normal_transform;
	if (has_bones) {
		mat4 bone_transform = mat4(0.f);
		for (int i = 0; i < NR_WEIGHTS; i++) {
			if (vertex_bone_index[i] >= 0) {
				bone_transform += bone_matrices[vertex_bone_index[i]] * vertex_bone_weight[i];
			}
		}
		local_bone_transform *= bone_transform;
		normal_bone_transform *= mat3(bone_transform);
	}

	// World-space normal vector.
	normal = normal_bone_transform * vertex_normal;

	mat3 TBN = mat3(1.0f);
	if (use_normal_map) {
		vec3 tangent = normal_bone_transform * vertex_tangent.xyz;
		vec3 bitangent = cross(normal, tangent) * vertex_tangent.w;
		TBN = transpose(mat3(tangent, bitangent, normal));
		// Tangent-space normal vector.
		normal = vec3(0.0, 0.0, 1.0);
	}

	vec4 world_pos = local_bone_transform * vec4(vertex_pos, 1.0);
	gl_Position = proj_transform * view_transform * world_pos;

	tex_coord = vertex_uv;
	light_tex_coord = vertex_light_uv;

	// These are in tangent-space if use_normal_map is true, in world-space
	// otherwise.
	frag_pos = TBN * vec3(world_pos);
	eye = TBN * view_pos;
	light_dir[0] = TBN * dir_lights[0].dir;
	light_dir[1] = TBN * dir_lights[1].dir;
	light_dir[2] = TBN * dir_lights[2].dir;
	specular_dir = TBN * specular_light_dir;
}
