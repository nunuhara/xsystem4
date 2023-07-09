/* Copyright (C) 2023 kichikuou <KichikuouChrome@gmail.com>
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

#ifndef SYSTEM4_SWF_H
#define SYSTEM4_SWF_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef int32_t fixed32;  // 32-bit 16.16 fixed-point number
typedef int16_t fixed16;  // 16-bit 8.8 fixed-point number
typedef uint16_t fixedu16;
typedef uint32_t rgb_t;  // 0x00BBGGRR
typedef uint32_t rgba_t; // 0xAABBGGRR

static inline float twips_to_float(int32_t twips) {
	return twips / 20.0f;
}

static inline float fixed32_to_float(fixed32 f32) {
	return f32 / 65536.0f;
}

static inline float fixed16_to_float(fixed16 f16) {
	return f16 / 256.0f;
}

static inline fixed16 fixed16_mul(fixed16 a, fixed16 b) {
	return (int32_t)a * (int32_t)b / 256;
}

struct swf_rect {
	// in twips (1/20 pixel)
	int32_t x_min;
	int32_t x_max;
	int32_t y_min;
	int32_t y_max;
};

struct swf {
	uint8_t version;
	uint32_t file_length;
	struct swf_rect frame_size;
	fixedu16 frame_rate;
	uint16_t frame_count;
	struct swf_tag *tags;
};

struct swf_matrix {
	fixed32 scale_x;
	fixed32 scale_y;
	fixed32 rotate_skew_0;
	fixed32 rotate_skew_1;
	int32_t translate_x;
	int32_t translate_y;
};

struct swf_cxform_with_alpha {
	int16_t add_terms[4];  // red, green, blue, alpha
	fixed16 mult_terms[4]; // red, green, blue, alpha
};

// We only support clipped bitmap fill.
struct swf_fill_style {
	uint16_t bitmap_id;
	bool smoothed;
	struct swf_matrix matrix;
};

struct swf_sound_spec {
	uint16_t rate;
	uint8_t sample_size;  // 8 or 16 (bits)
	uint8_t channels;
};

// Tags

enum swf_tag_type {
	TAG_END = 0,
	TAG_SHOW_FRAME = 1,
	TAG_DEFINE_SHAPE = 2,
	TAG_SET_BACKGROUND_COLOR = 9,
	TAG_DO_ACTION = 12,
	TAG_DEFINE_SOUND = 14,
	TAG_START_SOUND = 15,
	TAG_DEFINE_BITS_LOSSLESS = 20,
	TAG_PLACE_OBJECT2 = 26,
	TAG_REMOVE_OBJECT2 = 28,
	TAG_DEFINE_BITS_LOSSLESS2 = 36,
	TAG_DEFINE_SPRITE = 39,
	TAG_SOUND_STREAM_HEAD2 = 45,
	TAG_FILE_ATTRIBUTES = 69,
	TAG_PLACE_OBJECT3 = 70,
};

struct swf_tag {
	int type;
	struct swf_tag *next;
};

struct swf_tag_define_bits_lossless {
	struct swf_tag t;  // TAG_DEFINE_BITS_LOSSLESS or TAG_DEFINE_BITS_LOSSLESS2
	uint16_t character_id;
	uint8_t format;
	uint16_t width;
	uint16_t height;
	uint8_t *data;
};

// We only support simple rectangle shapes with a fill style.
struct swf_tag_define_shape {
	struct swf_tag t;
	uint16_t shape_id;
	struct swf_rect bounds;
	struct swf_fill_style fill_style;
};

enum swf_sound_format {
	SWF_SOUND_UNCOMPRESSED_LE = 3,
};

struct swf_tag_define_sound {
	struct swf_tag t;
	uint16_t sound_id;
	uint8_t format;
	struct swf_sound_spec spec;
	uint32_t sample_count;
	uint8_t *data;
	uint32_t data_len;
};

struct swf_tag_define_sprite {
	struct swf_tag t;
	uint16_t sprite_id;
	uint16_t frame_count;
	struct swf_tag *tags;
};

struct swf_tag_do_action {
	struct swf_tag t;
	struct swf_action *actions;
};

enum place_object_flags {
	PLACE_FLAG_RESERVED_BITS       = 0xffff - ((1 << 11) - 1),
	PLACE_FLAG_HAS_CACHE_AS_BITMAP = 1 << 10,
	PLACE_FLAG_HAS_BLEND_MODE      = 1 << 9,
	PLACE_FLAG_HAS_FILTER_LIST     = 1 << 8,
	PLACE_FLAG_HAS_CLIP_ACTIONS    = 1 << 7,
	PLACE_FLAG_HAS_CLIP_DEPTH      = 1 << 6,
	PLACE_FLAG_HAS_NAME            = 1 << 5,
	PLACE_FLAG_HAS_RATIO           = 1 << 4,
	PLACE_FLAG_HAS_COLOR_TRANSFORM = 1 << 3,
	PLACE_FLAG_HAS_MATRIX          = 1 << 2,
	PLACE_FLAG_HAS_CHARACTER       = 1 << 1,
	PLACE_FLAG_MOVE                = 1 << 0,
};

struct swf_tag_place_object {
	struct swf_tag t;  // TAG_PLACE_OBJECT2 or TAG_PLACE_OBJECT3
	uint16_t flags;
	uint16_t depth;
	uint16_t character_id;
	struct swf_matrix matrix;
	struct swf_cxform_with_alpha color_transform;
	uint16_t ratio;
	char *name;
	uint8_t blend_mode;
	uint8_t bitmap_cache;
};

struct swf_tag_remove_object2 {
	struct swf_tag t;
	uint16_t depth;
};

struct swf_tag_set_background_color {
	struct swf_tag t;
	rgb_t background_color;
};

struct swf_tag_sound_stream_head {
	struct swf_tag t;
	struct swf_sound_spec playback_spec;
	uint8_t compression;
	struct swf_sound_spec stream_spec;
	uint16_t sample_count;
	int16_t latency_seek;
};

enum start_sound_flags {
	SOUND_INFO_RESERVED_BITS    = 0xff - ((1 << 6) - 1),
	SOUND_INFO_SYNC_STOP        = 1 << 5,
	SOUND_INFO_SYNC_NO_MULTIPLE = 1 << 4,
	SOUND_INFO_HAS_ENVELOPE     = 1 << 3,
	SOUND_INFO_HAS_LOOPS        = 1 << 2,
	SOUND_INFO_HAS_OUT_POINT    = 1 << 1,
	SOUND_INFO_HAS_IN_POINT     = 1 << 0,
};

struct swf_tag_start_sound {
	struct swf_tag t;
	uint16_t sound_id;
	uint8_t flags;
	uint32_t in_point;
	uint32_t out_point;
	uint16_t loop_count;
};

// Actions

enum swf_action_type {
	// Actions without payload.
	ACTION_END = 0x00,
	ACTION_PLAY = 0x06,
	ACTION_STOP = 0x07,
	ACTION_POP = 0x17,
	ACTION_CALL_FUNCTION = 0x3d,

	// Actions with payloads.
	ACTION_GOTO_FRAME = 0x81,
	ACTION_CONSTANT_POOL = 0x88,
	ACTION_PUSH = 0x96,
};

struct swf_action {
	uint8_t code;
	struct swf_action *next;
};

struct swf_action_constant_pool {
	struct swf_action a;
	uint16_t count;
	char *constant_pool[];
};

struct swf_action_goto_frame {
	struct swf_action a;
	uint16_t frame;
};

enum swf_action_push_type {
	ACTION_PUSH_INTEGER = 7,
	ACTION_PUSH_CONSTANT8 = 8,
};

struct swf_action_push {
	struct swf_action a;
	uint16_t count;
	struct {
		enum swf_action_push_type type;
		uint32_t value;
	} values[];
};

struct swf *swf_load(uint8_t *data, size_t size);
void swf_free(struct swf *swf);
struct swf *aff_load(uint8_t *data, size_t size);

#endif /* SYSTEM4_SWF_H */
