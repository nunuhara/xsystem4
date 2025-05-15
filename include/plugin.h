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

#ifndef SYSTEM4_PLUGIN_H
#define SYSTEM4_PLUGIN_H

#include <stdbool.h>

typedef struct cJSON cJSON;
struct sact_sprite;

struct draw_plugin {
	const char *name;
	void (*free)(struct draw_plugin *);
	void (*update)(struct sact_sprite *);
	void (*render)(struct sact_sprite *);
	cJSON *(*to_json)(struct sact_sprite *, bool);
};

#endif /* SYSTEM4_PLUGIN_H */
