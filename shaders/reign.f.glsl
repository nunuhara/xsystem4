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

#define NR_DIR_LIGHTS 3

struct dir_light {
	vec3 dir;
	vec3 diffuse;
	vec3 globe_diffuse;
};

uniform sampler2D tex;
uniform sampler2D specular_texture;
uniform sampler2D light_texture;
uniform float alpha_mod;

uniform bool use_normal_map;
uniform sampler2D normal_texture;

uniform vec3 ambient;
uniform dir_light dir_lights[NR_DIR_LIGHTS];
uniform float specular_strength;
uniform float specular_shininess;
uniform bool use_specular_map;
uniform float rim_exponent;
uniform vec3 rim_color;
uniform bool use_light_map;

in vec2 tex_coord;
in vec2 light_tex_coord;
in vec3 frag_pos;
in vec3 eye;
in vec3 normal;
in vec3 light_dir[NR_DIR_LIGHTS];
in vec3 specular_dir;
out vec4 frag_color;

void main() {
	vec3 norm;
	if (use_normal_map) {
		// NOTE: TT3's shader doesn't normalize this vector.
		norm = texture(normal_texture, tex_coord).rgb * 2.0 - 1.0;
	} else {
		norm = normalize(normal);
	}

	vec3 view_dir = normalize(eye - frag_pos);
	vec3 frag_rgb = ambient;

	// Diffuse lighting
	vec3 diffuse = vec3(0.0);
	for (int i = 0; i < NR_DIR_LIGHTS; i++) {
		float half_lambert = dot(norm, -light_dir[i]) * 0.5 + 0.5;
		diffuse += mix(dir_lights[i].globe_diffuse, dir_lights[i].diffuse, half_lambert);
	}
	if (use_light_map) {
		diffuse *= texture(light_texture, light_tex_coord).rgb;
	}
	vec4 texel = texture(tex, tex_coord);
	frag_rgb += texel.rgb * diffuse;

	// Specular lighting
	if (specular_strength > 0.0) {
		vec3 reflect_dir = reflect(specular_dir, norm);
		float specular = pow(max(dot(view_dir, reflect_dir), 0.0), specular_shininess) * specular_strength;
		if (use_specular_map) {
			specular *= texture(specular_texture, tex_coord).r;
		}
		frag_rgb += vec3(specular);
	}

	// Rim lighting
	if (rim_exponent > 0.0) {
		// Normal map is not used here.
		frag_rgb += pow(1.0 - max(dot(normalize(normal), view_dir), 0.0), rim_exponent) * rim_color;
	}

	frag_color = vec4(frag_rgb, texel.a * alpha_mod);
}
