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

#ifndef SYSTEM4_HLL_VMSURFACE_H
#define SYSTEM4_HLL_VMSURFACE_H

#include "gfx/gfx.h"

struct vm_surface {
	struct texture texture;
	int no;
	int w;
	int h;
	bool has_pixel;
	bool has_alpha;
	int cg_no;
};

struct vm_surface *vm_surface_create(int width, int height);
struct vm_surface *vm_surface_get(int no);
void vm_surface_delete(int no);
int vm_surface_get_main_surface(void);

#endif /* SYSTEM4_HLL_VMSURFACE_H */
