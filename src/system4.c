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
#include <libgen.h>
#include <dirent.h>
#include <ctype.h>
#include <signal.h>
#include <getopt.h>
#include <limits.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/file.h"
#include "system4/ini.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "asset_manager.h"
#include "debugger.h"
#include "file.h"
#include "gfx/gfx.h"
#include "little_endian.h"
#include "vm.h"

struct config config = {
	.game_name = NULL,
	.ain_filename = NULL,
	.game_dir = NULL,
	.save_dir = NULL,
	.home_dir = NULL,
	.view_width = 800,
	.view_height = 600,
	.mixer_nr_channels = 0,
	.mixer_channels = NULL,
};

char *unix_path(const char *path)
{
	char *utf = sjis2utf(path, strlen(path));
	for (int i = 0; utf[i]; i++) {
		if (utf[i] == '\\')
			utf[i] = '/';
	}
	return utf;
}

static bool is_absolute_path(const char *path)
{
#if (defined(_WIN32) || defined(__WIN32__))
	int i = (isalpha(path[0]) && path[1] == ':') ? 2 : 0;
	return path[i] == '/' || path[i] == '\\';
#else
	return path[0] == '/';
#endif
}

static char *resolve_path(const char *dir, const char *path)
{
	char *utf = unix_path(path);
	if (is_absolute_path(utf))
		return utf;

	char *resolved = xmalloc(strlen(dir) + strlen(utf) + 2);
	strcpy(resolved, dir);
	strcat(resolved, "/");
	strcat(resolved, utf);

	free(utf);
	return resolved;
}

char *gamedir_path(const char *path)
{
	return resolve_path(config.game_dir, path);
}

char *savedir_path(const char *path)
{
	return resolve_path(config.save_dir, path);
}

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

static void read_mixer_channels(struct ini_entry *entry)
{
	if (entry->value.type != INI_LIST)
		ERROR("ini value for 'VolumeValancer' is not a list");

	config.mixer_nr_channels = entry->value.list_size;
	config.mixer_channels = xcalloc(entry->value.list_size, sizeof(char*));
	for (size_t i = 0; i < entry->value.list_size; i++) {
		if (entry->value.list[i].type == INI_NULL) {
			WARNING("No name given for VolumeValancer[%d]", i);
			continue;
		}
		if (entry->value.list[i].type == INI_STRING) {
			config.mixer_channels[i] = strdup(entry->value.list[i].s->text);
		} else if (entry->value.list[i].type == INI_LIST) {
			if (entry->value.list[i].list_size != 2)
				ERROR("ini value for 'VolumeValancer' is list of wrong size");
			if (entry->value.list[i].list[0].type != INI_STRING)
				ERROR("ini value for 'VolumeValancer' name is not a string");
			if (entry->value.list[i].list[1].type != INI_INTEGER)
				ERROR("ini value for 'VolumeValancer' level is not an integer");
			config.mixer_channels[i] = strdup(entry->value.list[i].list[0].s->text);
		}
	}
}

static void read_config(const char *path)
{
	int ini_size;
	struct ini_entry *ini = ini_parse(path, &ini_size);
	if (!ini)
		ERROR("Failed to read %s", path);

	for (int i = 0; i < ini_size; i++) {
		if (!strcmp(ini[i].name->text, "GameName")) {
			config.game_name = strdup(ini_string(&ini[i])->text);
		} else if (!strcmp(ini[i].name->text, "CodeName")) {
			config.ain_filename = strdup(ini_string(&ini[i])->text);
		} else if (!strcmp(ini[i].name->text, "SaveFolder")) {
			config.save_dir = strdup(ini_string(&ini[i])->text);
		} else if (!strcmp(ini[i].name->text, "ViewWidth")) {
			config.view_width = ini_integer(&ini[i]);
		} else if (!strcmp(ini[i].name->text, "ViewHeight")) {
			config.view_height = ini_integer(&ini[i]);
		} else if (!strcmp(ini[i].name->text, "VolumeValancer")) {
			read_mixer_channels(&ini[i]);
		}
		ini_free_entry(&ini[i]);
	}
	free(ini);
}

static void read_user_config_file(const char *path)
{
	int ini_size;
	struct ini_entry *ini = ini_parse(path, &ini_size);
	if (!ini)
		return;

	for (int i = 0; i < ini_size; i++) {
		if (!strcmp(ini[i].name->text, "font-mincho")) {
			font_paths[FONT_MINCHO] = xstrdup(ini_string(&ini[i])->text);
		} else if (!strcmp(ini[i].name->text, "font-gothic")) {
			font_paths[FONT_GOTHIC] = xstrdup(ini_string(&ini[i])->text);
		}
		ini_free_entry(&ini[i]);
	}
	free(ini);
}

static void read_user_config(void)
{
	char *path;

	// global config
	path = xmalloc(PATH_MAX);
	snprintf(path, PATH_MAX-1, "%s/.xsys4rc", config.home_dir);
	read_user_config_file(path);
	free(path);

	// game-specific config
	path = gamedir_path(".xsys4rc");
	read_user_config_file(path);
	free(path);
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
	return xstrdup(".");
}

static char *get_save_path(const char *dir_name)
{
	if (!dir_name)
		dir_name = "SaveData";

	char *utf8_game_name = sjis2utf(config.game_name, strlen(config.game_name));
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

static void config_init(void)
{
	config.home_dir = get_xsystem4_home();
	if (!config.game_name)
		config.game_name = strdup(config.ain_filename);

	char *new_save_dir = get_save_path(config.save_dir);
	free(config.save_dir);
	config.save_dir = new_save_dir;
	mkdir_p(new_save_dir);
}

static void config_init_with_ini(const char *ini_path)
{
	read_config(ini_path);
	if (!config.ain_filename)
		ERROR("No AIN filename specified in %s", ini_path);
	char *tmp = strdup(ini_path);
	config.game_dir = strdup(dirname(tmp));
	free(tmp);
	config_init();
}

static void config_init_with_dir(const char *dir)
{
	char path[PATH_MAX];
	snprintf(path, PATH_MAX, "%s/System40.ini", dir);
	if (!file_exists(path)) {
		snprintf(path, PATH_MAX, "%s/AliceStart.ini", dir);
	}
	config_init_with_ini(path);
}

static void config_init_with_ain(const char *ain_path)
{
	char *basename_tmp = strdup(ain_path);
	char *dirname_tmp = strdup(ain_path);
	config.ain_filename = strdup(basename(basename_tmp));
	config.game_dir = strdup(dirname(dirname_tmp));
	free(basename_tmp);
	free(dirname_tmp);
	config_init();
}

#if (!defined(_WIN32) && !defined(__WIN32__))
#include <sys/wait.h>
void cleanup(possibly_unused int signal)
{
	while (waitpid((pid_t)-1, 0, WNOHANG) > 0);
}
#endif

static void ain_audit(FILE *f, struct ain *ain)
{
	link_libraries();

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
	puts("Usage: xsystem4 <options> <inifile>");
	puts("    -h, --help         Display this message and exit");
	puts("    -a, --audit        Audit AIN file for xsystem4 compatibility");
	puts("        --font-mincho  Specify the path to the mincho font to use");
	puts("        --font-gothic  Specify the path to the gothic font to use");
	puts("        --nodebug      Disable debugger");
#ifdef DEBUGGER_ENABLED
	puts("        --debug    Start in debugger");
#endif
}

enum {
	LOPT_HELP = 256,
	LOPT_AUDIT,
	LOPT_FONT_MINCHO,
	LOPT_FONT_GOTHIC,
	LOPT_NODEBUG,
#ifdef DEBUGGER_ENABLED
	LOPT_DEBUG,
#endif
};

int main(int argc, char *argv[])
{
	initialize_instructions();
	char *ainfile;
	int err = AIN_SUCCESS;
	bool audit = false;
#ifdef DEBUGGER_ENABLED
	bool start_in_debugger = false;
#endif

	char *font_mincho = NULL;
	char *font_gothic = NULL;

	while (1) {
		static struct option long_options[] = {
			{ "help",        no_argument,       0, LOPT_HELP },
			{ "audit",       no_argument,       0, LOPT_AUDIT },
			{ "font-mincho", required_argument, 0, LOPT_FONT_MINCHO },
			{ "font-gothic", required_argument, 0, LOPT_FONT_GOTHIC },
			{ "nodebug",     no_argument,       0, LOPT_NODEBUG },
#ifdef DEBUGGER_ENABLED
			{ "debug",       no_argument,       0, LOPT_DEBUG },
#endif
		};
		int option_index = 0;
		int c = getopt_long(argc, argv, "ha", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
		case LOPT_HELP:
			usage();
			return 0;
		case 'a':
		case LOPT_AUDIT:
			audit = true;
			break;
		case LOPT_FONT_MINCHO:
			font_mincho = optarg;
			break;
		case LOPT_FONT_GOTHIC:
			font_gothic = optarg;
			break;
#ifdef DEBUGGER_ENABLED
		case LOPT_NODEBUG:
			dbg_enabled = false;
			break;
		case LOPT_DEBUG:
			start_in_debugger = true;
			break;
#else
		case LOPT_NODEBUG:
			break;
#endif
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		config_init_with_dir(".");
	} else if (argc > 1) {
		usage();
		ERROR("Too many arguments");
	} else if (is_directory(argv[0])) {
		config_init_with_dir(argv[0]);
	} else if (!strcasecmp(file_extension(argv[0]), "ini")) {
		config_init_with_ini(argv[0]);
	} else if (!strcasecmp(file_extension(argv[0]), "ain")) {
		config_init_with_ain(argv[0]);
	}
	ainfile = gamedir_path(config.ain_filename);

#if (!defined(_WIN32) && !defined(__WIN32__))
	signal(SIGCHLD, cleanup);
#endif

	read_user_config();

	// NOTE: some command line options are handled here so that they
	//       will override settings from .xsys4rc files
	if (font_mincho)
		font_paths[FONT_MINCHO] = font_mincho;
	if (font_gothic)
		font_paths[FONT_GOTHIC] = font_gothic;

	if (!(ain = ain_open(ainfile, &err))) {
		ERROR("%s", ain_strerror(err));
	}

	if (audit) {
		ain_audit(stdout, ain);
		ain_free(ain);
		return 0;
	}

	asset_manager_init();

#ifdef DEBUGGER_ENABLED
	dbg_init();
	if (start_in_debugger)
		dbg_repl();
#endif

	sys_exit(vm_execute_ain(ain));
}
