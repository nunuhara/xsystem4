#!/usr/bin/env python3
"""Native C regression tests for:
1. Rance 10 BFYY (全系列MOD) archive priority recognition in afa_is_mod_archive.
2. Heartful Maman (は～とふるママン) game detection & 1280x720 default viewport in hacks.c.
3. AIN v8 builtin HLL enablement (String/Int/Float), PartsEngine 5-arg bool IsSkip dispatch,
   Parts message_type_v8 + -1 empty-queue sentinel, and parts_v2 scrollbar no <= 0 guard.
4. Heartful Maman backlog text and voice HLL return signatures.
5. Heartful Maman mixed-encoding speaker labels.
6. Heartful Maman and Haha Ranman extra-save lookup keys.
"""
import unittest

from test_evenicle2_regressions import (
    LIBSYS4_CORE,
    ROOT,
    compile_and_run,
)


def _extract_def(rel_path: str, name: str) -> str:
    source = (ROOT / rel_path).read_text(encoding="utf-8", errors="ignore")
    pos = 0
    while True:
        idx = source.index(name + "(", pos)
        start = source.rfind("\n", 0, idx) + 1
        semi = source.find(";", idx)
        brace = source.find("{", idx)
        if brace != -1 and (semi == -1 or brace < semi):
            break
        pos = idx + len(name)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class MamanAndBfyyRegressionsTest(unittest.TestCase):
    def test_maman_cn_speaker_literals(self):
        harness = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "xsystem4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/instructions.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

void apply_game_specific_hacks(struct ain *ain);
struct config config;
bool message_layout_init_rance03(struct ain *ain) { return false; }
void escalayer_reboot_replace_cn_ui_text(struct ain *ain) {}

static size_t add_literal(struct ain *ain, struct buffer *code, const char *name, int literal)
{
    int fn = ain_add_function(ain, name);
    buffer_write_int16(code, FUNC);
    buffer_write_int32(code, fn);
    ain->functions[fn].address = code->index;
    size_t operand = code->index + 2;
    buffer_write_int16(code, S_PUSH);
    buffer_write_int32(code, literal);
    buffer_write_int16(code, ENDFUNC);
    buffer_write_int32(code, fn);
    return operand;
}

static void verify(bool maman, bool gbk)
{
    struct ain *ain = ain_new(8, 0);
    if (maman) {
        ain_add_struct(ain, "C_BOOT_WARNING");
        ain_add_struct(ain, "SaveData::C_PARAMETER");
    }
    config.game_name = gbk ? "\xb6\xe0\xc4\xc8" : NULL;
    const char *format = "\x81\x79%s\x81\x7a";
    const char *placeholder = "\x81\x96\x81\x96\x81\x96";
    int format_no = ain_add_string(ain, format);
    int placeholder_no = ain_add_string(ain, placeholder);
    struct buffer code;
    buffer_init(&code, NULL, 0);
    size_t name_op = add_literal(ain, &code, "MessageWindow::C_NAME_PLATE@Open", format_no);
    size_t scene_op = add_literal(ain, &code, "\x82\x64\x82\x75\x81\x5e" "scene", placeholder_no);
    size_t unrelated_op = add_literal(ain, &code, "OtherFunction", placeholder_no);
    ain->code = code.buf;
    ain->code_size = code.index;
    apply_game_specific_hacks(ain);
    assert(sys4_text_is_gbk == gbk);
    assert(!strcmp(ain->strings[LittleEndian_getDW(ain->code, name_op)]->text,
                   maman && gbk ? "\xa1\xbe%s\xa1\xbf" : format));
    assert(!strcmp(ain->strings[LittleEndian_getDW(ain->code, scene_op)]->text,
                   maman && gbk ? "\xa3\xaa\xa3\xaa\xa3\xaa" : placeholder));
    assert(LittleEndian_getDW(ain->code, unrelated_op) == placeholder_no);
    assert(!strcmp(ain->strings[placeholder_no]->text, placeholder));
    ain_free(ain);
}

int main(void)
{
    verify(true, true);
    verify(true, false);
    verify(false, true);
    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            [ROOT / "src/hacks.c", *LIBSYS4_CORE],
            ["-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-lz", "-lm"],
        )
        self.assertEqual(out.strip(), "OK")

    def test_haharanman_cn_name_plate(self):
        harness = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "xsystem4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/instructions.h"
#include "system4/little_endian.h"
#include "system4/string.h"

void apply_game_specific_hacks(struct ain *ain);
struct config config;
bool message_layout_init_rance03(struct ain *ain) { return false; }
void escalayer_reboot_replace_cn_ui_text(struct ain *ain) {}

static void instruction(struct buffer *code, int op, int arg)
{
    buffer_write_int16(code, op);
    buffer_write_int32(code, arg);
}

static void verify(bool game, bool gbk, bool real_getter)
{
    struct ain *ain = ain_new(14, 0);
    if (game)
        ain_add_string(ain, "83h2mjr");
    config.game_name = gbk ? "\xb6\xe0\xc4\xc8" : NULL;
    int cp932_ihisa = ain_add_string(ain, "\x88\xc9\x8d\xb2\x95\x90");
    int cp932_haru = ain_add_string(ain, "\x83\x6e\x83\x8b");
    int gbk_weina = ain_add_string(ain, "\xc6\xdf\xce\xb2");
    int fn = ain_add_function(ain, "AdvLayer::MessageWindow::nameplate::CText@Text::get");
    ain->functions[fn].return_type.data = AIN_STRING;
    struct buffer code;
    buffer_init(&code, NULL, 0);
    instruction(&code, FUNC, fn);
    unsigned start = code.index;
    ain->functions[fn].address = start;
    buffer_write_int16(&code, PUSHSTRUCTPAGE);
    instruction(&code, PUSH, real_getter ? 4 : 5);
    instruction(&code, X_REF, 1);
    buffer_write_int16(&code, A_REF);
    buffer_write_int16(&code, RETURN);
    instruction(&code, ENDFUNC, fn);
    ain->code = code.buf;
    ain->code_size = code.index;
    apply_game_specific_hacks(ain);
    unsigned addr = ain->functions[fn].address;
    assert(!strcmp(ain->strings[cp932_ihisa]->text, "\x88\xc9\x8d\xb2\x95\x90"));
    assert(!strcmp(ain->strings[gbk_weina]->text, "\xc6\xdf\xce\xb2"));
    if (game && gbk && real_getter) {
        assert(addr != start);
        assert(!memcmp(ain->code + start, ain->code + addr, 16));
        addr += 16;
        assert(LittleEndian_getW(ain->code, addr) == DUP);
        assert(LittleEndian_getW(ain->code, addr + 2) == PUSH);
        assert(LittleEndian_getDW(ain->code, addr + 4) == -1);
        assert(LittleEndian_getW(ain->code, addr + 8) == EQUALE);
        assert(LittleEndian_getW(ain->code, addr + 10) == IFNZ);
        unsigned null_branch = LittleEndian_getDW(ain->code, addr + 12);
        addr += 16;
        const int sources[] = { cp932_haru, cp932_ihisa };
        const char *targets[] = { "\xa5\xcf\xa5\xeb", "\xd2\xc1\xd7\xf4\xce\xe4" };
        for (int i = 0; i < 2; i++) {
            assert(LittleEndian_getW(ain->code, addr) == DUP);
            assert(LittleEndian_getW(ain->code, addr + 2) == DUP);
            assert(LittleEndian_getW(ain->code, addr + 4) == SP_INC);
            assert(LittleEndian_getW(ain->code, addr + 6) == S_PUSH);
            assert(LittleEndian_getDW(ain->code, addr + 8) == sources[i]);
            assert(LittleEndian_getW(ain->code, addr + 12) == S_EQUALE);
            assert(LittleEndian_getW(ain->code, addr + 14) == IFZ);
            assert(LittleEndian_getDW(ain->code, addr + 16) == addr + 30);
            assert(LittleEndian_getW(ain->code, addr + 20) == DELETE);
            assert(LittleEndian_getW(ain->code, addr + 22) == S_PUSH);
            assert(!strcmp(ain->strings[LittleEndian_getDW(ain->code, addr + 24)]->text, targets[i]));
            assert(LittleEndian_getW(ain->code, addr + 28) == RETURN);
            addr += 30;
        }
        assert(null_branch == addr);
        assert(LittleEndian_getW(ain->code, addr) == RETURN);
    } else {
        assert(addr == start);
    }
    ain_free(ain);
}

int main(void)
{
    verify(true, true, true);
    verify(true, false, true);
    verify(false, true, true);
    verify(true, true, false);
    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            [ROOT / "src/hacks.c", *LIBSYS4_CORE],
            ["-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-lz", "-lm"],
        )
        self.assertEqual(out.strip(), "OK")

    def test_maman_backlog_hll_string_returns(self):
        harness = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "AnteaterADVEngine.h"
#include "hll.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm/heap.h"
#include "vm/page.h"

struct ain *ain;
extern struct static_library lib_AnteaterADVEngine;
extern struct static_library lib_AnteaterADVLogList;

_Noreturn void _vm_error(const char *fmt, ...) { abort(); }
struct page *alloc_array(int r, union vm_value *d, enum ain_data_type t, int s, bool i) { abort(); }
struct page *alloc_page(enum page_type t, int i, int n) { abort(); }
enum ain_data_type array_type(enum ain_data_type t) { abort(); }
void delete_page_vars(struct page *p) { abort(); }
void free_page(struct page *p) { abort(); }
int32_t heap_alloc_page(struct page *p) { abort(); }
int32_t heap_alloc_string(struct string *s) { abort(); }
struct page *heap_get_page(int i) { abort(); }
struct string *heap_get_string(int i) { abort(); }

static struct static_hll_function *find_export(const char *name) {
    for (struct static_hll_function *f = lib_AnteaterADVEngine.functions; f->name; f++)
        if (!strcmp(f->name, name))
            return f;
    abort();
}

void static_library_replace(struct static_library *lib, const char *name, void *fun) {
    assert(lib == &lib_AnteaterADVEngine);
    find_export(name)->fun = fun;
}

int main(void) {
    struct ain_hll_argument window_args[] = {
        { .type.data = AIN_STRING }, { .type.data = AIN_STRING },
    };
    struct ain_hll_argument getter_args[] = {
        { .type.data = AIN_INT }, { .type.data = AIN_INT }, { .type.data = AIN_REF_STRING },
    };
    struct ain_hll_function functions[] = {
        { .name = "ADVLogList_AddText", .nr_arguments = 2, .arguments = window_args },
        { .name = "ADVLogList_AddVoice", .nr_arguments = 1, .arguments = window_args },
        { .name = "ADVLogList_GetADVLogText", .return_type.data = AIN_STRING,
          .nr_arguments = 2, .arguments = getter_args },
        { .name = "ADVLogList_GetADVLogVoice", .return_type.data = AIN_STRING,
          .nr_arguments = 2, .arguments = getter_args },
    };
    struct ain_library library = {
        .name = "AnteaterADVEngine", .nr_functions = 4, .functions = functions,
    };
    struct ain maman = { .version = 8, .nr_libraries = 1, .libraries = &library };
    ain = &maman;
    ((void (*)(void))find_export("_PreLink")->fun)();

    struct string *line = make_string("dialogue", 8);
    struct string *name = make_string("voice_01", 8);
    ((void (*)(struct string *, struct string *))find_export("ADVLogList_AddText")->fun)
        (line, &EMPTY_STRING);
    ((void (*)(struct string *))find_export("ADVLogList_AddVoice")->fun)(name);
    free_string(line);
    free_string(name);

    struct static_hll_function *text = find_export(functions[2].name);
    struct static_hll_function *voice = find_export(functions[3].name);
    assert(text->resolve && voice->resolve);
    struct string *(*get_text)(int, int) = text->resolve(&functions[2]);
    struct string *(*get_voice)(int, int) = voice->resolve(&functions[3]);
    line = get_text(0, 0);
    name = get_voice(0, 0);
    struct string *missing = get_voice(0, 1);
    assert(!missing->size);
    free_string(missing);

    struct ain_hll_function ref_string = {
        .nr_arguments = 3, .arguments = getter_args, .return_type.data = AIN_VOID,
    };
    void (*get_text_ref)(int, int, struct string **) = text->resolve(&ref_string);
    void (*get_voice_ref)(int, int, struct string **) = voice->resolve(&ref_string);
    struct string *ref = NULL;
    get_text_ref(0, 0, &ref);
    assert(ref == line);
    get_voice_ref(0, 0, &ref);
    assert(ref == name);
    free_string(ref);
    ((void (*)(void))find_export("ADVLogList_Clear")->fun)();
    assert(!strcmp(line->text, "dialogue"));
    assert(!strcmp(name->text, "voice_01"));
    free_string(line);
    free_string(name);

    const char mixed[] = "\x81\x79\xc4\xe3\xba\xc3\x81\x7a";
    const char expected[] = "\xa1\xbe\xc4\xe3\xba\xc3\xa1\xbf";
    line = make_string(mixed, sizeof(mixed) - 1);
    ((void (*)(struct string *, struct string *))find_export("ADVLogList_AddText")->fun)
        (line, &EMPTY_STRING);
    void (*add_list_text)(struct string *, struct string *) = NULL;
    struct string *(*get_list_text)(int, int) = NULL;
    void (*clear_list)(void) = NULL;
    for (struct static_hll_function *f = lib_AnteaterADVLogList.functions; f->name; f++) {
        if (!strcmp(f->name, "AddText")) add_list_text = f->fun;
        if (!strcmp(f->name, "GetADVLogText")) get_list_text = f->fun;
        if (!strcmp(f->name, "Clear")) clear_list = f->fun;
    }
    assert(add_list_text && get_list_text && clear_list);
    add_list_text(line, &EMPTY_STRING);
    struct string *display = get_text(0, 0);
    assert(!strcmp(display->text, mixed));
    free_string(display);
    sys4_set_text_gbk(true);
    display = get_text(0, 0);
    assert(!strcmp(display->text, expected));
    free_string(display);
    display = get_list_text(0, 0);
    assert(!strcmp(display->text, expected));
    free_string(display);
    sys4_set_source_utf8(true);
    display = get_text(0, 0);
    assert(!strcmp(display->text, mixed));
    free_string(display);
    display = get_list_text(0, 0);
    assert(!strcmp(display->text, mixed));
    free_string(display);
    sys4_set_source_utf8(false);
    free_string(line);
    clear_list();
    ((void (*)(void))find_export("ADVLogList_Clear")->fun)();

    const char sjis_speaker[] = "\x81\x79\x88\xc9\x8d\xb2\x95\x90\x81\x7a";
    const char gbk_speaker[] = "\xa1\xbe\xd2\xc1\xd7\xf4\xce\xe4\xa1\xbf";
    line = make_string(sjis_speaker, sizeof(sjis_speaker) - 1);
    add_list_text(line, &EMPTY_STRING);
    ((void (*)(struct string *, struct string *))find_export("ADVLogList_AddText")->fun)
        (line, &EMPTY_STRING);
    display = get_list_text(0, 0);
    assert(!strcmp(display->text, gbk_speaker));
    free_string(display);
    display = get_text(0, 0);
    assert(!strcmp(display->text, gbk_speaker));
    free_string(display);
    sys4_set_text_gbk(false);
    display = get_list_text(0, 0);
    assert(!strcmp(display->text, sjis_speaker));
    free_string(display);
    free_string(line);
    clear_list();
    ((void (*)(void))find_export("ADVLogList_Clear")->fun)();

    struct ain_hll_function int_voice = {
        .nr_arguments = 2, .arguments = getter_args, .return_type.data = AIN_INT,
    };
    int (*get_int_voice)(int, int) = voice->resolve(&int_voice);
    ADVLogList_AddVoice(42);
    assert(get_int_voice(0, 0) == 42);
    ADVLogList_Clear();
    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            [ROOT / "src/hll/AnteaterADVEngine.c", ROOT / "src/hll/AnteaterADVLogList.c",
             ROOT / "src/hll/iarray.c", *LIBSYS4_CORE],
            ["-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-lz", "-lm"],
        )
        self.assertEqual(out.strip(), "OK")

    def test_mixed_encoding_extra_saves_preserve_lookup_keys(self):
        harness = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "system4/ain.h"
#include "savedata.h"
#include "system4/savefile.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

bool game_maman, game_haharanman;
bool game_escalayer_reboot, game_pascha3, game_night_remaster, game_rance01, game_haruka;
int load_globals_from_gsave(struct gsave *save, const char *key, const char *group, int *count);
struct ain *ain;
union vm_value *stack;
int32_t stack_ptr;

union vm_value global_get(int i) { return heap[0].page->values[i]; }
void global_set(int i, union vm_value val, bool dtors) { heap[0].page->values[i] = val; }
union vm_value stack_pop(void) { abort(); }
union vm_value vm_copy(union vm_value val, enum ain_data_type type) { abort(); }
void vm_call(int fno, int slot) { abort(); }

static void check_group(bool modern, char *save_key, char *group, bool expected_match)
{
    const char key[] = "\x83\x6c\x83\x62\x83\x67\x83\x8f\x81\x5b\x83\x4e"
                       "\x90\xda\x91\xb1\x81\x5e\x8f\x89\x89\xf1";
    char *groups[] = { group };
    struct ain_type string_type = { .data = AIN_STRING };
    struct ain_variable member = {
        .name = modern ? "m_atList" : "m_aszList",
        .type = {
            .data = modern ? AIN_ARRAY : AIN_ARRAY_STRING, .rank = 1,
            .array_type = modern ? &string_type : NULL,
        },
    };
    struct ain_struct structure = {
        .name = modern ? "key<string>" : "C_KEY_LIST",
        .constructor = -1, .destructor = -1, .nr_members = 1, .members = &member,
    };
    struct ain_variable global = {
        .name = modern ? "g_tGlobalKeyList" : "g_cGlobalKeyList",
        .group_index = 0, .type = { .data = AIN_STRUCT, .struc = 0 },
    };
    struct ain script = {
        .version = modern ? 14 : 8, .nr_globals = 1, .globals = &global,
        .nr_structures = 1, .structures = &structure,
        .nr_global_groups = 1, .global_group_names = groups,
    };
    ain = &script;

    struct gsave *save = gsave_create(modern ? 8 : 7, save_key, 1, group);
    struct string *literal = make_string(key, strlen(key));
    int slot = heap_alloc_slot(VM_STRING);
    heap[slot].s = literal;
    struct gsave_array_value *value = xmalloc(sizeof(*value));
    *value = (struct gsave_array_value) {
        .type = AIN_STRING, .value = gsave_add_string(save, literal),
    };
    int32_t *dimension = xmalloc(sizeof(*dimension));
    *dimension = 1;
    struct gsave_flat_array *flat = xmalloc(sizeof(*flat));
    *flat = (struct gsave_flat_array) { .type = AIN_STRING, .nr_values = 1, .values = value };
    struct gsave_array array = {
        .rank = 1, .dimensions = dimension, .nr_flat_arrays = 1, .flat_arrays = flat,
    };
    gsave_add_globals_record(save, 1);
    struct gsave_keyval field = {
        .name = xstrdup(member.name), .type = member.type.data, .value = gsave_add_array(save, &array),
    };
    struct gsave_record record = {
        .type = GSAVE_RECORD_STRUCT, .nr_indices = 1, .indices = xmalloc(sizeof(*record.indices)),
        .struct_index = gsave_add_struct_def(save, &structure),
    };
    record.indices[0] = gsave_add_keyval(save, &field);
    save->globals[0] = (struct gsave_global) {
        .name = xstrdup(global.name), .type = global.type.data, .value = gsave_add_record(save, &record),
    };

    int count = 0;
    assert(load_globals_from_gsave(save, save_key, group, &count) && count == 1);
    struct page *list = heap_get_page(heap_get_page(global_get(0).i)->values[0].i);
    assert((array_find(list, 0, 1, vm_int(slot), 0, -1) == 0) == expected_match);
    heap_unref(global_get(0).i);
    heap_unref(slot);
    gsave_free(save);
}

int main(void)
{
    sys4_set_text_gbk(true);
    heap_init();
    heap[0].ref = 1;
    heap_set_page(0, alloc_page(GLOBAL_PAGE, 0, 1));

    game_maman = true;
    check_group(false, "MAMAN_EXTRA", "MAMAN_EXTRA_DATA", true);
    game_maman = false;
    check_group(false, "MAMAN_EXTRA", "MAMAN_EXTRA_DATA", false);
    game_haharanman = true;
    check_group(true, "MIBOUJIN_EXTRA", "MIBOUJIN_EXTRA_DATA", true);
    check_group(true, "MIBOUJIN_USER", "MIBOUJIN_USER_DATA", true);
    check_group(true, "MIBOUJIN_ENQUETE", "MIBOUJIN_ENQUETE_DATA", false);
    game_haharanman = false;
    check_group(true, "TYOUKOU3_EXTRA", "TYOUKOU3_EXTRA_DATA", false);
    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            [ROOT / "src/savedata.c", ROOT / "src/util.c", ROOT / "src/heap.c", ROOT / "src/page.c", ROOT / "src/vm_v12.c",
             ROOT / "subprojects/libsys4/src/savefile.c", *LIBSYS4_CORE],
            ["-DRELEASE", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-lz", "-lm"],
        )
        self.assertEqual(out.strip(), "OK")

    def test_bfyy_mod_archive_and_maman_hacks(self):
        afa_fn = _extract_def("src/asset_manager.c", "afa_is_mod_archive")
        harness = f"""
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xsystem4.h"
#include "system4/ain.h"
#include "system4/string.h"

void apply_game_specific_hacks(struct ain *ain);
struct config config;
void message_layout_init_rance03(void) {{}}
void escalayer_reboot_replace_cn_ui_text(struct ain *ain) {{ (void)ain; }}
_Noreturn void _vm_error(const char *fmt, ...) {{ abort(); }}

{afa_fn}

int main(void) {{
    // 1. BFYY and Ribuluo MOD .afa archives must be recognized as MOD priority.
    assert(afa_is_mod_archive("Rance10CGBFYY.afa"));
    assert(afa_is_mod_archive("/data/Rance10CGBFYY2.afa"));
    assert(afa_is_mod_archive("Rance10FlatBFYY.afa"));
    assert(afa_is_mod_archive("rance10cgbfyy3.afa"));
    assert(afa_is_mod_archive("Rance10MODCG.afa"));
    assert(afa_is_mod_archive("Rance10ModFlat.afa"));
    assert(!afa_is_mod_archive("Rance10CG.afa"));
    assert(!afa_is_mod_archive("Rance10Flat.afa"));
    assert(!afa_is_mod_archive("MamanCG.afa"));
    assert(!afa_is_mod_archive("MamanFlat.afa"));

    // 2. Heartful Maman AIN v8 detection via structs sets game_maman and 1280x720 default viewport.
    struct ain *maman_ain = ain_new(8, 0);
    ain_add_struct(maman_ain, "C_BOOT_WARNING");
    ain_add_struct(maman_ain, "SaveData::C_PARAMETER");
    memset(&config, 0, sizeof(config));
    config.view_width = 800;
    config.view_height = 600;
    apply_game_specific_hacks(maman_ain);
    assert(game_maman == true);
    assert(config.view_width == 1280 && config.view_height == 720);
    ain_free(maman_ain);

    // 3. Filename fallback when structs differ still detects Maman.ain on AIN v8.
    struct ain *maman_ain2 = ain_new(8, 0);
    memset(&config, 0, sizeof(config));
    config.view_width = 800;
    config.view_height = 600;
    config.ain_filename = strdup("/games/Maman/Maman.ain");
    apply_game_specific_hacks(maman_ain2);
    assert(game_maman == true);
    assert(config.view_width == 1280 && config.view_height == 720);
    free(config.ain_filename);
    ain_free(maman_ain2);

    // 4. Non-Maman AIN v8 does not falsely match game_maman.
    struct ain *other_ain = ain_new(8, 0);
    memset(&config, 0, sizeof(config));
    config.view_width = 800;
    config.view_height = 600;
    config.ain_filename = strdup("/games/Other/Other.ain");
    apply_game_specific_hacks(other_ain);
    assert(game_maman == false);
    assert(config.view_width == 800 && config.view_height == 600);
    free(config.ain_filename);
    ain_free(other_ain);

    puts("OK");
    return 0;
}}
"""
        out = compile_and_run(
            harness,
            [ROOT / "src/hacks.c", *LIBSYS4_CORE],
            ["-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-lz", "-lm"],
        )
        self.assertEqual(out.strip(), "OK")

    def test_ain_v8_hll_message_and_parts_update_dispatch(self):
        v12_builtin_fn = _extract_def("src/vm_v12.c", "v12_builtin_library")
        parts_engine_src = (ROOT / "src/hll/PartsEngine.c").read_text(encoding="utf-8", errors="ignore")
        self.assertIn("PartsEngine_Update_v8", parts_engine_src)
        self.assertIn("fun->arguments[1].type.data == AIN_BOOL", parts_engine_src)

        harness = f"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "parts.h"
#include "parts_internal.h"
#include "vm.h"

struct ain *ain = NULL;
bool parts_v2 = true;
bool parts_multi_controller = true;
bool game_tsumamigui3_jun = false;
bool game_tsumamigui3_ntr = false;
_Noreturn void _vm_error(const char *fmt, ...) {{ abort(); }}

{v12_builtin_fn}

int main(void) {{
    // 1. v12_builtin_library: AIN v8 enables String/Int/Float only; AIN v12 enables all 6; AIN v6 enables none.
    struct ain a6 = {{ .version = 6 }};
    struct ain a8 = {{ .version = 8 }};
    struct ain a12 = {{ .version = 12 }};

    ain = &a6;
    assert(!v12_builtin_library("String"));
    assert(!v12_builtin_library("Int"));
    assert(!v12_builtin_library("Float"));
    assert(!v12_builtin_library("Array"));

    ain = &a8;
    assert(v12_builtin_library("String"));
    assert(v12_builtin_library("Int"));
    assert(v12_builtin_library("Float"));
    assert(!v12_builtin_library("Array"));
    assert(!v12_builtin_library("Delegate"));
    assert(!v12_builtin_library("system"));

    ain = &a12;
    assert(v12_builtin_library("String"));
    assert(v12_builtin_library("Int"));
    assert(v12_builtin_library("Float"));
    assert(v12_builtin_library("Array"));
    assert(v12_builtin_library("Delegate"));
    assert(v12_builtin_library("system"));

    // 2. Parts message queue on AIN v8: empty queue returns -1; mouse/swipe IDs map via message_type_v8.
    ain = &a8;
    PE_ReleaseMessage();
    assert(PE_GetMessageType() == -1);

    struct parts dummy_part;
    memset(&dummy_part, 0, sizeof(dummy_part));
    dummy_part.no = 42;
    dummy_part.delegate_index = 7;

    parts_msg_push(&dummy_part, PARTS_MSG_MOUSE_ENTER, "");
    assert(PE_GetMessageType() == 0);
    PE_PopMessage();

    parts_msg_push(&dummy_part, PARTS_MSG_MOUSE_CLICK, "ii", 10, 20);
    assert(PE_GetMessageType() == 4);
    assert(PE_GetMessagePartsNumber() == 42);
    PE_PopMessage();

    parts_msg_push(&dummy_part, PARTS_MSG_SWIPE_BEGIN, "");
    assert(PE_GetMessageType() == 26);
    PE_PopMessage();

    assert(PE_GetMessageType() == -1);

    // 3. AIN v6 empty queue still returns 0 (no regression for Rance IX).
    ain = &a6;
    assert(PE_GetMessageType() == 0);

    puts("OK");
    return 0;
}}
"""
        out = compile_and_run(
            harness,
            [ROOT / "src/parts/message.c", *LIBSYS4_CORE],
            [f"-I{ROOT / 'src/parts'}", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-lz", "-lm"],
        )
        self.assertEqual(out.strip(), "OK")

    def test_parts_v2_scrollbar_ignores_nonpositive_part_numbers(self):
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "parts.h"
#include "parts_scrollbar.h"
#include "parts_internal.h"
#include "vm.h"

struct ain *ain = NULL;
bool parts_v2 = true;

static struct parts g_parts[16];
static bool g_exists[16];
static int g_parts_get_calls_on_nonpositive = 0;

struct parts *parts_get(int parts_no) {
    if (parts_no <= 0) {
        g_parts_get_calls_on_nonpositive++;
    }
    int slot = parts_no < 0 ? 0 : (parts_no < 16 ? parts_no : 15);
    g_exists[slot] = true;
    g_parts[slot].no = parts_no;
    return &g_parts[slot];
}

struct parts *parts_try_get(int parts_no) {
    if (parts_no < 0 || parts_no >= 16)
        return NULL;
    return g_exists[parts_no] ? &g_parts[parts_no] : NULL;
}

void parts_dirty(struct parts *parts) { (void)parts; }
void parts_set_dims(struct parts *parts, struct parts_common *common, int w, int h) {
    (void)parts; (void)common; (void)w; (void)h;
}
void parts_component_dirty(struct parts *parts) { (void)parts; }
void parts_recalculate_hitbox(struct parts *parts) { (void)parts; }
void parts_widget_free(struct parts *parts) { (void)parts; }
void parts_panel_free(struct parts *parts) { (void)parts; }
void parts_message_window_free(struct parts *parts) { (void)parts; }
void parts_state_reset(struct parts_state *state, enum parts_type type) {
    memset(state, 0, sizeof(*state));
    state->type = type;
}
bool parts_cg_set(struct parts *parts, struct parts_cg *cg, struct string *name) {
    (void)parts; (void)cg; (void)name; return false;
}
bool parts_flat_load(struct parts *parts, struct parts_flat *flat, struct string *name) {
    (void)parts; (void)flat; (void)name; return false;
}
const char *parts_utf8_to_cjk_literal(const char *utf8) { return utf8; }
void parts_msg_push(struct parts *parts, int type, const char *fmt, ...) {
    (void)parts; (void)type; (void)fmt;
}
void parts_set_surface_area(struct parts *parts, struct parts_common *common, int x, int y, int w, int h) {
    (void)parts; (void)common; (void)x; (void)y; (void)w; (void)h;
}
void parts_render_state(struct parts *parts, struct parts_state *state) { (void)parts; (void)state; }
void gfx_init_texture_rgba(Texture *dst, int w, int h, SDL_Color color) { (void)dst; (void)w; (void)h; (void)color; }
void gfx_delete_texture(Texture *t) { (void)t; }
void gfx_render_texture(Texture *t, Rectangle *r) { (void)t; (void)r; }
void gfx_fill_amap(Texture *t, int x, int y, int w, int h, int a) { (void)t; (void)x; (void)y; (void)w; (void)h; (void)a; }
void gfx_fill(Texture *t, int x, int y, int w, int h, int r, int g, int b) { (void)t; (void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b; }
_Noreturn void _vm_error(const char *fmt, ...) { abort(); }

int main(void) {
    memset(g_parts, 0, sizeof(g_parts));
    memset(g_exists, 0, sizeof(g_exists));
    parts_v2 = true;

    // Calling scrollbar/sliderbar APIs with no <= 0 in parts_v2 must NOT allocate part 0 or a phantom scrollbar!
    PE_SetHScrollbarScrollRate(0, 1.0f);
    PE_SetVScrollbarScrollRate(0, 0.5f);
    PE_SetHScrollbarTotalSize(0, 100);
    PE_SetVScrollbarViewSize(0, 50);
    PE_SetHScrollbarMoveSizeByButton(0, 10);
    PE_SetHSliderBarScrollRate(0, 0.75f);
    PE_SetVSliderBarScrollPos(-1, 5);
    assert(PE_GetHScrollbarTotalSize(0) == 0);
    assert(PE_GetVScrollbarScrollPos(0) == 0);
    assert(PE_GetHScrollbarScrollRate(0) == 0.f);
    assert(PE_GetHSliderBarScrollRate(0) == 0.f);
    assert(PE_GetHScrollTotalSizeLinkNumber(0) == 0);
    assert(g_parts_get_calls_on_nonpositive == 0);
    assert(!g_exists[0]);
    assert(g_parts[0].scrollbar == NULL);

    // Valid positive part numbers still create and update scrollbars normally in parts_v2.
    PE_SetHScrollbarTotalSize(5, 200);
    PE_SetHScrollbarViewSize(5, 100);
    PE_SetHScrollbarScrollRate(5, 0.5f);
    assert(g_exists[5] && g_parts[5].scrollbar != NULL);
    assert(PE_GetHScrollbarTotalSize(5) == 200);
    assert(PE_GetHScrollbarScrollPos(5) == 50);
    // Mismatched orientation or scrollbar/sliderbar kind must return 0 without reading/clobbering part 5.
    assert(PE_GetVScrollbarTotalSize(5) == 0);
    assert(PE_GetVScrollbarScrollPos(5) == 0);
    assert(PE_GetHSliderBarTotalSize(5) == 0);
    assert(PE_GetHSliderBarScrollPos(5) == 0);
    assert(PE_GetVSliderBarTotalSize(5) == 0);
    assert(PE_GetHScrollbarTotalSize(5) == 200);
    assert(PE_GetHScrollbarScrollPos(5) == 50);
    parts_scrollbar_free(&g_parts[5]);

    // Sliderbar getters match their own kind/orientation and reject mismatched kinds/orientations.
    PE_SetVSliderBarTotalSize(6, 300);
    PE_SetVSliderBarViewSize(6, 100);
    PE_SetVSliderBarScrollPos(6, 80);
    assert(PE_GetVSliderBarTotalSize(6) == 300);
    assert(PE_GetVSliderBarScrollPos(6) == 80);
    assert(PE_GetHSliderBarTotalSize(6) == 0);
    assert(PE_GetVScrollbarTotalSize(6) == 0);
    assert(PE_GetHScrollbarTotalSize(6) == 0);
    assert(PE_GetVSliderBarTotalSize(6) == 300);
    parts_scrollbar_free(&g_parts[6]);

    // Legacy parts_v2 == false keeps 0-based part IDs working.
    parts_v2 = false;
    PE_SetHScrollbarTotalSize(0, 120);
    assert(g_exists[0] && g_parts[0].scrollbar != NULL);
    assert(PE_GetHScrollbarTotalSize(0) == 120);
    parts_scrollbar_free(&g_parts[0]);

    puts("OK");
    return 0;
}
"""
        out = compile_and_run(
            harness,
            [ROOT / "src/parts/scrollbar.c", *LIBSYS4_CORE],
            [f"-I{ROOT / 'src/parts'}", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-lz", "-lm"],
        )
        self.assertEqual(out.strip(), "OK")

    def test_maman_mixed_parts_display_text(self):
        # Verify system4.c wires parts_set_mixed_display_text(game_maman) after apply_game_specific_hacks(ain).
        system4_src = (ROOT / "src/system4.c").read_text(encoding="utf-8")
        mixed_start = system4_src.rindex("parts_set_mixed_display_text(")
        mixed_call = system4_src[mixed_start:system4_src.index(");", mixed_start)]
        for flag in (
            "game_maman",
            "game_haharanman",
            "game_escalayer_reboot",
            "game_pascha3",
            "game_night_remaster",
            "game_evenicle2_rance",
            "game_rance01",
        ):
            self.assertIn(flag, mixed_call)

        def c_esc(b: bytes) -> str:
            return "".join(f"\\x{x:02x}" for x in b)

        cp932_exit_l1 = c_esc("ゲームを終了しますか？".encode("cp932"))
        cp932_exit_l2 = c_esc("※セーブされていないデータは失われます".encode("cp932"))
        cp932_locked = c_esc("？？？".encode("cp932"))
        cp932_recall_exit = c_esc("回想を終了します".encode("cp932"))
        cp932_music1 = c_esc("飛べない風船 ～Reach not think～ (short)".encode("cp932"))
        cp932_music2 = c_esc("ウェルカムママン".encode("cp932"))
        # Real CP932 UI/EX strings from sibling partial-CN releases:
        cp932_hhr_recall = c_esc("初めての召喚".encode("cp932"))
        cp932_hhr_music = c_esc("緋桜咲きて遠き空に憂う".encode("cp932"))
        cp932_esca_music = c_esc("侵略する帝国".encode("cp932"))
        cp932_esca_dex = c_esc("属性：生物系 出没：閂市街".encode("cp932"))
        cp932_yk_exit = c_esc("このまま終了しますか？".encode("cp932"))
        cp932_yk_skill = c_esc("モンスター感知Ⅰ".encode("cp932"))
        cp932_e2r_chapter = c_esc("ランス外伝　イブニクル２".encode("cp932"))
        cp932_r01_cfg = c_esc("クイックセーブ時に確認をおこなう".encode("cp932"))

        utf8_exit_l1 = c_esc("ゲームを終了しますか？".encode("utf-8"))
        utf8_exit_l2 = c_esc("※セーブされていないデータは失われます".encode("utf-8"))
        utf8_locked = c_esc("？？？".encode("utf-8"))
        utf8_recall_exit = c_esc("回想を終了します".encode("utf-8"))
        utf8_music1 = c_esc("飛べない風船 ～Reach not think～ (short)".encode("utf-8"))
        utf8_music2 = c_esc("ウェルカムママン".encode("utf-8"))
        utf8_hhr_recall = c_esc("初めての召喚".encode("utf-8"))
        utf8_hhr_music = c_esc("緋桜咲きて遠き空に憂う".encode("utf-8"))
        utf8_esca_music = c_esc("侵略する帝国".encode("utf-8"))
        utf8_esca_dex = c_esc("属性：生物系 出没：閂市街".encode("utf-8"))
        utf8_yk_exit = c_esc("このまま終了しますか？".encode("utf-8"))
        utf8_yk_skill = c_esc("モンスター感知Ⅰ".encode("utf-8"))
        utf8_e2r_chapter = c_esc("ランス外伝　イブニクル２".encode("utf-8"))
        utf8_r01_cfg = c_esc("クイックセーブ時に確認をおこなう".encode("utf-8"))

        gbk_titles = [
            c_esc(t.encode("gbk"))
            for t in ("失意的真生", "奶牛妈妈", "处理热烈的思念", "麻衣完结", "真.end", "真生", "汐梨", "麻衣", "实")
        ]
        gbk_titles_init = ", ".join(f'"{t}"' for t in gbk_titles)

        harness = f"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parts/text.c"
#include "parts/construction.c"

bool game_evenicle_rance_cn = false;
bool gfx_text_advance_edges = false;
static struct string *drawn = NULL;
static char last_built_text[512];

void gfx_register_reset_callback(gfx_reset_callback cb, void *user) {{ (void)cb; (void)user; }}
void gfx_unregister_reset_callback(gfx_reset_callback cb, void *user) {{ (void)cb; (void)user; }}
bool gfx_remap_texture(Texture *t) {{ (void)t; return false; }}
void gfx_init_texture_rgba(Texture *t, int w, int h, SDL_Color color) {{
    t->handle = 1; t->w = w; t->h = h; (void)color;
}}
void gfx_delete_texture(Texture *t) {{ t->handle = 0; }}
void gfx_fill_with_alpha(Texture *t, int x, int y, int w, int h, int r, int g, int b, int a) {{
    (void)t; (void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b; (void)a;
}}
void gfx_fill_amap(Texture *t, int x, int y, int w, int h, int a) {{
    (void)t; (void)x; (void)y; (void)w; (void)h; (void)a;
}}
float gfx_size_text(struct text_style *ts, const char *text) {{ (void)ts; (void)text; return 12.0f; }}
float gfx_render_text_mode(Texture *t, float x, int y, char *text, struct text_style *ts, enum text_render_mode mode) {{
    (void)t; (void)x; (void)y; (void)ts; (void)mode;
    snprintf(last_built_text, sizeof(last_built_text), "%s", text);
    return 12.0f;
}}
float gfx_size_char(struct text_style *ts, const char *ch) {{ (void)ts; (void)ch; return 12.0f; }}
float gfx_render_text_edge_only(Texture *t, float x, int y, char *ch, struct text_style *ts) {{
    (void)t; (void)x; (void)y; (void)ch; (void)ts; return 12.0f;
}}
float gfx_render_text_core_only(Texture *t, float x, int y, char *ch, struct text_style *ts) {{
    (void)t; (void)x; (void)y; (void)ts;
    string_append_cstr(&drawn, ch, strlen(ch));
    return 12.0f;
}}
void parts_set_dims(struct parts *parts, struct parts_common *common, int w, int h) {{
    (void)parts; common->w = w; common->h = h;
}}
void parts_dirty(struct parts *parts) {{ (void)parts; }}

static void check_converted_utf8(const char *cp932_src, const char *expected_utf8)
{{
    char *prepared = parts_prepare_display_text(cp932_src);
    assert(prepared != NULL);
    char *u = cjk2utf(prepared, strlen(prepared), true);
    assert(!strcmp(u, expected_utf8));
    free(u);

    // Also exercise build_text() (used by C_ACTIVITY_TEXT / Recall / Confirm dialogs).
    struct parts_construction_process cproc = {{0}};
    cproc.common.texture.handle = 1;
    cproc.common.texture.w = 640;
    cproc.common.texture.h = 64;
    struct string *s = make_string(cp932_src, strlen(cp932_src));
    struct parts_cp_text op = {{ .text = s, .style = {{ .size = 24.0f, .scale_x = 1.0f }} }};
    last_built_text[0] = '\\0';
    assert(build_text(&cproc, &op, RENDER_BLEND_ALPHA));
    assert(!strcmp(last_built_text, prepared));
    free_string(s);

    // Also exercise parts_text_append().
    struct parts part = {{0}};
    struct parts_text pt = {{ .ts = {{ .size = 24.0f, .scale_x = 1.0f }} }};
    drawn = make_string("", 0);
    s = make_string(cp932_src, strlen(cp932_src));
    parts_text_append(&part, &pt, s);
    assert(!strcmp(drawn->text, prepared));
    free_string(drawn);
    free_string(s);
    parts_text_free(&pt);
    free(prepared);
}}

int main(void)
{{
    sys4_set_text_gbk(true);
    sys4_set_source_utf8(false);

    // Default (non-Maman): line 2 and locked ??? are NOT converted.
    assert(!parts_uses_mixed_display_text());
    assert(parts_prepare_display_text("{cp932_exit_l2}") == NULL);
    assert(parts_prepare_display_text("{cp932_locked}") == NULL);

    // Heartful Maman CN: mixed display text converts CP932 UI strings cleanly.
    parts_set_mixed_display_text(true);
    assert(parts_uses_mixed_display_text());
    check_converted_utf8("{cp932_exit_l1}", "{utf8_exit_l1}");
    check_converted_utf8("{cp932_exit_l2}", "{utf8_exit_l2}");
    check_converted_utf8("{cp932_locked}", "{utf8_locked}");
    check_converted_utf8("{cp932_recall_exit}", "{utf8_recall_exit}");
    check_converted_utf8("{cp932_music1}", "{utf8_music1}");
    check_converted_utf8("{cp932_music2}", "{utf8_music2}");
    check_converted_utf8("{cp932_hhr_recall}", "{utf8_hhr_recall}");
    check_converted_utf8("{cp932_hhr_music}", "{utf8_hhr_music}");
    check_converted_utf8("{cp932_esca_music}", "{utf8_esca_music}");
    check_converted_utf8("{cp932_esca_dex}", "{utf8_esca_dex}");
    check_converted_utf8("{cp932_yk_exit}", "{utf8_yk_exit}");
    check_converted_utf8("{cp932_yk_skill}", "{utf8_yk_skill}");
    check_converted_utf8("{cp932_e2r_chapter}", "{utf8_e2r_chapter}");
    check_converted_utf8("{cp932_r01_cfg}", "{utf8_r01_cfg}");

    // Unlocked GBK Recall titles and GBK character nameplates must stay unchanged (0 false positives).
    const char *gbk_titles[] = {{ {gbk_titles_init} }};
    for (size_t i = 0; i < sizeof(gbk_titles) / sizeof(gbk_titles[0]); i++) {{
        assert(parts_prepare_display_text(gbk_titles[i]) == NULL);
    }}

    // Disabled in UTF-8 mode, in JP mode, and after parts_text_end_session().
    sys4_set_source_utf8(true);
    assert(!parts_uses_mixed_display_text());
    assert(parts_prepare_display_text("{cp932_locked}") == NULL);
    sys4_set_source_utf8(false);

    sys4_set_text_gbk(false);
    assert(!parts_uses_mixed_display_text());
    assert(parts_prepare_display_text("{cp932_locked}") == NULL);
    sys4_set_text_gbk(true);

    parts_text_end_session();
    assert(!parts_uses_mixed_display_text());
    puts("OK");
    return 0;
}}
"""
        out = compile_and_run(
            harness,
            [*LIBSYS4_CORE],
            [f"-I{ROOT / 'src'}", f"-I{ROOT / 'src/parts'}",
             "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-lz", "-lm"],
        )
        self.assertEqual(out.strip(), "OK")

    def test_maman_full_cg_extra_save_encoding_matrix(self):
        def c_esc(b: bytes) -> str:
            return "".join(f"\\x{x:02x}" for x in b)

        recall_key = "管理／日数／失意の真生"
        cg_key = "イベ／汐梨／イベ／搾乳／部屋／ブラ／目閉じ"
        cfg_group = "コンフィグデータ"
        fn_name = "■回想関数取得"

        harness = f"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4/ain.h"
#include "savedata.h"
#include "system4/savefile.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

bool game_maman, game_haharanman;
bool game_escalayer_reboot, game_pascha3, game_night_remaster, game_rance01, game_haruka;
int load_globals_from_gsave(struct gsave *save, const char *key, const char *group, int *count);
struct ain *ain;
union vm_value *stack;
int32_t stack_ptr;

union vm_value global_get(int i) {{ return heap[0].page->values[i]; }}
void global_set(int i, union vm_value val, bool dtors) {{ (void)dtors; heap[0].page->values[i] = val; }}
union vm_value stack_pop(void) {{ abort(); }}
union vm_value vm_copy(union vm_value val, enum ain_data_type type) {{ (void)val; (void)type; abort(); }}
void vm_call(int fno, int slot) {{ (void)fno; (void)slot; abort(); }}
_Noreturn void _vm_error(const char *fmt, ...) {{ (void)fmt; abort(); }}

static void run_case(bool gbk_mode, bool utf8_mode, bool ain_ids_gbk,
                     const char *saved_recall, const char *saved_cg,
                     const char *expected_recall, const char *expected_cg,
                     int leading_ascii_strings)
{{
    sys4_set_text_gbk(gbk_mode);
    sys4_set_source_utf8(utf8_mode);
    game_maman = true;
    game_haharanman = false;

    char *groups[] = {{
        "MAMAN_EXTRA_DATA",
        ain_ids_gbk ? "{c_esc(cfg_group.encode('gbk'))}" : "{c_esc(cfg_group.encode('cp932'))}",
    }};
    struct ain_function funcs[] = {{
        {{ .name = ain_ids_gbk ? "{c_esc(fn_name.encode('gbk'))}" : "{c_esc(fn_name.encode('cp932'))}" }},
    }};
    struct ain_variable key_list_member = {{
        .name = "m_aszList", .type = {{ .data = AIN_ARRAY_STRING, .rank = 1 }},
    }};
    struct ain_variable cg_members[] = {{
        {{ .name = "m_aszBase", .type = {{ .data = AIN_ARRAY_STRING, .rank = 1 }} }},
        {{ .name = "m_aszCutIn", .type = {{ .data = AIN_ARRAY_STRING, .rank = 1 }} }},
    }};
    struct ain_struct structures[] = {{
        {{ .name = "C_KEY_LIST", .constructor = -1, .destructor = -1, .nr_members = 1, .members = &key_list_member }},
        {{ .name = "C_COLLECTED_CG_LIST", .constructor = -1, .destructor = -1, .nr_members = 2, .members = cg_members }},
    }};
    struct ain_variable globals[] = {{
        {{ .name = "g_cGlobalKeyList", .group_index = 0, .type = {{ .data = AIN_STRUCT, .struc = 0 }} }},
        {{ .name = "g_cCollectedCgList", .group_index = 0, .type = {{ .data = AIN_STRUCT, .struc = 1 }} }},
    }};
    struct ain script = {{
        .version = 8,
        .nr_globals = 2, .globals = globals,
        .nr_structures = 2, .structures = structures,
        .nr_global_groups = 2, .global_group_names = groups,
        .nr_functions = 1, .functions = funcs,
    }};
    ain = &script;

    struct gsave *save = gsave_create(7, "MAMAN_EXTRA", 1, "MAMAN_EXTRA_DATA");
    // Optionally prepend >64 ASCII strings so encoding detection must skip ASCII entries.
    for (int i = 0; i < leading_ascii_strings; i++) {{
        char buf[32];
        snprintf(buf, sizeof(buf), "ASCII_KEY_%03d", i);
        struct string *as = make_string(buf, strlen(buf));
        gsave_add_string(save, as);
        free_string(as);
    }}

    struct string *s_rec = make_string(saved_recall, strlen(saved_recall));
    struct string *s_cg = make_string(saved_cg, strlen(saved_cg));
    int32_t rec_idx = gsave_add_string(save, s_rec);
    int32_t cg_idx = gsave_add_string(save, s_cg);
    free_string(s_rec);
    free_string(s_cg);

    // Build g_cGlobalKeyList.m_aszList = [saved_recall]
    struct gsave_array_value *rec_vals = xmalloc(sizeof(*rec_vals));
    rec_vals[0] = (struct gsave_array_value){{ .type = AIN_STRING, .value = rec_idx }};
    int32_t *rec_dim = xmalloc(sizeof(*rec_dim));
    *rec_dim = 1;
    struct gsave_flat_array *rec_flat = xmalloc(sizeof(*rec_flat));
    *rec_flat = (struct gsave_flat_array){{ .type = AIN_STRING, .nr_values = 1, .values = rec_vals }};
    struct gsave_array rec_arr = {{ .rank = 1, .dimensions = rec_dim, .nr_flat_arrays = 1, .flat_arrays = rec_flat }};
    struct gsave_keyval rec_kv = {{ .name = xstrdup("m_aszList"), .type = AIN_ARRAY_STRING, .value = gsave_add_array(save, &rec_arr) }};
    struct gsave_record rec_record = {{
        .type = GSAVE_RECORD_STRUCT, .nr_indices = 1, .indices = xmalloc(sizeof(int32_t)),
        .struct_index = gsave_add_struct_def(save, &structures[0]),
    }};
    rec_record.indices[0] = gsave_add_keyval(save, &rec_kv);

    // Build g_cCollectedCgList.m_aszBase = [saved_cg], m_aszCutIn = []
    struct gsave_array_value *cg_vals = xmalloc(sizeof(*cg_vals));
    cg_vals[0] = (struct gsave_array_value){{ .type = AIN_STRING, .value = cg_idx }};
    int32_t *cg_dim = xmalloc(sizeof(*cg_dim));
    *cg_dim = 1;
    struct gsave_flat_array *cg_flat = xmalloc(sizeof(*cg_flat));
    *cg_flat = (struct gsave_flat_array){{ .type = AIN_STRING, .nr_values = 1, .values = cg_vals }};
    struct gsave_array cg_arr = {{ .rank = 1, .dimensions = cg_dim, .nr_flat_arrays = 1, .flat_arrays = cg_flat }};
    struct gsave_array empty_arr = {{ .rank = -1 }};
    struct gsave_keyval cg_kv0 = {{ .name = xstrdup("m_aszBase"), .type = AIN_ARRAY_STRING, .value = gsave_add_array(save, &cg_arr) }};
    struct gsave_keyval cg_kv1 = {{ .name = xstrdup("m_aszCutIn"), .type = AIN_ARRAY_STRING, .value = gsave_add_array(save, &empty_arr) }};
    struct gsave_record cg_record = {{
        .type = GSAVE_RECORD_STRUCT, .nr_indices = 2, .indices = xmalloc(2 * sizeof(int32_t)),
        .struct_index = gsave_add_struct_def(save, &structures[1]),
    }};
    cg_record.indices[0] = gsave_add_keyval(save, &cg_kv0);
    cg_record.indices[1] = gsave_add_keyval(save, &cg_kv1);

    gsave_add_globals_record(save, 2);
    save->globals[0] = (struct gsave_global){{
        .name = xstrdup("g_cGlobalKeyList"), .type = AIN_STRUCT, .value = gsave_add_record(save, &rec_record),
    }};
    save->globals[1] = (struct gsave_global){{
        .name = xstrdup("g_cCollectedCgList"), .type = AIN_STRUCT, .value = gsave_add_record(save, &cg_record),
    }};

    int count = 0;
    assert(load_globals_from_gsave(save, "MAMAN_EXTRA", "MAMAN_EXTRA_DATA", &count) && count == 2);

    struct page *loaded_rec = heap_get_page(heap_get_page(global_get(0).i)->values[0].i);
    struct page *loaded_cg = heap_get_page(heap_get_page(global_get(1).i)->values[0].i);
    int exp_rec_slot = heap_alloc_slot(VM_STRING);
    heap[exp_rec_slot].s = make_string(expected_recall, strlen(expected_recall));
    int exp_cg_slot = heap_alloc_slot(VM_STRING);
    heap[exp_cg_slot].s = make_string(expected_cg, strlen(expected_cg));

    assert(array_find(loaded_rec, 0, 1, vm_int(exp_rec_slot), 0, -1) == 0);
    assert(array_find(loaded_cg, 0, 1, vm_int(exp_cg_slot), 0, -1) == 0);

    heap_unref(global_get(0).i);
    heap_unref(global_get(1).i);
    heap_unref(exp_rec_slot);
    heap_unref(exp_cg_slot);
    gsave_free(save);
}}

int main(void)
{{
    heap_init();
    heap[0].ref = 1;
    heap_set_page(0, alloc_page(GLOBAL_PAGE, 0, 2));

    const char *rec_cp = "{c_esc(recall_key.encode('cp932'))}";
    const char *cg_cp  = "{c_esc(cg_key.encode('cp932'))}";
    const char *rec_gb = "{c_esc(recall_key.encode('gbk'))}";
    const char *cg_gb  = "{c_esc(cg_key.encode('gbk'))}";

    // 1. Online CDN CN Maman (GBK text, CP932 .ain/.ex keys) + JP CP932 full-CG save (with >64 leading ASCII strings)
    run_case(true, false, false, rec_cp, cg_cp, rec_cp, cg_cp, 80);
    // 2. Online CDN CN Maman + GBK full-CG save (from another CN site / older xsystem4)
    run_case(true, false, false, rec_gb, cg_gb, rec_cp, cg_cp, 80);
    // 3. UTF-8 CN Maman + JP CP932 full-CG save
    run_case(true, true, false, rec_cp, cg_cp, rec_gb, cg_gb, 0);
    // 4. UTF-8 CN Maman + GBK full-CG save
    run_case(true, true, false, rec_gb, cg_gb, rec_gb, cg_gb, 0);
    // 5. Fully-GBK-identifier CN Maman + JP CP932 full-CG save
    run_case(true, false, true, rec_cp, cg_cp, rec_gb, cg_gb, 0);
    // 6. Fully-GBK-identifier CN Maman + GBK full-CG save
    run_case(true, false, true, rec_gb, cg_gb, rec_gb, cg_gb, 0);
    // 7. Original JP Maman + CP932 and GBK full-CG saves
    run_case(false, false, false, rec_cp, cg_cp, rec_cp, cg_cp, 0);
    run_case(false, false, false, rec_gb, cg_gb, rec_cp, cg_cp, 0);

    // 8. Mixed-encoding Maman_Extra.sav (native CP932 Recall + native CP932 CG alongside
    //    20 dominant GBK CG entries): must preserve native CP932 entries verbatim AND
    //    normalize imported GBK entries to CP932, then survive a resave+reload cleanly!
    {{
        sys4_set_text_gbk(true);
        sys4_set_source_utf8(false);
        game_maman = true;
        game_haharanman = false;
        char *groups[] = {{ "MAMAN_EXTRA_DATA", "{c_esc(cfg_group.encode('cp932'))}" }};
        struct ain_function funcs[] = {{
            {{ .name = "{c_esc(fn_name.encode('cp932'))}" }},
            {{ .name = (char *)rec_cp }},
        }};
        struct ain_variable key_list_member = {{
            .name = "m_aszList", .type = {{ .data = AIN_ARRAY_STRING, .rank = 1 }},
        }};
        struct ain_variable cg_members[] = {{
            {{ .name = "m_aszBase", .type = {{ .data = AIN_ARRAY_STRING, .rank = 1 }} }},
            {{ .name = "m_aszCutIn", .type = {{ .data = AIN_ARRAY_STRING, .rank = 1 }} }},
        }};
        struct ain_struct structures[] = {{
            {{ .name = "C_KEY_LIST", .constructor = -1, .destructor = -1, .nr_members = 1, .members = &key_list_member }},
            {{ .name = "C_COLLECTED_CG_LIST", .constructor = -1, .destructor = -1, .nr_members = 2, .members = cg_members }},
        }};
        struct ain_variable globals[] = {{
            {{ .name = "g_cGlobalKeyList", .group_index = 0, .type = {{ .data = AIN_STRUCT, .struc = 0 }} }},
            {{ .name = "g_cCollectedCgList", .group_index = 0, .type = {{ .data = AIN_STRUCT, .struc = 1 }} }},
        }};
        struct ain script = {{
            .version = 8,
            .nr_globals = 2, .globals = globals,
            .nr_structures = 2, .structures = structures,
            .nr_global_groups = 2, .global_group_names = groups,
            .nr_functions = 2, .functions = funcs,
        }};
        ain = &script;

        struct gsave *save = gsave_create(7, "MAMAN_EXTRA", 1, "MAMAN_EXTRA_DATA");
        // Dominant GBK strings first so file-level gsave_detect_is_gbk() chooses GBK!
        int32_t cg_indices[22];
        for (int i = 0; i < 20; i++) {{
            struct string *sg = make_string(cg_gb, strlen(cg_gb));
            cg_indices[i] = gsave_add_string(save, sg);
            free_string(sg);
        }}
        struct string *sc_cp = make_string(cg_cp, strlen(cg_cp));
        cg_indices[20] = gsave_add_string(save, sc_cp);
        free_string(sc_cp);
        struct string *sr_cp = make_string(rec_cp, strlen(rec_cp));
        int32_t rec_cp_idx = gsave_add_string(save, sr_cp);
        free_string(sr_cp);
        struct string *sr_gb = make_string(rec_gb, strlen(rec_gb));
        int32_t rec_gb_idx = gsave_add_string(save, sr_gb);
        free_string(sr_gb);

        struct gsave_array_value *rec_vals = xmalloc(2 * sizeof(*rec_vals));
        rec_vals[0] = (struct gsave_array_value){{ .type = AIN_STRING, .value = rec_cp_idx }};
        rec_vals[1] = (struct gsave_array_value){{ .type = AIN_STRING, .value = rec_gb_idx }};
        int32_t *rec_dim = xmalloc(sizeof(*rec_dim)); *rec_dim = 2;
        struct gsave_flat_array *rec_flat = xmalloc(sizeof(*rec_flat));
        *rec_flat = (struct gsave_flat_array){{ .type = AIN_STRING, .nr_values = 2, .values = rec_vals }};
        struct gsave_array rec_arr = {{ .rank = 1, .dimensions = rec_dim, .nr_flat_arrays = 1, .flat_arrays = rec_flat }};
        struct gsave_keyval rec_kv = {{ .name = xstrdup("m_aszList"), .type = AIN_ARRAY_STRING, .value = gsave_add_array(save, &rec_arr) }};
        struct gsave_record rec_record = {{
            .type = GSAVE_RECORD_STRUCT, .nr_indices = 1, .indices = xmalloc(sizeof(int32_t)),
            .struct_index = gsave_add_struct_def(save, &structures[0]),
        }};
        rec_record.indices[0] = gsave_add_keyval(save, &rec_kv);

        struct gsave_array_value *cg_vals = xmalloc(21 * sizeof(*cg_vals));
        for (int i = 0; i < 21; i++)
            cg_vals[i] = (struct gsave_array_value){{ .type = AIN_STRING, .value = cg_indices[i] }};
        int32_t *cg_dim = xmalloc(sizeof(*cg_dim)); *cg_dim = 21;
        struct gsave_flat_array *cg_flat = xmalloc(sizeof(*cg_flat));
        *cg_flat = (struct gsave_flat_array){{ .type = AIN_STRING, .nr_values = 21, .values = cg_vals }};
        struct gsave_array cg_arr = {{ .rank = 1, .dimensions = cg_dim, .nr_flat_arrays = 1, .flat_arrays = cg_flat }};
        struct gsave_array empty_arr = {{ .rank = -1 }};
        struct gsave_keyval cg_kv0 = {{ .name = xstrdup("m_aszBase"), .type = AIN_ARRAY_STRING, .value = gsave_add_array(save, &cg_arr) }};
        struct gsave_keyval cg_kv1 = {{ .name = xstrdup("m_aszCutIn"), .type = AIN_ARRAY_STRING, .value = gsave_add_array(save, &empty_arr) }};
        struct gsave_record cg_record = {{
            .type = GSAVE_RECORD_STRUCT, .nr_indices = 2, .indices = xmalloc(2 * sizeof(int32_t)),
            .struct_index = gsave_add_struct_def(save, &structures[1]),
        }};
        cg_record.indices[0] = gsave_add_keyval(save, &cg_kv0);
        cg_record.indices[1] = gsave_add_keyval(save, &cg_kv1);

        gsave_add_globals_record(save, 2);
        save->globals[0] = (struct gsave_global){{
            .name = xstrdup("g_cGlobalKeyList"), .type = AIN_STRUCT, .value = gsave_add_record(save, &rec_record),
        }};
        save->globals[1] = (struct gsave_global){{
            .name = xstrdup("g_cCollectedCgList"), .type = AIN_STRUCT, .value = gsave_add_record(save, &cg_record),
        }};

        int count = 0;
        assert(load_globals_from_gsave(save, "MAMAN_EXTRA", "MAMAN_EXTRA_DATA", &count) && count == 2);
        struct page *loaded_rec = heap_get_page(heap_get_page(global_get(0).i)->values[0].i);
        struct page *loaded_cg = heap_get_page(heap_get_page(global_get(1).i)->values[0].i);
        // BOTH the originally-CP932 and originally-GBK recall & CG entries must equal native CP932!
        assert(!strcmp(heap[loaded_rec->values[0].i].s->text, rec_cp));
        assert(!strcmp(heap[loaded_rec->values[1].i].s->text, rec_cp));
        for (int i = 0; i < 21; i++)
            assert(!strcmp(heap[loaded_cg->values[i].i].s->text, cg_cp));

        heap_unref(global_get(0).i);
        heap_unref(global_get(1).i);
        gsave_free(save);
    }}

    puts("OK");
    return 0;
}}
"""
        out = compile_and_run(
            harness,
            [ROOT / "src/savedata.c", ROOT / "src/util.c", ROOT / "src/heap.c", ROOT / "src/page.c", ROOT / "src/vm_v12.c",
             ROOT / "src/cJSON.c", ROOT / "subprojects/libsys4/src/savefile.c", *LIBSYS4_CORE],
            ["-DRELEASE", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-lz", "-lm"],
        )
        self.assertEqual(out.strip(), "OK")

    def test_passregister_cross_encoding_lookup(self):
        import tempfile

        def c_esc(b: bytes) -> str:
            return "".join(f"\\x{x:02x}" for x in b)

        bgm_cp = c_esc("01_メインテーマ".encode("cp932"))
        bgm_gb = c_esc("01_メインテーマ".encode("gbk"))
        with tempfile.TemporaryDirectory() as tmpdir:
            harness = f"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hll.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

struct ain *ain;
struct config config;
bool game_maman = true, game_haharanman = false;
bool game_escalayer_reboot = false, game_pascha3 = false, game_night_remaster = false;
bool game_rance01 = false, game_haruka = false;
union vm_value *stack;
int32_t stack_ptr;
extern struct static_library lib_PassRegister;

union vm_value global_get(int i) {{ (void)i; return (union vm_value){{0}}; }}
void global_set(int i, union vm_value val, bool dtors) {{ (void)i; (void)val; (void)dtors; }}
union vm_value stack_pop(void) {{ abort(); }}
union vm_value vm_copy(union vm_value val, enum ain_data_type type) {{ (void)val; (void)type; abort(); }}
void vm_call(int fno, int slot) {{ (void)fno; (void)slot; abort(); }}
_Noreturn void _vm_error(const char *fmt, ...) {{ (void)fmt; abort(); }}

static void *find_pr(const char *name) {{
    for (struct static_hll_function *f = lib_PassRegister.functions; f->name; f++)
        if (!strcmp(f->name, name))
            return f->fun;
    abort();
}}

int main(void)
{{
    config.save_dir = "{tmpdir}";
    sys4_set_text_gbk(true);

    bool (*set_file)(int, struct string *) = find_pr("SetFileName");
    bool (*reg_text)(int, struct string *) = find_pr("RegistText");
    bool (*unreg_text)(int, struct string *) = find_pr("UnregistText");
    bool (*exist_text)(int, struct string *) = find_pr("ExistText");
    void (*fini)(void) = find_pr("_ModuleFini");

    struct string *fn = make_string("Maman_AFBGMMode.asd", strlen("Maman_AFBGMMode.asd"));
    struct string *cp = make_string("{bgm_cp}", strlen("{bgm_cp}"));
    struct string *gb = make_string("{bgm_gb}", strlen("{bgm_gb}"));

    // Register CP932 key, reload from disk, verify both CP932 and GBK lookups succeed.
    assert(set_file(2, fn));
    assert(reg_text(2, cp));
    fini();
    assert(set_file(2, fn));
    assert(exist_text(2, cp));
    assert(exist_text(2, gb));
    // Re-registering equivalent GBK key must not create duplicate; unreg removes it.
    assert(reg_text(2, gb));
    assert(unreg_text(2, gb));
    assert(!exist_text(2, cp));
    assert(!exist_text(2, gb));

    // Reverse direction: save GBK key on disk, verify CP932 lookup matches.
    assert(reg_text(2, gb));
    fini();
    assert(set_file(2, fn));
    assert(exist_text(2, gb));
    assert(exist_text(2, cp));

    fini();
    free_string(fn);
    free_string(cp);
    free_string(gb);
    puts("OK");
    return 0;
}}
"""
            out = compile_and_run(
                harness,
                [ROOT / "src/hll/PassRegister.c", ROOT / "src/savedata.c", ROOT / "src/util.c",
                 ROOT / "src/heap.c", ROOT / "src/page.c", ROOT / "src/vm_v12.c",
                 ROOT / "src/cJSON.c", ROOT / "subprojects/libsys4/src/savefile.c", *LIBSYS4_CORE],
                ["-DRELEASE", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-lz", "-lm"],
            )
            self.assertEqual(out.strip(), "OK")

    def test_sibling_games_extra_save_raw_lookup_keys(self):
        def c_esc(b: bytes) -> str:
            return "".join(f"\\x{x:02x}" for x in b)

        cases = [
            ("haharanman", 14, "MIBOUJIN_USER", "MIBOUJIN_USER_DATA", "管理／ＥＶ／エンディング"),
            ("escalayer_reboot", 6, "ESCA_OMAKE", "ESCA_OMAKE_DATA", "沙由香０１／部屋Ｈ／視姦"),
            ("escalayer_reboot", 6, "ESCA_USER", "ESCA_USER_DATA", "凌辱イベント／初回"),
            ("pascha3", 6, "PC3_OMAKE", "PC3_OMAKE_DATA", "ＥＶ／リリアム／０１／０１"),
            ("pascha3", 6, "PC3_USER", "PC3_USER_DATA", "勲章キー／ＣＧモード１００％"),
            ("night_remaster", 14, "seEfa0ea?d8sdvaAekfe", "SaveGroup_MemModeInfo", "ＥＶ／元／E00001"),
            ("rance01", 6, "仮キー名", "共有データ", "シィル／学生服Ｈ"),
            ("haruka", 5, "超昂閃忍セーブキー", "SomeFlag", "ハルカ／初回Ｈ"),
        ]
        cfg_group = "コンフィグデータ"
        fn_marker = "■システム初期化"

        case_calls = []
        for flag, ver, key_s, grp_s, lookup_s in cases:
            case_calls.append(
                f'verify_sibling("{flag}", {ver}, '
                f'"{c_esc(key_s.encode("cp932"))}", "{c_esc(key_s.encode("gbk"))}", '
                f'"{c_esc(grp_s.encode("cp932"))}", "{c_esc(grp_s.encode("gbk"))}", '
                f'"{c_esc(lookup_s.encode("cp932"))}", "{c_esc(lookup_s.encode("gbk"))}");'
            )
        calls_src = "\n    ".join(case_calls)

        harness = f"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4/ain.h"
#include "savedata.h"
#include "system4/savefile.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

void apply_game_specific_hacks(struct ain *ain);
struct config config;
bool message_layout_init_rance03(struct ain *a) {{ (void)a; return false; }}
void escalayer_reboot_replace_cn_ui_text(struct string **s, int n) {{ (void)s; (void)n; }}

int load_globals_from_gsave(struct gsave *save, const char *key, const char *group, int *count);
struct ain *ain;
union vm_value *stack;
int32_t stack_ptr;

union vm_value global_get(int i) {{ return heap[0].page->values[i]; }}
void global_set(int i, union vm_value val, bool dtors) {{ (void)dtors; heap[0].page->values[i] = val; }}
union vm_value stack_pop(void) {{ abort(); }}
union vm_value vm_copy(union vm_value val, enum ain_data_type type) {{ (void)val; (void)type; abort(); }}
void vm_call(int fno, int slot) {{ (void)fno; (void)slot; abort(); }}
_Noreturn void _vm_error(const char *fmt, ...) {{ (void)fmt; abort(); }}

static void reset_flags(void)
{{
    game_maman = false;
    game_haharanman = false;
    game_escalayer_reboot = false;
    game_pascha3 = false;
    game_night_remaster = false;
    game_rance01 = false;
    game_haruka = false;
}}

static void set_flag(const char *name)
{{
    reset_flags();
    if (!strcmp(name, "haharanman")) game_haharanman = true;
    else if (!strcmp(name, "escalayer_reboot")) game_escalayer_reboot = true;
    else if (!strcmp(name, "pascha3")) game_pascha3 = true;
    else if (!strcmp(name, "night_remaster")) game_night_remaster = true;
    else if (!strcmp(name, "rance01")) game_rance01 = true;
    else if (!strcmp(name, "haruka")) game_haruka = true;
    else abort();
}}

static void run_one(const char *flag, int ver, bool ain_ids_gbk,
                    const char *ain_key, const char *ain_grp,
                    const char *save_key, const char *save_grp,
                    const char *saved_val, const char *expected_val)
{{
    sys4_set_text_gbk(true);
    sys4_set_source_utf8(false);
    set_flag(flag);

    char *groups[] = {{
        (char *)ain_grp,
        ain_ids_gbk ? "{c_esc(cfg_group.encode('gbk'))}" : "{c_esc(cfg_group.encode('cp932'))}",
    }};
    struct ain_function funcs[] = {{
        {{ .name = ain_ids_gbk ? "{c_esc(fn_marker.encode('gbk'))}" : "{c_esc(fn_marker.encode('cp932'))}" }},
    }};
    struct ain_type elem_str = {{ .data = AIN_STRING }};
    struct ain_variable globals[] = {{
        {{
            .name = "g_keys",
            .group_index = 0,
            .type = ver >= 12
                ? (struct ain_type){{ .data = AIN_ARRAY, .rank = 1, .array_type = &elem_str }}
                : (struct ain_type){{ .data = AIN_ARRAY_STRING, .rank = 1 }},
        }},
    }};
    struct ain script = {{
        .version = ver,
        .nr_globals = 1, .globals = globals,
        .nr_global_groups = 2, .global_group_names = groups,
        .nr_functions = 1, .functions = funcs,
    }};
    ain = &script;

    struct gsave *save = gsave_create(7, save_key, 1, save_grp);
    struct string *sv = make_string(saved_val, strlen(saved_val));
    int32_t idx = gsave_add_string(save, sv);
    free_string(sv);

    struct gsave_array_value *vals = xmalloc(sizeof(*vals));
    vals[0] = (struct gsave_array_value){{ .type = AIN_STRING, .value = idx }};
    int32_t *dim = xmalloc(sizeof(*dim));
    *dim = 1;
    struct gsave_flat_array *flat = xmalloc(sizeof(*flat));
    *flat = (struct gsave_flat_array){{ .type = AIN_STRING, .nr_values = 1, .values = vals }};
    struct gsave_array arr = {{ .rank = 1, .dimensions = dim, .nr_flat_arrays = 1, .flat_arrays = flat }};

    gsave_add_globals_record(save, 1);
    save->globals[0] = (struct gsave_global){{
        .name = xstrdup("g_keys"),
        .type = ver >= 12 ? AIN_ARRAY : AIN_ARRAY_STRING,
        .value = gsave_add_array(save, &arr),
    }};

    int count = 0;
    assert(load_globals_from_gsave(save, ain_key, ain_grp, &count) && count == 1);
    struct page *loaded = heap_get_page(global_get(0).i);
    assert(loaded && loaded->nr_vars == 1);
    assert(!strcmp(heap[loaded->values[0].i].s->text, expected_val));
    heap_unref(global_get(0).i);
    gsave_free(save);
}}

static void verify_sibling(const char *flag, int ver,
                           const char *key_cp, const char *key_gb,
                           const char *grp_cp, const char *grp_gb,
                           const char *val_cp, const char *val_gb)
{{
    // Partial CN build (CP932 identifiers): JP CP932 save stays CP932, GBK save normalizes to CP932.
    run_one(flag, ver, false, key_cp, grp_cp, key_cp, grp_cp, val_cp, val_cp);
    run_one(flag, ver, false, key_cp, grp_cp, key_gb, grp_gb, val_gb, val_cp);
    // Fully-GBK repack (GBK identifiers): JP CP932 save transcodes to GBK, GBK save stays GBK.
    run_one(flag, ver, true, key_gb, grp_gb, key_cp, grp_cp, val_cp, val_gb);
    run_one(flag, ver, true, key_gb, grp_gb, key_gb, grp_gb, val_gb, val_gb);
}}

static void verify_haruka_detection(void)
{{
    struct ain *h = ain_new(5, 0);
    ain_add_struct(h, "C_CG_DATA_AT_FUNC");
    ain_add_global(h, "g_FunctionString");
    ain_add_global(h, "g_cCGData");
    apply_game_specific_hacks(h);
    assert(game_haruka);
    ain_free(h);

    struct ain *other = ain_new(5, 0);
    ain_add_global(other, "g_other");
    apply_game_specific_hacks(other);
    assert(!game_haruka);
    ain_free(other);
}}

int main(void)
{{
    verify_haruka_detection();

    heap_init();
    heap[0].ref = 1;
    heap_set_page(0, alloc_page(GLOBAL_PAGE, 0, 1));

    {calls_src}

    puts("OK");
    return 0;
}}
"""
        out = compile_and_run(
            harness,
            [ROOT / "src/hacks.c", ROOT / "src/savedata.c", ROOT / "src/util.c",
             ROOT / "src/heap.c", ROOT / "src/page.c", ROOT / "src/vm_v12.c",
             ROOT / "src/cJSON.c", ROOT / "subprojects/libsys4/src/savefile.c", *LIBSYS4_CORE],
            ["-DRELEASE", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-lz", "-lm"],
        )
        self.assertEqual(out.strip(), "OK")


if __name__ == "__main__":
    unittest.main()
