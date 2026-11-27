#!/usr/bin/env python3
"""Native C regression tests for Wave 5 System 4.x ports:
- UIF uif_config.json character_substitution loading in libsys4
- Blade Briders detection & SystemService.GetSystemBufferName token
- MainEXFile empty .ex fallback (Haniwa Yami Kaihatsushitsu Petit)
- PartsEngine.UpdateGUIController export
"""
import unittest

from test_evenicle2_regressions import LIBSYS4_CORE, ROOT, compile_and_run
from test_vm_reign_regressions import extract_function


class Wave5PortsNativeTests(unittest.TestCase):
    def test_uif_config_character_substitution(self):
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/utfsjis.h"

int main(void) {
    // 1. Valid UIF JSON with UTF-8 BOM, nested objects, escaped quotes, and \uXXXX escapes
    // Source: '鵄'(0xe9f5), '関'(0x8ad6), '鍵'(0x8cae), '―'(0x815c)
    // Target: '瞪',        '关',         '键',         '—'(U+2014 via \u2014)
    const char *json =
        "\xef\xbb\xbf{\n"
        "  \"injector\": { \"enable\": false, \"note\": \"escaped \\\"quote\\\"\", \"arr\": [1, -2.5e3, null, true] },\n"
        "  \"character_substitution\": {\n"
        "    \"enable\": true,\n"
        "    \"source_characters\": \"\xe9\xb5\x84\xe9\x96\xa2\xe9\x8d\xb5\xe2\x80\x95\",\n"
        "    \"target_characters\": \"\xe7\x9e\xaa\xe5\x85\xb3\xe9\x94\xae\\u2014\"\n"
        "  }\n"
        "}\n";
    sys4_set_text_gbk(false);
    assert(sys4_load_uif_config_mem(json, strlen(json)) == true);
    assert(sys4_has_mai_word_table() == true);

    // Base SJIS decoding remains untouched for archive/EX lookups
    const char sjis_sample[] = { (char)0xe9, (char)0xf5, (char)0x8a, (char)0xd6, (char)0x8c, (char)0xae, (char)0x81, (char)0x5c, 0 };
    char *base_utf = sjis2utf(sjis_sample, 0);
    assert(strcmp(base_utf, "\xe9\xb5\x84\xe9\x96\xa2\xe9\x8d\xb5\xe2\x80\x95") == 0);
    free(base_utf);

    // MAI/UIF render decoding substitutes every configured glyph
    char *uif_utf = mai_sjis2utf(sjis_sample, 0);
    assert(strcmp(uif_utf, "\xe7\x9e\xaa\xe5\x85\xb3\xe9\x94\xae\xe2\x80\x94") == 0);

    // Reverse lookup round-trips cleanly back to the exact Shift-JIS byte pairs
    char *rt_sjis = mai_utf2sjis(uif_utf, 0);
    assert(strcmp(rt_sjis, sjis_sample) == 0);
    free(rt_sjis);
    free(uif_utf);

    // Single-char decode used by text.c
    int cp = 0;
    char *next = mai_sjis_char2unicode(sjis_sample, &cp);
    assert(next == sjis_sample + 2);
    assert(cp == 0x77aa); // '瞪'

    // 2. Disabled character_substitution returns false without corrupting state
    const char *disabled =
        "{\"character_substitution\":{\"enable\":false,\"source_characters\":\"\xe9\x96\xa2\",\"target_characters\":\"\xe5\x85\xb3\"}}";
    assert(sys4_load_uif_config_mem(disabled, strlen(disabled)) == false);

    // 3. Mismatched source/target lengths are rejected
    const char *mismatch =
        "{\"character_substitution\":{\"enable\":true,\"source_characters\":\"\xe9\x96\xa2\xe9\x8d\xb5\",\"target_characters\":\"\xe5\x85\xb3\"}}";
    assert(sys4_load_uif_config_mem(mismatch, strlen(mismatch)) == false);

    sys4_reset_mai_word_table();
    assert(sys4_has_mai_word_table() == false);
    return 0;
}
"""
        compile_and_run(harness, LIBSYS4_CORE, ["-lz", "-lm"])

    def test_bladebriders_hack_and_system_buffer_name(self):
        fn_set_out = extract_function("src/hll/SystemService.c", "set_out_cstr")
        fn_bufname = extract_function("src/hll/SystemService.c", "SystemService_GetSystemBufferName")

        harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xsystem4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm.h"

void apply_game_specific_hacks(struct ain *ain);
struct config config;
void message_layout_init_rance03(void) {{}}
_Noreturn void _vm_error(const char *fmt, ...) {{ abort(); }}

{fn_set_out}
{fn_bufname}

int main(void) {{
    struct ain *bb = ain_new(6, 0);
    ain_add_string(bb, "wf:-3");
    ain_add_struct(bb, "CPolygonUnitObject");
    ain_add_global(bb, "g_GrobalData");
    config.game_name = strdup("BladeBriders");
    apply_game_specific_hacks(bb);
    assert(game_bladebriders == true);

    struct string *out = make_string("", 0);
    SystemService_GetSystemBufferName(&out);
    assert(out->size >= 23);
    // Exact invariants checked by burebura.ain main():
    //   stoi(s[3]) - stoi(s[20]) == -1
    //   s[4]+s[18]+s[7]+s[12]+s[22] == "wf:-3"
    assert((out->text[3] - '0') - (out->text[20] - '0') == -1);
    char sig[6] = {{
        out->text[4], out->text[18], out->text[7], out->text[12], out->text[22], 0
    }};
    assert(strcmp(sig, "wf:-3") == 0);
    free_string(out);
    ain_free(bb);
    free(config.game_name);

    // Non-BladeBriders AIN v6 must not set game_bladebriders
    struct ain *other = ain_new(6, 0);
    ain_add_string(other, "hello");
    config.game_name = strdup("OtherGame");
    apply_game_specific_hacks(other);
    assert(game_bladebriders == false);
    ain_free(other);
    free(config.game_name);
    return 0;
}}
"""
        compile_and_run(
            harness,
            [ROOT / "src/hacks.c", ROOT / "src/escalayer_reboot_cn_ui.c", *LIBSYS4_CORE],
            ["-lz", "-lm"],
        )

    def test_parts_engine_update_gui_controller_and_empty_ex_fallback(self):
        pe_src = (ROOT / "src/hll/PartsEngine.c").read_text(encoding="utf-8")
        self.assertIn("HLL_EXPORT(UpdateGUIController, PE_UpdateInputState)", pe_src)
        ex_src = (ROOT / "src/hll/MainEXFile.c").read_text(encoding="utf-8")
        self.assertIn("if (!config.ex_path)", ex_src)
        self.assertIn("ex = xcalloc(1, sizeof(struct ex))", ex_src)


if __name__ == "__main__":
    unittest.main()
