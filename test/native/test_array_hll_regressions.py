#!/usr/bin/env python3
"""Host regression tests for the v12/v14 Array HLL argument ABI.

Runs the production Array dispatcher and reference-element insertion helpers
with a small heap fixture; no game files or graphics context are required.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_vm_reign_regressions import extract_function

ROOT = Path(__file__).resolve().parents[2]

FIXTURE = r'''
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "vm/v12.h"

static struct ain program = {.version = 14};
struct ain *ain = &program;
static struct vm_pointer slots[64];
struct vm_pointer *heap = slots;
size_t heap_size = 64;
static union vm_value values[128];
union vm_value *stack = values;
int32_t stack_ptr;
static int warnings;

_Noreturn void _vm_error(const char *fmt, ...) { (void)fmt; abort(); }
void sys_warning(const char *fmt, ...) { (void)fmt; warnings++; }
void *_xmalloc(size_t n, const char *f) { (void)f; return malloc(n); }
void *_xcalloc(size_t n, size_t s, const char *f) { (void)f; return calloc(n, s); }
union vm_value stack_pop(void) { assert(stack_ptr > 0); return stack[--stack_ptr]; }
bool page_index_valid(int i) { return i >= 0 && i < 64 && heap[i].ref > 0; }
bool string_index_valid(int i) { (void)i; return false; }
struct page *heap_get_page(int i) { assert(page_index_valid(i)); return heap[i].page; }
void heap_set_page(int i, struct page *p) { assert(page_index_valid(i)); heap[i].page = p; }
void heap_ref(int i) { if (i >= 0) { assert(page_index_valid(i)); heap[i].ref++; } }
void heap_unref(int i) { if (i >= 0) { assert(page_index_valid(i)); heap[i].ref--; } }
struct page *alloc_page(enum page_type t, int index, int n) {
    struct page *p = calloc(1, sizeof(*p) + n * sizeof(union vm_value));
    p->type = t; p->index = index; p->nr_vars = n; return p;
}
void free_page(struct page *p) { free(p); }
int32_t heap_alloc_page(struct page *p) { (void)p; abort(); }
void heap_struct_assign(int dst, int src) { (void)dst; (void)src; abort(); }
int alloc_struct(int t) { (void)t; abort(); }
void init_struct(int t, int slot) { (void)t; (void)slot; abort(); }
void create_struct(int t, union vm_value *v) { (void)t; (void)v; abort(); }
int variable_init_typed(union vm_value *v, const struct ain_type *t, bool a, bool b) {
    (void)a; (void)b;
    assert(t->data == AIN_REF_INT);
    v[0].i = -1; v[1].i = 0; return 2;
}
union vm_value vm_copy(union vm_value v, enum ain_data_type t) {
    assert(t == AIN_REF_INT); heap_ref(v.i); return v;
}
void variable_fini(union vm_value v, enum ain_data_type t, bool dtor) {
    assert(t == AIN_REF_INT); assert(dtor); heap_unref(v.i);
}
void vm_call(int f, int obj) { (void)f; (void)obj; abort(); }
unsigned int SDL_GetTicks(void) { abort(); }
static void alloc_dimensions(int s, const int *d, int n) { (void)s; (void)d; (void)n; abort(); }
static int predicate(int s, int i, union vm_value *cb) { (void)s; (void)i; (void)cb; abort(); }
static bool binary_predicate2(int s, int i, int t, int j, union vm_value *cb) {
    (void)s; (void)i; (void)t; (void)j; (void)cb; abort();
}
static bool binary_predicate(int s, int i, int j, union vm_value *cb) {
    (void)s; (void)i; (void)j; (void)cb; abort();
}
static int compare_element(int s, int i, union vm_value *v) { (void)s; (void)i; (void)v; abort(); }
static bool equal_element(int s, int i, union vm_value *v) { (void)s; (void)i; (void)v; abort(); }
static void insert_element(int s, int i, union vm_value *v) { (void)s; (void)i; (void)v; abort(); }
static void erase_elements_range(int s, int i, int n) { (void)s; (void)i; (void)n; abort(); }
static void erase_element(int s, int i) { (void)s; (void)i; abort(); }
static void sort_array(int s, union vm_value *cb, bool d) { (void)s; (void)cb; (void)d; abort(); }
struct shuffle_random { int unused; };
static void shuffle_random_init(struct shuffle_random *r, unsigned int s) { (void)r; (void)s; abort(); }
static unsigned int shuffle_random_next(struct shuffle_random *r) { (void)r; abort(); }
'''

TEST = r'''
static void widths(void) {
    const int params[] = {1, 2, 3, 0x10001, 0x10002, 0x10003,
                         0x20001, 0x20002, 0x20003};
    const int expected[] = {1, 1, 2, 2, 1, 2, 2, 2, 3};
    for (unsigned int i = 0; i < sizeof(params) / sizeof(*params); i++)
        assert(array_hll_param_width(params[i]) == expected[i]);
    program.version = 12;
    assert(array_hll_param_width(AIN_INT) == 1);
    assert(array_hll_param_width(AIN_STRUCT) == 1);
    assert(array_hll_param_width(AIN_IFACE) == 2);
    program.version = 14;
}

int main(void) {
    widths();
    struct ain_type ref = {.data = AIN_REF_INT};
    struct ain_hll_argument args[] = {
        {.type = {.data = AIN_REF_ARRAY}}, {.type = {.data = AIN_HLL_PARAM}}
    };
    struct ain_hll_function push = {.name = "PushBack", .nr_arguments = 2, .arguments = args};
    struct ain_hll_function numof = {.name = "Numof", .nr_arguments = 1, .arguments = args};
    heap[1] = (struct vm_pointer){.ref = 1, .page = alloc_page(STRUCT_PAGE, 0, 40)};
    heap[2] = (struct vm_pointer){.ref = 1, .page = v12_array_new(&ref, 0, false)};

    stack_push(123); stack_push(456);
    stack_push(2);
    assert(array_call(&numof, 0x10001));
    assert(stack_pop().i == 0 && stack_ptr == 2);

    // Same three-word calls as the status-recovery lists in Evenicle 2.
    for (int i = 0; i < 10; i++) {
        stack_push(2); stack_push(1); stack_push(20 + 2 * i);
        assert(array_call(&push, 0x10001));
        assert(stack_ptr == 2 && warnings == 0);
        assert(v12_array_length(heap[2].page) == i + 1);
        assert(heap[2].page->values[2 * i].i == 1);
        assert(heap[2].page->values[2 * i + 1].i == 20 + 2 * i);
        assert(heap[1].ref == i + 2);
    }
    // An invalid receiver still consumes the complete borrowed reference.
    stack_push(-1); stack_push(1); stack_push(20);
    assert(array_call(&push, 0x10001));
    assert(stack_ptr == 2 && heap[1].ref == 11);
    stack_push(1); stack_push(1); stack_push(20);
    assert(array_call(&push, 0x10001));
    assert(stack_ptr == 2 && warnings == 1 && heap[1].ref == 11);

    resize_array(2, 0);
    assert(heap[1].ref == 1 && v12_array_length(heap[2].page) == 0);
    assert(stack_pop().i == 456 && stack_pop().i == 123 && stack_ptr == 0);
    free_page(heap[2].page); free_page(heap[1].page);
    puts("Array HLL: scalar-reference pairs, stack balance, empty/invalid arrays and ownership: OK");
    return 0;
}
'''


class ArrayHllRegressionTests(unittest.TestCase):
    def test_scalar_reference_arguments(self):
        source = FIXTURE
        for path, names in [
            ("src/page.c", ["ain_type_slot_width", "elem_value_copy", "elem_value_fini", "array_type"]),
            ("src/vm_v12.c", ["type_stride", "element_type", "v12_array_length",
                              "v12_array_new", "resize_array_mode", "resize_array",
                              "set_element", "elem_ref_is_two_slot", "return_element",
                              "array_hll_param_width", "array_hll_ref_ret_is_two_slot",
                              "cleanup_array_value_args", "array_call_invalid_slot",
                              "is_hll_func_type", "array_call"]),
        ]:
            source += "\n".join(extract_function(path, name) for name in names)
        source += TEST
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp) / "test.c", Path(tmp) / "test"
            src.write_text(source)
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
                f"-I{ROOT / 'include'}", f"-I{ROOT / 'subprojects/libsys4/include'}",
                str(src), "-o", str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
