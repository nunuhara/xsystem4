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

#include <string.h>

#include "system4.h"
#include "system4/archive.h"
#include "system4/buffer.h"
#include "system4/hashtable.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/zlib.h"

#include "asset_manager.h"
#include "parts.h"
#include "parts_internal.h"
#include "vm/page.h"

enum pact_component_type {
	PACT_COMPONENT_BUTTON = 0,
	PACT_COMPONENT_CHECKBOX = 1,
	PACT_COMPONENT_VSCROLLBAR = 2,
	PACT_COMPONENT_HSCROLLBAR = 3,
	PACT_COMPONENT_MULTILINE_TEXT = 7,
	PACT_COMPONENT_LAYOUT_BOX = 8,
	PACT_COMPONENT_RADIO_BOX = 9,
	PACT_COMPONENT_SPRITE = 10,
};

enum pact_sprite_type {
	PACT_SPRITE_CG = 11,
	PACT_SPRITE_TEXT = 13,
	PACT_SPRITE_RECT = 17,
	PACT_SPRITE_CONSTRUCTION = 18,
	PACT_SPRITE_FLAT = 20,
};

enum pact_link {
	PACT_LINK_ALPHA_CLIPPER,
	PACT_LINK_ON_CURSOR_SHOW,
	PACT_LINK_SCROLL_POS_X,
	PACT_LINK_SCROLL_POS_Y,
	PACT_LINK_SCROLL_ALPHA,
	PACT_LINK_CHECKBOX_SHOW,
	PACT_NR_LINKS
};

#define PACT_NR_MOTIONS 11

struct activity_names {
	int count;
	const char **values;
};

struct activity_part {
	const char *name;
	int number;  // assigned at load time, not read from the .pact file
	struct activity_names children;
	const char *links[PACT_NR_LINKS];
};

struct activity_intent {
	const char *name;
	struct activity_names destinations;
	int type;
};

struct activity_function_pair {
	const char *map_id;
	const char *function;
};

struct activity_function_state {
	int state;
	int nr_pairs;
	struct activity_function_pair *pairs;
};

struct activity_functions {
	const char *name;
	int nr_states;
	struct activity_function_state *states;
};

struct activity {
	uint8_t *image;  // owns the strings that the other fields point to
	int nr_parts;
	struct activity_part *parts;
	int nr_intents;
	struct activity_intent *intents;
	int nr_functions;
	struct activity_functions *functions;
	int nr_end_keys;
	int *end_keys;
};

static struct activity_names pact_names(struct buffer *r)
{
	struct activity_names names = { .count = buffer_read_int32(r) };
	names.values = xcalloc(names.count, sizeof(*names.values));
	for (int i = 0; i < names.count; i++)
		names.values[i] = buffer_skip_string(r);
	return names;
}

static void pact_skip_names(struct buffer *r)
{
	int count = buffer_read_int32(r);
	for (int i = 0; i < count; i++)
		buffer_skip_string(r);
}

static void pact_skip_int_list(struct buffer *r)
{
	int count = buffer_read_int32(r);
	buffer_skip(r, (size_t)count * 4);
}

static void pact_skip(struct buffer *r, const char *schema)
{
	for (; *schema; schema++) {
		switch (*schema) {
		case 'i': case 'f': buffer_skip(r, 4); break;
		case 'b': buffer_skip(r, 1); break;
		case 's': buffer_skip_string(r); break;
		}
	}
}

static struct string *pact_string(struct buffer *r)
{
	return cstr_to_string(buffer_skip_string(r));
}

static SDL_Rect pact_surface_rect(struct buffer *r)
{
	SDL_Rect rect;
	rect.x = buffer_read_int32(r);
	rect.y = buffer_read_int32(r);
	rect.w = buffer_read_int32(r);
	rect.h = buffer_read_int32(r);
	return rect;
}

static void activity_set_surface(struct parts *p, int state, SDL_Rect rect)
{
	parts_set_surface_area(p, &p->states[state].common, rect.x, rect.y, rect.w, rect.h);
}

static void activity_set_cg(struct parts *p, int state, const char *name)
{
	struct string *s = cstr_to_string(name);
	struct parts_cg *cg = parts_get_cg(p, state);
	if (*name)
		parts_cg_set(p, cg, s);
	else
		cg->name = string_ref(s);
	free_string(s);
}

static void pact_sprite_cg(struct buffer *r, struct parts *p, int state)
{
	const char *name = buffer_skip_string(r);
	int deform = buffer_read_int32(r);
	SDL_Rect surface = pact_surface_rect(r);
	if (!p)
		return;
	activity_set_cg(p, state, name);
	p->sprite_deform = deform;
	activity_set_surface(p, state, surface);
}

struct pact_text_decoration {
	int face;
	int size;
	SDL_Color color;
	float bold_width;
	float edge_width;
	SDL_Color edge_color;
	int font_spacing;
	int line_space;
};

static SDL_Color pact_color(struct buffer *r)
{
	int red = buffer_read_int32(r);
	int green = buffer_read_int32(r);
	int blue = buffer_read_int32(r);
	return (SDL_Color){red, green, blue, 255};
}

static struct pact_text_decoration pact_decoration(struct buffer *r)
{
	struct pact_text_decoration d;
	d.face = buffer_read_int32(r);
	d.size = buffer_read_int32(r);
	d.color = pact_color(r);
	d.bold_width = buffer_read_float(r);
	d.edge_width = buffer_read_float(r);
	d.edge_color = pact_color(r);
	d.font_spacing = buffer_read_int32(r);
	d.line_space = buffer_read_int32(r);
	return d;
}

static void pact_sprite_text(struct buffer *r, struct parts *p, int state)
{
	struct pact_text_decoration d = pact_decoration(r);
	pact_decoration(r);  // ruby decoration
	pact_skip(r, "biii");  // count return, elapsed time, show time, text pos
	SDL_Rect surface = pact_surface_rect(r);
	int nr_highlights = buffer_read_int32(r);
	buffer_skip(r, (size_t)nr_highlights * 20);  // start, length, r, g, b
	struct string *text = pact_string(r);
	pact_skip(r, "bb");  // enable tag, join dashes
	if (p) {
		struct parts_text *t = parts_get_text(p, state);
		t->ts.face = d.face;
		t->ts.size = d.size;
		t->ts.color = d.color;
		t->ts.bold_width = d.bold_width;
		t->ts.weight = d.bold_width * 1000;
		text_style_set_edge_width(&t->ts, d.edge_width);
		t->ts.edge_color = d.edge_color;
		t->ts.font_spacing = d.font_spacing;
		t->line_space = d.line_space;
		parts_text_append(p, t, text);
		activity_set_surface(p, state, surface);
	}
	free_string(text);
}

static void pact_sprite_rect(struct buffer *r, struct parts *p, int state)
{
	pact_skip(r, "bffffffff");  // detection mode, 4 vertices (x, y)
	SDL_Rect surface = pact_surface_rect(r);
	if (!p)
		return;
	parts_state_reset(&p->states[state], PARTS_RECT_DETECTION);
	activity_set_surface(p, state, surface);
}

static void pact_process_dest(struct buffer *r, struct parts_cp_command *cmd)
{
	cmd->dx = buffer_read_int32(r);
	cmd->dy = buffer_read_int32(r);
	cmd->dw = buffer_read_int32(r);
	cmd->dh = buffer_read_int32(r);
}

static void pact_process_color(struct buffer *r, struct parts_cp_command *cmd)
{
	cmd->r = buffer_read_int32(r);
	cmd->g = buffer_read_int32(r);
	cmd->b = buffer_read_int32(r);
}

static void pact_process_text(struct buffer *r, struct parts_cp_command *cmd)
{
	cmd->dx = buffer_read_int32(r);
	cmd->dy = buffer_read_int32(r);
	cmd->text = pact_string(r);
	cmd->font_type = buffer_read_int32(r);
	cmd->font_size = buffer_read_int32(r);
	cmd->font_r = buffer_read_int32(r);
	cmd->font_g = buffer_read_int32(r);
	cmd->font_b = buffer_read_int32(r);
	cmd->bold_weight = buffer_read_float(r);
	cmd->edge_r = buffer_read_int32(r);
	cmd->edge_g = buffer_read_int32(r);
	cmd->edge_b = buffer_read_int32(r);
	cmd->edge_weight = buffer_read_float(r);
	cmd->char_space = buffer_read_int32(r);
	cmd->line_space = buffer_read_int32(r);
}

static void pact_process_cut_cg(struct buffer *r, struct parts_cp_command *cmd)
{
	cmd->cg_name = pact_string(r);
	pact_process_dest(r, cmd);
	cmd->sx = buffer_read_int32(r);
	cmd->sy = buffer_read_int32(r);
	cmd->sw = buffer_read_int32(r);
	cmd->sh = buffer_read_int32(r);
	cmd->interp_type = buffer_read_int32(r);
}

static bool pact_process(struct buffer *r, struct parts *p, int state)
{
	struct parts_cp_command cmd = { .type = buffer_read_int32(r) };
	switch (cmd.type) {
	case PARTS_CP_CMD_CREATE:
	case PARTS_CP_CMD_CREATE_PIXEL_ONLY:
		cmd.dw = buffer_read_int32(r);
		cmd.dh = buffer_read_int32(r);
		break;
	case PARTS_CP_CMD_CREATE_CG:
		cmd.cg_name = pact_string(r);
		break;
	case PARTS_CP_CMD_FILL:
	case PARTS_CP_CMD_DRAW_RECT:
		pact_process_dest(r, &cmd);
		pact_process_color(r, &cmd);
		break;
	case PARTS_CP_CMD_FILL_ALPHA_COLOR:
	case PARTS_CP_CMD_FILL_WITH_ALPHA:
		pact_process_dest(r, &cmd);
		pact_process_color(r, &cmd);
		cmd.a = buffer_read_int32(r);
		break;
	case PARTS_CP_CMD_FILL_AMAP:
		pact_process_dest(r, &cmd);
		cmd.a = buffer_read_int32(r);
		break;
	case PARTS_CP_CMD_DRAW_TEXT:
	case PARTS_CP_CMD_COPY_TEXT:
		pact_process_text(r, &cmd);
		break;
	case PARTS_CP_CMD_CUT_CG_BLEND:
	case PARTS_CP_CMD_CUT_CG_COPY:
	case PARTS_CP_CMD_CUT_CG_SCALE_BLEND:
	case PARTS_CP_CMD_CUT_CG_SCALE_COPY:
		pact_process_cut_cg(r, &cmd);
		break;
	case PARTS_CP_CMD_GRAY_FILTER:
		pact_process_dest(r, &cmd);
		cmd.full_size = buffer_read_u8(r);
		break;
	case PARTS_CP_CMD_FILL_GRADATION_HORIZON:
		pact_skip(r, "iiiiiiiiii");
		break;
	case PARTS_CP_CMD_ADD_FILTER:
	case PARTS_CP_CMD_MUL_FILTER:
		pact_skip(r, "iiiiiiib");
		break;
	case PARTS_CP_CMD_DRAW_LINE:
		pact_skip(r, "iiiiiiii");
		break;
	default:
		WARNING("Unsupported construction process command %d", cmd.type);
		return false;
	}
	if (p)
		PE_add_construct_process(p->no, state + 1, &cmd);
	if (cmd.text)
		free_string(cmd.text);
	if (cmd.cg_name)
		free_string(cmd.cg_name);
	return true;
}

static bool pact_sprite_construction(struct buffer *r, struct parts *p, int state)
{
	if (p)
		parts_get_construction_process(p, state);
	int count = buffer_read_int32(r);
	for (int i = 0; i < count; i++) {
		if (!pact_process(r, p, state))
			return false;
	}
	SDL_Rect surface = pact_surface_rect(r);
	if (p) {
		parts_build_construction_process(p, &p->states[state].cproc);
		activity_set_surface(p, state, surface);
	}
	return true;
}

static void pact_sprite_flat(struct buffer *r, struct parts *p, int state)
{
	struct string *name = pact_string(r);
	SDL_Rect surface = pact_surface_rect(r);
	for (int list = 0; list < 2; list++) {
		int count = buffer_read_int32(r);
		for (int i = 0; i < count; i++)
			pact_skip(r, "ss");  // resource substitutions
	}
	float speed = buffer_read_float(r);
	if (p) {
		struct parts_flat *f = parts_get_flat(p, state);
		if (name->size)
			parts_flat_load(p, f, name);
		f->update_speed_rate = speed;
		activity_set_surface(p, state, surface);
	}
	free_string(name);
}

static bool pact_sprite(struct buffer *r, struct parts *p, int state)
{
	int type = buffer_read_int32(r);
	switch (type) {
	case PACT_SPRITE_CG:
		pact_sprite_cg(r, p, state);
		return true;
	case PACT_SPRITE_TEXT:
		pact_sprite_text(r, p, state);
		return true;
	case PACT_SPRITE_RECT:
		pact_sprite_rect(r, p, state);
		return true;
	case PACT_SPRITE_CONSTRUCTION:
		return pact_sprite_construction(r, p, state);
	case PACT_SPRITE_FLAT:
		pact_sprite_flat(r, p, state);
		return true;
	default:
		WARNING("Unsupported sprite type %d", type);
		return false;
	}
}

static void pact_layout_box(struct buffer *r, struct parts *p)
{
	struct parts_layout_box *l = parts_get_layout_box(p);
	l->layout_type = buffer_read_int32(r);
	l->wrap = buffer_read_u8(r);
	l->wrap_size = buffer_read_float(r);
	pact_skip(r, "b");  // ReturnSizeForRate
	l->align = buffer_read_int32(r);
	l->padding_top = buffer_read_int32(r);
	l->padding_bottom = buffer_read_int32(r);
	l->padding_left = buffer_read_int32(r);
	l->padding_right = buffer_read_int32(r);
}

static bool pact_sprite_component(struct buffer *r, struct parts *p)
{
	pact_skip(r, "b");  // !lock_input_state (unused)
	int count = buffer_read_int32(r);
	for (int i = 0; i < count; i++) {
		// Slot 0 is unused.
		int state = i - 1;
		if (!pact_sprite(r, parts_state_valid(state) ? p : NULL, state))
			return false;
	}
	return true;
}

static bool pact_component(struct buffer *r, struct parts *p, int *component_type)
{
	int type = *component_type = buffer_read_int32(r);
	switch (type) {
	// GUI components are not implemented.
	case PACT_COMPONENT_BUTTON:
		pact_skip(r, "iibiiiiiiiiffiiisssiii");
		return true;
	case PACT_COMPONENT_CHECKBOX:
		pact_skip(r, "iibiiiiiiiiffiiisssibii");
		return true;
	case PACT_COMPONENT_VSCROLLBAR:
	case PACT_COMPONENT_HSCROLLBAR:
		pact_skip(r, "iiiiiiifiss");
		return true;
	case PACT_COMPONENT_MULTILINE_TEXT:
		pact_skip(r, "iiiiiiiffiiibsiiiiiiiiiiiiiiisii");
		return true;
	case PACT_COMPONENT_LAYOUT_BOX:
		pact_layout_box(r, p);
		return true;
	case PACT_COMPONENT_RADIO_BOX:
		pact_skip_int_list(r);  // radio member numbers (unused for .pact)
		return true;
	case PACT_COMPONENT_SPRITE:
		return pact_sprite_component(r, p);
	default:
		WARNING("Unsupported component type %d", type);
		return false;
	}
}

static void pact_skip_motion_entries(struct buffer *r)
{
	int count = buffer_read_int32(r);
	for (int i = 0; i < count; i++)
		pact_skip(r, "ssiiii");
}

static void pact_skip_motion(struct buffer *r)
{
	pact_skip_motion_entries(r);
	int count = buffer_read_int32(r);
	for (int i = 0; i < count; i++)
		pact_skip_motion_entries(r);
}

static void pact_common(struct buffer *r, struct parts *p)
{
	p->delegate_index = buffer_read_int32(r);
	p->want_save = buffer_read_u8(r);
	p->lock_input_state = buffer_read_u8(r);
	p->local.pos.x = buffer_read_float(r);
	p->local.pos.y = buffer_read_float(r);
	p->local.z = buffer_read_int32(r);
	p->origin_mode = buffer_read_int32(r);
	p->local.show = buffer_read_u8(r);
	p->message_window = buffer_read_u8(r);
	p->local.alpha = buffer_read_int32(r);
	p->local.add_color.r = buffer_read_int32(r);
	p->local.add_color.g = buffer_read_int32(r);
	p->local.add_color.b = buffer_read_int32(r);
	p->local.multiply_color.r = buffer_read_int32(r);
	p->local.multiply_color.g = buffer_read_int32(r);
	p->local.multiply_color.b = buffer_read_int32(r);
	p->draw_filter = buffer_read_int32(r);
	p->local.scale.x = buffer_read_float(r);
	p->local.scale.y = buffer_read_float(r);
	p->local.rotation.x = buffer_read_float(r);
	p->local.rotation.y = buffer_read_float(r);
	p->local.rotation.z = buffer_read_float(r);
	p->margin_top = buffer_read_int32(r);
	p->margin_bottom = buffer_read_int32(r);
	p->margin_left = buffer_read_int32(r);
	p->margin_right = buffer_read_int32(r);
	pact_skip(r, "iiiiii");  // link part numbers (unused for .pact)
	pact_skip(r, "ibi");  // texture filter, mipmap, message skip speedup
	pact_skip(r, "i");  // parent (unused for .pact)
	pact_skip_int_list(r);  // children (unused for .pact)
	pact_skip(r, "ii");  // unknown
	for (int i = 0; i < PACT_NR_MOTIONS; i++)
		pact_skip_motion(r);
	pact_skip(r, "ii");  // unknown
	for (int i = 0; i < PACT_NR_MOTIONS; i++)
		pact_skip_motion(r);
	pact_skip_int_list(r);  // unknown
	pact_skip_int_list(r);  // unknown
	p->clickable = buffer_read_u8(r);
	p->pass_cursor = buffer_read_u8(r);
	p->draggable = buffer_read_u8(r);
	pact_skip(r, "bb");  // unknown
	pact_skip(r, "ssss");  // sound event names (not yet implemented)
	pact_skip(r, "iiii");  // sound event values
}

static bool pact_part(struct buffer *r, struct activity_part *part)
{
	part->number = PE_GetFreeNumber();
	part->name = buffer_skip_string(r);
	part->children = pact_names(r);
	for (int i = 0; i < PACT_NR_LINKS; i++) {
		if (buffer_read_u8(r))
			part->links[i] = buffer_skip_string(r);
	}
	struct parts *p = parts_get(part->number);
	pact_common(r, p);
	int component_type;
	if (!pact_component(r, p, &component_type))
		return false;
	if (component_type == PACT_COMPONENT_RADIO_BOX)
		pact_skip_names(r);  // radio members
	parts_component_dirty(p);
	parts_dirty(p);
	return true;
}

static void pact_intents(struct buffer *r, struct activity *a)
{
	a->nr_intents = buffer_read_int32(r);
	a->intents = xcalloc(a->nr_intents, sizeof(*a->intents));
	for (int i = 0; i < a->nr_intents; i++) {
		a->intents[i].name = buffer_skip_string(r);
		a->intents[i].destinations = pact_names(r);
		a->intents[i].type = buffer_read_int32(r);
	}
}

static void pact_function_state(struct buffer *r, struct activity_function_state *s)
{
	s->state = buffer_read_int32(r);
	s->nr_pairs = buffer_read_int32(r);
	s->pairs = xcalloc(s->nr_pairs, sizeof(*s->pairs));
	for (int i = 0; i < s->nr_pairs; i++) {
		s->pairs[i].map_id = buffer_skip_string(r);
		s->pairs[i].function = buffer_skip_string(r);
	}
}

static void pact_functions(struct buffer *r, struct activity *a)
{
	a->nr_functions = buffer_read_int32(r);
	a->functions = xcalloc(a->nr_functions, sizeof(*a->functions));
	for (int i = 0; i < a->nr_functions; i++) {
		struct activity_functions *f = &a->functions[i];
		f->name = buffer_skip_string(r);
		f->nr_states = buffer_read_int32(r);
		f->states = xcalloc(f->nr_states, sizeof(*f->states));
		for (int j = 0; j < f->nr_states; j++)
			pact_function_state(r, &f->states[j]);
	}
}

static void pact_end_keys(struct buffer *r, struct activity *a)
{
	a->nr_end_keys = buffer_read_int32(r);
	a->end_keys = xcalloc(a->nr_end_keys, sizeof(*a->end_keys));
	for (int i = 0; i < a->nr_end_keys; i++)
		a->end_keys[i] = buffer_read_int32(r);
}

static struct activity_part *activity_find_part_by_name(struct activity *a, const char *name)
{
	for (int i = 0; i < a->nr_parts; i++) {
		if (!strcmp(a->parts[i].name, name))
			return &a->parts[i];
	}
	return NULL;
}

static int activity_part_number(struct activity *a, const char *name)
{
	struct activity_part *part = name ? activity_find_part_by_name(a, name) : NULL;
	return part ? part->number : 0;
}

static bool activity_restore_links(struct activity *a)
{
	for (int i = 0; i < a->nr_parts; i++) {
		struct activity_part *entry = &a->parts[i];
		struct parts *p = parts_get(entry->number);
		for (int j = 0; j < entry->children.count; j++) {
			int child_no = activity_part_number(a, entry->children.values[j]);
			if (!child_no)
				return false;
			struct parts *child = parts_get(child_no);
			parts_set_parent(child, p);
			parts_component_dirty(child);
		}
		int link_numbers[PACT_NR_LINKS];
		for (int j = 0; j < PACT_NR_LINKS; j++) {
			link_numbers[j] = activity_part_number(a, entry->links[j]);
			if (entry->links[j] && !link_numbers[j])
				return false;
		}
		if (link_numbers[PACT_LINK_ALPHA_CLIPPER])
			PE_SetPartsAlphaClipperPartsNumber(entry->number,
					link_numbers[PACT_LINK_ALPHA_CLIPPER]);
		if (link_numbers[PACT_LINK_ON_CURSOR_SHOW])
			PE_SetOnCursorShowLinkPartsNumber(entry->number,
					link_numbers[PACT_LINK_ON_CURSOR_SHOW]);
	}
	return true;
}

static bool activity_deserialize(struct activity *a, struct buffer *r)
{
	int version = buffer_read_int32(r);
	if (version != 6 && version != 7) {
		WARNING("Unsupported activity version %d", version);
		return false;
	}
	pact_skip(r, "i");  // unknown, always 1
	a->nr_parts = buffer_read_int32(r);
	a->parts = xcalloc(a->nr_parts, sizeof(*a->parts));
	for (int i = 0; i < a->nr_parts; i++) {
		if (!pact_part(r, &a->parts[i]))
			return false;
	}
	if (!activity_restore_links(a))
		return false;
	pact_skip_names(r);  // ClosePartsName
	pact_intents(r, a);
	pact_functions(r, a);
	pact_end_keys(r, a);
	return true;
}

static void activity_free(struct activity *a)
{
	for (int i = 0; i < a->nr_parts; i++)
		free(a->parts[i].children.values);
	for (int i = 0; i < a->nr_intents; i++)
		free(a->intents[i].destinations.values);
	for (int i = 0; i < a->nr_functions; i++) {
		for (int j = 0; j < a->functions[i].nr_states; j++)
			free(a->functions[i].states[j].pairs);
		free(a->functions[i].states);
	}
	free(a->parts);
	free(a->intents);
	free(a->functions);
	free(a->end_keys);
	free(a->image);
	free(a);
}

static struct activity *activity_read_bytes(uint8_t *bytes, size_t size)
{
	if (size <= 4)
		return NULL;
	uint32_t expected = LittleEndian_getDW(bytes, 0);
	if (!expected)
		return NULL;
	uint8_t *image = xmalloc(expected);
	if (!zlib_decompress_exact(image, expected, bytes + 4, size - 4)) {
		free(image);
		return NULL;
	}
	struct activity *a = xcalloc(1, sizeof(*a));
	a->image = image;
	struct buffer r;
	buffer_init(&r, image, expected);
	if (!activity_deserialize(a, &r)) {
		WARNING("Failed to deserialize activity at offset %#zx", r.index);
		activity_free(a);
		return NULL;
	}
	return a;
}

static struct hash_table *activities;

static struct activity *activity_get(const char *name)
{
	return activities ? ht_get(activities, name, NULL) : NULL;
}

// Stores the delegate index of each part in erase_number_list, not the part
// number.
bool PE_ReleaseActivity(struct string *name, struct page **erase_number_list)
{
	struct activity *a = activity_get(name->text);
	if (!a)
		return false;
	int nr_parts = a->nr_parts;
	int *indices = nr_parts ? xcalloc(nr_parts, sizeof(*indices)) : NULL;
	for (int i = 0; i < nr_parts; i++) {
		int number = a->parts[i].number;
		struct parts *p = parts_try_get(number);
		indices[i] = p ? p->delegate_index : -1;
		if (p && p->controller_no == PARTS_CONTROLLER_SYSTEM_OVERLAY)
			WARNING("Cannot release system part %d", number);
		else
			parts_release(number);
	}
	ht_put(activities, name->text, NULL)->value = NULL;
	activity_free(a);
	if (nr_parts) {
		union vm_value dim = vm_int(nr_parts);
		*erase_number_list = realloc_array(*erase_number_list, 1, &dim,
				AIN_ARRAY_INT, 0, false);
		for (int i = 0; i < nr_parts; i++)
			(*erase_number_list)->values[i].i = indices[i];
	}
	free(indices);
	return true;
}

bool PE_activity_is_exist_file(struct string *filename)
{
	return asset_exists_by_name(ASSET_PACT, filename->text, NULL);
}

bool PE_activity_read_file(struct string *name, struct string *filename)
{
	if (activity_get(name->text))
		return false;
	struct archive_data *data = asset_get_by_name(ASSET_PACT, filename->text, NULL);
	if (!data)
		return false;
	struct activity *a = activity_read_bytes(data->data, data->size);
	archive_free_data(data);
	if (!a)
		return false;
	if (!activities)
		activities = ht_create(32);
	ht_put(activities, name->text, NULL)->value = a;
	return true;
}

bool PE_activity_is_loaded(struct string *name)
{
	return !!activity_get(name->text);
}

int PE_activity_numof_parts(struct string *name)
{
	struct activity *a = activity_get(name->text);
	return a ? a->nr_parts : 0;
}

static void set_string(struct string **dst, const char *text)
{
	if (*dst)
		free_string(*dst);
	*dst = *text ? cstr_to_string(text) : string_ref(&EMPTY_STRING);
}

bool PE_activity_get_parts(struct string *name, int index, struct string **parts_name,
		int *number)
{
	struct activity *a = activity_get(name->text);
	struct activity_part *part = a && index >= 0 && index < a->nr_parts
		? &a->parts[index] : NULL;
	set_string(parts_name, part ? part->name : "");
	*number = part ? part->number : 0;
	return !!part;
}

bool PE_activity_is_exist_parts_by_name(struct string *name, struct string *parts_name)
{
	struct activity *a = activity_get(name->text);
	return a && activity_find_part_by_name(a, parts_name->text);
}

static struct activity_part *activity_find_part_by_number(struct activity *a, int number)
{
	for (int i = 0; i < a->nr_parts; i++) {
		if (a->parts[i].number == number)
			return &a->parts[i];
	}
	return NULL;
}

bool PE_activity_is_exist_parts_by_number(struct string *name, int number)
{
	struct activity *a = activity_get(name->text);
	return a && activity_find_part_by_number(a, number);
}

int PE_activity_get_parts_number(struct string *name, struct string *parts_name)
{
	struct activity *a = activity_get(name->text);
	return a ? activity_part_number(a, parts_name->text) : 0;
}

void PE_activity_get_parts_name(struct string *name, int number, struct string **parts_name)
{
	struct activity *a = activity_get(name->text);
	struct activity_part *part = a ? activity_find_part_by_number(a, number) : NULL;
	set_string(parts_name, part ? part->name : "");
}

static struct activity_intent *activity_get_intent(struct string *name,
		struct string *parts_name)
{
	struct activity *a = activity_get(name->text);
	if (!a)
		return NULL;
	for (int i = 0; i < a->nr_intents; i++) {
		if (!strcmp(a->intents[i].name, parts_name->text))
			return &a->intents[i];
	}
	return NULL;
}

bool PE_activity_is_exist_intent_data(struct string *name, struct string *parts_name)
{
	return !!activity_get_intent(name, parts_name);
}

int PE_activity_numof_intent_data_destination(struct string *name, struct string *parts_name)
{
	struct activity_intent *intent = activity_get_intent(name, parts_name);
	return intent ? intent->destinations.count : 0;
}

void PE_activity_get_intent_data_destination(struct string *name, struct string *parts_name,
		int index, struct string **destination)
{
	struct activity_intent *intent = activity_get_intent(name, parts_name);
	const char *text = intent && index >= 0 && index < intent->destinations.count
		? intent->destinations.values[index] : "";
	set_string(destination, text);
}

int PE_activity_get_intent_data_type(struct string *name, struct string *parts_name)
{
	struct activity_intent *intent = activity_get_intent(name, parts_name);
	return intent ? intent->type : 0;
}

void PE_activity_get_func(struct string *name, struct string *parts_name, int state,
		struct string *map_id, struct string **function)
{
	struct activity *a = activity_get(name->text);
	if (a) {
		for (int i = 0; i < a->nr_functions; i++) {
			struct activity_functions *f = &a->functions[i];
			if (strcmp(f->name, parts_name->text))
				continue;
			for (int j = 0; j < f->nr_states; j++) {
				struct activity_function_state *s = &f->states[j];
				if (s->state != state)
					continue;
				for (int k = 0; k < s->nr_pairs; k++) {
					if (!strcmp(s->pairs[k].map_id, map_id->text)) {
						set_string(function, s->pairs[k].function);
						return;
					}
				}
			}
		}
	}
	set_string(function, "");
}

int PE_activity_numof_end_key(struct string *name)
{
	struct activity *a = activity_get(name->text);
	return a ? a->nr_end_keys : 0;
}

int PE_activity_get_end_key(struct string *name, int index)
{
	struct activity *a = activity_get(name->text);
	if (!a || index < 0 || index >= a->nr_end_keys)
		return 0;
	return a->end_keys[index];
}
