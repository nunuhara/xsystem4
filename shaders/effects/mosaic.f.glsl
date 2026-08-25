/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

uniform sampler2D tex;   // the new scene
uniform sampler2D old;   // the old scene
uniform vec2 resolution; // the screen resolution
uniform float progress;  // effect progress (0..1)

in vec2 tex_coord;
out vec4 frag_color;

#define MAX_CHUNK 128.0

vec3 get_pixel(vec2 xy) {
        return mix(texture(old, xy).rgb, texture(tex, xy).rgb, progress);
}

float triangle(float f) {
        return 1.0 - abs(f * 2.0 - 1.0);
}

void main() {
	// center of the screen
	vec2 center = resolution * 0.5;
	// size of each chunk
	float chunk_size = floor(triangle(progress) * MAX_CHUNK);
	// number of chunks in each screen quadrant (including partial chunks at edges)
	vec2 chunks_per_quadrant = floor(center / chunk_size);
	// offset from screen to edge of grid (negative or zero)
	vec2 grid_off = center - (chunks_per_quadrant * chunk_size);
	// offset of pixel in chunk
	vec2 chunk_off = mod(gl_FragCoord.xy - grid_off, chunk_size);
	// the chunk coordinate
	vec2 chunk = tex_coord - vec2(chunk_off) / resolution;
	// coordinate of chunk center
	vec2 chunk_center = chunk + vec2(chunk_size/2.0, chunk_size/2.0) / resolution;
	frag_color = vec4(get_pixel(chunk_center), 1.0);
}
