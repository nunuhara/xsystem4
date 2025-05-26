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

#include <assert.h>
#include <stdlib.h>
#include <cglm/cglm.h>

#include "system4.h"

#include "3d_internal.h"

struct collider_triangle {
	vec3 vertices[3];
	vec2 aabb[2];  // xz coordinates
	vec2 slope;
	float intercept;
};

struct collider_edge {
	vec2 vertices[2];  // xz coordinates
	vec2 aabb[2];
};

static void init_triangles(struct collider *collider, struct pol_mesh *mesh)
{
	collider->nr_triangles = mesh->nr_triangles;
	collider->triangles = xcalloc(mesh->nr_triangles, sizeof(struct collider_triangle));
	struct collider_triangle *t = collider->triangles;
	for (int tri_i = 0; tri_i < mesh->nr_triangles; tri_i++, t++) {
		glm_aabb2d_invalidate(t->aabb);
		for (int i = 0; i < 3; i++) {
			glm_vec3_copy(mesh->vertices[mesh->triangles[tri_i].vert_index[i]].pos, t->vertices[i]);
			vec2 xz = { t->vertices[i][0], t->vertices[i][2] };
			glm_vec2_minv(xz, t->aabb[0], t->aabb[0]);
			glm_vec2_maxv(xz, t->aabb[1], t->aabb[1]);
		}
		// Do some precomputation so that we can calculate height(x, z) quickly.
		vec3 v1, v2, normal;
		glm_vec3_sub(t->vertices[1], t->vertices[0], v1);
		glm_vec3_sub(t->vertices[2], t->vertices[0], v2);
		glm_vec3_cross(v1, v2, normal);
		t->slope[0] = -normal[0] / normal[1];
		t->slope[1] = -normal[2] / normal[1];
		t->intercept = glm_vec3_dot(normal, t->vertices[0]) / normal[1];
	}
}

static void init_edges(struct collider *collider, struct pol_mesh *mesh)
{
	// Find boundary edges, i.e., edges that belong to only one triangle.
	const int nv = mesh->nr_vertices;
	const int bv_size = (nv * nv + 31) / 32;
	uint32_t *bv = xcalloc(bv_size, 4);
	int nr_edges = 0;
	for (int i = 0; i < mesh->nr_triangles; i++) {
		for (int j = 0; j < 3; j++) {
			int v1 = mesh->triangles[i].vert_index[j];
			int v2 = mesh->triangles[i].vert_index[(j + 1) % 3];
			int b = v2 * nv + v1;
			if (bv[b >> 5] & 1U << (b & 31)) {
				bv[b >> 5] &= ~(1U << (b & 31));
				nr_edges--;
			} else {
				b = v1 * nv + v2;
				bv[b >> 5] |= 1U << (b & 31);
				nr_edges++;
			}
		}
	}
	collider->nr_edges = nr_edges;
	collider->edges = xcalloc(nr_edges, sizeof(struct collider_edge));
	struct collider_edge* edge = collider->edges;
	for (int i = 0; i < bv_size; i++) {
		if (!bv[i]) continue;
		for (int j = 0; j < 32; j++) {
			if (!(bv[i] & 1U << j)) continue;
			int b = i << 5 | j;
			int v1 = b / nv;
			int v2 = b % nv;
			edge->vertices[0][0] = mesh->vertices[v1].pos[0];
			edge->vertices[0][1] = mesh->vertices[v1].pos[2];
			edge->vertices[1][0] = mesh->vertices[v2].pos[0];
			edge->vertices[1][1] = mesh->vertices[v2].pos[2];
			glm_vec2_minv(edge->vertices[0], edge->vertices[1], edge->aabb[0]);
			glm_vec2_maxv(edge->vertices[0], edge->vertices[1], edge->aabb[1]);
			edge++;
		}
	}
	assert(edge == collider->edges + nr_edges);
	free(bv);
}

struct collider *collider_create(struct pol_mesh *mesh)
{
	struct collider *collider = xcalloc(1, sizeof(struct collider));
	init_triangles(collider, mesh);
	init_edges(collider, mesh);
	NOTICE("collider: %d triangles %d edges", collider->nr_triangles, collider->nr_edges);
	return collider;
}

void collider_free(struct collider *collider)
{
	free(collider->triangles);
	free(collider->edges);
	free(collider);
}

static bool in_triangle(struct collider_triangle* t, vec2 xz)
{
	vec2 v[3];
	for (int i = 0; i < 3; i++) {
		v[i][0] = t->vertices[i][0];
		v[i][1] = t->vertices[i][2];
	}
	for (int i = 0; i < 3; i++) {
		vec2 a, b;
		glm_vec2_sub(v[(i + 1) % 3], v[i], a);
		glm_vec2_sub(xz, v[i], b);
		if (glm_vec2_cross(a, b) > 0.f)
			return false;
	}
	return true;
}

static struct collider_triangle* find_triangle(struct collider *collider, vec2 xz)
{
	struct collider_triangle* t = collider->triangles;
	for (; t < collider->triangles + collider->nr_triangles; t++) {
		if (!glm_aabb2d_point(t->aabb, xz))
			continue;
		if (in_triangle(t, xz))
			return t;
	}
	return NULL;
}

bool collider_height(struct collider *collider, vec2 xz, float *h_out)
{
	struct collider_triangle* t = find_triangle(collider, xz);
	if (!t)
		return false;
	*h_out = glm_vec2_dot(t->slope, xz) + t->intercept;
	return true;
}

static float distance_point_to_edge(vec2 p, struct collider_edge *e, vec2 closest_point_out)
{
	vec2 v, w;
	glm_vec2_sub(e->vertices[1], e->vertices[0], v);
	glm_vec2_sub(p, e->vertices[0], w);

	float c1 = glm_vec2_dot(w, v);
	if (c1 < 0.f) {
		glm_vec2_copy(e->vertices[0], closest_point_out);
		return glm_vec2_distance(p, e->vertices[0]);
	}
	float c2 = glm_vec2_norm2(v);
	if (c2 <= c1) {
		glm_vec2_copy(e->vertices[1], closest_point_out);
		return glm_vec2_distance(p, e->vertices[1]);
	}
	float t = c1 / c2;
	glm_vec2_copy(e->vertices[0], closest_point_out);
	glm_vec2_muladds(v, t, closest_point_out);
	return glm_vec2_distance(p, closest_point_out);
}

bool check_collision(struct collider *collider, vec2 p0, vec2 p1, float radius, vec2 out)
{
	if (find_triangle(collider, p1)) {
		glm_vec2_copy(p1, out);
	} else {
		if (!find_triangle(collider, p0))
			return false;
		// Find a point on the "collision" mesh, using binary search.
		vec2 good, bad;
		glm_vec2_copy(p0, good);
		glm_vec2_copy(p1, bad);
		float w = glm_vec2_distance(good, bad);
		while (w > radius) {
			vec2 mid;
			glm_vec2_center(good, bad, mid);
			if (find_triangle(collider, mid)) {
				glm_vec2_copy(mid, good);
			} else {
				glm_vec2_copy(mid, bad);
			}
			w /= 2.f;
		}
		glm_vec2_copy(good, out);
	}

	// Ensure that `out` is at least `radius` away from any boundary edge.
	vec2 aabb[2];
	glm_vec2_subs(out, radius, aabb[0]);
	glm_vec2_adds(out, radius, aabb[1]);
	for (int i = 0; i < collider->nr_edges; i++) {
		struct collider_edge *e = &collider->edges[i];
		if (!glm_aabb2d_aabb(e->aabb, aabb))
			continue;
		vec2 q;
		float distance = distance_point_to_edge(out, e, q);
		if (distance < radius) {
			// out = q + normalize(out - q) * radius
			vec2 v;
			glm_vec2_sub(out, q, v);
			glm_vec2_normalize(v);
			glm_vec2_copy(q, out);
			glm_vec2_muladds(v, radius, out);
			// update AABB
			glm_vec2_subs(out, radius, aabb[0]);
			glm_vec2_adds(out, radius, aabb[1]);
		}
	}
	return true;
}

bool collider_raycast(struct collider *collider, vec3 origin, vec3 direction, vec3 out)
{
	struct collider_triangle* t = collider->triangles;
	for (; t < collider->triangles + collider->nr_triangles; t++) {
		float d;
		if (glm_ray_triangle(origin, direction, t->vertices[0], t->vertices[1], t->vertices[2], &d)) {
			glm_vec3_scale(direction, d, out);
			glm_vec3_add(origin, out, out);
			return true;
		}
	}
	return false;
}
