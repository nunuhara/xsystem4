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

#include <stdbool.h>
#include <time.h>
#include <SDL.h>
#include "system4.h"
#include "queue.h"
#include "gfx/gfx.h"
#include "gfx/private.h"
#include "input.h"
#include "scene.h"
#include "vm.h"
#include "debugger.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

bool key_state[VK_NR_KEYCODES];
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

bool mouse_focus = true;
bool keyboard_focus = true;

#define MAX_CONTROLLERS 4
static SDL_GameController *controllers[MAX_CONTROLLERS];

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

// Stores a mouse button event synthesized from a touch event (valid if
// .timestamp != 0). We defer such events to prevent games from handling
// button down events before reading the pointer position.
static SDL_MouseButtonEvent deferred_synthetic_mouse_event;
#define SYNTHETIC_MOUSE_EVENT_DELAY 50

static uint32_t long_touch_start_timestamp;
static SDL_FRect long_touch_finger_rect;
#define LONG_TOUCH_DURATION 1000

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
	return key_state[code];
}

void key_clear_flag(bool no_ctrl)
{
	for (int i = 0; i < VK_NR_KEYCODES; i++) {
		if (no_ctrl && i == VK_CONTROL)
			continue;
		key_state[i] = false;
	}
}

void mouse_get_pos(int *x, int *y)
{
	int wx, wy;
	SDL_PumpEvents();
	SDL_GetMouseState(&wx, &wy);
	*x = (wx - sdl.viewport.x) * sdl.w / sdl.viewport.w;
	*y = (wy - sdl.viewport.y) * sdl.h / sdl.viewport.h;
}

void mouse_set_pos(int x, int y)
{
	int wx = x * sdl.viewport.w / sdl.w + sdl.viewport.x;
	int wy = y * sdl.viewport.h / sdl.h + sdl.viewport.y;
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

static void key_event(SDL_KeyboardEvent *e, bool pressed)
{
	if (e->keysym.scancode >= (sizeof(sdl_keytable)/sizeof(*sdl_keytable)))
		return;
	enum sact_keycode code = sdl_keytable[e->keysym.scancode];
	if (code)
		key_state[code] = pressed;
}

static void mouse_event(SDL_MouseButtonEvent *e)
{
	enum sact_keycode code = sdl_to_sact_button(e->button);
	if (code)
		key_state[code] = e->state == SDL_PRESSED;
#ifdef DEBUGGER_ENABLED
	if (e->button == SDL_BUTTON_MIDDLE && e->state == SDL_PRESSED
			&& (dbg_start_in_debugger || dbg_dap))
		dbg_repl(DBG_STOP_PAUSE, NULL);
#endif
}

static void synthetic_mouse_event(SDL_MouseButtonEvent *e)
{
	if (e->state == SDL_PRESSED) {
		// Touch outside the viewport is treated as right-click.
		SDL_Point p = { .x = e->x, .y = e->y };
		if (SDL_PointInRect(&p, &sdl.viewport))
			key_state[VK_LBUTTON] = true;
		else
			key_state[VK_RBUTTON] = true;
	} else {
		key_state[VK_LBUTTON] = false;
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

bool joybutton_state[SDL_CONTROLLER_BUTTON_MAX];

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
	case 5: // Esc
		return joybutton_state[SDL_CONTROLLER_BUTTON_A];
	case 6: // Enter
		return joybutton_state[SDL_CONTROLLER_BUTTON_B];
	case 7: // Space
		return joybutton_state[SDL_CONTROLLER_BUTTON_X];
	case 8: // Ctrl
		return joybutton_state[SDL_CONTROLLER_BUTTON_Y];
	case 9: // a
		return joybutton_state[SDL_CONTROLLER_BUTTON_LEFTSHOULDER];
	case 10: // z
		return joybutton_state[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER];
	case 11: // Page Up
		return joybutton_state[SDL_CONTROLLER_BUTTON_BACK];
	case 12: // Page Down
		return joybutton_state[SDL_CONTROLLER_BUTTON_START];
	case 13:
		return joybutton_state[SDL_CONTROLLER_BUTTON_GUIDE];
	case 14:
		return joybutton_state[SDL_CONTROLLER_BUTTON_LEFTSTICK];
	case 15:
		return joybutton_state[SDL_CONTROLLER_BUTTON_RIGHTSTICK];
	case 16:
		return joybutton_state[SDL_CONTROLLER_BUTTON_DPAD_LEFT];
	case 17:
		return joybutton_state[SDL_CONTROLLER_BUTTON_DPAD_RIGHT];
	case 18:
		return joybutton_state[SDL_CONTROLLER_BUTTON_DPAD_UP];
	case 19:
		return joybutton_state[SDL_CONTROLLER_BUTTON_DPAD_DOWN];
	default:
		return false;
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

bool joy_get_stick_status(int controller, int type, float *deg, float *pow)
{
	SDL_GameController *ctrl = controllers[controller];
	if (!ctrl)
		goto invalid;
	if (type == 0) {
		int x = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTX);
		int y = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTY);
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
		int left = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
		int right = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
		*pow = 1.0f;
		*deg = joy_calc_trigger(left, right);
		return true;
	}
invalid:
	*deg = 0.0f;
	*pow = 0.0f;
	return false;
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
	for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
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

static void controller_axis_event(SDL_ControllerAxisEvent *e)
{
	switch (e->axis) {
	case SDL_CONTROLLER_AXIS_LEFTX:
		joyaxis_update(JOYAXIS_X, e->value);
		break;
	case SDL_CONTROLLER_AXIS_LEFTY:
		joyaxis_update(JOYAXIS_Y, e->value);
		break;
	default:
		break;
	}
}

static void controller_button_event(SDL_ControllerButtonEvent *e)
{
	joybutton_state[e->button] = e->state == SDL_PRESSED;
}

static void(*input_handler)(const char*);
static void(*editing_handler)(const char*, int, int);

void register_input_handler(void(*handler)(const char*))
{
	input_handler = handler;
}

void clear_input_handler(void)
{
	input_handler = NULL;
}

void register_editing_handler(void(*handler)(const char*, int, int))
{
	editing_handler = handler;
}

void clear_editing_handler(void)
{
	editing_handler = NULL;
}

static void fire_deferred_events(void)
{
	uint32_t now = SDL_GetTicks();

	// Flush the deferred mouse button event if it's older than 50ms.
	if (deferred_synthetic_mouse_event.timestamp &&
		deferred_synthetic_mouse_event.timestamp + SYNTHETIC_MOUSE_EVENT_DELAY < now) {
		synthetic_mouse_event(&deferred_synthetic_mouse_event);
		deferred_synthetic_mouse_event.timestamp = 0;
	}
	// Long touch emulates pressing the Ctrl key.
	if (long_touch_start_timestamp && long_touch_start_timestamp + LONG_TOUCH_DURATION < now) {
		key_state[VK_LBUTTON] = false;
		key_state[VK_CONTROL] = true;
	}

#ifdef __ANDROID__
	// Fire the deferred keyboard and text input events.
	while (!STAILQ_EMPTY(&deferred_keyevent_queue) && now >= last_keyevent_timestamp + DEFERRED_KEY_DELAY) {
		struct deferred_keyevent *ev = STAILQ_FIRST(&deferred_keyevent_queue);
		switch (ev->e.type) {
		case SDL_TEXTINPUT:
			if (input_handler)
				input_handler(ev->e.text.text);
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			last_keyevent_timestamp = now;
			key_event(&ev->e.key, ev->e.type == SDL_KEYDOWN);
			break;
		}
		STAILQ_REMOVE_HEAD(&deferred_keyevent_queue, entry);
		free(ev);
	}
#endif
}

static void handle_window_event(SDL_Event *e)
{
	switch (e->window.event) {
	case SDL_WINDOWEVENT_EXPOSED:
		gfx_swap();
		break;
	case SDL_WINDOWEVENT_ENTER:
		mouse_focus = true;
		break;
	case SDL_WINDOWEVENT_LEAVE:
		mouse_focus = false;
		break;
	case SDL_WINDOWEVENT_FOCUS_GAINED:
		keyboard_focus = true;
		break;
	case SDL_WINDOWEVENT_FOCUS_LOST:
		keyboard_focus = false;
		break;
	case SDL_WINDOWEVENT_SIZE_CHANGED:
		gfx_update_screen_scale();
		gfx_swap();
		break;
	}
}

void handle_window_events(void)
{
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_QUIT:
			vm_exit(0);
			break;
		case SDL_WINDOWEVENT:
			handle_window_event(&e);
			break;
		}
	}
}

void handle_events(void)
{
	fire_deferred_events();

	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_QUIT:
			vm_exit(0);
			break;
		case SDL_WINDOWEVENT:
			handle_window_event(&e);
			break;
		case SDL_KEYDOWN:
			if (e.key.keysym.scancode == SDL_SCANCODE_F9) {
				vm_stack_trace();
			} else if (e.key.keysym.scancode == SDL_SCANCODE_F11) {
				uint32_t flag = SDL_WINDOW_FULLSCREEN_DESKTOP;
				bool fs = SDL_GetWindowFlags(sdl.window) & flag;
				SDL_SetWindowFullscreen(sdl.window, fs ? 0 : flag);
			} else if (e.key.keysym.scancode == SDL_SCANCODE_S) {
				if (e.key.keysym.mod & (KMOD_LALT | KMOD_RALT)) {
					screenshot_save();
				}
			}
			// fallthrough
		case SDL_KEYUP:
#ifdef __ANDROID__
			if (input_handler && e.key.timestamp < last_keyevent_timestamp + DEFERRED_KEY_DELAY) {
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
		case SDL_MOUSEBUTTONUP:
		case SDL_MOUSEBUTTONDOWN:
			if (e.button.which == SDL_TOUCH_MOUSEID) {
				if (deferred_synthetic_mouse_event.timestamp)
					synthetic_mouse_event(&deferred_synthetic_mouse_event);
				deferred_synthetic_mouse_event = e.button;
			} else {
				mouse_event(&e.button);
			}
			break;
		case SDL_MOUSEWHEEL:
			wheel_dir = e.wheel.y;
			break;
		case SDL_FINGERDOWN:
			if (SDL_GetNumTouchFingers(e.tfinger.touchId) >= 2) {
				// The user is about to start a multi-touch gesture.
				key_state[VK_LBUTTON] = false;
				key_state[VK_RBUTTON] = false;
				deferred_synthetic_mouse_event.timestamp = 0;
				long_touch_start_timestamp = 0;
				scroll_gesture_y = -1.0f;
				break;
			}
			long_touch_start_timestamp = e.tfinger.timestamp;
			// Movement within this rect (1% of the screen size from the touch
			// start position) will be ignored.
			long_touch_finger_rect = (SDL_FRect) {
				.x = e.tfinger.x - 0.01,
				.y = e.tfinger.y - 0.01,
				.w = 0.02,
				.h = 0.02
			};
			break;
		case SDL_FINGERMOTION:
			if (SDL_PointInFRect(&(SDL_FPoint){ e.tfinger.x, e.tfinger.y }, &long_touch_finger_rect))
				break;
			// Cancel the timer only if Ctrl emulation has not already started.
			if (!key_state[VK_CONTROL])
				long_touch_start_timestamp = 0;
			break;
		case SDL_FINGERUP:
			long_touch_start_timestamp = 0;
			key_state[VK_CONTROL] = false;
			break;
		case SDL_MULTIGESTURE:
			if (e.mgesture.numFingers == 2) {
				if (scroll_gesture_y < 0.0f)
					scroll_gesture_y = e.mgesture.y;
				float dy = scroll_gesture_y - e.mgesture.y;
				if (dy * dy > SCROLL_GESTURE_SENSITIVITY * SCROLL_GESTURE_SENSITIVITY) {
					wheel_dir = dy < 0 ? 1 : -1;  // Swipe up to scroll down.
					scroll_gesture_y = e.mgesture.y;
				}
			}
		case SDL_CONTROLLERDEVICEADDED:
			if (e.cdevice.which < MAX_CONTROLLERS)
				controllers[e.cdevice.which] = SDL_GameControllerOpen(e.cdevice.which);
			break;
		case SDL_CONTROLLERAXISMOTION:
			controller_axis_event(&e.caxis);
			break;
		case SDL_CONTROLLERBUTTONUP:
		case SDL_CONTROLLERBUTTONDOWN:
			controller_button_event(&e.cbutton);
			break;
		case SDL_TEXTINPUT:
			if (!input_handler) {
				break;
#ifdef __ANDROID__
			} else if (!STAILQ_EMPTY(&deferred_keyevent_queue)) {
				// The order of key events and text events must be preserved.
				struct deferred_keyevent *ev = xmalloc(sizeof(struct deferred_keyevent));
				ev->e = e;
				STAILQ_INSERT_TAIL(&deferred_keyevent_queue, ev, entry);
#endif
			} else {
				input_handler(e.text.text);
			}
			break;
		case SDL_TEXTEDITING:
			if (editing_handler)
				editing_handler(e.edit.text, e.edit.start, e.edit.length);
			break;
		default:
			break;
		}
	}
	if (dbg_dap)
		dbg_dap_handle_messages();
}

