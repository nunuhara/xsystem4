#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <ctype.h>
#include <getopt.h>
#include <time.h>
#include <math.h>
#include <limits.h>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#if !defined(__SWITCH__)
#define HAVE_POSIX_CRASH_HANDLER
#if defined(__linux__)
#include <ucontext.h>
#endif
#include <dlfcn.h>
#include <unwind.h>
#endif
#endif
#ifdef __ANDROID__
#include <android/log.h>
#endif
#if defined(__ANDROID__) || defined(ALICEHUB_IOS)
#include <pthread.h>
#endif
#if defined(__ANDROID__) || defined(ALICEHUB_IOS) || defined(__SWITCH__)
#include <setjmp.h>
#endif
#ifdef __SWITCH__
#include "launcher.h"
#include "hub/runtime_menu_engines.h"
#endif

#include "system4.h"
#include "system4/ain.h"
#include "system4/file.h"
#include "system4/ini.h"
#include "system4/instructions.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#ifdef ALICEHUB_IOS
#include "xsystem4_main.h"
extern int optreset;
void asset_manager_fini(void);
void audio_end_session(void);
void vm_end_session(void);
#endif
#include "asset_manager.h"
#include "debugger.h"
#include "gfx/gfx.h"
#include "gfx/font.h"
#include "input.h"
#include "mixer.h"
#include "savedata.h"
#include "vm.h"

#include "version.h"

void set_msgskip_delay(struct ain *ain, unsigned ms);
void apply_game_specific_hacks(struct ain *ain);
void parts_set_mixed_display_text(bool enable);

#ifdef __ANDROID__
static pthread_t android_sdl_main_thread;
static sigjmp_buf android_exit_jmp;
static volatile sig_atomic_t android_exit_jmp_active = 0;
static volatile int android_exit_code = 0;

static void android_exit_handler(int code)
{
	if (android_exit_jmp_active && pthread_equal(pthread_self(), android_sdl_main_thread)) {
		android_exit_code = code;
		android_exit_jmp_active = 0;
		siglongjmp(android_exit_jmp, 1);
	}
}

static int android_clean_return(int code)
{
	android_exit_jmp_active = 0;
	sys_exit_handler = NULL;
	savedata_flush_on_background();
	savedir_cleanup_memory_saves();
	mixer_set_app_backgrounded(true);
	gfx_android_fini();
	return code;
}
#endif

#ifdef ALICEHUB_IOS
static _Thread_local jmp_buf *ios_exit_target;
static int ios_exit_code;
static struct ain *ios_program;

static void ios_exit_handler(int code)
{
	if (ios_exit_target) {
		ios_exit_code = code;
		longjmp(*ios_exit_target, 1);
	}
}
#endif

struct config config = {
	.game_name = NULL,
	.boot_name = NULL,
	.ain_filename = NULL,
	.ain_basename_utf8 = NULL,
	.vm_name = NULL,
	.game_dir = NULL,
	.save_dir = NULL,
	.home_dir = NULL,
	.view_width = 800,
	.view_height = 600,
	.view_width_set = false,
	.view_height_set = false,
	.mixer_nr_channels = 0,
	.mixer_channels = NULL,
	.default_volume = 100,
	.joypad = false,
	.echo = false,
	.text_x_scale = 1.0,
	.manual_text_x_scale = false,
	.save_format = SAVE_FORMAT_RSM,
	.msgskip_delay = 0,

	.bgi_path = NULL,
	.wai_path = NULL,
	.ex_path = NULL,
	.fnl_path = NULL,
	.font_paths = { NULL, NULL },
};

#if defined(ALICEHUB_IOS) || defined(__SWITCH__)
static void reset_config(void)
{
	free(config.game_name);
	free(config.boot_name);
	free(config.ain_filename);
	free(config.ain_basename_utf8);
	free(config.vm_name);
	free(config.game_dir);
	free(config.save_dir);
	free(config.home_dir);
	free(config.bgi_path);
	free(config.wai_path);
	free(config.ex_path);
	free(config.fnl_path);
	for (size_t i = 0; i < 2; i++)
		free(config.font_paths[i]);
	for (size_t i = 0; i < config.mixer_nr_channels; i++)
		free(config.mixer_channels[i]);
	free(config.mixer_channels);
	free(config.mixer_volumes);
	config = (struct config){
		.view_width = 800,
		.view_height = 600,
		.default_volume = 100,
		.text_x_scale = 1.0,
		.save_format = SAVE_FORMAT_RSM,
	};
}
#endif

static struct string *ini_string(struct ini_entry *entry)
{
	if (entry->value.type != INI_STRING)
		ERROR("ini value for '%s' is not a string", entry->name->text);
	return entry->value.s;
}

static int ini_integer(struct ini_entry *entry)
{
	if (entry->value.type != INI_INTEGER)
		ERROR("ini value for '%s' is not an integer", entry->name->text);
	return entry->value.i;
}

static int ini_boolean(struct ini_entry *entry)
{
	if (entry->value.type != INI_BOOLEAN)
		ERROR("ini value for '%s' is not a boolean", entry->name->text);
	return entry->value.i;
}

static void read_mixer_channels(struct ini_entry *entry)
{
	if (entry->value.type != INI_LIST)
		ERROR("ini value for 'VolumeValancer' is not a list");

	for (size_t i = 0; i < config.mixer_nr_channels; i++)
		free(config.mixer_channels[i]);
	free(config.mixer_channels);
	free(config.mixer_volumes);

	config.mixer_nr_channels = entry->value.list_size;
	config.mixer_channels = xcalloc(entry->value.list_size, sizeof(char*));
	config.mixer_volumes = xcalloc(entry->value.list_size, sizeof(int));
	for (size_t i = 0; i < entry->value.list_size; i++) {
		if (entry->value.list[i].type == INI_NULL) {
			config.mixer_channels[i] = xstrdup("Unnamed");
			config.mixer_volumes[i] = 0;
		} else if (entry->value.list[i].type == INI_STRING) {
			config.mixer_channels[i] = xstrdup(entry->value.list[i].s->text);
			// XXX: assumes default_volume was already initialized
			config.mixer_volumes[i] = config.default_volume;
		} else if (entry->value.list[i].type == INI_LIST) {
			if (entry->value.list[i].list_size != 2)
				ERROR("ini value for 'VolumeValancer' is list of wrong size");
			if (entry->value.list[i].list[0].type != INI_STRING)
				ERROR("ini value for 'VolumeValancer' name is not a string");
			if (entry->value.list[i].list[1].type != INI_INTEGER)
				ERROR("ini value for 'VolumeValancer' level is not an integer");
			config.mixer_channels[i] = xstrdup(entry->value.list[i].list[0].s->text);
			config.mixer_volumes[i] = entry->value.list[i].list[1].i;
		}
	}
}

static bool file_text_is_utf8(const char *path)
{
	size_t len;
	uint8_t *data = file_read(path, &len);
	bool utf8 = data && sys4_bytes_look_utf8(data, len);
	free(data);
	return utf8;
}

// e.g. the BokiBoki release of Tsumamigui 3 (and its Simplified repack) stores all text as UTF-8.
static bool ain_text_is_utf8(const char *path)
{
	int err;
	bool prev = sys4_source_is_utf8;
	sys4_set_source_utf8(false);
	struct ain *raw = ain_open(path, &err);
	sys4_set_source_utf8(prev);
	if (!raw)
		return false;
	unsigned long valid = 0;
	unsigned long invalid = 0;
	if (raw->nr_messages > 0 && raw->messages) {
		int step = raw->nr_messages > 8192 ? raw->nr_messages / 8192 : 1;
		for (int i = 0; i < raw->nr_messages; i += step) {
			struct string *m = raw->messages[i];
			if (!m || !m->size)
				continue;
			bool ascii = true;
			for (int j = 0; j < m->size && ascii; j++)
				ascii = (uint8_t)m->text[j] < 0x80;
			if (ascii)
				continue;
			if (sys4_bytes_look_utf8((const uint8_t *)m->text, m->size))
				valid++;
			else
				invalid++;
		}
	}
	if (!valid && !invalid && raw->nr_strings > 0 && raw->strings) {
		int step = raw->nr_strings > 8192 ? raw->nr_strings / 8192 : 1;
		for (int i = 0; i < raw->nr_strings; i += step) {
			struct string *s = raw->strings[i];
			if (!s || !s->size)
				continue;
			bool ascii = true;
			for (int j = 0; j < s->size && ascii; j++)
				ascii = (uint8_t)s->text[j] < 0x80;
			if (ascii)
				continue;
			if (sys4_bytes_look_utf8((const uint8_t *)s->text, s->size))
				valid++;
			else
				invalid++;
		}
	}
	ain_free(raw);
	return valid > 0 && valid >= invalid * 9;
}

static bool ain_content_is_utf8(const struct ain *raw)
{
	if (!raw)
		return false;
	unsigned long valid = 0;
	unsigned long invalid = 0;
	if (raw->nr_messages > 0 && raw->messages) {
		int step = raw->nr_messages > 8192 ? raw->nr_messages / 8192 : 1;
		for (int i = 0; i < raw->nr_messages; i += step) {
			struct string *m = raw->messages[i];
			if (!m || !m->size)
				continue;
			bool ascii = true;
			for (int j = 0; j < m->size && ascii; j++)
				ascii = (uint8_t)m->text[j] < 0x80;
			if (ascii)
				continue;
			if (sys4_bytes_look_utf8((const uint8_t *)m->text, m->size))
				valid++;
			else
				invalid++;
		}
	}
	if (!valid && !invalid && raw->nr_strings > 0 && raw->strings) {
		int step = raw->nr_strings > 8192 ? raw->nr_strings / 8192 : 1;
		for (int i = 0; i < raw->nr_strings; i += step) {
			struct string *s = raw->strings[i];
			if (!s || !s->size)
				continue;
			bool ascii = true;
			for (int j = 0; j < s->size && ascii; j++)
				ascii = (uint8_t)s->text[j] < 0x80;
			if (ascii)
				continue;
			if (sys4_bytes_look_utf8((const uint8_t *)s->text, s->size))
				valid++;
			else
				invalid++;
		}
	}
	return valid > 0 && valid >= invalid * 9;
}

static void set_utf8_release(bool enabled)
{
	sys4_set_source_utf8(enabled);
	sys4_set_text_gbk(enabled);
}

static bool read_config_entries(const char *path);

static bool read_config(const char *path)
{
	set_utf8_release(file_text_is_utf8(path));
	return read_config_entries(path);
}

static bool read_config_entries(const char *path)
{
	int ini_size;
	struct ini_entry *ini = ini_parse(path, &ini_size);
	if (!ini)
		return false;

	for (int i = 0; i < ini_size; i++) {
		if (!strcmp(ini[i].name->text, "GameName")) {
			char *value = xstrdup(ini_string(&ini[i])->text);
			free(config.game_name);
			config.game_name = value;
		} else if (!strcmp(ini[i].name->text, "BootName")) {
			char *value = xstrdup(ini_string(&ini[i])->text);
			free(config.boot_name);
			config.boot_name = value;
		} else if (!strcmp(ini[i].name->text, "CodeName")) {
			char *value = xstrdup(ini_string(&ini[i])->text);
			free(config.ain_filename);
			config.ain_filename = value;
		} else if (!strcmp(ini[i].name->text, "MainVM")) {
			char *value = xstrdup(ini_string(&ini[i])->text);
			free(config.vm_name);
			config.vm_name = value;
		} else if (!strcmp(ini[i].name->text, "SaveFolder")) {
			char *value = xstrdup(ini_string(&ini[i])->text);
			free(config.save_dir);
			config.save_dir = value;
		} else if (!strcmp(ini[i].name->text, "ViewWidth")) {
			config.view_width = ini_integer(&ini[i]);
			config.view_width_set = true;
		} else if (!strcmp(ini[i].name->text, "ViewHeight")) {
			config.view_height = ini_integer(&ini[i]);
			config.view_height_set = true;
		} else if (!strcmp(ini[i].name->text, "VolumeValancer")) {
			read_mixer_channels(&ini[i]);
		} else if (!strcmp(ini[i].name->text, "DefaultVolumeRate")) {
			config.default_volume = ini_integer(&ini[i]);
		} else if (!strcmp(ini[i].name->text, "UseJoypad")) {
			config.joypad = ini_boolean(&ini[i]);
		}
		ini_free_entry(&ini[i]);
	}
	free(ini);
	return true;
}

static char *user_config_path(struct ini_entry *entry, const char *basedir)
{
	const char *path = ini_string(entry)->text;
	if (basedir && !is_absolute_path(path)) {
		return path_join(basedir, path);
	}
	return xstrdup(path);
}

static bool set_encoding(const char *name)
{
	if (!strcasecmp(name, "sjis")) {
		sys4_set_text_gbk(false);
	} else if (!strcasecmp(name, "gbk")) {
		sys4_set_text_gbk(true);
	} else {
		return false;
	}
	return true;
}

static void read_user_config_file(const char *path, const char *basedir)
{
	int ini_size;
	struct ini_entry *ini = ini_parse(path, &ini_size);
	if (!ini)
		return;

	for (int i = 0; i < ini_size; i++) {
		if (!strcmp(ini[i].name->text, "font-mincho")) {
			char *value = user_config_path(&ini[i], basedir);
			free(config.font_paths[FONT_MINCHO]);
			config.font_paths[FONT_MINCHO] = value;
		} else if (!strcmp(ini[i].name->text, "font-gothic")) {
			char *value = user_config_path(&ini[i], basedir);
			free(config.font_paths[FONT_GOTHIC]);
			config.font_paths[FONT_GOTHIC] = value;
		} else if (!strcmp(ini[i].name->text, "font-fnl")) {
			char *value = user_config_path(&ini[i], basedir);
			free(config.fnl_path);
			config.fnl_path = value;
		} else if (!strcmp(ini[i].name->text, "font-x-scale")) {
			float f = strtof(ini_string(&ini[i])->text, NULL);
			if (fabsf(f) < 0.01) {
				WARNING("Invalid value for font-x-scale in config: \"%s\"",
						ini_string(&ini[i])->text);
			} else {
				config.manual_text_x_scale = true;
				config.text_x_scale = f;
			}
		} else if (!strcmp(ini[i].name->text, "font-proportional")) {
			config.proportional_text = ini_boolean(&ini[i]);
		} else if (!strcmp(ini[i].name->text, "encoding")) {
			if (!set_encoding(ini_string(&ini[i])->text))
				WARNING("Invalid encoding: %s", ini_string(&ini[i])->text);
		} else if (!strcmp(ini[i].name->text, "msgskip-delay")) {
			config.msgskip_delay = ini_integer(&ini[i]);
			if (config.msgskip_delay < 0) {
				WARNING("Invalid value for msgskip-delay in config: %d",
						config.msgskip_delay);
				config.msgskip_delay = 0;
			}
		} else if (!strcmp(ini[i].name->text, "save-folder")) {
			char *value = user_config_path(&ini[i], basedir);
			free(config.save_dir);
			config.save_dir = value;
		} else if (!strcmp(ini[i].name->text, "save-format")) {
			if (!strcmp(ini_string(&ini[i])->text, "json")) {
				config.save_format = SAVE_FORMAT_JSON;
			} else if (!strcmp(ini_string(&ini[i])->text, "rsm")) {
				config.save_format = SAVE_FORMAT_RSM;
			} else {
				WARNING("Invalid value for save-format in config: \"%s\"",
						ini_string(&ini[i])->text);
			}
		}
		ini_free_entry(&ini[i]);
	}
	free(ini);
}

static void read_user_config(void)
{
	char *path;
	bool source_utf8 = sys4_source_is_utf8;
	sys4_set_source_utf8(false);

	// global config
	path = xmalloc(PATH_MAX);
	snprintf(path, PATH_MAX, "%s/.xsys4rc", config.home_dir);
	read_user_config_file(path, NULL);
	free(path);

	// game-specific config
	path = gamedir_path(".xsys4rc");
	read_user_config_file(path, config.game_dir);
	free(path);
	sys4_set_source_utf8(source_utf8);
}

static char *get_xsystem4_home(void)
{
	// $XSYSTEM4_HOME
	char *env_home = getenv("XSYSTEM4_HOME");
	if (env_home && *env_home) {
		return xstrdup(env_home);
	}

	// $XDG_DATA_HOME/xsystem4
	env_home = getenv("XDG_DATA_HOME");
	if (env_home && *env_home) {
		char *home = xmalloc(strlen(env_home) + strlen("/xsystem4") + 1);
		strcpy(home, env_home);
		strcat(home, "/xsystem4");
		return home;
	}

	// $HOME/.xsystem4
	env_home = getenv("HOME");
	if (env_home && *env_home) {
		char *home = xmalloc(strlen(env_home) + strlen("/.xsystem4") + 1);
		strcpy(home, env_home);
		strcat(home, "/.xsystem4");
		return home;
	}

	// If all else fails, use the current directory
	return realpath_utf8(".");
}

static char *get_save_path(const char *dir_name, const char *utf8_game_name_override)
{
	if (!dir_name)
		dir_name = "SaveData";

	char *utf8_game_name = (utf8_game_name_override && *utf8_game_name_override)
		? xstrdup(utf8_game_name_override)
		: sjis2utf(config.game_name, strlen(config.game_name));
	char *utf8_dir_name = sjis2utf(dir_name, strlen(dir_name));
	char *save_dir = xmalloc(strlen(config.home_dir) + 1 + strlen(utf8_game_name) + 1 + strlen(utf8_dir_name) + 1);
	strcpy(save_dir, config.home_dir);
	strcat(save_dir, "/");
	strcat(save_dir, utf8_game_name);
	strcat(save_dir, "/");
	strcat(save_dir, utf8_dir_name);
	free(utf8_game_name);
	free(utf8_dir_name);
	return save_dir;
}

static void config_init_with_utf8_codename(const char *utf8_code_name)
{
	bool omitted_game_name = !config.game_name;
	config.home_dir = get_xsystem4_home();
	if (omitted_game_name)
		config.game_name = xstrdup(config.ain_filename);
	if ((config.game_name && (strstr(config.game_name, "\xc0\xbc\xcb\xb9")
				|| strstr(config.game_name, "\xb6\xe0\xc4\xc8")
				|| strstr(config.game_name, "\xcf\xc4\xcd\xde")
				|| strstr(config.game_name, "\xd6\xd0\xce\xc4")
				|| strstr(config.game_name, "\xba\xba\xbb\xaf")
				|| strstr(config.game_name, "\x9e\x61\xbb\xaf")
				|| strstr(config.game_name, "\xb3\xac\xb0\xba\xc9\xf1\xc6\xef")
				|| strstr(config.game_name, "\xc6\xde\xa4\xb7\xa4\xdc\xa4\xea")
				|| strstr(config.game_name, "\xb4\xf3\xb7\xac\xe9L")
				|| strstr(config.game_name, "\xb4\xf3\xb7\xac\xb3\xa4")
				|| strstr(config.game_name, "\xb4\xf3\xb5\xdb\xb9\xfa")
				|| strstr(config.game_name, "\xd0\xa3\xc4\xda\xb3\xe0\xcf\xdf")
				|| strstr(config.game_name, "\xcc\xab\xcc\xab")
				|| strstr(config.game_name, "\xd2\xec\xbf\xd5\xbf\xdf")
				|| strstr(config.game_name, "\xd0\xb0\xd2\xb9")
				|| strstr(config.game_name, "\xb6\xb7\xc9\xf1")
				|| strstr(config.game_name, "\xb7\xdb\xb2\xca\xc1\xe5\xd2\xf4")
				|| strstr(config.game_name, "\xa4\xcf\xa1\xab\xa4\xc8\xa4\xd5\xa4\xeb\xa5\xde\xa5\xde\xa5\xf3")
				|| strstr(config.game_name, "\xbf\xa8\xc0\xad\xb1\xa3\xbb\xa4\xd0\xad\xbb\xe1")))
			|| (config.ain_filename && strstr(config.ain_filename, "\xc6\xde\xa4\xb7\xa4\xdc\xa4\xea"))
			|| (config.ain_filename && !strcasecmp(config.ain_filename, "Rance10.ain")
				&& (!config.game_name || !strstr(config.game_name, "\x83\x89\x83\x93\x83\x58")))
			|| (config.ain_filename && !strcasecmp(config.ain_filename, "dohnadohna.ain")
				&& (!config.game_name || !strstr(config.game_name, "\x83\x68\x81\x5b\x83\x69")))
			|| (config.ain_filename && !strcasecmp(config.ain_filename, "Evenicle.ain")
				&& (!config.game_name || !strstr(config.game_name, "\x83\x43\x83\x75\x83\x6a\x83\x4e\x83\x8b")))) {
		sys4_set_text_gbk(true);
	}

	char *legacy_save_dir = get_save_path(config.save_dir, NULL);
	char *new_save_dir = legacy_save_dir;
	if (omitted_game_name && utf8_code_name && *utf8_code_name &&
	    !strchr(utf8_code_name, '?') && config.ain_filename &&
	    strchr(config.ain_filename, '?') && !is_directory(legacy_save_dir)) {
		char *candidate = NULL;
#ifndef _WIN32
		if (strchr(utf8_code_name, '\\')) {
			char *raw_utf8_save_dir = get_save_path(config.save_dir, utf8_code_name);
			if (is_directory(raw_utf8_save_dir))
				candidate = raw_utf8_save_dir;
			else
				free(raw_utf8_save_dir);
		}
#endif
		if (!candidate) {
			char *normalized = xstrdup(utf8_code_name);
			for (char *p = normalized; *p; p++) {
				if (*p == '\\')
					*p = '/';
			}
			candidate = get_save_path(config.save_dir, normalized);
			free(normalized);
		}
		new_save_dir = candidate;
		free(legacy_save_dir);
	}
	free(config.save_dir);
	config.save_dir = new_save_dir;
}

static void config_init(void)
{
	config_init_with_utf8_codename(NULL);
}

static char *resolved_ain_path = NULL;

static char *resolve_configured_ain_path(const char *native_ain_path)
{
	if (native_ain_path && file_exists(native_ain_path))
		return xstrdup(native_ain_path);
	if (resolved_ain_path && file_exists(resolved_ain_path))
		return xstrdup(resolved_ain_path);
	char *ainfile = gamedir_path_icase(config.ain_filename);
	if (!ainfile) ainfile = gamedir_path(config.ain_filename);
	if (!file_exists(ainfile)) {
		// Handle GBK or Shift-JIS CodeName when GameName in INI uses the opposite encoding.
		bool prev = sys4_text_is_gbk;
		sys4_set_text_gbk(!prev);
		char *alt_ain = gamedir_path(config.ain_filename);
		sys4_set_text_gbk(prev);
		if (file_exists(alt_ain)) {
			free(ainfile);
			ainfile = alt_ain;
		} else {
			free(alt_ain);
		}
	}
	return ainfile;
}

static bool config_init_with_ini(const char *ini_path)
{
	free(resolved_ain_path);
	resolved_ain_path = NULL;
	free(config.ain_basename_utf8);
	config.ain_basename_utf8 = NULL;
	if (!read_config(ini_path))
		return false;
	if (!config.ain_filename)
		ERROR("No AIN filename specified in %s", ini_path);
	char *tmp = xstrdup(ini_path);
	config.game_dir = xstrdup(path_dirname(tmp));
	free(tmp);
	char *utf8_code_name = NULL;
	if (sys4_source_is_utf8) {
		// The script decides; a UTF-8 AliceStart.ini may have been edited by hand for a
		// Shift-JIS or GBK game. Preserve the resolved on-disk script path and configured
		// UTF-8 CodeName before re-reading the INI in legacy mode so non-Shift-JIS UTF-8
		// filenames (e.g. GBK-only chars) are not mangled into '?' when opening the script,
		// resolving sidecar assets, or creating default save folders for untitled INIs.
		utf8_code_name = sjis2utf(config.ain_filename, strlen(config.ain_filename));
		char *ain_path = gamedir_path_icase(config.ain_filename);
		if (!ain_path)
			ain_path = gamedir_path(config.ain_filename);
		if (ain_path && file_exists(ain_path)) {
			resolved_ain_path = xstrdup(ain_path);
			free(config.ain_basename_utf8);
			config.ain_basename_utf8 = xstrdup(path_basename(ain_path));
		}
		bool ain_utf8 = ain_path ? ain_text_is_utf8(ain_path) : false;
		free(ain_path);
		if (!ain_utf8) {
			sys4_set_text_gbk(false);
			read_config_entries(ini_path);
			sys4_set_source_utf8(false);
		}
	}
	config_init_with_utf8_codename(utf8_code_name);
	free(utf8_code_name);
	return true;
}

static bool config_init_with_dir(const char *dir)
{
	// Prefer Longinus CN launcher config (AliceStart.CHS + CHS.TBL, e.g. Oyakorankan).
	char *chs_ini = path_join(dir, "AliceStart.CHS");
	if (!file_exists(chs_ini)) {
		char *icase = path_get_icase(chs_ini);
		free(chs_ini);
		chs_ini = icase;
	}
	if (chs_ini && file_exists(chs_ini)) {
		char *chs_tbl = path_join(dir, "CHS.TBL");
		if (!file_exists(chs_tbl)) {
			char *icase = path_get_icase(chs_tbl);
			free(chs_tbl);
			chs_tbl = icase;
		}
		bool has_chs_tbl = chs_tbl && file_exists(chs_tbl);
		free(chs_tbl);
		if (has_chs_tbl) {
			bool ok = config_init_with_ini(chs_ini);
			free(chs_ini);
			return ok;
		}
	}
	free(chs_ini);

	char *alt_ini = path_join(dir, "AliceStart2.ini");
	if (!file_exists(alt_ini)) {
		char *icase = path_get_icase(alt_ini);
		free(alt_ini);
		alt_ini = icase;
	}
	if (alt_ini && file_exists(alt_ini)) {
		char *haru_ain = path_join(dir, "haru_c.ain");
		if (!file_exists(haru_ain)) {
			char *icase = path_get_icase(haru_ain);
			free(haru_ain);
			haru_ain = icase;
		}
		bool has_haru_cn = haru_ain && file_exists(haru_ain);
		free(haru_ain);
		if (has_haru_cn) {
			bool ok = config_init_with_ini(alt_ini);
			free(alt_ini);
			return ok;
		}
	}
	free(alt_ini);

	static const char *const ini_names[] = { "System40.ini", "AliceStart.ini", "AliceStart2.ini", "AliceStart.CHS" };
	for (size_t i = 0; i < sizeof(ini_names) / sizeof(ini_names[0]); i++) {
		char *path = path_join(dir, ini_names[i]);
		if (!file_exists(path)) {
			char *icase = path_get_icase(path);
			free(path);
			path = icase;
		}
		if (path && file_exists(path)) {
			bool ok = config_init_with_ini(path);
			free(path);
			return ok;
		}
		free(path);
	}
	return false;
}

static void config_init_with_ain(const char *ain_path)
{
	free(resolved_ain_path);
	resolved_ain_path = NULL;
	set_utf8_release(ain_text_is_utf8(ain_path));
	config.ain_filename = xstrdup(path_basename(ain_path));
	free(config.ain_basename_utf8);
	config.ain_basename_utf8 = xstrdup(path_basename(ain_path));
	config.game_dir = xstrdup(path_dirname(ain_path));
	config_init();
}

static void ain_audit(FILE *f, struct ain *ain)
{
	link_libraries_for_audit();

	for (size_t addr = 0; addr < ain->code_size;) {
		uint16_t opcode = LittleEndian_getW(ain->code, addr);
		const struct instruction *instr = &instructions[opcode];
		if (opcode >= NR_OPCODES) {
			ERROR("0x%08zx: Invalid/unknown opcode: %x", opcode);
		}
		if (!instr->implemented) {
			fprintf(f, "0x%08zx: %s (unimplemented instruction)\n", addr, instr->name);
		}
		if (opcode == CALLSYS) {
			uint32_t syscode = LittleEndian_getDW(ain->code, addr + 2);
			if (syscode >= NR_SYSCALLS) {
				ERROR("0x%08zx: CALLSYS system.(0x%x)\n", addr, syscode);
			}
			const char * const name = syscalls[syscode].name;
			if (!name) {
				fprintf(f, "0x%08zx: CALLSYS system.(0x%x)\n", addr, syscode);
			} else if (!syscalls[syscode].implemented) {
				fprintf(f, "0x%08zx: CALLSYS %s (unimplemented system call)\n", addr, name);
			}

		}
		if (opcode == CALLHLL) {
			uint32_t lib = LittleEndian_getDW(ain->code, addr+2);
			uint32_t fun = LittleEndian_getDW(ain->code, addr+6);
			if (!library_exists(lib)) {
				fprintf(f, "0x%08zx: CALLHLL %s.%s (unimplemented library)\n", addr,
					ain->libraries[lib].name, ain->libraries[lib].functions[fun].name);
			} else if (!library_function_exists(lib, fun)) {
				fprintf(f, "0x%08zx: CALLHLL %s.%s (unimplemented function)\n", addr,
					ain->libraries[lib].name, ain->libraries[lib].functions[fun].name);
			}
		}
		// TODO: audit library calls
		addr += instruction_width(opcode);
	}
	fflush(f);
}

static void usage(void)
{
	puts("Usage: xsystem4 [options] [inifile-or-directory]");
	puts("    -h, --help           Display this message and exit");
	puts("    -v, --version        Display the version and exit");
	puts("    -a, --audit          Audit AIN file for xsystem4 compatibility");
	puts("    -e, --echo-message   Echo in-game messages to standard output");
	puts("        --font-mincho    Specify the path to the mincho font to use");
	puts("        --font-gothic    Specify the path to the gothic font to use");
	puts("        --font-fnl       Specify the path to a .fnl font library to use");
	puts("        --font-x-scale   Specify the x scale for text rendering (1.0 = default scale)");
	puts("    -j, --joypad         Enable joypad");
	puts("        --encoding       Select sjis or gbk text encoding");
	puts("        --font-proportional  Use font glyph advances");
	puts("        --msgskip-delay  Specify the delay in ms to add when skipping messages with CTRL");
	puts("        --save-folder    Override save folder location");
	puts("        --save-format    Specify the resume save file format. rsm (default) or json");
#ifdef DEBUGGER_ENABLED
	puts("        --nodebug        Disable debugger");
	puts("        --debug          Start in debugger");
	puts("        --debug-info     Specify the path to the debug information file");
#endif
}

static _Noreturn void _usage_error(const char *fmt, ...)
{
	usage();
	puts("");

	va_list ap;
	va_start(ap, fmt);
	sys_verror(fmt, ap);
}
#define usage_error(fmt, ...) _usage_error("Error: " fmt "\n", ##__VA_ARGS__)

enum {
	LOPT_HELP = 256,
	LOPT_VERSION,
	LOPT_AUDIT,
	LOPT_ECHO_MESSAGE,
	LOPT_FONT_MINCHO,
	LOPT_FONT_GOTHIC,
	LOPT_FONT_FNL,
	LOPT_FONT_X_SCALE,
	LOPT_JOYPAD,
	LOPT_FONT_PROPORTIONAL,
	LOPT_ENCODING,
	LOPT_MSGSKIP_DELAY,
	LOPT_SAVE_FOLDER,
	LOPT_SAVE_FORMAT,
#ifdef DEBUGGER_ENABLED
	LOPT_NODEBUG,
	LOPT_DEBUG,
	LOPT_DEBUG_API,
	LOPT_DEBUG_INFO,
#endif
};

#define CRASH_RING_SIZE 32
#define CRASH_LINE_MAX 256
static char recent_warnings[CRASH_RING_SIZE][CRASH_LINE_MAX];
static unsigned int recent_warning_count = 0;
static volatile sig_atomic_t in_crash_handler = 0;
static char crash_report_buf[32768];

static void warning_handler(const char *msg)
{
	if (!msg || !*msg || in_crash_handler)
		return;
	char *dst = recent_warnings[recent_warning_count % CRASH_RING_SIZE];
	size_t i = 0;
	while (msg[i] && i + 1 < CRASH_LINE_MAX) {
		if (msg[i] == '\r' || msg[i] == '\n')
			break;
		dst[i] = msg[i];
		i++;
	}
	dst[i] = 0;
	if (i > 0)
		recent_warning_count++;
}

#ifdef HAVE_POSIX_CRASH_HANDLER
struct backtrace_state {
	char *buf;
	size_t size;
	size_t off;
	int depth;
};

static _Unwind_Reason_Code unwind_backtrace_cb(struct _Unwind_Context *ctx, void *arg)
{
	struct backtrace_state *st = (struct backtrace_state *)arg;
	uintptr_t pc = _Unwind_GetIP(ctx);
	if (!pc)
		return _URC_NO_REASON;
	if (st->depth >= 32 || st->off + 1 >= st->size)
		return _URC_END_OF_STACK;

	Dl_info info;
	int n;
	if (dladdr((void *)pc, &info) && info.dli_fname) {
		const char *sname = info.dli_sname ? info.dli_sname : "?";
		uintptr_t sym_off = info.dli_saddr ? (pc - (uintptr_t)info.dli_saddr) : 0;
		uintptr_t base_off = info.dli_fbase ? (pc - (uintptr_t)info.dli_fbase) : 0;
		n = snprintf(st->buf + st->off, st->size - st->off,
			"    #%02d 0x%016llx %s(%s+0x%llx) [base+0x%llx]\n",
			st->depth, (unsigned long long)pc, info.dli_fname, sname,
			(unsigned long long)sym_off, (unsigned long long)base_off);
	} else {
		n = snprintf(st->buf + st->off, st->size - st->off,
			"    #%02d 0x%016llx <unknown>\n",
			st->depth, (unsigned long long)pc);
	}
	if (n > 0)
		st->off += (size_t)n < (st->size - st->off) ? (size_t)n : (st->size - st->off - 1);
	st->depth++;
	return _URC_NO_REASON;
}

static size_t format_native_backtrace(char *buf, size_t size)
{
	struct backtrace_state st = { .buf = buf, .size = size, .off = 0, .depth = 0 };
	_Unwind_Backtrace(unwind_backtrace_cb, &st);
	return st.off;
}
#endif

#ifndef _WIN32
static bool write_file_raw(const char *path, const char *data, size_t len, bool append)
{
	if (!path || !*path)
		return false;
	int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
	int fd = open(path, flags, 0644);
	if (fd < 0)
		return false;
	size_t written = 0;
	while (written < len) {
		ssize_t r = write(fd, data + written, len - written);
		if (r <= 0)
			break;
		written += (size_t)r;
	}
	fsync(fd);
	close(fd);
	return written > 0;
}
#endif

#ifdef HAVE_POSIX_CRASH_HANDLER
static const char *signal_name_str(int sig)
{
	switch (sig) {
	case SIGSEGV: return "SIGSEGV (Segmentation fault)";
	case SIGABRT: return "SIGABRT (Abort)";
	case SIGBUS:  return "SIGBUS (Bus error)";
	case SIGFPE:  return "SIGFPE (Floating-point exception)";
	case SIGILL:  return "SIGILL (Illegal instruction)";
	default:      return "UNKNOWN SIGNAL";
	}
}
#endif

#ifndef _WIN32
static const char *cached_env_crash_log = NULL;
#endif

static void cbuf_puts(char *buf, size_t cap, size_t *off, const char *s)
{
	if (!s || *off + 1 >= cap)
		return;
	while (*s && *off + 1 < cap)
		buf[(*off)++] = *s++;
	buf[*off] = '\0';
}

static void cbuf_put_hex(char *buf, size_t cap, size_t *off, uint64_t val, int width)
{
	static const char hex[] = "0123456789abcdef";
	char tmp[16];
	int i = 0;
	do {
		tmp[i++] = hex[val & 0xf];
		val >>= 4;
	} while (val && i < 16);
	while (i < width && i < 16)
		tmp[i++] = '0';
	while (i > 0 && *off + 1 < cap)
		buf[(*off)++] = tmp[--i];
	if (cap > 0)
		buf[*off] = '\0';
}

static void cbuf_put_i64(char *buf, size_t cap, size_t *off, int64_t val)
{
	char tmp[24];
	uint64_t u;
	if (val < 0) {
		cbuf_puts(buf, cap, off, "-");
		u = (uint64_t)(-(val + 1)) + 1;
	} else {
		u = (uint64_t)val;
	}
	int i = 0;
	do {
		tmp[i++] = (char)('0' + (u % 10));
		u /= 10;
	} while (u && i < 24);
	while (i > 0 && *off + 1 < cap)
		buf[(*off)++] = tmp[--i];
	if (cap > 0)
		buf[*off] = '\0';
}

static void build_dir_file_path(char *dst, size_t cap, const char *dir, const char *file)
{
	size_t off = 0;
	if (cap == 0)
		return;
	dst[0] = '\0';
	cbuf_puts(dst, cap, &off, dir);
	if (off > 0 && dst[off - 1] != '/')
		cbuf_puts(dst, cap, &off, "/");
	cbuf_puts(dst, cap, &off, file);
}

static void write_crash_report(const char *kind, const char *msg, int sig,
	void *siginfo_ptr, void *ucontext_ptr, bool safe_signal,
	char *out_primary_path, size_t out_primary_size)
{
	if (in_crash_handler)
		return;
	in_crash_handler = 1;
	if (out_primary_path && out_primary_size > 0)
		out_primary_path[0] = 0;

	size_t off = 0;
	size_t cap = sizeof(crash_report_buf);
	crash_report_buf[0] = '\0';

	cbuf_puts(crash_report_buf, cap, &off,
		"============================================================\n"
		"AliceHub Crash Report\n"
		"============================================================\n"
		"Timestamp   : ");
#ifndef _WIN32
	if (!safe_signal) {
		time_t now = time(NULL);
		struct tm tm_buf;
		char time_str[64];
		if (localtime_r(&now, &tm_buf) &&
		    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf) > 0) {
			cbuf_puts(crash_report_buf, cap, &off, time_str);
		} else {
			cbuf_puts(crash_report_buf, cap, &off, "epoch:");
			cbuf_put_i64(crash_report_buf, cap, &off, (int64_t)now);
		}
	} else {
		struct timespec ts = {0, 0};
		clock_gettime(CLOCK_REALTIME, &ts);
		cbuf_puts(crash_report_buf, cap, &off, "epoch:");
		cbuf_put_i64(crash_report_buf, cap, &off, (int64_t)ts.tv_sec);
	}
#else
	cbuf_puts(crash_report_buf, cap, &off, "unknown");
#endif
	cbuf_puts(crash_report_buf, cap, &off, "\nEngine      : xsystem4 " XSYSTEM4_VERSION "\n");
#ifdef __ANDROID__
	cbuf_puts(crash_report_buf, cap, &off, "Platform    : Android (");
#else
	cbuf_puts(crash_report_buf, cap, &off, "Platform    : Host (");
#endif
#if defined(__aarch64__)
	cbuf_puts(crash_report_buf, cap, &off, "arm64-v8a / aarch64)\n");
#elif defined(__x86_64__)
	cbuf_puts(crash_report_buf, cap, &off, "x86_64)\n");
#else
	cbuf_puts(crash_report_buf, cap, &off, "unknown-arch)\n");
#endif
#ifndef _WIN32
	cbuf_puts(crash_report_buf, cap, &off, "PID         : ");
	cbuf_put_i64(crash_report_buf, cap, &off, (int64_t)getpid());
	cbuf_puts(crash_report_buf, cap, &off, "\n");
#endif
	cbuf_puts(crash_report_buf, cap, &off, "Crash Type  : ");
	cbuf_puts(crash_report_buf, cap, &off, kind ? kind : "UNKNOWN");
	cbuf_puts(crash_report_buf, cap, &off, "\n");

#ifdef HAVE_POSIX_CRASH_HANDLER
	if (sig != 0) {
		siginfo_t *si = (siginfo_t *)siginfo_ptr;
		cbuf_puts(crash_report_buf, cap, &off, "Signal      : ");
		cbuf_put_i64(crash_report_buf, cap, &off, sig);
		cbuf_puts(crash_report_buf, cap, &off, " - ");
		cbuf_puts(crash_report_buf, cap, &off, signal_name_str(sig));
		cbuf_puts(crash_report_buf, cap, &off, "\nSignal Code : ");
		cbuf_put_i64(crash_report_buf, cap, &off, si ? si->si_code : 0);
		cbuf_puts(crash_report_buf, cap, &off, "\nFault Addr  : 0x");
		cbuf_put_hex(crash_report_buf, cap, &off, si ? (uint64_t)(uintptr_t)si->si_addr : 0, 16);
		cbuf_puts(crash_report_buf, cap, &off, "\n");

#if defined(__linux__)
		ucontext_t *uc = (ucontext_t *)ucontext_ptr;
		if (uc) {
#if defined(__aarch64__)
			cbuf_puts(crash_report_buf, cap, &off, "Registers   : PC=0x");
			cbuf_put_hex(crash_report_buf, cap, &off, (uint64_t)uc->uc_mcontext.pc, 16);
			cbuf_puts(crash_report_buf, cap, &off, " LR=0x");
			cbuf_put_hex(crash_report_buf, cap, &off, (uint64_t)uc->uc_mcontext.regs[30], 16);
			cbuf_puts(crash_report_buf, cap, &off, " SP=0x");
			cbuf_put_hex(crash_report_buf, cap, &off, (uint64_t)uc->uc_mcontext.sp, 16);
			cbuf_puts(crash_report_buf, cap, &off, " FP=0x");
			cbuf_put_hex(crash_report_buf, cap, &off, (uint64_t)uc->uc_mcontext.regs[29], 16);
			cbuf_puts(crash_report_buf, cap, &off, "\n");
#elif defined(__x86_64__)
			cbuf_puts(crash_report_buf, cap, &off, "Registers   : RIP=0x");
			cbuf_put_hex(crash_report_buf, cap, &off, (uint64_t)uc->uc_mcontext.gregs[REG_RIP], 16);
			cbuf_puts(crash_report_buf, cap, &off, " RSP=0x");
			cbuf_put_hex(crash_report_buf, cap, &off, (uint64_t)uc->uc_mcontext.gregs[REG_RSP], 16);
			cbuf_puts(crash_report_buf, cap, &off, " RBP=0x");
			cbuf_put_hex(crash_report_buf, cap, &off, (uint64_t)uc->uc_mcontext.gregs[REG_RBP], 16);
			cbuf_puts(crash_report_buf, cap, &off, "\n");
#endif
		}
#endif
	}
#endif

	if (msg && *msg) {
		cbuf_puts(crash_report_buf, cap, &off, "Message     : ");
		cbuf_puts(crash_report_buf, cap, &off, msg);
		if (off > 0 && crash_report_buf[off - 1] != '\n')
			cbuf_puts(crash_report_buf, cap, &off, "\n");
	}

	cbuf_puts(crash_report_buf, cap, &off, "\n[Game Context]\n  ain_file  : ");
	cbuf_puts(crash_report_buf, cap, &off, config.ain_filename ? config.ain_filename : "(unset)");
	cbuf_puts(crash_report_buf, cap, &off, "\n  game_dir  : ");
	cbuf_puts(crash_report_buf, cap, &off, config.game_dir ? config.game_dir : "(unset)");
	cbuf_puts(crash_report_buf, cap, &off, "\n  save_dir  : ");
	cbuf_puts(crash_report_buf, cap, &off, config.save_dir ? config.save_dir : "(unset)");
	cbuf_puts(crash_report_buf, cap, &off, "\n  viewport  : ");
	cbuf_put_i64(crash_report_buf, cap, &off, config.view_width);
	cbuf_puts(crash_report_buf, cap, &off, "x");
	cbuf_put_i64(crash_report_buf, cap, &off, config.view_height);
	cbuf_puts(crash_report_buf, cap, &off, "\n\n[AIN VM State & Call Stack]\n");

	if (off + 1 < cap)
		off += vm_format_stack_trace(crash_report_buf + off, cap - off, safe_signal);
	if (off >= cap)
		off = cap - 1;

#ifdef HAVE_POSIX_CRASH_HANDLER
	if (!safe_signal && off + 1 < cap) {
		cbuf_puts(crash_report_buf, cap, &off, "\n[Native C Backtrace]\n");
		if (off + 1 < cap)
			off += format_native_backtrace(crash_report_buf + off, cap - off);
		if (off >= cap)
			off = cap - 1;
	}
#endif

	cbuf_puts(crash_report_buf, cap, &off, "\n[Recent Engine Warnings (");
	cbuf_put_i64(crash_report_buf, cap, &off, recent_warning_count);
	cbuf_puts(crash_report_buf, cap, &off, " total)]\n");
	unsigned int count = recent_warning_count < CRASH_RING_SIZE ? recent_warning_count : CRASH_RING_SIZE;
	unsigned int start = recent_warning_count >= CRASH_RING_SIZE ? (recent_warning_count - CRASH_RING_SIZE) : 0;
	for (unsigned int i = 0; i < count && off + 1 < cap; i++) {
		const char *w = recent_warnings[(start + i) % CRASH_RING_SIZE];
		cbuf_puts(crash_report_buf, cap, &off, "  - ");
		cbuf_puts(crash_report_buf, cap, &off, w);
		cbuf_puts(crash_report_buf, cap, &off, "\n");
	}

	cbuf_puts(crash_report_buf, cap, &off,
		"============================================================\n\n");

#ifndef _WIN32
	char path_buf[PATH_MAX];
	const char *env_crash_log = cached_env_crash_log;
	if (!safe_signal && (!env_crash_log || !*env_crash_log)) {
		env_crash_log = getenv("XSYSTEM4_CRASH_LOG_PATH");
		if (!env_crash_log || !*env_crash_log)
			env_crash_log = getenv("XSYSTEM4_CRASH_LOG");
	}
	if (env_crash_log && *env_crash_log) {
		if (write_file_raw(env_crash_log, crash_report_buf, off, false)) {
			if (out_primary_path && out_primary_size > 0 && !out_primary_path[0]) {
				size_t p_off = 0;
				cbuf_puts(out_primary_path, out_primary_size, &p_off, env_crash_log);
			}
		}
	}
#ifdef __ANDROID__
	{
		const char *ext_crash = "/storage/emulated/0/Android/data/io.github.alicehub/files/crash.log";
		const char *ext_hist  = "/storage/emulated/0/Android/data/io.github.alicehub/files/crash_history.log";
		if (write_file_raw(ext_crash, crash_report_buf, off, false)) {
			if (out_primary_path && out_primary_size > 0 && !out_primary_path[0]) {
				size_t p_off = 0;
				cbuf_puts(out_primary_path, out_primary_size, &p_off, ext_crash);
			}
		}
		write_file_raw(ext_hist, crash_report_buf, off, true);
	}
	if (!safe_signal)
		__android_log_write(ANDROID_LOG_FATAL, "XSystem4Crash", crash_report_buf);
#endif
	if (config.game_dir && *config.game_dir) {
		build_dir_file_path(path_buf, sizeof(path_buf), config.game_dir, "crash.log");
		if (write_file_raw(path_buf, crash_report_buf, off, false)) {
			if (out_primary_path && out_primary_size > 0 && !out_primary_path[0]) {
				size_t p_off = 0;
				cbuf_puts(out_primary_path, out_primary_size, &p_off, path_buf);
			}
		}
		build_dir_file_path(path_buf, sizeof(path_buf), config.game_dir, "crash_history.log");
		write_file_raw(path_buf, crash_report_buf, off, true);
	}
	if (config.save_dir && *config.save_dir) {
		build_dir_file_path(path_buf, sizeof(path_buf), config.save_dir, "crash.log");
		if (write_file_raw(path_buf, crash_report_buf, off, false)) {
			if (out_primary_path && out_primary_size > 0 && !out_primary_path[0]) {
				size_t p_off = 0;
				cbuf_puts(out_primary_path, out_primary_size, &p_off, path_buf);
			}
		}
		build_dir_file_path(path_buf, sizeof(path_buf), config.save_dir, "crash_history.log");
		write_file_raw(path_buf, crash_report_buf, off, true);
	}
	ssize_t unused_r = write(STDERR_FILENO, crash_report_buf, off);
	(void)unused_r;
#endif
	in_crash_handler = 0;
}

#ifdef HAVE_POSIX_CRASH_HANDLER
static uint8_t crash_alt_stack_mem[65536];
static const int crash_signals[] = { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL };
#define NR_CRASH_SIGNALS (sizeof(crash_signals) / sizeof(crash_signals[0]))
static struct sigaction old_sigactions[NR_CRASH_SIGNALS];

static void crash_signal_handler(int sig, siginfo_t *info, void *ucontext)
{
	char saved_path[PATH_MAX];
	write_crash_report("FATAL SIGNAL", NULL, sig, info, ucontext, true, saved_path, sizeof(saved_path));

	for (size_t i = 0; i < NR_CRASH_SIGNALS; i++) {
		if (crash_signals[i] != sig)
			continue;
		const struct sigaction *prev = &old_sigactions[i];
		sigaction(sig, prev, NULL);
		if ((prev->sa_flags & SA_SIGINFO) && prev->sa_sigaction &&
		    prev->sa_sigaction != crash_signal_handler) {
			prev->sa_sigaction(sig, info, ucontext);
			return;
		}
		if (!(prev->sa_flags & SA_SIGINFO) &&
		    prev->sa_handler != SIG_DFL && prev->sa_handler != SIG_IGN &&
		    prev->sa_handler != NULL) {
			prev->sa_handler(sig);
			return;
		}
		break;
	}
	signal(sig, SIG_DFL);
	raise(sig);
	_exit(128 + sig);
}

static void install_crash_signal_handlers(void)
{
	cached_env_crash_log = getenv("XSYSTEM4_CRASH_LOG_PATH");
	if (!cached_env_crash_log || !*cached_env_crash_log)
		cached_env_crash_log = getenv("XSYSTEM4_CRASH_LOG");

	stack_t ss;
	ss.ss_sp = crash_alt_stack_mem;
	ss.ss_size = sizeof(crash_alt_stack_mem);
	ss.ss_flags = 0;
	sigaltstack(&ss, NULL);

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = crash_signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;

	for (size_t i = 0; i < NR_CRASH_SIGNALS; i++) {
		sigaction(crash_signals[i], &sa, &old_sigactions[i]);
	}
}
#endif

static void error_handler(const char *msg)
{
	char saved_path[PATH_MAX];
	saved_path[0] = 0;
	write_crash_report("FATAL ERROR", msg, 0, NULL, NULL, false, saved_path, sizeof(saved_path));

	char dialog_msg[4096];
	size_t off = 0;
	int n = snprintf(dialog_msg + off, sizeof(dialog_msg) - off, "%s\n", msg ? msg : "Fatal error");
	if (n > 0)
		off += (size_t)n < (sizeof(dialog_msg) - off) ? (size_t)n : (sizeof(dialog_msg) - off - 1);
	if (saved_path[0]) {
		n = snprintf(dialog_msg + off, sizeof(dialog_msg) - off,
			"\nCrash log saved to:\n%s\n\n", saved_path);
		if (n > 0)
			off += (size_t)n < (sizeof(dialog_msg) - off) ? (size_t)n : (sizeof(dialog_msg) - off - 1);
	}
	vm_format_stack_trace(dialog_msg + off, sizeof(dialog_msg) - off, false);
	const char *no_box = getenv("XSYSTEM4_NO_ERROR_BOX");
	bool show_box = !no_box || !*no_box || !strcmp(no_box, "0");
#if !defined(__ANDROID__) && !defined(_WIN32) && !defined(ALICEHUB_IOS)
	if (show_box) {
		const char *disp = getenv("DISPLAY");
		const char *wayland = getenv("WAYLAND_DISPLAY");
		const char *ci = getenv("CI");
		if (!SDL_WasInit(SDL_INIT_VIDEO) ||
		    ((!disp || !*disp) && (!wayland || !*wayland)) ||
		    (ci && *ci && strcmp(ci, "0") != 0)) {
			show_box = false;
		}
	}
#endif
	if (show_box) {
#ifndef _WIN32
		signal(SIGTERM, SIG_DFL);
		signal(SIGINT, SIG_DFL);
#endif
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "xsystem4", dialog_msg, NULL);
	}
}

#ifdef __ANDROID__
static bool android_sdl_main_thread_ready = false;

bool sys_is_sdl_main_thread(void)
{
	return android_sdl_main_thread_ready &&
	       pthread_equal(pthread_self(), android_sdl_main_thread);
}
#else
bool sys_is_sdl_main_thread(void)
{
	return false;
}
#endif

static void init_text_dict(void)
{
	sys4_reset_text_dict();
	if (!config.game_dir)
		return;
	const char *base_src = (config.ain_basename_utf8 && *config.ain_basename_utf8)
		? config.ain_basename_utf8
		: config.ain_filename;
	if (!base_src || !*base_src)
		return;
	const char *slash = strrchr(base_src, '/');
	const char *bslash = strrchr(base_src, '\\');
	const char *name = slash > bslash ? slash + 1 : (bslash ? bslash + 1 : base_src);
	const char *dot = strrchr(name, '.');
	size_t stem_len = dot ? (size_t)(dot - name) : strlen(name);
	if (!stem_len)
		return;
	char *dict_name = xmalloc(stem_len + sizeof("Dict.dat"));
	memcpy(dict_name, name, stem_len);
	memcpy(dict_name + stem_len, "Dict.dat", sizeof("Dict.dat"));
	char *path = gamedir_path_icase(dict_name);
	free(dict_name);
	if (path && file_exists(path)) {
		if (!sys4_load_text_dict(path))
			WARNING("Failed to load text dictionary: %s", path);
	}
	free(path);
}

static void init_mai_word_table(void)
{
	sys4_reset_mai_word_table();
	if (!config.game_dir)
		return;
	if (!sys4_source_is_utf8) {
		char *chs_path = gamedir_path_icase("CHS.TBL");
		if (chs_path && file_exists(chs_path)) {
			if (sys4_load_chs_tbl(chs_path)) {
				// Longinus CHS.TBL remaps Shift-JIS byte pairs to GBK glyphs at render time;
				// keep VM/archive string encoding in Shift-JIS mode so EX/AFA lookups stay intact.
				sys4_set_text_gbk(false);
				free(chs_path);
				return;
			}
			WARNING("Failed to load Longinus CHS.TBL: %s", chs_path);
		}
		free(chs_path);

		char *uif_path = gamedir_path_icase("uif_config.json");
		if (uif_path && file_exists(uif_path)) {
			if (sys4_load_uif_config(uif_path)) {
				// UIF character_substitution remaps Shift-JIS codepoints to Chinese glyphs at
				// render time; keep VM/archive strings in Shift-JIS mode so EX/AFA lookups stay intact.
				sys4_set_text_gbk(false);
				free(uif_path);
				return;
			}
			WARNING("Failed to load UIF character_substitution config: %s", uif_path);
		}
		free(uif_path);
	}
	if (sys4_text_is_gbk)
		return;
	char *path = gamedir_path_icase("mai/word.mai");
	if (path && file_exists(path)) {
		if (!sys4_load_mai_word_table(path))
			WARNING("Failed to load mai word table: %s", path);
	}
	free(path);
}

#ifdef __SWITCH__
static jmp_buf switch_startup;

static void switch_startup_error(const char *message)
{
	const char *reason = !strncmp(message, "*ERROR*(", 8) ? strstr(message, "): ") : NULL;
	alicehub_switch_set_launch_error(reason ? reason + 3 : message);
}

static void switch_startup_exit(int code)
{
	longjmp(switch_startup, code);
}
#endif

#if defined(ALICEHUB_IOS) || defined(__SWITCH__)
static int run_xsystem4(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
#ifdef __ANDROID__
	static bool sdl_main_entered = false;
	if (sdl_main_entered) {
		WARNING("Prevented duplicate SDL_main re-entry in :game process");
		return 0;
	}
	sdl_main_entered = true;
#endif
#ifdef __SWITCH__
	sys_error_handler = switch_startup_error;
	sys_exit_handler = switch_startup_exit;
#else
	sys_error_handler = error_handler;
#endif
	sys_warning_handler = warning_handler;
#ifdef HAVE_POSIX_CRASH_HANDLER
	install_crash_signal_handlers();
#endif
#ifdef __ANDROID__
	android_sdl_main_thread = pthread_self();
	android_sdl_main_thread_ready = true;
	sys_exit_handler = android_exit_handler;
	if (sigsetjmp(android_exit_jmp, 0) != 0)
		return android_clean_return(android_exit_code);
	android_exit_jmp_active = 1;
#endif

	const char *encoding_option = NULL;
	char *ainfile;
	const char *native_ain_path = NULL;
	int err = AIN_SUCCESS;
	bool audit = false;

	char *font_mincho = NULL;
	char *font_gothic = NULL;
	char *font_fnl = NULL;
	char *joypad = NULL;
	char *savedir = NULL;
	char *debug_info_path = NULL;
	(void)debug_info_path;

	while (1) {
		static struct option long_options[] = {
			{ "help",          no_argument,       0, LOPT_HELP },
			{ "version",       no_argument,       0, LOPT_VERSION },
			{ "audit",         no_argument,       0, LOPT_AUDIT },
			{ "echo-message",  no_argument,       0, LOPT_ECHO_MESSAGE },
			{ "font-mincho",   required_argument, 0, LOPT_FONT_MINCHO },
			{ "font-gothic",   required_argument, 0, LOPT_FONT_GOTHIC },
			{ "font-fnl",      required_argument, 0, LOPT_FONT_FNL },
			{ "font-x-scale",  required_argument, 0, LOPT_FONT_X_SCALE },
			{ "joypad",        optional_argument, 0, LOPT_JOYPAD },
			{ "font-proportional", no_argument, 0, LOPT_FONT_PROPORTIONAL },
			{ "encoding", required_argument, 0, LOPT_ENCODING },
			{ "msgskip-delay", required_argument, 0, LOPT_MSGSKIP_DELAY },
			{ "save-folder",   required_argument, 0, LOPT_SAVE_FOLDER },
			{ "save-format",   required_argument, 0, LOPT_SAVE_FORMAT },
#ifdef DEBUGGER_ENABLED
			{ "nodebug",       no_argument,       0, LOPT_NODEBUG },
			{ "debug",         no_argument,       0, LOPT_DEBUG },
			{ "debug-api",     no_argument,       0, LOPT_DEBUG_API },
			{ "debug-info",    required_argument, 0, LOPT_DEBUG_INFO },
#endif
			{ 0 }
		};
		int option_index = 0;
		int c = getopt_long(argc, argv, "haej::v", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
		case LOPT_HELP:
			usage();
			return 0;
		case 'v':
		case LOPT_VERSION:
			NOTICE("xsystem4 " XSYSTEM4_VERSION);
			return 0;
		case 'a':
		case LOPT_AUDIT:
			audit = true;
			break;
		case 'e':
		case LOPT_ECHO_MESSAGE:
			config.echo = true;
			break;
		case LOPT_FONT_MINCHO:
			font_mincho = optarg;
			break;
		case LOPT_FONT_GOTHIC:
			font_gothic = optarg;
			break;
		case LOPT_FONT_FNL:
			font_fnl = optarg;
			break;
		case LOPT_FONT_X_SCALE:
			config.manual_text_x_scale = true;
			config.text_x_scale = strtof(optarg, NULL);
			if (fabsf(config.text_x_scale) < 0.01) {
				WARNING("Invalid value for --font-x-scale option: \"%s\"", optarg);
				config.manual_text_x_scale = false;
				config.text_x_scale = 1.0;
			}
			break;
		case 'j':
		case LOPT_JOYPAD:
			joypad = optarg ? optarg : "on";
			break;
		case LOPT_FONT_PROPORTIONAL:
			config.proportional_text = true;
			break;
		case LOPT_ENCODING:
			encoding_option = optarg;
			break;
		case LOPT_MSGSKIP_DELAY:
			config.msgskip_delay = atoi(optarg);
			if (config.msgskip_delay <= 0) {
				WARNING("Invalid value for --msgskip-delay: \"%s\"", optarg);
				config.msgskip_delay = 0;
			}
			break;
		case LOPT_SAVE_FOLDER:
			savedir = optarg;
			break;
		case LOPT_SAVE_FORMAT:
			if (!strcmp(optarg, "json")) {
				config.save_format = SAVE_FORMAT_JSON;
			} else if (!strcmp(optarg, "rsm")) {
				config.save_format = SAVE_FORMAT_RSM;
			} else {
				WARNING("Invalid value for --save-format option: \"%s\"", optarg);
			}
			break;
#ifdef DEBUGGER_ENABLED
		case LOPT_NODEBUG:
			dbg_enabled = false;
			break;
		case LOPT_DEBUG:
			dbg_start_in_debugger = true;
			break;
		case LOPT_DEBUG_API:
			dbg_dap = true;
			sys_silent = true;
			break;
		case LOPT_DEBUG_INFO:
			debug_info_path = optarg;
			break;
#endif
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		if (!config_init_with_dir(".")) {
			if (!config_init_with_dir(".."))
				usage_error("Failed to find game in current or parent directory");
		}
	} else if (argc > 1) {
		usage_error("Too many arguments");
	} else {
		const char *arg_ext = file_extension(argv[0]);
		if (is_directory(argv[0])) {
			if (!config_init_with_dir(argv[0]))
				usage_error("Failed to find game in '%s'", argv[0]);
		} else if (arg_ext && (!strcasecmp(arg_ext, "ini")
				|| (!strcasecmp(arg_ext, "chs") && !strcasecmp(path_basename(argv[0]), "AliceStart.CHS")))) {
			if (!config_init_with_ini(argv[0]))
				usage_error("Failed to read .ini file '%s'", argv[0]);
		} else if (arg_ext && (!strcasecmp(arg_ext, "ain") || !strcasecmp(arg_ext, "chs"))) {
			config_init_with_ain(argv[0]);
			native_ain_path = argv[0];
		} else {
			usage_error("Can't initialize game with argument '%s'", argv[0]);
		}
	}
	ainfile = resolve_configured_ain_path(native_ain_path);
#ifdef __SWITCH__
	if (!ainfile || access(ainfile, R_OK) != 0) {
		free(ainfile);
		ERROR("Cannot read the AIN named by the game configuration");
	}
#endif
	if (ainfile && file_exists(ainfile)) {
		free(config.ain_basename_utf8);
		config.ain_basename_utf8 = xstrdup(path_basename(ainfile));
	}

	read_user_config();
	if (encoding_option && !set_encoding(encoding_option))
		usage_error("Unsupported encoding: %s", encoding_option);

	// NOTE: some command line options are handled here so that they
	//       will override settings from .xsys4rc files
	if (font_mincho)
#ifdef ALICEHUB_IOS
		config.font_paths[FONT_MINCHO] = xstrdup(font_mincho);
#else
		config.font_paths[FONT_MINCHO] = font_mincho;
#endif
	if (font_gothic)
#ifdef ALICEHUB_IOS
		config.font_paths[FONT_GOTHIC] = xstrdup(font_gothic);
#else
		config.font_paths[FONT_GOTHIC] = font_gothic;
#endif
	if (font_fnl)
#ifdef ALICEHUB_IOS
		config.fnl_path = xstrdup(font_fnl);
#else
		config.fnl_path = font_fnl;
#endif
	if (joypad) {
		if (!strcmp(joypad, "on"))
			config.joypad = true;
		else if (!strcmp(joypad, "off"))
			config.joypad = false;
		else
			WARNING("Invalid value for 'joypad' option (must be 'on' or 'off')");
	}
	if (savedir) {
		free(config.save_dir);
		config.save_dir = xstrdup(savedir);
	}

	if (!(ain = ain_open(ainfile, &err))) {
		free(ainfile);
		ERROR("%s", ain_strerror(err));
	}
	if (!sys4_source_is_utf8 && ain_content_is_utf8(ain)) {
		ain_free(ain);
		set_utf8_release(true);
		if (!(ain = ain_open(ainfile, &err))) {
			free(ainfile);
			ERROR("%s", ain_strerror(err));
		}
	}
	free(ainfile);

#ifdef __SWITCH__
	sys_error_handler = error_handler;
	sys_exit_handler = NULL;
#endif
	if (audit) {
		ain_audit(stdout, ain);
		ain_free(ain);
		return 0;
	}
#ifdef ALICEHUB_IOS
	ios_program = ain;
#endif

	mkdir_p(config.save_dir);
	savedir_begin_memory_session();
#if !defined(__ANDROID__) && !defined(ALICEHUB_IOS)
	static bool mem_cleanup_registered = false;
	if (!mem_cleanup_registered) {
		atexit(savedir_cleanup_memory_saves);
		mem_cleanup_registered = true;
	}
#endif
#ifdef __SWITCH__
	switch_menu4_load_display(config.save_dir);
#endif
	apply_game_specific_hacks(ain);
	parts_set_mixed_display_text(
		game_maman
		|| game_haharanman
		|| game_escalayer_reboot
		|| game_pascha3
		|| game_night_remaster
		|| game_evenicle2_rance
		|| game_rance01
		|| game_rance8
		|| game_haniwapetit
	);
	init_mai_word_table();
	init_text_dict();
	if (config.msgskip_delay)
		set_msgskip_delay(ain, config.msgskip_delay);
	asset_manager_init();
	dbg_init(debug_info_path);
	int exec_rc = vm_execute_ain(ain);
#ifdef __ANDROID__
	return android_clean_return(exec_rc);
#elif defined(ALICEHUB_IOS)
	return exec_rc;
#else
	sys_exit(exec_rc);
#endif
}

#ifdef __SWITCH__
int alicehub_switch_engine_main(int argc, char *argv[])
{
	optind = 0;
	if (!setjmp(switch_startup)) {
		int result = run_xsystem4(argc, argv);
		savedir_cleanup_memory_saves();
		sys_error_handler = NULL;
		sys_exit_handler = NULL;
		return result;
	}
	savedir_cleanup_memory_saves();
	sys_error_handler = NULL;
	sys_exit_handler = NULL;
	ini_abort_parse();
	free(resolved_ain_path);
	resolved_ain_path = NULL;
	reset_config();
	set_utf8_release(false);
	return 1;
}
#elif defined(ALICEHUB_IOS)
int xsystem4_main(int argc, char **argv)
{
	jmp_buf target;
	ios_exit_target = &target;
	sys_exit_handler = ios_exit_handler;
	optind = optreset = 1;
	set_utf8_release(false);
	if (setjmp(target) == 0)
		ios_exit_code = run_xsystem4(argc, argv);
	sys_exit_handler = NULL;
	ios_exit_target = NULL;
	ini_abort_parse();
	audio_end_session();
	vm_end_session();
	savedir_cleanup_memory_saves();
	input_end_session();
	gfx_fini();
	asset_manager_fini();
	if (ios_program) {
		ain_free(ios_program);
		ios_program = NULL;
	}
	free(resolved_ain_path);
	resolved_ain_path = NULL;
	reset_config();
	set_utf8_release(false);
	for (size_t i = 0; i < NR_CRASH_SIGNALS; i++)
		sigaction(crash_signals[i], &old_sigactions[i], NULL);
	return ios_exit_code;
}
#endif
