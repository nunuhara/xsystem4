"""Host tests for the Switch hub's SD model, launch requests, and save transfers."""
from pathlib import Path
import re
import shlex
import struct
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile
import zlib

from switch_import_checks import SwitchImportTest
from switch_mod_checks import SwitchModsTest


REPO = Path(__file__).resolve().parents[3]
HUB = REPO / "cmake/switch/hub"
ENGINE = REPO / "xsystem4"
SYS4 = ENGINE / "subprojects/libsys4"
UNLOCK = REPO / "project/app/src/main/res/raw/full_unlock_saves.zip"

class SwitchScreenRegistryTest(unittest.TestCase):
    def test_registry_has_space_for_all_declared_hub_screens(self):
        app = (HUB / "app.c").read_text()
        capacity = int(re.search(r"const HubScreen \*screens\[(\d+)\];", app)[1])
        count = 0
        for path in HUB.glob("*.c"):
            declarations = re.findall(r"static const HubScreen \w+(?:\[\])? = \{(.*?)\};",
                                      path.read_text(), re.S)
            count += sum(max(1, initializer.count("{")) for initializer in declarations)
        self.assertLessEqual(count, capacity, f"{count} hub screens need room in the registry")


class SwitchArtworkTest(unittest.TestCase):
    def test_shared_artwork_preserves_catalog_aliases_and_limits_packaged_size(self):
        from PIL import Image

        android = REPO / "project/app/src/main"
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)
            subprocess.run(["python3", str(HUB.parent / "generate_artwork.py"),
                            str(android), tmp, str(output / "romfs")], check=True)
            aliases = set((output / "artwork_index.inc").read_text(encoding="utf-8").splitlines())
            for key in ("gakuenking", "rance1digest", "rance10"):
                self.assertIn(f'    {{"{key}", "{key}"}},', aliases)
                with Image.open(output / "romfs/artwork" / f"{key}.jpg") as image:
                    self.assertEqual((image.format, image.size), ("JPEG", (200, 156)))
            self.assertIn('    {"学园king", "gakuenking"},', aliases)
            self.assertIn('    {"ranceidigest", "rance1digest"},', aliases)
            with Image.open(output / "alicehub-icon.jpg") as image:
                self.assertEqual((image.format, image.size), ("JPEG", (256, 256)))
            self.assertLess(sum(path.stat().st_size for path in (output / "romfs/artwork").iterdir()),
                            2 * 1024 * 1024)

            (output / "romfs").rename(output / "romfs:")
            game = output / "Renamed"
            game.mkdir()
            marker = game / ".alicehub-online-v1"
            marker.write_text("gakuenking")
            packages = subprocess.run(["pkg-config", "--cflags", "--libs", "libpng", "libjpeg"],
                                      check=True, capture_output=True, text=True)
            executable = output / "check"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", tmp,
                            "-DALICEHUB_BUNDLED_ARTWORK", "-I", str(HUB.parent),
                            str(HUB.parent / "picker_art.c"), "-x", "c", "-",
                            "-o", str(executable), *shlex.split(packages.stdout)],
                           input=r"""
#include "picker_art.h"
#include <assert.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    assert(argc == 2);
    uint16_t *art = picker_art_cover(argv[1], 250, 194);
    assert(art);
    free(art);
    return 0;
}
""", text=True, check=True)
            for name in ("学园KING", "Rance I Digest"):
                (output / name).mkdir()
                subprocess.run([str(executable), name], cwd=output, check=True)
            subprocess.run([str(executable), "Renamed"], cwd=output, check=True)
            bundled = output / "romfs:/artwork/gakuenking.jpg"
            cached = marker.with_suffix(".icon.jpg")
            self.assertEqual(cached.read_bytes(), bundled.read_bytes())
            bundled.unlink()
            subprocess.run([str(executable), "Renamed"], cwd=output, check=True)

class SwitchRuntimeCaptureTest(unittest.TestCase):
    def test_captures_do_not_overwrite_and_failures_are_removed(self):
        with tempfile.TemporaryDirectory() as tmp:
            executable = Path(tmp) / "test"
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(HUB),
                 str(HUB / "runtime_capture.c"), "-x", "c", "-", "-o", str(executable)],
                input=r"""
#include "runtime_capture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static bool save(void *context, const char *path)
{
    FILE *fp = fopen(path, "wb");
    assert(fp && fputc('X', fp) == 'X' && !fclose(fp));
    return !context;
}

int main(void)
{
    char path[128];
    assert(switch_capture_write("screenshots", "bmp", 0, save, NULL, path, sizeof(path)));
    assert(!strcmp(path, "screenshots/19700101-000000-000.bmp") && !access(path, F_OK));
    assert(!switch_capture_write("screenshots", "bmp", 0, save, path, path, sizeof(path)));
    assert(access("screenshots/19700101-000000-001.bmp", F_OK) != 0);
    assert(switch_capture_write("screenshots", "bmp", 0, save, NULL, path, sizeof(path)));
    assert(!strcmp(path, "screenshots/19700101-000000-001.bmp") && !access(path, F_OK));
    assert(!access("screenshots/19700101-000000-000.bmp", F_OK));
    assert(!switch_capture_write("screenshots", "bmp", 0, save, NULL, path, 1));
    assert(access("screenshots/19700101-000000-002.bmp", F_OK) != 0);
}
""", text=True, check=True,
            )
            subprocess.run([str(executable)], cwd=tmp, check=True)


class SwitchRuntimeCtrlTest(unittest.TestCase):
    def test_ctrl_skip_advances_messages_without_sending_key_events(self):
        source = (REPO / "xsystem35/src/event.c").read_text()
        sources = [
            (source, ("event_rawkey_down", "set_raw_key", "event_reset_input_state")),
            ((REPO / "xsystem35/modules/SACT/sprite_event.c").read_text(), ("sact_wait_skip_level",)),
            ((REPO / "xsystem35/modules/SACT/sprite_msg.c").read_text(), ("smsg_keywait",)),
        ]
        funcs = "\n".join(re.search(rf"^(?:static )?\w+ {name}\([^\n]*\) \{{\n.*?^\}}",
                                    source, re.M | re.S).group()
                          for source, names in sources for name in names)
        with tempfile.TemporaryDirectory() as tmp:
            executable = Path(tmp) / "test"
            subprocess.run(
                ["cc", "-std=gnu11", "-D__SWITCH__", "-Wall", "-Wextra", "-Werror",
                 "-Wno-unused-parameter", "-x", "c", "-", "-o", str(executable)],
                input=r"""
#include <assert.h>
#include <stdbool.h>
#include <string.h>
enum { NUM_KEYCODES = 256, KEY_CTRL = 17, KEY_A = 65 };
enum { AGSEVENT_KEY_PRESS, AGSEVENT_KEY_RELEASE };
enum { KEYWAIT_NONE, KEYWAIT_MESSAGE, KEYWAIT_BACKLOG, KEYWAIT_NONCANCELABLE };
static int mouseb, joyinfo, switch_joy_buttons, ctrl_events;
static struct { int waittype, waitkey; bool zhiding; } sact;
static struct { bool is_quit; } engine, *nact = &engine;
struct markinfo { int interval, sp, cg; };
static bool RawKeyInfo[NUM_KEYCODES], skip, last_down;
static int waits, input_polls;
static bool switch_menu35_ctrl_skip(void) { return skip; }
static bool msgskip_isSkipping(void) { return false; }
static int sys_getInputInfo(void) { return ++input_polls; }
static int sys_get_ticks(void) { return 0; }
static int setupmark(int a, int b, struct markinfo *m) { return 0; }
static void update_mark(int sp, int cg) {}
void event_reset_input_state(void);
static void sys_keywait(int time, int flags)
{
    switch (++waits) {
    case 1:
        assert(sact.waittype == KEYWAIT_MESSAGE);
        sact.waittype = KEYWAIT_BACKLOG;
        skip = true;
        event_reset_input_state();
        break;
    case 2:
        assert(sact.waittype == KEYWAIT_BACKLOG);
        assert(sact.waitkey == -1);
        sact.waittype = KEYWAIT_MESSAGE;
        break;
    default:
        assert(false);
    }
}
static void send_agsevent(int type, int code)
{
    if (code != KEY_CTRL)
        return;
    ++ctrl_events;
    last_down = type == AGSEVENT_KEY_PRESS;
}
""" + funcs + r"""
int main(void)
{
    event_reset_input_state();
    assert(!event_rawkey_down(KEY_CTRL) && ctrl_events == 0);
    assert(smsg_keywait(0, 0, 0) == 0 && waits == 2 && sact.waittype == KEYWAIT_NONE);
    assert(smsg_keywait(0, 0, 0) == 0 && waits == 2 && input_polls == 1);
    assert(event_rawkey_down(KEY_CTRL) && ctrl_events == 0);
    set_raw_key(KEY_A, true);
    event_reset_input_state();
    assert(event_rawkey_down(KEY_CTRL) && !event_rawkey_down(KEY_A) && ctrl_events == 0);
    skip = false;
    event_reset_input_state();
    assert(!event_rawkey_down(KEY_CTRL) && ctrl_events == 0);
    set_raw_key(KEY_CTRL, true);
    assert(event_rawkey_down(KEY_CTRL) && ctrl_events == 1 && last_down);
    set_raw_key(KEY_CTRL, false);
    assert(!event_rawkey_down(KEY_CTRL) && ctrl_events == 2 && !last_down);
}
""", text=True, check=True,
            )
            subprocess.run([str(executable)], check=True)


    def test_ctrl_skip_during_native_dialogue(self):
        source = (REPO / "xsystem35/src/input.c").read_text()
        funcs = "\n".join(re.search(rf"^(?:static )?\w+ {name}\([^\n]*\) \{{\n.*?^\}}",
                                    source, re.M | re.S).group()
                          for name in ("menu_skip", "sys_keywait", "sys_hit_any_key"))
        with tempfile.TemporaryDirectory() as tmp:
            executable = Path(tmp) / "test"
            subprocess.run(
                ["cc", "-std=gnu11", "-D__SWITCH__", "-Wall", "-Wextra", "-Werror",
                 "-x", "c", "-", "-o", str(executable)],
                input=r"""
#include <assert.h>
#include <stdbool.h>
#include <limits.h>
enum { KEYWAIT_CANCELABLE = 1, KEYWAIT_SKIPPABLE = 2, KEYWAIT_CTRL_CANCELABLE = 4,
       KEYWAIT_MENU_SKIP = 8, MSGSKIP_STOP_ON_CLICK = 1, KEY_CTRL = 17 };
static int ticks, polls, marks;
static bool skip;
static void callback(void) {}
static struct { bool is_quit, messagewait_cancelled; void (*callback)(void); }
    engine = { .callback = callback }, *nact = &engine;
static int hak_ignore_mask = -1, hak_releasewait_mask = 16;
static bool switch_menu35_ctrl_skip(void) { return skip; }
static bool msgskip_isSkipping(void) { return false; }
static int msgskip_getFlags(void) { return 0; }
static void msgskip_activate(bool enabled) { assert(!enabled); }
static void sys_sleep(int duration) { ticks += duration; }
static void sys_wait_vsync(void) { ticks += 16; }
static int sys_get_ticks(void) { return ticks; }
static void texthook_keywait(void) {}
static void cancel_yield(void) {}
static void msg_hitAnyKey(void) { ++marks; }
static bool event_rawkey_down(int code) { assert(code == KEY_CTRL); return true; }
static bool sys_is_quit_requested(void) { return false; }
static void nact_quit(bool force) { (void)force; }
static int sys_getInputInfo(void)
{
    assert(polls < 2);
    skip = ++polls == 2;
    return 0;
}
""" + funcs + r"""
int main(void)
{
    skip = true;
    sys_hit_any_key();
    assert(polls == 0 && marks == 0);
    skip = false;
    sys_hit_any_key();
    assert(polls == 2 && marks == 1 && skip);
    polls = 0;
    skip = false;
    nact->messagewait_cancelled = true;
    sys_hit_any_key();
    assert(polls == 2 && marks == 2 && skip && !nact->messagewait_cancelled);
}
""", text=True, check=True,
            )
            subprocess.run([str(executable)], check=True)


    def test_streamer_row_requires_engine_capability(self):
        source = (HUB / "runtime_menu_system4.c").read_text()
        locks = source[source.index("enum { CTRL_SKIP"):source.index("static const int font_steps")]
        options = re.search(r"^static size_t options[^\n]*\n\{\n.*?^\}", source, re.M | re.S).group()
        with tempfile.TemporaryDirectory() as tmp:
            executable = Path(tmp) / "test"
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(ENGINE / "include"),
                 "-x", "c", "-", "-o", str(executable)],
                input=r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "modifier_api.h"
#define SDL_arraysize(a) (sizeof(a) / sizeof((a)[0]))
#define SDL_strlcpy(out, in, size) snprintf(out, size, "%s", in)
typedef struct { const char *label; int action; bool enabled; char value[32]; } SwitchMenuRow;
static bool supported;
bool modifier_api_has_streamer_mode(void) { return supported; }
bool modifier_api_get_ctrl_skip(void) { return false; }
int modifier_api_get_font_scale(void) { return 100; }
int modifier_api_get_scrollbar_scale(void) { return 100; }
int modifier_api_get_game_lock_mask(const char *game)
{
    assert(!strcmp(game, "rance10"));
    return 1 << 10;
}
""" + locks + r"""
static const LiveLocks *current_locks(void) { return supported_locks; }
""" + options + r"""
int main(void)
{
    SwitchMenuRow rows[12];
    assert(options(NULL, true, rows) == 11);
    for (int i = 0; i < 11; i++)
        assert(rows[i].action == LOCK_BASE + i);
    assert(!strcmp(rows[10].value, "On"));
    supported = true;
    assert(options(NULL, true, rows) == 12);
    assert(!strcmp(rows[11].label, "Streamer mode") && rows[11].action == LOCK_BASE + 11);
}
""", text=True, check=True,
            )
            subprocess.run([str(executable)], check=True)


class SwitchSaveTest(unittest.TestCase):
    def run_harness(self, source, extra=(), libraries=(), setup=None):
        with tempfile.TemporaryDirectory() as tmp:
            if setup:
                setup(tmp)
            executable = Path(tmp) / "test"
            subprocess.run(
                ["cc", "-std=gnu11", "-I", str(HUB), "-I", str(ENGINE / "include"),
                 "-I", str(SYS4 / "include"), str(HUB / "save_storage.c"), *extra,
                 "-x", "c", "-", "-o", str(executable), *libraries],
                input=source, text=True, check=True,
            )
            subprocess.run([str(executable)], cwd=tmp, check=True)

    def test_save_storage_unlock_and_fat_fallback(self):
        self.run_harness(r"""
#include "save_storage.h"
#include "savedata.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool fail_publish;
int __real_rename(const char *, const char *);
int __wrap_rename(const char *src, const char *dst)
{
    struct stat st;
    if (!stat(dst, &st)) {
        errno = EIO;
        return -1;
    }
    if (fail_publish && !strcmp(dst, "saves/game/LocalSave10.asd") && strstr(src, ".hub-tmp")) {
        errno = EIO;
        return -1;
    }
    return __real_rename(src, dst);
}

static void write_text(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");
    assert(fp && fputs(text, fp) >= 0 && !fclose(fp));
}

static void expect_text(const char *path, const char *text)
{
    char value[256] = {0};
    FILE *fp = fopen(path, "rb");
    assert(fp && fread(value, 1, sizeof(value) - 1, fp) == strlen(text) && !fclose(fp));
    assert(!strcmp(value, text));
}

int main(void)
{
    assert(!mkdir("game", 0700) && !mkdir("assets", 0700));
    write_text("game/Evenicle.ain", "");
    assert(!hub_save_supported("game"));
    write_text("game/Rance10.AIN", "");
    assert(hub_save_supported("game"));
    const char *files[] = {"GlobalSave.asd", "Collection.asd", "AFCGMode.asd", "AFBGMMode.asd"};
    for (size_t i = 0; i < 4; ++i) {
        HubSavePath file;
        assert(hub_save_join(file, "assets", files[i]));
        write_text(file, files[i]);
    }
    assert(hub_save_install_unlock("saves/fresh", "assets"));
    expect_text("saves/fresh/GlobalSave.asd", "GlobalSave.asd");
    assert(hub_save_restore_set("saves/fresh", HUB_SAVE_UNLOCK_BACKUP));
    assert(access("saves/fresh/GlobalSave.asd", F_OK) && errno == ENOENT);

    assert(!mkdir("saves/game", 0700) && !mkdir("saves/game/SaveData", 0700));
    write_text("saves/game/GlobalSave.asd", "root original");
    write_text("saves/game/SaveData/Collection.asd", "nested original");
    write_text("saves/game/SaveData/LocalSave2.ASD", "two");
    write_text("saves/game/LocalSave10.asd", "ten");
    write_text("saves/game/LocalSaveX.asd", "ignored");
    HubSaveSlots slots;
    assert(hub_save_scan("saves/game", &slots) && slots.count == 2);
    assert(!strcmp(slots.paths[0], "SaveData/LocalSave2.ASD"));
    assert(!strcmp(slots.paths[1], "LocalSave10.asd"));
    free(slots.paths);

    HubSavePath global, collection;
    assert(hub_save_global_paths("saves/game", global, collection));
    assert(!strcmp(global, "saves/game/GlobalSave.asd"));
    assert(!strcmp(collection, "saves/game/SaveData/Collection.asd"));
    assert(hub_save_install_unlock("saves/game", "assets"));
    assert(hub_save_install_unlock("saves/game", "assets"));
    expect_text("saves/game/GlobalSave.asd", "GlobalSave.asd");
    expect_text("saves/game/SaveData/Collection.asd", "Collection.asd");
    assert(hub_save_has_set_backup("saves/game", HUB_SAVE_UNLOCK_BACKUP));
    assert(hub_save_restore_set("saves/game", HUB_SAVE_UNLOCK_BACKUP));
    expect_text("saves/game/GlobalSave.asd", "root original");
    expect_text("saves/game/SaveData/Collection.asd", "nested original");
    assert(access("saves/game/SaveData/AFBGMMode.asd", F_OK) && errno == ENOENT);
    assert(!hub_save_has_set_backup("saves/game", HUB_SAVE_UNLOCK_BACKUP));

    const char *local = "saves/game/LocalSave10.asd";
    char *temp;
    FILE *fp = atomic_save_open(local, "wb", &temp);
    assert(fp && fputs("modified", fp) >= 0);
    fail_publish = true;
    assert(!atomic_save_commit(fp, local, temp, false));
    expect_text(local, "ten");
    fail_publish = false;
    fp = atomic_save_open(local, "wb", &temp);
    assert(fp && fputs("modified", fp) >= 0 && atomic_save_commit(fp, local, temp, false));
    expect_text(local, "modified");
    assert(!rename(local, "saves/game/LocalSave10.asd.hub-old"));
    assert(hub_save_scan("saves/game", &slots) && slots.count == 2);
    assert(!strcmp(slots.paths[1], "LocalSave10.asd"));
    free(slots.paths);
    assert(hub_save_restore(local, HUB_SAVE_EDIT_BACKUP));
    expect_text(local, "ten");
    assert(!hub_save_has_backup(local, HUB_SAVE_EDIT_BACKUP));
}
""", extra=["-Wall", "-Wextra", "-Werror"], libraries=["-Wl,--wrap=rename"])

    def test_offline_codec_round_trip(self):
        with tempfile.TemporaryDirectory() as tmp:
            probe = subprocess.run(
                ["cc", "-x", "c", "-", "-lz", "-o", str(Path(tmp) / "zlib-test")],
                input="#include <zlib.h>\nint main(void) { return zlibVersion() == 0; }",
                text=True, capture_output=True,
            )
        if probe.returncode:
            self.skipTest("host zlib development package required for codec test")

        def assets(tmp):
            with zipfile.ZipFile(UNLOCK) as archive:
                archive.extractall(Path(tmp) / "assets")

        sources = ("savefile", "buffer", "file", "mt19937int", "string", "system", "utfsjis", "zlib")
        self.run_harness(r"""
#include "save_storage.h"
#include "save_presets.h"
#include "modifier_api.h"
#include "cJSON.h"
#include "system4/savefile.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static cJSON *read_summary(char *text)
{
    cJSON *json = cJSON_Parse(text);
    free(text);
    assert(json);
    return json;
}

int main(void)
{
    const char *local = "LocalSave1.asd";
    struct gsave *save = gsave_create(5, "test", 1, "local");
    gsave_add_globals_record(save, 1);
    save->globals[0] = (struct gsave_global){AIN_INT, 3, strdup("\x81\x9f\x90\xed\x93\xac\x89\xf1\x90\x94"), 1};
    FILE *fp = fopen(local, "wb");
    assert(fp && gsave_write(save, fp, true, 1) == SAVEFILE_SUCCESS && !fclose(fp));
    gsave_free(save);
    cJSON *json = read_summary(modifier_api_read_local_save_summary(local));
    assert(cJSON_IsTrue(cJSON_GetObjectItem(json, "valid")));
    assert(cJSON_GetObjectItem(json, "battleCount")->valueint == 3);
    assert(hub_save_preset_count(false) == 4 && hub_save_preset_count(true) == 1);
    cJSON *preset = hub_save_preset_changes(false, 0, json);
    assert(preset && cJSON_GetObjectItem(preset, "foodTicket")->valueint == 999);
    assert(cJSON_GetObjectItem(preset, "atkPerPhys")->valueint == 999);
    assert(cJSON_GetObjectItem(preset, "statusMaxPer")->valueint == 999);
    cJSON_Delete(preset);
    preset = hub_save_preset_changes(false, 1, json);
    assert(preset && cJSON_GetArraySize(preset) == 3);
    assert(cJSON_GetObjectItem(preset, "perFindTreasure")->valueint == 999);
    assert(cJSON_GetObjectItem(preset, "treasureNotGetCount")->valueint == 99);
    assert(cJSON_GetObjectItem(preset, "rareLuck")->valueint == 999);
    cJSON_Delete(preset);
    preset = hub_save_preset_changes(false, 2, json);
    assert(preset && cJSON_GetArraySize(preset) == 5);
    assert(cJSON_GetObjectItem(preset, "setCardCount")->valueint == 99);
    assert(cJSON_GetObjectItem(preset, "setCharStar")->valueint == 5);
    cJSON_Delete(preset);
    preset = hub_save_preset_changes(false, 3, json);
    assert(preset && cJSON_GetObjectItem(preset, "humanDead")->valueint == 0);
    assert(cJSON_GetObjectItem(preset, "leazasState")->valueint == 0);
    assert(cJSON_GetObjectItem(preset, "subHanny")->valueint == 99999);
    cJSON_Delete(preset);
    cJSON_Delete(json);
    assert(modifier_api_write_local_save_mod(local, "{\"battleCount\":5}"));
    json = read_summary(modifier_api_read_local_save_summary(local));
    assert(cJSON_GetObjectItem(json, "battleCount")->valueint == 5);
    cJSON_Delete(json);
    assert(hub_save_restore(local, HUB_SAVE_EDIT_BACKUP));
    json = read_summary(modifier_api_read_local_save_summary(local));
    assert(cJSON_GetObjectItem(json, "battleCount")->valueint == 3);
    cJSON_Delete(json);

    assert(hub_save_install_unlock("saves/game", "assets"));
    HubSavePath global, collection;
    assert(hub_save_global_paths("saves/game", global, collection));
    json = read_summary(modifier_api_read_global_save_summary(global, collection));
    assert(cJSON_IsTrue(cJSON_GetObjectItem(json, "hasGlobal")));
    assert(cJSON_IsTrue(cJSON_GetObjectItem(json, "hasCollection")));
    preset = hub_save_preset_changes(true, 0, json);
    assert(preset && cJSON_GetArraySize(preset) == 22);
    assert(cJSON_GetObjectItem(preset, "gamePlayCount")->valueint == 50);
    assert(cJSON_GetObjectItem(preset, "leaderAtkRatio")->valuedouble == 10);
    assert(cJSON_GetObjectItem(preset, "totalClearPoint")->valueint == 999);
    char *preset_text = cJSON_PrintUnformatted(preset);
    assert(preset_text && modifier_api_write_global_save_mod(global, collection, preset_text));
    free(preset_text);
    cJSON_Delete(preset);
    cJSON_Delete(json);
    json = read_summary(modifier_api_read_global_save_summary(global, collection));
    assert(cJSON_GetObjectItem(json, "totalClearPoint")->valueint == 999);
    assert(cJSON_GetObjectItem(json, "gamePlayCount")->valueint == 50);
    assert(cJSON_GetObjectItem(json, "leaderAtkRatio")->valuedouble == 10);
    cJSON_Delete(json);
    json = cJSON_Parse("{\"hasCommonSystemData\":true}");
    preset = hub_save_preset_changes(true, 0, json);
    assert(preset && cJSON_GetArraySize(preset) == 15);
    assert(!cJSON_GetObjectItem(preset, "gamePlayCount") && !cJSON_GetObjectItem(preset, "leaderAtkRatio"));
    cJSON_Delete(preset);
    cJSON_Delete(json);
    assert(modifier_api_write_global_save_mod(global, collection,
        "{\"gamePlayCount\":20,\"leaderAtkRatio\":2.5,\"OpenMonsterpedia\":1}"));
    json = read_summary(modifier_api_read_global_save_summary(global, collection));
    assert(cJSON_GetObjectItem(json, "gamePlayCount")->valueint == 20);
    assert(cJSON_GetObjectItem(json, "leaderAtkRatio")->valuedouble == 2.5);
    assert(cJSON_GetObjectItem(json, "OpenMonsterpedia")->valueint == 1);
    cJSON_Delete(json);
    assert(hub_save_restore_set("saves/game", HUB_SAVE_EDIT_BACKUP));
    json = read_summary(modifier_api_read_global_save_summary(global, collection));
    assert(cJSON_IsFalse(cJSON_GetObjectItem(json, "hasCommonSystemData")));
    cJSON_Delete(json);
}
""", extra=["-DMODIFIER_OFFLINE_ONLY", str(HUB / "save_presets.c"), str(ENGINE / "src/modifier.c"),
               str(ENGINE / "src/cJSON.c"), *(str(SYS4 / f"src/{s}.c") for s in sources)],
            libraries=["-lz", "-lm"], setup=assets)


class SwitchHubTest(unittest.TestCase):
    def test_online_catalog_links_the_published_mobile_terms(self):
        online = (HUB / "online.c").read_text(encoding="utf-8")
        path = re.search(r'^#define TERMS_URL "https://github.com/TomorrowX6/AliceHub/blob/main/([^"]+)"$',
                         online, re.M)[1]
        terms = (REPO / path).read_text(encoding="utf-8")
        android = REPO / "project/app/src/main/res"
        for locale, heading in (("values-zh", "简体中文"), ("values", "English"), ("values-ja", "日本語")):
            strings = ET.parse(android / locale / "strings.xml")
            mobile = strings.find("./string[@name='online_disclaimer_msg']").text.replace(r"\n", "\n")
            published = terms.split(f"## {heading}\n\n", 1)[1].split("\n\n## ", 1)[0]
            self.assertEqual(mobile, published.rstrip("\n"))
        with tempfile.TemporaryDirectory() as tmp:
            index = Path(tmp) / "online_catalog.inc"
            subprocess.run(["python3", str(HUB / "generate_online_catalog.py"), str(REPO), str(index)],
                           check=True)
            generated = index.read_text(encoding="utf-8")
            self.assertIn('{"gakuenking",', generated)
            self.assertNotIn("online_terms", generated)

    def test_online_manifest_and_resume(self):
        repo = Path(__file__).resolve().parents[3]
        hub = repo / "cmake/switch/hub"
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(hub), "-I", str(repo / "xsystem4/include"),
                 str(hub / "model.c"), str(hub / "install_transaction.c"), str(hub / "fs_util.c"),
                 str(hub / "online_manifest.c"), str(repo / "xsystem4/src/cJSON.c"),
                 "-x", "c", "-", "-o", str(Path(tmp) / "test")],
                input=r"""
#include "online_manifest.h"
#include <assert.h>
#include <string.h>

#define SHA "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
#define ENTRY(PATH, BYTES) "{\"path\":\"" PATH "\",\"key\":\"Test/日本 語.ald\"," \
                           "\"sha256\":\"" SHA "\",\"size\":" BYTES "}"
#define MANIFEST(ENTRIES) "{\"baseName\":\"Test Game\",\"files\":[" ENTRIES "]}"

int main(void)
{
    OnlineManifest doc = {0};
    assert(online_manifest_parse(MANIFEST(ENTRY("GAMEsa.ALD", "4294967295") ","
                                         ENTRY("data\\\\sample", "0")), &doc));
    assert(doc.count == 2 && doc.total == ONLINE_FILE_LIMIT);
    assert(!strcmp(doc.folder, "Test Game") && !strcmp(doc.files[0].path, "data/sample"));
    assert(!strcmp(doc.files[1].sha256,
                   "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    assert(online_boot_marker(doc.files[1].path) == HUB_XSYSTEM35);
    assert(online_boot_marker("ADISK.DAT") == HUB_SYSTEM3);
    assert(online_boot_marker("AliceStart.ini") == HUB_XSYSTEM4);
    assert(online_boot_marker("nested/ADISK.DAT") == HUB_ENGINE_COUNT);
    online_manifest_free(&doc);
    assert(!online_manifest_parse(MANIFEST(ENTRY("GAMEsa.ALD", "4294967296")), &doc));
    assert(!online_manifest_parse(MANIFEST(ENTRY("GAMEsa.ALD", "23.25")), &doc));
    assert(!online_manifest_parse(MANIFEST(ENTRY("GAMEsa.ALD", "-1")), &doc));
    assert(!online_manifest_parse(MANIFEST(ENTRY("../file", "1")), &doc));
    assert(!online_manifest_parse(MANIFEST(ENTRY("AA", "1") "," ENTRY("aa/b", "1")), &doc));
    assert(!online_manifest_parse(MANIFEST(ENTRY("FILE", "1") "," ENTRY("file", "1")), &doc));
    assert(!online_manifest_parse(MANIFEST(ENTRY(".AliceHub-online-v1", "1")), &doc));
    assert(!online_manifest_parse(MANIFEST(ENTRY("n\\u0000ul", "1")), &doc));
    char url[ONLINE_URL_SIZE];
    assert(online_payload_url("rance10", NULL, url) &&
           !strcmp(url, "https://alicehub.000.moe/rance10/manifest.json"));
    assert(online_payload_url("test", "Test/日本 語.ald", url) &&
           !strcmp(url, "https://alicehub.000.moe/test/files/Test/%e6%97%a5%e6%9c%ac%20%e8%aa%9e.ald"));
    assert(!online_payload_url("test", "../file", url));
    assert(!online_payload_url("https://bad.invalid", "file", url));
    assert(online_resume_matches("bytes 2-3/4", "\"abc\"", "\"abc\"", 2, 4));
    assert(!online_resume_matches("bytes 2-3/4", "\"def\"", "\"abc\"", 2, 4));
    assert(!online_resume_matches("bytes 2-3/4", "W/\"abc\"", "\"abc\"", 2, 4));
    assert(!online_resume_matches("bytes 0-3/4", "\"abc\"", "\"abc\"", 2, 4));
    assert(!online_content_range("bytes 2-3/5", 2, 4));
    assert(!online_content_range("bytes 2-2/4", 2, 4));
    assert(!online_content_range("bytes 18446744073709551616-3/4", 2, 4));
    assert(!online_content_range("bytes 2-3/4 trailing", 2, 4));
}
""",
                text=True,
                check=True,
            )
            subprocess.run([str(Path(tmp) / "test")], cwd=tmp, check=True)

    def test_update_check_defaults_and_release_tags(self):
        with tempfile.TemporaryDirectory() as tmp:
            test = Path(tmp) / "test"
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(HUB), "-I", str(REPO / "xsystem4/include"),
                 str(HUB / "update_check.c"), str(REPO / "xsystem4/src/cJSON.c"),
                 "-x", "c", "-", "-o", str(test)],
                input=r"""
#include "update_check.h"
#include <assert.h>
#include <string.h>

#define SHA "abcdef0123456789abcdef0123456789abcdef01"
#define RELEASE(TAG) "{\"tag_name\":\"" TAG "\"}"
#define COMPARE(STATUS) "{\"status\":\"" STATUS "\"}"

int main(void)
{
    assert(hub_update_enabled("."));
    assert(hub_update_set_enabled(".", false));
    assert(!hub_update_enabled("."));
    assert(hub_update_set_enabled(".", true));
    assert(hub_update_enabled("."));
    char tag[HUB_UPDATE_TAG_SIZE], url[HUB_UPDATE_COMPARE_URL_SIZE];
    assert(hub_update_release_tag(RELEASE("ABCDEF0"), tag));
    assert(hub_update_release_tag(RELEASE("1234567"), tag));
    assert(!strcmp(tag, "1234567"));
    assert(!hub_update_release_tag(RELEASE("v0.1"), tag));
    assert(!hub_update_release_tag(RELEASE("1234567") "junk", tag));
    assert(!hub_update_compare_url("unknown", "1234567", url));
    assert(hub_update_compare_url(SHA, tag, url));
    assert(!strcmp(url, HUB_UPDATE_API "/compare/1234567..." SHA "?per_page=1"));
    assert(hub_update_is_behind(COMPARE("behind")));
    assert(!hub_update_is_behind(COMPARE("identical")));
    assert(!hub_update_is_behind(COMPARE("ahead")));
    assert(!hub_update_is_behind(COMPARE("diverged")));
    assert(!hub_update_is_behind(COMPARE("behind") "junk"));
    assert(!hub_update_is_behind("{}"));
}
""",
                text=True,
                check=True,
            )
            subprocess.run([str(test)], cwd=tmp, check=True)

    def test_install_transaction(self):
        hub = Path(__file__).resolve().parents[3] / "cmake/switch/hub"
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(hub),
                 str(hub / "model.c"), str(hub / "install_transaction.c"), str(hub / "fs_util.c"),
                 "-x", "c", "-", "-Wl,--wrap=rename", "-Wl,--wrap=fopen",
                 "-o", str(Path(tmp) / "test")],
                input=r"""
#include "install_transaction.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define TARGET "./xsystem4/games/Alice"
#define WORK "./xsystem4/.install-v1/Alice"
static bool no_rename;
static unsigned copy_attempts, fail_at, crash_at;

int __real_rename(const char *, const char *);
int __wrap_rename(const char *source, const char *destination)
{
    struct stat st;
    if (no_rename && !lstat(source, &st) && S_ISDIR(st.st_mode)) {
        errno = ENOSYS;
        return -1;
    }
    return __real_rename(source, destination);
}

FILE *__real_fopen(const char *, const char *);
FILE *__wrap_fopen(const char *path, const char *mode)
{
    if (!strncmp(path, TARGET "/", sizeof(TARGET)) && !strcmp(mode, "wbx")) {
        ++copy_attempts;
        if (copy_attempts == crash_at) _exit(42);
        if (copy_attempts == fail_at) {
            errno = EIO;
            return NULL;
        }
    }
    return __real_fopen(path, mode);
}

static void put(const char *path, int value)
{
    FILE *f = fopen(path, "wb");
    assert(f && fputc(value, f) != EOF && !fclose(f));
}

static bool verify(const char *dir, void *context)
{
    char path[HUB_PATH_SIZE];
    assert(snprintf(path, sizeof(path), "%s/data/game.dat", dir) < (int)sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    bool ok = fgetc(f) == *(int *)context;
    fclose(f);
    return ok;
}

static HubInstallTransaction *stage_overlay(const HubGame *game, int *value)
{
    HubInstallTransaction *tx = hub_install_begin(".", game);
    char path[HUB_PATH_SIZE];
    assert(tx && hub_install_stage_file(tx, "data/game.dat", path));
    put(path, *value);
    assert(hub_install_stage_file(tx, "added.dat", path));
    put(path, 9);
    assert(hub_install_verify(tx, verify, value));
    return tx;
}

int main(void)
{
    HubGame game;
    assert(hub_game_init(&game, HUB_XSYSTEM4, "Alice"));
    assert(hub_install_recover("."));
    HubInstallTransaction *tx = hub_install_begin(".", &game);
    assert(tx);
    assert(!hub_install_begin(".", &game) && errno == EBUSY);
    assert(!hub_install_recover(".") && errno == EBUSY);
    char path[HUB_PATH_SIZE], target[HUB_PATH_SIZE];
    assert(hub_game_path(".", &game, target));
    assert(access(target, F_OK));
    assert(!hub_install_stage_file(tx, "../../outside", path));
    assert(!hub_install_stage_file(tx, "alias. /file", path));
    assert(hub_install_stage_file(tx, "data/game.dat", path));
    assert(!symlink("/tmp", path));
    assert(!hub_install_stage_file(tx, "data/game.dat", path));
    int old = 1, new = 2;
    assert(!hub_install_verify(tx, verify, &old));
    assert(!unlink(path));
    put(path, old);
    assert(!hub_install_commit(tx));
    assert(!hub_install_verify(tx, verify, &new));
    assert(hub_install_verify(tx, verify, &old));
    assert(hub_install_commit(tx) && verify(target, &old));

    assert(!mkdir("xsystem4/saves", 0700) && !mkdir("xsystem4/saves/Alice", 0700));
    put("xsystem4/saves/Alice/save.dat", 3);
    tx = hub_install_begin(".", &game);
    assert(tx && hub_install_stage_file(tx, "data/game.dat", path));
    put(path, new);
    assert(hub_install_verify(tx, verify, &new));
    assert(hub_install_rollback(tx) && verify(target, &old));

    const char *work = "xsystem4/.install-v1/Alice";
    const char *backup = "xsystem4/.install-v1/Alice/backup";
    tx = hub_install_begin(".", &game);
    assert(tx && hub_install_stage_file(tx, "data/game.dat", path));
    put(path, new);
    assert(!mkdir("xsystem4/.install-v1/Alice/backup", 0700));
    put("xsystem4/.install-v1/Alice/backup/occupied", 4);
    assert(hub_install_verify(tx, verify, &new));
    assert(!hub_install_commit(tx));
    assert(hub_install_rollback(tx) && verify(target, &old));

    assert(!mkdir(work, 0700) && !rename(target, backup));
    assert(hub_install_recover(".") && verify(target, &old));
    assert(!mkdir(work, 0700) && !mkdir(backup, 0700));
    assert(hub_install_recover(".") && verify(target, &old) && access(backup, F_OK));
    tx = hub_install_begin(".", &game);
    assert(tx && hub_install_stage_file(tx, "data/game.dat", path));
    put(path, new);
    assert(hub_install_verify(tx, verify, &new));
    assert(hub_install_commit(tx) && verify(target, &new));

    no_rename = true;
    tx = stage_overlay(&game, &old);
    assert(hub_install_commit(tx) && verify(target, &old));
    assert(!access(TARGET "/added.dat", F_OK) && access(WORK, F_OK));
    assert(!unlink(TARGET "/added.dat"));
    put(TARGET "/original.dat", 7);

    tx = stage_overlay(&game, &new);
    copy_attempts = 0;
    fail_at = 2;
    assert(!hub_install_commit(tx) && copy_attempts == 2);
    assert(!access(WORK "/replacing", F_OK) && !access(WORK "/backup/original.dat", F_OK));
    assert(hub_install_rollback(tx) && verify(target, &old));
    assert(!access(TARGET "/original.dat", F_OK) && access(TARGET "/added.dat", F_OK));

    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        copy_attempts = fail_at = 0;
        crash_at = 2;
        tx = stage_overlay(&game, &new);
        hub_install_commit(tx);
        _exit(99);
    }
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 42);
    assert(!access(WORK "/replacing", F_OK) && !access(WORK "/backup/original.dat", F_OK));
    copy_attempts = 0;
    assert(!hub_install_recover("."));
    assert(!access(WORK "/replacing", F_OK) && !access(WORK "/backup/original.dat", F_OK));
    HubGame other;
    assert(hub_game_init(&other, HUB_XSYSTEM4, "Unrelated"));
    assert(!mkdir("xsystem4/games/Unrelated", 0700));
    HubLibrary library = {0};
    assert(hub_library_scan(&library, "."));
    bool found = false;
    for (size_t i = 0; i < library.count; ++i)
        found |= !strcmp(library.games[i].instance_id, other.instance_id);
    assert(found);
    assert(hub_install_pending(".", &game) && !hub_install_pending(".", &other));
    hub_library_free(&library);
    fail_at = 0;
    assert(hub_install_recover(".") && verify(target, &old));
    assert(!access(TARGET "/original.dat", F_OK) && access(TARGET "/added.dat", F_OK) && access(WORK, F_OK));
    assert(!access("xsystem4/saves/Alice/save.dat", F_OK));
}
""",
                text=True,
                check=True,
            )
            subprocess.run([str(Path(tmp) / "test")], cwd=tmp, check=True)

    def test_support_storage_and_error_export(self):
        hub = Path(__file__).resolve().parents[3] / "cmake/switch/hub"
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(hub),
                 str(hub / "model.c"), str(hub / "support_files.c"),
                 "-x", "c", "-", "-o", str(Path(tmp) / "support")],
                input=r"""
#include "support_files.h"
#include <assert.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file && fputs(text, file) >= 0 && !fclose(file));
}

int main(void)
{
    const char *dirs[] = {"system3", "system3/games", "system3/saves", "cache",
                          "cache/nested", ".downloads"};
    for (size_t i = 0; i < sizeof(dirs) / sizeof(*dirs); i++)
        assert(!mkdir(dirs[i], 0700));
    write_file("system3/games/data", "game");
    write_file("system3/saves/data", "save!");
    write_file(".downloads/data", "resume");
    write_file("cache/nested/data", "tmp");
    assert(!symlink("../../system3/games", "cache/nested/linked-game"));
    HubStorage storage = hub_support_storage(".");
    assert(storage.sizes_ok && storage.volume_ok && storage.capacity > 0);
    assert(storage.games == 4 && storage.saves == 5 && storage.cache == 3);
    assert(hub_support_clear_cache("."));
    assert(access("cache/nested", F_OK) && !access(".downloads/data", F_OK));
    storage = hub_support_storage(".");
    assert(storage.sizes_ok && storage.cache == 0 && storage.games == 4 && storage.saves == 5);
    assert(!rmdir("cache") && !symlink("system3/games", "cache"));
    assert(!hub_support_clear_cache(".") && !access("system3/games/data", F_OK));
    assert(!hub_support_export_error("."));
    HubGame game;
    assert(hub_game_init(&game, HUB_SYSTEM3, "Rance"));
    assert(hub_support_record_error(".", &game, "Missing scenario file"));
    assert(hub_support_export_error("."));
    assert(hub_support_record_error(".", &game, "Missing fonts"));
    assert(hub_support_export_error("."));
    FILE *report = fopen(HUB_SUPPORT_REPORT, "rb");
    char data[256] = {0};
    assert(report && fread(data, 1, sizeof(data) - 1, report) && !fclose(report));
    assert(strstr(data, "Nintendo Switch") && strstr(data, "Game: Rance"));
    assert(strstr(data, "Missing fonts") && !strstr(data, "Missing scenario file"));
}
""",
                text=True,
                check=True,
            )
            subprocess.run([str(Path(tmp) / "support")], cwd=tmp, check=True)

    def test_controller_profile_inheritance_and_layout(self):
        hub = Path(__file__).resolve().parents[3] / "cmake/switch/hub"
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(hub),
                 str(hub / "model.c"), str(hub / "controller_profile.c"),
                 "-x", "c", "-", "-o", str(Path(tmp) / "test")],
                input=r"""
#include "controller_profile.h"
#include <assert.h>

int main(void)
{
    HubGame system3, system4;
    assert(hub_game_init(&system3, HUB_SYSTEM3, "ランス"));
    assert(hub_game_init(&system4, HUB_XSYSTEM4, "ランス"));
    ControllerProfile profile;
    assert(controller_profile_load(".", &system3, &profile) == CP_DEFAULTS);
    assert(!profile.touch_enabled);
    assert(controller_profile_map(&profile, 1u << CP_A) == (1u << CP_A));
    assert(controller_profile_hit(&profile, .9f, .7f) == -1);

    profile.binding[CP_A] = CP_B;
    profile.binding[CP_B] = CP_B;
    profile.binding[CP_Y] = CP_BUTTONS;
    profile.touch_enabled = true;
    assert(controller_profile_save(".", NULL, &profile));
    assert(controller_profile_load(".", &system3, &profile) == CP_GLOBAL);
    assert(controller_profile_map(&profile, (1u << CP_A) | (1u << CP_B) | (1u << CP_Y)) == (1u << CP_B));
    assert(controller_profile_hit(&profile, .9f, .7f) == 4);
    assert(controller_profile_hit(&profile, 1.0f, .7f) == -1);

    assert(controller_profile_move(&profile, 4, profile.touch[5].column, profile.touch[5].row));
    assert(controller_profile_hit(&profile, .9f, .7f) == 5);
    assert(!controller_profile_move(&profile, 4, CP_COLUMNS, 0));
    profile.binding[CP_A] = CP_X;
    assert(controller_profile_save(".", &system3, &profile));
    assert(controller_profile_load(".", &system4, &profile) == CP_GLOBAL);
    assert(profile.binding[CP_A] == CP_B);
    assert(controller_profile_load(".", &system3, &profile) == CP_GAME);
    assert(profile.binding[CP_A] == CP_X && controller_profile_hit(&profile, .9f, .7f) == 5);

    FILE *bad = fopen("controller-profiles/system3/ランス/profile", "wb");
    assert(bad && fputs("alicehub-controller 1\n2\n", bad) >= 0 && !fclose(bad));
    assert(controller_profile_load(".", &system3, &profile) == CP_UNREADABLE);
    assert(profile.binding[CP_A] == CP_B);
    assert(controller_profile_reset(".", &system3));
    assert(controller_profile_load(".", &system3, &profile) == CP_GLOBAL);
    assert(!rename("controller-profile", "controller-profile.bak"));
    assert(controller_profile_load(".", &system3, &profile) == CP_GLOBAL);
    assert(controller_profile_reset(".", NULL));
    assert(controller_profile_load(".", &system3, &profile) == CP_DEFAULTS);
}
""",
                text=True,
                check=True,
            )
            subprocess.run([str(Path(tmp) / "test")], cwd=tmp, check=True)

    def test_model_and_handoff(self):
        hub = Path(__file__).resolve().parents[3] / "cmake/switch/hub"
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(hub),
                 str(hub / "model.c"), "-x", "c", "-", "-o", str(Path(tmp) / "test")],
                input=r"""
#include "model.h"
#include <assert.h>
#include <string.h>
#include <sys/stat.h>

static bool parse(const char *text, HubHandoff *handoff)
{
    FILE *file = tmpfile();
    assert(file && fputs(text, file) >= 0);
    rewind(file);
    bool ok = hub_handoff_parse(file, handoff);
    assert(!fclose(file));
    return ok;
}

static void test_library_view(void)
{
    HubGame games[4];
    assert(hub_game_init(&games[0], HUB_XSYSTEM4, "爱丽丝学园"));
    assert(hub_game_init(&games[1], HUB_XSYSTEM35, "King II"));
    assert(hub_game_init(&games[2], HUB_SYSTEM3, "King II"));
    assert(hub_game_init(&games[3], HUB_SYSTEM3, "zeta"));
    assert(hub_game_matches(&games[0], "丽丝"));
    assert(!hub_game_matches(&games[0], "麗絲"));
    assert(hub_game_matches(&games[1], "kInG ii"));
    assert(!hub_game_matches(&games[1], "King III"));
    assert(hub_game_matches(&games[1], ""));

    HubLibrary library = {games, 4};
    hub_library_sort(&library, HUB_SORT_NAME);
    assert(!strcmp(library.games[0].name, "King II"));
    assert(library.games[0].engine == HUB_SYSTEM3 && library.games[1].engine == HUB_XSYSTEM35);
    assert(!strcmp(library.games[2].name, "zeta"));
    hub_library_sort(&library, HUB_SORT_ENGINE);
    assert(!strcmp(library.games[0].instance_id, "local:system3:King II"));
    assert(library.games[1].engine == HUB_SYSTEM3 && !strcmp(library.games[1].name, "zeta"));
    assert(library.games[2].engine == HUB_XSYSTEM35 && library.games[3].engine == HUB_XSYSTEM4);
}

int main(void)
{
    test_library_view();
    const char *dirs[] = {"system3", "system3/games", "system3/games/ランス 1",
                          "xsystem35", "xsystem35/games", "xsystem35/games/ランス 1",
                          "xsystem4", "xsystem4/games", "xsystem4/games/爱丽丝学园",
                          "xsystem4/games/.partial"};
    for (size_t i = 0; i < sizeof(dirs) / sizeof(*dirs); ++i)
        assert(!mkdir(dirs[i], 0700));
    FILE *not_a_game = fopen("xsystem4/games/file", "wb");
    assert(not_a_game && !fclose(not_a_game));
    HubLibrary library = {0};
    assert(hub_library_scan(&library, ".") && library.count == 3);
    assert(strcmp(library.games[0].instance_id, library.games[1].instance_id));
    HubGame a = library.games[0];
    assert(hub_library_scan(&library, ".") && !strcmp(a.game_id, library.games[0].game_id));
    char path[HUB_PATH_SIZE];
    assert(hub_game_save_path(".", &a, path) && strstr(path, "/saves/ランス 1"));
    hub_library_free(&library);

    HubSettings settings;
    assert(hub_settings_load(".", &settings) && settings.sort == HUB_SORT_NAME);
    settings.sort = HUB_SORT_ENGINE;
    assert(hub_settings_save(".", &settings));
    assert(hub_settings_load(".", &settings) && settings.sort == HUB_SORT_ENGINE);
    assert(!rename("settings", "settings.bak"));
    assert(hub_settings_load(".", &settings) && settings.sort == HUB_SORT_ENGINE);
    FILE *future = fopen("settings", "wb");
    assert(future && fputs("alicehub-settings 9\nname\n", future) >= 0 && !fclose(future));
    assert(!hub_settings_load(".", &settings) && settings.sort == HUB_SORT_NAME);

    HubHandoff request = {.game = a}, read = {0};
    assert(hub_handoff_write(".", false, &request));
    assert(hub_handoff_take(".", false, &read) && !strcmp(read.game.instance_id, a.instance_id));
    assert(!hub_handoff_take(".", false, &read));
    strcpy(request.error, "Missing script\nline two");
    assert(!hub_handoff_write(".", false, &request));
    assert(hub_handoff_write(".", true, &request));
    assert(hub_handoff_take(".", true, &read) && !strcmp(read.error, "Missing script line two"));
    assert(!parse("alicehub-handoff 2\nsystem3\nvalid\n\n", &read));
    assert(!parse("alicehub-handoff 1\ninvalid\nvalid\n\n", &read));
    assert(!parse("alicehub-handoff 1\nsystem3\n../bad\n\n", &read));
    assert(!parse("alicehub-handoff 1\nsystem3\nsdmc:/other\n\n", &read));
    assert(!parse("alicehub-handoff 1\nsystem3\nbad\\path\n\n", &read));
    assert(!parse("alicehub-handoff 1\nsystem3\nvalid\n", &read));
    assert(!parse("alicehub-handoff 1\nsystem3\nvalid\n\ntrailing", &read));
}
""",
                text=True,
                check=True,
            )
            subprocess.run([str(Path(tmp) / "test")], cwd=tmp, check=True)

    def test_system4_engine_resolves_nested_launcher_paths_on_switch(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            game = root / "sdmc:/switch/Game"
            data = game / "Data"
            data.mkdir(parents=True)
            (data / "Patch.ain").touch()
            (data / "Font.ttf").touch()
            exe = root / "test"
            subprocess.run(
                ["cc", "-std=gnu11", "-D__SWITCH__", "-ffunction-sections", "-fdata-sections",
                 "-I", str(ENGINE / "include"), "-I", str(SYS4 / "include"),
                 str(ENGINE / "src/util.c"),
                 *(str(SYS4 / "src" / name) for name in ("file.c", "system.c", "utfsjis.c")),
                 "-Wl,--gc-sections", "-x", "c", "-", "-o", str(exe)],
                input=r"""
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "system4/file.h"
#include "xsystem4.h"

struct config config = {.game_dir = "sdmc:/switch/Game"};

int main(void)
{
    assert(!file_exists("sdmc:/switch/Game/./dAtA/PATCH.AIN"));
    assert(file_exists("sdmc:/switch/Game/Data/Patch.ain"));

    char *code = gamedir_path(".\\dAtA\\PATCH.AIN");
    assert(!strcmp(code, "sdmc:/switch/Game/Data/Patch.ain") && file_exists(code));
    free(code);
    char *font = gamedir_path_icase("sdmc:/SWITCH/Game/data/font.ttf");
    assert(font && !strcmp(font, "sdmc:/switch/Game/Data/Font.ttf"));
    free(font);
    assert(!gamedir_path_icase("Missing\\PATCH.AIN"));
    return 0;
}
""",
                text=True, check=True,
            )
            subprocess.run([str(exe)], cwd=root, check=True)

    def test_system4_boot_entries(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "test"
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(HUB),
                 str(HUB / "model.c"), "-x", "c", "-", "-o", str(exe)],
                input=r"""
#include "model.h"
#include <assert.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void put(const char *path, const char *content)
{
    FILE *out = fopen(path, "wb");
    assert(out && fputs(content, out) >= 0 && !fclose(out));
}

int main(void)
{
    assert(!mkdir("xsystem4", 0700));
    assert(!mkdir("xsystem4/games", 0700));
    assert(!chdir("xsystem4/games"));
    assert(!mkdir("Game", 0700));
    assert(!mkdir("Other", 0700));
    assert(!mkdir("Game/Data", 0700));
    put("Game/System40.ini", "CodeName = \"Original.ain\"\n");
    put("Game/Original.ain", "");
    put("Game/AliceStart.CHS", "CodeName = \"OYA.CHS\"\n");
    put("Game/Oya.chs", "");
    put("Game/ChineseLauncher.ini", "\xef\xbb\xbf\tCodeName = \".\\dAtA\\PATCH.AIN\"\n");
    put("Game/Data/Patch.ain", "");
    put("Game/Encoded.ini", "CodeName = \"\xd6\xd0\xce\xc4.ain\"\n");
    put("Game/中文.ain", "");
    put("Game/Unconfigured.chs", "");
    put("Game/Missing.ini", "CodeName = \"missing.ain\"\n");
    put("Outside.ain", "");
    put("Game/Outside.ini", "CodeName = \"../Outside.ain\"\n");
    assert(!symlink("..", "Game/Alias"));
    put("Game/Alias.ini", "CodeName = \"Alias/Outside.ain\"\n");
    put("Game/ChineseLauncher.exe", "");
    put("Game/system39.ini", "CodeName = \"Original.ain\"\n");
    put("Game/system39.ain", "");

    HubBootEntries entries;
    assert(hub_system4_boot_entries("Game", &entries) && entries.count == 4);
    assert(!strcmp(entries.names[0], "AliceStart.CHS"));
    assert(!strcmp(entries.names[1], "ChineseLauncher.ini"));
    assert(!strcmp(entries.names[2], "Encoded.ini"));
    assert(!strcmp(entries.names[3], "System40.ini"));
    hub_boot_entries_free(&entries);

    bool dsr;
    char target[HUB_PATH_SIZE], entry[HUB_NAME_SIZE];
    assert(hub_system4_resolve("Game", target, &dsr) && !dsr && !strcmp(target, "Game"));
    assert(hub_game_boot_set("Game", "AliceStart.CHS"));
    assert(hub_game_boot_get("Game", entry) && !strcmp(entry, "AliceStart.CHS"));
    assert(!hub_game_boot_get("Other", entry));
    assert(hub_system4_resolve("Game", target, &dsr) && !dsr && !strcmp(target, "Game/AliceStart.CHS"));
    assert(!unlink("Game/AliceStart.CHS"));
    assert(hub_system4_resolve("Game", target, &dsr) && !strcmp(target, "Game"));
    assert(hub_game_boot_set("Game", "ChineseLauncher.ini"));
    assert(hub_system4_resolve("Game", target, &dsr) && !strcmp(target, "Game/ChineseLauncher.ini"));
    assert(!hub_game_boot_set("Game", "../Outside.ain"));
    assert(hub_game_boot_set("Game", ""));
    assert(hub_system4_resolve("Game", target, &dsr) && !strcmp(target, "Game"));
    assert(!unlink("Game/System40.ini"));
    assert(hub_system4_resolve("Game", target, &dsr) && !strcmp(target, "Game/ChineseLauncher.ini"));

    put("Other/Original.ain", "");
    assert(hub_system4_boot_entries("Other", &entries) && !entries.count);
    assert(hub_system4_resolve("Other", target, &dsr) && !strcmp(target, "Other/Original.ain"));
    put("Other/CHS.TBL", "");
    put("Other/Alternative.chs", "");
    assert(hub_system4_boot_entries("Other", &entries) && entries.count == 2);
    assert(!strcmp(entries.names[0], "Alternative.chs"));
    hub_boot_entries_free(&entries);

    assert(!mkdir("Standard", 0700));
    put("Standard/System40.ini", "CodeName = \"Missing.ain\"\n");
    put("Standard/AliceStart2.ini", "CodeName = \"Missing.ain\"\nCodeName = \"Present.ain\"\n");
    put("Standard/AliceStart.ini", "CodeName = \"Present.ain\"\nCodeName = \"Missing.ain\"\n");
    put("Standard/Present.ain", "");
    assert(!hub_system4_configured_script("Standard", "AliceStart.ini", target));
    assert(hub_system4_configured_script("Standard", "AliceStart2.ini", target));
    assert(hub_system4_boot_entries("Standard", &entries) && entries.count == 1);
    assert(!strcmp(entries.names[0], "AliceStart2.ini"));
    hub_boot_entries_free(&entries);
    assert(hub_game_boot_set("Standard", "AliceStart2.ini"));
    assert(hub_system4_resolve("Standard", target, &dsr) && !strcmp(target, "Standard/AliceStart2.ini"));

    char long_dir[HUB_PATH_SIZE] = "Other";
    const char *component = "/01234567890123456789012345678901234567890123456789";
    while (strlen(long_dir) + strlen(component) < HUB_PATH_SIZE - 10) {
        strcat(long_dir, component);
        assert(!mkdir(long_dir, 0700));
    }
    char long_script[2 * HUB_PATH_SIZE];
    assert(snprintf(long_script, sizeof(long_script), "%s/%080d.ain", long_dir, 0) >= HUB_PATH_SIZE);
    put(long_script, "");
    assert(!hub_system4_resolve(long_dir, target, &dsr) && !*target);
    return 0;
}
""",
                text=True, check=True,
            )
            subprocess.run([str(exe)], cwd=tmp, check=True)

    def test_instance_copy_and_delete(self):
        hub = Path(__file__).resolve().parents[3] / "cmake/switch/hub"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "xsystem4/games/Rance10"
            nested = source / "patches"
            nested.mkdir(parents=True)
            (nested / "data.bin").write_bytes(b"\0AliceHub\xff")
            original_saves = root / "xsystem4/saves/Rance10"
            original_saves.mkdir(parents=True)
            (original_saves / "slot.rsm").write_text("original")
            retained = original_saves.with_name("Rance10-1")
            retained.mkdir()
            (retained / "slot.rsm").write_text("retained")
            sibling = root / "system3/games/Rance10-2"
            sibling.mkdir(parents=True)
            (sibling / "data.bin").write_text("different engine")
            (source / "external").symlink_to(sibling, target_is_directory=True)
            stale = root / "xsystem4/.hub-copy-1"
            stale.mkdir()
            (stale / "partial.bin").write_text("interrupted copy")
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(hub),
                 str(hub / "model.c"), str(hub / "instances.c"), str(hub / "fs_util.c"),
                 "-Wl,--wrap=rename",
                 "-x", "c", "-", "-o", str(root / "test")],
                input=r"""
#include "instances.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

static unsigned poll_count;
static bool force_fallback;
static unsigned rejected_renames;

int __real_rename(const char *source, const char *destination);

int __wrap_rename(const char *source, const char *destination)
{
    if (!force_fallback)
        return __real_rename(source, destination);
    ++rejected_renames;
    errno = ENOSYS;
    return -1;
}

static bool cancel_in_progress(void)
{
    return ++poll_count == 3;
}

int main(void)
{
    HubGame game, clone, other;
    char suggestion[HUB_NAME_SIZE], path[HUB_PATH_SIZE];
    assert(hub_game_init(&game, HUB_XSYSTEM4, "Rance10"));
    assert(hub_game_init(&clone, HUB_XSYSTEM4, "Rance10-2"));
    assert(hub_game_init(&other, HUB_SYSTEM3, "Rance10-2"));
    assert(hub_instance_suggest(".", &game, suggestion) && !strcmp(suggestion, clone.name));
    assert(hub_instance_copy(".", &game, "../escape", NULL) == HUB_INSTANCE_INVALID_NAME);
    assert(hub_instance_copy(".", &game, "wrong?", NULL) == HUB_INSTANCE_INVALID_NAME);
    assert(hub_instance_copy(".", &game, "rance10", NULL) == HUB_INSTANCE_NAME_TAKEN);
    assert(hub_instance_copy(".", &game, "rance10-1", NULL) == HUB_INSTANCE_NAME_TAKEN);
    assert(hub_instance_copy(".", &game, clone.name, NULL) == HUB_INSTANCE_COPY_FAILED);
    assert(hub_game_path(".", &clone, path) && access(path, F_OK));
    assert(!unlink("xsystem4/games/Rance10/external"));

    assert(hub_instance_copy(".", &game, clone.name, cancel_in_progress) == HUB_INSTANCE_COPY_FAILED);
    assert(poll_count >= 3 && access(path, F_OK));
    force_fallback = true;
    assert(hub_instance_copy(".", &game, clone.name, NULL) == HUB_INSTANCE_OK);
    assert(rejected_renames == 1);
    force_fallback = false;
    assert(!access(path, F_OK));
    FILE *copied = fopen("xsystem4/games/Rance10-2/patches/data.bin", "rb");
    unsigned char data[10];
    assert(copied && fread(data, 1, sizeof(data), copied) == sizeof(data));
    assert(data[0] == 0 && !memcmp(data + 1, "AliceHub", 8) && data[9] == 255 && !fclose(copied));
    assert(hub_game_save_path(".", &clone, path) && !access(path, F_OK));
    assert(access("xsystem4/saves/Rance10-2/slot.rsm", F_OK));
    assert(hub_instance_copy(".", &game, "Rance10-3", NULL) == HUB_INSTANCE_OK);

    HubLibrary library = {0};
    assert(hub_library_scan(&library, ".") && library.count == 4);
    assert(strcmp(clone.instance_id, game.instance_id));
    assert(strcmp(clone.instance_id, other.instance_id));
    hub_library_free(&library);

    FILE *save = fopen("xsystem4/saves/Rance10-2/slot.rsm", "wb");
    assert(save && fputs("copy save", save) >= 0 && !fclose(save));
    assert(!symlink("../../../system3/games/Rance10-2", "xsystem4/games/Rance10-2/external"));
    assert(hub_instance_delete(".", &clone, false) == HUB_INSTANCE_OK);
    assert(!access("xsystem4/saves/Rance10-2/slot.rsm", F_OK));
    assert(access("xsystem4/games/Rance10-2", F_OK));
    assert(hub_library_scan(&library, ".") && library.count == 3);
    hub_library_free(&library);
    assert(!access("system3/games/Rance10-2/data.bin", F_OK));
    assert(!access("xsystem4/games/Rance10-3/patches/data.bin", F_OK));
    assert(hub_instance_delete(".", &game, true) == HUB_INSTANCE_OK);
    assert(access("xsystem4/games/Rance10", F_OK));
    assert(access("xsystem4/saves/Rance10", F_OK));
    assert(!access("xsystem4/saves/Rance10-1/slot.rsm", F_OK));
    assert(!access("xsystem4/saves/Rance10-2/slot.rsm", F_OK));
}
""",
                text=True,
                check=True,
            )
            subprocess.run([str(root / "test")], cwd=root, check=True)
            self.assertEqual(list((root / "xsystem4").glob(".hub-copy-*")), [stale])
            self.assertEqual((stale / "partial.bin").read_text(), "interrupted copy")

    def test_save_transfers_and_pc_codec(self):
        repo = Path(__file__).resolve().parents[3]
        hub = repo / "cmake/switch/hub"
        engine = repo / "xsystem4"
        libsys4 = engine / "subprojects/libsys4"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            exe = root / "test"
            (root / "switch.h").write_text(
                "int fsdevCommitDevice(const char *device);\n#define R_FAILED(result) ((result) != 0)\n"
            )
            fs_util = root / "fs_util.o"
            subprocess.run(
                ["cc", "-std=gnu11", "-D__SWITCH__", "-I", str(root), "-c", str(hub / "fs_util.c"),
                 "-o", str(fs_util)], check=True,
            )
            subprocess.run(
                ["cc", "-std=gnu11", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                 "-Datomic_save_open=hub_codec_atomic_save_open",
                 "-Datomic_save_commit=hub_codec_atomic_save_commit",
                 "-Datomic_save_abort=hub_codec_atomic_save_abort",
                 "-Wl,--wrap=rename,--wrap=mkdir,--wrap=fopen",
                 "-I", str(hub), "-I", str(engine / "include"), "-I", str(libsys4 / "include"),
                 *(str(hub / name) for name in ("model.c", "save_store.c", "save_codec.c")),
                 str(fs_util),
                 str(engine / "src/savedata.c"),
                 *(str(libsys4 / "src" / name) for name in (
                     "ain.c", "buffer.c", "file.c", "hashtable.c", "instructions.c",
                     "mt19937int.c", "savefile.c", "string.c", "system.c", "utfsjis.c", "zlib.c",
                 )),
                 "-x", "c", "-", "-lz", "-o", str(exe)],
                input=r"""
#include "save_store.h"
#include "fs_util.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define BASE "save-manager/xsystem4/ランス"
#define LIVE "xsystem4/saves/ランス"

static bool deny_directory_renames, fail_live_mkdir, crash_live_copy;
static bool crash_snapshot_commit;

int fsdevCommitDevice(const char *device)
{
    assert(!strcmp(device, "sdmc"));
    if (crash_snapshot_commit) {
        assert(access("commit-test/.save-stage-0000/publishing", F_OK));
        assert(!unlink("commit-test/.save-stage-0000/previous/slot2"));
        _exit(43);
    }
    return 0;
}

FILE *__real_fopen(const char *path, const char *mode);
FILE *__wrap_fopen(const char *path, const char *mode)
{
    FILE *file = __real_fopen(path, mode);
    if (crash_live_copy && !strcmp(path, "./" LIVE "/main.asd") && !strcmp(mode, "wbx")) {
        assert(file && fputs("partial copy", file) >= 0 && !fclose(file));
        _exit(42);
    }
    return file;
}

int __real_rename(const char *source, const char *destination);
int __wrap_rename(const char *source, const char *destination)
{
    struct stat st;
    if (deny_directory_renames && !stat(source, &st) && S_ISDIR(st.st_mode)) {
        errno = EIO;
        return -1;
    }
    return __real_rename(source, destination);
}

int __real_mkdir(const char *path, mode_t mode);
int __wrap_mkdir(const char *path, mode_t mode)
{
    if (fail_live_mkdir && !strcmp(path, "./" LIVE)) {
        fail_live_mkdir = false;
        errno = ENOSPC;
        return -1;
    }
    return __real_mkdir(path, mode);
}

static void put(const char *path, const char *content)
{
    FILE *out = fopen(path, "wb");
    assert(out && fputs(content, out) >= 0 && !fclose(out));
}

static void expect(const char *path, const char *content)
{
    FILE *in = fopen(path, "rb");
    assert(in);
    char bytes[64];
    size_t n = fread(bytes, 1, sizeof(bytes), in);
    assert(n == strlen(content) && !memcmp(bytes, content, n) && !fclose(in));
}

int main(void)
{
    assert(!mkdir("commit-test", 0700) && !mkdir("commit-test/live", 0700));
    assert(!mkdir("commit-test/.save-stage-0000", 0700));
    assert(!mkdir("commit-test/.save-stage-0000/next", 0700));
    put("commit-test/live/slot1", "intact 1");
    put("commit-test/live/slot2", "intact 2");
    pid_t interrupted = fork();
    assert(interrupted >= 0);
    if (!interrupted) {
        bool pending;
        crash_snapshot_commit = true;
        hub_save_publish("commit-test/.save-stage-0000", "commit-test/live", true, &pending);
        _exit(99);
    }
    int commit_status;
    assert(waitpid(interrupted, &commit_status, 0) == interrupted && WIFEXITED(commit_status) &&
           WEXITSTATUS(commit_status) == 43);
    assert(hub_save_recover("commit-test/live"));
    expect("commit-test/live/slot1", "intact 1");
    expect("commit-test/live/slot2", "intact 2");
    assert(!unlink("commit-test/.save-stage-0000/intent"));
    assert(!unlink("commit-test/.save-stage-0000/previous/slot1"));
    assert(!rmdir("commit-test/.save-stage-0000/previous"));
    assert(!rmdir("commit-test/.save-stage-0000/next"));
    assert(!rmdir("commit-test/.save-stage-0000"));

    HubGame game;
    HubSaveResult result;
    atomic_bool cancelled = false;
    assert(hub_game_init(&game, HUB_XSYSTEM4, "ランス"));
    assert(hub_save_run(".", &game, HUB_SAVE_PREPARE, &cancelled, &result));
    assert(!mkdir(BASE "/Backups", 0700));
    assert(!mkdir(BASE "/Backups/.save-stage-0000", 0700));
    put(BASE "/Backups/.save-stage-0000/previous", "interrupted stage");
    assert(!mkdir(BASE "/Import/SaveData", 0700));
    put(BASE "/Import/SaveData/main.asd", "initial");
    put(BASE "/Import/SaveData/.hidden", "not imported");
    assert(hub_save_run(".", &game, HUB_SAVE_IMPORT, &cancelled, &result) && result.copied == 1);
    expect(LIVE "/main.asd", "initial");
    assert(access(LIVE "/.hidden", F_OK));

    deny_directory_renames = true;
    put(LIVE "/.internal", "hidden data");
    assert(hub_save_run(".", &game, HUB_SAVE_BACKUP, &cancelled, &result) && result.copied == 2);
    expect(BASE "/Backups/.save-stage-0000/previous", "interrupted stage");
    assert(!unlink(BASE "/Backups/.save-stage-0000/previous"));
    assert(!rmdir(BASE "/Backups/.save-stage-0000"));
    assert(!mkdir(BASE "/Backups/.save-stage-0001", 0700));
    put(BASE "/Backups/.save-stage-0001/publishing", "0SaveData-99990101-000000-0000");
    assert(!mkdir(BASE "/Backups/SaveData-99990101-000000-0000", 0700));
    put(BASE "/Backups/SaveData-99990101-000000-0000/main.asd", "partial backup");
    assert(hub_save_run(".", &game, HUB_SAVE_RESTORE, &cancelled, &result));
    expect(LIVE "/main.asd", "initial");
    assert(access(BASE "/Backups/SaveData-99990101-000000-0000", F_OK));
    put(BASE "/Import/SaveData/main.asd", "changed");
    put(BASE "/Import/SaveData/extra.asd", "extra");
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        crash_live_copy = true;
        hub_save_run(".", &game, HUB_SAVE_IMPORT, &cancelled, &result);
        _exit(99);
    }
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 42);
    expect(LIVE "/main.asd", "partial copy");
    assert(hub_save_recover("./" LIVE));
    expect(LIVE "/main.asd", "initial");
    expect(LIVE "/.internal", "hidden data");
    assert(access(LIVE "/extra.asd", F_OK));
    assert(hub_save_run(".", &game, HUB_SAVE_IMPORT, &cancelled, &result) && result.copied == 2);
    expect(LIVE "/main.asd", "changed");
    assert(hub_save_run(".", &game, HUB_SAVE_RESTORE, &cancelled, &result) && result.copied == 2);
    expect(LIVE "/main.asd", "initial");
    expect(LIVE "/.internal", "hidden data");
    assert(access(LIVE "/extra.asd", F_OK));

    put(LIVE "/main.asd", "keep on rollback");
    fail_live_mkdir = true;
    assert(!hub_save_run(".", &game, HUB_SAVE_RESTORE, &cancelled, &result));
    assert(!fail_live_mkdir);
    expect(LIVE "/main.asd", "keep on rollback");
    expect(LIVE "/.internal", "hidden data");
    assert(access(LIVE "/extra.asd", F_OK));
    put(LIVE "/main.asd", "initial");

    put(BASE "/Sync/main.asd", "external");
    put(BASE "/Sync/external.asd", "added");
    put(LIVE "/local.asd", "local");
    assert(hub_save_run(".", &game, HUB_SAVE_SYNC, &cancelled, &result));
    assert(result.copied == 2 && result.conflicts == 1);
    expect(LIVE "/external.asd", "added");
    expect(LIVE "/main.asd", "initial");
    expect(BASE "/Sync/main.asd", "external");
    expect(BASE "/Sync/local.asd", "local");
    assert(hub_save_run(".", &game, HUB_SAVE_PULL, &cancelled, &result));
    expect(LIVE "/main.asd", "external");
    put(LIVE "/main.asd", "local edit");
    assert(hub_save_run(".", &game, HUB_SAVE_PUSH, &cancelled, &result));
    expect(BASE "/Sync/main.asd", "local edit");

    put(LIVE "/ignored.bak", "backup");
    assert(!rename("fixture.asd", LIVE "/struct.asd"));
    assert(hub_save_run(".", &game, HUB_SAVE_EXPORT, &cancelled, &result));
    expect(BASE "/SaveData/ignored.bak", "backup");
    assert(hub_save_run(".", &game, HUB_SAVE_EXPORT_PC, &cancelled, &result));
    assert(result.converted == 1);
    expect(BASE "/PC_SaveData/main.asd", "local edit");
    assert(access(BASE "/PC_SaveData/ignored.bak", F_OK));

    put(LIVE "/main.asd", "not exported");
    assert(!rename("broken.asd", LIVE "/broken.asd"));
    assert(!hub_save_run(".", &game, HUB_SAVE_EXPORT_PC, &cancelled, &result));
    assert(strstr(result.error, "Could not convert broken.asd"));
    expect(BASE "/PC_SaveData/main.asd", "local edit");
    assert(!symlink("main.asd", BASE "/Import/SaveData/link.asd"));
    assert(!hub_save_run(".", &game, HUB_SAVE_IMPORT, &cancelled, &result));
    expect(LIVE "/main.asd", "not exported");
    atomic_store(&cancelled, true);
    assert(!hub_save_run(".", &game, HUB_SAVE_RESTORE, &cancelled, &result));
    assert(!strcmp(result.error, "Cancelled."));
    expect(LIVE "/main.asd", "not exported");
    atomic_store(&cancelled, false);

    char component[181], nested[600] = LIVE, exported[700];
    memset(component, 's', sizeof(component) - 1);
    component[sizeof(component) - 1] = 0;
    for (int i = 0; i < 3; ++i) {
        strcat(nested, "/");
        strcat(nested, component);
        assert(!mkdir(nested, 0700));
    }
    strcat(nested, "/deep.asd");
    put(nested, "long path");
    assert(hub_save_run(".", &game, HUB_SAVE_EXPORT, &cancelled, &result));
    snprintf(exported, sizeof(exported), BASE "/SaveData%s", nested + strlen(LIVE));
    expect(exported, "long path");
}
""",
                text=True,
                check=True,
            )
            magic = b"X4SS\x01\0\0\0"
            comment = b"Save comment"
            raw = (struct.pack("<III", 1, 1, 1) + b"\x01\x04\0\0\0Hero" +
                   b"\x01\0\0\0\x02\0\0\0HP\x01" + struct.pack("<I", 42))
            compressed = zlib.compress(raw)
            (root / "fixture.asd").write_bytes(
                magic + struct.pack("<I", len(comment)) + comment +
                struct.pack("<II", len(raw), len(compressed)) + compressed
            )
            (root / "broken.asd").write_bytes(magic)
            subprocess.run([str(exe)], cwd=root, check=True)
            export_dir = root / "save-manager/xsystem4/ランス"
            pc = (export_dir / "PC_SaveData/struct.asd").read_bytes()
            self.assertEqual(pc[:4], b"GD\x01\x01")
            unpacked = zlib.decompress(pc[8:])
            self.assertEqual(len(unpacked), struct.unpack("<I", pc[4:8])[0])
            self.assertTrue(unpacked.startswith(b"serialize_struct\0"))
            self.assertTrue(unpacked.endswith(comment))
            self.assertIn(b"Hero\0", unpacked)
            self.assertEqual((export_dir / "SaveData/struct.asd").read_bytes()[:8], magic)
            self.assertFalse(any(root.rglob(".save-stage-*")))

    def test_daisengoku_save_dir_isolation(self):
        repo = Path(__file__).resolve().parents[3]
        hub = repo / "cmake/switch/hub"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            exe = root / "test"
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(hub), str(hub / "model.c"), str(hub / "instances.c"), str(hub / "fs_util.c"),
                 "-x", "c", "-", "-o", str(exe)],
                input=r"""
#include "instances.h"
#include "model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void put(const char *path, const char *content)
{
    FILE *out = fopen(path, "wb");
    assert(out && fputs(content, out) >= 0 && !fclose(out));
}

static void expect(const char *path, const char *expected)
{
    char buf[128] = {0};
    FILE *in = fopen(path, "rb");
    assert(in);
    size_t n = fread(buf, 1, sizeof(buf) - 1, in);
    fclose(in);
    buf[n] = 0;
    assert(!strcmp(buf, expected));
}

int main(void)
{
    assert(!mkdir("xsystem4", 0700));
    assert(!mkdir("xsystem4/games", 0700));
    assert(!mkdir("xsystem4/games/SengokuRance", 0700));
    assert(!mkdir("xsystem4/saves", 0700));

    HubGame game;
    assert(hub_game_init(&game, HUB_XSYSTEM4, "SengokuRance"));

    put("xsystem4/games/SengokuRance/System40.ini",
        "GameName = \"战国兰斯中文版\"\nCodeName = \"AliceCN.ain\"\nSaveFolder = \"SaveData\"\n");
    assert(!hub_game_uses_daisengoku_save_folder("xsystem4/games/SengokuRance"));
    char vanilla_save[HUB_PATH_SIZE];
    assert(hub_game_save_path(".", &game, vanilla_save));
    assert(!strcmp(vanilla_save, "./xsystem4/saves/SengokuRance"));
    assert(!mkdir(vanilla_save, 0700));
    put("./xsystem4/saves/SengokuRance/Save001.asd", "vanilla-save");

    put("xsystem4/games/SengokuRance/System40.ini",
        "GameName = \"大战国兰斯 CN Ver.\"\nCodeName = \"DaiSengoku.ain\"\nSaveFolder = \"SaveDataDSR\"\n");
    assert(hub_game_uses_daisengoku_save_folder("xsystem4/games/SengokuRance"));
    char dsr_save[HUB_PATH_SIZE];
    assert(hub_game_save_path(".", &game, dsr_save));
    assert(!strcmp(dsr_save, "./xsystem4/saves/SengokuRance__SaveDataDSR"));
    assert(!mkdir(dsr_save, 0700));
    put("./xsystem4/saves/SengokuRance__SaveDataDSR/Save001.asd", "dsr-save");
    expect("./xsystem4/saves/SengokuRance/Save001.asd", "vanilla-save");

    // Retained DaiSengoku saves under a name block creating a fresh copy with that same name
    assert(!mkdir("./xsystem4/saves/Challenge__SaveDataDSR", 0700));
    put("./xsystem4/saves/Challenge__SaveDataDSR/Save001.asd", "retained-dsr");
    assert(hub_instance_copy(".", &game, "Challenge", NULL) == HUB_INSTANCE_NAME_TAKEN);
    assert(hub_instance_copy(".", &game, "FreshDSR", NULL) == HUB_INSTANCE_OK);
    struct stat st;
    assert(!stat("./xsystem4/saves/FreshDSR__SaveDataDSR", &st) && S_ISDIR(st.st_mode));
    expect("./xsystem4/saves/Challenge__SaveDataDSR/Save001.asd", "retained-dsr");

    const char *dir = "xsystem4/games/SengokuRance";
    put("xsystem4/games/SengokuRance/DaiSengoku.ain", "");
    put("xsystem4/games/SengokuRance/AliceCN.ain", "");
    put("xsystem4/games/SengokuRance/Unrecognized.ain", "");
    HubBootEntries entries;
    assert(hub_system4_boot_entries(dir, &entries) && entries.count == 2);
    assert(!strcmp(entries.names[0], "System40.ini") && !strcmp(entries.names[1], "AliceCN.ain"));
    hub_boot_entries_free(&entries);
    char target[HUB_PATH_SIZE], entry[HUB_NAME_SIZE], selected_save[HUB_PATH_SIZE];
    bool selected_dsr;
    assert(hub_game_boot_set(dir, "System40.ini"));
    assert(hub_system4_resolve(dir, target, &selected_dsr) && selected_dsr);
    assert(!strcmp(target, "xsystem4/games/SengokuRance/System40.ini"));
    assert(hub_game_boot_set(dir, "AliceCN.ain"));
    assert(hub_game_save_path(".", &game, selected_save) && !strcmp(selected_save, vanilla_save));
    assert(hub_system4_resolve(dir, target, &selected_dsr) && !selected_dsr);
    assert(!strcmp(target, "xsystem4/games/SengokuRance/AliceCN.ain"));
    assert(hub_instance_copy(".", &game, "Independent", NULL) == HUB_INSTANCE_OK);
    assert(!hub_game_boot_get("xsystem4/games/Independent", entry));
    assert(hub_game_boot_set(dir, "Unrecognized.ain"));
    assert(hub_game_save_path(".", &game, selected_save) && !strcmp(selected_save, dsr_save));

    put("xsystem4/games/SengokuRance/System40.ini",
        "GameName = \"战国兰斯中文版\"\nCodeName = \"AliceCN.ain\"\nSaveFolder = \"SaveData\"\n");
    char restored_save[HUB_PATH_SIZE];
    assert(hub_game_save_path(".", &game, restored_save));
    assert(!strcmp(restored_save, vanilla_save));
    expect("./xsystem4/saves/SengokuRance/Save001.asd", "vanilla-save");
    expect("./xsystem4/saves/SengokuRance__SaveDataDSR/Save001.asd", "dsr-save");

    put("xsystem4/games/SengokuRance/Mod.ini",
        "CodeName = \"AliceCN.ain\"\nSaveFolder = \"SaveData\"\nSaveFolder = \"../ModSaveData\"\n");
    put("xsystem4/games/SengokuRance/OtherMod.ini",
        "CodeName = \"AliceCN.ain\"\nSaveFolder = \"OtherModSaveData\"\n");
    put("xsystem4/games/SengokuRance/DSR.ini",
        "CodeName = \"DaiSengoku.ain\"\nSaveFolder = \"SaveDataDSR\"\n");
    assert(hub_game_boot_set(dir, "Mod.ini"));
    char mod_save[HUB_PATH_SIZE], manager[HUB_PATH_SIZE], key[HUB_PATH_SIZE];
    assert(hub_game_save_path(".", &game, mod_save));
    assert(strstr(mod_save, "/saves/.launch-entries/SengokuRance/") && !strstr(mod_save, "../"));
    assert(hub_system4_resolve(dir, target, &selected_dsr));
    assert(hub_system4_save_key(game.name, dir, target, key));
    assert(!strcmp(mod_save + strlen("./xsystem4/saves/"), key));
    assert(hub_game_save_manager_path(".", &game, manager));
    assert(!strcmp(manager + strlen("./save-manager/xsystem4/"), key));
    assert(!mkdir("./xsystem4/saves/.launch-entries", 0700));
    assert(!mkdir("./xsystem4/saves/.launch-entries/SengokuRance", 0700));
    assert(!mkdir(mod_save, 0700));
    assert(hub_game_boot_set(dir, "OtherMod.ini"));
    assert(hub_game_save_path(".", &game, selected_save) && strcmp(selected_save, mod_save));
    assert(!mkdir(selected_save, 0700));
    assert(!unlink("xsystem4/games/SengokuRance/Mod.ini"));

    assert(!mkdir("xsystem4/games/Independent__SaveDataDSR", 0700));
    HubGame independent;
    assert(hub_game_init(&independent, HUB_XSYSTEM4, "Independent"));
    assert(hub_instance_delete(".", &independent, true) == HUB_INSTANCE_OK);
    assert(!access("xsystem4/saves/Independent__SaveDataDSR", F_OK));
    assert(hub_instance_delete(".", &game, true) == HUB_INSTANCE_OK);
    assert(access(vanilla_save, F_OK) && access(dsr_save, F_OK));
    assert(access(mod_save, F_OK) && access(selected_save, F_OK));
    assert(!access("xsystem4/saves/Challenge__SaveDataDSR/Save001.asd", F_OK));
    return 0;
}
""",
                text=True,
                check=True,
            )
            subprocess.run([str(exe)], cwd=root, check=True)


class SwitchModsCatalogParityTest(unittest.TestCase):
    def test_uncensored_catalog_parity_and_spinoff_exclusions(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            exe = work / "test_catalog"
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(HUB), "-I", str(ENGINE / "include"),
                 str(HUB / "mods_catalog.c"),
                 str(HUB / "install_transaction.c"),
                 str(HUB / "fs_util.c"),
                 str(HUB / "model.c"),
                 str(ENGINE / "src/cJSON.c"),
                 "-x", "c", "-", "-o", str(exe)],
                input=r"""
#include "mods.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void make_game(const char *dir, const char *file)
{
    assert(!mkdir(dir, 0700));
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    FILE *f = fopen(path, "wb");
    assert(f && fputs("x", f) >= 0 && !fclose(f));
}

static void add_file(const char *dir, const char *file)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    FILE *f = fopen(path, "wb");
    assert(f && fputs("x", f) >= 0 && !fclose(f));
}

int main(void)
{
    // Dohna Dohna (ain and CG archive markers)
    make_game("dohna_ain", "dohnadohna.ain");
    const ModsGame *g = mods_game("dohna_ain", HUB_XSYSTEM4);
    assert(g && !strcmp(g->id, "dohnadohna") && g->kind == MODS_UNCENSORED && mods_game_has_uncensored(g));

    make_game("dohna_cg", "DohnaDohnaCG.afa");
    g = mods_game("dohna_cg", HUB_XSYSTEM4);
    assert(g && !strcmp(g->id, "dohnadohna") && g->kind == MODS_UNCENSORED && mods_game_has_uncensored(g));

    // Haruka (Japanese, CN, English, and mojibake markers)
    make_game("haruka_jp", "超昂閃忍ハルカ.ain");
    g = mods_game("haruka_jp", HUB_XSYSTEM4);
    assert(g && !strcmp(g->id, "haruka") && g->kind == MODS_UNCENSORED && mods_game_has_uncensored(g));

    make_game("haruka_en", "haruka.ain");
    g = mods_game("haruka_en", HUB_XSYSTEM4);
    assert(g && !strcmp(g->id, "haruka") && g->kind == MODS_UNCENSORED && mods_game_has_uncensored(g));

    make_game("haruka_bb", "BeatBladesHaruka.ain");
    g = mods_game("haruka_bb", HUB_XSYSTEM4);
    assert(g && !strcmp(g->id, "haruka") && g->kind == MODS_UNCENSORED && mods_game_has_uncensored(g));

    make_game("haruka_cs", "ChoukouSenninHaruka.ain");
    g = mods_game("haruka_cs", HUB_XSYSTEM4);
    assert(g && !strcmp(g->id, "haruka") && g->kind == MODS_UNCENSORED && mods_game_has_uncensored(g));

    make_game("haruka_cn", "超昂闪忍遥GA.ald");
    g = mods_game("haruka_cn", HUB_XSYSTEM4);
    assert(g && !strcmp(g->id, "haruka") && g->kind == MODS_UNCENSORED && mods_game_has_uncensored(g));

    // Evenicle supports BOTH translation AND uncensored; Evenicle 2 supports translation only.
    make_game("evenicle", "Evenicle.ain");
    g = mods_game("evenicle", HUB_XSYSTEM4);
    assert(g && !strcmp(g->id, "evenicle") && g->kind == MODS_TRANSLATION && mods_game_has_uncensored(g));

    make_game("evenicle2", "Evenicle2.ain");
    g = mods_game("evenicle2", HUB_XSYSTEM4);
    assert(g && !strcmp(g->id, "evenicle2") && g->kind == MODS_TRANSLATION && !mods_game_has_uncensored(g));

    // Spin-off exclusions must never match even when a base CG archive is also present.
    const char *excluded[] = {
        "Evenicle2Rance.ain",
        "EvenicleRance.ain",
        "EvenicleGaiden.ain",
        "EvenicleGaiten.ain",
        "EvenicleAPC.ain",
        "ハルカvs.ain",
        "疾風!?忍者大作戦.ain",
    };
    for (size_t i = 0; i < sizeof(excluded) / sizeof(*excluded); ++i) {
        char dir[64];
        snprintf(dir, sizeof(dir), "excl_%zu", i);
        make_game(dir, excluded[i]);
        add_file(dir, "Evenicle.ain");
        add_file(dir, "haruka.ain");
        assert(!mods_game(dir, HUB_XSYSTEM4));
    }

    // Verify manifest parsing accepts dohnadohna, haruka, and evenicle uncensored manifests.
    const char *sha = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const char *ids[] = {"dohnadohna", "haruka", "evenicle"};
    for (size_t i = 0; i < sizeof(ids) / sizeof(*ids); ++i) {
        ModsGame cg = {.id = ids[i], .kind = MODS_UNCENSORED};
        char json[512];
        snprintf(json, sizeof(json),
            "{\"gameId\":\"%s\",\"size\":128,\"key\":\"uncensored/AliceHubUncensoredCG.afa\","
            "\"fileName\":\"AliceHubUncensoredCG.afa\",\"sha256\":\"%s\"}",
            ids[i], sha);
        ModsCatalog cat = {0};
        assert(mods_catalog_parse(&cat, &cg, json) && cat.count == 1);
        free(cat.rows);
    }
    return 0;
}
""",
                text=True,
                check=True,
            )
            subprocess.run([str(exe)], cwd=work, check=True)
