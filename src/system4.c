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
#include <libgen.h>
#include <dirent.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>
#include <getopt.h>
#include "debugger.h"
#include "file.h"
#include "little_endian.h"
#include "system4.h"
#include "system4/ain.h"
#include "system4/ald.h"
#include "system4/ini.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm.h"

struct config config = {
	.game_name = NULL,
	.ain_filename = NULL,
	.game_dir = NULL,
	.save_dir = NULL,
	.home_dir = NULL,
	.view_width = 800,
	.view_height = 600
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

static void read_config(const char *path)
{
	size_t ini_size;
	struct ini_entry *ini = ini_parse(path, &ini_size);

	for (size_t i = 0; i < ini_size; i++) {
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
		}
		ini_free_entry(&ini[i]);
	}
	free(ini);
}

static char *ald_filenames[ALDFILETYPE_MAX][ALD_FILEMAX];
static int ald_count[ALDFILETYPE_MAX];

void ald_init(int type, char **files, int count)
{
	int error = ARCHIVE_SUCCESS;
	ald[type] = ald_open(files, count, ARCHIVE_MMAP, &error);
	if (error)
		ERROR("Failed to open ALD file: %s\n", archive_strerror(error));
}

static void init_gamedata_dir(const char *path)
{
	DIR *dir;
	struct dirent *d;
	char filepath[512] = { [511] = '\0' };

	if (!(dir = opendir(path))) {
		ERROR("Failed to open directory: %s", path);
	}

	// get ALD filenames
	while ((d = readdir(dir))) {
		int dno;
		size_t len = strlen(d->d_name);
		snprintf(filepath, 511, "%s/%s", path, d->d_name);
		if (strcasecmp(d->d_name+len-4, ".ald"))
			continue;
		dno = toupper(*(d->d_name+len-5)) - 'A';
		if (dno < 0 || dno >= ALD_FILEMAX)
			continue;

		switch (*(d->d_name+len-6)) {
		case 'b':
		case 'B':
			ald_filenames[ALDFILE_BGM][dno] = strdup(filepath);
			ald_count[ALDFILE_BGM] = max(ald_count[ALDFILE_BGM], dno+1);
			break;
		case 'g':
		case 'G':
			ald_filenames[ALDFILE_CG][dno] = strdup(filepath);
			ald_count[ALDFILE_CG] = max(ald_count[ALDFILE_CG], dno+1);
			break;
		case 'w':
		case 'W':
			ald_filenames[ALDFILE_WAVE][dno] = strdup(filepath);
			ald_count[ALDFILE_WAVE] = max(ald_count[ALDFILE_WAVE], dno+1);
			break;
		default:
			WARNING("Unhandled ALD file: %s", d->d_name);
			break;
		}
	}

	// open ALD archives
	if (ald_count[ALDFILE_BGM] > 0)
		ald_init(ALDFILE_BGM, ald_filenames[ALDFILE_BGM], ald_count[ALDFILE_BGM]);
	if (ald_count[ALDFILE_CG] > 0)
		ald_init(ALDFILE_CG, ald_filenames[ALDFILE_CG], ald_count[ALDFILE_CG]);
	if (ald_count[ALDFILE_WAVE] > 0)
		ald_init(ALDFILE_WAVE, ald_filenames[ALDFILE_WAVE], ald_count[ALDFILE_WAVE]);

	closedir(dir);
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

void cleanup(possibly_unused int signal)
{
	while (waitpid((pid_t)-1, 0, WNOHANG) > 0);
}

static void ain_audit(FILE *f, struct ain *ain)
{
	link_libraries();

	for (size_t addr = 0; addr < ain->code_size;) {
		uint16_t opcode = LittleEndian_getW(ain->code, addr);
		const struct instruction *instr = &instructions[opcode];
		if (opcode >= NR_OPCODES) {
			ERROR("0x%08" SIZE_T_FMT "x: Invalid/unknown opcode: %x", opcode);
		}
		if (!instr->implemented) {
			fprintf(f, "0x%08" SIZE_T_FMT "x: %s (unimplemented instruction)\n", addr, instr->name);
		}
		if (opcode == CALLSYS) {
			uint32_t syscode = LittleEndian_getDW(ain->code, addr + 2);
			if (syscode >= NR_SYSCALLS) {
				ERROR("0x%08" SIZE_T_FMT "x: CALLSYS system.(0x%x)\n", addr, syscode);
			}
			const char * const name = syscalls[syscode].name;
			if (!name) {
				fprintf(f, "0x%08" SIZE_T_FMT "x: CALLSYS system.(0x%x)\n", addr, syscode);
			} else if (!syscalls[syscode].implemented) {
				fprintf(f, "0x%08" SIZE_T_FMT "x: CALLSYS %s (unimplemented system call)\n", addr, name);
			}

		}
		if (opcode == CALLHLL) {
			uint32_t lib = LittleEndian_getDW(ain->code, addr+2);
			uint32_t fun = LittleEndian_getDW(ain->code, addr+6);
			if (!library_exists(lib)) {
				fprintf(f, "0x%08" SIZE_T_FMT "x: CALLHLL %s.%s (unimplemented library)\n", addr,
					ain->libraries[lib].name, ain->libraries[lib].functions[fun].name);
			} else if (!library_function_exists(lib, fun)) {
				fprintf(f, "0x%08" SIZE_T_FMT "x: CALLHLL %s.%s (unimplemented function)\n", addr,
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
	puts("    -h, --help     Display this message and exit");
	puts("    -a, --audit    Audit AIN file for xsystem4 compatibility");
}

enum {
	LOPT_HELP = 256,
	LOPT_AUDIT,
};

int main(int argc, char *argv[])
{
	initialize_instructions();
	size_t len;
	char *ainfile;
	int err = AIN_SUCCESS;
	bool audit = false;

	while (1) {
		static struct option long_options[] = {
			{ "help",  no_argument, 0, LOPT_HELP },
			{ "audit", no_argument, 0, LOPT_AUDIT },
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
		}
	}
	argc -= optind;
	argv += optind;

	// TODO: if no argument given, try to read System40.ini/AliceStart.ini from current dir
	if (argc != 1) {
		ERROR("Wrong number of arguments");
		return 1;
	}

	len = strlen(argv[0]);
	if (len > 4 && !strcasecmp(&argv[0][len - 4], ".ini")) {
		read_config(argv[0]);
		if (!config.ain_filename)
			ERROR("No AIN filename specified in %s", argv[0]);

		config.game_dir = strdup(dirname(argv[0]));
		ainfile = gamedir_path(config.ain_filename);
	} else if (len > 4 && !strcasecmp(&argv[0][len - 4], ".ain")) {
		ainfile = strdup(argv[0]);
		config.ain_filename = strdup(basename(argv[0]));
		config.game_dir = strdup(dirname(argv[0]));
	} else  {
		ERROR("Not an AIN/INI file: %s", &argv[0][len - 4]);
	}

	signal(SIGCHLD, cleanup);

	config_init();

	if (!(ain = ain_open(ainfile, &err))) {
		ERROR("%s", ain_strerror(err));
	}

	if (audit) {
		ain_audit(stdout, ain);
		ain_free(ain);
		return 0;
	}

	init_gamedata_dir(config.game_dir);

#ifdef DEBUGGER_ENABLED
	dbg_init();
#endif

	vm_execute_ain(ain);
	sys_exit(0);
}
