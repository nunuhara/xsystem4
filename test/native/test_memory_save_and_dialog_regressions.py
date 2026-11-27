#!/usr/bin/env python3
"""Native regression tests for <memory> virtual saves and mixed-CJK system dialogs.

Covers:
1. savedir_path() / savedir_file_exists() map <memory> virtual save names
   (e.g. <memory>MemoryMode_EscalayerReboot_.asd, <memory>BackSelect1.asd,
   uppercase <MEMORY>, ./<memory>, nested slashes, trailing dot, empty name,
   and >160-byte names) to flat, FAT32/Android-FUSE/Switch-safe hidden dotfiles
   (.alicehub-mem.*) inside config.save_dir without literal '<' or '>'.
2. Full real save/load/exists/copy/delete round-trip through savedata.c +
   util.c (save_globals, load_globals, delete_save_file, file_copy) for
   <memory>MemoryMode_EscalayerReboot_.asd, including ASCII case-insensitive
   lookup, single-dot atomic temp paths, no .bak creation, and session cleanup
   via savedir_cleanup_memory_saves() leaving normal saves untouched.
3. cjk_dialog_text_to_utf8() in libsys4/src/utfsjis.c cleanly decodes leftover
   CP932 system message boxes in GBK games (including the exact Escalayer Reboot
   replay save error, quit prompts, and CP932-only symbols not in GBK) while
   leaving Simplified/Traditional Chinese GBK dialogs untouched and supporting
   mixed GBK + CP932 dialogs.
4. Wiring checks confirming vm.c (SYS_MSGBOX, SYS_MSGBOX_OK_CANCEL, SYS_ERROR,
   SYS_COPY_SAVE_FILE), hll/vmDialog.c, and system4.c startup/teardown cleanup
   hooks are wired across desktop, Android, iOS, and Switch.

Run: python3 xsystem4/test/native/test_memory_save_and_dialog_regressions.py
"""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LIBSYS4_DIR = ROOT / "subprojects" / "libsys4"
LIBSYS4_CORE = [
    LIBSYS4_DIR / "src/file.c",
    LIBSYS4_DIR / "src/string.c",
    LIBSYS4_DIR / "src/system.c",
    LIBSYS4_DIR / "src/utfsjis.c",
]
LIBSYS4_SAVEFILE = [
    *LIBSYS4_CORE,
    LIBSYS4_DIR / "src/ain.c",
    LIBSYS4_DIR / "src/buffer.c",
    LIBSYS4_DIR / "src/hashtable.c",
    LIBSYS4_DIR / "src/mt19937int.c",
    LIBSYS4_DIR / "src/savefile.c",
    LIBSYS4_DIR / "src/zlib.c",
]


def compile_and_run(c_source: str, extra_sources, extra_flags=None) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "test.c"
        exe = Path(tmp) / "test_bin"
        src.write_text(c_source, encoding="utf-8")
        cmd = [
            "cc",
            "-std=gnu11",
            "-Wall",
            "-Wextra",
            "-O2",
            f"-I{ROOT / 'include'}",
            f"-I{ROOT / 'src'}",
            f"-I{LIBSYS4_DIR / 'include'}",
        ]
        if extra_flags:
            cmd.extend(extra_flags)
        cmd.extend([
            str(src),
            *[str(s) for s in extra_sources],
            "-lz",
            "-lpthread",
            "-lm",
            "-o",
            str(exe),
        ])
        if sys.platform == "darwin":
            cmd = ["-Wl,-dead_strip" if arg == "-Wl,--gc-sections" else arg for arg in cmd]
            cmd.extend(["-framework", "CoreFoundation"])
        subprocess.run(cmd, check=True)
        subprocess.run([str(exe)], check=True)


class MemorySaveAndDialogRegressionTests(unittest.TestCase):
    def test_memory_save_path_mapping_and_cleanup(self) -> None:
        c_code = r"""
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "system4.h"
#include "system4/file.h"
#include "system4/utfsjis.h"
#include "xsystem4.h"

struct config config = { 0 };
bool dbg_dap = false;
void dbg_dap_log(const char *log, const char *fmt, va_list ap) { (void)log; (void)fmt; (void)ap; }

static bool has_illegal_fat_chars(const char *base)
{
    for (const unsigned char *p = (const unsigned char *)base; *p; p++) {
        unsigned char c = *p;
        if (c < 0x20 || c == '<' || c == '>' || c == ':' || c == '"'
                || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
            return true;
    }
    size_t len = strlen(base);
    return len == 0 || base[len - 1] == '.' || base[len - 1] == ' ';
}

static const char *strip_session_prefix(const char *base, char *expected_pfx, size_t cap)
{
    char pid_pfx[64];
    snprintf(pid_pfx, sizeof(pid_pfx), "%sp%" PRIu64 ".",
        SAVEDIR_MEMORY_PREFIX, (uint64_t)getpid());
    size_t n = strlen(pid_pfx);
    assert(strncmp(base, pid_pfx, n) == 0);
    const char *p = base + n;
    for (int i = 0; i < 16; i++) {
        char c = p[i];
        assert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
    assert(p[16] == '.');
    size_t full_len = n + 17;
    assert(full_len < cap);
    memcpy(expected_pfx, base, full_len);
    expected_pfx[full_len] = '\0';
    return base + full_len;
}

int main(void)
{
    char tmpl[] = "/tmp/xsystem4_memsave_XXXXXX";
    char *dir = mkdtemp(tmpl);
    assert(dir != NULL);
    config.save_dir = strdup(dir);
    sys4_set_text_gbk(true);

    // 0. Concurrent-instance isolation + PID-reuse crash recovery:
    //    - live_other (parent PID) must be preserved
    //    - dead_other (reaped child PID) must be swept
    //    - same_pid_stale (same PID with a stale nonce from a prior crashed PID-reused process) must be swept
    pid_t dead_pid = fork();
    assert(dead_pid >= 0);
    if (dead_pid == 0)
        _exit(0);
    int st = 0;
    assert(waitpid(dead_pid, &st, 0) == dead_pid);

    uint8_t payload[] = "replay-snapshot-ok";
    char live_other[512], dead_other[512], same_pid_stale[512];
    snprintf(live_other, sizeof(live_other), "%s/%sp%" PRIu64 ".0000000000000001.LiveOther.asd",
        dir, SAVEDIR_MEMORY_PREFIX, (uint64_t)getppid());
    snprintf(dead_other, sizeof(dead_other), "%s/%sp%" PRIu64 ".0000000000000001.DeadCrash.asd",
        dir, SAVEDIR_MEMORY_PREFIX, (uint64_t)dead_pid);
    snprintf(same_pid_stale, sizeof(same_pid_stale), "%s/%sp%" PRIu64 ".0000000000000001.ReusedPidCrash.asd",
        dir, SAVEDIR_MEMORY_PREFIX, (uint64_t)getpid());
    assert(file_write(live_other, payload, 4));
    assert(file_write(dead_other, payload, 4));
    assert(file_write(same_pid_stale, payload, 4));

    // Starting session 1 sweeps dead_other AND same_pid_stale, while preserving live_other!
    savedir_begin_memory_session();
    assert(!file_exists(dead_other));
    assert(!file_exists(same_pid_stale));
    assert(file_exists(live_other));

    // 1. Escalayer Reboot replay snapshot maps to a per-session flat hidden dotfile with no '<' or '>'.
    char pfx[64], pfx2[64];
    char *p_esca = savedir_path("<memory>MemoryMode_EscalayerReboot_.asd");
    assert(p_esca != NULL);
    assert(savedir_is_memory_save_path(p_esca));
    assert(strncmp(p_esca, dir, strlen(dir)) == 0);
    assert(p_esca[strlen(dir)] == '/');
    const char *base_esca = p_esca + strlen(dir) + 1;
    assert(strcmp(strip_session_prefix(base_esca, pfx, sizeof(pfx)),
        "MemoryMode_EscalayerReboot_.asd") == 0);
    assert(!has_illegal_fat_chars(base_esca));
    assert(!savedir_file_exists("<memory>MemoryMode_EscalayerReboot_.asd"));

    // Write via mapped path and verify exists + case-insensitive lookup + copy + delete.
    assert(file_write(p_esca, payload, sizeof(payload) - 1));
    assert(savedir_file_exists("<memory>MemoryMode_EscalayerReboot_.asd"));
    assert(savedir_file_exists("<MEMORY>memorymode_escalayerreboot_.ASD"));
    assert(savedir_file_exists("./<memory>MemoryMode_EscalayerReboot_.asd"));

    char *p_icase = savedir_path("<MEMORY>memorymode_escalayerreboot_.ASD");
    assert(strcmp(p_icase, p_esca) == 0);
    free(p_icase);

    // Leading config.save_dir prefix before <memory> is stripped before mapping.
    char prefixed[512];
    snprintf(prefixed, sizeof(prefixed), "%s/<memory>MemoryMode_EscalayerReboot_.asd", dir);
    char *p_prefixed = savedir_path(prefixed);
    assert(strcmp(p_prefixed, p_esca) == 0);
    free(p_prefixed);

    // Copy from <memory> save to a normal disk save and verify content.
    char *p_normal = savedir_path("SaveData01.asd");
    assert(!savedir_is_memory_save_path(p_normal));
    assert(file_copy(p_esca, p_normal));
    assert(savedir_file_exists("SaveData01.asd"));

    // 2. Traversal, slashes, backslashes, special chars, trailing dot, empty suffix.
    char *p_slash = savedir_path("<memory>sub/../../evil:name?.asd");
    char *p_bslash = savedir_path("<memory>sub\\..\\..\\evil:name?.asd");
    assert(strcmp(p_slash, p_bslash) == 0);
    const char *base_slash = p_slash + strlen(dir) + 1;
    assert(strchr(base_slash, '/') == NULL);
    assert(!has_illegal_fat_chars(base_slash));
    assert(savedir_is_memory_save_path(p_slash));
    free(p_slash);
    free(p_bslash);

    char *p_dot = savedir_path("<memory>trailing.");
    const char *base_dot = p_dot + strlen(dir) + 1;
    assert(!has_illegal_fat_chars(base_dot));
    assert(strcmp(strip_session_prefix(base_dot, pfx2, sizeof(pfx2)), "trailing%2e") == 0);
    assert(strcmp(pfx, pfx2) == 0);
    free(p_dot);

    char *p_empty = savedir_path("<memory>");
    char *p_lit_empty = savedir_path("<memory>_empty");
    char *p_tilde_empty = savedir_path("<memory>~empty");
    const char *base_empty = p_empty + strlen(dir) + 1;
    const char *base_lit_empty = p_lit_empty + strlen(dir) + 1;
    const char *base_tilde_empty = p_tilde_empty + strlen(dir) + 1;
    assert(!has_illegal_fat_chars(base_empty));
    assert(strcmp(strip_session_prefix(base_empty, pfx2, sizeof(pfx2)), "~empty") == 0);
    assert(strcmp(strip_session_prefix(base_lit_empty, pfx2, sizeof(pfx2)), "_empty") == 0);
    assert(strcmp(strip_session_prefix(base_tilde_empty, pfx2, sizeof(pfx2)), "%7eempty") == 0);
    assert(strcmp(p_empty, p_lit_empty) != 0);
    assert(strcmp(p_empty, p_tilde_empty) != 0);
    free(p_empty);
    free(p_lit_empty);
    free(p_tilde_empty);

    // 3. Long names (>140 bytes) stay bounded (<190 bytes) and distinct via hash.
    char long1[300], long2[300], long1_upper[300];
    memcpy(long1, "<memory>", 8);
    memset(long1 + 8, 'a', 220);
    strcpy(long1 + 228, "_1.asd");
    memcpy(long2, "<memory>", 8);
    memset(long2 + 8, 'a', 220);
    strcpy(long2 + 228, "_2.asd");
    memcpy(long1_upper, "<MEMORY>", 8);
    memset(long1_upper + 8, 'A', 220);
    strcpy(long1_upper + 228, "_1.ASD");

    char *p_long1 = savedir_path(long1);
    char *p_long2 = savedir_path(long2);
    const char *b_long1 = p_long1 + strlen(dir) + 1;
    const char *b_long2 = p_long2 + strlen(dir) + 1;
    assert(strlen(b_long1) < 190);
    assert(strlen(b_long2) < 190);
    assert(strcmp(b_long1, b_long2) != 0);
    assert(!has_illegal_fat_chars(b_long1));
    assert(file_write(p_long1, payload, 4));
    assert(savedir_file_exists(long1_upper));
    free(p_long1);
    free(p_long2);

    // 4. Create stray atomic .tmp and .switch-old memory artifacts for THIS session and verify
    //    teardown removes only THIS session's artifacts while preserving live_other and normal saves.
    char tmp_art[512], old_art[512];
    snprintf(tmp_art, sizeof(tmp_art), "%s/%s.tmp", dir, base_esca);
    snprintf(old_art, sizeof(old_art), "%s/%s.switch-old", dir, base_esca);
    assert(file_write(tmp_art, payload, 3));
    assert(file_write(old_art, payload, 3));

    savedir_cleanup_memory_saves();
    assert(!savedir_file_exists("<memory>MemoryMode_EscalayerReboot_.asd"));
    assert(!file_exists(p_esca));
    assert(!file_exists(tmp_art));
    assert(!file_exists(old_art));
    assert(file_exists(live_other));
    assert(savedir_file_exists("SaveData01.asd"));
    assert(file_exists(p_normal));

    // 5. Starting a second session in the same process (e.g. iOS re-launch) sweeps any leftover
    //    from session 1 while still keeping live_other intact.
    assert(file_write(p_esca, payload, 4));
    savedir_begin_memory_session();
    assert(!file_exists(p_esca));
    assert(file_exists(live_other));
    char *p_esca_s2 = savedir_path("<memory>MemoryMode_EscalayerReboot_.asd");
    assert(strcmp(p_esca_s2, p_esca) != 0);
    assert(strcmp(strip_session_prefix(p_esca_s2 + strlen(dir) + 1, pfx2, sizeof(pfx2)),
        "MemoryMode_EscalayerReboot_.asd") == 0);
    assert(strcmp(pfx, pfx2) != 0);

    // 5b. savedir_memory_find(): wildcard + case-insensitive exact + long hashed (>140B) +
    //     GBK-mode CP932 Japanese round-trip, while hiding other PIDs and deleted snapshots.
    assert(file_write(p_esca_s2, payload, 4));

    // Wildcard match returns exact original logical name and ignores live_other (different PID).
    int fcnt = 0;
    char *found = NULL;
    assert(savedir_memory_find("<memory>MemoryMode_*.asd", &fcnt, &found) == 1);
    assert(strcmp(found, "<memory>MemoryMode_EscalayerReboot_.asd") == 0);
    free(found);
    found = NULL;
    fcnt++;
    assert(savedir_memory_find("<memory>MemoryMode_*.asd", &fcnt, &found) == 0);

    // Case-insensitive exact match works consistently with savedir_path().
    fcnt = 0;
    assert(savedir_memory_find("<MEMORY>memorymode_escalayerreboot_.ASD", &fcnt, &found) == 1);
    assert(strcmp(found, "<memory>MemoryMode_EscalayerReboot_.asd") == 0);
    char *reopened = savedir_path(found);
    assert(strcmp(reopened, p_esca_s2) == 0);
    free(reopened);
    free(found);
    found = NULL;

    // Non-<memory> patterns return -1 so vmFile_Find handles disk enumeration.
    fcnt = 0;
    assert(savedir_memory_find("*.asd", &fcnt, &found) == -1);

    // GBK-mode CP932 Japanese snapshot (<memory>戦闘.asd -> 90 ed 93 ac) round-trips via Find -> savedir_path().
    const char cp932_sentou[] = "<memory>\x90\xed\x93\xac.asd";
    char *p_sentou = savedir_path(cp932_sentou);
    assert(file_write(p_sentou, payload, 4));
    fcnt = 0;
    assert(savedir_memory_find("<memory>\x90\xed*.asd", &fcnt, &found) == 1);
    assert(strcmp(found, cp932_sentou) == 0);
    reopened = savedir_path(found);
    assert(strcmp(reopened, p_sentou) == 0);
    assert(file_exists(reopened));
    free(reopened);
    free(found);
    found = NULL;

    // Long (>140 encoded bytes) hashed snapshot (16 Japanese CP932 chars + .asd) is found by BOTH
    // exact query and <memory>*.asd, and the returned name reopens the exact hashed disk file.
    char long_jp[128];
    strcpy(long_jp, "<memory>");
    for (int i = 0; i < 8; i++)
        strcat(long_jp, "\x90\xed\x93\xac"); // 16 Japanese chars = 144 percent-encoded bytes > 140
    strcat(long_jp, ".asd");
    char *p_long_jp = savedir_path(long_jp);
    assert(strchr(p_long_jp, '~') != NULL); // confirms hashed truncation on disk
    assert(file_write(p_long_jp, payload, 4));
    fcnt = 0;
    assert(savedir_memory_find(long_jp, &fcnt, &found) == 1);
    assert(strcmp(found, long_jp) == 0);
    reopened = savedir_path(found);
    assert(strcmp(reopened, p_long_jp) == 0);
    free(reopened);
    free(found);
    found = NULL;

    // Wildcard <memory>*.asd enumerates all 3 active snapshots including the hashed long_jp one.
    int seen_esca = 0, seen_sentou = 0, seen_long_jp = 0;
    for (fcnt = 0; savedir_memory_find("<memory>*.asd", &fcnt, &found) == 1; fcnt++) {
        if (!strcmp(found, "<memory>MemoryMode_EscalayerReboot_.asd")) seen_esca++;
        else if (!strcmp(found, cp932_sentou)) seen_sentou++;
        else if (!strcmp(found, long_jp)) seen_long_jp++;
        else assert(!"unexpected memory find match");
        free(found);
        found = NULL;
    }
    assert(seen_esca == 1 && seen_sentou == 1 && seen_long_jp == 1);

    // Deleted snapshots are immediately omitted from savedir_memory_find().
    remove_utf8(p_sentou);
    remove_utf8(p_long_jp);
    remove_utf8(p_esca_s2);
    fcnt = 0;
    assert(savedir_memory_find("<memory>*.asd", &fcnt, &found) == 0);
    free(p_sentou);
    free(p_long_jp);
    free(p_esca_s2);

    // 6. Fallback when config.save_dir is unset still never leaks '<' or '>'.
    free(config.save_dir);
    config.save_dir = NULL;
    char *p_nodir = savedir_path("<memory>Fallback.asd");
    assert(strchr(p_nodir, '<') == NULL && strchr(p_nodir, '>') == NULL);
    assert(savedir_is_memory_save_path(p_nodir));
    free(p_nodir);

    // Cleanup temp directory
    remove_utf8(live_other);
    remove_utf8(p_normal);
    free(p_normal);
    free(p_esca);
    rmdir(dir);
    return 0;
}
"""
        compile_and_run(c_code, [ROOT / "src/util.c", *LIBSYS4_CORE])

    def test_switch_relaunch_sweeps_first_session_leftover(self) -> None:
        c_code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "system4.h"
#include "system4/file.h"
#include "system4/utfsjis.h"
#include "xsystem4.h"

struct config config = { 0 };
bool dbg_dap = false;
void dbg_dap_log(const char *log, const char *fmt, va_list ap) { (void)log; (void)fmt; (void)ap; }

int main(void)
{
    char tmpl[] = "/tmp/xsystem4_switch_relaunch_XXXXXX";
    char *dir = mkdtemp(tmpl);
    assert(dir != NULL);
    config.save_dir = strdup(dir);

    // Simulate a leftover p1 snapshot from an unclean Switch exit before relaunch.
    uint8_t stale[] = "stale-switch-replay";
    char stale_path[512];
    snprintf(stale_path, sizeof(stale_path), "%s/%sp1.0000000000000001.MemoryMode_EscalayerReboot_.asd",
        dir, SAVEDIR_MEMORY_PREFIX);
    assert(file_write(stale_path, stale, sizeof(stale) - 1));

    char *normal_path = savedir_path("SaveData01.asd");
    assert(file_write(normal_path, stale, 4));

    // Fresh Switch launch begins session 1 (also pid=1, seq=1) and must sweep stale p1.1.
    savedir_begin_memory_session();
    assert(!file_exists(stale_path));
    assert(!savedir_file_exists("<memory>MemoryMode_EscalayerReboot_.asd"));
    assert(savedir_file_exists("SaveData01.asd"));

    remove_utf8(normal_path);
    free(normal_path);
    free(config.save_dir);
    rmdir(dir);
    return 0;
}
"""
        compile_and_run(c_code, [ROOT / "src/util.c", *LIBSYS4_CORE], ["-D__SWITCH__"])

    def test_memory_save_globals_roundtrip_via_savedata(self) -> None:
        c_code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "savedata.h"
#include "xsystem4.h"

struct config config = { 0 };
struct ain *ain = NULL;
union vm_value *stack = NULL;
int32_t stack_ptr = 0;
bool dbg_dap = false;
bool game_maman = false;
bool game_haharanman = false;
bool game_escalayer_reboot = true;
bool game_pascha3 = false;
bool game_night_remaster = false;
bool game_rance01 = false;
bool game_haruka = false;
bool game_rance10 = false;
bool game_dohnadohna = false;
bool game_evenicle2 = false;
bool game_evenicle = false;
bool game_rance02 = false;

void dbg_dap_log(const char *log, const char *fmt, va_list ap) { (void)log; (void)fmt; (void)ap; }
void modifier_on_config_loaded(void) {}
void modifier_on_globals_loaded(void) {}
_Noreturn void _vm_error(const char *fmt, ...) { (void)fmt; abort(); }
union vm_value global_get(int i) { return heap[0].page->values[i]; }
void global_set(int i, union vm_value val, bool dtors) { (void)dtors; heap[0].page->values[i] = val; }
union vm_value stack_pop(void) { abort(); }
union vm_value vm_copy(union vm_value val, enum ain_data_type type) { (void)val; (void)type; abort(); }
void vm_call(int fno, int struct_page) { (void)fno; (void)struct_page; abort(); }
struct string *heap_get_delegate_file(int slot) { (void)slot; return NULL; }

int main(void)
{
    char tmpl[] = "/tmp/xsystem4_memsave_gs_XXXXXX";
    char *dir = mkdtemp(tmpl);
    assert(dir != NULL);
    config.save_dir = strdup(dir);
    sys4_set_text_gbk(true);

    // Build a minimal AIN + global page with one int global in group "MEM_GRP".
    struct ain a;
    memset(&a, 0, sizeof(a));
    a.version = 6;
    a.minor_version = 0;
    a.nr_globals = 1;
    struct ain_variable gvar;
    memset(&gvar, 0, sizeof(gvar));
    gvar.name = "g_ReplayFlag";
    gvar.type.data = AIN_INT;
    gvar.group_index = 0;
    a.globals = &gvar;
    a.nr_global_groups = 1;
    char *gname = "MEM_GRP";
    a.global_group_names = &gname;
    ain = &a;

    heap_init();
    heap[0].ref = 1;
    heap_set_page(0, alloc_page(GLOBAL_PAGE, 0, 1));
    heap[0].page->values[0].i = 4242;

    int n = 0;
    const char *mem_name = "<memory>MemoryMode_EscalayerReboot_.asd";
    assert(save_globals("ESCA_KEY", mem_name, "MEM_GRP", &n) == 1);
    assert(n == 1);
    assert(savedir_file_exists(mem_name));

    // Verify no literal '<' file and no '.bak' file was created.
    char *mapped = savedir_path(mem_name);
    assert(savedir_is_memory_save_path(mapped));
    assert(file_exists(mapped));
    char bak[512];
    snprintf(bak, sizeof(bak), "%s.bak", mapped);
    assert(!file_exists(bak));

    // Saving <memory>foo.asd must NOT collide with a distinct <memory>foo.asd.tmp snapshot
    // via make_atomic_tmp_path() (atomic temp uses "..alicehub-mem.*.tmp").
    const char *mem_tmp_name = "<memory>MemoryMode_EscalayerReboot_.asd.tmp";
    heap[0].page->values[0].i = 9999;
    assert(save_globals("ESCA_KEY", mem_tmp_name, "MEM_GRP", &n) == 1);
    heap[0].page->values[0].i = 4242;
    assert(save_globals("ESCA_KEY", mem_name, "MEM_GRP", &n) == 1);
    assert(savedir_file_exists(mem_tmp_name));
    assert(savedir_file_exists(mem_name));
    heap[0].page->values[0].i = 0;
    assert(load_globals("ESCA_KEY", mem_tmp_name, "MEM_GRP", &n) == 1);
    assert(heap[0].page->values[0].i == 9999);
    assert(delete_save_file(mem_tmp_name) == 1);

    // Mutate in-memory global, reload from <memory>, confirm value restored.
    heap[0].page->values[0].i = 0;
    n = 0;
    assert(load_globals("ESCA_KEY", mem_name, "MEM_GRP", &n) == 1);
    assert(n == 1);
    assert(heap[0].page->values[0].i == 4242);

    // Delete via delete_save_file() and confirm gone.
    assert(delete_save_file(mem_name) == 1);
    assert(!savedir_file_exists(mem_name));
    assert(!file_exists(mapped));

    free(mapped);
    free(config.save_dir);
    rmdir(dir);
    return 0;
}
"""
        sdl_flags = subprocess.check_output(["pkg-config", "--cflags", "sdl2"], text=True).split()
        compile_and_run(
            c_code,
            [
                ROOT / "src/savedata.c",
                ROOT / "src/util.c",
                ROOT / "src/heap.c",
                ROOT / "src/page.c",
                ROOT / "src/vm_v12.c",
                ROOT / "src/cJSON.c",
                *LIBSYS4_SAVEFILE,
            ],
            [*sdl_flags, "-DRELEASE", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections"],
        )

    def test_dialog_text_to_utf8_mixed_and_chinese(self) -> None:
        c_code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/utfsjis.h"

static void check_decode(const char *label, const uint8_t *raw, size_t len, const char *expected_utf8)
{
    char *got = cjk_dialog_text_to_utf8((const char *)raw, len);
    if (!got || strcmp(got, expected_utf8) != 0) {
        fprintf(stderr, "FAIL [%s]:\n  got: %s\n  exp: %s\n",
                label, got ? got : "(null)", expected_utf8);
        free(got);
        abort();
    }
    free(got);
}

int main(void)
{
    // GBK mode (CN / partial-CN releases such as Escalayer Reboot).
    sys4_set_text_gbk(true);

    // 1. Exact Escalayer Reboot replay save-failure popup (CP932 bytes in GBK mode).
    const uint8_t esca_cp932[] = {
        // セーブファイル\n
        0x83, 0x5a, 0x81, 0x5b, 0x83, 0x75, 0x83, 0x74, 0x83, 0x40, 0x83, 0x43, 0x83, 0x8b, 0x0a,
        // 【 <memory>MemoryMode_EscalayerReboot_.asd 】\n
        0x81, 0x79, ' ', '<', 'm', 'e', 'm', 'o', 'r', 'y', '>',
        'M', 'e', 'm', 'o', 'r', 'y', 'M', 'o', 'd', 'e', '_',
        'E', 's', 'c', 'a', 'l', 'a', 'y', 'e', 'r', 'R', 'e', 'b', 'o', 'o', 't', '_', '.', 'a', 's', 'd',
        ' ', 0x81, 0x7a, 0x0a,
        // の保存に失敗しました
        0x82, 0xcc, 0x95, 0xdb, 0x91, 0xb6, 0x82, 0xc9, 0x8e, 0xb8,
        0x94, 0x73, 0x82, 0xb5, 0x82, 0xdc, 0x82, 0xb5, 0x82, 0xbd, 0x00
    };
    const char *esca_utf8 =
        "\xe3\x82\xbb\xe3\x83\xbc\xe3\x83\x96\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab\n"
        "\xe3\x80\x90 <memory>MemoryMode_EscalayerReboot_.asd \xe3\x80\x91\n"
        "\xe3\x81\xae\xe4\xbf\x9d\xe5\xad\x98\xe3\x81\xab\xe5\xa4\xb1\xe6\x95\x97\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f";
    check_decode("esca_len_explicit", esca_cp932, sizeof(esca_cp932) - 1, esca_utf8);
    check_decode("esca_len_zero", esca_cp932, 0, esca_utf8);

    // 2. CP932 quit prompt & CP932-only symbol (㈱ 0x87 0x8a, not present in GBK).
    const uint8_t quit_cp932[] = {
        // 終了しますか？
        0x8f, 0x49, 0x97, 0xb9, 0x82, 0xb5, 0x82, 0xdc, 0x82, 0xb7, 0x82, 0xa9, 0x81, 0x48, 0x00
    };
    check_decode("quit_cp932", quit_cp932, sizeof(quit_cp932) - 1,
        "\xe7\xb5\x82\xe4\xba\x86\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x99\xe3\x81\x8b\xef\xbc\x9f");

    const uint8_t quit_plain_cp932[] = {
        // 本当に終了する？ (standalone plain-form CP932 confirmation without 4-kana run)
        0x96, 0x7b, 0x93, 0x96, 0x82, 0xc9, 0x8f, 0x49, 0x97, 0xb9, 0x82, 0xb7, 0x82, 0xe9, 0x81, 0x48, 0x00
    };
    check_decode("quit_plain_cp932", quit_plain_cp932, sizeof(quit_plain_cp932) - 1,
        "\xe6\x9c\xac\xe5\xbd\x93\xe3\x81\xab\xe7\xb5\x82\xe4\xba\x86\xe3\x81\x99\xe3\x82\x8b\xef\xbc\x9f");

    const uint8_t non_gbk_cp932[] = {
        // セーブ㈱に失敗しました
        0x83, 0x5a, 0x81, 0x5b, 0x83, 0x75, 0x87, 0x8a,
        0x82, 0xc9, 0x8e, 0xb8, 0x94, 0x73, 0x82, 0xb5, 0x82, 0xdc, 0x82, 0xb5, 0x82, 0xbd, 0x00
    };
    check_decode("cp932_only_symbol", non_gbk_cp932, sizeof(non_gbk_cp932) - 1,
        "\xe3\x82\xbb\xe3\x83\xbc\xe3\x83\x96\xe3\x88\xb1\xe3\x81\xab\xe5\xa4\xb1\xe6\x95\x97\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f");

    // 3. Pure Simplified Chinese GBK dialog remains untouched.
    const uint8_t sc_gbk[] = {
        // 存档保存失败
        0xb4, 0xe6, 0xb5, 0xb5, 0xb1, 0xa3, 0xb4, 0xe6, 0xca, 0xa7, 0xb0, 0xdc, 0x00
    };
    check_decode("sc_gbk", sc_gbk, sizeof(sc_gbk) - 1,
        "\xe5\xad\x98\xe6\xa1\xa3\xe4\xbf\x9d\xe5\xad\x98\xe5\xa4\xb1\xe8\xb4\xa5");

    // 4. Traditional Chinese GBK containing dual-valid extension pairs remains untouched
    //    (including isolated 2-char GBK names whose bytes happen to overlap Shift-JIS kana).
    const uint8_t tc_gbk[] = {
        // 傑克已加入隊伍
        0x82, 0xdc, 0xbf, 0xcb, 0xd2, 0xd1, 0xbc, 0xd3, 0xc8, 0xeb, 0xea, 0xa0, 0xce, 0xe9, 0x00
    };
    check_decode("tc_gbk_with_82dc", tc_gbk, sizeof(tc_gbk) - 1,
        "\xe5\x82\x91\xe5\x85\x8b\xe5\xb7\xb2\xe5\x8a\xa0\xe5\x85\xa5\xe9\x9a\x8a\xe4\xbc\x8d");

    const uint8_t gbk_jiejie[] = {
        // 傑傑 (82 dc 82 dc -> must NOT be misdecoded as まま)
        0x82, 0xdc, 0x82, 0xdc, 0x00
    };
    check_decode("gbk_dual_valid_jiejie", gbk_jiejie, sizeof(gbk_jiejie) - 1,
        "\xe5\x82\x91\xe5\x82\x91");

    const uint8_t gbk_jiejie4[] = {
        // 傑傑傑傑 (82 dc x4 -> must NOT be misdecoded as まままま)
        0x82, 0xdc, 0x82, 0xdc, 0x82, 0xdc, 0x82, 0xdc, 0x00
    };
    check_decode("gbk_dual_valid_jiejie4", gbk_jiejie4, sizeof(gbk_jiejie4) - 1,
        "\xe5\x82\x91\xe5\x82\x91\xe5\x82\x91\xe5\x82\x91");

    const uint8_t gbk_jieyi_save[] = {
        // 傑儀, 存档 (82 dc 83 78 2c 20 b4 e6 b5 b5 -> must NOT become まベ, 存档)
        0x82, 0xdc, 0x83, 0x78, ',', ' ', 0xb4, 0xe6, 0xb5, 0xb5, 0x00
    };
    check_decode("gbk_dual_valid_jieyi", gbk_jieyi_save, sizeof(gbk_jieyi_save) - 1,
        "\xe5\x82\x91\xe5\x84\x80, \xe5\xad\x98\xe6\xa1\xa3");

    const uint8_t gbk_counterfeit_detector[] = {
        // 偽鈔偵測機 (82 ce e2 6e 82 c9 9c 79 99 43 -> 5-pair dual-valid GBK must stay Chinese)
        0x82, 0xce, 0xe2, 0x6e, 0x82, 0xc9, 0x9c, 0x79, 0x99, 0x43, 0x00
    };
    check_decode("gbk_five_pair_dual_valid", gbk_counterfeit_detector,
        sizeof(gbk_counterfeit_detector) - 1,
        "\xe5\x81\xbd\xe9\x88\x94\xe5\x81\xb5\xe6\xb8\xac\xe6\xa9\x9f");

    // 5. Mixed GBK Chinese filename (both GB2312 and dual-valid GBK extension) embedded inside
    //    a CP932 system error message: Japanese prompt decodes while Chinese filename is preserved.
    const uint8_t mixed[] = {
        // セーブ 【 存档.asd 】 の保存に失敗しました
        0x83, 0x5a, 0x81, 0x5b, 0x83, 0x75, ' ', 0x81, 0x79, ' ',
        0xb4, 0xe6, 0xb5, 0xb5, '.', 'a', 's', 'd', ' ', 0x81, 0x7a, ' ',
        0x82, 0xcc, 0x95, 0xdb, 0x91, 0xb6, 0x82, 0xc9, 0x8e, 0xb8, 0x94, 0x73,
        0x82, 0xb5, 0x82, 0xdc, 0x82, 0xb5, 0x82, 0xbd, 0x00
    };
    check_decode("mixed_gbk_cp932", mixed, sizeof(mixed) - 1,
        "\xe3\x82\xbb\xe3\x83\xbc\xe3\x83\x96 \xe3\x80\x90 \xe5\xad\x98\xe6\xa1\xa3.asd \xe3\x80\x91 "
        "\xe3\x81\xae\xe4\xbf\x9d\xe5\xad\x98\xe3\x81\xab\xe5\xa4\xb1\xe6\x95\x97\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f");

    const uint8_t mixed_jieyi[] = {
        // セーブファイル 【 傑儀.asd 】 の保存に失敗しました
        0x83, 0x5a, 0x81, 0x5b, 0x83, 0x75, 0x83, 0x74, 0x83, 0x40, 0x83, 0x43, 0x83, 0x8b,
        ' ', 0x81, 0x79, ' ', 0x82, 0xdc, 0x83, 0x78, '.', 'a', 's', 'd', ' ', 0x81, 0x7a, ' ',
        0x82, 0xcc, 0x95, 0xdb, 0x91, 0xb6, 0x82, 0xc9, 0x8e, 0xb8, 0x94, 0x73,
        0x82, 0xb5, 0x82, 0xdc, 0x82, 0xb5, 0x82, 0xbd, 0x00
    };
    check_decode("mixed_dual_valid_gbk_in_cp932", mixed_jieyi, sizeof(mixed_jieyi) - 1,
        "\xe3\x82\xbb\xe3\x83\xbc\xe3\x83\x96\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab "
        "\xe3\x80\x90 \xe5\x82\x91\xe5\x84\x80.asd \xe3\x80\x91 "
        "\xe3\x81\xae\xe4\xbf\x9d\xe5\xad\x98\xe3\x81\xab\xe5\xa4\xb1\xe6\x95\x97\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f");

    const uint8_t mixed_short_save[] = {
        // セーブ 【 存档.asd 】 の保存に失敗 (short clause without ファイル/しました)
        0x83, 0x5a, 0x81, 0x5b, 0x83, 0x75, ' ', 0x81, 0x79, ' ',
        0xb4, 0xe6, 0xb5, 0xb5, '.', 'a', 's', 'd', ' ', 0x81, 0x7a, ' ',
        0x82, 0xcc, 0x95, 0xdb, 0x91, 0xb6, 0x82, 0xc9, 0x8e, 0xb8, 0x94, 0x73, 0x00
    };
    check_decode("mixed_short_save_alert", mixed_short_save, sizeof(mixed_short_save) - 1,
        "\xe3\x82\xbb\xe3\x83\xbc\xe3\x83\x96 \xe3\x80\x90 \xe5\xad\x98\xe6\xa1\xa3.asd \xe3\x80\x91 "
        "\xe3\x81\xae\xe4\xbf\x9d\xe5\xad\x98\xe3\x81\xab\xe5\xa4\xb1\xe6\x95\x97");

    const uint8_t mixed_short_jieyi[] = {
        // セーブ 【 傑儀.asd 】 の保存に失敗
        0x83, 0x5a, 0x81, 0x5b, 0x83, 0x75, ' ', 0x81, 0x79, ' ',
        0x82, 0xdc, 0x83, 0x78, '.', 'a', 's', 'd', ' ', 0x81, 0x7a, ' ',
        0x82, 0xcc, 0x95, 0xdb, 0x91, 0xb6, 0x82, 0xc9, 0x8e, 0xb8, 0x94, 0x73, 0x00
    };
    check_decode("mixed_short_dual_valid_gbk", mixed_short_jieyi, sizeof(mixed_short_jieyi) - 1,
        "\xe3\x82\xbb\xe3\x83\xbc\xe3\x83\x96 \xe3\x80\x90 \xe5\x82\x91\xe5\x84\x80.asd \xe3\x80\x91 "
        "\xe3\x81\xae\xe4\xbf\x9d\xe5\xad\x98\xe3\x81\xab\xe5\xa4\xb1\xe6\x95\x97");

    const uint8_t mixed_8140_gbk[] = {
        // セーブ 丂.asd (GBK '丂' = 81 40 must NOT become Shift-JIS ideographic space)
        0x83, 0x5a, 0x81, 0x5b, 0x83, 0x75, ' ', 0x81, 0x40, '.', 'a', 's', 'd', 0x00
    };
    check_decode("mixed_8140_gbk_preserved", mixed_8140_gbk, sizeof(mixed_8140_gbk) - 1,
        "\xe3\x82\xbb\xe3\x83\xbc\xe3\x83\x96 \xe4\xb8\x82.asd");

    const uint8_t indented_bracket_cp932[] = {
        // セーブファイル\n　【 slot01.asd 】\nの保存に失敗しました (81 40 + 81 79 -> 　【)
        0x83, 0x5a, 0x81, 0x5b, 0x83, 0x75, 0x83, 0x74, 0x83, 0x40, 0x83, 0x43, 0x83, 0x8b, 0x0a,
        0x81, 0x40, 0x81, 0x79, ' ', 's', 'l', 'o', 't', '0', '1', '.', 'a', 's', 'd', ' ', 0x81, 0x7a, 0x0a,
        0x82, 0xcc, 0x95, 0xdb, 0x91, 0xb6, 0x82, 0xc9, 0x8e, 0xb8,
        0x94, 0x73, 0x82, 0xb5, 0x82, 0xdc, 0x82, 0xb5, 0x82, 0xbd, 0x00
    };
    check_decode("indented_bracket_cp932", indented_bracket_cp932, sizeof(indented_bracket_cp932) - 1,
        "\xe3\x82\xbb\xe3\x83\xbc\xe3\x83\x96\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab\n"
        "\xe3\x80\x80\xe3\x80\x90 slot01.asd \xe3\x80\x91\n"
        "\xe3\x81\xae\xe4\xbf\x9d\xe5\xad\x98\xe3\x81\xab\xe5\xa4\xb1\xe6\x95\x97\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f");

    const uint8_t mixed_hw_file[] = {
        // セーブ ﾌｧｲﾙ
        0x83, 0x5a, 0x81, 0x5b, 0x83, 0x75, ' ', 0xcc, 0xa7, 0xb2, 0xd9, 0x00
    };
    check_decode("mixed_hw_kana_file", mixed_hw_file, sizeof(mixed_hw_file) - 1,
        "\xe3\x82\xbb\xe3\x83\xbc\xe3\x83\x96 \xef\xbe\x8c\xef\xbd\xa7\xef\xbd\xb2\xef\xbe\x99");

    const uint8_t mixed_hw_err[] = {
        // セーブ ｴﾗｰ.asd (odd-length half-width katakana followed by ASCII separator '.')
        0x83, 0x5a, 0x81, 0x5b, 0x83, 0x75, ' ', 0xb4, 0xd7, 0xb0, '.', 'a', 's', 'd', 0x00
    };
    check_decode("mixed_hw_kana_odd_err", mixed_hw_err, sizeof(mixed_hw_err) - 1,
        "\xe3\x82\xbb\xe3\x83\xbc\xe3\x83\x96 \xef\xbd\xb4\xef\xbe\x97\xef\xbd\xb0.asd");

    const uint8_t mixed_gbk_ascii_trail[] = {
        // セーブ 【 遊戲.asd 】 の保存に失敗 (遊 = df 5b has ASCII-range GBK trail byte '[')
        0x83, 0x5a, 0x81, 0x5b, 0x83, 0x75, ' ', 0x81, 0x79, ' ',
        0xdf, 0x5b, 0x91, 0xf2, '.', 'a', 's', 'd', ' ', 0x81, 0x7a, ' ',
        0x82, 0xcc, 0x95, 0xdb, 0x91, 0xb6, 0x82, 0xc9, 0x8e, 0xb8, 0x94, 0x73, 0x00
    };
    check_decode("mixed_gbk_ascii_trail_youxi", mixed_gbk_ascii_trail,
        sizeof(mixed_gbk_ascii_trail) - 1,
        "\xe3\x82\xbb\xe3\x83\xbc\xe3\x83\x96 \xe3\x80\x90 \xe9\x81\x8a\xe6\x88\xb2.asd \xe3\x80\x91 "
        "\xe3\x81\xae\xe4\xbf\x9d\xe5\xad\x98\xe3\x81\xab\xe5\xa4\xb1\xe6\x95\x97");

    const uint8_t mixed_gb2312_in_hw_range[] = {
        // セーブ 【 中文.asd 】 の保存に失敗 (中文 = d6 d0 ce c4 is all 0xA1..0xDF but MUST stay 中文)
        0x83, 0x5a, 0x81, 0x5b, 0x83, 0x75, ' ', 0x81, 0x79, ' ',
        0xd6, 0xd0, 0xce, 0xc4, '.', 'a', 's', 'd', ' ', 0x81, 0x7a, ' ',
        0x82, 0xcc, 0x95, 0xdb, 0x91, 0xb6, 0x82, 0xc9, 0x8e, 0xb8, 0x94, 0x73, 0x00
    };
    check_decode("mixed_gb2312_zhongwen_preserved", mixed_gb2312_in_hw_range,
        sizeof(mixed_gb2312_in_hw_range) - 1,
        "\xe3\x82\xbb\xe3\x83\xbc\xe3\x83\x96 \xe3\x80\x90 \xe4\xb8\xad\xe6\x96\x87.asd \xe3\x80\x91 "
        "\xe3\x81\xae\xe4\xbf\x9d\xe5\xad\x98\xe3\x81\xab\xe5\xa4\xb1\xe6\x95\x97");

    // 6. Bounded length without NUL terminator + empty string + NULL input.
    const uint8_t bounded[] = {
        0x83, 0x5a, 0x81, 0x5b, 0x83, 0x75, 0x83, 0x74, 0x83, 0x40, 0x83, 0x43, 0x83, 0x8b,
        'X', 'Y', 'Z'
    };
    check_decode("bounded_no_nul", bounded, 14,
        "\xe3\x82\xbb\xe3\x83\xbc\xe3\x83\x96\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab");
    check_decode("empty", (const uint8_t *)"", 0, "");
    assert(cjk_dialog_text_to_utf8(NULL, 0) == NULL);

    // 7. Standard SJIS mode (non-GBK) passes straight through to CP932 decoding.
    sys4_set_text_gbk(false);
    check_decode("sjis_mode", esca_cp932, sizeof(esca_cp932) - 1, esca_utf8);
    return 0;
}
"""
        compile_and_run(c_code, LIBSYS4_CORE)

    def test_wiring_in_vm_dialog_and_system4(self) -> None:
        vm_src = (ROOT / "src/vm.c").read_text(encoding="utf-8", errors="ignore")
        vmdialog_src = (ROOT / "src/hll/vmDialog.c").read_text(encoding="utf-8", errors="ignore")
        sys4_src = (ROOT / "src/system4.c").read_text(encoding="utf-8", errors="ignore")

        for case_name in ("case SYS_MSGBOX:", "case SYS_MSGBOX_OK_CANCEL:", "case SYS_ERROR:"):
            start = vm_src.index(case_name)
            snippet = vm_src[start:start + 350]
            self.assertIn("cjk_dialog_text_to_utf8(", snippet, f"{case_name} should use cjk_dialog_text_to_utf8")
            self.assertNotIn("mai_sjis2utf(", snippet, f"{case_name} should not bypass dialog decoder")

        copy_start = vm_src.index("case SYS_COPY_SAVE_FILE:")
        copy_snippet = vm_src[copy_start:copy_start + 500]
        self.assertIn("mkdir_p(", copy_snippet)

        vmfile_src = (ROOT / "src/hll/vmFile.c").read_text(encoding="utf-8", errors="ignore")
        self.assertIn("savedir_memory_find(", vmfile_src)
        self.assertIn("savedir_is_memory_save_path(", vmfile_src)

        self.assertIn("cjk_dialog_text_to_utf8(", vmdialog_src)
        self.assertNotIn("mai_sjis2utf(", vmdialog_src)

        self.assertIn("savedir_begin_memory_session()", sys4_src)
        self.assertIn("#if !defined(__ANDROID__) && !defined(ALICEHUB_IOS)", sys4_src)
        self.assertIn("atexit(savedir_cleanup_memory_saves);", sys4_src)
        self.assertGreaterEqual(sys4_src.count("savedir_cleanup_memory_saves()"), 4)


if __name__ == "__main__":
    unittest.main()
