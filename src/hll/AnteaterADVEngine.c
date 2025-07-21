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

#include <assert.h>
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "vm/page.h"
#include "xsystem4.h"

#include "AnteaterADVEngine.h"
#include "iarray.h"
#include "hll.h"

/*
 * AnteaterADVEngine
 * -----------------
 * This library was first introduced in Shaman's Sanctuary, however the
 * functions existed as part of StoatSpriteEngine before that.
 */

struct adv_log {
	unsigned nr_lines;
	struct string **lines;
	unsigned nr_voices;
	int *voices;
};

static bool enabled = true;

// A circular buffer of logs.
#define LOG_BUFFER_SIZE 1000
static struct adv_log *logs = NULL;
static unsigned log_first;
static unsigned log_last;

static void AnteaterADVEngine_PreLink(void);

static void init_log(struct adv_log *log)
{
	log->lines = malloc(sizeof(struct string*));
	log->lines[0] = make_string("", 0);
	log->nr_lines = 1;
}

static void free_log(struct adv_log *log)
{
	for (unsigned i = 0; i < log->nr_lines; i++) {
		free_string(log->lines[i]);
	}
	free(log->lines);
	free(log->voices);
	log->nr_lines = 0;
	log->lines = NULL;
	log->nr_voices = 0;
	log->voices = NULL;
}

// Ensure there is at least one log allocated
static void ensure_log(void)
{
	if (!logs) {
		logs = xcalloc(LOG_BUFFER_SIZE, sizeof(struct adv_log));
		init_log(&logs[0]);
		log_first = log_last = 0;
	}
}

static struct adv_log *current_log()
{
	ensure_log();
	return &logs[log_last];
}

static struct string **current_line(struct adv_log *log)
{
	assert(log->nr_lines);
	return &log->lines[log->nr_lines-1];
}

void ADVLogList_Clear(void)
{
	// XXX: this function works even if logging is disabled
	if (!logs) return;
	for (unsigned i = log_first;; i = (i + 1) % LOG_BUFFER_SIZE) {
		free_log(&logs[i]);
		if (i == log_last)
			break;
	}
	free(logs);
	logs = NULL;
}

void ADVLogList_AddText(struct string **text)
{
	if (!enabled) return;
	struct string **line = current_line(current_log());
	string_append(line, *text);
}

void ADVLogList_AddNewLine(void)
{
	if (!enabled) return;
	struct adv_log *log = current_log();
	log->lines = xrealloc_array(log->lines, log->nr_lines, log->nr_lines+1, sizeof(struct string*));
	log->lines[log->nr_lines++] = make_string("", 0);
}

void ADVLogList_AddNewPage(void)
{
	if (!enabled) return;
	log_last = (log_last + 1) % LOG_BUFFER_SIZE;
	if (log_last == log_first) {
		free_log(&logs[log_first]);
		log_first = (log_first + 1) % LOG_BUFFER_SIZE;
	}
	init_log(&logs[log_last]);
}

void ADVLogList_AddVoice(int voice_no)
{
	if (!enabled) return;
	struct adv_log *log = current_log();
	log->voices = xrealloc_array(log->voices, log->nr_voices, log->nr_voices+1, sizeof(int));
	log->voices[log->nr_voices++] = voice_no;
}

void ADVLogList_SetEnable(bool on)
{
	enabled = on;
}

bool ADVLogList_IsEnable(void)
{
	return enabled;
}

int ADVLogList_GetNumofADVLog(void)
{
	if (!logs) return 1;
	return (log_last - log_first + LOG_BUFFER_SIZE) % LOG_BUFFER_SIZE + 1;
}

static struct adv_log *log_entry(int log_no)
{
	// NOTE: AnteaterADVEngine.dll segfaults on invalid index
	ensure_log();
	if (log_no < 0 || (unsigned)log_no >= ADVLogList_GetNumofADVLog())
		VM_ERROR("Invalid log number: %d", log_no);
	return &logs[(log_first + log_no) % LOG_BUFFER_SIZE];
}

int ADVLogList_GetNumofADVLogText(int log_no)
{
	return log_entry(log_no)->nr_lines;
}

void ADVLogList_GetADVLogText(int log_no, int line_no, struct string **text)
{
	struct adv_log *log = log_entry(log_no);
	// NOTE: AnteaterADVEngine.dll segfaults on invalid index
	if (line_no < 0 || (unsigned)line_no >= log->nr_lines)
		VM_ERROR("Invalid line number: %d", line_no);

	*text = string_ref(log->lines[line_no]);
}

int ADVLogList_GetNumofADVLogVoice(int log_no)
{
	return log_entry(log_no)->nr_voices;
}

int ADVLogList_GetADVLogVoice(int log_no, int voice_no)
{
	struct adv_log *log = log_entry(log_no);
	if (voice_no < 0 || (unsigned)voice_no >= log->nr_voices)
		VM_ERROR("Invalid voice number: %d", voice_no);

	return log->voices[voice_no];
}

int ADVLogList_GetADVLogVoiceLast(int log_no)
{
	struct adv_log *log = log_entry(log_no);
	unsigned nr_voices = log->nr_voices;
	if (!nr_voices)
		return 0;
	return log->voices[nr_voices - 1];
}

bool ADVLogList_Save(struct page **iarray)
{
	// 0x64 <- 'A'
	// 0x68 <- 'D'
	// 0x76 <- 'L'
	// 0x0  <- end of header
	// 0x0  <- ???
	// n    <- number of pages (min 1)
	// n    <- number of lines in page (min 1)
	// ...  <- strings (one per line)
	// n    <- number of voices in page
	// ...  <- integers (one per voice)
	// 0x1  <- ???
	ensure_log();

	// encode log as array of integers
	struct iarray_writer b;
	iarray_init_writer(&b, "ADL");
	iarray_write(&b, 0);
	int nr_logs = ADVLogList_GetNumofADVLog();
	iarray_write(&b, nr_logs);
	for (unsigned i = 0; i < nr_logs; i++) {
		struct adv_log *log = &logs[(log_first + i) % LOG_BUFFER_SIZE];
		iarray_write(&b, log->nr_lines);
		for (unsigned i = 0; i < log->nr_lines; i++) {
			iarray_write_string(&b, log->lines[i]);
		}
		iarray_write(&b, log->nr_voices);
		for (unsigned i = 0; i < log->nr_voices; i++) {
			iarray_write(&b, log->voices[i]);
		}
	}
	iarray_write(&b, 1);

	// write integers into caller array variable
	if (*iarray) {
		delete_page_vars(*iarray);
		free_page(*iarray);
	}
	*iarray = iarray_to_page(&b);
	iarray_free_writer(&b);
	return true;
}

bool ADVLogList_Load(struct page **data)
{
	if (!(*data))
		return false;
	struct iarray_reader r;
	iarray_init_reader(&r, *data, "ADL");
	if (iarray_read(&r))
		return false;

	ADVLogList_Clear();

	int nr_pages = iarray_read(&r);
	if (nr_pages < 0 || nr_pages > 10000) {
		WARNING("Invalid value for nr_pages: %d", nr_pages);
		goto error;
	}

	// XXX: For whatever reason this function always creates a blank page 0,
	//      and then loads the input pages after that. So a consecutive save
	//      and load will increase all page numbers by 1.
	ensure_log();

	for (int i = 1; i < nr_pages + 1; i++) {
		ADVLogList_AddNewPage();
		struct adv_log *log = current_log();
		int nr_lines = iarray_read(&r);
		if (nr_lines <= 0 || nr_lines > 10000) {
			WARNING("Invalid value for nr_lines[%d]: %d", i, nr_lines);
			goto error;
		}

		log->lines = xcalloc(nr_lines, sizeof(struct string*));
		for (int i = 0; i < nr_lines; i++) {
			log->lines[i] = iarray_read_string(&r);
		}
		log->nr_lines = nr_lines;

		int nr_voices = iarray_read(&r);
		if (nr_voices < 0 || nr_voices > 10000) {
			WARNING("Invalid value for nr_voices[%d]: %d", i, nr_voices);
			goto error;
		}

		log->voices = xcalloc(nr_voices, sizeof(int));
		for (int i = 0; i < nr_voices; i++) {
			log->voices[i] = iarray_read(&r);
		}
		log->nr_voices = nr_voices;
	}

	if (iarray_read(&r) != 1 || r.error)
		goto error;
	return true;
error:
	ADVLogList_Clear();
	return false;
}

#define SCENE_BUFFER_SIZE 500  // Maximum in Rance 02
static struct page **scenes = NULL;
static unsigned scene_first = 0;
static unsigned scene_last = 0;


static int scene_struct_no(void)
{
	static int no = -1;
	if (no < 0) {
		char *s = utf2sjis("画面保管_t", 0);
		no = ain_get_struct(ain, s);
		free(s);
		if (no < 0)
			VM_ERROR("Missing definition for struct 画面保管_t");
	}
	return no;
}

bool ADVSceneKeeper_AddADVScene(struct page **page)
{
	assert(*page);
	assert((*page)->type == STRUCT_PAGE);
	if ((*page)->index != scene_struct_no()) {
		WARNING("Invalid struct type: %d", (*page)->index);
		return false;
	}

	if (!scenes) {
		scenes = xcalloc(SCENE_BUFFER_SIZE, sizeof(struct page*));
		scene_first = 0;
		scene_last = 0;
	} else {
		scene_last = (scene_last + 1) % SCENE_BUFFER_SIZE;
		if (scene_last == scene_first) {
			free_page(scenes[scene_first]);
			scenes[scene_first] = NULL;
			scene_first = (scene_first + 1) % SCENE_BUFFER_SIZE;
		}
	}

	// serialize
	struct iarray_writer out;
	iarray_init_writer(&out, "SCN");
	iarray_write_struct(&out, *page, false);
	scenes[scene_last] = iarray_to_page(&out);
	iarray_free_writer(&out);

	return true;
}

int ADVSceneKeeper_GetNumofADVScene(void)
{
	if (!scenes)
		return 0;
	return (scene_last - scene_first + SCENE_BUFFER_SIZE) % SCENE_BUFFER_SIZE + 1;
}

bool ADVSceneKeeper_GetADVScene(int index, struct page **page)
{
	if (!scenes || index < 0 || (unsigned)index >= ADVSceneKeeper_GetNumofADVScene())
		return false;

	unsigned i = (scene_first + index) % SCENE_BUFFER_SIZE;

	// deserialize
	struct iarray_reader in;
	if (!iarray_init_reader(&in, scenes[i], "SCN"))
		return false;

	if (*page) {
		delete_page_vars(*page);
		free_page(*page);
	}

	*page = iarray_read_struct(&in, scene_struct_no(), false);
	return !in.error;
}

void ADVSceneKeeper_Clear(void)
{
	if (!scenes)
		return;
	for (unsigned i = scene_first;; i = (i + 1) % SCENE_BUFFER_SIZE) {
		if (scenes[i])
			free_page(scenes[i]);
		if (i == scene_last)
			break;
	}
	free(scenes);
	scenes = NULL;
}

bool ADVSceneKeeper_Save(struct page **iarray)
{
	if (*iarray) {
		delete_page_vars(*iarray);
		free_page(*iarray);
	}
	// NOTE: AnteaterADVEngine.dll knows about the structure of the input;
	//       this implementation just serializes whatever it's given. Note
	//       also that the structure of 画面保管_t objects differs between games.
	struct iarray_writer w;
	iarray_init_writer(&w, "SCS");
	iarray_write(&w, 0); // version
	int nr_scenes = ADVSceneKeeper_GetNumofADVScene();
	iarray_write(&w, nr_scenes);
	for (int i = 0; i < nr_scenes; i++) {
		unsigned idx = (scene_first + i) % SCENE_BUFFER_SIZE;
		iarray_write_array(&w, scenes[idx]);
	}
	*iarray = iarray_to_page(&w);
	iarray_free_writer(&w);
	return true;
}

bool ADVSceneKeeper_Load(struct page **iarray)
{
	if (!(*iarray))
		return false;

	ADVSceneKeeper_Clear();

	struct iarray_reader r;
	iarray_init_reader(&r, *iarray, "SCS");

	if (iarray_read(&r) != 0) {
		WARNING("Unsupported version of ADVSceneKeeper 'SCS' data");
		goto error;
	}

	int nr_scenes = iarray_read(&r);
	if (nr_scenes < 0 || nr_scenes > 10000)
		goto error;

	if (nr_scenes == 0)
		return true;

	int start_offset = 0;
	if (nr_scenes > SCENE_BUFFER_SIZE) {
		start_offset = nr_scenes - SCENE_BUFFER_SIZE;
	}

	struct ain_type type = { .data = AIN_ARRAY_INT, .struc = 0, .rank = 0 };
	for (int i = 0; i < start_offset; i++) {
		struct page *p = iarray_read_array(&r, &type);
		if (r.error)
			goto error;
		free_page(p);
	}

	int scenes_to_load = nr_scenes - start_offset;
	if (scenes_to_load > 0) {
		scenes = xcalloc(SCENE_BUFFER_SIZE, sizeof(struct page*));
		scene_first = 0;
		scene_last = scenes_to_load - 1;

		for (int i = 0; i < scenes_to_load; i++) {
			scenes[i] = iarray_read_array(&r, &type);
			if (r.error)
				goto error;
		}
	}

	return true;
error:
	ADVSceneKeeper_Clear();
	return false;
}

static void AnteaterADVEngine_ModuleFini(void)
{
	ADVSceneKeeper_Clear();
}

static void AnteaterADVEngine_ADVLogList_AddText_with_window(struct string **text, possibly_unused int window_no)
{
	ADVLogList_AddText(text);
}

HLL_LIBRARY(AnteaterADVEngine,
	    HLL_EXPORT(_PreLink, AnteaterADVEngine_PreLink),
	    HLL_EXPORT(_ModuleFini, AnteaterADVEngine_ModuleFini),
	    HLL_EXPORT(ADVLogList_Clear, ADVLogList_Clear),
	    HLL_EXPORT(ADVLogList_AddText, ADVLogList_AddText),
	    HLL_EXPORT(ADVLogList_AddNewLine, ADVLogList_AddNewLine),
	    HLL_EXPORT(ADVLogList_AddNewPage, ADVLogList_AddNewPage),
	    HLL_EXPORT(ADVLogList_AddVoice, ADVLogList_AddVoice),
	    HLL_EXPORT(ADVLogList_SetEnable, ADVLogList_SetEnable),
	    HLL_EXPORT(ADVLogList_IsEnable, ADVLogList_IsEnable),
	    HLL_EXPORT(ADVLogList_GetNumofADVLog, ADVLogList_GetNumofADVLog),
	    HLL_EXPORT(ADVLogList_GetNumofADVLogText, ADVLogList_GetNumofADVLogText),
	    HLL_EXPORT(ADVLogList_GetADVLogText, ADVLogList_GetADVLogText),
	    HLL_EXPORT(ADVLogList_GetNumofADVLogVoice, ADVLogList_GetNumofADVLogVoice),
	    HLL_EXPORT(ADVLogList_GetADVLogVoice, ADVLogList_GetADVLogVoice),
	    HLL_EXPORT(ADVLogList_Save, ADVLogList_Save),
	    HLL_EXPORT(ADVLogList_Load, ADVLogList_Load),
	    HLL_EXPORT(ADVSceneKeeper_AddADVScene, ADVSceneKeeper_AddADVScene),
	    HLL_EXPORT(ADVSceneKeeper_GetNumofADVScene, ADVSceneKeeper_GetNumofADVScene),
	    HLL_EXPORT(ADVSceneKeeper_GetADVScene, ADVSceneKeeper_GetADVScene),
	    HLL_EXPORT(ADVSceneKeeper_Clear, ADVSceneKeeper_Clear),
	    HLL_EXPORT(ADVSceneKeeper_Save, ADVSceneKeeper_Save),
	    HLL_EXPORT(ADVSceneKeeper_Load, ADVSceneKeeper_Load)
	);

static struct ain_hll_function *get_fun(int libno, const char *name)
{
	int fno = ain_get_library_function(ain, libno, name);
	return fno >= 0 ? &ain->libraries[libno].functions[fno] : NULL;
}

static void AnteaterADVEngine_PreLink(void)
{
	struct ain_hll_function *fun;
	int libno = ain_get_library(ain, "AnteaterADVEngine");
	assert(libno >= 0);

	fun = get_fun(libno, "ADVLogList_AddText");
	if (fun && fun->nr_arguments == 2) {
		static_library_replace(&lib_AnteaterADVEngine, "ADVLogList_AddText", AnteaterADVEngine_ADVLogList_AddText_with_window);
	}
}
