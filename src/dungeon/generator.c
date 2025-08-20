/* Copyright (C) 2025 kichikuou <KichikuouChrome@gmail.com>
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
#include <string.h>

#include "dungeon/dgn.h"
#include "dungeon/generator.h"
#include "dungeon/mtrand43.h"

#define DD2_MAP_WIDTH 18
#define DD2_MAP_HEIGHT 18
typedef char dd2_map[DD2_MAP_WIDTH+1][DD2_MAP_HEIGHT+1];  // +1 to prevent OOB access

struct point_list {
	int size;
	struct point {
		int8_t x;
		int8_t y;
	} p[DD2_MAP_WIDTH * DD2_MAP_HEIGHT];
};

static void point_list_add(struct point_list *list, int x, int y)
{
	assert(list->size < DD2_MAP_WIDTH * DD2_MAP_HEIGHT);
	list->p[list->size++] = (struct point){ x, y };
}

// Check if the cell is a dead end (surrounded by walls on 3 sides)
static bool is_dead_end(dd2_map map, int x, int y)
{
	if (x < 0 || x >= DD2_MAP_WIDTH || y < 0 || y >= DD2_MAP_HEIGHT) {
		return false;
	}

	int wall_count = 0;
	if (y - 1 < 0               || map[x][y - 1] == '#') wall_count++;
	if (y + 1 >= DD2_MAP_HEIGHT || map[x][y + 1] == '#') wall_count++;
	if (x - 1 < 0              || map[x - 1][y] == '#') wall_count++;
	if (x + 1 >= DD2_MAP_WIDTH || map[x + 1][y] == '#') wall_count++;
	return wall_count == 3;
}

static void dd2_generate_dfs(dd2_map map, struct mtrand43 *mt, int x, int y)
{
	map[x][y] = '.';
	const int dx_offsets[] = {0, 0, -1, 1};
	const int dy_offsets[] = {-1, 1, 0, 0};

	// Shuffle the order of directions (0, 1, 2, 3) randomly
	int directions[] = {0, 1, 2, 3};
	for (int i = 0; i < 32; ++i) {
		uint32_t r1 = mtrand43_genrand(mt) & 3;
		uint32_t r2 = mtrand43_genrand(mt) & 3;
		int temp = directions[r1];
		directions[r1] = directions[r2];
		directions[r2] = temp;
	}

	// Try each direction in the shuffled order
	for (int i = 0; i < 4; ++i) {
		int direction = directions[i];
		int nx = x + dx_offsets[direction] * 2;
		int ny = y + dy_offsets[direction] * 2;

		if (nx < 0 || nx >= DD2_MAP_WIDTH || ny < 0 || ny >= DD2_MAP_HEIGHT || map[nx][ny] != '#') {
			continue;
		}
		// Dig the wall and recurse
		map[x + dx_offsets[direction]][y + dy_offsets[direction]] = '.';
		dd2_generate_dfs(map, mt, nx, ny);
	}
}

static void dd2_fill_dead_ends(dd2_map map, struct mtrand43 *mt)
{
	struct point_list dead_end_cells = {0};
	for (int y = 1; y < DD2_MAP_HEIGHT - 1; ++y) {
		for (int x = 1; x < DD2_MAP_WIDTH - 1; ++x) {
			// NOTE: This does not check if (x, y) is a path cell ('.').
			if (is_dead_end(map, x, y)) {
				point_list_add(&dead_end_cells, x, y);
			}
		}
	}

	if (dead_end_cells.size < 5) {
		return;  // Not enough dead ends to fill
	}

	int num_to_attempt = dead_end_cells.size / 2;
	num_to_attempt += num_to_attempt * mtrand43_genfloat(mt);
	if (num_to_attempt == dead_end_cells.size) {
		num_to_attempt--;
	}

	bool attempted[DD2_MAP_WIDTH * DD2_MAP_HEIGHT] = { false };
	for (int i = 0; i < num_to_attempt; ++i) {
		int random_index;
		do {
			random_index = mtrand43_genfloat(mt) * (dead_end_cells.size - 1);
		} while (attempted[random_index]);
		attempted[random_index] = true;

		int start_x = dead_end_cells.p[random_index].x;
		int start_y = dead_end_cells.p[random_index].y;

		// Find the open direction (path direction) from the dead end cell
		int dx, dy;
		if (map[start_x][start_y - 1] == '.') {
			dx = 0; dy = -1;
		} else if (map[start_x][start_y + 1] == '.') {
			dx = 0; dy = 1;
		} else if (map[start_x - 1][start_y] == '.') {
			dx = -1; dy = 0;
		} else if (map[start_x + 1][start_y] == '.') {
			dx = 1; dy = 0;
		} else {
			continue;  // Skip if no open path
		}

		// How far does this path continue? (maximum fillable length)
		int max_fillable = 0;
		int x = start_x;
		int y = start_y;
		while (map[x][y] == '.') {
			// Break if there is a path in the perpendicular direction (i.e. an intersection)
			if (map[x - dy][y + dx] == '.' || map[x + dy][y - dx] == '.') {
				break;
			}
			max_fillable++;
			x += dx;
			y += dy;
		}

		// Fill with walls
		int fill_length = mtrand43_genfloat(mt) * max_fillable;
		for (int k = 0; k < fill_length; ++k) {
			map[start_x + k * dx][start_y + k * dy] = '#';
		}
	}
}

static void dd2_generate_halls(dd2_map map, struct mtrand43 *mt)
{
	// Randomly generate 0 to 3 halls
	int hall_count = mtrand43_genrand(mt) & 3;

	// List up all path cells
	struct point_list origin_candidates = {0};
	for (int y = 0; y < DD2_MAP_HEIGHT; ++y) {
		for (int x = 0; x < DD2_MAP_WIDTH; ++x) {
			if (map[x][y] == '.') {
				point_list_add(&origin_candidates, x, y);
			}
		}
	}

	for (int i = 0; i < hall_count; ++i) {
		// Randomly select a path cell as the hall origin
		int random_index = mtrand43_genrand(mt) % origin_candidates.size;
		// NOTE: x and y are swapped here (bug in the original code)
		int origin_x = origin_candidates.p[random_index].y;
		int origin_y = origin_candidates.p[random_index].x;

		// Randomly determine hall size (width and height: 2 to 5)
		int width  = (mtrand43_genrand(mt) & 3) + 2;
		int height = (mtrand43_genrand(mt) & 3) + 2;

		// Set the hall area cells as path
		for (int dy = 0; dy < height; ++dy) {
			for (int dx = 0; dx < width; ++dx) {
				int x = origin_x + dx;
				int y = origin_y + dy;
				if (x < DD2_MAP_WIDTH && y < DD2_MAP_HEIGHT) {
					map[x][y] = '.';
				}
			}
		}
	}
}

static void dd2_place_entities(dd2_map map, int level)
{
	// List up all dead end cells
	struct point_list candidates = {0};
	for (int y = 0; y < DD2_MAP_HEIGHT; ++y) {
		for (int x = 0; x < DD2_MAP_WIDTH; ++x) {
			if (map[x][y] == '.' && is_dead_end(map, x, y)) {
				point_list_add(&candidates, x, y);
			}
		}
	}
	// If there are few dead ends, use all path cells as candidates
	if (candidates.size < 3) {
		candidates.size = 0;
		for (int y = 0; y < DD2_MAP_HEIGHT; ++y) {
			for (int x = 0; x < DD2_MAP_WIDTH; ++x) {
				if (map[x][y] == '.') {
					point_list_add(&candidates, x, y);
				}
			}
		}
	}

	struct mtrand43 mt;
	mtrand43_init(&mt, (level + 0x400) | 1);

	int start, goal, treasure;
	start = mtrand43_genrand(&mt) % candidates.size;
	do {
		goal = mtrand43_genrand(&mt) % candidates.size;
	} while (goal == start);
	do {
		treasure = mtrand43_genrand(&mt) % candidates.size;
	} while (treasure == start || treasure == goal);
	map[candidates.p[start].x][candidates.p[start].y] = 'S';
	map[candidates.p[goal].x][candidates.p[goal].y] = 'E';
	map[candidates.p[treasure].x][candidates.p[treasure].y] = 'T';
}

static void dd2_generate_map(dd2_map map, int level)
{
	struct mtrand43 mt;
	mtrand43_init(&mt, (level + 2) * 16 | 1);
	memset(map, '#', sizeof(dd2_map));
	dd2_generate_dfs(map, &mt, DD2_MAP_WIDTH / 2, DD2_MAP_HEIGHT / 2);
	dd2_fill_dead_ends(map, &mt);
	dd2_generate_halls(map, &mt);
	dd2_place_entities(map, level);
}

static int wall_texture(char left, char right, int x, int z, int orientation)
{
	if (left != '#' && right != '#')
		return 0;
	if (left != '#')
		return 1;
	if (right != '#')
		return 2;
	return 4 + (z * 5 + x + 3 + orientation) % 4;
}

struct dgn *dgn_generate_drawdungeon2(int level)
{
	dd2_map map;
	dd2_generate_map(map, level);

	// Create dgn structure from the generated map
	struct dgn *dgn = dgn_new(DD2_MAP_WIDTH, 1, DD2_MAP_HEIGHT);

	struct mtrand43 floor_randomizer;
	mtrand43_init(&floor_randomizer, (level * 65 + 32) | 1);

	for (int z = 0; z < dgn->size_z; z++) {
		for (int x = 0; x < dgn->size_x; x++) {
			struct dgn_cell *cell = &dgn->cells[dgn_cell_index(dgn, x, 0, z)];

			if (map[x][z] == '#') {
				continue;
			}
			switch (map[x][z]) {
			case 'S':
				dgn->start_x = x;
				dgn->start_y = z;
				break;
			case 'E':
				dgn->exit_x = x;
				dgn->exit_y = z;
				cell->floor_event = 17; // goal
				break;
			case 'T':
				cell->floor_event = 11; // treasure chest
				break;
			}

			cell->floor = mtrand43_genrand(&floor_randomizer) % 3;
			cell->ceiling = mtrand43_genrand(&floor_randomizer) % 3;
			cell->enterable = 1;
			if (map[x - 1][z] == '#')
				cell->west_wall = wall_texture(map[x - 1][z - 1], map[x - 1][z + 1], x, z, 2);
			if (map[x + 1][z] == '#')
				cell->east_wall = wall_texture(map[x + 1][z + 1], map[x + 1][z - 1], x, z, 0);
			if (map[x][z - 1] == '#')
				cell->south_wall = wall_texture(map[x + 1][z - 1], map[x - 1][z - 1], x, z, 3);
			if (map[x][z + 1] == '#')
				cell->north_wall = wall_texture(map[x - 1][z + 1], map[x + 1][z + 1], x, z, 1);
		}
	}
	dgn_calc_lightmap(dgn);
	return dgn;
}

// Set pathfinding_cost for each cell on the shortest path to the exit cell
void dgn_paint_step(struct dgn *dgn, int start_x, int start_y)
{
	int nr_cells = dgn_nr_cells(dgn);
	for (int i = 0; i < nr_cells; i++) {
		dgn->cells[i].pathfinding_cost = -1;
	}

	// BFS.
	short dist[DD2_MAP_WIDTH][DD2_MAP_HEIGHT] = {0};
	struct { int8_t x, y; } prev[DD2_MAP_WIDTH][DD2_MAP_HEIGHT];
	struct point_list queue = {0};
	int queue_ptr = 0;

	dist[start_x][start_y] = 1;
	point_list_add(&queue, start_x, start_y);

	while (queue_ptr < queue.size) {
		int cx = queue.p[queue_ptr].x;
		int cy = queue.p[queue_ptr].y;
		if (cx == dgn->exit_x && cy == dgn->exit_y)
			break;
		queue_ptr++;

		// Add adjacent cells to the queue
		const int dx_offsets[] = {0, 0, -1, 1};
		const int dy_offsets[] = {-1, 1, 0, 0};
		for (int i = 0; i < 4; i++) {
			int nx = cx + dx_offsets[i];
			int ny = cy + dy_offsets[i];
			if (nx >= 0 && nx < DD2_MAP_WIDTH && ny >= 0 && ny < DD2_MAP_HEIGHT &&
			    !dist[nx][ny] && dgn->cells[dgn_cell_index(dgn, nx, 0, ny)].floor >= 0) {
				dist[nx][ny] = dist[cx][cy] + 1;
				prev[nx][ny].x = cx;
				prev[nx][ny].y = cy;
				point_list_add(&queue, nx, ny);
			}
		}
	}
	if (queue_ptr >= queue.size) {
		// The exit is not reachable from the start (should not happen)
		return;
	}

	// Backtrack from the exit to the start to paint the path
	int x = dgn->exit_x;
	int y = dgn->exit_y;
	for (;;) {
		struct dgn_cell *cell = &dgn->cells[dgn_cell_index(dgn, x, 0, y)];
		cell->pathfinding_cost = dist[x][y] - 1;  // -1 to make the start cell 0
		if (x == start_x && y == start_y)
			break;
		int px = prev[x][y].x;
		int py = prev[x][y].y;
		x = px;
		y = py;
	}
}
