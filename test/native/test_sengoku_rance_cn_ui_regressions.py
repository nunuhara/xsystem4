import shlex
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

_THIS_DIR = str(Path(__file__).resolve().parent)
if _THIS_DIR not in sys.path:
    sys.path.insert(0, _THIS_DIR)

from test_escalayer_reboot_regressions import ROOT, extract_function
from test_evenicle2_regressions import LIBSYS4_CORE, compile_and_run
from test_uncensored_overlay_regression import make_afa, make_ald, make_qnt_solid


def make_real_qnt_solid(w, h, r, g, b):
    """Build a valid QNT v1 24-bit solid-color image (even w, h)."""
    assert w % 2 == 0 and h % 2 == 0
    plane = w * h
    raw = bytearray(plane * 3)
    raw[0] = b
    raw[plane] = g
    raw[plane * 2] = r
    zpix = zlib.compress(bytes(raw), 9)
    hdr = struct.pack("<4sIIIIIIIIII", b"QNT\x00", 1, 48, 0, 0, w, h, 24, 1, len(zpix), 0) + b"\x00\x00\x00\x00"
    return hdr + zpix


def forge_crc32_bytes(data, target_len, target_crc):
    """Pad data with zeros to target_len - 4 and append 4 bytes so crc32 == target_crc."""
    assert len(data) <= target_len - 4
    prefix = data + b"\x00" * (target_len - 4 - len(data))
    state = target_crc ^ 0xFFFFFFFF
    for _ in range(32):
        if state & 0x80000000:
            state = (((state ^ 0xEDB88320) << 1) | 1) & 0xFFFFFFFF
        else:
            state = (state << 1) & 0xFFFFFFFF
    prefix_state = (zlib.crc32(prefix) & 0xFFFFFFFF) ^ 0xFFFFFFFF
    patch = struct.pack("<I", state ^ prefix_state)
    out = prefix + patch
    assert len(out) == target_len and (zlib.crc32(out) & 0xFFFFFFFF) == target_crc
    return out


class SengokuRanceChineseUiRegressionTest(unittest.TestCase):
    def compile_and_run(self, harness, extra_flags=()):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "test.c"
            exe = Path(tmp) / "test"
            source.write_text(harness)
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(ROOT / "subprojects/libsys4/include"), str(source),
                 *extra_flags, "-o", str(exe)],
                check=True,
            )
            subprocess.run([str(exe)], check=True)

    def test_native_codename_resolves_nested_directory_casing(self):
        harness = r"""
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "system4/file.h"
#include "system4/utfsjis.h"
#include "xsystem4.h"

struct config config;

static void check_path(const char *code_name, const char *expected, char *(*resolve)(const char *))
{
    char *path = resolve(code_name);
    assert(path && file_exists(path));
    assert(strcmp(path, expected) == 0);
    free(path);
}

int main(int argc, char **argv)
{
    assert(argc == 8);
    config.game_dir = argv[1];
    check_path("dAtA\\pAtChEs\\PATCH.AIN", argv[2], gamedir_path);
    check_path("dAtA\\pAtChEs\\PATCH.AIN", argv[2], gamedir_path_icase);
    check_path("LAUNCHERS\\eNtRiEs\\PATCH.AIN", argv[3], gamedir_path);
    check_path("dAtA\\pAtChEs\\", argv[4], gamedir_path);
    // Legacy CP932 CodeName (戦国ランス_alt.ain) resolves mojibake filename on disk (愴崙儔儞僗_alt.ain)
    sys4_set_text_gbk(false);
    sys4_set_source_utf8(false);
    char *sjis_repaired = utf2sjis("戦国ランス_alt.ain", 0);
    char *sjis_nested = utf2sjis("dAtA\\戦国ランス.AIN", 0);
    check_path(sjis_repaired, argv[6], gamedir_path);
    check_path(sjis_repaired, argv[6], gamedir_path_icase);
    check_path(sjis_nested, argv[7], gamedir_path_icase);
    free(sjis_repaired);
    free(sjis_nested);

    // UTF-8/GBK CodeName (ini_parse encodes UTF-8 to GBK) resolves both repaired and mojibake filenames
    sys4_set_text_gbk(true);
    sys4_set_source_utf8(true);
    char *gbk_repaired = utf2sjis("戦国ランス_alt.ain", 0);
    char *gbk_mojibake = utf2sjis("愴崙儔儞僗.ain", 0);
    char *gbk_nested = utf2sjis("dAtA\\愴崙儔儞僗.AIN", 0);
    check_path(gbk_mojibake, argv[5], gamedir_path);
    check_path(gbk_mojibake, argv[5], gamedir_path_icase);
    check_path(gbk_repaired, argv[6], gamedir_path);
    check_path(gbk_repaired, argv[6], gamedir_path_icase);
    check_path(gbk_nested, argv[7], gamedir_path_icase);
    free(gbk_repaired);
    free(gbk_mojibake);
    free(gbk_nested);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = root / "Data"
            data.mkdir()
            if (root / "DATA").exists():
                self.skipTest("requires a case-sensitive filesystem")
            script = data / "Patches" / "Patch.ain"
            script.parent.mkdir()
            script.touch()
            other_script = root / "Launchers" / "Entries" / "Patch.ain"
            other_script.parent.mkdir(parents=True)
            other_script.touch()
            exact_script = root / "LAUNCHERS" / "Entries" / "Patch.ain"
            exact_script.parent.mkdir(parents=True)
            exact_script.touch()
            repaired_script = root / "戦国ランス.ain"
            repaired_script.touch()
            mojibake_script = root / "愴崙儔儞僗_alt.ain"
            mojibake_script.touch()
            nested_repaired = data / "戦国ランス.ain"
            nested_repaired.touch()

            libsys4 = ROOT / "subprojects/libsys4"
            source = root / "test.c"
            source.write_text(harness)
            exe = root / "test"
            linker = ["-Wl,-dead_strip", "-framework", "CoreFoundation"] if sys.platform == "darwin" else ["-Wl,--gc-sections"]
            subprocess.run(
                ["cc", "-std=gnu11", "-ffunction-sections", "-fdata-sections",
                 "-I", str(ROOT / "include"), "-I", str(libsys4 / "include"),
                 str(source), str(ROOT / "src/util.c"),
                 *(str(libsys4 / "src" / name) for name in ("file.c", "system.c", "utfsjis.c")),
                 *linker, "-o", str(exe)],
                check=True,
            )
            subprocess.run(
                [str(exe), str(root), str(script), str(exact_script), f"{script.parent}/",
                 str(repaired_script), str(mojibake_script), str(nested_repaired)],
                check=True,
            )

    def test_skip_graphic_only_relabels_the_original_chinese_session_button(self):
        fix = extract_function("src/asset_manager.c", "localize_sengoku_skip_cg")
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "system4/archive.h"
#include "system4/cg.h"

static bool sys4_text_is_gbk;
static unsigned long crc32(unsigned long seed, const uint8_t *data, size_t size)
{
    assert(seed == 0 && size == 3451);
    return data[0] ? 0xdd729ed3 : 0;
}
""" + fix + r"""

int main(void)
{
    uint8_t data[] = {1};
    struct archive_data source = {.data = data, .size = 3451};
    uint8_t pixels[20][108][4];
    uint8_t original[sizeof(pixels)];
    memset(pixels, 127, sizeof(pixels));
    memcpy(original, pixels, sizeof(pixels));
    struct cg cg = {.type = ALCG_QNT, .metrics = {.w = 108, .h = 20}, .pixels = pixels};
    localize_sengoku_skip_cg(10005, &source, &cg);
    assert(!memcmp(pixels, original, sizeof(pixels)));
    sys4_text_is_gbk = true;
    localize_sengoku_skip_cg(10004, &source, &cg);
    assert(!memcmp(pixels, original, sizeof(pixels)));
    data[0] = 0;
    localize_sengoku_skip_cg(10005, &source, &cg);
    assert(!memcmp(pixels, original, sizeof(pixels)));

    data[0] = 1;
    localize_sengoku_skip_cg(10005, &source, &cg);
    for (int frame = 0; frame < 3; frame++) {
        int left = 36 * frame + 2 + (frame == 2);
        int top = 3 + (frame == 2);
        assert(pixels[top][left][0] == 226);
        assert(pixels[top][left + 3][0] == (frame == 1 ? 213 : 193));
        assert(pixels[top][left + 3][3] == 127);
        assert(pixels[top - 1][left][0] == 127);
        assert(pixels[top][left - 1][0] == 127);
    }
    return 0;
}
"""
        self.compile_and_run(harness)

    def test_alk_rejects_truncated_logo_entries(self):
        harness = r"""
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "system4/alk.h"

FILE *file_open_utf8(const char *path, const char *mode) { return fopen(path, mode); }
void sys_warning(const char *fmt, ...) { (void)fmt; }
void *_xmalloc(size_t size, const char *func) { (void)func; return malloc(size); }
void *_xcalloc(size_t n, size_t size, const char *func) { (void)func; return calloc(n, size); }

static void check_archive(const char *path, int flags, bool has_logo)
{
    int error = ARCHIVE_SUCCESS;
    struct alk_archive *alk = alk_open(path, flags, &error);
    assert(alk && error == ARCHIVE_SUCCESS);
    assert(alk->ar.mmapped == (flags == ARCHIVE_MMAP));
    struct archive_data *logo = archive_get(&alk->ar, 45);
    if (has_logo) {
        assert(logo && logo->size == 4 && !memcmp(logo->data, "logo", 4));
        archive_free_data(logo);
    } else {
        assert(!logo);
    }
    archive_free(&alk->ar);
}

int main(int argc, char **argv)
{
    assert(argc == 4);
    for (int flags = 0; flags <= ARCHIVE_MMAP; flags++) {
        check_archive(argv[1], flags, true);
        check_archive(argv[2], flags, false);
        check_archive(argv[3], flags, false);
    }
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "test.c"
            exe = root / "test"
            source.write_text(harness)
            header = struct.pack("<4sI", b"ALK0", 46) + bytes(45 * 8)
            frame_offset = len(header) + 8
            fixtures = []
            for name, offset, data in (
                ("complete", frame_offset, b"logo"),
                ("truncated", frame_offset, b"lo"),
                ("missing", frame_offset + 4, b""),
            ):
                fixture = root / f"{name}.alk"
                fixture.write_bytes(header + struct.pack("<II", offset, 4) + data)
                fixtures.append(str(fixture))
            libsys4 = ROOT / "subprojects/libsys4"
            subprocess.run(
                ["cc", "-std=gnu11", "-I", str(libsys4 / "include"),
                 str(source), str(libsys4 / "src/alk.c"), "-o", str(exe)],
                check=True,
            )
            subprocess.run([str(exe), *fixtures], check=True)

    def test_startup_logo_renders_the_localization_credit_if_available(self):
        run = extract_function("src/hll/AliceLogo2.c", "AliceLogo2_Run")
        harness = r"""
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "system4/alk.h"
#include "system4/cg.h"

#define MMAP_IF_64BIT 0
#define UNIMPLEMENTED(...) ((void)0)

static bool game_sengokurance, sys4_text_is_gbk, has_archive, has_logo;
static int opened, closed, drawn, presented;
static void close_archive(struct archive *ar);
static struct archive_ops archive_ops = {.free = close_archive};
static struct alk_archive alk = {.ar = {.ops = &archive_ops}};
static struct cg credit;
typedef struct { int w, h; } Texture;
static Texture screen = {800, 600};

static char *gamedir_path_icase(const char *name)
{
    assert(!strcmp(name, "AliceLogo2.alk"));
    if (!has_archive)
        return NULL;
    char *path = malloc(strlen(name) + 1);
    return strcpy(path, name);
}

struct alk_archive *alk_open(const char *path, int flags, int *error)
{
    assert(!strcmp(path, "AliceLogo2.alk") && flags == 0 && *error == ARCHIVE_SUCCESS);
    opened++;
    return &alk;
}
struct cg *cg_load(struct archive *ar, int id)
{
    assert(ar == &alk.ar && id == 45);
    return has_logo ? &credit : NULL;
}
static void close_archive(struct archive *ar) { assert(ar == &alk.ar); closed++; }
void cg_free(struct cg *cg) { assert(cg == &credit); }
static Texture *gfx_main_surface(void) { return &screen; }
static void gfx_init_texture_with_cg(Texture *tex, struct cg *cg)
{
    assert(cg == &credit);
    *tex = (Texture){400, 300};
}
static void gfx_copy_stretch(Texture *dst, int x, int y, int w, int h,
                            Texture *src, int sx, int sy, int sw, int sh)
{
    assert(dst == &screen && !x && !y && w == 800 && h == 600);
    assert(!sx && !sy && sw == src->w && sh == src->h);
    drawn++;
}
static void gfx_delete_texture(Texture *tex) { assert(tex->w == 400); }
static void gfx_swap(void) { assert(drawn == 1); presented++; }
static void gfx_android_delay(unsigned ms) { assert(presented == 1 && ms > 0); }
""" + run + r"""

int main(void)
{
    has_archive = true;
    AliceLogo2_Run(0, 0);
    game_sengokurance = true;
    AliceLogo2_Run(0, 0);
    sys4_text_is_gbk = true;
    AliceLogo2_Run(1, 0);
    AliceLogo2_Run(0, 1);
    has_archive = false;
    AliceLogo2_Run(0, 0);
    assert(!opened && !drawn && !presented);

    has_archive = true;
    AliceLogo2_Run(0, 0);
    assert(opened == 1 && closed == 1 && !drawn && !presented);

    has_logo = true;
    AliceLogo2_Run(0, 0);
    assert(opened == 2 && closed == 2 && drawn == 1 && presented == 1);
    return 0;
}
"""
        self.compile_and_run(harness)

    def test_nspic_loose_cg_override_and_precedence(self):
        with tempfile.TemporaryDirectory() as tmp:
            td = Path(tmp)
            # Detect whether tmp is case-sensitive.
            probe = td / "CaseProbe"
            probe.mkdir()
            case_sensitive = not (td / "caseprobe").exists()

            ga_10001 = make_real_qnt_solid(16, 16, 10, 20, 30)
            ga_10002 = make_real_qnt_solid(16, 16, 11, 22, 33)
            ga_10002_zzz = make_real_qnt_solid(16, 16, 99, 98, 97)
            ga_10005 = forge_crc32_bytes(make_real_qnt_solid(108, 20, 120, 120, 120), 3451, 0xDD729ED3)
            ga_1005 = make_real_qnt_solid(16, 16, 15, 25, 35)
            ga_10079 = make_real_qnt_solid(16, 16, 19, 29, 39)

            nspic_10001 = make_real_qnt_solid(24, 12, 100, 110, 120)
            nspic_10005 = make_real_qnt_solid(108, 20, 55, 166, 222)
            nspic_1005 = make_real_qnt_solid(20, 10, 66, 77, 88)
            nspic_10079_upper = make_real_qnt_solid(22, 14, 130, 140, 150)
            nspic_10079_lower = make_real_qnt_solid(28, 18, 160, 170, 180)
            nspic_99999 = make_real_qnt_solid(32, 16, 201, 202, 203)
            unc_10001 = make_qnt_solid(30, 20, 250, 50, 25)

            # Game 1: Renamed CN AIN + two fallback ALD prefixes (Zzz & Aaa) + AliceHubUncensoredCG.afa + NSPic/
            g1 = td / "game_nspic"
            g1.mkdir()
            (g1 / "AliceCN.ain").write_bytes(b"VERS")
            (g1 / "ZzzGameGA.ald").write_bytes(make_ald([(10001, "CG10002.QNT", ga_10002_zzz)]))
            (g1 / "AaaGameGA.ald").write_bytes(
                make_ald(
                    [
                        (1004, "CG01005.QNT", ga_1005),
                        (10000, "CG10001.QNT", ga_10001),
                        (10001, "CG10002.QNT", ga_10002),
                        (10004, "CG10005.QNT", ga_10005),
                        (10078, "CG10079.QNT", ga_10079),
                    ]
                )
            )
            (g1 / "AliceHubUncensoredCG.afa").write_bytes(
                make_afa([(b"CG10001.PNG", 10001, 0, 0, unc_10001)], version=1)
            )
            nspic = g1 / "NSPic"
            nspic.mkdir()
            (nspic / "cg10001.QNT").write_bytes(nspic_10001)
            (nspic / "cg10005.QNT").write_bytes(nspic_10005)
            (nspic / "cg01005.QNT").write_bytes(nspic_1005)
            if case_sensitive:
                (nspic / "cg10079.QNT").write_bytes(nspic_10079_upper)
            (nspic / "cg10079.qnt").write_bytes(nspic_10079_lower)
            (nspic / "cg99999.qnt").write_bytes(nspic_99999)
            (nspic / "new_dsr.qnt").write_bytes(nspic_10001)
            (nspic / ".DS_Store").write_bytes(b"junk")
            (nspic / "Thumbs.db").write_bytes(b"junk")
            (nspic / "cg0.qnt").write_bytes(nspic_10001)
            (nspic / "cg10002.bmp").write_bytes(b"BM\x36\x00\x00\x00\x00\x00\x00\x00\x36\x00\x00\x00")  # unsupported BMP must not mask GA.ald
            (nspic / "cg10009.qnt").write_bytes(b"")  # 0-byte file must be ignored
            (nspic / "cg99999999999999999999.qnt").write_bytes(nspic_10001)
            (nspic / "cg10002.QNT").mkdir()  # directory with matching name must be ignored

            # Game 2: lowercase nspic/ resolved via real gamedir_path_icase()
            g2 = td / "game_lower_nspic"
            g2.mkdir()
            (g2 / "Game.ain").write_bytes(b"VERS")
            (g2 / "GameGA.ald").write_bytes(make_ald([(10000, "CG10001.QNT", ga_10001)]))
            (g2 / "nspic").mkdir()
            (g2 / "nspic" / "cg10001.QNT").write_bytes(nspic_10001)

            # Game 3: no NSPic/ -> localize_sengoku_skip_cg still relabels Japanese CG 10005
            g3 = td / "game_no_nspic"
            g3.mkdir()
            (g3 / "Game.ain").write_bytes(b"VERS")
            (g3 / "GameGA.ald").write_bytes(make_ald([(10004, "CG10005.QNT", ga_10005)]))

            harness = f"""
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/utfsjis.h"
#include "xsystem4.h"
#include "asset_manager.h"

void asset_manager_fini(void);

struct config config = {{0}};
bool dbg_dap = false;
void dbg_dap_log(const char *log, const char *fmt, va_list ap) {{ (void)log; (void)fmt; (void)ap; }}
void save_cjk_canon_forms_buf(const char *s, char *b1, size_t s1, char **o1, char *b2, size_t s2, char **o2) {{
    (void)s; (void)b1; (void)s1; (void)b2; (void)s2; if (o1) *o1 = NULL; if (o2) *o2 = NULL;
}}
char *save_transcode_cstr_strict(const char *s, size_t n, bool a, bool b, size_t *out) {{
    (void)s; (void)n; (void)a; (void)b; (void)out; return NULL;
}}

static void assert_cg_pixel(int id, int w, int h, int px_x, int px_y, uint8_t r, uint8_t g, uint8_t b) {{
    struct cg_metrics m = {{0}};
    assert(asset_exists(ASSET_CG, id));
    assert(asset_cg_get_metrics(id, &m));
    assert(m.w == w && m.h == h);
    struct cg *cg = asset_cg_load(id);
    assert(cg && cg->pixels && cg->metrics.w == w && cg->metrics.h == h);
    uint8_t *px = cg->pixels + 4 * ((size_t)px_y * (size_t)w + (size_t)px_x);
    assert(px[0] == r && px[1] == g && px[2] == b);
    cg_free(cg);
}}

int main(void) {{
    // Phase 1: GBK mode with AliceHubUncensoredCG.afa > NSPic/ > GA.ald + deterministic fallback ALD
    sys4_set_text_gbk(true);
    sys4_set_source_utf8(false);
    config.game_dir = "{g1}";
    config.ain_filename = "AliceCN.ain";
    asset_manager_init();

    assert_cg_pixel(10001, 30, 20, 0, 0, 250, 50, 25);   // AliceHubUncensoredCG.afa wins over NSPic and GA.ald
    assert_cg_pixel(10079, 28, 18, 0, 0, 160, 170, 180); // NSPic/cg10079.qnt wins deterministically over .QNT and GA.ald
    assert_cg_pixel(1005,  20, 10, 0, 0, 66, 77, 88);    // zero-padded NSPic/cg01005.QNT
    assert_cg_pixel(10005, 108, 20, 2, 3, 55, 166, 222); // NSPic/cg10005.QNT bypasses localize_sengoku_skip_cg
    assert_cg_pixel(99999, 32, 16, 0, 0, 201, 202, 203); // NSPic-only ID not in GA.ald
    assert_cg_pixel(10002, 16, 16, 0, 0, 11, 22, 33);    // untouched ID falls back to AaaGameGA.ald (ignores cg10002.QNT dir & ZzzGameGA.ald)
    assert(!asset_exists(ASSET_CG, 0));
    assert(!asset_exists(ASSET_CG, 10009));
    assert(!asset_exists(ASSET_CG, 88888));
    asset_manager_fini();

    // Phase 2: Pure Shift-JIS mode ignores NSPic/ and loads base GA.ald
    sys4_set_text_gbk(false);
    sys4_set_source_utf8(false);
    asset_manager_init();

    assert_cg_pixel(10001, 30, 20, 0, 0, 250, 50, 25);   // uncensored AFA still active
    assert_cg_pixel(10079, 16, 16, 0, 0, 19, 29, 39);    // GA.ald base
    assert_cg_pixel(1005,  16, 16, 0, 0, 15, 25, 35);    // GA.ald base
    assert_cg_pixel(10005, 108, 20, 2, 3, 120, 120, 120);// GA.ald base, untouched in SJIS mode
    assert(!asset_exists(ASSET_CG, 99999));              // NSPic-only ID absent in SJIS mode
    asset_manager_fini();

    // Phase 3: UTF-8 source mode enables NSPic/
    sys4_set_text_gbk(false);
    sys4_set_source_utf8(true);
    asset_manager_init();

    assert_cg_pixel(10079, 28, 18, 0, 0, 160, 170, 180);
    assert_cg_pixel(99999, 32, 16, 0, 0, 201, 202, 203);
    asset_manager_fini();

    // Phase 4: Lowercase nspic/ directory resolved via real gamedir_path_icase()
    sys4_set_text_gbk(true);
    sys4_set_source_utf8(false);
    config.game_dir = "{g2}";
    config.ain_filename = "Game.ain";
    asset_manager_init();

    assert_cg_pixel(10001, 24, 12, 0, 0, 100, 110, 120);
    asset_manager_fini();

    // Phase 5: Absent NSPic/ preserves localize_sengoku_skip_cg on Japanese 3451-byte CG 10005
    config.game_dir = "{g3}";
    config.ain_filename = "Game.ain";
    asset_manager_init();

    assert_cg_pixel(10005, 108, 20, 2, 3, 226, 194, 180);
    asset_manager_fini();

    // Phase 6: Relaunch after teardown works cleanly
    config.game_dir = "{g1}";
    config.ain_filename = "AliceCN.ain";
    asset_manager_init();
    assert_cg_pixel(10079, 28, 18, 0, 0, 160, 170, 180);
    asset_manager_fini();

    puts("OK_NSPIC");
    return 0;
}}
"""
            libsys4_extra = [
                ROOT / "subprojects/libsys4/src/ald.c",
                ROOT / "subprojects/libsys4/src/ajp.c",
                ROOT / "subprojects/libsys4/src/cg.c",
                ROOT / "subprojects/libsys4/src/dcf.c",
                ROOT / "subprojects/libsys4/src/jpeg.c",
                ROOT / "subprojects/libsys4/src/pcf.c",
                ROOT / "subprojects/libsys4/src/pms.c",
                ROOT / "subprojects/libsys4/src/png.c",
                ROOT / "subprojects/libsys4/src/qnt.c",
                ROOT / "subprojects/libsys4/src/rou.c",
                ROOT / "subprojects/libsys4/src/tga.c",
                ROOT / "subprojects/libsys4/src/webp.c",
                ROOT / "src/util.c",
                ROOT / "src/asset_manager.c",
            ]
            ios_shim = td / "ios_shim"
            (ios_shim / "OpenGLES" / "ES3").mkdir(parents=True)
            (ios_shim / "OpenGLES" / "ES3" / "gl.h").write_text(
                "#ifdef __APPLE__\n#include <OpenGL/gl3.h>\n#else\n#include <GLES3/gl3.h>\n#endif\n"
            )
            pkg_flags = []
            try:
                pc = subprocess.run(
                    ["pkg-config", "--cflags", "--libs", "libpng", "libturbojpeg", "libwebp"],
                    capture_output=True,
                    text=True,
                    check=True,
                )
                pkg_flags = shlex.split(pc.stdout)
            except (FileNotFoundError, subprocess.CalledProcessError):
                pkg_flags = ["-lpng16", "-lturbojpeg", "-lwebp"]
            flags = [
                "-DALICEHUB_IOS",
                f"-I{ios_shim}",
                "-lz",
                "-lm",
                *pkg_flags,
            ]
            if sys.platform == "darwin":
                flags += ["-DGL_SILENCE_DEPRECATION", "-framework", "CoreFoundation"]
            out = compile_and_run(harness, [*LIBSYS4_CORE, *libsys4_extra], flags)
            self.assertEqual(out.strip(), "OK_NSPIC")

    def test_ios_freetype_glyph_fallback_on_load_error(self):
        fn_for_char = extract_function("src/font_freetype.c", "ft_font_for_char")
        fn_get_glyph = extract_function("src/font_freetype.c", "ft_font_get_glyph")
        fn_kerning = extract_function("src/font_freetype.c", "ft_font_size_char_kerning")
        harness = r"""
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define ALICEHUB_IOS 1
#define WARNING(...) ((void)0)
#define FT_LOAD_RENDER 1
#define FT_LOAD_DEFAULT 0
#define FT_KERNING_DEFAULT 0
#define FT_PIXEL_MODE_GRAY 2
#define FT_PIXEL_MODE_MONO 1
#define FT_HAS_KERNING(face) ((void)(face), 1)

typedef int FT_Error;
typedef unsigned int FT_UInt;
typedef long FT_Fixed;
typedef void *FT_Library;
static FT_Library ft_lib = NULL;

typedef struct { int x, y; } FT_Vector;
typedef struct {
    unsigned int rows;
    unsigned int width;
    int pitch;
    uint8_t *buffer;
    unsigned char pixel_mode;
} FT_Bitmap;
typedef struct {
    FT_Vector advance;
    FT_Bitmap bitmap;
    int bitmap_left;
    int bitmap_top;
} FT_GlyphSlotRec;
typedef struct FT_FaceRec {
    int id;
    FT_GlyphSlotRec *glyph;
} *FT_Face;

typedef struct { int x, y, w, h; } Rectangle;
typedef struct { int tag; } Texture;
enum font_weight { FONT_WEIGHT_NORMAL = 0, FONT_WEIGHT_BOLD = 1, FONT_WEIGHT_HEAVY = 2 };
struct font { int unused; };
struct font_size { float size; struct font *font; };
struct glyph { Texture t[3]; Rectangle rect; float advance; };
struct font_ft {
    struct font super;
    FT_Face font;
    struct font_ft *fallback;
    unsigned current_size;
};

static uint8_t fb_pixels[4] = { 0x80, 0xff, 0xff, 0x80 };
static int primary_load_calls = 0;
static int fallback_load_calls = 0;
static int primary_adv_calls = 0;
static int fallback_adv_calls = 0;
static int kerning_calls = 0;
static int last_rendered_from_fallback = 0;
static unsigned last_x_ppem = 0;

static bool is_half_width(uint32_t code) { return code < 128; }
static FT_Error FT_Set_Pixel_Sizes(FT_Face face, unsigned w, unsigned h) { (void)face; (void)h; last_x_ppem = w; return 0; }
static void ft_font_set_size(struct font_ft *font, unsigned size) { font->current_size = size; last_x_ppem = 0; }
static void FT_GlyphSlot_Own_Bitmap(FT_GlyphSlotRec *g) { (void)g; }
static void FT_Bitmap_Embolden(FT_Library lib, FT_Bitmap *bm, int x, int y) { (void)lib; (void)bm; (void)x; (void)y; }
static void FT_Bitmap_New(FT_Bitmap *bm) { (void)bm; }
static FT_Error FT_Bitmap_Convert(FT_Library lib, const FT_Bitmap *src, FT_Bitmap *dst, int align) { (void)lib; (void)src; (void)dst; (void)align; return 1; }
static void FT_Bitmap_Done(FT_Library lib, FT_Bitmap *bm) { (void)lib; (void)bm; }
static FT_Error FT_Get_Kerning(FT_Face face, FT_UInt l, FT_UInt r, unsigned mode, FT_Vector *delta) {
    (void)face; (void)l; (void)r; (void)mode; kerning_calls++; delta->x = 128; delta->y = 0; return 0;
}

static FT_UInt FT_Get_Char_Index(FT_Face face, uint32_t code)
{
    if (face->id == 1) {
        // Primary (PingFang HVF): reports non-zero cmap index for all tested codepoints.
        return 42;
    }
    // Fallback (HanaMinA): has 0x6218, 'A', and 0x9998; lacks 0x9999.
    return (code == 0x6218 || code == 'A' || code == 0x9998) ? 77 : 0;
}

static FT_Error FT_Load_Char(FT_Face face, uint32_t code, int flags)
{
    (void)flags;
    if (face->id == 1) {
        primary_load_calls++;
        // Simulate HVF glyph load failure on primary face.
        return 1;
    }
    fallback_load_calls++;
    if (code == 'A') {
        // Simulate non-square half-width load failure; square reload succeeds.
        if (last_x_ppem != 0) {
            face->glyph->bitmap.pixel_mode = 0;
            return 1;
        }
        face->glyph->bitmap.width = 2;
        face->glyph->bitmap.rows = 2;
        face->glyph->bitmap.pitch = 2;
        face->glyph->bitmap.buffer = fb_pixels;
        face->glyph->bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
        face->glyph->bitmap_left = 0;
        face->glyph->bitmap_top = 14;
        face->glyph->advance.x = 12 * 64; // > cell_w (8) + 0.5 -> triggers non-square retry
        return 0;
    }
    if (code == 0x6218) {
        face->glyph->bitmap.width = 2;
        face->glyph->bitmap.rows = 2;
        face->glyph->bitmap.pitch = 2;
        face->glyph->bitmap.buffer = fb_pixels;
        face->glyph->bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
        face->glyph->bitmap_left = 1;
        face->glyph->bitmap_top = 14;
        face->glyph->advance.x = 16 * 64;
        return 0;
    }
    return 1;
}

static FT_Error FT_Get_Advance(FT_Face face, FT_UInt idx, int flags, FT_Fixed *advance)
{
    (void)flags;
    if (face->id == 1) {
        primary_adv_calls++;
        *advance = 0x12345678; // poison value to confirm error resets/falls back
        return 1;
    }
    fallback_adv_calls++;
    if (idx == 77) {
        *advance = 16L << 16;
        return 0;
    }
    return 1;
}

static Rectangle init_glyph_texture(Texture *dst, FT_Bitmap *glyph, int left, int top, int size, bool half_width)
{
    (void)left; (void)top; (void)size; (void)half_width;
    assert(glyph->buffer == fb_pixels);
    dst->tag = 2;
    last_rendered_from_fallback = 1;
    return (Rectangle){4, 4, 16, 24};
}
""" + fn_for_char + "\n" + fn_get_glyph + "\n" + fn_kerning + r"""

int main(void)
{
    FT_GlyphSlotRec primary_slot = {0};
    FT_GlyphSlotRec fallback_slot = {0};
    struct FT_FaceRec primary_face = { .id = 1, .glyph = &primary_slot };
    struct FT_FaceRec fallback_face = { .id = 2, .glyph = &fallback_slot };
    struct font_ft fallback = { .font = &fallback_face, .fallback = NULL, .current_size = 0 };
    struct font_ft primary = { .font = &primary_face, .fallback = &fallback, .current_size = 0 };
    struct font_size fsize = { .size = 16.0f, .font = &primary.super };

    // 1. Primary cmap hits but FT_Load_Char fails -> must retry on fallback and succeed.
    struct glyph g = {0};
    assert(ft_font_get_glyph(&fsize, &g, 0x6218, FONT_WEIGHT_NORMAL));
    assert(primary_load_calls == 1 && fallback_load_calls == 1);
    assert(fallback.current_size == 16);
    assert(last_rendered_from_fallback == 1 && g.t[FONT_WEIGHT_NORMAL].tag == 2);
    assert(g.advance == 16.0f);

    // 2. Primary fails and fallback lacks codepoint -> returns false without loading fallback.
    last_rendered_from_fallback = 0;
    assert(!ft_font_get_glyph(&fsize, &g, 0x9999, FONT_WEIGHT_NORMAL));
    assert(primary_load_calls == 2 && fallback_load_calls == 1);

    // 3. Both primary and fallback fail FT_Load_Char -> returns false.
    assert(!ft_font_get_glyph(&fsize, &g, 0x9998, FONT_WEIGHT_NORMAL));
    assert(primary_load_calls == 3 && fallback_load_calls == 2);

    // 4. Half-width non-square FT_Load_Char error restores square size and renders glyph cleanly.
    last_rendered_from_fallback = 0;
    assert(ft_font_get_glyph(&fsize, &g, 'A', FONT_WEIGHT_NORMAL));
    assert(last_rendered_from_fallback == 1 && g.advance == 8.0f && fallback.current_size == 16);

    // 5. FT_Get_Advance fails on primary -> retries on fallback and returns fallback advance.
    kerning_calls = 0;
    float adv = ft_font_size_char_kerning(&fsize, 0x6218, 0x6218);
    assert(primary_adv_calls == 1 && fallback_adv_calls == 1);
    assert(fabsf(adv - 16.0f) < 0.001f);

    // 6. FT_Get_Advance fails on primary and fallback lacks glyph -> returns 0.0f without calling FT_Get_Kerning.
    kerning_calls = 0;
    float adv_miss = ft_font_size_char_kerning(&fsize, 0x9999, 0x6218);
    assert(primary_adv_calls == 2 && fallback_adv_calls == 1);
    assert(kerning_calls == 0 && adv_miss == 0.0f);

    return 0;
}
"""
        self.compile_and_run(harness, extra_flags=("-lm",))

    def test_ios_sengoku_rance_font_end_session_clears_cached_font_sizes(self):
        fn_end = extract_function("src/hll/SengokuRanceFont.c", "sengoku_rance_font_end_session")
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include "system4/hashtable.h"

struct text_style {
    void *font_size;
};
struct sr_text_properties {
    float x;
    struct text_style ts;
};
static struct sr_text_properties default_text_properties = { .x = 0.0f, .ts = { .font_size = NULL } };
static struct sr_text_properties current_text_properties;
static struct hash_table *sr_tp_table = NULL;
""" + fn_end + r"""

int main(void)
{
    int dummy = 42;
    sr_tp_table = ht_create(16);
    struct sr_text_properties *p = calloc(1, sizeof(*p));
    p->x = 12.0f;
    p->ts.font_size = &dummy;
    ht_put_int(sr_tp_table, 7, p);
    ht_put_int(sr_tp_table, 8, NULL); // cleared slot must be safe for free(NULL)
    current_text_properties.x = 99.0f;
    current_text_properties.ts.font_size = &dummy;

    sengoku_rance_font_end_session();
    assert(sr_tp_table == NULL);
    assert(current_text_properties.x == 0.0f);
    assert(current_text_properties.ts.font_size == NULL);

    // Idempotent second call when already empty
    sengoku_rance_font_end_session();
    assert(sr_tp_table == NULL);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "test.c"
            exe = Path(tmp) / "test"
            source.write_text(harness)
            libsys4 = ROOT / "subprojects/libsys4"
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-DALICEHUB_IOS",
                 "-I", str(libsys4 / "include"), str(source),
                 str(libsys4 / "src/hashtable.c"), str(libsys4 / "src/system.c"),
                 "-o", str(exe)],
                check=True,
            )
            subprocess.run([str(exe)], check=True)
