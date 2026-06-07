/* Copyright (C) 2026 kichikuou <KichikuouChrome@gmail.com>
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

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cglm/cglm.h>

#include "system4.h"
#include "system4/aar.h"
#include "system4/cg.h"
#include "system4/hashtable.h"
#include "system4/utfsjis.h"

#include "3d_internal.h"
#include "audio.h"

static void parse_error(const char *msg, char *input)
{
	char *eol = strchr(input, '\n');
	if (eol) *eol = '\0';
	WARNING("Parse error: %s Near: \"%s\"", msg, input);
}

static enum s3de_object_type parse_object_type(const char *s)
{
	if (!strcmp(s, "ビルボード"))
		return S3DE_OBJ_BILLBOARD;
	if (!strcmp(s, "ポリゴンオブジェクト"))
		return S3DE_OBJ_POLYGON;
	if (!strcmp(s, "カメラ"))
		return S3DE_OBJ_CAMERA;
	WARNING("unknown object type \"%s\"", s);
	return (enum s3de_object_type)-1;
}

static enum s3de_move_type parse_move_type(const char *s)
{
	if (!strcmp(s, "直線"))
		return S3DE_MOVE_LINEAR;
	if (!strcmp(s, "ベジェ"))
		return S3DE_MOVE_BEZIER;
	WARNING("unknown move type \"%s\"", s);
	return (enum s3de_move_type)-1;
}

static enum s3de_spawn_position_type parse_spawn_position_type(const char *s)
{
	if (!strcmp(s, "標準"))
		return S3DE_SPAWN_NORMAL;
	if (!strcmp(s, "球"))
		return S3DE_SPAWN_SPHERE;
	if (!strcmp(s, "水平面"))
		return S3DE_SPAWN_HORIZONTAL;
	WARNING("unknown spawn position type \"%s\"", s);
	return (enum s3de_spawn_position_type)-1;
}

static enum s3de_blend_type parse_blend_type(const char *s)
{
	if (!strcmp(s, "標準"))
		return S3DE_BLEND_NORMAL;
	if (!strcmp(s, "加算"))
		return S3DE_BLEND_ADDITIVE;
	WARNING("unknown blend type \"%s\"", s);
	return (enum s3de_blend_type)-1;
}

static enum s3de_direction_type parse_direction_type(const char *s)
{
	if (!strcmp(s, "ランダム"))
		return S3DE_DIR_RANDOM;
	if (!strcmp(s, "指定方向"))
		return S3DE_DIR_SPECIFIED;
	if (!strcmp(s, "エミッタ方向"))
		return S3DE_DIR_EMITTER;
	if (!strcmp(s, "エミッタ逆方向"))
		return S3DE_DIR_EMITTER_REVERSE;
	if (!strcmp(s, "エミッタ座標系"))
		return S3DE_DIR_EMITTER_COORD;
	if (!strcmp(s, "任意平面"))
		return S3DE_DIR_ARBITRARY_PLANE;
	if (!strcmp(s, "生成方向"))
		return S3DE_DIR_SPAWN;
	WARNING("unknown direction type \"%s\"", s);
	return (enum s3de_direction_type)-1;
}

static enum s3de_posture_type parse_posture_type(const char *s)
{
	if (!strcmp(s, "なし"))
		return S3DE_POSE_NONE;
	if (!strcmp(s, "カメラ"))
		return S3DE_POSE_CAMERA;
	if (!strcmp(s, "移動方向"))
		return S3DE_POSE_MOVE_DIR;
	if (!strcmp(s, "移動＆飛行方向"))
		return S3DE_POSE_MOVE_AND_FLY;
	if (!strcmp(s, "飛行＆エミッタ移動方向"))
		return S3DE_POSE_FLY_AND_EMITTER_MOVE;
	WARNING("unknown posture type \"%s\"", s);
	return (enum s3de_posture_type)-1;
}

static enum s3de_interp_type parse_interp_type(const char *s)
{
	if (!strcmp(s, "なし"))
		return S3DE_INTERP_NONE;
	if (!strcmp(s, "線形"))
		return S3DE_INTERP_LINEAR;
	if (!strcmp(s, "球面線形"))
		return S3DE_INTERP_SLERP;
	WARNING("unknown interp type \"%s\"", s);
	return (enum s3de_interp_type)-1;
}

#define MATCH(fmt, ...) (len = 0, sscanf(text, fmt " %n", ##__VA_ARGS__, &len), text += len, len)
#define STRING_BUF_SIZE 256
#define KEYWORD " %255[^, \t\r\n}]"
#define QUOTED_STRING "\"%255[^\"]\""

static char *parse_quoted_string(char **ptext)
{
	char *text = *ptext;

	while (isspace((unsigned char)*text)) text++;
	if (*text++ != '"') {
		parse_error("quoted string expected.", text);
		return NULL;
	}
	char *p = strchr(text, '"');
	if (!p) {
		parse_error("unfinished string.", text);
		return NULL;
	}
	*p = '\0';
	*ptext = p + 1;
	return utf2sjis(text, 0);
}

// range := float [',' float]
static bool parse_range(char **ptext, struct s3de_range *r)
{
	char *text = *ptext;
	int len;
	float a, b;
	if (!MATCH("%f", &a)) {
		parse_error("float expected.", text);
		return false;
	}
	r->base = a;
	r->random = 0.0f;
	if (MATCH(", %f", &b))
		r->random = b;
	*ptext = text;
	return true;
}

static bool parse_two_floats(char **ptext, float out[2])
{
	char *text = *ptext;
	int len;
	if (!MATCH("%f , %f", &out[0], &out[1])) {
		parse_error("two floats expected.", text);
		return false;
	}
	*ptext = text;
	return true;
}

static bool parse_spin_angle(char **ptext, struct s3de_range *start, struct s3de_range *end)
{
	float v[2];
	if (!parse_two_floats(ptext, v))
		return false;
	start->base = v[0];
	start->random = 0.0f;
	end->base = v[1];
	end->random = 0.0f;
	return true;
}

static bool parse_vec3(char **ptext, vec3 out)
{
	char *text = *ptext;
	int len;
	if (!MATCH("%f , %f , %f", &out[0], &out[1], &out[2])) {
		parse_error("three floats expected.", text);
		return false;
	}
	*ptext = text;
	return true;
}

static int *parse_int_list(char **ptext, int *nr)
{
	char *text = *ptext;
	int len, n;
	if (!MATCH("%d", &n)) {
		parse_error("int expected.", text);
		return NULL;
	}
	int cap = 16;
	int *v = xmalloc(cap * sizeof(int));
	v[0] = n;
	*nr = 1;
	while (MATCH(", %d", &n)) {
		if (*nr == cap) {
			cap *= 2;
			v = xrealloc(v, cap * sizeof(int));
		}
		v[(*nr)++] = n;
	}
	*ptext = text;
	return v;
}

// Parses a comma-separated list of quoted strings, storing the first non-empty
// one in *out and skipping the rest. Rance 9 only uses the first name.
static bool parse_texture_name(char **ptext, char **out)
{
	char *text = *ptext;
	int len;
	char *result = NULL;
	do {
		char *s = parse_quoted_string(&text);
		if (!s) {
			free(result);
			return false;
		}
		if (result || s[0] == '\0')
			free(s);
		else
			result = s;
	} while (MATCH(", "));
	*ptext = text;
	*out = result;
	return true;
}

// Parses a list of scalar entries of the form "{ frame, value, interp }, ...".
static bool parse_scalar_entries(char **ptext, struct s3de_scalar_entry **out, int *nr_out)
{
	char *text = *ptext;
	int len;
	int cap = 8, nr = 0;
	char buf[STRING_BUF_SIZE];
	struct s3de_scalar_entry *v = xmalloc(cap * sizeof(*v));
	for (;;) {
		if (!MATCH("{ %d , %f , " KEYWORD, &v[nr].frame, &v[nr].value, buf)) {
			parse_error("scalar list entry expected.", text);
			goto err;
		}
		int interp = parse_interp_type(buf);
		if (interp < 0) goto err;
		v[nr].interp = interp;
		if (!MATCH("}")) { parse_error("'}' expected.", text); goto err; }
		nr++;
		if (nr == cap) {
			cap *= 2;
			v = xrealloc(v, cap * sizeof(*v));
		}
		if (!MATCH(",")) break;
	}
	*out = v;
	*nr_out = nr;
	*ptext = text;
	return true;
err:
	free(v);
	return false;
}

// Parses a list of vec3 entries of the form "{ frame, x, y, z, interp }, ...".
static bool parse_vec3_entries(char **ptext, struct s3de_vec3_entry **out, int *nr_out)
{
	char *text = *ptext;
	int len;
	int cap = 8, nr = 0;
	char buf[STRING_BUF_SIZE];
	struct s3de_vec3_entry *v = xmalloc(cap * sizeof(*v));
	for (;;) {
		if (!MATCH("{ %d , %f , %f , %f , " KEYWORD,
				   &v[nr].frame, &v[nr].v[0], &v[nr].v[1], &v[nr].v[2], buf)) {
			parse_error("vec3 list entry expected.", text);
			goto err;
		}
		int interp = parse_interp_type(buf);
		if (interp < 0) goto err;
		v[nr].interp = interp;
		if (!MATCH("}")) { parse_error("'}' expected.", text); goto err; }
		nr++;
		if (nr == cap) {
			cap *= 2;
			v = xrealloc(v, cap * sizeof(*v));
		}
		if (!MATCH(",")) break;
	}
	*out = v;
	*nr_out = nr;
	*ptext = text;
	return true;
err:
	free(v);
	return false;
}

// Parses a list of position entries of the form
// "{ frame, x, y, z, interp, [spawn_radius, spawn_angle_deg] }, ...".
static bool parse_position_entries(char **ptext, struct s3de_position_entry **out, int *nr_out)
{
	char *text = *ptext;
	int len;
	int cap = 8, nr = 0;
	char buf[STRING_BUF_SIZE];
	struct s3de_position_entry *v = xmalloc(cap * sizeof(*v));
	for (;;) {
		memset(&v[nr], 0, sizeof(v[nr]));
		if (!MATCH("{ %d , %f , %f , %f , " KEYWORD,
				   &v[nr].frame, &v[nr].pos[0], &v[nr].pos[1], &v[nr].pos[2], buf)) {
			parse_error("position list entry expected.", text);
			goto err;
		}
		// Convert from left-handed coords to right-handed coords.
		v[nr].pos[2] = -v[nr].pos[2];
		int interp = parse_interp_type(buf);
		if (interp < 0) goto err;
		v[nr].interp = interp;
		(void)MATCH(", %f , %f", &v[nr].spawn_radius, &v[nr].spawn_angle_deg);
		if (!MATCH("}")) { parse_error("'}' expected.", text); goto err; }
		nr++;
		if (nr == cap) {
			cap *= 2;
			v = xrealloc(v, cap * sizeof(*v));
		}
		if (!MATCH(",")) break;
	}
	*out = v;
	*nr_out = nr;
	*ptext = text;
	return true;
err:
	free(v);
	return false;
}

static struct s3de *parse_3de(char *text)
{
	struct s3de *s = xcalloc(1, sizeof(struct s3de));
	s->loop_start_frame = -1;
	s->loop_end_frame = -1;

	struct s3de_object *current = NULL;

	for (;;) {
		// Skip whitespace and // comments.
		for (;;) {
			while (isspace((unsigned char)*text)) text++;
			if (text[0] == '/' && text[1] == '/') {
				char *nl = strchr(text, '\n');
				text = nl ? nl + 1 : text + strlen(text);
			} else {
				break;
			}
		}

		int len;
		int n;
		float f;
		char buf[STRING_BUF_SIZE];

		if (!current) {
			if (!*text) return s;

			if (MATCH("ループ開始フレーム = %d", &n)) {
				s->loop_start_frame = n;
			} else if (MATCH("ループ終了フレーム = %d", &n)) {
				s->loop_end_frame = n;
			} else if (MATCH("オブジェクト " QUOTED_STRING " = {", buf)) {
				s->objects = xrealloc_array(s->objects, s->nr_objects,
					s->nr_objects + 1, sizeof(struct s3de_object));
				current = &s->objects[s->nr_objects++];
				current->name = utf2sjis(buf, 0);
				current->texture_anime_frame = 1.0f;
			} else {
				parse_error("unknown toplevel token.", text);
				goto err;
			}
			continue;
		}

		if (!*text) {
			WARNING("unexpected EOF inside object");
			goto err;
		}

		if (MATCH("}")) {
			current = NULL;
		} else if (MATCH("種類 = " KEYWORD, buf)) {
			int v = parse_object_type(buf);
			if (v < 0) goto err;
			current->type = v;
		} else if (MATCH("移動タイプ = " KEYWORD, buf)) {
			int v = parse_move_type(buf);
			if (v < 0) goto err;
			current->movement_type = v;
		} else if (MATCH("生成位置タイプ = " KEYWORD, buf)) {
			int v = parse_spawn_position_type(buf);
			if (v < 0) goto err;
			current->spawn_position_type = v;
		} else if (MATCH("生成距離 = %f", &f)) {
			current->spawn_distance = f;
		} else if (MATCH("個数 = %d", &n)) {
			current->particle_count = n;
		} else if (MATCH("テクスチャ = ")) {
			if (!parse_texture_name(&text, &current->texture)) goto err;
		} else if (MATCH("ブレンドタイプ = " KEYWORD, buf)) {
			int v = parse_blend_type(buf);
			if (v < 0) goto err;
			current->blend_type = v;
		} else if (MATCH("テクスチャアニメフレーム = %f", &f)) {
			current->texture_anime_frame = f;
		} else if (MATCH("ポリゴン = ")) {
			current->polygon_name = parse_quoted_string(&text);
			if (!current->polygon_name) goto err;
		} else if (MATCH("フレーム参照 = " KEYWORD, buf)) {
			// Every emitter in Rance 9 uses "エフェクト".
			if (strcmp(buf, "エフェクト") != 0)
				WARNING("unhandled frame_reference \"%s\"", buf);
		} else if (MATCH("エミッタ連動タイプ = %d", &n)) {
			if (n < 0 || n > S3DE_LINK_MAX) {
				WARNING("unknown emitter_link_type %d", n);
				n = S3DE_LINK_NONE;
			}
			current->emitter_link_type = n;
		} else if (MATCH("ソフトフォグエッジ = %d", &n)) {
			current->soft_fog_edge = (n != 0);
		} else if (MATCH("姿勢タイプ = " KEYWORD, buf)) {
			int v = parse_posture_type(buf);
			if (v < 0) goto err;
			current->posture_type = v;
		} else if (MATCH("ターゲット名 = ")) {
			// Unused.
			char *s = parse_quoted_string(&text);
			if (!s) goto err;
			free(s);
		} else if (MATCH("方向タイプ = " KEYWORD, buf)) {
			int v = parse_direction_type(buf);
			if (v < 0) goto err;
			current->direction_type = v;
		} else if (MATCH("方向 = ")) {
			if (!parse_vec3(&text, current->direction))
				goto err;
			current->direction[2] = -current->direction[2];
		} else if (MATCH("方向角度 = %f", &f)) {
			current->direction_angle = f;
		}
		else if (MATCH("開始サイズ = ")) { if (!parse_range(&text, &current->start_size)) goto err; }
		else if (MATCH("終了サイズ = ")) { if (!parse_range(&text, &current->end_size)) goto err; }
		else if (MATCH("開始Ｘサイズ = ")) { if (!parse_range(&text, &current->start_x_size)) goto err; }
		else if (MATCH("終了Ｘサイズ = ")) { if (!parse_range(&text, &current->end_x_size)) goto err; }
		else if (MATCH("開始Ｙサイズ = ")) { if (!parse_range(&text, &current->start_y_size)) goto err; }
		else if (MATCH("終了Ｙサイズ = ")) { if (!parse_range(&text, &current->end_y_size)) goto err; }
		else if (MATCH("オフセットＸ = ")) { if (!parse_range(&text, &current->offset_x)) goto err; }
		else if (MATCH("オフセットＹ = ")) { if (!parse_range(&text, &current->offset_y)) goto err; }
		else if (MATCH("オフセットＺ = ")) {
			if (!parse_range(&text, &current->offset_z))
				goto err;
			current->offset_z.base = -current->offset_z.base;
		}
		else if (MATCH("速度 = ")) { if (!parse_range(&text, &current->speed)) goto err; }
		else if (MATCH("加速度 = ")) { if (!parse_range(&text, &current->acceleration)) goto err; }
		else if (MATCH("移動距離 = ")) { if (!parse_range(&text, &current->movement_distance)) goto err; }
		else if (MATCH("移動カーブ = ")) { if (!parse_range(&text, &current->movement_curve)) goto err; }
		else if (MATCH("自由落下 = %d", &n)) { current->free_fall = (n != 0); }
		else if (MATCH("質量 = %f", &f)) { current->mass = f; }
		else if (MATCH("空気抵抗係数 = %f", &f)) { current->air_resistance = f; }
		else if (MATCH("開始Ｘ回転角度 = ")) { if (!parse_range(&text, &current->start_x_rotation)) goto err; }
		else if (MATCH("終了Ｘ回転角度 = ")) { if (!parse_range(&text, &current->end_x_rotation)) goto err; }
		else if (MATCH("開始Ｙ回転角度 = ")) { if (!parse_range(&text, &current->start_y_rotation)) goto err; }
		else if (MATCH("終了Ｙ回転角度 = ")) { if (!parse_range(&text, &current->end_y_rotation)) goto err; }
		else if (MATCH("開始Ｚ回転角度 = ")) { if (!parse_range(&text, &current->start_z_rotation)) goto err; }
		else if (MATCH("終了Ｚ回転角度 = ")) { if (!parse_range(&text, &current->end_z_rotation)) goto err; }
		else if (MATCH("Ｘ軸自転角度 = ")) { if (!parse_spin_angle(&text, &current->start_x_rotation, &current->end_x_rotation)) goto err; }
		else if (MATCH("Ｙ軸自転角度 = ")) { if (!parse_spin_angle(&text, &current->start_y_rotation, &current->end_y_rotation)) goto err; }
		else if (MATCH("Ｚ軸自転角度 = ")) { if (!parse_spin_angle(&text, &current->start_z_rotation, &current->end_z_rotation)) goto err; }
		else if (MATCH("Ｘ軸公転角度 = ")) { if (!parse_two_floats(&text, current->x_revolution_angle)) goto err; }
		else if (MATCH("Ｙ軸公転角度 = ")) { if (!parse_two_floats(&text, current->y_revolution_angle)) goto err; }
		else if (MATCH("Ｚ軸公転角度 = ")) { if (!parse_two_floats(&text, current->z_revolution_angle)) goto err; }
		else if (MATCH("Ｘ軸公転距離 = ")) { if (!parse_two_floats(&text, current->x_revolution_distance)) goto err; }
		else if (MATCH("Ｙ軸公転距離 = ")) { if (!parse_two_floats(&text, current->y_revolution_distance)) goto err; }
		else if (MATCH("Ｚ軸公転距離 = ")) { if (!parse_two_floats(&text, current->z_revolution_distance)) goto err; }
		else if (MATCH("カーブＸ距離 = %f", &f)) { /* unused */ }
		else if (MATCH("カーブＹ距離 = %f", &f)) { /* unused */ }
		else if (MATCH("カーブＺ距離 = %f", &f)) { /* unused */ }
		else if (MATCH("子フレーム = %d", &n)) { current->child_frame = n; }
		else if (MATCH("子傾き = %f", &f)) { /* unused */ }
		else if (MATCH("子傾き２ = %f", &f)) { /* unused */ }
		else if (MATCH("子移動方向タイプ = %d", &n)) { /* unused */ }
		else if (MATCH("アップベクトルタイプ = %d", &n)) { /* unused */ }
		else if (MATCH("停止フレーム = %d", &n)) { /* unused */ }
		else if (MATCH("アルファフェードインフレーム = ")) { if (!parse_range(&text, &current->alpha_fade_in_frame)) goto err; }
		else if (MATCH("アルファフェードアウトフレーム = ")) { if (!parse_range(&text, &current->alpha_fade_out_frame)) goto err; }
		else if (MATCH("ダメージ = ")) {
			current->damages = parse_int_list(&text, &current->nr_damages);
			if (!current->damages)
				goto err;
		}
		else if (MATCH("位置リスト = ")) {
			if (!parse_position_entries(&text, &current->position_list, &current->nr_position_entries))
				goto err;
		}
		else if (MATCH("サイズリスト = ")) {
			if (!parse_scalar_entries(&text, &current->size_list, &current->nr_size_entries))
				goto err;
		}
		else if (MATCH("Ｘサイズリスト = ")) {
			if (!parse_scalar_entries(&text, &current->x_size_list, &current->nr_x_size_entries))
				goto err;
		}
		else if (MATCH("Ｙサイズリスト = ")) {
			if (!parse_scalar_entries(&text, &current->y_size_list, &current->nr_y_size_entries))
				goto err;
		}
		else if (MATCH("アルファリスト = ")) {
			if (!parse_scalar_entries(&text, &current->alpha_list, &current->nr_alpha_entries))
				goto err;
		}
		else if (MATCH("回転リスト = ")) {
			if (!parse_vec3_entries(&text, &current->rotation_list, &current->nr_rotation_entries))
				goto err;
		}
		else if (MATCH("乗算カラーリスト = ")) {
			if (!parse_vec3_entries(&text, &current->multiply_color_list, &current->nr_multiply_color_entries))
				goto err;
		}
		else if (MATCH("加算カラーリスト = ")) {
			if (!parse_vec3_entries(&text, &current->additive_color_list, &current->nr_additive_color_entries))
				goto err;
		}
		else {
			parse_error("unknown object field.", text);
			goto err;
		}
	}
err:
	s3de_free(s);
	return NULL;
}

static void load_textures(struct s3de *s, struct archive *aar)
{
	s->textures = ht_create(16);
	for (int i = 0; i < s->nr_objects; i++) {
		struct s3de_object *o = &s->objects[i];
		const char *name = o->texture;
		if (!name || ht_get(s->textures, name, NULL))
			continue;

		struct archive_data *dfile = RE_get_aar_entry(aar, s->path, name, ".png");
		if (!dfile) {
			WARNING("cannot load texture %s\\%s", s->path, name);
			continue;
		}
		struct cg *cg = cg_load_data(dfile);
		if (!cg) {
			WARNING("cg_load_data failed: %s", dfile->name);
			archive_free_data(dfile);
			continue;
		}
		archive_free_data(dfile);

		GLuint texture;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cg->metrics.w, cg->metrics.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, cg->pixels);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glGenerateMipmap(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, 0);

		struct billboard_texture *bt = xcalloc(1, sizeof(struct billboard_texture));
		bt->texture = texture;
		bt->has_alpha = cg->metrics.has_alpha;
		ht_put(s->textures, name, bt);
		cg_free(cg);
	}
}

static void load_polygons(struct s3de *s, struct archive *aar)
{
	for (int i = 0; i < s->nr_objects; i++) {
		struct s3de_object *o = &s->objects[i];
		if (o->polygon_name && *o->polygon_name) {
			char *pol_path = xmalloc(strlen(s->path) + strlen(o->polygon_name) + 2);
			sprintf(pol_path, "%s\\%s", s->path, o->polygon_name);
			o->model = model_load(aar, pol_path);
			free(pol_path);
		}
	}
}

struct s3de *s3de_load(struct archive *aar, const char *path)
{
	const char *basename = strrchr(path, '\\');
	basename = basename ? basename + 1 : path;

	struct archive_data *dfile = RE_get_aar_entry(aar, path, basename, ".3de");
	if (!dfile) {
		WARNING("%s\\%s.3de: not found", path, basename);
		return NULL;
	}

	char *sjis_text = xmalloc(dfile->size + 1);
	memcpy(sjis_text, dfile->data, dfile->size);
	sjis_text[dfile->size] = '\0';

	char *text = sjis2utf(sjis_text, dfile->size);
	free(sjis_text);
	struct s3de *s = parse_3de(text);
	free(text);
	if (!s) {
		WARNING("could not parse %s", dfile->name);
		archive_free_data(dfile);
		return NULL;
	}
	archive_free_data(dfile);

	s->path = strdup(path);
	load_textures(s, aar);
	load_polygons(s, aar);

	return s;
}

static void free_billboard_texture(void *value)
{
	struct billboard_texture *bt = value;
	glDeleteTextures(1, &bt->texture);
	free(bt);
}

void s3de_free(struct s3de *s)
{
	if (!s) return;
	for (int i = 0; i < s->nr_objects; i++) {
		struct s3de_object *o = &s->objects[i];
		free(o->name);
		free(o->texture);
		free(o->polygon_name);
		free(o->damages);
		free(o->position_list);
		free(o->size_list);
		free(o->x_size_list);
		free(o->y_size_list);
		free(o->alpha_list);
		free(o->rotation_list);
		free(o->multiply_color_list);
		free(o->additive_color_list);
		if (o->model)
			model_free(o->model);
	}
	if (s->textures) {
		ht_foreach_value(s->textures, free_billboard_texture);
		ht_free(s->textures);
	}
	free(s->objects);
	free(s->path);
	free(s);
}

static float interp_scalar(struct s3de_scalar_entry *list, int n, float frame, float def)
{
	if (n == 0)
		return def;
	if (frame <= list[0].frame)
		return list[0].value;
	if (frame >= list[n - 1].frame)
		return list[n - 1].value;
	int k = 0;
	while (k + 1 < n && list[k + 1].frame <= frame)
		k++;
	struct s3de_scalar_entry *a = &list[k];
	struct s3de_scalar_entry *b = &list[k + 1];
	if (a->interp == S3DE_INTERP_NONE)
		return a->value;
	float t = (frame - a->frame) / (float)(b->frame - a->frame);
	return glm_lerp(a->value, b->value, t);
}

static void interp_vec3(struct s3de_vec3_entry *list, int n, float frame, vec3 dest, vec3 def)
{
	if (n == 0) {
		glm_vec3_copy(def, dest);
		return;
	}
	if (frame <= list[0].frame) {
		glm_vec3_copy(list[0].v, dest);
		return;
	}
	if (frame >= list[n - 1].frame) {
		glm_vec3_copy(list[n - 1].v, dest);
		return;
	}
	int k = 0;
	while (k + 1 < n && list[k + 1].frame <= frame)
		k++;
	struct s3de_vec3_entry *a = &list[k];
	struct s3de_vec3_entry *b = &list[k + 1];
	if (a->interp == S3DE_INTERP_NONE) {
		glm_vec3_copy(a->v, dest);
		return;
	}
	float t = (frame - a->frame) / (float)(b->frame - a->frame);
	glm_vec3_lerp(a->v, b->v, t, dest);
}

// Evaluate position along the Bezier curve formed by consecutive cubic segments
// (with possible quadratic/linear fallback for the final segment).
static void interp_pos_bezier(struct s3de_position_entry *list, int n, float frame, vec3 dest)
{
	int k = 0;
	while (k + 1 < n) {
		// Hold: snap to this entry's position until the next entry.
		if (list[k].interp == S3DE_INTERP_NONE) {
			if (frame < list[k + 1].frame) {
				glm_vec3_copy(list[k].pos, dest);
				return;
			}
			k++;
			continue;
		}

		// Determine segment degree from remaining entries.
		// Prefer cubic (4 points), fall back to quadratic (3) or linear (2).
		int degree = min(n - 1 - k, 3);
		int seg_end = k + degree;
		// If frame lies within this segment or this is the last segment,
		// evaluate the Bezier curve.
		if (frame < list[seg_end].frame || seg_end == n - 1) {
			// For simplicity, t is mapped linearly within a segment.
			float t = (frame - list[k].frame) /
				  (float)(list[seg_end].frame - list[k].frame);
			// De Casteljau's algorithm.
			vec3 tmp[4];
			for (int i = 0; i <= degree; i++)
				glm_vec3_copy(list[k + i].pos, tmp[i]);
			for (int d = degree; d > 0; d--)
				for (int i = 0; i < d; i++)
					glm_vec3_lerp(tmp[i], tmp[i + 1], t, tmp[i]);
			glm_vec3_copy(tmp[0], dest);
			return;
		}
		// Skip to the start of the next cubic segment.
		k += 3;
	}
	glm_vec3_copy(list[n - 1].pos, dest);
}

// Linear-mode segment lookup.
// Caller must ensure n >= 2 and list[0].frame < frame < list[n-1].frame.
static void find_pos_segment(struct s3de_position_entry *list, int n, float frame,
			     int *k_out, float *t_out)
{
	// The boundary guarantee above keeps k <= n-2, so list[k+1] is valid.
	int k = 0;
	while (k + 1 < n && list[k + 1].frame <= frame)
		k++;
	*k_out = k;
	if (list[k].interp == S3DE_INTERP_NONE) {
		// t <- 0, so the caller can lerp unconditionally and naturally hold
		// list[k]'s value.
		*t_out = 0.0f;
	} else {
		// Linear interpolation.
		*t_out = (frame - list[k].frame) / (float)(list[k + 1].frame - list[k].frame);
	}
}

static void interp_pos(struct s3de_position_entry *list, int n, float frame,
		       enum s3de_move_type move_type, vec3 dest)
{
	if (n == 0) {
		glm_vec3_zero(dest);
		return;
	}
	if (frame <= list[0].frame) {
		glm_vec3_copy(list[0].pos, dest);
		return;
	}
	if (frame >= list[n - 1].frame) {
		glm_vec3_copy(list[n - 1].pos, dest);
		return;
	}
	switch (move_type) {
	case S3DE_MOVE_LINEAR: {
		int k;
		float t;
		find_pos_segment(list, n, frame, &k, &t);
		glm_vec3_lerp(list[k].pos, list[k + 1].pos, t, dest);
		break;
	}
	case S3DE_MOVE_BEZIER:
		interp_pos_bezier(list, n, frame, dest);
		break;
	}
}

// Interpolate the per-entry (spawn_radius, spawn_angle_deg) pair at `frame`.
static void interp_spawn_params(struct s3de_position_entry *list, int n, float frame,
				float *radius_out, float *angle_deg_out)
{
	if (n == 0) {
		*radius_out = 0.0f;
		*angle_deg_out = 0.0f;
		return;
	}
	if (frame <= list[0].frame) {
		*radius_out = list[0].spawn_radius;
		*angle_deg_out = list[0].spawn_angle_deg;
		return;
	}
	if (frame >= list[n - 1].frame) {
		*radius_out = list[n - 1].spawn_radius;
		*angle_deg_out = list[n - 1].spawn_angle_deg;
		return;
	}
	int k;
	float t;
	find_pos_segment(list, n, frame, &k, &t);
	*radius_out    = glm_lerp(list[k].spawn_radius,    list[k + 1].spawn_radius,    t);
	*angle_deg_out = glm_lerp(list[k].spawn_angle_deg, list[k + 1].spawn_angle_deg, t);
}

// Deterministic per-emitter PRNG (splitmix32), so repeated playback produces
// identical particle layouts.
struct s3de_rng { uint32_t state; };

static inline uint32_t rng_next_u32(struct s3de_rng *r)
{
	uint32_t z = (r->state += 0x9e3779b9u);
	z = (z ^ (z >> 16)) * 0x85ebca6bu;
	z = (z ^ (z >> 13)) * 0xc2b2ae35u;
	return z ^ (z >> 16);
}

// Uniform float in [0, 1).
static inline float randf(struct s3de_rng *r)
{
	return (float)(rng_next_u32(r) >> 8) * (1.0f / 16777216.0f);
}

static inline float eval_range(struct s3de_rng *rng, struct s3de_range r)
{
	// base + uniform([-random/2, +random/2]).
	return r.base + (randf(rng) - 0.5f) * r.random;
}

// Uniform sampling on the unit sphere.
static void random_unit_vec3(struct s3de_rng *rng, vec3 dest)
{
	float z = randf(rng) * 2.0f - 1.0f;
	float phi = randf(rng) * 2.0f * GLM_PIf;
	float r = sqrtf(fmaxf(0.0f, 1.0f - z * z));
	dest[0] = r * cosf(phi);
	dest[1] = r * sinf(phi);
	dest[2] = z;
}

// Rejection sample inside the unit ball: pick three uniforms in [-1, 1] and
// retry until inside.
static void random_in_unit_ball(struct s3de_rng *rng, vec3 dest)
{
	for (int i = 0; i < 100; i++) {
		float x = randf(rng) * 2.0f - 1.0f;
		float y = randf(rng) * 2.0f - 1.0f;
		float z = randf(rng) * 2.0f - 1.0f;
		if (x * x + y * y + z * z <= 1.0f) {
			dest[0] = x; dest[1] = y; dest[2] = z;
			return;
		}
	}
	dest[0] = 1.0f; dest[1] = 0.0f; dest[2] = 0.0f;
}

// Same idea but for an XZ-plane disk; y is fixed to 0.
static void random_in_unit_disk_xz(struct s3de_rng *rng, vec3 dest)
{
	for (int i = 0; i < 100; i++) {
		float x = randf(rng) * 2.0f - 1.0f;
		float z = randf(rng) * 2.0f - 1.0f;
		if (x * x + z * z <= 1.0f) {
			dest[0] = x; dest[1] = 0.0f; dest[2] = z;
			return;
		}
	}
	dest[0] = 1.0f; dest[1] = 0.0f; dest[2] = 0.0f;
}

// Sample a unit vector lying in the plane whose normal is `normal`.
static void random_in_plane(struct s3de_rng *rng, vec3 normal, vec3 dest)
{
	vec3 r;
	random_unit_vec3(rng, r);
	// NOTE: This yields a zero vector if the cross is degenerate.
	glm_vec3_crossn(normal, r, dest);
}

// Rejection-sample a unit vector within a cone of half-angle `angle_deg` around `axis`.
static void randomize_direction_within_cone(struct s3de_rng *rng, vec3 axis, float angle_deg)
{
	if (!(angle_deg > 0.0f))
		return;
	float cos_t = cosf(glm_rad(angle_deg));
	for (int i = 0; i < 100; i++) {
		vec3 c;
		random_unit_vec3(rng, c);
		if (glm_vec3_dot(axis, c) >= cos_t) {
			glm_vec3_copy(c, axis);
			return;
		}
	}
}

static void init_particle_direction(struct s3de_rng *rng, struct s3de_object *obj,
				     struct s3de_particle *p, vec3 dest)
{
	switch (obj->direction_type) {
	case S3DE_DIR_SPECIFIED: {
		// normalize(obj->direction), scattered within the cone.
		glm_vec3_copy(obj->direction, dest);
		float n2 = glm_vec3_norm2(dest);
		if (n2 > 0.0f)
			glm_vec3_scale(dest, 1.0f / sqrtf(n2), dest);
		else
			glm_vec3_copy(GLM_YUP, dest);
		randomize_direction_within_cone(rng, dest, obj->direction_angle);
		break;
	}
	case S3DE_DIR_EMITTER:
		// spawn_dir (the emitter's velocity tangent at the spawn frame),
		// scattered within the cone.
		glm_vec3_copy(p->spawn_dir, dest);
		randomize_direction_within_cone(rng, dest, obj->direction_angle);
		break;
	case S3DE_DIR_EMITTER_REVERSE:
		// -spawn_dir, scattered within the cone.
		glm_vec3_negate_to(p->spawn_dir, dest);
		randomize_direction_within_cone(rng, dest, obj->direction_angle);
		break;
	case S3DE_DIR_ARBITRARY_PLANE:
		// A random unit vector in the plane whose normal is obj->direction.
		// Falls back to a fully random direction if the normal is zero.
		if (glm_vec3_norm2(obj->direction) > 0.0f)
			random_in_plane(rng, obj->direction, dest);
		else
			random_unit_vec3(rng, dest);
		break;
	case S3DE_DIR_SPAWN:
		// normalize(spawn_offset), i.e. radially outward from the emitter
		// center.
		glm_vec3_normalize_to(p->spawn_offset, dest);
		break;
	case S3DE_DIR_EMITTER_COORD: {
		// normalize(obj->direction) rotated from emitter-local to world
		// space by the emitter rotation at the spawn frame.
		vec3 local;
		glm_vec3_normalize_to(obj->direction, local);
		vec3 deg;
		interp_vec3(obj->rotation_list, obj->nr_rotation_entries, p->begin_frame,
			    deg, GLM_VEC3_ZERO);
		vec3 rad = {
			glm_rad(-deg[0]),
			glm_rad(-deg[1]),
			glm_rad(deg[2]),
		};
		mat4 R;
		glm_euler_zxy(rad, R);
		glm_mat4_mulv3(R, local, 0.0f, dest);
		break;
	}
	case S3DE_DIR_RANDOM:
	default:
		// A fully random unit vector (uniform over the sphere).
		random_unit_vec3(rng, dest);
		break;
	}
}

static void init_particle(struct s3de_rng *rng, struct s3de_object *obj, int index,
			  struct s3de_particle *p)
{
	// Distribute spawn frames evenly across the position list's frame range.
	float first_frame = 0.0f, last_frame = 0.0f;
	if (obj->nr_position_entries > 0) {
		first_frame = obj->position_list[0].frame;
		last_frame = obj->position_list[obj->nr_position_entries - 1].frame;
	}
	float t = obj->particle_count > 1
		? index / (float)(obj->particle_count - 1)
		: 0.0f;
	p->begin_frame = first_frame + t * (last_frame - first_frame);
	p->end_frame = p->begin_frame + obj->child_frame;

	// Per-particle quad pivot offset.
	p->offset_x = eval_range(rng, obj->offset_x);
	p->offset_y = eval_range(rng, obj->offset_y);

	// Spawn position offset from the emitter, built in two stages:
	//   offset = R_y(spawn_angle) * (spawn_radius, 0, 0)
	//   offset += spawn_distance * random_unit_(sphere|disk)
	float spawn_radius = 0.0f, spawn_angle = 0.0f;
	interp_spawn_params(obj->position_list, obj->nr_position_entries,
			    p->begin_frame, &spawn_radius, &spawn_angle);
	glm_make_rad(&spawn_angle);
	vec3 offset = {
		spawn_radius * cosf(spawn_angle),
		0.0f,
		spawn_radius * sinf(spawn_angle),
	};
	if (obj->spawn_distance != 0.0f) {
		vec3 sp = GLM_VEC3_ZERO_INIT;
		if (obj->spawn_position_type == S3DE_SPAWN_SPHERE)
			random_in_unit_ball(rng, sp);
		else if (obj->spawn_position_type == S3DE_SPAWN_HORIZONTAL)
			random_in_unit_disk_xz(rng, sp);
		glm_vec3_muladds(sp, obj->spawn_distance, offset);
	}
	glm_vec3_copy(offset, p->spawn_offset);

	// world_pos = emitter_pos(begin_frame) + spawn_offset
	vec3 emitter_pos;
	interp_pos(obj->position_list, obj->nr_position_entries, p->begin_frame,
		   obj->movement_type, emitter_pos);
	glm_vec3_add(emitter_pos, offset, p->world_pos);

	// spawn_dir = normalize(emitter_pos(begin+0.1) - emitter_pos(begin-0.1))
	{
		const float delta = 0.1f;
		vec3 p0, p1;
		interp_pos(obj->position_list, obj->nr_position_entries,
			   p->begin_frame - delta, obj->movement_type, p0);
		interp_pos(obj->position_list, obj->nr_position_entries,
			   p->begin_frame + delta, obj->movement_type, p1);
		glm_vec3_sub(p1, p0, p->spawn_dir);
		glm_vec3_normalize(p->spawn_dir);
	}

	init_particle_direction(rng, obj, p, p->direction);

	p->speed = eval_range(rng, obj->speed);
	p->accel = eval_range(rng, obj->acceleration);
	p->movement_distance = eval_range(rng, obj->movement_distance);
	p->movement_curve = eval_range(rng, obj->movement_curve);

	p->start_size_scale = eval_range(rng, obj->start_size);
	p->end_size_scale   = eval_range(rng, obj->end_size);
	p->start_x_scale    = eval_range(rng, obj->start_x_size);
	p->end_x_scale      = eval_range(rng, obj->end_x_size);
	p->start_y_scale    = eval_range(rng, obj->start_y_size);
	p->end_y_scale      = eval_range(rng, obj->end_y_size);

	p->start_rotation_deg[0] = eval_range(rng, obj->start_x_rotation);
	p->start_rotation_deg[1] = eval_range(rng, obj->start_y_rotation);
	p->start_rotation_deg[2] = eval_range(rng, obj->start_z_rotation);
	p->end_rotation_deg[0]   = eval_range(rng, obj->end_x_rotation);
	p->end_rotation_deg[1]   = eval_range(rng, obj->end_y_rotation);
	p->end_rotation_deg[2]   = eval_range(rng, obj->end_z_rotation);

	p->fade_in_frames  = eval_range(rng, obj->alpha_fade_in_frame);
	p->fade_out_frames = eval_range(rng, obj->alpha_fade_out_frame);
}

// Compute particle position in instance-local space at the given frame. Sums a
// motion vector (distance traveled along p->direction, plus optional gravity)
// onto a base position chosen by emitter_link_type.
static void particle_local_pos(const struct s3de_object *obj,
			     struct s3de_object_state *st,
			     struct s3de_particle *p, float frame, vec3 dest)
{
	// Physics distance along p->direction: speed*t + 0.5*accel*t^2,
	// where t is the elapsed time (rel_frame * 0.03) capped at the
	// velocity-zero peak for accel < 0, so the particle stops there instead of
	// reversing.
	float rel_frame = frame - p->begin_frame;
	float lifetime = p->end_frame - p->begin_frame;
	float elapsed = rel_frame * 0.03f;
	float motion_t = elapsed;
	if (p->accel < 0.0f)
		motion_t = fminf(motion_t, -p->speed / p->accel);
	float dist = p->speed * motion_t + 0.5f * p->accel * motion_t * motion_t;
	// Curve distance: movement_distance eased over the normalized lifetime.
	//   f = pow(t, c)        if c > 1
	//   f = t                if -1 <= c <= 1
	//   f = 1 - pow(1-t, -c) if c < -1
	if (p->movement_distance != 0.0f) {
		float t_norm = lifetime > 0.0f ? rel_frame / lifetime : 0.0f;
		t_norm = fminf(t_norm, 1.0f);
		float c = p->movement_curve;
		float ease;
		if (c > 1.0f)
			ease = powf(t_norm, c);
		else if (c < -1.0f)
			ease = 1.0f - powf(1.0f - t_norm, -c);
		else
			ease = t_norm;
		dist += p->movement_distance * ease;
	}
	vec3 motion;
	glm_vec3_scale(p->direction, dist, motion);
	// free_fall subtracts a gravity Y offset from the motion vector.
	if (obj->free_fall) {
		const float g = 9.80665f;
		float dy;
		if (obj->mass != 0.0f && obj->air_resistance != 0.0f) {
			// Viscous drag: the particle approaches a terminal velocity of mass*g/air
			float e = expf(-(obj->air_resistance / obj->mass) * elapsed);
			dy = (elapsed - (1.0f - e) * (obj->mass / obj->air_resistance))
				* obj->mass * g / obj->air_resistance;
		} else {
			dy = elapsed * elapsed * g * 0.5f;
		}
		motion[1] -= dy;
	}
	// Base position by emitter_link_type.
	switch (obj->emitter_link_type) {
	case S3DE_LINK_FOLLOW:
		// dest = current emitter_pos + spawn_offset + motion
		glm_vec3_add(st->emitter_pos, p->spawn_offset, dest);
		glm_vec3_add(dest, motion, dest);
		break;
	case S3DE_LINK_ROTATE_AT_SPAWN:
	case S3DE_LINK_ROTATE_AT_CURRENT: {
		// Rotate (spawn_offset + motion) by the current emitter rotation.
		vec3 v;
		glm_vec3_add(p->spawn_offset, motion, v);
		vec3 rad = {
			glm_rad(-st->emitter_rotation_deg[0]),
			glm_rad(-st->emitter_rotation_deg[1]),
			glm_rad(st->emitter_rotation_deg[2]),
		};
		mat4 R;
		glm_euler_zxy(rad, R);
		glm_mat4_mulv3(R, v, 0.0f, dest);
		if (obj->emitter_link_type == S3DE_LINK_ROTATE_AT_SPAWN) {
			// Rotate around emitter_pos at spawn.
			glm_vec3_add(dest, p->world_pos, dest);
			glm_vec3_sub(dest, p->spawn_offset, dest);
		} else {
			// Rotate around current emitter_pos.
			glm_vec3_add(dest, st->emitter_pos, dest);
		}
		break;
	}
	case S3DE_LINK_NONE:
	default:
		// Spawn-time world_pos.
		glm_vec3_add(p->world_pos, motion, dest);
		break;
	}
}

static void particle_scale(const struct s3de_particle *p,
			 const struct s3de_object_state *st,
			 float inst_scale, float t, vec3 out)
{
	float size = glm_lerp(p->start_size_scale, p->end_size_scale, t);
	float sx = glm_lerp(p->start_x_scale, p->end_x_scale, t);
	float sy = glm_lerp(p->start_y_scale, p->end_y_scale, t);
	size *= st->emitter_size * inst_scale;
	out[0] = size * sx * st->emitter_x_size;
	out[1] = size * sy * st->emitter_y_size;
	out[2] = size;
}

static void particle_rotation_mat(struct s3de_particle *p, float t, mat4 out)
{
	vec3 angles;
	glm_vec3_lerp(p->start_rotation_deg, p->end_rotation_deg, t, angles);
	angles[0] = glm_rad(-angles[0]);
	angles[1] = glm_rad(-angles[1]);
	angles[2] = glm_rad(angles[2]);
	glm_euler_yxz(angles, out);
}

// Normalize v in place. Returns false when v is the zero vector.
static bool try_normalize(vec3 v)
{
	if (glm_vec3_norm2(v) <= 0.0f)
		return false;
	glm_vec3_normalize(v);
	return true;
}

// World-space, unit-length particle flight direction:
// normalize(inst_xform * normalize(p->direction)). Returns false if degenerate.
static bool particle_world_dir(struct s3de_particle *p, mat4 inst_xform, vec3 out)
{
	vec3 pdir;
	glm_vec3_copy(p->direction, pdir);
	if (!try_normalize(pdir))
		return false;
	glm_mat4_mulv3(inst_xform, pdir, 0.0f, out);
	return try_normalize(out);
}

// Build the world-space posture basis (right, up, normal) for the billboard
// posture types that orient the quad from the particle's flight direction.
// Returns false for posture types NONE / CAMERA, and also when any
// intermediate vector degenerates.
static bool billboard_posture_basis(struct s3de_object *obj,
				 struct s3de_particle *p,
				 mat4 inst_xform, vec3 camera_front, mat3 out_rot)
{
	vec3 dir_world;
	if (!particle_world_dir(p, inst_xform, dir_world))
		return false;

	vec3 right, up;
	switch (obj->posture_type) {
	case S3DE_POSE_MOVE_DIR:
		glm_vec3_cross(camera_front, dir_world, right);
		if (!try_normalize(right))
			return false;
		glm_vec3_cross(right, camera_front, up);
		if (!try_normalize(up))
			return false;
		break;
	case S3DE_POSE_MOVE_AND_FLY:
	case S3DE_POSE_FLY_AND_EMITTER_MOVE: {
		vec3 axis;
		if (obj->posture_type == S3DE_POSE_MOVE_AND_FLY)
			glm_vec3_copy(obj->direction, axis);
		else
			glm_vec3_copy(p->spawn_dir, axis);
		if (!try_normalize(axis))
			return false;
		glm_vec3_copy(dir_world, up);
		glm_vec3_cross(axis, up, right);
		if (!try_normalize(right))
			return false;
		break;
	}
	case S3DE_POSE_NONE:
	case S3DE_POSE_CAMERA:
		return false;
	}

	vec3 normal;
	glm_vec3_cross(right, up, normal);
	glm_vec3_copy(right,  out_rot[0]);
	glm_vec3_copy(up,     out_rot[1]);
	glm_vec3_copy(normal, out_rot[2]);
	return true;
}

// Build the world-space posture basis for polygon particles.
static bool mesh_posture_basis(struct s3de_object *obj,
			     struct s3de_particle *p,
			     vec3 world_pos, vec3 camera_pos, vec3 camera_up,
			     mat4 inst_xform, mat3 out_rot)
{
	vec3 right, up, forward;

	switch (obj->posture_type) {
	case S3DE_POSE_CAMERA:
	case S3DE_POSE_MOVE_DIR:
	case S3DE_POSE_MOVE_AND_FLY:
		// Unlike the billboard path, the mesh path collapses these three
		// modes into a single "look at the camera position" basis that
		// ignores the particle's flight direction.
		glm_vec3_sub(world_pos, camera_pos, forward);
		if (!try_normalize(forward))
			return false;
		glm_vec3_cross(camera_up, forward, right);
		if (!try_normalize(right))
			return false;
		glm_vec3_cross(forward, right, up);
		break;
	case S3DE_POSE_FLY_AND_EMITTER_MOVE: {
		if (!particle_world_dir(p, inst_xform, up))
			return false;
		vec3 spawn;
		glm_vec3_copy(p->spawn_dir, spawn);
		if (!try_normalize(spawn))
			return false;
		glm_vec3_cross(spawn, up, forward);
		if (!try_normalize(forward))
			return false;
		glm_vec3_cross(up, forward, right);
		if (!try_normalize(right))
			return false;
		break;
	}
	case S3DE_POSE_NONE:
		return false;
	}

	glm_vec3_copy(right,   out_rot[0]);
	glm_vec3_copy(up,      out_rot[1]);
	glm_vec3_copy(forward, out_rot[2]);
	return true;
}

// Lifetime-dependent per-particle state.
struct s3de_particle_xform {
	float t;        // normalized lifetime position, 0..1
	vec3 pos_local; // instance-local position
	vec3 scale;
	mat4 rmat;      // start->end rotation matrix
};

static bool eval_particle(struct RE_instance *inst, struct s3de_object *obj,
			struct s3de_object_state *st, struct s3de_particle *p,
			float frame, struct s3de_particle_xform *out)
{
	if (frame < p->begin_frame || p->end_frame <= frame)
		return false;
	float rel = frame - p->begin_frame;
	float lifetime = p->end_frame - p->begin_frame;
	out->t = lifetime > 0.0f ? rel / lifetime : 0.0f;
	particle_local_pos(obj, st, p, frame, out->pos_local);
	// Only the instance's scale_x sizes the quad/mesh; scale_y/_z affect
	// position via the world matrix but not the particle size.
	particle_scale(p, st, inst->scale[0], out->t, out->scale);
	particle_rotation_mat(p, out->t, out->rmat);
	return true;
}

// Current alpha (emitter alpha * per-particle fade) for one particle. Returns
// false when the particle is not alive at `frame`.
bool s3de_particle_alpha(struct s3de_object_state *st, struct s3de_particle *p,
		float frame, float *alpha_out)
{
	if (frame < p->begin_frame || p->end_frame <= frame)
		return false;
	float rel = frame - p->begin_frame;
	float total = p->end_frame - p->begin_frame;
	float fade = 1.0f;
	if (p->fade_in_frames > 0.0f && rel < p->fade_in_frames)
		fade = glm_clamp_zo(rel / p->fade_in_frames);
	else if (p->fade_out_frames > 0.0f && total - rel < p->fade_out_frames)
		fade = glm_clamp_zo((total - rel) / p->fade_out_frames);
	*alpha_out = st->emitter_alpha * fade;
	return true;
}

// Build the full quad world matrix for one billboard particle:
//   out = T(world_pos) * R_posture * R_particle * S * pivot
bool s3de_billboard_world_transform(struct RE_instance *inst,
		struct s3de_object *obj, struct s3de_object_state *st,
		struct s3de_particle *p, float frame, mat3 camera_rot,
		mat4 out)
{
	struct s3de_particle_xform px;
	if (!eval_particle(inst, obj, st, p, frame, &px))
		return false;

	// world_pos = inst_xform * px.pos_local.
	vec3 pos;
	glm_mat4_mulv3(inst->local_transform, px.pos_local, 1.0f, pos);
	glm_translate_make(out, pos);

	mat3 rot;
	if (!billboard_posture_basis(obj, p, inst->local_transform, camera_rot[2], rot)) {
		// Fall back to the instance rotation (NONE) or the camera basis.
		if (obj->posture_type == S3DE_POSE_NONE) {
			vec3 euler = { glm_rad(inst->pitch), glm_rad(inst->yaw), glm_rad(inst->roll) };
			mat4 m;
			glm_euler(euler, m);
			glm_mat4_pick3(m, rot);
		} else {
			glm_mat3_copy(camera_rot, rot);
		}
	}
	glm_mat4_ins3(rot, out);

	glm_mat4_mul(out, px.rmat, out);
	glm_scale(out, px.scale);

	glm_translate_x(out, -p->offset_x);
	glm_translate_y(out, -p->offset_y);
	return true;
}

// Build the full mesh world matrix for one polygon particle.
bool s3de_mesh_world_transform(struct RE_instance *inst,
		struct s3de_object *obj, struct s3de_object_state *st,
		struct s3de_particle *p, float frame, mat3 camera_rot, vec3 camera_pos,
		mat4 out)
{
	struct s3de_particle_xform px;
	if (!eval_particle(inst, obj, st, p, frame, &px))
		return false;

	// world_pos = inst_xform * pos_local.
	vec3 pos;
	glm_mat4_mulv3(inst->local_transform, px.pos_local, 1.0f, pos);

	mat3 rot;
	if (mesh_posture_basis(obj, p, pos, camera_pos, camera_rot[1],
				    inst->local_transform, rot)) {
		// A world-space basis replaces the instance transform's rotation/scale:
		//   out = T(world_pos) * R_posture * R_particle * S
		glm_translate_make(out, pos);
		glm_mat4_ins3(rot, out);
		glm_mat4_mul(out, px.rmat, out);
		glm_scale(out, px.scale);
	} else {
		// Keep the instance-local orientation:
		//   out = inst_xform * ( T(pos_local) * R_particle * S )
		mat4 local;
		glm_translate_make(local, px.pos_local);
		glm_mat4_mul(local, px.rmat, local);
		glm_scale(local, px.scale);
		glm_mat4_mul(inst->local_transform, local, out);
	}
	return true;
}

struct s3de_effect *s3de_effect_create(struct s3de *s, struct archive *aar)
{
	struct s3de_effect *eff = xcalloc(1, sizeof(struct s3de_effect));
	eff->s3de = s;
	eff->objects = xcalloc(s->nr_objects, sizeof(struct s3de_object_state));
	eff->wav_channel = -1;

	for (int i = 0; i < s->nr_objects; i++) {
		struct s3de_object *obj = &s->objects[i];
		struct s3de_object_state *st = &eff->objects[i];
		// Default per-emitter values used when a list is empty.
		st->emitter_size = st->emitter_x_size = st->emitter_y_size = 1.0f;
		st->emitter_alpha = 1.0f;
		glm_vec3_one(st->multiply_color);

		if (obj->particle_count <= 0 || obj->type == S3DE_OBJ_CAMERA)
			continue;
		struct s3de_rng rng = {};
		st->particles = xcalloc(obj->particle_count, sizeof(struct s3de_particle));
		for (int j = 0; j < obj->particle_count; j++)
			init_particle(&rng, obj, j, &st->particles[j]);
	}

	const char *basename = strrchr(s->path, '\\');
	basename = basename ? basename + 1 : s->path;
	struct archive_data *dfile = RE_get_aar_entry(aar, s->path, basename, ".wav");
	if (dfile) {
		int ch = wav_get_unused_channel();
		if (wav_prepare_from_archive_data(ch, dfile)) {
			eff->wav_channel = ch;
		} else {
			WARNING("wav_prepare_from_archive_data failed for %s\\%s.wav", s->path, basename);
		}
	}
	return eff;
}

void s3de_effect_free(struct s3de_effect *eff)
{
	if (!eff)
		return;
	if (eff->wav_channel >= 0) {
		wav_stop(eff->wav_channel);
		wav_unprepare(eff->wav_channel);
	}
	if (eff->objects) {
		for (int i = 0; i < eff->s3de->nr_objects; i++)
			free(eff->objects[i].particles);
		free(eff->objects);
	}
	free(eff);
}

// Pick the camera object whose position_list frame range contains `frame`.
// If `frame` is past every range, pick the camera with the latest last_frame.
// If `frame` precedes every range, return -1.
static int select_active_camera_obj(struct s3de_effect *eff, float frame)
{
	int best = -1;
	float best_last = -INFINITY, best_first = -INFINITY;
	for (int i = 0; i < eff->s3de->nr_objects; i++) {
		struct s3de_object *obj = &eff->s3de->objects[i];
		if (obj->type != S3DE_OBJ_CAMERA || obj->nr_position_entries == 0)
			continue;
		float first = obj->position_list[0].frame;
		float last = obj->position_list[obj->nr_position_entries - 1].frame;
		if (first > frame)
			continue;
		if (frame < last)
			return i;
		if (last > best_last || (last == best_last && first > best_first)) {
			best = i;
			best_last = last;
			best_first = first;
		}
	}
	return best;
}

static void update_sound(struct s3de_effect *eff, struct motion *m)
{
	if (eff->wav_channel < 0)
		return;
	bool stopped = (m->state == RE_MOTION_STATE_STOP);
	float frame = m->current_frame;
	if (eff->sound_started && (stopped || frame + 0.5f < eff->last_frame)) {
		wav_stop(eff->wav_channel);
		eff->sound_started = false;
	}
	if (!eff->sound_started && !stopped) {
		int ms = (int)(frame * 1000.0f / 30.0f);
		if (ms > 0)
			wav_seek(eff->wav_channel, ms);
		wav_play(eff->wav_channel);
		eff->sound_started = true;
	}
	eff->last_frame = frame;
}

static void update_object_states(struct s3de_effect *eff, float frame)
{
	for (int i = 0; i < eff->s3de->nr_objects; i++) {
		struct s3de_object *obj = &eff->s3de->objects[i];
		struct s3de_object_state *st = &eff->objects[i];
		interp_pos(obj->position_list, obj->nr_position_entries, frame,
			   obj->movement_type, st->emitter_pos);
		interp_vec3(obj->rotation_list, obj->nr_rotation_entries, frame, st->emitter_rotation_deg, GLM_VEC3_ZERO);
		if (obj->type == S3DE_OBJ_CAMERA)
			continue;
		st->emitter_size   = interp_scalar(obj->size_list,   obj->nr_size_entries,   frame, 1.0f);
		st->emitter_x_size = interp_scalar(obj->x_size_list, obj->nr_x_size_entries, frame, 1.0f);
		st->emitter_y_size = interp_scalar(obj->y_size_list, obj->nr_y_size_entries, frame, 1.0f);
		st->emitter_alpha  = interp_scalar(obj->alpha_list,  obj->nr_alpha_entries,  frame, 1.0f);
		interp_vec3(obj->multiply_color_list, obj->nr_multiply_color_entries, frame, st->multiply_color, GLM_VEC3_ONE);
		interp_vec3(obj->additive_color_list, obj->nr_additive_color_entries, frame, st->additive_color, GLM_VEC3_ZERO);
	}
}

static void update_camera_override(struct RE_instance *inst, struct s3de_effect *eff, float frame)
{
	int cam_idx = select_active_camera_obj(eff, frame);
	if (cam_idx < 0 || !inst->plugin)
		return;

	struct s3de_object_state *st = &eff->objects[cam_idx];
	if (inst->local_transform_needs_update)
		RE_instance_update_local_transform(inst);
	vec4 local_pos = { st->emitter_pos[0], st->emitter_pos[1], st->emitter_pos[2], 1.0f };
	vec4 world_pos;
	glm_mat4_mulv(inst->local_transform, local_pos, world_pos);
	struct RE_camera *cam = &inst->plugin->camera;
	glm_vec3_copy(world_pos, cam->override_pos);
	// XXX: Compose the instance and camera-object rotations by summing Euler
	// angles directly. This is not equivalent to true rotation composition in
	// general, but it is exact here: in Rance 9 non-camera instances are only
	// ever rotated about yaw. Since yaw is the outermost axis in the renderer's
	// YXZ convention, the two yaw rotations combine exactly by addition.
	cam->override_pitch = inst->pitch + st->emitter_rotation_deg[0];
	cam->override_yaw   = inst->yaw   + st->emitter_rotation_deg[1];
	cam->override_roll  = -(inst->roll + st->emitter_rotation_deg[2]);
	cam->override_active = true;
}

void s3de_effect_update(struct RE_instance *inst)
{
	struct s3de_effect *eff = inst->s3de_effect;
	if (!eff || !inst->motion)
		return;
	update_sound(eff, inst->motion);
	float frame = inst->motion->current_frame;
	update_object_states(eff, frame);
	update_camera_override(inst, eff, frame);  // must come after update_object_states()
}

void s3de_calc_frame_range(struct s3de *s, struct motion *motion)
{
	int begin = s->loop_start_frame >= 0 ? s->loop_start_frame : 0;
	int end = s->loop_end_frame >= 0 ? s->loop_end_frame : begin;
	motion->frame_begin = motion->loop_frame_begin = begin;
	motion->frame_end = motion->loop_frame_end = end;
}
