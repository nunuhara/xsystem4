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

#include "vm/page.h"
#include "parts.h"
#include "parts_internal.h"
#include "../hll/iarray.h"

#define CURRENT_SAVE_VERSION 2

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
}

static void load_parts_params(struct iarray_reader *r, struct parts_params *params)
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
	for (unsigned i = 0; i < nr_lines; i++) {
		struct string *line = iarray_read_string(r);
		parts_text_append(parts, text, line);
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
		iarray_write(w, op->cg.no);
		break;
	case PARTS_CP_FILL:
	case PARTS_CP_FILL_ALPHA_COLOR:
	case PARTS_CP_FILL_AMAP:
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
		iarray_write(w, op->cut_cg.cg_no);
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
	}
}

static struct parts_cp_op *load_parts_cp_op(struct iarray_reader *r)
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
		op->cg.no = iarray_read(r);
		break;
	case PARTS_CP_FILL:
	case PARTS_CP_FILL_ALPHA_COLOR:
	case PARTS_CP_FILL_AMAP:
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
		op->cut_cg.cg_no = iarray_read(r);
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
		struct parts_construction_process *cproc)
{
	int ops_count = iarray_read(r);
	for (int i = 0; i < ops_count; i++) {
		struct parts_cp_op *op = load_parts_cp_op(r);
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

static void save_parts_state(struct iarray_writer *w, struct parts_state *state)
{
	iarray_write(w, state->type);
	iarray_write(w, state->common.w);
	iarray_write(w, state->common.h);
	iarray_write_point(w, &state->common.origin_offset);
	iarray_write_rectangle(w, &state->common.hitbox);
	iarray_write_rectangle(w, &state->common.surface_area);
	switch (state->type) {
	case PARTS_UNINITIALIZED:
		break;
	case PARTS_CG:
		save_parts_cg(w, &state->cg);
		break;
	case PARTS_TEXT:
		save_parts_text(w, &state->text);
		break;
	case PARTS_ANIMATION:
		save_parts_animation(w, &state->anim);
		break;
	case PARTS_NUMERAL:
		save_parts_numeral(w, &state->num);
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		save_parts_gauge(w, &state->gauge);
		break;
	case PARTS_CONSTRUCTION_PROCESS:
		save_parts_construction_process(w, &state->cproc);
		break;
	case PARTS_FLASH:
		save_parts_flash(w, &state->flash);
		break;
	}
}

static void load_parts_state(struct iarray_reader *r, struct parts *parts,
		struct parts_state *state)
{
	parts_state_reset(state, iarray_read(r));
	state->common.w = iarray_read(r);
	state->common.h = iarray_read(r);
	iarray_read_point(r, &state->common.origin_offset);
	iarray_read_rectangle(r, &state->common.hitbox);
	iarray_read_rectangle(r, &state->common.surface_area);
	switch (state->type) {
	case PARTS_UNINITIALIZED:
		break;
	case PARTS_CG:
		load_parts_cg(r, parts, &state->cg);
		break;
	case PARTS_TEXT:
		load_parts_text(r, parts, &state->text);
		break;
	case PARTS_ANIMATION:
		load_parts_animation(r, parts, &state->anim);
		break;
	case PARTS_NUMERAL:
		load_parts_numeral(r, parts, &state->num);
		break;
	case PARTS_HGAUGE:
		load_parts_gauge(r, parts, &state->gauge, false);
		break;
	case PARTS_VGAUGE:
		load_parts_gauge(r, parts, &state->gauge, true);
		break;
	case PARTS_CONSTRUCTION_PROCESS:
		load_parts_construction_process(r, parts, &state->cproc);
		break;
	case PARTS_FLASH:
		load_parts_flash(r, parts, &state->flash);
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

static void save_parts(struct iarray_writer *w, struct parts *parts)
{
	iarray_write(w, parts->no);
	iarray_write(w, parts->state);
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		save_parts_state(w, &parts->states[i]);
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

	unsigned motion_count_pos = iarray_writer_pos(w);
	iarray_write(w, 0); // size of motion list

	unsigned motion_count = 0;
	struct parts_motion *motion;
	TAILQ_FOREACH(motion, &parts->motion, entry) {
		save_parts_motion(w, motion);
		motion_count++;
	}

	iarray_write_at(w, motion_count_pos, motion_count);
}

static void load_parts(struct iarray_reader *r, int version)
{
	int no = iarray_read(r);
	struct parts *parts = parts_get(no);
	parts->state = iarray_read(r);
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		load_parts_state(r, parts, &parts->states[i]);
	}

	load_parts_params(r, &parts->local);
	load_parts_params(r, &parts->global);
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

	int motion_count = iarray_read(r);
	for (int i = 0; i < motion_count; i++) {
		struct parts_motion *motion = load_parts_motion(r);
		parts_add_motion(parts, motion);
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

	unsigned count_pos = iarray_writer_pos(&w);
	iarray_write(&w, 0); // size of parts list

	unsigned count = 0;
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (!save_hidden && !parts->global.show)
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
