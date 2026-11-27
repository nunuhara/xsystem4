/* Translated dialogue: preserve script order and keep all glyphs in the window. */
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/ain.h"
#include "system4/cg.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "asset_manager.h"
#include "gfx/font.h"
#include "message_layout.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

static bool is_word_char(int c) { return c > 0 && c < 128 && (isalnum(c) || c == '_' || c == '\''); }

static bool is_opening_punct(int c)
{
	return c == '(' || c == '[' || c == '{' || c == 0x2018 || c == 0x201c || c == 0x3008
		|| c == 0x300a || c == 0x300c || c == 0x300e || c == 0x3010 || c == 0xff08;
}

static bool is_closing_punct(int c)
{
	return c == ',' || c == '.' || c == '!' || c == '?' || c == ':' || c == ';' || c == ')'
		|| c == ']' || c == '}' || c == 0x2019 || c == 0x201d || c == 0x3001 || c == 0x3002
		|| c == 0x3009 || c == 0x300b || c == 0x300d || c == 0x300f || c == 0x3011
		|| c == 0xff09 || c == 0xff0c || c == 0xff0e || c == 0xff01 || c == 0xff1f
		|| c == 0xff1a || c == 0xff1b;
}

bool message_layout_reflow(struct message_layout_cell *c, size_t n, int width, int height, int gap)
{
	if (!n) return true;
	if (width <= 0 || height <= 0) return false;
	int x = max(0, c[0].indent), y = 0, row_height = 0;
	for (size_t i = 0; i < n;) {
		if (i && c[i].source_y != c[i - 1].source_y) {
			y += row_height + gap;
			x = max(0, c[i].indent);
			row_height = 0;
		}
		size_t end = i + 1;
		while (end < n && c[end].source_y == c[i].source_y
				&& ((is_word_char(c[end - 1].code) && is_word_char(c[end].code))
					|| is_opening_punct(c[end - 1].code) || is_closing_punct(c[end].code)))
			end++;
		int group_width = 0;
		for (size_t k = i; k < end; k++)
			group_width += c[k].width + (k + 1 < end ? c[k].space : 0);
		if (x > 0 && x + group_width > width) {
			if (row_height) y += row_height + gap;
			x = row_height = 0;
		}
		for (; i < end; i++) {
			int required = c[i].width;
			if (i + 1 < end && is_closing_punct(c[i + 1].code))
				required += c[i].space + c[i + 1].width;
			// Split a word wider than a complete line, always making progress.
			if (x > 0 && x + required > width) {
				if (row_height) y += row_height + gap;
				x = row_height = 0;
			}
			if (c[i].width > width || y + c[i].height > height)
				return false;
			c[i].x = x;
			c[i].y = y;
			row_height = max(row_height, c[i].line_height);
			x += c[i].width + c[i].space;
		}
	}
	return true;
}

static struct {
	bool enabled;
	int view, item, property, window, manager, rect, global;
} schema;

static bool member_is(struct ain *a, int type, int index, const char *name)
{
	return type >= 0 && type < a->nr_structures && index >= 0 && index < a->structures[type].nr_members
		&& !strcmp(a->structures[type].members[index].name, name);
}

bool message_layout_init_rance03(struct ain *a)
{
	memset(&schema, 0, sizeof(schema));
	schema.view = ain_get_struct(a, "CMessageTextView");
	schema.item = ain_get_struct(a, "SDrawChar");
	schema.property = ain_get_struct(a, "CASCharSpriteProperty");
	schema.window = ain_get_struct(a, "CMessageWindowModel");
	schema.manager = ain_get_struct(a, "CMessageWindowModelManager");
	schema.rect = ain_get_struct(a, "CASRect");
	schema.global = ain_get_global(a, "g_MessageWindowModelManager");
	schema.enabled = schema.global >= 0 && member_is(a, schema.view, 0, "m_DrawCharList")
		&& member_is(a, schema.item, 17, "Blue") && member_is(a, schema.item, 9, "Property")
		&& member_is(a, schema.property, 1, "m_nSize") && member_is(a, schema.window, 23, "m_TextAreaRect")
		&& schema.window >= 0 && a->structures[schema.window].nr_members > 41
		&& member_is(a, schema.manager, 1, "m_ModelList") && member_is(a, schema.rect, 3, "m_nHeight");
	return schema.enabled;
}

static struct page *layout_struct(int slot, int type, int members)
{
	if (!page_index_valid(slot)) return NULL;
	struct page *p = heap_get_page(slot);
	return p && p->type == STRUCT_PAGE && p->index == type && p->nr_vars >= members ? p : NULL;
}

static struct page *layout_array(int slot)
{
	if (!page_index_valid(slot)) return NULL;
	struct page *p = heap_get_page(slot);
	return p && p->type == ARRAY_PAGE && p->array.rank == 1 ? p : NULL;
}

struct layout_record {
	struct page *item, *property;
	struct string *text;
	int size, x, y, height, line_height, width;
	struct text_style style;
};

static void fit_window(struct page *items, struct page *window, int window_no)
{
	struct page *r = layout_struct(window->values[23].i, schema.rect, 4);
	if (!r) return;
	int left = max(0, r->values[0].i), top = max(0, r->values[1].i);
	int width = r->values[2].i, height = r->values[3].i;
	struct cg_metrics cg;
	bool frame = string_index_valid(window->values[0].i)
		&& asset_cg_get_metrics_by_name(heap_get_string(window->values[0].i)->text, &cg);
	// Original left-aligned windows often provide only the left/top inset.
	if (width <= 0) width = frame ? cg.w - 2 * left : config.view_width - window->values[2].i - left - 8;
	if (height <= 0) height = frame ? cg.h - top - 8 : config.view_height - window->values[3].i - top - 8;
	width = min(width, config.view_width - window->values[2].i - left - 4);
	height = min(height, config.view_height - window->values[3].i - top - 4);
	if (width <= 0 || height <= 0) return;

	struct layout_record *records = xcalloc(items->nr_vars, sizeof(*records));
	struct message_layout_cell *cells = xcalloc(items->nr_vars, sizeof(*cells));
	size_t count = 0;
	bool fits = true;
	int max_size = 1;
	for (int i = 0; i < items->nr_vars; i++) {
		struct page *p = layout_struct(items->values[i].i, schema.item, 18);
		if (!p || p->values[0].i != 0 || p->values[1].i != window_no) continue;
		struct page *font = layout_struct(p->values[9].i, schema.property, 6);
		if (!font || !string_index_valid(p->values[8].i) || font->values[1].i <= 0) continue;
		struct layout_record *rec = &records[count];
		*rec = (struct layout_record){
			.item = p,
			.property = font,
			.text = heap_get_string(p->values[8].i),
			.size = font->values[1].i,
			.x = p->values[2].i,
			.y = p->values[3].i,
			.width = p->values[4].i,
			.height = p->values[5].i,
			.line_height = p->values[6].i,
			.style = {
				.face = font->values[0].i,
				.size = font->values[1].i,
				.scale_x = 1,
				.space_scale_x = 1,
				.bold_width = font->values[3].f,
				.weight = FW_NORMAL
			}
		};
		text_style_set_edge_width(&rec->style, font->values[4].f);
		int code = 0;
		sjis_char2unicode(rec->text->text, &code);
		cells[count] = (struct message_layout_cell){
			.code = code,
			.source_y = rec->y,
			.width = rec->width,
			.height = rec->size + 2 * ceilf(text_style_edge_width(&rec->style))
				+ max(0, rec->line_height - rec->height),
			.line_height = rec->line_height,
			.space = p->values[7].i
		};
		if (rec->x < left || rec->x + cells[count].width > left + width
				|| rec->y < top || rec->y + cells[count].height > top + height)
			fits = false;
		max_size = max(max_size, rec->size);
		count++;
	}
	if (!count || fits) goto done;

	bool success = false;
	for (int step = 0; step < max_size; step++) {
		for (size_t i = 0; i < count; i++) {
			struct layout_record *rec = &records[i];
			int size = max(1, rec->size - step);
			float ratio = (float)size / rec->size;
			rec->style.size = size;
			rec->style.font_size = NULL;
			cells[i].line_height = max(size, (int)ceilf(rec->line_height * ratio));
			cells[i].height = size + 2 * ceilf(text_style_edge_width(&rec->style))
				+ max(0, cells[i].line_height - (int)ceilf(rec->height * ratio));
			cells[i].width = step ? (int)ceilf(gfx_size_text(&rec->style, rec->text->text)) : rec->width;
			cells[i].indent = (!i || cells[i].source_y != cells[i - 1].source_y) ? max(0, rec->x - left) : 0;
		}
		if (!message_layout_reflow(cells, count, width, height, max(0, window->values[29].i)))
			continue;
		for (size_t i = 0; i < count; i++) {
			struct page *p = records[i].item;
			p->values[2].i = left + cells[i].x;
			p->values[3].i = top + cells[i].y;
			p->values[4].i = cells[i].width;
			// Height is a script baseline metric, not the font's visible height.
			p->values[5].i = (int)ceilf(records[i].height * (float)max(1, records[i].size - step) / records[i].size);
			p->values[6].i = cells[i].line_height;
			records[i].property->values[1].i = max(1, records[i].size - step);
		}
		success = true;
		break;
	}
	if (!success)
		WARNING("Dialogue cannot fit message window %d", window_no);
done:
	free(cells);
	free(records);
}

void message_layout_fit_rance03(int view_slot)
{
	if (!schema.enabled) return;
	struct page *view = layout_struct(view_slot, schema.view, 3);
	struct page *manager = layout_struct(global_get(schema.global).i, schema.manager, 2);
	if (!view || !manager) return;
	struct page *items = layout_array(view->values[0].i);
	struct page *windows = layout_array(manager->values[1].i);
	if (!items || !windows) return;
	for (int i = 0; i < windows->nr_vars; i++) {
		struct page *window = layout_struct(windows->values[i].i, schema.window, 42);
		if (window) fit_window(items, window, i);
	}
}
