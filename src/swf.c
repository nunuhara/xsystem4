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

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "system4.h"
#include "system4/buffer.h"
#include "system4/little_endian.h"
#include "swf.h"

static void free_tag_list(struct swf_tag *tags);
static void free_action_list(struct swf_action *actions);

void swf_free(struct swf *swf)
{
	free_tag_list(swf->tags);
	free(swf);
}

static void free_define_bits_lossless(struct swf_tag_define_bits_lossless *tag)
{
	free(tag->data);
	free(tag);
}

static void free_define_sound(struct swf_tag_define_sound *tag)
{
	free(tag->data);
	free(tag);
}

static void free_define_sprite(struct swf_tag_define_sprite *tag)
{
	free_tag_list(tag->tags);
	free(tag);
}

static void free_do_action(struct swf_tag_do_action *tag)
{
	free_action_list(tag->actions);
	free(tag);
}

static void free_place_object(struct swf_tag_place_object *tag)
{
	free(tag->name);
	free(tag);
}

static void free_tag_list(struct swf_tag *tags)
{
	struct swf_tag *tag = tags;
	while (tag) {
		struct swf_tag *next = tag->next;
		switch (tag->type) {
		case TAG_END:
		case TAG_SHOW_FRAME:
		case TAG_FILE_ATTRIBUTES:
		case TAG_REMOVE_OBJECT2:
		case TAG_SET_BACKGROUND_COLOR:
		case TAG_SOUND_STREAM_HEAD2:
		case TAG_START_SOUND:
		case TAG_DEFINE_SHAPE:
			free(tag);
			break;
		case TAG_DEFINE_BITS_LOSSLESS:
		case TAG_DEFINE_BITS_LOSSLESS2:
			free_define_bits_lossless((struct swf_tag_define_bits_lossless *)tag);
			break;
		case TAG_DEFINE_SOUND:
			free_define_sound((struct swf_tag_define_sound *)tag);
			break;
		case TAG_DEFINE_SPRITE:
			free_define_sprite((struct swf_tag_define_sprite *)tag);
			break;
		case TAG_DO_ACTION:
			free_do_action((struct swf_tag_do_action *)tag);
			break;
		case TAG_PLACE_OBJECT2:
		case TAG_PLACE_OBJECT3:
			free_place_object((struct swf_tag_place_object *)tag);
			break;
		default:
			ERROR("unknown tag %d", tag->type);
		}
		tag = next;
	}
}

static void free_action_constant_pool(struct swf_action_constant_pool *act)
{
	for (uint16_t i = 0; i < act->count; i++) {
		free(act->constant_pool[i]);
	}
	free(act);
}

static void free_action_list(struct swf_action *actions)
{
	struct swf_action *act = actions;
	while (act) {
		struct swf_action *next = act->next;
		switch (act->code) {
		case ACTION_CONSTANT_POOL:
			free_action_constant_pool((struct swf_action_constant_pool *)act);
			break;
		default:
			free(act);
			break;
		}
		act = next;
	}
}

struct bit_reader {
	struct buffer *buf;
	uint8_t byte;
	unsigned remaining_bits;
};

static uint32_t read_unsigned_bits(struct bit_reader* br, unsigned bits)
{
	assert(bits <= 32);

	uint32_t result = 0;
	if (br->remaining_bits) {
		if (bits < br->remaining_bits) {
			result = br->byte >> (br->remaining_bits - bits);
			br->remaining_bits -= bits;
			br->byte &= (1 << br->remaining_bits) - 1;
			return result;
		}
		result = br->byte;
		bits -= br->remaining_bits;
		br->remaining_bits = 0;
	}
	while (bits >= 8) {
		result <<= 8;
		result |= buffer_read_u8(br->buf);
		bits -= 8;
	}
	if (bits) {
		uint8_t byte = buffer_read_u8(br->buf);
		result <<= bits;
		br->remaining_bits = 8 - bits;
		result |= byte >> br->remaining_bits;
		br->byte = byte & ((1 << br->remaining_bits) - 1);
	}
	return result;
}

static int32_t read_signed_bits(struct bit_reader* br, unsigned bits)
{
	uint32_t u = read_unsigned_bits(br, bits);
	if (bits < 32 && u & 1 << (bits - 1))
		u |= ~((1 << bits) - 1);  // sign extension
	return (int32_t)u;
}

static rgb_t parse_rgb(struct buffer *buf)
{
	uint32_t r = buffer_read_u8(buf);
	uint32_t g = buffer_read_u8(buf);
	uint32_t b = buffer_read_u8(buf);
	return r | g << 8 | b << 16;
}

static void parse_swf_rect(struct buffer *r, struct swf_rect *dest)
{
	struct bit_reader br = {.buf = r};
	uint32_t n_bits = read_unsigned_bits(&br, 5);
	dest->x_min = read_signed_bits(&br, n_bits);
	dest->x_max = read_signed_bits(&br, n_bits);
	dest->y_min = read_signed_bits(&br, n_bits);
	dest->y_max = read_signed_bits(&br, n_bits);
}

static void parse_swf_matrix(struct buffer *r, struct swf_matrix *dest)
{
	struct bit_reader br = {.buf = r};
	bool has_scale = read_unsigned_bits(&br, 1);
	if (has_scale) {
		uint32_t n_scale_bits = read_unsigned_bits(&br, 5);
		dest->scale_x = read_signed_bits(&br, n_scale_bits);
		dest->scale_y = read_signed_bits(&br, n_scale_bits);
	} else {
		dest->scale_x = 1 << 16;
		dest->scale_y = 1 << 16;
	}
	bool has_rotate = read_unsigned_bits(&br, 1);
	if (has_rotate) {
		uint32_t n_rotate_bits = read_unsigned_bits(&br, 5);
		dest->rotate_skew_0 = read_signed_bits(&br, n_rotate_bits);
		dest->rotate_skew_1 = read_signed_bits(&br, n_rotate_bits);
	} else {
		dest->rotate_skew_0 = 0;
		dest->rotate_skew_1 = 0;
	}
	uint32_t n_translate_bits = read_unsigned_bits(&br, 5);
	dest->translate_x = read_signed_bits(&br, n_translate_bits);
	dest->translate_y = read_signed_bits(&br, n_translate_bits);
}

static void parse_swf_cxform_with_alpha(struct buffer *r, struct swf_cxform_with_alpha *dest)
{
	struct bit_reader br = {.buf = r};
	bool has_add_terms = read_unsigned_bits(&br, 1);
	bool has_mult_terms = read_unsigned_bits(&br, 1);
	uint32_t n_bits =read_unsigned_bits(&br, 4);
	for (int i = 0; i < 4; i++) {
		dest->mult_terms[i] = has_mult_terms ? read_signed_bits(&br, n_bits) : 256;
	}
	for (int i = 0; i < 4; i++) {
		dest->add_terms[i] = has_add_terms ? read_signed_bits(&br, n_bits) : 0;
	}
}

static void parse_swf_fill_style(struct buffer *r, struct swf_fill_style *dst)
{
	uint8_t type = buffer_read_u8(r);
	if (type == 0x41)
		dst->smoothed = true;
	else if (type == 0x43)
		dst->smoothed = false;
	else
		ERROR("unsupported fill style %x", type);

	dst->bitmap_id = buffer_read_u16(r);
	parse_swf_matrix(r, &dst->matrix);
}

static void parse_swf_sound_spec(struct bit_reader *br, struct swf_sound_spec *dst)
{
	const uint16_t rate[] = { 5512, 11025, 22050, 44100 };
	dst->rate = rate[read_unsigned_bits(br, 2)];
	dst->sample_size = read_unsigned_bits(br, 1) ? 16 : 8;
	dst->channels = read_unsigned_bits(br, 1) ? 2 : 1;
}

static struct swf_tag *parse_tag_list(struct buffer *r);

static struct swf_tag *create_simple_tag(int type)
{
	struct swf_tag *tag = xcalloc(1, sizeof(struct swf_tag));
	tag->type = type;
	return tag;
}

static struct swf_tag_define_bits_lossless *parse_define_bits_lossless(struct buffer *r, int type)
{
	assert(type == TAG_DEFINE_BITS_LOSSLESS || type == TAG_DEFINE_BITS_LOSSLESS2);
	struct swf_tag_define_bits_lossless *tag = xcalloc(1, sizeof(struct swf_tag_define_bits_lossless));
	tag->t.type = type;
	tag->character_id = buffer_read_u16(r);
	tag->format = buffer_read_u8(r);
	tag->width = buffer_read_u16(r);
	tag->height = buffer_read_u16(r);

	// Only 32-bit RGB/ARGB images are supported.
	if (tag->format != 5)
		ERROR("DefineBitsLossless: unsupported format %d", tag->format);

	unsigned long len = 4 * tag->width * tag->height;
	uint8_t *pixels = xmalloc(len);
	int rv = uncompress(pixels, &len, r->buf + r->index, buffer_remaining(r));
	if (rv != Z_OK || len != 4 * tag->width * tag->height)
		ERROR("DefineBitsLossless: broken bitmap data");

	uint8_t alpha_const = type == TAG_DEFINE_BITS_LOSSLESS ? 0xff : 0;
	// (A)RGB -> RGBA
	for (unsigned long i = 0; i < len; i += 4) {
		uint8_t alpha = pixels[i] | alpha_const;
		pixels[i + 0] = pixels[i + 1];
		pixels[i + 1] = pixels[i + 2];
		pixels[i + 2] = pixels[i + 3];
		pixels[i + 3] = alpha;
	}
	tag->data = pixels;

	return tag;
}

static struct swf_tag_define_shape *parse_define_shape(struct buffer *r)
{
	struct swf_tag_define_shape *tag = xcalloc(1, sizeof(struct swf_tag_define_shape));
	tag->t.type = TAG_DEFINE_SHAPE;
	tag->shape_id = buffer_read_u16(r);
	parse_swf_rect(r, &tag->bounds);

	uint8_t fill_style_count = buffer_read_u8(r);
	struct swf_fill_style *fill_styles = xcalloc(fill_style_count, sizeof(struct swf_fill_style));
	for (uint8_t i = 0; i < fill_style_count; i++) {
		parse_swf_fill_style(r, &fill_styles[i]);
	}

	uint8_t line_style_count = buffer_read_u8(r);
	if (line_style_count)
		ERROR("DefineShape: line styles are not supported");
	uint8_t fill_bits = buffer_read_u8(r) >> 4;

	// We support shapes consisting of only a single rectangle. That is, one
	// StyleChangeRecord followed by four StraightEdgeRecords. The rectangle
	// should match `bounds`, so we'll just skip coordinate values below.
	struct bit_reader br = {.buf = r};

	// Read a StyleChangeRecord.
	uint8_t style_flags = read_unsigned_bits(&br, 6);
	if (!style_flags || style_flags >> 3)
		ERROR("DefineShape: unexpected shape structure");
	bool state_fill_style1 = (style_flags >> 2) & 1;
	bool state_fill_style0 = (style_flags >> 1) & 1;
	if (state_fill_style0 == state_fill_style1)
		ERROR("DefineShape: unexpected shape structure");
	bool state_move_to = style_flags & 1;
	if (state_move_to) {
		uint8_t move_bits = read_unsigned_bits(&br, 5);
		read_signed_bits(&br, move_bits);  // x
		read_signed_bits(&br, move_bits);  // y
	}
	uint16_t fill_style = read_unsigned_bits(&br, fill_bits);
	if (!fill_style || fill_style > fill_style_count)
		ERROR("DefineShape: invalid fill style");
	tag->fill_style = fill_styles[fill_style - 1];
	free(fill_styles);

	// Read four vertical / horizontal StraightEdgeRecords.
	for (int i = 0; i < 4; i++) {
		uint8_t flags = read_unsigned_bits(&br, 2);
		if (flags != 3)  // straight edge
			ERROR("DefineShape: unexpected shape structure");
		uint8_t num_bits = read_unsigned_bits(&br, 4);
		bool general_line_flag = read_unsigned_bits(&br, 1);
		if (general_line_flag)
			ERROR("DefineShape: unexpected shape structure");
		read_signed_bits(&br, num_bits + 3);  // vert_line_flag and delta
	}
	// Read a EndShapeRecord.
	if (read_unsigned_bits(&br, 6) != 0)
		ERROR("DefineShape: unexpected shape structure");
	return tag;
}

static struct swf_tag_define_sound *parse_define_sound(struct buffer *r)
{
	struct swf_tag_define_sound *tag = xcalloc(1, sizeof(struct swf_tag_define_sound));
	tag->t.type = TAG_DEFINE_SOUND;
	tag->sound_id = buffer_read_u16(r);

	struct bit_reader br = {.buf = r};
	tag->format = read_unsigned_bits(&br, 4);
	parse_swf_sound_spec(&br, &tag->spec);

	tag->sample_count = buffer_read_int32(r);
	size_t len = buffer_remaining(r);
	tag->data = xmalloc(len);
	buffer_read_bytes(r, tag->data, len);
	tag->data_len = len;

	return tag;
}

static struct swf_tag_define_sprite *parse_define_sprite(struct buffer *r)
{
	struct swf_tag_define_sprite *tag = xcalloc(1, sizeof(struct swf_tag_define_sprite));
	tag->t.type = TAG_DEFINE_SPRITE;
	tag->sprite_id = buffer_read_u16(r);
	tag->frame_count = buffer_read_u16(r);
	tag->tags = parse_tag_list(r);
	return tag;
}

static struct swf_tag_place_object *parse_place_object(struct buffer *r, int type)
{
	assert(type == TAG_PLACE_OBJECT2 || type == TAG_PLACE_OBJECT3);
	struct swf_tag_place_object *tag = xcalloc(1, sizeof(struct swf_tag_place_object));
	tag->t.type = type;
	tag->flags = type == TAG_PLACE_OBJECT3 ? buffer_read_u16(r) : buffer_read_u8(r);
	tag->depth = buffer_read_u16(r);

	if (tag->flags & (PLACE_FLAG_RESERVED_BITS | PLACE_FLAG_HAS_CLIP_DEPTH | PLACE_FLAG_HAS_FILTER_LIST | PLACE_FLAG_HAS_CLIP_ACTIONS))
		ERROR("PlaceObject: unsupported flag 0x%x", tag->flags);
	if (tag->flags & PLACE_FLAG_HAS_CHARACTER)
		tag->character_id = buffer_read_u16(r);
	if (tag->flags & PLACE_FLAG_HAS_MATRIX)
		parse_swf_matrix(r, &tag->matrix);
	if (tag->flags & PLACE_FLAG_HAS_COLOR_TRANSFORM)
		parse_swf_cxform_with_alpha(r, &tag->color_transform);
	if (tag->flags & PLACE_FLAG_HAS_RATIO)
		tag->ratio = buffer_read_u16(r);
	if (tag->flags & PLACE_FLAG_HAS_NAME)
		tag->name = strdup(buffer_skip_string(r));
	if (tag->flags & PLACE_FLAG_HAS_BLEND_MODE)
		tag->blend_mode = buffer_read_u8(r);
	if (tag->flags & PLACE_FLAG_HAS_CACHE_AS_BITMAP)
		tag->bitmap_cache = buffer_read_u8(r);

	return tag;
}

static struct swf_tag_remove_object2 *parse_remove_object2(struct buffer *r)
{
	struct swf_tag_remove_object2 *tag = xcalloc(1, sizeof(struct swf_tag_remove_object2));
	tag->t.type = TAG_REMOVE_OBJECT2;
	tag->depth = buffer_read_u16(r);
	return tag;
}

static struct swf_tag_set_background_color *parse_set_background_color(struct buffer *r)
{
	struct swf_tag_set_background_color *tag = xcalloc(1, sizeof(struct swf_tag_set_background_color));
	tag->t.type = TAG_SET_BACKGROUND_COLOR;
	tag->background_color = parse_rgb(r);
	return tag;
}

static struct swf_tag_sound_stream_head *parse_sound_stream_head2(struct buffer *r)
{
	struct swf_tag_sound_stream_head *tag = xcalloc(1, sizeof(struct swf_tag_sound_stream_head));
	tag->t.type = TAG_SOUND_STREAM_HEAD2;

	struct bit_reader br = {.buf = r};
	read_unsigned_bits(&br, 4);  // reserved
	parse_swf_sound_spec(&br, &tag->playback_spec);
	tag->compression = read_unsigned_bits(&br, 4);
	parse_swf_sound_spec(&br, &tag->stream_spec);
	tag->sample_count = buffer_read_u16(r);
	if (tag->compression == 2)
		tag->latency_seek = (int16_t)buffer_read_u16(r);
	return tag;
}

static struct swf_tag_start_sound *parse_start_sound(struct buffer *r)
{
	struct swf_tag_start_sound *tag = xcalloc(1, sizeof(struct swf_tag_start_sound));
	tag->t.type = TAG_START_SOUND;
	tag->sound_id = buffer_read_u16(r);

	tag->flags = buffer_read_u8(r);
	if (tag->flags & (SOUND_INFO_RESERVED_BITS | SOUND_INFO_HAS_ENVELOPE))
		ERROR("StartSound: unsupported flag 0x%x", tag->flags);
	if (tag->flags & SOUND_INFO_HAS_IN_POINT)
		tag->in_point = buffer_read_int32(r);
	if (tag->flags & SOUND_INFO_HAS_OUT_POINT)
		tag->out_point = buffer_read_int32(r);
	if (tag->flags & SOUND_INFO_HAS_LOOPS)
		tag->loop_count = buffer_read_u16(r);

	return tag;
}

static struct swf_tag_do_action *parse_do_action(struct buffer *r);

static struct swf_tag *parse_tag(struct buffer *r)
{
	uint16_t code_and_len = buffer_read_u16(r);
	int type = code_and_len >> 6;
	uint32_t len = code_and_len & 0x3f;
	if (len == 0x3f)
		len = buffer_read_int32(r);

	struct buffer rr;
	buffer_init(&rr, r->buf + r->index, len);
	buffer_skip(r, len);

	switch (type) {
	case TAG_END:
	case TAG_SHOW_FRAME:
	case TAG_FILE_ATTRIBUTES:
		return create_simple_tag(type);
	case TAG_DEFINE_BITS_LOSSLESS:
	case TAG_DEFINE_BITS_LOSSLESS2:
		return &parse_define_bits_lossless(&rr, type)->t;
	case TAG_DEFINE_SHAPE:
		return &parse_define_shape(&rr)->t;
	case TAG_DEFINE_SOUND:
		return &parse_define_sound(&rr)->t;
	case TAG_DEFINE_SPRITE:
		return &parse_define_sprite(&rr)->t;
	case TAG_DO_ACTION:
		return &parse_do_action(&rr)->t;
	case TAG_PLACE_OBJECT2:
	case TAG_PLACE_OBJECT3:
		return &parse_place_object(&rr, type)->t;
	case TAG_REMOVE_OBJECT2:
		return &parse_remove_object2(&rr)->t;
	case TAG_SET_BACKGROUND_COLOR:
		return &parse_set_background_color(&rr)->t;
	case TAG_SOUND_STREAM_HEAD2:
		return &parse_sound_stream_head2(&rr)->t;
	case TAG_START_SOUND:
		return &parse_start_sound(&rr)->t;
	default:
		WARNING("unknown tag %d %d", type, len);
		return create_simple_tag(type);
	}
}

static struct swf_tag *parse_tag_list(struct buffer *r)
{
	struct swf_tag *head;
	struct swf_tag **tag_ptr = &head;
	while (buffer_remaining(r) > 0) {
		struct swf_tag *tag = parse_tag(r);
		*tag_ptr = tag;
		tag_ptr = &tag->next;
	}
	return head;
}

static struct swf_action *create_simple_action(uint8_t code)
{
	struct swf_action *act = xcalloc(1, sizeof(struct swf_action));
	act->code = code;
	return act;
}

static struct swf_action_constant_pool *parse_action_constant_pool(struct buffer *r)
{
	uint16_t count = buffer_read_u16(r);
	struct swf_action_constant_pool *act = xcalloc(1, sizeof(struct swf_action_constant_pool) + count * sizeof(char *));
	act->a.code = ACTION_CONSTANT_POOL;
	act->count = count;
	for (uint16_t i = 0; i < count; i++) {
		act->constant_pool[i] = strdup(buffer_skip_string(r));
	}
	return act;
}

static struct swf_action_goto_frame *parse_action_goto_frame(struct buffer *r)
{
	struct swf_action_goto_frame *act = xcalloc(1, sizeof(struct swf_action_goto_frame));
	act->a.code = ACTION_GOTO_FRAME;
	act->frame = buffer_read_u16(r);
	return act;
}

static struct swf_action_push *parse_action_push(struct buffer *r)
{
	struct swf_action_push *act = xcalloc(1, sizeof(struct swf_action_push));
	act->a.code = ACTION_PUSH;
	act->count = 0;
	while (buffer_remaining(r)) {
		int i = act->count++;
		act = xrealloc(act, sizeof(struct swf_action_push) + act->count * sizeof(act->values[0]));
		uint8_t type = buffer_read_u8(r);
		act->values[i].type = type;
		switch (type) {
		case ACTION_PUSH_INTEGER:
			act->values[i].value = buffer_read_int32(r);
			break;
		case ACTION_PUSH_CONSTANT8:
			act->values[i].value = buffer_read_u8(r);
			break;
		default:
			ERROR("ActionPush: unsupported type %d", type);
		}
	}
	return act;
}

static struct swf_action *parse_action(struct buffer *r)
{
	uint8_t code = buffer_read_u8(r);
	if (code < 0x80)
		return create_simple_action(code);

	uint16_t len = buffer_read_u16(r);
	struct buffer rr;
	buffer_init(&rr, r->buf + r->index, len);
	buffer_skip(r, len);

	switch (code) {
	case ACTION_CONSTANT_POOL:
		return &parse_action_constant_pool(&rr)->a;
	case ACTION_GOTO_FRAME:
		return &parse_action_goto_frame(&rr)->a;
	case ACTION_PUSH:
		return &parse_action_push(&rr)->a;
	default:
		ERROR("unknown action 0x%x", code);
	}
}

static struct swf_tag_do_action *parse_do_action(struct buffer *r)
{
	struct swf_tag_do_action *tag = xcalloc(1, sizeof(struct swf_tag_do_action));
	tag->t.type = TAG_DO_ACTION;

	struct swf_action **act_ptr = &tag->actions;
	while (buffer_remaining(r)) {
		struct swf_action *act = parse_action(r);
		*act_ptr = act;
		act_ptr = &act->next;
	}
	return tag;
}

struct swf *swf_load(uint8_t *data, size_t size)
{
	struct buffer r;
	buffer_init(&r, data, size);
	if (!buffer_check_bytes(&r, "FWS", 3))
		return NULL;
	struct swf *swf = xcalloc(1, sizeof(struct swf));
	swf->version = buffer_read_u8(&r);
	swf->file_length = buffer_read_int32(&r);
	if (swf->file_length != r.size) {
		free(swf);
		return NULL;
	}
	parse_swf_rect(&r, &swf->frame_size);
	swf->frame_rate = buffer_read_u16(&r);
	swf->frame_count = buffer_read_u16(&r);
	swf->tags = parse_tag_list(&r);
	return swf;
}

static const uint8_t aff_mask[] = {
  0xC8, 0xBB, 0x8F, 0xB7, 0xED, 0x43, 0x99, 0x4A,
  0xA2, 0x7E, 0x5B, 0xB0, 0x68, 0x18, 0xF8, 0x88
};

struct swf *aff_load(uint8_t *data, size_t size)
{
	// aff_header:
	//   char magic[4];   // "AFF\0"
	//   uint32 version;  // 1
	//   uint32 size;     // file size
	//   uint32 unknown;  // 0x4d2
	if (size < 16 ||
	    memcmp(data, "AFF", 4) ||
	    LittleEndian_getDW(data, 4) != 1 ||
	    (size_t)LittleEndian_getDW(data, 8) != size ||
	    LittleEndian_getDW(data, 12) != 0x4d2) {
		return NULL;
	}

	// AFF -> SWF
	size -= 16;
	uint8_t *buf = xmalloc(size);
	memcpy(buf, data + 16, size);
	for (int i = 0; i < 0x40; i++) {
		buf[i] ^= aff_mask[i & 0xf];
	}

	// decompress
	if (!memcmp(buf, "CWS", 3)) {
		uint32_t raw_size = LittleEndian_getDW(buf, 4);
		uint8_t *raw = xmalloc(raw_size);
		memcpy(raw, buf, 8);
		raw[0] = 'F';
		unsigned long dest_size = raw_size - 8;
		int rv = uncompress(raw + 8, &dest_size, buf + 8, size - 8);
		free(buf);
		if (rv != Z_OK || dest_size != raw_size - 8) {
			free(raw);
			return NULL;
		}
		buf = raw;
		size = raw_size;
	}

	struct swf *swf = swf_load(buf, size);
	free(buf);
	return swf;
}
