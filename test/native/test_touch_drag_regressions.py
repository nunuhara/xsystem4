#!/usr/bin/env python3
"""Dependency-free native regression tests for Android synthetic touch-drag input.

Compiles the actual C implementations from xsystem4/src/input.c against minimal
SDL/input fixtures to verify:
1. Non-PartsEngine polling loops (e.g., Rance 9 pre-battle party drag using
   IbisInputEngine/SACT2 Key_IsDown + Mouse_GetPos while PE_Update runs without
   BeginInput) mark synthetic touch presses as observed so mouse_get_pos()
   tracks finger motion instead of freezing at touchdown coordinates.
2. Quick touch drags whose lift occurs before the next frame or before the 50ms
   synthetic press delay expires still expose the final release coordinates
   while VK_LBUTTON/VK_RBUTTON remains pressed prior to release.
3. PartsEngine.BeginInput (allow_press=true) quick taps and drags remain
   unchanged (hover -> press -> release).
4. Pure non-Parts loops without PE_UpdateInputState and RBUTTON/multi-touch reset
   paths preserve correct synthetic cursor tracking.
5. Disabling Parts input releases observed touches promptly without enabling new
   presses or consuming an unobserved press.
6. Event-order touch finger tracking clears multi_touch_active on SDL_FINGERUP
   even under same-frame races so drags immediately after 2-finger taps/scrolls
   work cleanly.
7. Sengoku Rance tagMapControl@checkSelect + tagSansenList@dragBusho loops
   handle normal drags, rapid consecutive drags (<100ms apart), and sub-50ms
   quick flicks with prompt release and preserved drop coordinates.
8. Edge-clamped drags calling mouse_set_pos() keep synthetic touch mode intact
   without SDL_WarpMouseInWindow while real mouse warps continue normally.
9. Non-Parts quick single taps (<50ms) and unobserved button holds preserve
   fallback timeouts when key_is_down() is not polled yet.

Run: python3 xsystem4/test/native/test_touch_drag_regressions.py
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
    start = match.start()
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def compile_and_run(c_source: str) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "test.c"
        exe = Path(tmp) / "test_bin"
        src.write_text(c_source, encoding="utf-8")
        subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", str(src), "-o", str(exe)],
            check=True,
        )
        subprocess.run([str(exe)], check=True)


def build_input_harness(body: str) -> str:
    fns = "\n\n".join([
        extract_function("src/input.c", "sync_gamepad_cursor_norm_from_window"),
        extract_function("src/input.c", "sync_gamepad_cursor_from_window"),
        extract_function("src/input.c", "sync_ctrl_key_state"),
        extract_function("src/input.c", "input_cancel_long_touch"),
        extract_function("src/input.c", "release_long_touch_ctrl"),
        extract_function("src/input.c", "input_parts_poll_begin"),
        extract_function("src/input.c", "input_notify_parts_poll"),
        extract_function("src/input.c", "clear_active_touch_fingers"),
        extract_function("src/input.c", "track_touch_finger_down"),
        extract_function("src/input.c", "track_touch_finger_up"),
        extract_function("src/input.c", "key_is_down"),
        extract_function("src/input.c", "key_clear_flag"),
        extract_function("src/input.c", "mouse_get_pos"),
        extract_function("src/input.c", "mouse_set_pos"),
        extract_function("src/input.c", "synthetic_mouse_event"),
        extract_function("src/input.c", "pop_deferred_synthetic_mouse_event"),
        extract_function("src/input.c", "fire_deferred_events"),
        extract_function("src/input.c", "long_touch_right_button_down"),
        extract_function("src/input.c", "cancel_long_touch_click"),
        extract_function("src/input.c", "update_long_touch_ctrl"),
        extract_function("src/input.c", "enqueue_synthetic_mouse_button"),
        extract_function("src/input.c", "handle_touch_mouse_button"),
        extract_function("src/input.c", "handle_touch_finger_event"),
    ])
    return f"""
#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

enum sact_keycode {{
    VK_LBUTTON = 0x01,
    VK_RBUTTON = 0x02,
    VK_MBUTTON = 0x04,
    VK_CONTROL = 0x11,
    VK_NR_KEYCODES = 256,
}};

enum {{
    SDL_RELEASED = 0,
    SDL_PRESSED = 1,
    SDL_BUTTON_LEFT = 1,
    SDL_BUTTON_MIDDLE = 2,
    SDL_BUTTON_RIGHT = 3,
    SDL_MOUSEBUTTONDOWN = 0x401,
    SDL_MOUSEBUTTONUP = 0x402,
    SDL_FINGERDOWN = 0x700,
    SDL_FINGERUP = 0x701,
    SDL_TOUCH_MOUSEID = 0xFFFFFFFFu,
}};

typedef int64_t SDL_TouchID;
typedef int64_t SDL_FingerID;
typedef struct {{ int x, y; }} SDL_Point;
typedef struct {{ int x, y, w, h; }} SDL_Rect;
typedef struct {{ float x, y, w, h; }} SDL_FRect;
typedef struct {{
    uint32_t type;
    uint32_t timestamp;
    uint32_t which;
    uint8_t button;
    uint8_t state;
    uint8_t clicks;
    int x;
    int y;
}} SDL_MouseButtonEvent;
typedef struct {{
    uint32_t type;
    uint32_t timestamp;
    SDL_TouchID touchId;
    SDL_FingerID fingerId;
    float x;
    float y;
    float dx;
    float dy;
    float pressure;
}} SDL_TouchFingerEvent;

struct sdl_private {{
    void *window;
    int w, h;
    SDL_Rect viewport;
}};

static struct sdl_private sdl = {{
    .window = (void *)0x1,
    .w = 1280,
    .h = 720,
    .viewport = {{ 0, 0, 1280, 720 }},
}};

#define MAYBE_UNUSED __attribute__((unused))

static uint32_t fake_ticks = 1000;
static int fake_sdl_mouse_x = 0;
static int fake_sdl_mouse_y = 0;
static int fake_num_touch_fingers = 0;
MAYBE_UNUSED static int warp_calls = 0;

MAYBE_UNUSED static uint32_t SDL_GetTicks(void) {{ return fake_ticks; }}
MAYBE_UNUSED static void SDL_PumpEvents(void) {{}}
MAYBE_UNUSED static uint32_t SDL_GetMouseState(int *x, int *y) {{
    if (x) *x = fake_sdl_mouse_x;
    if (y) *y = fake_sdl_mouse_y;
    return 0;
}}
static void SDL_WarpMouseInWindow(void *window, int x, int y) {{
    (void)window;
    warp_calls++;
    fake_sdl_mouse_x = x;
    fake_sdl_mouse_y = y;
}}
static bool SDL_PointInRect(const SDL_Point *p, const SDL_Rect *r) {{
    return p->x >= r->x && p->x < r->x + r->w && p->y >= r->y && p->y < r->y + r->h;
}}
static int SDL_GetNumTouchFingers(SDL_TouchID touchId) {{
    (void)touchId;
    return fake_num_touch_fingers;
}}

static bool key_state[VK_NR_KEYCODES];
static bool kbd_key_state[VK_NR_KEYCODES];
static bool vpad_vk_down[VK_NR_KEYCODES];
static bool pad_vk_down[VK_NR_KEYCODES];
static bool injected_vk_down[VK_NR_KEYCODES];
static bool sdl_mouse_down[VK_NR_KEYCODES];
static bool prev_gamepad_mouse[VK_NR_KEYCODES];
static bool suppressed_movie_keys[VK_NR_KEYCODES];
static bool kbd_ctrl_down = false;
static atomic_bool touch_ctrl_skip = false;
static atomic_bool long_press_ctrl_enabled = false;
static bool long_touch_armed;
static uint32_t long_touch_start_tick;
static SDL_FRect long_touch_finger_rect;
static bool long_touch_ctrl_down;
static uint64_t next_touch_press_id;
static uint64_t touch_press_id;
static uint64_t long_touch_click_cancel;
#define LONG_TOUCH_DURATION 1000
static bool gamepad_cursor_active = false;
static bool gamepad_cursor_visible = false;
static float gamepad_cursor_norm_x = 0.5f;
static float gamepad_cursor_norm_y = 0.5f;
static bool vpad_cursor_btn_down = false;
static bool single_touch_down = false;
static SDL_FingerID single_touch_finger_id = 0;
static float single_touch_start_x = 0.0f;
static float single_touch_start_y = 0.0f;
static uint32_t single_touch_down_tick = 0;
static bool movie_touch_hold_eligible = false;
static bool suppress_touch_until_release = false;
static uint32_t movie_hold_token = 0;
static bool movie_early_release_hint = false;
static float scroll_gesture_y = -1.0f;

static bool movie_has_active_playback(void) {{ return false; }}
static bool is_movie_skip_suppressed_key(enum sact_keycode code) {{ (void)code; return false; }}

#define MAX_DEFERRED_SYNTHETIC_MOUSE_EVENTS 32
static SDL_MouseButtonEvent deferred_synthetic_mouse_events[MAX_DEFERRED_SYNTHETIC_MOUSE_EVENTS];
static int nr_deferred_synthetic_mouse_events = 0;
static bool synthetic_mouse_hover_observed = false;
static uint32_t synthetic_mouse_hover_tick = 0;
static bool synthetic_mouse_down_observed = false;
static uint32_t synthetic_mouse_down_observed_tick = 0;
static bool synthetic_mouse_up_observed = true;
static bool synthetic_mouse_release_pos_observed = false;
static int synthetic_mouse_release_pos_obs_count = 0;
static bool synthetic_mouse_release_queued_before_down_obs = false;
static uint32_t synthetic_mouse_release_pos_tick = 0;
static int synthetic_mouse_press_x = 0;
static int synthetic_mouse_press_y = 0;
static bool has_synthetic_mouse_pos = false;
static int synthetic_mouse_x = 0;
static int synthetic_mouse_y = 0;
static bool synthetic_mouse_warped = false;
static int synthetic_mouse_warp_raw_x = 0;
static int synthetic_mouse_warp_raw_y = 0;
static bool has_last_touch_down_pos = false;
static SDL_Point last_touch_down_pos = {{0, 0}};
static uint32_t last_parts_poll_timestamp = 0;
static bool in_parts_poll = false;
static bool mouse_down_unobserved[VK_NR_KEYCODES];
static uint32_t mouse_down_timestamp[VK_NR_KEYCODES];
static bool deferred_mouse_release[VK_NR_KEYCODES];
#define DEFERRED_MOUSE_RELEASE_MAX_DELAY 100
#define SYNTHETIC_MOUSE_EVENT_DELAY 50
#define SYNTHETIC_MOUSE_HOVER_MIN_DELAY 16
#define SYNTHETIC_MOUSE_RELEASE_DROP_MIN_DELAY 16
#define SYNTHETIC_MOUSE_RELEASE_DOWN_MIN_DELAY 50
#define SYNTHETIC_MOUSE_RELEASE_OBSERVED_NO_POS_DELAY 80
#define SYNTHETIC_MOUSE_RELEASE_FALLBACK_DELAY 250

#define MAX_ACTIVE_TOUCH_FINGERS 16
struct active_touch_finger {{
    SDL_TouchID touch_id;
    SDL_FingerID finger_id;
}};
static struct active_touch_finger active_touch_fingers[MAX_ACTIVE_TOUCH_FINGERS];
static int nr_active_touch_fingers = 0;
static bool multi_touch_active = false;
static bool two_finger_tap_candidate = false;
static bool two_finger_scrolled = false;
static uint32_t two_finger_tap_start_tick = 0;
#define TWO_FINGER_TAP_MAX_MS 300

static void fire_deferred_events(bool from_parts_poll, bool allow_press);
static void update_long_touch_ctrl(void);

static void handle_events(void) {{
    static const enum sact_keycode mouse_btns[] = {{ VK_LBUTTON, VK_RBUTTON, VK_MBUTTON }};
    uint32_t poll_now = SDL_GetTicks();
    for (size_t i = 0; i < sizeof(mouse_btns) / sizeof(mouse_btns[0]); i++) {{
        enum sact_keycode code = mouse_btns[i];
        if (deferred_mouse_release[code] &&
            (!mouse_down_unobserved[code] || (poll_now - mouse_down_timestamp[code]) >= DEFERRED_MOUSE_RELEASE_MAX_DELAY)) {{
            key_state[code] = false;
            deferred_mouse_release[code] = false;
            mouse_down_unobserved[code] = false;
        }}
    }}
    fire_deferred_events(false, false);
    update_long_touch_ctrl();
}}

{fns}

MAYBE_UNUSED static void sim_touch_down_btn(int x, int y, uint8_t button) {{
    fake_sdl_mouse_x = x;
    fake_sdl_mouse_y = y;
    SDL_MouseButtonEvent ev = {{
        .type = SDL_MOUSEBUTTONDOWN,
        .which = SDL_TOUCH_MOUSEID,
        .button = button,
        .state = SDL_PRESSED,
        .clicks = 1,
        .x = x,
        .y = y,
    }};
    handle_touch_mouse_button(&ev);
}}

MAYBE_UNUSED static void sim_touch_down(int x, int y) {{
    sim_touch_down_btn(x, y, SDL_BUTTON_LEFT);
}}

MAYBE_UNUSED static void sim_touch_move(int x, int y) {{
    fake_sdl_mouse_x = x;
    fake_sdl_mouse_y = y;
}}

MAYBE_UNUSED static void sim_touch_up_btn(int x, int y, uint8_t button) {{
    fake_sdl_mouse_x = x;
    fake_sdl_mouse_y = y;
    SDL_MouseButtonEvent ev = {{
        .type = SDL_MOUSEBUTTONUP,
        .which = SDL_TOUCH_MOUSEID,
        .button = button,
        .state = SDL_RELEASED,
        .clicks = 1,
        .x = x,
        .y = y,
    }};
    handle_touch_mouse_button(&ev);
}}

MAYBE_UNUSED static void sim_touch_up(int x, int y) {{
    sim_touch_up_btn(x, y, SDL_BUTTON_LEFT);
}}

MAYBE_UNUSED static void sim_finger_event(SDL_TouchID touch_id, SDL_FingerID finger_id,
                                          float x, float y, bool down) {{
    SDL_TouchFingerEvent tf = {{
        .type = down ? SDL_FINGERDOWN : SDL_FINGERUP,
        .timestamp = fake_ticks,
        .touchId = touch_id,
        .fingerId = finger_id,
        .x = x,
        .y = y,
        .dx = 0.0f,
        .dy = 0.0f,
        .pressure = down ? 1.0f : 0.0f,
    }};
    handle_touch_finger_event(&tf, down);
}}

MAYBE_UNUSED static void sim_pe_update(bool allow_press, int *out_x, int *out_y, bool *out_clicking) {{
    input_parts_poll_begin(allow_press);
    int x = 0, y = 0;
    mouse_get_pos(&x, &y);
    bool clicking = key_is_down(VK_LBUTTON) || key_is_down(VK_RBUTTON);
    input_notify_parts_poll(allow_press, clicking);
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    if (out_clicking) *out_clicking = clicking;
}}

static void reset_test_state(void) {{
    key_clear_flag(false);
    nr_deferred_synthetic_mouse_events = 0;
    synthetic_mouse_hover_observed = false;
    synthetic_mouse_hover_tick = 0;
    synthetic_mouse_down_observed = false;
    synthetic_mouse_down_observed_tick = 0;
    synthetic_mouse_up_observed = true;
    synthetic_mouse_release_pos_observed = false;
    synthetic_mouse_release_pos_obs_count = 0;
    synthetic_mouse_release_queued_before_down_obs = false;
    synthetic_mouse_release_pos_tick = 0;
    synthetic_mouse_press_x = 0;
    synthetic_mouse_press_y = 0;
    has_synthetic_mouse_pos = false;
    synthetic_mouse_x = 0;
    synthetic_mouse_y = 0;
    synthetic_mouse_warped = false;
    synthetic_mouse_warp_raw_x = 0;
    synthetic_mouse_warp_raw_y = 0;
    has_last_touch_down_pos = false;
    last_parts_poll_timestamp = 0;
    in_parts_poll = false;
    gamepad_cursor_active = false;
    gamepad_cursor_visible = false;
    gamepad_cursor_norm_x = 0.5f;
    gamepad_cursor_norm_y = 0.5f;
    vpad_cursor_btn_down = false;
    clear_active_touch_fingers();
    fake_num_touch_fingers = 0;
    multi_touch_active = false;
    two_finger_tap_candidate = false;
    two_finger_scrolled = false;
    two_finger_tap_start_tick = 0;
    single_touch_down = false;
    single_touch_finger_id = 0;
    single_touch_start_x = 0.0f;
    single_touch_start_y = 0.0f;
    single_touch_down_tick = 0;
    movie_touch_hold_eligible = false;
    suppress_touch_until_release = false;
    movie_hold_token = 0;
    movie_early_release_hint = false;
    scroll_gesture_y = -1.0f;
    warp_calls = 0;
    fake_ticks += 2000;
}}

int main(void) {{
{body}
    return 0;
}}
"""


class TouchDragRegressionTests(unittest.TestCase):
    def test_rance9_party_drag_tracks_pointer_with_pe_update_without_begin_input(self) -> None:
        c_code = build_input_harness("""
    reset_test_state();

    // Idle frames with PE_Update(allow_press=false), matching Rance 9 T_Party.
    for (int i = 0; i < 3; i++) {
        fake_ticks += 16;
        sim_pe_update(false, NULL, NULL, NULL);
        (void)key_is_down(VK_LBUTTON);
    }

    sim_touch_down(300, 500);
    bool pressed = false;
    for (int f = 0; f < 6; f++) {
        fake_ticks += 16;
        sim_pe_update(false, NULL, NULL, NULL);
        if (key_is_down(VK_LBUTTON)) {
            pressed = true;
            break;
        }
    }
    assert(pressed);

    // T_DragMouse@start reads mouse_get_pos without calling mouse_set_pos.
    int sx = 0, sy = 0;
    mouse_get_pos(&sx, &sy);
    assert(sx == 300 && sy == 500);

    // Drag across multiple frames while finger stays down.
    const int pts[3][2] = { {450, 420}, {600, 350}, {700, 300} };
    for (int i = 0; i < 3; i++) {
        fake_ticks += 16;
        sim_touch_move(pts[i][0], pts[i][1]);
        sim_pe_update(false, NULL, NULL, NULL);
        int mx = 0, my = 0;
        mouse_get_pos(&mx, &my);
        assert(key_is_down(VK_LBUTTON));
        assert(mx == pts[i][0] && my == pts[i][1]);
    }

    sim_touch_up(700, 300);
    bool released = false;
    for (int f = 0; f < 25; f++) {
        fake_ticks += 16;
        sim_pe_update(false, NULL, NULL, NULL);
        if (!key_is_down(VK_LBUTTON)) {
            released = true;
            break;
        }
    }
    assert(released);
""")
        compile_and_run(c_code)

    def test_rance9_quick_drag_and_sub50ms_swipe_expose_drop_coords_while_pressed(self) -> None:
        c_code = build_input_harness("""
    // Case A: Final move + lift arrive together between frames during an active drag.
    reset_test_state();
    sim_touch_down(300, 500);
    for (int f = 0; f < 6; f++) {
        fake_ticks += 16;
        sim_pe_update(false, NULL, NULL, NULL);
        if (key_is_down(VK_LBUTTON))
            break;
    }
    assert(key_is_down(VK_LBUTTON));

    fake_ticks += 16;
    sim_touch_move(520, 380);
    sim_pe_update(false, NULL, NULL, NULL);
    int mx = 0, my = 0;
    mouse_get_pos(&mx, &my);
    assert(mx == 520 && my == 380);
    (void)key_is_down(VK_LBUTTON);

    fake_ticks += 16;
    sim_touch_move(700, 300);
    sim_touch_up(700, 300);
    sim_pe_update(false, NULL, NULL, NULL);
    // While still pressed on the drag frame before fallback release pops, coordinates reach (700, 300).
    mouse_get_pos(&mx, &my);
    assert(key_is_down(VK_LBUTTON));
    assert(mx == 700 && my == 300);
    fake_ticks += 260;
    sim_pe_update(false, NULL, NULL, NULL);
    assert(!key_is_down(VK_LBUTTON));

    // Case B: Ultra-fast <50ms swipe + lift before deferred PRESSED fires.
    reset_test_state();
    sim_touch_down(300, 500);
    fake_ticks += 20;
    sim_touch_move(700, 300);
    sim_touch_up(700, 300);
    sim_pe_update(false, NULL, NULL, NULL);
    assert(!key_is_down(VK_LBUTTON));

    fake_ticks += 40;
    sim_pe_update(false, NULL, NULL, NULL);
    assert(key_is_down(VK_LBUTTON));
    mouse_get_pos(&mx, &my);
    assert(mx == 700 && my == 300);
    fake_ticks += 260;
    sim_pe_update(false, NULL, NULL, NULL);
    assert(!key_is_down(VK_LBUTTON));
""")
        compile_and_run(c_code)

    def test_non_parts_loop_without_pe_update_tracks_drag_and_fast_lift(self) -> None:
        c_code = build_input_harness("""
    reset_test_state();
    sim_touch_down(200, 400);
    fake_ticks += 60;
    handle_events();
    assert(key_is_down(VK_LBUTTON));

    fake_ticks += 16;
    sim_touch_move(500, 250);
    handle_events();
    int mx = 0, my = 0;
    mouse_get_pos(&mx, &my);
    assert(key_is_down(VK_LBUTTON));
    assert(mx == 500 && my == 250);

    // Final move + lift in fallback delay window (<250ms): pos still updates while held.
    fake_ticks += 16;
    sim_touch_move(640, 220);
    sim_touch_up(640, 220);
    handle_events();
    mouse_get_pos(&mx, &my);
    assert(key_is_down(VK_LBUTTON));
    assert(mx == 640 && my == 220);

    fake_ticks += 260;
    handle_events();
    assert(!key_is_down(VK_LBUTTON));
""")
        compile_and_run(c_code)

    def test_parts_engine_begin_input_quick_tap_and_drag_preserved(self) -> None:
        c_code = build_input_harness("""
    // Quick <50ms tap with BeginInput=true must deliver hover -> press -> release across 3 frames.
    reset_test_state();
    sim_touch_down(400, 300);
    fake_ticks += 16;
    sim_touch_up(404, 302); // snaps back to (400, 300)

    int x1 = 0, y1 = 0;
    bool c1 = false;
    sim_pe_update(true, &x1, &y1, &c1);
    assert(!c1 && x1 == 400 && y1 == 300);

    int x2 = 0, y2 = 0;
    bool c2 = false;
    fake_ticks += 16;
    sim_pe_update(true, &x2, &y2, &c2);
    assert(c2 && x2 == 400 && y2 == 300);

    bool c3 = true;
    fake_ticks += 16;
    sim_pe_update(true, NULL, NULL, &c3);
    assert(!c3);

    // Touch drag with BeginInput=true also tracks finger motion while held.
    reset_test_state();
    sim_touch_down(150, 180);
    fake_ticks += 16;
    sim_pe_update(true, &x1, &y1, &c1);
    assert(!c1 && x1 == 150 && y1 == 180);
    fake_ticks += 16;
    sim_pe_update(true, &x2, &y2, &c2);
    assert(c2);
    fake_ticks += 16;
    sim_touch_move(510, 420);
    sim_pe_update(true, &x2, &y2, &c2);
    assert(c2 && x2 == 510 && y2 == 420);
    sim_touch_up(510, 420);
    fake_ticks += 16;
    sim_pe_update(true, &x2, &y2, &c2);
    assert(!c2);
""")
        compile_and_run(c_code)

    def test_parts_input_disabled_releases_observed_touches_promptly(self) -> None:
        c_code = build_input_harness("""
    const uint8_t buttons[] = { SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT };
    const enum sact_keycode keys[] = { VK_LBUTTON, VK_RBUTTON };
    for (int b = 0; b < 2; b++) {
        for (int moved = 0; moved < 2; moved++) {
            reset_test_state();
            sim_touch_down_btn(300, 500, buttons[b]);
            fake_ticks += 16;
            sim_pe_update(true, NULL, NULL, NULL); // hover
            fake_ticks += 16;
            sim_pe_update(true, NULL, NULL, NULL); // observed press
            assert(key_is_down(keys[b]));
            assert(synthetic_mouse_down_observed);

            // EndInput disables new presses, not release of this observed press.
            int drop_x = moved ? 700 : 300;
            int drop_y = moved ? 300 : 500;
            sim_touch_up_btn(drop_x, drop_y, buttons[b]);
            uint32_t lifted_at = fake_ticks;
            fake_ticks += 16;
            int x = 0, y = 0;
            sim_pe_update(false, &x, &y, NULL);
            assert(x == drop_x && y == drop_y);
            if (moved) {
                // Preserve one pressed frame at the final drag coordinates.
                assert(key_is_down(keys[b]));
                fake_ticks += 16;
                sim_pe_update(false, &x, &y, NULL);
            }
            assert(!key_is_down(keys[b]));
            assert(nr_deferred_synthetic_mouse_events == 0);
            assert(fake_ticks - lifted_at <= 32);
            assert(fake_ticks - last_parts_poll_timestamp < 500);

            // A queued new touch must still not become pressed while disabled.
            sim_touch_down_btn(120, 180, buttons[b]);
            for (int f = 0; f < 4; f++) {
                fake_ticks += 16;
                sim_pe_update(false, NULL, NULL, NULL);
                assert(!key_is_down(keys[b]));
                assert(nr_deferred_synthetic_mouse_events == 1);
            }
            // Re-enabling still follows hover -> press, with no stale release.
            sim_pe_update(true, NULL, NULL, NULL);
            assert(!key_is_down(keys[b]));
            fake_ticks += 16;
            sim_pe_update(true, NULL, NULL, NULL);
            assert(key_is_down(keys[b]));
        }
    }
""")
        compile_and_run(c_code)

    def test_parts_input_disabled_does_not_release_an_unobserved_press(self) -> None:
        c_code = build_input_harness("""
    reset_test_state();
    sim_touch_down(300, 500);
    fake_ticks += 16;
    sim_pe_update(true, NULL, NULL, NULL); // hover
    fake_ticks += 16;
    input_parts_poll_begin(true); // press delivered, not yet observed by caller
    assert(key_state[VK_LBUTTON]);
    assert(!synthetic_mouse_down_observed);
    input_notify_parts_poll(false, true);
    sim_touch_up(300, 500);
    for (int f = 0; f < 3; f++) {
        fake_ticks += 16;
        sim_pe_update(false, NULL, NULL, NULL);
        assert(key_state[VK_LBUTTON]);
        assert(!synthetic_mouse_down_observed);
        assert(nr_deferred_synthetic_mouse_events == 1);
    }
    // An enabled poll observes the press before the next poll releases it.
    sim_pe_update(true, NULL, NULL, NULL);
    assert(synthetic_mouse_down_observed);
    fake_ticks += 16;
    sim_pe_update(false, NULL, NULL, NULL);
    assert(!key_state[VK_LBUTTON]);
    assert(nr_deferred_synthetic_mouse_events == 0);
""")
        compile_and_run(c_code)

    def test_rbutton_drag_and_multitouch_cancel_reset(self) -> None:
        c_code = build_input_harness("""
    reset_test_state();
    sim_touch_down_btn(320, 240, SDL_BUTTON_RIGHT);
    fake_ticks += 60;
    sim_pe_update(false, NULL, NULL, NULL);
    assert(key_is_down(VK_RBUTTON));

    fake_ticks += 16;
    sim_touch_move(480, 360);
    sim_pe_update(false, NULL, NULL, NULL);
    int mx = 0, my = 0;
    mouse_get_pos(&mx, &my);
    assert(key_is_down(VK_RBUTTON));
    assert(mx == 480 && my == 360);

    // Simulate multi-touch gesture cancelling active synthetic touch state.
    key_state[VK_LBUTTON] = false;
    key_state[VK_RBUTTON] = false;
    nr_deferred_synthetic_mouse_events = 0;
    synthetic_mouse_hover_observed = false;
    synthetic_mouse_down_observed = false;
    synthetic_mouse_up_observed = true;
    has_last_touch_down_pos = false;

    // Subsequent single-finger drag starts cleanly with fresh observation.
    sim_touch_down(100, 120);
    fake_ticks += 60;
    sim_pe_update(false, NULL, NULL, NULL);
    assert(key_is_down(VK_LBUTTON));
    fake_ticks += 16;
    sim_touch_move(260, 290);
    sim_pe_update(false, NULL, NULL, NULL);
    mouse_get_pos(&mx, &my);
    assert(mx == 260 && my == 290);
""")
        compile_and_run(c_code)

    def test_touch_drag_immediately_after_two_finger_tap_and_scroll(self) -> None:
        c_code = build_input_harness("""
    // Case 1: 2-finger tap (right-click) followed immediately by a single-finger drag,
    // including the same-frame race where SDL_GetNumTouchFingers() already sees the
    // next finger while SDL_FINGERUP is processed and SDL_MOUSEBUTTONDOWN arrives
    // before SDL_FINGERDOWN (SDL2's Android event order).
    reset_test_state();
    fake_num_touch_fingers = 1;
    sim_touch_down(400, 300);
    sim_finger_event(1, 101, 400.0f / 1280.0f, 300.0f / 720.0f, true);
    assert(nr_active_touch_fingers == 1 && !multi_touch_active);

    fake_ticks += 10;
    fake_num_touch_fingers = 2;
    sim_finger_event(1, 102, 500.0f / 1280.0f, 320.0f / 720.0f, true);
    assert(nr_active_touch_fingers == 2);
    assert(multi_touch_active);
    assert(two_finger_tap_candidate && !two_finger_scrolled);
    assert(nr_deferred_synthetic_mouse_events == 0);
    assert(!key_is_down(VK_LBUTTON));

    fake_ticks += 40;
    fake_num_touch_fingers = 1;
    sim_finger_event(1, 101, 400.0f / 1280.0f, 300.0f / 720.0f, false);
    assert(nr_active_touch_fingers == 1 && multi_touch_active);

    // Same-frame race: finger 103 is already touching down in SDL's live touch table,
    // so SDL_GetNumTouchFingers(1) returns 1 when finger 102's FINGERUP is processed.
    fake_num_touch_fingers = 1;
    sim_finger_event(1, 102, 500.0f / 1280.0f, 320.0f / 720.0f, false);
    assert(nr_active_touch_fingers == 0);
    assert(!multi_touch_active);
    assert(nr_deferred_synthetic_mouse_events == 2);
    assert(deferred_synthetic_mouse_events[0].button == SDL_BUTTON_RIGHT);
    assert(deferred_synthetic_mouse_events[0].state == SDL_PRESSED);
    assert(deferred_synthetic_mouse_events[1].button == SDL_BUTTON_RIGHT);
    assert(deferred_synthetic_mouse_events[1].state == SDL_RELEASED);

    // Next single-finger drag starts in the very same frame, with SDL_MOUSEBUTTONDOWN
    // arriving before SDL_FINGERDOWN.
    sim_touch_down(220, 480);
    assert(nr_deferred_synthetic_mouse_events == 3);
    sim_finger_event(1, 103, 220.0f / 1280.0f, 480.0f / 720.0f, true);
    assert(nr_active_touch_fingers == 1 && !multi_touch_active);

    bool saw_rbutton_down = false;
    bool saw_rbutton_up = false;
    bool saw_lbutton_down = false;
    int start_x = -1, start_y = -1;
    for (int f = 0; f < 25; f++) {
        fake_ticks += 16;
        handle_events();
        int mx = 0, my = 0;
        mouse_get_pos(&mx, &my);
        if (key_is_down(VK_RBUTTON)) {
            saw_rbutton_down = true;
        } else if (saw_rbutton_down && !saw_rbutton_up) {
            saw_rbutton_up = true;
        }
        if (key_is_down(VK_LBUTTON)) {
            saw_lbutton_down = true;
            start_x = mx;
            start_y = my;
            break;
        }
    }
    assert(saw_rbutton_down && saw_rbutton_up && saw_lbutton_down);
    assert(start_x == 220 && start_y == 480);

    // Drag tracks pointer cleanly and releases promptly.
    const int tap_drag_pts[2][2] = { {420, 390}, {690, 250} };
    for (int i = 0; i < 2; i++) {
        fake_ticks += 16;
        sim_touch_move(tap_drag_pts[i][0], tap_drag_pts[i][1]);
        handle_events();
        int mx = 0, my = 0;
        mouse_get_pos(&mx, &my);
        assert(key_is_down(VK_LBUTTON));
        assert(mx == tap_drag_pts[i][0] && my == tap_drag_pts[i][1]);
    }
    fake_ticks += 32;
    fake_num_touch_fingers = 0;
    sim_touch_up(690, 250);
    sim_finger_event(1, 103, 690.0f / 1280.0f, 250.0f / 720.0f, false);
    assert(nr_active_touch_fingers == 0);
    uint32_t tap_lift_tick = fake_ticks;
    bool released = false;
    for (int f = 0; f < 5; f++) {
        fake_ticks += 16;
        handle_events();
        int mx = 0, my = 0;
        mouse_get_pos(&mx, &my);
        if (!key_is_down(VK_LBUTTON)) {
            released = true;
            break;
        }
    }
    assert(released);
    assert(fake_ticks - tap_lift_tick <= 48);

    // Case 2: 2-finger scroll followed immediately by a single-finger drag under the
    // same-frame SDL_GetNumTouchFingers race (and MOUSEBUTTONDOWN before FINGERDOWN).
    reset_test_state();
    fake_num_touch_fingers = 1;
    sim_touch_down(360, 360);
    sim_finger_event(1, 201, 360.0f / 1280.0f, 360.0f / 720.0f, true);
    fake_ticks += 10;
    fake_num_touch_fingers = 2;
    sim_finger_event(1, 202, 460.0f / 1280.0f, 360.0f / 720.0f, true);
    assert(multi_touch_active);

    // SDL_MULTIGESTURE marks two-finger scroll and cancels right-click tap candidate.
    two_finger_scrolled = true;
    two_finger_tap_candidate = false;

    fake_ticks += 40;
    fake_num_touch_fingers = 1;
    sim_finger_event(1, 201, 360.0f / 1280.0f, 300.0f / 720.0f, false);
    assert(nr_active_touch_fingers == 1 && multi_touch_active);

    // Same-frame race: next single finger 203 is already in SDL's live touch table.
    fake_num_touch_fingers = 1;
    sim_finger_event(1, 202, 460.0f / 1280.0f, 300.0f / 720.0f, false);
    assert(nr_active_touch_fingers == 0);
    assert(!multi_touch_active);
    assert(nr_deferred_synthetic_mouse_events == 0);

    // MOUSEBUTTONDOWN arrives before FINGERDOWN for finger 203.
    sim_touch_down(300, 500);
    assert(nr_deferred_synthetic_mouse_events == 1);
    sim_finger_event(1, 203, 300.0f / 1280.0f, 500.0f / 720.0f, true);
    assert(nr_active_touch_fingers == 1 && !multi_touch_active);

    // Frame 1 observes hover, Frame 2 fires press (no spurious RBUTTON).
    fake_ticks += 16;
    handle_events();
    int mx = 0, my = 0;
    mouse_get_pos(&mx, &my);
    assert(mx == 300 && my == 500);
    assert(!key_is_down(VK_RBUTTON));
    assert(!key_is_down(VK_LBUTTON));

    fake_ticks += 16;
    handle_events();
    mouse_get_pos(&mx, &my);
    assert(!key_is_down(VK_RBUTTON));
    assert(key_is_down(VK_LBUTTON));
    assert(mx == 300 && my == 500);

    fake_ticks += 16;
    sim_touch_move(640, 240);
    handle_events();
    mouse_get_pos(&mx, &my);
    assert(key_is_down(VK_LBUTTON));
    assert(mx == 640 && my == 240);

    fake_ticks += 40;
    fake_num_touch_fingers = 0;
    sim_touch_up(640, 240);
    sim_finger_event(1, 203, 640.0f / 1280.0f, 240.0f / 720.0f, false);
    assert(nr_active_touch_fingers == 0);
    uint32_t scroll_lift_tick = fake_ticks;
    released = false;
    for (int f = 0; f < 5; f++) {
        fake_ticks += 16;
        handle_events();
        mouse_get_pos(&mx, &my);
        if (!key_is_down(VK_LBUTTON)) {
            released = true;
            break;
        }
    }
    assert(released);
    assert(fake_ticks - scroll_lift_tick <= 48);

    // Case 3: Dropped FINGERUP recovery when SDL_GetNumTouchFingers(touch_id) == 0.
    reset_test_state();
    fake_num_touch_fingers = 1;
    sim_finger_event(1, 301, 0.2f, 0.2f, true);
    fake_num_touch_fingers = 2;
    sim_finger_event(1, 302, 0.4f, 0.2f, true);
    assert(nr_active_touch_fingers == 2 && multi_touch_active);
    // Suppose FINGERUP for 301 was dropped by the OS; 302 lifts with 0 live fingers.
    fake_num_touch_fingers = 0;
    two_finger_scrolled = true;
    two_finger_tap_candidate = false;
    sim_finger_event(1, 302, 0.4f, 0.2f, false);
    assert(nr_active_touch_fingers == 0);
    assert(!multi_touch_active);
    // Subsequent drag starts cleanly.
    fake_num_touch_fingers = 1;
    sim_touch_down(180, 220);
    sim_finger_event(1, 303, 180.0f / 1280.0f, 220.0f / 720.0f, true);
    assert(nr_deferred_synthetic_mouse_events == 1 && !multi_touch_active);
""")
        compile_and_run(c_code)

    def test_sengoku_rance_check_select_and_drag_busho_loops(self) -> None:
        c_code = build_input_harness("""
    // Simulates Sengoku Rance's exact non-Parts polling structure:
    // - tagMapControl@checkSelect: each frame calls handle_events(), mouse_get_pos(),
    //   then checks key_is_down(VK_LBUTTON) to enter tagSansenList@dragBusho.
    // - tagSansenList@dragBusho: reads initial mouse_get_pos() (1st read), then loops
    //   while (key_is_down(VK_LBUTTON)) calling UpdateView (handle_events, +16ms)
    //   and 2nd mouse_get_pos() to update the dragged busho & drop target.

    // Case 1: Normal formation drag.
    reset_test_state();
    for (int i = 0; i < 2; i++) {
        fake_ticks += 16;
        handle_events();
        int ix = 0, iy = 0;
        mouse_get_pos(&ix, &iy);
        assert(!key_is_down(VK_LBUTTON));
    }

    sim_touch_down(260, 520);
    uint32_t down1_tick = fake_ticks;
    int sel_x = -1, sel_y = -1;
    bool entered = false;
    for (int f = 0; f < 4; f++) {
        fake_ticks += 16;
        handle_events();
        mouse_get_pos(&sel_x, &sel_y);
        if (key_is_down(VK_LBUTTON)) {
            entered = true;
            break;
        }
    }
    assert(entered);
    assert(sel_x == 260 && sel_y == 520);
    assert(fake_ticks - down1_tick <= 32);

    int grab_x = -1, grab_y = -1;
    mouse_get_pos(&grab_x, &grab_y); // 1st mouse_get_pos on entering dragBusho
    assert(grab_x == 260 && grab_y == 520);

    const int drag1_pts[4][2] = { {340, 470}, {460, 390}, {600, 310}, {720, 260} };
    for (int i = 0; i < 4; i++) {
        assert(key_is_down(VK_LBUTTON));
        sim_touch_move(drag1_pts[i][0], drag1_pts[i][1]);
        fake_ticks += 16;
        handle_events();
        assert(key_is_down(VK_LBUTTON));
        int cx = 0, cy = 0;
        mouse_get_pos(&cx, &cy);
        assert(cx == drag1_pts[i][0] && cy == drag1_pts[i][1]);
    }

    // Final move + lift arrive between frames, right before dragBusho's UpdateView.
    sim_touch_move(760, 240);
    sim_touch_up(760, 240);
    uint32_t lift1_tick = fake_ticks;
    int last_x = -1, last_y = -1;
    bool saw_drop_while_pressed = false;
    int post_lift_iters = 0;
    while (key_is_down(VK_LBUTTON)) {
        fake_ticks += 16;
        handle_events();
        bool still_down = key_is_down(VK_LBUTTON);
        int cx = 0, cy = 0;
        mouse_get_pos(&cx, &cy);
        if (still_down && cx == 760 && cy == 240)
            saw_drop_while_pressed = true;
        last_x = cx;
        last_y = cy;
        post_lift_iters++;
        assert(post_lift_iters < 10);
    }
    assert(saw_drop_while_pressed);
    assert(last_x == 760 && last_y == 240);
    assert(fake_ticks - lift1_tick <= 48);

    // Case 2: Rapid consecutive drags (<100ms apart), including an overlapping third
    // drag starting only 24ms after Drag 2's lift while Drag 2's release is draining.
    fake_ticks += 16; // 48ms total since lift1_tick (<100ms!)
    assert(fake_ticks - lift1_tick < 100);
    sim_touch_down(180, 580);
    entered = false;
    for (int f = 0; f < 4; f++) {
        fake_ticks += 16;
        handle_events();
        mouse_get_pos(&sel_x, &sel_y);
        if (key_is_down(VK_LBUTTON)) {
            entered = true;
            break;
        }
    }
    assert(entered && sel_x == 180 && sel_y == 580);
    mouse_get_pos(&grab_x, &grab_y);
    assert(grab_x == 180 && grab_y == 580);

    const int drag2_pts[4][2] = { {240, 520}, {320, 450}, {420, 360}, {480, 310} };
    for (int i = 0; i < 4; i++) {
        assert(key_is_down(VK_LBUTTON));
        sim_touch_move(drag2_pts[i][0], drag2_pts[i][1]);
        fake_ticks += 16;
        handle_events();
        assert(key_is_down(VK_LBUTTON));
        int cx = 0, cy = 0;
        mouse_get_pos(&cx, &cy);
        assert(cx == drag2_pts[i][0] && cy == drag2_pts[i][1]);
    }

    sim_touch_move(520, 280);
    sim_touch_up(520, 280);
    uint32_t lift2_tick = fake_ticks;
    // Frame 1 after Drag 2 lift: dragBusho observes drop coords (520, 280) while pressed.
    assert(key_is_down(VK_LBUTTON));
    fake_ticks += 16;
    handle_events();
    assert(key_is_down(VK_LBUTTON));
    int d2x = 0, d2y = 0;
    mouse_get_pos(&d2x, &d2y);
    assert(d2x == 520 && d2y == 280);

    // Drag 3 starts only 24ms after Drag 2's lift while Drag 2's release is still queued.
    fake_ticks += 8;
    assert(fake_ticks - lift2_tick == 24);
    sim_touch_down(620, 540);
    assert(nr_deferred_synthetic_mouse_events == 2);

    // Next 8ms completes the 16ms frame: Drag 2's release pops and dragBusho exits.
    fake_ticks += 8;
    handle_events();
    assert(!key_is_down(VK_LBUTTON));
    assert(fake_ticks - lift2_tick == 32);

    // checkSelect picks up Drag 3 cleanly at (620, 540) without Drag 2 contamination.
    entered = false;
    for (int f = 0; f < 4; f++) {
        fake_ticks += 16;
        handle_events();
        mouse_get_pos(&sel_x, &sel_y);
        if (key_is_down(VK_LBUTTON)) {
            entered = true;
            break;
        }
    }
    assert(entered && sel_x == 620 && sel_y == 540);
    mouse_get_pos(&grab_x, &grab_y);
    assert(grab_x == 620 && grab_y == 540);

    for (int i = 0; i < 4; i++) {
        assert(key_is_down(VK_LBUTTON));
        int tx = 620 - (i + 1) * 80;
        int ty = 540 - (i + 1) * 70;
        sim_touch_move(tx, ty);
        fake_ticks += 16;
        handle_events();
        int cx = 0, cy = 0;
        mouse_get_pos(&cx, &cy);
        assert(key_is_down(VK_LBUTTON));
        assert(cx == tx && cy == ty);
    }
    sim_touch_move(290, 220);
    sim_touch_up(290, 220);
    uint32_t lift3_tick = fake_ticks;
    saw_drop_while_pressed = false;
    post_lift_iters = 0;
    while (key_is_down(VK_LBUTTON)) {
        fake_ticks += 16;
        handle_events();
        bool still_down = key_is_down(VK_LBUTTON);
        int cx = 0, cy = 0;
        mouse_get_pos(&cx, &cy);
        if (still_down && cx == 290 && cy == 220)
            saw_drop_while_pressed = true;
        post_lift_iters++;
        assert(post_lift_iters < 10);
    }
    assert(saw_drop_while_pressed);
    assert(fake_ticks - lift3_tick <= 48);

    // Case 3A: Sub-50ms quick flick (20ms touch duration, lift after hover frame
    // before deferred PRESS fires). Verify dragBusho's 1st mouse_get_pos at entry
    // AND 2nd mouse_get_pos inside while(key_is_down) both see drop coords (740, 260)
    // while VK_LBUTTON remains pressed before release.
    reset_test_state();
    sim_touch_down(300, 500);
    fake_ticks += 16;
    handle_events();
    mouse_get_pos(&sel_x, &sel_y);
    assert(sel_x == 300 && sel_y == 500);
    assert(!key_is_down(VK_LBUTTON));

    fake_ticks += 4; // 20ms total touch duration (<50ms)
    sim_touch_move(740, 260);
    sim_touch_up(740, 260);

    fake_ticks += 12; // next 16ms frame
    handle_events();
    mouse_get_pos(&sel_x, &sel_y);
    assert(sel_x == 300 && sel_y == 500); // checkSelect hits busho at start coords
    assert(key_is_down(VK_LBUTTON));      // enters dragBusho
    uint32_t flick_press_tick = fake_ticks;

    // 1st mouse_get_pos on entering dragBusho observes drop coords, but because RELEASE was
    // queued before down was observed on a drag, it waits for the 2nd observation inside the loop.
    mouse_get_pos(&grab_x, &grab_y);
    assert(grab_x == 740 && grab_y == 260);
    assert(!synthetic_mouse_release_pos_observed);
    assert(synthetic_mouse_release_pos_obs_count == 1);

    int flick_iters = 0;
    bool second_pos_saw_drop_while_pressed = false;
    bool second_iter_saw_drop_while_pressed = false;
    while (key_is_down(VK_LBUTTON)) {
        fake_ticks += 16;
        handle_events();
        bool still_down = key_is_down(VK_LBUTTON);
        int cx = 0, cy = 0;
        mouse_get_pos(&cx, &cy); // 2nd mouse_get_pos inside while(key_is_down)
        if (flick_iters == 0) {
            second_pos_saw_drop_while_pressed = still_down && cx == 740 && cy == 260;
        } else if (flick_iters == 1) {
            second_iter_saw_drop_while_pressed = still_down && cx == 740 && cy == 260;
        }
        flick_iters++;
        assert(flick_iters < 12);
    }
    assert(second_pos_saw_drop_while_pressed);
    assert(second_iter_saw_drop_while_pressed);
    assert(fake_ticks - flick_press_tick >= SYNTHETIC_MOUSE_RELEASE_DOWN_MIN_DELAY);
    assert(fake_ticks - flick_press_tick <= 80);

    // Case 3B: Ultra-fast 12ms flick completing entirely before even Frame 1 polls.
    reset_test_state();
    sim_touch_down(310, 490);
    fake_ticks += 12;
    sim_touch_move(680, 290);
    sim_touch_up(680, 290);

    fake_ticks += 4;
    handle_events();
    mouse_get_pos(&sel_x, &sel_y);
    assert(sel_x == 310 && sel_y == 490);
    assert(!key_is_down(VK_LBUTTON));

    fake_ticks += 16;
    handle_events();
    mouse_get_pos(&sel_x, &sel_y);
    assert(sel_x == 310 && sel_y == 490);
    assert(key_is_down(VK_LBUTTON));
    uint32_t flick2_press_tick = fake_ticks;

    mouse_get_pos(&grab_x, &grab_y); // 1st mouse_get_pos on entering dragBusho
    assert(grab_x == 680 && grab_y == 290);

    flick_iters = 0;
    second_pos_saw_drop_while_pressed = false;
    second_iter_saw_drop_while_pressed = false;
    while (key_is_down(VK_LBUTTON)) {
        fake_ticks += 16;
        handle_events();
        bool still_down = key_is_down(VK_LBUTTON);
        int cx = 0, cy = 0;
        mouse_get_pos(&cx, &cy); // 2nd mouse_get_pos inside while(key_is_down)
        if (flick_iters == 0) {
            second_pos_saw_drop_while_pressed = still_down && cx == 680 && cy == 290;
        } else if (flick_iters == 1) {
            second_iter_saw_drop_while_pressed = still_down && cx == 680 && cy == 290;
        }
        flick_iters++;
        assert(flick_iters < 12);
    }
    assert(second_pos_saw_drop_while_pressed);
    assert(second_iter_saw_drop_while_pressed);
    assert(fake_ticks - flick2_press_tick >= SYNTHETIC_MOUSE_RELEASE_DOWN_MIN_DELAY);
    assert(fake_ticks - flick2_press_tick <= 80);

    // Case 3C: Pre-loop CG-load stall (120ms) between dragBusho's entry mouse_get_pos and
    // its pre-loop UPDATE() must NOT prematurely release VK_LBUTTON before while(key_is_down)
    // checks the 6 formation slots.
    reset_test_state();
    sim_touch_down(480, 130);
    fake_ticks += 16;
    handle_events();
    mouse_get_pos(&sel_x, &sel_y);
    assert(sel_x == 480 && sel_y == 130);
    assert(!key_is_down(VK_LBUTTON));

    fake_ticks += 14;
    sim_touch_move(260, 435);
    sim_touch_up(260, 435);

    fake_ticks += 16;
    handle_events();
    mouse_get_pos(&sel_x, &sel_y);
    assert(sel_x == 480 && sel_y == 130);
    assert(key_is_down(VK_LBUTTON)); // caller enters dragBusho

    // Step 3a: dragBusho entry mouse_get_pos + Step 3b: key_is_down
    mouse_get_pos(&grab_x, &grab_y);
    assert(grab_x == 260 && grab_y == 435);
    assert(key_is_down(VK_LBUTTON));
    assert(!synthetic_mouse_release_pos_observed);

    // Step 3c/3d: portrait CG load + pre-loop UPDATE() stalls 120ms (> 80ms no-pos timeout).
    fake_ticks += 120;
    handle_events();
    // Step 3e: while(key_is_down) MUST still enter and observe drop coords!
    assert(key_is_down(VK_LBUTTON));
    int drop_cx = 0, drop_cy = 0;
    mouse_get_pos(&drop_cx, &drop_cy);
    assert(drop_cx == 260 && drop_cy == 435);
    assert(synthetic_mouse_release_pos_observed);

    // Next frame UPDATE() releases promptly after the drop slot was matched.
    fake_ticks += 16;
    handle_events();
    assert(!key_is_down(VK_LBUTTON));
    assert(nr_deferred_synthetic_mouse_events == 0);
""")
        compile_and_run(c_code)

    def test_edge_clamped_drag_mouse_set_pos_preserves_synthetic_touch_state(self) -> None:
        c_code = build_input_harness("""
    // Case 1: Active touch drag hits edge clamp calling mouse_set_pos().
    reset_test_state();
    sim_touch_down(400, 360);
    fake_ticks += 16;
    handle_events();
    int mx = 0, my = 0;
    mouse_get_pos(&mx, &my);
    assert(mx == 400 && my == 360);
    fake_ticks += 16;
    handle_events();
    assert(key_is_down(VK_LBUTTON));
    assert(has_synthetic_mouse_pos);
    assert(warp_calls == 0);

    // Finger crosses clamp edge to (30, 15); game clamps cursor to (100, 80).
    fake_ticks += 16;
    sim_touch_move(30, 15);
    handle_events();
    mouse_get_pos(&mx, &my);
    assert(mx == 30 && my == 15);

    mouse_set_pos(100, 80);
    assert(has_synthetic_mouse_pos);
    assert(warp_calls == 0);
    assert(synthetic_mouse_warped);

    // While finger remains stationary at raw (30, 15), mouse_get_pos returns clamped (100, 80).
    for (int f = 0; f < 3; f++) {
        fake_ticks += 16;
        handle_events();
        mouse_get_pos(&mx, &my);
        assert(key_is_down(VK_LBUTTON));
        assert(mx == 100 && my == 80);
        assert(has_synthetic_mouse_pos);
        assert(warp_calls == 0);
    }

    // As soon as the finger moves, tracking resumes immediately.
    fake_ticks += 16;
    sim_touch_move(145, 115);
    handle_events();
    mouse_get_pos(&mx, &my);
    assert(key_is_down(VK_LBUTTON));
    assert(mx == 145 && my == 115);
    assert(!synthetic_mouse_warped);
    assert(has_synthetic_mouse_pos);
    assert(warp_calls == 0);

    // Clamp again near opposite edge, then move to drop target and lift.
    fake_ticks += 16;
    sim_touch_move(1260, 710);
    handle_events();
    mouse_get_pos(&mx, &my);
    assert(mx == 1260 && my == 710);
    mouse_set_pos(1180, 640);
    assert(has_synthetic_mouse_pos && warp_calls == 0 && synthetic_mouse_warped);

    fake_ticks += 16;
    handle_events();
    mouse_get_pos(&mx, &my);
    assert(mx == 1180 && my == 640);

    sim_touch_move(820, 410);
    sim_touch_up(820, 410);
    fake_ticks += 16;
    handle_events();
    mouse_get_pos(&mx, &my);
    assert(key_is_down(VK_LBUTTON));
    assert(mx == 820 && my == 410);
    assert(has_synthetic_mouse_pos && warp_calls == 0);

    fake_ticks += 16;
    handle_events();
    assert(!key_is_down(VK_LBUTTON));
    mouse_get_pos(&mx, &my);
    assert(mx == 820 && my == 410);
    assert(has_synthetic_mouse_pos && warp_calls == 0);

    // Case 2: Lifting directly while clamped still exposes final release coords cleanly.
    reset_test_state();
    sim_touch_down(400, 360);
    fake_ticks += 16;
    handle_events();
    mouse_get_pos(&mx, &my);
    fake_ticks += 16;
    handle_events();
    assert(key_is_down(VK_LBUTTON));
    fake_ticks += 60;
    sim_touch_move(1260, 710);
    handle_events();
    mouse_get_pos(&mx, &my);
    assert(mx == 1260 && my == 710);
    mouse_set_pos(1180, 640);
    assert(synthetic_mouse_warped && warp_calls == 0);

    sim_touch_up(1260, 710);
    fake_ticks += 16;
    handle_events();
    mouse_get_pos(&mx, &my);
    assert(key_is_down(VK_LBUTTON));
    assert(mx == 1260 && my == 710);
    assert(has_synthetic_mouse_pos && warp_calls == 0);
    fake_ticks += 16;
    handle_events();
    assert(!key_is_down(VK_LBUTTON));
    assert(warp_calls == 0);

    // Case 3: Real mouse (has_synthetic_mouse_pos == false) still calls SDL_WarpMouseInWindow.
    reset_test_state();
    assert(!has_synthetic_mouse_pos);
    assert(warp_calls == 0);
    mouse_set_pos(640, 360);
    assert(warp_calls == 1);
    assert(fake_sdl_mouse_x == 640 && fake_sdl_mouse_y == 360);
    mouse_get_pos(&mx, &my);
    assert(mx == 640 && my == 360);
    assert(fabsf(gamepad_cursor_norm_x - (640.0f / 1279.0f)) < 1e-5f);
    assert(fabsf(gamepad_cursor_norm_y - (360.0f / 719.0f)) < 1e-5f);

    // Case 4: Active virtual gamepad cursor keeps active/visible state while syncing
    // gamepad_cursor_norm_x/y on Mouse_SetPos edge clamps (including letterboxed viewport).
    reset_test_state();
    gamepad_cursor_active = true;
    gamepad_cursor_visible = true;
    gamepad_cursor_norm_x = 0.01f;
    gamepad_cursor_norm_y = 0.02f;
    has_synthetic_mouse_pos = true;
    synthetic_mouse_x = 12;
    synthetic_mouse_y = 14;
    mouse_set_pos(100, 80);
    assert(warp_calls == 0);
    assert(has_synthetic_mouse_pos);
    assert(gamepad_cursor_active && gamepad_cursor_visible);
    assert(fabsf(gamepad_cursor_norm_x - (100.0f / 1279.0f)) < 1e-5f);
    assert(fabsf(gamepad_cursor_norm_y - (80.0f / 719.0f)) < 1e-5f);
    mouse_get_pos(&mx, &my);
    assert(mx == 100 && my == 80);

    // Next gamepad stick step integrates from the clamped normalized coordinates, not 0.01/0.02.
    gamepad_cursor_norm_x += 16.0f / 1280.0f;
    gamepad_cursor_norm_y += 12.0f / 720.0f;
    synthetic_mouse_x = sdl.viewport.x + (int)(gamepad_cursor_norm_x * max(1, sdl.viewport.w - 1));
    synthetic_mouse_y = sdl.viewport.y + (int)(gamepad_cursor_norm_y * max(1, sdl.viewport.h - 1));
    mouse_get_pos(&mx, &my);
    assert(mx >= 115 && mx <= 116);
    assert(my >= 91 && my <= 92);

    // Letterboxed viewport & out-of-bounds clamp stay within [0.0f, 1.0f].
    SDL_Rect saved_vp = sdl.viewport;
    sdl.viewport = (SDL_Rect){ .x = 160, .y = 40, .w = 960, .h = 640 };
    mouse_set_pos(640, 360);
    assert(fabsf(gamepad_cursor_norm_x - (480.0f / 959.0f)) < 1e-5f);
    assert(fabsf(gamepad_cursor_norm_y - (320.0f / 639.0f)) < 1e-5f);
    mouse_set_pos(-50, 2000);
    assert(gamepad_cursor_norm_x == 0.0f && gamepad_cursor_norm_y == 1.0f);
    sdl.viewport = saved_vp;
""")
        compile_and_run(c_code)

    def test_non_parts_quick_tap_and_unobserved_hold_respect_fallback_timeouts(self) -> None:
        c_code = build_input_harness("""
    // Case 1: Quick single tap (<50ms) with neither mouse_get_pos() nor key_is_down()
    // polled respects the 50ms press delay and full 250ms release fallback timeout.
    reset_test_state();
    sim_touch_down(400, 300);
    fake_ticks += 20;
    sim_touch_up(404, 302); // snaps back to (400, 300)

    fake_ticks += 20; // 40ms since touchdown (<50ms)
    handle_events();
    assert(!key_state[VK_LBUTTON]);
    assert(nr_deferred_synthetic_mouse_events == 2);

    fake_ticks += 10; // 50ms since touchdown -> press fires
    handle_events();
    assert(key_state[VK_LBUTTON]);
    assert(!synthetic_mouse_down_observed);
    assert(nr_deferred_synthetic_mouse_events == 1);

    // Without key_is_down() polling, button stays pressed past 80ms and up to <250ms.
    fake_ticks += 80;
    handle_events();
    assert(key_state[VK_LBUTTON]);
    assert(nr_deferred_synthetic_mouse_events == 1);

    fake_ticks += 160; // 240ms since press (<250ms)
    handle_events();
    assert(key_state[VK_LBUTTON]);
    assert(nr_deferred_synthetic_mouse_events == 1);

    fake_ticks += 10; // 250ms fallback expires -> release fires
    handle_events();
    assert(!key_state[VK_LBUTTON]);
    assert(nr_deferred_synthetic_mouse_events == 0);

    // Case 2: Quick single tap (<50ms) with hover observed, but key_is_down() is not
    // polled until 112ms later (even though mouse_get_pos() is called each frame).
    // Must stay held under fallback so the late key_is_down() still catches the tap.
    reset_test_state();
    sim_touch_down(420, 310);
    fake_ticks += 16;
    handle_events();
    int mx = 0, my = 0;
    mouse_get_pos(&mx, &my);
    assert(mx == 420 && my == 310);
    assert(synthetic_mouse_hover_observed);

    fake_ticks += 4;
    sim_touch_up(422, 311); // 20ms quick tap, snaps to (420, 310)

    fake_ticks += 12; // 16ms after hover -> press fires
    handle_events();
    assert(key_state[VK_LBUTTON]);
    assert(!synthetic_mouse_down_observed);

    for (int f = 0; f < 7; f++) {
        fake_ticks += 16; // up to +112ms after press
        handle_events();
        mouse_get_pos(&mx, &my);
        assert(mx == 420 && my == 310);
        assert(key_state[VK_LBUTTON]);
        assert(!synthetic_mouse_down_observed);
        assert(!synthetic_mouse_release_pos_observed);
    }

    // Late key_is_down() still observes the tap, then holds >=50ms before release.
    assert(key_is_down(VK_LBUTTON));
    assert(synthetic_mouse_down_observed);
    fake_ticks += 32;
    handle_events();
    assert(key_is_down(VK_LBUTTON));
    fake_ticks += 20; // >=50ms since down observed, >=80ms since release queued
    handle_events();
    assert(!key_is_down(VK_LBUTTON));
    assert(nr_deferred_synthetic_mouse_events == 0);

    // Case 3: Observed button hold (key_is_down polled, down settled >=50ms) where
    // mouse_get_pos() is NOT polled after lift respects the 80ms no-pos delay.
    reset_test_state();
    sim_touch_down(500, 350);
    fake_ticks += 50;
    handle_events();
    assert(key_is_down(VK_LBUTTON));
    assert(synthetic_mouse_down_observed);

    fake_ticks += 60; // down already settled (>=50ms)
    sim_touch_up(500, 350);

    fake_ticks += 64; // <80ms without mouse_get_pos()
    handle_events();
    assert(key_is_down(VK_LBUTTON));
    assert(!synthetic_mouse_release_pos_observed);

    fake_ticks += 16; // 80ms reached -> releases promptly (well below 250ms)
    handle_events();
    assert(!key_is_down(VK_LBUTTON));
    assert(nr_deferred_synthetic_mouse_events == 0);
""")
        compile_and_run(c_code)


if __name__ == "__main__":
    unittest.main()
