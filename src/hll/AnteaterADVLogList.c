/* Sys43 named-window ADV history, GPL-2.0-or-later. */
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/buffer.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm/page.h"
#include "hll.h"

#define HISTORY_LIMIT 1000
#define HISTORY_SLOTS HISTORY_LIMIT

struct history_voice { struct string *name, *filter; };
struct history_entry {
	struct string *window;
	struct string **lines;
	struct history_voice *voices;
	int nr_lines, nr_voices;
};
struct history {
	struct history_entry entries[HISTORY_SLOTS];
	int first, count;
};
static struct history history;
static bool enabled = true;

static void free_entry(struct history_entry *e)
{
	if (e->window) free_string(e->window);
	for (int i = 0; i < e->nr_lines; i++) free_string(e->lines[i]);
	for (int i = 0; i < e->nr_voices; i++) {
		if (e->voices[i].name) free_string(e->voices[i].name);
		if (e->voices[i].filter) free_string(e->voices[i].filter);
	}
	free(e->lines);
	free(e->voices);
	memset(e, 0, sizeof(*e));
}

static void clear_history(struct history *h)
{
	for (int i = 0; i < HISTORY_SLOTS; i++) free_entry(&h->entries[i]);
	h->first = h->count = 0;
}

static struct history_entry *current(void)
{
	if (!history.count) history.count = 1;
	struct history_entry *e = &history.entries[(history.first + history.count - 1) % HISTORY_SLOTS];
	if (!e->nr_lines) {
		e->lines = xcalloc(1, sizeof(*e->lines));
		e->lines[0] = string_ref(&EMPTY_STRING);
		e->nr_lines = 1;
	}
	return e;
}

static struct history_entry *entry(int index)
{
	current();
	return index >= 0 && index < history.count
		? &history.entries[(history.first + index) % HISTORY_SLOTS] : NULL;
}

static void evict_first(void)
{
	free_entry(&history.entries[history.first]);
	history.first = (history.first + 1) % HISTORY_SLOTS;
	history.count--;
}

static void Clear(void) { clear_history(&history); enabled = true; }
static void Init(void) { Clear(); enabled = true; }
static void SetEnable(bool value) { enabled = value; }
static bool IsEnable(void) { return enabled; }

static int text_count(struct history_entry *e)
{
	return e->nr_lines - (e->nr_lines && !e->lines[e->nr_lines - 1]->size);
}

static int GetNumofADVLog(void)
{
	return history.count - (history.count && !text_count(current()));
}

static void AddNewPage(void)
{
	if (!enabled) return;
	if (text_count(current())) {
		if (history.count == HISTORY_SLOTS) evict_first();
		history.count++;
		current();
	}
}

static void AddNewLine(void)
{
	if (!enabled) return;
	struct history_entry *e = current();
	if (e->nr_lines >= 1000000) VM_ERROR("Too many ADV history lines");
	e->lines = xrealloc(e->lines, (e->nr_lines + 1) * sizeof(*e->lines));
	e->lines[e->nr_lines++] = string_ref(&EMPTY_STRING);
}

static void AddText(struct string *text, struct string *window)
{
	if (!enabled || !text->size) return;
	struct history_entry *e = current();
	if (text_count(e) && e->window && strcmp(e->window->text, window->text)) {
		AddNewPage();
		e = current();
	}
	if (!e->window) e->window = string_dup(window);
	string_append(&e->lines[e->nr_lines - 1], text);
}

static void AddVoice(struct string *name, struct string *filter)
{
	if (!enabled) return;
	struct history_entry *e = current();
	e->voices = xrealloc(e->voices, (e->nr_voices + 1) * sizeof(*e->voices));
	e->voices[e->nr_voices++] = (struct history_voice){string_dup(name), string_dup(filter)};
}

static int GetNumofADVLogText(int index)
{
	struct history_entry *e = entry(index);
	return e ? text_count(e) : 0;
}

static struct string *GetADVLogText(int index, int line)
{
	struct history_entry *e = entry(index);
	return cjk_display_log_line(e && line >= 0 && line < e->nr_lines ? e->lines[line] : &EMPTY_STRING);
}

static int GetNumofADVLogVoice(int index)
{
	struct history_entry *e = entry(index);
	return e ? e->nr_voices : 0;
}

static struct string *GetADVLogVoice(int index, int voice)
{
	struct history_entry *e = entry(index);
	return string_ref(e && voice >= 0 && voice < e->nr_voices ? e->voices[voice].name : &EMPTY_STRING);
}

static struct string *GetADVLogVoiceFilter(int index, int voice)
{
	struct history_entry *e = entry(index);
	return string_ref(e && voice >= 0 && voice < e->nr_voices ? e->voices[voice].filter : &EMPTY_STRING);
}

static bool Save(struct page **out)
{
	struct buffer b;
	buffer_init(&b, NULL, 0);
	buffer_write_bytes(&b, (const uint8_t *)"ADL\0", 4);
	buffer_write_int32(&b, 1);
	current();
	int count = history.count;
	buffer_write_int32(&b, count);
	for (int i = 0; i < count; i++) {
		struct history_entry *e = entry(i);
		buffer_write_int32(&b, e->nr_lines);
		for (int j = 0; j < e->nr_lines; j++) buffer_write_string(&b, e->lines[j]);
		buffer_write_int32(&b, e->nr_voices);
		for (int j = 0; j < e->nr_voices; j++) {
			buffer_write_string(&b, e->voices[j].name);
			buffer_write_string(&b, e->voices[j].filter);
		}
		buffer_write_int8(&b, e->window != NULL);
		if (e->window) buffer_write_string(&b, e->window);
	}
	while (b.index % 4) buffer_write_int8(&b, 0);
	union vm_value dim = { .i = b.index / 4 };
	struct page *page = alloc_array(1, &dim, AIN_ARRAY_INT, 0, false);
	for (int i = 0; i < dim.i; i++) page->values[i].i = LittleEndian_getDW(b.buf, i * 4);
	free(b.buf);
	if (*out) { delete_page_vars(*out); free_page(*out); }
	*out = page;
	return true;
}

static bool read_count(struct buffer *b, int *count)
{
	if (buffer_remaining(b) < 4) return false;
	*count = buffer_read_int32(b);
	return *count >= 0 && *count <= buffer_remaining(b);
}

static struct string *read_string(struct buffer *b)
{
	uint8_t *end = memchr(buffer_data(b), 0, buffer_remaining(b));
	if (!end) return NULL;
	struct string *s = make_string((char *)buffer_data(b), end - buffer_data(b));
	buffer_skip(b, s->size + 1);
	return s;
}

static bool Load(struct page **in)
{
	if (!*in || (*in)->nr_vars < 3 || (*in)->nr_vars > INT_MAX / 4) return false;
	struct buffer b;
	buffer_init(&b, NULL, 0);
	for (int i = 0; i < (*in)->nr_vars; i++) buffer_write_int32(&b, (*in)->values[i].i);
	b.size = b.index;
	b.index = 0;
	struct history *loaded = xcalloc(1, sizeof(*loaded));
	bool valid = false;
	if (!buffer_check_bytes(&b, "ADL\0", 4) || buffer_read_int32(&b) != 1) goto done;
	int count;
	if (!read_count(&b, &count) || count > HISTORY_LIMIT) goto done;
	loaded->count = count;
	for (int i = 0; i < count; i++) {
		struct history_entry *e = &loaded->entries[i];
		int lines, voices;
		if (!read_count(&b, &lines)) goto done;
		e->lines = xcalloc(lines, sizeof(*e->lines));
		for (int j = 0; j < lines; j++) {
			struct string *s = read_string(&b);
			if (!s) goto done;
			e->lines[e->nr_lines++] = s;
		}
		if (!read_count(&b, &voices)) goto done;
		e->voices = xcalloc(voices, sizeof(*e->voices));
		for (int j = 0; j < voices; j++) {
			struct history_voice *v = &e->voices[e->nr_voices++];
			if (!(v->name = read_string(&b)) || !(v->filter = read_string(&b))) goto done;
		}
		if (!buffer_remaining(&b)) goto done;
		int have_window = buffer_read_u8(&b);
		if (have_window > 1 || (have_window && !(e->window = read_string(&b)))) goto done;
	}
	if (buffer_remaining(&b) > 3) goto done;
	Clear();
	history = *loaded;
	memset(loaded, 0, sizeof(*loaded));
	valid = true;
done:
	clear_history(loaded);
	free(loaded);
	free(b.buf);
	return valid;
}

HLL_LIBRARY(AnteaterADVLogList,
	HLL_EXPORT(_ModuleInit, Init), HLL_EXPORT(_ModuleFini, Clear),
	HLL_EXPORT(Clear, Clear), HLL_EXPORT(AddText, AddText),
	HLL_EXPORT(AddNewLine, AddNewLine), HLL_EXPORT(AddNewPage, AddNewPage),
	HLL_EXPORT(AddVoice, AddVoice), HLL_EXPORT(SetEnable, SetEnable), HLL_EXPORT(IsEnable, IsEnable),
	HLL_EXPORT(GetNumofADVLog, GetNumofADVLog), HLL_EXPORT(GetNumofADVLogText, GetNumofADVLogText),
	HLL_EXPORT(GetADVLogText, GetADVLogText), HLL_EXPORT(GetNumofADVLogVoice, GetNumofADVLogVoice),
	HLL_EXPORT(GetADVLogVoice, GetADVLogVoice), HLL_EXPORT(GetADVLogVoiceFilter, GetADVLogVoiceFilter),
	HLL_EXPORT(Save, Save), HLL_EXPORT(Load, Load));
