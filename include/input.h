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

#ifndef SYSTEM4_INPUT_H
#define SYSTEM4_INPUT_H

#include <stdbool.h>
#include <stdint.h>

enum sact_keycode {
	VK_LBUTTON   = 0x01, // マウスの左ボタン
	VK_RBUTTON   = 0x02, // マウスの右ボタン
	VK_MBUTTON   = 0x04, // マウスの中央ボタンもしくはホイールクリック
	VK_BACK      = 0x08, // Backspace
	VK_TAB       = 0x09, // Tab
	VK_CLEAR     = 0x0c, // Clear(不明)
	VK_RETURN    = 0x0d, // Enter
	VK_SHIFT     = 0x10, // Shift
	VK_CONTROL   = 0x11, // Ctrl
	VK_MENU      = 0x12, // Alt([Alt]+[何か]の形でシステムが使用するので、通常は使用不可)
	VK_PAUSE     = 0x13, // Pause(通常は使用不可)
	VK_CAPITAL   = 0x14, // CapsLock(通常は使用不可)
	VK_KANA      = 0x15, // 英数カナ(不明)
	VK_KANJI     = 0x19, // (不明)
	VK_ESCAPE    = 0x1b, // Esc
	VK_CONVERT   = 0x1c, // (不明)
	VK_NOCONVERT = 0x1d, // (不明)
	VK_SPACE     = 0x20, // space
	VK_PRIOR     = 0x21, // PageUp
	VK_NEXT      = 0x22, // PageDown
	VK_END       = 0x23, // End
	VK_HOME      = 0x24, // Home
	VK_LEFT      = 0x25, // ←
	VK_UP        = 0x26, // ↑
	VK_RIGHT     = 0x27, // →
	VK_DOWN      = 0x28, // ↓
	VK_SELECT    = 0x29, // Select(不明)
	VK_EXECUTE   = 0x2B, // Execute(不明)
	VK_SNAPSHOT  = 0x2C, // PrintScrn(使用不可)
	VK_INSERT    = 0x2D, // Insert,Ins
	VK_DELETE    = 0x2E, // Delete,Del
	VK_HELP      = 0x2F, // Help(不明)
	VK_0         = 0x30, // テンキーはVK_NUMPAD0
	VK_1         = 0x31, // テンキーはVK_NUMPAD1
	VK_2         = 0x32, // テンキーはVK_NUMPAD2
	VK_3         = 0x33, // テンキーはVK_NUMPAD3
	VK_4         = 0x34, // テンキーはVK_NUMPAD4
	VK_5         = 0x35, // テンキーはVK_NUMPAD5
	VK_6         = 0x36, // テンキーはVK_NUMPAD6
	VK_7         = 0x37, // テンキーはVK_NUMPAD7
	VK_8         = 0x38, // テンキーはVK_NUMPAD8
	VK_9         = 0x39, // テンキーはVK_NUMPAD9
	VK_A         = 0x41, // 'A'で代用可
	VK_B         = 0x42, // 'B'で代用可
	VK_C         = 0x43, // 'C'で代用可
	VK_D         = 0x44, // 'D'で代用可
	VK_E         = 0x45, // 'E'で代用可
	VK_F         = 0x46, // 'F'で代用可
	VK_G         = 0x47, // 'G'で代用可
	VK_H         = 0x48, // 'H'で代用可
	VK_I         = 0x49, // 'I'で代用可
	VK_J         = 0x4a, // 'J'で代用可
	VK_K         = 0x4b, // 'K'で代用可
	VK_L         = 0x4c, // 'L'で代用可
	VK_M         = 0x4d, // 'M'で代用可
	VK_N         = 0x4e, // 'N'で代用可
	VK_O         = 0x4f, // 'O'で代用可
	VK_P         = 0x50, // 'P'で代用可
	VK_Q         = 0x51, // 'Q'で代用可
	VK_R         = 0x52, // 'R'で代用可
	VK_S         = 0x53, // 'S'で代用可
	VK_T         = 0x54, // 'T'で代用可
	VK_U         = 0x55, // 'U'で代用可
	VK_V         = 0x56, // 'V'で代用可
	VK_W         = 0x57, // 'W'で代用可
	VK_X         = 0x58, // 'X'で代用可
	VK_Y         = 0x59, // 'Y'で代用可
	VK_Z         = 0x5a, // 'Z'で代用可
	VK_NUMPAD0   = 0x60, // テンキーの0
	VK_NUMPAD1   = 0x61, // テンキーの1
	VK_NUMPAD2   = 0x62, // テンキーの2
	VK_NUMPAD3   = 0x63, // テンキーの3
	VK_NUMPAD4   = 0x64, // テンキーの4
	VK_NUMPAD5   = 0x65, // テンキーの5
	VK_NUMPAD6   = 0x66, // テンキーの6
	VK_NUMPAD7   = 0x67, // テンキーの7
	VK_NUMPAD8   = 0x68, // テンキーの8
	VK_NUMPAD9   = 0x69, // テンキーの9
	VK_MULTIPLY  = 0x6a, // テンキーの*
	VK_ADD       = 0x6b, // テンキーの+
	VK_SEPARATOR = 0x6c, // Separator(不明)
	VK_SUBTRACT  = 0x6d, // テンキーの-
	VK_DECIMAL   = 0x6e, // テンキーの.
	VK_DIVIDE    = 0x6f, // テンキーの/
	VK_F1        = 0x70, // F1
	VK_F2        = 0x71, // F2
	VK_F3        = 0x72, // F3
	VK_F4        = 0x73, // F4
	VK_F5        = 0x74, // F5
	VK_F6        = 0x75, // F6
	VK_F7        = 0x76, // F7
	VK_F8        = 0x77, // F8
	VK_F9        = 0x78, // F9
	VK_F10       = 0x79, // F10(使用不可)
	VK_F11       = 0x7A, // F11
	VK_F12       = 0x7B, // F12
	VK_F13       = 0x7C, // F13
	VK_F14       = 0x7D, // F14
	VK_F15       = 0x7E, // F15
	VK_F16       = 0x7F, // F16
	VK_F17       = 0x80, // F17
	VK_F18       = 0x81, // F18
	VK_F19       = 0x82, // F19
	VK_F20       = 0x83, // F20
	VK_F21       = 0x84, // F21
	VK_F22       = 0x85, // F22
	VK_F23       = 0x86, // F23
	VK_F24       = 0x87, // F24
	VK_NUMLOCK   = 0x90, // NumLock(通常は使用不可)
	VK_SCROLL    = 0x91, // ScrollLock(通常は使用不可)
	VK_NR_KEYCODES
};

extern bool mouse_focus;
extern bool keyboard_focus;

void handle_window_events(void);
void handle_events(void);
bool key_is_down(enum sact_keycode code);
void key_clear_flag(bool no_ctrl);
bool joy_key_is_down(uint8_t code);
void joy_clear_flag(void);
bool joy_get_stick_status(int controller, int type, float *deg, float *pow);
void mouse_get_pos(int *x, int *y);
void mouse_set_pos(int x, int y);
void mouse_get_wheel(int *forward, int *back);
void mouse_clear_wheel(void);
bool mouse_show_cursor(bool show);
void register_input_handler(void(*handler)(const char*));
void clear_input_handler(void);
void register_editing_handler(void(*handler)(const char*, int, int));
void clear_editing_handler(void);

#endif /* SYSTEM4_INPUT_H */
