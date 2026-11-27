#!/usr/bin/env python3
"""Native C regression test for loading resume saves with VM stack sizes > 4096 (e.g. Evenicle 4102)."""
import unittest

from test_evenicle2_regressions import LIBSYS4_CORE, ROOT, compile_and_run
from test_vm_reign_regressions import extract_function


class ResumeVmStackRegressionTest(unittest.TestCase):
    def test_evenicle_rance_resume_bonus_skill_strings(self):
        names = [
            "static void repair_evenicle_rance_bonus_skill_string",
            "static void repair_evenicle_rance_resume_skills",
            "void vm_load_image",
        ]
        functions = "\n".join(extract_function("src/resume.c", name) for name in names)
        harness = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/ain.h"
#include "system4/savefile.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

struct ain *ain;
bool game_evenicle_rance_cn, dg_callbegin_pending;
static struct vm_pointer slots[10];
struct vm_pointer *heap = slots;
size_t heap_size = sizeof(slots) / sizeof(slots[0]);
static enum savefile_error load_result;
static int json_loads;

bool page_index_valid(int slot) {
    return slot >= 0 && slot < heap_size && heap[slot].ref > 0 && heap[slot].type == VM_PAGE;
}

bool string_index_valid(int slot) {
    return slot >= 0 && slot < heap_size && heap[slot].ref > 0 && heap[slot].type == VM_STRING;
}

void heap_string_assign(int slot, struct string *s) {
    assert(string_index_valid(slot));
    free_string(heap[slot].s);
    heap[slot].s = string_ref(s);
}

static enum savefile_error load_rsave_image(const char *key, const char *path) {
    return load_result;
}

static void load_json_image(const char *key, const char *path) { json_loads++; }
static void modifier_on_config_loaded(void) {}
static void modifier_on_globals_loaded(void) {}
#undef VM_ERROR
#define VM_ERROR(...) abort()
""" + functions + r"""

static void put_string(int slot, const char *utf8, bool gbk) {
    char *bytes = utf2cjk(utf8, 0, gbk);
    slots[slot] = (struct vm_pointer){ .ref = 1, .type = VM_STRING,
        .s = make_string(bytes, strlen(bytes)) };
    free(bytes);
}

static void expect_string(int slot, const char *utf8, bool gbk) {
    char *bytes = utf2cjk(utf8, 0, gbk);
    assert(!strcmp(heap[slot].s->text, bytes));
    free(bytes);
}

int main(void) {
    const char *name = "ランスアタック";
    const char *short_desc = "5.0倍 全体 必殺技";
    const char *long_desc = "ランスの発明した、すごい物理攻撃技\n敵全体に５倍の大攻撃";
    ain = ain_new(6, 0);
    int index = ain_add_struct(ain, "S_SkillParam");
    struct ain_variable members[35] = {0};
    members[2].type.data = AIN_INT;
    members[3].type.data = members[19].type.data = members[20].type.data = members[34].type.data = AIN_STRING;
    ain->structures[index].nr_members = 35;
    ain->structures[index].members = members;
    struct page *skill = calloc(1, sizeof(*skill) + 35 * sizeof(skill->values[0]));
    assert(skill);
    skill->type = STRUCT_PAGE;
    skill->index = index;
    skill->nr_vars = 35;
    skill->values[2].i = 116;
    skill->values[3].i = 1;
    skill->values[19].i = 2;
    skill->values[20].i = 3;
    skill->values[22].i = 4;
    skill->values[34].i = 5;
    slots[0] = (struct vm_pointer){ .ref = 1, .type = VM_PAGE, .page = skill };
    put_string(1, name, false);
    put_string(2, short_desc, false);
    put_string(3, long_desc, false);
    put_string(4, name, false);
    put_string(5, "兰斯攻击", true);

    load_result = SAVEFILE_SUCCESS;
    vm_load_image("", "");
    expect_string(1, name, false);
    sys4_set_text_gbk(true);
    vm_load_image("", "");
    expect_string(1, name, false);
    game_evenicle_rance_cn = true;
    skill->values[2].i = 101;
    vm_load_image("", "");
    expect_string(1, name, false);
    skill->values[2].i = 116;
    vm_load_image("", "");
    expect_string(1, name, true);
    expect_string(2, short_desc, true);
    expect_string(3, long_desc, true);
    expect_string(4, name, false);
    expect_string(5, "兰斯攻击", true);

    load_result = SAVEFILE_INVALID_SIGNATURE;
    put_string(6, "捕獲", false);
    skill->values[2].i = 128;
    skill->values[34].i = 6;
    vm_load_image("", "");
    assert(json_loads == 1);
    expect_string(6, "捕獲", true);
    for (int i = 1; i <= 6; i++)
        free_string(slots[i].s);
    free(skill);
    ain->structures[index].nr_members = 0;
    ain->structures[index].members = NULL;
    ain_free(ain);
    puts("OK");
    return 0;
}
"""
        out = compile_and_run(harness, LIBSYS4_CORE, ["-lz", "-lm"])
        self.assertEqual(out.strip(), "OK")

    def test_load_rsave_and_json_stack_dynamic_growth_and_bounds(self):
        fn_ensure = extract_function("src/vm.c", "void vm_stack_ensure")
        fn_load_json = extract_function("src/resume.c", "static void load_json_stack")
        fn_load_rsave = extract_function("src/resume.c", "static void load_rsave_stack")

        harness = f"""
#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "system4.h"
#include "system4/savefile.h"
#include "vm.h"

#define INITIAL_STACK_SIZE 65536
#define MAX_RESUME_VM_STACK_SIZE (1024 * 1024)

static jmp_buf err_jmp;
static int err_count = 0;
#undef VM_ERROR
#define VM_ERROR(...) do {{ err_count++; longjmp(err_jmp, 1); }} while (0)
#define invalid_save_data(...) do {{ err_count++; longjmp(err_jmp, 1); }} while (0)

static cJSON *type_check(int type, cJSON *item) {{
    if (!item || (item->type & 0xff) != type)
        invalid_save_data("bad json type");
    return item;
}}

union vm_value *stack = NULL;
int32_t stack_ptr = 0;
static size_t stack_size = 0;

{fn_ensure}
{fn_load_json}
{fn_load_rsave}

int main(void) {{
    // 1. Evenicle crash reproduction: rsave with stack_size = 4102 (> legacy 4096 cap).
    int32_t *buf1 = xcalloc(4102, sizeof(int32_t));
    for (int i = 0; i < 4102; i++) buf1[i] = i * 3 - 7;
    struct rsave rs = {{ .stack_size = 4102, .stack = buf1 }};
    load_rsave_stack(&rs);
    assert(stack_ptr == 4102);
    assert(stack_size == INITIAL_STACK_SIZE);
    assert(stack[0].i == -7 && stack[4095].i == 4095 * 3 - 7 && stack[4101].i == 4101 * 3 - 7);
    free(buf1);

    // 2. Dynamic growth beyond INITIAL_STACK_SIZE (65536 -> 131072).
    int32_t *buf2 = xcalloc(70000, sizeof(int32_t));
    for (int i = 0; i < 70000; i++) buf2[i] = i ^ 0x5a5a;
    rs.stack_size = 70000;
    rs.stack = buf2;
    load_rsave_stack(&rs);
    assert(stack_ptr == 70000);
    assert(stack_size >= 131072);
    assert(stack[0].i == 0x5a5a && stack[69999].i == (69999 ^ 0x5a5a));
    free(buf2);

    // 3. JSON resume stack with 4102 items also loads cleanly.
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < 4102; i++)
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(i + 100));
    load_json_stack(arr);
    assert(stack_ptr == 4102);
    assert(stack[0].i == 100 && stack[4101].i == 4201);
    cJSON_Delete(arr);

    // 4. Reject negative, missing-buffer, and oversized stack_size cleanly.
    int before = err_count;
    rs.stack_size = -1; rs.stack = NULL;
    if (setjmp(err_jmp) == 0) {{ load_rsave_stack(&rs); assert(0); }}
    assert(err_count == before + 1);

    rs.stack_size = 10; rs.stack = NULL;
    if (setjmp(err_jmp) == 0) {{ load_rsave_stack(&rs); assert(0); }}
    assert(err_count == before + 2);

    rs.stack_size = MAX_RESUME_VM_STACK_SIZE + 1; rs.stack = NULL;
    if (setjmp(err_jmp) == 0) {{ load_rsave_stack(&rs); assert(0); }}
    assert(err_count == before + 3);

    free(stack);
    puts("OK");
    return 0;
}}
"""
        out = compile_and_run(
            harness,
            [*LIBSYS4_CORE, ROOT / "src/cJSON.c"],
            ["-lz", "-lm"],
        )
        self.assertEqual(out.strip(), "OK")


if __name__ == "__main__":
    unittest.main()
