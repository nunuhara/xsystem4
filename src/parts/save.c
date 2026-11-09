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

#include "system4.h"
#include "system4/string.h"
#include "system4/archive.h"

#include "vm/page.h"
#include "asset_manager.h"
#include "parts.h"
#include "reign.h"
#include "parts_internal.h"
#include "../hll/iarray.h"

#define CURRENT_SAVE_VERSION 8

// Old save data can store a layout box as this sprite type.
#define LEGACY_SPRITE_LAYOUTBOX 12

static void save_parts_params(struct iarray_writer *w, struct parts_params *params)
{
	iarray_write(w, params->z);
	iarray_write_point(w, &params->pos);
	iarray_write(w, params->show);
	iarray_write(w, params->alpha);
	iarray_write_float(w, params->scale.x);
	iarray_write_float(w, params->scale.y);
	iarray_write_float(w, params->rotation.x);
	iarray_write_float(w, params->rotation.y);
	iarray_write_float(w, params->rotation.z);
	iarray_write_color(w, &params->add_color);
	iarray_write_color(w, &params->multiply_color);
	iarray_write(w, params->alpha_clipper_parts_no);
}

static void load_parts_params(struct iarray_reader *r, struct parts_params *params, int version)
{
	params->z = iarray_read(r);
	iarray_read_point(r, &params->pos);
	params->show = iarray_read(r);
	params->alpha = iarray_read(r);
	params->scale.x = iarray_read_float(r);
	params->scale.y = iarray_read_float(r);
	params->rotation.x = iarray_read_float(r);
	params->rotation.y = iarray_read_float(r);
	params->rotation.z = iarray_read_float(r);
	iarray_read_color(r, &params->add_color);
	iarray_read_color(r, &params->multiply_color);
	if (version >= 7)
		params->alpha_clipper_parts_no = iarray_read(r);
}

static void save_parts_cg(struct iarray_writer *w, struct parts_cg *cg)
{
	iarray_write(w, cg->no);
	iarray_write_string_or_null(w, cg->name);
}

static void load_parts_cg(struct iarray_reader *r, struct parts *parts, struct parts_cg *cg)
{
	int no = iarray_read(r);
	struct string *name = iarray_read_string_or_null(r);
	if (name) {
		parts_cg_set(parts, cg, name);
		free_string(name);
	} else if (no) {
		parts_cg_set_by_index(parts, cg, no);
	}
}

static void save_parts_text(struct iarray_writer *w, struct parts_text *text)
{
	iarray_write(w, text->line_space);
	iarray_write_text_style(w, &text->ts);
	iarray_write(w, text->nr_lines);
	for (unsigned i = 0; i < text->nr_lines; i++) {
		struct string *s = parts_text_line_get(&text->lines[i]);
		iarray_write_string(w, s);
		free_string(s);
	}
}

static void load_parts_text(struct iarray_reader *r, struct parts *parts,
		struct parts_text *text)
{
	// FIXME: this won't accurately restore the text state if the text style
	//        varies per-character
	text->line_space = iarray_read(r);
	iarray_read_text_style(r, &text->ts);

	text->nr_lines = 0;
	text->lines = NULL;
	text->cursor.x = 0;
	text->cursor.y = 0;

	int nr_lines = iarray_read(r);
	for (int i = 0; i < nr_lines; i++) {
		struct string *line = iarray_read_string(r);
		if (i > 0) {
			struct string *nl = make_string("\n", 1);
			parts_text_append(parts, text, nl);
			free_string(nl);
		}
		parts_text_append(parts, text, line);
		free_string(line);
	}
}

static void save_parts_animation(struct iarray_writer *w, struct parts_animation *anim)
{
	iarray_write_string_or_null(w, anim->cg_name);
	iarray_write(w, anim->start_no);
	iarray_write(w, anim->nr_frames);
	iarray_write(w, anim->frame_time);
	iarray_write(w, anim->elapsed);
	iarray_write(w, anim->current_frame);
}

static void load_parts_animation(struct iarray_reader *r, struct parts *parts,
		struct parts_animation *anim)
{
	struct string *cg_name = iarray_read_string_or_null(r);
	int start_no = iarray_read(r);
	int nr_frames = iarray_read(r);
	int frame_time = iarray_read(r);

	if (cg_name) {
		parts_animation_set_cg(parts, anim, cg_name, start_no, nr_frames, frame_time);
		free_string(cg_name);
	} else {
		parts_animation_set_cg_by_index(parts, anim, start_no, nr_frames, frame_time);
	}

	anim->elapsed = iarray_read(r);
	anim->current_frame = iarray_read(r);
}

static void save_parts_numeral(struct iarray_writer *w, struct parts_numeral *num)
{
	iarray_write(w, num->have_num);
	iarray_write(w, num->num);
	iarray_write(w, num->space);
	iarray_write(w, num->show_comma);
	iarray_write(w, num->length);
	iarray_write(w, num->font_no);
}

static void load_parts_numeral(struct iarray_reader *r, struct parts *parts,
		struct parts_numeral *num)
{
	num->have_num = iarray_read(r);
	num->num = iarray_read(r);
	num->space = iarray_read(r);
	num->show_comma = iarray_read(r);
	num->length = iarray_read(r);
	num->font_no = iarray_read(r);

	if (num->have_num)
		parts_numeral_set_number(parts, num, num->num);
}

static void save_parts_gauge(struct iarray_writer *w, struct parts_gauge *gauge)
{
	iarray_write(w, gauge->cg_no);
	iarray_write_float(w, gauge->rate);
}

static void load_parts_gauge(struct iarray_reader *r, struct parts *parts,
		struct parts_gauge *gauge, bool vert)
{
	gauge->cg_no = iarray_read(r);
	gauge->rate = iarray_read_float(r);

	if (gauge->cg_no >= 0)
		parts_gauge_set_cg_by_index(parts, gauge, gauge->cg_no);
	if (vert)
		parts_vgauge_set_rate(parts, gauge, gauge->rate);
	else
		parts_hgauge_set_rate(parts, gauge, gauge->rate);
}

static void save_parts_cp_op(struct iarray_writer *w, struct parts_cp_op *op)
{
	iarray_write(w, op->type);
	switch (op->type) {
	case PARTS_CP_CREATE:
	case PARTS_CP_CREATE_PIXEL_ONLY:
		iarray_write(w, op->create.w);
		iarray_write(w, op->create.h);
		break;
	case PARTS_CP_CG:
		iarray_write_string(w, op->cg.name);
		break;
	case PARTS_CP_FILL:
	case PARTS_CP_FILL_ALPHA_COLOR:
	case PARTS_CP_FILL_AMAP:
	case PARTS_CP_FILL_WITH_ALPHA:
	case PARTS_CP_DRAW_RECT:
		iarray_write(w, op->fill.x);
		iarray_write(w, op->fill.y);
		iarray_write(w, op->fill.w);
		iarray_write(w, op->fill.h);
		iarray_write(w, op->fill.r);
		iarray_write(w, op->fill.g);
		iarray_write(w, op->fill.b);
		iarray_write(w, op->fill.a);
		break;
	case PARTS_CP_DRAW_CUT_CG:
	case PARTS_CP_COPY_CUT_CG:
		iarray_write_string(w, op->cut_cg.cg_name);
		iarray_write(w, op->cut_cg.dx);
		iarray_write(w, op->cut_cg.dy);
		iarray_write(w, op->cut_cg.dw);
		iarray_write(w, op->cut_cg.dh);
		iarray_write(w, op->cut_cg.sx);
		iarray_write(w, op->cut_cg.sy);
		iarray_write(w, op->cut_cg.sw);
		iarray_write(w, op->cut_cg.sh);
		iarray_write(w, op->cut_cg.interp_type);
		break;
	case PARTS_CP_DRAW_TEXT:
	case PARTS_CP_COPY_TEXT:
		iarray_write_string(w, op->text.text);
		iarray_write(w, op->text.x);
		iarray_write(w, op->text.y);
		iarray_write(w, op->text.line_space);
		iarray_write_text_style(w, &op->text.style);
		break;
	case PARTS_CP_GRAY_FILTER:
		iarray_write(w, op->filter.x);
		iarray_write(w, op->filter.y);
		iarray_write(w, op->filter.w);
		iarray_write(w, op->filter.h);
		iarray_write(w, op->filter.full_size);
		break;
	}
}

static struct string *load_cp_cg_name(struct iarray_reader *r, int version)
{
	if (version >= 5)
		return iarray_read_string(r);
	// In version 4 and earlier, the CG name was stored as an integer ID.
	// Convert it to a string name by looking up the asset.
	int cg_no = iarray_read(r);
	if (cg_no <= 0)
		return string_ref(&EMPTY_STRING);
	struct archive_data *data = asset_get(ASSET_CG, cg_no);
	if (!data)
		return string_ref(&EMPTY_STRING);
	struct string *name = cstr_to_string(data->name);
	archive_free_data(data);
	return name;
}

static struct parts_cp_op *load_parts_cp_op(struct iarray_reader *r, int version)
{
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = iarray_read(r);
	switch (op->type) {
	case PARTS_CP_CREATE:
	case PARTS_CP_CREATE_PIXEL_ONLY:
		op->create.w = iarray_read(r);
		op->create.h = iarray_read(r);
		break;
	case PARTS_CP_CG:
		op->cg.name = load_cp_cg_name(r, version);
		break;
	case PARTS_CP_FILL:
	case PARTS_CP_FILL_ALPHA_COLOR:
	case PARTS_CP_FILL_AMAP:
	case PARTS_CP_FILL_WITH_ALPHA:
	case PARTS_CP_DRAW_RECT:
		op->fill.x = iarray_read(r);
		op->fill.y = iarray_read(r);
		op->fill.w = iarray_read(r);
		op->fill.h = iarray_read(r);
		op->fill.r = iarray_read(r);
		op->fill.g = iarray_read(r);
		op->fill.b = iarray_read(r);
		op->fill.a = iarray_read(r);
		break;
	case PARTS_CP_DRAW_CUT_CG:
	case PARTS_CP_COPY_CUT_CG:
		op->cut_cg.cg_name = load_cp_cg_name(r, version);
		op->cut_cg.dx = iarray_read(r);
		op->cut_cg.dy = iarray_read(r);
		op->cut_cg.dw = iarray_read(r);
		op->cut_cg.dh = iarray_read(r);
		op->cut_cg.sx = iarray_read(r);
		op->cut_cg.sy = iarray_read(r);
		op->cut_cg.sw = iarray_read(r);
		op->cut_cg.sh = iarray_read(r);
		op->cut_cg.interp_type = iarray_read(r);
		break;
	case PARTS_CP_DRAW_TEXT:
	case PARTS_CP_COPY_TEXT:
		op->text.text = iarray_read_string(r);
		op->text.x = iarray_read(r);
		op->text.y = iarray_read(r);
		op->text.line_space = iarray_read(r);
		iarray_read_text_style(r, &op->text.style);
		break;
	case PARTS_CP_GRAY_FILTER:
		op->filter.x = iarray_read(r);
		op->filter.y = iarray_read(r);
		op->filter.w = iarray_read(r);
		op->filter.h = iarray_read(r);
		op->filter.full_size = !!iarray_read(r);
		break;
	}
	return op;
}

static void save_parts_construction_process(struct iarray_writer *w,
		struct parts_construction_process *cproc)
{
	unsigned ops_count_pos = iarray_writer_pos(w);
	iarray_write(w, 0); // size of ops list

	unsigned ops_count = 0;
	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &cproc->ops, entry) {
		save_parts_cp_op(w, op);
		ops_count++;
	}

	iarray_write_at(w, ops_count_pos, ops_count);
}

static void load_parts_construction_process(struct iarray_reader *r, struct parts *parts,
		struct parts_construction_process *cproc, int version)
{
	int ops_count = iarray_read(r);
	for (int i = 0; i < ops_count; i++) {
		struct parts_cp_op *op = load_parts_cp_op(r, version);
		parts_add_cp_op(cproc, op);
	}
	parts_build_construction_process(parts, cproc);
}

static void save_parts_flash(struct iarray_writer *w, struct parts_flash *flash)
{
	iarray_write_string_or_null(w, flash->name);
	iarray_write(w, flash->stopped);
	iarray_write(w, flash->current_frame);
}

static void load_parts_flash(struct iarray_reader *r, struct parts *parts,
		struct parts_flash *flash)
{
	struct string *name = iarray_read_string_or_null(r);
	parts_flash_load(parts, flash, name);
	free_string(name);
	flash->stopped = !!iarray_read(r);
	parts_flash_seek(flash, iarray_read(r));
}

static void save_parts_flat(struct iarray_writer *w, struct parts_flat *flat)
{
	iarray_write_string_or_null(w, flat->name);
}

static void load_parts_flat(struct iarray_reader *r, struct parts *parts,
		struct parts_flat *flat)
{
	struct string *name = iarray_read_string_or_null(r);
	parts_flat_load(parts, flat, name);
	free_string(name);
	flat->needs_advance = true;
}

static void save_parts_3dlayer(struct iarray_writer *w, struct parts_3dlayer *l)
{
	struct RE_plugin *plugin = l->plugin >= 0 ? RE_get_plugin(l->plugin) : NULL;
	iarray_write(w, !!plugin);
	if (plugin)
		RE_plugin_serialize(plugin, w);
}

static void load_parts_3dlayer(struct iarray_reader *r, struct parts_3dlayer *l,
		int version)
{
	if (!iarray_read(r))
		return;
	if (!parts_3dlayer_create_plugin(l))
		VM_ERROR("cannot create 3D layer plugin");
	RE_plugin_deserialize(RE_get_plugin(l->plugin), r, version);
}

static void save_parts_layoutbox(struct iarray_writer *w, struct parts_layoutbox *lb)
{
	iarray_write(w, lb->layout_type);
	iarray_write(w, lb->wrap);
	iarray_write(w, lb->wrap_size);
	iarray_write(w, lb->align);
	iarray_write(w, lb->padding_top);
	iarray_write(w, lb->padding_bottom);
	iarray_write(w, lb->padding_left);
	iarray_write(w, lb->padding_right);
}

static void load_parts_layoutbox(struct iarray_reader *r, struct parts_layoutbox *lb)
{
	lb->layout_type = iarray_read(r);
	lb->wrap = iarray_read(r);
	lb->wrap_size = iarray_read(r);
	lb->align = iarray_read(r);
	lb->padding_top = iarray_read(r);
	lb->padding_bottom = iarray_read(r);
	lb->padding_left = iarray_read(r);
	lb->padding_right = iarray_read(r);
}

static void save_parts_sprite(struct iarray_writer *w, struct parts_sprite *sprite)
{
	iarray_write(w, sprite->type);
	iarray_write(w, sprite->common.w);
	iarray_write(w, sprite->common.h);
	iarray_write_point(w, &sprite->common.origin_offset);
	iarray_write_rectangle(w, &sprite->common.hitbox);
	iarray_write_rectangle(w, &sprite->common.surface_area);
	switch (sprite->type) {
	case PARTS_SPRITE_NONE:
	case PARTS_SPRITE_MOVIE:
	case PARTS_SPRITE_RECT_DETECTION:
		break;
	case PARTS_SPRITE_3DLAYER:
		save_parts_3dlayer(w, &sprite->layer3d);
		break;
	case PARTS_SPRITE_CG:
		save_parts_cg(w, &sprite->cg);
		break;
	case PARTS_SPRITE_TEXT:
		save_parts_text(w, &sprite->text);
		break;
	case PARTS_SPRITE_ANIMATION:
		save_parts_animation(w, &sprite->anim);
		break;
	case PARTS_SPRITE_NUMERAL:
		save_parts_numeral(w, &sprite->num);
		break;
	case PARTS_SPRITE_HGAUGE:
	case PARTS_SPRITE_VGAUGE:
		save_parts_gauge(w, &sprite->gauge);
		break;
	case PARTS_SPRITE_CONSTRUCTION_PROCESS:
		save_parts_construction_process(w, &sprite->cproc);
		break;
	case PARTS_SPRITE_FLASH:
		save_parts_flash(w, &sprite->flash);
		break;
	case PARTS_SPRITE_FLAT:
		save_parts_flat(w, &sprite->flat);
		break;
	}
}

static void load_parts_sprite(struct iarray_reader *r, struct parts *parts,
		struct parts_sprite *sprite, int version)
{
	int type = iarray_read(r);
	if (type == LEGACY_SPRITE_LAYOUTBOX)
		VM_ERROR("layout box sprite in save data is not supported");
	parts_sprite_reset(sprite, type);
	sprite->common.w = iarray_read(r);
	sprite->common.h = iarray_read(r);
	iarray_read_point(r, &sprite->common.origin_offset);
	iarray_read_rectangle(r, &sprite->common.hitbox);
	iarray_read_rectangle(r, &sprite->common.surface_area);
	switch (sprite->type) {
	case PARTS_SPRITE_NONE:
	case PARTS_SPRITE_MOVIE:
	case PARTS_SPRITE_RECT_DETECTION:
		break;
	case PARTS_SPRITE_3DLAYER:
		if (version > 5)
			load_parts_3dlayer(r, &sprite->layer3d, version);
		break;
	case PARTS_SPRITE_CG:
		load_parts_cg(r, parts, &sprite->cg);
		break;
	case PARTS_SPRITE_TEXT:
		load_parts_text(r, parts, &sprite->text);
		break;
	case PARTS_SPRITE_ANIMATION:
		load_parts_animation(r, parts, &sprite->anim);
		break;
	case PARTS_SPRITE_NUMERAL:
		load_parts_numeral(r, parts, &sprite->num);
		break;
	case PARTS_SPRITE_HGAUGE:
		load_parts_gauge(r, parts, &sprite->gauge, false);
		break;
	case PARTS_SPRITE_VGAUGE:
		load_parts_gauge(r, parts, &sprite->gauge, true);
		break;
	case PARTS_SPRITE_CONSTRUCTION_PROCESS:
		load_parts_construction_process(r, parts, &sprite->cproc, version);
		break;
	case PARTS_SPRITE_FLASH:
		load_parts_flash(r, parts, &sprite->flash);
		break;
	case PARTS_SPRITE_FLAT:
		load_parts_flat(r, parts, &sprite->flat);
		break;
	}
}

static void save_parts_motion(struct iarray_writer *w, struct parts_motion *motion)
{
	iarray_write(w, motion->type);
	switch (motion->type) {
	case PARTS_MOTION_POS:
		iarray_write(w, motion->begin.x);
		iarray_write(w, motion->begin.y);
		iarray_write(w, motion->end.x);
		iarray_write(w, motion->end.y);
		break;
	case PARTS_MOTION_ALPHA:
	case PARTS_MOTION_CG:
	case PARTS_MOTION_NUMERAL_NUMBER:
		iarray_write(w, motion->begin.i);
		iarray_write(w, motion->end.i);
		break;
	case PARTS_MOTION_HGAUGE_RATE:
	case PARTS_MOTION_VGAUGE_RATE:
	case PARTS_MOTION_MAG_X:
	case PARTS_MOTION_MAG_Y:
	case PARTS_MOTION_ROTATE_X:
	case PARTS_MOTION_ROTATE_Y:
	case PARTS_MOTION_ROTATE_Z:
		iarray_write_float(w, motion->begin.f);
		iarray_write_float(w, motion->end.f);
		break;
	case PARTS_MOTION_VIBRATION_SIZE:
		iarray_write(w, motion->begin.x);
		iarray_write(w, motion->begin.y);
		break;
	}
	iarray_write(w, motion->begin_time);
	iarray_write(w, motion->end_time);
}

static struct parts_motion *load_parts_motion(struct iarray_reader *r)
{
	struct parts_motion *motion = xcalloc(1, sizeof(struct parts_motion));
	motion->type = iarray_read(r);
	switch (motion->type) {
	case PARTS_MOTION_POS:
		motion->begin.x = iarray_read(r);
		motion->begin.y = iarray_read(r);
		motion->end.x = iarray_read(r);
		motion->end.y = iarray_read(r);
		break;
	case PARTS_MOTION_ALPHA:
	case PARTS_MOTION_CG:
	case PARTS_MOTION_NUMERAL_NUMBER:
		motion->begin.i = iarray_read(r);
		motion->end.i = iarray_read(r);
		break;
	case PARTS_MOTION_HGAUGE_RATE:
	case PARTS_MOTION_VGAUGE_RATE:
	case PARTS_MOTION_MAG_X:
	case PARTS_MOTION_MAG_Y:
	case PARTS_MOTION_ROTATE_X:
	case PARTS_MOTION_ROTATE_Y:
	case PARTS_MOTION_ROTATE_Z:
		motion->begin.f = iarray_read_float(r);
		motion->end.f = iarray_read_float(r);
		break;
	case PARTS_MOTION_VIBRATION_SIZE:
		motion->begin.x = iarray_read(r);
		motion->begin.y = iarray_read(r);
		break;
	}
	motion->begin_time = iarray_read(r);
	motion->end_time = iarray_read(r);
	return motion;
}

static void save_parts_button_style(struct iarray_writer *w, struct parts_button_style *s)
{
	iarray_write(w, s->w);
	iarray_write(w, s->h);
	iarray_write(w, s->r);
	iarray_write(w, s->g);
	iarray_write(w, s->b);
	iarray_write_text_style(w, &s->font);
	iarray_write_string(w, s->cg_name);
	iarray_write_string(w, s->flat_name);
	iarray_write_string(w, s->text);
	iarray_write(w, s->text_align);
	iarray_write(w, s->line_space);
}

static void load_parts_button_style(struct iarray_reader *r, struct parts_button_style *s)
{
	s->w = iarray_read(r);
	s->h = iarray_read(r);
	s->r = iarray_read(r);
	s->g = iarray_read(r);
	s->b = iarray_read(r);
	iarray_read_text_style(r, &s->font);
	free_string(s->cg_name);
	free_string(s->flat_name);
	free_string(s->text);
	s->cg_name = iarray_read_string(r);
	s->flat_name = iarray_read_string(r);
	s->text = iarray_read_string(r);
	s->text_align = iarray_read(r);
	s->line_space = iarray_read(r);
}

static void save_parts_button(struct iarray_writer *w, struct parts_button *b)
{
	save_parts_button_style(w, &b->style);
	iarray_write(w, b->enabled);
}

static void load_parts_button(struct iarray_reader *r, struct parts *parts)
{
	struct parts_button *b = parts_get_button(parts);
	load_parts_button_style(r, &b->style);
	b->enabled = iarray_read(r);
	parts_button_rebuild(parts);
}

static void save_parts_checkbox(struct iarray_writer *w, struct parts_checkbox *c)
{
	save_parts_button_style(w, &c->style);
	iarray_write(w, c->checked);
	iarray_write(w, c->button_mode);
}

static void load_parts_checkbox(struct iarray_reader *r, struct parts *parts)
{
	struct parts_checkbox *c = parts_get_checkbox(parts);
	load_parts_button_style(r, &c->style);
	c->checked = iarray_read(r);
	c->button_mode = iarray_read(r);
	parts_checkbox_rebuild(parts);
}

static void save_parts_scrollbar(struct iarray_writer *w, struct parts_scrollbar *s)
{
	iarray_write(w, s->length);
	iarray_write(w, s->thickness);
	iarray_write(w, s->leading);
	iarray_write(w, s->trailing);
	iarray_write(w, s->total_size);
	iarray_write(w, s->view_size);
	iarray_write(w, s->pos);
	iarray_write_float(w, s->rate);
	iarray_write(w, s->move_size);
	iarray_write_string(w, s->cg_name);
}

static void load_parts_scrollbar(struct iarray_reader *r, struct parts *parts, bool horizontal)
{
	struct parts_scrollbar *s = parts_get_scrollbar(parts, horizontal);
	s->length = iarray_read(r);
	s->thickness = iarray_read(r);
	s->leading = iarray_read(r);
	s->trailing = iarray_read(r);
	s->total_size = iarray_read(r);
	s->view_size = iarray_read(r);
	s->pos = iarray_read(r);
	s->rate = iarray_read_float(r);
	s->move_size = iarray_read(r);
	free_string(s->cg_name);
	s->cg_name = iarray_read_string(r);
	parts_scrollbar_rebuild(parts);
}

static void save_parts_radiobox(struct iarray_writer *w, struct parts_radiobox *box)
{
	iarray_write(w, box->count);
	for (int i = 0; i < box->count; i++)
		iarray_write(w, box->members[i]);
}

static void load_parts_radiobox(struct iarray_reader *r, struct parts *parts)
{
	struct parts_radiobox *box = parts_get_radiobox(parts);
	free(box->members);
	box->count = iarray_read(r);
	box->members = box->count > 0 ? xcalloc(box->count, sizeof(*box->members)) : NULL;
	for (int i = 0; i < box->count; i++)
		box->members[i] = iarray_read(r);
}

static void save_parts_widget(struct iarray_writer *w, struct parts *parts)
{
	iarray_write(w, parts->type);
	switch (parts->type) {
	case PARTS_TYPE_SPRITE:
		break;
	case PARTS_TYPE_BUTTON:
		save_parts_button(w, parts->widget.button);
		break;
	case PARTS_TYPE_CHECKBOX:
		save_parts_checkbox(w, parts->widget.checkbox);
		break;
	case PARTS_TYPE_VSCROLLBAR:
	case PARTS_TYPE_HSCROLLBAR:
		save_parts_scrollbar(w, parts->widget.scrollbar);
		break;
	case PARTS_TYPE_LAYOUTBOX:
		save_parts_layoutbox(w, parts->widget.layoutbox);
		break;
	case PARTS_TYPE_RADIOBOX:
		save_parts_radiobox(w, parts->widget.radiobox);
		break;
	}
}

static void load_parts_widget(struct iarray_reader *r, struct parts *parts)
{
	int type = iarray_read(r);
	switch (type) {
	case PARTS_TYPE_SPRITE:
		break;
	case PARTS_TYPE_BUTTON:
		load_parts_button(r, parts);
		break;
	case PARTS_TYPE_CHECKBOX:
		load_parts_checkbox(r, parts);
		break;
	case PARTS_TYPE_VSCROLLBAR:
	case PARTS_TYPE_HSCROLLBAR:
		load_parts_scrollbar(r, parts, type == PARTS_TYPE_HSCROLLBAR);
		break;
	case PARTS_TYPE_LAYOUTBOX:
		load_parts_layoutbox(r, parts_get_layoutbox(parts));
		break;
	case PARTS_TYPE_RADIOBOX:
		load_parts_radiobox(r, parts);
		break;
	default:
		VM_ERROR("invalid saved widget type %d", type);
	}
}

static void save_parts(struct iarray_writer *w, struct parts *parts)
{
	iarray_write(w, parts->no);
	iarray_write(w, parts->state);
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		save_parts_sprite(w, &parts->sprites[i]);
	}

	save_parts_params(w, &parts->local);
	save_parts_params(w, &parts->global);
	iarray_write(w, parts->parent ? parts->parent->no : -1);
	iarray_write(w, parts->delegate_index);
	iarray_write(w, parts->sprite_deform);
	iarray_write(w, parts->clickable);
	iarray_write(w, parts->on_cursor_sound);
	iarray_write(w, parts->on_click_sound);
	iarray_write(w, parts->origin_mode);
	// XXX: no need to save pending parent since we called UpdateComponent first
	iarray_write(w, parts->linked_to);
	iarray_write(w, parts->linked_from);
	iarray_write(w, parts->draw_filter);
	iarray_write(w, parts->message_window);
	iarray_write(w, parts->controller_no);
	iarray_write(w, parts->pass_cursor);
	iarray_write(w, parts->lock_input_state);
	iarray_write(w, parts->margin_top);
	iarray_write(w, parts->margin_bottom);
	iarray_write(w, parts->margin_left);
	iarray_write(w, parts->margin_right);
	iarray_write(w, parts->draggable);

	unsigned motion_count_pos = iarray_writer_pos(w);
	iarray_write(w, 0); // size of motion list

	unsigned motion_count = 0;
	struct parts_motion *motion;
	TAILQ_FOREACH(motion, &parts->motion, entry) {
		save_parts_motion(w, motion);
		motion_count++;
	}

	iarray_write_at(w, motion_count_pos, motion_count);
	iarray_write(w, parts->checkbox_show_link);
	save_parts_widget(w, parts);
}

static void load_parts(struct iarray_reader *r, int version)
{
	int no = iarray_read(r);
	struct parts *parts = parts_get(no);
	parts->state = iarray_read(r);
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		load_parts_sprite(r, parts, &parts->sprites[i], version);
	}

	load_parts_params(r, &parts->local, version);
	load_parts_params(r, &parts->global, version);
	parts->pending_parent = iarray_read(r);
	parts->delegate_index = iarray_read(r);
	parts->sprite_deform = iarray_read(r);
	parts->clickable = iarray_read(r);
	parts->on_cursor_sound = iarray_read(r);
	parts->on_click_sound = iarray_read(r);
	parts->origin_mode = iarray_read(r);
	parts->linked_to = iarray_read(r);
	parts->linked_from = iarray_read(r);
	parts->draw_filter = iarray_read(r);
	if (version > 0)
		parts->message_window = iarray_read(r);
	if (version > 2 && version < 7)
		parts->local.alpha_clipper_parts_no = iarray_read(r);
	if (version > 3 || (version == 3 && parts_multi_controller)) {
		parts->controller_no = iarray_read(r);
		parts->pass_cursor = iarray_read(r);
		parts->lock_input_state = iarray_read(r);
		parts->margin_top = iarray_read(r);
		parts->margin_bottom = iarray_read(r);
		parts->margin_left = iarray_read(r);
		parts->margin_right = iarray_read(r);
		parts->draggable = iarray_read(r);
	}

	int motion_count = iarray_read(r);
	for (int i = 0; i < motion_count; i++) {
		struct parts_motion *motion = load_parts_motion(r);
		parts_add_motion(parts, motion);
	}

	if (version >= 8) {
		parts->checkbox_show_link = iarray_read(r);
		load_parts_widget(r, parts);
	}

	parts_list_resort(parts);
	parts_component_dirty(parts);
	parts_recalculate_hitbox(parts);
}

static void save_numeral_fonts(struct iarray_writer *w)
{
	iarray_write(w, parts_nr_numeral_fonts);
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		iarray_write(w, font->type);
		iarray_write(w, font->cg_no);
		for (int i = 0; i < 12; i++) {
			iarray_write(w, font->width[i]);
		}
	}
}

static void load_numeral_fonts(struct iarray_reader *r)
{
	parts_nr_numeral_fonts = iarray_read(r);
	parts_numeral_fonts = xcalloc(parts_nr_numeral_fonts, sizeof(struct parts_numeral_font));
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		font->type = iarray_read(r);
		font->cg_no = iarray_read(r);
		for (int i = 0; i < 12; i++) {
			font->width[i] = iarray_read(r);
		}
		parts_numeral_font_init(font);
	}
}

static bool parts_engine_save(struct page **buffer, bool save_hidden)
{
	// get parts into clean state first
	PE_UpdateComponent(0);

	struct iarray_writer w;
	iarray_init_writer(&w, "XPE");
	iarray_write(&w, CURRENT_SAVE_VERSION);
	if (CURRENT_SAVE_VERSION > 1)
		save_numeral_fonts(&w);

	iarray_write(&w, ctrl_stack.active);
	iarray_write(&w, ctrl_stack.nr_controllers);

	unsigned count_pos = iarray_writer_pos(&w);
	iarray_write(&w, 0); // size of parts list

	unsigned count = 0;
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (!save_hidden && !parts->global.show)
			continue;
		if (!parts->want_save)
			continue;
		save_parts(&w, parts);
		count++;
	}

	iarray_write_at(&w, count_pos, count);

	if (*buffer) {
		delete_page_vars(*buffer);
		free_page(*buffer);
	}
	*buffer = iarray_to_page(&w);;
	iarray_free_writer(&w);
	return true;

}

bool PE_SaveWithoutHideParts(struct page **buffer)
{
	return parts_engine_save(buffer, false);
}

bool PE_Save(struct page **buffer)
{
	return parts_engine_save(buffer, true);
}

bool PE_Load(struct page **buffer)
{
	if (!(*buffer)) {
		WARNING("savedata array is empty");
		return false;
	}

	struct iarray_reader r;
	if (!iarray_init_reader(&r, *buffer, "XPE")) {
		WARNING("unrecognized savedata magic");
		return false;
	}
	int version = iarray_read(&r);
	if (version > CURRENT_SAVE_VERSION) {
		WARNING("unrecognized savedata version");
		return false;
	}

	parts_release_all();

	if (version > 1)
		load_numeral_fonts(&r);

	if (version > 3 || (version == 3 && parts_multi_controller)) {
		ctrl_stack.active = iarray_read(&r);
		ctrl_stack.nr_controllers = iarray_read(&r);
	}

	int nr_parts = iarray_read(&r);
	if (nr_parts < 0) {
		WARNING("invalid parts count");
		return false;
	}

	for (int i = 0; i < nr_parts; i++) {
		load_parts(&r, version);
	}

	parts_engine_clean();
	return true;
}
