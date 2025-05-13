/* Copyright (C) 2024 kichikuou <KichikuouChrome@gmail.com>
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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4/file.h"

#include "cJSON.h"
#include "debugger.h"

struct mapping {
	int addr;
	int file;
	int line;
};

struct source_content {
	int nr_lines;
	char *lines[];
};

struct dbg_info {
	char *src_root;
	int nr_sources;
	char **sources;
	struct source_content **source_contents;
	int nr_mappings;
	struct mapping *mappings;
};

struct dbg_info *dbg_info_load(const char *path)
{
	char *json_text = file_read(path, NULL);
	if (!json_text) {
		DBG_ERROR("Cannot load debug information file %s: %s", path, strerror(errno));
		return NULL;
	}

	cJSON *root = cJSON_Parse(json_text);
	free(json_text);

	cJSON *version = cJSON_GetObjectItem(root, "version");
	if (!cJSON_IsString(version) || strcmp(version->valuestring, "alpha-1")) {
		DBG_ERROR("%s: Unsupported debug information format", display_utf0(path));
		cJSON_Delete(root);
		return NULL;
	}

	struct dbg_info *info = xcalloc(1, sizeof(struct dbg_info));
	cJSON *sources = cJSON_GetObjectItem(root, "sources");
	info->nr_sources = cJSON_GetArraySize(sources);
	info->sources = xcalloc(info->nr_sources, sizeof(char *));
	cJSON *source;
	int i = 0;
	cJSON_ArrayForEach(source, sources) {
		info->sources[i++] = xstrdup(source->valuestring);
	}

	cJSON *mappings = cJSON_GetObjectItem(root, "mappings");
	info->nr_mappings = cJSON_GetArraySize(mappings);
	info->mappings = xcalloc(info->nr_mappings, sizeof(struct mapping));

	struct mapping *m = info->mappings;
	cJSON *item;
	cJSON_ArrayForEach(item, mappings) {
		m->addr = cJSON_GetArrayItem(item, 0)->valueint;
		m->file = cJSON_GetArrayItem(item, 1)->valueint;
		m->line = cJSON_GetArrayItem(item, 2)->valueint;
		m++;
	}

	cJSON_Delete(root);

	info->src_root = xstrdup(path_dirname(path));
	info->source_contents = xcalloc(info->nr_sources, sizeof(struct source_content *));
	return info;
}

static struct source_content *load_source_content(const char *path)
{
	char *text = file_read(path, NULL);
	if (!text) {
		DBG_ERROR("Cannot load source file %s", path);
		return NULL;
	}

	int nr_lines = 1; // count the last line even if it doesn't end with '\n'
	for (char *p = text; *p; p++) {
		if (*p == '\n')
			nr_lines++;
	}
	struct source_content *content = xcalloc(1, sizeof(struct source_content) + nr_lines * sizeof(char *));
	content->nr_lines = nr_lines;
	for (int i = 0;; i++) {
		content->lines[i] = text;
		char *next = strchr(text, '\n');
		if (!next)
			break;
		*next = '\0';
		text = next + 1;
	}
	return content;
}

const char *dbg_info_source_name(const struct dbg_info *info, int file)
{
	if (file < 0 || file >= info->nr_sources)
		return NULL;
	return info->sources[file];
}

char *dbg_info_source_path(const struct dbg_info *info, int file)
{
	if (file < 0 || file >= info->nr_sources)
		return NULL;
	return path_join(info->src_root, info->sources[file]);
}

const char *dbg_info_source_line(const struct dbg_info *info, int file, int line)
{
	if (file < 0 || file >= info->nr_sources)
		return NULL;
	if (!info->source_contents[file]) {
		char *path = dbg_info_source_path(info, file);
		info->source_contents[file] = load_source_content(path);
		free(path);
		if (!info->source_contents[file])
			return NULL;
	}
	if (line <= 0 || line > info->source_contents[file]->nr_lines)
		return NULL;
	return info->source_contents[file]->lines[line - 1];
}

int dbg_info_find_file(const struct dbg_info *info, const char *filename)
{
	if (is_absolute_path(filename)) {
		char *target = realpath_utf8(filename);  // Convert '\\' to '/' on Windows
		char *prefix = realpath_utf8(info->src_root);
		for (int i = 0; i < info->nr_sources; i++) {
			char *path = path_join(prefix, info->sources[i]);
			if (!strcasecmp(path, target)) {
				free(path);
				free(prefix);
				free(target);
				return i;
			}
			free(path);
		}
		free(prefix);
		free(target);
	} else {
		for (int i = 0; i < info->nr_sources; i++) {
			if (!strcasecmp(info->sources[i], filename) ||
			    !strcasecmp(path_basename(info->sources[i]), filename)) {
				return i;
			}
		}
	}
	return -1;
}

bool dbg_info_addr2line(const struct dbg_info *info, int addr, int *file, int *line)
{
	// Find the last mapping whose address is less than or equal to `addr`.
	int left = 0, right = info->nr_mappings;
	while (left < right) {
		int mid = (left + right) / 2;
		if (info->mappings[mid].addr <= addr)
			left = mid + 1;
		else
			right = mid;
	}
	if (left == 0) {
		assert(info->nr_mappings == 0 || addr < info->mappings[0].addr);
		return false;
	}

	assert(info->mappings[left - 1].addr <= addr);
	assert(left == info->nr_mappings || addr < info->mappings[left].addr);

	if (file)
		*file = info->mappings[left - 1].file;
	if (line)
		*line = info->mappings[left - 1].line;
	return true;
}

int dbg_info_line2addr(const struct dbg_info *info, int file, int line)
{
	if (file < 0 || file >= info->nr_sources || line <= 0)
		return -1;

	// Find the first mapping whose file is `file` and line is greater than or equal to `line`.
	int left = 0, right = info->nr_mappings;
	while (left < right) {
		int mid = (left + right) / 2;
		if (info->mappings[mid].file < file || (info->mappings[mid].file == file && info->mappings[mid].line < line))
			left = mid + 1;
		else
			right = mid;
	}
	if (left == info->nr_mappings) {
		assert(info->nr_mappings == 0 || info->mappings[info->nr_mappings - 1].file < file ||
				(info->mappings[info->nr_mappings - 1].file == file && info->mappings[info->nr_mappings - 1].line < line));
		return -1;
	}

	assert(left == 0 || info->mappings[left - 1].file < file ||
			(info->mappings[left - 1].file == file && info->mappings[left - 1].line < line));
	if (file < info->mappings[left].file)
		return -1;
	assert(file == info->mappings[left].file && line <= info->mappings[left].line);

	return info->mappings[left].addr;
}
