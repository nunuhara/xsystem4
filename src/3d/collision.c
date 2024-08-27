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

#include <stdlib.h>
#include <cglm/cglm.h>

#include "system4.h"

#include "3d_internal.h"

struct collider *collider_create(struct pol *pol)
{
	struct collider *collider = xcalloc(1, sizeof(struct collider));

	int nr_triangles = 0;
	for (int i = 0; i < pol->nr_meshes; i++) {
		nr_triangles += pol->meshes[i]->nr_triangles;
	}

	collider->nr_triangles = nr_triangles;
	collider->triangles = xcalloc(nr_triangles, sizeof(struct collider_triangle));
	struct collider_triangle *t = collider->triangles;
	for (int mesh_i = 0; mesh_i < pol->nr_meshes; mesh_i++) {
		struct pol_mesh *mesh = pol->meshes[mesh_i];
		for (int tri_i = 0; tri_i < mesh->nr_triangles; tri_i++) {
			glm_aabb_invalidate(t->aabb);
			for (int i = 0; i < 3; i++) {
				glm_vec3_copy(mesh->vertices[mesh->triangles[tri_i].vert_index[i]].pos, t->vertices[i]);
				glm_vec3_minv(t->vertices[i], t->aabb[0], t->aabb[0]);
				glm_vec3_maxv(t->vertices[i], t->aabb[1], t->aabb[1]);
			}
			t++;
		}
	}

	return collider;
}

void collider_free(struct collider *collider)
{
	free(collider->triangles);
	free(collider);
}

// When a sphere of radius `radius` moves from `p0` to `p1`, return the point
// where it collides with `collider` in `out`. If no collision is detected,
// return `p1` in `out`.
bool check_collision(struct collider *collider, vec3 p0, vec3 p1, float radius, vec3 out)
{
	// XXX: Very rough collision detection; just check if p1 is inside any triangle's AABB.
	for (int i = 0; i < collider->nr_triangles; i++) {
		if (glm_aabb_point(collider->triangles[i].aabb, p1)) {
			glm_vec3_copy(p0, out);
			return true;
		}
	}
	glm_vec3_copy(p1, out);
	return true;
}
