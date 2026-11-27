#!/usr/bin/env python3
"""Host C regression tests for AJP decode, EX hash lookup, and MainEXFile_v12 name_list optimizations."""

import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NATIVE_TEST_DIR = Path(__file__).resolve().parent
if str(NATIVE_TEST_DIR) not in sys.path:
    sys.path.insert(0, str(NATIVE_TEST_DIR))

from test_evenicle2_regressions import (
    LIBSYS4_CORE,
    _ex_block,
    _ex_leaf,
    _ex_list,
    _ex_pstr,
    _ex_tree,
    build_extf,
    compile_and_run,
)

DOHNA_DIR = Path("/root/Rance10_CN/DohnaDohna/多娜多娜 一起干坏事吧")
DOHNA_CG_AFA = DOHNA_DIR / "dohnadohnaCG.afa"
DOHNA_EX = DOHNA_DIR / "dohnadohnaEx.ex"
RANCE10_EX = Path("/root/Rance10_CN/Rance10EX.ex")


def pkg_libs(*pkgs: str) -> list[str]:
    return subprocess.check_output(["pkg-config", "--cflags", "--libs", *pkgs], text=True).split()


class TestAjpExPerfRegressions(unittest.TestCase):
    def test_ajp_extract_bit_exact_corrupt_and_multithreaded(self):
        harness = r"""
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <turbojpeg.h>
#include <webp/decode.h>
#include <zlib.h>

#include "system4/little_endian.h"
#include "system4.h"
#include "system4/ajp.h"
#include "system4/cg.h"
#include "system4/pms.h"
#include "system4/webp.h"
#include "system4/zlib.h"

static const uint8_t test_ajp_key[16] = {
    0x5d, 0x91, 0xae, 0x87,
    0x4a, 0x56, 0x41, 0xcd,
    0x83, 0xec, 0x4c, 0x92,
    0xb5, 0xcb, 0x16, 0x34
};

static void xor_first16(uint8_t *buf, size_t n) {
    for (size_t i = 0; i < 16 && i < n; i++)
        buf[i] ^= test_ajp_key[i];
}

/* Reference implementation matching the original TJPF_RGB + load_mask behavior */
static uint8_t *ref_ajp_extract(const uint8_t *data, size_t size, int *out_w, int *out_h) {
    uint32_t w = LittleEndian_getDW(data, 12);
    uint32_t h = LittleEndian_getDW(data, 16);
    uint32_t joff = LittleEndian_getDW(data, 20);
    uint32_t jsz = LittleEndian_getDW(data, 24);
    uint32_t moff = LittleEndian_getDW(data, 28);
    uint32_t msz = LittleEndian_getDW(data, 32);

    uint8_t *jbuf = malloc(jsz);
    memcpy(jbuf, data + joff, jsz);
    xor_first16(jbuf, jsz);

    tjhandle tj = tjInitDecompress();
    assert(tj != NULL);
    int dw = 0, dh = 0, ss = 0;
    assert(tjDecompressHeader2(tj, jbuf, jsz, &dw, &dh, &ss) == 0);
    assert((uint32_t)dw == w && (uint32_t)dh == h);
    size_t npix = (size_t)w * (size_t)h;
    uint8_t *rgb = malloc(npix * 3);
    assert(tjDecompress2(tj, jbuf, jsz, rgb, dw, 0, dh, TJPF_RGB, TJFLAG_FASTDCT) == 0);
    tjDestroy(tj);
    free(jbuf);

    uint8_t *mask = NULL;
    if (msz > 0) {
        uint8_t *mbuf = malloc(msz);
        memcpy(mbuf, data + moff, msz);
        xor_first16(mbuf, msz);
        if (msz >= 4 && pms8_checkfmt(mbuf)) {
            mask = pms_extract_mask(mbuf, msz);
        } else if (msz >= 12 && webp_checkfmt(mbuf)) {
            int ww = 0, wh = 0;
            uint8_t *tmp = WebPDecodeRGBA(mbuf, msz, &ww, &wh);
            if (tmp && (uint32_t)ww == w && (uint32_t)wh == h) {
                mask = malloc(npix);
                for (size_t i = 0; i < npix; i++)
                    mask[i] = tmp[i * 4 + 3];
            }
            WebPFree(tmp);
        } else if (mbuf[0] == 0x78) {
            mask = malloc(npix);
            if (!zlib_decompress_exact(mask, npix, mbuf, msz)) {
                free(mask);
                mask = NULL;
            }
        }
        free(mbuf);
    }
    if (!mask) {
        mask = malloc(npix);
        memset(mask, 0xFF, npix);
    }

    uint8_t *rgba = malloc(npix * 4);
    for (size_t i = 0; i < npix; i++) {
        rgba[i * 4 + 0] = rgb[i * 3 + 0];
        rgba[i * 4 + 1] = rgb[i * 3 + 1];
        rgba[i * 4 + 2] = rgb[i * 3 + 2];
        rgba[i * 4 + 3] = mask[i];
    }
    free(rgb);
    free(mask);
    *out_w = (int)w;
    *out_h = (int)h;
    return rgba;
}

static uint8_t *make_jpeg(int w, int h, int subsamp, unsigned seed, size_t *out_sz) {
    size_t npix = (size_t)w * (size_t)h;
    uint8_t *rgb = malloc(npix * 3);
    for (size_t i = 0; i < npix; i++) {
        rgb[i * 3 + 0] = (uint8_t)((i * 7 + seed * 13) & 0xFF);
        rgb[i * 3 + 1] = (uint8_t)((i * 11 + seed * 29) & 0xFF);
        rgb[i * 3 + 2] = (uint8_t)((i * 17 + seed * 43) & 0xFF);
    }
    tjhandle comp = tjInitCompress();
    assert(comp != NULL);
    unsigned char *jbuf = NULL;
    unsigned long jsz = 0;
    assert(tjCompress2(comp, rgb, w, 0, h, TJPF_RGB, &jbuf, &jsz, subsamp, 85, 0) == 0);
    tjDestroy(comp);
    free(rgb);

    uint8_t *ret = malloc(jsz);
    memcpy(ret, jbuf, jsz);
    tjFree(jbuf);
    *out_sz = (size_t)jsz;
    return ret;
}

/* Build a minimal valid PMS8 (uncompressed literal bytes <= 0xf7) alpha mask */
static uint8_t *make_pms8_mask(int w, int h, unsigned seed, size_t *out_sz) {
    size_t npix = (size_t)w * (size_t)h;
    size_t hdr = 64;
    uint8_t *buf = calloc(1, hdr + npix);
    buf[0] = 'P';
    buf[1] = 'M';
    LittleEndian_putW(buf, 2, 1);
    LittleEndian_putW(buf, 4, (uint16_t)hdr);
    buf[6] = 8; /* bpp = 8 */
    LittleEndian_putDW(buf, 24, (uint32_t)w);
    LittleEndian_putDW(buf, 28, (uint32_t)h);
    LittleEndian_putDW(buf, 32, (uint32_t)hdr); /* dp */
    for (size_t i = 0; i < npix; i++) {
        /* keep <= 0xf7 so every byte is a literal in pms8_extract */
        buf[hdr + i] = (uint8_t)((i * 19 + seed * 37) % 0xf8);
    }
    *out_sz = hdr + npix;
    return buf;
}

static uint8_t *make_zlib_mask(int w, int h, unsigned seed, size_t *out_sz) {
    size_t npix = (size_t)w * (size_t)h;
    uint8_t *raw = malloc(npix);
    for (size_t i = 0; i < npix; i++)
        raw[i] = (uint8_t)((i * 23 + seed * 5) & 0xFF);
    uLongf dest_len = compressBound((uLong)npix);
    uint8_t *comp = malloc(dest_len);
    assert(compress2(comp, &dest_len, raw, (uLong)npix, Z_DEFAULT_COMPRESSION) == Z_OK);
    free(raw);
    *out_sz = (size_t)dest_len;
    return comp;
}

static uint8_t *make_ajp(int w, int h, const uint8_t *jpeg, size_t jsz,
                         const uint8_t *mask, size_t msz, size_t *out_sz) {
    uint32_t joff = 36;
    uint32_t moff = joff + (uint32_t)jsz;
    size_t total = (size_t)moff + msz;
    uint8_t *buf = calloc(1, total);
    buf[0] = 'A'; buf[1] = 'J'; buf[2] = 'P'; buf[3] = 0;
    LittleEndian_putDW(buf, 12, (uint32_t)w);
    LittleEndian_putDW(buf, 16, (uint32_t)h);
    LittleEndian_putDW(buf, 20, joff);
    LittleEndian_putDW(buf, 24, (uint32_t)jsz);
    LittleEndian_putDW(buf, 28, msz ? moff : 0);
    LittleEndian_putDW(buf, 32, (uint32_t)msz);
    memcpy(buf + joff, jpeg, jsz);
    xor_first16(buf + joff, jsz);
    if (msz > 0) {
        memcpy(buf + moff, mask, msz);
        xor_first16(buf + moff, msz);
    }
    *out_sz = total;
    return buf;
}

struct thread_arg {
    const uint8_t *ajp_rgb;
    size_t sz_rgb;
    const uint8_t *ref_rgb;
    size_t ref_rgb_len;
    const uint8_t *ajp_alpha;
    size_t sz_alpha;
    const uint8_t *ref_alpha;
    size_t ref_alpha_len;
    const uint8_t *ajp_corrupt;
    size_t sz_corrupt;
};

static void *worker_fn(void *argp) {
    const struct thread_arg *a = (const struct thread_arg *)argp;
    for (int i = 0; i < 40; i++) {
        struct cg cg = {0};
        if ((i % 7) == 3) {
            /* Corrupt JPEG on the same thread; next call must still succeed cleanly */
            ajp_extract(a->ajp_corrupt, a->sz_corrupt, &cg);
            assert(cg.pixels == NULL);
            continue;
        }
        if (i & 1) {
            ajp_extract(a->ajp_alpha, a->sz_alpha, &cg);
            assert(cg.pixels != NULL);
            assert(cg.type == ALCG_AJP);
            assert(memcmp(cg.pixels, a->ref_alpha, a->ref_alpha_len) == 0);
            free(cg.pixels);
        } else {
            ajp_extract(a->ajp_rgb, a->sz_rgb, &cg);
            assert(cg.pixels != NULL);
            assert(cg.type == ALCG_AJP);
            assert(memcmp(cg.pixels, a->ref_rgb, a->ref_rgb_len) == 0);
            free(cg.pixels);
        }
    }
    return NULL;
}

int main(void) {
    /* 1. Bit-exact checks across even/odd dimensions, subsampling modes, and mask types */
    struct { int w, h, subsamp, mask_kind; } cases[] = {
        { 64, 48, TJSAMP_420, 0 }, /* RGB-only even */
        { 37, 29, TJSAMP_444, 0 }, /* RGB-only odd */
        {  1,  1, TJSAMP_444, 0 }, /* 1x1 RGB-only */
        { 17, 13, TJSAMP_GRAY, 0 }, /* Grayscale JPEG RGB-only */
        { 64, 48, TJSAMP_420, 1 }, /* zlib alpha even */
        { 53, 31, TJSAMP_422, 1 }, /* zlib alpha odd */
        { 40, 30, TJSAMP_420, 2 }, /* PMS8 alpha even */
        { 25, 19, TJSAMP_444, 2 }, /* PMS8 alpha odd */
        { 32, 24, TJSAMP_420, 3 }, /* corrupt zlib mask -> fallback 0xFF alpha */
        { 32, 24, TJSAMP_420, 4 }, /* unsupported mask magic -> fallback 0xFF alpha */
    };

    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        size_t jsz = 0, msz = 0, asz = 0;
        uint8_t *jpeg = make_jpeg(cases[c].w, cases[c].h, cases[c].subsamp, (unsigned)c + 1, &jsz);
        uint8_t *mask = NULL;
        if (cases[c].mask_kind == 1)
            mask = make_zlib_mask(cases[c].w, cases[c].h, (unsigned)c + 1, &msz);
        else if (cases[c].mask_kind == 2)
            mask = make_pms8_mask(cases[c].w, cases[c].h, (unsigned)c + 1, &msz);
        else if (cases[c].mask_kind == 3) {
            /* Starts with 0x78 (zlib header) but truncated/invalid payload */
            msz = 12;
            mask = malloc(msz);
            memset(mask, 0xAA, msz);
            mask[0] = 0x78;
            mask[1] = 0x9c;
        } else if (cases[c].mask_kind == 4) {
            msz = 8;
            mask = malloc(msz);
            memcpy(mask, "NOPE1234", 8);
        }

        uint8_t *ajp = make_ajp(cases[c].w, cases[c].h, jpeg, jsz, mask, msz, &asz);
        int rw = 0, rh = 0;
        uint8_t *ref = ref_ajp_extract(ajp, asz, &rw, &rh);

        struct cg cg = {0};
        ajp_extract(ajp, asz, &cg);
        assert(cg.pixels != NULL);
        assert(cg.type == ALCG_AJP);
        assert(cg.metrics.w == rw && cg.metrics.h == rh);
        assert(cg.metrics.has_alpha == (msz > 0));
        size_t nbytes = (size_t)rw * (size_t)rh * 4;
        assert(memcmp(cg.pixels, ref, nbytes) == 0);
        if (cases[c].mask_kind == 0 || cases[c].mask_kind == 3 || cases[c].mask_kind == 4) {
            const uint8_t *px = (const uint8_t *)cg.pixels;
            for (size_t p = 0; p < (size_t)rw * (size_t)rh; p++)
                assert(px[p * 4 + 3] == 0xFF);
        }

        free(cg.pixels);
        free(ref);
        free(ajp);
        free(mask);
        free(jpeg);
    }

    /* 2. Large JPEG exceeding 512 KiB TLS scratch cap (exercises heap scratch path) */
    {
        int w = 1024, h = 768;
        size_t npix = (size_t)w * (size_t)h;
        uint8_t *rgb = malloc(npix * 3);
        uint32_t rng = 0x12345678u;
        for (size_t i = 0; i < npix * 3; i++) {
            rng = rng * 1664525u + 1013904223u;
            rgb[i] = (uint8_t)(rng >> 24);
        }
        tjhandle comp = tjInitCompress();
        unsigned char *jbuf = NULL;
        unsigned long jsz = 0;
        assert(tjCompress2(comp, rgb, w, 0, h, TJPF_RGB, &jbuf, &jsz, TJSAMP_444, 100, 0) == 0);
        tjDestroy(comp);
        free(rgb);
        assert(jsz > 512u * 1024u);

        size_t asz = 0;
        uint8_t *ajp = make_ajp(w, h, jbuf, (size_t)jsz, NULL, 0, &asz);
        tjFree(jbuf);
        int rw = 0, rh = 0;
        uint8_t *ref = ref_ajp_extract(ajp, asz, &rw, &rh);
        struct cg cg = {0};
        ajp_extract(ajp, asz, &cg);
        assert(cg.pixels != NULL);
        assert(memcmp(cg.pixels, ref, (size_t)w * (size_t)h * 4) == 0);
        free(cg.pixels);
        free(ref);
        free(ajp);
    }

    /* 3. Corrupt / truncated inputs do not crash or poison the thread-local tjhandle */
    {
        struct cg cg = {0};
        ajp_extract(NULL, 0, &cg);
        assert(cg.pixels == NULL);

        uint8_t tiny[10] = {'A', 'J', 'P', 0};
        ajp_extract(tiny, sizeof(tiny), &cg);
        assert(cg.pixels == NULL);

        uint8_t bad_dims[36] = {'A', 'J', 'P', 0};
        LittleEndian_putDW(bad_dims, 12, 0);
        LittleEndian_putDW(bad_dims, 16, 10);
        ajp_extract(bad_dims, sizeof(bad_dims), &cg);
        assert(cg.pixels == NULL);

        /* Out-of-bounds jpeg range */
        uint8_t bad_range[40] = {'A', 'J', 'P', 0};
        LittleEndian_putDW(bad_range, 12, 16);
        LittleEndian_putDW(bad_range, 16, 16);
        LittleEndian_putDW(bad_range, 20, 36);
        LittleEndian_putDW(bad_range, 24, 999);
        ajp_extract(bad_range, sizeof(bad_range), &cg);
        assert(cg.pixels == NULL);

        /* Dimension mismatch between AJP header (32x32) and JPEG (16x16) */
        size_t jsz = 0, asz = 0;
        uint8_t *jpeg16 = make_jpeg(16, 16, TJSAMP_420, 9, &jsz);
        uint8_t *mismatch = make_ajp(32, 32, jpeg16, jsz, NULL, 0, &asz);
        memset(&cg, 0, sizeof(cg));
        ajp_extract(mismatch, asz, &cg);
        assert(cg.pixels == NULL);
        free(mismatch);

        /* Corrupt JPEG bytes */
        uint8_t garbage[64];
        memset(garbage, 0x55, sizeof(garbage));
        uint8_t *corrupt_ajp = make_ajp(16, 16, garbage, sizeof(garbage), NULL, 0, &asz);
        memset(&cg, 0, sizeof(cg));
        ajp_extract(corrupt_ajp, asz, &cg);
        assert(cg.pixels == NULL);

        /* Immediately decode a valid AJP on the SAME thread after corrupt JPEG */
        uint8_t *good_ajp = make_ajp(16, 16, jpeg16, jsz, NULL, 0, &asz);
        int rw = 0, rh = 0;
        uint8_t *ref = ref_ajp_extract(good_ajp, asz, &rw, &rh);
        memset(&cg, 0, sizeof(cg));
        ajp_extract(good_ajp, asz, &cg);
        assert(cg.pixels != NULL);
        assert(memcmp(cg.pixels, ref, (size_t)rw * (size_t)rh * 4) == 0);
        free(cg.pixels);
        free(ref);

        /* 4. Multi-threaded concurrent stress test + thread-exit destructor cleanup */
        size_t msz = 0, asz_alpha = 0, jsz2 = 0;
        uint8_t *jpeg_odd = make_jpeg(35, 27, TJSAMP_420, 77, &jsz2);
        uint8_t *zmask = make_zlib_mask(35, 27, 77, &msz);
        uint8_t *ajp_alpha = make_ajp(35, 27, jpeg_odd, jsz2, zmask, msz, &asz_alpha);
        int aw = 0, ah = 0;
        uint8_t *ref_alpha = ref_ajp_extract(ajp_alpha, asz_alpha, &aw, &ah);
        int gw = 0, gh = 0;
        uint8_t *ref_good = ref_ajp_extract(good_ajp, asz, &gw, &gh);

        struct thread_arg targ = {
            .ajp_rgb = good_ajp,
            .sz_rgb = asz,
            .ref_rgb = ref_good,
            .ref_rgb_len = (size_t)gw * (size_t)gh * 4,
            .ajp_alpha = ajp_alpha,
            .sz_alpha = asz_alpha,
            .ref_alpha = ref_alpha,
            .ref_alpha_len = (size_t)aw * (size_t)ah * 4,
            .ajp_corrupt = corrupt_ajp,
            .sz_corrupt = 36 + sizeof(garbage),
        };
        pthread_t threads[8];
        for (int t = 0; t < 8; t++)
            assert(pthread_create(&threads[t], NULL, worker_fn, &targ) == 0);
        for (int t = 0; t < 8; t++)
            assert(pthread_join(threads[t], NULL) == 0);

        free(ref_good);
        free(ref_alpha);
        free(ajp_alpha);
        free(zmask);
        free(jpeg_odd);
        free(good_ajp);
        free(corrupt_ajp);
        free(jpeg16);
    }

    puts("OK_AJP");
    return 0;
}
"""
        extra_srcs = [
            ROOT / "subprojects/libsys4/src/ajp.c",
            ROOT / "subprojects/libsys4/src/pms.c",
            ROOT / "subprojects/libsys4/src/webp.c",
            ROOT / "subprojects/libsys4/src/zlib.c",
            ROOT / "subprojects/libsys4/src/system.c",
        ]
        out = compile_and_run(
            harness,
            extra_srcs,
            [
                "-ffunction-sections",
                "-fdata-sections",
                "-Wl,--gc-sections",
                *pkg_libs("libturbojpeg", "libwebp"),
                "-lz",
                "-lm",
                "-pthread",
            ],
        )
        self.assertEqual(out.strip(), "OK_AJP")

    def test_ex_hash_index_duplicates_gbk_and_append(self):
        # Build synthetic EX files exercising:
        # - < 8 children/blocks (linear scan path, hash_slots stays NULL)
        # - >= 8 children/blocks (lazy hash table path, hash_slots populated on first lookup)
        # - Duplicate child names (must return FIRST match!)
        # - Duplicate root block names with SAME type and DIFFERENT types
        # - GBK/SJIS CJK fallback + pure-ASCII fast rejection (|| fix)
        # - ex_append / ex_replace invalidating cached hash tables cleanly
        sjis_01 = b"\x82\x4f\x82\x50\x94\xd4"   # SJIS '０１番'
        gbk_01_mixed = b"\xa3\xb0\xa3\xb1\x94\xd4"  # GBK '０１' + SJIS '番'

        small_tree = _ex_tree(
            b"SmallTree",
            [
                _ex_leaf(b"k0", 1, struct.pack("<i", 10)),
                _ex_leaf(b"dup", 1, struct.pack("<i", 20)),
                _ex_leaf(b"dup", 1, struct.pack("<i", 99)),
                _ex_leaf(sjis_01, 1, struct.pack("<i", 30)),
            ],
        )

        big_children = []
        for i in range(24):
            big_children.append(_ex_leaf(f"item_{i:02d}".encode(), 1, struct.pack("<i", 1000 + i)))
        # Insert duplicate name at index 24 (value 777) and index 28 (value 888)
        big_children.append(_ex_leaf(b"dup_key", 1, struct.pack("<i", 777)))
        big_children.append(_ex_leaf(b"mid_a", 1, struct.pack("<i", 701)))
        big_children.append(_ex_leaf(sjis_01, 1, struct.pack("<i", 555)))
        big_children.append(_ex_leaf(b"mid_b", 1, struct.pack("<i", 702)))
        big_children.append(_ex_leaf(b"dup_key", 1, struct.pack("<i", 888)))
        big_tree = _ex_tree(b"BigTree", big_children)

        # Build >= 12 top-level blocks including:
        # - duplicate name "shared_name": first EX_INT(42), later EX_STRING("second_type")
        # - duplicate name "dup_int": first EX_INT(111), later EX_INT(222)
        # - SJIS block name for GBK fallback
        blocks = [
            _ex_block(b"SmallTree", 6, small_tree),
            _ex_block(b"BigTree", 6, big_tree),
            _ex_block(b"shared_name", 1, struct.pack("<i", 42)),
            _ex_block(b"dup_int", 1, struct.pack("<i", 111)),
            _ex_block(sjis_01, 1, struct.pack("<i", 321)),
        ]
        for i in range(10):
            blocks.append(_ex_block(f"blk_{i:02d}".encode(), 1, struct.pack("<i", i * 10)))
        blocks.append(_ex_block(b"shared_name", 3, _ex_pstr(b"second_type")))
        blocks.append(_ex_block(b"dup_int", 1, struct.pack("<i", 222)))
        main_ex_bytes = build_extf(blocks)

        # Secondary EX for ex_append / ex_replace hash invalidation check
        append_tree = _ex_tree(
            b"BigTree",
            [
                _ex_leaf(b"added_child", 1, struct.pack("<i", 9001)),
                _ex_leaf(b"item_03", 1, struct.pack("<i", 3003)),
            ],
        )
        append_ex_bytes = build_extf(
            [
                _ex_block(b"BigTree", 6, append_tree),
                _ex_block(b"added_root_block", 1, struct.pack("<i", 4242)),
            ]
        )

        # Small root EX (< 8 blocks)
        small_ex_bytes = build_extf(
            [
                _ex_block(b"only_a", 1, struct.pack("<i", 1)),
                _ex_block(b"only_b", 1, struct.pack("<i", 2)),
                _ex_block(b"only_a", 1, struct.pack("<i", 99)),
            ]
        )

        with tempfile.TemporaryDirectory() as tmpdir:
            td = Path(tmpdir)
            main_path = td / "main.ex"
            append_path = td / "append.ex"
            small_path = td / "small.ex"
            main_path.write_bytes(main_ex_bytes)
            append_path.write_bytes(append_ex_bytes)
            small_path.write_bytes(small_ex_bytes)

            harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/ex.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

int main(int argc, char **argv) {
    assert(argc == 4);
    sys4_set_text_gbk(false);

    /* 1. Small EX (< 8 blocks): linear path, hash_slots remains NULL */
    struct ex *small = ex_read_file(argv[3]);
    assert(small != NULL);
    assert(small->name_hash_slots == NULL);
    assert(ex_get_int(small, "only_a", -1) == 1); /* first-match duplicate */
    assert(ex_get_int(small, "only_b", -1) == 2);
    assert(ex_get_int(small, "missing", -1) == -1);
    assert(small->name_hash_slots == NULL);
    ex_free(small);

    /* 2. Main EX (>= 8 blocks, SmallTree < 8 children, BigTree >= 8 children) */
    struct ex *ex = ex_read_file(argv[1]);
    assert(ex != NULL);
    assert(ex->name_hash_slots == NULL); /* lazy: not built before first lookup */

    struct ex_tree *st = ex_get_tree(ex, "SmallTree");
    assert(st != NULL);
    assert(ex->name_hash_slots != NULL); /* built lazily on first root lookup */
    assert(st->name_hash_slots == NULL);
    struct ex_value *v = ex_get(ex, "SmallTree.dup");
    assert(v && v->type == EX_INT && v->i == 20); /* first match in linear path */
    assert(st->name_hash_slots == NULL); /* < 8 children stays linear */

    struct ex_tree *bt = ex_get_tree(ex, "BigTree");
    assert(bt != NULL);
    assert(bt->name_hash_slots == NULL); /* lazy: not built before lookup on BigTree */

    for (int i = 0; i < 24; i++) {
        char path[64], cname[32];
        snprintf(path, sizeof(path), "BigTree.item_%02d", i);
        snprintf(cname, sizeof(cname), "item_%02d", i);
        struct ex_value *iv = ex_get(ex, path);
        assert(iv && iv->type == EX_INT && iv->i == 1000 + i);
        struct ex_tree *ch = ex_tree_get_child(bt, cname);
        assert(ch && ch->is_leaf && ch->leaf.value.i == 1000 + i);
    }
    assert(bt->name_hash_slots != NULL); /* >= 8 children built lazy hash */

    /* Duplicate child name in hashed tree must return FIRST match (777, not 888) */
    v = ex_get(ex, "BigTree.dup_key");
    assert(v && v->type == EX_INT && v->i == 777);
    struct ex_tree *dup_ch = ex_tree_get_child(bt, "dup_key");
    assert(dup_ch && dup_ch->is_leaf && dup_ch->leaf.value.i == 777);

    /* Duplicate root block name with SAME type returns FIRST match (111, not 222) */
    assert(ex_get_int(ex, "dup_int", -1) == 111);
    v = ex_get(ex, "dup_int");
    assert(v && v->type == EX_INT && v->i == 111);

    /* Duplicate root block name with DIFFERENT types:
     * ex_get / ex_get_int returns first block (EX_INT 42),
     * ex_get_string MUST still find the second block (EX_STRING "second_type")! */
    v = ex_get(ex, "shared_name");
    assert(v && v->type == EX_INT && v->i == 42);
    assert(ex_get_int(ex, "shared_name", -1) == 42);
    struct string *sh_str = ex_get_string(ex, "shared_name");
    assert(sh_str && strcmp(sh_str->text, "second_type") == 0);
    free_string(sh_str);

    /* 3. GBK fallback & ASCII fast rejection */
    const char gbk_mixed[] = "\xa3\xb0\xa3\xb1\x94\xd4";
    const char big_mixed_path[] = "BigTree.\xa3\xb0\xa3\xb1\x94\xd4";
    /* With gbk=false, mixed key misses */
    assert(ex_get_int(ex, gbk_mixed, -1) == -1);
    assert(ex_get(ex, big_mixed_path) == NULL);

    /* With gbk=true, mixed key matches SJIS sibling on both root and hashed tree */
    sys4_set_text_gbk(true);
    assert(ex_get_int(ex, gbk_mixed, -1) == 321);
    v = ex_get(ex, big_mixed_path);
    assert(v && v->type == EX_INT && v->i == 555);
    struct ex_tree *gbk_ch = ex_tree_get_child(bt, gbk_mixed);
    assert(gbk_ch && gbk_ch->is_leaf && gbk_ch->leaf.value.i == 555);
    /* Pure-ASCII miss must return NULL cleanly in GBK mode too */
    assert(ex_get(ex, "BigTree.__nonexistent_ascii__") == NULL);
    assert(ex_get(ex, "__nonexistent_root_ascii__") == NULL);
    assert(ex_tree_get_child(bt, "__nonexistent_ascii__") == NULL);
    sys4_set_text_gbk(false);

    /* 4. ex_append & ex_extract_append invalidate cached hash tables cleanly */
    struct ex *app = ex_read_file(argv[2]);
    assert(app != NULL);
    struct ex *extracted = ex_extract_append(ex, app);
    assert(extracted != NULL);
    assert(ex_get_int(extracted, "added_root_block", -1) == 4242);
    v = ex_get(extracted, "BigTree.added_child");
    assert(v && v->type == EX_INT && v->i == 9001);
    ex_free(extracted);

    ex_append(ex, app);
    /* Re-fetch BigTree pointer after ex_append (blocks array may have been realloc'd) */
    assert(ex_get_int(ex, "added_root_block", -1) == 4242);
    v = ex_get(ex, "BigTree.added_child");
    assert(v && v->type == EX_INT && v->i == 9001);
    v = ex_get(ex, "BigTree.item_03");
    assert(v && v->type == EX_INT && v->i == 3003);
    v = ex_get(ex, "BigTree.item_00");
    assert(v && v->type == EX_INT && v->i == 1000);
    ex_free(app);

    /* ex_replace adding a brand new block also invalidates hash (and rep's 2nd only_a replaces 1st -> 99) */
    struct ex *rep = ex_read_file(argv[3]);
    ex_replace(ex, rep);
    assert(ex_get_int(ex, "only_a", -1) == 99);
    assert(ex_get_int(ex, "only_b", -1) == 2);
    assert(ex_get_int(ex, "added_root_block", -1) == 4242);
    ex_free(rep);

    ex_free(ex);
    puts("OK_EX");
    return 0;
}
"""
            out = compile_and_run(
                harness,
                LIBSYS4_CORE,
                ["-lz", "-lm"],
                [str(main_path), str(append_path), str(small_path)],
            )
            self.assertEqual(out.strip(), "OK_EX")

    def test_mainex_v12_name_list_correctness_and_linear_scaling(self):
        # Build an EX file with:
        # - root blocks: scalar leaves, table, list, and trees
        # - nested tree with mixed leaves and subtrees (both small and 2,000-child large tree)
        small_sub = _ex_tree(
            b"SmallNode",
            [
                _ex_leaf(b"leaf_a", 1, struct.pack("<i", 1)),
                _ex_tree(b"sub_0", [_ex_leaf(b"inner", 1, struct.pack("<i", 9))]),
                _ex_leaf(b"leaf_b", 3, _ex_pstr(b"hello")),
                _ex_tree(b"sub_1", []),
            ],
        )

        # Large tree: 1,000 subtrees + 1,000 leaves interleaved (2,000 children total)
        large_children = []
        for i in range(1000):
            large_children.append(_ex_tree(f"node_{i:04d}".encode(), []))
            large_children.append(_ex_leaf(f"leaf_{i:04d}".encode(), 1, struct.pack("<i", i)))
        large_tree = _ex_tree(b"LargeTree", large_children)

        # Table block (1 column, 1 row)
        table_payload = (
            struct.pack("<I", 1)  # nr_fields
            + struct.pack("<I", 1) + _ex_pstr(b"ColA") + struct.pack("<II", 0, 0)
            + struct.pack("<II", 1, 1)  # nr_columns, nr_rows
            + struct.pack("<Ii", 1, 77)
        )
        list_payload = _ex_list([(1, struct.pack("<i", 123))])

        ex_bytes = build_extf(
            [
                _ex_block(b"RootInt", 1, struct.pack("<i", 7)),
                _ex_block(b"SmallNode", 6, small_sub),
                _ex_block(b"MyTable", 4, table_payload),
                _ex_block(b"MyList", 5, list_payload),
                _ex_block(b"LargeTree", 6, large_tree),
            ]
        )

        with tempfile.TemporaryDirectory() as tmpdir:
            ex_path = Path(tmpdir) / "test_v12.ex"
            ex_path.write_bytes(ex_bytes)

            harness = r"""
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "xsystem4.h"
#include "system4/ain.h"
#include "system4/ex.h"
#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "hll.h"
#include "mainex_v12.h"

struct config config;
struct ain program = { .version = 14 };
struct ain *ain = &program;
bool game_evenicle2 = false;
bool game_okusama = false;
bool game_haharanman = false;
struct vm_value_slot *unused_placeholder;

/* Minimal VM heap stubs */
#define MAX_SLOTS 32768
static struct vm_pointer heap_slots[MAX_SLOTS];
struct vm_pointer *heap = heap_slots;
size_t heap_size = MAX_SLOTS;
static int next_slot = 1;

int32_t heap_alloc_slot(enum vm_pointer_type type) {
    assert(next_slot < MAX_SLOTS);
    int slot = next_slot++;
    heap_slots[slot] = (struct vm_pointer){ .type = type, .ref = 1 };
    return slot;
}

int32_t heap_alloc_string(struct string *s) {
    int slot = heap_alloc_slot(VM_STRING);
    heap_slots[slot].s = s;
    return slot;
}

int32_t heap_alloc_page(struct page *p) {
    int slot = heap_alloc_slot(VM_PAGE);
    heap_slots[slot].page = p;
    return slot;
}

void heap_unref(int slot) {
    if (slot <= 0 || slot >= MAX_SLOTS) return;
    if (heap_slots[slot].ref > 0) heap_slots[slot].ref--;
    if (heap_slots[slot].ref == 0) {
        if (heap_slots[slot].type == VM_STRING && heap_slots[slot].s) {
            free_string(heap_slots[slot].s);
            heap_slots[slot].s = NULL;
        } else if (heap_slots[slot].type == VM_PAGE && heap_slots[slot].page) {
            delete_page_vars(heap_slots[slot].page);
            free_page(heap_slots[slot].page);
            heap_slots[slot].page = NULL;
        }
    }
}

void heap_ref(int slot) {
    if (slot > 0 && slot < MAX_SLOTS) heap_slots[slot].ref++;
}

uint32_t heap_get_seq(int slot) { (void)slot; return 0; }
bool heap_index_valid(int slot) { return slot > 0 && slot < next_slot; }
struct page *heap_get_page(int slot) { return heap_slots[slot].page; }
struct string *heap_get_string(int slot) { return heap_slots[slot].s; }
void heap_set_page(int slot, struct page *p) { heap_slots[slot].page = p; }
void heap_set_string(int slot, struct string *s) { heap_slots[slot].s = s; }
void exit_unref(int slot) { heap_unref(slot); }
union vm_value vm_copy(union vm_value v, enum ain_data_type type) { (void)type; return v; }
void vm_call(int fn, int obj) { (void)fn; (void)obj; abort(); }

_Noreturn void _vm_error(const char *fmt, ...) { abort(); }
char *gamedir_path(const char *p) { return strdup(p); }
char *gamedir_path_icase(const char *p) { return strdup(p); }
struct archive *RE_open_3d_archive(void) { return NULL; }
struct archive_data *RE_archive_find_txtex(struct archive *ar, const char *dir) { return NULL; }

struct reg_fn { const char *name; void *fn; };
static struct reg_fn reg_fns[256];
static int nr_reg_fns = 0;

void static_library_replace(struct static_library *lib, const char *name, void *fun) {
    (void)lib;
    assert(nr_reg_fns < 256);
    reg_fns[nr_reg_fns].name = name;
    reg_fns[nr_reg_fns].fn = fun;
    nr_reg_fns++;
}

static void *find_fn(const char *name) {
    for (int i = 0; i < nr_reg_fns; i++)
        if (!strcmp(reg_fns[i].name, name))
            return reg_fns[i].fn;
    return NULL;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

void mainex_v12_prelink(struct static_library *library);

int main(int argc, char **argv) {
    assert(argc == 2);
    config.ex_path = argv[1];

    struct static_library lib;
    memset(&lib, 0, sizeof(lib));
    mainex_v12_prelink(&lib);

    void (*init_fn)(void) = find_fn("_ModuleInit");
    void (*fini_fn)(void) = find_fn("_ModuleFini");
    int (*node_count_fn)(struct string *, int) = find_fn("GetNodeNameCount");
    int (*name_count_fn)(struct string *, int) = find_fn("GetEXNameCount");
    bool (*node_fn)(struct string *, int, struct string **, int) = find_fn("GetNodeName");
    bool (*name_fn)(struct string *, int, struct string **, int) = find_fn("GetEXName");
    bool (*nodes_fn)(struct string *, struct page **, int) = find_fn("GetNodeNameList");
    bool (*names_fn)(struct string *, struct page **, int) = find_fn("GetEXNameList");
    assert(init_fn && fini_fn && node_count_fn && name_count_fn && node_fn && name_fn && nodes_fn && names_fn);

    init_fn();

    /* 1. Root ("") nodes vs leaves */
    struct string *s_empty = make_string("", 0);
    struct page *arr = NULL;
    assert(nodes_fn(s_empty, &arr, 0) == true);
    assert(arr && arr->nr_vars == 2);
    assert(strcmp(heap[arr->values[0].i].s->text, "SmallNode") == 0);
    assert(strcmp(heap[arr->values[1].i].s->text, "LargeTree") == 0);

    /* Calling again replaces existing non-NULL *out cleanly */
    assert(names_fn(s_empty, &arr, 0) == true);
    assert(arr && arr->nr_vars == 3);
    assert(strcmp(heap[arr->values[0].i].s->text, "RootInt") == 0);
    assert(strcmp(heap[arr->values[1].i].s->text, "MyTable") == 0);
    assert(strcmp(heap[arr->values[2].i].s->text, "MyList") == 0);

    /* 2. SmallNode tree: leaves & subtrees match GetName/GetNodeName */
    struct string *s_small = make_string("SmallNode", 9);
    assert(node_count_fn(s_small, 0) == 2);
    assert(name_count_fn(s_small, 0) == 2);
    assert(nodes_fn(s_small, &arr, 0) == true);
    assert(arr->nr_vars == 2);
    assert(strcmp(heap[arr->values[0].i].s->text, "sub_0") == 0);
    assert(strcmp(heap[arr->values[1].i].s->text, "sub_1") == 0);

    assert(names_fn(s_small, &arr, 0) == true);
    assert(arr->nr_vars == 2);
    assert(strcmp(heap[arr->values[0].i].s->text, "leaf_a") == 0);
    assert(strcmp(heap[arr->values[1].i].s->text, "leaf_b") == 0);

    /* get_name single lookup + negative/out-of-range bounds */
    struct string *single = NULL;
    assert(name_fn(s_small, 0, &single, 0) == true && strcmp(single->text, "leaf_a") == 0);
    assert(name_fn(s_small, 1, &single, 0) == true && strcmp(single->text, "leaf_b") == 0);
    assert(name_fn(s_small, -1, &single, 0) == false);
    assert(name_fn(s_small, 2, &single, 0) == false);
    free_string(single);

    /* 3. Table, list, scalar leaf, and missing paths return true with empty array */
    const char *non_tree_paths[] = { "MyTable", "MyList", "RootInt", "SmallNode.leaf_a", "DoesNotExist" };
    for (size_t i = 0; i < sizeof(non_tree_paths) / sizeof(non_tree_paths[0]); i++) {
        struct string *p = make_string(non_tree_paths[i], strlen(non_tree_paths[i]));
        assert(nodes_fn(p, &arr, 0) == true && arr && arr->nr_vars == 0);
        assert(names_fn(p, &arr, 0) == true && arr && arr->nr_vars == 0);
        free_string(p);
    }

    /* 4. Invalid reader ID or NULL out returns false and preserves *out */
    struct page *sentinel = arr;
    assert(nodes_fn(s_small, &arr, 99) == false && arr == sentinel);
    assert(nodes_fn(s_small, NULL, 0) == false);

    /* 5. AIN v8 array type compatibility */
    program.version = 8;
    assert(nodes_fn(s_small, &arr, 0) == true);
    assert(arr->a_type == AIN_ARRAY_STRING && arr->nr_vars == 2);
    program.version = 14;

    /* 6. Large 2,000-child tree: verify exact order and O(N) runtime */
    struct string *s_large = make_string("LargeTree", 9);
    double t0 = now_ms();
    assert(nodes_fn(s_large, &arr, 0) == true);
    assert(arr->nr_vars == 1000);
    for (int i = 0; i < 1000; i++) {
        char expected[32];
        snprintf(expected, sizeof(expected), "node_%04d", i);
        assert(strcmp(heap[arr->values[i].i].s->text, expected) == 0);
    }
    assert(names_fn(s_large, &arr, 0) == true);
    assert(arr->nr_vars == 1000);
    for (int i = 0; i < 1000; i++) {
        char expected[32];
        snprintf(expected, sizeof(expected), "leaf_%04d", i);
        assert(strcmp(heap[arr->values[i].i].s->text, expected) == 0);
    }
    double elapsed_ms = now_ms() - t0;
    assert(elapsed_ms < 250.0);

    delete_page_vars(arr);
    free_page(arr);
    free_string(s_empty);
    free_string(s_small);
    free_string(s_large);
    fini_fn();
    puts("OK_V12");
    return 0;
}
"""
            out = compile_and_run(
                harness,
                [
                    ROOT / "src/hll/MainEXFile_v12.c",
                    ROOT / "src/page.c",
                    ROOT / "src/vm_v12.c",
                    *LIBSYS4_CORE,
                ],
                ["-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-lz", "-lm"],
                [str(ex_path)],
            )
            self.assertEqual(out.strip(), "OK_V12")

    @unittest.skipUnless(DOHNA_CG_AFA.exists() and DOHNA_EX.exists(), "Host Dohna Dohna assets not present")
    def test_host_dohnadohna_ajp_and_ex_parity(self):
        harness = r"""
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <turbojpeg.h>
#include <webp/decode.h>

#include "system4/little_endian.h"
#include "system4.h"
#include "system4/afa.h"
#include "system4/ajp.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/ex.h"
#include "system4/pms.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "system4/webp.h"
#include "system4/zlib.h"

static const uint8_t key16[16] = {
    0x5d, 0x91, 0xae, 0x87, 0x4a, 0x56, 0x41, 0xcd,
    0x83, 0xec, 0x4c, 0x92, 0xb5, 0xcb, 0x16, 0x34
};

static uint8_t *ref_decode_ajp(const uint8_t *data, size_t size) {
    uint32_t w = LittleEndian_getDW(data, 12);
    uint32_t h = LittleEndian_getDW(data, 16);
    uint32_t joff = LittleEndian_getDW(data, 20);
    uint32_t jsz = LittleEndian_getDW(data, 24);
    uint32_t moff = LittleEndian_getDW(data, 28);
    uint32_t msz = LittleEndian_getDW(data, 32);

    uint8_t *jbuf = malloc(jsz);
    memcpy(jbuf, data + joff, jsz);
    for (size_t i = 0; i < 16 && i < jsz; i++) jbuf[i] ^= key16[i];

    tjhandle tj = tjInitDecompress();
    int dw = 0, dh = 0, ss = 0;
    assert(tjDecompressHeader2(tj, jbuf, jsz, &dw, &dh, &ss) == 0);
    size_t npix = (size_t)w * (size_t)h;
    uint8_t *rgb = malloc(npix * 3);
    assert(tjDecompress2(tj, jbuf, jsz, rgb, dw, 0, dh, TJPF_RGB, TJFLAG_FASTDCT) == 0);
    tjDestroy(tj);
    free(jbuf);

    uint8_t *mask = NULL;
    if (msz > 0) {
        uint8_t *mbuf = malloc(msz);
        memcpy(mbuf, data + moff, msz);
        for (size_t i = 0; i < 16 && i < msz; i++) mbuf[i] ^= key16[i];
        if (msz >= 4 && pms8_checkfmt(mbuf)) {
            mask = pms_extract_mask(mbuf, msz);
        } else if (msz >= 12 && webp_checkfmt(mbuf)) {
            int ww = 0, wh = 0;
            uint8_t *tmp = WebPDecodeRGBA(mbuf, msz, &ww, &wh);
            if (tmp && (uint32_t)ww == w && (uint32_t)wh == h) {
                mask = malloc(npix);
                for (size_t i = 0; i < npix; i++) mask[i] = tmp[i * 4 + 3];
            }
            WebPFree(tmp);
        } else if (mbuf[0] == 0x78) {
            mask = malloc(npix);
            if (!zlib_decompress_exact(mask, npix, mbuf, msz)) {
                free(mask);
                mask = NULL;
            }
        }
        free(mbuf);
    }
    if (!mask) {
        mask = malloc(npix);
        memset(mask, 0xFF, npix);
    }
    uint8_t *rgba = malloc(npix * 4);
    for (size_t i = 0; i < npix; i++) {
        rgba[i * 4 + 0] = rgb[i * 3 + 0];
        rgba[i * 4 + 1] = rgb[i * 3 + 1];
        rgba[i * 4 + 2] = rgb[i * 3 + 2];
        rgba[i * 4 + 3] = mask[i];
    }
    free(rgb);
    free(mask);
    return rgba;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
    assert(argc == 3);
    /* 1. Compare pixel-for-pixel on real Dohna Dohna AJPs (with & without alpha, odd & even dims) */
    int err = 0;
    struct afa_archive *afa = afa_open(argv[1], 0, &err);
    assert(afa != NULL);
    int checked_alpha = 0, checked_noalpha = 0, checked_odd = 0;
    for (uint32_t i = 0; i < afa->nr_files && (checked_alpha < 20 || checked_noalpha < 20); i++) {
        struct archive_data *ad = archive_get(&afa->ar, i);
        if (!ad) continue;
        struct cg_metrics m;
        if (ajp_get_metrics(ad->data, ad->size, &m)) {
            bool want = (m.has_alpha && checked_alpha < 20) || (!m.has_alpha && checked_noalpha < 20);
            if (want) {
                uint8_t *ref = ref_decode_ajp(ad->data, ad->size);
                struct cg cg = {0};
                ajp_extract(ad->data, ad->size, &cg);
                assert(cg.pixels != NULL);
                assert(cg.metrics.w == m.w && cg.metrics.h == m.h);
                assert(memcmp(cg.pixels, ref, (size_t)m.w * (size_t)m.h * 4) == 0);
                free(cg.pixels);
                free(ref);
                if (m.has_alpha) checked_alpha++; else checked_noalpha++;
                if ((m.w & 1) || (m.h & 1)) checked_odd++;
            }
        }
        archive_free_data(ad);
    }
    archive_free(&afa->ar);
    assert(checked_alpha == 20 && checked_noalpha == 20 && checked_odd > 0);

    /* 2. Verify EX lookup parity (hash vs linear) and fast ASCII misses on real DohnaDohna EX */
    sys4_set_text_gbk(true);
    struct ex *ex = ex_read_file(argv[2]);
    assert(ex != NULL);
    for (uint32_t i = 0; i < ex->nr_blocks; i++) {
        struct ex_value *v = ex_get(ex, ex->blocks[i].name->text);
        assert(v != NULL);
    }
    double t0 = now_ms();
    for (int i = 0; i < 5000; i++) {
        assert(ex_get(ex, "__nonexistent_ascii_block__") == NULL);
    }
    double miss_ms = now_ms() - t0;
    assert(miss_ms < 50.0);
    ex_free(ex);

    puts("OK_HOST_DOHNA");
    return 0;
}
"""
        extra_srcs = [
            *LIBSYS4_CORE,
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
            ROOT / "subprojects/libsys4/src/ald.c",
        ]
        out = compile_and_run(
            harness,
            extra_srcs,
            [*pkg_libs("libturbojpeg", "libwebp", "libpng"), "-lz", "-lm"],
            [str(DOHNA_CG_AFA), str(DOHNA_EX)],
        )
        self.assertEqual(out.strip(), "OK_HOST_DOHNA")


if __name__ == "__main__":
    unittest.main()
