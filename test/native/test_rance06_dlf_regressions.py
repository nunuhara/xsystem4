#!/usr/bin/env python3
"""Compile the engine's DLF path selection against a real filesystem fixture."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_evenicle2_regressions import LIBSYS4_CORE, compile_and_run
from test_vm_reign_regressions import extract_function


class DungeonDataTests(unittest.TestCase):
    def test_switch_mount_paths(self):
        harness = r"""
#include <assert.h>
#include <string.h>
#include "system4/file.h"

int main(void) {
    assert(!strcmp(path_dirname("sdmc:/game.ain"), "sdmc:/"));
    assert(!strcmp(path_dirname("sdmc:/switch/game.ain"), "sdmc:/switch"));
    assert(!strcmp(path_basename("sdmc:/switch/game.ain"), "game.ain"));
    assert(!strcmp(path_dirname("romfs:/fonts/"), "romfs:/"));
    assert(!strcmp(path_basename("romfs:/fonts/"), "fonts"));
    assert(!strcmp(path_dirname("game.ain"), "."));
    assert(!strcmp(path_basename("game.ain"), "game.ain"));
    return 0;
}
"""
        compile_and_run(harness, LIBSYS4_CORE, ["-D__SWITCH__", "-lz", "-lm"])

    def test_dungeon_data_paths(self):
        function = extract_function("src/dungeon/dungeon.c", "dungeon_data_path")
        harness = r'''
#define _DEFAULT_SOURCE
#include <assert.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
typedef DIR UDIR;
static const char *root;
static char *path_join(const char *a, const char *b) {
    char *p; assert(asprintf(&p, "%s/%s", a, b) >= 0); return p;
}
static char *gamedir_path(const char *p) { return path_join(root, p); }
static bool file_exists(const char *p) { struct stat s; return stat(p, &s) == 0; }
static bool is_directory(const char *p) { struct stat s; return !stat(p, &s) && S_ISDIR(s.st_mode); }
static char *path_get_icase(const char *p) {
    char *dir = strdup(p); char *base = strrchr(dir, '/'); *base++ = 0;
    DIR *d = opendir(dir); char *result = NULL; struct dirent *e;
    while (d && (e = readdir(d))) {
        if (!strcasecmp(base, e->d_name)) { result = path_join(dir, e->d_name); break; }
    }
    if (d) closedir(d);
    free(dir); return result;
}
static char *gamedir_path_icase(const char *p) {
    char *path = gamedir_path(p); char *result = path_get_icase(path); free(path); return result;
}
static DIR *opendir_utf8(const char *p) { return opendir(p); }
static char *readdir_utf8(DIR *d) { struct dirent *e = readdir(d); return e ? strdup(e->d_name) : NULL; }
static int closedir_utf8(DIR *d) { return closedir(d); }
'''
        harness += function + r'''
int main(int argc, char **argv) {
    assert(argc == 3); root = argv[1];
    char *path = dungeon_data_path(); char *expected = path_join(root, argv[2]);
    assert(!strcmp(path, expected)); free(path); free(expected); return 0;
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "test.c"
            binary = root / "test"
            source.write_text(harness)
            subprocess.run(["cc", "-D_GNU_SOURCE", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", str(binary)], check=True)

            def check(expected):
                subprocess.run([str(binary), str(root), expected], check=True)

            check("Data/DungeonData.dlf")
            data = root / "dAtA"
            data.mkdir()
            check("dAtA/DungeonData.dlf")
            for invalid in ["DungeonData().dlf", "DungeonData(x).dlf", "DungeonData(1).dlf.bak"]:
                (data / invalid).touch()
            (data / "DungeonData(0).dlf").mkdir()
            check("dAtA/DungeonData.dlf")
            (data / "dUnGeOnDaTa(7).DlF").touch()
            check("dAtA/dUnGeOnDaTa(7).DlF")
            (data / "DungeonData(2).dlf").touch()
            check("dAtA/DungeonData(2).dlf")
            (data / "DUNGEONDATA.DLF").write_bytes(b"invalid canonical must still win")
            check("dAtA/DUNGEONDATA.DLF")


if __name__ == "__main__":
    unittest.main()
