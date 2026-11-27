"""Exercise Switch regressions without the Switch SDK."""
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

from test_vm_reign_regressions import extract_function


class SwitchRegressionTest(unittest.TestCase):
    def run_c(self, source, *libs):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test.c"
            exe = Path(tmp) / "test"
            path.write_text(source)
            subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            str(path), "-o", str(exe), *libs], check=True)
            subprocess.run([str(exe)], cwd=tmp, check=True)

    def test_terms_browser_keeps_consent_separate(self):
        terms = extract_function("../cmake/switch/hub/online.c", "terms_input")
        header = Path(__file__).resolve().parents[3] / "cmake/switch/hub/hub.h"
        self.run_c(f'#include "{header}"\n' + r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define TERMS_URL "https://example.invalid/terms"
#define R_SUCCEEDED(rc) (!(rc))
#define R_FAILED(rc) ((rc) != 0)
typedef int Result, WebCommonConfig, WebCommonReply;
typedef enum { WebExitReason_BackButton, WebExitReason_ErrorDialog } WebExitReason;
static bool consent;
static int create_error, show_error, visits, toasts, waits, pops, saves;
static WebExitReason exit_reason;

static Result webPageCreate(WebCommonConfig *config, const char *url)
{
    assert(!strcmp(url, TERMS_URL));
    *config = 1;
    return create_error;
}
static Result webConfigShow(WebCommonConfig *config, WebCommonReply *reply)
{
    assert(*config == 1);
    ++visits;
    *reply = exit_reason;
    return show_error;
}
static Result webReplyGetExitReason(WebCommonReply *reply, WebExitReason *reason)
{
    *reason = *reply;
    return 0;
}
void hub_wait_for_release(HubApp *app) { (void)app; ++waits; }
void hub_toast(HubApp *app, const char *message) { (void)app; (void)message; ++toasts; }
bool hub_pop(HubApp *app) { (void)app; ++pops; return true; }
static bool save_consent(bool enabled) { assert(enabled); ++saves; return true; }
""" + terms + r"""
int main(void)
{
    assert(terms_input(NULL, NULL, NULL, HUB_ACTION));
    assert(visits == 1 && toasts == 0);
    create_error = 1;
    assert(terms_input(NULL, NULL, NULL, HUB_ACTION));
    assert(visits == 1 && toasts == 1);
    create_error = 0;
    show_error = 1;
    assert(terms_input(NULL, NULL, NULL, HUB_ACTION));
    assert(visits == 2 && toasts == 2);
    show_error = 0;
    exit_reason = WebExitReason_ErrorDialog;
    assert(terms_input(NULL, NULL, NULL, HUB_ACTION));
    assert(visits == 3 && toasts == 3 && waits == 4);
    assert(!terms_input(NULL, NULL, NULL, HUB_BACK));
    assert(!consent && !pops && !saves);
    assert(terms_input(NULL, NULL, NULL, HUB_CONFIRM));
    assert(consent && pops == 1 && saves == 1);
}
""")

    def test_picker_draws_unicode_codepoints_and_respects_stride(self):
        renderer = "\n".join(extract_function("../cmake/switch/picker_renderer.h", name)
                             for name in ("picker_rgb", "picker_mix", "picker_text"))
        self.run_c(r"""
#include <assert.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <uchar.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
enum { PICKER_WIDTH = 1280, PICKER_HEIGHT = 720, FT_PIXEL_MODE_GRAY = 2 };
#define RGB565_FROM_RGB8(r, g, b) (((r) >> 3) | (((g) >> 2) << 5) | (((b) >> 3) << 11))
typedef struct {
    unsigned rows, width;
    int pitch, pixel_mode;
    const u8 *buffer;
} FT_Bitmap;
typedef struct {
    int bitmap_top, bitmap_left;
    FT_Bitmap bitmap;
    struct { int x; } advance;
} Glyph, *FT_GlyphSlot;
typedef struct {
    u16 *pixels;
    u32 stride;
} Picker;
static ssize_t decode_utf8(u32 *out, const u8 *text)
{
    char32_t cp;
    size_t n = mbrtoc32(&cp, (const char *)text, 4, NULL);
    if (n == (size_t)-1 || n == (size_t)-2)
        return -1;
    *out = cp;
    return (ssize_t)n;
}
static u32 drawn[12];
static int count;
static FT_GlyphSlot picker_glyph(Picker *picker, u32 cp)
{
    (void)picker;
    static const u8 bitmap[] = { 255 };
    static Glyph glyph = { 1, 0, { 1, 1, 1, FT_PIXEL_MODE_GRAY, bitmap }, { 2 * 64 } };
    drawn[count++] = cp;
    return &glyph;
}
""" + renderer + r"""
int main(void)
{
    assert(setlocale(LC_CTYPE, "C.UTF-8"));
    static u16 pixels[2][PICKER_WIDTH + 16];
    Picker picker = { pixels[0], PICKER_WIDTH + 16 };
    picker_text(&picker, 56, 2, PICKER_WIDTH, 0xffffff, "ランス1 中文 한글");
    const u32 expected[] = { 0x30e9, 0x30f3, 0x30b9, '1', ' ', 0x4e2d, 0x6587, ' ', 0xd55c, 0xae00 };
    assert(count == (int)(sizeof(expected) / sizeof(*expected)));
    for (int i = 0; i < count; i++) {
        assert(drawn[i] == expected[i]);
        assert(pixels[1][56 + 2 * i] == 0xffff);
        assert(pixels[0][56 + 2 * i] == 0);
    }
    picker_text(&picker, 56, 0, PICKER_WIDTH, 0xffffff, "字");
    assert(drawn[count - 1] == 0x5b57 && pixels[0][56] == 0);
}
""")

    def test_picker_uses_bundled_and_shared_fonts_per_glyph(self):
        fonts = "\n".join(extract_function("../cmake/switch/picker_renderer.h", name)
                          for name in ("picker_load_fonts", "picker_glyph"))
        self.run_c(r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>
#define ALICEHUB_PICKER_COMPRESSED_FONT_SIZE 4
#define FT_LOAD_RENDER 1
#define R_SUCCEEDED(r) ((r) == 0)
enum { PlServiceType_User, PlSharedFontType_Total = 6 };
typedef uint32_t u32;
typedef int FT_Library;
typedef struct { u32 codepoint; } Glyph, *FT_GlyphSlot;
typedef struct { FT_GlyphSlot glyph; u32 first, last; } Face, *FT_Face;
typedef struct { const unsigned char *address; int size; } PlFontData;
typedef struct {
    FT_Library library;
    FT_Face bundled[2], shared[PlSharedFontType_Total];
    unsigned char *fallback_data;
    bool shared_attempted, shared_open;
    int text_size;
} Picker;
static Glyph glyph;
static Face primary = { &glyph, 1, 0 };
static Face bundled = { &glyph, 0x4e00, 0x9fff };
static Face shared[PlSharedFontType_Total];
static int FT_Init_FreeType(FT_Library *library) { *library = 1; return 0; }
static int FT_Set_Pixel_Sizes(FT_Face face, int width, int height)
{
    assert(face && !width && height == 24);
    return 0;
}
static int FT_New_Face(FT_Library library, const char *path, int index, FT_Face *face)
{
    assert(library == 1 && index == 0);
    assert(!strcmp(path, "romfs:/fonts/MTLc3m.ttf"));
    *face = &primary;
    return 0;
}
static int FT_New_Memory_Face(FT_Library library, const unsigned char *data, int size,
                              int index, FT_Face *face)
{
    assert(library == 1 && index == 0);
    if (size == 4) {
        assert(!memcmp(data, "Hana", 4));
        *face = &bundled;
    } else {
        assert(size == 1 && *data < PlSharedFontType_Total);
        shared[*data] = (Face){ &glyph, 0x10000 + *data, 0x10000 + *data };
        *face = &shared[*data];
    }
    return 0;
}
static int FT_Get_Char_Index(FT_Face face, u32 cp)
{
    return (face->first <= cp && cp <= face->last) || cp == '?';
}
static int FT_Load_Char(FT_Face face, u32 cp, int flags)
{
    assert(flags == FT_LOAD_RENDER && FT_Get_Char_Index(face, cp));
    face->glyph->codepoint = cp;
    return 0;
}
static bool shared_available;
static int shared_attempts, shared_loaded;
static int plInitialize(int service)
{
    assert(service == PlServiceType_User);
    ++shared_attempts;
    return !shared_available;
}
static int plGetSharedFontByType(PlFontData *font, int type)
{
    static const unsigned char ids[] = { 0, 1, 2, 3, 4, 5 };
    assert(type == shared_loaded++);
    *font = (PlFontData){ &ids[type], 1 };
    return 0;
}
""" + fonts + r"""
int main(void)
{
    assert(!mkdir("romfs:", 0700) && !mkdir("romfs:/fonts", 0700));
    gzFile file = gzopen("romfs:/fonts/HanaMinA.ttf.gz", "wb");
    assert(file && gzwrite(file, "Hana", 4) == 4 && gzclose(file) == Z_OK);
    Picker keyless = {0};
    assert(picker_load_fonts(&keyless));
    const u32 title[] = { 0x7231, 0x4e3d, 0x4e1d, 0x5b66, 0x56ed };
    for (size_t i = 0; i < sizeof(title) / sizeof(*title); ++i)
        assert(picker_glyph(&keyless, title[i])->codepoint == title[i]);
    assert(shared_attempts == 0);
    assert(picker_glyph(&keyless, 0xd55c)->codepoint == '?');
    assert(picker_glyph(&keyless, 0xae00)->codepoint == '?');
    assert(shared_attempts == 1 && shared_loaded == 0);

    shared_available = true;
    Picker console = {0};
    assert(picker_load_fonts(&console));
    for (u32 i = 0; i < PlSharedFontType_Total; ++i)
        assert(picker_glyph(&console, 0x10000 + i)->codepoint == 0x10000 + i);
    assert(shared_attempts == 2 && shared_loaded == PlSharedFontType_Total);
    free(keyless.fallback_data);
    free(console.fallback_data);
}
""", "-lz")

    def test_picker_presents_cached_frame_without_input(self):
        launcher = "../cmake/switch/launcher.c"
        hub = Path(__file__).resolve().parents[3] / "cmake/switch/hub"
        loop = "\n".join([extract_function("../cmake/switch/picker_renderer.h", "picker_begin"),
                          *(extract_function(launcher, name)
                            for name in ("compare_names", "resolve_game", "choose_game"))])
        self.run_c(r"""
#include <assert.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "model.h"
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int PadState;
typedef int PadRepeater;
typedef struct { int fb; u16 *pixels; u32 stride; } Picker;
#define GAMES "games"
#define ALICEHUB_ENGINE "xsystem4"
#define ALICEHUB_SWITCH_XSYSTEM4
enum { PICKER_HEIGHT = 1, CARD_COUNT = 4, ART_WIDTH = 250, ART_HEIGHT = 194 };
enum { HidNpadStyleSet_NpadStandard, HidNpadButton_A = 1, HidNpadButton_B = 2,
       HidNpadButton_Plus = 4, HidNpadButton_Up = 8, HidNpadButton_Down = 16,
       HidNpadButton_Left = 32, HidNpadButton_Right = 64 };
static u16 pixel;
static int ticks, draws, submitted;
static const u64 *keys;
static bool saw_name, saw_invalid, saw_startup_error;
static bool check_error_dismissal;
static bool appletMainLoop(void) { return ticks < 4; }
static void padConfigureInput(int count, int style) { (void)count; (void)style; }
static void padInitializeDefault(PadState *pad) { *pad = 0; }
static void padUpdate(PadState *pad) { *pad = ++ticks; }
static void padRepeaterInitialize(PadRepeater *r, int delay, int interval)
{ *r = 0; assert(delay > interval && interval > 0); }
static void padRepeaterUpdate(PadRepeater *r, u64 mask) { (void)r; (void)mask; }
static u64 padRepeaterGetButtons(PadRepeater *r) { (void)r; return 0; }
static u64 padGetButtons(PadState *pad)
{
    return keys ? keys[*pad - 1] : (*pad == 4 ? HidNpadButton_B : 0);
}
static u64 padGetButtonsDown(PadState *pad)
{
    return padGetButtons(pad) & ~(keys && *pad > 1 ? keys[*pad - 2] : 0);
}
static void svcSleepThread(int ns) { assert(ns == 16000000); }
static u16 *framebufferBegin(int *fb, u32 *stride)
{
    (void)fb;
    *stride = sizeof(pixel);
    return &pixel;
}
static u16 *picker_art_load(const char *path, int w, int h)
{
    (void)path; (void)w; (void)h;
    return NULL;
}
static u16 *picker_art_cover(const char *path, int w, int h)
{
    return picker_art_load(path, w, h);
}
static void picker_draw(Picker *picker, char **names, size_t count, size_t selected,
                        const u16 *logo, u16 *const *covers, const char *game, const char *error)
{
    (void)names; (void)count; (void)selected; (void)logo; (void)covers;
    *picker->pixels = ++draws;
    saw_name |= error && !strcmp(game, "bad");
    saw_invalid |= error && strstr(error, "System 4 launcher or script");
    saw_startup_error |= error && !strcmp(error, "Broken AIN");
    if (check_error_dismissal)
        assert((error != NULL) == (ticks < 4));
}
static void framebufferEnd(int *fb)
{
    (void)fb;
    if (!keys)
        assert(pixel == 1);
    ++submitted;
}
""" + loop + r"""
int main(void)
{
    Picker picker = {0};
    char game[HUB_PATH_SIZE], target[HUB_PATH_SIZE];
    bool uses_dsr;
    assert(!choose_game(&picker, game, target, &uses_dsr, NULL));
    assert(draws == 1 && submitted == 3);

    assert(!mkdir("games", 0700) && !mkdir("games/bad", 0700) && !mkdir("games/good", 0700));
    assert(!mkdir("games/bad/System40.ini", 0700));
    FILE *ini = fopen("games/good/SYSTEM40.INI", "w");
    assert(ini && fputs("CodeName = \"Test.ain\"\n", ini) >= 0 && !fclose(ini));
    const u64 attempt[] = { 0, HidNpadButton_A, HidNpadButton_Right, HidNpadButton_A };
    ticks = 0;
    keys = attempt;
    assert(choose_game(&picker, game, target, &uses_dsr, NULL));
    assert(!strcmp(game, "games/good") && saw_name && saw_invalid);
    assert(!strcmp(target, game) && !uses_dsr);

    const u64 retry[] = { 0, HidNpadButton_B };
    ticks = 0;
    keys = retry;
    strcpy(game, "games/bad");
    assert(!choose_game(&picker, game, target, &uses_dsr, "Broken AIN"));
    assert(saw_startup_error);

    const u64 held_a[] = { HidNpadButton_A, HidNpadButton_A, 0, HidNpadButton_A };
    ticks = 0;
    keys = held_a;
    check_error_dismissal = true;
    int prior_draws = draws;
    strcpy(game, "games/bad");
    assert(!choose_game(&picker, game, target, &uses_dsr, "Broken AIN"));
    assert(draws == prior_draws + 2);
}
""", str(hub / "model.c"), "-I" + str(hub))

    def test_system3_engines_require_a_readable_scenario_source(self):
        validator = extract_function("../cmake/switch/launcher.c", "resolve_game")
        for engine in ("", "#define ALICEHUB_SWITCH_XSYSTEM35"):
            self.run_c(engine + r"""
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#define HUB_PATH_SIZE 512
""" + validator + r"""
int main(void)
{
    assert(!mkdir("game", 0700));
    FILE *other = fopen("game/GraphicsGA.ald", "wb");
    assert(other && fputs("not a scenario", other) >= 0 && !fclose(other));
    char target[HUB_PATH_SIZE];
    bool uses_dsr;
    assert(resolve_game("game", target, &uses_dsr));
#ifdef ALICEHUB_SWITCH_XSYSTEM35
    FILE *scenario = fopen("game/TestSA_chs.ALD", "wb");
    assert(scenario && fputs("scenario", scenario) >= 0 && !fclose(scenario));
#else
    FILE *scenario = fopen("game/adisk.dat", "wb");
    assert(scenario && fputc(1, scenario) != EOF && !fclose(scenario));
    assert(resolve_game("game", target, &uses_dsr));
    scenario = fopen("game/adisk.dat", "wb");
    const unsigned char header[] = { 2, 0, 3, 0 };
    assert(scenario && fwrite(header, 1, sizeof(header), scenario) == sizeof(header));
    assert(!fseek(scenario, 512, SEEK_SET) && fputc(0, scenario) != EOF && !fclose(scenario));
#endif
    assert(!resolve_game("game", target, &uses_dsr));
    assert(!strcmp(target, "game") && !uses_dsr);
}
""")

    def test_cover_art_decodes_png_and_skips_corrupt_or_missing_files(self):
        if not shutil.which("pkg-config"):
            self.skipTest("pkg-config required for host image decoder test")
        packages = subprocess.run(["pkg-config", "--cflags", "--libs", "libpng", "libjpeg"],
                                  capture_output=True, text=True)
        if packages.returncode:
            self.skipTest("host libpng and libjpeg required for image decoder test")
        repo = Path(__file__).resolve().parents[3]
        source = repo / "cmake/switch/picker_art.c"
        icon = repo / "project/app/src/main/res/mipmap-xxxhdpi/ic_launcher_round.png"
        self.run_c(r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "picker_art.h"
#include <jpeglib.h>

int main(void)
{
    FILE *file = fopen("cover.jpg", "wb");
    assert(file && fputs("\xff\xd8", file) >= 0 && !fclose(file));
    assert(!picker_art_cover(".", 58, 58));
    FILE *original = fopen(ALICEHUB_ICON, "rb");
    file = fopen("thumbnail.png", "wb");
    assert(original && file);
    for (int byte; (byte = fgetc(original)) != EOF;)
        assert(fputc(byte, file) == byte);
    fclose(original);
    fclose(file);
    uint16_t *png = picker_art_cover(".", 58, 58);
    assert(png && png[0] != png[29 * 58 + 29]);
    free(png);

    struct jpeg_compress_struct jpeg;
    struct jpeg_error_mgr error;
    jpeg.err = jpeg_std_error(&error);
    jpeg_create_compress(&jpeg);
    file = fopen("cover.jpg", "wb");
    assert(file);
    jpeg_stdio_dest(&jpeg, file);
    jpeg.image_width = 1;
    jpeg.image_height = 1;
    jpeg.input_components = 3;
    jpeg.in_color_space = JCS_RGB;
    jpeg_set_defaults(&jpeg);
    jpeg_start_compress(&jpeg, TRUE);
    unsigned char red[] = {255, 0, 0};
    JSAMPROW row = red;
    assert(jpeg_write_scanlines(&jpeg, &row, 1) == 1);
    jpeg_finish_compress(&jpeg);
    jpeg_destroy_compress(&jpeg);
    fclose(file);
    uint16_t *jpg = picker_art_cover(".", 58, 58);
    assert(jpg && jpg[0] == 0xf800 && jpg[58 * 58 - 1] == 0xf800);
    free(jpg);
    assert(!picker_art_load("cover.jpg", 0, 58));

    assert(!remove("thumbnail.png") && !remove("cover.jpg"));
    const unsigned char ico[] = {
        0,0,1,0,1,0, 1,1,0,0,1,0,24,0,48,0,0,0,22,0,0,0,
        40,0,0,0, 1,0,0,0, 2,0,0,0, 1,0,24,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,255,0,0, 0,0,0,0
    };
    file = fopen("Game.ICO", "wb");
    assert(file && fwrite(ico, sizeof(ico), 1, file) == 1 && !fclose(file));
    uint16_t *art = picker_art_cover(".", 58, 58);
    assert(art && art[29 * 58 + 29] == 0x07e0 && art[0] != 0x07e0);
    free(art);
    file = fopen("Game.ICO", "wb");
    assert(file && fwrite(ico, sizeof(ico) - 1, 1, file) == 1 && !fclose(file));
    assert(!picker_art_cover(".", 58, 58));

    unsigned char pe[0x200 + 48] = {
        [0] = 'M', [1] = 'Z', [0x3c] = 64,
        [64] = 'P', [65] = 'E', [70] = 1, [84] = 120,
        [88] = 0x0b, [89] = 1, [180] = 3, [201] = 0x10,
        [221] = 0x10, [224] = 48, [225] = 1, [229] = 1,
        [0x10e] = 1, [0x110] = 3, [0x114] = 24, [0x117] = 0x80,
        [0x126] = 1, [0x128] = 1, [0x12c] = 48, [0x12f] = 0x80,
        [0x13e] = 1, [0x140] = 9, [0x144] = 72,
        [0x149] = 0x11, [0x14c] = 48
    };
    memcpy(pe + 0x200, ico + 22, 48);
    pe[0x200 + 40] = 255;
    pe[0x200 + 41] = 0;
    file = fopen("System40.exe", "wb");
    assert(file && fwrite(pe, sizeof(pe), 1, file) == 1 && !fclose(file));
    art = picker_art_cover(".", 58, 58);
    assert(art && art[29 * 58 + 29] == 0x001f);
    free(art);
    pe[0x147] = 0x80;
    file = fopen("System40.exe", "wb");
    assert(file && fwrite(pe, sizeof(pe), 1, file) == 1 && !fclose(file));
    assert(!picker_art_cover(".", 58, 58));
}
""", str(source), "-I" + str(source.parent), f'-DALICEHUB_ICON="{icon}"',
                   *shlex.split(packages.stdout))

    def test_picker_reopens_only_for_reported_startup_failure(self):
        launcher = "../cmake/switch/launcher.c"
        setter = extract_function(launcher, "alicehub_switch_set_launch_error")
        exit_romfs = extract_function(launcher, "exit_romfs")
        main = extract_function(launcher, "standalone_main")
        self.run_c(r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define R_FAILED(result) (result)
#define SAVES "saves"
#define HUB_PATH_SIZE 512
typedef struct { int unused; } Picker;
static char launch_error[512];
static bool romfs_mounted;
static bool report_error;
static int selections, mounts, unmounts, quits;
static int romfsInit(void) { ++mounts; return 0; }
static void romfsExit(void) { ++unmounts; }
static int nvInitialize(void) { return 0; }
static void nvExit(void) {}
static bool picker_open(Picker *picker) { (void)picker; return true; }
static void picker_close(Picker *picker) { (void)picker; }
static bool prepare_save(const char *game, const char *target, char *save, size_t size)
{
    (void)game; (void)target; (void)save; (void)size;
    return true;
}
static bool choose_game(Picker *picker, char *game, char *target, bool *uses_dsr, const char *error)
{
    (void)picker; (void)game; (void)target;
    *uses_dsr = false;
    if (++selections == 1) {
        assert(!error);
        return true;
    }
    assert(report_error && !strcmp(error, "Bad scenario disk"));
    return false;
}
static void SDL_Quit(void) { ++quits; }
""" + exit_romfs + setter + r"""
static int launch_game(const char *game, char *target, char *save)
{
    (void)game; (void)target; (void)save;
    exit_romfs();
    if (report_error)
        alicehub_switch_set_launch_error("Bad scenario disk");
    return 1;
}
""" + main + r"""
int main(void)
{
    assert(standalone_main() == 1);
    assert(selections == 1 && mounts == 1 && unmounts == 1 && !quits);
    selections = mounts = unmounts = 0;
    report_error = true;
    assert(standalone_main() == 0);
    assert(selections == 2 && mounts == 2 && unmounts == 2 && quits == 1);
}
""")

    def test_invalid_config_string_keeps_previous_value_owned(self):
        source = "src/system4.c"
        signature = "static bool read_config_entries(const char *path)\n{"
        text = (Path(__file__).resolve().parents[2] / source).read_text(encoding="utf-8")
        definition = text.split(signature, 1)[1].split("\n}", 1)[0]
        readers = "\n".join([
            *(extract_function(source, name) for name in ("ini_string", "ini_integer", "ini_boolean")),
            signature + definition + "\n}",
            *(extract_function(source, name) for name in ("user_config_path", "read_user_config_file")),
        ])
        self.run_c(r"""
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
enum { INI_INTEGER, INI_BOOLEAN, INI_STRING };
enum { FONT_MINCHO, FONT_GOTHIC, SAVE_FORMAT_JSON, SAVE_FORMAT_RSM };
struct string { const char *text; };
struct ini_entry { struct string *name; struct { int type, i; struct string *s; } value; };
static struct {
    char *game_name, *boot_name, *ain_filename, *vm_name, *save_dir, *font_paths[2], *fnl_path;
    int view_width, view_height, default_volume, msgskip_delay, save_format;
    bool view_width_set, view_height_set, manual_text_x_scale, joypad;
    float text_x_scale;
} config;
static struct string key;
static struct ini_entry entry = { .name = &key };
static jmp_buf recovery;
static unsigned frees;
static void track_free(void *ptr) { ++frees; free(ptr); }
#define free track_free
#define ERROR(...) longjmp(recovery, 1)
#define WARNING(...) ((void)0)
#define xstrdup strdup
static struct ini_entry *ini_parse(const char *path, int *count)
{
    (void)path;
    *count = 1;
    return &entry;
}
static void ini_free_entry(struct ini_entry *e) { (void)e; }
static void read_mixer_channels(struct ini_entry *e) { (void)e; }
static bool is_absolute_path(const char *path) { return *path == '/'; }
static char *path_join(const char *dir, const char *path) { (void)dir; return strdup(path); }
""" + readers + r"""
static void check(const char *setting, char **slot, bool user_config)
{
    key.text = setting;
    *slot = strdup("original");
    unsigned before = frees;
    if (!setjmp(recovery)) {
        if (user_config)
            read_user_config_file("game/.xsys4rc", "game");
        else
            read_config_entries("game/System40.ini");
        assert(0);
    }
    assert(frees == before);
    assert(!strcmp(*slot, "original"));
    free(*slot);
    *slot = NULL;
}
int main(void)
{
    check("SaveFolder", &config.save_dir, false);
    check("CodeName", &config.ain_filename, false);
    check("save-folder", &config.save_dir, true);
    check("font-mincho", &config.font_paths[FONT_MINCHO], true);
    check("font-gothic", &config.font_paths[FONT_GOTHIC], true);
    check("font-fnl", &config.fnl_path, true);
}
""")

    def test_face_buttons_keep_sideways_mapping(self):
        helper = extract_function("../cmake/switch/controller.h",
                                  "alicehub_switch_face_button")
        self.run_c(r"""
#include <assert.h>
typedef int SDL_JoystickID;
typedef int SDL_GameControllerButton;
typedef int HidNpadIdType;
enum { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X };
enum { HidNpadIdType_No1 = 0, HidNpadIdType_No8 = 7, HidNpadStyleTag_NpadJoyDual = 4 };
enum { HidDeviceTypeBits_JoyLeft = 16, HidDeviceTypeBits_JoyRight = 32 };
static int style[8], type[8];
static int hidGetNpadStyleSet(int id) { return style[id]; }
static int hidGetNpadDeviceType(int id) { return type[id]; }
""" + helper + r"""
int main(void)
{
    style[1] = HidNpadStyleTag_NpadJoyDual;
    type[1] = HidDeviceTypeBits_JoyLeft | HidDeviceTypeBits_JoyRight;
    type[2] = HidDeviceTypeBits_JoyLeft;
    type[3] = HidDeviceTypeBits_JoyRight;
    for (int i = 0; i < 4; ++i) {
        assert(alicehub_switch_face_button(i, SDL_CONTROLLER_BUTTON_A) ==
            (i < 2 ? SDL_CONTROLLER_BUTTON_B : SDL_CONTROLLER_BUTTON_A));
        assert(alicehub_switch_face_button(i, SDL_CONTROLLER_BUTTON_B) ==
            (i < 2 ? SDL_CONTROLLER_BUTTON_A : SDL_CONTROLLER_BUTTON_B));
        assert(alicehub_switch_face_button(i, SDL_CONTROLLER_BUTTON_X) == SDL_CONTROLLER_BUTTON_X);
    }
}
""")

    def test_system4_dispatches_virtual_touch_before_game_input(self):
        gameplay = extract_function("src/input.c", "void handle_events")
        window_only = extract_function("src/input.c", "void handle_window_events")
        self.assertRegex(gameplay, r"while \(SDL_PollEvent\(&e\)\) \{\s*"
                         r"#ifdef __SWITCH__\s*"
                         r"if \(switch_controls_gl_touch_event\(&e, sdl.window, &redraw_switch_controls\)\)\s*"
                         r"continue;\s*#endif\s*switch")
        self.assertNotIn("switch_controls_gl_touch_event", window_only)

    def test_system4_repaints_when_virtual_touch_state_changes(self):
        touch = extract_function("../cmake/switch/hub/controller_runtime.c",
                                 "bool switch_controls_gl_touch_event")
        gameplay = extract_function("src/input.c", "void handle_events")
        self.assertRegex(touch, r"held = held_touch_buttons\(\);\s*"
                         r"bool handled = switch_controls_touch\(event\);\s*"
                         r"\*redraw \|= held != held_touch_buttons\(\);")
        self.assertRegex(gameplay, r"\}\s*#ifdef __SWITCH__\s*"
                         r"if \(redraw_switch_controls\) \{\s*gfx_swap\(\);\s*"
                         r"did_swap = true;\s*\}\s*#endif\s*finish_event_drain")

    def test_save_replace_and_rollback(self):
        helper = extract_function("src/savedata.c", "commit_switch_save")
        self.run_c(r"""
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define xmalloc malloc
#define WARNING(...) ((void)0)
static int fail_promotion;
static int switch_rename(const char *src, const char *dst)
{
    if (access(dst, F_OK) == 0)
        return errno = EEXIST, -1;
    if (fail_promotion && !strcmp(src, "tmp"))
        return errno = EIO, -1;
    return rename(src, dst);
}
#define rename switch_rename
""" + helper + r"""
static void write_file(const char *path, char content)
{
    FILE *fp = fopen(path, "w");
    assert(fp && fputc(content, fp) == content && fclose(fp) == 0);
}
static int read_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    assert(fp);
    int content = fgetc(fp);
    assert(fclose(fp) == 0);
    return content;
}
int main(void)
{
    write_file("tmp", '1');
    assert(commit_switch_save("tmp", "slot") == 0 && read_file("slot") == '1');
    write_file("tmp", '2');
    assert(commit_switch_save("tmp", "slot") == 0 && read_file("slot") == '2');
    assert(access("slot.switch-old", F_OK) != 0);
    write_file("tmp", '3');
    fail_promotion = 1;
    assert(commit_switch_save("tmp", "slot") == -1 && errno == EIO);
    assert(read_file("slot") == '2' && read_file("tmp") == '3');
    assert(access("slot.switch-old", F_OK) != 0);
}
""")

    def test_descriptor_save_names_are_under_launcher_folder(self):
        helper = extract_function("../xsystem35/src/gameresource.c", "descriptor_save_path")
        self.run_c(r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NOMEMERR abort
""" + helper + r"""
int main(void)
{
    const char *dir = "sdmc:/switch/alicehub/xsystem35/saves/game";
    const char *names[] = {"KICHIKUSA.ASD", "../KICHIKUSA.ASD",
                           "sdmc:/old/KICHIKUSA.ASD", "C:\\old\\KICHIKUSA.ASD"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(*names); ++i) {
        char *path = descriptor_save_path(dir, names[i]);
        assert(path && !strcmp(path, "sdmc:/switch/alicehub/xsystem35/saves/game/KICHIKUSA.ASD"));
        free(path);
    }
    assert(!descriptor_save_path(dir, "../"));
    assert(!descriptor_save_path(dir, ".."));
}
""")

    def test_descriptor_slots_with_the_same_basename_remain_separate(self):
        parser = "\n".join(
            extract_function("../xsystem35/src/gameresource.c", name)
            for name in ("storeDataName", "storeSaveName", "trimRight", "descriptor_save_path",
                         "descriptor_save_path_used", "disambiguate_descriptor_saves",
                         "initGameResourceFromFile")
        )
        self.run_c(r"""
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#define NOMEMERR abort
#define SYSERROR(...) fprintf(stderr, __VA_ARGS__)
#define max(a, b) ((a) > (b) ? (a) : (b))
enum { DRIFILE_SCO, DRIFILE_CG, DRIFILE_WAVE, DRIFILE_MIDI, DRIFILE_DATA, DRIFILE_RSC, DRIFILE_BGM };
enum { DRIFILEMAX = 255, SAVE_MAXNUMBER = 26 };
typedef struct {
    const char *game_fname[7][DRIFILEMAX], *save_fname[SAVE_MAXNUMBER];
    int cnt[7];
    const char *ain, *wai, *bgi, *sact01, *init, *alk[10], *msgskip, *save_path, *gr_fname;
} GameResource;
""" + parser + r"""
int main(void)
{
    assert(mkdir("saves", 0700) == 0);
    FILE *descriptor = tmpfile();
    assert(descriptor);
    fputs("SaveB second/SLOT.ASD\nSaveA first/slot.asd\nSaveC save-B-1.asd\n"
          "SaveD old/unique.asd\nMsgSkip old/skip.dat\n", descriptor);
    rewind(descriptor);
    GameResource gr;
    assert(initGameResourceFromFile(&gr, descriptor, "xsystem35.gr", "saves"));
    assert(!strcmp(gr.save_fname[0], "saves/slot.asd"));
    assert(!strcmp(gr.save_fname[1], "saves/save-B-2.asd"));
    assert(!strcmp(gr.save_fname[2], "saves/save-B-1.asd"));
    assert(!strcmp(gr.save_fname[3], "saves/unique.asd"));
    assert(!strcmp(gr.msgskip, "saves/skip.dat"));
    assert(!strcmp(gr.save_path, "saves"));
    for (int i = 0; i < 2; i++) {
        FILE *slot = fopen(gr.save_fname[i], "wb");
        assert(slot && fputc('A' + i, slot) == 'A' + i && fclose(slot) == 0);
    }
    for (int i = 0; i < 2; i++) {
        FILE *slot = fopen(gr.save_fname[i], "rb");
        assert(slot && fgetc(slot) == 'A' + i && fclose(slot) == 0);
    }
    fclose(descriptor);
}
""")


if __name__ == "__main__":
    unittest.main()
