"""Wave-2 stability, concurrency, memory safety, and security regression tests for Switch Hub/Launcher and iOS App."""
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile
import unittest
import zipfile

REPO = Path(__file__).resolve().parents[3]
SWITCH = REPO / "cmake/switch"
HUB = SWITCH / "hub"
IOS_APP = REPO / "ios/App"


def _sanitizers_supported():
    with tempfile.TemporaryDirectory() as tmp:
        probe = subprocess.run(
            ["cc", "-std=gnu11", "-fsanitize=address,undefined", "-x", "c", "-", "-o", str(Path(tmp) / "probe")],
            input="int main(void) { return 0; }\n",
            text=True, capture_output=True,
        )
        return probe.returncode == 0


HAS_SANITIZERS = _sanitizers_supported()


def _zlib_flags():
    devkitpro = os.environ.get("DEVKITPRO")
    if devkitpro and not Path("/usr/include/zlib.h").exists():
        headers = Path(devkitpro) / "portlibs/switch/include"
        return ["-idirafter", str(headers), "-l:libz.so.1"]
    return ["-lz"]


def _extract_c_function(path: Path, name: str) -> str:
    text = path.read_text(encoding="utf-8")
    m = re.search(rf"^[ \t]*[^\n]*\b{re.escape(name)}\s*\([^\n]*\n\{{", text, re.MULTILINE)
    if not m:
        raise AssertionError(f"Could not find function {name} in {path}")
    start = m.start()
    idx = text.index("{", m.start())
    depth = 0
    for pos in range(idx, len(text)):
        ch = text[pos]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[start:pos + 1]
    raise AssertionError(f"Unclosed function {name} in {path}")


class Wave2SwitchCRegressionsTest(unittest.TestCase):
    def compile_and_run(self, code, sources=(), includes=(), extra=(), libs=(), cwd=None):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            exe = work / "test_bin"
            san = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if HAS_SANITIZERS else []
            cmd = [
                "cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-g",
                *san,
                *(arg for inc in includes for arg in ("-I", str(inc))),
                *(str(s) for s in sources),
                *extra,
                "-x", "c", "-",
                *libs,
                "-o", str(exe),
            ]
            compile_res = subprocess.run(cmd, input=code, text=True, capture_output=True)
            self.assertEqual(compile_res.returncode, 0, f"Compile failed:\n{compile_res.stderr}")
            env = dict(os.environ)
            if HAS_SANITIZERS:
                env["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
                env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
            run_res = subprocess.run([str(exe)], cwd=cwd or work, env=env, text=True, capture_output=True)
            self.assertEqual(run_res.returncode, 0, f"Run failed:\nstdout:\n{run_res.stdout}\nstderr:\n{run_res.stderr}")
            return run_res

    def test_switch_install_transaction_eexist_recovery_and_copy_cleanup(self):
        """[S1-2, S1-5] commit_new EEXIST must unlink publishing marker immediately and preserve existing game;
        startup recovery must also clean orphaned .hub-copy-* staging dirs."""
        self.compile_and_run(
            r"""
#include "install_transaction.h"
#include "instances.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static bool race_create_target;

FILE *__real_fopen(const char *, const char *);
FILE *__wrap_fopen(const char *path, const char *mode)
{
    FILE *fp = __real_fopen(path, mode);
    if (fp && race_create_target && strstr(path, "/publishing") && !strcmp(mode, "wbx")) {
        race_create_target = false;
        /* Simulate another writer creating target right after publishing marker is opened */
        assert(!mkdir("./xsystem4/games/Alice", 0700));
        FILE *f = __real_fopen("./xsystem4/games/Alice/existing.dat", "wb");
        assert(f && fputs("PREEXISTING", f) >= 0 && !fclose(f));
    }
    return fp;
}

static bool always_ok(const char *stage, void *ctx)
{
    (void)stage; (void)ctx;
    return true;
}

int main(void)
{
    HubGame game;
    assert(hub_game_init(&game, HUB_XSYSTEM4, "Alice"));

    /* 1. Verify orphaned .hub-copy-* cleanup by hub_instance_recover and hub_install_recover */
    assert(!mkdir("xsystem4", 0700));
    assert(!mkdir("xsystem4/.hub-copy-1", 0700));
    FILE *orphan = __real_fopen("xsystem4/.hub-copy-1/junk.bin", "wb");
    assert(orphan && fputs("x", orphan) >= 0 && !fclose(orphan));
    assert(!mkdir("xsystem4/.hub-copy-42", 0700));
    assert(!mkdir("xsystem4/.keep-dir", 0700));
    assert(hub_instance_recover("."));
    assert(access("xsystem4/.hub-copy-1", F_OK) != 0 && errno == ENOENT);
    assert(access("xsystem4/.hub-copy-42", F_OK) != 0 && errno == ENOENT);
    assert(access("xsystem4/.keep-dir", F_OK) == 0);

    assert(!mkdir("xsystem4/.hub-copy-7", 0700));
    assert(hub_install_recover("."));
    assert(access("xsystem4/.hub-copy-7", F_OK) != 0 && errno == ENOENT);
    assert(access("xsystem4/.keep-dir", F_OK) == 0);

    /* 2. Concurrent EEXIST during commit_new: fork so we simulate crash right after failed commit_new */
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        HubInstallTransaction *tx = hub_install_begin(".", &game);
        assert(tx);
        char staged[HUB_PATH_SIZE];
        assert(hub_install_stage_file(tx, "new.dat", staged));
        FILE *sf = __real_fopen(staged, "wb");
        assert(sf && fputs("NEW", sf) >= 0 && !fclose(sf));
        assert(hub_install_verify(tx, always_ok, NULL));
        race_create_target = true;
        bool ok = hub_install_commit_new(tx);
        assert(!ok && errno == EEXIST);
        /* publishing marker MUST already be gone before returning false */
        assert(access("./xsystem4/.install-v1/Alice/publishing", F_OK) != 0 && errno == ENOENT);
        /* Simulate abrupt crash without calling hub_install_rollback */
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);

    /* Next startup recovery MUST preserve the pre-existing game at target! */
    assert(hub_install_recover("."));
    assert(access("./xsystem4/.install-v1/Alice", F_OK) != 0 && errno == ENOENT);
    FILE *existing = __real_fopen("./xsystem4/games/Alice/existing.dat", "rb");
    assert(existing);
    char buf[32] = {0};
    assert(fread(buf, 1, sizeof(buf) - 1, existing) == 11 && !fclose(existing));
    assert(!strcmp(buf, "PREEXISTING"));
    return 0;
}
""",
            sources=[HUB / "model.c", HUB / "fs_util.c", HUB / "install_transaction.c", HUB / "instances.c"],
            includes=[HUB],
            libs=["-Wl,--wrap=fopen"],
        )

    def test_switch_instance_suggest_non_utf8_no_underflow(self):
        """[S1-4] hub_instance_suggest must not underflow len when >60 bytes are UTF-8 continuation bytes."""
        self.compile_and_run(
            r"""
#include "instances.h"
#include <assert.h>
#include <string.h>
#include <sys/stat.h>

int main(void)
{
    assert(!mkdir("xsystem4", 0700));
    assert(!mkdir("xsystem4/games", 0700));
    assert(!mkdir("xsystem4/saves", 0700));

    HubGame src = {0};
    src.engine = HUB_XSYSTEM4;
    /* 70 bytes of 0x80 (all look like UTF-8 continuation bytes) */
    memset(src.name, 0x80, 70);
    src.name[70] = '\0';

    char suggestion[HUB_NAME_SIZE] = {0};
    assert(hub_instance_suggest(".", &src, suggestion));
    assert(!strcmp(suggestion, "Copy-1"));

    assert(!mkdir("xsystem4/games/Copy-1", 0700));
    assert(hub_instance_suggest(".", &src, suggestion));
    assert(!strcmp(suggestion, "Copy-2"));

    /* Valid >60 byte multi-byte UTF-8 still truncates cleanly on codepoint boundary */
    HubGame utf8 = {0};
    utf8.engine = HUB_XSYSTEM4;
    for (int i = 0; i < 22; ++i)
        memcpy(utf8.name + i * 3, "\xe6\xbc\xa2", 3); /* '漢' x 22 = 66 bytes */
    utf8.name[66] = '\0';
    assert(hub_instance_suggest(".", &utf8, suggestion));
    assert(strlen(suggestion) == 62); /* 60 bytes (20 chars) + "-1" */
    assert(!strcmp(suggestion + 60, "-1"));
    return 0;
}
""",
            sources=[HUB / "model.c", HUB / "fs_util.c", HUB / "instances.c"],
            includes=[HUB],
        )

    def test_switch_fs_util_depth_cap_and_trailing_slash(self):
        """[S1-6] fs_util.c must reject trailing-slash descendant copy with EINVAL and deep recursion with ELOOP."""
        self.compile_and_run(
            r"""
#include "fs_util.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    assert(!mkdir("src", 0700));
    FILE *f = fopen("src/a.txt", "wb");
    assert(f && fputs("hi", f) >= 0 && !fclose(f));

    /* Trailing slash must not bypass descendant check */
    errno = 0;
    assert(hub_copy_dir("src/", "src/sub") == -1 && errno == EINVAL);
    errno = 0;
    assert(hub_copy_dir("src///", "src/sub") == -1 && errno == EINVAL);
    assert(access("src/sub", F_OK) != 0 && errno == ENOENT);

    /* Normal sibling copy works */
    assert(hub_copy_dir("src/", "dst") == 0);
    assert(access("dst/a.txt", F_OK) == 0);

    /* Deep recursion (>64 levels) fails cleanly with ELOOP */
    char cur[512] = "deep";
    assert(!mkdir(cur, 0700));
    for (int i = 0; i < 68; ++i) {
        strcat(cur, "/a");
        assert(!mkdir(cur, 0700));
    }
    errno = 0;
    assert(hub_copy_dir("deep", "deep_copy") == -1 && errno == ELOOP);
    assert(access("deep_copy", F_OK) != 0 && errno == ENOENT);
    return 0;
}
""",
            sources=[HUB / "model.c", HUB / "fs_util.c"],
            includes=[HUB],
        )

    def test_switch_save_storage_depth_cap_lstat_and_edit_backup_cleanup(self):
        """[S1-6, S1-11] save_storage.c must use lstat, enforce depth cap, and clear stale edit backups
        on unlock install and restore."""
        self.compile_and_run(
            r"""
#include "save_storage.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void put(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");
    assert(fp && fputs(text, fp) >= 0 && !fclose(fp));
}

int main(void)
{
    /* 1. Symlink loop under save dir must not infinitely recurse (lstat skips symlink) */
    assert(!mkdir("saves_loop", 0700));
    put("saves_loop/LocalSave1.asd", "slot1");
    assert(!symlink("../saves_loop", "saves_loop/self"));
    HubSaveSlots slots;
    assert(hub_save_scan("saves_loop", &slots));
    assert(slots.count == 1 && !strcmp(slots.paths[0], "LocalSave1.asd"));
    free(slots.paths);

    /* 2. Real directory chain deeper than 64 fails cleanly with ELOOP */
    char deep[512] = "saves_deep";
    assert(!mkdir(deep, 0700));
    for (int i = 0; i < 68; ++i) {
        strcat(deep, "/d");
        assert(!mkdir(deep, 0700));
    }
    errno = 0;
    assert(!hub_save_scan("saves_deep", &slots) && errno == ELOOP);
    assert(slots.paths == NULL && slots.count == 0);

    /* 3. S1-11: installing or restoring unlock clears stale edit backups on affected global targets */
    assert(!mkdir("assets", 0700));
    put("assets/GlobalSave.asd", "UNLOCK_G");
    put("assets/Collection.asd", "UNLOCK_C");
    put("assets/AFCGMode.asd", "UNLOCK_CG");
    put("assets/AFBGMMode.asd", "UNLOCK_BGM");

    assert(!mkdir("root", 0700));
    assert(!mkdir("root/game", 0700));
    assert(!mkdir("root/game/SaveData", 0700));
    put("root/game/GlobalSave.asd", "ORIG_G");
    put("root/game/SaveData/Collection.asd", "ORIG_C");
    /* Pre-existing stale edit backups */
    put("root/game/GlobalSave.asd.hub-edit.bak", "STALE_EDIT_G");
    put("root/game/Collection.asd.hub-edit.absent", "");
    put("root/game/SaveData/Collection.asd.hub-edit.bak", "STALE_EDIT_C");
    assert(hub_save_has_backup("root/game/GlobalSave.asd", HUB_SAVE_EDIT_BACKUP));
    assert(hub_save_has_backup("root/game/Collection.asd", HUB_SAVE_EDIT_BACKUP));
    assert(hub_save_has_backup("root/game/SaveData/Collection.asd", HUB_SAVE_EDIT_BACKUP));

    assert(hub_save_install_unlock("root/game", "assets"));
    assert(!hub_save_has_backup("root/game/GlobalSave.asd", HUB_SAVE_EDIT_BACKUP));
    assert(!hub_save_has_backup("root/game/Collection.asd", HUB_SAVE_EDIT_BACKUP));
    assert(!hub_save_has_backup("root/game/SaveData/Collection.asd", HUB_SAVE_EDIT_BACKUP));

    /* Simulate post-unlock offline edit creating new edit backups */
    put("root/game/GlobalSave.asd.hub-edit.bak", "POST_UNLOCK_EDIT_G");
    put("root/game/SaveData/Collection.asd.hub-edit.absent", "");
    assert(hub_save_has_backup("root/game/GlobalSave.asd", HUB_SAVE_EDIT_BACKUP));
    assert(hub_save_has_backup("root/game/SaveData/Collection.asd", HUB_SAVE_EDIT_BACKUP));

    /* Restoring pre-unlock saves must also remove stale post-unlock edit backups */
    assert(hub_save_restore_set("root/game", HUB_SAVE_UNLOCK_BACKUP));
    assert(!hub_save_has_backup("root/game/GlobalSave.asd", HUB_SAVE_EDIT_BACKUP));
    assert(!hub_save_has_backup("root/game/SaveData/Collection.asd", HUB_SAVE_EDIT_BACKUP));
    return 0;
}
""",
            sources=[HUB / "model.c", HUB / "save_storage.c"],
            includes=[HUB, REPO / "xsystem4/include", REPO / "xsystem4/subprojects/libsys4/include"],
        )

    def test_switch_local_import_path_guard_and_zip_posix_dir(self):
        """[S1-7, S1-12] local_import guards overlong/NULL source paths; import_zip handles POSIX S_IFDIR without trailing '/'."""
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            zpath = work / "posix_dir.zip"
            with zipfile.ZipFile(zpath, "w") as zf:
                dir_info = zipfile.ZipInfo("MyGame/SubDir")  # no trailing slash!
                dir_info.create_system = 3
                dir_info.external_attr = (stat.S_IFDIR | 0o755) << 16
                zf.writestr(dir_info, b"")
                zf.writestr("MyGame/System40.ini", 'CodeName = "Demo.ain"\n')
                zf.writestr("MyGame/Demo.ain", b"aindata")
                zf.writestr("MyGame/SubDir/asset.dat", b"nested")

            self.compile_and_run(
                r"""
#include "local_import.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    HubGame game;
    char error[160] = {0};

    /* S1-12: NULL and >= HUB_PATH_SIZE source paths rejected safely */
    assert(!hub_local_import("lib", NULL, NULL, &game, error));
    assert(*error != '\0');

    char overlong[HUB_PATH_SIZE + 64];
    memset(overlong, 'a', sizeof(overlong) - 1);
    overlong[sizeof(overlong) - 1] = '\0';
    *error = '\0';
    assert(!hub_local_import("lib", overlong, NULL, &game, error));
    assert(*error != '\0');

    /* S1-7: POSIX S_IFDIR without trailing '/' extracts as directory, not truncated file */
    assert(!mkdir("lib", 0700));
    assert(hub_local_import("lib", "posix_dir.zip", NULL, &game, error));
    assert(!strcmp(game.name, "MyGame"));
    struct stat st;
    assert(!lstat("lib/xsystem4/games/MyGame/SubDir", &st) && S_ISDIR(st.st_mode));
    assert(access("lib/xsystem4/games/MyGame/SubDi", F_OK) != 0);
    assert(!lstat("lib/xsystem4/games/MyGame/SubDir/asset.dat", &st) && S_ISREG(st.st_mode));
    return 0;
}
""",
                sources=[
                    HUB / "model.c", HUB / "fs_util.c", HUB / "install_transaction.c",
                    HUB / "local_import.c", HUB / "import_zip.c", HUB / "import_iso.c",
                ],
                includes=[HUB],
                libs=_zlib_flags(),
                cwd=work,
            )

    def test_switch_controller_runtime_gamecontroller_and_sideways_joycon(self):
        """[S1-9] switch_controls_gamecontroller uses SDL_GameControllerGetButton/GetAxis + alicehub_switch_face_button."""
        with tempfile.TemporaryDirectory() as tmp:
            mock = Path(tmp) / "mock"
            mock.mkdir()
            (mock / "switch.h").write_text(
                """#pragma once
typedef int HidNpadIdType;
enum { HidNpadIdType_No1 = 0, HidNpadIdType_No8 = 7, HidNpadStyleTag_NpadJoyDual = 4 };
enum { HidDeviceTypeBits_JoyLeft = 16, HidDeviceTypeBits_JoyRight = 32 };
extern int mock_style[8], mock_type[8];
static inline int hidGetNpadStyleSet(HidNpadIdType id) { return mock_style[id]; }
static inline int hidGetNpadDeviceType(HidNpadIdType id) { return mock_type[id]; }
""",
                encoding="utf-8",
            )
            (mock / "SDL.h").write_text(
                """#pragma once
#include <stdint.h>
#define SDL_arraysize(a) (sizeof(a) / sizeof((a)[0]))
#define SDL_MOUSE_TOUCHID ((SDL_TouchID)-1)
#define SDL_TOUCH_MOUSEID ((uint32_t)-1)
typedef int32_t SDL_JoystickID;
typedef int64_t SDL_TouchID;
typedef int64_t SDL_FingerID;
typedef struct SDL_Joystick { SDL_JoystickID id; } SDL_Joystick;
typedef struct SDL_GameController {
    SDL_Joystick joy;
    uint8_t btn[16];
    int16_t axis[6];
} SDL_GameController;
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct { int x, y, w, h; } SDL_Rect;
typedef struct { uint8_t r, g, b, a; } SDL_Color;
typedef int SDL_BlendMode;
enum { SDL_BLENDMODE_BLEND = 1 };
typedef enum {
    SDL_CONTROLLER_BUTTON_A = 0, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE, SDL_CONTROLLER_BUTTON_START,
    SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT
} SDL_GameControllerButton;
typedef enum {
    SDL_CONTROLLER_AXIS_LEFTX = 0, SDL_CONTROLLER_AXIS_LEFTY,
    SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY,
    SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_CONTROLLER_AXIS_TRIGGERRIGHT
} SDL_GameControllerAxis;
enum {
    SDL_APP_WILLENTERBACKGROUND = 1, SDL_APP_DIDENTERBACKGROUND,
    SDL_MULTIGESTURE, SDL_FINGERDOWN, SDL_FINGERMOTION, SDL_FINGERUP,
    SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP, SDL_MOUSEMOTION
};
typedef struct { SDL_TouchID touchId; SDL_FingerID fingerId; float x, y; } SDL_TouchFingerEvent;
typedef struct { SDL_TouchID touchId; } SDL_MultiGestureEvent;
typedef struct { uint32_t which; int x, y; } SDL_MouseButtonEvent;
typedef struct { uint32_t which; } SDL_MouseMotionEvent;
typedef struct {
    uint32_t type;
    SDL_TouchFingerEvent tfinger;
    SDL_MultiGestureEvent mgesture;
    SDL_MouseButtonEvent button;
    SDL_MouseMotionEvent motion;
} SDL_Event;
static inline SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *c) { return &c->joy; }
static inline SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *j) { return j->id; }
static inline uint8_t SDL_GameControllerGetButton(SDL_GameController *c, SDL_GameControllerButton b) { return c->btn[b]; }
static inline int16_t SDL_GameControllerGetAxis(SDL_GameController *c, SDL_GameControllerAxis a) { return c->axis[a]; }
static inline int SDL_GetNumTouchFingers(SDL_TouchID t) { (void)t; return 0; }
static inline void SDL_GetWindowSize(SDL_Window *w, int *pw, int *ph) { (void)w; *pw = 1280; *ph = 720; }
static inline void SDL_SetRenderDrawColor(SDL_Renderer *r, uint8_t a, uint8_t b, uint8_t c, uint8_t d) { (void)r;(void)a;(void)b;(void)c;(void)d; }
static inline void SDL_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rc) { (void)r;(void)rc; }
static inline void SDL_RenderGetLogicalSize(SDL_Renderer *r, int *w, int *h) { (void)r; *w = 1280; *h = 720; }
static inline void SDL_GetRendererOutputSize(SDL_Renderer *r, int *w, int *h) { (void)r; *w = 1280; *h = 720; }
static inline void SDL_GetRenderDrawColor(SDL_Renderer *r, uint8_t *a, uint8_t *b, uint8_t *c, uint8_t *d) { (void)r; *a=*b=*c=*d=0; }
static inline void SDL_GetRenderDrawBlendMode(SDL_Renderer *r, SDL_BlendMode *m) { (void)r; *m = 0; }
static inline void SDL_SetRenderDrawBlendMode(SDL_Renderer *r, SDL_BlendMode m) { (void)r; (void)m; }
""",
                encoding="utf-8",
            )
            self.compile_and_run(
                r"""
#include "controller_runtime.h"
#include <switch.h>
#include <assert.h>
#include <string.h>

int mock_style[8];
int mock_type[8];

int main(void)
{
    /* Dual Joy-Con (slot 1): SDL B is physical Switch A -> CP_A; X and Y stay unswapped */
    mock_style[1] = HidNpadStyleTag_NpadJoyDual;
    mock_type[1] = HidDeviceTypeBits_JoyLeft | HidDeviceTypeBits_JoyRight;
    SDL_GameController dual = {.joy = {.id = 1}};
    dual.btn[SDL_CONTROLLER_BUTTON_B] = 1;
    dual.btn[SDL_CONTROLLER_BUTTON_X] = 1;
    dual.axis[SDL_CONTROLLER_AXIS_TRIGGERLEFT] = 20000;
    dual.axis[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 10000; /* below 11468 threshold */
    uint32_t mask = switch_controls_gamecontroller(&dual);
    assert(mask == ((1u << CP_A) | (1u << CP_X) | (1u << CP_ZL)));

    /* Sideways single Joy-Con (slot 2): SDL A -> CP_A, SDL B -> CP_B (no face swap) */
    mock_style[2] = 0;
    mock_type[2] = HidDeviceTypeBits_JoyLeft;
    SDL_GameController sideways = {.joy = {.id = 2}};
    sideways.btn[SDL_CONTROLLER_BUTTON_A] = 1;
    sideways.btn[SDL_CONTROLLER_BUTTON_Y] = 1;
    sideways.axis[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = 32767;
    mask = switch_controls_gamecontroller(&sideways);
    assert(mask == ((1u << CP_A) | (1u << CP_Y) | (1u << CP_ZR)));
    return 0;
}
""",
                sources=[HUB / "model.c", HUB / "controller_profile.c", HUB / "controller_runtime.c"],
                includes=[mock, HUB, SWITCH],
                extra=["-D__SWITCH__"],
            )

    def test_switch_picker_text_null_guard(self):
        """[S1-1] picker_text(NULL) must return cleanly without dereferencing NULL."""
        fn = _extract_c_function(SWITCH / "picker_renderer.h", "picker_text")
        self.compile_and_run(
            r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
enum { PICKER_WIDTH = 1280, PICKER_HEIGHT = 720, FT_PIXEL_MODE_GRAY = 1 };
typedef struct { int pixel_mode, pitch; unsigned rows, width; const u8 *buffer; } FT_Bitmap;
typedef struct { FT_Bitmap bitmap; int bitmap_top, bitmap_left; struct { int x; } advance; } *FT_GlyphSlot;
typedef struct { u16 *pixels; u32 stride; } Picker;
static ssize_t decode_utf8(u32 *cp, const u8 *s) { *cp = *s; return 1; }
static FT_GlyphSlot picker_glyph(Picker *p, u32 cp) { (void)p; (void)cp; return NULL; }
static u16 picker_rgb(unsigned c) { return (u16)c; }
static unsigned picker_mix(unsigned a, unsigned b, unsigned c) { (void)b; (void)c; return a; }
""" + fn + r"""
int main(void)
{
    Picker p = {0};
    picker_text(&p, 0, 0, 100, 0xffffff, NULL);
    return 0;
}
""",
        )

    def test_ios_archive_bridge_same_inode_hardlink_and_heap_buffer(self):
        """[S2-8] ArchiveBridge.c must heap-allocate copy buffer and skip self-copy when hardlink target shares (st_dev, st_ino)."""
        src = (IOS_APP / "ArchiveBridge.c").read_text(encoding="utf-8")
        self.assertNotIn("char buffer[131072]", src)
        self.assertIn("malloc(AH_ARCHIVE_COPY_BUFFER_SIZE)", src)
        self.assertIn("free(buffer);", src)

        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            mock = work / "mock_inc"
            mock.mkdir()
            (mock / "archive.h").write_text(
                """#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
typedef ssize_t la_ssize_t;
enum { ARCHIVE_OK = 0, ARCHIVE_EOF = 1, ARCHIVE_WARN = -20, ARCHIVE_FATAL = -30 };
struct archive;
struct archive_entry;
struct archive *archive_read_new(void);
int archive_read_support_format_zip(struct archive *);
int archive_read_support_format_rar(struct archive *);
int archive_read_support_format_rar5(struct archive *);
int archive_read_support_format_7zip(struct archive *);
int archive_read_support_format_tar(struct archive *);
int archive_read_support_format_iso9660(struct archive *);
int archive_read_support_filter_all(struct archive *);
int archive_read_open_filename(struct archive *, const char *, size_t);
int archive_read_next_header(struct archive *, struct archive_entry **);
int archive_read_data_skip(struct archive *);
la_ssize_t archive_read_data(struct archive *, void *, size_t);
const char *archive_error_string(struct archive *);
int archive_read_free(struct archive *);
""",
                encoding="utf-8",
            )
            (mock / "archive_entry.h").write_text(
                """#pragma once
#include <stdint.h>
#include <sys/types.h>
#define AE_IFREG 0100000
struct archive_entry;
int archive_entry_is_encrypted(struct archive_entry *);
mode_t archive_entry_filetype(struct archive_entry *);
const char *archive_entry_symlink(struct archive_entry *);
const char *archive_entry_pathname_utf8(struct archive_entry *);
const char *archive_entry_pathname(struct archive_entry *);
int archive_entry_size_is_set(struct archive_entry *);
int64_t archive_entry_size(struct archive_entry *);
const char *archive_entry_hardlink_utf8(struct archive_entry *);
const char *archive_entry_hardlink(struct archive_entry *);
""",
                encoding="utf-8",
            )
            self.compile_and_run(
                r"""
#include "ArchiveBridge.h"
#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct archive_entry {
    const char *name;
    const char *hardlink;
    const char *payload;
    size_t offset;
};
struct archive {
    struct archive_entry entries[3];
    int idx;
};

struct archive *archive_read_new(void) {
    struct archive *a = calloc(1, sizeof(*a));
    a->idx = -1;
    a->entries[0] = (struct archive_entry){"Foo.txt", NULL, "KEEP-ME-12345", 0};
    a->entries[1] = (struct archive_entry){"foo.txt", "Foo.txt", "", 0};
    a->entries[2] = (struct archive_entry){"bar.txt", "Foo.txt", "", 0};
    return a;
}
int archive_read_support_format_zip(struct archive *a) { (void)a; return ARCHIVE_OK; }
int archive_read_support_format_rar(struct archive *a) { (void)a; return ARCHIVE_OK; }
int archive_read_support_format_rar5(struct archive *a) { (void)a; return ARCHIVE_OK; }
int archive_read_support_format_7zip(struct archive *a) { (void)a; return ARCHIVE_OK; }
int archive_read_support_format_tar(struct archive *a) { (void)a; return ARCHIVE_OK; }
int archive_read_support_format_iso9660(struct archive *a) { (void)a; return ARCHIVE_OK; }
int archive_read_support_filter_all(struct archive *a) { (void)a; return ARCHIVE_OK; }
int archive_read_open_filename(struct archive *a, const char *f, size_t b) { (void)a;(void)f;(void)b; return ARCHIVE_OK; }
int archive_read_next_header(struct archive *a, struct archive_entry **e) {
    if (++a->idx >= 3) return ARCHIVE_EOF;
    *e = &a->entries[a->idx];
    return ARCHIVE_OK;
}
int archive_read_data_skip(struct archive *a) { (void)a; return ARCHIVE_OK; }
la_ssize_t archive_read_data(struct archive *a, void *buf, size_t len) {
    struct archive_entry *e = &a->entries[a->idx];
    size_t total = strlen(e->payload);
    if (e->offset >= total) return 0;
    size_t n = total - e->offset;
    if (n > len) n = len;
    memcpy(buf, e->payload + e->offset, n);
    e->offset += n;
    return (la_ssize_t)n;
}
const char *archive_error_string(struct archive *a) { (void)a; return "err"; }
int archive_read_free(struct archive *a) { free(a); return ARCHIVE_OK; }
int archive_entry_is_encrypted(struct archive_entry *e) { (void)e; return 0; }
mode_t archive_entry_filetype(struct archive_entry *e) { (void)e; return AE_IFREG; }
const char *archive_entry_symlink(struct archive_entry *e) { (void)e; return NULL; }
const char *archive_entry_pathname_utf8(struct archive_entry *e) { return e->name; }
const char *archive_entry_pathname(struct archive_entry *e) { return e->name; }
int archive_entry_size_is_set(struct archive_entry *e) { (void)e; return 1; }
int64_t archive_entry_size(struct archive_entry *e) { return (int64_t)strlen(e->payload); }
const char *archive_entry_hardlink_utf8(struct archive_entry *e) { return e->hardlink; }
const char *archive_entry_hardlink(struct archive_entry *e) { return e->hardlink; }

static char *accept_cb(void *ctx, const uint8_t *raw, size_t n, int64_t size)
{
    (void)ctx; (void)size;
    if (n == 7 && !memcmp(raw, "Foo.txt", 7))
        return strdup("Foo.txt");
    if (n == 7 && !memcmp(raw, "foo.txt", 7)) {
        /* Simulate case-insensitive APFS aliasing where foo.txt resolves to the same inode as Foo.txt */
        assert(link("Foo.txt", "same_inode_alias.txt") == 0);
        return strdup("same_inode_alias.txt");
    }
    if (n == 7 && !memcmp(raw, "bar.txt", 7))
        return strdup("distinct_copy.txt");
    return NULL;
}

static int progress_cb(void *ctx, int64_t total) { (void)ctx; (void)total; return 0; }

static void assert_contents(const char *path, const char *expected)
{
    FILE *fp = fopen(path, "rb");
    assert(fp);
    char buf[64] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    assert(fclose(fp) == 0);
    assert(n == strlen(expected) && !strcmp(buf, expected));
}

int main(void)
{
    char err[256] = {0};
    int count = ah_archive_extract("dummy.tar", accept_cb, progress_cb, NULL, err, sizeof(err));
    assert(count == 3);
    /* Same-inode hardlink must NOT self-truncate Foo.txt to 0 bytes! */
    assert_contents("Foo.txt", "KEEP-ME-12345");
    assert_contents("same_inode_alias.txt", "KEEP-ME-12345");
    /* Distinct-path hardlink still copies payload properly */
    assert_contents("distinct_copy.txt", "KEEP-ME-12345");
    return 0;
}
""",
                sources=[IOS_APP / "ArchiveBridge.c"],
                includes=[mock, IOS_APP],
                cwd=work,
            )


class Wave2StructuralRegressionsTest(unittest.TestCase):
    def test_switch_ui_and_archive_warn_structural_fixes(self):
        """[S1-1, S1-3, S1-5, S1-8, S1-10, S1-13] Structural assertions for Switch Hub/Launcher fixes."""
        app_c = (HUB / "app.c").read_text(encoding="utf-8")
        self.assertIn('if (back)\n        picker_hint(p, 1094, "B", back, false);', app_c)
        self.assertIn("hub_instance_recover(HUB_ROOT)", app_c)

        mods_screen = (HUB / "mods_screen.c").read_text(encoding="utf-8")
        self.assertIn('canceled ? "Wait" : "Cancel"', mods_screen)
        self.assertNotIn('canceled ? NULL : "Cancel"', mods_screen)
        self.assertRegex(
            mods_screen,
            r"(?s)for\s*\(size_t i = \*focus \+ 1;\s*i < screen\.catalog\.count;\s*\+\+i\).*?"
            r"!strcmp\(screen\.catalog\.rows\[i\]\.group,\s*screen\.release\.group\).*?break;",
        )

        online_install = (HUB / "online_install.c").read_text(encoding="utf-8")
        self.assertIn("result == ARCHIVE_WARN", online_install)
        self.assertIn("archive_entry_pathname_utf8(entry)", online_install)

        mods_install = (HUB / "mods_install.c").read_text(encoding="utf-8")
        self.assertEqual(mods_install.count("rc == ARCHIVE_WARN"), 2)

        instance_actions = (HUB / "instance_actions.c").read_text(encoding="utf-8")
        self.assertIn("hub_online_worker_busy()", instance_actions)
        self.assertIn("hub_install_pending(HUB_ROOT, game)", instance_actions)

        import_screen = (HUB / "import_screen.c").read_text(encoding="utf-8")
        draw_fn = import_screen.split("static void draw(", 1)[1]
        rollback_pos = draw_fn.index('"SD recovery needed"')
        cancel_toast_pos = draw_fn.index('"Import canceled"')
        self.assertLess(rollback_pos, cancel_toast_pos)

        launcher = (SWITCH / "launcher.c").read_text(encoding="utf-8")
        self.assertIn("static bool romfs_mounted;", launcher)
        self.assertIn("atexit(exit_romfs)", launcher)

    def test_ios_swift_wave2_fixes(self):
        """[S2-3, S2-9..S2-16] Structural assertions for every Wave-2 iOS Swift fix."""
        # S2-3: SaveSyncSession + SaveSync
        session = (IOS_APP / "SaveSyncSession.swift").read_text(encoding="utf-8")
        self.assertNotIn("atexit", session)
        self.assertNotIn("SaveSyncFolder.finish", session)
        self.assertNotIn("DispatchSemaphore", session)
        self.assertIn("removeObservers()", session)
        self.assertIn("MainActor.assumeIsolated { self?.end() }", session)
        # end() must run upload asynchronously inside a BackgroundTask
        end_block = session.split("static func end()", 1)[1]
        self.assertIn("BackgroundTask()", end_block)
        self.assertIn("SaveSyncFolder.enqueue", end_block)

        sync = (IOS_APP / "SaveSync.swift").read_text(encoding="utf-8")
        self.assertNotIn("private let coordinator = NSFileCoordinator()", sync)
        self.assertNotIn("static func finish(", sync)
        rename_fn = sync.split("func rename(", 1)[1].split("@discardableResult func sync(", 1)[0]
        self.assertNotIn("renameSnapshot(", rename_fn)
        self.assertNotIn("coordinatedWrite(", rename_fn)
        pub_fn = sync.split("private func publishedRename(", 1)[1].split("func rename(", 1)[0]
        self.assertIn("try? JSONDecoder().decode(Rename.self", pub_fn)
        self.assertIn("removeItem(at: coordinatedURL)", pub_fn)

        # S2-10: OnlineGameInstaller + ModInstaller + UncensoredPatch
        online_installer = (IOS_APP / "OnlineGameInstaller.swift").read_text(encoding="utf-8")
        self.assertIn("let progressLock = NSLock()", online_installer)
        self.assertIn("progressLock.lock()", online_installer)

        mod_installer = (IOS_APP / "ModInstaller.swift").read_text(encoding="utf-8")
        self.assertIn("let progressEmitLock = NSLock()", mod_installer)
        self.assertIn("maxEmittedBytes", mod_installer)
        self.assertIn("emitMonotonicProgress", mod_installer)
        transfer_cls = mod_installer.split("private final class ModDownloadTransfer:", 1)[1].split("private final class DownloadSegment", 1)[0]
        self.assertIn("lastProgressEmit", transfer_cls)
        self.assertIn(">= 0.08", transfer_cls)
        self.assertIn("progress(received, total > 0 ? total : received)", transfer_cls)

        uncensored = (IOS_APP / "UncensoredPatch.swift").read_text(encoding="utf-8")
        self.assertIn("let progressLock = NSLock()", uncensored)
        self.assertIn(">= 0.08", uncensored)
        # S2-12: replace + InstalledUncensoredInfo.write happen BEFORE removing prevURLToRemove
        replace_idx = uncensored.index("try ModFiles.replace(downloaded, dest)")
        write_idx = uncensored.index(".write(to: directory)", replace_idx)
        remove_prev_idx = uncensored.index("if let prevURLToRemove", write_idx)
        self.assertLess(replace_idx, write_idx)
        self.assertLess(write_idx, remove_prev_idx)

        # S2-11: ModOverlay recursive .vanilla.bak detection & non-empty stem guard
        overlay = (IOS_APP / "ModOverlay.swift").read_text(encoding="utf-8")
        self.assertIn("guard !isDirectory(destination) else", overlay)
        self.assertIn("private static func originalURL(forVanillaBackup backup: URL) -> URL?", overlay)
        self.assertIn("guard !stem.isEmpty", overlay)
        can_restore = overlay.split("static func canRestoreVanilla(", 1)[1].split("static func displayName(", 1)[0]
        self.assertIn("ModFiles.walk(directory)", can_restore)
        self.assertIn("originalURL(forVanillaBackup:", can_restore)

        # S2-13: SaveZip cumulative cap (both read & write), atomic write, Gregorian calendar
        save_zip = (IOS_APP / "SaveZip.swift").read_text(encoding="utf-8")
        self.assertNotIn("Calendar.current", save_zip)
        self.assertIn("Calendar(identifier: .gregorian)", save_zip)
        self.assertIn("maxTotalDecompressedBytes = 512 * 1024 * 1024", save_zip)
        self.assertIn("size <= Self.maxTotalDecompressedBytes - totalDecompressed", save_zip)
        write_fn = save_zip.split("static func write(", 1)[1].split("static func read(", 1)[0]
        self.assertIn("entry.data.count <= Self.maxEntryDecompressedBytes", write_fn)
        self.assertIn("entry.data.count <= Self.maxTotalDecompressedBytes - totalUncompressed", write_fn)
        self.assertIn("try out.write(to: url, options: .atomic)", save_zip)

        # S2-14: BackgroundActivity + GlobalSaveEditor
        bg = (IOS_APP / "BackgroundActivity.swift").read_text(encoding="utf-8")
        begin_fn = bg.split("private func begin()", 1)[1].split("private func submitContinuedTask()", 1)[0]
        self.assertIn("let alreadyEnded = ended", begin_fn)
        self.assertIn("guard !alreadyEnded else { return }", begin_fn)
        self.assertIn("UIApplication.shared.endBackgroundTask(taskID)", begin_fn)
        end_legacy = bg.split("private func endLegacyTask()", 1)[1].split("private func notify(", 1)[0]
        self.assertIn("lock.lock()", end_legacy)

        gse = (IOS_APP / "GlobalSaveEditor.swift").read_text(encoding="utf-8")
        self.assertIn("var pendingFilename: String", gse)
        self.assertIn("struct PendingBackup: Codable, Equatable", gse)
        self.assertIn("pendingBackupURL(", gse)
        self.assertIn("activeBackupURL(", gse)
        self.assertIn("stageBackup(", gse)
        self.assertIn("commitStagedBackup(", gse)
        self.assertIn("replaceAndCommit(", gse)
        # commitStagedBackup must write committed backup before removing staged pending file
        commit_fn = gse.split("private func commitStagedBackup(", 1)[1].split("private func apply(", 1)[0]
        self.assertLess(commit_fn.index("try writeCommittedBackup("), commit_fn.index("removeItem(at: staged.url)"))
        rac_fn = gse.split("private func replaceAndCommit(", 1)[1].split("func edit(", 1)[0]
        self.assertLess(rac_fn.index("try apply([entry], to: directory)"), rac_fn.index("commitStagedBackup"))
        self.assertIn("if rollback(applied), let stagedBackup", rac_fn)
        active_fn = gse.split("private func activeBackupURL(", 1)[1].split("private func exists(", 1)[0]
        self.assertIn("liveEntries == pendingRecord.baseline", active_fn)
        self.assertIn("try writeCommittedBackup(pendingRecord.undo, for: kind)", active_fn)
        self.assertIn("catch is DecodingError", active_fn)
        restore_fn = gse.split("func restore(", 1)[1]
        self.assertIn("activeBackupURL(kind)", restore_fn)
        self.assertIn("removeItem(at: url)", restore_fn)

        # S2-15: Iso9660Extractor overflow checks
        iso = (IOS_APP / "Iso9660Extractor.swift").read_text(encoding="utf-8")
        self.assertGreaterEqual(iso.count("addingReportingOverflow"), 2)
        self.assertIn("guard !overflow else { throw ArchiveError.invalidImage }", iso)

        # S2-16: SaveManager case-only rename on APFS, backup size pre-check, pending backup invalidation
        save_mgr = (IOS_APP / "SaveManager.swift").read_text(encoding="utf-8")
        backup_fn = save_mgr.split("@discardableResult func backup()", 1)[1].split("private func safePath(", 1)[0]
        self.assertIn("SaveZip.maxTotalDecompressedBytes - totalSize", backup_fn)
        self.assertIn("$0.pendingFilename == child.lastPathComponent", save_mgr)
        rename_mgr = save_mgr.split("func rename(", 1)[1].split("func delete(", 1)[0]
        self.assertIn(".fileResourceIdentifierKey", rename_mgr)
        self.assertIn("sourceID.isEqual(targetID)", rename_mgr)


if __name__ == "__main__":
    unittest.main()
