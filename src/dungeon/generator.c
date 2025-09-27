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
#include "system4.h"
#include "system4/mt19937int.h"

#include "dungeon/dgn.h"
#include "dungeon/generator.h"
#include "dungeon/mtrand43.h"
#include "gfx/types.h"
#include "vm.h"

/*
 * DrawDungeon2
 */

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

/*
 * DrawField
 */

#define FLOOR(dg, x, y) (dgn_cell_at((dg)->dgn, (x), (dg)->floor, (y))->floor)
#define FLAG(dg, x, y) ((dg)->flags[(x) + (y) * (dg)->field_size_x])

#define WALL_OUTER 2
#define WALL_LEFT_UP_STAIRS 4
#define WALL_RIGHT_UP_STAIRS 5
#define WALL_AROUND_DOWN_STAIRS 6
#define WALL_TREASURE_ROOM 9
#define WALL_BACK_UP_STAIRS 11

struct drawfield_generator {
	struct dgn *dgn;
	uint8_t *flags;
	int wall_arrange_method;
	int floor_arrange_method;
	int current_room_id;
	int current_complexity;
	int target_complexity;
	struct mt19937 *rand;
	int floor;
	int field_size_x;
	int field_size_y;
};

static int rand_between(struct drawfield_generator *dg, int min, int max)
{
	if (min > max) {
		int tmp = min;
		min = max;
		max = tmp;
	}
	return mt19937_genrand(dg->rand) % (max - min + 1) + min;
}

// Sets a cell flag at a random position within a given rectangular area.
static void set_flag_at_random_pos(struct drawfield_generator *dg,
	enum drawfield_cell_flag flag, int min_x, int max_x, int min_y, int max_y)
{
	int x = rand_between(dg, min_x, max_x);
	int y = rand_between(dg, min_y, max_y);
	FLAG(dg, x, y) |= flag;
}

enum direction {
	DIR_NORTH = 0,
	DIR_WEST = 1,
	DIR_SOUTH = 2,
	DIR_EAST = 3,
};

struct direction_pool {
	enum direction d[4];
	int n;
};

#define DIRECTION_POOL_INIT { \
	.d = {DIR_NORTH, DIR_WEST, DIR_SOUTH, DIR_EAST}, \
	.n = 4, \
}

static enum direction direction_pool_pick(struct direction_pool *dp, struct drawfield_generator *dg)
{
	if (dp->n <= 0)
		ERROR("direction pool is empty");
	int index = mt19937_genrand(dg->rand) % dp->n;
	enum direction d = dp->d[index];
	for (int i = index; i < dp->n - 1; i++) {
		dp->d[i] = dp->d[i + 1];
	}
	dp->n--;
	return d;
}

// Checks if the specified rectangular area collides with any existing rooms.
static bool check_room_collision(struct drawfield_generator *dg, int min_x, int max_x, int min_y, int max_y)
{
	for (int y = min_y; y <= max_y; y++) {
		for (int x = min_x; x <= max_x; x++) {
			if (FLOOR(dg, x, y) != -1) {
				return true;
			}
		}
	}
	return false;
}

// Creates a door in a specified wall of a cell.
static void create_door(struct drawfield_generator *dg, int x, int y, enum direction dir)
{
	struct dgn_cell *cell = dgn_cell_at(dg->dgn, x, dg->floor, y);
	struct dgn_cell *neighbor;
	switch (dir) {
	case DIR_NORTH:
		cell->north_wall = -1;
		cell->north_door = 0;
		neighbor = dgn_cell_at(dg->dgn, x, dg->floor, y + 1);
		neighbor->south_wall = -1;
		neighbor->south_door = 0;
		break;
	case DIR_WEST:
		cell->west_wall = -1;
		cell->west_door = 0;
		neighbor = dgn_cell_at(dg->dgn, x - 1, dg->floor, y);
		neighbor->east_wall = -1;
		neighbor->east_door = 0;
		break;
	case DIR_SOUTH:
		cell->south_wall = -1;
		cell->south_door = 0;
		neighbor = dgn_cell_at(dg->dgn, x, dg->floor, y - 1);
		neighbor->north_wall = -1;
		neighbor->north_door = 0;
		break;
	case DIR_EAST:
		cell->east_wall = -1;
		cell->east_door = 0;
		neighbor = dgn_cell_at(dg->dgn, x + 1, dg->floor, y);
		neighbor->west_wall = -1;
		neighbor->west_door = 0;
		break;
	}
}

/*
 * Room decoration functions
 *
 * The following functions are called from create_room() to add features to a
 * newly created room area.
 *
 * Before calling any of these, create_room() fills the entire rectangular
 * area with a single, new room ID. These functions then modify that area
 * by creating new rooms with different IDs, or by carving out empty space.
 *
 * After these functions return, walls will be generated at the boundaries
 * between cells with different room IDs.
 */

// Creates a cross-shaped corridor that divides the room into four quadrants.
static void divide_room_with_cross_corridor(struct drawfield_generator *dg, int min_x, int max_x, int min_y, int max_y)
{
	int center_x = (min_x + max_x) / 2;
	int center_y = (min_y + max_y) / 2;

	// Create the cross-shaped corridor by assigning it a new room ID.
	dg->current_room_id++;
	for (int y = min_y; y <= max_y; y++) {
		FLOOR(dg, center_x, y) = dg->current_room_id;
	}
	for (int x = min_x; x <= max_x; x++) {
		FLOOR(dg, x, center_y) = dg->current_room_id;
	}

	// Add doors to connect the four quadrants to the central intersection.
	if (rand_between(dg, 0, 1)) {
		create_door(dg, rand_between(dg, min_x + 1, center_x - 2), center_y, DIR_SOUTH);
	} else {
		create_door(dg, center_x, rand_between(dg, min_y + 1, center_y - 2), DIR_WEST);
	}
	if (rand_between(dg, 0, 1)) {
		create_door(dg, rand_between(dg, min_x + 1, center_x - 2), center_y, DIR_NORTH);
	} else {
		create_door(dg, center_x, rand_between(dg, center_y + 2, max_y - 1), DIR_WEST);
	}
	if (rand_between(dg, 0, 1)) {
		create_door(dg, rand_between(dg, center_x + 2, max_x - 1), center_y, DIR_SOUTH);
	} else {
		create_door(dg, center_x, rand_between(dg, min_y + 1, center_y - 2), DIR_EAST);
	}
	if (rand_between(dg, 0, 1)) {
		create_door(dg, rand_between(dg, center_x + 2, max_x - 1), center_y, DIR_NORTH);
	} else {
		create_door(dg, center_x, rand_between(dg, center_y + 2, max_y - 1), DIR_EAST);
	}

	// Place one item in each of the four quadrants.
	set_flag_at_random_pos(dg, DF_FLAG_ITEM,
		min_x + 1, center_x - 2,
		min_y + 1, center_y - 2);
	set_flag_at_random_pos(dg, DF_FLAG_ITEM,
		center_x + 2, max_x - 1,
		min_y + 1, center_y - 2);
	set_flag_at_random_pos(dg, DF_FLAG_ITEM,
		min_x + 1, center_x - 2,
		center_y + 2, max_y - 1);
	set_flag_at_random_pos(dg, DF_FLAG_ITEM,
		center_x + 2, max_x - 1,
		center_y + 2, max_y - 1);

	// Place an enemy and a trap at the center of the cross.
	FLAG(dg, center_x, center_y) |= DF_FLAG_ENEMY | DF_FLAG_TRAP;

	dg->current_complexity += 4;
}

// Subdivides a room into four quadrants.
static void divide_room_into_quadrants(struct drawfield_generator *dg, int min_x, int max_x, int min_y, int max_y)
{
	// Pick a pivot point near the center of the room.
	int center_x = (min_x + max_x) / 2;
	int center_y = (min_y + max_y) / 2;
	int pivot_x = rand_between(dg, center_x - 1, center_x);
	int pivot_y = rand_between(dg, center_y - 1, center_y);

	// Divide the area into four rooms by creating two new rooms in diagonally
	// opposite quadrants.
	dg->current_room_id++;
	for (int y = min_y; y <= pivot_y; y++) {
		for (int x = min_x; x <= pivot_x; x++) {
			FLOOR(dg, x, y) = dg->current_room_id;
		}
	}
	dg->current_room_id++;
	for (int y = pivot_y + 1; y <= max_y; y++) {
		for (int x = pivot_x + 1; x <= max_x; x++) {
			FLOOR(dg, x, y) = dg->current_room_id;
		}
	}

	// Place 3 random doors on the internal dividing walls.
	struct direction_pool dp = DIRECTION_POOL_INIT;
	for (int i = 0; i < 3; i++) {
		switch (direction_pool_pick(&dp, dg)) {
		case 0: // Door on horizontal divider, left half
			create_door(dg, rand_between(dg, min_x + 1, pivot_x - 1), pivot_y, DIR_NORTH);
			break;
		case 1: // Door on horizontal divider, right half
			create_door(dg, rand_between(dg, pivot_x + 2, max_x - 1), pivot_y, DIR_NORTH);
			break;
		case 2: // Door on vertical divider, top half
			create_door(dg, pivot_x, rand_between(dg, min_y + 1, pivot_y - 1), DIR_EAST);
			break;
		case 3: // Door on vertical divider, bottom half
			create_door(dg, pivot_x, rand_between(dg, pivot_y + 2, max_y - 1), DIR_EAST);
			break;
		}
	}

	// Place one item randomly in each of the 4 quadrants.
	set_flag_at_random_pos(dg, DF_FLAG_ITEM,
		min_x + 1, pivot_x - 1,
		min_y + 1, pivot_y - 1); // TL
	set_flag_at_random_pos(dg, DF_FLAG_ITEM,
		pivot_x + 1, max_x - 1,
		min_y + 1, pivot_y - 1); // TR
	set_flag_at_random_pos(dg, DF_FLAG_ITEM,
		min_x + 1, pivot_x - 1,
		pivot_y + 1, max_y - 1); // BL
	set_flag_at_random_pos(dg, DF_FLAG_ITEM,
		pivot_x + 1, max_x - 1,
		pivot_y + 1, max_y - 1); // BR

	dg->current_complexity += 4;
}

// Creates a room inside the original room, forming a concentric layout.
static void create_concentric_room(struct drawfield_generator *dg, int min_x, int max_x, int min_y, int max_y)
{
	// Create the inner room, leaving a 1-tile corridor around it.
	dg->current_room_id++;
	for (int y = min_y + 1; y <= max_y - 1; y++) {
		for (int x = min_x + 1; x <= max_x - 1; x++) {
			FLOOR(dg, x, y) = dg->current_room_id;
		}
	}

	// Place 1 to 4 doors on random walls.
	struct direction_pool dp = DIRECTION_POOL_INIT;
	int num_doors = rand_between(dg, 1, 4);
	for (int i = 0; i < num_doors; i++) {
		enum direction dir = direction_pool_pick(&dp, dg);
		switch (dir) {
		case DIR_NORTH:
			create_door(dg, rand_between(dg, min_x + 2, max_x - 2), min_y, DIR_NORTH);
			break;
		case DIR_WEST:
			create_door(dg, max_x, rand_between(dg, min_y + 2, max_y - 2), DIR_WEST);
			break;
		case DIR_SOUTH:
			create_door(dg, rand_between(dg, min_x + 2, max_x - 2), max_y, DIR_SOUTH);
			break;
		case DIR_EAST:
			create_door(dg, min_x, rand_between(dg, min_y + 2, max_y - 2), DIR_EAST);
			break;
		}
	}

	// Populate the inner room with objects.
	for (int y = min_y + 2; y <= max_y - 2; y++) {
		for (int x = min_x + 2; x <= max_x - 2; x++) {
			switch (rand_between(dg, 0, 2)) {
			case 0:
				FLAG(dg, x, y) |= DF_FLAG_ITEM | DF_FLAG_ENEMY | DF_FLAG_TRAP;
				break;
			case 1:
				FLAG(dg, x, y) |= DF_FLAG_ITEM;
				break;
			}
		}
	}

	// Place enemies and traps in the corners with a 50% chance for each corner.
	if (rand_between(dg, 0, 1)) {
		FLAG(dg, min_x, min_y) |= DF_FLAG_ENEMY | DF_FLAG_TRAP;
	}
	if (rand_between(dg, 0, 1)) {
		FLAG(dg, max_x, min_y) |= DF_FLAG_ENEMY | DF_FLAG_TRAP;
	}
	if (rand_between(dg, 0, 1)) {
		FLAG(dg, min_x, max_y) |= DF_FLAG_ENEMY | DF_FLAG_TRAP;
	}
	if (rand_between(dg, 0, 1)) {
		FLAG(dg, max_x, max_y) |= DF_FLAG_ENEMY | DF_FLAG_TRAP;
	}

	dg->current_complexity++;
}

// Splits the west half of the room into two new rooms.
static void divide_west_half_vertically(struct drawfield_generator *dg, int min_x, int max_x, int min_y, int max_y)
{
	int split_x = (min_x + 1 + max_x) / 2;
	int split_y = rand_between(dg, (min_y + max_y) / 2, (min_y + max_y + 1) / 2);

	// Create the south-west room section.
	dg->current_room_id++;
	for (int y = min_y; y < split_y; y++) {
		for (int x = min_x; x <= split_x; x++) {
			FLOOR(dg, x, y) = dg->current_room_id;
		}
	}
	// Create the north-west room section.
	dg->current_room_id++;
	for (int y = split_y; y <= max_y; y++) {
		for (int x = min_x; x <= split_x; x++) {
			FLOOR(dg, x, y) = dg->current_room_id;
		}
	}

	// Create doors on the vertical dividing line.
	create_door(dg, split_x, rand_between(dg, min_y + 1, split_y - 2), DIR_EAST);
	create_door(dg, split_x, rand_between(dg, split_y + 1, max_y - 1), DIR_EAST);

	// Place items in the new sections.
	set_flag_at_random_pos(dg, DF_FLAG_ITEM,
		min_x + 1, split_x - 1,
		split_y + 1, max_y - 1);
	set_flag_at_random_pos(dg, DF_FLAG_ITEM,
		min_x + 1, split_x - 1,
		min_y + 1, split_y - 1);

	dg->current_complexity += 3;
}

// Splits the east half of the room into two new rooms.
static void divide_east_half_vertically(struct drawfield_generator *dg, int min_x, int max_x, int min_y, int max_y)
{
	int split_x = (min_x + max_x) / 2;
	int split_y = rand_between(dg, (min_y + max_y) / 2, (min_y + max_y + 1) / 2);

	// Create the south-east room section.
	dg->current_room_id++;
	for (int y = min_y; y < split_y; y++) {
		for (int x = split_x; x <= max_x; x++) {
			FLOOR(dg, x, y) = dg->current_room_id;
		}
	}
	// Create the north-east room section.
	dg->current_room_id++;
	for (int y = split_y; y <= max_y; y++) {
		for (int x = split_x; x <= max_x; x++) {
			FLOOR(dg, x, y) = dg->current_room_id;
		}
	}

	// Create doors on the vertical dividing line.
	create_door(dg, split_x, rand_between(dg, min_y + 1, split_y - 2), DIR_WEST);
	create_door(dg, split_x, rand_between(dg, split_y + 1, max_y - 1), DIR_WEST);

	// Place items in the new sections.
	set_flag_at_random_pos(dg, DF_FLAG_ITEM,
		split_x + 1, max_x - 1,
		min_y + 1, split_y - 1);
	set_flag_at_random_pos(dg, DF_FLAG_ITEM,
		split_x + 1, max_x - 1,
		split_y + 1, max_y - 1);

	dg->current_complexity += 3;
}

// Splits the south half of the room into two new rooms.
static void divide_south_half_horizontally(struct drawfield_generator *dg, int min_x, int max_x, int min_y, int max_y)
{
	int split_y = (min_y + 1 + max_y) / 2;
	int split_x = rand_between(dg, (min_x + max_x) / 2, (min_x + max_x + 1) / 2);

	// Create the south-west room section.
	dg->current_room_id++;
	for (int y = min_y; y <= split_y; y++) {
		for (int x = min_x; x < split_x; x++) {
			FLOOR(dg, x, y) = dg->current_room_id;
		}
	}
	// Create the south-east room section.
	dg->current_room_id++;
	for (int y = min_y; y <= split_y; y++) {
		for (int x = split_x; x <= max_x; x++) {
			FLOOR(dg, x, y) = dg->current_room_id;
		}
	}

	// Create doors on the horizontal dividing line.
	create_door(dg, rand_between(dg, min_x + 1, split_x - 2), split_y, DIR_NORTH);
	create_door(dg, rand_between(dg, split_x + 1, max_x - 1), split_y, DIR_NORTH);

	// Place items in the new sections.
	set_flag_at_random_pos(dg, DF_FLAG_ITEM,
		min_x + 1, split_x - 1,
		min_y + 1, split_y - 1);
	set_flag_at_random_pos(dg, DF_FLAG_ITEM,
		split_x + 1, max_x - 1,
		min_y + 1, split_y - 1);

	dg->current_complexity += 3;
}

// Splits the north half of the room into two new rooms.
static void divide_north_half_horizontally(struct drawfield_generator *dg, int min_x, int max_x, int min_y, int max_y)
{
	int split_y = (min_y + max_y) / 2;
	int split_x = rand_between(dg, (min_x + max_x) / 2, (min_x + max_x + 1) / 2);

	// Create the north-west room section.
	dg->current_room_id++;
	for (int y = split_y; y <= max_y; y++) {
		for (int x = min_x; x < split_x; x++) {
			FLOOR(dg, x, y) = dg->current_room_id;
		}
	}
	// Create the north-east room section.
	dg->current_room_id++;
	for (int y = split_y; y <= max_y; y++) {
		for (int x = split_x; x <= max_x; x++) {
			FLOOR(dg, x, y) = dg->current_room_id;
		}
	}

	// Create doors on the horizontal dividing line.
	create_door(dg, rand_between(dg, min_x + 1, split_x - 2), split_y, DIR_SOUTH);
	create_door(dg, rand_between(dg, split_x + 1, max_x - 1), split_y, DIR_SOUTH);

	// Place items in the new sections.
	set_flag_at_random_pos(dg, DF_FLAG_ITEM,
		min_x + 1, split_x - 1,
		split_y + 1, max_y - 1);
	set_flag_at_random_pos(dg, DF_FLAG_ITEM,
		split_x + 1, max_x - 1,
		split_y + 1, max_y - 1);

	dg->current_complexity += 3;
}

// Divides the room with a horizontal corridor.
static void divide_room_with_horizontal_corridor(struct drawfield_generator *dg, int min_x, int max_x, int min_y, int max_y)
{
	int center_x = (min_x + max_x) / 2;
	int center_y = (min_y + max_y) / 2;

	// Create the horizontal corridor.
	dg->current_room_id++;
	for (int x = min_x; x <= max_x; x++) {
		FLOOR(dg, x, center_y) = dg->current_room_id;
	}

	if (max_x - min_x + 1 > 5) {
		// For wide rooms, create two new rooms on the left side. The right side
		// remains part of the original room, forming a 5-room 'H' shape.

		// Create south-west room.
		int split_x = rand_between(dg, center_x - 1, center_x);
		dg->current_room_id++;
		for (int y = min_y; y < center_y; y++) {
			for (int x = min_x; x <= split_x; x++) {
				FLOOR(dg, x, y) = dg->current_room_id;
			}
		}
		// Create two doors on the south side of the corridor.
		create_door(dg, rand_between(dg, min_x + 1, split_x - 1), center_y, DIR_SOUTH);
		create_door(dg, rand_between(dg, split_x + 2, max_x - 1), center_y, DIR_SOUTH);

		// Place items in a row in the south rooms (50% chance).
		int item_y_top = (center_y - 1 + min_y) / 2;
		for (int x = min_x + 1; x <= max_x - 1; x++) {
			if (x == split_x)
				continue;
			if (rand_between(dg, 0, 1) == 0)
				FLAG(dg, x, item_y_top) |= DF_FLAG_ITEM;
		}

		split_x = rand_between(dg, center_x - 1, center_x);

		// Create north-west room.
		dg->current_room_id++;
		for (int y = center_y + 1; y <= max_y; y++) {
			for (int x = min_x; x <= split_x; x++) {
				FLOOR(dg, x, y) = dg->current_room_id;
			}
		}
		// Create two doors on the north side of the corridor.
		create_door(dg, rand_between(dg, min_x + 1, split_x - 1), center_y, DIR_NORTH);
		create_door(dg, rand_between(dg, split_x + 2, max_x - 1), center_y, DIR_NORTH);

		// Place items in a row in the north rooms (100% chance).
		int item_y_bottom = (center_y + 1 + max_y) / 2;
		for (int x = min_x + 1; x <= max_x - 1; x++) {
			if (x == split_x)
				continue;
			FLAG(dg, x, item_y_bottom) |= DF_FLAG_ITEM;
		}

		dg->current_complexity += 4;
	} else {
		// For narrow rooms, just punch two vertical passages through the corridor.
		create_door(dg, rand_between(dg, min_x + 1, max_x - 1), center_y, DIR_NORTH);
		create_door(dg, rand_between(dg, min_x + 1, max_x - 1), center_y, DIR_SOUTH);

		// Place items in the middle of the top and bottom halves.
		int item_y_top = (center_y - 1 + min_y) / 2;
		int item_y_bottom = (center_y + 1 + max_y) / 2;
		for (int x = min_x + 1; x < max_x; x++) {
			FLAG(dg, x, item_y_top) |= DF_FLAG_ITEM;
			FLAG(dg, x, item_y_bottom) |= DF_FLAG_ITEM;
		}

		dg->current_complexity += 2;
	}
}

// Divides the room with a vertical corridor.
static void divide_room_with_vertical_corridor(struct drawfield_generator *dg, int min_x, int max_x, int min_y, int max_y)
{
	int center_x = (min_x + max_x) / 2;
	int center_y = (min_y + max_y) / 2;

	// Create the vertical corridor.
	dg->current_room_id++;
	for (int y = min_y; y <= max_y; y++) {
		FLOOR(dg, center_x, y) = dg->current_room_id;
	}

	if (max_y - min_y + 1 > 5) {
		// For tall rooms, create two new rooms on the bottom side. The top side
		// remains part of the original room, forming a 5-room rotated 'H' shape.

		// Create south-west room.
		int split_y = rand_between(dg, center_y - 1, center_y);
		dg->current_room_id++;
		for (int y = min_y; y <= split_y; y++) {
			for (int x = min_x; x < center_x; x++) {
				FLOOR(dg, x, y) = dg->current_room_id;
			}
		}
		// Create two doors on the west side of the corridor.
		create_door(dg, center_x, rand_between(dg, min_y + 1, split_y - 1), DIR_WEST);
		create_door(dg, center_x, rand_between(dg, split_y + 2, max_y - 1), DIR_WEST);

		// Place items in a column in the south-west quadrant (50% chance).
		int item_x_west = (center_x - 1 + min_x) / 2;
		for (int y = min_y + 1; y < split_y; y++) {
			if (rand_between(dg, 0, 1) == 0)
				FLAG(dg, item_x_west, y) |= DF_FLAG_ITEM;
		}

		// Create south-east room.
		split_y = rand_between(dg, center_y - 1, center_y);
		dg->current_room_id++;
		for (int y = min_y; y <= split_y; y++) {
			for (int x = center_x + 1; x <= max_x; x++) {
				FLOOR(dg, x, y) = dg->current_room_id;
			}
		}
		// Create two doors on the east side of the corridor.
		create_door(dg, center_x, rand_between(dg, min_y + 1, split_y - 1), DIR_EAST);
		create_door(dg, center_x, rand_between(dg, split_y + 2, max_y - 1), DIR_EAST);

		// Place items in a column in the north-east quadrant (100% chance).
		int item_x_east = (center_x + 1 + max_x) / 2;
		for (int y = split_y + 1; y < max_y; y++) {
			FLAG(dg, item_x_east, y) |= DF_FLAG_ITEM;
		}

		dg->current_complexity += 4;
	} else {
		// For narrow rooms, just punch two horizontal passages through the corridor.
		create_door(dg, center_x, rand_between(dg, min_y + 1, max_y - 1), DIR_WEST);
		create_door(dg, center_x, rand_between(dg, min_y + 1, max_y - 1), DIR_EAST);

		// Place items in the middle of the left and right halves.
		int item_x_west = (center_x - 1 + min_x) / 2;
		int item_x_east = (center_x + 1 + max_x) / 2;
		for (int y = min_y + 1; y < max_y; y++) {
			FLAG(dg, item_x_west, y) |= DF_FLAG_ITEM;
			FLAG(dg, item_x_east, y) |= DF_FLAG_ITEM;
		}

		dg->current_complexity += 2;
	}
}

// Creates a small room in a randomly chosen corner of the area.
static void create_corner_room(struct drawfield_generator *dg, int min_x, int max_x, int min_y, int max_y)
{
	int room_min_x, room_max_x, room_min_y, room_max_y;
	int door_x, door_y;

	int corner = rand_between(dg, 0, 3);
	switch (corner) {
	case 0: // South-West
		room_min_x = min_x;
		room_max_x = (min_x + 1 + max_x) / 2;
		room_min_y = min_y;
		room_max_y = (min_y + 1 + max_y) / 2;
		break;
	case 1: // North-West
		room_min_x = min_x;
		room_max_x = (min_x + 1 + max_x) / 2;
		room_min_y = (min_y + max_y) / 2;
		room_max_y = max_y;
		break;
	case 2: // North-East
		room_min_x = (min_x + max_x) / 2;
		room_max_x = max_x;
		room_min_y = (min_y + max_y) / 2;
		room_max_y = max_y;
		break;
	case 3: // South-East
	default:
		room_min_x = (min_x + max_x) / 2;
		room_max_x = max_x;
		room_min_y = min_y;
		room_max_y = (min_y + 1 + max_y) / 2;
		break;
	}

	dg->current_room_id++;
	for (int y = room_min_y; y <= room_max_y; y++) {
		for (int x = room_min_x; x <= room_max_x; x++) {
			FLOOR(dg, x, y) = dg->current_room_id;
		}
	}

	if (rand_between(dg, 0, 1)) {
		// Create East/West door
		door_y = rand_between(dg, room_min_y + 1, room_max_y - 1);
		if (corner == 0 || corner == 1) // South-West or North-West
			create_door(dg, room_max_x, door_y, DIR_EAST);
		else // North-East or South-East
			create_door(dg, room_min_x, door_y, DIR_WEST);
	} else {
		// Create North/South door
		door_x = rand_between(dg, room_min_x + 1, room_max_x - 1);
		if (corner == 0 || corner == 3) // South-West or South-East
			create_door(dg, door_x, room_max_y, DIR_NORTH);
		else // North-West or North-East
			create_door(dg, door_x, room_min_y, DIR_SOUTH);
	}

	// Place an object in the room
	int obj_x = rand_between(dg, room_min_x + 1, room_max_x - 1);
	int obj_y = rand_between(dg, room_min_y + 1, room_max_y - 1);
	FLAG(dg, obj_x, obj_y) |= DF_FLAG_ITEM | DF_FLAG_ENEMY | DF_FLAG_TRAP;

	dg->current_complexity += 2;
}

// Carves out passages in a spiral pattern, making parts of the room empty.
static void carve_spiral_passages(struct drawfield_generator *dg, int min_x, int max_x, int min_y, int max_y)
{
	int center_x = (min_x + max_x) / 2;
	int center_y = (min_y + max_y) / 2;

	int axis_choice = rand_between(dg, 0, 1);
	if (axis_choice == 0) {
		// Horizontal openings
		int pattern_choice = rand_between(dg, 0, 1);
		if (pattern_choice == 0) {
			// Carve passages in an S-like shape.
			for (int x = min_x; x < max_x; x++) FLOOR(dg, x, min_y + 1) = -1;
			for (int x = min_x + 1; x <= max_x; x++) FLOOR(dg, x, max_y - 1) = -1;
			for (int y = min_y; y < center_y; y++) FLOOR(dg, min_x, y) = -1;
			for (int y = center_y + 1; y <= max_y; y++) FLOOR(dg, max_x, y) = -1;

			// Place enemies/traps in the remaining corners.
			FLAG(dg, min_x, max_y) |= DF_FLAG_ENEMY | DF_FLAG_TRAP;
			FLAG(dg, max_x, min_y) |= DF_FLAG_ENEMY | DF_FLAG_TRAP;
			// Place enemies/traps in the central column.
			for (int y = min_y + 2; y <= max_y - 2; y++) {
				FLAG(dg, center_x, y) |= DF_FLAG_ENEMY | DF_FLAG_TRAP;
			}
		} else {
			// Carve passages in a mirrored S-like shape.
			for (int x = min_x; x < max_x; x++) FLOOR(dg, x, max_y - 1) = -1;
			for (int x = min_x + 1; x <= max_x; x++) FLOOR(dg, x, min_y + 1) = -1;
			for (int y = min_y; y < center_y; y++) FLOOR(dg, max_x, y) = -1;
			for (int y = center_y + 1; y <= max_y; y++) FLOOR(dg, min_x, y) = -1;

			FLAG(dg, min_x, min_y) |= DF_FLAG_ENEMY | DF_FLAG_TRAP;
			FLAG(dg, max_x, max_y) |= DF_FLAG_ENEMY | DF_FLAG_TRAP;
			for (int y = min_y + 2; y <= max_y - 2; y++) {
				FLAG(dg, center_x, y) |= DF_FLAG_ENEMY | DF_FLAG_TRAP;
			}
		}
	} else {
		// Vertical openings
		int pattern_choice = rand_between(dg, 0, 1);
		if (pattern_choice == 0) {
			// Carve passages in a rotated and mirrored S-like shape.
			for (int y = min_y; y < max_y; y++) FLOOR(dg, min_x + 1, y) = -1;
			for (int y = min_y + 1; y <= max_y; y++) FLOOR(dg, max_x - 1, y) = -1;
			for (int x = min_x; x < center_x; x++) FLOOR(dg, x, min_y) = -1;
			for (int x = center_x + 1; x <= max_x; x++) FLOOR(dg, x, max_y) = -1;

			FLAG(dg, min_x, max_y) |= DF_FLAG_ENEMY | DF_FLAG_TRAP;
			FLAG(dg, max_x, min_y) |= DF_FLAG_ENEMY | DF_FLAG_TRAP;
			// Place enemies/traps in the central row.
			for (int x = min_x + 2; x <= max_x - 2; x++) {
				FLAG(dg, x, center_y) |= DF_FLAG_ENEMY | DF_FLAG_TRAP;
			}
		} else {
			// Carve passages in a rotated S-like shape.
			for (int y = min_y; y < max_y; y++) FLOOR(dg, max_x - 1, y) = -1;
			for (int y = min_y + 1; y <= max_y; y++) FLOOR(dg, min_x + 1, y) = -1;
			for (int x = min_x; x < center_x; x++) FLOOR(dg, x, max_y) = -1;
			for (int x = center_x + 1; x <= max_x; x++) FLOOR(dg, x, min_y) = -1;

			FLAG(dg, min_x, min_y) |= DF_FLAG_ENEMY | DF_FLAG_TRAP;
			FLAG(dg, max_x, max_y) |= DF_FLAG_ENEMY | DF_FLAG_TRAP;
			for (int x = min_x + 2; x <= max_x - 2; x++) {
				FLAG(dg, x, center_y) |= DF_FLAG_ENEMY | DF_FLAG_TRAP;
			}
		}
	}

	// Place an enemy or a trap in the absolute center.
	if (rand_between(dg, 0, 1) == 0) {
		FLAG(dg, center_x, center_y) |= DF_FLAG_ENEMY;
	} else {
		FLAG(dg, center_x, center_y) |= DF_FLAG_TRAP;
	}

	dg->current_complexity += 2;
}

// Creates a new room in the specified rectangular area and then populates it
// with a randomly chosen layout.
static void create_room(struct drawfield_generator *dg,
	int min_x, int max_x, int min_y, int max_y, enum direction from_direction)
{
	if (min_x > max_x) { int tmp = min_x; min_x = max_x; max_x = tmp; }
	if (min_y > max_y) { int tmp = min_y; min_y = max_y; max_y = tmp; }

	int width = max_x - min_x + 1;
	int height = max_y - min_y + 1;
	int center_x = (min_x + max_x) / 2;
	int center_y = (min_y + max_y) / 2;

	// Fill the entire rectangular area with a single, new room ID.
	dg->current_room_id++;
	for (int y = min_y; y <= max_y; y++) {
		for (int x = min_x; x <= max_x; x++) {
			FLOOR(dg, x, y) = dg->current_room_id;
		}
	}

	if (width > 2 && height > 2) {
		if (width != 3 && height != 3) {
			if (width > 5 && height > 5 && rand_between(dg, 0, 2) == 0) {
				divide_room_with_cross_corridor(dg, min_x, max_x, min_y, max_y);
				return;
			}

			switch (rand_between(dg, 0, 3)) {
			case 0:
			case 1:
				if ((from_direction == DIR_WEST || from_direction == DIR_EAST) && height > 6) {
					divide_room_with_horizontal_corridor(dg, min_x, max_x, min_y, max_y);
				} else if ((from_direction == DIR_NORTH || from_direction == DIR_SOUTH) && width > 6) {
					divide_room_with_vertical_corridor(dg, min_x, max_x, min_y, max_y);
				} else {
					if (rand_between(dg, 0, 1) == 0) {
						create_corner_room(dg, min_x, max_x, min_y, max_y);
					} else {
						carve_spiral_passages(dg, min_x, max_x, min_y, max_y);
					}
				}
				return;
			case 2:
				if (width > 5 && height > 5) {
					divide_room_into_quadrants(dg, min_x, max_x, min_y, max_y);
				} else {
					create_concentric_room(dg, min_x, max_x, min_y, max_y);
				}
				return;
			case 3:
				if (width <= height) {
					if (rand_between(dg, 0, 1))
						divide_west_half_vertically(dg, min_x, max_x, min_y, max_y);
					else
						divide_east_half_vertically(dg, min_x, max_x, min_y, max_y);
				} else {
					if (rand_between(dg, 0, 1))
						divide_south_half_horizontally(dg, min_x, max_x, min_y, max_y);
					else
						divide_north_half_horizontally(dg, min_x, max_x, min_y, max_y);
				}
				return;
			}
		} else {
			if (width == height) {
				FLAG(dg, center_x, center_y) |= DF_FLAG_ITEM | DF_FLAG_TREASURE_ROOM;
			} else if (width > 4 || height > 4 || rand_between(dg, 0, 1)) {
				// Turn the rectangular room into a cross-shaped corridor by marking the
				// corner quadrants for later removal. The value -2 is a temporary marker
				// that will be converted to empty space (-1).
				for (int y = min_y; y <= max_y; y++) {
					for (int x = min_x; x <= max_x; x++) {
						if (x != center_x && y != center_y)
							FLOOR(dg, x, y) = -2;
					}
				}
				FLAG(dg, center_x, center_y) |= DF_FLAG_ENEMY | DF_FLAG_TRAP;
			}
		}
	}

	dg->current_complexity++;
}

static void expand_room(struct drawfield_generator *dg, int origin_x, int origin_y, enum direction dir)
{
	if (dg->current_complexity >= dg->target_complexity)
		return;

	int room_width_radius, room_height_radius, room_width, room_height;
	int min_x, max_x, min_y, max_y;

	if (dir == DIR_NORTH || dir == DIR_SOUTH) {
		room_width_radius = rand_between(dg, 1, 3);
		room_height = rand_between(dg, 1, 3) * 2;
		min_x = origin_x - room_width_radius;
		max_x = origin_x + room_width_radius;
		if (dir == DIR_NORTH) {
			min_y = origin_y + 1;
			max_y = origin_y + 1 + room_height;
		} else { // DIR_SOUTH
			min_y = origin_y - 1 - room_height;
			max_y = origin_y - 1;
		}
	} else { // DIR_WEST || DIR_EAST
		room_height_radius = rand_between(dg, 1, 3);
		room_width = rand_between(dg, 1, 3) * 2;
		min_y = origin_y - room_height_radius;
		max_y = origin_y + room_height_radius;
		if (dir == DIR_WEST) {
			min_x = origin_x - 1 - room_width;
			max_x = origin_x - 1;
		} else { // DIR_EAST
			min_x = origin_x + 1;
			max_x = origin_x + 1 + room_width;
		}
	}

	if (min_x <= 0 || min_y <= 0 || max_x > dg->field_size_x - 2 || max_y > dg->field_size_y - 2)
		return;

	if (!check_room_collision(dg, min_x, max_x, min_y, max_y)) {
		create_door(dg, origin_x, origin_y, dir);
		create_room(dg, min_x, max_x, min_y, max_y, dir);

		int mid_x = (min_x + max_x) / 2;
		int mid_y = (min_y + max_y) / 2;
		struct direction_pool dp = DIRECTION_POOL_INIT;
		enum direction opposite_dir = (dir + 2) % 4;

		// Recursively expand from the new room in random directions.
		while (dp.n > 0) {
			enum direction next_dir = direction_pool_pick(&dp, dg);
			if (next_dir == opposite_dir)
				continue;  // Do not expand back toward the direction we came from.

			switch (next_dir) {
			case DIR_NORTH:
				expand_room(dg, mid_x, max_y, next_dir);
				break;
			case DIR_WEST:
				expand_room(dg, min_x, mid_y, next_dir);
				break;
			case DIR_SOUTH:
				expand_room(dg, mid_x, min_y, next_dir);
				break;
			case DIR_EAST:
				expand_room(dg, max_x, mid_y, next_dir);
				break;
			}
		}
	} else {
		// The desired area is occupied. As a fallback, attempt to connect to
		// the adjacent room by creating a door, which forms a loop.
		switch (dir) {
		case DIR_NORTH:
			if (FLOOR(dg, origin_x, origin_y + 1) != -1 &&
			    dgn_cell_at(dg->dgn, origin_x - 1, dg->floor, origin_y)->north_door == -1 &&
			    dgn_cell_at(dg->dgn, origin_x + 1, dg->floor, origin_y)->north_door == -1 &&
			    rand_between(dg, 0, 1)) {
				create_door(dg, origin_x, origin_y, DIR_NORTH);
			}
			break;
		case DIR_WEST:
			if (FLOOR(dg, origin_x - 1, origin_y) != -1 &&
			    dgn_cell_at(dg->dgn, origin_x, dg->floor, origin_y - 1)->west_door == -1 &&
			    dgn_cell_at(dg->dgn, origin_x, dg->floor, origin_y + 1)->west_door == -1 &&
			    rand_between(dg, 0, 1)) {
				create_door(dg, origin_x, origin_y, DIR_WEST);
			}
			break;
		case DIR_SOUTH:
			if (FLOOR(dg, origin_x, origin_y - 1) != -1 &&
			    dgn_cell_at(dg->dgn, origin_x - 1, dg->floor, origin_y)->south_door == -1 &&
			    dgn_cell_at(dg->dgn, origin_x + 1, dg->floor, origin_y)->south_door == -1 &&
			    rand_between(dg, 0, 1)) {
				create_door(dg, origin_x, origin_y, DIR_SOUTH);
			}
			break;
		case DIR_EAST:
			if (FLOOR(dg, origin_x + 1, origin_y) != -1 &&
			    dgn_cell_at(dg->dgn, origin_x, dg->floor, origin_y - 1)->east_door == -1 &&
			    dgn_cell_at(dg->dgn, origin_x, dg->floor, origin_y + 1)->east_door == -1 &&
			    rand_between(dg, 0, 1)) {
				create_door(dg, origin_x, origin_y, DIR_EAST);
			}
			break;
		}
	}
}

// Replaces temporary floor markers with standard empty space.
static void cleanup_floor_markers(struct drawfield_generator *dg)
{
	for (int y = 1; y < dg->field_size_y - 1; y++) {
		for (int x = 1; x < dg->field_size_x - 1; x++) {
			struct dgn_cell *cell = dgn_cell_at(dg->dgn, x, dg->floor, y);
			if (cell->floor < -1) {
				cell->floor = -1;
			}
		}
	}
}

static bool is_vertical_corridor(struct drawfield_generator *dg, int x, int y)
{
	struct dgn_cell *cell = dgn_cell_at(dg->dgn, x, dg->floor, y);
	return cell->floor != -1 &&
		FLOOR(dg, x - 1, y) != cell->floor &&
		FLOOR(dg, x + 1, y) != cell->floor &&
		cell->east_door == -1 &&
		cell->west_door == -1;
}

static bool is_horizontal_corridor(struct drawfield_generator *dg, int x, int y)
{
	struct dgn_cell *cell = dgn_cell_at(dg->dgn, x, dg->floor, y);
	return cell->floor != -1 &&
		FLOOR(dg, x, y - 1) != cell->floor &&
		FLOOR(dg, x, y + 1) != cell->floor &&
		cell->north_door == -1 &&
		cell->south_door == -1;
}

// Prunes or extends dead-end corridors to add variation.
static void prune_and_extend_corridors(struct drawfield_generator *dg)
{
	for (int y = 1; y < dg->field_size_y - 1; y++) {
		for (int x = 1; x < dg->field_size_x - 1; x++) {
			if (FLOOR(dg, x, y) == -1)
				continue;

			// Dead end facing North (corridor from South)
			if (FLOOR(dg, x, y - 1) != -1 && FLOOR(dg, x - 1, y) == -1 &&
			    FLOOR(dg, x + 1, y) == -1 && FLOOR(dg, x, y + 1) == -1) {
				if (rand_between(dg, 0, 3) == 0) { // Extend
					int room_id = FLOOR(dg, x, y);
					int cur_y = y + 1;
					while (cur_y < dg->field_size_y - 1 && FLOOR(dg, x, cur_y) == -1)
						cur_y++;
					if (cur_y < dg->field_size_y - 1 && FLOOR(dg, x, cur_y) != -1) {
						for (int i = y + 1; i < cur_y; i++)
							FLOOR(dg, x, i) = room_id;
					}
				} else { // Prune
					int room_id = FLOOR(dg, x, y);
					int cur_y = y;
					while (cur_y > 0 && FLOOR(dg, x, cur_y) == room_id &&
					       is_vertical_corridor(dg, x, cur_y)) {
						FLOOR(dg, x, cur_y) = -1;
						cur_y--;
					}
				}
			} // Dead end facing South (corridor from North)
			else if (FLOOR(dg, x, y + 1) != -1 && FLOOR(dg, x - 1, y) == -1 &&
				 FLOOR(dg, x + 1, y) == -1 && FLOOR(dg, x, y - 1) == -1) {
				if (rand_between(dg, 0, 3) == 0) { // Extend
					int room_id = FLOOR(dg, x, y);
					int cur_y = y - 1;
					while (cur_y > 0 && FLOOR(dg, x, cur_y) == -1)
						cur_y--;
					if (cur_y > 0 && FLOOR(dg, x, cur_y) != -1) {
						for (int i = y - 1; i > cur_y; i--)
							FLOOR(dg, x, i) = room_id;
					}
				} else { // Prune
					int room_id = FLOOR(dg, x, y);
					int cur_y = y;
					while (cur_y < dg->field_size_y - 1 &&
					       FLOOR(dg, x, cur_y) == room_id &&
					       is_vertical_corridor(dg, x, cur_y)) {
						FLOOR(dg, x, cur_y) = -1;
						cur_y++;
					}
				}
			} // Dead end facing East (corridor from West)
			else if (FLOOR(dg, x - 1, y) != -1 && FLOOR(dg, x, y - 1) == -1 &&
				 FLOOR(dg, x, y + 1) == -1 && FLOOR(dg, x + 1, y) == -1) {
				if (rand_between(dg, 0, 3) == 0) { // Extend
					int room_id = FLOOR(dg, x, y);
					int cur_x = x + 1;
					while (cur_x < dg->field_size_x - 1 && FLOOR(dg, cur_x, y) == -1)
						cur_x++;
					if (cur_x < dg->field_size_x - 1 && FLOOR(dg, cur_x, y) != -1) {
						for (int i = x + 1; i < cur_x; i++)
							FLOOR(dg, i, y) = room_id;
					}
				} else { // Prune
					int room_id = FLOOR(dg, x, y);
					int cur_x = x;
					while (cur_x > 0 && FLOOR(dg, cur_x, y) == room_id &&
					       is_horizontal_corridor(dg, cur_x, y)) {
						FLOOR(dg, cur_x, y) = -1;
						cur_x--;
					}
				}
			} // Dead end facing West (corridor from East)
			else if (FLOOR(dg, x + 1, y) != -1 && FLOOR(dg, x, y - 1) == -1 &&
				 FLOOR(dg, x, y + 1) == -1 && FLOOR(dg, x - 1, y) == -1) {
				if (rand_between(dg, 0, 3) == 0) { // Extend
					int room_id = FLOOR(dg, x, y);
					int cur_x = x - 1;
					while (cur_x > 0 && FLOOR(dg, cur_x, y) == -1)
						cur_x--;
					if (cur_x > 0 && FLOOR(dg, cur_x, y) != -1) {
						for (int i = x - 1; i > cur_x; i--)
							FLOOR(dg, i, y) = room_id;
					}
				} else { // Prune
					int room_id = FLOOR(dg, x, y);
					int cur_x = x;
					while (cur_x < dg->field_size_x - 1 &&
					       FLOOR(dg, cur_x, y) == room_id &&
					       is_horizontal_corridor(dg, cur_x, y)) {
						FLOOR(dg, cur_x, y) = -1;
						cur_x++;
					}
				}
			}
		}
	}
}

// Searches for dead-end corridors and attaches a new treasure room to them.
static void create_treasure_rooms(struct drawfield_generator *dg)
{
	for (int y = 1; y < dg->field_size_y - 1; y++) {
		for (int x = 1; x < dg->field_size_x - 1; x++) {
			if (FLOOR(dg, x, y) == -1)
				continue;

			// Dead end facing South (corridor from North)
			if (FLOOR(dg, x, y + 1) != -1 && FLOOR(dg, x - 1, y) == -1 &&
			    FLOOR(dg, x + 1, y) == -1 && FLOOR(dg, x, y - 1) == -1) {
				if (y > 2 && !check_room_collision(dg, x - 1, x + 1, y - 3, y - 1)) {
					dg->current_room_id++;
					for (int ry = y - 3; ry <= y - 1; ry++)
						for (int rx = x - 1; rx <= x + 1; rx++)
							FLOOR(dg, rx, ry) = dg->current_room_id;
					create_door(dg, x, y, DIR_SOUTH);
					FLAG(dg, x, y - 2) |= DF_FLAG_ITEM | DF_FLAG_TREASURE_ROOM;
					dg->current_complexity++;
				}
			}
			// Dead end facing North (corridor from South)
			else if (FLOOR(dg, x, y - 1) != -1 && FLOOR(dg, x - 1, y) == -1 &&
				 FLOOR(dg, x + 1, y) == -1 && FLOOR(dg, x, y + 1) == -1) {
				if (y < dg->field_size_y - 4 && !check_room_collision(dg, x - 1, x + 1, y + 1, y + 3)) {
					dg->current_room_id++;
					for (int ry = y + 1; ry <= y + 3; ry++)
						for (int rx = x - 1; rx <= x + 1; rx++)
							FLOOR(dg, rx, ry) = dg->current_room_id;
					create_door(dg, x, y, DIR_NORTH);
					FLAG(dg, x, y + 2) |= DF_FLAG_ITEM | DF_FLAG_TREASURE_ROOM;
					dg->current_complexity++;
				}
			}
			// Dead end facing West (corridor from East)
			else if (FLOOR(dg, x + 1, y) != -1 && FLOOR(dg, x, y - 1) == -1 &&
				 FLOOR(dg, x, y + 1) == -1 && FLOOR(dg, x - 1, y) == -1) {
				if (x > 2 && !check_room_collision(dg, x - 3, x - 1, y - 1, y + 1)) {
					dg->current_room_id++;
					for (int ry = y - 1; ry <= y + 1; ry++)
						for (int rx = x - 3; rx <= x - 1; rx++)
							FLOOR(dg, rx, ry) = dg->current_room_id;
					create_door(dg, x, y, DIR_WEST);
					FLAG(dg, x - 2, y) |= DF_FLAG_ITEM | DF_FLAG_TREASURE_ROOM;
					dg->current_complexity++;
				}
			}
			// Dead end facing East (corridor from West)
			else if (FLOOR(dg, x - 1, y) != -1 && FLOOR(dg, x, y - 1) == -1 &&
				 FLOOR(dg, x, y + 1) == -1 && FLOOR(dg, x + 1, y) == -1) {
				if (x < dg->field_size_x - 4 && !check_room_collision(dg, x + 1, x + 3, y - 1, y + 1)) {
					dg->current_room_id++;
					for (int ry = y - 1; ry <= y + 1; ry++)
						for (int rx = x + 1; rx <= x + 3; rx++)
							FLOOR(dg, rx, ry) = dg->current_room_id;
					create_door(dg, x, y, DIR_EAST);
					FLAG(dg, x + 2, y) |= DF_FLAG_ITEM | DF_FLAG_TREASURE_ROOM;
					dg->current_complexity++;
				}
			}
		}
	}
}

// Generates walls between cells with different room IDs.
static void generate_walls(struct drawfield_generator *dg)
{
	const int wall_types[] = {7, 8, 10, 0, 0, 0};
	for (int y = 0; y < dg->field_size_y; y++) {
		for (int x = 0; x < dg->field_size_x; x++) {
			int wall_type = wall_types[rand_between(dg, 0, 5)];

			int current_floor = FLOOR(dg, x, y);
			struct dgn_cell *cell = dgn_cell_at(dg->dgn, x, dg->floor, y);

			if (current_floor == -1) {
				// North
				if (y < dg->field_size_y - 1 && FLOOR(dg, x, y + 1) != -1) {
					struct dgn_cell *neighbor = dgn_cell_at(dg->dgn, x, dg->floor, y + 1);
					cell->north_wall = WALL_OUTER;
					cell->north_door = -1;
					neighbor->south_wall = wall_type;
					neighbor->south_door = -1;
				}
				// West
				if (x > 0 && FLOOR(dg, x - 1, y) != -1) {
					struct dgn_cell *neighbor = dgn_cell_at(dg->dgn, x - 1, dg->floor, y);
					cell->west_wall = WALL_OUTER;
					cell->west_door = -1;
					neighbor->east_wall = wall_type;
					neighbor->east_door = -1;
				}
				// South
				if (y > 0 && FLOOR(dg, x, y - 1) != -1) {
					struct dgn_cell *neighbor = dgn_cell_at(dg->dgn, x, dg->floor, y - 1);
					cell->south_wall = WALL_OUTER;
					cell->south_door = -1;
					neighbor->north_wall = wall_type;
					neighbor->north_door = -1;
				}
				// East
				if (x < dg->field_size_x - 1 && FLOOR(dg, x + 1, y) != -1) {
					struct dgn_cell *neighbor = dgn_cell_at(dg->dgn, x + 1, dg->floor, y);
					cell->east_wall = WALL_OUTER;
					cell->east_door = -1;
					neighbor->west_wall = wall_type;
					neighbor->west_door = -1;
				}
			} else {
				// North
				if (cell->north_door == -1 &&
				    (y == dg->field_size_y - 1 || current_floor != FLOOR(dg, x, y + 1))) {
					cell->north_wall = wall_type;
				}
				// West
				if (cell->west_door == -1 &&
				    (x == 0 || current_floor != FLOOR(dg, x - 1, y))) {
					cell->west_wall = wall_type;
				}
				// South
				if (cell->south_door == -1 &&
				    (y == 0 || current_floor != FLOOR(dg, x, y - 1))) {
					cell->south_wall = wall_type;
				}
				// East
				if (cell->east_door == -1 &&
				    (x == dg->field_size_x - 1 || current_floor != FLOOR(dg, x + 1, y))) {
					cell->east_wall = wall_type;
				}
			}
		}
	}
}

// Removes walls between adjacent non-floor cells.
static void remove_internal_walls(struct drawfield_generator *dg)
{
	for (int y = 0; y < dg->field_size_y; y++) {
		for (int x = 0; x < dg->field_size_x; x++) {
			struct dgn_cell *cell = dgn_cell_at(dg->dgn, x, dg->floor, y);
			if (cell->floor != -1)
				continue;

			// Check neighbor to the North.
			if (y < dg->field_size_y - 1) {
				struct dgn_cell *neighbor = dgn_cell_at(dg->dgn, x, dg->floor, y + 1);
				if (neighbor->floor == -1) {
					cell->north_wall = -1;
					cell->north_door = -1;
					neighbor->south_wall = -1;
					neighbor->south_door = -1;
				}
			}

			// Check neighbor to the East.
			if (x < dg->field_size_x - 1) {
				struct dgn_cell *neighbor = dgn_cell_at(dg->dgn, x + 1, dg->floor, y);
				if (neighbor->floor == -1) {
					cell->east_wall = -1;
					cell->east_door = -1;
					neighbor->west_wall = -1;
					neighbor->west_door = -1;
				}
			}
		}
	}
}

// Assigns a random floor texture to every cell that has a floor.
static void randomize_floor_texture(struct drawfield_generator *dg)
{
	const int floor_types[] = {1, 10, 11, 12};
	for (int y = 0; y < dg->field_size_y; y++) {
		for (int x = 0; x < dg->field_size_x; x++) {
			if (FLOOR(dg, x, y) != -1) {
				FLOOR(dg, x, y) = floor_types[rand_between(dg, 0, 3)];
			}
		}
	}
}

// Resolves dead-end cells by adding new doors to adjacent rooms.
static void resolve_dead_ends(struct drawfield_generator *dg)
{
	for (int y = 0; y < dg->field_size_y; y++) {
		for (int x = 0; x < dg->field_size_x; x++) {
			struct dgn_cell *cell = dgn_cell_at(dg->dgn, x, dg->floor, y);
			if (cell->floor == -1)
				continue;

			// North opening
			if (cell->north_wall == -1 && cell->west_wall != -1 && cell->south_wall != -1 && cell->east_wall != -1) {
				if (FLOOR(dg, x, y - 1) != -1) {
					create_door(dg, x, y, DIR_SOUTH);
				} else if (FLOOR(dg, x - 1, y) != -1 &&
				           dgn_cell_at(dg->dgn, x, dg->floor, y - 1)->west_door == -1 &&
				           dgn_cell_at(dg->dgn, x, dg->floor, y + 1)->west_door == -1) {
					create_door(dg, x, y, DIR_WEST);
				} else if (FLOOR(dg, x + 1, y) != -1 &&
				           dgn_cell_at(dg->dgn, x, dg->floor, y - 1)->east_door == -1 &&
				           dgn_cell_at(dg->dgn, x, dg->floor, y + 1)->east_door == -1) {
					create_door(dg, x, y, DIR_EAST);
				}
			}

			// West opening
			if (cell->west_wall == -1 && cell->south_wall != -1 && cell->east_wall != -1 && cell->north_wall != -1) {
				if (FLOOR(dg, x + 1, y) != -1) {
					create_door(dg, x, y, DIR_EAST);
				} else if (FLOOR(dg, x, y - 1) != -1 &&
				           dgn_cell_at(dg->dgn, x - 1, dg->floor, y)->south_door == -1 &&
				           dgn_cell_at(dg->dgn, x + 1, dg->floor, y)->south_door == -1) {
					create_door(dg, x, y, DIR_SOUTH);
				} else if (FLOOR(dg, x, y + 1) != -1 &&
				           dgn_cell_at(dg->dgn, x - 1, dg->floor, y)->north_door == -1 &&
				           dgn_cell_at(dg->dgn, x + 1, dg->floor, y)->north_door == -1) {
					create_door(dg, x, y, DIR_NORTH);
				}
			}

			// South opening
			if (cell->south_wall == -1 && cell->east_wall != -1 && cell->north_wall != -1 && cell->west_wall != -1) {
				if (FLOOR(dg, x, y + 1) != -1) {
					create_door(dg, x, y, DIR_NORTH);
				} else if (FLOOR(dg, x - 1, y) != -1 &&
				           dgn_cell_at(dg->dgn, x, dg->floor, y - 1)->west_door == -1 &&
				           dgn_cell_at(dg->dgn, x, dg->floor, y + 1)->west_door == -1) {
					create_door(dg, x, y, DIR_WEST);
				} else if (FLOOR(dg, x + 1, y) != -1 &&
				           dgn_cell_at(dg->dgn, x, dg->floor, y - 1)->east_door == -1 &&
				           dgn_cell_at(dg->dgn, x, dg->floor, y + 1)->east_door == -1) {
					create_door(dg, x, y, DIR_EAST);
				}
			}

			// East opening
			if (cell->east_wall == -1 && cell->north_wall != -1 && cell->west_wall != -1 && cell->south_wall != -1) {
				if (FLOOR(dg, x - 1, y) != -1) {
					create_door(dg, x, y, DIR_WEST);
				} else if (FLOOR(dg, x, y - 1) != -1 &&
				           dgn_cell_at(dg->dgn, x - 1, dg->floor, y)->south_door == -1 &&
				           dgn_cell_at(dg->dgn, x + 1, dg->floor, y)->south_door == -1) {
					create_door(dg, x, y, DIR_SOUTH);
				} else if (FLOOR(dg, x, y + 1) != -1 &&
				           dgn_cell_at(dg->dgn, x - 1, dg->floor, y)->north_door == -1 &&
				           dgn_cell_at(dg->dgn, x + 1, dg->floor, y)->north_door == -1) {
					create_door(dg, x, y, DIR_NORTH);
				}
			}
		}
	}
}

// Checks if a cell can be part of a consolidated, open passage.
static bool is_simple_passage(struct drawfield_generator *dg, int x, int y)
{
	struct dgn_cell *cell = dgn_cell_at(dg->dgn, x, dg->floor, y);
	// North Check
	if (y < dg->field_size_y - 1 && cell->north_wall == -1) {
		if (dgn_cell_at(dg->dgn, x, dg->floor, y + 1)->east_wall == -1)
			return false;
		if (dgn_cell_at(dg->dgn, x, dg->floor, y + 1)->west_wall == -1)
			return false;
	}
	// West Check
	if (x > 0 && cell->west_wall == -1) {
		if (dgn_cell_at(dg->dgn, x - 1, dg->floor, y)->north_wall == -1)
			return false;
		if (dgn_cell_at(dg->dgn, x - 1, dg->floor, y)->south_wall == -1)
			return false;
	}
	// South Check
	if (y > 0 && cell->south_wall == -1) {
		if (dgn_cell_at(dg->dgn, x, dg->floor, y - 1)->west_wall == -1)
			return false;
		if (dgn_cell_at(dg->dgn, x, dg->floor, y - 1)->east_wall == -1)
			return false;
	}
	// East Check
	if (x < dg->field_size_x - 1 && cell->east_wall == -1) {
		if (dgn_cell_at(dg->dgn, x + 1, dg->floor, y)->south_wall == -1)
			return false;
		if (dgn_cell_at(dg->dgn, x + 1, dg->floor, y)->north_wall == -1)
			return false;
	}
	return true;
}

// Consolidates adjacent passage cells by removing internal doors.
static void consolidate_passages(struct drawfield_generator *dg)
{
	for (int y = 0; y < dg->field_size_y; y++) {
		for (int x = 0; x < dg->field_size_x; x++) {
			struct dgn_cell *cell = dgn_cell_at(dg->dgn, x, dg->floor, y);
			if (cell->floor == -1)
				continue;
			if (is_simple_passage(dg, x, y)) {
				if (cell->north_door != -1) {
					cell->north_door = -1;
					dgn_cell_at(dg->dgn, x, dg->floor, y + 1)->south_door = -1;
				}
				if (cell->west_door != -1) {
					cell->west_door = -1;
					dgn_cell_at(dg->dgn, x - 1, dg->floor, y)->east_door = -1;
				}
				if (cell->south_door != -1) {
					cell->south_door = -1;
					dgn_cell_at(dg->dgn, x, dg->floor, y - 1)->north_door = -1;
				}
				if (cell->east_door != -1) {
					cell->east_door = -1;
					dgn_cell_at(dg->dgn, x + 1, dg->floor, y)->west_door = -1;
				}
			} else {
				// Horizontal check
				if (cell->east_door != -1) {
					struct dgn_cell *neighbor_e = dgn_cell_at(dg->dgn, x + 1, dg->floor, y);
					bool cell_is_horizontal = (cell->north_wall != -1 || cell->north_door != -1) &&
								  (cell->south_wall != -1 || cell->south_door != -1);
					bool neighbor_is_horizontal = (neighbor_e->north_wall != -1 || neighbor_e->north_door != -1) &&
									(neighbor_e->south_wall != -1 || neighbor_e->south_door != -1);

					if (cell_is_horizontal && neighbor_is_horizontal) {
						cell->east_door = -1;
						neighbor_e->west_door = -1;
					}
				}

				// Vertical check
				if (cell->north_door != -1) {
					struct dgn_cell *neighbor_n = dgn_cell_at(dg->dgn, x, dg->floor, y + 1);
					bool cell_is_vertical = (cell->west_wall != -1 || cell->west_door != -1) &&
								(cell->east_wall != -1 || cell->east_door != -1);
					bool neighbor_is_vertical = (neighbor_n->west_wall != -1 || neighbor_n->west_door != -1) &&
								    (neighbor_n->east_wall != -1 || neighbor_n->east_door != -1);

					if (cell_is_vertical && neighbor_is_vertical) {
						cell->north_door = -1;
						neighbor_n->south_door = -1;
					}
				}
			}
		}
	}
}

// Applies special floor and wall textures to treasure rooms.
static void format_treasure_rooms(struct drawfield_generator *dg)
{
	for (int cy = 0; cy < dg->field_size_y; cy++) {
		for (int cx = 0; cx < dg->field_size_x; cx++) {
			if (!(FLAG(dg, cx, cy) & DF_FLAG_TREASURE_ROOM))
				continue;
			for (int y = cy - 1; y <= cy + 1; y++) {
				for (int x = cx - 1; x <= cx + 1; x++) {
					struct dgn_cell *cell = dgn_cell_at(dg->dgn, x, dg->floor, y);
					if (cell->floor == -1)
						continue;
					cell->floor = 8;
					FLAG(dg, x, y) |= DF_FLAG_ITEM;
					if (cell->north_wall != -1 && cell->north_door == -1) {
						cell->north_wall = WALL_TREASURE_ROOM;
					}
					if (cell->south_wall != -1 && cell->south_door == -1) {
						cell->south_wall = WALL_TREASURE_ROOM;
					}
					if (cell->east_wall != -1 && cell->east_door == -1) {
						cell->east_wall = WALL_TREASURE_ROOM;
					}
					if (cell->west_wall != -1 && cell->west_door == -1) {
						cell->west_wall = WALL_TREASURE_ROOM;
					}
				}
			}
		}
	}
}

// Randomly locks a specified percentage of doors.
// Note that every door is checked twice (once from each side). As a result,
// the actual probability of a door being locked is higher than the specified
// door_lock_percent.
static void randomly_lock_doors(struct drawfield_generator *dg, int door_lock_percent)
{
	if (door_lock_percent <= 0)
		return;

	for (int y = 0; y < dg->field_size_y; y++) {
		for (int x = 0; x < dg->field_size_x; x++) {
			struct dgn_cell *cell = dgn_cell_at(dg->dgn, x, dg->floor, y);

			// North
			if (rand_between(dg, 0, 99) < door_lock_percent && cell->north_door != -1) {
				cell->north_door_lock = 1;
				dgn_cell_at(dg->dgn, x, dg->floor, y + 1)->south_door_lock = 1;
			}

			// West
			if (rand_between(dg, 0, 99) < door_lock_percent && cell->west_door != -1) {
				cell->west_door_lock = 1;
				dgn_cell_at(dg->dgn, x - 1, dg->floor, y)->east_door_lock = 1;
			}

			// South
			if (rand_between(dg, 0, 99) < door_lock_percent && cell->south_door != -1) {
				cell->south_door_lock = 1;
				dgn_cell_at(dg->dgn, x, dg->floor, y - 1)->north_door_lock = 1;
			}

			// East
			if (rand_between(dg, 0, 99) < door_lock_percent && cell->east_door != -1) {
				cell->east_door_lock = 1;
				dgn_cell_at(dg->dgn, x + 1, dg->floor, y)->west_door_lock = 1;
			}
		}
	}
}

static void create_upward_stairs(
	struct drawfield_generator *dg, int x, int y,
	int stairs_texture, int orientation)
{
	struct dgn_cell *cell = dgn_cell_at(dg->dgn, x, dg->floor, y);
	cell->stairs_texture = stairs_texture;
	cell->stairs_orientation = orientation;

	switch (orientation) {
	case DIR_NORTH: // Entrance from South
		dg->dgn->start_x = x;
		dg->dgn->start_y = y - 1;
		if (cell->east_wall == -1 && cell->east_door == -1) {
			cell->east_wall = WALL_RIGHT_UP_STAIRS;
			dgn_cell_at(dg->dgn, x + 1, dg->floor, y)->west_wall = WALL_LEFT_UP_STAIRS;
		}
		if (cell->west_wall == -1 && cell->west_door == -1) {
			cell->west_wall = WALL_LEFT_UP_STAIRS;
			dgn_cell_at(dg->dgn, x - 1, dg->floor, y)->east_wall = WALL_RIGHT_UP_STAIRS;
		}
		if (cell->north_wall == -1 && cell->north_door == -1) {
			cell->north_wall = WALL_BACK_UP_STAIRS;
			dgn_cell_at(dg->dgn, x, dg->floor, y + 1)->south_wall = WALL_BACK_UP_STAIRS;
		}
		break;
	case DIR_WEST: // Entrance from East
		dg->dgn->start_x = x + 1;
		dg->dgn->start_y = y;
		if (cell->north_wall == -1 && cell->north_door == -1) {
			cell->north_wall = WALL_RIGHT_UP_STAIRS;
			dgn_cell_at(dg->dgn, x, dg->floor, y + 1)->south_wall = WALL_LEFT_UP_STAIRS;
		}
		if (cell->south_wall == -1 && cell->south_door == -1) {
			cell->south_wall = WALL_LEFT_UP_STAIRS;
			dgn_cell_at(dg->dgn, x, dg->floor, y - 1)->north_wall = WALL_RIGHT_UP_STAIRS;
		}
		if (cell->west_wall == -1 && cell->west_door == -1) {
			cell->west_wall = WALL_BACK_UP_STAIRS;
			dgn_cell_at(dg->dgn, x - 1, dg->floor, y)->east_wall = WALL_BACK_UP_STAIRS;
		}
		break;
	case DIR_SOUTH: // Entrance from North
		dg->dgn->start_x = x;
		dg->dgn->start_y = y + 1;
		if (cell->east_wall == -1 && cell->east_door == -1) {
			cell->east_wall = WALL_LEFT_UP_STAIRS;
			dgn_cell_at(dg->dgn, x + 1, dg->floor, y)->west_wall = WALL_RIGHT_UP_STAIRS;
		}
		if (cell->west_wall == -1 && cell->west_door == -1) {
			cell->west_wall = WALL_RIGHT_UP_STAIRS;
			dgn_cell_at(dg->dgn, x - 1, dg->floor, y)->east_wall = WALL_LEFT_UP_STAIRS;
		}
		if (cell->south_wall == -1 && cell->south_door == -1) {
			cell->south_wall = WALL_BACK_UP_STAIRS;
			dgn_cell_at(dg->dgn, x, dg->floor, y - 1)->north_wall = WALL_BACK_UP_STAIRS;
		}
		break;
	case DIR_EAST: // Entrance from West
		dg->dgn->start_x = x - 1;
		dg->dgn->start_y = y;
		if (cell->north_wall == -1 && cell->north_door == -1) {
			cell->north_wall = WALL_LEFT_UP_STAIRS;
			dgn_cell_at(dg->dgn, x, dg->floor, y + 1)->south_wall = WALL_RIGHT_UP_STAIRS;
		}
		if (cell->south_wall == -1 && cell->south_door == -1) {
			cell->south_wall = WALL_RIGHT_UP_STAIRS;
			dgn_cell_at(dg->dgn, x, dg->floor, y - 1)->north_wall = WALL_LEFT_UP_STAIRS;
		}
		if (cell->east_wall == -1 && cell->east_door == -1) {
			cell->east_wall = WALL_BACK_UP_STAIRS;
			dgn_cell_at(dg->dgn, x + 1, dg->floor, y)->west_wall = WALL_BACK_UP_STAIRS;
		}
		break;
	}
}

static void create_downward_stairs(
	struct drawfield_generator *dg, int x, int y,
	int stairs_texture, int orientation)
{
	int lower_floor = dg->floor - 1;
	struct dgn_cell *cell = dgn_cell_at(dg->dgn, x, dg->floor, y);
	struct dgn_cell *lower_cell = dgn_cell_at(dg->dgn, x, lower_floor, y);
	lower_cell->stairs_texture = stairs_texture;
	switch (orientation) {
	case 0: lower_cell->stairs_orientation = 2; break;
	case 1: lower_cell->stairs_orientation = 3; break;
	case 2: lower_cell->stairs_orientation = 0; break;
	case 3: lower_cell->stairs_orientation = 1; break;
	}

	cell->floor = -1;

	switch (orientation) {
	case DIR_NORTH: // Entrance from South
		if (cell->east_wall == -1 && cell->east_door == -1) {
			cell->east_wall = WALL_AROUND_DOWN_STAIRS;
			dgn_cell_at(dg->dgn, x + 1, dg->floor, y)->west_wall = WALL_AROUND_DOWN_STAIRS;
		}
		if (cell->west_wall == -1 && cell->west_door == -1) {
			cell->west_wall = WALL_AROUND_DOWN_STAIRS;
			dgn_cell_at(dg->dgn, x - 1, dg->floor, y)->east_wall = WALL_AROUND_DOWN_STAIRS;
		}
		if (cell->north_wall == -1 && cell->north_door == -1) {
			cell->north_wall = WALL_AROUND_DOWN_STAIRS;
			dgn_cell_at(dg->dgn, x, dg->floor, y + 1)->south_wall = WALL_AROUND_DOWN_STAIRS;
		}
		break;
	case DIR_WEST: // Entrance from East
		if (cell->north_wall == -1 && cell->north_door == -1) {
			cell->north_wall = WALL_AROUND_DOWN_STAIRS;
			dgn_cell_at(dg->dgn, x, dg->floor, y + 1)->south_wall = WALL_AROUND_DOWN_STAIRS;
		}
		if (cell->south_wall == -1 && cell->south_door == -1) {
			cell->south_wall = WALL_AROUND_DOWN_STAIRS;
			dgn_cell_at(dg->dgn, x, dg->floor, y - 1)->north_wall = WALL_AROUND_DOWN_STAIRS;
		}
		if (cell->west_wall == -1 && cell->west_door == -1) {
			cell->west_wall = WALL_AROUND_DOWN_STAIRS;
			dgn_cell_at(dg->dgn, x - 1, dg->floor, y)->east_wall = WALL_AROUND_DOWN_STAIRS;
		}
		break;
	case DIR_SOUTH: // Entrance from North
		if (cell->east_wall == -1 && cell->east_door == -1) {
			cell->east_wall = WALL_AROUND_DOWN_STAIRS;
			dgn_cell_at(dg->dgn, x + 1, dg->floor, y)->west_wall = WALL_AROUND_DOWN_STAIRS;
		}
		if (cell->west_wall == -1 && cell->west_door == -1) {
			cell->west_wall = WALL_AROUND_DOWN_STAIRS;
			dgn_cell_at(dg->dgn, x - 1, dg->floor, y)->east_wall = WALL_AROUND_DOWN_STAIRS;
		}
		if (cell->south_wall == -1 && cell->south_door == -1) {
			cell->south_wall = WALL_AROUND_DOWN_STAIRS;
			dgn_cell_at(dg->dgn, x, dg->floor, y - 1)->north_wall = WALL_AROUND_DOWN_STAIRS;
		}
		break;
	case DIR_EAST: // Entrance from West
		if (cell->north_wall == -1 && cell->north_door == -1) {
			cell->north_wall = WALL_AROUND_DOWN_STAIRS;
			dgn_cell_at(dg->dgn, x, dg->floor, y + 1)->south_wall = WALL_AROUND_DOWN_STAIRS;
		}
		if (cell->south_wall == -1 && cell->south_door == -1) {
			cell->south_wall = WALL_AROUND_DOWN_STAIRS;
			dgn_cell_at(dg->dgn, x, dg->floor, y - 1)->north_wall = WALL_AROUND_DOWN_STAIRS;
		}
		if (cell->east_wall == -1 && cell->east_door == -1) {
			cell->east_wall = WALL_AROUND_DOWN_STAIRS;
			dgn_cell_at(dg->dgn, x + 1, dg->floor, y)->west_wall = WALL_AROUND_DOWN_STAIRS;
		}
		break;
	}

	dgn_cell_at(dg->dgn, x, lower_floor, y)->north_wall = WALL_OUTER;
	dgn_cell_at(dg->dgn, x, lower_floor, y + 1)->south_wall = WALL_OUTER;
	dgn_cell_at(dg->dgn, x, lower_floor, y)->west_wall = WALL_OUTER;
	dgn_cell_at(dg->dgn, x - 1, lower_floor, y)->east_wall = WALL_OUTER;
	dgn_cell_at(dg->dgn, x, lower_floor, y)->south_wall = WALL_OUTER;
	dgn_cell_at(dg->dgn, x, lower_floor, y - 1)->north_wall = WALL_OUTER;
	dgn_cell_at(dg->dgn, x, lower_floor, y)->east_wall = WALL_OUTER;
	dgn_cell_at(dg->dgn, x + 1, lower_floor, y)->west_wall = WALL_OUTER;
}

static bool is_suitable_for_exit(struct drawfield_generator *dg, int x, int y)
{
	struct dgn_cell *cell = dgn_cell_at(dg->dgn, x, dg->floor, y);

	// Must have a floor.
	if (cell->floor == -1) {
		return false;
	}

	// Must not be next to a 1-tile corridor.
	struct dgn_cell *north_neighbor = dgn_cell_at(dg->dgn, x, dg->floor, y + 1);
	if (north_neighbor->floor != -1 && north_neighbor->east_wall != -1 && north_neighbor->west_wall != -1) {
		return false;
	}
	struct dgn_cell *west_neighbor = dgn_cell_at(dg->dgn, x - 1, dg->floor, y);
	if (west_neighbor->floor != -1 && west_neighbor->north_wall != -1 && west_neighbor->south_wall != -1) {
		return false;
	}
	struct dgn_cell *south_neighbor = dgn_cell_at(dg->dgn, x, dg->floor, y - 1);
	if (south_neighbor->floor != -1 && south_neighbor->east_wall != -1 && south_neighbor->west_wall != -1) {
		return false;
	}
	struct dgn_cell *east_neighbor = dgn_cell_at(dg->dgn, x + 1, dg->floor, y);
	if (east_neighbor->floor != -1 && east_neighbor->north_wall != -1 && east_neighbor->south_wall != -1) {
		return false;
	}

	// Must not have doors.
	if (cell->north_door != -1 || cell->west_door != -1 || cell->south_door != -1 || cell->east_door != -1) {
		return false;
	}

	// Must not be adjacent to existing stairs.
	for (int yy = y - 1; yy <= y + 1; yy++) {
		for (int xx = x - 1; xx <= x + 1; xx++) {
			if (dgn_cell_at(dg->dgn, xx, dg->floor, yy)->stairs_texture != -1) {
				return false;
			}
		}
	}

	// Must not be in a 1-tile tunnel.
	bool north_wall = cell->north_wall != -1;
	bool south_wall = cell->south_wall != -1;
	bool east_wall = cell->east_wall != -1;
	bool west_wall = cell->west_wall != -1;

	if (west_wall && east_wall && !north_wall && !south_wall) {
		return false;
	}
	if (north_wall && south_wall && !west_wall && !east_wall) {
		return false;
	}

	return true;
}

static bool create_dungeon_exit(struct drawfield_generator *dg)
{
	Point *candidates = xcalloc(dg->field_size_y * dg->field_size_x, sizeof(Point));
	int nr_candidates = 0;

	for (int y = 1; y < dg->field_size_y - 1; y++) {
		for (int x = 1; x < dg->field_size_x - 1; x++) {
			if (is_suitable_for_exit(dg, x, y)) {
				candidates[nr_candidates++] = (Point){x, y};
			}
		}
	}

	if (nr_candidates == 0) {
		free(candidates);
		return false;
	}

	Point exit_pos = candidates[rand_between(dg, 0, nr_candidates - 1)];
	free(candidates);

	struct dgn_cell *cell = dgn_cell_at(dg->dgn, exit_pos.x, dg->floor, exit_pos.y);

	int orientations[4];
	int nr_orientations = 0;

	// Check for 3-tile wide opening to the North
	if (cell->north_wall == -1 &&
	    dgn_cell_at(dg->dgn, exit_pos.x - 1, dg->floor, exit_pos.y)->north_wall == -1 &&
	    dgn_cell_at(dg->dgn, exit_pos.x + 1, dg->floor, exit_pos.y)->north_wall == -1) {
		orientations[nr_orientations++] = DIR_SOUTH;
	}
	// Check for 3-tile wide opening to the West
	if (cell->west_wall == -1 &&
	    dgn_cell_at(dg->dgn, exit_pos.x, dg->floor, exit_pos.y - 1)->west_wall == -1 &&
	    dgn_cell_at(dg->dgn, exit_pos.x, dg->floor, exit_pos.y + 1)->west_wall == -1) {
		orientations[nr_orientations++] = DIR_EAST;
	}
	// Check for 3-tile wide opening to the South
	if (cell->south_wall == -1 &&
	    dgn_cell_at(dg->dgn, exit_pos.x - 1, dg->floor, exit_pos.y)->south_wall == -1 &&
	    dgn_cell_at(dg->dgn, exit_pos.x + 1, dg->floor, exit_pos.y)->south_wall == -1) {
		orientations[nr_orientations++] = DIR_NORTH;
	}
	// Check for 3-tile wide opening to the East
	if (cell->east_wall == -1 &&
	    dgn_cell_at(dg->dgn, exit_pos.x, dg->floor, exit_pos.y - 1)->east_wall == -1 &&
	    dgn_cell_at(dg->dgn, exit_pos.x, dg->floor, exit_pos.y + 1)->east_wall == -1) {
		orientations[nr_orientations++] = DIR_WEST;
	}

	if (nr_orientations == 0) {
		return false;
	}

	int chosen_orientation = orientations[rand_between(dg, 0, nr_orientations - 1)];

	if (cell->east_wall != -1) {
		cell->east_wall = 12;
	}
	if (cell->north_wall != -1) {
		cell->north_wall = 12;
	}

	create_downward_stairs(dg, exit_pos.x, exit_pos.y, 1, chosen_orientation);
	dg->dgn->exit_x = exit_pos.x;
	dg->dgn->exit_y = exit_pos.y;

	return true;
}

static bool generate_dungeon_layout(struct drawfield_generator *dg, int door_lock_percent)
{
	// Determine the entrance coordinates, randomly placed near the center of the dungeon.
	int entry_x = rand_between(dg, 3, dg->field_size_x - 4);
	int entry_y = rand_between(dg, 3, dg->field_size_y - 4);
	// Create the initial 3x3 room centered on the entrance coordinates.
	dg->current_complexity++;
	dg->current_room_id++;
	for (int y = entry_y - 1; y <= entry_y + 1; y++) {
		for (int x = entry_x - 1; x <= entry_x + 1; x++) {
			FLOOR(dg, x, y) = dg->current_room_id;
		}
	}

	// Attempt to expand in two randomly chosen directions out of four.
	struct direction_pool dp = DIRECTION_POOL_INIT;
	for (int i = 0; i < 2; i++) {
		enum direction dir = direction_pool_pick(&dp, dg);
		int next_x = entry_x;
		int next_y = entry_y;
		switch (dir) {
		case DIR_NORTH: next_y++; break;
		case DIR_WEST:  next_x--; break;
		case DIR_SOUTH: next_y--; break;
		case DIR_EAST:  next_x++; break;
		}
		expand_room(dg, next_x, next_y, dir);
	}

	cleanup_floor_markers(dg);
	prune_and_extend_corridors(dg);
	create_treasure_rooms(dg);

	if (dg->wall_arrange_method == 1)
		generate_walls(dg);
	else
		VM_ERROR("unsupported wall arrangement method: %d", dg->wall_arrange_method);
	remove_internal_walls(dg);
	if (dg->floor_arrange_method == 1)
		randomize_floor_texture(dg);
	else
		VM_ERROR("unsupported floor arrangement method: %d", dg->floor_arrange_method);
	resolve_dead_ends(dg);
	consolidate_passages(dg);
	format_treasure_rooms(dg);
	randomly_lock_doors(dg, door_lock_percent);

	// Finalize the starting area: place the entrance stairs and prevent object spawns.
	for (int y = entry_y - 1; y <= entry_y + 1; y++) {
		for (int x = entry_x - 1; x <= entry_x + 1; x++) {
			FLAG(dg, x, y) |= DF_FLAG_NO_TREASURE | DF_FLAG_NO_MONSTER | DF_FLAG_NO_TRAP;
		}
	}
	FLOOR(dg, entry_x, entry_y) = 3;
	int stairs_orientation = rand_between(dg, 0, 3);
	create_upward_stairs(dg, entry_x, entry_y, 1, stairs_orientation);

	// Create the dungeon exit.
	if (!create_dungeon_exit(dg)) {
		return false;
	}
	// To prevent the downward stairs from being visible from the side, set
	// dark floor tiles in the empty space around the exit.
	for (int y = dg->dgn->exit_y - 1; y <= dg->dgn->exit_y + 1; y++) {
		for (int x = dg->dgn->exit_x - 1; x <= dg->dgn->exit_x + 1; x++) {
			if ((x == dg->dgn->exit_x && y == dg->dgn->exit_y) || FLOOR(dg, x, y) != -1)
				continue;
			FLOOR(dg, x, y) = 3;
			FLAG(dg, x, y) |= DF_FLAG_NO_TREASURE | DF_FLAG_NO_MONSTER | DF_FLAG_NO_TRAP;
		}
	}

	// Ensure no objects spawn directly on the start or exit locations.
	FLAG(dg, dg->dgn->start_x, dg->dgn->start_y) &= ~(DF_FLAG_ITEM | DF_FLAG_ENEMY | DF_FLAG_TRAP);
	FLAG(dg, dg->dgn->exit_x, dg->dgn->exit_y) &= ~(DF_FLAG_ITEM | DF_FLAG_ENEMY | DF_FLAG_TRAP);
	// Final cleanup: ensure no objects are flagged to spawn in empty space.
	for (int y = 0; y < dg->field_size_y; y++) {
		for (int x = 0; x < dg->field_size_x; x++) {
			if (FLOOR(dg, x, y) == -1) {
				FLAG(dg, x, y) &= ~(DF_FLAG_ITEM | DF_FLAG_ENEMY | DF_FLAG_TRAP);
			}
		}
	}
	return dg->current_complexity >= min(dg->target_complexity, 35);
}

struct dgn *dgn_generate_drawfield(
	int floor, int complexity, int wall_arrange_method, int floor_arrange_method,
	int field_size_x, int field_size_y, int door_lock_percent, uint32_t seed,
	uint8_t *cell_flags)
{
	struct mt19937 rand;
	mt19937_init(&rand, seed | 1);

	while (1) {
		struct drawfield_generator dg = {
			.dgn = dgn_new(field_size_x, 3, field_size_y),
			.flags = cell_flags,
			.wall_arrange_method = wall_arrange_method,
			.floor_arrange_method = floor_arrange_method,
			.current_room_id = 0,
			.current_complexity = 0,
			.target_complexity = complexity + 5,
			.rand = &rand,
			.floor = floor,
			.field_size_x = field_size_x,
			.field_size_y = field_size_y,
		};
		if (generate_dungeon_layout(&dg, door_lock_percent)) {
			return dg.dgn;
		}
		dgn_free(dg.dgn);
	}
}
