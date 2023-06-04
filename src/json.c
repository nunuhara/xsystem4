/* Copyright (C) 2023 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include <cglm/types.h>

#include "base64.h"
#include "cJSON.h"
#include "json.h"
#include "gfx/gfx.h"
#include "gfx/font.h"
#include "plugin.h"
#include "scene.h"
#include "sprite.h"

cJSON *num2_to_json(double a, double b)
{
	cJSON *ar = cJSON_CreateArray();
	cJSON_AddItemToArray(ar, cJSON_CreateNumber(a));
	cJSON_AddItemToArray(ar, cJSON_CreateNumber(b));
	return ar;
}

cJSON *num2_to_json_object(const char *a_name, double a, const char *b_name, double b)
{
	cJSON *obj = cJSON_CreateObject();
	cJSON_AddNumberToObject(obj, a_name, a);
	cJSON_AddNumberToObject(obj, b_name, b);
	return obj;
}

cJSON *num3_to_json(double a, double b, double c)
{
	cJSON *ar = cJSON_CreateArray();
	cJSON_AddItemToArray(ar, cJSON_CreateNumber(a));
	cJSON_AddItemToArray(ar, cJSON_CreateNumber(b));
	cJSON_AddItemToArray(ar, cJSON_CreateNumber(c));
	return ar;
}

cJSON *num3_to_json_object(const char *a_name, double a, const char *b_name, double b,
		const char *c_name, double c)
{
	cJSON *obj = cJSON_CreateObject();
	cJSON_AddNumberToObject(obj, a_name, a);
	cJSON_AddNumberToObject(obj, b_name, b);
	cJSON_AddNumberToObject(obj, c_name, c);
	return obj;
}

cJSON *num4_to_json(double a, double b, double c, double d)
{
	cJSON *ar = cJSON_CreateArray();
	cJSON_AddItemToArray(ar, cJSON_CreateNumber(a));
	cJSON_AddItemToArray(ar, cJSON_CreateNumber(b));
	cJSON_AddItemToArray(ar, cJSON_CreateNumber(c));
	cJSON_AddItemToArray(ar, cJSON_CreateNumber(d));
	return ar;
}

cJSON *num4_to_json_object(const char *a_name, double a, const char *b_name, double b,
		const char *c_name, double c, const char *d_name, double d)
{
	cJSON *obj = cJSON_CreateObject();
	cJSON_AddNumberToObject(obj, a_name, a);
	cJSON_AddNumberToObject(obj, b_name, b);
	cJSON_AddNumberToObject(obj, c_name, c);
	cJSON_AddNumberToObject(obj, d_name, d);
	return obj;
}

cJSON *texture_to_json(struct texture *t, bool verbose)
{
	if (!t->handle)
		return cJSON_CreateNull();
	if (!verbose)
		return num2_to_json(t->w, t->h);
	cJSON *obj = cJSON_CreateObject();
	cJSON_AddNumberToObject(obj, "width", t->w);
	cJSON_AddNumberToObject(obj, "height", t->h);
	return obj;
}

cJSON *texture_to_json_with_pixels(struct texture *t)
{
	size_t size;
	unsigned char *pixels = gfx_get_pixels(t);
	unsigned char *b64 = base64_encode(pixels, t->w * t->h * 4, &size);
	free(pixels);

	cJSON *obj = texture_to_json(t, true);
	cJSON_AddStringToObject(obj, "pixels", (char*)b64);
	free(b64);
	return obj;
}

cJSON *text_style_to_json(struct text_style *ts, bool verbose)
{
	cJSON *obj;
	obj = cJSON_CreateObject();
	cJSON_AddNumberToObject(obj, "face", ts->face);
	cJSON_AddNumberToObject(obj, "size", ts->size);
	cJSON_AddNumberToObject(obj, "bold_width", ts->bold_width);
	cJSON_AddNumberToObject(obj, "weight", ts->weight);
	cJSON_AddNumberToObject(obj, "edge_left", ts->edge_left);
	cJSON_AddNumberToObject(obj, "edge_up", ts->edge_up);
	cJSON_AddNumberToObject(obj, "edge_right", ts->edge_right);
	cJSON_AddNumberToObject(obj, "edge_down", ts->edge_down);
	cJSON_AddItemToObjectCS(obj, "color", color_to_json(&ts->color, verbose));
	cJSON_AddItemToObjectCS(obj, "edge_color", color_to_json(&ts->edge_color, verbose));
	cJSON_AddNumberToObject(obj, "scale_x", ts->scale_x);
	cJSON_AddNumberToObject(obj, "space_scale_x", ts->space_scale_x);
	cJSON_AddNumberToObject(obj, "font_spacing", ts->font_spacing);
	return obj;
}

cJSON *scene_sprite_to_json(struct sprite *sp, bool verbose)
{
	cJSON *obj = cJSON_CreateObject();
	cJSON_AddNumberToObject(obj, "id", sp->id);
	cJSON_AddNumberToObject(obj, "z", sp->z);
	cJSON_AddNumberToObject(obj, "z2", sp->z2);
	cJSON_AddBoolToObject(obj, "has_pixel", sp->has_pixel);
	cJSON_AddBoolToObject(obj, "has_alpha", sp->has_alpha);
	cJSON_AddBoolToObject(obj, "hidden", sp->hidden);
	cJSON_AddBoolToObject(obj, "in_scene", sp->in_scene);
	return obj;
}

static cJSON *draw_plugin_to_json(struct sact_sprite *sp, bool verbose)
{
	if (!sp->plugin)
		return cJSON_CreateNull();
	if (sp->plugin->to_json)
		return sp->plugin->to_json(sp, verbose);
	cJSON *obj = cJSON_CreateObject();
	cJSON_AddStringToObject(obj, "name", sp->plugin->name);
	return obj;
}

static const char *draw_method_string(int draw_method)
{
	switch (draw_method) {
	case DRAW_METHOD_NORMAL: return "normal";
	case DRAW_METHOD_SCREEN: return "screen";
	case DRAW_METHOD_MULTIPLY: return "multiply";
	case DRAW_METHOD_ADDITIVE: return "additive";
	default: return "unknown";
	}
}

cJSON *sprite_to_json(struct sact_sprite *sp, bool verbose)
{
	cJSON *ent, *sprite;

	ent = scene_sprite_to_json(&sp->sp, verbose);
	cJSON_AddItemToObjectCS(ent, "sprite", sprite = cJSON_CreateObject());
	cJSON_AddNumberToObject(sprite, "no", sp->no);
	cJSON_AddItemToObjectCS(sprite, "texture", texture_to_json(&sp->texture, verbose));
	cJSON_AddItemToObjectCS(sprite, "color", color_to_json(&sp->color, verbose));
	if (verbose || sp->multiply_color.r != 255 || sp->multiply_color.g != 255
			|| sp->multiply_color.b != 255)
		cJSON_AddItemToObjectCS(sprite, "mul_color", color_to_json(&sp->multiply_color, verbose));
	if (verbose || sp->add_color.r || sp->add_color.g || sp->add_color.b)
		cJSON_AddItemToObjectCS(sprite, "add_color", color_to_json(&sp->add_color, verbose));
	if (verbose || sp->blend_rate != 255)
		cJSON_AddNumberToObject(sprite, "blend_rate", sp->blend_rate);
	if (verbose || sp->draw_method != DRAW_METHOD_NORMAL)
		cJSON_AddStringToObject(sprite, "draw_method", draw_method_string(sp->draw_method));
	cJSON_AddItemToObjectCS(sprite, "rect", rectangle_to_json(&sp->rect, verbose));
	if (verbose || sp->cg_no)
		cJSON_AddNumberToObject(sprite, "cg_no", sp->cg_no);
	if (verbose || sp->text.texture.handle) {
		cJSON *text;
		cJSON_AddItemToObjectCS(sprite, "text", text = cJSON_CreateObject());
		cJSON_AddItemToObjectCS(text, "texture", texture_to_json(&sp->text.texture, verbose));
		if (verbose || sp->text.home.x || sp->text.home.y)
			cJSON_AddItemToObjectCS(text, "home", point_to_json(&sp->text.home, verbose));
		cJSON_AddItemToObjectCS(text, "pos", point_to_json(&sp->text.pos, verbose));
		if (verbose || sp->text.char_space)
			cJSON_AddNumberToObject(text, "char_space", sp->text.char_space);
		if (verbose || sp->text.line_space)
			cJSON_AddNumberToObject(text, "line_space", sp->text.line_space);
		if (verbose || sp->text.current_line_height)
			cJSON_AddNumberToObject(text, "current_line_height", sp->text.current_line_height);
	}
	if (verbose || sp->plugin)
		cJSON_AddItemToObjectCS(sprite, "plugin", draw_plugin_to_json(sp, verbose));

	return ent;
}
