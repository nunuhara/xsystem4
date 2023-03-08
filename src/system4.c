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
#include "gfx/gfx.h"
#include "gfx/font.h"
#include "little_endian.h"
#include "vm.h"

#include "version.h"

void apply_game_specific_hacks(struct ain *ain);

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
	.default_volume = 100,
	.joypad = false,
	.echo = false,
	.text_x_scale = 1.0,
	.manual_text_x_scale = false,

	.bgi_path = NULL,
	.wai_path = NULL,
	.ex_path = NULL,
	.fnl_path = NULL,
	.font_paths = { NULL, NULL },
};

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

	config.mixer_nr_channels = entry->value.list_size;
	config.mixer_channels = xcalloc(entry->value.list_size, sizeof(char*));
	config.mixer_volumes = xcalloc(entry->value.list_size, sizeof(int));
	for (size_t i = 0; i < entry->value.list_size; i++) {
		if (entry->value.list[i].type == INI_NULL) {
			config.mixer_channels[i] = strdup("Unnamed");
			config.mixer_volumes[i] = 0;
		} else if (entry->value.list[i].type == INI_STRING) {
			config.mixer_channels[i] = strdup(entry->value.list[i].s->text);
			// XXX: assumes default_volume was already initialized
			config.mixer_volumes[i] = config.default_volume;
		} else if (entry->value.list[i].type == INI_LIST) {
			if (entry->value.list[i].list_size != 2)
				ERROR("ini value for 'VolumeValancer' is list of wrong size");
			if (entry->value.list[i].list[0].type != INI_STRING)
				ERROR("ini value for 'VolumeValancer' name is not a string");
			if (entry->value.list[i].list[1].type != INI_INTEGER)
				ERROR("ini value for 'VolumeValancer' level is not an integer");
			config.mixer_channels[i] = strdup(entry->value.list[i].list[0].s->text);
			config.mixer_volumes[i] = entry->value.list[i].list[1].i;
		}
	}
}

static bool read_config(const char *path)
{
	int ini_size;
	struct ini_entry *ini = ini_parse(path, &ini_size);
	if (!ini)
		return false;

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

static void read_user_config_file(const char *path)
{
	int ini_size;
	struct ini_entry *ini = ini_parse(path, &ini_size);
	if (!ini)
		return;

	for (int i = 0; i < ini_size; i++) {
		if (!strcmp(ini[i].name->text, "font-mincho")) {
			config.font_paths[FONT_MINCHO] = xstrdup(ini_string(&ini[i])->text);
		} else if (!strcmp(ini[i].name->text, "font-gothic")) {
			config.font_paths[FONT_GOTHIC] = xstrdup(ini_string(&ini[i])->text);
		} else if (!strcmp(ini[i].name->text, "font-fnl")) {
			config.fnl_path = xstrdup(ini_string(&ini[i])->text);
		} else if (!strcmp(ini[i].name->text, "font-x-scale")) {
			float f = strtof(ini_string(&ini[i])->text, NULL);
			if (fabsf(f) < 0.01) {
				WARNING("Invalid value for font-x-scale in config: \"%s\"",
						ini_string(&ini[i])->text);
			} else {
				config.manual_text_x_scale = true;
				config.text_x_scale = f;
			}
		} else if (!strcmp(ini[i].name->text, "save-folder")) {
			free(config.save_dir);
			config.save_dir = xstrdup(ini_string(&ini[i])->text);
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

static bool config_init_with_ini(const char *ini_path)
{
	if (!read_config(ini_path))
		return false;
	if (!config.ain_filename)
		ERROR("No AIN filename specified in %s", ini_path);
	char *tmp = strdup(ini_path);
	config.game_dir = strdup(path_dirname(tmp));
	free(tmp);
	config_init();
	return true;
}

static bool config_init_with_dir(const char *dir)
{
	char path[PATH_MAX];
	snprintf(path, PATH_MAX, "%s/System40.ini", dir);
	if (!file_exists(path)) {
		snprintf(path, PATH_MAX, "%s/AliceStart.ini", dir);
		if (!file_exists(path))
			return false;
	}
	return config_init_with_ini(path);
}

static void config_init_with_ain(const char *ain_path)
{
	config.ain_filename = strdup(path_basename(ain_path));
	config.game_dir = strdup(path_dirname(ain_path));
	config_init();
}

static void ain_audit(FILE *f, struct ain *ain)
{
	init_libraries();

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
	puts("    -h, --help          Display this message and exit");
	puts("    -v, --version       Display the version and exit");
	puts("    -a, --audit         Audit AIN file for xsystem4 compatibility");
	puts("    -e, --echo-message  Echo in-game messages to standard output");
	puts("        --font-mincho   Specify the path to the mincho font to use");
	puts("        --font-gothic   Specify the path to the gothic font to use");
	puts("        --font-fnl      Specify the path to a .fnl font library to use");
	puts("        --font-x-scale  Specify the x scale for text rendering (1.0 = default scale)");
	puts("    -j, --joypad        Enable joypad");
	puts("        --save-folder   Override save folder location");
#ifdef DEBUGGER_ENABLED
	puts("        --nodebug       Disable debugger");
	puts("        --debug         Start in debugger");
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
	LOPT_SAVE_FOLDER,
#ifdef DEBUGGER_ENABLED
	LOPT_NODEBUG,
	LOPT_DEBUG,
#endif
};

#ifdef _WIN32
static void windows_error_handler(const char *msg)
{
	sys_warning("%s", msg);
	sys_warning("Press the enter key to exit...\n");
	getchar();
	sys_exit(1);
}
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
	sys_error_handler = windows_error_handler;
#endif
	initialize_instructions();
	char *ainfile;
	int err = AIN_SUCCESS;
	bool audit = false;

	char *font_mincho = NULL;
	char *font_gothic = NULL;
	char *font_fnl = NULL;
	char *joypad = NULL;
	char *savedir = NULL;

	while (1) {
		static struct option long_options[] = {
			{ "help",         no_argument,       0, LOPT_HELP },
			{ "version",      no_argument,       0, LOPT_VERSION },
			{ "audit",        no_argument,       0, LOPT_AUDIT },
			{ "echo-message", no_argument,       0, LOPT_ECHO_MESSAGE },
			{ "font-mincho",  required_argument, 0, LOPT_FONT_MINCHO },
			{ "font-gothic",  required_argument, 0, LOPT_FONT_GOTHIC },
			{ "font-fnl",     required_argument, 0, LOPT_FONT_FNL },
			{ "font-x-scale", required_argument, 0, LOPT_FONT_X_SCALE },
			{ "joypad",       optional_argument, 0, LOPT_JOYPAD },
			{ "save-folder",  required_argument, 0, LOPT_SAVE_FOLDER },
#ifdef DEBUGGER_ENABLED
			{ "nodebug",      no_argument,       0, LOPT_NODEBUG },
			{ "debug",        no_argument,       0, LOPT_DEBUG },
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
		case LOPT_SAVE_FOLDER:
			savedir = optarg;
			break;
#ifdef DEBUGGER_ENABLED
		case LOPT_NODEBUG:
			dbg_enabled = false;
			break;
		case LOPT_DEBUG:
			dbg_start_in_debugger = true;
			break;
#endif
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		if (!config_init_with_dir(".")) {
			if (!config_init_with_dir("../"))
				usage_error("Failed to find game in current or parent directory");
		}
	} else if (argc > 1) {
		usage_error("Too many arguments");
	} else if (is_directory(argv[0])) {
		if (!config_init_with_dir(argv[0]))
			usage_error("Failed to find game in '%s'", argv[0]);
	} else if (!strcasecmp(file_extension(argv[0]), "ini")) {
		if (!config_init_with_ini(argv[0]))
			usage_error("Failed to read .ini file '%s'", argv[0]);
	} else if (!strcasecmp(file_extension(argv[0]), "ain")) {
		config_init_with_ain(argv[0]);
	} else {
		usage_error("Can't initialize game with argument '%s'", argv[0]);
	}
	ainfile = gamedir_path(config.ain_filename);

	read_user_config();

	// NOTE: some command line options are handled here so that they
	//       will override settings from .xsys4rc files
	if (font_mincho)
		config.font_paths[FONT_MINCHO] = font_mincho;
	if (font_gothic)
		config.font_paths[FONT_GOTHIC] = font_gothic;
	if (font_fnl)
		config.fnl_path = font_fnl;
	if (joypad) {
		if (!strcmp(joypad, "on"))
			config.joypad = true;
		else if (!strcmp(joypad, "off"))
			config.joypad = false;
		else
			WARNING("Invalid value for 'joypad' option (must be 'on' or 'off')");
	} if (savedir) {
		free(config.save_dir);
		config.save_dir = strdup(savedir);
	}

	if (!(ain = ain_open(ainfile, &err))) {
		ERROR("%s", ain_strerror(err));
	}

	if (audit) {
		ain_audit(stdout, ain);
		ain_free(ain);
		return 0;
	}

	apply_game_specific_hacks(ain);

	asset_manager_init();

#ifdef DEBUGGER_ENABLED
	dbg_init();
	if (dbg_start_in_debugger)
		dbg_repl();
#endif

	sys_exit(vm_execute_ain(ain));
}
