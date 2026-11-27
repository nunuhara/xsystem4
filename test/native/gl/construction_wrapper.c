/* Include the production TU so the test can call its file-local filter. */
#include "../../../src/parts/construction.c"

bool sys4_text_is_gbk = false;
/* ex.c references these text helpers; the GL tests never call them. */
char *cjk2utf_strict_buf(const char *src, size_t len, bool gbk, char *buf, size_t buf_cap, bool *valid_out)
{ (void)src; (void)len; (void)gbk; (void)buf; (void)buf_cap; if (valid_out) *valid_out = false; return NULL; }
char *cjk_gbk_zenkaku_to_sjis_buf(const char *src, size_t len, char *buf, size_t cap, bool *changed_out)
{ (void)src; (void)len; (void)buf; (void)cap; if (changed_out) *changed_out = false; return NULL; }
struct cg *asset_cg_load_by_name(const char *name, int *id) { (void)name; (void)id; return NULL; }
bool asset_cg_supports_name(void) { return true; }
uint64_t asset_cg_archive_gen(void) { return 1; }
void cg_free(struct cg *cg) { (void)cg; }
struct string *string_dup(const struct string *in) { (void)in; return NULL; }
void free_string(struct string *s) { (void)s; }

bool test_blur(struct parts_construction_process *cp, struct parts_cp_filter *filter, bool vertical)
{
    return build_blur_filter(cp, filter, vertical);
}
bool test_gray(struct parts_construction_process *cp, struct parts_cp_filter *filter)
{
    return build_gray_filter(cp, filter);
}
bool test_v14(struct parts_construction_process *cp, struct parts_cp_v14 *op)
{
    return build_v14(cp, op);
}
