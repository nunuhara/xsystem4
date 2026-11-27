#!/usr/bin/env python3
"""Host C regression tests for Rance 10 MOD GameConfig.asd persistence in modifier.c.

The floating modifier marks the game config "ready" after 60 frames even when
the game has not yet called GroupLoad for GameConfig.asd. A GroupSave of
g_gameConfig at that point overwrites the user's file with defaults (battle /
map speed, MOD unlock flags), so persisting must wait for the real load.

Run: python3 xsystem4/test/native/test_rance10_game_config_persist_regressions.py
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MODIFIER = "src/modifier.c"


def extract_function(rel_path: str, name: str) -> str:
    source = (ROOT / rel_path).read_text(encoding="utf-8")
    pattern = re.compile(
        rf"^(?:static\s+)?[A-Za-z_][A-Za-z0-9_\s\*]*\b{re.escape(name)}\([^;{{}}]*\)\s*\r?\n?\{{",
        re.MULTILINE,
    )
    match = pattern.search(source)
    if not match:
        raise ValueError(f"Function definition not found: {name} in {rel_path}")
    brace = source.index("{", match.start())
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

struct ain_variable { int group_index; };
struct ain { int nr_globals; int nr_global_groups; char **global_group_names; struct ain_variable *globals; };
static struct ain_variable test_globals[1] = { { .group_index = 0 } };
static char *test_group_names[1] = { "group" };
static struct ain test_ain = { 1, 1, test_group_names, test_globals };
static struct ain *ain = &test_ain;
static struct { const char *save_dir; } config = { "/save" };
static int s_glob_game_config = 0;
static bool s_game_config_ready = false;
static bool s_game_config_loaded = false;
static int nr_saves = 0;
static int nr_mark_ready = 0;

static bool vm_globals_ready(void) { return true; }
static void vm_inspect_mod_streamer_ain(void) {}
static int vm_find_global_idx(const char *name, const char *name2) { (void)name; (void)name2; return 0; }
static int save_globals(const char *key, const char *file, const char *group, int *n)
{
	(void)n;
	assert(!strcmp(key, "rance10") && !strcmp(file, "GameConfig.asd") && !strcmp(group, "group"));
	nr_saves++;
	return 1;
}
static void modifier_mark_config_ready(void) { nr_mark_ready++; s_game_config_ready = true; }

void modifier_on_config_loaded(void);

@FUNCS@

int main(void)
{
	/* Frame-tick fallback: ready, but the game's GroupLoad has not run yet. */
	modifier_mark_config_ready();
	assert(s_game_config_ready);
	assert(!vm_persist_game_config_asd());
	assert(nr_saves == 0);

	/* GroupLoad of GameConfig.asd finished: persisting live values is safe. */
	modifier_on_config_loaded();
	assert(s_game_config_loaded && nr_mark_ready == 2);
	assert(vm_persist_game_config_asd());
	assert(nr_saves == 1);
	puts("ok");
	return 0;
}
"""


class Rance10GameConfigPersistTest(unittest.TestCase):
    def test_no_group_save_before_game_config_load(self):
        funcs = "\n\n".join([
            extract_function(MODIFIER, "vm_persist_game_config_asd"),
            extract_function(MODIFIER, "modifier_on_config_loaded"),
        ])
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "test.c"
            exe = Path(tmp) / "test_bin"
            src.write_text(HARNESS.replace("@FUNCS@", funcs), encoding="utf-8")
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                            str(src), "-o", str(exe)], check=True)
            out = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            self.assertEqual(out.stdout.strip(), "ok")

    def test_frame_fallback_does_not_mark_config_loaded(self):
        src = (ROOT / MODIFIER).read_text(encoding="utf-8")
        frame = extract_function(MODIFIER, "modifier_on_frame")
        self.assertIn("modifier_mark_config_ready();", frame)
        self.assertNotIn("modifier_on_config_loaded();", frame)
        self.assertEqual(src.count("s_game_config_loaded = true;"), 1)



class VmResetTests(unittest.TestCase):
    def test_ain_init_clears_loaded_flag(self):
        body = extract_function("src/modifier.c", "modifier_on_ain_init")
        self.assertIn("s_game_config_loaded = false;", body)

if __name__ == "__main__":
    unittest.main()
