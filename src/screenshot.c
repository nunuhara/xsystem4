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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <SDL.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "xsystem4.h"

// XXX: can't include gfx/gfx.h because windows.h redefines Rectangle
typedef struct texture Texture;
Texture *gfx_main_surface(void);
int gfx_save_texture(Texture *t, const char *path, enum cg_type);

#ifdef _WIN32
#include <windows.h>

static char *default_filename(void)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	char datetime[128];
	strftime(datetime, sizeof(datetime), "%Y%m%d_%H%M%S", tm);

	char *name = xmalloc(PATH_MAX);
	snprintf(name, PATH_MAX, "%s_%s.png", config.game_name, datetime);
	return name;
}

static char *default_path(void)
{
	// FIXME: Earlier games use "[gamedir]/Snapshot/", but later games save under
	//        "Documents/AliceSoft/[gamename]/Screenshot/"
	char *path = gamedir_path("Snapshot");
	if (mkdir_p(path)) {
		WARNING("mkdir_p: %s", strerror(errno));
		free(path);
		return NULL;
	}

	for (int i = 0; path[i]; i++) {
		if (path[i] == '/')
			path[i] = '\\';
	}

	return path;
}

void screenshot_save(void)
{
	char *name = default_filename();
	char *path = default_path();

	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFilter = "PNG Files (*.png)\0*.png\0";
	ofn.lpstrFile = name;
	ofn.lpstrInitialDir = path;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
	ofn.lpstrDefExt = "png";

	if (GetSaveFileName(&ofn))
	{
		for (int i = 0; name[i]; i++) {
			if (name[i] == '\\')
				name[i] = '/';
		}
		gfx_save_texture(gfx_main_surface(), name, ALCG_PNG);
		char msg[PATH_MAX + 64];
		snprintf(msg, sizeof(msg), "Screenshot saved to %s", name);
		SDL_ShowSimpleMessageBox(0, "xsystem4", msg, NULL);
	}

	free(name);
	free(path);
}

#else
#ifdef __ANDROID__

void screenshot_save(void)
{

}

#else

static char *default_filename(void)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	char datetime[128];
	strftime(datetime, sizeof(datetime), "%Y%m%d_%H%M%S", tm);

	// FIXME: Earlier games use "[gamedir]/Snapshot/", but later games save under
	//        "Documents/AliceSoft/[gamename]/Screenshot/"
	char name[PATH_MAX];
	snprintf(name, sizeof(name), "Snapshot/%s_%s.png", config.game_name, datetime);
	return gamedir_path(name);
}

static bool have_zenity(void)
{
	FILE *fp = popen("zenity --version", "r");
	if (!fp)
		return false;

	char line[256];
	while (fgets(line, sizeof(line), fp))
		printf("%s", line);
	if (pclose(fp) != 0)
		return false;
	return true;
}

static char *zenity_get_filename(const char *dflt)
{
	char buf[PATH_MAX + 64];
	snprintf(buf, sizeof(buf), "zenity --file-selection --filename=\"%s\" --save", dflt);

	FILE *fp = popen(buf, "r");
	if (!fp)
		return NULL;

	// read filename
	char *filename = fgets(buf, sizeof(buf), fp);
	if (filename && *filename) {
		filename = xstrdup(filename);

		// delete trailing newline
		size_t len = strlen(filename);
		if (filename[len-1] == '\n')
			filename[len-1] = '\0';
	} else {
		filename = NULL;
	}

	// empty pipe and close
	while (fgets(buf, sizeof(buf), fp))
		/* nothing */;
	pclose(fp);

	return filename;
}

void screenshot_save(void)
{
	// get filename
	char *name = default_filename();
	if (have_zenity()) {
		char *tmp = zenity_get_filename(name);
		free(name);
		name = tmp;
		if (!name)
			return;
	}

	// create directory
	char *dir = path_dirname(name);
	if (mkdir_p(dir)) {
		WARNING("mkdir_p: %s", strerror(errno));
		free(name);
		return;
	}

	// save screenshot
	gfx_save_texture(gfx_main_surface(), name, ALCG_PNG);

	// XXX: this uses zenity on wayland, so there is no confirmation
	//      that anything happened if zenity is not installed
	char msg[PATH_MAX + 64];
	snprintf(msg, sizeof(msg), "Screenshot saved to %s", name);
	SDL_ShowSimpleMessageBox(0, "xsystem4", msg, NULL);
	NOTICE("%s", msg);

	free(name);
}

#endif // __ANDROID__
#endif // _WIN32
