"""Cross-platform 170-game audit regression tests (Android / iOS / Switch)."""
import glob
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zlib


REPO = Path(__file__).resolve().parents[3]
ANDROID_SRC = REPO / "project/app/src/main/java/io/github/kichikuou/xsystem4"
ANDROID_RES = REPO / "project/app/src/main/res"
IOS_APP = REPO / "ios/App"
SWITCH_DIR = REPO / "cmake/switch"
HUB = SWITCH_DIR / "hub"
ENGINE = REPO / "xsystem4"
SYS4 = ENGINE / "subprojects/libsys4"
MANIFEST_DIR = Path("/tmp/alicehub_audit_manifests")


def _cp932_char_class(ch: str) -> str:
    code = ord(ch)
    if code < 0x80:
        return "ascii"
    if 0xE000 <= code <= 0xF8FF or ch == "\ufffd":
        return "pua"
    try:
        b = ch.encode("cp932")
    except UnicodeEncodeError:
        return "non_cp932"
    if len(b) == 1:
        return "halfwidth"
    if len(b) != 2:
        return "ext"
    sjis = (b[0] << 8) | b[1]
    if 0x8140 <= sjis <= 0x84BE:
        return "kana_sym"
    if 0x889F <= sjis <= 0x9872:
        return "jis1"
    if 0x989F <= sjis <= 0xEAA4:
        return "jis2"
    return "ext"


def _japanese_score(text: str) -> int:
    weights = {
        "ascii": 0,
        "kana_sym": 4,
        "jis1": 3,
        "jis2": -1,
        "halfwidth": -4,
        "ext": -4,
        "non_cp932": -4,
        "pua": -20,
    }
    return sum(weights[_cp932_char_class(ch)] for ch in text)


def repair_sjis_as_gbk_mojibake(text: str) -> str:
    """Reference implementation matching ArchiveExtractor.kt / ArchiveExtractor.swift / SaveManager.swift."""
    if not text or not any(ord(c) >= 0x80 for c in text):
        return text
    sep = "\\" if ("\\" in text and "/" not in text) else "/"
    segments = []
    for seg in text.replace("\\", "/").split("/"):
        if not seg or not any(ord(c) >= 0x80 for c in seg):
            segments.append(seg)
            continue
        try:
            raw = seg.encode("gbk")
        except UnicodeEncodeError:
            segments.append(seg)
            continue
        i = 0
        pairs = 0
        non_gb2312 = 0
        ok = True
        while i < len(raw):
            b0 = raw[i]
            if b0 < 0x80:
                i += 1
                continue
            if 0xA1 <= b0 <= 0xDF or not ((0x81 <= b0 <= 0x9F) or (0xE0 <= b0 <= 0xEF)) or i + 1 >= len(raw):
                ok = False
                break
            b1 = raw[i + 1]
            if not ((0x40 <= b1 <= 0x7E) or (0x80 <= b1 <= 0xFC)):
                ok = False
                break
            pairs += 1
            if b0 < 0xA1 or b1 < 0xA1:
                non_gb2312 += 1
            i += 2
        if not ok or pairs == 0 or non_gb2312 != pairs:
            segments.append(seg)
            continue
        try:
            dec = raw.decode("cp932")
        except UnicodeDecodeError:
            segments.append(seg)
            continue
        if "\ufffd" in dec:
            segments.append(seg)
            continue
        orig_classes = [_cp932_char_class(c) for c in seg if ord(c) >= 0x80]
        if all(c in ("kana_sym", "jis1") for c in orig_classes):
            segments.append(seg)
            continue
        dec_classes = [_cp932_char_class(c) for c in dec if ord(c) >= 0x80]
        if any(c in ("pua", "non_cp932", "ext", "halfwidth") for c in dec_classes):
            segments.append(seg)
            continue
        if _japanese_score(dec) <= _japanese_score(seg) or _japanese_score(dec) <= 0:
            segments.append(seg)
            continue
        segments.append(dec)
    return sep.join(segments)


def parse_apple_strings_strict(path: Path) -> dict:
    """Strictly parse a .strings file, validating quotes, semicolons, and rejecting duplicate keys."""
    text = path.read_text(encoding="utf-8")
    result = {}
    pos = 0
    n = len(text)
    line_no = 1

    def skip_ws_and_comments():
        nonlocal pos, line_no
        while pos < n:
            if text[pos].isspace():
                if text[pos] == "\n":
                    line_no += 1
                pos += 1
            elif text.startswith("//", pos):
                nl = text.find("\n", pos)
                pos = n if nl == -1 else nl + 1
                line_no += 1
            elif text.startswith("/*", pos):
                end = text.find("*/", pos + 2)
                if end == -1:
                    raise AssertionError(f"{path}:{line_no}: unterminated block comment")
                line_no += text[pos:end + 2].count("\n")
                pos = end + 2
            else:
                break

    def read_quoted():
        nonlocal pos, line_no
        if pos >= n or text[pos] != '"':
            raise AssertionError(f"{path}:{line_no}: expected '\"', got {text[pos:pos+20]!r}")
        pos += 1
        out = []
        while pos < n:
            ch = text[pos]
            if ch == "\\":
                if pos + 1 >= n:
                    raise AssertionError(f"{path}:{line_no}: dangling escape")
                nxt = text[pos + 1]
                out.append("\\" + nxt)
                pos += 2
            elif ch == '"':
                pos += 1
                return "".join(out)
            else:
                if ch == "\n":
                    line_no += 1
                out.append(ch)
                pos += 1
        raise AssertionError(f"{path}:{line_no}: unterminated string literal")

    while True:
        skip_ws_and_comments()
        if pos >= n:
            break
        key_line = line_no
        key = read_quoted()
        skip_ws_and_comments()
        if pos >= n or text[pos] != "=":
            raise AssertionError(f"{path}:{key_line}: expected '=' after key {key!r}")
        pos += 1
        skip_ws_and_comments()
        val = read_quoted()
        skip_ws_and_comments()
        if pos >= n or text[pos] != ";":
            raise AssertionError(f"{path}:{key_line}: expected ';' after value for key {key!r}")
        pos += 1
        if key in result:
            raise AssertionError(f"{path}:{key_line}: duplicate key {key!r}")
        result[key] = val
    return result


class CrossPlatformAuditRegressionTest(unittest.TestCase):
    # ------------------------------------------------------------------
    # Item 1: P0 mojibake repair & CDN manifest path handling
    # ------------------------------------------------------------------
    def test_item1_mojibake_repair_preserves_jis1_and_repairs_genuine_mojibake(self):
        # paschacpp & pastelchime2 regressions: never rewrite valid JIS-1 kanji or double-rewrite
        for untouched in (
            "dat/戦闘EF.acx",
            "dat/戦闘EFT.acx",
            "戦闘",
            "鷹鶚",
            "dat/鷹鶚EF.acx",
            "戦国ランスGA.ald",
            "战国兰斯中文版/测试游戏.ain",
            "超昂閃忍ハルカ.ain",
        ):
            self.assertEqual(repair_sjis_as_gbk_mojibake(untouched), untouched)

        # Genuine GBK-mojibake Shift-JIS filenames must still repair and be idempotent
        for moji, expected in (
            ("愴崙儔儞僗GA.ald", "戦国ランスGA.ald"),
            ("愴崙儔儞僗_Sys.ASD", "戦国ランス_Sys.ASD"),
            ("movie/僗僞僢僼.alm", "movie/スタッフ.alm"),
            ("儔儞僗丒僋僄僗僩CG.afa", "ランス・クエストCG.afa"),
            ("儔儞僗丒僋僄僗僩.ain", "ランス・クエスト.ain"),
            ("嵢偟傏傝sys.asd", "妻しぼりsys.asd"),
            ("嵢傒偖偄俀.ain", "妻みぐい２.ain"),
            ("儔儞僗俇偦偺屻.ain", "ランス６その後.ain"),
            ("楇幃.ain", "零式.ain"),
            ("柤慜.ASD", "名前.ASD"),
            ("墹巕_M1.asd", "王子_M1.asd"),
            ("偼乣偲傆傞儅儅儞", "は～とふるママン"),
        ):
            repaired = repair_sjis_as_gbk_mojibake(moji)
            self.assertEqual(repaired, expected)
            self.assertEqual(repair_sjis_as_gbk_mojibake(repaired), expected)

        # Exhaustive check across all 170 CDN manifests when available
        if MANIFEST_DIR.is_dir():
            manifest_files = sorted(MANIFEST_DIR.glob("*.json"))
            checked_manifests = 0
            for mf in manifest_files:
                data = json.loads(mf.read_text(encoding="utf-8"))
                if not isinstance(data, dict) or "files" not in data:
                    continue
                checked_manifests += 1
                slug = mf.stem
                for entry in data["files"]:
                    path = entry["path"]
                    repaired = repair_sjis_as_gbk_mojibake(path)
                    if slug == "sengokurance":
                        self.assertEqual(repair_sjis_as_gbk_mojibake(repaired), repaired)
                    else:
                        self.assertEqual(
                            repaired, path,
                            f"False-positive mojibake rewrite on {slug}: {path!r} -> {repaired!r}",
                        )
            self.assertGreaterEqual(checked_manifests, 170)

    def test_item1_android_and_ios_sources_enforce_manifest_and_heuristic_fixes(self):
        online_installer = (ANDROID_SRC / "OnlineInstaller.kt").read_text(encoding="utf-8")
        self.assertNotIn("ArchiveExtractor.repairSjisAsGbkMojibake(entry.path)", online_installer)

        for path in (
            ANDROID_SRC / "ArchiveExtractor.kt",
            IOS_APP / "ArchiveExtractor.swift",
            IOS_APP / "SaveManager.swift",
        ):
            src = path.read_text(encoding="utf-8")
            self.assertIn("0x889F", src, f"Missing JIS-1 range guard in {path}")
            self.assertIn("scoreJapanesePlausibility", src, f"Missing plausibility score in {path}")

    # ------------------------------------------------------------------
    # Item 2: P0 iOS kichikuou classification + local-import parity
    # ------------------------------------------------------------------
    def test_item2_ios_kichikuou_and_local_import_parity(self):
        gl = (IOS_APP / "GameLibrary.swift").read_text(encoding="utf-8")

        # kichikuou explicit detection in inspect()
        self.assertIn('regularNamesLower.contains("kichikurancesa.ald")', gl)
        self.assertIn('regularNamesLower.contains("kichikurancega.ald")', gl)
        self.assertIn('regularNamesLower.contains("鬼畜王sa.ald")', gl)
        self.assertIn('else if kichikuou { detectedArtworkID = "kichikuou" }', gl)

        # kichikuou ordered above rance1classic in artworkAliases, and bare rancesa/rancega removed
        aliases_block = gl.split("private static let artworkAliases", 1)[1].split("    ]", 1)[0]
        self.assertLess(
            aliases_block.index('("kichikuou",'),
            aliases_block.index('("rance1classic",'),
        )
        r1_line = re.search(r'\("rance1classic", \[[^\]]+\]\)', aliases_block).group(0)
        self.assertNotIn("rancesa.ald", r1_line)
        self.assertNotIn("rancega.ald", r1_line)

        # rance02 and rance06 aliases
        r02_line = re.search(r'\("rance02", \[[^\]]+\]\)', aliases_block).group(0)
        self.assertIn("ランス０２", r02_line)
        r06_line = re.search(r'\("rance06", \[[^\]]+\]\)', aliases_block).group(0)
        self.assertIn('"rance6"', r06_line)

        # 6 ADISK.DAT CRC32s matching GameCatalog.kt
        gc = (ANDROID_SRC / "GameCatalog.kt").read_text(encoding="utf-8")
        for crc in ("0xa43fb4b6", "0xc39ca9e6", "0x04d24d1e", "0x07e46c85", "0x45951cab", "0x9418eef5"):
            self.assertIn(crc, gc.lower())
            self.assertIn(crc, gl.lower())

    # ------------------------------------------------------------------
    # Item 3: P1 Android install directory parity & AliceCN save match
    # ------------------------------------------------------------------
    def test_item3_android_resolve_install_dir_and_alicecn_match(self):
        online_installer = (ANDROID_SRC / "OnlineInstaller.kt").read_text(encoding="utf-8")
        gc = (ANDROID_SRC / "GameCatalog.kt").read_text(encoding="utf-8")

        fn_body = online_installer.split("fun resolveInstallDirBaseName(", 1)[1].split("\n    fun ", 1)[0]
        early_expected = {
            "ID_RANCE10": "Rance10",
            "ID_DOHNA_DOHNA": "dohnadohna",
            "ID_EVENICLE": "Evenicle",
            "ID_EVENICLE2": "Evenicle2",
            "ID_SENGOKU_RANCE": "SengokuRance",
            "ID_RANCE01": "Rance01",
            "ID_RANCE02": "Rance02",
            "ID_RANCE03": "Rance03",
            "ID_RANCE08": "Rance08",
            "ID_RANCE09": "Rance09",
            "ID_KICHIKUOU": "KichikuRance",
            "ID_DAIAKUJI": "Daiakuji",
            "ID_ESCALAYER": "Escalayer",
            "ID_ESCALAYER_REBOOT": "EscalayerReboot",
            "ID_IXSEAL": "Ixseal",
        }
        for const_name, expected_dir in early_expected.items():
            pattern = rf'classifiedStagedId == GameCatalog\.{const_name} -> sanitizedManifestBase\.ifEmpty \{{ "{expected_dir}" \}}'
            self.assertRegex(fn_body, pattern)
            # Also cross-check against GameCatalog.kt defaultDirName
            desc_m = re.search(rf'{const_name} -> GameDescriptor\((.*?)\n {{12}}\)', gc, re.S)
            self.assertIsNotNone(desc_m, f"Missing GameDescriptor for {const_name}")
            self.assertIn(f'defaultDirName = "{expected_dir}"', desc_m.group(1))

        # GameCatalog.kt AliceCN save dir match for sengokurance
        self.assertIn('lowerDir == "alicecn" || lowerDir.startsWith("alicecn-")', gc)

    # ------------------------------------------------------------------
    # Item 4: P1 iOS & Switch SaveFolder + Switch HUB_SAVE_EXPORT_PC
    # ------------------------------------------------------------------
    def test_item4_switch_save_key_save_folder_import_and_pc_export_with_ain(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            exe = root / "test_switch_saves"
            hub_objs = []
            for name in ("model.c", "save_store.c", "save_codec.c", "fs_util.c"):
                obj = root / (name + ".o")
                subprocess.run(
                    [
                        "cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-ffunction-sections", "-fdata-sections",
                        "-I", str(HUB), "-I", str(ENGINE / "include"), "-I", str(SYS4 / "include"),
                        "-c", str(HUB / name), "-o", str(obj),
                    ],
                    check=True,
                )
                hub_objs.append(str(obj))
            subprocess.run(
                [
                    "cc", "-std=gnu11",
                    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                    "-Datomic_save_open=hub_codec_atomic_save_open",
                    "-Datomic_save_commit=hub_codec_atomic_save_commit",
                    "-Datomic_save_abort=hub_codec_atomic_save_abort",
                    "-I", str(HUB), "-I", str(ENGINE / "include"), "-I", str(SYS4 / "include"),
                    *hub_objs,
                    str(ENGINE / "src/savedata.c"),
                    *(str(SYS4 / "src" / s) for s in (
                        "ain.c", "buffer.c", "file.c", "hashtable.c", "instructions.c",
                        "mt19937int.c", "savefile.c", "string.c", "system.c", "utfsjis.c", "zlib.c",
                    )),
                    "-x", "c", "-", "-lz", "-o", str(exe),
                ],
                input=r"""
#include "model.h"
#include "save_store.h"
#include "system4/savefile.h"
#include "system4/string.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void put_bytes(const char *path, const void *data, size_t len)
{
    FILE *fp = fopen(path, "wb");
    assert(fp && (len == 0 || fwrite(data, 1, len, fp) == len) && !fclose(fp));
}

static void put_text(const char *path, const char *text)
{
    put_bytes(path, text, strlen(text));
}

static void expect_text(const char *path, const char *text)
{
    char buf[128] = {0};
    FILE *fp = fopen(path, "rb");
    assert(fp);
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    assert(n == strlen(text) && !memcmp(buf, text, n));
}

int main(void)
{
    assert(!mkdir("xsystem4", 0700));
    assert(!mkdir("xsystem4/games", 0700));
    assert(!mkdir("xsystem4/saves", 0700));

    // 1. Single-entry default game with SaveFolder = "SaveDataCN" preserves existing
    //    .launch-entries/<name>/<fnv1a64> save key (never strands pre-update saves)
    assert(!mkdir("xsystem4/games/EvenicleRance", 0700));
    put_text("xsystem4/games/EvenicleRance/AliceStart.ini",
             "CodeName = \"EvenicleRance.ain\"\nSaveFolder = \"SaveDataCN\"\n");
    put_text("xsystem4/games/EvenicleRance/EvenicleRance.ain", "");
    HubGame evr;
    assert(hub_game_init(&evr, HUB_XSYSTEM4, "EvenicleRance"));
    char save_path[HUB_PATH_SIZE], mgr_path[HUB_PATH_SIZE], folder[HUB_NAME_SIZE], script[HUB_PATH_SIZE];
    char evr_save_path[HUB_PATH_SIZE], evr_mgr_path[HUB_PATH_SIZE];
    assert(hub_game_save_path(".", &evr, evr_save_path));
    assert(strstr(evr_save_path, "./xsystem4/saves/.launch-entries/EvenicleRance/") == evr_save_path);
    assert(hub_game_save_manager_path(".", &evr, evr_mgr_path));
    assert(strstr(evr_mgr_path, "./save-manager/xsystem4/.launch-entries/EvenicleRance/") == evr_mgr_path);
    assert(hub_system4_save_folder("xsystem4/games/EvenicleRance", folder));
    assert(!strcmp(folder, "SaveDataCN"));
    assert(hub_system4_active_script("xsystem4/games/EvenicleRance", script));
    assert(!strcmp(script, "xsystem4/games/EvenicleRance/EvenicleRance.ain"));

    // 2. Haruurare with haru_c.ain + AliceStart2.ini (SaveFolder = "SaveData_C")
    //    vs System40.ini (SaveFolder = "SaveData") vs CustomMod.ini (SaveFolder = "SaveDataMod"):
    //    all three MUST have distinct save keys and NEVER collide!
    assert(!mkdir("xsystem4/games/Haruurare", 0700));
    put_text("xsystem4/games/Haruurare/System40.ini",
             "CodeName = \"haru.ain\"\nSaveFolder = \"SaveData\"\n");
    put_text("xsystem4/games/Haruurare/AliceStart2.ini",
             "CodeName = \"haru_c.ain\"\nSaveFolder = \"SaveData_C\"\n");
    put_text("xsystem4/games/Haruurare/CustomMod.ini",
             "CodeName = \"haru_c.ain\"\nSaveFolder = \"SaveDataMod\"\n");
    put_text("xsystem4/games/Haruurare/haru.ain", "");
    put_text("xsystem4/games/Haruurare/haru_c.ain", "");
    HubGame haru;
    assert(hub_game_init(&haru, HUB_XSYSTEM4, "Haruurare"));
    char haru_c_save[HUB_PATH_SIZE], haru_c_mgr[HUB_PATH_SIZE], haru_jp_save[HUB_PATH_SIZE], haru_mod_save[HUB_PATH_SIZE];
    assert(hub_game_save_path(".", &haru, haru_c_save));
    assert(strstr(haru_c_save, "./xsystem4/saves/.launch-entries/Haruurare/") == haru_c_save);
    assert(hub_game_save_manager_path(".", &haru, haru_c_mgr));
    assert(hub_system4_save_folder("xsystem4/games/Haruurare", folder));
    assert(!strcmp(folder, "SaveData_C"));
    assert(hub_system4_active_script("xsystem4/games/Haruurare", script));
    assert(!strcmp(script, "xsystem4/games/Haruurare/haru_c.ain"));

    // Selecting System40.ini (SaveData) gets base key ./xsystem4/saves/Haruurare, distinct from SaveData_C!
    assert(hub_game_boot_set("xsystem4/games/Haruurare", "System40.ini"));
    assert(hub_game_save_path(".", &haru, haru_jp_save));
    assert(!strcmp(haru_jp_save, "./xsystem4/saves/Haruurare"));
    assert(strcmp(haru_jp_save, haru_c_save) != 0);
    assert(hub_system4_save_folder("xsystem4/games/Haruurare", folder));
    assert(!strcmp(folder, "SaveData"));
    assert(hub_system4_active_script("xsystem4/games/Haruurare", script));
    assert(!strcmp(script, "xsystem4/games/Haruurare/haru.ain"));

    // Selecting CustomMod.ini (SaveDataMod) gets a distinct .launch-entries hash key!
    assert(hub_game_boot_set("xsystem4/games/Haruurare", "CustomMod.ini"));
    assert(hub_game_save_path(".", &haru, haru_mod_save));
    assert(strstr(haru_mod_save, "./xsystem4/saves/.launch-entries/Haruurare/") == haru_mod_save);
    assert(strcmp(haru_mod_save, haru_c_save) != 0);
    assert(strcmp(haru_mod_save, haru_jp_save) != 0);
    assert(hub_game_boot_set("xsystem4/games/Haruurare", ""));
    assert(hub_game_save_path(".", &haru, save_path));
    assert(!strcmp(save_path, haru_c_save));

    // 3. HUB_SAVE_IMPORT unwraps Import/SaveDataCN & Import/SaveData_C into exact save_path
    atomic_bool cancelled = false;
    HubSaveResult res;
    char p[HUB_PATH_SIZE];
    assert(hub_save_run(".", &evr, HUB_SAVE_PREPARE, &cancelled, &res));
    snprintf(p, sizeof(p), "%s/Import/SaveDataCN", evr_mgr_path);
    assert(!mkdir(p, 0700));
    snprintf(p, sizeof(p), "%s/Import/SaveDataCN/slot01.asd", evr_mgr_path);
    put_text(p, "cn-save");
    assert(hub_save_run(".", &evr, HUB_SAVE_IMPORT, &cancelled, &res) && res.copied == 1);
    snprintf(p, sizeof(p), "%s/slot01.asd", evr_save_path);
    expect_text(p, "cn-save");

    assert(hub_save_run(".", &haru, HUB_SAVE_PREPARE, &cancelled, &res));
    snprintf(p, sizeof(p), "%s/Import/SaveData_C", haru_c_mgr);
    assert(!mkdir(p, 0700));
    snprintf(p, sizeof(p), "%s/Import/SaveData_C/slot02.asd", haru_c_mgr);
    put_text(p, "c-save");
    assert(hub_save_run(".", &haru, HUB_SAVE_IMPORT, &cancelled, &res) && res.copied == 1);
    snprintf(p, sizeof(p), "%s/slot02.asd", haru_c_save);
    expect_text(p, "c-save");

    // Verify HUB_SAVE_EXPORT_PC writes PC_SaveData_C for Haruurare too
    assert(hub_save_run(".", &haru, HUB_SAVE_EXPORT_PC, &cancelled, &res) && res.copied == 1);
    snprintf(p, sizeof(p), "%s/PC_SaveData_C/slot02.asd", haru_c_mgr);
    expect_text(p, "c-save");

    // 4. HUB_SAVE_EXPORT_PC uses savedata_export_pc_file_with_ain, folds .comment into gsave,
    //    decrypts encrypted RSM v7+, excludes .bak/.log/.comment/.mod.absent, and writes PC_<SaveFolder>!
    struct gsave *gs = gsave_create(7, "test", 1, "slot");
    snprintf(p, sizeof(p), "%s/gsave01.asd", evr_save_path);
    FILE *gfp = fopen(p, "wb");
    assert(gfp && gsave_write(gs, gfp, false, 1) == SAVEFILE_SUCCESS && !fclose(gfp));
    gsave_free(gs);
    snprintf(p, sizeof(p), "%s/gsave01.asd.comment", evr_save_path);
    put_text(p, "Folded Comment");
    snprintf(p, sizeof(p), "%s/GlobalSave.asd.mod.absent", evr_save_path);
    put_text(p, "");
    snprintf(p, sizeof(p), "%s/old.bak", evr_save_path);
    put_text(p, "bak");
    snprintf(p, sizeof(p), "%s/debug.log", evr_save_path);
    put_text(p, "log");

    // Encrypted RSM v7 resume save -> must be decrypted on PC export
    uint8_t rsm_payload[16] = {'R','S','M',0, 7,0,0,0, 1,2,3,4,5,6,7,8};
    struct savefile rsm_sf = { .buf = rsm_payload, .len = sizeof(rsm_payload), .encrypted = true, .compression_level = 1 };
    snprintf(p, sizeof(p), "%s/resume.rsm", evr_save_path);
    FILE *rfp = fopen(p, "wb");
    assert(rfp && savefile_write(&rsm_sf, rfp) == SAVEFILE_SUCCESS && !fclose(rfp));

    assert(hub_save_run(".", &evr, HUB_SAVE_EXPORT_PC, &cancelled, &res));
    // slot01.asd (plain copy), gsave01.asd (comment folded -> converted), resume.rsm (decrypted -> converted)
    assert(res.copied == 3);
    assert(res.converted == 2);
    snprintf(p, sizeof(p), "%s/PC_SaveDataCN/slot01.asd", evr_mgr_path);
    expect_text(p, "cn-save");
    snprintf(p, sizeof(p), "%s/PC_SaveDataCN/gsave01.asd.comment", evr_mgr_path);
    assert(access(p, F_OK) != 0);
    snprintf(p, sizeof(p), "%s/PC_SaveDataCN/GlobalSave.asd.mod.absent", evr_mgr_path);
    assert(access(p, F_OK) != 0);
    snprintf(p, sizeof(p), "%s/PC_SaveDataCN/old.bak", evr_mgr_path);
    assert(access(p, F_OK) != 0);
    snprintf(p, sizeof(p), "%s/PC_SaveDataCN/debug.log", evr_mgr_path);
    assert(access(p, F_OK) != 0);

    // Verify exported gsave01.asd now embeds the folded comment and resume.rsm is unencrypted!
    enum savefile_error err = SAVEFILE_SUCCESS;
    snprintf(p, sizeof(p), "%s/PC_SaveDataCN/resume.rsm", evr_mgr_path);
    struct savefile *out_rsm = savefile_read(p, &err);
    assert(out_rsm && !out_rsm->encrypted);
    savefile_free(out_rsm);

    snprintf(p, sizeof(p), "%s/PC_SaveDataCN/gsave01.asd", evr_mgr_path);
    struct gsave *out_gs = gsave_read(p, &err);
    assert(out_gs && out_gs->nr_comments > 0 && out_gs->comments[0] &&
           strstr(out_gs->comments[0]->text, "Folded Comment") != NULL);
    gsave_free(out_gs);

    (void)mgr_path;

    return 0;
}
""",
                text=True,
                check=True,
            )
            subprocess.run([str(exe)], cwd=root, check=True)

    def test_item4_ios_save_manager_supports_non_default_save_folders_and_extensions(self):
        sm = (IOS_APP / "SaveManager.swift").read_text(encoding="utf-8")
        smv = (IOS_APP / "SaveManagerView.swift").read_text(encoding="utf-8")

        self.assertIn("static func resolveSaveFolder(", sm)
        for name in ("SaveData", "SaveDataDSR", "SaveDataCN", "SaveData_C"):
            self.assertIn(f'"{name}"', sm)
        for ext in ('"asd"', '"sav"', '"rsm"', '"vsf"', '"msgskip"'):
            self.assertIn(ext, sm)
        # Only known save .dat filenames (never arbitrary ADISK.DAT) are treated as loose saves
        self.assertIn("static func isLooseSaveFileName(", sm)
        self.assertNotIn('["asd", "sav", "rsm", "vsf", "msgskip", "dat"]', sm)
        for known_dat in ('"asleep.dat"', '"msgskip.dat"', '"keyfile.dat"', '"key0104.dat"', '"asleep_"'):
            self.assertIn(known_dat, sm)
        # resolveSaveFolder must NOT suppress SaveDataDSR when the launched INI specifies it
        resolve_fn = sm.split("static func resolveSaveFolder(", 1)[1].split("static func isLooseSaveFileName(", 1)[0]
        self.assertNotIn('if folder?.caseInsensitiveCompare("SaveDataDSR") == .orderedSame { continue }', resolve_fn)
        self.assertIn('let wrapper = pc ? saveFolder : "SaveData"', sm)
        self.assertIn("GameLibrary.launchInfo(game.url, bootEntry: configured)", smv)
        self.assertIn("SaveManager.resolveSaveFolder(", smv)
        self.assertIn("saveFolder: saveFolderName", smv)

    # ------------------------------------------------------------------
    # Item 5: P1 Android SaveImportPaths, SaveManagerActivity, XSystem4Activity
    # ------------------------------------------------------------------
    def test_item5_android_save_import_and_system3_guards(self):
        sip = (ANDROID_SRC / "SaveImportPaths.kt").read_text(encoding="utf-8")
        self.assertIn('Regex("^ASLEEP(?:_[A-Z])?\\\\.DAT$", RegexOption.IGNORE_CASE)', sip)
        self.assertIn('Regex("^MSGSKIP\\\\.DAT$", RegexOption.IGNORE_CASE)', sip)
        self.assertIn('name.equals("msgskip.dat", ignoreCase = true)', sip)

        sma = (ANDROID_SRC / "SaveManagerActivity.kt").read_text(encoding="utf-8")
        self.assertIn('"msgskip.dat"', sma)

        act = (ANDROID_SRC / "XSystem4Activity.kt").read_text(encoding="utf-8")
        on_new_intent = act.split("override fun onNewIntent(", 1)[1].split("override fun onPause(", 1)[0]
        self.assertIn("if (isLegacySystem3Game) return", on_new_intent)
        dispatch_key = act.split("override fun dispatchKeyEvent(", 1)[1].split("private fun restoreWindowAndFrameRate(", 1)[0]
        self.assertIn("if (isLegacySystem3Game) return super.dispatchKeyEvent(event)", dispatch_key)
        dispatch_motion = act.split("override fun dispatchGenericMotionEvent(", 1)[1].split("override fun onKeyDown(", 1)[0]
        self.assertIn("if (isLegacySystem3Game) return super.dispatchGenericMotionEvent(ev)", dispatch_motion)

    # ------------------------------------------------------------------
    # Item 6: P1/P2 iOS & Switch System 3.5 wiring & modifier UI parity
    # ------------------------------------------------------------------
    def test_item6_switch_launcher_resolves_xsystem35_modifier_game(self):
        launcher = (SWITCH_DIR / "launcher.c").read_text(encoding="utf-8")
        block = launcher.split("#if defined(ALICEHUB_SWITCH_XSYSTEM35)\nstatic bool contains_icase", 1)[1]
        block = "static bool contains_icase" + block.split("\n#endif\n", 1)[0]
        self.assertIn('setenv("XSYSTEM35_MODIFIER_GAME", mod_game, 1);', launcher)
        self.assertIn('unsetenv("XSYSTEM35_MODIFIER_GAME");', launcher)

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            exe = root / "test_s35_mod"
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-x", "c", "-", "-o", str(exe)],
                input=r"""
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#define HUB_PATH_SIZE 512
""" + block + r"""
static void put(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");
    assert(fp && fputs(text, fp) >= 0 && !fclose(fp));
}
int main(void)
{
    char buf[64];
    assert(!mkdir("CustomDir", 0700));
    put("CustomDir/.alicehub-online-v1", "kichikuou\n");
    assert(!strcmp(resolve_xsystem35_modifier_game("CustomDir", buf), "kichikuou"));

    assert(!mkdir("Kaerunyo", 0700));
    assert(!strcmp(resolve_xsystem35_modifier_game("Kaerunyo", buf), "kaerunyo"));

    assert(!mkdir("LocalImport", 0700));
    put("LocalImport/鬼畜王SA.ALD", "");
    assert(!strcmp(resolve_xsystem35_modifier_game("LocalImport", buf), "kichikuou"));

    assert(!mkdir("UnknownGame", 0700));
    put("UnknownGame/FooSA.ALD", "");
    assert(resolve_xsystem35_modifier_game("UnknownGame", buf) == NULL);
    return 0;
}
""",
                text=True,
                check=True,
            )
            subprocess.run([str(exe)], cwd=root, check=True)

    def test_item6_ios_switch_android_modifier_parity(self):
        gl = (IOS_APP / "GameLibrary.swift").read_text(encoding="utf-8")
        self.assertIn("ModifierBridge.clearDisplayScaleGame()", gl)
        self.assertIn('setenv("XSYSTEM35_MODIFIER_GAME"', gl)
        self.assertIn('unsetenv("XSYSTEM35_MODIFIER_GAME")', gl)

        mv = (IOS_APP / "ModifierView.swift").read_text(encoding="utf-8")
        self.assertIn("if !system35, let gameID = ModifierBridge.displayScaleGame", mv)

        gmr = (ANDROID_SRC / "GenericModifierRegistry.kt").read_text(encoding="utf-8")
        ixseal_kt = gmr.split("id = GameCatalog.ID_IXSEAL", 1)[1].split("id = GameCatalog.ID_PASCHA3", 1)[0]
        self.assertIn('"魔石板拉满 (12)"', ixseal_kt)
        self.assertIn('"gold" to 12', ixseal_kt)
        self.assertIn('"cheat" to "maxItems"', ixseal_kt)
        self.assertNotIn('"addGold" to 50_000', ixseal_kt)

        mgp = (IOS_APP / "ModifierGamePanels.swift").read_text(encoding="utf-8")
        ixseal_sw = mgp.split('ModifierGamePanel("ixseal"', 1)[1].split('ModifierGamePanel("pascha3"', 1)[0]
        self.assertIn('cheat("魔石板拉满 (12)", ["gold": 12])', ixseal_sw)
        self.assertIn('cheat("补满消耗品", ["cheat": "maxItems"])', ixseal_sw)
        self.assertNotIn("50_000", ixseal_sw)

        t3_sw = mgp.split('ModifierGamePanel("tsumamigui3"', 1)[1].split('ModifierGamePanel("maman"', 1)[0]
        self.assertIn('cheat("回满精力并清除情敌", ["cheat": "fullHeal"])', t3_sw)

        ts_sw = mgp.split('ModifierGamePanel("tsumashibori"', 1)[1].split("okusamaPanel(", 1)[0]
        self.assertIn('"女主满好感零嫉妒"', ts_sw)

        dt_sw = mgp.split('ModifierGamePanel("daiteikoku"', 1)[1].split('ModifierGamePanel("galzoo"', 1)[0]
        self.assertIn('["addResource": 100_000]', dt_sw)
        self.assertIn('["addTechnology": 100_000]', dt_sw)
        self.assertIn('cheat("全员满血并重置行动", ["cheat": "fullHeal"])', dt_sw)

        sw_menu4 = (HUB / "runtime_menu_system4.c").read_text(encoding="utf-8")
        self.assertIn('"Max affection, zero jealousy"', sw_menu4)
        self.assertNotIn('"Zero misfortune & jealousy"', sw_menu4)

    # ------------------------------------------------------------------
    # Item 7: P2 iOS 52 missing localized game titles + strict .strings check
    # ------------------------------------------------------------------
    def test_item7_ios_all_170_game_titles_and_strings_syntax_valid(self):
        en_map = parse_apple_strings_strict(IOS_APP / "en.lproj/Localizable.strings")
        ja_map = parse_apple_strings_strict(IOS_APP / "ja.lproj/Localizable.strings")

        oc = (IOS_APP / "OnlineGameCatalog.swift").read_text(encoding="utf-8")
        ios_games = re.findall(r'Self\(id:\s*"([^"]+)",\s*title:\s*"([^"]+)",\s*directory:\s*"([^"]+)"', oc)
        self.assertEqual(len(ios_games), 170)

        missing_en = [gid for gid, title, _ in ios_games if title not in en_map]
        missing_ja = [gid for gid, title, _ in ios_games if title not in ja_map]
        self.assertEqual(missing_en, [], f"Missing EN game titles: {missing_en}")
        self.assertEqual(missing_ja, [], f"Missing JA game titles: {missing_ja}")

        # Verify all hardcoded GameLibrary.swift displayNames are localized too
        gl = (IOS_APP / "GameLibrary.swift").read_text(encoding="utf-8")
        for name in set(re.findall(r'displayName\s*=\s*"([^"]+)"', gl)):
            self.assertIn(name, en_map, f"Missing EN GameLibrary displayName: {name}")
            self.assertIn(name, ja_map, f"Missing JA GameLibrary displayName: {name}")

        # Cross-check newly added 52 entries against Android strings.xml
        en_xml = {e.attrib["name"]: e.text.replace("\\'", "'") for e in ET.parse(ANDROID_RES / "values/strings.xml").findall("string")}
        ja_xml = {e.attrib["name"]: e.text.replace("\\'", "'") for e in ET.parse(ANDROID_RES / "values-ja/strings.xml").findall("string")}
        gc = (ANDROID_SRC / "GameCatalog.kt").read_text(encoding="utf-8")
        id_map = dict(re.findall(r'const val (ID_\w+)\s*=\s*"([^"]+)"', gc))
        slug_to_res = {}
        for m in re.finditer(r'GameDescriptor\((.*?)\n\s*\)', gc, re.S):
            blk = m.group(1)
            id_m = re.search(r'id\s*=\s*(ID_\w+)', blk)
            title_m = re.search(r'R\.string\.(\w+)', blk)
            if id_m and title_m:
                slug_to_res[id_map[id_m.group(1)]] = title_m.group(1)

        spot_checks = ("bladebriders", "dorapeko", "rance1classic", "dpssgfahren", "hiyoko2", "momoiroguardian")
        by_id = {gid: title for gid, title, _ in ios_games}
        for slug in spot_checks:
            key = by_id[slug]
            res = slug_to_res[slug]
            self.assertEqual(en_map[key], en_xml[res])
            self.assertEqual(ja_map[key], ja_xml[res])

        # Verify updated modifier UI labels exist in EN and JA
        for mod_label in (
            "魔石板拉满 (12)",
            "回满精力并清除情敌",
            "女主满好感零嫉妒",
            "资源 +10 万",
            "技术 +10 万",
            "全员满血并重置行动",
        ):
            self.assertIn(mod_label, en_map)
            self.assertIn(mod_label, ja_map)


if __name__ == "__main__":
    unittest.main()
