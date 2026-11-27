/* Native Parts texture suspension, GPL-2.0-or-later. */
#include <limits.h>
#include <stdlib.h>
#include <zlib.h>

#include "system4.h"
#include "system4/hashtable.h"
#include "vm.h"
#include "parts.h"
#include "parts_internal.h"

struct parts_texture_backup {
	Texture *target;
	uint8_t *data;
	uLongf size;
	int width, height;
	GLint min_filter, mag_filter, wrap_s, wrap_t;
	bool opaque;
	struct parts_texture_backup *next;
};

static void suspend_texture(struct parts_state *state, Texture *texture)
{
	if (!gfx_remap_texture(texture) || !texture->handle || texture->w <= 0 || texture->h <= 0) return;
	if ((size_t)texture->w > INT_MAX / 4 / (size_t)texture->h)
		VM_ERROR("Parts texture is too large to suspend");
	struct parts_texture_backup *b = xcalloc(1, sizeof(*b));
	b->target = texture;
	b->width = texture->w;
	b->height = texture->h;
	b->opaque = texture->opaque;
	glBindTexture(GL_TEXTURE_2D, texture->handle);
	glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &b->min_filter);
	glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &b->mag_filter);
	glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &b->wrap_s);
	glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &b->wrap_t);
	uLong raw_size = (uLong)texture->w * texture->h * 4;
	void *pixels = gfx_get_pixels(texture);
	b->size = compressBound(raw_size);
	b->data = xmalloc(b->size);
	if (compress2(b->data, &b->size, pixels, raw_size, Z_BEST_SPEED) != Z_OK)
		VM_ERROR("Could not compress a suspended Parts texture");
	free(pixels);
	b->data = xrealloc(b->data, b->size);
	b->next = state->texture_backups;
	state->texture_backups = b;
	gfx_delete_texture(texture);
}

static void suspend_bitmap(struct ht_slot *slot, void *state)
{
	if (slot->value) suspend_texture(state, slot->value);
}

static void suspend_state(struct parts_state *state)
{
	switch (state->type) {
	case PARTS_CG:
		if (state->cg.cg_asset) {
			parts_cg_suspend(&state->cg);
			break;
		}
		suspend_texture(state, &state->common.texture);
		break;
	case PARTS_NUMERAL:
	case PARTS_CONSTRUCTION_PROCESS:
		suspend_texture(state, &state->common.texture);
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		suspend_texture(state, &state->common.texture);
		suspend_texture(state, &state->gauge.cg);
		break;
	case PARTS_TEXT:
		break;
	case PARTS_ANIMATION:
		for (unsigned i = 0; i < state->anim.nr_frames; i++)
			suspend_texture(state, &state->anim.frames[i]);
		state->common.texture.handle = 0; // Alias of the current frame.
		break;
	case PARTS_FLAT:
		for (size_t i = 0; i < state->flat.nr_libraries; i++) {
			if (state->flat.owns_texture && state->flat.owns_texture[i])
				suspend_texture(state, &state->flat.textures[i]);
		}
		break;
	case PARTS_FLASH:
		if (state->flash.bitmaps) ht_foreach(state->flash.bitmaps, suspend_bitmap, state);
		break;
	default:
		// Layouts own no textures. Movie and 3D textures belong to their
		// respective players and must not be deleted through an alias.
		break;
	}
}

void parts_state_resume_textures(struct parts *parts, struct parts_state *state)
{
	while (state->texture_backups) {
		struct parts_texture_backup *b = state->texture_backups;
		state->texture_backups = b->next;
		if (!b->target->handle) {
			uLongf size = (uLongf)b->width * b->height * 4;
			void *pixels = xmalloc(size);
			if (uncompress(pixels, &size, b->data, b->size) != Z_OK
					|| size != (uLongf)b->width * b->height * 4)
				VM_ERROR("Could not restore a suspended Parts texture");
			gfx_init_texture_with_pixels(b->target, b->width, b->height, pixels);
			b->target->opaque = b->opaque;
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, b->min_filter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, b->mag_filter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, b->wrap_s);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, b->wrap_t);
			free(pixels);
		}
		free(b->data);
		free(b);
	}
	if (state->type == PARTS_CG && !state->cg.cg_asset && !state->common.texture.handle
			&& (state->cg.name || state->cg.no > 0)) {
		parts_cg_resume(parts, &state->cg);
	}
	if (state->type == PARTS_ANIMATION && state->anim.nr_frames)
		state->common.texture = state->anim.frames[state->anim.current_frame];
}

int parts_copy_texture_pixels(struct parts_state *state, Texture *texture, struct parts_texture_pixels *out)
{
	memset(out, 0, sizeof(*out));
	if (!texture)
		return 0;
	gfx_remap_texture(texture);
	if (texture->handle && texture->w > 0 && texture->h > 0) {
		if ((size_t)texture->w > INT_MAX / 4 / (size_t)texture->h)
			return -1;
		out->w = texture->w;
		out->h = texture->h;
		out->opaque = texture->opaque;
		GLint prev_binding = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_binding);
		glBindTexture(GL_TEXTURE_2D, texture->handle);
		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &out->min_filter);
		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &out->mag_filter);
		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &out->wrap_s);
		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &out->wrap_t);
		glBindTexture(GL_TEXTURE_2D, (GLuint)prev_binding);
		out->rgba = gfx_get_pixels(texture);
		return out->rgba ? 1 : -1;
	}
	if (state) {
		for (struct parts_texture_backup *b = state->texture_backups; b; b = b->next) {
			if (b->target == texture && b->width > 0 && b->height > 0) {
				uLongf raw_size = (uLongf)b->width * b->height * 4;
				uint8_t *pixels = malloc(raw_size);
				if (!pixels)
					return -1;
				if (uncompress(pixels, &raw_size, b->data, b->size) != Z_OK ||
				    raw_size != (uLongf)b->width * b->height * 4) {
					free(pixels);
					return -1;
				}
				out->rgba = pixels;
				out->w = b->width;
				out->h = b->height;
				out->min_filter = b->min_filter;
				out->mag_filter = b->mag_filter;
				out->wrap_s = b->wrap_s;
				out->wrap_t = b->wrap_t;
				out->opaque = b->opaque;
				return 1;
			}
		}
	}
	return 0;
}

void parts_state_discard_texture_backups(struct parts_state *state)
{
	// Called before freeing the state arrays that contain the target pointers.
	while (state->texture_backups) {
		struct parts_texture_backup *b = state->texture_backups;
		state->texture_backups = b->next;
		free(b->data);
		free(b);
	}
}

void PE_SuspendTexture(int no)
{
	struct parts *p = parts_try_get(no);
	if (!p) return;
	for (int i = 0; i < PARTS_NR_STATES; i++) suspend_state(&p->states[i]);
	if (p->scrollbar) {
		for (int piece = 0; piece < 4; piece++)
			for (int state = 0; state < 4; state++) suspend_state(&p->scrollbar->skins[piece][state]);
	}
	if (p->widget) {
		suspend_state(&p->widget->disabled);
		suspend_state(&p->widget->checked_disabled);
		for (int i = 0; i < PARTS_NR_STATES; i++) suspend_state(&p->widget->checked_states[i]);
		suspend_state(&p->widget->label);
	}
	if (p->message_box) {
		suspend_state(&p->message_box->label);
		suspend_state(&p->message_box->key_wait);
	}
	p->textures_suspended = true;
	parts_dirty(p);
}

void PE_ResumeTexture(int no)
{
	struct parts *p = parts_try_get(no);
	if (!p) return;
	for (int i = 0; i < PARTS_NR_STATES; i++) parts_state_resume_textures(p, &p->states[i]);
	if (p->scrollbar) {
		for (int piece = 0; piece < 4; piece++)
			for (int state = 0; state < 4; state++) parts_state_resume_textures(p, &p->scrollbar->skins[piece][state]);
	}
	if (p->widget) {
		parts_state_resume_textures(p, &p->widget->disabled);
		parts_state_resume_textures(p, &p->widget->checked_disabled);
		for (int i = 0; i < PARTS_NR_STATES; i++) parts_state_resume_textures(p, &p->widget->checked_states[i]);
		parts_state_resume_textures(p, &p->widget->label);
	}
	if (p->message_box) {
		parts_state_resume_textures(p, &p->message_box->label);
		parts_state_resume_textures(p, &p->message_box->key_wait);
	}
	p->textures_suspended = false;
	parts_dirty(p);
}
