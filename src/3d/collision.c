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
	vec3 center;
	vec2 aabb[2];  // xz coordinates
	vec2 slope;
	float intercept;
	int neighbors[3];
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
		t->slope[0] = -normal[0] / normal[1];
		t->slope[1] = -normal[2] / normal[1];
		t->intercept = glm_vec3_dot(normal, t->vertices[0]) / normal[1];

		for (int i = 0; i < 3; i++) {
			t->neighbors[i] = -1;
		}
	}
}

static void link_neighbors(struct collider *collider, int t1_index, int t2_index)
{
	struct collider_triangle *t1 = &collider->triangles[t1_index];
	struct collider_triangle *t2 = &collider->triangles[t2_index];
	for (int i = 0; i < 3; i++) {
		if (t1->neighbors[i] == -1) {
			t1->neighbors[i] = t2_index;
			break;
		}
	}
	for (int i = 0; i < 3; i++) {
		if (t2->neighbors[i] == -1) {
			t2->neighbors[i] = t1_index;
			break;
		}
	}
}

static void init_edges(struct collider *collider, struct pol_mesh *mesh)
{
	// Link neighboring triangles.
	if (mesh->nr_triangles >= 65535)
		ERROR("Too many triangles in collision mesh: %d", mesh->nr_triangles);
	const int nv = mesh->nr_vertices;
	const int table_size = nv * nv;
	uint16_t *table = xcalloc(table_size, sizeof(uint16_t));
	int nr_edges = 0;
	for (int i = 0; i < mesh->nr_triangles; i++) {
		for (int j = 0; j < 3; j++) {
			int v1 = mesh->triangles[i].vert_index[j];
			int v2 = mesh->triangles[i].vert_index[(j + 1) % 3];
			int k = v2 * nv + v1;
			if (table[k]) {
				link_neighbors(collider, table[k] - 1, i);
				table[k] = 0;
				nr_edges--;
			} else {
				table[v1 * nv + v2] = i + 1;
				nr_edges++;
			}
		}
	}
	// Collect boundary edges, i.e., edges that belong to only one triangle.
	collider->nr_edges = nr_edges;
	collider->edges = xcalloc(nr_edges, sizeof(struct collider_edge));
	struct collider_edge* edge = collider->edges;
	for (int i = 0; i < table_size; i++) {
		if (!table[i]) continue;
		int v1 = i / nv;
		int v2 = i % nv;
		edge->vertices[0][0] = mesh->vertices[v1].pos[0];
		edge->vertices[0][1] = mesh->vertices[v1].pos[2];
		edge->vertices[1][0] = mesh->vertices[v2].pos[0];
		edge->vertices[1][1] = mesh->vertices[v2].pos[2];
		glm_vec2_minv(edge->vertices[0], edge->vertices[1], edge->aabb[0]);
		glm_vec2_maxv(edge->vertices[0], edge->vertices[1], edge->aabb[1]);
		edge++;
	}
	assert(edge == collider->edges + nr_edges);
	free(table);
}

struct collider *collider_create(struct pol_mesh *mesh)
{
	struct collider *collider = xcalloc(1, sizeof(struct collider));
	init_triangles(collider, mesh);
	init_edges(collider, mesh);
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
		float sq_distance = distance2_point_to_segment(out, e->vertices[0], e->vertices[1], q);
		if (sq_distance < radius * radius) {
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
	// Check if the point is in the view frustum.
	return p[0] >= -p[3] && p[0] <= p[3] &&
	       p[1] >= -p[3] && p[1] <= p[3] &&
	       p[2] >= -p[3] && p[2] <= p[3];
}

bool collider_find_path(struct collider *collider, vec3 start, vec3 goal, mat4 vp_transform)
{
	if (collider->path_points) {
		collider->nr_path_points = 0;
		free(collider->path_points);
		collider->path_points = NULL;
	}

	// A* search for a graph with triangle centers as nodes and neighbors as edges.
	struct collider_triangle* start_t = find_triangle(collider, (vec2){ start[0], start[2] });
	if (!start_t || !is_point_visible(start_t->center, vp_transform))
		return false;
	struct collider_triangle* goal_t = find_triangle(collider, (vec2){ goal[0], goal[2] });
	if (!goal_t || !is_point_visible(goal_t->center, vp_transform))
		return false;

	int start_i = start_t - collider->triangles;
	int goal_i = goal_t - collider->triangles;
	struct pathfinder pf;
	pf.nodes = xcalloc(collider->nr_triangles, sizeof(struct pathfinder_node));
	pf.nodes[goal_i].pred = -2;
	pf.nodes[start_i].pred = -1;
	pf.nodes[start_i].g_score = 0.f;
	pf.nodes[start_i].state = IN_FRONTIER;
	pf.frontiers = xcalloc(collider->nr_triangles, sizeof(struct frontier));
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
