#!/usr/bin/env python3
"""Compile DrawDungeon's walk data (de)serialization against original-engine layouts.

Rance VI reported "歩きデータ読み込みに失敗しました Map = 2": DrawDungeon.dll leaves
unused padding after the last map, new maps were appended after that padding,
and the next LoadWalkData could not parse them.

Run: python3 xsystem4/test/native/test_rance06_walk_data_regressions.py
"""
import unittest

from test_vm_reign_regressions import compile_and_run, extract_function

PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ARRAY_PAGE = 1 };
enum ain_data_type { AIN_ARRAY_INT = 1 };
union vm_value { int32_t i; float f; };
struct page {
    int type;
    enum ain_data_type a_type;
    struct { int rank; } array;
    int nr_vars;
    union vm_value values[];
};
struct dgn_cell { int x, y, z, walked; };
struct dgn { uint32_t size_x, size_y, size_z; struct dgn_cell *cells; };
struct dungeon_context { struct dgn *dgn; int revealed; };
enum { WALK_DATA_NOT_FOUND = -1, WALK_DATA_BROKEN = -2 };

#define WARNING(...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
#define VM_ERROR(...) (fprintf(stderr, __VA_ARGS__), abort())

static int dgn_nr_cells(struct dgn *dgn) { return dgn->size_x * dgn->size_y * dgn->size_z; }
static void dungeon_map_reveal(struct dungeon_context *ctx, int x, int y, int z, bool b)
{
    (void)x; (void)y; (void)z; (void)b;
    ctx->revealed++;
}
static struct page *realloc_array(struct page *src, int rank, union vm_value *dim,
                                  enum ain_data_type type, int struct_type, bool init)
{
    (void)struct_type; (void)init;
    int old = src ? src->nr_vars : 0;
    struct page *p = realloc(src, sizeof(struct page) + sizeof(union vm_value) * dim->i);
    for (int i = old; i < dim->i; i++)
        p->values[i].i = 0;
    p->type = ARRAY_PAGE;
    p->a_type = type;
    p->array.rank = rank;
    p->nr_vars = dim->i;
    return p;
}
'''

HARNESS = r'''
#define SX 2
#define SY 1
#define SZ 2
#define NC (SX * SY * SZ)

static struct dgn_cell cells[NC];
static struct dgn dgn = { SX, SY, SZ, cells };
static struct dungeon_context ctx = { &dgn, 0 };

static struct page *make(const int *v, int n)
{
    union vm_value dim = { .i = n };
    struct page *p = realloc_array(NULL, 1, &dim, AIN_ARRAY_INT, 0, false);
    for (int i = 0; i < n; i++)
        p->values[i].i = v[i];
    return p;
}

static void set_walked(int base)
{
    for (int i = 0; i < NC; i++)
        cells[i].walked = base + i;
}

static void expect_walked(int base)
{
    for (int i = 0; i < NC; i++)
        assert(cells[i].walked == base + i);
}

static void expect_loads(struct page **page, int map, int base)
{
    set_walked(-100);
    assert(dd1_load_walk_data(&ctx, map, page));
    expect_walked(base);
}

int main(void)
{
    // Original engine save: two maps followed by 9 unused ints per map.
    int original[2 + 2 * (4 + NC) + 18] = {
        0, 2,
        0, SX, SY, SZ, 10, 11, 12, 13,
        1, SX, SY, SZ, 20, 21, 22, 23,
    };
    struct page *page = make(original, sizeof(original) / sizeof(*original));
    set_walked(30);
    assert(dd1_save_walk_data(&ctx, 2, &page));
    assert(page->values[1].i == 3);
    assert(page->nr_vars == 2 + 3 * (4 + NC));
    expect_loads(&page, 2, 30);
    expect_loads(&page, 0, 10);
    expect_loads(&page, 1, 20);
    free(page);

    // No walk data yet: loading is fine and the first save writes a header.
    page = NULL;
    assert(dd1_load_walk_data(&ctx, 2, &page));
    set_walked(40);
    assert(dd1_save_walk_data(&ctx, 2, &page));
    assert(page->values[0].i == 0 && page->values[1].i == 1);
    expect_loads(&page, 2, 40);
    free(page);

    // Saved by older builds: map 2 appended after the padding is recovered.
    int legacy[2 + 2 * (4 + NC) + 18 + 4 + NC] = {
        0, 3,
        0, SX, SY, SZ, 10, 11, 12, 13,
        1, SX, SY, SZ, 20, 21, 22, 23,
    };
    int *tail = legacy + 2 + 2 * (4 + NC) + 18;
    int map2[4 + NC] = { 2, SX, SY, SZ, 30, 31, 32, 33 };
    memcpy(tail, map2, sizeof(map2));
    page = make(legacy, sizeof(legacy) / sizeof(*legacy));
    expect_loads(&page, 0, 10);
    expect_loads(&page, 2, 30);
    set_walked(50);
    assert(dd1_save_walk_data(&ctx, 2, &page));
    assert(page->values[1].i == 3);
    assert(page->nr_vars == 2 + 3 * (4 + NC));
    expect_loads(&page, 2, 50);
    expect_loads(&page, 1, 20);
    set_walked(70);
    assert(dd1_save_walk_data(&ctx, 3, &page));
    assert(page->values[1].i == 4);
    expect_loads(&page, 3, 70);
    expect_loads(&page, 2, 50);
    expect_loads(&page, 0, 10);
    free(page);

    // Count claims more maps than are stored: keep the intact ones.
    int truncated[2 + 2 * (4 + NC) + 18] = {
        0, 3,
        0, SX, SY, SZ, 10, 11, 12, 13,
        1, SX, SY, SZ, 20, 21, 22, 23,
    };
    page = make(truncated, sizeof(truncated) / sizeof(*truncated));
    set_walked(-100);
    assert(dd1_load_walk_data(&ctx, 2, &page));
    expect_walked(-100);
    set_walked(80);
    assert(dd1_save_walk_data(&ctx, 2, &page));
    assert(page->values[1].i == 3);
    expect_loads(&page, 2, 80);
    expect_loads(&page, 1, 20);
    free(page);

    // Unparseable header: start over.
    int garbage[] = { 7, 7, 7 };
    page = make(garbage, 3);
    assert(dd1_load_walk_data(&ctx, 2, &page));
    set_walked(60);
    assert(dd1_save_walk_data(&ctx, 2, &page));
    assert(page->nr_vars == 2 + 4 + NC && page->values[1].i == 1);
    expect_loads(&page, 2, 60);
    free(page);
    return 0;
}
'''


class WalkDataTests(unittest.TestCase):
    def test_walk_data_round_trip(self):
        functions = "\n".join(
            extract_function("src/dungeon/dungeon.c", name)
            for name in (
                "dd1_walk_record_cells",
                "dd1_fix_legacy_walk_data",
                "dd1_find_walk_data",
                "dd1_load_walk_data",
                "dd1_save_walk_data",
            )
        )
        compile_and_run(PRELUDE + functions + HARNESS)


if __name__ == "__main__":
    unittest.main()
