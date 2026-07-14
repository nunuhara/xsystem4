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

#include "system4/string.h"

#include "3d_internal.h"
#include "cJSON.h"
#include "debugger.h"
#include "gfx/gfx.h"
#include "json.h"
#include "reign.h"
#include "sact.h"
#include "scene.h"
#include "sprite.h"
#include "xsystem4.h"

#define SPREAD_VEC3(v) (v)[0], (v)[1], (v)[2]

static const char *instance_type_name(enum RE_instance_type type)
{
	switch (type) {
	case RE_ITYPE_UNINITIALIZED:     return "UNINITIALIZED";
	case RE_ITYPE_STATIC:            return "STATIC";
	case RE_ITYPE_SKINNED:           return "SKINNED";
	case RE_ITYPE_BILLBOARD:         return "BILLBOARD";
	case RE_ITYPE_DIRECTIONAL_LIGHT: return "DIRECTIONAL_LIGHT";
	case RE_ITYPE_SPECULAR_LIGHT:    return "SPECULAR_LIGHT";
	case RE_ITYPE_PARTICLE_EFFECT:   return "PARTICLE_EFFECT";
	case RE_ITYPE_PATH_LINE:         return "PATH_LINE";
	}
	return "UNKNOWN";
}

static const char *motion_state_name(enum RE_motion_state state)
{
	switch (state) {
	case RE_MOTION_STATE_STOP:   return "STOP";
	case RE_MOTION_STATE_NOLOOP: return "NOLOOP";
	case RE_MOTION_STATE_LOOP:   return "LOOP";
	}
	return "UNKNOWN";
}

static const char *fog_type_name(enum RE_fog_type type)
{
	switch (type) {
	case RE_FOG_NONE:             return "NONE";
	case RE_FOG_LINEAR:           return "LINEAR";
	case RE_FOG_LIGHT_SCATTERING: return "LIGHT_SCATTERING";
	}
	return "UNKNOWN";
}

static cJSON *motion_to_json(struct motion *m)
{
	if (m->instance->type == RE_ITYPE_BILLBOARD) {
		cJSON *obj = cJSON_CreateObject();
		cJSON_AddStringToObject(obj, "state", motion_state_name(m->state));
		cJSON_AddNumberToObject(obj, "frame", m->current_frame);
		cJSON_AddItemToObjectCS(obj, "range", num2_to_json(m->frame_begin, m->frame_end));
		cJSON_AddItemToObjectCS(obj, "loop_range", num2_to_json(m->loop_frame_begin, m->loop_frame_end));
		return obj;
	}
	if (m->mot) {
		cJSON *obj = cJSON_CreateObject();
		cJSON_AddStringToObject(obj, "name", m->mot->name);
		cJSON_AddStringToObject(obj, "state", motion_state_name(m->state));
		cJSON_AddNumberToObject(obj, "frame", m->current_frame);
		return obj;
	}
	return cJSON_CreateNull();
}

static cJSON *instance_to_json(struct RE_instance *inst, int index, bool verbose)
{
	cJSON *obj = cJSON_CreateObject();
	cJSON_AddNumberToObject(obj, "index", index);
	cJSON_AddStringToObject(obj, "type", instance_type_name(inst->type));
	if (inst->type == RE_ITYPE_DIRECTIONAL_LIGHT || inst->type == RE_ITYPE_SPECULAR_LIGHT) {
		cJSON_AddItemToObjectCS(obj, "vec", vec3_point_to_json(inst->vec, verbose));
		cJSON_AddItemToObjectCS(obj, "diffuse", vec3_color_to_json(inst->diffuse, verbose));
		cJSON_AddItemToObjectCS(obj, "globe_diffuse", vec3_color_to_json(inst->globe_diffuse, verbose));
	} else {
		cJSON_AddNumberToObject(obj, "draw", inst->draw);
		cJSON_AddItemToObjectCS(obj, "pos", vec3_point_to_json(inst->pos, verbose));
		cJSON_AddItemToObjectCS(obj, "scale", vec3_point_to_json(inst->scale, verbose));
		cJSON_AddItemToObjectCS(obj, "ambient", vec3_color_to_json(inst->ambient, verbose));
	}
	if (inst->model) {
		cJSON *tmp;
		cJSON_AddStringToObject(obj, "path", inst->model->path);
		cJSON_AddItemToObjectCS(obj, "aabb", tmp = cJSON_CreateObject());
		cJSON_AddItemToObjectCS(tmp, "min", vec3_point_to_json(inst->model->aabb[0], verbose));
		cJSON_AddItemToObjectCS(tmp, "max", vec3_point_to_json(inst->model->aabb[1], verbose));
	}
	if (inst->motion) {
		cJSON_AddNumberToObject(obj, "fps", inst->fps);
		cJSON_AddItemToObjectCS(obj, "motion", motion_to_json(inst->motion));
	}
	if (inst->next_motion) {
		cJSON_AddItemToObjectCS(obj, "next_motion", motion_to_json(inst->next_motion));
	}
	if (inst->motion_blend) {
		cJSON_AddNumberToObject(obj, "motion_blend_rate", inst->motion_blend_rate);
	}
	if (inst->effect) {
		cJSON_AddStringToObject(obj, "path", inst->effect->pae->path);
	}
	if (inst->s3de_effect && inst->s3de_effect->s3de) {
		cJSON_AddStringToObject(obj, "path", inst->s3de_effect->s3de->path);
	}

	return obj;
}

static cJSON *back_cg_to_json(struct RE_back_cg *bcg, int index, bool verbose)
{
	cJSON *obj = cJSON_CreateObject();
	cJSON_AddNumberToObject(obj, "index", index);
	if (bcg->no) {
		cJSON_AddNumberToObject(obj, "no", bcg->no);
	}
	if (bcg->name) {
		cJSON_AddStringToObject(obj, "name", bcg->name->text);
	}
	if (!verbose) {
		cJSON_AddItemToObjectCS(obj, "pos", num2_to_json(bcg->x, bcg->y));
	} else {
		cJSON *pos;
		cJSON_AddItemToObjectCS(obj, "pos", pos = cJSON_CreateObject());
		cJSON_AddNumberToObject(pos, "x", bcg->x);
		cJSON_AddNumberToObject(pos, "y", bcg->y);
	}
	cJSON_AddNumberToObject(obj, "blend_rate", bcg->blend_rate);
	cJSON_AddNumberToObject(obj, "mag", bcg->mag);
	cJSON_AddNumberToObject(obj, "show", bcg->show);
	return obj;
}

static cJSON *re_plugin_to_json(struct RE_plugin *p, bool verbose)
{
	cJSON *obj, *cam, *a;

	obj = cJSON_CreateObject();
	cJSON_AddStringToObject(obj, "name", p->plugin.name);
	cJSON_AddItemToObjectCS(obj, "camera", cam = cJSON_CreateObject());
	cJSON_AddItemToObjectCS(cam, "pos", vec3_point_to_json(p->camera.pos, verbose));
	cJSON_AddNumberToObject(cam, "pitch", p->camera.pitch);
	cJSON_AddNumberToObject(cam, "roll", p->camera.roll);
	cJSON_AddNumberToObject(cam, "yaw", p->camera.yaw);
	cJSON_AddItemToObjectCS(obj, "shadow_map_light_dir",
			vec3_point_to_json(p->shadow_map_light_dir, verbose));
	cJSON_AddNumberToObject(obj, "shadow_bias", p->shadow_bias);
	cJSON_AddStringToObject(obj, "fog_type", fog_type_name(p->fog_type));
	switch (p->fog_type) {
	case RE_FOG_NONE:
		break;
	case RE_FOG_LINEAR:
		cJSON_AddNumberToObject(obj, "fog_near", p->fog_near);
		cJSON_AddNumberToObject(obj, "fog_far", p->fog_far);
		break;
	case RE_FOG_LIGHT_SCATTERING:
		cJSON_AddNumberToObject(obj, "ls_beta_r", p->ls_beta_r);
		cJSON_AddNumberToObject(obj, "ls_beta_m", p->ls_beta_m);
		cJSON_AddNumberToObject(obj, "ls_g", p->ls_g);
		cJSON_AddNumberToObject(obj, "ls_distance", p->ls_distance);
		cJSON_AddItemToObjectCS(obj, "ls_light_dir",
				vec3_point_to_json(p->ls_light_dir, verbose));
		cJSON_AddItemToObjectCS(obj, "ls_light_color",
				vec3_color_to_json(p->ls_light_color, verbose));
		cJSON_AddItemToObjectCS(obj, "ls_sun_color",
				vec3_color_to_json(p->ls_sun_color, verbose));
		break;
	}

	cJSON_AddItemToObjectCS(obj, "instances", a = cJSON_CreateArray());
	for (int i = 0; i < p->nr_instances; i++) {
		if (!p->instances[i])
			continue;
		cJSON_AddItemToArray(a, instance_to_json(p->instances[i], i, verbose));
		
	}

	cJSON_AddItemToObjectCS(obj, "back_cgs", a = cJSON_CreateArray());
	for (int i = 0; i < RE_NR_BACK_CGS; i++) {
		struct RE_back_cg *bcg = &p->back_cg[i];
		if (!bcg->name && !bcg->no)
			continue;
		cJSON_AddItemToArray(a, back_cg_to_json(bcg, i, verbose));
	}

	return obj;
}

cJSON *RE_to_json(struct sact_sprite *sp, bool verbose)
{
	return re_plugin_to_json((struct RE_plugin*)sp->plugin, verbose);
}

#ifdef DEBUGGER_ENABLED

static int dbg_current_plugin = -1;

// Returns the plugin number to use. Falls back to the only existing plugin
// if no current is set. Prints an error and returns -1 if neither holds.
static int dbg_resolve_plugin(void)
{
	if (dbg_current_plugin >= 0) {
		if (RE_get_plugin(dbg_current_plugin))
			return dbg_current_plugin;
		DBG_ERROR("Current plugin %d no longer exists", dbg_current_plugin);
		dbg_current_plugin = -1;
		return -1;
	}
	int found = -1;
	for (int h = 0; h < RE_MAX_PLUGINS; h++) {
		if (!RE_get_plugin(h))
			continue;
		if (found >= 0) {
			DBG_ERROR("Multiple plugins exist; use '3d plugin <no>' to choose one");
			return -1;
		}
		found = h;
	}
	if (found < 0) {
		DBG_ERROR("No ReignEngine plugins");
		return -1;
	}
	return found;
}

static struct RE_plugin *dbg_current_re_plugin(void)
{
	int no = dbg_resolve_plugin();
	return no < 0 ? NULL : RE_get_plugin(no);
}

static bool instance_has_content(struct RE_instance *inst)
{
	if (inst->type == RE_ITYPE_UNINITIALIZED)
		return false;
	if (inst->model && inst->model->path)
		return true;
	if (inst->motion && inst->motion->mot)
		return true;
	if (inst->effect && inst->effect->pae)
		return true;
	if (inst->s3de_effect && inst->s3de_effect->s3de)
		return true;
	return false;
}

static void instance_list_print(struct RE_instance *inst, int index, int indent)
{
	indent_message(indent, inst->draw ? "+ " : "- ");
	sys_message("instance %d (%s)", index, instance_type_name(inst->type));
	if (inst->model && inst->model->path)
		sys_message(" model=%s", display_sjis0(inst->model->path));
	if (inst->motion && inst->motion->mot)
		sys_message(" motion=%s/%s frame=%.2f",
				display_sjis0(inst->motion->mot->name),
				motion_state_name(inst->motion->state),
				inst->motion->current_frame);
	if (inst->effect && inst->effect->pae)
		sys_message(" effect=%s", display_sjis0(inst->effect->pae->path));
	if (inst->s3de_effect && inst->s3de_effect->s3de)
		sys_message(" s3de=%s", display_sjis0(inst->s3de_effect->s3de->path));
	sys_message("\n");
}

static void re_cmd_list(unsigned nr_args, char **args)
{
	bool show_all = nr_args >= 1 && !strcmp(args[0], "all");
	for (int h = 0; h < RE_MAX_PLUGINS; h++) {
		struct RE_plugin *p = RE_get_plugin(h);
		if (!p)
			continue;
		int nr_hidden = 0;
		sys_message("plugin %d (%s sprite=%d nr_instances=%d)\n",
				h, p->plugin.name, p->sprite, p->nr_instances);
		for (int i = 0; i < p->nr_instances; i++) {
			struct RE_instance *inst = p->instances[i];
			if (!inst)
				continue;
			if (!show_all && !instance_has_content(inst)) {
				nr_hidden++;
				continue;
			}
			instance_list_print(inst, i, 1);
		}
		if (nr_hidden)
			indent_message(1, "(%d empty instance(s) hidden; use 'list all' to show)\n",
					nr_hidden);
	}
}

static void re_cmd_plugin(unsigned nr_args, char **args)
{
	if (nr_args == 0) {
		int no = dbg_resolve_plugin();
		if (no < 0)
			return;
		struct RE_plugin *p = RE_get_plugin(no);
		sys_message("plugin %d (%s)%s\n", no, p->plugin.name,
				dbg_current_plugin < 0 ? " (auto-selected)" : "");
		return;
	}
	int no = atoi(args[0]);
	if (!RE_get_plugin(no)) {
		DBG_ERROR("No ReignEngine plugin with handle '%s'", args[0]);
		return;
	}
	dbg_current_plugin = no;
}

static void re_cmd_show(unsigned nr_args, char **args)
{
	struct RE_plugin *p = dbg_current_re_plugin();
	if (!p)
		return;

	cJSON *json;
	if (nr_args >= 1) {
		int idx = atoi(args[0]);
		if (idx < 0 || idx >= p->nr_instances || !p->instances[idx]) {
			DBG_ERROR("No instance with index '%s'", args[0]);
			return;
		}
		json = instance_to_json(p->instances[idx], idx, true);
	} else {
		json = re_plugin_to_json(p, true);
	}
	char *text = cJSON_Print(json);

	sys_message("%s\n", text);

	free(text);
	cJSON_Delete(json);
}

static void re_cmd_set(unsigned nr_args, char **args)
{
	struct RE_plugin *p = dbg_current_re_plugin();
	if (!p)
		return;
	int idx = atoi(args[0]);
	if (idx < 0 || idx >= p->nr_instances || !p->instances[idx]) {
		DBG_ERROR("No instance with index '%s'", args[0]);
		return;
	}
	struct RE_instance *inst = p->instances[idx];
	const char *prop = args[1];
	unsigned nr_values = nr_args - 2;
	char **values = &args[2];

	if (!strcmp(prop, "draw")) {
		if (nr_values != 1) {
			DBG_ERROR("'draw' requires 1 value (0 or 1)");
			return;
		}
		inst->draw = atoi(values[0]) != 0;
	} else if (!strcmp(prop, "pos")) {
		if (nr_values != 3) {
			DBG_ERROR("'pos' requires 3 values (x y z)");
			return;
		}
		inst->pos[0] = strtof(values[0], NULL);
		inst->pos[1] = strtof(values[1], NULL);
		inst->pos[2] = strtof(values[2], NULL);
		inst->local_transform_needs_update = true;
	} else {
		DBG_ERROR("Unknown property '%s'", prop);
		return;
	}

	sprite_call_plugins();
	scene_render();
	gfx_swap();
}

static void re_cmd_help(unsigned nr_args, char **args);

static struct dbg_cmd re_cmds[] = {
	{ "list", NULL, "[all]",
		"Display ReignEngine plugins and their instances (use 'all' to include uninitialized)",
		0, 1, re_cmd_list },
	{ "plugin", NULL, "[<plugin-no>]",
		"Show or set the current plugin used by 'show' and 'set'",
		0, 1, re_cmd_plugin },
	{ "show", NULL, "[<instance-no>]",
		"Display current plugin or instance state as JSON",
		0, 1, re_cmd_show },
	{ "set", NULL, "<instance-no> <property> <value...>",
		"Set an instance property (draw, pos)",
		3, 5, re_cmd_set },
	{ "help", "h", NULL, "List 3d commands",
		0, 0, re_cmd_help },
};

static void re_cmd_help(unsigned nr_args, char **args)
{
	int name_len = 0;
	for (unsigned i = 0; i < sizeof(re_cmds)/sizeof(*re_cmds); i++) {
		int n = strlen(re_cmds[i].fullname);
		if (n > name_len)
			name_len = n;
	}
	sys_message("\n3d commands\n===========\n");
	for (unsigned i = 0; i < sizeof(re_cmds)/sizeof(*re_cmds); i++) {
		struct dbg_cmd *c = &re_cmds[i];
		sys_message("%*s", name_len, c->fullname);
		if (c->shortname)
			sys_message(" (%s)", c->shortname);
		if (c->arg_description)
			sys_message(" %s", c->arg_description);
		sys_message(" -- %s\n", c->description);
	}
}

void re_debug_init(void)
{
	static bool initialized = false;
	if (initialized)
		return;
	initialized = true;

	dbg_cmd_add_module("3d", sizeof(re_cmds)/sizeof(*re_cmds), re_cmds);
}

#else

void re_debug_init(void) {}

#endif // DEBUGGER_ENABLED
