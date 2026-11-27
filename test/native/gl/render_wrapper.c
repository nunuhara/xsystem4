/* Include the production TU so the test can call its file-local CG renderer. */
#include "../../../src/parts/render.c"
void test_render_cg(struct parts *p, struct parts_common *c) { parts_render_cg(p, c); }
void test_render_flat(struct parts *p, struct parts_flat *f) { parts_render_flat(p, f); }
/* No alpha-clipper is part of this fixture; fail loudly if that changes. */
bool __wrap_parts_build_construction_process(struct parts *p, struct parts_construction_process *cp)
{
    (void)p; (void)cp;
    abort();
}
