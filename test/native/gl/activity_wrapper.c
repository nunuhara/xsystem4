#include <math.h>
#include "../../../src/parts/activity.c"

// Fixture keys are UTF-8, not game-encoded SJIS/GBK. Encoding conversion is not
// under test; EX tree lookup, numeric/list decoding and scale application are.
const char *parts_utf8_to_cjk_literal(const char *key) { return key; }
void sys4_set_text_gbk(bool enabled) { (void)enabled; }

static struct string *fixture_name(const char *key)
{
    size_t n = strlen(key);
    struct string *s = malloc(sizeof(*s) + n + 1);
    s->size = n; s->ref = 1; s->cow = 0;
    memcpy(s->text, key, n + 1);
    return s;
}

void test_activity_scale(struct parts *p, bool lr, bool tb, float sx, float sy)
{
    struct ex_list_item mags[] = {
        {.value = {.type = EX_FLOAT, .f = sx}},
        {.value = {.type = EX_FLOAT, .f = sy}},
    };
    struct ex_list list = {.nr_items = 2, .items = mags};
    struct ex_tree leaves[] = {
        {.name = fixture_name("左右反转"), .is_leaf = true,
         .leaf.value = {.type = EX_INT, .i = lr}},
        {.name = fixture_name("上下反转"), .is_leaf = true,
         .leaf.value = {.type = EX_INT, .i = tb}},
        {.name = fixture_name("拡大縮小"), .is_leaf = true,
         .leaf.value = {.type = EX_LIST, .list = &list}},
    };
    struct ex_tree tree = {.nr_children = 3, .children = leaves};
    load_part_scale(p, &tree);
    for (int i = 0; i < 3; i++) free(leaves[i].name);
}

void parts_widget_get_size(struct parts *p, int *w, int *h)
{
    (void)p; (void)w; (void)h;
    sys_error("Unexpected widget in the CG activity fixture");
}
