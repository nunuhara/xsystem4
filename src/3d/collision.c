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
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <cglm/cglm.h>

#include "system4.h"

#include "3d_internal.h"

struct collider_triangle {
	vec3 vertices[3];
	vec3 center;
	vec2 aabb[2];  // xz coordinates
	vec2 slope;
	float intercept;
	int neighbors[3];
};

struct collider_edge {
	vec2 vertices[2];  // xz coordinates
	vec2 aabb[2];
	int tri_index;
};

static struct collider_triangle *fill_triangles(struct collider_triangle *t, struct pol_mesh *mesh)
{
	for (int tri_i = 0; tri_i < mesh->nr_triangles; tri_i++, t++) {
		glm_aabb2d_invalidate(t->aabb);
		for (int i = 0; i < 3; i++) {
			glm_vec3_copy(mesh->vertices[mesh->triangles[tri_i].vert_index[i]].pos, t->vertices[i]);
			glm_vec3_add(t->center, t->vertices[i], t->center);
			vec2 xz = { t->vertices[i][0], t->vertices[i][2] };
			glm_vec2_minv(xz, t->aabb[0], t->aabb[0]);
			glm_vec2_maxv(xz, t->aabb[1], t->aabb[1]);
		}
		glm_vec3_divs(t->center, 3.f, t->center);

		// Do some precomputation so that we can calculate height(x, z) quickly.
		vec3 v1, v2, normal;
		glm_vec3_sub(t->vertices[1], t->vertices[0], v1);
		glm_vec3_sub(t->vertices[2], t->vertices[0], v2);
		glm_vec3_cross(v1, v2, normal);
		if (fabsf(normal[1]) > 1e-6f) {
			t->slope[0] = -normal[0] / normal[1];
			t->slope[1] = -normal[2] / normal[1];
			t->intercept = glm_vec3_dot(normal, t->vertices[0]) / normal[1];
		} else {
			t->slope[0] = 0.0f;
			t->slope[1] = 0.0f;
			t->intercept = t->center[1];
		}

		for (int i = 0; i < 3; i++) {
			t->neighbors[i] = -1;
		}
	}
	return t;
}

static void init_triangles(struct collider *collider, struct pol_mesh *mesh)
{
	collider->nr_triangles = mesh->nr_triangles;
	collider->triangles = xcalloc(mesh->nr_triangles, sizeof(struct collider_triangle));
	fill_triangles(collider->triangles, mesh);
}

static void link_unique_neighbors(struct collider *collider, int t1_index, int t2_index)
{
	struct collider_triangle *t1 = &collider->triangles[t1_index];
	struct collider_triangle *t2 = &collider->triangles[t2_index];
	bool has12 = false, has21 = false;
	for (int i = 0; i < 3; i++) {
		if (t1->neighbors[i] == t2_index) has12 = true;
		if (t2->neighbors[i] == t1_index) has21 = true;
	}
	if (!has12) {
		for (int i = 0; i < 3; i++) {
			if (t1->neighbors[i] == -1) {
				t1->neighbors[i] = t2_index;
				break;
			}
		}
	}
	if (!has21) {
		for (int i = 0; i < 3; i++) {
			if (t2->neighbors[i] == -1) {
				t2->neighbors[i] = t1_index;
				break;
			}
		}
	}
}

struct half_edge_slot {
	uint64_t key;
	uint32_t tri_p1;  // 0 = empty, UINT32_MAX = tombstone, else tri_index + 1
};

static inline size_t hash_edge_key(uint64_t x)
{
	x ^= x >> 30;
	x *= 0xbf58476d1ce4e5b9ULL;
	x ^= x >> 27;
	x *= 0x94d049bb133111ebULL;
	x ^= x >> 31;
	return (size_t)x;
}

static struct half_edge_slot *edge_table_find(struct half_edge_slot *table, size_t mask, uint64_t key)
{
	size_t idx = hash_edge_key(key) & mask;
	for (;;) {
		struct half_edge_slot *slot = &table[idx];
		if (slot->tri_p1 == 0)
			return NULL;
		if (slot->tri_p1 != UINT32_MAX && slot->key == key)
			return slot;
		idx = (idx + 1) & mask;
	}
}

static void init_edges(struct collider *collider, struct pol_mesh *mesh)
{
	// Link neighboring triangles via an O(nr_triangles) open-addressed half-edge table.
	if (mesh->nr_triangles == 0) {
		collider->nr_edges = 0;
		collider->edges = NULL;
		return;
	}
	size_t max_tris = (SIZE_MAX - 1) / 6;
	if (max_tris > (size_t)(INT32_MAX / 3))
		max_tris = (size_t)(INT32_MAX / 3);
	if ((size_t)mesh->nr_triangles > max_tris)
		ERROR("Too many triangles in collision mesh: %u", mesh->nr_triangles);

	size_t min_cap = (size_t)mesh->nr_triangles * 6 + 1;
	size_t cap = 16;
	while (cap < min_cap) {
		if (cap > (SIZE_MAX >> 1))
			ERROR("Too many triangles in collision mesh: %u", mesh->nr_triangles);
		cap <<= 1;
	}
	size_t mask = cap - 1;
	struct half_edge_slot *table = xcalloc(cap, sizeof(*table));

	uint32_t nr_edges = 0;
	for (uint32_t i = 0; i < mesh->nr_triangles; i++) {
		for (int j = 0; j < 3; j++) {
			uint32_t v1 = mesh->triangles[i].vert_index[j];
			uint32_t v2 = mesh->triangles[i].vert_index[(j + 1) % 3];
			if (v1 >= mesh->nr_vertices || v2 >= mesh->nr_vertices || v1 == v2)
				continue;
			uint64_t opp_key = ((uint64_t)v2 << 32) | (uint64_t)v1;
			struct half_edge_slot *opp = edge_table_find(table, mask, opp_key);
			if (opp) {
				link_unique_neighbors(collider, (int)(opp->tri_p1 - 1), (int)i);
				opp->tri_p1 = UINT32_MAX;
				nr_edges--;
			} else {
				uint64_t key = ((uint64_t)v1 << 32) | (uint64_t)v2;
				size_t idx = hash_edge_key(key) & mask;
				struct half_edge_slot *first_tomb = NULL;
				for (;;) {
					struct half_edge_slot *slot = &table[idx];
					if (slot->tri_p1 == 0) {
						struct half_edge_slot *dst = first_tomb ? first_tomb : slot;
						dst->key = key;
						dst->tri_p1 = i + 1;
						nr_edges++;
						break;
					}
					if (slot->tri_p1 == UINT32_MAX) {
						if (!first_tomb)
							first_tomb = slot;
					} else if (slot->key == key) {
						// Non-manifold duplicate directed edge: link without corrupting edge count.
						link_unique_neighbors(collider, (int)(slot->tri_p1 - 1), (int)i);
						break;
					}
					idx = (idx + 1) & mask;
				}
			}
		}
	}

	// Collect boundary edges, i.e., edges that belong to only one triangle.
	collider->nr_edges = nr_edges;
	collider->edges = nr_edges ? xcalloc(nr_edges, sizeof(struct collider_edge)) : NULL;
	struct collider_edge *edge = collider->edges;
	for (size_t i = 0; i < cap; i++) {
		if (table[i].tri_p1 == 0 || table[i].tri_p1 == UINT32_MAX)
			continue;
		uint32_t v1 = (uint32_t)(table[i].key >> 32);
		uint32_t v2 = (uint32_t)(table[i].key & 0xffffffffu);
		edge->vertices[0][0] = mesh->vertices[v1].pos[0];
		edge->vertices[0][1] = mesh->vertices[v1].pos[2];
		edge->vertices[1][0] = mesh->vertices[v2].pos[0];
		edge->vertices[1][1] = mesh->vertices[v2].pos[2];
		glm_vec2_minv(edge->vertices[0], edge->vertices[1], edge->aabb[0]);
		glm_vec2_maxv(edge->vertices[0], edge->vertices[1], edge->aabb[1]);
		edge->tri_index = (int)(table[i].tri_p1 - 1);
		edge++;
	}
	assert(edge == collider->edges + nr_edges);
	free(table);
}

struct collider *collider_create(struct pol_mesh *mesh)
{
	struct collider *collider = xcalloc(1, sizeof(struct collider));
	collider->is_navmesh = true;
	init_triangles(collider, mesh);
	init_edges(collider, mesh);
	return collider;
}

// Creates a collider for raycasting only (no edges / neighbor links), from the
// union of the given meshes' triangles. Used for SealEngine HeightDetection meshes.
struct collider *collider_create_raycast(struct pol_mesh **meshes, int nr_meshes)
{
	struct collider *collider = xcalloc(1, sizeof(struct collider));
	for (int i = 0; i < nr_meshes; i++)
		collider->nr_triangles += meshes[i]->nr_triangles;
	collider->triangles = xcalloc(collider->nr_triangles, sizeof(struct collider_triangle));
	struct collider_triangle *t = collider->triangles;
	for (int i = 0; i < nr_meshes; i++)
		t = fill_triangles(t, meshes[i]);
	return collider;
}

void collider_free(struct collider *collider)
{
	free(collider->triangles);
	free(collider->edges);
	free(collider->path_points);
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

struct cand_boundary_edge {
	vec2 v0;
	vec2 v1;
	int tri_index;
	uint32_t tile_start;
	uint32_t tile_end;
};

struct collider *collider_create_from_plugin(struct RE_plugin *plugin)
{
	if (!plugin)
		return NULL;

	int nr_inst = 0;
	uint32_t total_tris = 0;
	uint32_t total_cand_edges = 0;
	for (int i = 0; i < plugin->nr_instances; i++) {
		struct RE_instance *inst = plugin->instances[i];
		if (!inst || !inst->draw || !inst->model || !inst->model->collider || !inst->model->collider->is_navmesh)
			continue;
		nr_inst++;
		total_tris += inst->model->collider->nr_triangles;
		total_cand_edges += inst->model->collider->nr_edges;
	}
	if (nr_inst == 0 || total_tris == 0)
		return NULL;

	struct collider *dst = xcalloc(1, sizeof(struct collider));
	dst->is_navmesh = true;
	dst->nr_triangles = total_tris;
	dst->triangles = xcalloc(total_tris, sizeof(struct collider_triangle));

	struct cand_boundary_edge *cands = xcalloc(total_cand_edges ? total_cand_edges : 1, sizeof(struct cand_boundary_edge));
	uint32_t tri_base = 0;
	uint32_t cand_count = 0;

	for (int i = 0; i < plugin->nr_instances; i++) {
		struct RE_instance *inst = plugin->instances[i];
		if (!inst || !inst->draw || !inst->model || !inst->model->collider || !inst->model->collider->is_navmesh)
			continue;
		if (inst->local_transform_needs_update)
			RE_instance_update_local_transform(inst);

		struct collider *src = inst->model->collider;
		for (uint32_t ti = 0; ti < src->nr_triangles; ti++) {
			struct collider_triangle *st = &src->triangles[ti];
			struct collider_triangle *dt = &dst->triangles[tri_base + ti];
			glm_aabb2d_invalidate(dt->aabb);
			glm_vec3_zero(dt->center);
			for (int k = 0; k < 3; k++) {
				glm_mat4_mulv3(inst->local_transform, st->vertices[k], 1.0f, dt->vertices[k]);
				glm_vec3_add(dt->center, dt->vertices[k], dt->center);
				vec2 xz = { dt->vertices[k][0], dt->vertices[k][2] };
				glm_vec2_minv(xz, dt->aabb[0], dt->aabb[0]);
				glm_vec2_maxv(xz, dt->aabb[1], dt->aabb[1]);
			}
			glm_vec3_divs(dt->center, 3.0f, dt->center);

			vec3 v1, v2, normal;
			glm_vec3_sub(dt->vertices[1], dt->vertices[0], v1);
			glm_vec3_sub(dt->vertices[2], dt->vertices[0], v2);
			glm_vec3_cross(v1, v2, normal);
			if (fabsf(normal[1]) > 1e-6f) {
				dt->slope[0] = -normal[0] / normal[1];
				dt->slope[1] = -normal[2] / normal[1];
				dt->intercept = glm_vec3_dot(normal, dt->vertices[0]) / normal[1];
			} else {
				dt->slope[0] = 0.0f;
				dt->slope[1] = 0.0f;
				dt->intercept = dt->center[1];
			}

			for (int k = 0; k < 3; k++) {
				dt->neighbors[k] = st->neighbors[k] >= 0 ? (int)tri_base + st->neighbors[k] : -1;
			}
		}

		for (uint32_t ei = 0; ei < src->nr_edges; ei++) {
			struct collider_edge *se = &src->edges[ei];
			struct cand_boundary_edge *ce = &cands[cand_count++];
			vec3 p0_in = { se->vertices[0][0], 0.0f, se->vertices[0][1] };
			vec3 p1_in = { se->vertices[1][0], 0.0f, se->vertices[1][1] };
			vec3 p0_out, p1_out;
			glm_mat4_mulv3(inst->local_transform, p0_in, 1.0f, p0_out);
			glm_mat4_mulv3(inst->local_transform, p1_in, 1.0f, p1_out);
			ce->v0[0] = p0_out[0];
			ce->v0[1] = p0_out[2];
			ce->v1[0] = p1_out[0];
			ce->v1[1] = p1_out[2];
			ce->tri_index = (int)tri_base + se->tri_index;
			ce->tile_start = tri_base;
			ce->tile_end = tri_base + src->nr_triangles;
		}

		tri_base += src->nr_triangles;
	}

	dst->edges = xcalloc(cand_count ? cand_count : 1, sizeof(struct collider_edge));
	uint32_t out_edges = 0;

	if (nr_inst == 1) {
		for (uint32_t ei = 0; ei < cand_count; ei++) {
			struct collider_edge *de = &dst->edges[out_edges++];
			glm_vec2_copy(cands[ei].v0, de->vertices[0]);
			glm_vec2_copy(cands[ei].v1, de->vertices[1]);
			glm_vec2_minv(de->vertices[0], de->vertices[1], de->aabb[0]);
			glm_vec2_maxv(de->vertices[0], de->vertices[1], de->aabb[1]);
			de->tri_index = cands[ei].tri_index;
		}
	} else {
		for (uint32_t ei = 0; ei < cand_count; ei++) {
			struct cand_boundary_edge *ce = &cands[ei];
			struct collider_triangle *t1 = &dst->triangles[ce->tri_index];
			vec2 mid, ev, nrm, to_mid;
			glm_vec2_center(ce->v0, ce->v1, mid);
			glm_vec2_sub(ce->v1, ce->v0, ev);
			nrm[0] = -ev[1];
			nrm[1] = ev[0];
			glm_vec2_normalize(nrm);
			to_mid[0] = mid[0] - t1->center[0];
			to_mid[1] = mid[1] - t1->center[2];
			if (glm_vec2_dot(nrm, to_mid) < 0.0f) {
				nrm[0] = -nrm[0];
				nrm[1] = -nrm[1];
			}
			vec2 probe = { mid[0] + nrm[0] * 0.05f, mid[1] + nrm[1] * 0.05f };
			float mid_y = glm_vec2_dot(t1->slope, mid) + t1->intercept;
			int seam_tri = -1;
			for (uint32_t ti = 0; ti < dst->nr_triangles; ti++) {
				if (ti >= ce->tile_start && ti < ce->tile_end)
					continue;
				struct collider_triangle *t2 = &dst->triangles[ti];
				if (!glm_aabb2d_point(t2->aabb, probe))
					continue;
				if (!in_triangle(t2, probe))
					continue;
				float y2 = glm_vec2_dot(t2->slope, probe) + t2->intercept;
				if (fabsf(y2 - mid_y) < 2.0f) {
					seam_tri = (int)ti;
					break;
				}
			}
			if (seam_tri >= 0) {
				link_unique_neighbors(dst, ce->tri_index, seam_tri);
			} else {
				struct collider_edge *de = &dst->edges[out_edges++];
				glm_vec2_copy(ce->v0, de->vertices[0]);
				glm_vec2_copy(ce->v1, de->vertices[1]);
				glm_vec2_minv(de->vertices[0], de->vertices[1], de->aabb[0]);
				glm_vec2_maxv(de->vertices[0], de->vertices[1], de->aabb[1]);
				de->tri_index = ce->tri_index;
			}
		}
	}

	dst->nr_edges = out_edges;
	free(cands);
	return dst;
}

bool collider_height(struct collider *collider, vec2 xz, float *h_out)
{
	struct collider_triangle* t = find_triangle(collider, xz);
	if (!t)
		return false;
	*h_out = glm_vec2_dot(t->slope, xz) + t->intercept;
	return true;
}

// Calculates the squared distance from a point to a line segment in 2D space.
static float distance2_point_to_segment(vec2 p, vec2 a, vec2 b, vec2 closest_point_out)
{
	vec2 v, w;
	glm_vec2_sub(b, a, v);
	glm_vec2_sub(p, a, w);

	float c1 = glm_vec2_dot(w, v);
	if (c1 < 0.f) {
		glm_vec2_copy(a, closest_point_out);
		return glm_vec2_distance2(p, a);
	}
	float c2 = glm_vec2_norm2(v);
	if (c2 <= c1) {
		glm_vec2_copy(b, closest_point_out);
		return glm_vec2_distance2(p, b);
	}
	float t = c1 / c2;
	glm_vec2_copy(a, closest_point_out);
	glm_vec2_muladds(v, t, closest_point_out);
	return glm_vec2_distance2(p, closest_point_out);
}

static bool segments_intersect(vec2 p0, vec2 p1, vec2 q0, vec2 q1)
{
	vec2 r, s;
	glm_vec2_sub(p1, p0, r);
	glm_vec2_sub(q1, q0, s);
	float r_cross_s = glm_vec2_cross(r, s);
	if (r_cross_s == 0.f) {
		// The segments are collinear.
		return false;
	}
	vec2 q0_p0;
	glm_vec2_sub(q0, p0, q0_p0);
	float t = glm_vec2_cross(q0_p0, s) / r_cross_s;
	float u = glm_vec2_cross(q0_p0, r) / r_cross_s;
	return (t >= 0.f && t <= 1.f && u >= 0.f && u <= 1.f);
}

bool check_collision(struct collider *collider, vec2 p0, vec2 p1, float radius, vec2 out)
{
	if (!isfinite(radius) || radius < 0.0f)
		radius = 0.0f;
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
		float min_w = fmaxf(radius, 1e-4f);
		if (isfinite(w)) {
			for (int iter = 0; iter < 32 && w > min_w; iter++) {
				vec2 mid;
				glm_vec2_center(good, bad, mid);
				if (find_triangle(collider, mid)) {
					glm_vec2_copy(mid, good);
				} else {
					glm_vec2_copy(mid, bad);
				}
				w *= 0.5f;
			}
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
		float sq_distance = distance2_point_to_segment(out, e->vertices[0], e->vertices[1], q);
		if (sq_distance < radius * radius) {
			// out = q + normalize(out - q) * radius
			vec2 v, pushed;
			glm_vec2_sub(out, q, v);
			if (glm_vec2_norm2(v) < 1e-10f)
				glm_vec2_sub(p0, q, v);
			if (glm_vec2_norm2(v) >= 1e-10f) {
				glm_vec2_normalize(v);
				glm_vec2_copy(q, pushed);
				glm_vec2_muladds(v, radius, pushed);
				if (find_triangle(collider, pushed)) {
					glm_vec2_copy(pushed, out);
					// update AABB
					glm_vec2_subs(out, radius, aabb[0]);
					glm_vec2_adds(out, radius, aabb[1]);
				}
			}
		}
	}
	return true;
}

bool collider_raycast(struct collider *collider, vec3 origin, vec3 direction, vec3 out)
{
	// Return the intersection nearest to the ray origin (closest to the camera).
	float nearest = FLT_MAX;
	struct collider_triangle* t = collider->triangles;
	for (; t < collider->triangles + collider->nr_triangles; t++) {
		float d;
		if (glm_ray_triangle(origin, direction, t->vertices[0], t->vertices[1], t->vertices[2], &d)) {
			if (d < nearest)
				nearest = d;
		}
	}
	if (nearest == FLT_MAX)
		return false;
	glm_vec3_scale(direction, nearest, out);
	glm_vec3_add(origin, out, out);
	return true;
}

struct pathfinder_node {
	int pred;
	float g_score;
	enum { UNDISCOVERED, IN_FRONTIER, VISITED, NOT_VISIBLE } state;
};

struct frontier {
	int triangle_index;
	float f_score;
};

struct pathfinder {
	struct pathfinder_node *nodes;  // indexed by triangle index
	struct frontier *frontiers;  // min-heap
	int nr_frontiers;
	int frontiers_cap;
};

static int frontier_pop(struct pathfinder *pf)
{
	if (pf->nr_frontiers == 0) {
		return -1;
	}
	int index = pf->frontiers[0].triangle_index;
	if (--pf->nr_frontiers > 0) {
		pf->frontiers[0] = pf->frontiers[pf->nr_frontiers];
		int i = 0;
		while (i < pf->nr_frontiers) {
			int left = 2 * i + 1;
			int right = 2 * i + 2;
			int smallest = i;
			if (left < pf->nr_frontiers && pf->frontiers[left].f_score < pf->frontiers[smallest].f_score) {
				smallest = left;
			}
			if (right < pf->nr_frontiers && pf->frontiers[right].f_score < pf->frontiers[smallest].f_score) {
				smallest = right;
			}
			if (smallest == i) {
				break;
			}
			struct frontier tmp = pf->frontiers[i];
			pf->frontiers[i] = pf->frontiers[smallest];
			pf->frontiers[smallest] = tmp;
			i = smallest;
		}
	}
	return index;
}

static void frontier_push(struct pathfinder *pf, int index, float f_score)
{
	if (pf->nr_frontiers >= pf->frontiers_cap) {
		int new_cap = pf->frontiers_cap > 0 ? pf->frontiers_cap * 2 : 16;
		pf->frontiers = xrealloc(pf->frontiers, (size_t)new_cap * sizeof(struct frontier));
		pf->frontiers_cap = new_cap;
	}
	pf->frontiers[pf->nr_frontiers++] = (struct frontier){ index, f_score };
	int i = pf->nr_frontiers - 1;
	while (i > 0) {
		int parent = (i - 1) / 2;
		if (pf->frontiers[i].f_score >= pf->frontiers[parent].f_score) {
			break;
		}
		struct frontier tmp = pf->frontiers[i];
		pf->frontiers[i] = pf->frontiers[parent];
		pf->frontiers[parent] = tmp;
		i = parent;
	}
}

static bool is_point_visible(vec3 point, mat4 vp_transform)
{
	vec4 p = { point[0], point[1], point[2], 1.f };
	glm_mat4_mulv(vp_transform, p, p);
	// Check if the point is in the view frustum (with margin for large overworld triangles).
	float xy_limit = p[3] * 1.25f;
	return p[3] > 0.f &&
	       p[0] >= -xy_limit && p[0] <= xy_limit &&
	       p[1] >= -xy_limit && p[1] <= xy_limit &&
	       p[2] >= -p[3] && p[2] <= p[3];
}

static struct collider_triangle *find_triangle_or_nearest(struct collider *collider, vec2 xz, float max_dist)
{
	struct collider_triangle *t = find_triangle(collider, xz);
	if (t)
		return t;
	float best_d2 = max_dist * max_dist;
	struct collider_triangle *best = NULL;
	vec2 query_aabb[2] = { { xz[0] - max_dist, xz[1] - max_dist }, { xz[0] + max_dist, xz[1] + max_dist } };
	for (uint32_t i = 0; i < collider->nr_triangles; i++) {
		struct collider_triangle *cand = &collider->triangles[i];
		if (!glm_aabb2d_aabb(cand->aabb, query_aabb))
			continue;
		for (int e = 0; e < 3; e++) {
			vec2 a = { cand->vertices[e][0], cand->vertices[e][2] };
			vec2 b = { cand->vertices[(e + 1) % 3][0], cand->vertices[(e + 1) % 3][2] };
			vec2 cp;
			float d2 = distance2_point_to_segment(xz, a, b, cp);
			if (d2 < best_d2) {
				best_d2 = d2;
				best = cand;
			}
		}
	}
	return best;
}

bool collider_find_path(struct collider *collider, vec3 start, vec3 goal, mat4 vp_transform)
{
	if (collider->path_points) {
		collider->nr_path_points = 0;
		free(collider->path_points);
		collider->path_points = NULL;
	}

	// A* search for a graph with triangle centers as nodes and neighbors as edges.
	struct collider_triangle* start_t = find_triangle_or_nearest(collider, (vec2){ start[0], start[2] }, 0.5f);
	if (!start_t || (!is_point_visible(start_t->center, vp_transform) && !is_point_visible(start, vp_transform)))
		return false;
	struct collider_triangle* goal_t = find_triangle_or_nearest(collider, (vec2){ goal[0], goal[2] }, 0.5f);
	if (!goal_t || (!is_point_visible(goal_t->center, vp_transform) && !is_point_visible(goal, vp_transform)))
		return false;

	int start_i = start_t - collider->triangles;
	int goal_i = goal_t - collider->triangles;
	struct pathfinder pf;
	pf.nodes = xcalloc(collider->nr_triangles, sizeof(struct pathfinder_node));
	pf.nodes[goal_i].pred = -2;
	pf.nodes[start_i].pred = -1;
	pf.nodes[start_i].g_score = 0.f;
	pf.nodes[start_i].state = IN_FRONTIER;
	pf.frontiers_cap = collider->nr_triangles > 16 ? (int)collider->nr_triangles : 16;
	pf.frontiers = xcalloc(pf.frontiers_cap, sizeof(struct frontier));
	pf.nr_frontiers = 0;
	frontier_push(&pf, start_i, glm_vec3_distance(start_t->center, goal_t->center));

	while (pf.nr_frontiers > 0) {
		int current_i = frontier_pop(&pf);
		if (pf.nodes[current_i].state == VISITED)
			continue;
		struct collider_triangle *current_t = &collider->triangles[current_i];
		if (current_i == goal_i)
			break;
		pf.nodes[current_i].state = VISITED;
		for (int j = 0; j < 3; j++) {
			int neighbor_i = current_t->neighbors[j];
			if (neighbor_i == -1)
				continue;
			struct collider_triangle *neighbor_t = &collider->triangles[neighbor_i];
			struct pathfinder_node *neighbor_n = &pf.nodes[neighbor_i];
			if (neighbor_n->state == NOT_VISIBLE)
				continue;
			if (neighbor_n->state == UNDISCOVERED && !is_point_visible(neighbor_t->center, vp_transform)) {
				neighbor_n->state = NOT_VISIBLE;
				continue;
			}
			float g = pf.nodes[current_i].g_score + glm_vec3_distance(current_t->center, neighbor_t->center);
			if (neighbor_n->state == UNDISCOVERED || g < neighbor_n->g_score) {
				neighbor_n->pred = current_i;
				neighbor_n->g_score = g;
				neighbor_n->state = IN_FRONTIER;
				float f_score = g + glm_vec3_distance(neighbor_t->center, goal_t->center);
				frontier_push(&pf, neighbor_i, f_score);
			}
		}
	}
	if (pf.nodes[goal_i].pred != -2) {
		// Reconstruct the path.
		int path_length = 0;
		int current_i = goal_i;
		while (current_i != -1) {
			path_length++;
			current_i = pf.nodes[current_i].pred;
		}
		collider->nr_path_points = path_length + 2;
		collider->path_points = xcalloc(path_length + 2, sizeof(vec3));
		glm_vec3_copy(start, collider->path_points[0]);
		glm_vec3_copy(goal, collider->path_points[path_length + 1]);
		current_i = goal_i;
		for (int i = path_length - 1; i >= 0; i--) {
			glm_vec3_copy(collider->triangles[current_i].center, collider->path_points[i + 1]);
			current_i = pf.nodes[current_i].pred;
		}
	}
	free(pf.nodes);
	free(pf.frontiers);
	return !!collider->path_points;
}

// determines if a segment intersects with any collider edges, or is close enough to them.
static bool test_segment(struct collider *collider, vec2 p0, vec2 p1, float threshold)
{
	vec2 aabb[2];
	glm_vec2_minv(p0, p1, aabb[0]);
	glm_vec2_maxv(p0, p1, aabb[1]);
	aabb[0][0] -= threshold;
	aabb[0][1] -= threshold;
	aabb[1][0] += threshold;
	aabb[1][1] += threshold;
	float threshold2 = threshold * threshold;
	for (int i = 0; i < collider->nr_edges; i++) {
		struct collider_edge *e = &collider->edges[i];
		if (!glm_aabb2d_aabb(e->aabb, aabb))
			continue;
		if (segments_intersect(p0, p1, e->vertices[0], e->vertices[1]))
			return true;
		vec2 closest_point;
		float distance2 = distance2_point_to_segment(p0, e->vertices[0], e->vertices[1], closest_point);
		if (distance2 < threshold2)
			return true;
		distance2 = distance2_point_to_segment(p1, e->vertices[0], e->vertices[1], closest_point);
		if (distance2 < threshold2)
			return true;
		distance2 = distance2_point_to_segment(p0, p1, e->vertices[0], closest_point);
		if (distance2 < threshold2)
			return true;
		distance2 = distance2_point_to_segment(p0, p1, e->vertices[1], closest_point);
		if (distance2 < threshold2)
			return true;
	}
	return false;
}

static void optimize_path_rec(struct collider *collider, vec3 *points, int start, int end)
{
	if (end - start >= 2) {
		// If the segment (points[start], points[end]) does not intersect any
		// collider edges, we can skip all points in between.
		vec2 p0 = { points[start][0], points[start][2] };
		vec2 p1 = { points[end][0], points[end][2] };
		if (test_segment(collider, p0, p1, 0.5f)) {
			int mid = (start + end) / 2;
			optimize_path_rec(collider, points, start, mid);
			optimize_path_rec(collider, points, mid, end);
			return;
		}
	}
	glm_vec3_copy(points[start], collider->path_points[collider->nr_path_points++]);
}

bool collider_optimize_path(struct collider *collider)
{
	if (collider->nr_path_points < 3)
		return collider->nr_path_points > 0;  // nothing to optimize

	int nr_points = collider->nr_path_points;
	vec3 *points = xmalloc(nr_points * sizeof(vec3));
	memcpy(points, collider->path_points, nr_points * sizeof(vec3));
	collider->nr_path_points = 0;
	optimize_path_rec(collider, points, 0, nr_points - 1);
	glm_vec3_copy(points[nr_points - 1], collider->path_points[collider->nr_path_points++]);
	free(points);
	return true;
}
