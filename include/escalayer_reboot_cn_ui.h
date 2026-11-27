#ifndef ESCALAYER_REBOOT_CN_UI_H
#define ESCALAYER_REBOOT_CN_UI_H

struct string;

/* Exact-match replacement of leftover SJIS *display* strings with GBK Chinese.
 * Returns the number of string-table slots rewritten. Does not modify
 * resource/archive keys or already-translated GBK text.
 */
int escalayer_reboot_replace_cn_ui_text(struct string **strings, int nr_strings);
int escalayer_reboot_cn_ui_pair_count(void);
const char *escalayer_reboot_cn_ui_sjis(int i);
const char *escalayer_reboot_cn_ui_gbk(int i);

#endif
