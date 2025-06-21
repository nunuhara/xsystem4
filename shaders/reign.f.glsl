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

#define DIFFUSE_EMISSIVE 0
#define DIFFUSE_NORMAL 1
#define DIFFUSE_LIGHT_MAP 2
#define DIFFUSE_ENV_MAP 3

#define ALPHA_BLEND 0
#define ALPHA_TEST 1
#define ALPHA_MAP_BLEND 2
#define ALPHA_MAP_TEST 3

#if ENGINE == REIGN_ENGINE

#define NR_DIR_LIGHTS 3
#define FOG_LINEAR 1
#define FOG_LIGHT_SCATTERING 2

struct dir_light {
	vec3 dir;
	vec3 diffuse;
	vec3 globe_diffuse;
};

uniform sampler2D specular_texture;
uniform dir_light dir_lights[NR_DIR_LIGHTS];
uniform float specular_strength;
uniform float specular_shininess;
uniform bool use_specular_map;
uniform float rim_exponent;
uniform vec3 rim_color;
uniform int fog_type;
uniform float fog_near;
uniform float fog_far;
uniform vec3 fog_color;
in vec3 light_dir[NR_DIR_LIGHTS];
in vec3 specular_dir;
in vec3 ls_ex;
in vec3 ls_in;

uniform vec3 global_ambient;

vec3 dir_lights_diffuse(vec3 norm)
{
	vec3 diffuse = vec3(0.0);
	for (int i = 0; i < NR_DIR_LIGHTS; i++) {
		float half_lambert = dot(norm, -light_dir[i]) * 0.5 + 0.5;
		diffuse += mix(dir_lights[i].globe_diffuse, dir_lights[i].diffuse, half_lambert);
	}
	return diffuse;
}

#else // ENGINE == REIGN_ENGINE

const vec3 global_ambient = vec3(0.0);

vec3 dir_lights_diffuse(vec3 norm)
{
	return vec3(1.0);
}

#endif // ENGINE == REIGN_ENGINE

uniform sampler2D tex;
uniform sampler2D alpha_texture;
uniform sampler2D light_texture;
uniform sampler2D shadow_texture;
uniform float alpha_mod;

uniform bool use_normal_map;
uniform sampler2D normal_texture;

uniform vec3 instance_ambient;
uniform int diffuse_type;
uniform float shadow_darkness;
uniform float shadow_bias;
uniform int alpha_mode;

in vec2 tex_coord;
in vec2 light_tex_coord;
in vec4 color_mod;
in vec3 frag_pos;
in vec4 shadow_frag_pos;
in float dist;
in vec3 eye;
in vec3 normal;
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
	vec3 frag_rgb = global_ambient + instance_ambient;

	// Diffuse lighting
	vec4 texel;
	if (diffuse_type == DIFFUSE_ENV_MAP) {
		texel = texture(tex, (vec2(normal.x, -normal.y) + 1.0) / 2.0);
		frag_rgb = texel.rgb;
	} else {
		texel = texture(tex, tex_coord);
		if (diffuse_type == DIFFUSE_EMISSIVE) {
			frag_rgb = texel.rgb;
		} else {
			vec3 diffuse = dir_lights_diffuse(norm);
			if (diffuse_type == DIFFUSE_LIGHT_MAP) {
				diffuse *= texture(light_texture, light_tex_coord).rgb;
			}
			frag_rgb += texel.rgb * diffuse * color_mod.rgb;
		}
	}

#if ENGINE == REIGN_ENGINE
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
		vec3 n = use_normal_map ? vec3(0.0, 0.0, 1.0) : normalize(normal);
		frag_rgb += pow(1.0 - max(dot(n, view_dir), 0.0), rim_exponent) * rim_color;
	}

	// Fog
	if (fog_type == FOG_LINEAR) {
		float fog_factor = clamp((dist - fog_near) / (fog_far - fog_near), 0.0, 1.0);
		frag_rgb = mix(frag_rgb, fog_color, fog_factor);
	} else if (fog_type == FOG_LIGHT_SCATTERING) {
		frag_rgb = frag_rgb * ls_ex + ls_in;
	}
#endif

	// Shadow mapping
	if (shadow_darkness > 0.0) {
		vec3 shadow_coords = shadow_frag_pos.xyz / shadow_frag_pos.w * 0.5 + 0.5;
		shadow_coords.z = min(1.0, shadow_coords.z - shadow_bias);
		// PCF with 9 samples.
		float brightness = 1.0;
		vec2 texel_size = 1.0 / vec2(textureSize(shadow_texture, 0));
		for (int x = -1; x <= 1; ++x) {
			for (int y = -1; y <= 1; ++y) {
				float depth = texture(shadow_texture, shadow_coords.xy + vec2(x, y) * texel_size).r;
				if (depth < shadow_coords.z)
					brightness -= 0.05 * shadow_darkness;
			}
		}
		frag_rgb *= brightness;
	}

	// Alpha mapping
	float alpha = texel.a * color_mod.a * alpha_mod;
	if (alpha_mode == ALPHA_TEST) {
		if (alpha < 0.75)
			discard;
	} else if (alpha_mode == ALPHA_MAP_BLEND) {
		alpha *= texture(alpha_texture, tex_coord).r;
	} else if (alpha_mode == ALPHA_MAP_TEST) {
		if (texture(alpha_texture, tex_coord).r < 0.1)
			discard;
	}

	frag_color = vec4(frag_rgb, alpha);
}
