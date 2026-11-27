#!/usr/bin/env python3
"""Native regression tests for keys the modifier holds down on the game's behalf.

The Rance 10 日不落 MOD opens its cheat prompt only while key 0 is down.
The floating modifier's Cheat Code / Open MOD Cheat Console hold VK_0 from
modifier_on_frame(), but rebuild_effective_keys() recomputes key_state for
every non-mouse key from keyboard/gamepad state on each poll, which silently
dropped a direct key_state[VK_0] write. Injected keys must survive that rebuild.

Run: python3 xsystem4/test/native/test_modifier_injected_key_regressions.py
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


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
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


HARNESS = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
enum sact_keycode { VK_LBUTTON = 1, VK_RBUTTON = 2, VK_MBUTTON = 4, VK_CONTROL = 17, VK_0 = 48, VK_NR_KEYCODES = 256 };
bool key_state[VK_NR_KEYCODES];
static bool kbd_key_state[VK_NR_KEYCODES];
static bool vpad_vk_down[VK_NR_KEYCODES];
static bool pad_vk_down[VK_NR_KEYCODES];
static bool injected_vk_down[VK_NR_KEYCODES];
static bool sdl_mouse_down[VK_NR_KEYCODES];
static bool prev_gamepad_mouse[VK_NR_KEYCODES];
static bool deferred_mouse_release[VK_NR_KEYCODES];
static bool mouse_down_unobserved[VK_NR_KEYCODES];
static uint32_t mouse_down_timestamp[VK_NR_KEYCODES];
static bool suppressed_movie_keys[VK_NR_KEYCODES];
static bool vpad_cursor_btn_down;
static atomic_bool touch_ctrl_skip;
static bool long_touch_ctrl_down;
static bool movie_has_active_playback(void) { return false; }
static bool is_movie_skip_suppressed_key(int code) { (void)code; return false; }
%(funcs)s
#define CHECK(c) do { if (!(c)) { printf("FAIL %%s\n", #c); return 1; } } while (0)
int main(void) {
    key_set_injected(VK_0, true);
    CHECK(key_state[VK_0]);
    rebuild_effective_keys(100);
    CHECK(key_state[VK_0]);
    key_set_injected(VK_0, false);
    CHECK(!key_state[VK_0]);
    rebuild_effective_keys(200);
    CHECK(!key_state[VK_0]);
    kbd_key_state[VK_0] = true;
    key_set_injected(VK_0, false);
    CHECK(key_state[VK_0]);
    rebuild_effective_keys(300);
    CHECK(key_state[VK_0]);
    puts("OK");
    return 0;
}
"""


class InjectedKeyTests(unittest.TestCase):
    def test_injected_key_survives_rebuild(self):
        funcs = "\n".join(
            extract_function("src/input.c", n)
            for n in ("key_set_injected", "rebuild_effective_keys")
        )
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "t.c"
            exe = Path(tmp) / "t"
            src.write_text(HARNESS % {"funcs": funcs}, encoding="utf-8")
            subprocess.run(["cc", "-std=gnu11", "-o", str(exe), str(src)], check=True)
            out = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stdout + out.stderr)

    def test_ctrl_recompositions_include_injected_hold(self):
        for name in ("sync_ctrl_key_state", "key_is_down"):
            body = extract_function("src/input.c", name)
            self.assertIn("injected_vk_down[VK_CONTROL]", body, name)

    def test_modifier_does_not_write_key_state_directly(self):
        source = (ROOT / "src/modifier.c").read_text(encoding="utf-8")
        self.assertNotRegex(source, r"\bkey_state\s*\[")
        self.assertIn("key_set_injected(VK_0, true)", source)


if __name__ == "__main__":
    unittest.main()
