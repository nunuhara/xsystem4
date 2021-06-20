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

#define MAX_STRIDE 128.0

vec3 get_pixel(vec2 xy) {
        return mix(texture(old, xy).rgb, texture(tex, xy).rgb, progress);
}

void main() {
        // size of each chunk
        float stride = MAX_STRIDE * (1.0 - abs(progress * 2.0 - 1.0)); // progress=(0..0.5..1) -> stride=(0..max..0)
        // offset of frag coordinate relative to chunk
        vec2 offset = mod(gl_FragCoord.xy, stride);
        // the chunk coordinate
        vec2 chunk = tex_coord - offset / resolution;
        // coordinate of chunk center
        vec2 center = chunk + vec2(stride/2.0, stride/2.0) / resolution;

        // FIXME: In this implementation, the anchor point is at the top left of the screen.
        //        It should be anchored at the center to match SACT2.dll.
        frag_color = vec4(get_pixel(center), 1.0);
}

/* NOTE: This is an alternate implementation which computes an average for each chunk.
         SACT2.dll does not do this.

#define SAMPLES 8.0
#define MAX_STRIDE 16.0

vec3 get_pixel(vec2 xy) {
        return mix(texture(old, xy).xyz, texture(tex, xy).xyz, progress);
}

void main() {
        float stride = MAX_STRIDE * (1.0 - abs(progress * 2.0 - 1.0)); // progress=(0..0.5..1) -> stride=(0..max..0)
        vec3 avg = vec3(0, 0, 0);
        vec2 xy = floor(gl_FragCoord.xy / (SAMPLES * stride)) * (SAMPLES * stride);
        for (float j = 0.0; j < SAMPLES; j += 1.0) {
                for (float i = 0.0; i < SAMPLES; i+= 1.0) {
                        avg += get_pixel((xy + stride * vec2(i, j)) / resolution);
                }
        }
        avg /= SAMPLES * SAMPLES;
        frag_color = vec4(avg, 1.0);
}
*/
