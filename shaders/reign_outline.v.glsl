/* Copyright (C) 2024 kichikuou <KichikuouChrome@gmail.com>
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
uniform float outline_thickness;

in vec3 vertex_pos;
in vec3 vertex_normal;
in ivec4 vertex_bone_index;
in vec4 vertex_bone_weight;

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

	vec4 pos = local_bone_transform * vec4(vertex_pos, 1.0);
	vec3 normal = normalize(normal_bone_transform * vertex_normal);

	gl_Position = proj_transform * view_transform * (pos + vec4(normal, 0.0) * outline_thickness);
}
