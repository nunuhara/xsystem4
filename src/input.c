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

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL.h>
#ifdef __ANDROID__
#include <jni.h>
#include <SDL_system.h>
#endif
#include "cJSON.h"
#include "system4.h"
#include "queue.h"
#include "gfx/gfx.h"
#include "gfx/private.h"
#include "input.h"
#include "movie.h"
#include "parts.h"
#include "savedata.h"
#include "scene.h"
#include "mixer.h"
#include "vm.h"
#include "xsystem4.h"
#include "debugger.h"
#ifdef __SWITCH__
#include "controller.h"
#include "hub/controller_runtime.h"
#include "hub/runtime_menu.h"
#include "hub/runtime_menu_engines.h"
#endif
#ifdef ALICEHUB_IOS
#include "EnginePlatform.h"
#endif

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

bool key_state[VK_NR_KEYCODES];
static atomic_bool hook_close_app = false;
static unsigned pending_close_requests = 0;
static uint32_t first_unpopped_close_tick = 0;
static bool kbd_ctrl_down = false;
static atomic_bool touch_ctrl_skip = false;
static atomic_bool long_press_ctrl_enabled = false;
static bool long_touch_armed = false;
static uint32_t long_touch_start_tick;
static SDL_FRect long_touch_finger_rect;
static bool long_touch_ctrl_down = false;
static uint64_t next_touch_press_id;
static uint64_t touch_press_id;
static uint64_t long_touch_click_cancel;
#define LONG_TOUCH_DURATION 1000

#define MOVIE_HOLD_SKIP_MS 800
#define MOVIE_TOUCH_SLOP_SQ (0.05f * 0.05f)
#ifdef __ANDROID__
#define COMMAND_TOGGLE_CTRL_OVERLAY 0x8002
#define COMMAND_MOVIE_SKIP_STATE    0x8003
#define COMMAND_CURSOR_STATE        0x8004
#define COMMAND_GAMEPAD_UI_EVENT    0x8005
enum movie_skip_ui_state {
	MOVIE_SKIP_UI_HIDDEN  = 0,
	MOVIE_SKIP_UI_HINT    = 1,
	MOVIE_SKIP_UI_HOLDING = 2,
};
enum gamepad_ui_event_cmd {
	GAMEPAD_UI_EMU_CMD          = 1,
	GAMEPAD_UI_CONTROLLER_COUNT = 2,
};
#endif

static bool kbd_key_state[VK_NR_KEYCODES];
static bool vpad_vk_down[VK_NR_KEYCODES];
static bool pad_vk_down[VK_NR_KEYCODES];
// Keys held down by in-process features (e.g. the modifier's MOD cheat key).
static bool injected_vk_down[VK_NR_KEYCODES];
static bool sdl_mouse_down[VK_NR_KEYCODES];
static bool prev_gamepad_mouse[VK_NR_KEYCODES];

#define MAX_VPAD_SLOTS 32
struct input_action_spec {
	uint8_t type;
	int16_t code;
	int16_t code2;
	bool toggle_mode;
};

// Protected by gp_mutex (written by UI thread / JNI, snapshotted by SDLThread)
struct vpad_ui_slot_state {
	bool phys_down;
	bool pending_press_edge;
	struct input_action_spec pending_action;
	struct input_action_spec active_action;
};

// Private to SDLThread
struct vpad_rt_slot_state {
	bool prev_effective_phys;
	bool latched;
	bool movie_suppressed;
	uint32_t last_wheel_tick;
};

static pthread_mutex_t gp_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct vpad_ui_slot_state vpad_ui_slots[MAX_VPAD_SLOTS];
static struct vpad_rt_slot_state vpad_rt_slots[MAX_VPAD_SLOTS];
static uint32_t vpad_latch_reset_gen = 0;
static uint32_t phys_latch_reset_gen = 0;
static uint32_t seen_vpad_latch_reset_gen = 0;
static uint32_t seen_phys_latch_reset_gen = 0;
static bool pending_cursor_reset = false;
static bool pending_cursor_reset_visible = false;
static float vpad_stick_x[2] = { 0.0f, 0.0f };
static float vpad_stick_y[2] = { 0.0f, 0.0f };
static int vpad_dpad_cfg_mode = STICK_MODE_ARROWS;
static bool vpad_dpad_dir[4] = { false, false, false, false }; // L, R, U, D

static bool gamepad_cursor_active = false;
static bool gamepad_cursor_visible = false;
static float gamepad_cursor_norm_x = 0.5f;
static float gamepad_cursor_norm_y = 0.5f;
static uint32_t last_cursor_step_tick = 0;
static uint32_t last_cursor_move_tick = 0;
static bool vpad_cursor_btn_down = false;

static void sync_gamepad_cursor_norm_from_window(int wx, int wy)
{
	if (sdl.viewport.w > 1 && sdl.viewport.h > 1) {
		float nx = (float)(wx - sdl.viewport.x) / (float)(sdl.viewport.w - 1);
		float ny = (float)(wy - sdl.viewport.y) / (float)(sdl.viewport.h - 1);
		if (nx < 0.0f) nx = 0.0f;
		if (nx > 1.0f) nx = 1.0f;
		if (ny < 0.0f) ny = 0.0f;
		if (ny > 1.0f) ny = 1.0f;
		gamepad_cursor_norm_x = nx;
		gamepad_cursor_norm_y = ny;
	}
}

static void sync_gamepad_cursor_from_window(int wx, int wy)
{
	sync_gamepad_cursor_norm_from_window(wx, wy);
	gamepad_cursor_active = false;
	gamepad_cursor_visible = false;
}

static bool movie_mode_active = false;
static uint32_t movie_mode_seq = 0;

static bool single_touch_down = false;
static SDL_FingerID single_touch_finger_id = 0;
static float single_touch_start_x = 0.0f;
static float single_touch_start_y = 0.0f;
static uint32_t single_touch_down_tick = 0;
static bool movie_touch_hold_eligible = false;
static bool suppress_touch_until_release = false;

static bool movie_mouse_holding = false;
static uint8_t movie_mouse_button = 0;
static uint32_t movie_mouse_hold_start_tick = 0;
static bool suppress_mouse_until_release = false;

static bool movie_key_holding = false;
static SDL_Scancode movie_key_scancode = SDL_SCANCODE_UNKNOWN;
static uint32_t movie_key_hold_start_tick = 0;
static bool suppressed_movie_keys[VK_NR_KEYCODES];
static uint32_t movie_hold_token = 0;
static bool movie_early_release_hint = false;

static bool is_movie_skip_suppressed_key(enum sact_keycode code)
{
	switch (code) {
	case VK_LBUTTON:
	case VK_RBUTTON:
	case VK_MBUTTON:
	case VK_RETURN:
	case VK_SPACE:
	case VK_CONTROL:
		return true;
	default:
		return false;
	}
}

static bool is_movie_hold_key(enum sact_keycode code)
{
	return code == VK_RETURN || code == VK_SPACE || code == VK_CONTROL;
}

static void send_movie_skip_ui_state(int mode, uint32_t seq, uint32_t token, int duration_ms, bool force)
{
#ifdef __ANDROID__
	static int last_mode = MOVIE_SKIP_UI_HIDDEN;
	static uint32_t last_seq = 0;
	static uint32_t last_token = 0;
	if (mode == MOVIE_SKIP_UI_HIDDEN) {
		if (!force && last_mode == MOVIE_SKIP_UI_HIDDEN)
			return;
		last_mode = MOVIE_SKIP_UI_HIDDEN;
		SDL_AndroidSendMessage(COMMAND_MOVIE_SKIP_STATE, MOVIE_SKIP_UI_HIDDEN);
		return;
	}
	if (mode == MOVIE_SKIP_UI_HOLDING) {
		if (!force && last_mode == MOVIE_SKIP_UI_HOLDING && last_seq == seq && last_token == token)
			return;
		last_mode = MOVIE_SKIP_UI_HOLDING;
		last_seq = seq;
		last_token = token;
		if (duration_ms < 100)
			duration_ms = 100;
		if (duration_ms > 5000)
			duration_ms = 5000;
		SDL_AndroidSendMessage(COMMAND_MOVIE_SKIP_STATE,
			MOVIE_SKIP_UI_HOLDING | (duration_ms << 8));
		return;
	}
	if (!force && last_mode == MOVIE_SKIP_UI_HINT && last_seq == seq)
		return;
	last_mode = MOVIE_SKIP_UI_HINT;
	last_seq = seq;
	SDL_AndroidSendMessage(COMMAND_MOVIE_SKIP_STATE, MOVIE_SKIP_UI_HINT);
#else
	(void)mode;
	(void)seq;
	(void)token;
	(void)duration_ms;
	(void)force;
#endif
}

void input_reset_close_hook(void)
{
	atomic_store(&hook_close_app, false);
	pending_close_requests = 0;
	first_unpopped_close_tick = 0;
}

void input_set_close_hook(bool enabled)
{
	atomic_store(&hook_close_app, enabled);
	if (!enabled) {
		pending_close_requests = 0;
		first_unpopped_close_tick = 0;
	}
}

bool input_get_close_hook(void)
{
	return atomic_load(&hook_close_app);
}

bool input_has_system_message(void)
{
	return pending_close_requests != 0;
}

bool input_pop_system_message(int *message)
{
	if (!message || !pending_close_requests)
		return false;
	pending_close_requests--;
	*message = 7; // AppClosing consumed by SystemService.PopSystemMessage
	return true;
}

static bool input_hook_close_request(void)
{
	if (!atomic_load(&hook_close_app))
		return false;
	uint32_t now = SDL_GetTicks();
	if (pending_close_requests == 0) {
		pending_close_requests = 1;
		first_unpopped_close_tick = now;
		return true;
	}
	if ((now - first_unpopped_close_tick) >= 1500)
		return false;
	return true;
}

static void handle_quit_event(void)
{
	if (input_hook_close_request())
		return;
#ifdef __ANDROID__
	static const SDL_MessageBoxButtonData quit_buttons[] = {
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "OK" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel" },
	};
	const SDL_MessageBoxData mbox = {
		SDL_MESSAGEBOX_INFORMATION,
		NULL,
		"__ALICEHUB_QUIT__",
		"",
		SDL_arraysize(quit_buttons),
		quit_buttons,
		NULL
	};
	int result = 0;
	if (SDL_ShowMessageBox(&mbox, &result) == 0 && result != 1) {
		pending_close_requests = 0;
		first_unpopped_close_tick = 0;
		gfx_reset_frame_pacer();
		return;
	}
#endif
	savedata_flush_on_background();
	vm_exit(0);
}

static void sync_ctrl_key_state(void)
{
	kbd_key_state[VK_CONTROL] = kbd_ctrl_down;
	key_state[VK_CONTROL] = kbd_ctrl_down || injected_vk_down[VK_CONTROL] || atomic_load(&touch_ctrl_skip)
		|| vpad_vk_down[VK_CONTROL] || pad_vk_down[VK_CONTROL] || long_touch_ctrl_down;
}

void input_set_touch_ctrl_skip(bool enabled)
{
	// Safe to call from Android UI thread; SDLThread observes touch_ctrl_skip atomically.
	atomic_store(&touch_ctrl_skip, enabled);
}

bool input_get_touch_ctrl_skip(void)
{
	return atomic_load(&touch_ctrl_skip);
}

void input_set_long_press_ctrl(bool enabled)
{
	atomic_store(&long_press_ctrl_enabled, enabled);
}
static enum sact_keycode sdl_keytable[] = {
	[SDL_SCANCODE_0] = VK_0,
	[SDL_SCANCODE_1] = VK_1,
	[SDL_SCANCODE_2] = VK_2,
	[SDL_SCANCODE_3] = VK_3,
	[SDL_SCANCODE_4] = VK_4,
	[SDL_SCANCODE_5] = VK_5,
	[SDL_SCANCODE_6] = VK_6,
	[SDL_SCANCODE_7] = VK_7,
	[SDL_SCANCODE_8] = VK_8,
	[SDL_SCANCODE_9] = VK_9,
	[SDL_SCANCODE_A] = VK_A,
	[SDL_SCANCODE_B] = VK_B,
	[SDL_SCANCODE_C] = VK_C,
	[SDL_SCANCODE_D] = VK_D,
	[SDL_SCANCODE_E] = VK_E,
	[SDL_SCANCODE_F] = VK_F,
	[SDL_SCANCODE_G] = VK_G,
	[SDL_SCANCODE_H] = VK_H,
	[SDL_SCANCODE_I] = VK_I,
	[SDL_SCANCODE_J] = VK_J,
	[SDL_SCANCODE_K] = VK_K,
	[SDL_SCANCODE_L] = VK_L,
	[SDL_SCANCODE_M] = VK_M,
	[SDL_SCANCODE_N] = VK_N,
	[SDL_SCANCODE_O] = VK_O,
	[SDL_SCANCODE_P] = VK_P,
	[SDL_SCANCODE_Q] = VK_Q,
	[SDL_SCANCODE_R] = VK_R,
	[SDL_SCANCODE_S] = VK_S,
	[SDL_SCANCODE_T] = VK_T,
	[SDL_SCANCODE_U] = VK_U,
	[SDL_SCANCODE_V] = VK_V,
	[SDL_SCANCODE_W] = VK_W,
	[SDL_SCANCODE_X] = VK_X,
	[SDL_SCANCODE_Y] = VK_Y,
	[SDL_SCANCODE_Z] = VK_Z,
	[SDL_SCANCODE_BACKSPACE] = VK_BACK,
	[SDL_SCANCODE_TAB] = VK_TAB,
	[SDL_SCANCODE_CLEAR] = VK_CLEAR,
	[SDL_SCANCODE_RETURN] = VK_RETURN,
	[SDL_SCANCODE_LSHIFT] = VK_SHIFT,
	[SDL_SCANCODE_RSHIFT] = VK_SHIFT,
	[SDL_SCANCODE_LCTRL] = VK_CONTROL,
	[SDL_SCANCODE_RCTRL] = VK_CONTROL,
	[SDL_SCANCODE_LALT] = VK_MENU,
	[SDL_SCANCODE_RALT] = VK_MENU,
	[SDL_SCANCODE_PAUSE] = VK_PAUSE,
	[SDL_SCANCODE_CAPSLOCK] = VK_CAPITAL,
	[SDL_SCANCODE_ESCAPE] = VK_ESCAPE,
	[SDL_SCANCODE_SPACE] = VK_SPACE,
	[SDL_SCANCODE_PAGEUP] = VK_PRIOR,
	[SDL_SCANCODE_PAGEDOWN] = VK_NEXT,
	[SDL_SCANCODE_END] = VK_END,
	[SDL_SCANCODE_HOME] = VK_HOME,
	[SDL_SCANCODE_LEFT] = VK_LEFT,
	[SDL_SCANCODE_UP] = VK_UP,
	[SDL_SCANCODE_RIGHT] = VK_RIGHT,
	[SDL_SCANCODE_DOWN] = VK_DOWN,
	[SDL_SCANCODE_SELECT] = VK_SELECT,
	[SDL_SCANCODE_EXECUTE] = VK_EXECUTE,
	[SDL_SCANCODE_PRINTSCREEN] = VK_SNAPSHOT,
	[SDL_SCANCODE_INSERT] = VK_INSERT,
	[SDL_SCANCODE_DELETE] = VK_DELETE,
	[SDL_SCANCODE_HELP] = VK_HELP,
	[SDL_SCANCODE_KP_0] = VK_NUMPAD0,
	[SDL_SCANCODE_KP_1] = VK_NUMPAD1,
	[SDL_SCANCODE_KP_2] = VK_NUMPAD2,
	[SDL_SCANCODE_KP_3] = VK_NUMPAD3,
	[SDL_SCANCODE_KP_4] = VK_NUMPAD4,
	[SDL_SCANCODE_KP_5] = VK_NUMPAD5,
	[SDL_SCANCODE_KP_6] = VK_NUMPAD6,
	[SDL_SCANCODE_KP_7] = VK_NUMPAD7,
	[SDL_SCANCODE_KP_8] = VK_NUMPAD8,
	[SDL_SCANCODE_KP_9] = VK_NUMPAD9,
	[SDL_SCANCODE_KP_MULTIPLY] = VK_MULTIPLY,
	[SDL_SCANCODE_KP_PLUS] = VK_ADD,
	[SDL_SCANCODE_KP_VERTICALBAR] = VK_SEPARATOR, // ???
	[SDL_SCANCODE_KP_MINUS] = VK_SUBTRACT,
	[SDL_SCANCODE_KP_PERIOD] = VK_DECIMAL,
	[SDL_SCANCODE_KP_DIVIDE] = VK_DIVIDE,
	[SDL_SCANCODE_KP_ENTER] = VK_RETURN,
	[SDL_SCANCODE_F1] = VK_F1,
	[SDL_SCANCODE_F2] = VK_F2,
	[SDL_SCANCODE_F3] = VK_F3,
	[SDL_SCANCODE_F4] = VK_F4,
	[SDL_SCANCODE_F5] = VK_F5,
	[SDL_SCANCODE_F6] = VK_F6,
	[SDL_SCANCODE_F7] = VK_F7,
	[SDL_SCANCODE_F8] = VK_F8,
	[SDL_SCANCODE_F9] = VK_F9,
	[SDL_SCANCODE_F10] = VK_F10,
	[SDL_SCANCODE_F11] = VK_F11,
	[SDL_SCANCODE_F12] = VK_F12,
	[SDL_SCANCODE_F13] = VK_F13,
	[SDL_SCANCODE_F14] = VK_F14,
	[SDL_SCANCODE_F15] = VK_F15,
	[SDL_SCANCODE_F16] = VK_F16,
	[SDL_SCANCODE_F17] = VK_F17,
	[SDL_SCANCODE_F18] = VK_F18,
	[SDL_SCANCODE_F19] = VK_F19,
	[SDL_SCANCODE_F20] = VK_F20,
	[SDL_SCANCODE_F21] = VK_F21,
	[SDL_SCANCODE_F22] = VK_F22,
	[SDL_SCANCODE_F23] = VK_F23,
	[SDL_SCANCODE_F24] = VK_F24,
	[SDL_SCANCODE_NUMLOCKCLEAR] = VK_NUMLOCK,
	[SDL_SCANCODE_SCROLLLOCK] = VK_SCROLL,
};

static enum sact_keycode sdl_to_sact_key(SDL_Scancode scancode, SDL_Keymod modifiers)
{
	if (!(modifiers & KMOD_NUM)) {
		switch (scancode) {
		case SDL_SCANCODE_KP_0:      return VK_INSERT;
		case SDL_SCANCODE_KP_1:      return VK_END;
		case SDL_SCANCODE_KP_2:      return VK_DOWN;
		case SDL_SCANCODE_KP_3:      return VK_NEXT;
		case SDL_SCANCODE_KP_4:      return VK_LEFT;
		case SDL_SCANCODE_KP_5:      return VK_CLEAR;
		case SDL_SCANCODE_KP_6:      return VK_RIGHT;
		case SDL_SCANCODE_KP_7:      return VK_HOME;
		case SDL_SCANCODE_KP_8:      return VK_UP;
		case SDL_SCANCODE_KP_9:      return VK_PRIOR;
		case SDL_SCANCODE_KP_PERIOD: return VK_DELETE;
		default: break;
		}
	}
	if (scancode >= sizeof(sdl_keytable) / sizeof(*sdl_keytable))
		return 0;
	return sdl_keytable[scancode];
}

bool mouse_focus = true;
bool keyboard_focus = true;

#define MAX_CONTROLLERS 4
#define GAMEPAD_WHEEL_REPEAT_MS 135
#define GAMEPAD_CURSOR_AUTOHIDE_MS 3500

struct pad_device_slot {
	bool active;
	bool is_game_controller;
	bool has_rumble;
	SDL_GameController *ctrl;
	SDL_Joystick *joy;
	SDL_JoystickID instance_id;
	char name[128];
	uint16_t vendor_id;
	uint16_t product_id;
	bool btn_down[NR_PAD_SLOTS];
#ifdef __SWITCH__
	bool face_swapped_at_press[2];
#endif
	bool hat_dir[4]; // U, D, L, R
	float axes[6];   // LX, LY, RX, RY, LT, RT
	bool trigger_latched[2];
};

struct android_fallback_pad {
	bool active;
	int device_id;
	bool btn_down[NR_PAD_SLOTS];
	float axes[6];
	bool trigger_latched[2];
	float hat_x;
	float hat_y;
};

struct gamepad_config {
	bool initialized;
	bool phys_enabled;
	bool swap_ab_xy;
	int phys_left_stick_mode;
	int phys_right_stick_mode;
	int phys_dpad_mode;
	int vpad_left_stick_mode;
	int vpad_right_stick_mode;
	float deadzone;
	float cursor_speed;
	bool rumble_enabled;
	struct input_action_spec phys_bindings[NR_PAD_SLOTS];
};

static struct pad_device_slot pad_slots[MAX_CONTROLLERS];
static struct android_fallback_pad android_pad;
static struct gamepad_config gp_cfg;
static bool phys_slot_latched[NR_PAD_SLOTS];
static bool phys_slot_prev_raw[NR_PAD_SLOTS];
static bool phys_slot_movie_suppressed[NR_PAD_SLOTS];
static bool prev_mapped_joybtn[NR_PAD_SLOTS];
static bool prev_mapped_joyaxis_active = false;
static bool prev_raw_joyaxis_suppressed = false;
static bool phys_pad_had_any_down = false;
static uint32_t phys_slot_wheel_tick[NR_PAD_SLOTS];
static uint32_t stick_wheel_tick = 0;
static int stick_wheel_prev_dir = 0;
static uint32_t live_status_buttons_mask = 0;
static float live_status_axes[6] = { 0 };
static bool movie_pad_holding = false;
static uint32_t movie_pad_hold_start_tick = 0;

static void ensure_gamepad_config_defaults_locked(void);
static void process_gamepad_frame(void);
static void reset_gamepad_transient_state(void);

#ifdef __ANDROID__
struct deferred_keyevent {
	STAILQ_ENTRY(deferred_keyevent) entry;
	SDL_Event e;
};
static STAILQ_HEAD(deferred_keyevent_queue, deferred_keyevent) deferred_keyevent_queue =
	STAILQ_HEAD_INITIALIZER(deferred_keyevent_queue);
static uint32_t last_keyevent_timestamp;
#define DEFERRED_KEY_DELAY 10
#endif

// Stores mouse button events synthesized from touch events. We defer such
// events so the game first observes the pointer at the new position (hover),
// then at least one pressed frame, and then the release frame.
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
static SDL_Point last_touch_down_pos = {0, 0};
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

static void fire_deferred_events(bool from_parts_poll, bool allow_press);
static void update_long_touch_ctrl(void);

void input_parts_poll_begin(bool allow_press)
{
	in_parts_poll = true;
	if (allow_press)
		last_parts_poll_timestamp = SDL_GetTicks();
	handle_events();
	fire_deferred_events(true, allow_press);
	update_long_touch_ctrl();
}

void input_notify_parts_poll(bool allow_press, bool clicking)
{
	in_parts_poll = false;
	if (allow_press && clicking) {
		if (!synthetic_mouse_down_observed) {
			synthetic_mouse_down_observed = true;
			synthetic_mouse_down_observed_tick = SDL_GetTicks();
		}
		if (key_state[VK_LBUTTON])
			mouse_down_unobserved[VK_LBUTTON] = false;
		if (key_state[VK_RBUTTON])
			mouse_down_unobserved[VK_RBUTTON] = false;
		if (key_state[VK_MBUTTON])
			mouse_down_unobserved[VK_MBUTTON] = false;
	} else if (!clicking) {
		synthetic_mouse_up_observed = true;
		if (allow_press && nr_deferred_synthetic_mouse_events > 0 &&
		    deferred_synthetic_mouse_events[0].state == SDL_PRESSED) {
			synthetic_mouse_x = deferred_synthetic_mouse_events[0].x;
			synthetic_mouse_y = deferred_synthetic_mouse_events[0].y;
			has_synthetic_mouse_pos = true;
			synthetic_mouse_warped = false;
			synthetic_mouse_hover_observed = true;
		}
	}
}

void input_cancel_long_touch(void)
{
	long_touch_armed = false;
}

uint64_t input_get_touch_press_id(void)
{
	return touch_press_id;
}

uint64_t input_take_long_touch_click_cancel(void)
{
	uint64_t cancel = long_touch_click_cancel;
	long_touch_click_cancel = 0;
	return cancel;
}

static void release_long_touch_ctrl(void)
{
	input_cancel_long_touch();
	if (long_touch_ctrl_down) {
		long_touch_ctrl_down = false;
		sync_ctrl_key_state();
	}
}

#define MAX_ACTIVE_TOUCH_FINGERS 16
struct active_touch_finger {
	SDL_TouchID touch_id;
	SDL_FingerID finger_id;
};
static struct active_touch_finger active_touch_fingers[MAX_ACTIVE_TOUCH_FINGERS];
static int nr_active_touch_fingers = 0;
static bool multi_touch_active = false;
static bool two_finger_tap_candidate = false;
static bool two_finger_scrolled = false;
static uint32_t two_finger_tap_start_tick = 0;
#define TWO_FINGER_TAP_MAX_MS 300

static void clear_active_touch_fingers(void)
{
	nr_active_touch_fingers = 0;
}

static int track_touch_finger_down(SDL_TouchID touch_id, SDL_FingerID finger_id)
{
	for (int i = 0; i < nr_active_touch_fingers; i++) {
		if (active_touch_fingers[i].touch_id == touch_id &&
		    active_touch_fingers[i].finger_id == finger_id) {
			return nr_active_touch_fingers;
		}
	}
	if (nr_active_touch_fingers < MAX_ACTIVE_TOUCH_FINGERS) {
		active_touch_fingers[nr_active_touch_fingers].touch_id = touch_id;
		active_touch_fingers[nr_active_touch_fingers].finger_id = finger_id;
		nr_active_touch_fingers++;
	}
	return nr_active_touch_fingers;
}

static int track_touch_finger_up(SDL_TouchID touch_id, SDL_FingerID finger_id)
{
	for (int i = 0; i < nr_active_touch_fingers; i++) {
		if (active_touch_fingers[i].touch_id == touch_id &&
		    active_touch_fingers[i].finger_id == finger_id) {
			active_touch_fingers[i] = active_touch_fingers[--nr_active_touch_fingers];
			break;
		}
	}
	if (SDL_GetNumTouchFingers(touch_id) == 0) {
		int dst = 0;
		for (int i = 0; i < nr_active_touch_fingers; i++) {
			if (active_touch_fingers[i].touch_id != touch_id)
				active_touch_fingers[dst++] = active_touch_fingers[i];
		}
		nr_active_touch_fingers = dst;
	}
	return nr_active_touch_fingers;
}

static float scroll_gesture_y;
#define SCROLL_GESTURE_SENSITIVITY 0.02f  // 2% of screen height

static enum sact_keycode sdl_to_sact_button(int button)
{
	switch (button) {
	case SDL_BUTTON_LEFT:   return VK_LBUTTON;
	case SDL_BUTTON_RIGHT:  return VK_RBUTTON;
	case SDL_BUTTON_MIDDLE: return VK_MBUTTON;
	default:                return 0;
	}
}

bool key_is_down(enum sact_keycode code)
{
	if (code < 0 || code >= VK_NR_KEYCODES) {
		return false;
	}
	if (movie_has_active_playback() && is_movie_skip_suppressed_key(code)) {
		return false;
	}
	if (suppressed_movie_keys[code]) {
		return false;
	}
	if (code == VK_CONTROL) {
		key_state[VK_CONTROL] = kbd_ctrl_down || injected_vk_down[VK_CONTROL] || atomic_load(&touch_ctrl_skip)
			|| vpad_vk_down[VK_CONTROL] || pad_vk_down[VK_CONTROL] || long_touch_ctrl_down;
	}
	bool is_mouse_btn = (code == VK_LBUTTON || code == VK_RBUTTON || code == VK_MBUTTON);
	bool parts_polling = last_parts_poll_timestamp != 0 &&
		(SDL_GetTicks() - last_parts_poll_timestamp) < 250;
	if (!in_parts_poll && !(is_mouse_btn && parts_polling) && key_state[code]) {
		mouse_down_unobserved[code] = false;
		// Non-PartsEngine polling loops (e.g. IbisInputEngine/SACT2 Key_IsDown)
		// must mark synthetic touch presses observed so mouse_get_pos() tracks drags.
		if ((code == VK_LBUTTON || code == VK_RBUTTON) && !synthetic_mouse_down_observed) {
			synthetic_mouse_down_observed = true;
			synthetic_mouse_down_observed_tick = SDL_GetTicks();
		}
	}
	return key_state[code];
}

void key_set_injected(enum sact_keycode code, bool down)
{
	if (code <= 0 || code >= VK_NR_KEYCODES)
		return;
	injected_vk_down[code] = down;
	key_state[code] = down || kbd_key_state[code] || vpad_vk_down[code] || pad_vk_down[code];
}

void key_clear_flag(bool no_ctrl)
{
	for (int i = 0; i < VK_NR_KEYCODES; i++) {
		if (no_ctrl && i == VK_CONTROL)
			continue;
		key_state[i] = false;
		kbd_key_state[i] = false;
		vpad_vk_down[i] = false;
		pad_vk_down[i] = false;
		sdl_mouse_down[i] = false;
		prev_gamepad_mouse[i] = false;
		mouse_down_unobserved[i] = false;
		deferred_mouse_release[i] = false;
	}
	if (atomic_load(&touch_ctrl_skip) || long_touch_ctrl_down)
		key_state[VK_CONTROL] = true;
}

void mouse_get_pos(int *x, int *y)
{
	int wx, wy;
	fire_deferred_events(false, false);
	SDL_PumpEvents();
	SDL_GetMouseState(&wx, &wy);
	if (has_synthetic_mouse_pos) {
		if (!gamepad_cursor_active && !vpad_cursor_btn_down &&
		    nr_deferred_synthetic_mouse_events == 0 &&
		    synthetic_mouse_down_observed &&
		    (key_state[VK_LBUTTON] || key_state[VK_RBUTTON])) {
			if (synthetic_mouse_warped &&
			    (wx != synthetic_mouse_warp_raw_x || wy != synthetic_mouse_warp_raw_y)) {
				synthetic_mouse_warped = false;
			}
			if (!synthetic_mouse_warped) {
				synthetic_mouse_x = wx;
				synthetic_mouse_y = wy;
			}
		}
		wx = synthetic_mouse_x;
		wy = synthetic_mouse_y;
		if (!in_parts_poll && nr_deferred_synthetic_mouse_events > 0) {
			uint32_t now = SDL_GetTicks();
			bool parts_active = last_parts_poll_timestamp != 0 &&
				(now - last_parts_poll_timestamp) < 500;
			if (!parts_active) {
				SDL_MouseButtonEvent *ev = &deferred_synthetic_mouse_events[0];
				if (ev->state == SDL_PRESSED) {
					if (!synthetic_mouse_hover_observed &&
					    synthetic_mouse_x == ev->x && synthetic_mouse_y == ev->y) {
						synthetic_mouse_hover_observed = true;
						synthetic_mouse_hover_tick = now;
					}
				} else if (synthetic_mouse_down_observed &&
				           !synthetic_mouse_release_pos_observed &&
				           synthetic_mouse_x == ev->x && synthetic_mouse_y == ev->y) {
					bool is_drag_release = ev->x != synthetic_mouse_press_x ||
						ev->y != synthetic_mouse_press_y;
					int min_obs = (synthetic_mouse_release_queued_before_down_obs &&
						is_drag_release) ? 2 : 1;
					synthetic_mouse_release_pos_obs_count++;
					if (synthetic_mouse_release_pos_obs_count >= min_obs) {
						synthetic_mouse_release_pos_observed = true;
						synthetic_mouse_release_pos_tick = now;
					}
				}
			}
		}
	}
	*x = (wx - sdl.viewport.x) * sdl.w / sdl.viewport.w;
	*y = (wy - sdl.viewport.y) * sdl.h / sdl.viewport.h;
}

void mouse_set_pos(int x, int y)
{
	int wx = x * sdl.viewport.w / sdl.w + sdl.viewport.x;
	int wy = y * sdl.viewport.h / sdl.h + sdl.viewport.y;
	sync_gamepad_cursor_norm_from_window(wx, wy);
	if (has_synthetic_mouse_pos) {
		synthetic_mouse_x = wx;
		synthetic_mouse_y = wy;
		SDL_GetMouseState(&synthetic_mouse_warp_raw_x, &synthetic_mouse_warp_raw_y);
		synthetic_mouse_warped = true;
		return;
	}
	SDL_WarpMouseInWindow(sdl.window, wx, wy);
}

static int wheel_dir = 0;

void mouse_get_wheel(int *forward, int *back)
{
	*forward = wheel_dir > 0;
	*back = wheel_dir < 0;
}

void mouse_clear_wheel(void)
{
	wheel_dir = 0;
}

bool mouse_show_cursor(bool show)
{
	return SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE) >= 0;
}

static const struct input_text_handlers *legacy_text_handlers;
static const struct input_text_handlers *parts_text_handlers;
static const struct input_text_handlers *text_handlers;

#ifdef __ANDROID__
static uint32_t android_parts_text_event;
static unsigned android_parts_text_generation;
static atomic_uint android_parts_text_session;

struct android_parts_text {
	bool composing;
	int backspaces;
	char text[];
};

static void dispatch_android_parts_text(const SDL_UserEvent *event)
{
	struct android_parts_text *input = event->data1;
	if ((unsigned)event->code == atomic_load(&android_parts_text_session)) {
		if (input->composing) {
			text_handlers->edit(input->text, 0, 0);
		} else if (input->backspaces) {
			for (int i = 0; i < input->backspaces; i++)
				text_handlers->key(VK_BACK);
		} else {
			text_handlers->commit(input->text);
			if (strpbrk(input->text, "\r\n"))
				text_handlers->key(VK_RETURN);
		}
	}
	free(input);
}
#endif

static void update_text_handlers(void)
{
	const struct input_text_handlers *next = legacy_text_handlers ? legacy_text_handlers : parts_text_handlers;
	if (text_handlers == next)
		return;
	if (text_handlers && text_handlers->edit)
		text_handlers->edit("", 0, 0);
	if (text_handlers)
		SDL_StopTextInput();
	text_handlers = next;
#ifdef __ANDROID__
	unsigned session = 0;
	if (next && next == parts_text_handlers) {
		if (!android_parts_text_event)
			android_parts_text_event = SDL_RegisterEvents(1);
		if (android_parts_text_event != (uint32_t)-1) {
			if (++android_parts_text_generation == 0)
				++android_parts_text_generation;
			session = android_parts_text_generation;
		}
	}
	atomic_store(&android_parts_text_session, session);
#endif
	if (text_handlers)
		SDL_StartTextInput();
}

static void key_event(SDL_KeyboardEvent *e, bool pressed)
{
	enum sact_keycode code = sdl_to_sact_key(e->keysym.scancode, e->keysym.mod);
	if (code) {
		if (movie_has_active_playback() && is_movie_hold_key(code)) {
			if (code == VK_CONTROL) {
				kbd_ctrl_down = pressed;
				sync_ctrl_key_state();
			} else {
				kbd_key_state[code] = pressed;
				suppressed_movie_keys[code] = pressed;
				if (!pressed)
					key_state[code] = false;
			}
			if (pressed) {
				if (!e->repeat && (!movie_key_holding || movie_key_scancode != e->keysym.scancode)) {
					movie_key_holding = true;
					movie_key_scancode = e->keysym.scancode;
					movie_key_hold_start_tick = SDL_GetTicks();
					movie_hold_token++;
				}
			} else if (movie_key_holding && movie_key_scancode == e->keysym.scancode) {
				movie_key_holding = false;
				movie_early_release_hint = true;
			}
			return;
		}
		if (!pressed && suppressed_movie_keys[code]) {
			if (movie_key_holding && movie_key_scancode == e->keysym.scancode) {
				movie_key_holding = false;
			}
			kbd_key_state[code] = false;
			suppressed_movie_keys[code] = false;
			key_state[code] = false;
			return;
		}
		if (code == VK_CONTROL) {
			kbd_ctrl_down = pressed;
			sync_ctrl_key_state();
		} else {
			kbd_key_state[code] = pressed;
			key_state[code] = pressed || vpad_vk_down[code] || pad_vk_down[code];
		}
		if (pressed && text_handlers && text_handlers->key)
			text_handlers->key(code);
	}
}

static void mouse_event(SDL_MouseButtonEvent *e)
{
	enum sact_keycode code = sdl_to_sact_button(e->button);
	if (code) {
		if (e->state == SDL_PRESSED) {
			if (code == VK_RBUTTON)
				release_long_touch_ctrl();
			sdl_mouse_down[code] = true;
			deferred_mouse_release[code] = false;
			key_state[code] = true;
			mouse_down_unobserved[code] = true;
			mouse_down_timestamp[code] = SDL_GetTicks();
		} else if (key_state[code] && mouse_down_unobserved[code]) {
			sdl_mouse_down[code] = false;
			deferred_mouse_release[code] = true;
		} else {
			sdl_mouse_down[code] = false;
			if (!vpad_vk_down[code] && !pad_vk_down[code])
				key_state[code] = false;
			deferred_mouse_release[code] = false;
			mouse_down_unobserved[code] = false;
		}
	}
#ifdef DEBUGGER_ENABLED
	if (e->button == SDL_BUTTON_MIDDLE && e->state == SDL_PRESSED
			&& (dbg_start_in_debugger || dbg_dap))
		dbg_repl(DBG_STOP_PAUSE, NULL);
#endif
}

static void synthetic_mouse_event(SDL_MouseButtonEvent *e)
{
	if (e->state == SDL_PRESSED) {
		if (e->button == SDL_BUTTON_RIGHT) {
			release_long_touch_ctrl();
			sdl_mouse_down[VK_RBUTTON] = true;
			key_state[VK_RBUTTON] = true;
			return;
		}
		SDL_Point p = { .x = e->x, .y = e->y };
		if (SDL_PointInRect(&p, &sdl.viewport)) {
			if (atomic_load(&long_press_ctrl_enabled))
				touch_press_id = ++next_touch_press_id;
			sdl_mouse_down[VK_LBUTTON] = true;
			key_state[VK_LBUTTON] = true;
		} else {
#ifdef __ANDROID__
			SDL_AndroidSendMessage(COMMAND_TOGGLE_CTRL_OVERLAY, 0);
#else
			release_long_touch_ctrl();
			sdl_mouse_down[VK_RBUTTON] = true;
			key_state[VK_RBUTTON] = true;
#endif
		}
	} else {
		touch_press_id = 0;
		sdl_mouse_down[VK_LBUTTON] = false;
		sdl_mouse_down[VK_RBUTTON] = false;
		if (!vpad_vk_down[VK_LBUTTON] && !pad_vk_down[VK_LBUTTON])
			key_state[VK_LBUTTON] = false;
		if (!vpad_vk_down[VK_RBUTTON] && !pad_vk_down[VK_RBUTTON])
			key_state[VK_RBUTTON] = false;
	}
}

#define JOYAXIS_DEADZONE 13500

enum joyaxis_axis {
	JOYAXIS_X,
	JOYAXIS_Y
};

enum joyaxis_direction {
	JOYAXIS_DIR_LEFT_UP    = -1,
	JOYAXIS_DIR_NONE       =  0,
	JOYAXIS_DIR_RIGHT_DOWN =  1
};

enum joyaxis_state {
	// A: Initial state
	// - when joy_clear_flag is called, goes on long cooldown
	//   and then enters JOYAXIS_STATE_B
	JOYAXIS_STATE_A,
	// B: Held state
	// - when joy_clear_flag is called, goes on short cooldown
	//   state remains JOYAXIS_STATE_B until direction changes
	JOYAXIS_STATE_B,
};

struct joyaxis {
	enum joyaxis_state state;
	unsigned long cooldown_expire_time;
	enum joyaxis_direction real_direction;
	enum joyaxis_direction logical_direction;
};

struct joyaxis joyaxis_state[2] = { 0 };

#define NR_JOYBUTTON_STATES 32
bool joybutton_state[NR_JOYBUTTON_STATES];

static int sact_joy_code_to_pad_slot(int code)
{
	switch (code) {
	case 5:  return PAD_SLOT_A;
	case 6:  return PAD_SLOT_B;
	case 7:  return PAD_SLOT_X;
	case 8:  return PAD_SLOT_Y;
	case 9:  return PAD_SLOT_LB;
	case 10: return PAD_SLOT_RB;
	case 11: return PAD_SLOT_BACK;
	case 12: return PAD_SLOT_START;
	case 13: return PAD_SLOT_GUIDE;
	case 14: return PAD_SLOT_L3;
	case 15: return PAD_SLOT_R3;
	case 16: return PAD_SLOT_DPAD_LEFT;
	case 17: return PAD_SLOT_DPAD_RIGHT;
	case 18: return PAD_SLOT_DPAD_UP;
	case 19: return PAD_SLOT_DPAD_DOWN;
	case 20: return PAD_SLOT_LT;
	case 21: return PAD_SLOT_RT;
	default: return -1;
	}
}

static unsigned long joy_time(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((unsigned long)ts.tv_sec * 1000) + ((unsigned long)ts.tv_nsec / 1000000);
}

static bool joy_check_axis(struct joyaxis *axis, enum joyaxis_direction direction)
{
	if (axis->logical_direction == direction)
		return true;
	if (!axis->cooldown_expire_time)
		return false;
	// on cooldown: check if cooldown expired
	if (joy_time() >= axis->cooldown_expire_time) {
		axis->state = JOYAXIS_STATE_B;
		axis->cooldown_expire_time = 0;
		axis->logical_direction = axis->real_direction;
	}
	return axis->logical_direction == direction;
}

bool joy_key_is_down(uint8_t code)
{
	// Default SACT key mapping in comments below
	switch (code) {
	case 1: // Left Arrow
		return joy_check_axis(&joyaxis_state[JOYAXIS_X], JOYAXIS_DIR_LEFT_UP);
	case 2: // Right Arrow
		return joy_check_axis(&joyaxis_state[JOYAXIS_X], JOYAXIS_DIR_RIGHT_DOWN);
	case 3: // Up Arrow
		return joy_check_axis(&joyaxis_state[JOYAXIS_Y], JOYAXIS_DIR_LEFT_UP);
	case 4: // Down Arrow
		return joy_check_axis(&joyaxis_state[JOYAXIS_Y], JOYAXIS_DIR_RIGHT_DOWN);
	default: {
		int slot = sact_joy_code_to_pad_slot(code);
		if (slot >= 0 && slot < NR_JOYBUTTON_STATES)
			return joybutton_state[slot];
		return false;
	}
	}
}

static float joy_calc_deg(int x, int y)
{
	// calculate angle (degrees)
	float a = atan2f(y, x) * (180.0f/M_PI);
	if (a < 0)
		a += 360.0f;
	// rotate so that up is 0/360
	a += 90.0f;
	if (a > 360.0f)
		a -= 360.0f;
	return a;
}

static float joy_calc_pow(int x, int y)
{
	// XXX: in theory this should be scaled according to the angle so that
	//      (32767,32767) returns exactly 1, but alicesoft themselves seem
	//      to simply clamp pythagoras as below
	return min(1.0f, sqrtf(x*x + y*y) / 32767.0f);
}

static float joy_calc_trigger(int left, int right)
{
	// for whatever reason alicesoft treats triggers as an analog stick
	// (at least on an xbox controller)
	float v = 315.0f;
	v += ((float)left/32767.0f) * 90.0f;
	v -= ((float)right/32767.0f) * 90.0f;
	if (v < 0)
		v += 360.0f;
	if (v > 360.0f)
		v -= 360.0f;
	return v;
}

static struct pad_device_slot *get_active_pad_slot_locked(int controller)
{
	if (controller < 0)
		return NULL;
	int count = 0;
	for (int i = 0; i < MAX_CONTROLLERS; i++) {
		if (pad_slots[i].active) {
			if (count == controller)
				return &pad_slots[i];
			count++;
		}
	}
	return NULL;
}

int joy_get_num_controllers(void)
{
	int count = 0;
	pthread_mutex_lock(&gp_mutex);
	for (int i = 0; i < MAX_CONTROLLERS; i++) {
		if (pad_slots[i].active)
			count++;
	}
	if (count == 0 && android_pad.active)
		count = 1;
	pthread_mutex_unlock(&gp_mutex);
	return count;
}

static bool raw_joy_btn_slot_locked(const struct pad_device_slot *s, int pad_slot)
{
	if (!s || !s->active || pad_slot < 0 || pad_slot >= NR_PAD_SLOTS)
		return false;
	if (pad_slot == PAD_SLOT_DPAD_UP && s->hat_dir[0]) return true;
	if (pad_slot == PAD_SLOT_DPAD_DOWN && s->hat_dir[1]) return true;
	if (pad_slot == PAD_SLOT_DPAD_LEFT && s->hat_dir[2]) return true;
	if (pad_slot == PAD_SLOT_DPAD_RIGHT && s->hat_dir[3]) return true;
	return s->btn_down[pad_slot];
}

static bool raw_android_pad_btn_locked(int pad_slot)
{
	if (!android_pad.active || pad_slot < 0 || pad_slot >= NR_PAD_SLOTS)
		return false;
	if (pad_slot == PAD_SLOT_DPAD_UP && android_pad.hat_y < -0.5f) return true;
	if (pad_slot == PAD_SLOT_DPAD_DOWN && android_pad.hat_y > 0.5f) return true;
	if (pad_slot == PAD_SLOT_DPAD_LEFT && android_pad.hat_x < -0.5f) return true;
	if (pad_slot == PAD_SLOT_DPAD_RIGHT && android_pad.hat_x > 0.5f) return true;
	if (pad_slot == PAD_SLOT_LT && android_pad.axes[4] > 0.35f) return true;
	if (pad_slot == PAD_SLOT_RT && android_pad.axes[5] > 0.35f) return true;
	return android_pad.btn_down[pad_slot];
}

bool joy_is_button_down(int controller, int code)
{
	if (controller < 0)
		return false;
	if (controller == 0 && code >= 1 && code <= 21 && joy_key_is_down((uint8_t)code))
		return true;

	bool down = false;
	const float axis_thresh = (float)JOYAXIS_DEADZONE / 32767.0f;
	pthread_mutex_lock(&gp_mutex);
	struct pad_device_slot *slot = get_active_pad_slot_locked(controller);
	if (slot) {
		switch (code) {
		case 1: down = slot->axes[0] < -axis_thresh; break;
		case 2: down = slot->axes[0] >  axis_thresh; break;
		case 3: down = slot->axes[1] < -axis_thresh; break;
		case 4: down = slot->axes[1] >  axis_thresh; break;
		default: {
			int pad_slot = sact_joy_code_to_pad_slot(code);
			if (pad_slot >= 0)
				down = raw_joy_btn_slot_locked(slot, pad_slot);
			break;
		}
		}
	} else if (controller == 0 && android_pad.active) {
		switch (code) {
		case 1: down = android_pad.axes[0] < -axis_thresh; break;
		case 2: down = android_pad.axes[0] >  axis_thresh; break;
		case 3: down = android_pad.axes[1] < -axis_thresh; break;
		case 4: down = android_pad.axes[1] >  axis_thresh; break;
		default: {
			int pad_slot = sact_joy_code_to_pad_slot(code);
			if (pad_slot >= 0)
				down = raw_android_pad_btn_locked(pad_slot);
			break;
		}
		}
	}
	pthread_mutex_unlock(&gp_mutex);
	return down;
}

float joy_get_axis_value(int controller, int axis)
{
	if (axis < 0 || axis >= 6)
		return 0.0f;
	float v = 0.0f;
	pthread_mutex_lock(&gp_mutex);
	struct pad_device_slot *slot = get_active_pad_slot_locked(controller);
	if (slot) {
		v = slot->axes[axis];
	} else if (controller == 0 && android_pad.active) {
		v = android_pad.axes[axis];
	}
	pthread_mutex_unlock(&gp_mutex);
	return v;
}

bool joy_get_stick_status(int controller, int type, float *deg, float *pow)
{
	float ax[6] = { 0 };
	bool valid = false;
	pthread_mutex_lock(&gp_mutex);
	struct pad_device_slot *slot = get_active_pad_slot_locked(controller);
	if (slot) {
		memcpy(ax, slot->axes, sizeof(ax));
		valid = true;
	} else if (controller == 0 && android_pad.active) {
		memcpy(ax, android_pad.axes, sizeof(ax));
		valid = true;
	}
	pthread_mutex_unlock(&gp_mutex);
	if (!valid)
		goto invalid;
	if (type == 0) {
		int x = (int)(ax[0] * 32767.0f);
		int y = (int)(ax[1] * 32767.0f);
		if (abs(x) < JOYAXIS_DEADZONE && abs(y) < JOYAXIS_DEADZONE) {
			*pow = 0.0f;
			*deg = 0.0f;
		} else {
			*pow = joy_calc_pow(x, y);
			*deg = joy_calc_deg(x, y);
		}
		return true;
	}
	if (type == 1) {
		int left = (int)(ax[4] * 32767.0f);
		int right = (int)(ax[5] * 32767.0f);
		*pow = 1.0f;
		*deg = joy_calc_trigger(left, right);
		return true;
	}
invalid:
	*deg = 0.0f;
	*pow = 0.0f;
	return false;
}

bool joy_get_digital_stick_status(int controller, int type, int *left, int *right, int *up, int *down)
{
	float ax = 0.0f, ay = 0.0f;
	bool valid = false;
	int base = (type == 1) ? 2 : 0;
	pthread_mutex_lock(&gp_mutex);
	struct pad_device_slot *slot = get_active_pad_slot_locked(controller);
	if (slot) {
		ax = slot->axes[base];
		ay = slot->axes[base + 1];
		valid = true;
	} else if (controller == 0 && android_pad.active) {
		ax = android_pad.axes[base];
		ay = android_pad.axes[base + 1];
		valid = true;
	}
	pthread_mutex_unlock(&gp_mutex);
	if (left) *left = ax < -0.45f ? 1 : 0;
	if (right) *right = ax > 0.45f ? 1 : 0;
	if (up) *up = ay < -0.45f ? 1 : 0;
	if (down) *down = ay > 0.45f ? 1 : 0;
	return valid;
}

void joy_rumble(int controller, int magnitude, int duration_ms)
{
	if (magnitude <= 0 || duration_ms <= 0)
		return;
	SDL_GameController *ctrl = NULL;
	pthread_mutex_lock(&gp_mutex);
	ensure_gamepad_config_defaults_locked();
	if (gp_cfg.rumble_enabled) {
		struct pad_device_slot *slot = get_active_pad_slot_locked(controller);
		if (slot && slot->is_game_controller && slot->ctrl && slot->has_rumble)
			ctrl = slot->ctrl;
	}
	pthread_mutex_unlock(&gp_mutex);
	if (!ctrl)
		return;
	uint16_t amp = (uint16_t)(min(100, magnitude) * 655);
	SDL_GameControllerRumble(ctrl, amp, amp, (Uint32)min(2000, duration_ms));
}

static void joyaxis_clear_flag(struct joyaxis *joy)
{
	// put axis on cooldown
	if (joy->logical_direction) {
		joy->logical_direction = 0;
		if (joy->state == JOYAXIS_STATE_A) {
			joy->cooldown_expire_time = joy_time() + 300;
		} else {
			joy->cooldown_expire_time = joy_time() + 50;
		}
	}
}

void joy_clear_flag(void)
{
	for (int i = 0; i < NR_JOYBUTTON_STATES; i++) {
		joybutton_state[i] = false;
	}
	joyaxis_clear_flag(&joyaxis_state[JOYAXIS_X]);
	joyaxis_clear_flag(&joyaxis_state[JOYAXIS_Y]);
}

static void joyaxis_update(enum joyaxis_axis axis, int value)
{
	// determine direction
	int direction = 0;
	if (value < -JOYAXIS_DEADZONE)
		direction = -1;
	else if (value > JOYAXIS_DEADZONE)
		direction = 1;

	// reset axis state if direction changed
	if (direction != joyaxis_state[axis].real_direction) {
		joyaxis_state[axis].state = JOYAXIS_STATE_A;
		joyaxis_state[axis].cooldown_expire_time = 0;
		joyaxis_state[axis].real_direction = direction;
		joyaxis_state[axis].logical_direction = direction;
	}
}

static float normalize_sdl_axis(int16_t v, bool is_trigger)
{
	if (is_trigger) {
		if (v <= 0) return 0.0f;
		float f = (float)v / 32767.0f;
		return f > 1.0f ? 1.0f : f;
	}
	float f = (v < 0) ? ((float)v / 32768.0f) : ((float)v / 32767.0f);
	if (f < -1.0f) f = -1.0f;
	if (f > 1.0f) f = 1.0f;
	return f;
}

static void sanitize_utf8_copy(const char *src, char *dst, size_t dst_size)
{
	if (!dst || dst_size == 0)
		return;
	if (!src) {
		dst[0] = '\0';
		return;
	}
	const unsigned char *s = (const unsigned char *)src;
	size_t di = 0;
	while (*s && di + 1 < dst_size) {
		unsigned char b0 = s[0];
		if (b0 < 0x80) {
			dst[di++] = (char)b0;
			s += 1;
		} else if (b0 >= 0xC2 && b0 <= 0xDF && (s[1] & 0xC0) == 0x80) {
			if (di + 2 >= dst_size) break;
			dst[di++] = (char)b0;
			dst[di++] = (char)s[1];
			s += 2;
		} else if (b0 >= 0xE0 && b0 <= 0xEF &&
		           (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
			uint32_t cp = ((uint32_t)(b0 & 0x0F) << 12) |
			              ((uint32_t)(s[1] & 0x3F) << 6) |
			              (uint32_t)(s[2] & 0x3F);
			if (cp >= 0x800 && !(cp >= 0xD800 && cp <= 0xDFFF)) {
				if (di + 3 >= dst_size) break;
				dst[di++] = (char)b0;
				dst[di++] = (char)s[1];
				dst[di++] = (char)s[2];
				s += 3;
			} else {
				dst[di++] = '?';
				s += 1;
			}
		} else if (b0 >= 0xF0 && b0 <= 0xF4 &&
		           (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
			uint32_t cp = ((uint32_t)(b0 & 0x07) << 18) |
			              ((uint32_t)(s[1] & 0x3F) << 12) |
			              ((uint32_t)(s[2] & 0x3F) << 6) |
			              (uint32_t)(s[3] & 0x3F);
			if (cp >= 0x10000 && cp <= 0x10FFFF) {
				if (di + 4 >= dst_size) break;
				dst[di++] = (char)b0;
				dst[di++] = (char)s[1];
				dst[di++] = (char)s[2];
				dst[di++] = (char)s[3];
				s += 4;
			} else {
				dst[di++] = '?';
				s += 1;
			}
		} else {
			dst[di++] = '?';
			s += 1;
		}
	}
	dst[di] = '\0';
}

static struct pad_device_slot *find_pad_by_instance_locked(SDL_JoystickID id)
{
	for (int i = 0; i < MAX_CONTROLLERS; i++) {
		if (pad_slots[i].active && pad_slots[i].instance_id == id)
			return &pad_slots[i];
	}
	return NULL;
}

static void notify_android_controller_count(void)
{
#ifdef __ANDROID__
	static int last_notified = -1;
	int sdl_cnt = 0;
	pthread_mutex_lock(&gp_mutex);
	for (int i = 0; i < MAX_CONTROLLERS; i++) {
		if (pad_slots[i].active)
			sdl_cnt++;
	}
	pthread_mutex_unlock(&gp_mutex);
	if (sdl_cnt != last_notified) {
		last_notified = sdl_cnt;
		SDL_AndroidSendMessage(COMMAND_GAMEPAD_UI_EVENT,
			GAMEPAD_UI_CONTROLLER_COUNT | (sdl_cnt << 8));
	}
#endif
}

static void open_game_controller_index(int device_index)
{
	SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(device_index);
	pthread_mutex_lock(&gp_mutex);
	bool already_open = (id >= 0 && find_pad_by_instance_locked(id) != NULL);
	pthread_mutex_unlock(&gp_mutex);
	if (already_open)
		return;

	SDL_GameController *ctrl = SDL_GameControllerOpen(device_index);
	if (!ctrl)
		return;
	SDL_Joystick *joy = SDL_GameControllerGetJoystick(ctrl);
	SDL_JoystickID real_id = SDL_JoystickInstanceID(joy);
	const char *nm = SDL_GameControllerName(ctrl);
	uint16_t vendor_id = SDL_GameControllerGetVendor(ctrl);
	uint16_t product_id = SDL_GameControllerGetProduct(ctrl);
	bool has_rumble = SDL_GameControllerHasRumble(ctrl) == SDL_TRUE;

	bool inserted = false;
	pthread_mutex_lock(&gp_mutex);
	if (!find_pad_by_instance_locked(real_id)) {
		for (int i = 0; i < MAX_CONTROLLERS; i++) {
			if (!pad_slots[i].active) {
				struct pad_device_slot *s = &pad_slots[i];
				memset(s, 0, sizeof(*s));
				s->active = true;
				s->is_game_controller = true;
				s->ctrl = ctrl;
				s->joy = joy;
				s->instance_id = real_id;
				sanitize_utf8_copy((nm && nm[0]) ? nm : "Game Controller", s->name, sizeof(s->name));
				s->vendor_id = vendor_id;
				s->product_id = product_id;
				s->has_rumble = has_rumble;
				inserted = true;
				break;
			}
		}
	}
	pthread_mutex_unlock(&gp_mutex);

	if (inserted) {
		notify_android_controller_count();
		return;
	}
	SDL_GameControllerClose(ctrl);
}

static void open_generic_joystick_index(int device_index)
{
	if (SDL_IsGameController(device_index))
		return;
	SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(device_index);
	pthread_mutex_lock(&gp_mutex);
	bool already_open = (id >= 0 && find_pad_by_instance_locked(id) != NULL);
	pthread_mutex_unlock(&gp_mutex);
	if (already_open)
		return;

	SDL_Joystick *joy = SDL_JoystickOpen(device_index);
	if (!joy)
		return;
	const char *nm = SDL_JoystickName(joy);
	int nbuttons = SDL_JoystickNumButtons(joy);
	// Ignore Android sensor/accelerometer pseudo-joysticks without buttons.
	if (nbuttons <= 0 || (nm && (strstr(nm, "Accelerometer") || strstr(nm, "Sensor") || strstr(nm, "uinput")))) {
		SDL_JoystickClose(joy);
		return;
	}
	SDL_JoystickID real_id = SDL_JoystickInstanceID(joy);
	uint16_t vendor_id = SDL_JoystickGetVendor(joy);
	uint16_t product_id = SDL_JoystickGetProduct(joy);

	bool inserted = false;
	pthread_mutex_lock(&gp_mutex);
	if (!find_pad_by_instance_locked(real_id)) {
		for (int i = 0; i < MAX_CONTROLLERS; i++) {
			if (!pad_slots[i].active) {
				struct pad_device_slot *s = &pad_slots[i];
				memset(s, 0, sizeof(*s));
				s->active = true;
				s->is_game_controller = false;
				s->ctrl = NULL;
				s->joy = joy;
				s->instance_id = real_id;
				sanitize_utf8_copy((nm && nm[0]) ? nm : "Generic Gamepad", s->name, sizeof(s->name));
				s->vendor_id = vendor_id;
				s->product_id = product_id;
				inserted = true;
				break;
			}
		}
	}
	pthread_mutex_unlock(&gp_mutex);

	if (inserted) {
		notify_android_controller_count();
		return;
	}
	SDL_JoystickClose(joy);
}

static void close_controller_instance(SDL_JoystickID id)
{
	SDL_GameController *ctrl = NULL;
	SDL_Joystick *joy = NULL;
	bool was_gc = false;
	bool found = false;

	pthread_mutex_lock(&gp_mutex);
	struct pad_device_slot *s = find_pad_by_instance_locked(id);
	if (s) {
		was_gc = s->is_game_controller;
		ctrl = s->ctrl;
		joy = s->joy;
		memset(s, 0, sizeof(*s));
		found = true;
	}
	pthread_mutex_unlock(&gp_mutex);

	if (!found)
		return;
	if (was_gc && ctrl) {
		SDL_GameControllerClose(ctrl);
	} else if (joy) {
		SDL_JoystickClose(joy);
	}
	notify_android_controller_count();
}

static bool raw_left_stick_drives_joyaxis_locked(void)
{
	return !gp_cfg.initialized || !gp_cfg.phys_enabled || gp_cfg.phys_left_stick_mode == STICK_MODE_JOYPAD;
}

static void controller_axis_event(SDL_ControllerAxisEvent *e)
{
	bool update_raw_joyaxis = false;
	bool trig_edge = false;
	int trig_slot = -1;
	bool trig_state = false;

	pthread_mutex_lock(&gp_mutex);
	update_raw_joyaxis = raw_left_stick_drives_joyaxis_locked();
	struct pad_device_slot *s = find_pad_by_instance_locked(e->which);
	switch (e->axis) {
	case SDL_CONTROLLER_AXIS_LEFTX:
		if (s) s->axes[0] = normalize_sdl_axis(e->value, false);
		break;
	case SDL_CONTROLLER_AXIS_LEFTY:
		if (s) s->axes[1] = normalize_sdl_axis(e->value, false);
		break;
	case SDL_CONTROLLER_AXIS_RIGHTX:
		if (s) s->axes[2] = normalize_sdl_axis(e->value, false);
		break;
	case SDL_CONTROLLER_AXIS_RIGHTY:
		if (s) s->axes[3] = normalize_sdl_axis(e->value, false);
		break;
	case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
		if (s) {
			float v = normalize_sdl_axis(e->value, true);
			bool prev = s->trigger_latched[0];
			s->axes[4] = v;
			if (!s->trigger_latched[0] && v > 0.35f) s->trigger_latched[0] = true;
			else if (s->trigger_latched[0] && v < 0.25f) s->trigger_latched[0] = false;
			s->btn_down[PAD_SLOT_LT] = s->trigger_latched[0];
			if (s->trigger_latched[0] != prev) {
				trig_edge = true;
				trig_slot = PAD_SLOT_LT;
				trig_state = s->trigger_latched[0];
			}
		}
		break;
	case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
		if (s) {
			float v = normalize_sdl_axis(e->value, true);
			bool prev = s->trigger_latched[1];
			s->axes[5] = v;
			if (!s->trigger_latched[1] && v > 0.35f) s->trigger_latched[1] = true;
			else if (s->trigger_latched[1] && v < 0.25f) s->trigger_latched[1] = false;
			s->btn_down[PAD_SLOT_RT] = s->trigger_latched[1];
			if (s->trigger_latched[1] != prev) {
				trig_edge = true;
				trig_slot = PAD_SLOT_RT;
				trig_state = s->trigger_latched[1];
			}
		}
		break;
	default:
		break;
	}
	pthread_mutex_unlock(&gp_mutex);

	if (update_raw_joyaxis) {
		if (e->axis == SDL_CONTROLLER_AXIS_LEFTX)
			joyaxis_update(JOYAXIS_X, e->value);
		else if (e->axis == SDL_CONTROLLER_AXIS_LEFTY)
			joyaxis_update(JOYAXIS_Y, e->value);
	}
	if (trig_edge && trig_slot >= 0 && trig_slot < NR_JOYBUTTON_STATES)
		joybutton_state[trig_slot] = trig_state;
}

static void controller_button_event(SDL_ControllerButtonEvent *e)
{
	bool pressed = e->state == SDL_PRESSED;
	// Only SDL_CONTROLLER_BUTTON_A (0) .. SDL_CONTROLLER_BUTTON_DPAD_RIGHT (14)
	// correspond to PAD_SLOT_A .. PAD_SLOT_DPAD_RIGHT; 15/16 are MISC1/PADDLE1, not LT/RT.
	if (e->button < 15) {
		int button = e->button;
		pthread_mutex_lock(&gp_mutex);
		struct pad_device_slot *s = find_pad_by_instance_locked(e->which);
#ifdef __SWITCH__
		if (s && (button == SDL_CONTROLLER_BUTTON_A || button == SDL_CONTROLLER_BUTTON_B)) {
			if (pressed)
				s->face_swapped_at_press[button] = alicehub_switch_face_button(e->which, button) != button;
			if (s->face_swapped_at_press[button])
				button = button == SDL_CONTROLLER_BUTTON_A ? SDL_CONTROLLER_BUTTON_B : SDL_CONTROLLER_BUTTON_A;
		}
#endif
		joybutton_state[button] = pressed;
		if (s)
			s->btn_down[button] = pressed;
		pthread_mutex_unlock(&gp_mutex);
	}
}

static void generic_joy_axis_event(SDL_JoyAxisEvent *e)
{
	bool update_raw_joyaxis = false;
	bool valid_generic = false;
	bool trig_edge = false;
	int trig_slot = -1;
	bool trig_state = false;

	pthread_mutex_lock(&gp_mutex);
	update_raw_joyaxis = raw_left_stick_drives_joyaxis_locked();
	struct pad_device_slot *s = find_pad_by_instance_locked(e->which);
	if (s && !s->is_game_controller) {
		valid_generic = true;
		if (e->axis < 4) {
			s->axes[e->axis] = normalize_sdl_axis(e->value, false);
		} else if (e->axis == 4 || e->axis == 5) {
			int idx = (e->axis == 4) ? 0 : 1;
			bool prev = s->trigger_latched[idx];
			float v = (normalize_sdl_axis(e->value, false) + 1.0f) * 0.5f;
			s->axes[4 + idx] = v;
			if (!s->trigger_latched[idx] && v > 0.35f) s->trigger_latched[idx] = true;
			else if (s->trigger_latched[idx] && v < 0.25f) s->trigger_latched[idx] = false;
			s->btn_down[PAD_SLOT_LT + idx] = s->trigger_latched[idx];
			if (s->trigger_latched[idx] != prev) {
				trig_edge = true;
				trig_slot = PAD_SLOT_LT + idx;
				trig_state = s->trigger_latched[idx];
			}
		}
	}
	pthread_mutex_unlock(&gp_mutex);

	if (!valid_generic)
		return;
	if (update_raw_joyaxis) {
		if (e->axis == 0) joyaxis_update(JOYAXIS_X, e->value);
		else if (e->axis == 1) joyaxis_update(JOYAXIS_Y, e->value);
	}
	if (trig_edge && trig_slot >= 0 && trig_slot < NR_JOYBUTTON_STATES)
		joybutton_state[trig_slot] = trig_state;
}

static void generic_joy_hat_event(SDL_JoyHatEvent *e)
{
	if (e->hat != 0)
		return;
	bool prev_hat[4] = { false, false, false, false };
	bool next_hat[4] = {
		(e->value & SDL_HAT_UP) != 0,
		(e->value & SDL_HAT_DOWN) != 0,
		(e->value & SDL_HAT_LEFT) != 0,
		(e->value & SDL_HAT_RIGHT) != 0,
	};
	bool valid_generic = false;
	bool sync_raw_btns = false;

	pthread_mutex_lock(&gp_mutex);
	sync_raw_btns = !gp_cfg.initialized || !gp_cfg.phys_enabled;
	struct pad_device_slot *s = find_pad_by_instance_locked(e->which);
	if (s && !s->is_game_controller) {
		memcpy(prev_hat, s->hat_dir, sizeof(prev_hat));
		memcpy(s->hat_dir, next_hat, sizeof(s->hat_dir));
		valid_generic = true;
	}
	pthread_mutex_unlock(&gp_mutex);

	if (valid_generic && sync_raw_btns) {
		for (int d = 0; d < 4; d++) {
			if (next_hat[d] != prev_hat[d])
				joybutton_state[PAD_SLOT_DPAD_UP + d] = next_hat[d];
		}
	}
}

static void generic_joy_button_event(SDL_JoyButtonEvent *e)
{
	if (e->button >= 12)
		return;
	static const int fallback_map[12] = {
		PAD_SLOT_A, PAD_SLOT_B, PAD_SLOT_X, PAD_SLOT_Y,
		PAD_SLOT_LB, PAD_SLOT_RB, PAD_SLOT_LT, PAD_SLOT_RT,
		PAD_SLOT_BACK, PAD_SLOT_START, PAD_SLOT_L3, PAD_SLOT_R3
	};
	int slot = fallback_map[e->button];
	bool pressed = e->state == SDL_PRESSED;
	bool valid_generic = false;

	pthread_mutex_lock(&gp_mutex);
	struct pad_device_slot *s = find_pad_by_instance_locked(e->which);
	if (s && !s->is_game_controller) {
		s->btn_down[slot] = pressed;
		valid_generic = true;
	}
	pthread_mutex_unlock(&gp_mutex);

	if (valid_generic && slot >= 0 && slot < NR_JOYBUTTON_STATES)
		joybutton_state[slot] = pressed;
}

static void ensure_gamepad_config_defaults_locked(void)
{
	if (gp_cfg.initialized)
		return;
	memset(&gp_cfg, 0, sizeof(gp_cfg));
	gp_cfg.initialized = true;
	gp_cfg.phys_enabled = false;
	gp_cfg.swap_ab_xy = false;
	gp_cfg.phys_left_stick_mode = STICK_MODE_MOUSE;
	gp_cfg.phys_right_stick_mode = STICK_MODE_WHEEL;
	gp_cfg.phys_dpad_mode = STICK_MODE_ARROWS;
	gp_cfg.vpad_left_stick_mode = STICK_MODE_MOUSE;
	gp_cfg.vpad_right_stick_mode = STICK_MODE_WHEEL;
	gp_cfg.deadzone = 0.18f;
	gp_cfg.cursor_speed = 720.0f;
	gp_cfg.rumble_enabled = false;

	gp_cfg.phys_bindings[PAD_SLOT_A] = (struct input_action_spec){ INPUT_ACTION_MOUSE_BTN, VK_LBUTTON, 0, false };
	gp_cfg.phys_bindings[PAD_SLOT_B] = (struct input_action_spec){ INPUT_ACTION_MOUSE_BTN, VK_RBUTTON, 0, false };
	gp_cfg.phys_bindings[PAD_SLOT_X] = (struct input_action_spec){ INPUT_ACTION_VK, VK_SPACE, 0, false };
	gp_cfg.phys_bindings[PAD_SLOT_Y] = (struct input_action_spec){ INPUT_ACTION_VK, VK_RETURN, 0, false };
	gp_cfg.phys_bindings[PAD_SLOT_LB] = (struct input_action_spec){ INPUT_ACTION_MOUSE_WHEEL, 1, 0, false };
	gp_cfg.phys_bindings[PAD_SLOT_RB] = (struct input_action_spec){ INPUT_ACTION_MOUSE_WHEEL, -1, 0, false };
	gp_cfg.phys_bindings[PAD_SLOT_LT] = (struct input_action_spec){ INPUT_ACTION_VK, VK_ESCAPE, 0, false };
	gp_cfg.phys_bindings[PAD_SLOT_RT] = (struct input_action_spec){ INPUT_ACTION_CTRL_TOGGLE, 0, 0, false };
	gp_cfg.phys_bindings[PAD_SLOT_BACK] = (struct input_action_spec){ INPUT_ACTION_EMU_CMD, INPUT_EMU_TOGGLE_CURSOR, 0, false };
	gp_cfg.phys_bindings[PAD_SLOT_START] = (struct input_action_spec){ INPUT_ACTION_EMU_CMD, INPUT_EMU_OPEN_MENU, 0, false };
	gp_cfg.phys_bindings[PAD_SLOT_GUIDE] = (struct input_action_spec){ INPUT_ACTION_EMU_CMD, INPUT_EMU_TOGGLE_VPAD, 0, false };
	gp_cfg.phys_bindings[PAD_SLOT_L3] = (struct input_action_spec){ INPUT_ACTION_VK, VK_SHIFT, 0, false };
	gp_cfg.phys_bindings[PAD_SLOT_R3] = (struct input_action_spec){ INPUT_ACTION_MOUSE_BTN, VK_MBUTTON, 0, false };
}

static int remap_abxy_slot(int slot, bool swap)
{
	if (!swap)
		return slot;
	switch (slot) {
	case PAD_SLOT_A: return PAD_SLOT_B;
	case PAD_SLOT_B: return PAD_SLOT_A;
	case PAD_SLOT_X: return PAD_SLOT_Y;
	case PAD_SLOT_Y: return PAD_SLOT_X;
	default: return slot;
	}
}

static bool is_movie_skip_action(const struct input_action_spec *act)
{
	if (!act) return false;
	if (act->type == INPUT_ACTION_MOUSE_BTN || act->type == INPUT_ACTION_CTRL_TOGGLE)
		return true;
	if (act->type == INPUT_ACTION_VK) {
		return act->code == VK_RETURN || act->code == VK_SPACE || act->code == VK_CONTROL;
	}
	return false;
}

static void emit_emu_cmd_to_ui(int cmd)
{
#ifndef __ANDROID__
	if (cmd == INPUT_EMU_SCREENSHOT) {
		screenshot_save();
		return;
	}
#endif
	if (cmd == INPUT_EMU_TOGGLE_CURSOR) {
		gamepad_cursor_visible = !gamepad_cursor_visible;
		if (gamepad_cursor_visible) {
			has_synthetic_mouse_pos = true;
			gamepad_cursor_active = true;
			last_cursor_move_tick = SDL_GetTicks();
			if (sdl.viewport.w > 0 && sdl.viewport.h > 0) {
				synthetic_mouse_x = sdl.viewport.x + (int)(gamepad_cursor_norm_x * max(1, sdl.viewport.w - 1));
				synthetic_mouse_y = sdl.viewport.y + (int)(gamepad_cursor_norm_y * max(1, sdl.viewport.h - 1));
			}
		}
		return;
	}
#ifdef __ANDROID__
	SDL_AndroidSendMessage(COMMAND_GAMEPAD_UI_EVENT, GAMEPAD_UI_EMU_CMD | (cmd << 8));
#elif defined(ALICEHUB_IOS)
	alicehub_ios_gamepad_command(cmd);
#endif
}

static void apply_action_output(const struct input_action_spec *act, bool active, bool edge_rising,
		uint32_t now, uint32_t *wheel_tick, bool *vk_down_arr, bool *mapped_joybtn_arr)
{
	switch (act->type) {
	case INPUT_ACTION_VK:
		if (active && act->code > 0 && act->code < VK_NR_KEYCODES)
			vk_down_arr[act->code] = true;
		break;
	case INPUT_ACTION_MOUSE_BTN: {
		int vk = act->code;
		if (vk != VK_LBUTTON && vk != VK_RBUTTON && vk != VK_MBUTTON)
			vk = VK_LBUTTON;
		if (active) {
			vk_down_arr[vk] = true;
			has_synthetic_mouse_pos = true;
			if (sdl.viewport.w > 0 && sdl.viewport.h > 0) {
				synthetic_mouse_x = sdl.viewport.x + (int)(gamepad_cursor_norm_x * max(1, sdl.viewport.w - 1));
				synthetic_mouse_y = sdl.viewport.y + (int)(gamepad_cursor_norm_y * max(1, sdl.viewport.h - 1));
			}
		}
		break;
	}
	case INPUT_ACTION_COMBO:
		if (active) {
			if (act->code > 0 && act->code < VK_NR_KEYCODES)
				vk_down_arr[act->code] = true;
			if (act->code2 > 0 && act->code2 < VK_NR_KEYCODES)
				vk_down_arr[act->code2] = true;
		}
		break;
	case INPUT_ACTION_MOUSE_WHEEL:
		if (active) {
			int dir = act->code >= 0 ? 1 : -1;
			if (edge_rising || (now - *wheel_tick) >= GAMEPAD_WHEEL_REPEAT_MS) {
				wheel_dir = dir;
				*wheel_tick = now;
			}
		} else {
			*wheel_tick = 0;
		}
		break;
	case INPUT_ACTION_CTRL_TOGGLE:
		if (edge_rising) {
			bool next = !atomic_load(&touch_ctrl_skip);
			atomic_store(&touch_ctrl_skip, next);
			sync_ctrl_key_state();
		}
		break;
	case INPUT_ACTION_JOYPAD_BTN:
		if (act->code >= 0 && act->code < NR_PAD_SLOTS) {
			if (active && mapped_joybtn_arr)
				mapped_joybtn_arr[act->code] = true;
		}
		break;
	case INPUT_ACTION_EMU_CMD:
		if (edge_rising)
			emit_emu_cmd_to_ui(act->code);
		break;
	default:
		break;
	}
}

static void apply_directional_stick(int mode, float x, float y, float deadzone, uint32_t now,
		bool *vk_down_arr, float *out_mouse_vx, float *out_mouse_vy,
		float *out_joy_vx, float *out_joy_vy, bool *out_has_joy_source)
{
	if (mode == STICK_MODE_NONE)
		return;
	if (mode == STICK_MODE_JOYPAD && out_has_joy_source)
		*out_has_joy_source = true;
	float mag = sqrtf(x * x + y * y);
	if (mag < deadzone)
		return;
	float norm = (mag - deadzone) / (1.0f - deadzone);
	if (norm > 1.0f) norm = 1.0f;
	float nx = (x / mag) * norm;
	float ny = (y / mag) * norm;
	if (mode == STICK_MODE_MOUSE) {
		float curve = powf(norm, 1.55f);
		*out_mouse_vx += (x / mag) * curve;
		*out_mouse_vy += (y / mag) * curve;
	} else if (mode == STICK_MODE_ARROWS) {
		if (nx < -0.35f) vk_down_arr[VK_LEFT] = true;
		if (nx > 0.35f)  vk_down_arr[VK_RIGHT] = true;
		if (ny < -0.35f) vk_down_arr[VK_UP] = true;
		if (ny > 0.35f)  vk_down_arr[VK_DOWN] = true;
	} else if (mode == STICK_MODE_WASD) {
		if (nx < -0.35f) vk_down_arr[VK_A] = true;
		if (nx > 0.35f)  vk_down_arr[VK_D] = true;
		if (ny < -0.35f) vk_down_arr[VK_W] = true;
		if (ny > 0.35f)  vk_down_arr[VK_S] = true;
	} else if (mode == STICK_MODE_WHEEL) {
		int dir = 0;
		if (ny < -0.35f) dir = 1;
		else if (ny > 0.35f) dir = -1;
		if (dir != 0) {
			if (dir != stick_wheel_prev_dir || (now - stick_wheel_tick) >= GAMEPAD_WHEEL_REPEAT_MS) {
				wheel_dir = dir;
				stick_wheel_tick = now;
				stick_wheel_prev_dir = dir;
			}
		} else {
			stick_wheel_prev_dir = 0;
		}
	} else if (mode == STICK_MODE_JOYPAD) {
		if (out_joy_vx) *out_joy_vx += nx;
		if (out_joy_vy) *out_joy_vy += ny;
	}
}

static void send_cursor_ui_state(bool force)
{
#ifdef __ANDROID__
	static int last_param = -1;
	int dw = 0, dh = 0;
	if (sdl.window)
		SDL_GL_GetDrawableSize(sdl.window, &dw, &dh);
	if (dw <= 0) dw = sdl.viewport.x + sdl.viewport.w;
	if (dh <= 0) dh = sdl.viewport.y + sdl.viewport.h;
	if (dw <= 1 || dh <= 1)
		return;
	int wx = has_synthetic_mouse_pos ? synthetic_mouse_x
		: (sdl.viewport.x + (int)(gamepad_cursor_norm_x * max(1, sdl.viewport.w - 1)));
	int wy = has_synthetic_mouse_pos ? synthetic_mouse_y
		: (sdl.viewport.y + (int)(gamepad_cursor_norm_y * max(1, sdl.viewport.h - 1)));
	float nx = (float)wx / (float)(dw - 1);
	float ny = (float)wy / (float)(dh - 1);
	if (nx < 0.0f) nx = 0.0f;
	if (nx > 1.0f) nx = 1.0f;
	if (ny < 0.0f) ny = 0.0f;
	if (ny > 1.0f) ny = 1.0f;
	int ix = (int)(nx * 16383.0f + 0.5f);
	int iy = (int)(ny * 16383.0f + 0.5f);
	int vis = (gamepad_cursor_visible && !movie_has_active_playback()) ? 1 : 0;
	int param = (ix & 0x3FFF) | ((iy & 0x3FFF) << 14) | (vis << 28);
	if (force || param != last_param) {
		last_param = param;
		SDL_AndroidSendMessage(COMMAND_CURSOR_STATE, param);
	}
#else
	(void)force;
#endif
}

static void rebuild_effective_keys(uint32_t now)
{
	bool in_movie = movie_has_active_playback();
	bool ctrl_skip = atomic_load(&touch_ctrl_skip) || long_touch_ctrl_down;
	for (int code = 1; code < VK_NR_KEYCODES; code++) {
		bool is_mouse = (code == VK_LBUTTON || code == VK_RBUTTON || code == VK_MBUTTON);
		bool pad_mouse = is_mouse && (vpad_vk_down[code] || pad_vk_down[code]);
		if (is_mouse) {
			if (pad_mouse && !prev_gamepad_mouse[code]) {
				deferred_mouse_release[code] = false;
				key_state[code] = true;
				mouse_down_unobserved[code] = true;
				mouse_down_timestamp[code] = now;
			} else if (!pad_mouse && prev_gamepad_mouse[code]) {
				if (key_state[code] && mouse_down_unobserved[code]) {
					deferred_mouse_release[code] = true;
				} else if (!sdl_mouse_down[code] && !kbd_key_state[code]) {
					key_state[code] = false;
					deferred_mouse_release[code] = false;
				}
			}
			prev_gamepad_mouse[code] = pad_mouse;
			if (in_movie || suppressed_movie_keys[code])
				key_state[code] = false;
		} else {
			bool down = kbd_key_state[code] || vpad_vk_down[code] || pad_vk_down[code]
				|| injected_vk_down[code];
			if (code == VK_CONTROL && ctrl_skip)
				down = true;
			if ((in_movie && is_movie_skip_suppressed_key(code)) || suppressed_movie_keys[code])
				down = false;
			key_state[code] = down;
		}
	}
	vpad_cursor_btn_down = vpad_vk_down[VK_LBUTTON] || vpad_vk_down[VK_RBUTTON]
		|| pad_vk_down[VK_LBUTTON] || pad_vk_down[VK_RBUTTON];
}

static void reset_gamepad_transient_state(void)
{
	pthread_mutex_lock(&gp_mutex);
	for (int i = 0; i < MAX_VPAD_SLOTS; i++) {
		vpad_ui_slots[i].phys_down = false;
		vpad_ui_slots[i].pending_press_edge = false;
	}
	vpad_stick_x[0] = vpad_stick_y[0] = 0.0f;
	vpad_stick_x[1] = vpad_stick_y[1] = 0.0f;
	memset(vpad_dpad_dir, 0, sizeof(vpad_dpad_dir));
	vpad_latch_reset_gen++;
	phys_latch_reset_gen++;
	seen_vpad_latch_reset_gen = vpad_latch_reset_gen;
	seen_phys_latch_reset_gen = phys_latch_reset_gen;
	pthread_mutex_unlock(&gp_mutex);

	memset(vpad_rt_slots, 0, sizeof(vpad_rt_slots));
	memset(phys_slot_latched, 0, sizeof(phys_slot_latched));
	memset(phys_slot_prev_raw, 0, sizeof(phys_slot_prev_raw));
	memset(phys_slot_movie_suppressed, 0, sizeof(phys_slot_movie_suppressed));
	for (int b = 0; b < NR_PAD_SLOTS; b++) {
		if (prev_mapped_joybtn[b]) {
			joybutton_state[b] = false;
			prev_mapped_joybtn[b] = false;
		}
	}
	if (prev_mapped_joyaxis_active) {
		joyaxis_update(JOYAXIS_X, 0);
		joyaxis_update(JOYAXIS_Y, 0);
		prev_mapped_joyaxis_active = false;
	}
	memset(vpad_vk_down, 0, sizeof(vpad_vk_down));
	memset(pad_vk_down, 0, sizeof(pad_vk_down));
	memset(prev_gamepad_mouse, 0, sizeof(prev_gamepad_mouse));
	movie_pad_holding = false;
	vpad_cursor_btn_down = false;
}

static void process_gamepad_frame(void)
{
	uint32_t now = SDL_GetTicks();
	memset(vpad_vk_down, 0, sizeof(vpad_vk_down));
	memset(pad_vk_down, 0, sizeof(pad_vk_down));

	struct gamepad_config cfg;
	struct vpad_ui_slot_state local_vpad[MAX_VPAD_SLOTS];
	float vsx[2], vsy[2];
	int vdmode;
	bool vddir[4];

	bool raw_phys[NR_PAD_SLOTS];
	memset(raw_phys, 0, sizeof(raw_phys));
	float phys_lx = 0.0f, phys_ly = 0.0f, phys_rx = 0.0f, phys_ry = 0.0f;
	float phys_lt = 0.0f, phys_rt = 0.0f;
	bool has_sdl_pad = false;
	bool has_android_pad = false;
#ifdef __SWITCH__
	static const int switch_slots[CP_BUTTONS] = {
		PAD_SLOT_DPAD_UP, PAD_SLOT_DPAD_DOWN, PAD_SLOT_DPAD_LEFT, PAD_SLOT_DPAD_RIGHT,
		PAD_SLOT_A, PAD_SLOT_B, PAD_SLOT_X, PAD_SLOT_Y,
		PAD_SLOT_LB, PAD_SLOT_RB, PAD_SLOT_LT, PAD_SLOT_RT,
		PAD_SLOT_BACK, PAD_SLOT_START, PAD_SLOT_L3, PAD_SLOT_R3
	};
	bool use_switch_profile = switch_controls_remapping();
	uint32_t switch_physical = 0;
#endif
	uint32_t cur_vpad_gen = 0;
	uint32_t cur_phys_gen = 0;
	bool do_cursor_reset = false;
	bool cursor_reset_vis = false;

	pthread_mutex_lock(&gp_mutex);
	ensure_gamepad_config_defaults_locked();
	for (int i = 0; i < MAX_CONTROLLERS; i++) {
		if (!pad_slots[i].active)
			continue;
		has_sdl_pad = true;
		for (int b = 0; b < NR_PAD_SLOTS; b++) {
			if (pad_slots[i].btn_down[b])
				raw_phys[b] = true;
		}
		if (pad_slots[i].hat_dir[0]) raw_phys[PAD_SLOT_DPAD_UP] = true;
		if (pad_slots[i].hat_dir[1]) raw_phys[PAD_SLOT_DPAD_DOWN] = true;
		if (pad_slots[i].hat_dir[2]) raw_phys[PAD_SLOT_DPAD_LEFT] = true;
		if (pad_slots[i].hat_dir[3]) raw_phys[PAD_SLOT_DPAD_RIGHT] = true;
#ifdef __SWITCH__
		if (use_switch_profile) {
			if (pad_slots[i].is_game_controller) {
				switch_physical |= switch_controls_gamecontroller(pad_slots[i].ctrl);
			} else {
				for (unsigned b = 0; b < CP_BUTTONS; ++b)
					if (pad_slots[i].btn_down[switch_slots[b]])
						switch_physical |= 1u << b;
			}
		}
#endif
		if (fabsf(pad_slots[i].axes[0]) > fabsf(phys_lx)) phys_lx = pad_slots[i].axes[0];
		if (fabsf(pad_slots[i].axes[1]) > fabsf(phys_ly)) phys_ly = pad_slots[i].axes[1];
		if (fabsf(pad_slots[i].axes[2]) > fabsf(phys_rx)) phys_rx = pad_slots[i].axes[2];
		if (fabsf(pad_slots[i].axes[3]) > fabsf(phys_ry)) phys_ry = pad_slots[i].axes[3];
		if (pad_slots[i].axes[4] > phys_lt) phys_lt = pad_slots[i].axes[4];
		if (pad_slots[i].axes[5] > phys_rt) phys_rt = pad_slots[i].axes[5];
	}

	if (!has_sdl_pad && android_pad.active) {
		has_android_pad = true;
		for (int b = 0; b < NR_PAD_SLOTS; b++) {
			if (android_pad.btn_down[b]) raw_phys[b] = true;
		}
		if (android_pad.hat_y < -0.5f) raw_phys[PAD_SLOT_DPAD_UP] = true;
		if (android_pad.hat_y > 0.5f)  raw_phys[PAD_SLOT_DPAD_DOWN] = true;
		if (android_pad.hat_x < -0.5f) raw_phys[PAD_SLOT_DPAD_LEFT] = true;
		if (android_pad.hat_x > 0.5f)  raw_phys[PAD_SLOT_DPAD_RIGHT] = true;
		phys_lx = android_pad.axes[0];
		phys_ly = android_pad.axes[1];
		phys_rx = android_pad.axes[2];
		phys_ry = android_pad.axes[3];
		phys_lt = android_pad.axes[4];
		phys_rt = android_pad.axes[5];
		if (phys_lt > 0.35f) android_pad.trigger_latched[0] = true;
		else if (phys_lt < 0.25f) android_pad.trigger_latched[0] = false;
		if (phys_rt > 0.35f) android_pad.trigger_latched[1] = true;
		else if (phys_rt < 0.25f) android_pad.trigger_latched[1] = false;
		if (android_pad.trigger_latched[0]) raw_phys[PAD_SLOT_LT] = true;
		if (android_pad.trigger_latched[1]) raw_phys[PAD_SLOT_RT] = true;
	}
	uint32_t bmask = 0;
	for (int b = 0; b < NR_PAD_SLOTS; b++) {
		if (raw_phys[b]) bmask |= (1u << b);
	}
	live_status_buttons_mask = bmask;
	live_status_axes[0] = phys_lx;
	live_status_axes[1] = phys_ly;
	live_status_axes[2] = phys_rx;
	live_status_axes[3] = phys_ry;
	live_status_axes[4] = phys_lt;
	live_status_axes[5] = phys_rt;

	cfg = gp_cfg;
	cur_vpad_gen = vpad_latch_reset_gen;
	cur_phys_gen = phys_latch_reset_gen;
	if (pending_cursor_reset) {
		do_cursor_reset = true;
		cursor_reset_vis = pending_cursor_reset_visible;
		pending_cursor_reset = false;
	}
	for (int i = 0; i < MAX_VPAD_SLOTS; i++) {
		if (vpad_ui_slots[i].pending_press_edge) {
			// Consume short intra-frame tap edge so this frame registers press even if released early.
			vpad_ui_slots[i].pending_press_edge = false;
			local_vpad[i] = vpad_ui_slots[i];
			local_vpad[i].phys_down = true;
			local_vpad[i].active_action = vpad_ui_slots[i].pending_action;
		} else {
			local_vpad[i] = vpad_ui_slots[i];
		}
	}
	vsx[0] = vpad_stick_x[0]; vsy[0] = vpad_stick_y[0];
	vsx[1] = vpad_stick_x[1]; vsy[1] = vpad_stick_y[1];
	vdmode = vpad_dpad_cfg_mode;
	memcpy(vddir, vpad_dpad_dir, sizeof(vddir));
	pthread_mutex_unlock(&gp_mutex);

#ifdef __SWITCH__
	if (use_switch_profile) {
		uint32_t mapped = switch_controls_buttons(switch_physical);
		has_sdl_pad |= mapped != 0;
		for (unsigned b = 0; b < CP_BUTTONS; ++b) {
			int slot = switch_slots[b];
			raw_phys[slot] = (mapped & (1u << b)) != 0;
			joybutton_state[slot] = raw_phys[slot];
		}
	}
#endif

	if (cur_vpad_gen != seen_vpad_latch_reset_gen) {
		seen_vpad_latch_reset_gen = cur_vpad_gen;
		for (int i = 0; i < MAX_VPAD_SLOTS; i++)
			vpad_rt_slots[i].latched = false;
	}
	if (cur_phys_gen != seen_phys_latch_reset_gen) {
		seen_phys_latch_reset_gen = cur_phys_gen;
		memset(phys_slot_latched, 0, sizeof(phys_slot_latched));
	}
	if (do_cursor_reset) {
		gamepad_cursor_visible = cursor_reset_vis;
		if (cursor_reset_vis) {
			gamepad_cursor_active = true;
			has_synthetic_mouse_pos = true;
			last_cursor_move_tick = now;
			if (sdl.viewport.w > 0 && sdl.viewport.h > 0) {
				synthetic_mouse_x = sdl.viewport.x + (int)(gamepad_cursor_norm_x * max(1, sdl.viewport.w - 1));
				synthetic_mouse_y = sdl.viewport.y + (int)(gamepad_cursor_norm_y * max(1, sdl.viewport.h - 1));
			}
		}
	}

	bool in_movie = movie_has_active_playback();
	float mouse_vx = 0.0f, mouse_vy = 0.0f;
	float joy_vx = 0.0f, joy_vy = 0.0f;
	bool has_joy_stick_source = false;
	bool mapped_joybtn[NR_PAD_SLOTS];
	memset(mapped_joybtn, 0, sizeof(mapped_joybtn));

	// Virtual buttons
	for (int i = 0; i < MAX_VPAD_SLOTS; i++) {
		bool eff_phys = local_vpad[i].phys_down;
		bool edge = eff_phys && !vpad_rt_slots[i].prev_effective_phys;
		if (in_movie && is_movie_skip_action(&local_vpad[i].active_action)) {
			if (!eff_phys) {
				vpad_rt_slots[i].movie_suppressed = false;
			} else if (edge && !vpad_rt_slots[i].movie_suppressed) {
				if (!movie_pad_holding) {
					movie_pad_holding = true;
					movie_pad_hold_start_tick = now;
					movie_hold_token++;
				}
			}
			vpad_rt_slots[i].prev_effective_phys = eff_phys;
			continue;
		}
		if (!eff_phys)
			vpad_rt_slots[i].movie_suppressed = false;
		vpad_rt_slots[i].prev_effective_phys = eff_phys;
		if (vpad_rt_slots[i].movie_suppressed)
			continue;

		bool out_active = eff_phys;
		bool out_edge = edge;
		if (local_vpad[i].active_action.toggle_mode) {
			if (edge)
				vpad_rt_slots[i].latched = !vpad_rt_slots[i].latched;
			out_active = vpad_rt_slots[i].latched;
			out_edge = edge && out_active;
		}
		apply_action_output(&local_vpad[i].active_action, out_active, out_edge, now,
				&vpad_rt_slots[i].last_wheel_tick, vpad_vk_down, mapped_joybtn);
	}

	// Virtual D-Pad & Virtual Sticks
	if (!in_movie) {
		float vdx = (vddir[1] ? 1.0f : 0.0f) - (vddir[0] ? 1.0f : 0.0f);
		float vdy = (vddir[3] ? 1.0f : 0.0f) - (vddir[2] ? 1.0f : 0.0f);
		if (vdmode == STICK_MODE_MOUSE) {
			mouse_vx += vdx * 0.65f;
			mouse_vy += vdy * 0.65f;
		} else if (vdmode != STICK_MODE_NONE) {
			apply_directional_stick(vdmode, vdx, vdy, 0.25f, now, vpad_vk_down,
					&mouse_vx, &mouse_vy, &joy_vx, &joy_vy, &has_joy_stick_source);
		}
		apply_directional_stick(cfg.vpad_left_stick_mode, vsx[0], vsy[0], cfg.deadzone, now,
				vpad_vk_down, &mouse_vx, &mouse_vy, &joy_vx, &joy_vy, &has_joy_stick_source);
		apply_directional_stick(cfg.vpad_right_stick_mode, vsx[1], vsy[1], cfg.deadzone, now,
				vpad_vk_down, &mouse_vx, &mouse_vy, &joy_vx, &joy_vy, &has_joy_stick_source);
	}

	// Physical Controller Mapping
	bool phys_any_trigger_down = false;
	if (cfg.phys_enabled) {
		bool logical_phys[NR_PAD_SLOTS];
		memset(logical_phys, 0, sizeof(logical_phys));
		for (int s = 0; s < NR_PAD_SLOTS; s++) {
			int mapped_slot = remap_abxy_slot(s, cfg.swap_ab_xy);
			if (raw_phys[s])
				logical_phys[mapped_slot] = true;
		}

		if (!in_movie && cfg.phys_dpad_mode == STICK_MODE_JOYPAD) {
			has_joy_stick_source = true;
			float pdx = (logical_phys[PAD_SLOT_DPAD_RIGHT] ? 1.0f : 0.0f)
				- (logical_phys[PAD_SLOT_DPAD_LEFT] ? 1.0f : 0.0f);
			float pdy = (logical_phys[PAD_SLOT_DPAD_DOWN] ? 1.0f : 0.0f)
				- (logical_phys[PAD_SLOT_DPAD_UP] ? 1.0f : 0.0f);
			joy_vx += pdx;
			joy_vy += pdy;
		}

		for (int s = 0; s < NR_PAD_SLOTS; s++) {
			bool down = logical_phys[s];
			bool edge = down && !phys_slot_prev_raw[s];
			phys_slot_prev_raw[s] = down;

			if (in_movie) {
				if (!down) {
					phys_slot_movie_suppressed[s] = false;
				} else {
					if (s <= PAD_SLOT_Y || s == PAD_SLOT_START || s == PAD_SLOT_LB ||
					    s == PAD_SLOT_RB || s == PAD_SLOT_LT || s == PAD_SLOT_RT) {
						if (edge && !phys_slot_movie_suppressed[s]) {
							if (!movie_pad_holding) {
								movie_pad_holding = true;
								movie_pad_hold_start_tick = now;
								movie_hold_token++;
							}
						}
						if (!phys_slot_movie_suppressed[s])
							phys_any_trigger_down = true;
					}
				}
				continue;
			}
			if (!down)
				phys_slot_movie_suppressed[s] = false;
			if (phys_slot_movie_suppressed[s])
				continue;

			// D-Pad fallback when explicit slot binding is NONE
			struct input_action_spec act = cfg.phys_bindings[s];
			if (act.type == INPUT_ACTION_NONE && s >= PAD_SLOT_DPAD_UP && s <= PAD_SLOT_DPAD_RIGHT) {
				if (cfg.phys_dpad_mode == STICK_MODE_ARROWS) {
					static const int arrow_vk[4] = { VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT };
					act.type = INPUT_ACTION_VK;
					act.code = arrow_vk[s - PAD_SLOT_DPAD_UP];
				} else if (cfg.phys_dpad_mode == STICK_MODE_WASD) {
					static const int wasd_vk[4] = { VK_W, VK_S, VK_A, VK_D };
					act.type = INPUT_ACTION_VK;
					act.code = wasd_vk[s - PAD_SLOT_DPAD_UP];
				} else if (cfg.phys_dpad_mode == STICK_MODE_JOYPAD) {
					act.type = INPUT_ACTION_JOYPAD_BTN;
					act.code = (int16_t)s;
				} else if (cfg.phys_dpad_mode == STICK_MODE_MOUSE && down) {
					if (s == PAD_SLOT_DPAD_LEFT) mouse_vx -= 0.55f;
					if (s == PAD_SLOT_DPAD_RIGHT) mouse_vx += 0.55f;
					if (s == PAD_SLOT_DPAD_UP) mouse_vy -= 0.55f;
					if (s == PAD_SLOT_DPAD_DOWN) mouse_vy += 0.55f;
					continue;
				}
			}

			bool out_active = down;
			bool out_edge = edge;
			if (act.toggle_mode) {
				if (edge)
					phys_slot_latched[s] = !phys_slot_latched[s];
				out_active = phys_slot_latched[s];
				out_edge = edge && out_active;
			}
			apply_action_output(&act, out_active, out_edge, now,
					&phys_slot_wheel_tick[s], pad_vk_down, mapped_joybtn);
		}

		if (!in_movie) {
			apply_directional_stick(cfg.phys_left_stick_mode, phys_lx, phys_ly, cfg.deadzone,
					now, pad_vk_down, &mouse_vx, &mouse_vy, &joy_vx, &joy_vy, &has_joy_stick_source);
			apply_directional_stick(cfg.phys_right_stick_mode, phys_rx, phys_ry, cfg.deadzone,
					now, pad_vk_down, &mouse_vx, &mouse_vy, &joy_vx, &joy_vy, &has_joy_stick_source);
		}
#ifdef __SWITCH__
	} else if (has_sdl_pad && !in_movie) {
		pad_vk_down[VK_LBUTTON] = raw_phys[PAD_SLOT_A];
		pad_vk_down[VK_RBUTTON] = raw_phys[PAD_SLOT_B];
		if (use_switch_profile) {
			pad_vk_down[VK_UP] = raw_phys[PAD_SLOT_DPAD_UP];
			pad_vk_down[VK_DOWN] = raw_phys[PAD_SLOT_DPAD_DOWN];
			pad_vk_down[VK_LEFT] = raw_phys[PAD_SLOT_DPAD_LEFT];
			pad_vk_down[VK_RIGHT] = raw_phys[PAD_SLOT_DPAD_RIGHT];
		}
#endif
	} else if (!has_sdl_pad && has_android_pad && !in_movie) {
		// Native Android fallback controller pass-through to SACT joypad when custom mapping is disabled
		for (int b = 0; b < NR_PAD_SLOTS; b++) {
			if (raw_phys[b])
				mapped_joybtn[b] = true;
		}
		apply_directional_stick(STICK_MODE_JOYPAD, phys_lx, phys_ly, cfg.deadzone,
				now, pad_vk_down, &mouse_vx, &mouse_vy, &joy_vx, &joy_vy, &has_joy_stick_source);
	}

	// Update joybutton_state on rising AND falling edges of mapped JOYPAD_BTN actions
	// so releasing a button cleanly resets its joybutton_state bit.
	for (int b = 0; b < NR_PAD_SLOTS; b++) {
		bool cur = !in_movie && mapped_joybtn[b];
		if (cur && !prev_mapped_joybtn[b]) {
			joybutton_state[b] = true;
		} else if (!cur && prev_mapped_joybtn[b]) {
			joybutton_state[b] = false;
		}
		prev_mapped_joybtn[b] = cur;
	}

	// Update joyaxis_state for STICK_MODE_JOYPAD sticks/D-Pads and reset cleanly to 0 on release.
	bool suppress_raw_joyaxis = cfg.phys_enabled && cfg.phys_left_stick_mode != STICK_MODE_JOYPAD;
	if (suppress_raw_joyaxis && !prev_raw_joyaxis_suppressed && !has_joy_stick_source) {
		joyaxis_update(JOYAXIS_X, 0);
		joyaxis_update(JOYAXIS_Y, 0);
	}
	prev_raw_joyaxis_suppressed = suppress_raw_joyaxis;

	bool joy_active = !in_movie && has_joy_stick_source &&
		(fabsf(joy_vx) > 0.25f || fabsf(joy_vy) > 0.25f);
	if (joy_active) {
		float jx = joy_vx < -1.0f ? -1.0f : (joy_vx > 1.0f ? 1.0f : joy_vx);
		float jy = joy_vy < -1.0f ? -1.0f : (joy_vy > 1.0f ? 1.0f : joy_vy);
		int ix = 0, iy = 0;
		if (fabsf(jx) > 0.25f) {
			ix = (int)(jx * 32767.0f);
			if (abs(ix) <= JOYAXIS_DEADZONE)
				ix = (ix > 0) ? (JOYAXIS_DEADZONE + 1000) : -(JOYAXIS_DEADZONE + 1000);
		}
		if (fabsf(jy) > 0.25f) {
			iy = (int)(jy * 32767.0f);
			if (abs(iy) <= JOYAXIS_DEADZONE)
				iy = (iy > 0) ? (JOYAXIS_DEADZONE + 1000) : -(JOYAXIS_DEADZONE + 1000);
		}
		joyaxis_update(JOYAXIS_X, ix);
		joyaxis_update(JOYAXIS_Y, iy);
		prev_mapped_joyaxis_active = true;
	} else if (prev_mapped_joyaxis_active) {
		joyaxis_update(JOYAXIS_X, 0);
		joyaxis_update(JOYAXIS_Y, 0);
		prev_mapped_joyaxis_active = false;
	}

	if (in_movie) {
		bool any_vpad_skip_down = false;
		for (int i = 0; i < MAX_VPAD_SLOTS; i++) {
			if (local_vpad[i].phys_down && !vpad_rt_slots[i].movie_suppressed &&
			    is_movie_skip_action(&local_vpad[i].active_action)) {
				any_vpad_skip_down = true;
				break;
			}
		}
		if (movie_pad_holding && !phys_any_trigger_down && !any_vpad_skip_down) {
			movie_pad_holding = false;
			movie_early_release_hint = true;
		}
		phys_pad_had_any_down = phys_any_trigger_down || any_vpad_skip_down;
	} else {
		phys_pad_had_any_down = false;
	}

	// Integrate smooth virtual analog mouse cursor
	if (!in_movie && (fabsf(mouse_vx) > 0.001f || fabsf(mouse_vy) > 0.001f)) {
		float dt = last_cursor_step_tick ? ((float)(now - last_cursor_step_tick) / 1000.0f) : 0.016f;
		if (dt > 0.05f) dt = 0.05f;
		if (dt < 0.001f) dt = 0.001f;
		float w = sdl.w > 0 ? (float)sdl.w : 1280.0f;
		float h = sdl.h > 0 ? (float)sdl.h : 720.0f;
		gamepad_cursor_norm_x += (mouse_vx * cfg.cursor_speed * dt) / w;
		gamepad_cursor_norm_y += (mouse_vy * cfg.cursor_speed * dt) / h;
		if (gamepad_cursor_norm_x < 0.0f) gamepad_cursor_norm_x = 0.0f;
		if (gamepad_cursor_norm_x > 1.0f) gamepad_cursor_norm_x = 1.0f;
		if (gamepad_cursor_norm_y < 0.0f) gamepad_cursor_norm_y = 0.0f;
		if (gamepad_cursor_norm_y > 1.0f) gamepad_cursor_norm_y = 1.0f;
		if (sdl.viewport.w > 0 && sdl.viewport.h > 0) {
			synthetic_mouse_x = sdl.viewport.x + (int)(gamepad_cursor_norm_x * max(1, sdl.viewport.w - 1));
			synthetic_mouse_y = sdl.viewport.y + (int)(gamepad_cursor_norm_y * max(1, sdl.viewport.h - 1));
			has_synthetic_mouse_pos = true;
			synthetic_mouse_warped = false;
			synthetic_mouse_hover_observed = true;
		}
		gamepad_cursor_active = true;
		gamepad_cursor_visible = true;
		last_cursor_move_tick = now;
	}
	last_cursor_step_tick = now;

	if (gamepad_cursor_visible && last_cursor_move_tick != 0 &&
	    (now - last_cursor_move_tick) > GAMEPAD_CURSOR_AUTOHIDE_MS &&
	    !vpad_vk_down[VK_LBUTTON] && !pad_vk_down[VK_LBUTTON]) {
		gamepad_cursor_visible = false;
	}
	send_cursor_ui_state(do_cursor_reset);
	rebuild_effective_keys(now);
#ifdef ALICEHUB_IOS
	if (sdl.window) {
		int width = 0, height = 0;
		SDL_GetWindowSize(sdl.window, &width, &height);
		alicehub_ios_gamepad_frame((float)synthetic_mouse_x / max(1, width),
			(float)synthetic_mouse_y / max(1, height), gamepad_cursor_visible && !in_movie,
			in_movie, has_sdl_pad, key_state[VK_CONTROL]);
	}
#endif
}

void input_set_legacy_text_handlers(const struct input_text_handlers *handlers)
{
	legacy_text_handlers = handlers;
	update_text_handlers();
}

void input_set_parts_text_handlers(const struct input_text_handlers *handlers)
{
	parts_text_handlers = handlers;
	update_text_handlers();
}

void input_end_session(void)
{
	SDL_GameController *ctrls[MAX_CONTROLLERS] = {0};
	SDL_Joystick *joys[MAX_CONTROLLERS] = {0};

	pthread_mutex_lock(&gp_mutex);
	for (int i = 0; i < MAX_CONTROLLERS; i++) {
		ctrls[i] = pad_slots[i].ctrl;
		joys[i] = pad_slots[i].joy;
	}
	memset(pad_slots, 0, sizeof(pad_slots));
	memset(&android_pad, 0, sizeof(android_pad));
	pthread_mutex_unlock(&gp_mutex);

	for (int i = 0; i < MAX_CONTROLLERS; i++) {
		if (ctrls[i])
			SDL_GameControllerClose(ctrls[i]);
		else if (joys[i])
			SDL_JoystickClose(joys[i]);
	}

	reset_gamepad_transient_state();
	input_reset_close_hook();

	memset(key_state, 0, sizeof(key_state));
	memset(kbd_key_state, 0, sizeof(kbd_key_state));
	memset(injected_vk_down, 0, sizeof(injected_vk_down));
	memset(sdl_mouse_down, 0, sizeof(sdl_mouse_down));
	kbd_ctrl_down = false;
	atomic_store(&touch_ctrl_skip, false);
	atomic_store(&long_press_ctrl_enabled, false);
	long_touch_armed = false;
	long_touch_ctrl_down = false;
	touch_press_id = 0;
	long_touch_click_cancel = 0;

	movie_mode_active = false;
	movie_mode_seq = 0;
	single_touch_down = false;
	movie_touch_hold_eligible = false;
	suppress_touch_until_release = false;
	movie_mouse_holding = false;
	suppress_mouse_until_release = false;
	movie_key_holding = false;
	memset(suppressed_movie_keys, 0, sizeof(suppressed_movie_keys));
	movie_early_release_hint = false;

	clear_active_touch_fingers();
	multi_touch_active = false;
	two_finger_tap_candidate = false;
	two_finger_scrolled = false;

	nr_deferred_synthetic_mouse_events = 0;
	memset(deferred_synthetic_mouse_events, 0, sizeof(deferred_synthetic_mouse_events));
	synthetic_mouse_hover_observed = false;
	synthetic_mouse_down_observed = false;
	synthetic_mouse_up_observed = true;
	synthetic_mouse_release_pos_observed = false;
	synthetic_mouse_release_pos_obs_count = 0;
	synthetic_mouse_release_queued_before_down_obs = false;
	has_synthetic_mouse_pos = false;
	synthetic_mouse_warped = false;
	has_last_touch_down_pos = false;
	in_parts_poll = false;
	memset(mouse_down_unobserved, 0, sizeof(mouse_down_unobserved));
	memset(deferred_mouse_release, 0, sizeof(deferred_mouse_release));

	gamepad_cursor_active = false;
	gamepad_cursor_visible = false;
	mouse_focus = true;
	keyboard_focus = true;

	legacy_text_handlers = NULL;
	parts_text_handlers = NULL;
	if (SDL_WasInit(SDL_INIT_VIDEO))
		update_text_handlers();
}

static void pop_deferred_synthetic_mouse_event(uint32_t now)
{
	SDL_MouseButtonEvent *ev = &deferred_synthetic_mouse_events[0];
	synthetic_mouse_event(ev);
	synthetic_mouse_x = ev->x;
	synthetic_mouse_y = ev->y;
	has_synthetic_mouse_pos = true;
	synthetic_mouse_warped = false;
	synthetic_mouse_release_pos_observed = false;
	synthetic_mouse_release_pos_obs_count = 0;
	synthetic_mouse_release_queued_before_down_obs = false;
	if (ev->state == SDL_PRESSED) {
		synthetic_mouse_press_x = ev->x;
		synthetic_mouse_press_y = ev->y;
		synthetic_mouse_down_observed = false;
	} else {
		synthetic_mouse_up_observed = false;
		synthetic_mouse_hover_observed = false;
	}
	nr_deferred_synthetic_mouse_events--;
	for (int i = 0; i < nr_deferred_synthetic_mouse_events; i++) {
		deferred_synthetic_mouse_events[i] = deferred_synthetic_mouse_events[i + 1];
	}
	if (nr_deferred_synthetic_mouse_events > 0) {
		deferred_synthetic_mouse_events[0].timestamp = now;
		if (deferred_synthetic_mouse_events[0].state == SDL_RELEASED &&
		    !synthetic_mouse_down_observed) {
			synthetic_mouse_release_queued_before_down_obs = true;
		}
	}
}

static void fire_deferred_events(bool from_parts_poll, bool allow_press)
{
	uint32_t now = SDL_GetTicks();
	bool parts_active = last_parts_poll_timestamp != 0 && (now - last_parts_poll_timestamp) < 500;

	// Flush the next deferred synthetic mouse button event once the game
	// has observed the preceding hover/press/release phase (or after the timeout).
	while (nr_deferred_synthetic_mouse_events > 0 &&
	       deferred_synthetic_mouse_events[0].state == SDL_RELEASED &&
	       !key_state[VK_LBUTTON] && !key_state[VK_RBUTTON]) {
		pop_deferred_synthetic_mouse_event(now);
		synthetic_mouse_up_observed = true;
	}

	if (nr_deferred_synthetic_mouse_events > 0) {
		SDL_MouseButtonEvent *ev = &deferred_synthetic_mouse_events[0];
		if (from_parts_poll) {
			if (ev->state == SDL_PRESSED) {
				if (synthetic_mouse_up_observed) {
					if (!synthetic_mouse_hover_observed || !allow_press) {
						synthetic_mouse_x = ev->x;
						synthetic_mouse_y = ev->y;
						has_synthetic_mouse_pos = true;
						synthetic_mouse_warped = false;
					} else {
						pop_deferred_synthetic_mouse_event(now);
					}
				}
			} else {
				// Keep releasing an observed Parts press after EndInput, but leave
				// non-Parts drags on their fallback timing so they see the drop position.
				if ((allow_press || parts_active) && synthetic_mouse_down_observed) {
					if (has_synthetic_mouse_pos &&
					    (synthetic_mouse_x != ev->x || synthetic_mouse_y != ev->y)) {
						synthetic_mouse_x = ev->x;
						synthetic_mouse_y = ev->y;
						synthetic_mouse_warped = false;
					} else {
						pop_deferred_synthetic_mouse_event(now);
					}
				}
			}
		} else if (!parts_active) {
			bool ready = false;
			if (ev->state == SDL_PRESSED) {
				synthetic_mouse_x = ev->x;
				synthetic_mouse_y = ev->y;
				has_synthetic_mouse_pos = true;
				synthetic_mouse_warped = false;
				ready = (now - ev->timestamp) >= SYNTHETIC_MOUSE_EVENT_DELAY ||
					(synthetic_mouse_hover_observed &&
					 (now - synthetic_mouse_hover_tick) >= SYNTHETIC_MOUSE_HOVER_MIN_DELAY);
			} else {
				// If the down state was already observed before lift, expose the
				// final touch release position while keeping the button pressed
				// long enough for the game's drag loop to read it, then release
				// promptly instead of stalling for the full 250ms fallback window.
				if (synthetic_mouse_down_observed) {
					if (!synthetic_mouse_release_pos_observed) {
						synthetic_mouse_x = ev->x;
						synthetic_mouse_y = ev->y;
						has_synthetic_mouse_pos = true;
						synthetic_mouse_warped = false;
					}
					bool is_drag_release = ev->x != synthetic_mouse_press_x ||
						ev->y != synthetic_mouse_press_y;
					bool down_settled = (now - synthetic_mouse_down_observed_tick) >=
						SYNTHETIC_MOUSE_RELEASE_DOWN_MIN_DELAY;
					if (synthetic_mouse_release_pos_observed) {
						ready = down_settled &&
							(now - synthetic_mouse_release_pos_tick) >=
							SYNTHETIC_MOUSE_RELEASE_DROP_MIN_DELAY;
					} else if (synthetic_mouse_release_queued_before_down_obs &&
					           is_drag_release) {
						ready = (now - synthetic_mouse_down_observed_tick) >=
							SYNTHETIC_MOUSE_RELEASE_FALLBACK_DELAY;
					} else {
						ready = down_settled &&
							(now - ev->timestamp) >=
							SYNTHETIC_MOUSE_RELEASE_OBSERVED_NO_POS_DELAY;
					}
				} else {
					ready = (now - ev->timestamp) >= SYNTHETIC_MOUSE_RELEASE_FALLBACK_DELAY;
				}
			}
			if (ready) {
				bool was_pressed = ev->state == SDL_PRESSED;
				pop_deferred_synthetic_mouse_event(now);
				if (!was_pressed)
					synthetic_mouse_up_observed = true;
			}
		}
	}

#ifdef __ANDROID__
	// Fire the deferred keyboard and text input events.
	while (!STAILQ_EMPTY(&deferred_keyevent_queue) && now >= last_keyevent_timestamp + DEFERRED_KEY_DELAY) {
		struct deferred_keyevent *ev = STAILQ_FIRST(&deferred_keyevent_queue);
		switch (ev->e.type) {
		case SDL_TEXTINPUT:
			if (text_handlers && text_handlers->commit)
				text_handlers->commit(ev->e.text.text);
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			last_keyevent_timestamp = now;
			key_event(&ev->e.key, ev->e.type == SDL_KEYDOWN);
			break;
		default:
			if (ev->e.type == android_parts_text_event)
				dispatch_android_parts_text(&ev->e.user);
			break;
		}
		STAILQ_REMOVE_HEAD(&deferred_keyevent_queue, entry);
		free(ev);
	}
#endif
}

static bool long_touch_right_button_down(void)
{
	return sdl_mouse_down[VK_RBUTTON] || vpad_vk_down[VK_RBUTTON] || pad_vk_down[VK_RBUTTON];
}

static void cancel_long_touch_click(void)
{
	// Parts can only have captured one press since its last poll.
	if (!long_touch_click_cancel)
		long_touch_click_cancel = touch_press_id;
	touch_press_id = 0;
	sdl_mouse_down[VK_LBUTTON] = false;
	if (!vpad_vk_down[VK_LBUTTON] && !pad_vk_down[VK_LBUTTON]) {
		key_state[VK_LBUTTON] = false;
		mouse_down_unobserved[VK_LBUTTON] = false;
		deferred_mouse_release[VK_LBUTTON] = false;
	}
	synthetic_mouse_hover_observed = false;
	synthetic_mouse_up_observed = true;
}

static void update_long_touch_ctrl(void)
{
	if (!long_touch_armed && !long_touch_ctrl_down)
		return;
	if (long_touch_right_button_down()) {
		release_long_touch_ctrl();
		return;
	}
	if (!long_touch_armed || !touch_press_id || nr_deferred_synthetic_mouse_events ||
	    SDL_GetTicks() - long_touch_start_tick < LONG_TOUCH_DURATION)
		return;

	long_touch_armed = false;
	long_touch_ctrl_down = true;
	cancel_long_touch_click();
	sync_ctrl_key_state();
}

static void reset_inputs_on_movie_start(void)
{
	touch_press_id = 0;
	key_state[VK_LBUTTON] = false;
	key_state[VK_RBUTTON] = false;
	key_state[VK_MBUTTON] = false;
	mouse_down_unobserved[VK_LBUTTON] = false;
	mouse_down_unobserved[VK_RBUTTON] = false;
	mouse_down_unobserved[VK_MBUTTON] = false;
	deferred_mouse_release[VK_LBUTTON] = false;
	deferred_mouse_release[VK_RBUTTON] = false;
	deferred_mouse_release[VK_MBUTTON] = false;
	nr_deferred_synthetic_mouse_events = 0;
	synthetic_mouse_hover_observed = false;
	synthetic_mouse_down_observed = false;
	synthetic_mouse_up_observed = true;
	synthetic_mouse_release_pos_observed = false;
	synthetic_mouse_release_pos_obs_count = 0;
	synthetic_mouse_release_queued_before_down_obs = false;
	synthetic_mouse_warped = false;
	has_last_touch_down_pos = false;
	two_finger_tap_candidate = false;
	two_finger_scrolled = false;
	release_long_touch_ctrl();
	movie_pad_holding = false;
	for (int s = 0; s < NR_PAD_SLOTS; s++) {
		if (phys_slot_prev_raw[s])
			phys_slot_movie_suppressed[s] = true;
	}
	bool ui_down[MAX_VPAD_SLOTS];
	pthread_mutex_lock(&gp_mutex);
	for (int i = 0; i < MAX_VPAD_SLOTS; i++)
		ui_down[i] = vpad_ui_slots[i].phys_down;
	pthread_mutex_unlock(&gp_mutex);
	for (int i = 0; i < MAX_VPAD_SLOTS; i++) {
		if (vpad_rt_slots[i].prev_effective_phys || ui_down[i])
			vpad_rt_slots[i].movie_suppressed = true;
	}
}

static void reset_movie_hold_state(void)
{
	movie_mode_active = false;
	single_touch_down = false;
	movie_touch_hold_eligible = false;
	suppress_touch_until_release = false;
	movie_mouse_holding = false;
	suppress_mouse_until_release = false;
	movie_key_holding = false;
	movie_pad_holding = false;
	movie_early_release_hint = false;
	memset(suppressed_movie_keys, 0, sizeof(suppressed_movie_keys));
	send_movie_skip_ui_state(0, 0, 0, 0, false);
}

static void update_movie_skip_state(void)
{
	uint32_t now = SDL_GetTicks();
	bool active = movie_has_active_playback();
	uint32_t seq = movie_get_playback_seq();

	if (!active) {
		if (movie_mode_active) {
			movie_mode_active = false;
			movie_touch_hold_eligible = false;
			movie_mouse_holding = false;
			movie_key_holding = false;
			movie_pad_holding = false;
			movie_early_release_hint = false;
			send_movie_skip_ui_state(0, seq, 0, 0, false);
		}
		return;
	}

	if (!movie_mode_active || seq != movie_mode_seq) {
		bool new_seq = (seq != movie_mode_seq);
		movie_mode_active = true;
		movie_mode_seq = seq;
		movie_early_release_hint = false;
		reset_inputs_on_movie_start();

		if (single_touch_down) {
			single_touch_down_tick = now;
			movie_touch_hold_eligible = true;
			movie_hold_token++;
			suppress_touch_until_release = true;
		}
		if (movie_mouse_holding && new_seq) {
			movie_mouse_hold_start_tick = now;
			movie_hold_token++;
		}
		if (movie_key_holding && new_seq) {
			movie_key_hold_start_tick = now;
			movie_hold_token++;
		}
		if (!movie_touch_hold_eligible && !movie_mouse_holding && !movie_key_holding && !movie_pad_holding) {
			send_movie_skip_ui_state(1, seq, 0, 0, false);
		}
	}

	bool holding = false;
	uint32_t hold_elapsed = 0;
	if (movie_touch_hold_eligible) {
		holding = true;
		hold_elapsed = now - single_touch_down_tick;
	}
	if (movie_mouse_holding) {
		uint32_t e = now - movie_mouse_hold_start_tick;
		if (!holding || e > hold_elapsed)
			hold_elapsed = e;
		holding = true;
	}
	if (movie_key_holding) {
		uint32_t e = now - movie_key_hold_start_tick;
		if (!holding || e > hold_elapsed)
			hold_elapsed = e;
		holding = true;
	}
	if (movie_pad_holding) {
		uint32_t e = now - movie_pad_hold_start_tick;
		if (!holding || e > hold_elapsed)
			hold_elapsed = e;
		holding = true;
	}

	if (holding) {
		movie_early_release_hint = false;
		if (hold_elapsed >= MOVIE_HOLD_SKIP_MS) {
			movie_touch_hold_eligible = false;
			movie_mouse_holding = false;
			movie_key_holding = false;
			movie_pad_holding = false;
			for (int s = 0; s < NR_PAD_SLOTS; s++) {
				if (phys_slot_prev_raw[s])
					phys_slot_movie_suppressed[s] = true;
			}
			bool ui_down[MAX_VPAD_SLOTS];
			pthread_mutex_lock(&gp_mutex);
			for (int i = 0; i < MAX_VPAD_SLOTS; i++)
				ui_down[i] = vpad_ui_slots[i].phys_down;
			pthread_mutex_unlock(&gp_mutex);
			for (int i = 0; i < MAX_VPAD_SLOTS; i++) {
				if (ui_down[i] || vpad_rt_slots[i].prev_effective_phys)
					vpad_rt_slots[i].movie_suppressed = true;
			}
			if (movie_skip_active()) {
				movie_mode_active = false;
				send_movie_skip_ui_state(0, seq, 0, 0, false);
			}
			return;
		}
		uint32_t remaining = MOVIE_HOLD_SKIP_MS - hold_elapsed;
		send_movie_skip_ui_state(2, seq, movie_hold_token, (int)remaining, false);
	} else if (movie_early_release_hint) {
		movie_early_release_hint = false;
		send_movie_skip_ui_state(1, seq, 0, 0, true);
	}
}

static atomic_bool android_redraw_requested = false;

static void on_render_device_reset(bool *did_swap)
{
	bool recovered = gfx_recover_gl_if_needed();
	gfx_trace_resume(recovered ? "device_reset.recovered" : "device_reset.checked");
	if (recovered || (did_swap && !*did_swap)) {
		gfx_restore_window();
		if (did_swap)
			*did_swap = true;
	}
}

static void on_app_will_enter_background(bool *returned_foreground)
{
	if (returned_foreground)
		*returned_foreground = false;
	gfx_trace_resume("app.background");
	kbd_ctrl_down = false;
	atomic_store(&touch_ctrl_skip, false);
	memset(kbd_key_state, 0, sizeof(kbd_key_state));
	memset(sdl_mouse_down, 0, sizeof(sdl_mouse_down));
	touch_press_id = 0;
	key_state[VK_CONTROL] = false;
	clear_active_touch_fingers();
	multi_touch_active = false;
	two_finger_tap_candidate = false;
	two_finger_scrolled = false;
	synthetic_mouse_warped = false;
	release_long_touch_ctrl();
	reset_gamepad_transient_state();
	reset_movie_hold_state();
	mixer_set_app_backgrounded(true);
	if (!gfx_android_consume_lifecycle_event(SDL_APP_WILLENTERBACKGROUND))
		gfx_android_on_background();
	savedata_flush_on_background();
}

static void on_app_did_enter_foreground(bool *returned_foreground)
{
	if (returned_foreground)
		*returned_foreground = true;
	gfx_trace_resume("app.foreground");
	mixer_set_app_backgrounded(false);
	if (!gfx_android_consume_lifecycle_event(SDL_APP_DIDENTERFOREGROUND))
		gfx_android_on_foreground();
}

static void finish_event_drain(bool did_swap, bool returned_foreground)
{
	if (atomic_exchange(&android_redraw_requested, false) && !did_swap)
		gfx_restore_window();
	if (returned_foreground)
		gfx_release_context_backup_if_ready();
}

static void handle_window_event(SDL_Event *e, bool *did_swap, bool *returned_foreground)
{
	if (e->window.event == SDL_WINDOWEVENT_RESTORED)
		gfx_trace_resume("window.restored");
	else if (e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
		gfx_trace_resume("window.focus_gained");
	else if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST)
		gfx_trace_resume("window.focus_lost");
	switch (e->window.event) {
	case SDL_WINDOWEVENT_EXPOSED:
		gfx_swap();
		if (did_swap)
			*did_swap = true;
		break;
	case SDL_WINDOWEVENT_ENTER:
		mouse_focus = true;
		break;
	case SDL_WINDOWEVENT_LEAVE:
#ifndef __ANDROID__
		mouse_focus = false;
#endif
		break;
	case SDL_WINDOWEVENT_RESTORED:
		if (returned_foreground)
			*returned_foreground = true;
		gfx_android_on_foreground();
		gfx_restore_window();
		if (did_swap)
			*did_swap = true;
		break;
	case SDL_WINDOWEVENT_FOCUS_GAINED:
		mouse_focus = true;
		keyboard_focus = true;
		mixer_set_app_backgrounded(false);
		mixer_set_window_focus(true);
		if (returned_foreground)
			*returned_foreground = true;
		gfx_android_on_foreground();
		break;
	case SDL_WINDOWEVENT_FOCUS_LOST:
#ifndef __ANDROID__
		keyboard_focus = false;
#endif
		kbd_ctrl_down = false;
		atomic_store(&touch_ctrl_skip, false);
		memset(kbd_key_state, 0, sizeof(kbd_key_state));
		memset(sdl_mouse_down, 0, sizeof(sdl_mouse_down));
		touch_press_id = 0;
		key_state[VK_CONTROL] = false;
		clear_active_touch_fingers();
		multi_touch_active = false;
		two_finger_tap_candidate = false;
		two_finger_scrolled = false;
		synthetic_mouse_warped = false;
		release_long_touch_ctrl();
		reset_gamepad_transient_state();
		reset_movie_hold_state();
		mixer_set_window_focus(false);
		break;
	case SDL_WINDOWEVENT_SIZE_CHANGED:
		gfx_update_screen_scale();
		gfx_reset_frame_pacer();
		gfx_swap();
		if (did_swap)
			*did_swap = true;
		break;
	}
}

static void enqueue_synthetic_mouse_button(const SDL_MouseButtonEvent *btn)
{
	if (nr_deferred_synthetic_mouse_events == MAX_DEFERRED_SYNTHETIC_MOUSE_EVENTS) {
		pop_deferred_synthetic_mouse_event(SDL_GetTicks());
	}
	if (nr_deferred_synthetic_mouse_events == 0) {
		synthetic_mouse_warped = false;
		if (btn->state == SDL_PRESSED) {
			synthetic_mouse_hover_observed = false;
			if (!key_state[VK_LBUTTON] && !key_state[VK_RBUTTON]) {
				synthetic_mouse_x = btn->x;
				synthetic_mouse_y = btn->y;
				has_synthetic_mouse_pos = true;
			}
		} else {
			synthetic_mouse_release_pos_observed = false;
			synthetic_mouse_release_pos_obs_count = 0;
			synthetic_mouse_release_queued_before_down_obs = !synthetic_mouse_down_observed;
		}
	}
	deferred_synthetic_mouse_events[nr_deferred_synthetic_mouse_events] = *btn;
	deferred_synthetic_mouse_events[nr_deferred_synthetic_mouse_events].timestamp = SDL_GetTicks();
	nr_deferred_synthetic_mouse_events++;
}

static void handle_touch_mouse_button(SDL_MouseButtonEvent *btn)
{
	if (movie_has_active_playback() || suppress_touch_until_release) {
		has_last_touch_down_pos = false;
		return;
	}
	if (multi_touch_active) {
		has_last_touch_down_pos = false;
		return;
	}
	if (btn->state == SDL_PRESSED) {
		last_touch_down_pos.x = btn->x;
		last_touch_down_pos.y = btn->y;
		has_last_touch_down_pos = true;
		sync_gamepad_cursor_from_window(btn->x, btn->y);
	} else {
		if (has_last_touch_down_pos) {
			int dx = abs(btn->x - last_touch_down_pos.x);
			int dy = abs(btn->y - last_touch_down_pos.y);
			int snap_dist = max(16, min(sdl.viewport.w, sdl.viewport.h) / 40);
			if (dx <= snap_dist && dy <= snap_dist) {
				btn->x = last_touch_down_pos.x;
				btn->y = last_touch_down_pos.y;
			}
			has_last_touch_down_pos = false;
		}
		if (nr_deferred_synthetic_mouse_events == 0 &&
		    !key_state[VK_LBUTTON] && !key_state[VK_RBUTTON]) {
			return;
		}
	}
	enqueue_synthetic_mouse_button(btn);
}

static void handle_touch_finger_event(const SDL_TouchFingerEvent *tfinger, bool down)
{
	if (down) {
		int num_fingers = track_touch_finger_down(tfinger->touchId, tfinger->fingerId);
		if (num_fingers >= 2) {
			if (movie_touch_hold_eligible)
				movie_early_release_hint = true;
			single_touch_down = false;
			movie_touch_hold_eligible = false;
			if (movie_has_active_playback() || suppress_touch_until_release) {
				suppress_touch_until_release = true;
			}
			release_long_touch_ctrl();
			touch_press_id = 0;
			// Multi-touch gesture: cancel single-finger mouse state and
			// track 2-finger tap (right-click) vs 2-finger scroll.
			multi_touch_active = true;
			key_state[VK_LBUTTON] = false;
			key_state[VK_RBUTTON] = false;
			sdl_mouse_down[VK_LBUTTON] = false;
			sdl_mouse_down[VK_RBUTTON] = false;
			nr_deferred_synthetic_mouse_events = 0;
			synthetic_mouse_hover_observed = false;
			synthetic_mouse_down_observed = false;
			synthetic_mouse_up_observed = true;
			synthetic_mouse_release_pos_observed = false;
			synthetic_mouse_release_pos_obs_count = 0;
			synthetic_mouse_release_queued_before_down_obs = false;
			synthetic_mouse_warped = false;
			has_last_touch_down_pos = false;
			scroll_gesture_y = -1.0f;
			if (num_fingers == 2) {
				two_finger_tap_candidate = true;
				two_finger_scrolled = false;
				two_finger_tap_start_tick = SDL_GetTicks();
			} else {
				two_finger_tap_candidate = false;
			}
		} else {
			multi_touch_active = false;
			two_finger_tap_candidate = false;
			single_touch_down = true;
			single_touch_finger_id = tfinger->fingerId;
			single_touch_start_x = tfinger->x;
			single_touch_start_y = tfinger->y;
			single_touch_down_tick = SDL_GetTicks();
			gamepad_cursor_active = false;
			gamepad_cursor_visible = false;
			if (movie_has_active_playback()) {
				movie_touch_hold_eligible = true;
				movie_hold_token++;
				suppress_touch_until_release = true;
			}
			release_long_touch_ctrl();
			if (atomic_load(&long_press_ctrl_enabled) && !suppress_touch_until_release) {
				long_touch_armed = true;
				long_touch_start_tick = tfinger->timestamp;
				long_touch_finger_rect = (SDL_FRect) {
					.x = tfinger->x - 0.01f,
					.y = tfinger->y - 0.01f,
					.w = 0.02f,
					.h = 0.02f
				};
			}
		}
		return;
	}

	// SDL can queue the matching synthetic mouse release before FINGERUP.
	bool only_touch_release_pending = nr_deferred_synthetic_mouse_events == 1 &&
		deferred_synthetic_mouse_events[0].button == SDL_BUTTON_LEFT &&
		deferred_synthetic_mouse_events[0].state == SDL_RELEASED;
	if (long_touch_armed && touch_press_id && !long_touch_right_button_down() &&
	    nr_active_touch_fingers == 1 && active_touch_fingers[0].touch_id == tfinger->touchId &&
	    active_touch_fingers[0].finger_id == tfinger->fingerId &&
	    tfinger->timestamp - long_touch_start_tick >= LONG_TOUCH_DURATION &&
	    (nr_deferred_synthetic_mouse_events == 0 || only_touch_release_pending)) {
		cancel_long_touch_click();
	}
	release_long_touch_ctrl();

	if (single_touch_down && tfinger->fingerId == single_touch_finger_id) {
		if (movie_touch_hold_eligible)
			movie_early_release_hint = true;
		single_touch_down = false;
		movie_touch_hold_eligible = false;
	}
	int rem_fingers = track_touch_finger_up(tfinger->touchId, tfinger->fingerId);
	if (rem_fingers == 0) {
		single_touch_down = false;
		movie_touch_hold_eligible = false;
		multi_touch_active = false;
		if (movie_has_active_playback() || suppress_touch_until_release) {
			if (suppress_touch_until_release && movie_has_active_playback())
				movie_early_release_hint = true;
			suppress_touch_until_release = false;
			two_finger_tap_candidate = false;
			two_finger_scrolled = false;
			return;
		}
		if (two_finger_tap_candidate && !two_finger_scrolled &&
		    (SDL_GetTicks() - two_finger_tap_start_tick) <= TWO_FINGER_TAP_MAX_MS) {
			int rx = has_synthetic_mouse_pos ? synthetic_mouse_x
				: (sdl.viewport.x + sdl.viewport.w / 2);
			int ry = has_synthetic_mouse_pos ? synthetic_mouse_y
				: (sdl.viewport.y + sdl.viewport.h / 2);
			if (sdl.viewport.w > 0 && sdl.viewport.h > 0) {
				rx = max(sdl.viewport.x, min(sdl.viewport.x + sdl.viewport.w - 1, rx));
				ry = max(sdl.viewport.y, min(sdl.viewport.y + sdl.viewport.h - 1, ry));
			}
			SDL_MouseButtonEvent rpress = {
				.type = SDL_MOUSEBUTTONDOWN,
				.which = SDL_TOUCH_MOUSEID,
				.button = SDL_BUTTON_RIGHT,
				.state = SDL_PRESSED,
				.clicks = 1,
				.x = rx,
				.y = ry,
			};
			SDL_MouseButtonEvent rrelease = rpress;
			rrelease.type = SDL_MOUSEBUTTONUP;
			rrelease.state = SDL_RELEASED;
			enqueue_synthetic_mouse_button(&rpress);
			enqueue_synthetic_mouse_button(&rrelease);
		}
		two_finger_tap_candidate = false;
		two_finger_scrolled = false;
	}
}

void handle_window_events(void)
{
	bool did_swap = false;
	bool returned_foreground = false;
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_QUIT:
			handle_quit_event();
			break;
		case SDL_WINDOWEVENT:
			handle_window_event(&e, &did_swap, &returned_foreground);
			break;
		case SDL_RENDER_DEVICE_RESET:
			on_render_device_reset(&did_swap);
			break;
		case SDL_RENDER_TARGETS_RESET:
			gfx_trace_resume("targets_reset");
			break;
		case SDL_APP_WILLENTERBACKGROUND:
			on_app_will_enter_background(&returned_foreground);
			break;
		case SDL_APP_DIDENTERFOREGROUND:
			on_app_did_enter_foreground(&returned_foreground);
			if (!did_swap) {
				gfx_restore_window();
				did_swap = true;
			}
			break;
		default:
#ifdef __ANDROID__
			if (android_parts_text_event && e.type == android_parts_text_event)
				free(e.user.data1);
#endif
			break;
		}
	}
	finish_event_drain(did_swap, returned_foreground);
}

void handle_events(void)
{
#ifdef __SWITCH__
	// Keep the video clock running normally; do not park the VM in a modal
	// while a movie is using the game's input/render loop.
	if (!movie_has_active_playback() && switch_menu4_poll()) {
		kbd_ctrl_down = false;
		memset(kbd_key_state, 0, sizeof(kbd_key_state));
		memset(sdl_mouse_down, 0, sizeof(sdl_mouse_down));
		clear_active_touch_fingers();
		single_touch_down = multi_touch_active = false;
		cancel_long_touch_click();
		reset_inputs_on_movie_start();
		reset_gamepad_transient_state();
		pthread_mutex_lock(&gp_mutex);
		for (size_t i = 0; i < MAX_CONTROLLERS; ++i) {
			memset(pad_slots[i].btn_down, 0, sizeof(pad_slots[i].btn_down));
			memset(pad_slots[i].hat_dir, 0, sizeof(pad_slots[i].hat_dir));
			memset(pad_slots[i].axes, 0, sizeof(pad_slots[i].axes));
		}
		pthread_mutex_unlock(&gp_mutex);
		joy_clear_flag();
		if (!switch_menu_lifecycle_pending())
			gfx_swap();
	}
#endif
	update_movie_skip_state();
	static const enum sact_keycode mouse_btns[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON };
	uint32_t poll_now = SDL_GetTicks();
	for (size_t i = 0; i < sizeof(mouse_btns) / sizeof(mouse_btns[0]); i++) {
		enum sact_keycode code = mouse_btns[i];
		if (deferred_mouse_release[code] &&
		    (!mouse_down_unobserved[code] || (poll_now - mouse_down_timestamp[code]) >= DEFERRED_MOUSE_RELEASE_MAX_DELAY)) {
			key_state[code] = false;
			deferred_mouse_release[code] = false;
			mouse_down_unobserved[code] = false;
		}
	}

	fire_deferred_events(false, false);

	bool did_swap = false;
	bool returned_foreground = false;
#ifdef __SWITCH__
	bool redraw_switch_controls = false;
#endif
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
#ifdef __SWITCH__
		if (switch_controls_gl_touch_event(&e, sdl.window, &redraw_switch_controls))
			continue;
#endif
		switch (e.type) {
		case SDL_QUIT:
			handle_quit_event();
			break;
		case SDL_WINDOWEVENT:
			handle_window_event(&e, &did_swap, &returned_foreground);
			break;
		case SDL_APP_LOWMEMORY:
			PE_FlushCGCache();
			break;
		case SDL_RENDER_DEVICE_RESET:
			on_render_device_reset(&did_swap);
			break;
		case SDL_RENDER_TARGETS_RESET:
			gfx_trace_resume("targets_reset");
			break;
		case SDL_APP_WILLENTERBACKGROUND:
			on_app_will_enter_background(&returned_foreground);
			break;
		case SDL_APP_DIDENTERFOREGROUND:
			on_app_did_enter_foreground(&returned_foreground);
			gfx_restore_window();
			did_swap = true;
			break;
#ifdef ALICEHUB_IOS
		case SDL_AUDIODEVICEADDED:
			if (!e.adevice.iscapture)
				mixer_retry_audio_device();
			break;
#endif
		case SDL_AUDIODEVICEREMOVED:
			if (!e.adevice.iscapture)
				mixer_handle_device_removed(e.adevice.which);
			break;
		case SDL_KEYDOWN:
			if (e.key.keysym.scancode == SDL_SCANCODE_F9) {
				vm_stack_trace();
			} else if (e.key.keysym.scancode == SDL_SCANCODE_F11) {
				gfx_toggle_fullscreen();
			} else if (e.key.keysym.scancode == SDL_SCANCODE_S) {
				if (e.key.keysym.mod & (KMOD_LALT | KMOD_RALT)) {
#ifdef __ANDROID__
					emit_emu_cmd_to_ui(INPUT_EMU_SCREENSHOT);
#else
					screenshot_save();
#endif
				}
			}
			// fallthrough
		case SDL_KEYUP:
#ifdef __ANDROID__
			if (text_handlers && e.key.timestamp < last_keyevent_timestamp + DEFERRED_KEY_DELAY) {
				// Input from virtual keyboard is sent as consecutive
				// SDL_KEYDOWN and SDL_KEYUP events. To give the game a chance
				// to see the previous key event, delay the event.
				struct deferred_keyevent *ev = xmalloc(sizeof(struct deferred_keyevent));
				ev->e = e;
				STAILQ_INSERT_TAIL(&deferred_keyevent_queue, ev, entry);
			} else {
				last_keyevent_timestamp = e.key.timestamp;
				key_event(&e.key, e.type == SDL_KEYDOWN);
			}
#else
			key_event(&e.key, e.type == SDL_KEYDOWN);
#endif
			break;
		case SDL_MOUSEMOTION:
			if (e.motion.which != SDL_TOUCH_MOUSEID) {
				has_synthetic_mouse_pos = false;
				synthetic_mouse_warped = false;
				sync_gamepad_cursor_from_window(e.motion.x, e.motion.y);
			} else if (synthetic_mouse_warped &&
			           (e.motion.x != synthetic_mouse_warp_raw_x ||
			            e.motion.y != synthetic_mouse_warp_raw_y)) {
				synthetic_mouse_warped = false;
			}
			break;
		case SDL_MOUSEBUTTONUP:
		case SDL_MOUSEBUTTONDOWN:
			if (e.button.which == SDL_TOUCH_MOUSEID) {
				handle_touch_mouse_button(&e.button);
			} else {
				has_synthetic_mouse_pos = false;
				synthetic_mouse_warped = false;
				sync_gamepad_cursor_from_window(e.button.x, e.button.y);
				if (movie_has_active_playback() || suppress_mouse_until_release) {
					if (e.button.state == SDL_PRESSED && movie_has_active_playback()) {
						if (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT) {
							movie_mouse_holding = true;
							movie_mouse_button = e.button.button;
							movie_mouse_hold_start_tick = SDL_GetTicks();
							movie_hold_token++;
							suppress_mouse_until_release = true;
						}
					} else if (e.button.state == SDL_RELEASED) {
						if (movie_mouse_holding && e.button.button == movie_mouse_button) {
							movie_mouse_holding = false;
							movie_early_release_hint = true;
						}
						if (suppress_mouse_until_release &&
						    !(SDL_GetMouseState(NULL, NULL) & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK))) {
							suppress_mouse_until_release = false;
						}
					}
					break;
				}
				mouse_event(&e.button);
			}
			break;
		case SDL_MOUSEWHEEL:
			wheel_dir = e.wheel.y;
			break;
		case SDL_FINGERDOWN:
			handle_touch_finger_event(&e.tfinger, true);
			break;
		case SDL_FINGERMOTION:
			if ((long_touch_armed || long_touch_ctrl_down) &&
			    e.tfinger.fingerId == single_touch_finger_id &&
			    !SDL_PointInFRect(&(SDL_FPoint){ e.tfinger.x, e.tfinger.y }, &long_touch_finger_rect)) {
				release_long_touch_ctrl();
			}
			if (single_touch_down && e.tfinger.fingerId == single_touch_finger_id && movie_touch_hold_eligible) {
				float dx = e.tfinger.x - single_touch_start_x;
				float dy = e.tfinger.y - single_touch_start_y;
				if (dx * dx + dy * dy > MOVIE_TOUCH_SLOP_SQ) {
					movie_touch_hold_eligible = false;
					movie_early_release_hint = true;
				}
			}
			break;
		case SDL_FINGERUP:
			handle_touch_finger_event(&e.tfinger, false);
			break;
		case SDL_MULTIGESTURE:
			if (e.mgesture.numFingers == 2) {
				if (scroll_gesture_y < 0.0f)
					scroll_gesture_y = e.mgesture.y;
				float dy = scroll_gesture_y - e.mgesture.y;
				if (dy * dy > SCROLL_GESTURE_SENSITIVITY * SCROLL_GESTURE_SENSITIVITY) {
					wheel_dir = dy < 0 ? 1 : -1;  // Swipe up to scroll down.
					scroll_gesture_y = e.mgesture.y;
					two_finger_scrolled = true;
					two_finger_tap_candidate = false;
				}
			}
			break;
		case SDL_CONTROLLERDEVICEADDED:
			open_game_controller_index(e.cdevice.which);
			break;
		case SDL_CONTROLLERDEVICEREMOVED:
			close_controller_instance(e.cdevice.which);
			break;
		case SDL_CONTROLLERAXISMOTION:
			controller_axis_event(&e.caxis);
			break;
		case SDL_CONTROLLERBUTTONUP:
		case SDL_CONTROLLERBUTTONDOWN:
			controller_button_event(&e.cbutton);
			break;
		case SDL_JOYDEVICEADDED:
			open_generic_joystick_index(e.jdevice.which);
			break;
		case SDL_JOYDEVICEREMOVED:
			close_controller_instance(e.jdevice.which);
			break;
		case SDL_JOYAXISMOTION:
			generic_joy_axis_event(&e.jaxis);
			break;
		case SDL_JOYHATMOTION:
			generic_joy_hat_event(&e.jhat);
			break;
		case SDL_JOYBUTTONUP:
		case SDL_JOYBUTTONDOWN:
			generic_joy_button_event(&e.jbutton);
			break;
		case SDL_TEXTINPUT:
			if (!text_handlers || !text_handlers->commit) {
				break;
#ifdef __ANDROID__
			} else if (!STAILQ_EMPTY(&deferred_keyevent_queue)) {
				// The order of key events and text events must be preserved.
				struct deferred_keyevent *ev = xmalloc(sizeof(struct deferred_keyevent));
				ev->e = e;
				STAILQ_INSERT_TAIL(&deferred_keyevent_queue, ev, entry);
#endif
			} else {
				text_handlers->commit(e.text.text);
			}
			break;
		case SDL_TEXTEDITING:
			if (text_handlers && text_handlers->edit)
				text_handlers->edit(e.edit.text, e.edit.start, e.edit.length);
			break;
		default:
#ifdef __ANDROID__
			if (android_parts_text_event && e.type == android_parts_text_event) {
				if (STAILQ_EMPTY(&deferred_keyevent_queue)) {
					dispatch_android_parts_text(&e.user);
				} else {
					struct deferred_keyevent *ev = xmalloc(sizeof(*ev));
					ev->e = e;
					STAILQ_INSERT_TAIL(&deferred_keyevent_queue, ev, entry);
				}
			}
#endif
			break;
		}
	}
#ifdef __SWITCH__
	if (redraw_switch_controls) {
		gfx_swap();
		did_swap = true;
	}
#endif
	finish_event_drain(did_swap, returned_foreground);
	process_gamepad_frame();
	update_movie_skip_state();
	update_long_touch_ctrl();
	if (movie_has_active_playback()) {
		for (int i = 1; i < VK_NR_KEYCODES; i++) {
			if (is_movie_skip_suppressed_key(i))
				key_state[i] = false;
		}
	}
	if (dbg_dap)
		dbg_dap_handle_messages();
}


void input_reset(void)
{
	memset(key_state, 0, sizeof(key_state));
	memset(joybutton_state, 0, sizeof(joybutton_state));
	memset(joyaxis_state, 0, sizeof(joyaxis_state));
	wheel_dir = 0;
	legacy_text_handlers = parts_text_handlers = text_handlers = NULL;
	reset_inputs_on_movie_start();
	input_reset_close_hook();
	mouse_focus = keyboard_focus = true;
	scroll_gesture_y = 0;
#ifdef __ANDROID__
	while (!STAILQ_EMPTY(&deferred_keyevent_queue)) {
		struct deferred_keyevent *event = STAILQ_FIRST(&deferred_keyevent_queue);
		STAILQ_REMOVE_HEAD(&deferred_keyevent_queue, entry);
		free(event);
	}
	last_keyevent_timestamp = 0;
#endif
}

static void parse_binding_json(cJSON *obj, struct input_action_spec *out)
{
	if (!obj || !cJSON_IsObject(obj))
		return;
	cJSON *t = cJSON_GetObjectItemCaseSensitive(obj, "type");
	cJSON *c = cJSON_GetObjectItemCaseSensitive(obj, "code");
	cJSON *c2 = cJSON_GetObjectItemCaseSensitive(obj, "code2");
	cJSON *tg = cJSON_GetObjectItemCaseSensitive(obj, "toggle");
	out->type = (uint8_t)(cJSON_IsNumber(t) ? t->valueint : 0);
	out->code = (int16_t)(cJSON_IsNumber(c) ? c->valueint : 0);
	out->code2 = (int16_t)(cJSON_IsNumber(c2) ? c2->valueint : 0);
	out->toggle_mode = cJSON_IsBool(tg) ? cJSON_IsTrue(tg) : false;
}

void input_set_gamepad_config_json(const char *json)
{
	if (!json || !json[0])
		return;
	cJSON *root = cJSON_Parse(json);
	if (!root || !cJSON_IsObject(root)) {
		cJSON_Delete(root);
		return;
	}
	pthread_mutex_lock(&gp_mutex);
	ensure_gamepad_config_defaults_locked();
	struct gamepad_config prev_cfg = gp_cfg;

	cJSON *phys = cJSON_GetObjectItemCaseSensitive(root, "physical");
	if (cJSON_IsObject(phys)) {
		cJSON *en = cJSON_GetObjectItemCaseSensitive(phys, "enabled");
		if (cJSON_IsBool(en)) gp_cfg.phys_enabled = cJSON_IsTrue(en);
		cJSON *swap = cJSON_GetObjectItemCaseSensitive(phys, "swapAbXy");
		if (cJSON_IsBool(swap)) gp_cfg.swap_ab_xy = cJSON_IsTrue(swap);
		cJSON *ls = cJSON_GetObjectItemCaseSensitive(phys, "leftStickMode");
		if (cJSON_IsNumber(ls)) gp_cfg.phys_left_stick_mode = ls->valueint;
		cJSON *rs = cJSON_GetObjectItemCaseSensitive(phys, "rightStickMode");
		if (cJSON_IsNumber(rs)) gp_cfg.phys_right_stick_mode = rs->valueint;
		cJSON *dp = cJSON_GetObjectItemCaseSensitive(phys, "dpadMode");
		if (cJSON_IsNumber(dp)) gp_cfg.phys_dpad_mode = dp->valueint;
		cJSON *dz = cJSON_GetObjectItemCaseSensitive(phys, "deadzonePct");
		if (cJSON_IsNumber(dz)) {
			int p = dz->valueint;
			if (p < 5) p = 5;
			if (p > 45) p = 45;
			gp_cfg.deadzone = (float)p / 100.0f;
		}
		cJSON *sp = cJSON_GetObjectItemCaseSensitive(phys, "cursorSpeedPct");
		if (cJSON_IsNumber(sp)) {
			int p = sp->valueint;
			if (p < 20) p = 20;
			if (p > 300) p = 300;
			gp_cfg.cursor_speed = 720.0f * ((float)p / 100.0f);
		}
		cJSON *rm = cJSON_GetObjectItemCaseSensitive(phys, "rumbleEnabled");
		if (cJSON_IsBool(rm)) gp_cfg.rumble_enabled = cJSON_IsTrue(rm);

		cJSON *bindings = cJSON_GetObjectItemCaseSensitive(phys, "bindings");
		if (cJSON_IsObject(bindings)) {
			for (int s = 0; s < NR_PAD_SLOTS; s++) {
				char key[16];
				snprintf(key, sizeof(key), "%d", s);
				cJSON *item = cJSON_GetObjectItemCaseSensitive(bindings, key);
				if (item)
					parse_binding_json(item, &gp_cfg.phys_bindings[s]);
			}
		}
	}

	cJSON *vpad = cJSON_GetObjectItemCaseSensitive(root, "virtual");
	if (cJSON_IsObject(vpad)) {
		cJSON *vls = cJSON_GetObjectItemCaseSensitive(vpad, "leftStickMode");
		if (cJSON_IsNumber(vls)) gp_cfg.vpad_left_stick_mode = vls->valueint;
		cJSON *vrs = cJSON_GetObjectItemCaseSensitive(vpad, "rightStickMode");
		if (cJSON_IsNumber(vrs)) gp_cfg.vpad_right_stick_mode = vrs->valueint;
	}

	bool phys_mapping_changed = prev_cfg.phys_enabled != gp_cfg.phys_enabled
		|| prev_cfg.swap_ab_xy != gp_cfg.swap_ab_xy
		|| prev_cfg.phys_dpad_mode != gp_cfg.phys_dpad_mode
		|| memcmp(prev_cfg.phys_bindings, gp_cfg.phys_bindings, sizeof(gp_cfg.phys_bindings)) != 0;
	bool vpad_mapping_changed = prev_cfg.vpad_left_stick_mode != gp_cfg.vpad_left_stick_mode
		|| prev_cfg.vpad_right_stick_mode != gp_cfg.vpad_right_stick_mode;

	if (phys_mapping_changed)
		phys_latch_reset_gen++;
	if (vpad_mapping_changed)
		vpad_latch_reset_gen++;

	pthread_mutex_unlock(&gp_mutex);
	cJSON_Delete(root);
}

void input_set_virtual_stick(int stick_id, float x, float y)
{
	if (stick_id < 0 || stick_id > 1)
		return;
	if (x < -1.0f) x = -1.0f;
	if (x > 1.0f) x = 1.0f;
	if (y < -1.0f) y = -1.0f;
	if (y > 1.0f) y = 1.0f;
	pthread_mutex_lock(&gp_mutex);
	vpad_stick_x[stick_id] = x;
	vpad_stick_y[stick_id] = y;
	pthread_mutex_unlock(&gp_mutex);
}

void input_set_virtual_dpad(int stick_mode, bool left, bool right, bool up, bool down)
{
	pthread_mutex_lock(&gp_mutex);
	vpad_dpad_cfg_mode = stick_mode;
	vpad_dpad_dir[0] = left;
	vpad_dpad_dir[1] = right;
	vpad_dpad_dir[2] = up;
	vpad_dpad_dir[3] = down;
	pthread_mutex_unlock(&gp_mutex);
}

void input_set_virtual_button(int slot_id, int action_type, int code, int code2, bool toggle_mode, bool pressed)
{
	if (slot_id < 0 || slot_id >= MAX_VPAD_SLOTS)
		return;
	pthread_mutex_lock(&gp_mutex);
	struct vpad_ui_slot_state *st = &vpad_ui_slots[slot_id];
	struct input_action_spec act = {
		.type = (uint8_t)action_type,
		.code = (int16_t)code,
		.code2 = (int16_t)code2,
		.toggle_mode = toggle_mode,
	};
	if (pressed && !st->phys_down) {
		st->pending_press_edge = true;
		st->pending_action = act;
	}
	st->active_action = act;
	st->phys_down = pressed;
	pthread_mutex_unlock(&gp_mutex);
}

void input_clear_virtual_inputs(void)
{
	pthread_mutex_lock(&gp_mutex);
#ifdef ALICEHUB_IOS
	atomic_store(&touch_ctrl_skip, false);
#endif
	for (int i = 0; i < MAX_VPAD_SLOTS; i++) {
		vpad_ui_slots[i].phys_down = false;
		vpad_ui_slots[i].pending_press_edge = false;
	}
	vpad_latch_reset_gen++;
	vpad_stick_x[0] = vpad_stick_y[0] = 0.0f;
	vpad_stick_y[1] = vpad_stick_x[1] = 0.0f;
	memset(vpad_dpad_dir, 0, sizeof(vpad_dpad_dir));
	pthread_mutex_unlock(&gp_mutex);
}

static int android_keycode_to_pad_slot(int keycode)
{
	switch (keycode) {
	case 96:  return PAD_SLOT_A;      // KEYCODE_BUTTON_A
	case 97:  return PAD_SLOT_B;      // KEYCODE_BUTTON_B
	case 99:  return PAD_SLOT_X;      // KEYCODE_BUTTON_X
	case 100: return PAD_SLOT_Y;      // KEYCODE_BUTTON_Y
	case 102: return PAD_SLOT_LB;     // KEYCODE_BUTTON_L1
	case 103: return PAD_SLOT_RB;     // KEYCODE_BUTTON_R1
	case 104: return PAD_SLOT_LT;     // KEYCODE_BUTTON_L2
	case 105: return PAD_SLOT_RT;     // KEYCODE_BUTTON_R2
	case 106: return PAD_SLOT_L3;     // KEYCODE_BUTTON_THUMBL
	case 107: return PAD_SLOT_R3;     // KEYCODE_BUTTON_THUMBR
	case 108: return PAD_SLOT_START;  // KEYCODE_BUTTON_START
	case 109: return PAD_SLOT_BACK;   // KEYCODE_BUTTON_SELECT
	case 110: return PAD_SLOT_GUIDE;  // KEYCODE_BUTTON_MODE
	case 19:  return PAD_SLOT_DPAD_UP;
	case 20:  return PAD_SLOT_DPAD_DOWN;
	case 21:  return PAD_SLOT_DPAD_LEFT;
	case 22:  return PAD_SLOT_DPAD_RIGHT;
	case 23:  return PAD_SLOT_A;      // KEYCODE_DPAD_CENTER
	case 188: return PAD_SLOT_A;      // KEYCODE_BUTTON_1
	case 189: return PAD_SLOT_B;      // KEYCODE_BUTTON_2
	case 190: return PAD_SLOT_X;      // KEYCODE_BUTTON_3
	case 191: return PAD_SLOT_Y;      // KEYCODE_BUTTON_4
	case 192: return PAD_SLOT_LB;     // KEYCODE_BUTTON_5
	case 193: return PAD_SLOT_RB;     // KEYCODE_BUTTON_6
	case 194: return PAD_SLOT_LT;     // KEYCODE_BUTTON_7
	case 195: return PAD_SLOT_RT;     // KEYCODE_BUTTON_8
	case 196: return PAD_SLOT_BACK;   // KEYCODE_BUTTON_9
	case 197: return PAD_SLOT_START;  // KEYCODE_BUTTON_10
	case 198: return PAD_SLOT_L3;     // KEYCODE_BUTTON_11
	case 199: return PAD_SLOT_R3;     // KEYCODE_BUTTON_12
	default:  return -1;
	}
}

void input_set_android_pad_key(int device_id, int android_keycode, bool pressed)
{
	if (android_keycode < 0 && !pressed) {
		pthread_mutex_lock(&gp_mutex);
		if (android_pad.active && (device_id < 0 || android_pad.device_id == device_id)) {
			memset(&android_pad, 0, sizeof(android_pad));
		}
		pthread_mutex_unlock(&gp_mutex);
		return;
	}
	int slot = android_keycode_to_pad_slot(android_keycode);
	if (slot < 0)
		return;
	pthread_mutex_lock(&gp_mutex);
	android_pad.active = true;
	android_pad.device_id = device_id;
	android_pad.btn_down[slot] = pressed;
	pthread_mutex_unlock(&gp_mutex);
}

void input_set_android_pad_motion(int device_id, float lx, float ly, float rx, float ry,
		float lt, float rt, float hat_x, float hat_y)
{
	pthread_mutex_lock(&gp_mutex);
	android_pad.active = true;
	android_pad.device_id = device_id;
	android_pad.axes[0] = lx;
	android_pad.axes[1] = ly;
	android_pad.axes[2] = rx;
	android_pad.axes[3] = ry;
	android_pad.axes[4] = lt;
	android_pad.axes[5] = rt;
	android_pad.hat_x = hat_x;
	android_pad.hat_y = hat_y;
	pthread_mutex_unlock(&gp_mutex);
}

void input_reset_gamepad_cursor(bool visible)
{
	pthread_mutex_lock(&gp_mutex);
	pending_cursor_reset = true;
	pending_cursor_reset_visible = visible;
	pthread_mutex_unlock(&gp_mutex);
}

char *input_get_controller_status_json(void)
{
	struct {
		char name[128];
		uint16_t vendor_id;
		uint16_t product_id;
		bool is_game_controller;
	} snap_pads[MAX_CONTROLLERS + 1];

	int count = 0;
	bool raw_phys[NR_PAD_SLOTS];
	memset(raw_phys, 0, sizeof(raw_phys));
	float ax[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

	pthread_mutex_lock(&gp_mutex);
	for (int i = 0; i < MAX_CONTROLLERS; i++) {
		if (!pad_slots[i].active)
			continue;
		sanitize_utf8_copy(pad_slots[i].name, snap_pads[count].name, sizeof(snap_pads[count].name));
		snap_pads[count].vendor_id = pad_slots[i].vendor_id;
		snap_pads[count].product_id = pad_slots[i].product_id;
		snap_pads[count].is_game_controller = pad_slots[i].is_game_controller;
		count++;

		for (int b = 0; b < NR_PAD_SLOTS; b++) {
			if (pad_slots[i].btn_down[b])
				raw_phys[b] = true;
		}
		if (pad_slots[i].hat_dir[0]) raw_phys[PAD_SLOT_DPAD_UP] = true;
		if (pad_slots[i].hat_dir[1]) raw_phys[PAD_SLOT_DPAD_DOWN] = true;
		if (pad_slots[i].hat_dir[2]) raw_phys[PAD_SLOT_DPAD_LEFT] = true;
		if (pad_slots[i].hat_dir[3]) raw_phys[PAD_SLOT_DPAD_RIGHT] = true;
		if (fabsf(pad_slots[i].axes[0]) > fabsf(ax[0])) ax[0] = pad_slots[i].axes[0];
		if (fabsf(pad_slots[i].axes[1]) > fabsf(ax[1])) ax[1] = pad_slots[i].axes[1];
		if (fabsf(pad_slots[i].axes[2]) > fabsf(ax[2])) ax[2] = pad_slots[i].axes[2];
		if (fabsf(pad_slots[i].axes[3]) > fabsf(ax[3])) ax[3] = pad_slots[i].axes[3];
		if (pad_slots[i].axes[4] > ax[4]) ax[4] = pad_slots[i].axes[4];
		if (pad_slots[i].axes[5] > ax[5]) ax[5] = pad_slots[i].axes[5];
	}
	if (count == 0 && android_pad.active) {
		sanitize_utf8_copy("Android Gamepad", snap_pads[0].name, sizeof(snap_pads[0].name));
		snap_pads[0].vendor_id = 0;
		snap_pads[0].product_id = 0;
		snap_pads[0].is_game_controller = true;
		count = 1;

		for (int b = 0; b < NR_PAD_SLOTS; b++) {
			if (android_pad.btn_down[b])
				raw_phys[b] = true;
		}
		if (android_pad.hat_y < -0.5f) raw_phys[PAD_SLOT_DPAD_UP] = true;
		if (android_pad.hat_y > 0.5f)  raw_phys[PAD_SLOT_DPAD_DOWN] = true;
		if (android_pad.hat_x < -0.5f) raw_phys[PAD_SLOT_DPAD_LEFT] = true;
		if (android_pad.hat_x > 0.5f)  raw_phys[PAD_SLOT_DPAD_RIGHT] = true;
		memcpy(ax, android_pad.axes, sizeof(ax));
		if (ax[4] > 0.35f || android_pad.trigger_latched[0]) raw_phys[PAD_SLOT_LT] = true;
		if (ax[5] > 0.35f || android_pad.trigger_latched[1]) raw_phys[PAD_SLOT_RT] = true;
	}
	uint32_t bmask = 0;
	for (int b = 0; b < NR_PAD_SLOTS; b++) {
		if (raw_phys[b])
			bmask |= (1u << b);
	}
	live_status_buttons_mask = bmask;
	memcpy(live_status_axes, ax, sizeof(ax));
	pthread_mutex_unlock(&gp_mutex);

	cJSON *root = cJSON_CreateObject();
	cJSON *arr = cJSON_CreateArray();
	for (int i = 0; i < count; i++) {
		cJSON *item = cJSON_CreateObject();
		cJSON_AddNumberToObject(item, "index", i);
		cJSON_AddStringToObject(item, "name", snap_pads[i].name);
		cJSON_AddNumberToObject(item, "vendorId", snap_pads[i].vendor_id);
		cJSON_AddNumberToObject(item, "productId", snap_pads[i].product_id);
		cJSON_AddBoolToObject(item, "isGameController", snap_pads[i].is_game_controller);
		cJSON_AddItemToArray(arr, item);
	}
	cJSON *axes_arr = cJSON_CreateArray();
	for (int i = 0; i < 6; i++)
		cJSON_AddItemToArray(axes_arr, cJSON_CreateNumber(ax[i]));

	cJSON_AddNumberToObject(root, "count", count);
	cJSON_AddNumberToObject(root, "connectedCount", count);
	cJSON_AddItemToObject(root, "controllers", arr);
	cJSON_AddNumberToObject(root, "buttonsMask", (double)bmask);
	cJSON_AddItemToObject(root, "axes", axes_arr);
	cJSON_AddNumberToObject(root, "lx", ax[0]);
	cJSON_AddNumberToObject(root, "ly", ax[1]);
	cJSON_AddNumberToObject(root, "rx", ax[2]);
	cJSON_AddNumberToObject(root, "ry", ax[3]);
	cJSON_AddNumberToObject(root, "lt", ax[4]);
	cJSON_AddNumberToObject(root, "rt", ax[5]);
	char *out = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	return out;
}

#ifdef __ANDROID__
static jstring jni_new_string_utf8_safe(JNIEnv *env, const char *utf8)
{
	if (!env)
		return NULL;
	if (!utf8 || !utf8[0]) {
		jchar empty = 0;
		return (*env)->NewString(env, &empty, 0);
	}
	size_t len = strlen(utf8);
	jchar stack_buf[256];
	jchar *u16 = (len <= 256) ? stack_buf : (jchar *)malloc(len * sizeof(jchar));
	if (!u16) {
		jchar empty = 0;
		return (*env)->NewString(env, &empty, 0);
	}
	size_t out = 0;
	const unsigned char *s = (const unsigned char *)utf8;
	size_t i = 0;
	while (i < len) {
		unsigned char b0 = s[i];
		if (b0 < 0x80u) {
			u16[out++] = (jchar)b0;
			i++;
		} else if (b0 >= 0xC2u && b0 <= 0xDFu) {
			if (i + 1 < len && (s[i + 1] & 0xC0u) == 0x80u) {
				uint32_t cp = ((uint32_t)(b0 & 0x1Fu) << 6) | (uint32_t)(s[i + 1] & 0x3Fu);
				u16[out++] = (jchar)cp;
				i += 2;
			} else {
				u16[out++] = (jchar)0xFFFDu;
				i++;
			}
		} else if (b0 >= 0xE0u && b0 <= 0xEFu) {
			bool b1_ok = (i + 1 < len) && (
				(b0 == 0xE0u) ? (s[i + 1] >= 0xA0u && s[i + 1] <= 0xBFu) :
				(b0 == 0xEDu) ? (s[i + 1] >= 0x80u && s[i + 1] <= 0x9Fu) :
				((s[i + 1] & 0xC0u) == 0x80u));
			if (b1_ok && i + 2 < len && (s[i + 2] & 0xC0u) == 0x80u) {
				uint32_t cp = ((uint32_t)(b0 & 0x0Fu) << 12) |
				              ((uint32_t)(s[i + 1] & 0x3Fu) << 6) |
				              (uint32_t)(s[i + 2] & 0x3Fu);
				u16[out++] = (jchar)cp;
				i += 3;
			} else {
				u16[out++] = (jchar)0xFFFDu;
				i += b1_ok ? 2 : 1;
			}
		} else if (b0 >= 0xF0u && b0 <= 0xF4u) {
			bool b1_ok = (i + 1 < len) && (
				(b0 == 0xF0u) ? (s[i + 1] >= 0x90u && s[i + 1] <= 0xBFu) :
				(b0 == 0xF4u) ? (s[i + 1] >= 0x80u && s[i + 1] <= 0x8Fu) :
				((s[i + 1] & 0xC0u) == 0x80u));
			bool b2_ok = b1_ok && (i + 2 < len) && ((s[i + 2] & 0xC0u) == 0x80u);
			if (b2_ok && i + 3 < len && (s[i + 3] & 0xC0u) == 0x80u) {
				uint32_t cp = ((uint32_t)(b0 & 0x07u) << 18) |
				              ((uint32_t)(s[i + 1] & 0x3Fu) << 12) |
				              ((uint32_t)(s[i + 2] & 0x3Fu) << 6) |
				              (uint32_t)(s[i + 3] & 0x3Fu);
				uint32_t v = cp - 0x10000u;
				u16[out++] = (jchar)(0xD800u | (v >> 10));
				u16[out++] = (jchar)(0xDC00u | (v & 0x3FFu));
				i += 4;
			} else {
				u16[out++] = (jchar)0xFFFDu;
				i += b2_ok ? 3 : (b1_ok ? 2 : 1);
			}
		} else {
			u16[out++] = (jchar)0xFFFDu;
			i++;
		}
	}
	jstring ret = (*env)->NewString(env, u16, (jsize)out);
	if (u16 != stack_buf)
		free(u16);
	return ret;
}

JNIEXPORT jint JNICALL
Java_io_github_kichikuou_xsystem4_XSystem4Activity_nativePartsTextInputSession(
	JNIEnv *env, jobject activity)
{
	(void)env;
	(void)activity;
	return (jint)atomic_load(&android_parts_text_session);
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_XSystem4Activity_sendPartsTextInput(
	JNIEnv *env, jobject activity, jint session, jbyteArray text, jboolean composing, jint backspaces)
{
	(void)activity;
	if (!session || (unsigned)session != atomic_load(&android_parts_text_session))
		return;
	jsize size = text ? (*env)->GetArrayLength(env, text) : 0;
	struct android_parts_text *input = xmalloc(sizeof(*input) + (size_t)size + 1);
	input->composing = composing;
	input->backspaces = backspaces;
	if (size)
		(*env)->GetByteArrayRegion(env, text, 0, size, (jbyte *)input->text);
	input->text[size] = '\0';
	SDL_Event event = { .user = { .type = android_parts_text_event, .code = session, .data1 = input } };
	if (SDL_PushEvent(&event) <= 0)
		free(input);
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GamepadBridge_nativeSetGamepadConfigJson(
	JNIEnv *env, jclass clazz, jstring jjson)
{
	(void)clazz;
	if (!jjson)
		return;
	const char *s = (*env)->GetStringUTFChars(env, jjson, NULL);
	if (s) {
		input_set_gamepad_config_json(s);
		(*env)->ReleaseStringUTFChars(env, jjson, s);
	}
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GamepadBridge_nativeSetVirtualStick(
	JNIEnv *env, jclass clazz, jint stick_id, jfloat x, jfloat y)
{
	(void)env;
	(void)clazz;
	input_set_virtual_stick((int)stick_id, (float)x, (float)y);
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GamepadBridge_nativeSetVirtualDPad(
	JNIEnv *env, jclass clazz, jint stick_mode, jboolean left, jboolean right, jboolean up, jboolean down)
{
	(void)env;
	(void)clazz;
	input_set_virtual_dpad((int)stick_mode, left == JNI_TRUE, right == JNI_TRUE,
			up == JNI_TRUE, down == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GamepadBridge_nativeSetVirtualButton(
	JNIEnv *env, jclass clazz, jint slot_id, jint action_type, jint code, jint code2,
	jboolean toggle_mode, jboolean pressed)
{
	(void)env;
	(void)clazz;
	input_set_virtual_button((int)slot_id, (int)action_type, (int)code, (int)code2,
			toggle_mode == JNI_TRUE, pressed == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GamepadBridge_nativeClearVirtualInputs(
	JNIEnv *env, jclass clazz)
{
	(void)env;
	(void)clazz;
	input_clear_virtual_inputs();
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GamepadBridge_nativeSetAndroidControllerKey(
	JNIEnv *env, jclass clazz, jint device_id, jint keycode, jboolean pressed)
{
	(void)env;
	(void)clazz;
	input_set_android_pad_key((int)device_id, (int)keycode, pressed == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GamepadBridge_nativeSetAndroidControllerMotion(
	JNIEnv *env, jclass clazz, jint device_id, jfloat lx, jfloat ly, jfloat rx, jfloat ry,
	jfloat lt, jfloat rt, jfloat hat_x, jfloat hat_y)
{
	(void)env;
	(void)clazz;
	input_set_android_pad_motion((int)device_id, (float)lx, (float)ly, (float)rx, (float)ry,
			(float)lt, (float)rt, (float)hat_x, (float)hat_y);
}

JNIEXPORT jstring JNICALL
Java_io_github_kichikuou_xsystem4_GamepadBridge_nativeGetControllerStatusJson(
	JNIEnv *env, jclass clazz)
{
	(void)clazz;
	char *json = input_get_controller_status_json();
	jstring ret = jni_new_string_utf8_safe(env, json ? json : "{\"count\":0,\"connectedCount\":0,\"controllers\":[],\"axes\":[0,0,0,0,0,0]}");
	free(json);
	return ret;
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_GamepadBridge_nativeResetGamepadCursor(
	JNIEnv *env, jclass clazz, jboolean visible)
{
	(void)env;
	(void)clazz;
	input_reset_gamepad_cursor(visible == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_io_github_kichikuou_xsystem4_XSystem4Activity_nativeRequestRedraw(
	JNIEnv *env, jobject thiz)
{
	(void)env;
	(void)thiz;
	atomic_store(&android_redraw_requested, true);
}
#endif /* __ANDROID__ */
