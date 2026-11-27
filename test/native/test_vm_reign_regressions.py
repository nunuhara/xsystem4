#!/usr/bin/env python3
"""Dependency-free native regression tests for VM and ReignEngine fixes.

Compiles the actual C implementations from xsystem4/src/vm.c and
xsystem4/src/3d/reign.c against minimal fixtures to verify:
1. struct_page_slot() / get_struct_page() inherit enclosing `this` across
   non-lambda CALLFUNC frames (@2 member initializers & intra-class helpers)
   while stopping at explicit has_struct_page barriers (null-this CALLMETHOD,
   free-function delegates, scenario_call, vm_call(..., -1)).
2. AIN v14 X_A_INIT does not prematurely heap_unref live array slots when the
   compiler reuses a scratch carrier variable across bulk X_ASSIGN sequences.
3. RE_set_projection() returns true (with a finite identity matrix) when
   height == 0.0f instead of failing TapirEngine.SetProjection in Evenicle.

Run: python3 xsystem4/test/native/test_vm_reign_regressions.py
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LIBSYS4_HACKS_SRCS = [
    ROOT / "src/hacks.c",
    ROOT / "src/escalayer_reboot_cn_ui.c",
    ROOT / "subprojects/libsys4/src/ain.c",
    ROOT / "subprojects/libsys4/src/buffer.c",
    ROOT / "subprojects/libsys4/src/file.c",
    ROOT / "subprojects/libsys4/src/hashtable.c",
    ROOT / "subprojects/libsys4/src/instructions.c",
    ROOT / "subprojects/libsys4/src/mt19937int.c",
    ROOT / "subprojects/libsys4/src/string.c",
    ROOT / "subprojects/libsys4/src/system.c",
    ROOT / "subprojects/libsys4/src/utfsjis.c",
    ROOT / "subprojects/libsys4/src/zlib.c",
]


def compile_and_run_with_sources(c_source: str, extra_sources: list[Path], extra_libs: list[str], args: list[str] | None = None) -> str:
    sdl_cflags = subprocess.check_output(["pkg-config", "--cflags", "sdl2"], text=True).split()
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "test.c"
        exe = Path(tmp) / "test_bin"
        src.write_text(c_source, encoding="utf-8")
        subprocess.run(
            [
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror=implicit-function-declaration",
                "-Wno-unused-parameter", "-Wno-sign-compare", "-Wno-type-limits", "-O2",
                "-D_DEFAULT_SOURCE", "-DUSE_GLES",
                f"-I{ROOT / 'include'}",
                f"-I{ROOT / 'src'}",
                f"-I{ROOT / 'subprojects/libsys4/include'}",
                *sdl_cflags,
                str(src),
                *[str(p) for p in extra_sources],
                *extra_libs,
                "-o", str(exe),
            ],
            check=True,
        )
        res = subprocess.run([str(exe), *(args or [])], check=True, capture_output=True, text=True)
        return res.stdout


def extract_function(rel_path: str, name: str) -> str:
    source = (ROOT / rel_path).read_text(encoding="utf-8")
    idx = source.index(name + "(")
    start = source.rfind("\n", 0, idx) + 1
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def extract_case_block(rel_path: str, case_label: str) -> str:
    source = (ROOT / rel_path).read_text(encoding="utf-8")
    idx = source.index(f"case {case_label}:")
    brace = source.index("{", idx)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[idx:end]


def compile_and_run(c_source: str) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "test.c"
        exe = Path(tmp) / "test_bin"
        src.write_text(c_source, encoding="utf-8")
        subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", str(src), "-lm", "-o", str(exe)],
            check=True,
        )
        subprocess.run([str(exe)], check=True)


class VmAndReignRegressionTests(unittest.TestCase):
    def test_zero_vector_normalization_and_set_length(self) -> None:
        guard_fn = extract_function("src/vm.c", "vm_zero_vector_div_guard")
        fdiv_block = extract_case_block("src/vm.c", "F_DIV")
        fdiva_block = extract_case_block("src/vm.c", "F_DIVA")
        compile_and_run(f"""
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define unlikely(x) (x)
enum {{ F_DIV, F_DIVA }};
union vm_value {{ float f; }};
struct ain_function {{ const char *name; }};
struct ain {{ int nr_functions; struct ain_function *functions; }};
struct function_call {{ int fno; }};

static struct ain_function funcs[] = {{
    {{ "Other" }},
    {{ "VECTOR3D@Normalize" }},
    {{ "VECTOR2D@Normalize" }},
    {{ "VECTOR3D@SetLength" }},
    {{ "VECTOR2D@SetLength" }},
    {{ "fxyz_t@Normalize" }},
    {{ "VECTOR2D@Rotate" }},
    {{ "CASVector3D@Normalize" }},
    {{ "CASVector2D@Normalize" }},
    {{ "CASVector3D@SetLength" }},
    {{ "CASVector2D@SetLength" }},
}};
static struct ain program = {{
    .nr_functions = (int)(sizeof(funcs) / sizeof(funcs[0])),
    .functions = funcs,
}};
static struct ain *ain = &program;
static struct function_call call_stack[2];
static int call_stack_ptr;
static union vm_value stack[4];
static int sp;
static union vm_value target_var;
#define stack_pop() stack[--sp]
#define stack_peek(n) stack[sp - 1 - (n)]
#define stack_set(n, value) (stack_peek(n).f = (value))
#define stack_push(value) (stack[sp++].f = (value))
static union vm_value *stack_pop_var(void)
{{
    return &target_var;
}}

{guard_fn}

static float divide(float lhs, float rhs, int fno, int depth)
{{
    call_stack[0].fno = 0;
    call_stack[1].fno = fno;
    call_stack_ptr = depth;
    stack[0].f = lhs;
    stack[1].f = rhs;
    sp = 2;
    switch (F_DIV) {{
        {fdiv_block}
    }}
    assert(sp == 1);
    return stack[0].f;
}}

static float divide_assign(float lhs, float rhs, int fno, int depth)
{{
    call_stack[0].fno = 0;
    call_stack[1].fno = fno;
    call_stack_ptr = depth;
    target_var.f = lhs;
    stack[0].f = rhs;
    sp = 1;
    switch (F_DIVA) {{
        {fdiva_block}
    }}
    assert(sp == 1);
    assert(isnan(stack[0].f) ? isnan(target_var.f) : stack[0].f == target_var.f);
    return target_var.f;
}}

int main(void)
{{
    // VECTOR3D@Normalize and VECTOR2D@Normalize keep 0/0 finite across all games
    assert(divide(0.0f, 0.0f, 1, 2) == 0.0f);
    assert(divide(0.0f, 0.0f, 2, 2) == 0.0f);
    assert(divide(-0.0f, 0.0f, 1, 2) == 0.0f);
    assert(divide(3.0f, 5.0f, 1, 2) == 0.6f);
    assert(isinf(divide(1.0f, 0.0f, 1, 2)));

    // SetLength, Rotate, CASVector*, and fxyz_t@Normalize keep 0/0 finite
    assert(divide_assign(0.0f, 0.0f, 3, 2) == 0.0f);
    assert(divide_assign(0.0f, 0.0f, 4, 2) == 0.0f);
    assert(divide_assign(0.0f, 0.0f, 5, 2) == 0.0f);
    assert(divide_assign(0.0f, 0.0f, 6, 2) == 0.0f);
    assert(divide(0.0f, 0.0f, 6, 2) == 0.0f);
    assert(divide_assign(3.0f, 5.0f, 6, 2) == 0.6f);
    assert(isinf(divide_assign(1.0f, 0.0f, 6, 2)));
    for (int fno = 7; fno <= 10; fno++) {{
        assert(divide(0.0f, 0.0f, fno, 2) == 0.0f);
        assert(divide_assign(0.0f, 0.0f, fno, 2) == 0.0f);
        assert(divide(3.0f, 5.0f, fno, 2) == 0.6f);
        assert(isinf(divide(1.0f, 0.0f, fno, 2)));
    }}
    assert(divide_assign(3.0f, 5.0f, 3, 2) == 0.6f);
    assert(isinf(divide_assign(1.0f, 0.0f, 3, 2)));

    // Unrelated functions, empty call stack, and out-of-range fno remain IEEE NaN
    assert(isnan(divide(0.0f, 0.0f, 0, 2)));
    assert(isnan(divide(0.0f, 0.0f, 1, 1)));
    assert(isnan(divide(0.0f, 0.0f, 1, 0)));
    assert(isnan(divide(0.0f, 0.0f, 99, 2)));
    assert(isnan(divide_assign(0.0f, 0.0f, 0, 2)));
    assert(isnan(divide_assign(0.0f, 0.0f, 3, 0)));
}}
""")

    def test_struct_page_slot_and_get_struct_page(self) -> None:
        fn_slot = extract_function("src/vm.c", "struct_page_slot")
        fn_get = extract_function("src/vm.c", "get_struct_page")
        c_code = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define unlikely(x) (x)

struct page {{
    int dummy;
}};

struct heap_slot {{
    struct page *page;
    int ref;
}};

struct function_call {{
    int fno;
    int32_t struct_page;
    bool has_struct_page;
}};

struct ain_function {{
    int is_lambda;
}};

struct ain {{
    int nr_functions;
    struct ain_function *functions;
}};

static struct heap_slot heap[128];
static struct function_call call_stack[32];
static int call_stack_ptr = 0;
static struct ain_function funcs[8];
static struct ain ain_obj = {{ .nr_functions = 8, .functions = funcs }};
__attribute__((unused)) static struct ain *ain = &ain_obj;

static bool page_index_valid(int slot) {{
    return slot >= 0 && slot < 128 && heap[slot].ref > 0 && heap[slot].page != NULL;
}}

{fn_slot}
{fn_get}

int main(void) {{
    struct page obj_page = {{ .dummy = 42 }};
    heap[86].page = &obj_page;
    heap[86].ref = 1;

    // #0: "0" / main called via vm_call(fno, -1) -> has_struct_page=true, struct_page=-1
    call_stack[0] = (struct function_call){{ .fno = 0, .struct_page = -1, .has_struct_page = true }};
    // #1: CSpriteParts@0 constructor -> has_struct_page=true, struct_page=86
    call_stack[1] = (struct function_call){{ .fno = 1, .struct_page = 86, .has_struct_page = true }};
    // #2: CSpriteParts@2 or CASTimerImp@Reset via CALLFUNC (is_lambda=0, has_struct_page=false)
    funcs[2].is_lambda = 0;
    call_stack[2] = (struct function_call){{ .fno = 2, .struct_page = -1, .has_struct_page = false }};
    call_stack_ptr = 3;

    assert(struct_page_slot() == 86);
    assert(get_struct_page(0) == &obj_page);

    // #3: nested non-lambda helper via CALLFUNC -> still inherits 86
    funcs[3].is_lambda = 0;
    call_stack[3] = (struct function_call){{ .fno = 3, .struct_page = -1, .has_struct_page = false }};
    call_stack_ptr = 4;
    assert(struct_page_slot() == 86);
    assert(get_struct_page(0) == &obj_page);

    // #4: null-this CALLMETHOD / free-function delegate / scenario_call -> barrier!
    call_stack[4] = (struct function_call){{ .fno = 4, .struct_page = -1, .has_struct_page = true }};
    call_stack_ptr = 5;
    assert(struct_page_slot() == -1);
    assert(get_struct_page(0) == NULL);

    // #5: helper called via CALLFUNC from inside null-this method -> stops at #4 (-1), does NOT steal 86!
    call_stack[5] = (struct function_call){{ .fno = 2, .struct_page = -1, .has_struct_page = false }};
    call_stack_ptr = 6;
    assert(struct_page_slot() == -1);
    assert(get_struct_page(0) == NULL);
    // Frame 4 (index 1 from bottom = call_stack[1]) still sees &obj_page
    assert(get_struct_page(4) == &obj_page);

    return 0;
}}
"""
        compile_and_run(c_code)

    def test_x_a_init_reused_carrier_preserves_live_slots(self) -> None:
        case_block = extract_case_block("src/vm.c", "X_A_INIT")
        c_code = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum ain_data_type {{
    AIN_VOID = 0,
    AIN_INT = 10,
    AIN_ARRAY = 14,
    AIN_WRAP = 100,
    AIN_OPTION = 101,
}};

enum page_type {{
    GLOBAL_PAGE = 0,
    LOCAL_PAGE = 1,
    STRUCT_PAGE = 2,
    ARRAY_PAGE = 3,
}};

struct ain_type {{
    enum ain_data_type data;
    int struc;
    struct ain_type *array_type;
}};

union vm_value {{
    int32_t i;
    float f;
}};

struct page {{
    enum page_type type;
    enum ain_data_type a_type;
    int nr_vars;
    struct {{
        int rank;
        int struct_type;
        struct ain_type element_type;
    }} array;
    union vm_value values[];
}};

struct heap_slot {{
    struct page *page;
    int ref;
}};

static struct heap_slot heap[64];
static int next_slot = 1;
static union vm_value stack[64];
static int stack_ptr = 0;
static unsigned int invalid_var_warn_count = 0;
#define INVALID_VAR_WARN_LIMIT 64
#define WARNING(...) ((void)0)

static void stack_push(int32_t v) {{ stack[stack_ptr++].i = v; }}
static union vm_value stack_pop(void) {{ return stack[--stack_ptr]; }}

static bool page_index_valid(int slot) {{
    return slot >= 0 && slot < 64 && heap[slot].ref > 0 && heap[slot].page != NULL;
}}

static bool heap_index_valid(int slot) {{
    return slot >= 0 && slot < 64 && heap[slot].ref > 0;
}}

static struct page *alloc_page(enum page_type t, int sub, int nr_vars) {{
    struct page *p = calloc(1, sizeof(struct page) + (size_t)nr_vars * sizeof(union vm_value));
    p->type = t;
    p->a_type = (enum ain_data_type)sub;
    p->nr_vars = nr_vars;
    return p;
}}

static int heap_alloc_page(struct page *p) {{
    int s = next_slot++;
    heap[s].page = p;
    heap[s].ref = 1;
    return s;
}}

static void heap_unref(int slot) {{
    assert(heap_index_valid(slot));
    if (--heap[slot].ref == 0) {{
        free(heap[slot].page);
        heap[slot].page = NULL;
    }}
}}

static struct ain_type elem_int = {{ .data = AIN_INT, .struc = -1, .array_type = NULL }};
static struct ain_type arr_int = {{ .data = AIN_ARRAY, .struc = -1, .array_type = &elem_int }};

static const struct ain_type *lookup_expected_var_type(int32_t pageno, int32_t varno) {{
    (void)pageno;
    (void)varno;
    return &arr_int;
}}

static int ain_type_slot_width(const struct ain_type *t) {{ (void)t; return 1; }}
static bool ain_type_is_null_handle(enum ain_data_type dt) {{ (void)dt; return false; }}

static struct page *v12_array_new(const struct ain_type *elem_t, int count, bool init) {{
    (void)init;
    struct page *p = alloc_page(ARRAY_PAGE, AIN_ARRAY, count);
    p->array.rank = 1;
    p->array.struct_type = -1;
    p->array.element_type = elem_t ? *elem_t : elem_int;
    return p;
}}

enum opcode {{ X_A_INIT = 1 }};
static int cur_arg0 = 0;
static int get_argument(int idx) {{ assert(idx == 0); return cur_arg0; }}

static void run_x_a_init(int init_elems) {{
    cur_arg0 = init_elems;
    switch (X_A_INIT) {{
    {case_block}
    }}
}}

int main(void) {{
    // Allocate global page (slot 0) and local scratch page (slot 1)
    heap[0].page = alloc_page(GLOBAL_PAGE, 0, 4);
    heap[0].ref = 1;
    int local_slot = heap_alloc_page(alloc_page(LOCAL_PAGE, 0, 2));
    heap[local_slot].page->values[0].i = -1;

    // 1st X_A_INIT on scratch carrier (local_slot, 0), transferred to global[0] via X_ASSIGN
    stack_push(local_slot);
    stack_push(0);
    stack_push(4);
    run_x_a_init(0);
    int arr1 = stack_pop().i;
    heap[0].page->values[0].i = arr1; // X_ASSIGN raw slot transfer (ref stays 1)

    // 2nd X_A_INIT reusing the same scratch carrier (local_slot, 0)
    stack_push(local_slot);
    stack_push(0);
    stack_push(8);
    run_x_a_init(0);
    int arr2 = stack_pop().i;
    heap[0].page->values[1].i = arr2;

    // Both arr1 and arr2 MUST still be alive with ref == 1 and type == ARRAY_PAGE!
    assert(arr1 != arr2);
    assert(page_index_valid(arr1) && heap[arr1].page->type == ARRAY_PAGE && heap[arr1].page->nr_vars == 4);
    assert(page_index_valid(arr2) && heap[arr2].page->type == ARRAY_PAGE && heap[arr2].page->nr_vars == 8);
    assert(heap[arr1].ref == 1);
    assert(heap[arr2].ref == 1);

    heap_unref(arr1);
    heap_unref(arr2);
    heap_unref(local_slot);
    free(heap[0].page);
    return 0;
}}
"""
        compile_and_run(c_code)

    def test_re_set_projection_zero_height_succeeds(self) -> None:
        fn_proj = extract_function("src/3d/reign.c", "RE_set_projection")
        c_code = f"""
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>

typedef float mat4[4][4];

struct RE_plugin {{
    mat4 proj_transform;
}};

static float glm_rad(float deg) {{
    return deg * 0.017453292519943295f;
}}

static void glm_mat4_identity(mat4 m) {{
    memset(m, 0, sizeof(mat4));
    for (int i = 0; i < 4; i++)
        m[i][i] = 1.0f;
}}

static void glm_perspective(float fovy, float aspect, float nearZ, float farZ, mat4 dest) {{
    memset(dest, 0, sizeof(mat4));
    float f = 1.0f / tanf(fovy * 0.5f);
    dest[0][0] = f / aspect;
    dest[1][1] = f;
    dest[2][2] = (farZ + nearZ) / (nearZ - farZ);
    dest[2][3] = -1.0f;
    dest[3][2] = (2.0f * farZ * nearZ) / (nearZ - farZ);
}}

{fn_proj}

int main(void) {{
    assert(!RE_set_projection(NULL, 1280.0f, 720.0f, 1.0f, 1000.0f, 60.0f));

    struct RE_plugin p;
    memset(&p, 0x7f, sizeof(p));

    // Zero-height initial projection call (Evenicle TapirEngine setup) must succeed cleanly!
    assert(RE_set_projection(&p, 1280.0f, 0.0f, 1.0f, 1000.0f, 60.0f));
    for (int c = 0; c < 4; c++) {{
        for (int r = 0; r < 4; r++) {{
            assert(isfinite(p.proj_transform[c][r]));
            assert(p.proj_transform[c][r] == (c == r ? 1.0f : 0.0f));
        }}
    }}

    // Normal non-zero projection call
    assert(RE_set_projection(&p, 1280.0f, 720.0f, 1.0f, 1000.0f, 60.0f));
    assert(isfinite(p.proj_transform[0][0]) && p.proj_transform[0][0] > 0.0f);
    assert(isfinite(p.proj_transform[1][1]) && p.proj_transform[1][1] > 0.0f);
    // Horizontal FOV convention: [0][0] == f, [1][1] == f * aspect
    float aspect = 1280.0f / 720.0f;
    assert(fabsf(p.proj_transform[1][1] / p.proj_transform[0][0] - aspect) < 1e-5f);
    return 0;
}}
"""
        compile_and_run(c_code)

    def test_rance6_hacks_and_gbk_detection(self) -> None:
        fn_cfg_utf8 = extract_function("src/system4.c", "config_init_with_utf8_codename")
        fn_cfg = extract_function("src/system4.c", "config_init")
        c_code = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "system4.h"
#include "xsystem4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

void apply_game_specific_hacks(struct ain *ain);

struct config config;
bool message_layout_init_rance03(struct ain *ain) {{ (void)ain; return false; }}

static bool gbk_at_get_save_path = false;
static char *get_xsystem4_home(void) {{ return xstrdup("/tmp/xsys4_home"); }}
static bool is_directory(const char *path) {{ (void)path; return false; }}
static char *get_save_path(const char *dir, const char *utf8_override) {{
    (void)utf8_override;
    gbk_at_get_save_path = sys4_text_is_gbk;
    return xstrdup(dir ? dir : "SaveData");
}}

{fn_cfg_utf8}
{fn_cfg}

static void reset_config_for_test(const char *game_name, const char *ain_name) {{
    free(config.game_name);
    free(config.ain_filename);
    free(config.save_dir);
    free(config.home_dir);
    config.game_name = game_name ? xstrdup(game_name) : NULL;
    config.ain_filename = xstrdup(ain_name);
    config.save_dir = xstrdup("SaveData");
    config.home_dir = NULL;
    config.view_width = 800;
    config.view_height = 600;
    config.view_width_set = false;
    config.view_height_set = false;
    sys4_set_text_gbk(false);
    sys4_source_is_utf8 = false;
    gbk_at_get_save_path = false;
}}

static struct ain *make_r6_ain(const char *font_str, bool with_get_text_width) {{
    struct ain *a = ain_new(4, 0);
    ain_add_library(a, "SACT2");
    ain_add_library(a, "DrawGraph");
    ain_add_library(a, "ACXLoader");
    ain_add_library(a, "DrawDungeon");
    ain_add_library(a, "PlayMovie");
    ain_add_struct(a, "CDungeon");
    ain_add_struct(a, "CScreenBattle");
    ain_add_function(a, "main");
    if (with_get_text_width)
        ain_add_function(a, "GetTextWidth");
    ain_add_string(a, font_str);
    return a;
}}

int main(void) {{
    // 1. JP Rance VI: Shift-JIS MS Gothic, GameName = "Rance6"
    struct ain *jp_ain = make_r6_ain("\\x82\\x6c\\x82\\x72 \\x83\\x53\\x83\\x56\\x83\\x62\\x83\\x4e", false);
    reset_config_for_test("Rance6", "Rance6.ain");
    config_init();
    assert(!gbk_at_get_save_path);
    assert(!sys4_text_is_gbk);
    apply_game_specific_hacks(jp_ain);
    assert(game_rance6 && !game_rance6_mg && !sys4_text_is_gbk);
    assert(config.view_width == 640 && config.view_height == 480);

    // 2. MangaGamer English Rance VI (even with customized GameName!)
    struct ain *mg_ain = make_r6_ain("\\x82\\x6c\\x82\\x72 \\x83\\x53\\x83\\x56\\x83\\x62\\x83\\x4e", true);
    reset_config_for_test("Rance VI - The Collapse of Zeth", "Rance6.ain");
    config_init();
    apply_game_specific_hacks(mg_ain);
    assert(game_rance6 && game_rance6_mg && !sys4_text_is_gbk);

    // 3. CN Rance VI v1.10 Simplified System40.ini
    struct ain *cn_ain = make_r6_ain("\\xba\\xda\\xcc\\xe5", false);
    reset_config_for_test("Rance6\\xd6\\xd0\\xce\\xc4\\xb0\\xe6 v1.10\\xa3\\xa8\\xba\\xba\\xbb\\xaf\\xd7\\xe9\\xa3\\xa9", "Rance6.ain");
    config_init();
    assert(gbk_at_get_save_path && sys4_text_is_gbk);
    apply_game_specific_hacks(cn_ain);
    assert(game_rance6 && !game_rance6_mg && sys4_text_is_gbk);

    // 4. CN Rance VI v1.10 Traditional System40Tcn.ini
    reset_config_for_test("Rance6\\xd6\\xd0\\xce\\xc4\\xb0\\xe6 v1.10\\xa3\\xa8\\x9e\\x61\\xbb\\xaf\\xbf\\x82\\xa3\\xa9", "Rance6.ain");
    config_init();
    assert(gbk_at_get_save_path && sys4_text_is_gbk);
    apply_game_specific_hacks(cn_ain);
    assert(game_rance6 && !game_rance6_mg && sys4_text_is_gbk);

    // 5. CN Rance VI with unpatched JP GameName = "Rance6" (v1.10 "黑体" and v0.90 GBK "ＭＳ ゴシック")
    reset_config_for_test("Rance6", "Rance6.ain");
    config_init();
    apply_game_specific_hacks(cn_ain);
    assert(game_rance6 && !game_rance6_mg && sys4_text_is_gbk);

    struct ain *cn_v090_ain = make_r6_ain("\\xa3\\xcd\\xa3\\xd3\\x20\\xa5\\xb4\\xa5\\xb7\\xa5\\xc3\\xa5\\xaf", false);
    reset_config_for_test("Rance6", "Rance6.ain");
    config_init();
    apply_game_specific_hacks(cn_v090_ain);
    assert(game_rance6 && !game_rance6_mg && sys4_text_is_gbk);

    ain_free(jp_ain);
    ain_free(mg_ain);
    ain_free(cn_ain);
    ain_free(cn_v090_ain);
    free(config.game_name);
    free(config.ain_filename);
    free(config.save_dir);
    free(config.home_dir);
    return 0;
}}
"""
        compile_and_run_with_sources(c_code, LIBSYS4_HACKS_SRCS, ["-lz", "-lm"])

    def test_evenicle_reverse_erase_loop_bytecode_patch(self) -> None:
        ev1_ain_path = Path("/root/Rance10_CN/Evenicle/イブニクル/Evenicle.ain")
        ev1_arg = [str(ev1_ain_path)] if ev1_ain_path.is_file() else []
        c_code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xsystem4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/instructions.h"
#include "system4/little_endian.h"

void apply_game_specific_hacks(struct ain *ain);

struct config config;
bool message_layout_init_rance03(struct ain *ain) { (void)ain; return false; }

static void emit0(struct buffer *b, enum opcode op) {
    buffer_write_int16(b, (int16_t)op);
}
static void emit1(struct buffer *b, enum opcode op, int32_t a0) {
    buffer_write_int16(b, (int16_t)op);
    buffer_write_int32(b, a0);
}
static void emit3(struct buffer *b, enum opcode op, int32_t a0, int32_t a1, int32_t a2) {
    buffer_write_int16(b, (int16_t)op);
    buffer_write_int32(b, a0);
    buffer_write_int32(b, a1);
    buffer_write_int32(b, a2);
}

static void add_array_library(struct ain *a, int *lib_out, int *numof_out, int *erase_out) {
    int lib = ain_add_library(a, "Array");
    struct ain_library *l = &a->libraries[lib];
    l->nr_functions = 2;
    l->functions = calloc(2, sizeof(*l->functions));
    l->functions[0].name = strdup("Numof");
    l->functions[1].name = strdup("Erase");
    *lib_out = lib;
    *numof_out = 0;
    *erase_out = 1;
}

static void attach_code(struct ain *a, int fno, struct buffer *b, uint32_t body_start) {
    a->functions[fno].address = (int32_t)body_start;
    a->code = malloc(b->index);
    memcpy(a->code, b->buf, b->index);
    a->code_size = b->index;
}

// Mini-VM verifier for the Evenicle 2 (AIN v14) reverse-erase loop bytecode.
// Confirms the patched bytecode terminates cleanly in exactly N iterations
// instead of spinning toward INT_MAX.
static int run_ev2_loop(const uint8_t *code, size_t code_size, uint32_t start,
        int array_lib, int numof_fn, int erase_fn, int initial_count, int *erases_out) {
    int32_t locals[8] = {0};
    int32_t stack[32] = {0};
    int sp = 0;
    int list_len = initial_count;
    int erases = 0;
    uint32_t ip = start;
    int steps = 0;
    while (ip < code_size && steps < 1000) {
        steps++;
        enum opcode op = (enum opcode)(uint16_t)LittleEndian_getW(code, ip);
        switch (op) {
        case PUSHLOCALPAGE:
            stack[sp++] = 1;
            ip += 2;
            break;
        case PUSHSTRUCTPAGE:
            stack[sp++] = 2;
            ip += 2;
            break;
        case PUSH:
            stack[sp++] = LittleEndian_getDW(code, ip + 2);
            ip += 6;
            break;
        case POP:
            sp--;
            ip += 2;
            break;
        case X_REF: {
            int n = LittleEndian_getDW(code, ip + 2);
            assert(n == 1);
            int var = stack[--sp];
            int page = stack[--sp];
            stack[sp++] = (page == 1) ? locals[var] : 99;
            ip += 6;
            break;
        }
        case X_ASSIGN: {
            int n = LittleEndian_getDW(code, ip + 2);
            assert(n == 1);
            int val = stack[--sp];
            int var = stack[--sp];
            int page = stack[--sp];
            assert(page == 1);
            locals[var] = val;
            stack[sp++] = val;
            ip += 6;
            break;
        }
        case X_DUP: {
            int n = LittleEndian_getDW(code, ip + 2);
            assert(n == 2);
            int b = stack[sp - 1];
            int a = stack[sp - 2];
            stack[sp++] = a;
            stack[sp++] = b;
            ip += 6;
            break;
        }
        case SUB: {
            int rhs = stack[--sp];
            int lhs = stack[--sp];
            stack[sp++] = lhs - rhs;
            ip += 2;
            break;
        }
        case GTE: {
            int rhs = stack[--sp];
            int lhs = stack[--sp];
            stack[sp++] = lhs >= rhs ? 1 : 0;
            ip += 2;
            break;
        }
        case INC: {
            int var = stack[--sp];
            int page = stack[--sp];
            assert(page == 1);
            locals[var]++;
            ip += 2;
            break;
        }
        case DEC: {
            int var = stack[--sp];
            int page = stack[--sp];
            assert(page == 1);
            locals[var]--;
            ip += 2;
            break;
        }
        case IFZ: {
            uint32_t target = (uint32_t)LittleEndian_getDW(code, ip + 2);
            int cond = stack[--sp];
            ip = (cond == 0) ? target : ip + 6;
            break;
        }
        case JUMP:
            ip = (uint32_t)LittleEndian_getDW(code, ip + 2);
            break;
        case CALLHLL: {
            int lib = LittleEndian_getDW(code, ip + 2);
            int fn = LittleEndian_getDW(code, ip + 6);
            assert(lib == array_lib);
            if (fn == numof_fn) {
                sp--; // pop array ref
                stack[sp++] = list_len;
            } else if (fn == erase_fn) {
                int len = stack[--sp];
                int idx = stack[--sp];
                int arr = stack[--sp];
                assert(arr == 99 && len == 1 && idx >= 0);
                erases++;
                stack[sp++] = 1;
            } else {
                assert(0);
            }
            ip += 14;
            break;
        }
        case RETURN:
            assert(sp == 0);
            *erases_out = erases;
            return 1;
        default:
            assert(0);
        }
    }
    return 0; // did not terminate within step bound (hung!)
}

// Mini-VM verifier for the Evenicle 1 (AIN v6) reverse-erase loop bytecode.
static int run_ev1_loop(const uint8_t *code, size_t code_size, uint32_t start,
        int initial_count, int *erases_out) {
    int32_t locals[8] = {0};
    int32_t stack[32] = {0};
    int sp = 0;
    int list_len = initial_count;
    int erases = 0;
    uint32_t ip = start;
    int steps = 0;
    while (ip < code_size && steps < 1000) {
        steps++;
        enum opcode op = (enum opcode)(uint16_t)LittleEndian_getW(code, ip);
        switch (op) {
        case PUSHLOCALPAGE:
            stack[sp++] = 1;
            ip += 2;
            break;
        case PUSHGLOBALPAGE:
            stack[sp++] = 3;
            ip += 2;
            break;
        case PUSH:
            stack[sp++] = LittleEndian_getDW(code, ip + 2);
            ip += 6;
            break;
        case POP:
            sp--;
            ip += 2;
            break;
        case REF: {
            int var = stack[--sp];
            int page = stack[--sp];
            assert(page == 1);
            stack[sp++] = locals[var];
            ip += 2;
            break;
        }
        case ASSIGN: {
            int val = stack[--sp];
            int var = stack[--sp];
            int page = stack[--sp];
            assert(page == 1);
            locals[var] = val;
            stack[sp++] = val;
            ip += 2;
            break;
        }
        case DUP2: {
            int b = stack[sp - 1];
            int a = stack[sp - 2];
            stack[sp++] = a;
            stack[sp++] = b;
            ip += 2;
            break;
        }
        case A_NUMOF: {
            int rank = stack[--sp];
            int var = stack[--sp];
            int page = stack[--sp];
            assert(page == 1 && var == 4 && rank == 1);
            stack[sp++] = list_len;
            ip += 2;
            break;
        }
        case A_ERASE: {
            int idx = stack[--sp];
            int var = stack[--sp];
            int page = stack[--sp];
            assert(page == 3 && var == 293 && idx >= 0);
            erases++;
            stack[sp++] = 1;
            ip += 2;
            break;
        }
        case SUB: {
            int rhs = stack[--sp];
            int lhs = stack[--sp];
            stack[sp++] = lhs - rhs;
            ip += 2;
            break;
        }
        case GTE: {
            int rhs = stack[--sp];
            int lhs = stack[--sp];
            stack[sp++] = lhs >= rhs ? 1 : 0;
            ip += 2;
            break;
        }
        case INC: {
            int var = stack[--sp];
            int page = stack[--sp];
            assert(page == 1);
            locals[var]++;
            ip += 2;
            break;
        }
        case DEC: {
            int var = stack[--sp];
            int page = stack[--sp];
            assert(page == 1);
            locals[var]--;
            ip += 2;
            break;
        }
        case SH_LOCALINC: {
            int var = LittleEndian_getDW(code, ip + 2);
            locals[var]++;
            ip += 6;
            break;
        }
        case SH_LOCALDEC: {
            int var = LittleEndian_getDW(code, ip + 2);
            locals[var]--;
            ip += 6;
            break;
        }
        case IFZ: {
            uint32_t target = (uint32_t)LittleEndian_getDW(code, ip + 2);
            int cond = stack[--sp];
            ip = (cond == 0) ? target : ip + 6;
            break;
        }
        case JUMP:
            ip = (uint32_t)LittleEndian_getDW(code, ip + 2);
            break;
        case RETURN:
            assert(sp == 0);
            *erases_out = erases;
            return 1;
        default:
            assert(0);
        }
    }
    return 0;
}

static struct ain *build_ev2_ain(bool is_evenicle2, bool is_rance, const char *target_fn,
        enum opcode loop2_step_op, uint32_t *loop1_inc_addr, uint32_t *loop2_start_addr,
        uint32_t *loop2_step_addr, int *lib_out, int *numof_out, int *erase_out) {
    struct ain *a = ain_new(14, 0);
    if (is_evenicle2)
        ain_add_function(a, "C_MedicaMenu@0");
    if (is_rance)
        ain_add_string(a, "th74s65a");
    int fno = ain_add_function(a, target_fn);
    add_array_library(a, lib_out, numof_out, erase_out);

    struct buffer b;
    buffer_init(&b, NULL, 0);
    emit1(&b, FUNC, fno);
    uint32_t body_start = (uint32_t)b.index;

    // Loop 1 (forward loop: MUST NOT be modified!)
    uint32_t l1_cond = (uint32_t)b.index;
    emit0(&b, PUSHLOCALPAGE);
    emit1(&b, PUSH, 1);
    emit1(&b, X_REF, 1);
    emit0(&b, PUSHSTRUCTPAGE);
    emit1(&b, PUSH, 0);
    emit1(&b, X_REF, 1);
    emit3(&b, CALLHLL, *lib_out, *numof_out, 2);
    emit0(&b, LT);
    uint32_t l1_ifz_pos = (uint32_t)b.index;
    emit1(&b, IFZ, 0);
    emit0(&b, PUSHLOCALPAGE);
    emit1(&b, PUSH, 1);
    emit1(&b, X_DUP, 2);
    *loop1_inc_addr = (uint32_t)b.index;
    emit0(&b, INC);
    emit0(&b, POP);
    emit0(&b, POP);
    emit1(&b, JUMP, (int32_t)l1_cond);

    // Loop 2 (exact verified Evenicle 2 EraseUnnecessaryInList reverse-erase loop)
    uint32_t l2_init = (uint32_t)b.index;
    *loop2_start_addr = l2_init;
    buffer_write_int32_at(&b, l1_ifz_pos + 2, (int32_t)l2_init);
    emit0(&b, PUSHLOCALPAGE);
    emit1(&b, PUSH, 3);
    emit0(&b, PUSHLOCALPAGE);
    emit1(&b, PUSH, 0);
    emit1(&b, X_REF, 1);
    emit3(&b, CALLHLL, *lib_out, *numof_out, 1);
    emit1(&b, PUSH, 1);
    emit0(&b, SUB);
    emit1(&b, X_ASSIGN, 1);
    emit0(&b, POP);

    uint32_t l2_cond = (uint32_t)b.index;
    assert(l2_cond == l2_init + 52u);
    emit0(&b, PUSHLOCALPAGE);
    emit1(&b, PUSH, 3);
    emit1(&b, X_REF, 1);
    emit1(&b, PUSH, 0);
    emit0(&b, GTE);
    uint32_t l2_ifz_pos = (uint32_t)b.index;
    emit1(&b, IFZ, 0);

    emit0(&b, PUSHSTRUCTPAGE);
    emit1(&b, PUSH, 0);
    emit1(&b, X_REF, 1);
    emit0(&b, PUSHLOCALPAGE);
    emit1(&b, PUSH, 3);
    emit1(&b, X_REF, 1);
    emit1(&b, PUSH, 1);
    emit3(&b, CALLHLL, *lib_out, *erase_out, 2);
    emit0(&b, POP);

    emit0(&b, PUSHLOCALPAGE);
    emit1(&b, PUSH, 3);
    emit1(&b, X_DUP, 2);
    *loop2_step_addr = (uint32_t)b.index;
    emit0(&b, loop2_step_op);
    emit0(&b, POP);
    emit0(&b, POP);
    emit1(&b, JUMP, (int32_t)l2_cond);

    uint32_t l2_exit = (uint32_t)b.index;
    assert(l2_exit == l2_cond + 104u);
    buffer_write_int32_at(&b, l2_ifz_pos + 2, (int32_t)l2_exit);
    emit0(&b, RETURN);
    emit1(&b, ENDFUNC, fno);

    attach_code(a, fno, &b, body_start);
    free(b.buf);
    return a;
}

static struct ain *build_ev1_ain(int variant, const char *target_fn, bool shorthand,
        enum opcode loop2_step_op, uint32_t *loop1_inc_addr, uint32_t *loop2_start_addr,
        uint32_t *loop2_step_addr) {
    // variant: 0=non-evenicle, 1=evenicle, 2=evenicle_rance, 3=evenicle_gaiden
    struct ain *a = ain_new(6, 1);
    if (variant >= 1)
        ain_add_struct(a, "CBigMap");
    if (variant == 1)
        ain_add_function(a, "CMapPlayer@RunPathFindMove");
    if (variant == 2)
        ain_add_string(a, "56s5d4");
    if (variant == 3)
        ain_add_string(a, "aa6sdd5432");
    int fno = ain_add_function(a, target_fn);

    struct buffer b;
    buffer_init(&b, NULL, 0);
    emit1(&b, FUNC, fno);
    uint32_t body_start = (uint32_t)b.index;

    // Loop 1 (forward loop: MUST NOT be modified!)
    uint32_t l1_cond = (uint32_t)b.index;
    emit0(&b, PUSHLOCALPAGE);
    emit1(&b, PUSH, 3);
    emit0(&b, REF);
    emit0(&b, PUSHGLOBALPAGE);
    emit1(&b, PUSH, 293);
    emit1(&b, PUSH, 1);
    emit0(&b, A_NUMOF);
    emit0(&b, LT);
    uint32_t l1_ifz_pos = (uint32_t)b.index;
    emit1(&b, IFZ, 0);
    emit0(&b, PUSHLOCALPAGE);
    emit1(&b, PUSH, 3);
    emit0(&b, DUP2);
    *loop1_inc_addr = (uint32_t)b.index;
    emit0(&b, INC);
    emit0(&b, POP);
    emit0(&b, POP);
    emit1(&b, JUMP, (int32_t)l1_cond);

    // Loop 2 (exact verified Evenicle 1 WaitAllOrderCharaMotionFinish reverse-erase loop)
    uint32_t l2_init = (uint32_t)b.index;
    *loop2_start_addr = l2_init;
    buffer_write_int32_at(&b, l1_ifz_pos + 2, (int32_t)l2_init);
    emit0(&b, PUSHLOCALPAGE);
    emit1(&b, PUSH, 3);
    emit0(&b, PUSHLOCALPAGE);
    emit1(&b, PUSH, 4);
    emit1(&b, PUSH, 1);
    emit0(&b, A_NUMOF);
    emit1(&b, PUSH, 1);
    emit0(&b, SUB);
    emit0(&b, ASSIGN);
    emit0(&b, POP);

    uint32_t l2_cond = (uint32_t)b.index;
    assert(l2_cond == l2_init + 36u);
    emit0(&b, PUSHLOCALPAGE);
    emit1(&b, PUSH, 3);
    emit0(&b, REF);
    emit1(&b, PUSH, 0);
    emit0(&b, GTE);
    uint32_t l2_ifz_pos = (uint32_t)b.index;
    emit1(&b, IFZ, 0);
    uint32_t l2_jbody_pos = (uint32_t)b.index;
    emit1(&b, JUMP, 0);

    uint32_t l2_step = (uint32_t)b.index;
    if (!shorthand) {
        emit0(&b, PUSHLOCALPAGE);
        emit1(&b, PUSH, 3);
        emit0(&b, DUP2);
        *loop2_step_addr = (uint32_t)b.index;
        emit0(&b, loop2_step_op);
        emit0(&b, POP);
        emit0(&b, POP);
    } else {
        *loop2_step_addr = (uint32_t)b.index;
        emit1(&b, loop2_step_op, 3);
    }
    emit1(&b, JUMP, (int32_t)l2_cond);

    uint32_t l2_body = (uint32_t)b.index;
    buffer_write_int32_at(&b, l2_jbody_pos + 2, (int32_t)l2_body);
    emit0(&b, PUSHGLOBALPAGE);
    emit1(&b, PUSH, 293);
    emit0(&b, PUSHLOCALPAGE);
    emit1(&b, PUSH, 3);
    emit0(&b, REF);
    emit0(&b, A_ERASE);
    emit0(&b, POP);
    emit1(&b, JUMP, (int32_t)l2_step);

    uint32_t l2_exit = (uint32_t)b.index;
    buffer_write_int32_at(&b, l2_ifz_pos + 2, (int32_t)l2_exit);
    emit0(&b, RETURN);
    emit1(&b, ENDFUNC, fno);

    attach_code(a, fno, &b, body_start);
    free(b.buf);
    return a;
}

int main(int argc, char **argv) {
    config.view_width = 1280;
    config.view_height = 720;

    // 1. Evenicle 2 positive test (buggy INC hangs before patch, terminates cleanly after)
    {
        uint32_t l1_inc = 0, l2_start = 0, l2_step = 0;
        int lib = 0, numof = 0, erase = 0;
        struct ain *a = build_ev2_ain(true, false, "CReservePolyMotion@EraseUnnecessaryInList",
            INC, &l1_inc, &l2_start, &l2_step, &lib, &numof, &erase);
        uint8_t *before = malloc(a->code_size);
        memcpy(before, a->code, a->code_size);

        int erases = 0;
        assert(run_ev2_loop(a->code, a->code_size, l2_start, lib, numof, erase, 3, &erases) == 0);

        apply_game_specific_hacks(a);
        assert(game_evenicle2);
        assert((uint16_t)LittleEndian_getW(a->code, l1_inc) == INC);
        assert((uint16_t)LittleEndian_getW(a->code, l2_step) == DEC);
        for (size_t i = 0; i < a->code_size; i++) {
            if (i != l2_step && i != l2_step + 1)
                assert(a->code[i] == before[i]);
        }
        assert(run_ev2_loop(a->code, a->code_size, l2_start, lib, numof, erase, 3, &erases) == 1);
        assert(erases == 3);

        // Idempotent on second call (already DEC)
        memcpy(before, a->code, a->code_size);
        apply_game_specific_hacks(a);
        assert(memcmp(a->code, before, a->code_size) == 0);

        free(before);
        ain_free(a);
    }

    // 2. Evenicle 2 Rance positive test
    {
        uint32_t l1_inc = 0, l2_start = 0, l2_step = 0;
        int lib = 0, numof = 0, erase = 0;
        struct ain *a = build_ev2_ain(true, true, "CReservePolyMotion@EraseUnnecessaryInList",
            INC, &l1_inc, &l2_start, &l2_step, &lib, &numof, &erase);
        apply_game_specific_hacks(a);
        assert(game_evenicle2 && game_evenicle2_rance);
        assert((uint16_t)LittleEndian_getW(a->code, l2_step) == DEC);
        ain_free(a);
    }

    // 3. Evenicle 2 negative tests (already-fixed DEC, unrelated function, non-Evenicle2 v14)
    {
        uint32_t l1_inc = 0, l2_start = 0, l2_step = 0;
        int lib = 0, numof = 0, erase = 0;
        struct ain *fixed = build_ev2_ain(true, false, "CReservePolyMotion@EraseUnnecessaryInList",
            DEC, &l1_inc, &l2_start, &l2_step, &lib, &numof, &erase);
        uint8_t *snap = malloc(fixed->code_size);
        memcpy(snap, fixed->code, fixed->code_size);
        apply_game_specific_hacks(fixed);
        assert(memcmp(fixed->code, snap, fixed->code_size) == 0);
        free(snap);
        ain_free(fixed);

        struct ain *other_fn = build_ev2_ain(true, false, "CReservePolyMotion@OtherFunc",
            INC, &l1_inc, &l2_start, &l2_step, &lib, &numof, &erase);
        apply_game_specific_hacks(other_fn);
        assert((uint16_t)LittleEndian_getW(other_fn->code, l2_step) == INC);
        ain_free(other_fn);

        struct ain *other_game = build_ev2_ain(false, false, "CReservePolyMotion@EraseUnnecessaryInList",
            INC, &l1_inc, &l2_start, &l2_step, &lib, &numof, &erase);
        apply_game_specific_hacks(other_game);
        assert(!game_evenicle2);
        assert((uint16_t)LittleEndian_getW(other_game->code, l2_step) == INC);
        ain_free(other_game);
    }

    // 4. Evenicle 1 family positive tests (Evenicle, Evenicle Rance, Evenicle Gaiden + SH_LOCALINC)
    for (int variant = 1; variant <= 3; variant++) {
        for (int sh = 0; sh <= 1; sh++) {
            uint32_t l1_inc = 0, l2_start = 0, l2_step = 0;
            enum opcode buggy_op = sh ? SH_LOCALINC : INC;
            enum opcode fixed_op = sh ? SH_LOCALDEC : DEC;
            struct ain *a = build_ev1_ain(variant, "WaitAllOrderCharaMotionFinish",
                sh != 0, buggy_op, &l1_inc, &l2_start, &l2_step);
            uint8_t *before = malloc(a->code_size);
            memcpy(before, a->code, a->code_size);

            int erases = 0;
            assert(run_ev1_loop(a->code, a->code_size, l2_start, 4, &erases) == 0);

            apply_game_specific_hacks(a);
            assert(game_evenicle || game_evenicle_rance || game_evenicle_gaiden);
            assert((uint16_t)LittleEndian_getW(a->code, l1_inc) == INC);
            assert((uint16_t)LittleEndian_getW(a->code, l2_step) == fixed_op);
            for (size_t i = 0; i < a->code_size; i++) {
                if (i != l2_step && i != l2_step + 1)
                    assert(a->code[i] == before[i]);
            }
            assert(run_ev1_loop(a->code, a->code_size, l2_start, 4, &erases) == 1);
            assert(erases == 4);

            free(before);
            ain_free(a);
        }
    }

    // 5. Evenicle 1 negative tests (already-fixed DEC, unrelated function, non-Evenicle v6)
    {
        uint32_t l1_inc = 0, l2_start = 0, l2_step = 0;
        struct ain *fixed = build_ev1_ain(1, "WaitAllOrderCharaMotionFinish",
            false, DEC, &l1_inc, &l2_start, &l2_step);
        uint8_t *snap = malloc(fixed->code_size);
        memcpy(snap, fixed->code, fixed->code_size);
        apply_game_specific_hacks(fixed);
        assert(memcmp(fixed->code, snap, fixed->code_size) == 0);
        free(snap);
        ain_free(fixed);

        struct ain *other_fn = build_ev1_ain(1, "OtherMotionWait",
            false, INC, &l1_inc, &l2_start, &l2_step);
        apply_game_specific_hacks(other_fn);
        assert((uint16_t)LittleEndian_getW(other_fn->code, l2_step) == INC);
        ain_free(other_fn);

        struct ain *other_game = build_ev1_ain(0, "WaitAllOrderCharaMotionFinish",
            false, INC, &l1_inc, &l2_start, &l2_step);
        apply_game_specific_hacks(other_game);
        assert(!game_evenicle && !game_evenicle_rance && !game_evenicle_gaiden);
        assert((uint16_t)LittleEndian_getW(other_game->code, l2_step) == INC);
        ain_free(other_game);
    }

    // 6. Live read-only verification against Evenicle.ain when present
    if (argc > 1) {
        int err = 0;
        struct ain *live = ain_open(argv[1], &err);
        assert(live != NULL && err == 0);
        int fno = ain_get_function(live, "WaitAllOrderCharaMotionFinish");
        assert(fno > 0);
        uint32_t begin = (uint32_t)live->functions[fno].address;
        uint32_t end = (uint32_t)live->code_size;
        for (int i = 0; i < live->nr_functions; i++) {
            uint32_t addr = (uint32_t)live->functions[i].address;
            if (addr > begin && addr < end)
                end = addr;
        }
        assert(begin < 0x1ea106u && 0x1ea106u + 2u <= end);
        assert((uint16_t)LittleEndian_getW(live->code, 0x1e9feeu) == INC);
        assert((uint16_t)LittleEndian_getW(live->code, 0x1ea106u) == INC);
        uint8_t *before = malloc(end - begin);
        memcpy(before, live->code + begin, end - begin);

        apply_game_specific_hacks(live);
        assert(game_evenicle);
        assert((uint16_t)LittleEndian_getW(live->code, 0x1e9feeu) == INC);
        assert((uint16_t)LittleEndian_getW(live->code, 0x1ea106u) == DEC);
        for (uint32_t p = begin; p < end; p++) {
            if (p != 0x1ea106u && p != 0x1ea107u)
                assert(live->code[p] == before[p - begin]);
        }
        free(before);
        ain_free(live);
        puts("LIVE_EV1_OK");
    }

    puts("ALL_OK");
    return 0;
}
"""
        out = compile_and_run_with_sources(c_code, LIBSYS4_HACKS_SRCS, ["-lz", "-lm"], ev1_arg)
        self.assertIn("ALL_OK", out)
        if ev1_arg:
            self.assertIn("LIVE_EV1_OK", out)


if __name__ == "__main__":
    unittest.main()
