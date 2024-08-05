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

/*

+---------------------+
| .                   |
|   .                 |
|     .               |
|       .             |
|         .           |
|           .         |
|          p2 +-------+ p4
|             | .   ,'|
|             |   ,'  |
|             | ,'  . |
+-------------+'------+
           p3           p1

The triangle between p2-p3-p4 contains the old scene mirrored.
The triangle between p1-p3-p4 contains the new scene.
The rest contains the old scene.

p2 moves towards the top-left along the line between p1 and the origin.

*/

void main() {
	vec2 p = tex_coord * resolution;

	vec2 p1 = resolution;
	vec2 p2 = vec2((1.0 - max(progress, 0.01)*2.0) * p1.x, (1.0 - max(progress, 0.01)*2.0) * p1.y);
	vec2 p3 = vec2(p2.x, resolution.y);
	vec2 p4 = vec2(resolution.x, p2.y);

	// 1.0 if p is below the line between p3 and p4, else 0.0
	float b_below_line = step(0.0, (p4.x - p3.x)*(p.y - p3.y) - (p4.y - p3.y)*(p.x - p3.x));

	// bool b_old = p.x < p2.x || p.y < p2.y;
	float b_old = step(0.5, step(p.x, p2.x) + step(p.y, p2.y));
	// bool b_mirror = !b_old && !b_below_line;
	float b_mirror = abs(1.0 - b_old) * abs(1.0 - b_below_line);
	// bool b_new = b_below_line;
	float b_new = b_below_line;

	vec2 p_diff = p - p2;
	vec3 c_old = texture(old, tex_coord).rgb;
	vec3 c_mirror = texture(old, vec2(p1.x - p_diff.x, p1.y - p_diff.y)/resolution).rgb;
	vec3 c_new = texture(tex, tex_coord).rgb;

	vec3 col = c_old * b_old + c_mirror * b_mirror + c_new * b_new;
	frag_color = vec4(col, 1.0);
}
