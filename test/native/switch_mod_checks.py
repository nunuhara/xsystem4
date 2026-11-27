from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile


REPO = Path(__file__).resolve().parents[3]
HUB = REPO / "cmake/switch/hub"
ENGINE = REPO / "xsystem4"


class SwitchModsTest(unittest.TestCase):
    def test_catalog_and_overlay_transaction(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            originals = work / "xsystem4/games/My copy"
            originals.mkdir(parents=True)
            (originals / "Rance10.ain").write_bytes(b"vanilla")
            (originals / "AliceStart.ini").write_bytes(b"config")
            separate_saves = work / "xsystem4/saves/My copy"
            separate_saves.mkdir(parents=True)
            (separate_saves / "GlobalSave.asd").write_bytes(b"save")
            evenicle = work / "xsystem4/games/Other copy"
            evenicle.mkdir()
            (evenicle / "Evenicle2.ain").write_bytes(b"untranslated")
            fixtures = {
                "base.zip": {
                    "wrapper/RANCE10.AIN": b"mod",
                    "wrapper/data/new.txt": b"addition",
                    "wrapper/SaveData/GlobalSave.asd": b"ignored save",
                },
                "patch.zip": {"Rance10.ain": b"patched", "patch.txt": b"patch only"},
                "translation.zip": {
                    "wrapper/MAI/SC/Evenicle2.ain": b"translation",
                    "wrapper/Evenicle2CG.afa": b"cg",
                },
                "bad.zip": {"Rance10.ain": b"bad", "../escaped": b"bad"},
                "empty-boot.zip": {"Rance10.ain": b""},
            }
            for archive, entries in fixtures.items():
                with zipfile.ZipFile(work / archive, "w", compression=zipfile.ZIP_DEFLATED) as f:
                    for name, content in entries.items():
                        f.writestr(name, content)
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(HUB), "-I", str(ENGINE / "include"),
                 *(str(HUB / f"{s}.c") for s in (
                     "model", "fs_util", "install_transaction", "mods_catalog", "mods_install",
                     "local_import", "import_zip", "import_iso")),
                 str(ENGINE / "src/cJSON.c"), "-x", "c", "-", "-o", str(work / "test"),
                 "-larchive", "-lz", "-Wl,--wrap=rename"],
                input=r"""
#include "mods.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GAME "xsystem4/games/My copy"
#define EVENICLE "xsystem4/games/Other copy"
#define SHA "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
#define RELEASE(ID, NAME, PATCH, GROUP) "{\"fileId\":" ID ",\"version\":\"" NAME "\",\"fileName\":\"" NAME ".zip\"," \
    "\"key\":\"mods/" NAME ".zip\",\"size\":1,\"sha256\":\"" SHA "\",\"isPatch\":" PATCH ",\"group\":\"" GROUP "\"}"

static bool fail_publish, fail_download, cancel;
int __real_rename(const char *, const char *);
int __wrap_rename(const char *from, const char *to)
{
    if (fail_publish && strstr(from, "/stage") && !strcmp(to, "./" GAME)) {
        errno = EIO;
        return -1;
    }
    return __real_rename(from, to);
}

static void expect(const char *path, const char *value)
{
    char bytes[32] = {0};
    FILE *f = fopen(path, "rb");
    assert(f && fread(bytes, 1, sizeof(bytes) - 1, f) == strlen(value) && !fclose(f));
    assert(!strcmp(bytes, value));
}

static bool update(void *data, const char *detail, uint64_t done, uint64_t total)
{
    (void)data; (void)done; (void)total;
    return !cancel || strcmp(detail, "Applying archive");
}

static bool fetch(void *data, const ModsRelease *release, const char *path)
{
    (void)data;
    FILE *out = fopen(path, "wb");
    assert(out);
    if (release->kind == MODS_UNCENSORED || fail_download) {
        assert(fputs("standalone", out) >= 0 && !fclose(out));
        return !fail_download;
    }
    FILE *in = fopen(release->name, "rb");
    assert(in);
    for (int c; (c = fgetc(in)) != EOF;) assert(fputc(c, out) != EOF);
    assert(!fclose(in) && !fclose(out));
    return true;
}

int main(void)
{
    const ModsGame *descriptor = mods_game(GAME, HUB_XSYSTEM4);
    assert(descriptor && !strcmp(descriptor->id, "rance10") && descriptor->kind == MODS_ARCHIVE);
    assert(!mods_game(GAME, HUB_SYSTEM3));
    assert(!strcmp(mods_game(EVENICLE, HUB_XSYSTEM4)->id, "evenicle2"));
    ModsCatalog catalog = {0}, uncensored = {0};
    assert(mods_catalog_parse(&catalog, descriptor, "{\"releases\":["
        RELEASE("3", "patch", "true", "A") ","
        RELEASE("2", "unrelated", "false", "B") ","
        RELEASE("1", "base", "false", "A") "]}"));
    assert(catalog.count == 3 && catalog.rows[0].base_id == 1 &&
           !strcmp(catalog.rows[0].sha256, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    ModsGame cg = {.id = "rance10", .kind = MODS_UNCENSORED};
    assert(!mods_catalog_parse(&uncensored, &cg, "{\"gameId\":\"rance01\","
        "\"size\":10,\"key\":\"uncensored/x.afa\",\"fileName\":\"AliceHubUncensoredCG.afa\",\"sha256\":\"" SHA "\"}"));
    assert(!mods_catalog_parse(&uncensored, &cg, "{\"gameId\":\"rance10\","
        "\"size\":10,\"key\":\"uncensored/../x\",\"fileName\":\"AliceHubUncensoredCG.afa\",\"sha256\":\"" SHA "\"}"));
    assert(mods_catalog_parse(&uncensored, &cg, "{\"gameId\":\"rance10\","
        "\"size\":10,\"key\":\"uncensored/x.afa\",\"fileName\":\"AliceHubUncensoredCG.afa\",\"sha256\":\"" SHA "\"}"));
    ModsCallbacks cb = {.fetch = fetch, .update = update};
    ModsInstalled state;
    HubGame game;
    char error[160];
    assert(hub_game_init(&game, HUB_XSYSTEM4, "My copy"));
    assert(mods_installed(GAME, MODS_ARCHIVE, &state) && !state.installed);
    assert(!mods_apply(".", &game, MODS_ARCHIVE, NULL, NULL, "bad.zip", &cb, error));
    assert(access("escaped", F_OK));
    assert(!mods_apply(".", &game, MODS_ARCHIVE, NULL, NULL, "empty-boot.zip", &cb, error));
    cancel = true;
    assert(!mods_apply(".", &game, MODS_ARCHIVE, NULL, NULL, "base.zip", &cb, error));
    cancel = false;
    fail_publish = true;
    assert(!mods_apply(".", &game, MODS_ARCHIVE, NULL, NULL, "base.zip", &cb, error));
    fail_publish = false;
    expect(GAME "/Rance10.ain", "vanilla");
    assert(access(GAME "/Rance10.ain.vanilla.bak", F_OK));
    assert(mods_apply(".", &game, MODS_ARCHIVE, &catalog.rows[0], &catalog.rows[2], NULL, &cb, error));
    expect(GAME "/Rance10.ain", "patched");
    expect(GAME "/Rance10.ain.vanilla.bak", "vanilla");
    expect(GAME "/data/new.txt", "addition");
    assert(access(GAME "/SaveData", F_OK));
    assert(mods_installed(GAME, MODS_ARCHIVE, &state) && state.installed && state.file_id == 3 && state.base_id == 1);
    assert(mods_apply(".", &game, MODS_UNCENSORED, uncensored.rows, NULL, NULL, &cb, error));
    fail_download = true;
    assert(!mods_apply(".", &game, MODS_UNCENSORED, uncensored.rows, NULL, NULL, &cb, error));
    fail_download = false;
    assert(mods_installed(GAME, MODS_UNCENSORED, &state) && state.installed);
    assert(mods_apply(".", &game, MODS_ARCHIVE, NULL, NULL, NULL, &cb, error));
    expect(GAME "/Rance10.ain", "vanilla");
    assert(access(GAME "/Rance10.ain.vanilla.bak", F_OK));
    assert(access(GAME "/data/new.txt", F_OK) && access(GAME "/patch.txt", F_OK));
    expect(GAME "/AliceHubUncensoredCG.afa", "standalone");
    assert(mods_apply(".", &game, MODS_UNCENSORED, NULL, NULL, NULL, &cb, error));
    assert(mods_installed(GAME, MODS_UNCENSORED, &state) && !state.installed);
    expect("xsystem4/saves/My copy/GlobalSave.asd", "save");
    assert(hub_game_init(&game, HUB_XSYSTEM4, "Other copy"));
    assert(mods_apply(".", &game, MODS_TRANSLATION, NULL, NULL, "translation.zip", &cb, error));
    expect(EVENICLE "/Evenicle2.ain", "translation");
    expect(EVENICLE "/Evenicle2CG.afa", "cg");
    assert(mods_apply(".", &game, MODS_TRANSLATION, NULL, NULL, NULL, &cb, error));
    expect(EVENICLE "/Evenicle2.ain", "untranslated");
    free(catalog.rows);
    free(uncensored.rows);
}
""",
                text=True, check=True,
            )
            subprocess.run([str(work / "test")], cwd=work, check=True)
