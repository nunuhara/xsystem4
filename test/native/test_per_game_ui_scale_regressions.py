#!/usr/bin/env python3
"""Host C regression tests for per-game font & scrollbar scale persistence in modifier.c.

Run: python3 xsystem4/test/native/test_per_game_ui_scale_regressions.py
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
        rf"^(?:static\s+)?(?:inline\s+)?[A-Za-z_][A-Za-z0-9_\s\*]*\b{re.escape(name)}\([^;{{}}]*\)\s*\r?\n?\{{",
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
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cJSON.h"

#define LOCK_DISABLE_STREAMER_MODE (1 << 11)
#define EX_MAX_GAMES 32

static void ex_snapshot_locks_locked(int *out_masks, bool *out_init)
{
	memset(out_masks, 0, sizeof(int) * EX_MAX_GAMES);
	memset(out_init, 0, sizeof(bool) * EX_MAX_GAMES);
}
static bool ex_all_locks_initialized_locked(void) { return true; }
static void ex_load_locks_from_json_locked(cJSON *root) { (void)root; }
static bool ex_locks_differ_from_json(cJSON *root, const int *snap_masks, const bool *snap_init)
{
	(void)root; (void)snap_masks; (void)snap_init;
	return false;
}
static void ex_save_locks_to_json(cJSON *root, const int *snap_masks, const bool *snap_init)
{
	(void)root; (void)snap_masks; (void)snap_init;
}

static pthread_mutex_t g_mod_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_cfg_io_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_cfg_tmp_seq = 0;

static char *g_modifier_config_path = NULL;
static char *g_ui_scale_game_key = NULL;
static int g_live_lock_mask = 0;
static bool g_has_lock_mask_init = false;
static int g_dohna_lock_mask = 0;
static bool g_has_dohna_lock_mask_init = false;
static int g_evenicle_lock_mask = 0;
static bool g_has_evenicle_lock_mask_init = false;
static int g_sr_lock_mask = 0;
static bool g_has_sr_lock_mask_init = false;
static int g_r03_lock_mask = 0;
static bool g_has_r03_lock_mask_init = false;
static int g_r08_lock_mask = 0;
static bool g_has_r08_lock_mask_init = false;
static int g_r09_lock_mask = 0;
static bool g_has_r09_lock_mask_init = false;
static int g_r01_lock_mask = 0;
static bool g_has_r01_lock_mask_init = false;
static int g_r02_lock_mask = 0;
static bool g_has_r02_lock_mask_init = false;
static int g_font_scale_pct = 100;
static int g_scrollbar_scale_pct = 100;
static bool g_has_ui_scale_init = false;
static int s_applied_font_scale_pct = 100;
static int s_applied_scrollbar_scale_pct = 100;
static int g_pending_lock_streamer_mode = -1;

static int stub_parts_font = 100;
static int stub_parts_sb = 100;
static void parts_set_modifier_font_scale(int pct) { stub_parts_font = pct; }
static void parts_set_modifier_scrollbar_scale(int pct) { stub_parts_sb = pct; }

static bool file_exists(const char *path)
{
	struct stat st;
	return path && stat(path, &st) == 0;
}

static char *file_read(const char *path, size_t *len_out)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) return NULL;
	fseek(fp, 0, SEEK_END);
	long sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (sz < 0) { fclose(fp); return NULL; }
	char *buf = malloc((size_t)sz + 1);
	if (!buf) { fclose(fp); return NULL; }
	size_t n = fread(buf, 1, (size_t)sz, fp);
	fclose(fp);
	buf[n] = '\0';
	if (len_out) *len_out = n;
	return buf;
}

static bool write_config_atomic_unique(const char *path, const char *data, size_t len, uint32_t seq)
{
	(void)seq;
	FILE *fp = fopen(path, "wb");
	if (!fp) return false;
	bool ok = fwrite(data, 1, len, fp) == len;
	fclose(fp);
	return ok;
}

@FUNCS@

static void reset_state(const char *cfg_path, const char *game_key)
{
	free(g_modifier_config_path);
	g_modifier_config_path = cfg_path ? strdup(cfg_path) : NULL;
	free(g_ui_scale_game_key);
	g_ui_scale_game_key = (game_key && game_key[0]) ? strdup(game_key) : NULL;
	g_live_lock_mask = 0;
	g_has_lock_mask_init = false;
	g_dohna_lock_mask = 0;
	g_has_dohna_lock_mask_init = false;
	g_evenicle_lock_mask = 0;
	g_has_evenicle_lock_mask_init = false;
	g_sr_lock_mask = 0;
	g_has_sr_lock_mask_init = false;
	g_r03_lock_mask = 0;
	g_has_r03_lock_mask_init = false;
	g_r08_lock_mask = 0;
	g_has_r08_lock_mask_init = false;
	g_r09_lock_mask = 0;
	g_has_r09_lock_mask_init = false;
	g_r01_lock_mask = 0;
	g_has_r01_lock_mask_init = false;
	g_r02_lock_mask = 0;
	g_has_r02_lock_mask_init = false;
	g_font_scale_pct = 100;
	g_scrollbar_scale_pct = 100;
	g_has_ui_scale_init = false;
	s_applied_font_scale_pct = 100;
	s_applied_scrollbar_scale_pct = 100;
	stub_parts_font = 100;
	stub_parts_sb = 100;
}

static cJSON *read_json_file(const char *path)
{
	size_t sz = 0;
	char *raw = file_read(path, &sz);
	assert(raw != NULL);
	cJSON *root = cJSON_Parse(raw);
	free(raw);
	assert(root != NULL);
	return root;
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	const char *cfg_path = argv[1];

	/* 1. Seed disk config with per-game scales AND legacy global scales. */
	const char *seed =
		"{\"lock_mask\":0,\"disable_streamer_mode\":false,"
		"\"font_scale_pct\":150,\"scrollbar_scale_pct\":260,"
		"\"game_font_scale_pct\":{\"Rance10\":135,\"DohnaDohna\":85},"
		"\"game_scrollbar_scale_pct\":{\"Rance10\":210}}";
	assert(write_config_atomic_unique(cfg_path, seed, strlen(seed), 1));

	/* 2. Rance10 loads its own per-game scales. */
	reset_state(cfg_path, "Rance10");
	maybe_load_initial_modifier_config();
	assert(g_font_scale_pct == 135 && stub_parts_font == 135);
	assert(g_scrollbar_scale_pct == 210 && stub_parts_sb == 210);

	/* 3. DohnaDohna loads its own font scale and default 100 scrollbar (NOT legacy 260!). */
	reset_state(cfg_path, "DohnaDohna");
	maybe_load_initial_modifier_config();
	assert(g_font_scale_pct == 85 && stub_parts_font == 85);
	assert(g_scrollbar_scale_pct == 100 && stub_parts_sb == 100);

	/* 4. Unconfigured Evenicle gets 100/100 (legacy global keys must NOT leak!). */
	reset_state(cfg_path, "Evenicle");
	maybe_load_initial_modifier_config();
	assert(g_font_scale_pct == 100 && stub_parts_font == 100);
	assert(g_scrollbar_scale_pct == 100 && stub_parts_sb == 100);

	/* 5. NULL game key gets 100/100 (legacy global keys must NOT leak!). */
	reset_state(cfg_path, NULL);
	maybe_load_initial_modifier_config();
	assert(g_font_scale_pct == 100 && stub_parts_font == 100);
	assert(g_scrollbar_scale_pct == 100 && stub_parts_sb == 100);

	/* 6. Persisting Rance10 scale updates only Rance10, keeps DohnaDohna, strips legacy global keys. */
	reset_state(cfg_path, "Rance10");
	g_has_ui_scale_init = true;
	g_font_scale_pct = 120;
	g_scrollbar_scale_pct = 175;
	persist_modifier_config_from_native(0);

	cJSON *root = read_json_file(cfg_path);
	assert(cJSON_GetObjectItem(root, "font_scale_pct") == NULL);
	assert(cJSON_GetObjectItem(root, "scrollbar_scale_pct") == NULL);
	assert(cjson_get_per_game_scale(root, "game_font_scale_pct", "Rance10", 100, clamp_font_scale_pct) == 120);
	assert(cjson_get_per_game_scale(root, "game_font_scale_pct", "DohnaDohna", 100, clamp_font_scale_pct) == 85);
	assert(cjson_get_per_game_scale(root, "game_scrollbar_scale_pct", "Rance10", 100, clamp_scrollbar_scale_pct) == 175);
	assert(cjson_get_per_game_scale(root, "game_scrollbar_scale_pct", "DohnaDohna", 100, clamp_scrollbar_scale_pct) == 100);
	cJSON_Delete(root);

	/* 7. Resetting Rance10 back to 100/100 removes only Rance10 entries and keeps DohnaDohna. */
	g_font_scale_pct = 100;
	g_scrollbar_scale_pct = 100;
	persist_modifier_config_from_native(0);

	root = read_json_file(cfg_path);
	cJSON *fmap = cJSON_GetObjectItemCaseSensitive(root, "game_font_scale_pct");
	cJSON *smap = cJSON_GetObjectItemCaseSensitive(root, "game_scrollbar_scale_pct");
	assert(fmap && cJSON_GetObjectItemCaseSensitive(fmap, "Rance10") == NULL);
	assert(fmap && cJSON_GetObjectItemCaseSensitive(fmap, "DohnaDohna")->valueint == 85);
	assert(smap && cJSON_GetObjectItemCaseSensitive(smap, "Rance10") == NULL);
	cJSON_Delete(root);

	/* 8. Persisting lock change with NULL game key preserves per-game UI scale maps untouched. */
	reset_state(cfg_path, NULL);
	persist_modifier_config_from_native(1);
	root = read_json_file(cfg_path);
	assert(cJSON_GetObjectItem(root, "font_scale_pct") == NULL);
	assert(cJSON_GetObjectItem(root, "scrollbar_scale_pct") == NULL);
	assert(cjson_get_per_game_scale(root, "game_font_scale_pct", "DohnaDohna", 100, clamp_font_scale_pct) == 85);
	cJSON_Delete(root);

	puts("ok");
	return 0;
}
"""


class PerGameUiScaleNativeTest(unittest.TestCase):
    def test_per_game_ui_scale_load_and_persist(self):
        funcs = "\n\n".join([
            extract_function(MODIFIER, "clamp_font_scale_pct"),
            extract_function(MODIFIER, "clamp_scrollbar_scale_pct"),
            extract_function(MODIFIER, "cjson_get_per_game_scale"),
            extract_function(MODIFIER, "cjson_set_per_game_scale"),
            extract_function(MODIFIER, "persist_modifier_config_from_native"),
            extract_function(MODIFIER, "maybe_load_initial_modifier_config"),
        ])
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "test_per_game_ui_scale.c"
            exe = Path(tmp) / "test_per_game_ui_scale"
            cfg = Path(tmp) / "modifier_config.json"
            src.write_text(HARNESS.replace("@FUNCS@", funcs), encoding="utf-8")
            subprocess.run(
                [
                    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-function",
                    f"-I{ROOT / 'include'}",
                    str(src),
                    str(ROOT / "src" / "cJSON.c"),
                    "-pthread",
                    "-o", str(exe),
                ],
                check=True,
            )
            out = subprocess.run([str(exe), str(cfg)], check=True, capture_output=True, text=True)
            self.assertEqual(out.stdout.strip(), "ok")


if __name__ == "__main__":
    unittest.main()
