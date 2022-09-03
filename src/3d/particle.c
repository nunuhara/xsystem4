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

#include "system4.h"
#include "system4/aar.h"
#include "system4/utfsjis.h"

#include "3d_internal.h"

static void parse_error(const char *msg, char *input)
{
	char *eol = strchr(input, '\n');
	*eol = '\0';
	WARNING("Parse error: %s Near: \"%s\"", msg, input);
}

static enum particle_type parse_particle_type(const char *s)
{
	if (!strcmp(s, "ビルボード"))
		return PARTICLE_TYPE_BILLBOARD;
	else if (!strcmp(s, "ポリゴンオブジェクト"))
		return PARTICLE_TYPE_POLYGON_OBJECT;
	else if (!strcmp(s, "剣ブラー"))
		return PARTICLE_TYPE_SWORD_BLUR;
	else if (!strcmp(s, "カメラ振動"))
		return PARTICLE_TYPE_CAMERA_VIBRATION;
	WARNING("unknown object type \"%s\"", s);
	return (enum particle_type)-1;
}

static enum particle_move_type parse_particle_move_type(const char *s)
{
	if (!strcmp(s, "固定"))
		return PARTICLE_MOVE_FIXED;
	else if (!strcmp(s, "直線"))
		return PARTICLE_MOVE_LINEAR;
	else if (!strcmp(s, "放射"))
		return PARTICLE_MOVE_EMITTER;
	WARNING("unknown move type \"%s\"", s);
	return (enum particle_move_type)-1;
}

static enum particle_blend_type parse_particle_blend_type(const char *s)
{
	if (!strcmp(s, "標準"))
		return PARTICLE_BLEND_NORMAL;
	else if (!strcmp(s, "加算"))
		return PARTICLE_BLEND_ADDITIVE;
	WARNING("unknown blend type \"%s\"", s);
	return (enum particle_blend_type)-1;
}

static enum particle_frame_ref_type parse_particle_frame_ref_type(const char *s, int *param)
{
	if (!strcmp(s, "エフェクト"))
		return PARTICLE_FRAME_REF_EFFECT;
	else if (sscanf(s, "ターゲット[%d]", param) == 1)
		return PARTICLE_FRAME_REF_TARGET;
	WARNING("unknown blend type \"%s\"", s);
	return (enum particle_frame_ref_type)-1;
}

static char *parse_quoted_string(char **ptext)
{
	char *text = *ptext;

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

#define MATCH(fmt, ...) (len = 0, sscanf(text, fmt " %n", ##__VA_ARGS__, &len), text += len, len)
#define STRING_BUF_SIZE 64
#define KEYWORD "%63s"
#define QUOTED_STRING "\"%63[^\"]\""

static float *parse_float_list(char **ptext, int *nr)
{
	char *text = *ptext;
	int len;
	float f;

	if (!MATCH("%f", &f)) {
		parse_error("float expected.", text);
		return NULL;
	}

	int cap = 16;
	float *floats = xmalloc(cap * sizeof(float));
	floats[0] = f;
	*nr = 1;
	while (MATCH(" , %f", &f)) {
		if (*nr == cap) {
			cap *= 2;
			floats = xrealloc(floats, cap * sizeof(float));
		}
		floats[(*nr)++] = f;
	}
	*ptext = text;
	return floats;
}

static int *parse_int_list(char **ptext, int *nr)
{
	char *text = *ptext;
	int len;
	int n;

	if (!MATCH("%d", &n)) {
		parse_error("int expected.", text);
		return NULL;
	}

	int cap = 16;
	int *ints = xmalloc(cap * sizeof(int));
	ints[0] = n;
	*nr = 1;
	while (MATCH(" , %d", &n)) {
		if (*nr == cap) {
			cap *= 2;
			ints = xrealloc(ints, cap * sizeof(int));
		}
		ints[(*nr)++] = n;
	}
	*ptext = text;
	return ints;
}

static char **parse_qstr_list(char **ptext, int *nr)
{
	char *text = *ptext;
	int len;

	int cap = 16;
	char **strs = xmalloc(cap * sizeof(char *));
	*nr = 0;
	do {
		if (*nr == cap) {
			cap *= 2;
			strs = xrealloc(strs, cap * sizeof(char *));
		}
		strs[*nr] = parse_quoted_string(&text);
		if (!strs[*nr]) {
			for (int i = 0; i < *nr; i++)
				free(strs[i]);
			free(strs);
			return NULL;
		}
		(*nr)++;
	} while (MATCH(" , "));
	*ptext = text;
	return strs;
}

static struct particle_position *parse_position(char **ptext)
{
	char *text = *ptext;
	int len;

	struct particle_position *epos = xcalloc(1, sizeof(struct particle_position));

	int n;
	float x, y, z;
	char string_buf[STRING_BUF_SIZE];
	for (int i = 0; i < NR_PARTICLE_POSITION_UNITS; i++) {
		struct particle_position_unit *unit = &epos->units[i];
		if (MATCH("なし")) {
			unit->type = PARTICLE_POS_UNIT_NONE;
		} else if (MATCH("絶対位置( %f , %f , %f )", &x, &y, &z)) {
			unit->type = PARTICLE_POS_UNIT_ABSOLUTE;
			unit->absolute[0] = x;
			unit->absolute[1] = y;
			unit->absolute[2] = -z;
		} else if (MATCH("ボーン[ %d , "QUOTED_STRING" , %f , %f , %f ]", &n, string_buf, &x, &y, &z) ||
				   MATCH("ボーン[ %d , \"\" , %f , %f , %f ]", &n, &x, &y, &z)) {
			unit->type = PARTICLE_POS_UNIT_BONE;
			unit->bone.n = n;
			// unit->bone.name = utf2sjis(string_buf, 0);
			unit->bone.pos[0] = x;
			unit->bone.pos[1] = y;
			unit->bone.pos[2] = -z;
		} else if (MATCH("ターゲット[ %d , %f , %f , %f ]", &n, &x, &y, &z)) {
			unit->type = PARTICLE_POS_UNIT_TARGET;
			unit->target.n = n;
			unit->target.pos[0] = x;
			unit->target.pos[1] = y;
			unit->target.pos[2] = -z;
		} else if (MATCH("ランダム方向( %f )", &x)) {
			unit->type = PARTICLE_POS_UNIT_RAND;
			unit->rand.f = x;
		} else if (MATCH("ランダム方向Ｙ＋( %f )", &x)) {
			unit->type = PARTICLE_POS_UNIT_RAND_POSITIVE_Y;
			unit->rand.f = x;
		} else {
			parse_error("unknown position unit.", text);
			free(epos);
			return NULL;
		}
		if (!MATCH(" + "))
			break;
	}

	*ptext = text;
	return epos;
}

static struct particle_effect *parse_pae(char *text)
{
	struct particle_effect *pae = xcalloc(1, sizeof(struct particle_effect));
	struct particle_object *current_object = NULL;

	for (;;) {
		// Skip whitespaces and comments
		for (;;) {
			while (isspace(*text))
				text++;
			if (text[0] == '/' && text[1] == '/') {
				char *newline = strchr(text, '\n');
				if (newline)
					text = newline + 1;
				else
					text += strlen(text);
			} else {
				break;
			}
		}

		int len;
		int n, n2;
		float f, f2;
		char string_buf[STRING_BUF_SIZE];
		if (!current_object) {
			// Toplevel
			if (!*text) { // EOF
				return pae;
			} else if (MATCH("オブジェクト " QUOTED_STRING " = {", string_buf)) {
				pae->objects = xrealloc_array(pae->objects, pae->nr_objects, pae->nr_objects + 1, sizeof(struct particle_object));
				current_object = &pae->objects[pae->nr_objects++];
				current_object->name = utf2sjis(string_buf, 0);
				// Default values.
				current_object->move_type = PARTICLE_MOVE_LINEAR;
			} else {
				parse_error("unknown toplevel.", text);
				goto err;
			}
		} else {
			// Object context
			if (!*text) {
				WARNING("unexpected EOF");
				return false;
			} else if (MATCH("}")) {
				current_object = NULL;
			} else if (MATCH("種類 = " KEYWORD, string_buf)) {
				current_object->type = parse_particle_type(string_buf);
				if (current_object->type < 0)
					goto err;
			} else if (MATCH("移動タイプ = " KEYWORD, string_buf)) {
				current_object->move_type = parse_particle_move_type(string_buf);
				if (current_object->move_type < 0)
					goto err;
			} else if (MATCH("アップベクトルタイプ = %d", &n)) {
				current_object->up_vector_type = n;
			} else if (MATCH("移動カーブ = %f", &f)) {
				current_object->move_curve = f;
			} else if (MATCH("位置 = ")) {
				current_object->position[0] = parse_position(&text);
				if (!current_object->position[0])
					goto err;
				if (MATCH(" , ")) {
					current_object->position[1] = parse_position(&text);
					if (!current_object->position[1])
						goto err;
				}
			} else if (MATCH("サイズ = %f", &f)) {
				current_object->size[0] = f;
				if (MATCH(" , %f", &f2))
					current_object->size[1] = f2;
				else
					current_object->size[1] = f;
			} else if (MATCH("サイズ2 = ")) {
				current_object->sizes2 = parse_float_list(&text, &current_object->nr_sizes2);
				if (!current_object->sizes2)
					goto err;
			} else if (MATCH("サイズＸ = ")) {
				current_object->sizes_x = parse_float_list(&text, &current_object->nr_sizes_x);
				if (!current_object->sizes_x)
					goto err;
			} else if (MATCH("サイズＹ = ")) {
				current_object->sizes_y = parse_float_list(&text, &current_object->nr_sizes_y);
				if (!current_object->sizes_y)
					goto err;
			} else if (MATCH("サイズタイプ = ")) {
				current_object->size_types = parse_int_list(&text, &current_object->nr_size_types);
				if (!current_object->size_types)
					goto err;
			} else if (MATCH("サイズＸタイプ = ")) {
				current_object->size_x_types = parse_int_list(&text, &current_object->nr_size_x_types);
				if (!current_object->size_x_types)
					goto err;
			} else if (MATCH("サイズＹタイプ = ")) {
				current_object->size_y_types = parse_int_list(&text, &current_object->nr_size_y_types);
				if (!current_object->size_y_types)
					goto err;
			} else if (MATCH("テクスチャ = ")) {
				current_object->textures = parse_qstr_list(&text, &current_object->nr_textures);
				if (!current_object->textures)
					goto err;
			} else if (MATCH("ブレンドタイプ = " KEYWORD, string_buf)) {
				current_object->blend_type = parse_particle_blend_type(string_buf);
				if (current_object->blend_type < 0)
					goto err;
			} else if (MATCH("テクスチャアニメフレーム = %f", &f)) {
				current_object->texture_anime_frame = f;
			} else if (MATCH("テクスチャアニメ時間 = %d", &n)) {
				current_object->texture_anime_time = n;
			} else if (MATCH("ポリゴン = ")) {
				current_object->polygon_name = parse_quoted_string(&text);
				if (!current_object->polygon_name)
					goto err;
			} else if (MATCH("フレーム = %d , %d", &n, &n2)) {
				current_object->begin_frame = n;
				current_object->end_frame = n2;
			} else if (MATCH("停止フレーム = %d", &n)) {
				current_object->stop_frame = n;
			} else if (MATCH("フレーム参照 = " KEYWORD, string_buf)) {
				current_object->frame_ref_type = parse_particle_frame_ref_type(string_buf, &current_object->frame_ref_param);
				if (current_object->frame_ref_type < 0)
					goto err;
			} else if (MATCH("個数 = %d", &n)) {
				current_object->nr_particles = n;
			} else if (MATCH("アルファフェードインフレーム = %f", &f)) {
				current_object->alpha_fadein_frame = f;
			} else if (MATCH("アルファフェードイン時間 = %d", &n)) {
				current_object->alpha_fadein_time = n;
			} else if (MATCH("アルファフェードアウトフレーム = %f", &f)) {
				current_object->alpha_fadeout_frame = f;
			} else if (MATCH("アルファフェードアウト時間 = %d", &n)) {
				current_object->alpha_fadeout_time = n;
			} else if (MATCH("Ｘ軸自転角度 = %f , %f", &f, &f2)) {
				current_object->rotation[0][0] = f;
				current_object->rotation[1][0] = f2;
			} else if (MATCH("Ｙ軸自転角度 = %f , %f", &f, &f2)) {
				current_object->rotation[0][1] = f;
				current_object->rotation[1][1] = f2;
			} else if (MATCH("Ｚ軸自転角度 = %f , %f", &f, &f2)) {
				current_object->rotation[0][2] = f;
				current_object->rotation[1][2] = f2;
			} else if (MATCH("Ｘ軸公転角度 = %f , %f", &f, &f2)) {
				current_object->revolution_angle[0][0] = f;
				current_object->revolution_angle[1][0] = f2;
			} else if (MATCH("Ｙ軸公転角度 = %f , %f", &f, &f2)) {
				current_object->revolution_angle[0][1] = f;
				current_object->revolution_angle[1][1] = f2;
			} else if (MATCH("Ｚ軸公転角度 = %f , %f", &f, &f2)) {
				current_object->revolution_angle[0][2] = f;
				current_object->revolution_angle[1][2] = f2;
			} else if (MATCH("Ｘ軸公転距離 = %f , %f", &f, &f2)) {
				current_object->revolution_distance[0][0] = f;
				current_object->revolution_distance[1][0] = f2;
			} else if (MATCH("Ｙ軸公転距離 = %f , %f", &f, &f2)) {
				current_object->revolution_distance[0][1] = f;
				current_object->revolution_distance[1][1] = f2;
			} else if (MATCH("Ｚ軸公転距離 = %f , %f", &f, &f2)) {
				current_object->revolution_distance[0][2] = f;
				current_object->revolution_distance[1][2] = f2;
			} else if (MATCH("カーブＸ距離 = %f", &f)) {
				current_object->curve_length[0] = f;
			} else if (MATCH("カーブＹ距離 = %f", &f)) {
				current_object->curve_length[1] = f;
			} else if (MATCH("カーブＺ距離 = %f", &f)) {
				current_object->curve_length[2] = f;
			} else if (MATCH("子フレーム = %d", &n)) {
				current_object->child_frame = n;
			} else if (MATCH("子距離 = %f", &f)) {
				current_object->child_length = f;
			} else if (MATCH("子傾き = %f", &f)) {
				current_object->child_begin_slope = f;
			} else if (MATCH("子傾き２ = %f", &f)) {
				current_object->child_end_slope = f;
			} else if (MATCH("子生成開始フレーム = %f", &f)) {
				current_object->child_create_begin_frame = f;
			} else if (MATCH("子生成終了フレーム = %f", &f)) {
				current_object->child_create_end_frame = f;
			} else if (MATCH("子移動方向タイプ = %d", &n)) {
				current_object->child_move_dir_type = n;
			} else if (MATCH("方向タイプ = %d", &n)) {
				current_object->dir_type = n;
			} else if (MATCH("ダメージ = ")) {
				current_object->damages = parse_int_list(&text, &current_object->nr_damages);
				if (!current_object->damages)
					goto err;
			} else if (MATCH("オフセットＸ = %f", &f)) {
				current_object->offset_x = f;
			} else if (MATCH("オフセットＹ = %f", &f)) {
				current_object->offset_y = f;
			} else {
				parse_error("unknown object field.", text);
				goto err;
			}
		}
	}
 err:
	particle_effect_free(pae);
	return NULL;
}

struct particle_effect *particle_effect_load(struct archive *aar, const char *path)
{
	const char *basename = strrchr(path, '\\');
	basename = basename ? basename + 1 : path;

	struct archive_data *dfile = RE_get_aar_entry(aar, path, basename, ".pae");
	if (!dfile) {
		WARNING("%s\\%s.pae: not found", path, basename);
		return NULL;
	}

	char *sjis_text = xmalloc(dfile->size + 1);
	memcpy(sjis_text, dfile->data, dfile->size);
	// Replace null characters (written by buggy .pae formatter) with spaces.
	for (size_t i = 0; i < dfile->size; i++) {
		if (sjis_text[i] == '\0')
			sjis_text[i] = ' ';
	}
	sjis_text[dfile->size] = '\0';

	// We could parse the SJIS text directly, but convert it to UTF-8 for code
	// clarity.
	char *text = sjis2utf(sjis_text, dfile->size);
	free(sjis_text);
	struct particle_effect *effect = parse_pae(text);
	free(text);
	if (!effect) {
		WARNING("could not parse %s", dfile->name);
		archive_free_data(dfile);
		return NULL;
	}
	archive_free_data(dfile);

	effect->path = strdup(path);

	return effect;
}

void particle_effect_free(struct particle_effect *effect)
{
	for (int i = 0; i < effect->nr_objects; i++) {
		struct particle_object *obj = &effect->objects[i];
		free(obj->name);
		free(obj->position[0]);
		free(obj->position[1]);
		free(obj->sizes2);
		free(obj->sizes_x);
		free(obj->sizes_y);
		free(obj->size_types);
		free(obj->size_x_types);
		free(obj->size_y_types);
		for (int j = 0; j < obj->nr_textures; j++)
			free(obj->textures[j]);
		free(obj->textures);
		free(obj->polygon_name);
		free(obj->damages);
	}
	free(effect->objects);
	free(effect->path);
	free(effect);
}
