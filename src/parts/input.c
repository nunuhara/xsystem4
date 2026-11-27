/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
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
#include "system4/ain.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "asset_manager.h"
#include "audio.h"
#include "input.h"
#include "kiwi.h"
#include "parts.h"
#include "parts_widgets.h"
#define VM_PRIVATE
#include "vm.h"
#include "xsystem4.h"
#include "parts_internal.h"
#include "../hll/iarray.h"

// true between calls to BeginClick and EndClick
bool parts_began_click = false;
static bool parts_input_enabled = true;
static int input_depth;

// the mouse position at last update
static Point parts_prev_pos = {0};
// true when the left mouse button was down last update
static bool prev_clicking = false;
static bool prev_rclicking = false;
static bool prev_mclicking = false;
static bool whole_lclick_down = false;
static bool whole_rclick_down = false;
static bool whole_mclick_down = false;
// the last (fully) clicked parts number
static int clicked_parts = 0;
// the current (partially) clicked parts number
static int click_down_parts = 0;
static uint64_t click_touch_press_id = 0;

// drag state
static struct parts *drag_parts = NULL;
static bool is_dragging = false;
static SDL_FPoint drag_initial_pos;
static Point drag_start_cursor;
static struct parts *drop_target = NULL;
static struct string *click_miss_sound_name;

// gesture disambiguation & swipe state
static Point press_start_pos = {0};
static int press_hold_ms = 0;
static bool click_fired_this_frame = false;
static struct parts *swipe_candidate = NULL;
static struct parts *swipe_parts = NULL;
static bool is_swiping = false;
static bool swipe_inertia = false;
static Point swipe_begin_pos = {0};
static SDL_FPoint swipe_cur_posf = {0};
static SDL_FPoint swipe_vel = {0};
static int swipe_idle_ms = 0;
static struct parts *text_box_focus;
static bool text_box_composing;

static bool text_box_can_edit(struct parts *parts)
{
	return parts_input_enabled && parts && parts->component_type == 4
		&& parts->focused && parts->enable_input && !parts->text_box_read_only
		&& (!parts_v2 || parts->controller_no == ctrl_stack.active)
		&& parts_is_visible(parts);
}

static void text_box_commit(const char *text)
{
	struct parts *parts = text_box_focus;
	if (!text_box_can_edit(parts))
		return;
	text_box_composing = false;
	char *encoded = mai_utf2sjis(text, 0);
	struct string *updated = PE_GetTextBoxText(parts->no);
	size_t len = strcspn(encoded, "\r\n");
	if (parts->text_box_max_length > 0) {
		int remaining = parts->text_box_max_length;
		for (const char *p = updated->text; *p && remaining > 0; remaining--)
			p += SJIS_2BYTE(*p) && p[1] ? 2 : 1;
		int end = sjis_index(encoded, max(0, remaining));
		if (end >= 0)
			len = min(len, (size_t)end);
	}
	string_append_cstr(&updated, encoded, len);
	free(encoded);
	if (len) {
		PE_SetTextBoxText(parts->no, updated);
		parts_msg_push(parts, PARTS_MSG_TEXT_CHANGED, "");
	} else {
		parts_text_box_set_display(parts, NULL);
	}
	free_string(updated);
}

static void text_box_edit(const char *text, possibly_unused int start, possibly_unused int length)
{
	struct parts *parts = text_box_focus;
	if (!parts || (*text && !text_box_can_edit(parts)))
		return;
	text_box_composing = *text != '\0';
	char *encoded = *text ? mai_utf2sjis(text, 0) : NULL;
	parts_text_box_set_display(parts, encoded);
	free(encoded);
}

static void text_box_key(int key)
{
	struct parts *parts = text_box_focus;
	if (!text_box_can_edit(parts) || text_box_composing)
		return;
	if (key == VK_RETURN) {
		parts_msg_push(parts, PARTS_MSG_TEXT_FIXED, "");
	} else if (key == VK_BACK && parts->text_box_text->size) {
		struct string *updated = PE_GetTextBoxText(parts->no);
		string_pop_back(&updated);
		PE_SetTextBoxText(parts->no, updated);
		free_string(updated);
		parts_msg_push(parts, PARTS_MSG_TEXT_CHANGED, "");
	}
}

bool PE_IsTextBoxComposing(void)
{
	return text_box_composing;
}

static void text_box_set_focus(struct parts *parts)
{
	static const struct input_text_handlers handlers = { text_box_commit, text_box_edit, text_box_key };
	if (text_box_focus == parts)
		return;
	if (text_box_focus)
		input_set_parts_text_handlers(NULL);
	text_box_focus = parts;
	text_box_composing = false;
	if (parts)
		input_set_parts_text_handlers(&handlers);
}

static void text_box_update_focus(void)
{
	if (text_box_can_edit(text_box_focus))
		return;
	struct parts *parts;
	PARTS_LIST_FOREACH_REVERSE(parts) {
		if (text_box_can_edit(parts)) {
			text_box_set_focus(parts);
			return;
		}
	}
	text_box_set_focus(NULL);
}

bool parts_input_is_busy(void)
{
	return prev_clicking || is_dragging || is_swiping || swipe_inertia;
}

static void parts_set_focus(struct parts *parts, bool focus)
{
	if (parts->focused == focus) return;
	parts->focused = focus;
	if (!focus && parts == text_box_focus)
		text_box_set_focus(NULL);
	parts_msg_push(parts, focus ? PARTS_MSG_FOCUS_IN : PARTS_MSG_FOCUS_OUT, "");
}

static void parts_clear_focus(int controller)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (controller < 0 || parts->controller_no == controller)
			parts_set_focus(parts, false);
	}
}

void PE_SetFocus(int no)
{
	struct parts *parts = no > 0 ? parts_try_get(no) : NULL;
	if (!parts) return;
	if (parts->focused && parts == text_box_focus)
		return;
	parts_clear_focus(parts->controller_no);
	parts_set_focus(parts, true);
	text_box_update_focus();
}

bool PE_IsFocus(int no)
{
	struct parts *parts = parts_try_get(no);
	return parts && parts->focused;
}

static int parts_focused_descendant(struct parts *parts)
{
	if (parts->focused) return parts->no;
	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		int focused = parts_focused_descendant(child);
		if (focused) return focused;
	}
	return 0;
}

static void parts_update_focus(int root_no)
{
	struct parts *root = parts_try_get(root_no);
	if (!root) return;
	int focused = parts_focused_descendant(root);
	if (focused == root->active_part) return;
	struct parts *before = parts_try_get(root->active_part), *after = parts_try_get(focused);
	if (before) parts_msg_push(before, PARTS_MSG_FOCUS_OUT, "");
	if (after) parts_msg_push(after, PARTS_MSG_FOCUS_IN, "");
	root->active_part = focused;
}

int PE_GetActiveParts(void)
{
	struct parts *root = parts_try_get(PE_get_active_controller());
	return root && parts_try_get(root->active_part) ? root->active_part : 0;
}

static void parts_play_sound(struct parts *parts, int state)
{
	if (parts->sound_names[state]->size)
		kiwi_play_system_sound(parts->sound_names[state]);
	else if (state == PARTS_STATE_HOVERED && parts->on_cursor_sound >= 0)
		audio_play_system_sound(parts->on_cursor_sound);
	else if (state == PARTS_STATE_CLICKED && parts->on_click_sound >= 0)
		audio_play_system_sound(parts->on_click_sound);
}

static struct parts_state *parts_hittest_alpha_state(struct parts *parts, int state)
{
	if (state == PARTS_STATE_DEFAULT && parts->widget) {
		struct parts_widget *w = parts->widget;
		struct parts_state *ws = NULL;
		if (!w->enabled) {
			ws = (parts->component_type == 1 && w->checked && w->checked_disabled.type != PARTS_UNINITIALIZED)
				? &w->checked_disabled : &w->disabled;
		} else if (parts->component_type == 1 && w->checked) {
			ws = &w->checked_states[state];
		}
		if (ws && ws->type != PARTS_UNINITIALIZED)
			return ws;
	}
	return &parts->states[state];
}

static void parts_apply_sprite_deform_pixel(struct parts *parts,
		struct parts_state *alpha_state, int *px, int *py)
{
	if (alpha_state->type != PARTS_CG)
		return;
	if (parts->sprite_deform == 1)
		*px = alpha_state->common.w - 1 - *px;
	else if (parts->sprite_deform == 2)
		*py = alpha_state->common.h - 1 - *py;
}

static bool parts_hittest(struct parts *parts, int state, Point pos)
{
	if (parts->states[state].type == PARTS_LAYOUT_BOX || parts->component_type == 15)
		return false;
	Rectangle hitbox = parts->states[state].common.hitbox;
	if (parts->scrollbar)
		parts_scrollbar_hitbox(parts, state, &hitbox);
	if (hitbox.w <= 0 || hitbox.h <= 0) {
		if (parts->states[state].type != PARTS_FLAT &&
				!(parts->states[state].type == PARTS_RECT_DETECTION && parts->states[state].rect.custom))
			return false;
	}
	bool checkbox_with_label = (parts->component_type == 1 && parts->widget
			&& !parts->widget->button_mode && parts->widget->text->size);
	if (parts_v2) {
		int clipper_no = parts_get_effective_alpha_clipper(parts);
		if (clipper_no > 0) {
			static uint64_t cached_clip_gen = 0;
			static int cached_clip_no = 0;
			static Point cached_clip_pos = { INT_MIN, INT_MIN };
			static bool cached_clip_inside = true;
			if (cached_clip_gen != parts_render_gen || cached_clip_no != clipper_no ||
					cached_clip_pos.x != pos.x || cached_clip_pos.y != pos.y) {
				cached_clip_gen = parts_render_gen;
				cached_clip_no = clipper_no;
				cached_clip_pos = pos;
				cached_clip_inside = true;
				struct parts *clipper = parts_try_get(clipper_no);
				if (clipper) {
					struct parts_common *c_common = &clipper->states[clipper->state].common;
					mat4 clip_m;
					parts_world_matrix(clipper, clip_m);
					float cdet = clip_m[0][0] * clip_m[1][1] - clip_m[0][1] * clip_m[1][0];
					if (!isfinite(cdet) || fabsf(cdet) < 1e-12f) {
						cached_clip_inside = false;
					} else {
						float cdx = pos.x - clip_m[3][0], cdy = pos.y - clip_m[3][1];
						float cx = (clip_m[1][1] * cdx - clip_m[1][0] * cdy) / cdet - c_common->origin_offset.x;
						float cy = (clip_m[0][0] * cdy - clip_m[0][1] * cdx) / cdet - c_common->origin_offset.y;
						cached_clip_inside = (cx >= 0 && cy >= 0 && cx < c_common->w && cy < c_common->h);
					}
				}
			}
			if (!cached_clip_inside)
				return false;
		}
		mat4 matrix;
		parts_world_matrix(parts, matrix);
		float det = matrix[0][0] * matrix[1][1] - matrix[0][1] * matrix[1][0];
		if (!isfinite(det) || fabsf(det) < 1e-12f) return false;
		float dx = pos.x - matrix[3][0], dy = pos.y - matrix[3][1];
		float x = (matrix[1][1] * dx - matrix[1][0] * dy) / det;
		float y = (matrix[0][0] * dy - matrix[0][1] * dx) / det;
		if (parts->states[state].type == PARTS_RECT_DETECTION && parts->states[state].rect.custom) {
			struct parts_rect *rect = &parts->states[state].rect;
			x -= rect->common.origin_offset.x;
			y -= rect->common.origin_offset.y;
			const int order[4] = {0, 1, 3, 2};
			bool inside = false;
			for (int i = 0, j = 3; i < 4; j = i++) {
				Point a = rect->points[order[i]], b = rect->points[order[j]];
				if ((a.y > y) != (b.y > y)
						&& x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)
					inside = !inside;
			}
			return inside;
		}
		if (parts->states[state].type == PARTS_FLAT && !checkbox_with_label) {
			return parts_flat_hittest(&parts->states[state].flat, x, y);
		}
		if (!(x >= hitbox.x && y >= hitbox.y
				&& x < hitbox.x + hitbox.w && y < hitbox.y + hitbox.h))
			return false;
		struct parts_state *alpha_state = parts_hittest_alpha_state(parts, state);
		if ((alpha_state->type == PARTS_RECT_DETECTION && alpha_state->rect.is_cg_detection)
				|| (parts->pixel_decide && !parts->scrollbar && !checkbox_with_label)) {
			int px = (int)floorf(x - alpha_state->common.origin_offset.x);
			int py = (int)floorf(y - alpha_state->common.origin_offset.y);
			parts_apply_sprite_deform_pixel(parts, alpha_state, &px, &py);
			return parts_common_check_alpha(alpha_state, px, py);
		}
		return true;
	}
	if (parts->parent) {
		hitbox.x += parts->parent->global.pos.x;
		hitbox.y += parts->parent->global.pos.y;
	}
	if (!SDL_PointInRect(&pos, &hitbox))
		return false;
	struct parts_state *alpha_state = parts_hittest_alpha_state(parts, state);
	if ((alpha_state->type == PARTS_RECT_DETECTION && alpha_state->rect.is_cg_detection)
			|| (parts->pixel_decide && !parts->scrollbar && !checkbox_with_label)) {
		float base_x = parts->local.pos.x + (parts->parent ? parts->parent->global.pos.x : 0.0f);
		float base_y = parts->local.pos.y + (parts->parent ? parts->parent->global.pos.y : 0.0f);
		int px = (int)floorf(pos.x - base_x - alpha_state->common.origin_offset.x);
		int py = (int)floorf(pos.y - base_y - alpha_state->common.origin_offset.y);
		parts_apply_sprite_deform_pixel(parts, alpha_state, &px, &py);
		return parts_common_check_alpha(alpha_state, px, py);
	}
	return true;
}

static void drag_state_reset(void)
{
	drag_parts = NULL;
	is_dragging = false;
	drop_target = NULL;
}

static void swipe_state_reset(void)
{
	swipe_candidate = NULL;
	swipe_parts = NULL;
	is_swiping = false;
	swipe_inertia = false;
	swipe_vel = (SDL_FPoint) {0};
	swipe_idle_ms = 0;
}

void PE_Parts_StopSwipe(void)
{
	if (is_swiping && swipe_parts)
		parts_msg_push(swipe_parts, PARTS_MSG_SWIPE_END, "");
	swipe_state_reset();
}

void PE_Parts_SetSwipeType(int parts_no, int swipe_type)
{
	struct parts *parts = parts_get(parts_no);
	parts->swipe_type = swipe_type;
	if (swipe_type == 0 || swipe_type == 4) {
		if (swipe_parts == parts)
			PE_Parts_StopSwipe();
		if (swipe_candidate == parts)
			swipe_candidate = NULL;
	}
}

int PE_Parts_GetSwipeType(int parts_no)
{
	return parts_get(parts_no)->swipe_type;
}

void parts_input_reset_part(struct parts *parts)
{
	if (parts == text_box_focus)
		text_box_set_focus(NULL);
	if (parts == drag_parts)
		drag_state_reset();
	else if (parts == drop_target)
		drop_target = NULL;
	if (parts == swipe_parts || parts == swipe_candidate)
		swipe_state_reset();
	if (click_down_parts == parts->no)
		click_down_parts = 0;
}

static void reset_transient_pointer_state(void)
{
	prev_clicking = false;
	prev_rclicking = false;
	prev_mclicking = false;
	whole_lclick_down = false;
	whole_rclick_down = false;
	whole_mclick_down = false;
	clicked_parts = 0;
	click_down_parts = 0;
	click_touch_press_id = 0;
	press_hold_ms = 0;
	click_fired_this_frame = false;
	drag_state_reset();
	swipe_state_reset();
}

void parts_input_reset(void)
{
	PE_EndInput();
	text_box_set_focus(NULL);
	input_depth = 0;
	parts_began_click = false;
	parts_input_enabled = true;
	reset_transient_pointer_state();
	parts_prev_pos = (Point) {0};
	if (click_miss_sound_name) free_string(click_miss_sound_name);
	click_miss_sound_name = NULL;
}

void parts_input_save_state(struct iarray_writer *w)
{
	iarray_write(w, input_depth);
	iarray_write(w, parts_began_click);
	iarray_write(w, parts_input_enabled);
}

void parts_input_load_state(struct iarray_reader *r, int version)
{
	input_depth = iarray_read(r);
	if (input_depth < 0)
		input_depth = 0;
	parts_began_click = iarray_read(r);
	// Games disable input before loading, then rely on the restored state.
	PE_SetEnableInput(version >= 34 ? !!iarray_read(r) : true);
	reset_transient_pointer_state();
}

void parts_input_load_legacy_state(void)
{
	if (parts_v2 && ain) {
		int depth = 0;
		for (int i = 0; i < call_stack_ptr; i++) {
			int fno = call_stack[i].fno;
			if (fno < 0 || fno >= ain->nr_functions || !ain->functions[fno].name)
				continue;
			const char *fname = ain->functions[fno].name;
			if (!strcmp(fname, "parts::detail::WaitForClick")
					|| !strcmp(fname, "parts::WaitForClick")
					|| strstr(fname, "@UpdateActivity")
					|| strstr(fname, "::UpdateActivity")
					|| !strcmp(fname, "activityeditor::detail::CActivityEditor@Run")
					|| !strcmp(fname, "elkeditor::detail::CBezier2DEditor@Run")
					|| !strcmp(fname, "elkeditor::detail::CRandTypeSelecter@Run")) {
				depth++;
			}
		}
		if (depth == 0 && input_depth > 0)
			depth = 1;
		input_depth = depth;
		parts_began_click = input_depth > 0;
	}
	reset_transient_pointer_state();
}

static bool parts_is_descendant_of(struct parts *child, struct parts *ancestor)
{
	for (struct parts *p = child; p; p = p->parent) {
		if (p == ancestor)
			return true;
	}
	return false;
}

static struct parts *find_swipe_target(Point pos)
{
	if (!parts_v2 || !parts_began_click)
		return NULL;
	struct parts *first_opaque = NULL;
	struct parts *p;
	PARTS_LIST_FOREACH_REVERSE(p) {
		if (p->controller_no != ctrl_stack.active || !parts_is_visible(p))
			continue;
		if (p->enable_input && p->scrollbar && parts_hittest(p, PARTS_STATE_DEFAULT, pos))
			return NULL;
		if (p->enable_input && !first_opaque && !p->pass_cursor && parts_hittest(p, PARTS_STATE_DEFAULT, pos))
			first_opaque = p;
		if (p->swipe_type > 0 && parts_hittest(p, PARTS_STATE_DEFAULT, pos)) {
			if (p->swipe_type == 4)
				return NULL;
			if (first_opaque && first_opaque != p && !parts_is_descendant_of(first_opaque, p))
				return NULL;
			return (p->swipe_type >= 1 && p->swipe_type <= 3) ? p : NULL;
		}
	}
	return NULL;
}

static bool parts_needs_mouse_hittest(struct parts *parts)
{
	if (parts->component_type == 4)
		return true;
	if (!parts->pass_cursor || parts->clickable || parts->draggable || parts->wheelable || parts->swipe_type > 0)
		return true;
	if (parts->delegate_index >= 0 || parts->widget || parts->scrollbar)
		return true;
	if (parts->linked_from >= 0 || parts->linked_to >= 0 || parts->is_hovered)
		return true;
	if (parts->states[PARTS_STATE_HOVERED].type != PARTS_UNINITIALIZED ||
			parts->states[PARTS_STATE_CLICKED].type != PARTS_UNINITIALIZED)
		return true;
	if (parts->on_cursor_sound >= 0 || parts->sound_names[PARTS_STATE_HOVERED]->size > 0)
		return true;
	return false;
}

static void parts_update_mouse(struct parts *parts, Point cur_pos, bool cur_clicking,
		int passed_time, int wheel_forward, int wheel_back, bool *hover_consumed, bool *click_consumed)
{
	if (!parts->enable_input)
		return;
	if (parts_v2 && parts->controller_no != ctrl_stack.active) {
		parts->is_hovered = false;
		parts->hover_time = 0;
		parts_set_state(parts, PARTS_STATE_DEFAULT);
		parts_input_reset_part(parts);
		return;
	}
	// Visibility affects automatic routing, not Parts_IsCursorIn's geometry query.
	if (parts_v2 && !parts_is_visible(parts)) {
		parts->is_hovered = false;
		parts->hover_time = 0;
		parts_set_state(parts, PARTS_STATE_DEFAULT);
		parts_input_reset_part(parts);
		return;
	}

	// Always use DEFAULT state hitbox regardless of the current display state.
	bool is_hovered = !*hover_consumed
		&& parts_needs_mouse_hittest(parts)
		&& parts_hittest(parts, PARTS_STATE_DEFAULT, cur_pos);

	bool was_hovered = parts->is_hovered;
	parts->is_hovered = is_hovered;

	// !pass_cursor parts (and scrollbars) consume the cursor for parts behind them
	if (is_hovered && (!parts->pass_cursor || parts->scrollbar))
		*hover_consumed = true;

	if (is_hovered != was_hovered) {
		if (parts->linked_from >= 0) {
			struct parts *lf = parts_try_get(parts->linked_from);
			if (lf) parts_dirty(lf);
		}
		parts_engine_dirty();
	}

	if (!parts_began_click)
		return;

	if (is_hovered && !was_hovered) {
		parts_msg_push(parts, PARTS_MSG_MOUSE_ENTER, "ii", cur_pos.x, cur_pos.y);
		parts->hover_time = 0;
	}
	if (!is_hovered && was_hovered) {
		parts_msg_push(parts, PARTS_MSG_MOUSE_LEAVE, "ii", cur_pos.x, cur_pos.y);
	}

	if (is_hovered) {
		if (parts->wheelable && (wheel_forward || wheel_back))
			parts_msg_push(parts, PARTS_MSG_MOUSE_WHEEL, "ii", wheel_forward, wheel_back);
		// MOUSE_ON message fires every frame while hovering
		parts_msg_push(parts, PARTS_MSG_MOUSE_ON,
				"iii", cur_pos.x, cur_pos.y, parts->hover_time);
		parts->hover_time += passed_time;

		// MOUSE_MOVE message fires when cursor moves while hovering
		if (cur_pos.x != parts_prev_pos.x || cur_pos.y != parts_prev_pos.y) {
			parts_msg_push(parts, PARTS_MSG_MOUSE_MOVE, "ii", cur_pos.x, cur_pos.y);
		}
	}

	if (parts->scrollbar) {
		parts_scrollbar_input(parts, cur_pos, is_hovered, cur_clicking,
			is_hovered && !*click_consumed && cur_clicking && !prev_clicking,
			passed_time, wheel_back - wheel_forward);
	}
	if (!is_hovered) {
		parts_set_state(parts, PARTS_STATE_DEFAULT);
		return;
	}

	bool enabled = !parts->widget || parts->widget->enabled;
	bool clickable = enabled && (parts->clickable || parts->widget || parts->scrollbar
		|| parts->component_type == 4);
	bool click_eligible = enabled && (clickable || !parts->pass_cursor);
	if (!click_eligible || *click_consumed) {
		if (!was_hovered)
			parts_play_sound(parts, PARTS_STATE_HOVERED);
		parts_set_state(parts, PARTS_STATE_HOVERED);
		return;
	}

	// click down: first eligible part captures the click
	if (cur_clicking && !prev_clicking) {
		click_down_parts = parts->no;
		*click_consumed = true;
		// Rance 10's script-handled choices use the active backdrop to confirm a click.
		if (parts_v2 && (clickable || game_rance10))
			PE_SetFocus(parts->no);

		drag_parts = parts;
		drag_initial_pos = parts->local.pos;
		drag_start_cursor = cur_pos;

		// KEY_TRIGGER message fires on press transition
		parts_msg_push(parts, PARTS_MSG_KEY_TRIGGER, "i", VK_LBUTTON);
	}

	// KEY_DOWN message fires every frame while held (not first frame)
	if (prev_clicking && cur_clicking && click_down_parts == parts->no) {
		parts_msg_push(parts, PARTS_MSG_KEY_DOWN, "i", VK_LBUTTON);
	}

	if (cur_clicking && click_down_parts == parts->no) {
		parts_set_state(parts, PARTS_STATE_CLICKED);
	} else {
		if (!was_hovered) {
				parts_play_sound(parts, PARTS_STATE_HOVERED);
		}
		parts_set_state(parts, PARTS_STATE_HOVERED);
	}

	// click event: only if the click down event had same parts number
	if (clickable && prev_clicking && !cur_clicking
			&& click_down_parts == parts->no) {
		parts_play_sound(parts, PARTS_STATE_CLICKED);
		if (parts->clickable || (game_dohnadohna && parts->widget))
			clicked_parts = parts->no;
		click_fired_this_frame = true;
		if (parts->component_type == 1 && parts->widget)
			parts_widget_check(parts, !parts->widget->checked);

		parts_msg_push(parts, PARTS_MSG_MOUSE_CLICK,
				"iii", cur_pos.x, cur_pos.y, VK_LBUTTON);
		parts_msg_push(parts, PARTS_MSG_KEY_UP, "i", VK_LBUTTON);
	}
}

static void update_drop_target(Point cur_pos)
{
	struct parts *new_drop = NULL;
	struct parts *parts;
	PARTS_LIST_FOREACH_REVERSE(parts) {
		if (parts == drag_parts || !parts->enable_input
				|| (parts_v2 && (parts->controller_no != ctrl_stack.active || !parts_is_visible(parts))))
			continue;
		if (parts_hittest(parts, PARTS_STATE_DEFAULT, cur_pos)) {
			new_drop = parts;
			if (!parts->pass_cursor)
				break;
		}
	}
	if (new_drop != drop_target) {
		if (drop_target) {
			parts_msg_push(drop_target, PARTS_MSG_DROP_LEAVE,
					"i", drag_parts->no);
		}
		if (new_drop) {
			parts_msg_push(new_drop, PARTS_MSG_DROP_ENTER,
					"i", drag_parts->no);
		}
		drop_target = new_drop;
	} else if (drop_target) {
		parts_msg_push(drop_target, PARTS_MSG_DROP_ON,
				"iii", drag_parts->no, cur_pos.x, cur_pos.y);
	}
}

void PE_SetEnableInput(bool enable)
{
	if (parts_input_enabled == enable)
		return;
	parts_input_enabled = enable;
	if (!enable) {
		reset_transient_pointer_state();
		struct parts *p;
		PARTS_LIST_FOREACH(p) {
			parts_input_reset_part(p);
			p->is_hovered = false;
			p->hover_time = 0;
		}
	}
}

bool PE_IsEnableInput(void)
{
	return parts_input_enabled;
}

static void update_touch_click_capture(bool cur_clicking)
{
	uint64_t canceled_press = input_take_long_touch_click_cancel();
	if (prev_clicking && canceled_press && click_touch_press_id == canceled_press) {
		struct parts *down = click_down_parts ? parts_try_get(click_down_parts) : NULL;
		if (down)
			parts_msg_push(down, PARTS_MSG_KEY_UP, "i", VK_LBUTTON);
		if (parts_v2 && parts_began_click && whole_lclick_down)
			parts_msg_push(NULL, PARTS_MSG_KEY_UP, "i", VK_LBUTTON);
		prev_clicking = false;
		whole_lclick_down = false;
		click_down_parts = 0;
		drag_state_reset();
		swipe_candidate = NULL;
	}
	if (!cur_clicking || !parts_began_click)
		click_touch_press_id = 0;
	else if (!prev_clicking)
		click_touch_press_id = input_get_touch_press_id();
}

void PE_UpdateInputState(int passed_time)
{
	Point cur_pos;
	click_fired_this_frame = false;
	input_parts_poll_begin(parts_began_click && parts_input_enabled);
	mouse_get_pos(&cur_pos.x, &cur_pos.y);
	if (!parts_input_enabled) {
		text_box_set_focus(NULL);
		input_take_long_touch_click_cancel();
		parts_prev_pos = cur_pos;
		input_notify_parts_poll(false, false);
		return;
	}
	bool cur_clicking = key_is_down(VK_LBUTTON);
	update_touch_click_capture(cur_clicking);
	bool cur_rclicking = key_is_down(VK_RBUTTON);
	bool cur_mclicking = key_is_down(VK_MBUTTON);
	int wheel_forward, wheel_back;
	mouse_get_wheel(&wheel_forward, &wheel_back);

	bool hover_consumed = false;
	bool click_consumed = false;
	if (parts_v2 && parts_began_click && cur_clicking && !prev_clicking) {
		if (swipe_inertia && swipe_parts) {
			parts_msg_push(swipe_parts, PARTS_MSG_SWIPE_END, "");
			swipe_state_reset();
		}
		press_start_pos = cur_pos;
		press_hold_ms = 0;
		swipe_candidate = find_swipe_target(cur_pos);
		parts_clear_focus(ctrl_stack.active);
	}
	struct parts *parts;
	// Iterate front-to-back (highest z first) for proper cursor consumption
	PARTS_LIST_FOREACH_REVERSE(parts) {
		parts_update_mouse(parts, cur_pos, cur_clicking, passed_time,
				wheel_forward, wheel_back, &hover_consumed, &click_consumed);
	}

	if (parts_v2 && parts_began_click) {
		if (cur_pos.x != parts_prev_pos.x || cur_pos.y != parts_prev_pos.y)
			parts_msg_push(NULL, PARTS_MSG_MOUSE_MOVE, "ii", cur_pos.x, cur_pos.y);
		if (wheel_forward || wheel_back)
			parts_msg_push(NULL, PARTS_MSG_MOUSE_WHEEL, "ii", wheel_forward, wheel_back);
		const int btns[3] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON };
		const bool cur_btns[3] = { cur_clicking, cur_rclicking, cur_mclicking };
		const bool prev_btns[3] = { prev_clicking, prev_rclicking, prev_mclicking };
		bool *whole_downs[3] = { &whole_lclick_down, &whole_rclick_down, &whole_mclick_down };
		for (int i = 0; i < 3; i++) {
			if (cur_btns[i] && !prev_btns[i]) {
				*whole_downs[i] = true;
				parts_msg_push(NULL, PARTS_MSG_KEY_TRIGGER, "i", btns[i]);
			} else if (prev_btns[i] && cur_btns[i] && *whole_downs[i]) {
				parts_msg_push(NULL, PARTS_MSG_KEY_DOWN, "i", btns[i]);
			} else if (prev_btns[i] && !cur_btns[i] && *whole_downs[i]) {
				*whole_downs[i] = false;
				parts_msg_push(NULL, PARTS_MSG_MOUSE_CLICK, "iii", cur_pos.x, cur_pos.y, btns[i]);
				parts_msg_push(NULL, PARTS_MSG_KEY_UP, "i", btns[i]);
			}
		}
	}

	int step_ms = passed_time < 0 ? 0 : (passed_time > 50 ? 50 : passed_time);
	int dt_ms = passed_time < 1 ? 1 : (passed_time > 100 ? 100 : passed_time);

	// Gesture disambiguation, Swipe, and Drag movement processing
	if (cur_clicking && parts_began_click) {
		bool cursor_moved = (cur_pos.x != parts_prev_pos.x || cur_pos.y != parts_prev_pos.y);
		if (prev_clicking)
			press_hold_ms += step_ms;

		if (!is_dragging && !is_swiping) {
			if (swipe_candidate && (swipe_candidate->controller_no != ctrl_stack.active
					|| !parts_is_visible(swipe_candidate)
					|| swipe_candidate->swipe_type < 1 || swipe_candidate->swipe_type > 3)) {
				swipe_candidate = NULL;
			}
			int dx = cur_pos.x - press_start_pos.x;
			int dy = cur_pos.y - press_start_pos.y;
			int adx = abs(dx), ady = abs(dy);

			bool swipe_axis_match = false;
			if (swipe_candidate) {
				if (swipe_candidate->swipe_type == 1)
					swipe_axis_match = (adx >= 8 || ady >= 8);
				else if (swipe_candidate->swipe_type == 2)
					swipe_axis_match = (ady >= 8 && ady >= adx);
				else if (swipe_candidate->swipe_type == 3)
					swipe_axis_match = (adx >= 8 && adx >= ady);
			}

			bool can_drag_part = drag_parts && (drag_parts->draggable
					|| (ain->version >= 11 && (drag_parts->clickable || !drag_parts->pass_cursor)));
			if (swipe_axis_match && (!can_drag_part || press_hold_ms < 180)) {
				if (click_down_parts > 0) {
					struct parts *cdp = parts_try_get(click_down_parts);
					if (cdp)
						parts_set_state(cdp, cdp->is_hovered ? PARTS_STATE_HOVERED : PARTS_STATE_DEFAULT);
					click_down_parts = 0;
				}
				drag_state_reset();
				input_cancel_long_touch();
				swipe_parts = swipe_candidate;
				swipe_candidate = NULL;
				is_swiping = true;
				swipe_inertia = false;
				swipe_begin_pos = press_start_pos;
				swipe_cur_posf = (SDL_FPoint) { (float)cur_pos.x, (float)cur_pos.y };
				float ivx = (swipe_parts->swipe_type == 2) ? 0.0f : (float)dx / (float)(press_hold_ms > 0 ? press_hold_ms : dt_ms);
				float ivy = (swipe_parts->swipe_type == 3) ? 0.0f : (float)dy / (float)(press_hold_ms > 0 ? press_hold_ms : dt_ms);
				swipe_vel = (SDL_FPoint) { ivx, ivy };
				swipe_idle_ms = 0;
				int mx = (swipe_parts->swipe_type == 2) ? swipe_begin_pos.x : cur_pos.x;
				int my = (swipe_parts->swipe_type == 3) ? swipe_begin_pos.y : cur_pos.y;
				parts_msg_push(swipe_parts, PARTS_MSG_SWIPE_BEGIN, "");
				parts_msg_push(swipe_parts, PARTS_MSG_SWIPING, "iiiib",
						swipe_begin_pos.x, swipe_begin_pos.y, mx, my, false);
			} else if (can_drag_part) {
				if (ain->version < 11) {
					if (cursor_moved && drag_parts->draggable) {
						SDL_FPoint new_pos = {
							drag_initial_pos.x + (cur_pos.x - drag_start_cursor.x),
							drag_initial_pos.y + (cur_pos.y - drag_start_cursor.y)
						};
						parts_set_posf(drag_parts, new_pos);
						parts_msg_push(drag_parts, PARTS_MSG_DRAG_BEGIN, "");
						parts_msg_push(drag_parts, PARTS_MSG_DRAGGING, "iiii",
								drag_start_cursor.x, drag_start_cursor.y, cur_pos.x, cur_pos.y);
						is_dragging = true;
						input_cancel_long_touch();
						update_drop_target(cur_pos);
					}
				} else {
					bool start_drag = false;
					if (!swipe_candidate) {
						start_drag = drag_parts->draggable ? cursor_moved : (adx >= 4 || ady >= 4);
					} else {
						bool ortho_drag = (swipe_candidate->swipe_type == 2 && adx >= 10 && adx > ady * 2)
								|| (swipe_candidate->swipe_type == 3 && ady >= 10 && ady > adx * 2);
						start_drag = (press_hold_ms >= 180) || ortho_drag;
					}
					if (start_drag) {
						swipe_candidate = NULL;
						is_dragging = true;
						input_cancel_long_touch();
						parts_msg_push(drag_parts, PARTS_MSG_DRAG_BEGIN, "i", VK_LBUTTON);
						if (drag_parts->draggable) {
							SDL_FPoint new_pos = {
								drag_initial_pos.x + (cur_pos.x - drag_start_cursor.x),
								drag_initial_pos.y + (cur_pos.y - drag_start_cursor.y)
							};
							parts_set_posf(drag_parts, new_pos);
						}
						if (cur_pos.x != drag_start_cursor.x || cur_pos.y != drag_start_cursor.y) {
							parts_msg_push(drag_parts, PARTS_MSG_DRAGGING, "iiiii",
									drag_start_cursor.x, drag_start_cursor.y,
									cur_pos.x, cur_pos.y, VK_LBUTTON);
							update_drop_target(cur_pos);
						}
					}
				}
			}
		} else if (is_swiping && !swipe_inertia && swipe_parts) {
			int sdx = cur_pos.x - parts_prev_pos.x;
			int sdy = cur_pos.y - parts_prev_pos.y;
			if (sdx != 0 || sdy != 0) {
				float inst_vx = (swipe_parts->swipe_type == 2) ? 0.0f : (float)sdx / (float)dt_ms;
				float inst_vy = (swipe_parts->swipe_type == 3) ? 0.0f : (float)sdy / (float)dt_ms;
				swipe_vel.x = swipe_vel.x * 0.4f + inst_vx * 0.6f;
				swipe_vel.y = swipe_vel.y * 0.4f + inst_vy * 0.6f;
				swipe_idle_ms = 0;
				swipe_cur_posf = (SDL_FPoint) { (float)cur_pos.x, (float)cur_pos.y };
				int mx = (swipe_parts->swipe_type == 2) ? swipe_begin_pos.x : cur_pos.x;
				int my = (swipe_parts->swipe_type == 3) ? swipe_begin_pos.y : cur_pos.y;
				parts_msg_push(swipe_parts, PARTS_MSG_SWIPING, "iiiib",
						swipe_begin_pos.x, swipe_begin_pos.y, mx, my, false);
			} else {
				swipe_idle_ms += dt_ms;
				if (swipe_idle_ms > 80)
					swipe_vel = (SDL_FPoint) { 0.0f, 0.0f };
			}
		} else if (is_dragging && drag_parts && cursor_moved) {
			if (drag_parts->draggable) {
				SDL_FPoint new_pos = {
					drag_initial_pos.x + (cur_pos.x - drag_start_cursor.x),
					drag_initial_pos.y + (cur_pos.y - drag_start_cursor.y)
				};
				parts_set_posf(drag_parts, new_pos);
			}
			if (ain->version >= 11) {
				parts_msg_push(drag_parts, PARTS_MSG_DRAGGING, "iiiii",
						drag_start_cursor.x, drag_start_cursor.y,
						cur_pos.x, cur_pos.y, VK_LBUTTON);
			} else if (drag_parts->draggable) {
				parts_msg_push(drag_parts, PARTS_MSG_DRAGGING, "iiii",
						drag_start_cursor.x, drag_start_cursor.y, cur_pos.x, cur_pos.y);
			}
			update_drop_target(cur_pos);
		}
	}

	// Release handling
	if (prev_clicking && !cur_clicking) {
		swipe_candidate = NULL;
		if (is_swiping && !swipe_inertia && swipe_parts) {
			float speed = hypotf(swipe_vel.x, swipe_vel.y);
			if (speed >= 0.15f) {
				if (speed > 4.5f) {
					swipe_vel.x *= 4.5f / speed;
					swipe_vel.y *= 4.5f / speed;
				}
				swipe_inertia = true;
			} else {
				parts_msg_push(swipe_parts, PARTS_MSG_SWIPE_END, "");
				swipe_state_reset();
			}
		} else {
			if (is_dragging && drag_parts) {
				if (ain->version >= 11 && drag_parts->clickable
						&& click_down_parts == drag_parts->no && !click_fired_this_frame
						&& (drag_parts->local.pos.x != drag_initial_pos.x || drag_parts->local.pos.y != drag_initial_pos.y)
						&& parts_hittest(drag_parts, PARTS_STATE_DEFAULT, cur_pos)) {
					parts_msg_push(drag_parts, PARTS_MSG_MOUSE_CLICK,
							"iii", cur_pos.x, cur_pos.y, VK_LBUTTON);
					parts_msg_push(drag_parts, PARTS_MSG_KEY_UP, "i", VK_LBUTTON);
				}
				if (drop_target) {
					parts_msg_push(drop_target, PARTS_MSG_DROPPED,
							"iii", drag_parts->no, cur_pos.x, cur_pos.y);
					parts_msg_push(drop_target, PARTS_MSG_DROP_LEAVE, "i", drag_parts->no);
				}
				if (ain->version >= 12) {
					parts_msg_push(drag_parts, PARTS_MSG_DRAG_END, "ib", VK_LBUTTON, false);
				} else if (ain->version >= 11) {
					parts_msg_push(drag_parts, PARTS_MSG_DRAG_END, "i", VK_LBUTTON);
				} else if (drag_parts->draggable) {
					parts_msg_push(drag_parts, PARTS_MSG_DRAG_END, "");
				}
			}
			drag_state_reset();

			if (!click_down_parts) {
				if (parts_began_click && click_miss_sound_name && click_miss_sound_name->size)
					kiwi_play_system_sound(click_miss_sound_name);
			}
			click_down_parts = 0;
		}
	}

	// Inertia coasting update
	if (swipe_inertia && swipe_parts && !cur_clicking) {
		if (!parts_began_click
				|| swipe_parts->controller_no != ctrl_stack.active
				|| !parts_is_visible(swipe_parts)
				|| swipe_parts->swipe_type < 1 || swipe_parts->swipe_type > 3) {
			parts_msg_push(swipe_parts, PARTS_MSG_SWIPE_END, "");
			swipe_state_reset();
		} else {
			int prev_ix = (int)lroundf(swipe_cur_posf.x);
			int prev_iy = (int)lroundf(swipe_cur_posf.y);
			swipe_cur_posf.x += swipe_vel.x * (float)dt_ms;
			swipe_cur_posf.y += swipe_vel.y * (float)dt_ms;
			float decay = powf(0.92f, (float)dt_ms / 16.0f);
			swipe_vel.x *= decay;
			swipe_vel.y *= decay;
			int new_ix = (swipe_parts->swipe_type == 2) ? swipe_begin_pos.x : (int)lroundf(swipe_cur_posf.x);
			int new_iy = (swipe_parts->swipe_type == 3) ? swipe_begin_pos.y : (int)lroundf(swipe_cur_posf.y);
			if (new_ix != prev_ix || new_iy != prev_iy) {
				parts_msg_push(swipe_parts, PARTS_MSG_SWIPING, "iiiib",
						swipe_begin_pos.x, swipe_begin_pos.y, new_ix, new_iy, true);
			}
			if (hypotf(swipe_vel.x, swipe_vel.y) < 0.04f) {
				parts_msg_push(swipe_parts, PARTS_MSG_SWIPE_END, "");
				swipe_state_reset();
			}
		}
	}

	if (is_dragging || is_swiping)
		input_cancel_long_touch();

	if (parts_began_click) {
		prev_clicking = cur_clicking;
		prev_rclicking = cur_rclicking;
		prev_mclicking = cur_mclicking;
	} else {
		prev_clicking = false;
		prev_rclicking = false;
		prev_mclicking = false;
		click_down_parts = 0;
	}
	parts_prev_pos = cur_pos;
	input_notify_parts_poll(parts_began_click, cur_clicking || cur_rclicking);
	text_box_update_focus();
	if (parts_v2) {
		parts_update_focus(ctrl_stack.overlay_id);
		for (int i = 0; i < ctrl_stack.nr_controllers; i++)
			parts_update_focus(ctrl_stack.ids[i]);
	}
}

void PE_SetPassCursor(int parts_no, bool pass)
{
	parts_get(parts_no)->pass_cursor = !!pass;
}

bool PE_GetPartsPassCursor(int parts_no)
{
	return parts_get(parts_no)->pass_cursor;
}

void PE_SetClickable(int parts_no, bool clickable)
{
	parts_get(parts_no)->clickable = !!clickable;
}

void PE_SetWheelable(int parts_no, bool enable)
{
	parts_get(parts_no)->wheelable = enable;
}

bool PE_GetPartsClickable(int parts_no)
{
	return parts_get(parts_no)->clickable;
}

void PE_Parts_SetSoundName(int parts_no, struct string *name, int state)
{
	if (!parts_state_valid(--state)) return;
	struct parts *parts = parts_get(parts_no);
	free_string(parts->sound_names[state]);
	parts->sound_names[state] = string_dup(name);
}

struct string *PE_Parts_GetSoundName(int parts_no, int state)
{
	if (!parts_state_valid(--state)) return string_ref(&EMPTY_STRING);
	return string_ref(parts_get(parts_no)->sound_names[state]);
}

void PE_Parts_PlaySound(int parts_no, int state)
{
	if (parts_state_valid(--state))
		parts_play_sound(parts_get(parts_no), state);
}

void PE_SetEnableInputProcess(int parts_no, bool enable)
{
	struct parts *parts = parts_get(parts_no);
	parts->enable_input = enable;
	if (!enable) {
		parts_set_focus(parts, false);
		parts_input_reset_part(parts);
		parts->is_hovered = false;
		parts->hover_time = 0;
	}
}

bool PE_IsEnableInputProcess(int parts_no)
{
	return parts_get(parts_no)->enable_input;
}

void PE_SetPartsGroupDecideOnCursor(possibly_unused int group_no, possibly_unused bool decide_on_cursor)
{
	UNIMPLEMENTED("(%d, %s)", group_no, decide_on_cursor ? "true" : "false");
}

void PE_SetPartsGroupDecideClick(possibly_unused int group_no, possibly_unused bool decide_click)
{
	UNIMPLEMENTED("(%d, %s)", group_no, decide_click ? "true" : "false");
}

void PE_SetOnCursorShowLinkPartsNumber(int parts_no, int link_parts_no)
{
	struct parts *parts = parts_get(parts_no);
	if (parts->linked_to > 0) {
		struct parts *old_link = parts_try_get(parts->linked_to);
		if (old_link && old_link->linked_from == parts_no)
			old_link->linked_from = -1;
	}
	if (link_parts_no <= 0) {
		parts->linked_to = -1;
		parts_dirty(parts);
		return;
	}
	struct parts *link_parts = parts_get(link_parts_no);
	parts->linked_to = link_parts_no;
	link_parts->linked_from = parts_no;
	parts_dirty(parts);
}

int PE_GetOnCursorShowLinkPartsNumber(int parts_no)
{
	int linked_to = parts_get(parts_no)->linked_to;
	return parts_v2 && linked_to < 0 ? 0 : linked_to;
}

bool PE_SetPartsOnCursorSoundNumber(int parts_no, int sound_no)
{
	if (!asset_exists(ASSET_SOUND, sound_no)) {
		WARNING("Invalid sound number: %d", sound_no);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	parts->on_cursor_sound = sound_no;
	return true;
}

bool PE_SetPartsClickSoundNumber(int parts_no, int sound_no)
{
	if (!asset_exists(ASSET_SOUND, sound_no)) {
		WARNING("Invalid sound number: %d", sound_no);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	parts->on_click_sound = sound_no;
	return true;
}

bool PE_SetClickMissSoundNumber(possibly_unused int sound_no)
{
	UNIMPLEMENTED("(%d)", sound_no);
	return true;
}

void PE_SetClickMissSoundName(struct string *name)
{
	struct string *copy = string_dup(name);
	if (click_miss_sound_name) free_string(click_miss_sound_name);
	click_miss_sound_name = copy;
}

struct string *PE_GetClickMissSoundName(void)
{
	return string_ref(click_miss_sound_name ? click_miss_sound_name : &EMPTY_STRING);
}

void PE_BeginInput(void)
{
	if (parts_v2) {
		input_depth++;
		parts_clear_focus(-1);
	}
	parts_began_click = true;
}

void PE_EndInput(void)
{
	if (parts_v2) {
		if (input_depth > 0) input_depth--;
		if (input_depth > 0) parts_clear_focus(-1);
		parts_began_click = input_depth > 0;
	} else parts_began_click = false;
	if (!parts_began_click) {
		if (text_box_focus)
			parts_set_focus(text_box_focus, false);
		prev_clicking = false;
		prev_rclicking = false;
		prev_mclicking = false;
		whole_lclick_down = false;
		whole_rclick_down = false;
		whole_mclick_down = false;
		click_down_parts = 0;
		click_touch_press_id = 0;
		swipe_state_reset();
	}
	clicked_parts = 0;
	drag_state_reset();
}

void PE_SetDrag(int parts_no, bool enable)
{
	parts_get(parts_no)->draggable = !!enable;
}

int PE_GetClickPartsNumber(void)
{
	return clicked_parts;
}

bool PE_IsCursorIn(int parts_no, int mouse_x, int mouse_y, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return false;

	Point mouse_pos = { mouse_x, mouse_y };
	return parts_hittest(parts, state, mouse_pos);
}
