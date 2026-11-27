/* Copyright (C) 2023 kichikuou <KichikuouChrome@gmail.com>
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

#ifndef SYSTEM4_MOVIE_H
#define SYSTEM4_MOVIE_H

#include <stdbool.h>
#include <stdint.h>

struct movie_context;
struct sact_sprite;

struct movie_context *movie_load(const char *filename);
struct movie_context *movie_load_apeg(const char *filename, int sound_id, int sound_group);
void movie_free(struct movie_context *mc);
bool movie_play(struct movie_context *mc);
bool movie_play_at(struct movie_context *mc, int time_ms);
bool movie_set_time(struct movie_context *mc, int time_ms);
bool movie_draw(struct movie_context *mc, struct sact_sprite *sprite);
bool movie_is_end(struct movie_context *mc);
int movie_get_position(struct movie_context *mc);
int movie_get_end_time(struct movie_context *mc);
int movie_get_width(struct movie_context *mc);
int movie_get_height(struct movie_context *mc);
bool movie_set_volume(struct movie_context *mc, int volume /* 0-100 */);
bool movie_has_active_playback(void);
uint32_t movie_get_playback_seq(void);
bool movie_skip_active(void);
void movie_end_session(void);

#endif /* SYSTEM4_MOVIE_H */
