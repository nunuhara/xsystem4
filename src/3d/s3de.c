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

	return s;
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
	}
	free(s->objects);
	free(s->path);
	free(s);
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

static void interp_pos(struct s3de_position_entry *list, int n, float frame, vec3 dest)
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
	int k = 0;
	while (k + 1 < n && list[k + 1].frame <= frame)
		k++;
	struct s3de_position_entry *a = &list[k];
	struct s3de_position_entry *b = &list[k + 1];
	if (a->interp == S3DE_INTERP_NONE) {
		glm_vec3_copy(a->pos, dest);
		return;
	}
	float t = (frame - a->frame) / (float)(b->frame - a->frame);
	glm_vec3_lerp(a->pos, b->pos, t, dest);
}

struct s3de_effect *s3de_effect_create(struct s3de *s, struct archive *aar)
{
	struct s3de_effect *eff = xcalloc(1, sizeof(struct s3de_effect));
	eff->s3de = s;
	eff->wav_channel = -1;

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

static void update_camera_override(struct RE_instance *inst, struct s3de_effect *eff, float frame)
{
	int cam_idx = select_active_camera_obj(eff, frame);
	if (cam_idx < 0 || !inst->plugin)
		return;

	struct s3de_object *obj = &eff->s3de->objects[cam_idx];
	vec3 emitter_pos, emitter_rotation_deg;
	interp_pos(obj->position_list, obj->nr_position_entries, frame, emitter_pos);
	interp_vec3(obj->rotation_list, obj->nr_rotation_entries, frame,
		    emitter_rotation_deg, GLM_VEC3_ZERO);

	if (inst->local_transform_needs_update)
		RE_instance_update_local_transform(inst);
	vec4 local_pos = { emitter_pos[0], emitter_pos[1], emitter_pos[2], 1.0f };
	vec4 world_pos;
	glm_mat4_mulv(inst->local_transform, local_pos, world_pos);
	struct RE_camera *cam = &inst->plugin->camera;
	glm_vec3_copy(world_pos, cam->override_pos);
	// XXX: Compose the instance and camera-object rotations by summing Euler
	// angles directly. This is not equivalent to true rotation composition in
	// general, but it is exact here: in Rance 9 non-camera instances are only
	// ever rotated about yaw. Since yaw is the outermost axis in the renderer's
	// YXZ convention, the two yaw rotations combine exactly by addition.
	cam->override_pitch = inst->pitch + emitter_rotation_deg[0];
	cam->override_yaw   = inst->yaw   + emitter_rotation_deg[1];
	cam->override_roll  = -(inst->roll + emitter_rotation_deg[2]);
	cam->override_active = true;
}

void s3de_effect_update(struct RE_instance *inst)
{
	struct s3de_effect *eff = inst->s3de_effect;
	if (!eff || !inst->motion)
		return;
	update_sound(eff, inst->motion);
	float frame = inst->motion->current_frame;
	update_camera_override(inst, eff, frame);
}

void s3de_calc_frame_range(struct s3de *s, struct motion *motion)
{
	int begin = s->loop_start_frame >= 0 ? s->loop_start_frame : 0;
	int end = s->loop_end_frame >= 0 ? s->loop_end_frame : begin;
	motion->frame_begin = motion->loop_frame_begin = begin;
	motion->frame_end = motion->loop_frame_end = end;
}
