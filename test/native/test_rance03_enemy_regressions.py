#!/usr/bin/env python3
"""Host C regression tests for Rance 03 Battle Enemy Position fix.

Pre-UpdateMatrix PartsEngine games (AIN v6.1: Rance 03, Rance 9, Evenicle,
Escalayer Reboot) have parts_v2=true (via GetControllerID/PartsFunc) but do NOT
propagate parent scale/rotation into child local positions or UpperLeftPos
origin offsets. Hierarchical matrix propagation begins in UpdateMatrix / AIN
v11+ titles (Tsumamigui 3, Heartful Maman, Ixseal, Rance 10, Evenicle 2,
Dohna Dohna).

Compiles the real production functions from PartsEngine.c and parts.c directly
against minimal fixtures (no SDL, GL context, or game data required).
Run: python3 xsystem4/test/native/test_rance03_enemy_regressions.py
"""

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def extract_function(rel_path: str, name: str) -> str:
    source = (ROOT / rel_path).read_text(encoding="utf-8")
    pattern = re.compile(
        rf"^(?:static\s+)?(?:inline\s+)?[A-Za-z_][A-Za-z0-9_\s\*]*\b{re.escape(name)}\([^;{{}}]*\)\s*\r?\n?\{{",
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


def compile_and_run(c_src: str) -> str:
    cc = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
    assert cc, "C compiler required"
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        c_path = tmp / "harness.c"
        exe_path = tmp / "harness"
        c_path.write_text(c_src, encoding="utf-8")
        subprocess.run(
            [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
             str(c_path), "-lm", "-o", str(exe_path)],
            check=True,
        )
        res = subprocess.run([str(exe_path)], check=True, capture_output=True, text=True)
        return res.stdout


class Rance03EnemyRegressionsTest(unittest.TestCase):
    def test_parts_engine_prelink_capability_selection(self):
        """Compile real PartsEngine_PreLink and verify parts_v2 / parts_hierarchical_matrix."""
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum ain_data_type {
    AIN_VOID = 0,
    AIN_INT = 10,
    AIN_FLOAT = 11,
    AIN_STRING = 12,
    AIN_REF_STRING = 20,
    AIN_BOOL = 50,
};

struct ain_type { enum ain_data_type data; };
struct ain_hll_argument { const char *name; struct ain_type type; };
struct ain_hll_function {
    const char *name;
    struct ain_type return_type;
    int nr_arguments;
    struct ain_hll_argument *arguments;
};
struct ain_library {
    const char *name;
    int nr_functions;
    struct ain_hll_function *functions;
};
struct ain {
    int version;
    int nr_libraries;
    struct ain_library *libraries;
};

static struct ain *ain;
static bool parts_v2;
static bool parts_hierarchical_matrix;
static int multi_controller_calls;
static int replace_calls;

static int ain_get_library(struct ain *a, const char *name) {
    for (int i = 0; i < a->nr_libraries; i++) {
        if (!strcmp(a->libraries[i].name, name))
            return i;
    }
    return -1;
}

static int ain_get_library_function(struct ain *a, int libno, const char *name) {
    struct ain_library *lib = &a->libraries[libno];
    for (int i = 0; i < lib->nr_functions; i++) {
        if (!strcmp(lib->functions[i].name, name))
            return i;
    }
    return -1;
}

static void record_replace(const char *name) { (void)name; replace_calls++; }
#define static_library_replace(lib, name, fn) record_replace(name)
static void PE_enable_multi_controller(void) { multi_controller_calls++; }
''' + "\n".join([
            extract_function("src/hll/PartsEngine.c", "get_fun"),
            extract_function("src/hll/PartsEngine.c", "PartsEngine_PreLink"),
        ]) + r'''

static void run_case(int version, const char **fn_names, int nr_fns,
                     bool expect_v2, bool expect_hier, const char *label) {
    struct ain_hll_function funcs[16] = {0};
    assert(nr_fns <= 16);
    for (int i = 0; i < nr_fns; i++) {
        funcs[i].name = fn_names[i];
        funcs[i].return_type.data = AIN_INT;
    }
    struct ain_library lib = { "PartsEngine", nr_fns, funcs };
    struct ain mock_ain = { version, 1, &lib };
    ain = &mock_ain;
    parts_v2 = !expect_v2;
    parts_hierarchical_matrix = !expect_hier;
    replace_calls = 0;
    multi_controller_calls = 0;

    PartsEngine_PreLink();
    ain = NULL;
    if (parts_v2 != expect_v2 || parts_hierarchical_matrix != expect_hier) {
        fprintf(stderr, "FAIL %s: v2=%d (exp %d), hier=%d (exp %d)\n",
                label, parts_v2, expect_v2, parts_hierarchical_matrix, expect_hier);
        assert(0);
    }
}

int main(void) {
    // Legacy AIN v6 without GetControllerID/PartsFunc/UpdateMatrix
    const char *legacy_fns[] = { "SetPos", "SetZ" };
    run_case(6, legacy_fns, 2, false, false, "legacy-v6");

    // Rance 03 / Rance 9 / Evenicle / Escalayer Reboot (AIN v6.1, PartsFunc, no UpdateMatrix)
    const char *r03_fns[] = { "PartsFunc", "Parts_SetParentPartsNumber", "Parts_SetPartsOriginPosMode" };
    run_case(6, r03_fns, 3, true, false, "rance03-v6-partsfunc");

    // AIN v6 with GetControllerID but no UpdateMatrix
    const char *v6_ctrl_fns[] = { "GetControllerID", "AddController" };
    run_case(6, v6_ctrl_fns, 2, true, false, "v6-getcontrollerid");
    assert(multi_controller_calls == 1);

    // Tsumamigui 3 / Heartful Maman (AIN v8/v9 with UpdateMatrix)
    const char *v8_um_fns[] = { "GetControllerID", "UpdateMatrix" };
    run_case(8, v8_um_fns, 2, true, true, "v8-updatematrix");

    // Beat Valkyrie Ixseal (AIN v11, hierarchical even without UpdateMatrix symbol)
    const char *ixseal_fns[] = { "GetControllerID", "AddController" };
    run_case(11, ixseal_fns, 2, true, true, "ixseal-v11");

    // Rance 10 (AIN v12)
    const char *r10_fns[] = { "GetControllerID", "UpdateMatrix" };
    run_case(12, r10_fns, 2, true, true, "rance10-v12");

    // Evenicle 2 / Dohna Dohna (AIN v14)
    const char *v14_fns[] = { "GetControllerID", "UpdateMatrix" };
    run_case(14, v14_fns, 2, true, true, "v14-e2-dd");

    puts("prelink-ok");
    return 0;
}
'''
        out = compile_and_run(code)
        self.assertEqual(out.strip(), "prelink-ok")

    def test_default_flag_and_apply_local_matrix_preserved(self):
        """Ensure parts_hierarchical_matrix defaults true and parts_apply_local_matrix stays intact."""
        parts_c = (ROOT / "src/parts/parts.c").read_text(encoding="utf-8")
        self.assertIn("bool parts_hierarchical_matrix = true;", parts_c)
        apply_local = extract_function("src/parts/parts.c", "parts_apply_local_matrix")
        self.assertNotIn("parts_hierarchical_matrix", apply_local)

    def test_parts_transform_and_upper_left_pos(self):
        """Compile real parts.c transform/matrix/UpperLeftPos functions and verify both modes."""
        code = r'''
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

typedef float mat4[4][4];
typedef float vec3[3];
typedef float vec4[4];
typedef struct { float x, y; } SDL_FPoint;
typedef struct { int x, y; } Point;
typedef struct { int x, y, w, h; } Rectangle;
typedef struct { uint8_t r, g, b, a; } SDL_Color;

#define PARTS_NR_STATES 3
#define PARTS_LAYOUT_BOX 1
#define PARTS_LAYOUT_FREE 2

struct parts_params {
    int z;
    SDL_FPoint pos;
    bool show;
    uint8_t alpha;
    SDL_FPoint scale;
    struct { float x, y, z; } rotation;
    SDL_Color add_color;
    SDL_Color multiply_color;
};

struct parts_common {
    int w, h;
    Point origin_offset;
    Rectangle hitbox;
};

struct parts_state {
    int type;
    struct parts_common common;
    struct { int layout_type; } layout_box;
};

struct parts_scrollbar {
    float pos;
};

struct parts {
    int no;
    int component_type;
    struct { int z, z2; } sp;
    struct parts *parent;
    struct parts *first_child;
    struct parts *next_sibling;
    struct parts_state states[PARTS_NR_STATES];
    struct parts_params local;
    struct parts_params global;
    int scroll_links[2];
    struct parts_scrollbar *scrollbar;
    bool reverse_lr, reverse_tb;
    bool dirty;
    struct parts *world_matrix_self;
    uint64_t world_matrix_gen;
    mat4 world_matrix;
    mat4 committed_matrix;
};

#define PARTS_FOREACH_CHILD(child, p) \
    for ((child) = (p)->first_child; (child) != NULL; (child) = (child)->next_sibling)

static int dirty_list;
#define TAILQ_REMOVE(head, elem, field) do { (void)(head); (void)(elem); } while (0)

static bool parts_v2 = true;
static bool parts_hierarchical_matrix = true;
static uint64_t parts_render_gen = 1;
static Point root_pos = {0, 0};

#define MAX_PARTS 32
static struct parts *parts_registry[MAX_PARTS];

static bool parts_state_valid(int state) {
    return state >= 0 && state < PARTS_NR_STATES;
}
static struct parts *parts_try_get(int no) {
    if (no < 0 || no >= MAX_PARTS) return NULL;
    return parts_registry[no];
}
static struct parts *parts_get(int no) {
    struct parts *p = parts_try_get(no);
    assert(p != NULL);
    return p;
}
static SDL_FPoint parts_local_position(struct parts *p) {
    return p->local.pos;
}
static int parts_get_sprite_z(struct parts *p) { return p->sp.z; }
static int parts_get_sprite_z2(struct parts *p) { return p->sp.z2; }
static void parts_list_resort(struct parts *p) { (void)p; }
static void parts_do_layout(struct parts *p) { (void)p; }

static float glm_rad(float deg) { return deg * 0.017453292519943295f; }
static void glm_mat4_identity(mat4 m) {
    memset(m, 0, sizeof(mat4));
    for (int i = 0; i < 4; i++) m[i][i] = 1.0f;
}
static void glm_mat4_copy(mat4 src, mat4 dst) {
    memcpy(dst, src, sizeof(mat4));
}
static void mat4_mul(mat4 a, mat4 b, mat4 out) {
    mat4 m = {0};
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            for (int k = 0; k < 4; k++)
                m[c][r] += a[k][r] * b[c][k];
    memcpy(out, m, sizeof(mat4));
}
static void glm_mat4_mulv(mat4 m, vec4 v, vec4 dest) {
    vec4 r = {0};
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 4; col++)
            r[row] += m[col][row] * v[col];
    memcpy(dest, r, sizeof(vec4));
}
static void glm_translate(mat4 m, vec3 p) {
    mat4 t;
    glm_mat4_identity(t);
    for (int i = 0; i < 3; i++) t[3][i] = p[i];
    mat4_mul(m, t, m);
}
static void glm_scale(mat4 m, vec3 p) {
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 4; r++)
            m[c][r] *= p[c];
}
static void rotate_axis(mat4 m, float a, mat4 out, int x, int y) {
    mat4 t;
    glm_mat4_identity(t);
    t[x][x] = t[y][y] = cosf(a);
    t[x][y] = sinf(a);
    t[y][x] = -sinf(a);
    mat4_mul(m, t, out);
}
static void glm_rotate_z(mat4 m, float a, mat4 out) { rotate_axis(m, a, out, 0, 1); }
static void glm_rotate_x(mat4 m, float a, mat4 out) { rotate_axis(m, a, out, 1, 2); }
static void glm_rotate_y(mat4 m, float a, mat4 out) { rotate_axis(m, a, out, 2, 0); }

void parts_world_matrix(struct parts *parts, mat4 out);
''' + "\n".join([
            extract_function("src/parts/parts.c", "parts_apply_local_matrix"),
            extract_function("src/parts/parts.c", "parts_apply_local_transform"),
            extract_function("src/parts/parts.c", "parts_update_global_pos_mat"),
            extract_function("src/parts/parts.c", "parts_combine_params"),
            extract_function("src/parts/parts.c", "parts_update_component_mat"),
            extract_function("src/parts/parts.c", "parts_world_matrix"),
            extract_function("src/parts/parts.c", "PE_GetPartsUpperLeftPosX"),
            extract_function("src/parts/parts.c", "PE_GetPartsUpperLeftPosY"),
        ]) + r'''

static bool approx(float a, float b, float eps) {
    return fabsf(a - b) <= eps;
}

static void init_part(struct parts *p, int no, struct parts *parent) {
    memset(p, 0, sizeof(*p));
    p->no = no;
    p->parent = parent;
    p->local.show = true;
    p->local.alpha = 255;
    p->local.scale = (SDL_FPoint){1.0f, 1.0f};
    p->local.multiply_color = (SDL_Color){255, 255, 255, 255};
    p->global = p->local;
    p->world_matrix_self = p;
    glm_mat4_identity(p->committed_matrix);
    if (parent) {
        p->next_sibling = parent->first_child;
        parent->first_child = p;
    }
    assert(no > 0 && no < MAX_PARTS);
    parts_registry[no] = p;
}

static void test_monster_stand_rance03_vs_hierarchical(void) {
    // Rance 03 MonsterStand (trim_怪物／利萨斯亲卫队: 367x419, origin=7 bottom-left)
    const int w = 367, h = 419;
    const float offset_x = 627.0f;
    const float offset_y = 115.0f + 419.0f - 960.0f; // -426.0f

    struct {
        float px, py, scale;
        float exp_r03_x, exp_r03_y;
        float exp_r03_left, exp_r03_right, exp_r03_top, exp_r03_bottom;
        int exp_r03_ulx, exp_r03_uly, exp_r03_center_y;
        float exp_v12_x, exp_v12_y;
        int exp_v12_ulx, exp_v12_uly;
    } slots[3] = {
        // pos=0: scale 1.00
        { -128.0f, 864.0f, 1.00f,
           499.00f, 438.00f, 499.00f,  866.00f,  19.00f, 438.00f, 499,   19, 229,
           499.00f, 438.00f, 499,  19 },
        // pos=1: scale 0.85
        {  120.0f, 707.0f, 0.85f,
           652.95f, 344.90f, 652.95f,  964.90f, -11.25f, 344.90f, 652,  -74, 167,
           573.0075f, 399.215f, 573,  43 },
        // pos=2: scale 0.70
        {  329.0f, 574.0f, 0.70f,
           767.90f, 275.80f, 767.90f, 1024.80f, -17.50f, 275.80f, 767, -143, 130,
           636.23f, 365.26f, 636,  71 },
    };

    for (int i = 0; i < 3; i++) {
        memset(parts_registry, 0, sizeof(parts_registry));
        struct parts parent, offset_parent, sprite;
        init_part(&parent, 1, NULL);
        init_part(&offset_parent, 2, &parent);
        init_part(&sprite, 3, &offset_parent);

        parent.local.pos = (SDL_FPoint){slots[i].px, slots[i].py};
        parent.local.scale = (SDL_FPoint){slots[i].scale, slots[i].scale};
        // MonsterStand@getOffsetParentPosition pre-multiplies by parent scale:
        offset_parent.local.pos = (SDL_FPoint){offset_x * slots[i].scale, offset_y * slots[i].scale};
        sprite.local.pos = (SDL_FPoint){0.0f, 0.0f};
        sprite.states[0].common.w = w;
        sprite.states[0].common.h = h;
        sprite.states[0].common.origin_offset = (Point){0, -h};
        sprite.states[0].common.hitbox = (Rectangle){0, -h, w, h};

        // 1) Pre-UpdateMatrix mode (Rance 03): parts_v2=true, parts_hierarchical_matrix=false
        parts_v2 = true;
        parts_hierarchical_matrix = false;
        parts_render_gen++;
        mat4 m;
        parts_world_matrix(&sprite, m);
        assert(approx(m[3][0], slots[i].exp_r03_x, 0.01f));
        assert(approx(m[3][1], slots[i].exp_r03_y, 0.01f));
        assert(approx(m[0][0], slots[i].scale, 0.0001f));
        assert(approx(m[1][1], slots[i].scale, 0.0001f));

        // Rendered sprite quad corners (origin_offset=(0,-h) to (w,0))
        vec4 tl = {0.0f, (float)-h, 0.0f, 1.0f};
        vec4 br = {(float)w, 0.0f, 0.0f, 1.0f};
        glm_mat4_mulv(m, tl, tl);
        glm_mat4_mulv(m, br, br);
        assert(approx(tl[0], slots[i].exp_r03_left, 0.02f));
        assert(approx(br[0], slots[i].exp_r03_right, 0.02f));
        assert(approx(tl[1], slots[i].exp_r03_top, 0.02f));
        assert(approx(br[1], slots[i].exp_r03_bottom, 0.02f));

        // PE_GetPartsUpperLeftPosX/Y must remain unscaled for MonsterStand@getCenter
        int ulx = PE_GetPartsUpperLeftPosX(3, 1);
        int uly = PE_GetPartsUpperLeftPosY(3, 1);
        assert(ulx == slots[i].exp_r03_ulx);
        assert(uly == slots[i].exp_r03_uly);
        int real_h = (int)(h * slots[i].scale);
        int center_y = uly + h - real_h / 2;
        assert(center_y == slots[i].exp_r03_center_y);
        float visual_center_y = 0.5f * (tl[1] + br[1]);
        assert(fabsf((float)center_y - visual_center_y) <= 1.0f);

        // parts_update_global_pos_mat and parts_update_component_mat must agree
        parts_render_gen++;
        mat4 ident;
        glm_mat4_identity(ident);
        parts_update_global_pos_mat(&parent, (SDL_FPoint){0, 0}, ident);
        assert(approx(sprite.global.pos.x, slots[i].exp_r03_x, 0.01f));
        assert(approx(sprite.global.pos.y, slots[i].exp_r03_y, 0.01f));

        parts_render_gen++;
        parts_update_component_mat(&parent, ident);
        assert(approx(sprite.global.pos.x, slots[i].exp_r03_x, 0.01f));
        assert(approx(sprite.global.pos.y, slots[i].exp_r03_y, 0.01f));

        // 2) Hierarchical mode (v11+/v12+: Ixseal, Rance 10, Evenicle 2, Dohna Dohna)
        // Fresh parts start with committed_matrix = Identity until PE_UpdateComponent/UpdateMatrix runs.
        parts_hierarchical_matrix = true;
        glm_mat4_identity(parent.committed_matrix);
        glm_mat4_identity(offset_parent.committed_matrix);
        glm_mat4_identity(sprite.committed_matrix);

        parts_render_gen++;
        parts_world_matrix(&sprite, m);
        assert(approx(m[3][0], slots[i].exp_v12_x, 0.01f));
        assert(approx(m[3][1], slots[i].exp_v12_y, 0.01f));
        assert(approx(m[0][0], slots[i].scale, 0.0001f));
        assert(approx(m[1][1], slots[i].scale, 0.0001f));
        // Before PE_UpdateComponent, UpperLeftPos transforms hitbox by Identity.
        assert(PE_GetPartsUpperLeftPosX(3, 1) == 0);
        assert(PE_GetPartsUpperLeftPosY(3, 1) == -h);

        // SetPos / parts_update_global_pos_mat updates live global.pos without committing committed_matrix.
        parts_render_gen++;
        parts_update_global_pos_mat(&parent, (SDL_FPoint){0, 0}, ident);
        assert(approx(sprite.global.pos.x, slots[i].exp_v12_x, 0.01f));
        assert(approx(sprite.global.pos.y, slots[i].exp_v12_y, 0.01f));
        assert(PE_GetPartsUpperLeftPosX(3, 1) == 0);
        assert(PE_GetPartsUpperLeftPosY(3, 1) == -h);

        // After PE_UpdateComponent / UpdateMatrix, committed_matrix holds the hierarchical world matrix.
        parts_render_gen++;
        parts_update_component_mat(&parent, ident);
        assert(approx(sprite.global.pos.x, slots[i].exp_v12_x, 0.01f));
        assert(approx(sprite.global.pos.y, slots[i].exp_v12_y, 0.01f));
        assert(PE_GetPartsUpperLeftPosX(3, 1) == slots[i].exp_v12_ulx);
        assert(PE_GetPartsUpperLeftPosY(3, 1) == slots[i].exp_v12_uly);
    }
}

static void test_ixseal_full_length_create_three_enemies(void) {
    // Ixseal C_FULL_LENGTH@Create (FUNC[18827]) creates:
    //   enemy_slot (640/150/1130, 720) -> root_parts (-640, -720) -> area (0, 0)
    //     -> m_cRootParts (640, 740; 1280x740, origin=8, hitbox=(-640,-740))
    //     -> m_cFullLength
    // and sets m_cFullLength.local = m_cRootParts.GetUpperLeftPos() + (640, 740)
    // BEFORE PE_UpdateComponent(0) / PartsEngine.UpdateMatrix() runs.
    const float slot_xs[3] = { 640.0f, 150.0f, 1130.0f };
    parts_v2 = true;
    parts_hierarchical_matrix = true;
    root_pos = (Point){0, 0};

    mat4 ident;
    glm_mat4_identity(ident);

    for (int i = 0; i < 3; i++) {
        memset(parts_registry, 0, sizeof(parts_registry));
        struct parts enemy_slot, root_parts, area_parts, stand_root, full_length;
        init_part(&enemy_slot, 1, NULL);
        init_part(&root_parts, 2, &enemy_slot);
        init_part(&area_parts, 3, &root_parts);
        init_part(&stand_root, 4, &area_parts);
        init_part(&full_length, 5, &stand_root);

        enemy_slot.local.pos = (SDL_FPoint){slot_xs[i], 720.0f};
        root_parts.local.pos = (SDL_FPoint){-640.0f, -720.0f};
        area_parts.local.pos = (SDL_FPoint){0.0f, 0.0f};
        stand_root.local.pos = (SDL_FPoint){640.0f, 740.0f};
        stand_root.states[0].common.w = 1280;
        stand_root.states[0].common.h = 740;
        stand_root.states[0].common.origin_offset = (Point){-640, -740};
        stand_root.states[0].common.hitbox = (Rectangle){-640, -740, 1280, 740};

        // Simulate SetPos calls prior to PE_UpdateComponent(0).
        parts_render_gen++;
        parts_update_global_pos_mat(&enemy_slot, (SDL_FPoint){0, 0}, ident);

        // Before PE_UpdateComponent(0), GetUpperLeftPos transforms hitbox=(-640,-740) by Identity.
        int ulx = PE_GetPartsUpperLeftPosX(4, 1);
        int uly = PE_GetPartsUpperLeftPosY(4, 1);
        assert(ulx == -640);
        assert(uly == -740);

        // C_FULL_LENGTH@Create adds (640, 740), placing m_cFullLength at local (0, 0).
        full_length.local.pos = (SDL_FPoint){(float)(ulx + 640), (float)(uly + 740)};
        assert(approx(full_length.local.pos.x, 0.0f, 0.001f));
        assert(approx(full_length.local.pos.y, 0.0f, 0.001f));

        // Now run PE_UpdateComponent(0) / PartsEngine.UpdateMatrix().
        parts_render_gen++;
        parts_update_component_mat(&enemy_slot, ident);

        // All 3 enemies must land on screen at (640, 740), (150, 740), (1130, 740), never Y=1480.
        assert(approx(full_length.global.pos.x, slot_xs[i], 0.01f));
        assert(approx(full_length.global.pos.y, 740.0f, 0.01f));
        mat4 m;
        parts_world_matrix(&full_length, m);
        assert(approx(m[3][0], slot_xs[i], 0.01f));
        assert(approx(m[3][1], 740.0f, 0.01f));

        // After PE_UpdateComponent(0), stand_root's committed_matrix matches its world transform.
        assert(PE_GetPartsUpperLeftPosX(4, 1) == (int)slot_xs[i] - 640);
        assert(PE_GetPartsUpperLeftPosY(4, 1) == 0);
    }
}

static void test_layout_free_scroll_links_reverse_rotation_root_pos(void) {
    memset(parts_registry, 0, sizeof(parts_registry));
    struct parts parent, child, hscroll, vscroll;
    struct parts_scrollbar hbar = { .pos = 12.5f };
    struct parts_scrollbar vbar = { .pos = 7.25f };
    init_part(&parent, 1, NULL);
    init_part(&child, 2, &parent);
    init_part(&hscroll, 3, NULL);
    init_part(&vscroll, 4, NULL);
    hscroll.scrollbar = &hbar;
    vscroll.scrollbar = &vbar;

    // Parent is a LAYOUT_FREE box with origin_offset=(-20, -10), scale=(2, 0.5), reverse_lr=true
    parent.states[0].type = PARTS_LAYOUT_BOX;
    parent.states[0].layout_box.layout_type = PARTS_LAYOUT_FREE;
    parent.states[0].common.origin_offset = (Point){-20, -10};
    parent.local.pos = (SDL_FPoint){100.0f, 50.0f};
    parent.local.scale = (SDL_FPoint){2.0f, 0.5f};
    parent.reverse_lr = true;

    child.local.pos = (SDL_FPoint){40.0f, 30.0f};
    child.local.scale = (SDL_FPoint){1.5f, 2.0f};
    child.reverse_tb = true;
    child.scroll_links[0] = 3;
    child.scroll_links[1] = 4;

    // Effective child (tx, ty) = (-20 + 40 - 12.5, -10 + 30 - 7.25) = (7.5, 12.75)
    root_pos = (Point){5, -3};
    parts_v2 = true;

    // Non-hierarchical mode: translation is purely additive; axes still compose parent*child
    parts_hierarchical_matrix = false;
    parts_render_gen++;
    mat4 m;
    parts_world_matrix(&child, m);
    assert(approx(m[3][0], 5.0f + 100.0f + 7.5f, 0.001f));
    assert(approx(m[3][1], -3.0f + 50.0f + 12.75f, 0.001f));
    // scale_x = (2.0 * -1) * 1.5 = -3.0; scale_y = 0.5 * (2.0 * -1) = -1.0
    assert(approx(m[0][0], -3.0f, 0.001f));
    assert(approx(m[1][1], -1.0f, 0.001f));

    // Hierarchical mode: child (tx, ty) transformed by parent's (-2.0, 0.5)
    parts_hierarchical_matrix = true;
    parts_render_gen++;
    parts_world_matrix(&child, m);
    assert(approx(m[3][0], 5.0f + 100.0f + (-2.0f) * 7.5f, 0.001f));
    assert(approx(m[3][1], -3.0f + 50.0f + 0.5f * 12.75f, 0.001f));
    assert(approx(m[0][0], -3.0f, 0.001f));
    assert(approx(m[1][1], -1.0f, 0.001f));

    // Rotation test: parent rotated 90 deg around Z, child offset (100, 0) + child rot 30/20/10
    root_pos = (Point){0, 0};
    memset(parts_registry, 0, sizeof(parts_registry));
    init_part(&parent, 1, NULL);
    init_part(&child, 2, &parent);
    parent.local.pos = (SDL_FPoint){200.0f, 300.0f};
    parent.local.rotation.z = 90.0f;
    parent.local.scale = (SDL_FPoint){2.0f, 2.0f};
    child.local.pos = (SDL_FPoint){100.0f, 0.0f};
    child.local.rotation.z = 15.0f;
    child.local.rotation.x = 20.0f;
    child.local.rotation.y = 10.0f;
    child.local.scale = (SDL_FPoint){0.5f, 0.5f};
    child.reverse_lr = true;

    // Non-hierarchical: child translation stays additive (300, 300), linear part still composes
    parts_hierarchical_matrix = false;
    parts_render_gen++;
    parts_world_matrix(&child, m);
    assert(approx(m[3][0], 300.0f, 0.001f));
    assert(approx(m[3][1], 300.0f, 0.001f));
    assert(approx(m[3][2], 0.0f, 0.001f));

    // Compare linear columns 0..2 against reference composition
    mat4 ref;
    glm_mat4_identity(ref);
    glm_rotate_z(ref, glm_rad(90.0f), ref);
    glm_scale(ref, (vec3){2.0f, 2.0f, 1.0f});
    glm_rotate_z(ref, glm_rad(15.0f), ref);
    glm_rotate_x(ref, glm_rad(-20.0f), ref);
    glm_rotate_y(ref, glm_rad(10.0f), ref);
    glm_scale(ref, (vec3){-0.5f, 0.5f, 1.0f});
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 4; r++)
            assert(approx(m[c][r], ref[c][r], 0.0001f));

    // Hierarchical: child translation (100, 0) rotated 90 deg and scaled by 2 => (0, 200) -> (200, 500)
    parts_hierarchical_matrix = true;
    parts_render_gen++;
    parts_world_matrix(&child, m);
    assert(approx(m[3][0], 200.0f, 0.01f));
    assert(approx(m[3][1], 500.0f, 0.01f));
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 4; r++)
            assert(approx(m[c][r], ref[c][r], 0.0001f));
}

int main(void) {
    test_monster_stand_rance03_vs_hierarchical();
    test_ixseal_full_length_create_three_enemies();
    test_layout_free_scroll_links_reverse_rotation_root_pos();
    puts("parts-transform-ok");
    return 0;
}
'''
        out = compile_and_run(code)
        self.assertEqual(out.strip(), "parts-transform-ok")


if __name__ == "__main__":
    unittest.main()
