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

#include <limits.h>
#include <cglm/cglm.h>

#include "system4.h"
#include "system4/aar.h"
#include "system4/cg.h"
#include "system4/hashtable.h"
#include "system4/utfsjis.h"

#include "3d_internal.h"

static inline float randf(void)
{
	return rand() / (float)RAND_MAX;
}

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
		return PARTICLE_TYPE_CAMERA_QUAKE;
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
	WARNING("unknown frame ref type \"%s\"", s);
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

static char **parse_qstr_list(char **ptext, int *nr, bool ignore_empty)
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
		char *s = parse_quoted_string(&text);
		if (!s) {
			for (int i = 0; i < *nr; i++)
				free(strs[i]);
			free(strs);
			return NULL;
		}
		if (s[0] == '\0' && ignore_empty)
			free(s);
		else
			strs[(*nr)++] = s;
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
			unit->bone.target_index = n;
			unit->bone.name = utf2sjis(string_buf, 0);
			unit->bone.offset[0] = x;
			unit->bone.offset[1] = y;
			unit->bone.offset[2] = -z;
		} else if (MATCH("ターゲット[ %d , %f , %f , %f ]", &n, &x, &y, &z)) {
			unit->type = PARTICLE_POS_UNIT_TARGET;
			unit->target.index = n;
			unit->target.offset[0] = x;
			unit->target.offset[1] = y;
			unit->target.offset[2] = -z;
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

static struct pae *parse_pae(char *text)
{
	struct pae *pae = xcalloc(1, sizeof(struct pae));
	struct pae_object *current_object = NULL;

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
				pae->objects = xrealloc_array(pae->objects, pae->nr_objects, pae->nr_objects + 1, sizeof(struct pae_object));
				current_object = &pae->objects[pae->nr_objects++];
				current_object->name = utf2sjis(string_buf, 0);
				// Default values.
				current_object->move_type = PARTICLE_MOVE_LINEAR;
				current_object->texture_anime_frame = 1.0;
				current_object->child_end_slope = 1.0;
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
				if (n > PARTICLE_UP_VECTOR_TYPE_MAX) {
					WARNING("unknown up vector type %d", n);
					n = PARTICLE_UP_VECTOR_TYPE_MAX;
				}
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
				current_object->textures = parse_qstr_list(&text, &current_object->nr_textures, true);
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
				current_object->rotation.begin[0] = f;
				current_object->rotation.end[0] = f2;
			} else if (MATCH("Ｙ軸自転角度 = %f , %f", &f, &f2)) {
				current_object->rotation.begin[1] = f;
				current_object->rotation.end[1] = f2;
			} else if (MATCH("Ｚ軸自転角度 = %f , %f", &f, &f2)) {
				current_object->rotation.begin[2] = f;
				current_object->rotation.end[2] = f2;
			} else if (MATCH("Ｘ軸公転角度 = %f , %f", &f, &f2)) {
				current_object->revolution_angle.begin[0] = f;
				current_object->revolution_angle.end[0] = f2;
			} else if (MATCH("Ｙ軸公転角度 = %f , %f", &f, &f2)) {
				current_object->revolution_angle.begin[1] = f;
				current_object->revolution_angle.end[1] = f2;
			} else if (MATCH("Ｚ軸公転角度 = %f , %f", &f, &f2)) {
				current_object->revolution_angle.begin[2] = f;
				current_object->revolution_angle.end[2] = f2;
			} else if (MATCH("Ｘ軸公転距離 = %f , %f", &f, &f2)) {
				current_object->revolution_distance.begin[0] = f;
				current_object->revolution_distance.end[0] = f2;
			} else if (MATCH("Ｙ軸公転距離 = %f , %f", &f, &f2)) {
				current_object->revolution_distance.begin[1] = f;
				current_object->revolution_distance.end[1] = f2;
			} else if (MATCH("Ｚ軸公転距離 = %f , %f", &f, &f2)) {
				current_object->revolution_distance.begin[2] = f;
				current_object->revolution_distance.end[2] = f2;
			} else if (MATCH("カーブＸ距離 = %f", &f)) {
				current_object->curve_length[0] = f;
			} else if (MATCH("カーブＹ距離 = %f", &f)) {
				current_object->curve_length[1] = f;
			} else if (MATCH("カーブＺ距離 = %f", &f)) {
				current_object->curve_length[2] = -f;
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
				if (n > PARTICLE_CHILD_MOVE_DIR_TYPE_MAX) {
					WARNING("unknown child move dir type %d", n);
					n = PARTICLE_CHILD_MOVE_DIR_TYPE_MAX;
				}
				current_object->child_move_dir_type = n;
			} else if (MATCH("方向タイプ = %d", &n)) {
				if (n > PARTICLE_DIR_TYPE_MAX) {
					WARNING("unknown dir type %d", n);
					n = PARTICLE_DIR_TYPE_MAX;
				}
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
	pae_free(pae);
	return NULL;
}

static void load_textures(struct pae *effect, struct archive *aar)
{
	effect->textures = ht_create(16);
	for (int i = 0; i < effect->nr_objects; i++) {
		struct pae_object *po = &effect->objects[i];
		for (int j = 0; j < po->nr_textures; j++) {
			const char *name = po->textures[j];
			if (ht_get(effect->textures, name, NULL))
				continue;  // already loaded.

			struct archive_data *dfile = RE_get_aar_entry(aar, effect->path, name, ".bmp");
			if (!dfile)
				dfile = RE_get_aar_entry(aar, effect->path, name, ".tga");
			if (!dfile) {
				WARNING("cannot load texture %s\\%s", effect->path, name);
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
			ht_put(effect->textures, name, bt);
			cg_free(cg);
		}
	}
}

static struct RE_instance *get_target(struct RE_instance *inst, int index)
{
	int target_id = inst->target[index];
	if (target_id < 0 || target_id >= inst->plugin->nr_instances)
		return NULL;
	return inst->plugin->instances[target_id];
}

static void eval_pos_for_object(struct RE_instance *inst, struct particle_position *pp, vec3 dest)
{
	glm_vec3_zero(dest);
	if (!pp)
		return;
	for (int i = 0; i < NR_PARTICLE_POSITION_UNITS; i++) {
		struct particle_position_unit *unit = &pp->units[i];
		switch (unit->type) {
		case PARTICLE_POS_UNIT_NONE:
			break;
		case PARTICLE_POS_UNIT_RAND:
		case PARTICLE_POS_UNIT_RAND_POSITIVE_Y:
			// These are computed per instance, in eval_pos_for_instance().
			break;
		case PARTICLE_POS_UNIT_ABSOLUTE:
			glm_vec3_add(dest, unit->absolute, dest);
			break;
		case PARTICLE_POS_UNIT_BONE:
			{
				struct RE_instance *target = get_target(inst, unit->bone.target_index);
				int bone = RE_instance_get_bone_index(target, unit->bone.name);
				vec3 target_pos;
				if (RE_instance_trans_local_pos_to_world_pos_by_bone(target, bone, unit->bone.offset, target_pos))
					glm_vec3_add(dest, target_pos, dest);
				else
					glm_vec3_add(dest, unit->bone.offset, dest);
			}
			break;
		case PARTICLE_POS_UNIT_TARGET:
			{
				struct RE_instance *target = get_target(inst, unit->target.index);
				if (!target)
					break;
				vec3 scale = {
					target->column_radius,
					target->column_height,
					target->column_radius
				};
				mat4 transform;
				glm_scale_make(transform, scale);
				glm_rotate_y(transform, -glm_rad(target->column_angle), transform);
				vec3 target_pos;
				glm_mat4_mulv3(transform, unit->target.offset, 1.0f, target_pos);

				glm_vec3_addadd(target->pos, target->column_pos, target_pos);
				glm_vec3_add(dest, target_pos, dest);
			}
			break;
		}
	}
}

static void eval_pos_for_instance(struct particle_position *pp, vec3 dest)
{
	glm_vec3_zero(dest);
	if (!pp)
		return;
	for (int i = 0; i < NR_PARTICLE_POSITION_UNITS; i++) {
		struct particle_position_unit *unit = &pp->units[i];
		switch (unit->type) {
		case PARTICLE_POS_UNIT_NONE:
			break;
		case PARTICLE_POS_UNIT_ABSOLUTE:
		case PARTICLE_POS_UNIT_BONE:
		case PARTICLE_POS_UNIT_TARGET:
			// These are computed per object, in eval_pos_for_object().
			break;
		case PARTICLE_POS_UNIT_RAND:
			dest[0] += (randf() * 2.0 - 1.0) * unit->rand.f;
			dest[1] += (randf() * 2.0 - 1.0) * unit->rand.f;
			dest[2] += (randf() * 2.0 - 1.0) * unit->rand.f;
			break;
		case PARTICLE_POS_UNIT_RAND_POSITIVE_Y:
			dest[0] += (randf() * 2.0 - 1.0) * unit->rand.f;
			dest[1] += randf() * unit->rand.f;
			dest[2] += (randf() * 2.0 - 1.0) * unit->rand.f;
			break;
		}
	}
}

static float eval_child_slope(struct pae_object *po)
{
	float slope = glm_lerp(po->child_begin_slope, po->child_end_slope, randf());
	if (slope > 1.0)
		slope = 0.5 + 0.5 / slope;  // ???
	return (1.0 + slope) * GLM_PI_2;
}

static void interpolate_pos(vec3 begin, vec3 end, float move_curve, float t, vec3 dest)
{
	if (move_curve == 0.0f) {
		glm_vec3_copy(end, dest);
		return;
	}
	float x = move_curve > 0.0f ? powf(t, move_curve) : 1.0f - powf(1.0f - t, -move_curve);
	glm_vec3_lerp(begin, end, x, dest);
}

static void init_particle_instances(struct particle_object *po)
{
	struct pae_object *pae_obj = po->pae_obj;
	po->instances = xcalloc(pae_obj->nr_particles, sizeof(struct particle_instance));

	for (int i = 0; i < pae_obj->nr_particles; i++) {
		struct particle_instance *pi = &po->instances[i];
		switch (pae_obj->move_type) {
		case PARTICLE_MOVE_FIXED:
		case PARTICLE_MOVE_LINEAR:
			pi->begin_frame = pae_obj->begin_frame;
			pi->end_frame = pae_obj->end_frame;
			break;
		case PARTICLE_MOVE_EMITTER:
			pi->begin_frame = glm_lerp(pae_obj->child_create_begin_frame, pae_obj->child_create_end_frame, (float)i / pae_obj->nr_particles);
			pi->end_frame = pi->begin_frame + pae_obj->child_frame;
			pi->roll_angle = eval_child_slope(pae_obj);
			pi->pitch_angle = randf() * (2.0f * GLM_PIf);
			break;
		}
		eval_pos_for_instance(pae_obj->position[0], pi->random_offset.begin);
		eval_pos_for_instance(pae_obj->position[1], pi->random_offset.end);
	}
}

struct pae *pae_load(struct archive *aar, const char *path)
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
	struct pae *pae = parse_pae(text);
	free(text);
	if (!pae) {
		WARNING("could not parse %s", dfile->name);
		archive_free_data(dfile);
		return NULL;
	}
	archive_free_data(dfile);

	pae->path = strdup(path);
	load_textures(pae, aar);
	for (int i = 0; i < pae->nr_objects; i++) {
		struct pae_object *po = &pae->objects[i];

		if (po->polygon_name && *po->polygon_name) {
			char *pol_path = xmalloc(strlen(path) + strlen(po->polygon_name) + 2);
			sprintf(pol_path, "%s\\%s", path, po->polygon_name);
			po->model = model_load(aar, pol_path);
			free(pol_path);
		}
	}

	return pae;
}

struct particle_effect *particle_effect_create(struct pae *pae)
{
	struct particle_effect *effect = xcalloc(1, sizeof(struct particle_effect));
	effect->pae = pae;
	effect->objects = xcalloc(pae->nr_objects, sizeof(struct particle_object));
	effect->camera_quake_enabled = true;
	for (int i = 0; i < pae->nr_objects; i++) {
		struct particle_object *po = &effect->objects[i];
		po->pae_obj = &pae->objects[i];
		init_particle_instances(po);
	}
	return effect;
}

static void free_billboard_texture(void *value)
{
	struct billboard_texture *bt = value;
	glDeleteTextures(1, &bt->texture);
	free(bt);
}

static void particle_position_free(struct particle_position *pos)
{
	if (!pos)
		return;
	for (int i = 0; i < NR_PARTICLE_POSITION_UNITS; i++) {
		if (pos->units[i].type == PARTICLE_POS_UNIT_BONE)
			free(pos->units[i].bone.name);
	}
	free(pos);
}

void pae_free(struct pae *pae)
{
	for (int i = 0; i < pae->nr_objects; i++) {
		struct pae_object *po = &pae->objects[i];
		free(po->name);
		for (int j = 0; j < NR_PARTICLE_POSITIONS; j++)
			particle_position_free(po->position[j]);
		free(po->sizes2);
		free(po->sizes_x);
		free(po->sizes_y);
		free(po->size_types);
		free(po->size_x_types);
		free(po->size_y_types);
		for (int j = 0; j < po->nr_textures; j++)
			free(po->textures[j]);
		free(po->textures);
		free(po->polygon_name);
		free(po->damages);
		if (po->model)
			model_free(po->model);
	}
	ht_foreach_value(pae->textures, free_billboard_texture);
	ht_free(pae->textures);
	free(pae->objects);
	free(pae->path);
	free(pae);
}

void particle_effect_free(struct particle_effect *effect)
{
	for (int i = 0; i < effect->pae->nr_objects; i++) {
		struct particle_object *obj = &effect->objects[i];
		free(obj->instances);
	}
	free(effect->objects);
	free(effect);
}

void pae_calc_frame_range(struct pae *pae, struct motion *motion)
{
	int begin_frame = INT_MAX;
	int end_frame = INT_MIN;
	for (int i = 0; i < pae->nr_objects; i++) {
		struct pae_object *po = &pae->objects[i];
		int obj_begin = po->begin_frame;
		int obj_end = max(po->end_frame, po->child_create_end_frame + po->child_frame);
		if (begin_frame > obj_begin)
			begin_frame = obj_begin;
		if (end_frame < obj_end)
			end_frame = obj_end;
	}
	motion->frame_begin = motion->loop_frame_begin = begin_frame;
	motion->frame_end = motion->loop_frame_end = end_frame;
}

static void update_camera_quake(struct RE_instance *inst, struct pae_object *po)
{
	float frame = inst->motion->current_frame - po->begin_frame;
	float total_frames = po->end_frame - po->begin_frame;
	if (frame < 0 || frame >= total_frames)
		return;

	const float ease_in_duration = 4.0f;
	const float ease_out_duration = 4.0f;
	float amplitude = 1.0f;
	if (total_frames - frame < ease_out_duration)
		amplitude *= (total_frames - frame) / ease_out_duration;
	else if (frame < ease_in_duration)
		amplitude *= frame / ease_in_duration;

	inst->plugin->camera.quake_pitch = sinf(frame * 1.1) * amplitude * -8.0f;
	inst->plugin->camera.quake_yaw = sinf(frame * 1.7) * amplitude * 12.0f;
}

void particle_effect_update(struct RE_instance *inst)
{
	struct particle_effect *effect = inst->effect;
	if (!effect)
		return;
	for (int i = 0; i < effect->pae->nr_objects; i++) {
		struct particle_object *po = &effect->objects[i];
		struct pae_object *pae_obj = po->pae_obj;
		if (pae_obj->type == PARTICLE_TYPE_CAMERA_QUAKE) {
			if (effect->camera_quake_enabled)
				update_camera_quake(inst, pae_obj);
			continue;
		}
		// These cannot be precomputed, because they may depend on the target's
		// *current* bone matrix.
		eval_pos_for_object(inst, pae_obj->position[0], po->pos.begin);
		eval_pos_for_object(inst, pae_obj->position[1], po->pos.end);
		glm_vec3_sub(po->pos.end, po->pos.begin, po->move_vec);
		switch (pae_obj->dir_type) {
		case PARTICLE_DIR_TYPE_NORMAL:
			glm_vec3_normalize(po->move_vec);
			glm_vec3_ortho(po->move_vec, po->move_ortho);
			glm_vec3_normalize(po->move_ortho);
			break;
		case PARTICLE_DIR_TYPE_XY_PLANE:
			po->move_vec[2] = 0.0f;
			glm_vec3_normalize(po->move_vec);
			glm_vec3_copy(GLM_ZUP, po->move_ortho);
			break;
		}
	}
}

static void calc_emitter_pos(struct particle_object *po, struct particle_instance *pi, float t, vec3 dest, vec3 move_vec)
{
	struct pae_object *pae_obj = po->pae_obj;
	vec3 begin, end;
	glm_vec3_add(po->pos.begin, pi->random_offset.begin, begin);
	glm_vec3_add(po->pos.end, pi->random_offset.end, end);
	interpolate_pos(begin, end, pae_obj->move_curve, t, dest);
	if (move_vec)
		glm_vec3_sub(end, begin, move_vec);

	// Orbital rotation.
	vec3 angle, dist;
	glm_vec3_lerp(pae_obj->revolution_angle.begin, pae_obj->revolution_angle.end, t, angle);
	glm_vec3_lerp(pae_obj->revolution_distance.begin, pae_obj->revolution_distance.end, t, dist);
	glm_vec3_scale(angle, GLM_PIf / 180.0f, angle);  // deg -> rad
	if (pae_obj->revolution_angle.begin[0] != 0.0f || pae_obj->revolution_angle.end[0] != 0.0f) {
		dest[1] += dist[0] * sinf(angle[0]);
		dest[2] -= dist[0] * cosf(angle[0]);
	}
	if (pae_obj->revolution_angle.begin[1] != 0.0f || pae_obj->revolution_angle.end[1] != 0.0f) {
		dest[0] += dist[1] * sinf(angle[1]);
		dest[2] -= dist[1] * cosf(angle[1]);
	}
	if (pae_obj->revolution_angle.begin[2] != 0.0f || pae_obj->revolution_angle.end[2] != 0.0f) {
		dest[1] += dist[2] * sinf(angle[2]);
		dest[0] += dist[2] * cosf(angle[2]);
	}

	// Apply the curve.
	glm_vec3_muladds(pae_obj->curve_length, 4.9f * t * (1.0f - t), dest);
}

static void calc_pos_and_up_vector(struct particle_object *po, struct particle_instance *pi, float t, vec3 pos, vec3 up_vector)
{
	struct pae_object *pae_obj = po->pae_obj;
	switch (pae_obj->move_type) {
	case PARTICLE_MOVE_FIXED:
		glm_vec3_add(po->pos.begin, pi->random_offset.begin, pos);
		glm_vec3_copy(GLM_YUP, up_vector);
		break;
	case PARTICLE_MOVE_LINEAR:
		calc_emitter_pos(po, pi, t, pos, up_vector);
		switch (pae_obj->up_vector_type) {
		case PARTICLE_UP_VECTOR_Y_AXIS:
		case PARTICLE_UP_VECTOR_INSTANCE_MOVE:
			glm_vec3_copy(GLM_YUP, up_vector);
			break;
		case PARTICLE_UP_VECTOR_OBJECT_MOVE:
			if (glm_vec3_norm2(up_vector) == 0.0f)
				glm_vec3_copy(GLM_XUP, up_vector);
			else
				glm_vec3_normalize(up_vector);
			break;
		}
		break;
	case PARTICLE_MOVE_EMITTER:
		{
			// Calculate the instance's initial and final position.
			float t0 = glm_clamp_zo(glm_percent(pae_obj->begin_frame, pae_obj->end_frame, pi->begin_frame));
			vec3_range inst_pos;
			calc_emitter_pos(po, pi, t0, inst_pos.begin, NULL);
			vec3 inst_move;
			glm_vec3_copy(po->move_vec, inst_move);
			switch (pae_obj->dir_type) {
			case PARTICLE_DIR_TYPE_NORMAL:
				glm_vec3_rotate(inst_move, pi->roll_angle, po->move_ortho);
				glm_vec3_rotate(inst_move, pi->pitch_angle, po->move_vec);
				break;
			case PARTICLE_DIR_TYPE_XY_PLANE:
				glm_vec3_rotate(inst_move, (pi->pitch_angle < GLM_PIf ? -1.0f : 1.0f) * pi->roll_angle, po->move_ortho);
				break;
			}
			glm_vec3_copy(inst_pos.begin, inst_pos.end);
			switch (pae_obj->child_move_dir_type) {
			case PARTICLE_CHILD_MOVE_DIR_NORMAL:
				glm_vec3_muladds(inst_move, pae_obj->child_length, inst_pos.end);
				break;
			case PARTICLE_CHILD_MOVE_DIR_REVERSE:
				glm_vec3_muladds(inst_move, pae_obj->child_length, inst_pos.begin);
				glm_vec3_negate(inst_move);
				break;
			}
			// Current position of the instance.
			glm_vec3_lerp(inst_pos.begin, inst_pos.end, t, pos);

			// Calculate the up vector.
			switch (pae_obj->up_vector_type) {
			case PARTICLE_UP_VECTOR_Y_AXIS:
				glm_vec3_copy(GLM_YUP, up_vector);
				break;
			case PARTICLE_UP_VECTOR_OBJECT_MOVE:
				glm_vec3_copy(po->move_vec, up_vector);
				break;
			case PARTICLE_UP_VECTOR_INSTANCE_MOVE:
				if (pae_obj->child_length != 0.0f)
					glm_vec3_copy(inst_move, up_vector);
				else
					glm_vec3_copy(GLM_XUP, up_vector);
				break;
			}
			if (pae_obj->dir_type == PARTICLE_DIR_TYPE_XY_PLANE) {
				mat3 rot = {{ inst_move[1], -inst_move[0], 0.0f },
							{ inst_move[0],  inst_move[1], 0.0f },
							{         0.0f,          0.0f, 1.0f }};
				glm_mat3_mulv(rot, up_vector, up_vector);
			}
		}
		break;
	}
}

static void calc_scale(struct pae_object *po, int frame, vec3 dest)
{
	glm_vec3_broadcast(po->sizes2[min(frame, po->nr_sizes2 - 1)], dest);
	if (po->nr_sizes_x)
		dest[0] *= po->sizes_x[min(frame, po->nr_sizes_x - 1)];
	if (po->nr_sizes_y)
		dest[1] *= po->sizes_y[min(frame, po->nr_sizes_y - 1)];
}

void particle_object_calc_local_transform(struct RE_instance *inst, struct particle_object *po, struct particle_instance *pi, float frame, mat4 dest)
{
	struct pae_object *pae_obj = po->pae_obj;
	float t = glm_percent(pi->begin_frame, pi->end_frame, frame);

	vec3 pos = {0}, up_vector;
	calc_pos_and_up_vector(po, pi, t, pos, up_vector);

	float rel_frame = frame - pi->begin_frame;
	int cur_step = rel_frame;
	int next_step = ceilf(rel_frame);
	float step_frac = rel_frame - cur_step;

	glm_translate_make(dest, pos);
	if (pae_obj->type == PARTICLE_TYPE_BILLBOARD) {
		mat4 view_mat;
		RE_calc_view_matrix(&inst->plugin->camera, up_vector, view_mat);
		mat3 rot;
		glm_mat4_pick3t(view_mat, rot);
		glm_mat4_ins3(rot, dest);
	}

	vec3 angles;
	glm_vec3_lerp(pae_obj->rotation.begin, pae_obj->rotation.end, t, angles);
	angles[0] *= -GLM_PIf / 180.0f;
	angles[1] *= -GLM_PIf / 180.0f;
	angles[2] *=  GLM_PIf / 180.0f;
	mat4 rotation_mat;
	glm_euler_zxy(angles, rotation_mat);
	glm_mat4_mul(dest, rotation_mat, dest);

	if (pae_obj->nr_sizes2 > 0) {
		vec3 scale, scale2;
		calc_scale(pae_obj, cur_step, scale);
		calc_scale(pae_obj, next_step, scale2);
		glm_vec3_lerp(scale, scale2, step_frac, scale);
		glm_scale(dest, scale);
	} else {
		glm_scale_uni(dest, glm_lerp(pae_obj->size[0], pae_obj->size[1], t));
	}

	glm_translate_x(dest, -pae_obj->offset_x);
	glm_translate_y(dest, -pae_obj->offset_y);
}

float pae_object_calc_alpha(struct pae_object *po, struct particle_instance *pi, float frame)
{
	frame -= pi->begin_frame;
	if (frame < po->alpha_fadein_frame)
		return glm_clamp_zo(frame / po->alpha_fadein_frame);
	float total_frames = pi->end_frame - pi->begin_frame;
	if (total_frames - po->alpha_fadeout_frame < frame)
		return glm_clamp_zo((total_frames - frame) / po->alpha_fadeout_frame);
	return 1.0;
}
