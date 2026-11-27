#!/usr/bin/env python3
"""Exercise the Parts text-box handlers and SDL text-session ownership."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_parts_render_regressions import ROOT, function


FIXTURE = r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "input.h"

enum { PARTS_MSG_FOCUS_IN, PARTS_MSG_FOCUS_OUT, PARTS_MSG_TEXT_CHANGED, PARTS_MSG_TEXT_FIXED };
struct parts {
    int no, component_type, controller_no, text_box_max_length;
    bool focused, enable_input, text_box_read_only, visible;
    struct string *text_box_text;
};
static struct parts fields[2];
static struct parts *text_box_focus;
static bool text_box_composing, parts_v2 = true, parts_input_enabled = true;
static struct { int active; } ctrl_stack;
static int started, stopped, changed, fixed;
static bool preview;
#define PARTS_LIST_FOREACH(p) for ((p) = fields; (p) < fields + 2; (p)++)
#define PARTS_LIST_FOREACH_REVERSE(p) PARTS_LIST_FOREACH(p)
static struct parts *parts_try_get(int no) { return &fields[no - 1]; }
#define parts_get parts_try_get
static bool parts_is_visible(struct parts *p) { return p->visible; }
static void SDL_StartTextInput(void) { started++; }
static void SDL_StopTextInput(void) { stopped++; }
static void parts_text_box_set_display(struct parts *parts, const char *text) { preview = text && *text; }
static void set_string(struct string **out, struct string *value) {
    struct string *copy = string_dup(value);
    free_string(*out);
    *out = copy;
}
static void parts_msg_push(struct parts *p, int type, const char *fmt, ...) {
    assert(!*fmt);
    changed += type == PARTS_MSG_TEXT_CHANGED;
    fixed += type == PARTS_MSG_TEXT_FIXED;
}
static const struct input_text_handlers *legacy_text_handlers, *parts_text_handlers, *text_handlers;
typedef struct { int code; void *data1; } SDL_UserEvent;
static atomic_uint android_parts_text_session;
struct android_parts_text { bool composing; int backspaces; char text[]; };
'''

SCENARIO = r'''
static void legacy_commit(const char *text) { assert(!strcmp(text, "legacy")); }

static void android_text(int session, const char *text, bool composing, int backspaces)
{
    struct android_parts_text *input = malloc(sizeof(*input) + strlen(text) + 1);
    input->composing = composing;
    input->backspaces = backspaces;
    strcpy(input->text, text);
    SDL_UserEvent event = { .code = session, .data1 = input };
    dispatch_android_parts_text(&event);
}

int main(void)
{
    struct input_text_handlers legacy = { .commit = legacy_commit };
    const char *word = "\xe8\x8a\xb1";
    sys4_set_text_gbk(true);
    for (int i = 0; i < 2; i++) {
        fields[i] = (struct parts) { .no = i + 1, .component_type = 4, .text_box_max_length = 2,
            .enable_input = true, .visible = true, .text_box_text = string_ref(&EMPTY_STRING) };
    }
    PE_SetFocus(1);
    assert(started == 1 && stopped == 0 && text_handlers);
    text_handlers->commit(word);
    assert(changed == 1 && !strcmp(fields[0].text_box_text->text, "\xbb\xa8"));
    text_handlers->edit(word, 0, 1);
    assert(PE_IsTextBoxComposing() && preview && changed == 1);
    text_handlers->key(VK_BACK);
    text_handlers->key(VK_RETURN);
    assert(changed == 1 && fixed == 0);
    text_handlers->commit("ab");
    assert(changed == 2 && !PE_IsTextBoxComposing() && !preview);
    assert(!strcmp(fields[0].text_box_text->text, "\xbb\xa8" "a"));
    text_handlers->key(VK_BACK);
    assert(!strcmp(fields[0].text_box_text->text, "\xbb\xa8"));
    text_handlers->key(VK_BACK);
    assert(fields[0].text_box_text->size == 0 && changed == 4);
    text_handlers->key(VK_RETURN);
    assert(fixed == 1);

    atomic_store(&android_parts_text_session, 2);
    android_text(2, "A", false, 0);
    android_text(2, "kana", true, 0);
    assert(!strcmp(fields[0].text_box_text->text, "A") && changed == 5 && preview);
    android_text(1, "B", false, 0);
    android_text(2, "\xe4\xbb\xae\xe5\x90\x8d", false, 0);
    assert(!strcmp(fields[0].text_box_text->text, "A\x81\xa2") && changed == 6 && !preview);
    android_text(2, "", false, 1);
    assert(!strcmp(fields[0].text_box_text->text, "A") && changed == 7);
    android_text(2, "", false, 1);
    assert(fields[0].text_box_text->size == 0 && changed == 8);

    input_set_legacy_text_handlers(&legacy);
    assert(text_handlers == &legacy);
    text_handlers->commit("legacy");
    input_set_legacy_text_handlers(NULL);
    assert(text_handlers == parts_text_handlers);

    PE_SetFocus(2);
    text_handlers->commit("X");
    assert(!strcmp(fields[1].text_box_text->text, "X") && fields[0].text_box_text->size == 0);
    fields[1].visible = false;
    text_box_update_focus();
    assert(!text_handlers && started == stopped);
    fields[1].visible = true;
    fields[1].text_box_read_only = true;
    text_box_update_focus();
    assert(!text_handlers && started == stopped);
    for (int i = 0; i < 2; i++)
        free_string(fields[i].text_box_text);
}
'''


class PartsTextBoxTests(unittest.TestCase):
    def test_focused_text_box_uses_sdl_and_keeps_legacy_input_separate(self):
        functions = {
            'src/input.c': ['dispatch_android_parts_text', 'update_text_handlers',
                            'input_set_legacy_text_handlers', 'input_set_parts_text_handlers'],
            'src/parts/widgets.c': ['PE_SetTextBoxText', 'PE_GetTextBoxText'],
            'src/parts/input.c': [
                'text_box_can_edit', 'text_box_commit', 'text_box_edit', 'text_box_key',
                'PE_IsTextBoxComposing', 'text_box_set_focus', 'text_box_update_focus',
                'parts_set_focus', 'parts_clear_focus', 'PE_SetFocus',
            ],
        }
        bodies = '\n'.join(function(path, name) for path, names in functions.items() for name in names)
        lib = ROOT / 'subprojects/libsys4'
        with tempfile.TemporaryDirectory() as tempdir:
            src = Path(tempdir, 'test.c')
            src.write_text(FIXTURE + bodies + SCENARIO)
            binary = src.with_suffix('')
            subprocess.run([
                'cc', '-std=gnu11', '-I' + str(ROOT / 'include'), '-I' + str(lib / 'include'),
                str(src), *(str(lib / 'src' / f'{name}.c') for name in ('string', 'utfsjis', 'system', 'file')),
                '-lm', '-o', str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
