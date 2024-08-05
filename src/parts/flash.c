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
#include <stdlib.h>
#include <string.h>
#include <cglm/cglm.h>
#include "system4.h"
#include "system4/archive.h"
#include "system4/hashtable.h"
#include "system4/little_endian.h"
#include "system4/string.h"

#include "asset_manager.h"
#include "audio.h"
#include "swf.h"
#include "xsystem4.h"
#include "parts.h"
#include "parts_internal.h"

#define muldiv(x, y, denom) ((int64_t)(x) * (int64_t)(y) / (int64_t)(denom))

static void clear_display_list(struct parts_flash *f)
{
	while (!TAILQ_EMPTY(&f->display_list)) {
		struct parts_flash_object *obj = TAILQ_FIRST(&f->display_list);
		TAILQ_REMOVE(&f->display_list, obj, entry);
		free(obj);
	}
}

static void free_bitmap(void *value)
{
	gfx_delete_texture(value);
	free(value);
}

void parts_flash_free(struct parts_flash *f)
{
	clear_display_list(f);
	if (f->swf)
		swf_free(f->swf);
	if (f->name)
		free_string(f->name);
	if (f->dictionary)
		ht_free_int(f->dictionary);
	if (f->bitmaps) {
		ht_foreach_value(f->bitmaps, free_bitmap);
		ht_free_int(f->bitmaps);
	}
	if (f->sprites) {
		ht_foreach_value(f->sprites, free);
		ht_free_int(f->sprites);
	}
}

bool parts_flash_load(struct parts *parts, struct parts_flash *f, struct string *filename)
{
	if (f->swf) {
		parts_flash_free(f);
		memset(f, 0, sizeof(struct parts_flash));
	}

	TAILQ_INIT(&f->display_list);
	if (!filename)
		return false;
	struct archive_data *dfile = asset_get_by_name(ASSET_FLASH, filename->text, NULL);
	if (!dfile)
		return false;

	f->swf = aff_load(dfile->data, dfile->size);
	archive_free_data(dfile);
	if (!f->swf)
		return false;

	f->name = string_dup(filename);
	f->tag = f->swf->tags;
	f->stopped = false;
	f->elapsed = 0;
	f->current_frame = 0;
	f->dictionary = ht_create(256);
	f->bitmaps = ht_create(64);
	f->sprites = ht_create(64);

	int width = f->swf->frame_size.x_max / 20;
	int height = f->swf->frame_size.y_max / 20;
	parts_set_dims(parts, &f->common, width, height);
	return true;
}

static void define_bits_lossless(struct parts_flash *f, struct swf_tag_define_bits_lossless *tag)
{
	ht_put_int(f->dictionary, tag->character_id, tag);
	struct ht_slot *slot = ht_put_int(f->bitmaps, tag->character_id, NULL);
	if (!slot->value) {
		struct texture *t = xcalloc(1, sizeof(struct texture));
		gfx_init_texture_with_pixels(t, tag->width, tag->height, tag->data);
		slot->value = t;
	}
}

static void define_shape(struct parts_flash *f, struct swf_tag_define_shape *tag)
{
	ht_put_int(f->dictionary, tag->shape_id, tag);
}

static void define_sound(struct parts_flash *f, struct swf_tag_define_sound *tag)
{
	ht_put_int(f->dictionary, tag->sound_id, tag);
}

static int do_action(struct parts_flash *f, struct swf_tag_do_action *tag)
{
	int seek_frame = -1;
	for (struct swf_action *a = tag->actions; a->code != ACTION_END; a = a->next) {
		switch (a->code) {
		case ACTION_PLAY:
			f->stopped = false;
			break;
		case ACTION_STOP:
			f->stopped = true;
			break;
		case ACTION_GOTO_FRAME:
			seek_frame = ((struct swf_action_goto_frame*)a)->frame;
			break;
		case ACTION_CONSTANT_POOL:
		case ACTION_PUSH:
		case ACTION_POP:
		case ACTION_CALL_FUNCTION:
			break;
		default:
			ERROR("unimplemented action %x", a->code);
			break;
		}
	}
	return seek_frame;
}

static struct parts_flash_object *update_object(struct parts_flash_object *obj, struct swf_tag_place_object *tag)
{
	if (!obj) {
		if (tag->flags & PLACE_FLAG_HAS_CHARACTER) {
			obj = xcalloc(1, sizeof(struct parts_flash_object));
			obj->depth = tag->depth;
		} else {
			ERROR("no object to update (depth=%d)", tag->depth);
		}
	}
	if (tag->flags & PLACE_FLAG_HAS_CHARACTER) {
		obj->character_id = tag->character_id;
		glm_mat4_identity(obj->matrix);
		obj->color_transform = (struct swf_cxform_with_alpha) {
			.mult_terms = { 256, 256, 256, 256 }
		};
	}

	if (tag->flags & PLACE_FLAG_HAS_MATRIX) {
		obj->matrix[0][0] = fixed32_to_float(tag->matrix.scale_x);
		obj->matrix[1][1] = fixed32_to_float(tag->matrix.scale_y);
		obj->matrix[0][1] = fixed32_to_float(tag->matrix.rotate_skew_0);
		obj->matrix[1][0] = fixed32_to_float(tag->matrix.rotate_skew_1);
		obj->matrix[3][0] = twips_to_float(tag->matrix.translate_x);
		obj->matrix[3][1] = twips_to_float(tag->matrix.translate_y);
	}
	if (tag->flags & PLACE_FLAG_HAS_COLOR_TRANSFORM)
		obj->color_transform = tag->color_transform;
	if (tag->flags & PLACE_FLAG_HAS_BLEND_MODE) {
		switch ((obj->blend_mode = tag->blend_mode)) {
		case PARTS_FLASH_BLEND_NORMAL0:
		case PARTS_FLASH_BLEND_NORMAL1:
		case PARTS_FLASH_BLEND_MULTIPLY:
		case PARTS_FLASH_BLEND_SCREEN:
		case PARTS_FLASH_BLEND_ADD:
			break;
		default:
			WARNING("Unsupported blend mode: %d", obj->blend_mode);
		}
	}

	return obj;
}

static void place_object(struct parts_flash *f, struct swf_tag_place_object *tag)
{
	struct parts_flash_object *p;
	TAILQ_FOREACH(p, &f->display_list, entry) {
		if (p->depth == tag->depth) {
			update_object(p, tag);
			return;
		} else if (p->depth > tag->depth) {
			struct parts_flash_object *obj = update_object(NULL, tag);
			TAILQ_INSERT_BEFORE(p, obj, entry);
			return;
		}
	}
	p = update_object(NULL, tag);
	TAILQ_INSERT_TAIL(&f->display_list, p, entry);
}

static void remove_object(struct parts_flash *f, struct swf_tag_remove_object2 *tag)
{
	struct parts_flash_object *p;
	TAILQ_FOREACH(p, &f->display_list, entry) {
		if (p->depth == tag->depth) {
			TAILQ_REMOVE(&f->display_list, p, entry);
			free(p);
			return;
		}
	}
}

static void flash_archive_free_data(struct archive_data *data)
{
	free(data->data);
	free(data);
}

static struct archive_ops flash_archive_ops = {
	.free_data = flash_archive_free_data
};

static struct archive flash_archive = {
	.mmapped = false,
	.ops = &flash_archive_ops
};

static void start_sound(struct parts_flash *f, struct swf_tag_start_sound *tag)
{
	if (tag->flags != 0) {
		ERROR("unsupported StartSound flag 0x%x", tag->flags);
	}
	struct swf_tag_define_sound *sound = ht_get_int(f->dictionary, tag->sound_id, NULL);
	if (!sound || sound->t.type != TAG_DEFINE_SOUND) {
		WARNING("undefined sound id %d", tag->sound_id);
		return;
	}
	if (sound->format != SWF_SOUND_UNCOMPRESSED_LE) {
		ERROR("unsupported sound format %d", sound->format);
	}
	int bytes_per_sample = sound->spec.sample_size / 8;

	const uint8_t wav_header[] = {
		'R' , 'I' , 'F' , 'F' , 0x00, 0x00, 0x00, 0x00,
		'W' , 'A' , 'V' , 'E' , 'f' , 'm' , 't' , ' ' ,
		0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 'd' , 'a' , 't' , 'a' ,
		0x00, 0x00, 0x00, 0x00
	};
	uint32_t wav_size = sizeof(wav_header) + sound->data_len;
	uint8_t *wav_buf = xmalloc(wav_size);
	memcpy(wav_buf, wav_header, sizeof(wav_header));
	LittleEndian_putDW(wav_buf, 4, wav_size - 8);
	LittleEndian_putW(wav_buf, 22, sound->spec.channels);
	LittleEndian_putDW(wav_buf, 24, sound->spec.rate);
	LittleEndian_putDW(wav_buf, 28, sound->spec.rate * bytes_per_sample * sound->spec.channels);
	LittleEndian_putW(wav_buf, 32, bytes_per_sample * sound->spec.channels);
	LittleEndian_putW(wav_buf, 34, sound->spec.sample_size);
	LittleEndian_putDW(wav_buf, 40, sound->data_len);
	memcpy(wav_buf + sizeof(wav_header), sound->data, sound->data_len);

	struct archive_data *dfile = xcalloc(1, sizeof(struct archive_data));
	dfile->size = wav_size;
	dfile->data = wav_buf;
	dfile->archive = &flash_archive;

	audio_play_archive_data(dfile);
}

static void define_sprite(struct parts_flash *f, struct swf_tag_define_sprite *tag)
{
	ht_put_int(f->dictionary, tag->sprite_id, tag);
	struct ht_slot *slot = ht_put_int(f->sprites, tag->sprite_id, NULL);
	if (slot->value)
		return;  // already defined.
	if (tag->frame_count != 1)
		ERROR("sprites with multiple frames are not supported");
	struct parts_flash_object *obj = NULL;
	for (struct swf_tag *t = tag->tags; t && t->type != TAG_END; t = t->next) {
		switch (t->type) {
		case TAG_SOUND_STREAM_HEAD2:
		case TAG_SHOW_FRAME:
			// ignored.
			break;
		case TAG_PLACE_OBJECT2:
			if (obj)
				ERROR("sprites with multiple objects are not supported");;
			obj = update_object(NULL, (struct swf_tag_place_object*)t);
			break;
		default:
			ERROR("unsupported tag %d in sprite", t->type);
		}
	}
	if (!obj)
		ERROR("sprite has no PlaceObject2 tag");
	slot->value = obj;
}

bool parts_flash_seek(struct parts_flash *f, int frame)
{
	if (!f->swf || f->current_frame == frame)
		return false;

	if (f->current_frame > frame) {
		f->current_frame = 0;
		f->tag = f->swf->tags;
		clear_display_list(f);
	}

	int seek_frame;
	struct swf_tag *t;
	for (t = f->tag; t && f->current_frame < frame; t = t->next) {
		switch (t->type) {
		case TAG_FILE_ATTRIBUTES:
		case TAG_SET_BACKGROUND_COLOR:
		case TAG_SOUND_STREAM_HEAD2:
			// Do nothing.
			break;
		case TAG_END:
			f->stopped = true;
			break;
		case TAG_SHOW_FRAME:
			f->current_frame++;
			break;
		case TAG_DEFINE_BITS_LOSSLESS:
		case TAG_DEFINE_BITS_LOSSLESS2:
			define_bits_lossless(f, (struct swf_tag_define_bits_lossless*)t);
			break;
		case TAG_DEFINE_SHAPE:
			define_shape(f, (struct swf_tag_define_shape*)t);
			break;
		case TAG_DEFINE_SPRITE:
			define_sprite(f, (struct swf_tag_define_sprite*)t);
			break;
		case TAG_DEFINE_SOUND:
			define_sound(f, (struct swf_tag_define_sound*)t);
			break;
		case TAG_DO_ACTION:
			if ((seek_frame = do_action(f, (struct swf_tag_do_action*)t)) >= 0)
				// XXX: ensure one frame is rendered when seeking to start
				return parts_flash_seek(f, max(1, seek_frame));
			break;
		case TAG_PLACE_OBJECT2:
		case TAG_PLACE_OBJECT3:
			place_object(f, (struct swf_tag_place_object*)t);
			break;
		case TAG_REMOVE_OBJECT2:
			remove_object(f, (struct swf_tag_remove_object2*)t);
			break;
		case TAG_START_SOUND:
			start_sound(f, (struct swf_tag_start_sound*)t);
			break;
		default:
			ERROR("unknown tag %d", t->type);
		}
	}
	f->tag = t;
	return true;
}

bool parts_flash_update(struct parts_flash *f, int passed_time)
{
	if (!f->swf || f->stopped)
		return false;
	f->elapsed += passed_time;
	int delta_frame = muldiv(f->elapsed, f->swf->frame_rate, 256000);
	if (!delta_frame)
		return false;
	f->elapsed -= muldiv(delta_frame, 256000, f->swf->frame_rate);
	return parts_flash_seek(f, f->current_frame + delta_frame);
}

bool PE_SetPartsFlash(int parts_no, struct string *flash_filename, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flash *f = parts_get_flash(parts, state);
	if (parts_flash_load(parts, f, flash_filename)) {
		parts_flash_seek(f, 1);
		parts_dirty(parts);
		return true;
	}
	return false;
}

bool PE_IsPartsFlashEnd(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flash *f = parts_get_flash(parts, state);
	return f->swf && f->current_frame == f->swf->frame_count;
}

int PE_GetPartsFlashCurrentFrameNumber(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;

	struct parts *parts = parts_get(parts_no);
	struct parts_flash *f = parts_get_flash(parts, state);
	return f->current_frame;
}

bool PE_BackPartsFlashBeginFrame(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flash *f = parts_get_flash(parts, state);
	if (!f->swf)
		return false;
	if (parts_flash_seek(f, 1))
		parts_dirty(parts);
	f->elapsed = 0;
	return true;
}

bool PE_StepPartsFlashFinalFrame(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flash *f = parts_get_flash(parts, state);
	if (!f->swf)
		return false;
	if (parts_flash_seek(f, f->swf->frame_count))
		parts_dirty(parts);
	f->elapsed = 0;
	return true;
}

bool PE_SetPartsFlashAndStop(int parts_no, struct string *flash_filename, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flash *f = parts_get_flash(parts, state);
	bool result = parts_flash_load(parts, f, flash_filename);
	f->stopped = true;
	return result;
}

bool PE_StopPartsFlash(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flash *f = parts_get_flash(parts, state);
	if (!f->swf)
		return false;
	f->stopped = true;
	return true;
}

bool PE_StartPartsFlash(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flash *f = parts_get_flash(parts, state);
	if (!f->swf)
		return false;
	f->stopped = false;
	return true;
}

bool PE_GoFramePartsFlash(int parts_no, int frame_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flash *f = parts_get_flash(parts, state);
	if (!f->swf)
		return false;
	if (parts_flash_seek(f, frame_no))
		parts_dirty(parts);
	f->elapsed = 0;
	return true;
}

int PE_GetPartsFlashEndFrame(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;

	struct parts *parts = parts_get(parts_no);
	struct parts_flash *f = parts_get_flash(parts, state);
	if (!f->swf)
		return 0;
	return f->swf->frame_count;
}
