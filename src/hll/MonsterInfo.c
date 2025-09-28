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

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "system4/aar.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "hll.h"
#include "xsystem4.h"

enum motion_type {
	MOTION_UNSPECIFIED = 0,
	MOTION_LOOP = 2
};

struct motion {
	char *name;
	char *data_name;
	enum motion_type type;
	int frame_begin;
	int frame_end;
	int loop_frame_begin;
	int loop_frame_end;
	char *effect_name;
};

struct monster {
	char *name;
	char *poly_name;
	float scale;
	float pos_x;
	float pos_y;
	float pos_z;
	char *wear_effect_name;
	bool make_shadow;
	bool draw_shadow;
	float shadow_bone_radius;
	unsigned nr_motions;
	struct motion *motions;
};

static unsigned nr_monsters = 0;
static struct monster *monsters = NULL;

static void return_string(struct string **out, const char *s)
{
	if (*out)
		free_string(*out);
	*out = s ? cstr_to_string(s) : string_ref(&EMPTY_STRING);
}

static struct motion *get_motion(unsigned monster, unsigned motion)
{
	if (monster >= nr_monsters)
		return NULL;
	if (motion >= monsters[monster].nr_motions)
		return NULL;
	return &monsters[monster].motions[motion];
}

static void init_monster(struct monster *m)
{
	m->scale = 1.0;
	m->make_shadow = true;
	m->draw_shadow = true;
}

static bool parse_monster_info(char *text)
{
	struct monster *current_monster = NULL;
	struct motion *current_motion = NULL;

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

#define MATCH(fmt, ...) len = 0, sscanf(text, fmt " %n", ##__VA_ARGS__, &len), text += len, len
#define STRING_BUF_SIZE 64
#define QUOTED_STRING "\"%63[^\"]\""

		int len;
		unsigned n;
		char string_buf[STRING_BUF_SIZE];
		if (!current_monster) {
			// Toplevel
			if (!*text) { // EOF
				return true;
			} else if (MATCH("モンスター [ %u ] = {", &n)) {
				if (n >= nr_monsters) {
					monsters = xrealloc_array(monsters, nr_monsters, n + 1, sizeof(struct monster));
					nr_monsters = n + 1;
				}
				current_monster = &monsters[n];
				init_monster(current_monster);
			} else {
				char *eol = strchr(text, '\n');
				*eol = '\0';
				WARNING("Parse error in toplevel, near: \"%s\"", text);
				return false;
			}
		} else if (!current_motion) {
			// Monster context
			float f, x, y, z;
			if (!*text) {
				WARNING("unexpected EOF");
				return false;
			} else if (MATCH("}")) {
				current_monster = NULL;
			} else if (MATCH("モーション [ %u ] = {", &n)) {
				if (n >= current_monster->nr_motions) {
					current_monster->motions = xrealloc_array(current_monster->motions, current_monster->nr_motions, n + 1, sizeof(struct motion));
					current_monster->nr_motions = n + 1;
				}
				current_motion = &current_monster->motions[n];
			} else if (MATCH("名前 = " QUOTED_STRING, string_buf)) {
				current_monster->name = utf2sjis(string_buf, 0);
			} else if (MATCH("データ = " QUOTED_STRING, string_buf)) {
				current_monster->poly_name = utf2sjis(string_buf, 0);
			} else if (MATCH("スケール = %f", &f)) {
				current_monster->scale = f;
			} else if (MATCH("位置 = ( %f , %f , %f )", &x, &y, &z)) {
				current_monster->pos_x = x;
				current_monster->pos_y = y;
				current_monster->pos_z = z;
			} else if (MATCH("影生成 = %u", &n)) {
				current_monster->make_shadow = !!n;
			} else if (MATCH("影描画 = %u", &n)) {
				current_monster->draw_shadow = !!n;
			} else if (MATCH("影ボーン半径 = %f", &f)) {
				current_monster->shadow_bone_radius = f;
			} else if (MATCH("常備エフェクト = " QUOTED_STRING, string_buf)) {
				current_monster->wear_effect_name = utf2sjis(string_buf, 0);
			} else {
				char *eol = strchr(text, '\n');
				*eol = '\0';
				WARNING("Parse error in monster, near: \"%s\"", text);
				return false;
			}
		} else {
			// Motion context
			int begin, end;
			if (!*text) {
				WARNING("unexpected EOF");
				return false;
			} else if (MATCH("}")) {
				current_motion = NULL;
			} else if (MATCH("名前 = " QUOTED_STRING, string_buf)) {
				current_motion->name = utf2sjis(string_buf, 0);
			} else if (MATCH("データ = " QUOTED_STRING, string_buf)) {
				current_motion->data_name = utf2sjis(string_buf, 0);
// XXX: sscanf doesn't match this string on Windows for some reason...
#define LOOP_STR "タイプ\x09\x09= ループ"
			} else if (!strncmp(text, LOOP_STR, sizeof(LOOP_STR)-1)) {
				text += sizeof(LOOP_STR)-1;
				current_motion->type = MOTION_LOOP;
			} else if (MATCH("タイプ = ループ")) {
				current_motion->type = MOTION_LOOP;
			} else if (MATCH("フレーム = %d , %d", &begin, &end)) {
				current_motion->frame_begin = begin;
				current_motion->frame_end = end;
			} else if (MATCH("ループフレーム = %d , %d", &begin, &end)) {
				current_motion->loop_frame_begin = begin;
				current_motion->loop_frame_end = end;
			} else if (MATCH("エフェクト = " QUOTED_STRING, string_buf)) {
				current_motion->effect_name = utf2sjis(string_buf, 0);
			} else {
				char *eol = strchr(text, '\n');
				*eol = '\0';
				WARNING("Parse error in motion, near: \"%s\"", text);
				return false;
			}
		}
	}
}

static bool MonsterInfo_Load(struct string *filename)
{
	char *path = gamedir_path("Data/ReignData.red");
	int error = ARCHIVE_SUCCESS;
	struct archive *aar = (struct archive *)aar_open(path, MMAP_IF_64BIT, &error);
	if (error == ARCHIVE_FILE_ERROR) {
		WARNING("aar_open(\"%s\"): %s", display_utf0(path), strerror(errno));
	} else if (error == ARCHIVE_BAD_ARCHIVE_ERROR) {
		WARNING("aar_open(\"%s\"): invalid AAR archive", display_utf0(path));
	}
	free(path);
	if (!aar)
		return false;
	struct archive_data *dfile = archive_get_by_name(aar, filename->text);
	if (!dfile) {
		archive_free(aar);
		return false;
	}

	// We could parse the SJIS text directly, but convert it to UTF-8 for code
	// clarity.
	char *sjis_text = xmalloc(dfile->size + 1);
	memcpy(sjis_text, dfile->data, dfile->size);
	sjis_text[dfile->size] = '\0';
	char *text = sjis2utf(sjis_text, dfile->size);
	bool ok = parse_monster_info(text);
	free(text);
	free(sjis_text);
	archive_free_data(dfile);
	archive_free(aar);
	return ok;
}

static void MonsterInfo_Release(void)
{
	for (unsigned i = 0; i < nr_monsters; i++) {
		struct monster *m = &monsters[i];
		free(m->name);
		free(m->poly_name);
		free(m->wear_effect_name);
		for (unsigned j = 0; j < m->nr_motions; j++) {
			struct motion *mot = &m->motions[j];
			free(mot->name);
			free(mot->data_name);
			free(mot->effect_name);
		}
		free(m->motions);
	}
	free(monsters);
	monsters = NULL;
	nr_monsters = 0;
}

static int MonsterInfo_GetNumof(void)
{
	return nr_monsters;
}

static void MonsterInfo_GetViewName(int monster, struct string **name)
{
	if ((unsigned)monster >= nr_monsters)
		return;
	return_string(name, monsters[monster].name);
}

static void MonsterInfo_GetPolyName(int monster, struct string **name)
{
	if ((unsigned)monster >= nr_monsters)
		return;
	return_string(name, monsters[monster].poly_name);
}

static float MonsterInfo_GetSpecular(int monster)
{
	return 0.0;
}

static float MonsterInfo_GetPosX(int monster)
{
	if ((unsigned)monster >= nr_monsters)
		return 0.0;
	return monsters[monster].pos_x;
}

static float MonsterInfo_GetPosY(int monster)
{
	if ((unsigned)monster >= nr_monsters)
		return 0.0;
	return monsters[monster].pos_y;
}

static float MonsterInfo_GetPosZ(int monster)
{
	if ((unsigned)monster >= nr_monsters)
		return 0.0;
	return monsters[monster].pos_z;
}

static float MonsterInfo_GetScale(int monster)
{
	if ((unsigned)monster >= nr_monsters)
		return 0.0;
	return monsters[monster].scale;
}

static float MonsterInfo_GetShadowOffsetX(int monster)
{
	return 0.0;
}

static float MonsterInfo_GetShadowOffsetY(int monster)
{
	return 1.5;
}

static float MonsterInfo_GetShadowOffsetZ(int monster)
{
	return 0.0;
}

static float MonsterInfo_GetShadowBoxSizeX(int monster)
{
	return 3.0;
}

static float MonsterInfo_GetShadowBoxSizeY(int monster)
{
	return 3.0;
}

static float MonsterInfo_GetShadowBoxSizeZ(int monster)
{
	return 3.0;
}

static int MonsterInfo_GetNumofMotion(int monster)
{
	if ((unsigned)monster >= nr_monsters)
		return 0;
	return monsters[monster].nr_motions;
}

static void MonsterInfo_GetWearEffectName(int monster, struct string **name)
{
	if ((unsigned)monster >= nr_monsters)
		return;
	return_string(name, monsters[monster].wear_effect_name);
}

static int MonsterInfo_GetCameraSetting(int monster)
{
	return 0;
}

static void MonsterInfo_GetMotionName(int monster, int motion, struct string **name)
{
	struct motion *m = get_motion(monster, motion);
	return_string(name, m ? m->name : NULL);
}

static void MonsterInfo_GetMotionDataName(int monster, int motion, struct string **name)
{
	struct motion *m = get_motion(monster, motion);
	return_string(name, m ? m->data_name : NULL);
}

static int MonsterInfo_GetMotionType(int monster, int motion)
{
	struct motion *m = get_motion(monster, motion);
	return m ? m->type : 0;
}

static void MonsterInfo_GetMotionEffectName(int monster, int motion, struct string **name)
{
	struct motion *m = get_motion(monster, motion);
	return_string(name, m ? m->effect_name : NULL);
}

static float MonsterInfo_GetBeginFrame(int monster, int motion)
{
	struct motion *m = get_motion(monster, motion);
	return m ? m->frame_begin : 0.0;
}

static float MonsterInfo_GetEndFrame(int monster, int motion)
{
	struct motion *m = get_motion(monster, motion);
	return m ? m->frame_end : 0.0;
}

static float MonsterInfo_GetBeginLoopFrame(int monster, int motion)
{
	struct motion *m = get_motion(monster, motion);
	return m ? m->loop_frame_begin : 0.0;
}

static float MonsterInfo_GetEndLoopFrame(int monster, int motion)
{
	struct motion *m = get_motion(monster, motion);
	return  m ? m->loop_frame_end : 0.0;
}

static bool MonsterInfo_GetColumnPos(int monster, float *pos_x, float *pos_y, float *pos_z)
{
	*pos_x = *pos_y = *pos_z = 0.0;
	return true;
}

static float MonsterInfo_GetColumnHeight(int monster)
{
	return 2.0;
}

static float MonsterInfo_GetColumnRadius(int monster)
{
	return 1.0;
}

static int MonsterInfo_GetDrawShadow(int monster)
{
	if ((unsigned)monster >= nr_monsters)
		return 0;
	return monsters[monster].draw_shadow;
}

static int MonsterInfo_GetMakeShadow(int monster)
{
	if ((unsigned)monster >= nr_monsters)
		return 0;
	return monsters[monster].make_shadow;
}

static float MonsterInfo_GetShadowVolumeBoneRadius(int monster)
{
	if ((unsigned)monster >= nr_monsters)
		return 0.0;
	return monsters[monster].shadow_bone_radius;
}

static void MonsterInfo_ModuleFini(void)
{
	MonsterInfo_Release();
}

HLL_LIBRARY(MonsterInfo,
	    HLL_EXPORT(_ModuleFini, MonsterInfo_ModuleFini),
	    HLL_EXPORT(Load, MonsterInfo_Load),
	    HLL_EXPORT(Release, MonsterInfo_Release),
	    HLL_EXPORT(GetNumof, MonsterInfo_GetNumof),
	    HLL_EXPORT(GetViewName, MonsterInfo_GetViewName),
	    HLL_EXPORT(GetPolyName, MonsterInfo_GetPolyName),
	    HLL_EXPORT(GetSpecular, MonsterInfo_GetSpecular),
	    HLL_EXPORT(GetPosX, MonsterInfo_GetPosX),
	    HLL_EXPORT(GetPosY, MonsterInfo_GetPosY),
	    HLL_EXPORT(GetPosZ, MonsterInfo_GetPosZ),
	    HLL_EXPORT(GetScale, MonsterInfo_GetScale),
	    HLL_EXPORT(GetShadowOffsetX, MonsterInfo_GetShadowOffsetX),
	    HLL_EXPORT(GetShadowOffsetY, MonsterInfo_GetShadowOffsetY),
	    HLL_EXPORT(GetShadowOffsetZ, MonsterInfo_GetShadowOffsetZ),
	    HLL_EXPORT(GetShadowBoxSizeX, MonsterInfo_GetShadowBoxSizeX),
	    HLL_EXPORT(GetShadowBoxSizeY, MonsterInfo_GetShadowBoxSizeY),
	    HLL_EXPORT(GetShadowBoxSizeZ, MonsterInfo_GetShadowBoxSizeZ),
	    HLL_EXPORT(GetNumofMotion, MonsterInfo_GetNumofMotion),
	    HLL_EXPORT(GetWearEffectName, MonsterInfo_GetWearEffectName),
	    HLL_EXPORT(GetCameraSetting, MonsterInfo_GetCameraSetting),
	    HLL_EXPORT(GetMotionName, MonsterInfo_GetMotionName),
	    HLL_EXPORT(GetMotionDataName, MonsterInfo_GetMotionDataName),
	    HLL_EXPORT(GetMotionType, MonsterInfo_GetMotionType),
	    HLL_EXPORT(GetMotionEffectName, MonsterInfo_GetMotionEffectName),
	    HLL_EXPORT(GetBeginFrame, MonsterInfo_GetBeginFrame),
	    HLL_EXPORT(GetEndFrame, MonsterInfo_GetEndFrame),
	    HLL_EXPORT(GetBeginLoopFrame, MonsterInfo_GetBeginLoopFrame),
	    HLL_EXPORT(GetEndLoopFrame, MonsterInfo_GetEndLoopFrame),
	    HLL_EXPORT(GetColumnPos, MonsterInfo_GetColumnPos),
	    HLL_EXPORT(GetColumnHeight, MonsterInfo_GetColumnHeight),
	    HLL_EXPORT(GetColumnRadius, MonsterInfo_GetColumnRadius),
	    HLL_EXPORT(GetDrawShadow, MonsterInfo_GetDrawShadow),
	    HLL_EXPORT(GetMakeShadow, MonsterInfo_GetMakeShadow),
	    HLL_EXPORT(GetShadowVolumeBoneRadius, MonsterInfo_GetShadowVolumeBoneRadius)
	    );
