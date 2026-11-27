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

uniform mat4 local_transform;
uniform mat4 view_transform;

const int NR_WEIGHTS = 4;
const int MAX_BONES = 308;
uniform bool has_bones;
// Bone matrices are stored in a texture rather than a uniform block, because
// some GLES implementations (e.g. SwiftShader) count uniform block members
// against GL_MAX_VERTEX_UNIFORM_VECTORS.
uniform sampler2D bone_texture;  // 3 texels (columns of a transposed mat3x4) per bone

mat3x4 bone_matrix(int i) {
	return mat3x4(
		texelFetch(bone_texture, ivec2(i * 3, 0), 0),
		texelFetch(bone_texture, ivec2(i * 3 + 1, 0), 0),
		texelFetch(bone_texture, ivec2(i * 3 + 2, 0), 0));
}

in vec3 vertex_pos;
in ivec4 vertex_bone_index;
in vec4 vertex_bone_weight;

void main() {
	mat4 local_bone_transform = local_transform;
	if (has_bones) {
		mat3x4 bone_transform = mat3x4(0.0);
		float weight_sum = 0.0;
		for (int i = 0; i < NR_WEIGHTS; i++) {
			int idx = vertex_bone_index[i];
			float w = vertex_bone_weight[i];
			if (idx >= 0 && idx < MAX_BONES && w > 0.0) {
				bone_transform += bone_matrix(idx) * w;
				weight_sum += w;
			}
		}
		if (weight_sum > 0.0) {
			local_bone_transform *= mat4(
				vec4(bone_transform[0].x, bone_transform[1].x, bone_transform[2].x, 0.0),
				vec4(bone_transform[0].y, bone_transform[1].y, bone_transform[2].y, 0.0),
				vec4(bone_transform[0].z, bone_transform[1].z, bone_transform[2].z, 0.0),
				vec4(bone_transform[0].w, bone_transform[1].w, bone_transform[2].w, 1.0));
		}
	}

	gl_Position = view_transform * local_bone_transform * vec4(vertex_pos, 1.0);
}
