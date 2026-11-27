#!/usr/bin/env python3
"""Comprehensive native C regression tests for PC save export compatibility.

Covers:
1. Exact Windows dohnadohna.exe CSerializeLoader validator (GD\x01\x01 v9).
2. Live serialize_structs / deserialize_structs round-trip (null=-1, AIN_WRAP,
   AIN_OPTION 2-slot & 3-slot, AIN_BOOL/ENUM/FLOAT, [nosave] name2=="",
   name!=name2, multi-root objs[0]/sec_cnt[0], C6 comments).
3. Backwards-compatible load of broken legacy AliceHub GD\x01\x01 saves and
   full repair via savedata_export_pc_file / savedata_export_pc_file_with_ain.
4. Read-only live schema & round-trip verification against real dohnadohna.ain.
5. X4SS -> PC export (with/without .ain) and rejection of unconvertible X4SS.
6. write_group_save_comment / read_group_save_comment embedded in gsave v8 +
   legacy .comment sidecar fallback & export folding.
7. RSM v6+ unencrypted output, AIN v14 @x4t: avoidance for standard arrays,
   export decryption of encrypted RSM, savedata_file_has_xsystem4_only_state,
   and clean copy of PSR/CSD/QNT/MsgSkip files.
"""

import os
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

from test_evenicle2_regressions import LIBSYS4_CORE, ROOT, sdl_cflags
from test_vm_reign_regressions import extract_function

DOHNADOHNA_AIN = Path("/root/Rance10_CN/DohnaDohna/多娜多娜 一起干坏事吧/dohnadohna.ain")

# AIN data type constants (subprojects/libsys4/include/system4/ain.h)
AIN_VOID = 0
AIN_INT = 10
AIN_FLOAT = 11
AIN_STRING = 12
AIN_STRUCT = 13
AIN_ARRAY_INT = 14
AIN_REF_INT = 18
AIN_REF_FLOAT = 19
AIN_REF_STRING = 20
AIN_REF_STRUCT = 21
AIN_FUNC_TYPE = 27
AIN_REF_FUNC_TYPE = 31
AIN_BOOL = 47
AIN_REF_BOOL = 51
AIN_LONG_INT = 55
AIN_REF_LONG_INT = 59
AIN_DELEGATE = 63
AIN_REF_DELEGATE = 67
AIN_ARRAY = 79
AIN_REF_ARRAY = 80
AIN_WRAP = 82
AIN_OPTION = 86
AIN_REF_OPTION = 87
AIN_IFACE = 89
AIN_ENUM = 92
AIN_REF_ENUM = 93
AIN_IFACE_WRAP = 100

WRAP_TO_REF = {
    AIN_INT: AIN_REF_INT,
    AIN_FLOAT: AIN_REF_FLOAT,
    AIN_STRING: AIN_REF_STRING,
    AIN_STRUCT: AIN_REF_STRUCT,
    AIN_BOOL: AIN_REF_BOOL,
    AIN_LONG_INT: AIN_REF_LONG_INT,
    AIN_DELEGATE: AIN_REF_DELEGATE,
    AIN_ARRAY: AIN_REF_ARRAY,
    AIN_WRAP: AIN_WRAP,
    AIN_OPTION: AIN_REF_OPTION,
    AIN_ENUM: AIN_REF_ENUM,
    AIN_IFACE_WRAP: AIN_IFACE,
    AIN_FUNC_TYPE: AIN_REF_FUNC_TYPE,
}

STRUCT_OR_IFACE_TYPES = {AIN_STRUCT, AIN_REF_STRUCT, AIN_IFACE, AIN_IFACE_WRAP}

LIBSYS4_WITH_SAVEFILE = [
    *LIBSYS4_CORE,
    ROOT / "subprojects/libsys4/src/savefile.c",
]


def _read_cstr(buf: bytes, pos: int, limit: int) -> tuple[bytes, int]:
    if pos >= limit:
        raise AssertionError(f"C-string out of bounds at {pos} >= {limit}")
    end = buf.find(b"\x00", pos, limit)
    if end < 0:
        raise AssertionError(f"Unterminated C-string at {pos} (limit {limit})")
    return buf[pos:end], end + 1


def _effective_type(t1: int, t2: int) -> int:
    return t2 if t1 in (AIN_WRAP, AIN_OPTION) else t1


def validate_windows_gdss(
    path: Path,
    expected_schemas: dict[bytes, list[tuple[int, int, bytes]]] | None = None,
    expected_roots: int | None = None,
    expected_comment: bytes | None = None,
    allow_missing_comment: bool = False,
) -> dict:
    """Strict validator matching unpacked Windows dohnadohna.exe CSerializeLoader."""
    data = path.read_bytes()
    if len(data) < 10:
        raise AssertionError(f"GD file too short ({len(data)} bytes)")
    if data[:4] != b"GD\x01\x01":
        raise AssertionError(f"Bad outer magic: {data[:4]!r}")
    ulen = struct.unpack_from("<I", data, 4)[0]
    if data[8] != 0x78:
        raise AssertionError(f"Expected unencrypted zlib header 0x78, got 0x{data[8]:02x}")
    raw = zlib.decompress(data[8:])
    if len(raw) != ulen:
        raise AssertionError(f"Decompressed length mismatch: {len(raw)} != {ulen}")

    # 98-byte inner header
    if len(raw) < 98:
        raise AssertionError(f"Inner payload too short: {len(raw)}")
    if raw[0:17] != b"serialize_struct\x00":
        raise AssertionError(f"Bad inner magic: {raw[0:17]!r}")
    ver, unk = struct.unpack_from("<II", raw, 17)
    if ver != 1000 or unk != 9:
        raise AssertionError(f"Bad version/unknown: ({ver}, {unk})")
    if raw[81:98] != b"serialize_struct\x00":
        raise AssertionError(f"Bad trailing header magic: {raw[81:98]!r}")

    sec_off = []
    sec_cnt = []
    for i in range(7):
        off, cnt = struct.unpack_from("<II", raw, 25 + i * 8)
        sec_off.append(off)
        sec_cnt.append(cnt)

    if sec_off[0] != 64:
        raise AssertionError(f"sec_off[0] must be 64, got {sec_off[0]}")
    if sec_off[1] != 98:
        raise AssertionError(f"sec_off[1] must be 98, got {sec_off[1]}")
    for i in range(2, 7):
        if sec_off[i] < sec_off[i - 1] or sec_off[i] > ulen:
            raise AssertionError(f"Invalid section offsets: {sec_off}")

    # C1: objects
    nr_objs = sec_cnt[1]
    if nr_objs < 1:
        raise AssertionError("C1 must contain at least objs[0]")
    objs = []
    pos = sec_off[1]
    for i in range(nr_objs):
        sdef, nvals = struct.unpack_from("<iI", raw, pos)
        pos += 8
        vidxs = list(struct.unpack_from(f"<{nvals}I", raw, pos))
        pos += nvals * 4
        objs.append((sdef, vidxs))
    if pos != sec_off[2]:
        raise AssertionError(f"C1 framing mismatch: pos={pos}, sec_off[2]={sec_off[2]}")

    # C2: roots + sentinel
    roots = []
    pos = sec_off[2]
    for i in range(sec_cnt[2]):
        t1, t2, rid, zero = struct.unpack_from("<iiii", raw, pos)
        pos += 16
        rname, pos = _read_cstr(raw, pos, sec_off[3])
        if i < sec_cnt[2] - 1:
            if t1 != AIN_STRUCT or t2 != AIN_STRUCT or zero != 0 or rname != b"":
                raise AssertionError(f"Bad C2 root entry {i}: {(t1, t2, rid, zero, rname)}")
            if rid <= 0 or rid >= nr_objs:
                raise AssertionError(f"Invalid C2 root_id {rid} (nr_objs={nr_objs})")
            roots.append(rid)
        else:
            if (t1, t2, rid, zero, rname) != (0, 0, 0, 0, b""):
                raise AssertionError(f"Bad C2 sentinel: {(t1, t2, rid, zero, rname)}")
    if pos != sec_off[3]:
        raise AssertionError(f"C2 framing mismatch: pos={pos}, sec_off[3]={sec_off[3]}")

    nr_roots = len(roots)
    if expected_roots is not None and nr_roots != expected_roots:
        raise AssertionError(f"Root count mismatch: {nr_roots} != {expected_roots}")

    # Multi-root objs[0] and sec_cnt[0] check
    if sec_cnt[0] != nr_roots:
        raise AssertionError(f"sec_cnt[0]={sec_cnt[0]} != nr_roots={nr_roots}")
    if objs[0][0] != -1:
        raise AssertionError(f"objs[0].struct_def must be -1, got {objs[0][0]}")
    if objs[0][1] != list(range(nr_roots)):
        raise AssertionError(f"objs[0].val_idxs={objs[0][1]} != {list(range(nr_roots))}")

    # C3: strings
    strings = []
    pos = sec_off[3]
    for _ in range(sec_cnt[3]):
        s, pos = _read_cstr(raw, pos, sec_off[4])
        strings.append(s)
    if pos != sec_off[4]:
        raise AssertionError(f"C3 framing mismatch: pos={pos}, sec_off[4]={sec_off[4]}")

    # C4: arrays
    arrays = []
    pos = sec_off[4]
    for ai in range(sec_cnt[4]):
        rank, dim0, one, total, etype = struct.unpack_from("<iIIIi", raw, pos)
        pos += 20
        if rank != 1 or one != 1 or dim0 != total:
            raise AssertionError(f"Bad C4 array {ai} header: {(rank, dim0, one, total, etype)}")
        elems = list(struct.unpack_from(f"<{total}i", raw, pos))
        pos += total * 4
        # Windows CSerializeLoader rejects obj_idx == 0 in struct/iface arrays!
        if etype in STRUCT_OR_IFACE_TYPES:
            for k, ev in enumerate(elems):
                if ev == 0:
                    raise AssertionError(
                        f"C4 array[{ai}] (elem_type={etype}) elem[{k}] == 0 (invalid null struct!)"
                    )
                if ev != -1 and not (1 <= ev < nr_objs):
                    raise AssertionError(
                        f"C4 array[{ai}] (elem_type={etype}) elem[{k}]={ev} out of range [1..{nr_objs - 1}]"
                    )
        elif etype in (AIN_STRING, AIN_REF_STRING):
            for k, ev in enumerate(elems):
                if ev != 0x7FFFFFFF and not (0 <= ev < len(strings)):
                    raise AssertionError(f"C4 array[{ai}] bad string idx {ev}")
        elif etype in (AIN_ARRAY, AIN_REF_ARRAY):
            for k, ev in enumerate(elems):
                if ev != -1 and not (0 <= ev < sec_cnt[4]):
                    raise AssertionError(f"C4 array[{ai}] bad array idx {ev}")
        arrays.append((etype, elems))
    if pos != sec_off[5]:
        raise AssertionError(f"C4 framing mismatch: pos={pos}, sec_off[5]={sec_off[5]}")

    # C5: values + schemas
    nr_vals = sec_cnt[5]
    pos = sec_off[5]
    vals = list(struct.unpack_from(f"<{nr_vals}i", raw, pos))
    pos += nr_vals * 4
    nr_schemas = struct.unpack_from("<I", raw, pos)[0]
    pos += 4
    schemas = []
    for si in range(nr_schemas):
        sname, pos = _read_cstr(raw, pos, sec_off[6])
        nf = struct.unpack_from("<I", raw, pos)[0]
        pos += 4
        fields = []
        for _ in range(nf):
            t1, t2 = struct.unpack_from("<ii", raw, pos)
            pos += 8
            fname, pos = _read_cstr(raw, pos, sec_off[6])
            fields.append((t1, t2, fname))
        schemas.append((sname, fields))
    if pos != sec_off[6]:
        raise AssertionError(f"C5 framing mismatch: pos={pos}, sec_off[6]={sec_off[6]}")

    # Validate schemas against expected Windows schema definitions
    if expected_schemas is not None:
        for sname, fields in schemas:
            if sname in expected_schemas:
                exp = expected_schemas[sname]
                if len(fields) != len(exp):
                    raise AssertionError(
                        f"Schema {sname!r} field count {len(fields)} != expected {len(exp)}\n"
                        f"  actual:   {fields}\n  expected: {exp}"
                    )
                for idx, (act_f, exp_f) in enumerate(zip(fields, exp)):
                    if act_f != exp_f:
                        raise AssertionError(
                            f"Schema {sname!r}[{idx}] mismatch: actual={act_f} != expected={exp_f}"
                        )

    # Validate EVERY C1 object against Windows CSerializeLoader invariants
    for oid in range(1, nr_objs):
        sdef, vidxs = objs[oid]
        if not (0 <= sdef < nr_schemas):
            raise AssertionError(f"objs[{oid}].struct_def={sdef} out of range")
        sname, fields = schemas[sdef]
        if len(vidxs) != len(fields):
            raise AssertionError(
                f"objs[{oid}] ({sname!r}) nr_vals={len(vidxs)} != schema nr_fields={len(fields)}"
            )
        for fi, ((t1, t2, fname), vidx) in enumerate(zip(fields, vidxs)):
            if vidx >= nr_vals:
                raise AssertionError(f"objs[{oid}].val_idxs[{fi}]={vidx} >= nr_vals={nr_vals}")
            rv = vals[vidx]
            eff = _effective_type(t1, t2)
            # Windows CSerializeLoader crashes / rejects obj_idx == 0 on any struct/iface!
            if eff in STRUCT_OR_IFACE_TYPES:
                if rv == 0:
                    raise AssertionError(
                        f"objs[{oid}] ({sname!r}).{fname!r}[{fi}] (t1={t1}, t2={t2}) has obj_idx == 0!"
                    )
                if rv != -1 and not (1 <= rv < nr_objs):
                    raise AssertionError(
                        f"objs[{oid}] ({sname!r}).{fname!r}[{fi}] obj_idx={rv} out of range"
                    )
            if fname == b"" and t1 != AIN_VOID:
                # [nosave] placeholder check
                exp_placeholder = -1 if eff in STRUCT_OR_IFACE_TYPES else 0
                if rv != exp_placeholder:
                    raise AssertionError(
                        f"[nosave] objs[{oid}] ({sname!r})[{fi}] (t1={t1}, t2={t2}) "
                        f"expected placeholder {exp_placeholder}, got {rv}"
                    )
            if t1 == AIN_OPTION:
                if fi + 1 >= len(fields) or fields[fi + 1][0] != AIN_VOID or fields[fi + 1][2] != b"":
                    raise AssertionError(
                        f"objs[{oid}] ({sname!r}).{fname!r}[{fi}] AIN_OPTION missing trailing AIN_VOID slot"
                    )

    # C6: optional comment
    comment = None
    if sec_cnt[6] > 0:
        if sec_off[6] + 4 > ulen:
            raise AssertionError("Truncated C6 comment length")
        clen = struct.unpack_from("<I", raw, sec_off[6])[0]
        if sec_off[6] + 4 + clen != ulen:
            raise AssertionError(f"C6 framing mismatch: {sec_off[6] + 4 + clen} != {ulen}")
        comment = raw[sec_off[6] + 4 : sec_off[6] + 4 + clen]
    else:
        if sec_off[6] != ulen:
            raise AssertionError(f"Unexpected trailing bytes after C5: {sec_off[6]} != {ulen}")

    if expected_comment is not None:
        if comment != expected_comment:
            raise AssertionError(f"Comment mismatch: {comment!r} != {expected_comment!r}")
    elif not allow_missing_comment and comment is not None:
        pass

    return {
        "sec_off": sec_off,
        "sec_cnt": sec_cnt,
        "roots": roots,
        "objs": objs,
        "strings": strings,
        "arrays": arrays,
        "vals": vals,
        "schemas": schemas,
        "comment": comment,
    }


def _pack_ain_type(buf: bytearray, t: tuple, version: int):
    """Pack ain_type tuple (data, struc, inner_tuple_or_None)."""
    data, struc, inner = t
    if version >= 11:
        rank = 1 if inner is not None else 0
        buf.extend(struct.pack("<iii", data, struc, rank))
        if inner is not None:
            _pack_ain_type(buf, inner, version)
    else:
        buf.extend(struct.pack("<iii", data, struc, 0))


def build_synthetic_ain(
    version: int,
    structures: list[tuple[bytes, list[tuple[int, int]], list[tuple[bytes, bytes, tuple]]]],
    minor_version: int = 0,
) -> bytes:
    """Build a valid AI2-compressed .ain file readable by libsys4 ain_open().

    structures: list of (name_bytes, [(iface_sno, voff), ...], [(name, name2, type_tuple), ...])
    """
    raw = bytearray()
    raw.extend(b"VERS" + struct.pack("<I", version))
    raw.extend(b"KEYC" + struct.pack("<I", 0))
    raw.extend(b"CODE" + struct.pack("<I", 0))

    # FUNC section (needed so ain->minor_version can be recognized if v14.0 vs v14.1)
    funcs = [b"NULL"]
    if version == 14 and minor_version == 0:
        funcs.append(b"_ALICETOOLS_AINV14_00")
    raw.extend(b"FUNC" + struct.pack("<I", len(funcs)))
    for fn in funcs:
        raw.extend(struct.pack("<I", 0))
        raw.extend(fn + b"\x00")
        if 1 < version < 7:
            raw.extend(struct.pack("<i", 0))  # is_label
        if version >= 11:
            _pack_ain_type(raw, (AIN_VOID, -1, None), version)
        else:
            raw.extend(struct.pack("<ii", AIN_VOID, -1))
        raw.extend(struct.pack("<ii", 0, 0))  # nr_args, nr_vars
        if version >= 11:
            raw.extend(struct.pack("<i", 0))  # is_lambda
        if version > 1:
            raw.extend(struct.pack("<i", 0))  # crc

    raw.extend(b"GLOB" + struct.pack("<I", 0))
    raw.extend(b"GSET" + struct.pack("<I", 0))

    # STRT section
    raw.extend(b"STRT" + struct.pack("<I", len(structures)))
    for sname, ifaces, members in structures:
        raw.extend(sname + b"\x00")
        if version >= 11:
            raw.extend(struct.pack("<I", len(ifaces)))
            for isno, voff in ifaces:
                raw.extend(struct.pack("<ii", isno, voff))
        raw.extend(struct.pack("<ii", -1, -1))  # ctor, dtor
        raw.extend(struct.pack("<I", len(members)))
        for mname, mname2, mtype in members:
            raw.extend(mname + b"\x00")
            if version >= 12:
                raw.extend(mname2 + b"\x00")
            _pack_ain_type(raw, mtype, version)
            if version >= 8:
                raw.extend(struct.pack("<i", 0))  # has_initval
        if version > 14 or (version == 14 and minor_version >= 1):
            raw.extend(struct.pack("<I", 0))  # nr_vmethods

    raw.extend(b"MSG0" + struct.pack("<I", 0))
    raw.extend(b"MAIN" + struct.pack("<i", 0))
    raw.extend(b"MSGF" + struct.pack("<i", -1))
    raw.extend(b"HLL0" + struct.pack("<I", 0))
    raw.extend(b"SWI0" + struct.pack("<I", 0))
    raw.extend(b"GVER" + struct.pack("<I", 100))
    raw.extend(b"STR0" + struct.pack("<I", 0))
    raw.extend(b"FNAM" + struct.pack("<I", 0))

    comp = zlib.compress(bytes(raw), 1)
    return b"AI2\x00\x00\x00\x00\x00" + struct.pack("<II", len(raw), len(comp)) + comp


# Common minimal VM runtime stubs allowing us to compile & link the REAL src/savedata.c + src/page.c!
SAVEDATA_VM_STUBS = r"""
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/file.h"
#include "system4/savefile.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "savedata.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "vm/v12.h"
#include "xsystem4.h"

struct ain *ain = NULL;
struct config config = {0};
bool game_maman = false;
bool game_haharanman = false;
bool game_rance10 = false;
bool game_rance01 = false;
bool game_rance03 = false;
bool game_rance8 = false;
bool game_evenicle = false;
bool game_evenicle2 = false;
bool game_dohnadohna = false;
bool game_escalayer_reboot = false;
bool game_pascha3 = false;
bool game_night_remaster = false;
bool game_haruka = false;
bool game_beatblade = false;

static char g_test_savedir[1024] = ".";
static char g_test_gamedir[1024] = ".";

char *savedir_path(const char *filename) {
    size_t n = strlen(g_test_savedir) + 1 + strlen(filename) + 1;
    char *out = xmalloc(n);
    snprintf(out, n, "%s/%s", g_test_savedir, filename);
    return out;
}

char *gamedir_path(const char *filename) {
    size_t n = strlen(g_test_gamedir) + 1 + strlen(filename) + 1;
    char *out = xmalloc(n);
    snprintf(out, n, "%s/%s", g_test_gamedir, filename);
    return out;
}

/* Minimal heap implementation sufficient for savedata.c + page.c */
#define TEST_HEAP_CAP 4096
static struct vm_pointer g_heap_slots[TEST_HEAP_CAP];
struct vm_pointer *heap = g_heap_slots;
size_t heap_size = TEST_HEAP_CAP;
static int g_next_slot = 1;

static void test_heap_reset(void) {
    for (int i = 0; i < TEST_HEAP_CAP; i++) {
        if (g_heap_slots[i].ref > 0) {
            if (g_heap_slots[i].type == VM_STRING && g_heap_slots[i].s) {
                free_string(g_heap_slots[i].s);
            } else if (g_heap_slots[i].type == VM_PAGE && g_heap_slots[i].page) {
                free(g_heap_slots[i].page);
            }
        }
        memset(&g_heap_slots[i], 0, sizeof(g_heap_slots[i]));
    }
    g_next_slot = 1;
}

bool page_index_valid(int slot) {
    return slot >= 0 && (size_t)slot < heap_size && heap[slot].ref > 0 && heap[slot].type == VM_PAGE;
}

bool string_index_valid(int slot) {
    return slot >= 0 && (size_t)slot < heap_size && heap[slot].ref > 0 && heap[slot].type == VM_STRING;
}

int heap_alloc_slot(enum vm_pointer_type type) {
    int slot = g_next_slot++;
    assert(slot < TEST_HEAP_CAP);
    heap[slot].ref = 1;
    heap[slot].seq = slot;
    heap[slot].type = type;
    heap[slot].page = NULL;
    return slot;
}

int heap_alloc_page(struct page *page) {
    int slot = heap_alloc_slot(VM_PAGE);
    heap[slot].page = page;
    return slot;
}

int heap_alloc_string(struct string *s) {
    int slot = heap_alloc_slot(VM_STRING);
    heap[slot].s = s ? s : string_ref(&EMPTY_STRING);
    return slot;
}

void heap_set_page(int slot, struct page *page) {
    assert(page_index_valid(slot));
    heap[slot].page = page;
}

struct page *heap_get_page(int slot) {
    return page_index_valid(slot) ? heap[slot].page : NULL;
}

struct string *heap_get_string(int slot) {
    return string_index_valid(slot) ? heap[slot].s : &EMPTY_STRING;
}

void heap_ref(int slot) {
    if (slot >= 0 && (size_t)slot < heap_size && heap[slot].ref > 0)
        heap[slot].ref++;
}

void heap_unref(int slot) {
    if (slot >= 0 && (size_t)slot < heap_size && heap[slot].ref > 0)
        heap[slot].ref--;
}

void exit_unref(int slot) {
    heap_unref(slot);
}

void delete_page(int slot) {
    if (page_index_valid(slot)) {
        free(heap[slot].page);
        heap[slot].page = NULL;
    }
}

void delete_page_vars(struct page *page) { (void)page; }

struct page *alloc_page(enum page_type type, int type_index, int nr_vars) {
    struct page *p = xcalloc(1, sizeof(struct page) + (size_t)max(nr_vars, 0) * sizeof(union vm_value));
    p->type = type;
    p->index = type_index;
    p->nr_vars = nr_vars;
    return p;
}

void free_page(struct page *page) { free(page); }

int ain_type_slot_width(const struct ain_type *type) {
    if (!type) return 1;
    switch (type->data) {
    case AIN_IFACE:
    case AIN_IFACE_WRAP:
    case AIN_REF_OPTION:
    case AIN_REF_INT:
    case AIN_REF_FLOAT:
    case AIN_REF_BOOL:
    case AIN_REF_LONG_INT:
    case AIN_REF_ENUM:
    case AIN_REF_FUNC_TYPE:
        return 2;
    case AIN_WRAP:
        if (ain && ain->version < 14) return 2;
        if (!type->array_type) return 1;
        switch (type->array_type->data) {
        case AIN_STRING:
        case AIN_STRUCT:
        case AIN_ARRAY:
        case AIN_DELEGATE:
        case AIN_ARRAY_TYPE:
            return 1;
        default:
            return 2;
        }
    case AIN_OPTION:
        return (type->array_type ? ain_type_slot_width(type->array_type) : 1) + 1;
    default:
        return 1;
    }
}

int variable_init_typed(union vm_value *dst, const struct ain_type *t, bool call_ctors, bool init_structs) {
    (void)call_ctors; (void)init_structs;
    int w = ain_type_slot_width(t);
    if (!t) return 1;
    if (t->data == AIN_OPTION) {
        for (int k = 0; k < w - 1; k++) dst[k].i = -1;
        dst[w - 1].i = 1;
        return w;
    }
    const struct ain_type *eff = t;
    while (eff->data == AIN_WRAP && eff->array_type) eff = eff->array_type;
    switch (eff->data) {
    case AIN_STRING:
        dst[0].i = heap_alloc_string(string_ref(&EMPTY_STRING));
        break;
    case AIN_STRUCT:
    case AIN_REF_STRUCT:
        dst[0].i = -1;
        break;
    case AIN_IFACE:
    case AIN_IFACE_WRAP:
        dst[0].i = -1;
        dst[1].i = 0;
        break;
    case AIN_ARRAY:
    case AIN_REF_ARRAY:
    case AIN_ARRAY_TYPE:
    case AIN_REF_ARRAY_TYPE: {
        int s = heap_alloc_slot(VM_PAGE);
        heap_set_page(s, NULL);
        dst[0].i = s;
        break;
    }
    default:
        for (int k = 0; k < w; k++) dst[k].i = 0;
        break;
    }
    return w;
}

union vm_value variable_initval(enum ain_data_type type) {
    struct ain_type t = { .data = type, .struc = -1 };
    union vm_value v[4] = {0};
    variable_init_typed(v, &t, false, false);
    return v[0];
}

union vm_value variable_initval_typed(const struct ain_type *type) {
    union vm_value v[4] = {0};
    variable_init_typed(v, type, false, false);
    return v[0];
}

int elem_value_fini(const union vm_value *dst, const struct ain_type *t, bool call_dtors) {
    (void)dst; (void)call_dtors;
    return ain_type_slot_width(t);
}

void variable_fini(union vm_value v, enum ain_data_type type, bool call_dtors) {
    (void)v; (void)type; (void)call_dtors;
}

void variable_set(struct page *page, int varno, enum ain_data_type type, union vm_value val) {
    (void)type;
    if (page && varno >= 0 && varno < page->nr_vars)
        page->values[varno] = val;
}

enum ain_data_type variable_type(struct page *page, int varno, int *struct_type, int *array_rank) {
    if (struct_type) *struct_type = -1;
    if (array_rank) *array_rank = 0;
    if (!page) return AIN_VOID;
    if (page->type == STRUCT_PAGE && ain && page->index >= 0 && page->index < ain->nr_structures) {
        struct ain_struct *st = &ain->structures[page->index];
        if (varno >= 0 && varno < st->nr_members) {
            if (struct_type) *struct_type = st->members[varno].type.struc;
            if (array_rank) *array_rank = st->members[varno].type.rank;
            return st->members[varno].type.data;
        }
    } else if (page->type == ARRAY_PAGE) {
        if (page->a_type == AIN_ARRAY) {
            if (struct_type) *struct_type = page->array.element_type.struc;
            if (array_rank) *array_rank = page->array.element_type.rank;
            return page->array.element_type.data;
        }
        if (struct_type) *struct_type = page->array.struct_type;
        if (array_rank) *array_rank = page->array.rank - 1;
        return array_type(page->a_type);
    }
    return AIN_INT;
}

const struct ain_type *variable_full_type(struct page *page, int varno) {
    if (!page) return NULL;
    if (page->type == STRUCT_PAGE && ain && page->index >= 0 && page->index < ain->nr_structures) {
        struct ain_struct *st = &ain->structures[page->index];
        if (varno >= 0 && varno < st->nr_members)
            return &st->members[varno].type;
    } else if (page->type == ARRAY_PAGE && page->a_type == AIN_ARRAY) {
        return &page->array.element_type;
    }
    return NULL;
}

enum ain_data_type array_type(enum ain_data_type type) {
    switch (type) {
    case AIN_ARRAY_INT:       case AIN_REF_ARRAY_INT:       return AIN_INT;
    case AIN_ARRAY_FLOAT:     case AIN_REF_ARRAY_FLOAT:     return AIN_FLOAT;
    case AIN_ARRAY_STRING:    case AIN_REF_ARRAY_STRING:    return AIN_STRING;
    case AIN_ARRAY_STRUCT:    case AIN_REF_ARRAY_STRUCT:    return AIN_STRUCT;
    case AIN_ARRAY_FUNC_TYPE: case AIN_REF_ARRAY_FUNC_TYPE: return AIN_FUNC_TYPE;
    case AIN_ARRAY_BOOL:      case AIN_REF_ARRAY_BOOL:      return AIN_BOOL;
    case AIN_ARRAY_LONG_INT:  case AIN_REF_ARRAY_LONG_INT:  return AIN_LONG_INT;
    case AIN_ARRAY_DELEGATE:  case AIN_REF_ARRAY_DELEGATE:  return AIN_DELEGATE;
    default: return AIN_VOID;
    }
}

int alloc_struct(int no) {
    struct ain_struct *st = &ain->structures[no];
    struct page *p = alloc_page(STRUCT_PAGE, no, st->nr_members);
    for (int m = 0; m < st->nr_members; ) {
        int w = variable_init_typed(&p->values[m], &st->members[m].type, false, false);
        m += (w > 0 ? w : 1);
    }
    return heap_alloc_page(p);
}

struct page *alloc_array(int rank, union vm_value *dimensions, enum ain_data_type data_type, int struct_type, bool init_structs) {
    (void)init_structs;
    int len = dimensions ? dimensions[0].i : 0;
    struct page *p = alloc_page(ARRAY_PAGE, data_type, len);
    p->array.rank = rank;
    p->array.struct_type = struct_type;
    return p;
}

struct page *v12_array_new(const struct ain_type *elem_type, int len, bool init_structs) {
    (void)init_structs;
    int w = ain_type_slot_width(elem_type);
    struct page *p = alloc_page(ARRAY_PAGE, AIN_ARRAY, len * w);
    p->array.rank = 1;
    p->array.struct_type = elem_type->struc;
    p->array.element_type = *elem_type;
    for (int k = 0; k < len; k++)
        variable_init_typed(&p->values[k * w], elem_type, false, false);
    return p;
}

int v12_array_length(struct page *page) {
    if (!page || page->type != ARRAY_PAGE) return 0;
    if (page->a_type == AIN_ARRAY) {
        int w = ain_type_slot_width(&page->array.element_type);
        return w > 0 ? page->nr_vars / w : 0;
    }
    return page->nr_vars;
}

struct page *delegate_clear(struct page *page) { return page; }

union vm_value global_get(int varno) { (void)varno; return vm_int(0); }
void global_set(int varno, union vm_value val, bool call_dtors) { (void)varno; (void)val; (void)call_dtors; }
void modifier_on_config_loaded(void) {}
void modifier_on_globals_loaded(void) {}
const char *display_utf0(const char *s) { return s ? s : ""; }
const char *display_sjis0(const char *s) { return s ? s : ""; }
const char *display_sjis1(const char *s) { return s ? s : ""; }
void _vm_error(const char *fmt, ...) { fprintf(stderr, "VM_ERROR: %s\n", fmt); abort(); }
"""


def _build_demo_ain_spec():
    """Shared rich AIN v14.1 schema with all Windows CSerializeSaver edge cases.

    Struct 0: IAction (interface)
    Struct 1: Child (implements IAction with vtable_offset=7)
    Struct 2: RootMain (all scalar/struct/wrap/iface/option/nosave/name2/array cases)
    Struct 3: RootAux (second root for multi-root objs[0]/sec_cnt[0] check)
    """
    t_void = (AIN_VOID, -1, None)
    t_int = (AIN_INT, -1, None)
    t_float = (AIN_FLOAT, -1, None)
    t_str = (AIN_STRING, -1, None)
    t_bool = (AIN_BOOL, -1, None)
    t_enum = (AIN_ENUM, 0, None)
    t_delg = (AIN_DELEGATE, 0, None)
    t_vtable = (AIN_ARRAY, -1, t_int)
    t_child = (AIN_STRUCT, 1, None)
    t_ref_child = (AIN_REF_STRUCT, 1, None)
    t_wrap_child = (AIN_WRAP, 1, t_child)
    t_iface_wrap = (AIN_IFACE_WRAP, 0, None)
    t_wrap_iface = (AIN_WRAP, 0, t_iface_wrap)
    t_iface = (AIN_IFACE, 0, None)
    t_opt_int = (AIN_OPTION, -1, t_int)
    t_opt_child = (AIN_OPTION, 1, t_child)
    t_opt_wrap_child = (AIN_OPTION, 1, t_wrap_child)
    t_opt_wrap_iface = (AIN_OPTION, 0, t_wrap_iface)
    t_arr_child = (AIN_ARRAY, 1, t_child)
    t_arr_wrap_child = (AIN_ARRAY, 1, t_wrap_child)

    structures = [
        # 0: IAction (interface)
        (b"IAction", [], []),
        # 1: Child (implements IAction, vtable_offset=7)
        (
            b"Child",
            [(0, 7)],
            [
                (b"<vtable>", b"<vtable>", t_vtable),
                (b"m_id", b"m_id", t_int),
                (b"ame>", b"<CGName>", t_str),  # name != name2!
                (b"m_ratio", b"m_ratio", t_float),
                (b"m_flag", b"m_flag", t_bool),
                (b"m_kind", b"m_kind", t_enum),
                (b"<OnChange>", b"", t_delg),  # [nosave] delegate
            ],
        ),
        # 2: RootMain
        (
            b"RootMain",
            [],
            [
                (b"<vtable>", b"<vtable>", t_vtable),             # 0
                (b"m_child_ok", b"m_child_ok", t_child),          # 1
                (b"m_child_null", b"m_child_null", t_child),      # 2
                (b"m_ref_null", b"m_ref_null", t_ref_child),      # 3
                (b"m_wrap_ok", b"m_wrap_ok", t_wrap_child),       # 4: t1=82, t2=21
                (b"m_wrap_null", b"m_wrap_null", t_wrap_child),   # 5: t1=82, t2=21
                (b"m_wiface_ok", b"m_wiface_ok", t_wrap_iface),   # 6: t1=82, t2=89 (width 2)
                (b"<void>", b"", t_void),                         # 7
                (b"m_wiface_null", b"m_wiface_null", t_wrap_iface),  # 8: t1=82, t2=89 (width 2)
                (b"<void>", b"", t_void),                         # 9
                (b"m_iface_null", b"m_iface_null", t_iface),      # 10: t1=89, t2=89 (width 2)
                (b"<void>", b"", t_void),                         # 11
                (b"m_opt_int_some", b"m_opt_int_some", t_opt_int),  # 12: t1=86, t2=10
                (b"<void>", b"", t_void),                         # 13
                (b"m_opt_int_none", b"m_opt_int_none", t_opt_int),  # 14: t1=86, t2=10
                (b"<void>", b"", t_void),                         # 15
                (b"m_opt_wrap_ok", b"m_opt_wrap_ok", t_opt_wrap_child),  # 16: t1=86, t2=21
                (b"<void>", b"", t_void),                         # 17
                (b"m_opt_wiface_ok", b"m_opt_wiface_ok", t_opt_wrap_iface),  # 18: t1=86, t2=89 (width 3!)
                (b"<void>", b"", t_void),                         # 19
                (b"<void>", b"", t_void),                         # 20
                (b"m_opt_child_none", b"m_opt_child_none", t_opt_child),  # 21: t1=86, t2=13
                (b"<void>", b"", t_void),                         # 22
                (b"m_nosave_struct", b"", t_child),               # 23: [nosave] -> placeholder -1
                (b"m_nosave_arr", b"", t_arr_child),              # 24: [nosave] -> placeholder 0
                (b"m_nosave_int", b"", t_int),                    # 25: [nosave] -> placeholder 0
                (b"m_arr_child", b"m_arr_child", t_arr_child),    # 26: array<Child> with [ok, null]
                (b"m_arr_wchild", b"m_arr_wchild", t_arr_wrap_child),  # 27: array<wrap<Child>>
                (b"sertString", b"m_insertString", t_str),        # 28: name != name2
                (b"m_empty_str", b"m_empty_str", t_str),          # 29
            ],
        ),
        # 3: RootAux (second root)
        (
            b"RootAux",
            [],
            [
                (b"m_tag", b"m_tag", t_int),
                (b"m_bool", b"m_bool", t_bool),
                (b"m_enum", b"m_enum", t_enum),
            ],
        ),
    ]

    expected_schemas = {
        b"Child": [
            (AIN_ARRAY, AIN_ARRAY, b"<vtable>"),
            (AIN_INT, AIN_INT, b"m_id"),
            (AIN_STRING, AIN_STRING, b"<CGName>"),
            (AIN_FLOAT, AIN_FLOAT, b"m_ratio"),
            (AIN_BOOL, AIN_BOOL, b"m_flag"),
            (AIN_ENUM, AIN_ENUM, b"m_kind"),
            (AIN_DELEGATE, AIN_DELEGATE, b""),
        ],
        b"RootMain": [
            (AIN_ARRAY, AIN_ARRAY, b"<vtable>"),
            (AIN_STRUCT, AIN_STRUCT, b"m_child_ok"),
            (AIN_STRUCT, AIN_STRUCT, b"m_child_null"),
            (AIN_REF_STRUCT, AIN_REF_STRUCT, b"m_ref_null"),
            (AIN_WRAP, AIN_REF_STRUCT, b"m_wrap_ok"),
            (AIN_WRAP, AIN_REF_STRUCT, b"m_wrap_null"),
            (AIN_WRAP, AIN_IFACE, b"m_wiface_ok"),
            (AIN_VOID, AIN_VOID, b""),
            (AIN_WRAP, AIN_IFACE, b"m_wiface_null"),
            (AIN_VOID, AIN_VOID, b""),
            (AIN_IFACE, AIN_IFACE, b"m_iface_null"),
            (AIN_VOID, AIN_VOID, b""),
            (AIN_OPTION, AIN_INT, b"m_opt_int_some"),
            (AIN_VOID, AIN_VOID, b""),
            (AIN_OPTION, AIN_INT, b"m_opt_int_none"),
            (AIN_VOID, AIN_VOID, b""),
            (AIN_OPTION, AIN_REF_STRUCT, b"m_opt_wrap_ok"),
            (AIN_VOID, AIN_VOID, b""),
            (AIN_OPTION, AIN_IFACE, b"m_opt_wiface_ok"),
            (AIN_VOID, AIN_VOID, b""),
            (AIN_VOID, AIN_VOID, b""),
            (AIN_OPTION, AIN_STRUCT, b"m_opt_child_none"),
            (AIN_VOID, AIN_VOID, b""),
            (AIN_STRUCT, AIN_STRUCT, b""),
            (AIN_ARRAY, AIN_ARRAY, b""),
            (AIN_INT, AIN_INT, b""),
            (AIN_ARRAY, AIN_ARRAY, b"m_arr_child"),
            (AIN_ARRAY, AIN_ARRAY, b"m_arr_wchild"),
            (AIN_STRING, AIN_STRING, b"m_insertString"),
            (AIN_STRING, AIN_STRING, b"m_empty_str"),
        ],
        b"RootAux": [
            (AIN_INT, AIN_INT, b"m_tag"),
            (AIN_BOOL, AIN_BOOL, b"m_bool"),
            (AIN_ENUM, AIN_ENUM, b"m_enum"),
        ],
    }
    return structures, expected_schemas


def _build_broken_legacy_gdss(comment: bytes) -> bytes:
    """Build a legacy AliceHub GD\x01\x01 v9 save with ALL the old PC-incompatible bugs:
    - sec_cnt[0] = 1 & objs[0].val_idxs = [0] despite nr_roots = 2
    - null structs encoded as 0 instead of -1 (fields & struct array elements)
    - AIN_WRAP stripped to 13/89, missing <void> slots after wrap<iface_wrap> & iface
    - 3-slot AIN_OPTION<wrap<iface_wrap>> emitted with only 1 <void> slot
    - member->name ("ame>", "sertString") used instead of member->name2
    - [nosave] member ("m_nosave_struct") serialized by name with value 0
    """
    # Objects:
    # 0: root container (BROKEN: only [0] instead of [0, 1])
    # 1: RootMain
    # 2: Child (m_id=77, name="cg_hero", ratio=2.5, flag=1, kind=3)
    # 3: RootAux (m_tag=999, m_bool=1, m_enum=5)
    strings = [b"cg_hero", b"inserted_val"]
    vals = []

    def add_val(v: int) -> int:
        idx = len(vals)
        vals.append(v)
        return idx

    add_val(0)  # vals[0] = 0

    # Schema 0: RootMain (OLD broken AliceHub layout: stripped wrap, missing <void> slots, old names)
    s0_fields = [
        (AIN_ARRAY, AIN_ARRAY, b"<vtable>"),
        (AIN_STRUCT, AIN_STRUCT, b"m_child_ok"),
        (AIN_STRUCT, AIN_STRUCT, b"m_child_null"),          # BROKEN 0
        (AIN_STRUCT, AIN_STRUCT, b"m_ref_null"),            # BROKEN 0 & stripped 21->13
        (AIN_STRUCT, AIN_STRUCT, b"m_wrap_ok"),             # BROKEN stripped 82->13
        (AIN_STRUCT, AIN_STRUCT, b"m_wrap_null"),           # BROKEN 0 & stripped 82->13
        (AIN_IFACE, AIN_IFACE, b"m_wiface_ok"),             # BROKEN stripped 82->89 & MISSING <void>
        (AIN_IFACE, AIN_IFACE, b"m_wiface_null"),           # BROKEN 0 & stripped 82->89 & MISSING <void>
        (AIN_IFACE, AIN_IFACE, b"m_iface_null"),            # BROKEN 0 & MISSING <void>
        (AIN_OPTION, AIN_INT, b"m_opt_int_some"),
        (AIN_VOID, AIN_VOID, b""),
        (AIN_OPTION, AIN_INT, b"m_opt_int_none"),
        (AIN_VOID, AIN_VOID, b""),
        (AIN_OPTION, AIN_STRUCT, b"m_opt_wrap_ok"),         # BROKEN type2=13 instead of 21
        (AIN_VOID, AIN_VOID, b""),
        (AIN_OPTION, AIN_IFACE, b"m_opt_wiface_ok"),        # BROKEN only 1 <void> instead of 2
        (AIN_VOID, AIN_VOID, b""),
        (AIN_OPTION, AIN_STRUCT, b"m_opt_child_none"),
        (AIN_VOID, AIN_VOID, b""),
        (AIN_STRUCT, AIN_STRUCT, b"m_nosave_struct"),       # BROKEN [nosave] saved by name with 0
        (AIN_INT, AIN_INT, b"m_nosave_int"),                # BROKEN [nosave] saved by name
        (AIN_ARRAY, AIN_ARRAY, b"m_arr_child"),
        (AIN_ARRAY, AIN_ARRAY, b"m_arr_wchild"),
        (AIN_STRING, AIN_STRING, b"sertString"),            # BROKEN name instead of name2
        (AIN_STRING, AIN_STRING, b"m_empty_str"),
    ]
    o1_vidxs = [
        0,                  # <vtable>
        add_val(2),         # m_child_ok -> Child (oid 2)
        add_val(0),         # m_child_null -> BROKEN 0!
        add_val(0),         # m_ref_null -> BROKEN 0!
        add_val(2),         # m_wrap_ok -> Child (oid 2)
        add_val(0),         # m_wrap_null -> BROKEN 0!
        add_val(2),         # m_wiface_ok -> Child (oid 2)
        add_val(0),         # m_wiface_null -> BROKEN 0!
        add_val(0),         # m_iface_null -> BROKEN 0!
        add_val(42), add_val(0),        # m_opt_int_some = Some(42)
        add_val(-1), add_val(1),        # m_opt_int_none = None
        add_val(2), add_val(0),         # m_opt_wrap_ok = Some(Child)
        add_val(2), add_val(0),         # m_opt_wiface_ok = Some(Child) (old 2-slot)
        add_val(0), add_val(1),         # m_opt_child_none = None (BROKEN 0)
        add_val(0),         # m_nosave_struct -> BROKEN 0
        add_val(1234),      # m_nosave_int
        add_val(1),         # m_arr_child -> array[1]
        add_val(2),         # m_arr_wchild -> array[2]
        add_val(1),         # sertString -> strings[1] ("inserted_val")
        add_val(0x7FFFFFFF),  # m_empty_str
    ]

    # Schema 1: Child (old name "ame>" instead of "<CGName>")
    s1_fields = [
        (AIN_ARRAY, AIN_ARRAY, b"<vtable>"),
        (AIN_INT, AIN_INT, b"m_id"),
        (AIN_STRING, AIN_STRING, b"ame>"),                  # BROKEN name instead of name2
        (AIN_FLOAT, AIN_FLOAT, b"m_ratio"),
        (AIN_BOOL, AIN_BOOL, b"m_flag"),
        (AIN_ENUM, AIN_ENUM, b"m_kind"),
        (AIN_DELEGATE, AIN_DELEGATE, b"<OnChange>"),        # BROKEN [nosave] saved by name
    ]
    ratio_bits = struct.unpack("<i", struct.pack("<f", 2.5))[0]
    o2_vidxs = [0, add_val(77), add_val(0), add_val(ratio_bits), add_val(1), add_val(3), 0]

    # Schema 2: RootAux
    s2_fields = [
        (AIN_INT, AIN_INT, b"m_tag"),
        (AIN_BOOL, AIN_BOOL, b"m_bool"),
        (AIN_ENUM, AIN_ENUM, b"m_enum"),
    ]
    o3_vidxs = [add_val(999), add_val(1), add_val(5)]

    schemas = [
        (b"RootMain", s0_fields),
        (b"Child", s1_fields),
        (b"RootAux", s2_fields),
    ]
    # Arrays: [0] empty, [1] array<Child> with BROKEN 0 null element, [2] array<wrap<Child>> with BROKEN elem_type=13 & 0
    arrays = [
        (AIN_INT, []),
        (AIN_STRUCT, [2, 0]),
        (AIN_STRUCT, [0, 2]),
    ]
    objs = [
        (-1, [0]),          # BROKEN: only 1 val_idx despite 2 roots!
        (0, o1_vidxs),
        (1, o2_vidxs),
        (2, o3_vidxs),
    ]
    roots = [1, 3]

    raw = bytearray()
    raw.extend(b"serialize_struct\x00")
    raw.extend(struct.pack("<II", 1000, 9))
    raw.extend(b"\x00" * (7 * 8))
    raw.extend(b"serialize_struct\x00")

    c1_off = len(raw)
    for sdef, vidxs in objs:
        raw.extend(struct.pack("<iI", sdef, len(vidxs)))
        for vi in vidxs:
            raw.extend(struct.pack("<I", vi))

    c2_off = len(raw)
    for rid in roots:
        raw.extend(struct.pack("<iiiiB", AIN_STRUCT, AIN_STRUCT, rid, 0, 0))
    raw.extend(struct.pack("<iiiiB", 0, 0, 0, 0, 0))

    c3_off = len(raw)
    for s in strings:
        raw.extend(s + b"\x00")

    c4_off = len(raw)
    for et, elems in arrays:
        raw.extend(struct.pack("<iIIIi", 1, len(elems), 1, len(elems), et))
        for ev in elems:
            raw.extend(struct.pack("<i", ev))

    c5_off = len(raw)
    for v in vals:
        raw.extend(struct.pack("<i", v))
    raw.extend(struct.pack("<I", len(schemas)))
    for sname, fields in schemas:
        raw.extend(sname + b"\x00")
        raw.extend(struct.pack("<I", len(fields)))
        for t1, t2, fname in fields:
            raw.extend(struct.pack("<ii", t1, t2))
            raw.extend(fname + b"\x00")

    c6_off = len(raw)
    raw.extend(struct.pack("<I", len(comment)))
    raw.extend(comment)

    sec_off = [64, c1_off, c2_off, c3_off, c4_off, c5_off, c6_off]
    sec_cnt = [1, len(objs), len(roots) + 1, len(strings), len(arrays), len(vals), 1]  # BROKEN sec_cnt[0]=1!
    for i in range(7):
        struct.pack_into("<II", raw, 25 + i * 8, sec_off[i], sec_cnt[i])

    comp = zlib.compress(bytes(raw), 1)
    return b"GD\x01\x01" + struct.pack("<I", len(raw)) + comp


class SavedataPcExportTest(unittest.TestCase):
    _obj_tmpdir = None
    _shared_objs: list[Path] = []
    _cflags: list[str] = []

    @classmethod
    def setUpClass(cls):
        cc = shutil.which("cc") or shutil.which("gcc")
        assert cc, "C compiler required"
        cls._cc = cc
        cls._cflags = [
            "-std=c11",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Wno-unused-parameter",
            "-Wno-unused-function",
            "-Wno-sign-compare",
            "-Wno-type-limits",
            "-D_DEFAULT_SOURCE",
            "-DUSE_GLES",
            f"-I{ROOT / 'include'}",
            f"-I{ROOT / 'src'}",
            f"-I{ROOT / 'src' / 'hll'}",
            f"-I{ROOT / 'src' / 'parts'}",
            f"-I{ROOT / 'subprojects' / 'libsys4' / 'include'}",
            *sdl_cflags(),
        ]
        cls._obj_tmpdir = tempfile.TemporaryDirectory()
        obj_dir = Path(cls._obj_tmpdir.name)
        cls._shared_objs = []
        sources = [ROOT / "src/savedata.c", ROOT / "src/cJSON.c", *LIBSYS4_WITH_SAVEFILE]
        for idx, src in enumerate(sources):
            obj = obj_dir / f"{idx}_{src.stem}.o"
            res = subprocess.run(
                [cc, *cls._cflags, "-c", str(src), "-o", str(obj)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                errors="replace",
            )
            if res.returncode != 0:
                raise AssertionError(f"Failed to compile {src}:\n{res.stderr}")
            cls._shared_objs.append(obj)

    @classmethod
    def tearDownClass(cls):
        if cls._obj_tmpdir is not None:
            cls._obj_tmpdir.cleanup()
            cls._obj_tmpdir = None
            cls._shared_objs = []

    @classmethod
    def run_harness(cls, c_src: str, args: list[str] | None = None) -> str:
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            src_path = tmp / "harness.c"
            bin_path = tmp / "harness"
            src_path.write_text(c_src, encoding="utf-8")
            cmd = [
                cls._cc,
                *cls._cflags,
                str(src_path),
                *[str(o) for o in cls._shared_objs],
                "-lz",
                "-lm",
                "-o",
                str(bin_path),
            ]
            res_c = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors="replace")
            if res_c.returncode != 0:
                raise AssertionError(f"Compile failed ({res_c.returncode}):\n{res_c.stderr}")
            res_r = subprocess.run(
                [str(bin_path), *(args or [])],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                errors="replace",
            )
            if res_r.returncode != 0:
                raise AssertionError(
                    f"Run failed ({res_r.returncode}):\nstdout:\n{res_r.stdout}\nstderr:\n{res_r.stderr}"
                )
            return res_r.stdout

    def test_live_serialize_deserialize_roundtrip_and_windows_validator(self):
        """Test live serialize_structs / deserialize_structs against strict Windows CSerializeLoader."""
        structures, expected_schemas = _build_demo_ain_spec()
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            ain_path = tmp / "demo.ain"
            ain_path.write_bytes(build_synthetic_ain(14, structures, minor_version=1))
            out_asd = tmp / "live_roundtrip.asd"

            harness = SAVEDATA_VM_STUBS + r"""
static int make_str_slot(const char *s) {
    return heap_alloc_string(make_string(s, (unsigned)strlen(s)));
}

int main(int argc, char **argv) {
    assert(argc == 3);
    snprintf(g_test_savedir, sizeof(g_test_savedir), "%s", argv[2]);
    snprintf(g_test_gamedir, sizeof(g_test_gamedir), "%s", argv[2]);

    int err = 0;
    ain = ain_open(argv[1], &err);
    assert(ain && err == AIN_SUCCESS);
    assert(ain->version == 14 && ain->minor_version == 1);

    // Build Child (struct 1)
    int child_slot = alloc_struct(1);
    struct page *cp = heap[child_slot].page;
    cp->values[1].i = 77;
    cp->values[2].i = make_str_slot("cg_hero");
    union { float f; int32_t i; } rf = { .f = 2.5f };
    cp->values[3].i = rf.i;
    cp->values[4].i = 1;
    cp->values[5].i = 3;

    // Build [nosave] child that must NOT be serialized or clobbered on load
    int nosave_child = alloc_struct(1);
    heap[nosave_child].page->values[1].i = 555;

    // Build array<Child> with [child_slot, -1]
    struct ain_type et_child = { .data = AIN_STRUCT, .struc = 1 };
    struct page *arr1 = v12_array_new(&et_child, 2, false);
    arr1->values[0].i = child_slot;
    arr1->values[1].i = -1;
    int arr1_slot = heap_alloc_page(arr1);

    // Build array<wrap<Child>> with [-1, child_slot]
    struct ain_type et_wchild = { .data = AIN_WRAP, .struc = 1, .rank = 1, .array_type = &et_child };
    struct page *arr2 = v12_array_new(&et_wchild, 2, false);
    arr2->values[0].i = -1;
    arr2->values[1].i = child_slot;
    int arr2_slot = heap_alloc_page(arr2);

    // Build RootMain (struct 2)
    int r1_slot = alloc_struct(2);
    struct page *r1 = heap[r1_slot].page;
    r1->values[1].i = child_slot;   // m_child_ok
    r1->values[2].i = -1;           // m_child_null
    r1->values[3].i = -1;           // m_ref_null
    r1->values[4].i = child_slot;   // m_wrap_ok
    r1->values[5].i = -1;           // m_wrap_null
    r1->values[6].i = child_slot;   // m_wiface_ok (slot 6)
    r1->values[7].i = 7;            // vtable offset (slot 7)
    r1->values[8].i = -1;           // m_wiface_null
    r1->values[9].i = 0;
    r1->values[10].i = -1;          // m_iface_null
    r1->values[11].i = 0;
    r1->values[12].i = 42;          // m_opt_int_some = Some(42)
    r1->values[13].i = 0;
    r1->values[14].i = -1;          // m_opt_int_none = None
    r1->values[15].i = 1;
    r1->values[16].i = child_slot;  // m_opt_wrap_ok = Some(child)
    r1->values[17].i = 0;
    r1->values[18].i = child_slot;  // m_opt_wiface_ok = Some(iface) (width 3)
    r1->values[19].i = 7;
    r1->values[20].i = 0;
    r1->values[21].i = -1;          // m_opt_child_none = None
    r1->values[22].i = 1;
    r1->values[23].i = nosave_child;  // m_nosave_struct ([nosave])
    r1->values[24].i = arr1_slot;     // m_nosave_arr ([nosave])
    r1->values[25].i = 888;           // m_nosave_int ([nosave])
    r1->values[26].i = arr1_slot;     // m_arr_child
    r1->values[27].i = arr2_slot;     // m_arr_wchild
    r1->values[28].i = make_str_slot("inserted_val");
    r1->values[29].i = make_str_slot("");

    // Build RootAux (struct 3)
    int r2_slot = alloc_struct(3);
    struct page *r2 = heap[r2_slot].page;
    r2->values[0].i = 999;
    r2->values[1].i = 1;
    r2->values[2].i = 5;

    // Multi-root list [RootMain, RootAux]
    struct ain_type et_root = { .data = AIN_STRUCT, .struc = -1 };
    struct page *roots = v12_array_new(&et_root, 2, false);
    roots->values[0].i = r1_slot;
    roots->values[1].i = r2_slot;

    assert(serialize_structs("live_roundtrip.asd", roots, true) == 1);
    struct string *cstr = make_string("SLOT-01 COMMENT", 15);
    assert(write_serialize_struct_comment("live_roundtrip.asd", cstr, true) == 1);
    free_string(cstr);

    // Read comment back
    struct string *c_read = NULL;
    assert(read_serialize_struct_comment("live_roundtrip.asd", &c_read, true) == 1);
    assert(c_read && strcmp(c_read->text, "SLOT-01 COMMENT") == 0);
    free_string(c_read);

    // Deserialize into brand-new roots with pre-populated [nosave] runtime values
    int nr1_slot = alloc_struct(2);
    struct page *nr1 = heap[nr1_slot].page;
    nr1->values[23].i = nosave_child;  // Must be preserved!
    nr1->values[25].i = 7777;          // Must be preserved!
    int nr2_slot = alloc_struct(3);
    struct page *nroots = v12_array_new(&et_root, 2, false);
    nroots->values[0].i = nr1_slot;
    nroots->values[1].i = nr2_slot;

    assert(deserialize_structs("live_roundtrip.asd", nroots, true) == 1);

    // Verify every single field!
    assert(nr1->values[1].i > 0 && page_index_valid(nr1->values[1].i));
    struct page *nc = heap[nr1->values[1].i].page;
    assert(nc->values[1].i == 77);
    assert(strcmp(heap_get_string(nc->values[2].i)->text, "cg_hero") == 0);
    assert(nc->values[3].i == rf.i);
    assert(nc->values[4].i == 1);
    assert(nc->values[5].i == 3);
    assert(nr1->values[2].i == -1);
    assert(nr1->values[3].i == -1);
    assert(nr1->values[4].i == nr1->values[1].i);
    assert(nr1->values[5].i == -1);
    assert(nr1->values[6].i == nr1->values[1].i);
    assert(nr1->values[7].i == 7);  // vtable offset resolved!
    assert(nr1->values[8].i == -1 && nr1->values[9].i == 0);
    assert(nr1->values[10].i == -1 && nr1->values[11].i == 0);
    assert(nr1->values[12].i == 42 && nr1->values[13].i == 0);
    assert(nr1->values[14].i == -1 && nr1->values[15].i == 1);
    assert(nr1->values[16].i == nr1->values[1].i && nr1->values[17].i == 0);
    assert(nr1->values[18].i == nr1->values[1].i && nr1->values[19].i == 7 && nr1->values[20].i == 0);
    assert(nr1->values[21].i == -1 && nr1->values[22].i == 1);
    // [nosave] fields MUST NOT have been clobbered!
    assert(nr1->values[23].i == nosave_child);
    assert(nr1->values[25].i == 7777);
    // Arrays with null elements
    struct page *na1 = heap_get_page(nr1->values[26].i);
    assert(na1 && na1->nr_vars == 2);
    assert(na1->values[0].i == nr1->values[1].i);
    assert(na1->values[1].i == -1);
    struct page *na2 = heap_get_page(nr1->values[27].i);
    assert(na2 && na2->nr_vars == 2);
    assert(na2->values[0].i == -1);
    assert(na2->values[1].i == nr1->values[1].i);
    assert(strcmp(heap_get_string(nr1->values[28].i)->text, "inserted_val") == 0);
    assert(strcmp(heap_get_string(nr1->values[29].i)->text, "") == 0);

    // Second root
    struct page *nr2 = heap[nr2_slot].page;
    assert(nr2->values[0].i == 999 && nr2->values[1].i == 1 && nr2->values[2].i == 5);

    free_page(roots);
    free_page(nroots);
    test_heap_reset();
    ain_free(ain);
    puts("OK");
    return 0;
}
"""
            out = self.run_harness(
                harness,
                args=[str(ain_path), str(tmp)],
            )
            self.assertEqual(out.strip(), "OK")

            # Strict Windows CSerializeLoader validation on the emitted save!
            parsed = validate_windows_gdss(
                out_asd,
                expected_schemas=expected_schemas,
                expected_roots=2,
                expected_comment=b"SLOT-01 COMMENT",
            )
            # Confirm [nosave] child (m_id=555) was NOT serialized into C1!
            # Only objs[0], RootMain, Child(77), RootAux -> 4 objects total!
            self.assertEqual(len(parsed["objs"]), 4)
            # Confirm array<wrap<Child>> has elem_type == AIN_REF_STRUCT (21) and null elem == -1
            arr_types = [et for et, _ in parsed["arrays"][1:]]
            self.assertIn(AIN_STRUCT, arr_types)
            self.assertIn(AIN_REF_STRUCT, arr_types)

    def test_legacy_broken_gdss_backcompat_and_pc_export_repair(self):
        """Old broken AliceHub GD\x01\x01 still loads in AliceHub AND repairs to Windows-exact on export."""
        structures, expected_schemas = _build_demo_ain_spec()
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            ain_path = tmp / "demo.ain"
            ain_path.write_bytes(build_synthetic_ain(14, structures, minor_version=1))

            legacy_path = tmp / "legacy_broken.asd"
            legacy_comment = b"LEGACY\x82\xa0COMMENT"
            legacy_path.write_bytes(_build_broken_legacy_gdss(legacy_comment))

            # Sanity check: the strict Windows validator MUST REJECT the legacy broken file!
            with self.assertRaises(AssertionError):
                validate_windows_gdss(legacy_path, expected_schemas=expected_schemas, expected_roots=2)

            out_no_ain = tmp / "exported_no_ain.asd"
            out_with_ain = tmp / "exported_with_ain.asd"

            harness = SAVEDATA_VM_STUBS + r"""
int main(int argc, char **argv) {
    assert(argc == 5);
    const char *ain_path = argv[1];
    const char *dir = argv[2];
    const char *out_no_ain = argv[3];
    const char *out_with_ain = argv[4];
    snprintf(g_test_savedir, sizeof(g_test_savedir), "%s", dir);

    // 1. Export WITHOUT VM globals initialized (ain == NULL!) — must never touch VM globals!
    char legacy_full[1024];
    snprintf(legacy_full, sizeof(legacy_full), "%s/legacy_broken.asd", dir);
    assert(ain == NULL);
    assert(savedata_export_pc_file(legacy_full, out_no_ain));
    assert(savedata_export_pc_file_with_ain(legacy_full, out_with_ain, ain_path));
    assert(ain == NULL);

    // 2. Load BOTH the old broken legacy save AND the repaired save in deserialize_structs!
    int err = 0;
    ain = ain_open(ain_path, &err);
    assert(ain && err == AIN_SUCCESS);

    const char *files[2] = { "legacy_broken.asd", "exported_with_ain.asd" };
    for (int f = 0; f < 2; f++) {
        test_heap_reset();
        int r1_slot = alloc_struct(2);
        int r2_slot = alloc_struct(3);
        struct page *r1 = heap[r1_slot].page;
        r1->values[25].i = 4321;  // pre-existing [nosave] int
        struct ain_type et_root = { .data = AIN_STRUCT, .struc = -1 };
        struct page *roots = v12_array_new(&et_root, 2, false);
        roots->values[0].i = r1_slot;
        roots->values[1].i = r2_slot;

        assert(deserialize_structs(files[f], roots, true) == 1);

        assert(r1->values[1].i > 0 && page_index_valid(r1->values[1].i));
        struct page *c = heap[r1->values[1].i].page;
        assert(c->values[1].i == 77);
        assert(strcmp(heap_get_string(c->values[2].i)->text, "cg_hero") == 0);
        assert(c->values[4].i == 1 && c->values[5].i == 3);
        // Null structs (legacy 0 and repaired -1) both load as -1!
        assert(r1->values[2].i == -1);
        assert(r1->values[3].i == -1);
        assert(r1->values[4].i == r1->values[1].i);
        assert(r1->values[5].i == -1);
        assert(r1->values[6].i == r1->values[1].i && r1->values[7].i == 7);
        assert(r1->values[8].i == -1 && r1->values[9].i == 0);
        assert(r1->values[10].i == -1 && r1->values[11].i == 0);
        assert(r1->values[12].i == 42 && r1->values[13].i == 0);
        assert(r1->values[14].i == -1 && r1->values[15].i == 1);
        assert(r1->values[16].i == r1->values[1].i && r1->values[17].i == 0);
        assert(r1->values[18].i == r1->values[1].i && r1->values[19].i == 7 && r1->values[20].i == 0);
        assert(r1->values[21].i == -1 && r1->values[22].i == 1);
        // Repaired save preserves [nosave] int cleanly!
        if (f == 1)
            assert(r1->values[25].i == 4321);
        struct page *a1 = heap_get_page(r1->values[26].i);
        assert(a1 && a1->nr_vars == 2 && a1->values[0].i == r1->values[1].i && a1->values[1].i == -1);
        struct page *a2 = heap_get_page(r1->values[27].i);
        assert(a2 && a2->nr_vars == 2 && a2->values[0].i == -1 && a2->values[1].i == r1->values[1].i);
        assert(strcmp(heap_get_string(r1->values[28].i)->text, "inserted_val") == 0);
        assert(strcmp(heap_get_string(r1->values[29].i)->text, "") == 0);

        struct page *r2 = heap[r2_slot].page;
        assert(r2->values[0].i == 999 && r2->values[1].i == 1 && r2->values[2].i == 5);
        free_page(roots);
    }

    test_heap_reset();
    ain_free(ain);
    puts("OK");
    return 0;
}
"""
            out = self.run_harness(
                harness,
                args=[str(ain_path), str(tmp), str(out_no_ain), str(out_with_ain)],
            )
            self.assertEqual(out.strip(), "OK")

            # Without .ain: null structs 0 -> -1, multi-root objs[0], verbatim C6 comment!
            validate_windows_gdss(
                out_no_ain,
                expected_schemas=None,
                expected_roots=2,
                expected_comment=legacy_comment,
            )
            # With .ain: passes FULL strict Windows schema + value + slot + comment check!
            repaired = validate_windows_gdss(
                out_with_ain,
                expected_schemas=expected_schemas,
                expected_roots=2,
                expected_comment=legacy_comment,
            )
            # Confirm array<wrap<Child>> elem_type was also repaired to AIN_REF_STRUCT (21)!
            self.assertEqual(repaired["arrays"][2][0], AIN_REF_STRUCT)
            self.assertEqual(repaired["arrays"][2][1], [-1, 2])
            self.assertEqual(repaired["arrays"][1][1], [2, -1])

    @unittest.skipUnless(DOHNADOHNA_AIN.exists(), "dohnadohna.ain not present")
    def test_real_dohnadohna_ain_live_schema_and_localgame_roundtrip(self):
        """Verify live serialize/deserialize & export repair against real dohnadohna.ain."""
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            out_asd = tmp / "dd_localgame.asd"
            repaired_asd = tmp / "dd_repaired.asd"

            harness = SAVEDATA_VM_STUBS + r"""
static int32_t expected_type2(const struct ain_type *t) {
    if (!t) return AIN_VOID;
    if (t->data == AIN_WRAP) {
        if (!t->array_type) return AIN_WRAP;
        switch (t->array_type->data) {
        case AIN_INT:        return AIN_REF_INT;
        case AIN_FLOAT:      return AIN_REF_FLOAT;
        case AIN_STRING:     return AIN_REF_STRING;
        case AIN_STRUCT:     return AIN_REF_STRUCT;
        case AIN_BOOL:       return AIN_REF_BOOL;
        case AIN_LONG_INT:   return AIN_REF_LONG_INT;
        case AIN_DELEGATE:   return AIN_REF_DELEGATE;
        case AIN_ARRAY:      return AIN_REF_ARRAY;
        case AIN_WRAP:       return AIN_WRAP;
        case AIN_OPTION:     return AIN_REF_OPTION;
        case AIN_ENUM:       return AIN_REF_ENUM;
        case AIN_IFACE_WRAP: return AIN_IFACE;
        case AIN_FUNC_TYPE:  return AIN_REF_FUNC_TYPE;
        default:             return (int32_t)t->array_type->data;
        }
    }
    if (t->data == AIN_OPTION)
        return t->array_type ? expected_type2(t->array_type) : AIN_VOID;
    return (int32_t)t->data;
}

int main(int argc, char **argv) {
    assert(argc == 4);
    const char *ain_path = argv[1];
    const char *dir = argv[2];
    const char *repaired_path = argv[3];
    snprintf(g_test_savedir, sizeof(g_test_savedir), "%s", dir);

    int err = 0;
    ain = ain_open(ain_path, &err);
    assert(ain && err == AIN_SUCCESS);

    int lg_sno = ain_get_struct(ain, "LocalGame");
    int bc_sno = ain_get_struct(ain, "BattleContext");
    int bp_sno = ain_get_struct(ain, "BattlePlayer");
    int pa_sno = ain_get_struct(ain, "PlayerAction");
    int nsfw_sno = ain_get_struct(ain, "NsfwCGParts");
    int ach_sno = ain_get_struct(ain, "AchievementCollection");
    assert(lg_sno >= 0 && bc_sno >= 0 && bp_sno >= 0 && pa_sno >= 0 && nsfw_sno >= 0 && ach_sno >= 0);

    // BFS all reachable structs from LocalGame and verify every member's compute_type2 & name2
    uint8_t *vis = xcalloc((size_t)ain->nr_structures, 1);
    int *q = xmalloc((size_t)ain->nr_structures * sizeof(int));
    int qh = 0, qt = 0;
    vis[lg_sno] = 1; q[qt++] = lg_sno;
    while (qh < qt) {
        int s = q[qh++];
        struct ain_struct *st = &ain->structures[s];
        for (int m = 0; m < st->nr_members; m++) {
            const struct ain_type *t = &st->members[m].type;
            assert(st->members[m].name2 != NULL);
            int32_t t2 = expected_type2(t);
            if (t->data == AIN_WRAP) {
                assert(t->array_type != NULL);
                if (t->array_type->data == AIN_STRUCT) assert(t2 == AIN_REF_STRUCT);
                if (t->array_type->data == AIN_IFACE_WRAP) assert(t2 == AIN_IFACE);
            }
            const struct ain_type *cur = t;
            while (cur) {
                if ((cur->data == AIN_STRUCT || cur->data == AIN_REF_STRUCT
                        || cur->data == AIN_IFACE || cur->data == AIN_IFACE_WRAP)
                        && cur->struc >= 0 && cur->struc < ain->nr_structures && !vis[cur->struc]) {
                    vis[cur->struc] = 1;
                    q[qt++] = cur->struc;
                }
                cur = cur->array_type;
            }
        }
    }
    assert(qt >= 85);

    // Allocate a live multi-root graph covering LocalGame, BattleContext, BattlePlayer,
    // PlayerAction (width-3 option!), NsfwCGParts (name!=name2), AchievementCollection ([nosave])
    int lg_slot = alloc_struct(lg_sno);
    int bc_slot = alloc_struct(bc_sno);
    int bp_slot = alloc_struct(bp_sno);
    int pa_slot = alloc_struct(pa_sno);
    int nsfw_slot = alloc_struct(nsfw_sno);
    int ach_slot = alloc_struct(ach_sno);

    // Populate LocalGame.m_battleContext = Some(bc_slot)
    struct page *lg_p = heap[lg_slot].page;
    lg_p->values[16].i = bc_slot;
    lg_p->values[17].i = 0;  // Some!
    lg_p->values[39].i = 1;  // <DisableAutoSave> = true

    // Populate NsfwCGParts[10] (name="ame>", name2="<CGName>")
    struct page *nsfw_p = heap[nsfw_slot].page;
    nsfw_p->values[10].i = heap_alloc_string(make_string("cg_test_01", 10));

    // Populate PlayerAction.m_innerAction = None (slots 2,3,4) and <Player> = bp_slot (slot 5)
    struct page *pa_p = heap[pa_slot].page;
    pa_p->values[5].i = bp_slot;

    // Put all 5 roots into serialize_structs
    struct ain_type et_root = { .data = AIN_STRUCT, .struc = -1 };
    struct page *roots = v12_array_new(&et_root, 5, false);
    roots->values[0].i = lg_slot;
    roots->values[1].i = bp_slot;
    roots->values[2].i = pa_slot;
    roots->values[3].i = nsfw_slot;
    roots->values[4].i = ach_slot;

    assert(serialize_structs("dd_localgame.asd", roots, true) == 1);

    // Roundtrip deserialize back into fresh pages
    int n_lg = alloc_struct(lg_sno);
    int n_bp = alloc_struct(bp_sno);
    int n_pa = alloc_struct(pa_sno);
    int n_nsfw = alloc_struct(nsfw_sno);
    int n_ach = alloc_struct(ach_sno);
    // Pre-set [nosave] AchievementCollection.m_achievements sentinel
    int ach_sentinel = heap_alloc_slot(VM_PAGE);
    heap[n_ach].page->values[1].i = ach_sentinel;

    struct page *nroots = v12_array_new(&et_root, 5, false);
    nroots->values[0].i = n_lg;
    nroots->values[1].i = n_bp;
    nroots->values[2].i = n_pa;
    nroots->values[3].i = n_nsfw;
    nroots->values[4].i = n_ach;

    assert(deserialize_structs("dd_localgame.asd", nroots, true) == 1);
    assert(heap[n_lg].page->values[16].i > 0 && heap[n_lg].page->values[17].i == 0);
    assert(heap[n_lg].page->values[39].i == 1);
    assert(strcmp(heap_get_string(heap[n_nsfw].page->values[10].i)->text, "cg_test_01") == 0);
    assert(heap[n_pa].page->values[5].i == n_bp);
    assert(heap[n_ach].page->values[1].i == ach_sentinel);  // [nosave] untouched!

    // Export through savedata_export_pc_file_with_ain against real dohnadohna.ain (with ain=NULL!)
    struct ain *saved_ain = ain;
    ain = NULL;
    char src_full[1024];
    snprintf(src_full, sizeof(src_full), "%s/dd_localgame.asd", dir);
    assert(savedata_export_pc_file_with_ain(src_full, repaired_path, ain_path));
    ain = saved_ain;

    // Emit expected schema table for Python validator to double-check
    int check_snos[6] = { lg_sno, bc_sno, bp_sno, pa_sno, nsfw_sno, ach_sno };
    for (int ci = 0; ci < 6; ci++) {
        struct ain_struct *st = &ain->structures[check_snos[ci]];
        printf("SCHEMA %s %d\n", st->name, st->nr_members);
        for (int m = 0; m < st->nr_members; m++) {
            const char *n2 = st->members[m].name2 ? st->members[m].name2 : "";
            printf("FIELD %d %d %s\n", (int)st->members[m].type.data, expected_type2(&st->members[m].type), n2);
        }
    }

    free(vis);
    free(q);
    free_page(roots);
    free_page(nroots);
    test_heap_reset();
    ain_free(ain);
    puts("OK");
    return 0;
}
"""
            out = self.run_harness(
                harness,
                args=[str(DOHNADOHNA_AIN), str(tmp), str(repaired_asd)],
            )
            lines = out.strip().splitlines()
            self.assertEqual(lines[-1], "OK")

            # Parse exact expected schemas dumped from real dohnadohna.ain
            dd_expected: dict[bytes, list[tuple[int, int, bytes]]] = {}
            cur_name = None
            cur_fields = None
            for line in lines[:-1]:
                if line.startswith("SCHEMA "):
                    _, sname, _ = line.split(" ", 2)
                    cur_name = sname.encode("latin1")
                    cur_fields = []
                    dd_expected[cur_name] = cur_fields
                elif line.startswith("FIELD "):
                    parts = line.split(" ", 3)
                    t1 = int(parts[1])
                    t2 = int(parts[2])
                    fn = parts[3].encode("latin1") if len(parts) > 3 else b""
                    cur_fields.append((t1, t2, fn))

            self.assertIn(b"LocalGame", dd_expected)
            self.assertIn(b"PlayerAction", dd_expected)
            self.assertIn(b"NsfwCGParts", dd_expected)
            # Validate BOTH live serialized save AND exported save against real dohnadohna.ain!
            validate_windows_gdss(out_asd, expected_schemas=dd_expected, expected_roots=5)
            validate_windows_gdss(repaired_asd, expected_schemas=dd_expected, expected_roots=5)

    def test_x4ss_export_with_and_without_ain_and_unconvertible_rejection(self):
        """Test X4SS -> GD\x01\x01 conversion (with/without .ain) and unconvertible X4SS failure."""
        structures, expected_schemas = _build_demo_ain_spec()
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            ain_path = tmp / "demo.ain"
            ain_path.write_bytes(build_synthetic_ain(14, structures, minor_version=1))

            # Build an X4SS v1 save matching RootMain + Child + RootAux + array<Child>
            def put_u32(b: bytearray, v: int): b.extend(struct.pack("<I", v))
            def put_i32(b: bytearray, v: int): b.extend(struct.pack("<i", v))
            def put_cstr(b: bytearray, s: bytes): put_u32(b, len(s)); b.extend(s)
            def put_type(b: bytearray, dt: int, struc: int, sym: bytes, inner=None):
                put_i32(b, dt); put_i32(b, struc); put_i32(b, 1 if inner else 0); put_cstr(b, sym)
                if inner:
                    b.append(1)
                    put_type(b, *inner)
                else:
                    b.append(0)

            payload = bytearray()
            # 2 roots: obj 1 (RootMain), obj 3 (RootAux)
            put_u32(payload, 2); put_u32(payload, 1); put_u32(payload, 3)
            # 5 objects: 1=RootMain, 2=Child, 3=RootAux, 4=array<Child>, 5=array<wrap<Child>>
            put_u32(payload, 5)
            # Pass 1 headers
            payload.append(1); put_cstr(payload, b"RootMain")
            payload.append(1); put_cstr(payload, b"Child")
            payload.append(1); put_cstr(payload, b"RootAux")
            payload.append(2); put_i32(payload, AIN_ARRAY); put_i32(payload, 1)
            put_type(payload, AIN_STRUCT, 1, b"Child")
            put_u32(payload, 2)
            payload.append(2); put_i32(payload, AIN_ARRAY); put_i32(payload, 1)
            put_type(payload, AIN_WRAP, 1, b"", (AIN_STRUCT, 1, b"Child", None))
            put_u32(payload, 2)

            # Pass 2 bodies
            # Obj 1: RootMain (uses member->name like serialize_x4ss_structs!)
            r1_fields = [
                (b"m_child_ok", bytes([3]) + struct.pack("<I", 2)),
                (b"m_child_null", bytes([3]) + struct.pack("<I", 0)),
                (b"m_ref_null", bytes([3]) + struct.pack("<I", 0)),
                (b"m_wrap_ok", bytes([3]) + struct.pack("<I", 2)),
                (b"m_wrap_null", bytes([3]) + struct.pack("<I", 0)),
                (b"m_wiface_ok", bytes([4]) + struct.pack("<Ii", 2, 7)),
                (b"m_wiface_null", bytes([4]) + struct.pack("<Ii", 0, 0)),
                (b"m_iface_null", bytes([4]) + struct.pack("<Ii", 0, 0)),
                (b"m_opt_int_some", bytes([5, 0, 1]) + struct.pack("<i", 42)),
                (b"m_opt_int_none", bytes([5, 1])),
                (b"m_opt_wrap_ok", bytes([5, 0, 3]) + struct.pack("<I", 2)),
                (b"m_opt_wiface_ok", bytes([5, 0, 4]) + struct.pack("<Ii", 2, 7)),
                (b"m_opt_child_none", bytes([5, 1])),
                (b"m_nosave_int", bytes([1]) + struct.pack("<i", 9999)),
                (b"m_arr_child", bytes([3]) + struct.pack("<I", 4)),
                (b"m_arr_wchild", bytes([3]) + struct.pack("<I", 5)),
                (b"sertString", bytes([2]) + struct.pack("<I", 12) + b"inserted_val"),
                (b"m_empty_str", bytes([2]) + struct.pack("<I", 0)),
            ]
            put_u32(payload, len(r1_fields))
            for fn, fval in r1_fields:
                put_cstr(payload, fn); payload.extend(fval)

            # Obj 2: Child (m_ratio=2.5f, m_flag=1, m_kind=3, name="ame>")
            ratio_bits = struct.unpack("<i", struct.pack("<f", 2.5))[0]
            c_fields = [
                (b"m_id", bytes([1]) + struct.pack("<i", 77)),
                (b"ame>", bytes([2]) + struct.pack("<I", 7) + b"cg_hero"),
                (b"m_ratio", bytes([1]) + struct.pack("<i", ratio_bits)),
                (b"m_flag", bytes([1]) + struct.pack("<i", 1)),
                (b"m_kind", bytes([1]) + struct.pack("<i", 3)),
            ]
            put_u32(payload, len(c_fields))
            for fn, fval in c_fields:
                put_cstr(payload, fn); payload.extend(fval)

            # Obj 3: RootAux
            r2_fields = [
                (b"m_tag", bytes([1]) + struct.pack("<i", 999)),
                (b"m_bool", bytes([1]) + struct.pack("<i", 1)),
                (b"m_enum", bytes([1]) + struct.pack("<i", 5)),
            ]
            put_u32(payload, len(r2_fields))
            for fn, fval in r2_fields:
                put_cstr(payload, fn); payload.extend(fval)

            # Obj 4: array<Child> = [Child, null]
            payload.extend(bytes([3]) + struct.pack("<I", 2))
            payload.extend(bytes([3]) + struct.pack("<I", 0))
            # Obj 5: array<wrap<Child>> = [null, Child]
            payload.extend(bytes([3]) + struct.pack("<I", 0))
            payload.extend(bytes([3]) + struct.pack("<I", 2))

            comment = b"X4SS-COMMENT"
            comp = zlib.compress(bytes(payload), 1)
            x4ss_path = tmp / "legacy.x4ss.asd"
            x4ss_path.write_bytes(
                b"X4SS\x01\x00\x00\x00"
                + struct.pack("<I", len(comment)) + comment
                + struct.pack("<II", len(payload), len(comp)) + comp
            )

            # Unconvertible X4SS containing X4SS_VAL_PAGE_VAR (tag=6)
            bad_payload = bytearray()
            put_u32(bad_payload, 1); put_u32(bad_payload, 1)
            put_u32(bad_payload, 1)
            bad_payload.append(1); put_cstr(bad_payload, b"RootAux")
            put_u32(bad_payload, 1)
            put_cstr(bad_payload, b"m_ref_int")
            bad_payload.extend(bytes([6]) + struct.pack("<Ii", 1, 0))  # X4SS_VAL_PAGE_VAR
            bad_comp = zlib.compress(bytes(bad_payload), 1)
            bad_x4ss = tmp / "unconvertible.x4ss.asd"
            bad_x4ss.write_bytes(
                b"X4SS\x01\x00\x00\x00"
                + struct.pack("<I", 0)
                + struct.pack("<II", len(bad_payload), len(bad_comp)) + bad_comp
            )

            out_no_ain = tmp / "x4ss_no_ain.asd"
            out_with_ain = tmp / "x4ss_with_ain.asd"
            bad_dst = tmp / "bad_dst.asd"

            harness = SAVEDATA_VM_STUBS + r"""
int main(int argc, char **argv) {
    assert(argc == 7);
    assert(ain == NULL);
    assert(x4ss_is_container_file(argv[1]));
    assert(savedata_export_pc_file(argv[1], argv[3]));
    assert(savedata_export_pc_file_with_ain(argv[1], argv[4], argv[2]));
    // Unconvertible X4SS must return false and must NOT create dst_path!
    assert(!savedata_export_pc_file_with_ain(argv[5], argv[6], argv[2]));
    assert(!file_exists(argv[6]));
    assert(ain == NULL);
    puts("OK");
    return 0;
}
"""
            out = self.run_harness(
                harness,
                args=[
                    str(x4ss_path), str(ain_path), str(out_no_ain),
                    str(out_with_ain), str(bad_x4ss), str(bad_dst),
                ],
            )
            self.assertEqual(out.strip(), "OK")

            # Without .ain: null structs are -1 (not 0), multi-root objs[0] valid, comment preserved
            validate_windows_gdss(out_no_ain, expected_roots=2, expected_comment=comment)
            # With .ain: exact AIN_BOOL/AIN_ENUM/AIN_FLOAT/AIN_WRAP/name2/[nosave] schema & values!
            parsed = validate_windows_gdss(
                out_with_ain,
                expected_schemas=expected_schemas,
                expected_roots=2,
                expected_comment=comment,
            )
            self.assertEqual(parsed["arrays"][1][0], AIN_STRUCT)
            self.assertEqual(parsed["arrays"][1][1], [2, -1])
            self.assertEqual(parsed["arrays"][2][0], AIN_REF_STRUCT)
            self.assertEqual(parsed["arrays"][2][1], [-1, 2])

    def test_group_save_comment_embed_fallback_and_export_folding(self):
        """write_group_save_comment embeds in gsave v8; export folds legacy .comment sidecar."""
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            harness = SAVEDATA_VM_STUBS + r"""
int main(int argc, char **argv) {
    assert(argc == 2);
    snprintf(g_test_savedir, sizeof(g_test_savedir), "%s", argv[1]);
    ain = ain_new(14, 0);

    // 1. Create a gsave v8 file and write a comment via write_group_save_comment
    struct gsave *gs = gsave_create(8, "TEST_KEY", 1, "GRP");
    gsave_add_globals_record(gs, 1);
    gs->globals[0].name = xstrdup("g_score");
    gs->globals[0].type = AIN_INT;
    gs->globals[0].value = 12345;
    char *p1 = savedir_path("slot1.asd");
    FILE *fp = fopen(p1, "wb");
    assert(fp);
    assert(gsave_write(gs, fp, false, 1) == SAVEFILE_SUCCESS);
    fclose(fp);
    gsave_free(gs);

    // Create a stale sidecar first to verify write_group_save_comment removes it!
    char stale[1024];
    snprintf(stale, sizeof(stale), "%s.comment", p1);
    FILE *sfp = fopen(stale, "wb");
    assert(sfp);
    fwrite("STALE", 1, 5, sfp);
    fclose(sfp);

    struct string *c1 = make_string("Chapter 3 - Boss", 16);
    assert(write_group_save_comment("TEST_KEY", "slot1.asd", c1) == 1);
    free_string(c1);

    // Sidecar must NOT exist anymore!
    assert(!file_exists(stale));

    // Verify comment is embedded inside gsave v8 directly
    enum savefile_error err;
    struct gsave *rd = gsave_read(p1, &err);
    assert(rd && err == SAVEFILE_SUCCESS);
    assert(rd->version == 8 && rd->nr_comments == 1);
    assert(strcmp(rd->comments[0]->text, "Chapter 3 - Boss") == 0);
    gsave_free(rd);

    // read_group_save_comment reads embedded comment
    struct string *got = NULL;
    assert(read_group_save_comment("TEST_KEY", "slot1.asd", &got) == 1);
    assert(got && strcmp(got->text, "Chapter 3 - Boss") == 0);
    free_string(got);

    // 1b. Rewriting globals via save_globals() must preserve embedded comments (gsave v8 AND v7->v8)!
    ain->nr_global_groups = 1;
    ain->global_group_names = xcalloc(1, sizeof(char *));
    ain->global_group_names[0] = xstrdup("GRP");
    ain->nr_globals = 1;
    ain->globals = xcalloc(1, sizeof(struct ain_variable));
    ain->globals[0].name = xstrdup("g_score");
    ain->globals[0].type.data = AIN_INT;
    ain->globals[0].group_index = 0;

    int saved_n = 0;
    assert(save_globals("TEST_KEY", "slot1.asd", "GRP", &saved_n) == 1);
    assert(saved_n == 1);
    got = NULL;
    assert(read_group_save_comment("TEST_KEY", "slot1.asd", &got) == 1);
    assert(got && strcmp(got->text, "Chapter 3 - Boss") == 0);
    free_string(got);
    rd = gsave_read(p1, &err);
    assert(rd && err == SAVEFILE_SUCCESS);
    assert(rd->version == 8 && rd->nr_comments == 1);
    assert(strcmp(rd->comments[0]->text, "Chapter 3 - Boss") == 0);
    assert(rd->globals[0].value == 0);  // proves globals were rewritten
    gsave_free(rd);

    // Also verify AIN v6 (get_gsave_version() == 7) preserves embedded comment across save_globals()!
    struct ain *ain_v14 = ain;
    ain = ain_new(6, 0);
    ain->nr_global_groups = 1;
    ain->global_group_names = xcalloc(1, sizeof(char *));
    ain->global_group_names[0] = xstrdup("GRP");
    ain->nr_globals = 1;
    ain->globals = xcalloc(1, sizeof(struct ain_variable));
    ain->globals[0].name = xstrdup("g_hp");
    ain->globals[0].type.data = AIN_INT;
    ain->globals[0].group_index = 0;
    assert(save_globals("TEST_KEY", "v7_rewrite.asd", "GRP", NULL) == 1);
    struct string *c_v7 = make_string("V7 Preserved Comment", 20);
    assert(write_group_save_comment("TEST_KEY", "v7_rewrite.asd", c_v7) == 1);
    free_string(c_v7);
    assert(save_globals("TEST_KEY", "v7_rewrite.asd", "GRP", NULL) == 1);
    got = NULL;
    assert(read_group_save_comment("TEST_KEY", "v7_rewrite.asd", &got) == 1);
    assert(got && strcmp(got->text, "V7 Preserved Comment") == 0);
    free_string(got);
    // Overwriting with a different key must NOT carry forward the previous save's comment
    assert(save_globals("OTHER_KEY", "v7_rewrite.asd", "GRP", NULL) == 1);
    got = NULL;
    assert(read_group_save_comment("OTHER_KEY", "v7_rewrite.asd", &got) == 0);
    ain_free(ain);
    ain = ain_v14;

    // 2. Legacy AliceHub gsave v8 (no embedded comment) + <file>.comment sidecar
    struct gsave *gs2 = gsave_create(8, "TEST_KEY", 1, "GRP");
    gsave_add_globals_record(gs2, 1);
    gs2->globals[0].name = xstrdup("g_score");
    gs2->globals[0].type = AIN_INT;
    gs2->globals[0].value = 54321;
    char *p2 = savedir_path("legacy_grp.asd");
    fp = fopen(p2, "wb");
    assert(fp);
    assert(gsave_write(gs2, fp, false, 1) == SAVEFILE_SUCCESS);
    fclose(fp);
    gsave_free(gs2);

    char cpath2[1024];
    snprintf(cpath2, sizeof(cpath2), "%s.comment", p2);
    sfp = fopen(cpath2, "wb");
    assert(sfp);
    fwrite("Sidecar Comment V8", 1, 18, sfp);
    fclose(sfp);

    // read_group_save_comment falls back to legacy sidecar
    got = NULL;
    assert(read_group_save_comment("TEST_KEY", "legacy_grp.asd", &got) == 1);
    assert(got && strcmp(got->text, "Sidecar Comment V8") == 0);
    free_string(got);

    // Export via savedata_export_pc_file_with_ain(..., NULL) folds sidecar into gs->comments[0]!
    char exp2[1024];
    snprintf(exp2, sizeof(exp2), "%s/exported_grp.asd", argv[1]);
    struct ain *saved_ain = ain;
    ain = NULL;
    assert(savedata_export_pc_file_with_ain(p2, exp2, NULL));
    ain = saved_ain;

    rd = gsave_read(exp2, &err);
    assert(rd && err == SAVEFILE_SUCCESS);
    assert(rd->version == 8 && rd->nr_comments == 1);
    assert(strcmp(rd->comments[0]->text, "Sidecar Comment V8") == 0);
    assert(rd->globals[0].value == 54321);
    gsave_free(rd);

    // 2b. Embedded comment must win over a stale .comment sidecar during export!
    struct gsave *gs_emb = gsave_create(8, "TEST_KEY", 1, "GRP");
    gsave_add_globals_record(gs_emb, 1);
    gs_emb->globals[0].name = xstrdup("g_score");
    gs_emb->globals[0].type = AIN_INT;
    gs_emb->globals[0].value = 777;
    gs_emb->nr_comments = 1;
    gs_emb->comments = xcalloc(1, sizeof(struct string *));
    gs_emb->comments[0] = make_string("Chapter 7", 9);
    char *p_emb = savedir_path("embedded_wins.asd");
    fp = fopen(p_emb, "wb");
    assert(fp);
    assert(gsave_write(gs_emb, fp, false, 1) == SAVEFILE_SUCCESS);
    fclose(fp);
    gsave_free(gs_emb);

    char cpath_emb[1024];
    snprintf(cpath_emb, sizeof(cpath_emb), "%s.comment", p_emb);
    sfp = fopen(cpath_emb, "wb");
    assert(sfp);
    fwrite("Chapter 2", 1, 9, sfp);
    fclose(sfp);

    char exp_emb[1024];
    snprintf(exp_emb, sizeof(exp_emb), "%s/exported_embedded_wins.asd", argv[1]);
    ain = NULL;
    assert(savedata_export_pc_file_with_ain(p_emb, exp_emb, NULL));
    ain = saved_ain;

    rd = gsave_read(exp_emb, &err);
    assert(rd && err == SAVEFILE_SUCCESS);
    assert(rd->version == 8 && rd->nr_comments == 1);
    assert(strcmp(rd->comments[0]->text, "Chapter 7") == 0);
    gsave_free(rd);
    free(p_emb);

    // 3. Also test gsave v7 + sidecar -> upgrades to v8 with embedded comment on export!
    struct gsave *gs3 = gsave_create(7, "TEST_KEY", 1, "GRP");
    gsave_add_globals_record(gs3, 1);
    gs3->globals[0].name = xstrdup("g_hp");
    gs3->globals[0].type = AIN_INT;
    gs3->globals[0].value = 999;
    char *p3 = savedir_path("legacy_v7.asd");
    fp = fopen(p3, "wb");
    assert(fp);
    assert(gsave_write(gs3, fp, false, 1) == SAVEFILE_SUCCESS);
    fclose(fp);
    gsave_free(gs3);

    char cpath3[1024];
    snprintf(cpath3, sizeof(cpath3), "%s.comment", p3);
    sfp = fopen(cpath3, "wb");
    assert(sfp);
    fwrite("Upgraded V7 Comment", 1, 19, sfp);
    fclose(sfp);

    char exp3[1024];
    snprintf(exp3, sizeof(exp3), "%s/exported_v7.asd", argv[1]);
    ain = NULL;
    assert(savedata_export_pc_file_with_ain(p3, exp3, NULL));
    ain = saved_ain;

    rd = gsave_read(exp3, &err);
    assert(rd && err == SAVEFILE_SUCCESS);
    assert(rd->version == 8 && rd->nr_comments == 1);
    assert(strcmp(rd->comments[0]->text, "Upgraded V7 Comment") == 0);
    gsave_free(rd);

    free(p1); free(p2); free(p3);
    ain_free(ain);
    puts("OK");
    return 0;
}
"""
            out = self.run_harness(
                harness,
                args=[str(tmp)],
            )
            self.assertEqual(out.strip(), "OK")

    def test_rsm_encryption_x4t_export_decryption_and_xsystem4_only_detection(self):
        """Test RSM v6+ unencrypted output, @x4t: avoidance, export decryption, XPE/XPP2 detection, and passthrough copy."""
        fn_unwrap = extract_function("src/resume.c", "static void unwrap_v12_array_type")
        fn_norm = extract_function("src/resume.c", "static enum ain_data_type normalize_v12_array_leaf_type")
        fn_leaf_name = extract_function("src/resume.c", "static const char *array_leaf_type_name")
        fn_needs_x4t = extract_function("src/resume.c", "static bool v14_array_needs_x4_type")
        fn_enc_node = extract_function("src/resume.c", "static void encode_x4_type_node")
        fn_enc = extract_function("src/resume.c", "static char *encode_x4_type")
        fn_arr_to_rsave = extract_function("src/resume.c", "static struct rsave_heap_array *array_page_to_rsave")
        fn_save_rsave = extract_function("src/resume.c", "static int save_rsave_image")

        with tempfile.TemporaryDirectory() as tmpdir:
            tmp = Path(tmpdir)
            ain_v4 = tmp / "v4.ain"
            ain_v4.write_bytes(build_synthetic_ain(4, []))
            ain_v6 = tmp / "v6.ain"
            ain_v6.write_bytes(build_synthetic_ain(6, []))

            # Non-RSM passthrough & negative XPE detection fixtures
            (tmp / "psr.asd").write_bytes(b"PSR\x00\x01\x02\x03\x04payload")
            (tmp / "csd.asd").write_bytes(b"CSD\x00\x05\x06\x07\x08payload")
            (tmp / "thumb.qnt").write_bytes(b"QNT\x00\x02\x00\x00\x00thumbdata")
            (tmp / "MsgSkip.asd").write_bytes(b"MSK\x00\x01\x00\x00\x00skipflags")
            (tmp / "gd_ss.asd").write_bytes(_build_broken_legacy_gdss(b"XPE_IN_COMMENT"))
            (tmp / "x4ss.asd").write_bytes(b"X4SS\x01\x00\x00\x00\x00\x00\x00\x00")

            harness = SAVEDATA_VM_STUBS + """
static void append_str(struct string **out, const char *s) {
    string_append_cstr(out, s, strlen(s));
}
""" + "\n".join([
                fn_unwrap, fn_norm, fn_leaf_name, fn_needs_x4t,
                fn_enc_node, fn_enc, fn_arr_to_rsave,
            ]) + r"""
static struct rsave *g_mock_rsave = NULL;
static struct rsave *vm_image_to_rsave(const char *key) {
    (void)key;
    struct rsave *r = g_mock_rsave;
    g_mock_rsave = NULL;
    return r;
}
""" + fn_save_rsave + r"""

static struct rsave *make_minimal_rsave(int32_t ver, int arr_mode) {
    /* arr_mode: 0=clean, 1=XPE iarray, 2=XPP2 iarray, 3=packed XPE, 4=string "XPE" only */
    struct rsave *rs = xcalloc(1, sizeof(struct rsave));
    rs->version = ver;
    rs->key = xstrdup("RSM_KEY");
    rs->ip.return_addr = -1;
    rs->nr_call_frames = 1;
    rs->call_frames = xcalloc(1, sizeof(struct rsave_call_frame));
    rs->call_frames[0].type = RSAVE_CALL_STACK_BOTTOM;
    rs->call_frames[0].local_ptr = -1;
    rs->call_frames[0].struct_ptr = -1;
    rs->nr_heap_objs = 2;
    rs->heap = xcalloc(2, sizeof(void *));
    rs->heap[0] = rsave_null;

    if (arr_mode == 0) {
        struct rsave_heap_array *a = xcalloc(1, sizeof(*a) + 4 * sizeof(int32_t));
        a->tag = RSAVE_ARRAY;
        a->ref = 1;
        a->rank_minus_1 = 0;
        a->data_type = AIN_INT;
        a->struct_type.name = (ver == 4) ? NULL : xstrdup("");
        a->root_rank = 1;
        a->is_not_empty = 1;
        a->nr_slots = 4;
        a->slots[0] = 1; a->slots[1] = 2; a->slots[2] = 3; a->slots[3] = 4;
        rs->heap[1] = a;
    } else if (arr_mode == 1) {
        struct rsave_heap_array *a = xcalloc(1, sizeof(*a) + 6 * sizeof(int32_t));
        a->tag = RSAVE_ARRAY;
        a->ref = 1;
        a->rank_minus_1 = 0;
        a->data_type = (ver >= 14) ? AIN_INT : AIN_ARRAY_INT;
        a->struct_type.name = (ver == 4) ? NULL : xstrdup("");
        a->root_rank = 1;
        a->is_not_empty = 1;
        a->nr_slots = 6;
        a->slots[0] = 'X'; a->slots[1] = 'P'; a->slots[2] = 'E'; a->slots[3] = 0;
        a->slots[4] = 34; a->slots[5] = 0;
        rs->heap[1] = a;
    } else if (arr_mode == 2) {
        struct rsave_heap_array *a = xcalloc(1, sizeof(*a) + 7 * sizeof(int32_t));
        a->tag = RSAVE_ARRAY;
        a->ref = 1;
        a->rank_minus_1 = 0;
        a->data_type = AIN_INT;
        a->struct_type.name = (ver == 4) ? NULL : xstrdup("");
        a->root_rank = 1;
        a->is_not_empty = 1;
        a->nr_slots = 7;
        a->slots[0] = 'X'; a->slots[1] = 'P'; a->slots[2] = 'P'; a->slots[3] = '2'; a->slots[4] = 0;
        a->slots[5] = 34; a->slots[6] = 7;
        rs->heap[1] = a;
    } else if (arr_mode == 3) {
        struct rsave_heap_array *a = xcalloc(1, sizeof(*a) + 2 * sizeof(int32_t));
        a->tag = RSAVE_ARRAY;
        a->ref = 1;
        a->rank_minus_1 = 0;
        a->data_type = AIN_INT;
        a->struct_type.name = (ver == 4) ? NULL : xstrdup("");
        a->root_rank = 1;
        a->is_not_empty = 1;
        a->nr_slots = 2;
        a->slots[0] = 0x00455058;  /* "XPE\0" */
        a->slots[1] = 34;
        rs->heap[1] = a;
    } else {
        struct rsave_heap_string *s = xcalloc(1, sizeof(*s) + 5);
        s->tag = RSAVE_STRING;
        s->ref = 1;
        s->len = 5;
        memcpy(s->text, "XPE!", 5);
        rs->heap[1] = s;
    }
    return rs;
}

static bool file_is_encrypted_gd(const char *path) {
    enum savefile_error err;
    struct savefile *sf = savefile_read(path, &err);
    assert(sf && err == SAVEFILE_SUCCESS);
    bool enc = sf->encrypted;
    savefile_free(sf);
    return enc;
}

int main(int argc, char **argv) {
    assert(argc == 4);
    const char *dir = argv[1];
    const char *ain_v4_path = argv[2];
    const char *ain_v6_path = argv[3];
    snprintf(g_test_savedir, sizeof(g_test_savedir), "%s", dir);

    // 1. Verify save_rsave_image() encrypts for AIN < 6 and does NOT encrypt for AIN >= 6!
    ain = ain_new(14, 0);
    g_mock_rsave = make_minimal_rsave(14, 0);
    assert(save_rsave_image("RSM_KEY", "rsm_v14.asd") == 1);
    char *p_v14 = savedir_path("rsm_v14.asd");
    assert(!file_is_encrypted_gd(p_v14));
    ain_free(ain);

    ain = ain_new(6, 0);
    g_mock_rsave = make_minimal_rsave(7, 0);
    assert(save_rsave_image("RSM_KEY", "rsm_v6.asd") == 1);
    char *p_v6 = savedir_path("rsm_v6.asd");
    assert(!file_is_encrypted_gd(p_v6));
    ain_free(ain);

    ain = ain_new(4, 0);
    g_mock_rsave = make_minimal_rsave(6, 0);
    assert(save_rsave_image("RSM_KEY", "rsm_v4.asd") == 1);
    char *p_v4 = savedir_path("rsm_v4.asd");
    assert(file_is_encrypted_gd(p_v4));
    ain_free(ain);

    // 2. Verify AIN v14 array_page_to_rsave avoids "@x4t:" for standard arrays, keeps it for wrap/option!
    ain = ain_new(14, 0);
    int s0 = ain_add_struct(ain, "Unit");
    // array<int> with bogus struct_type=0 (like iarray_to_page!) -> must emit "" NOT "Unit" and NOT "@x4t:"!
    struct ain_type t_int = { .data = AIN_INT, .struc = 0 };
    struct page *p_ai = v12_array_new(&t_int, 2, false);
    int s_ai = heap_alloc_page(p_ai);
    struct rsave_heap_array *ra_int = array_page_to_rsave(p_ai, s_ai);
    assert(strcmp(ra_int->struct_type.name, "") == 0);
    free(ra_int->struct_type.name); free(ra_int);

    // array<Unit> -> must emit "Unit" (NOT "@x4t:")!
    struct ain_type t_st = { .data = AIN_STRUCT, .struc = s0 };
    struct page *p_as = v12_array_new(&t_st, 1, false);
    int s_as = heap_alloc_page(p_as);
    struct rsave_heap_array *ra_st = array_page_to_rsave(p_as, s_as);
    assert(strcmp(ra_st->struct_type.name, "Unit") == 0);
    free(ra_st->struct_type.name); free(ra_st);

    // array<array<int>> -> rank_minus_1=1, data_type=AIN_INT, name="" (NOT "@x4t:")!
    struct ain_type t_arr_int = { .data = AIN_ARRAY, .struc = -1, .rank = 1, .array_type = &t_int };
    struct page *p_a2 = v12_array_new(&t_arr_int, 1, false);
    int s_a2 = heap_alloc_page(p_a2);
    struct rsave_heap_array *ra_a2 = array_page_to_rsave(p_a2, s_a2);
    assert(ra_a2->rank_minus_1 == 1 && ra_a2->data_type == AIN_INT);
    assert(strcmp(ra_a2->struct_type.name, "") == 0);
    free(ra_a2->struct_type.name); free(ra_a2);

    // array<wrap<Unit>> -> complex v14 element type -> emits "@x4t:..."!
    struct ain_type t_wrap = { .data = AIN_WRAP, .struc = s0, .rank = 1, .array_type = &t_st };
    struct page *p_aw = v12_array_new(&t_wrap, 1, false);
    int s_aw = heap_alloc_page(p_aw);
    struct rsave_heap_array *ra_aw = array_page_to_rsave(p_aw, s_aw);
    assert(strncmp(ra_aw->struct_type.name, "@x4t:", 5) == 0);
    free(ra_aw->struct_type.name); free(ra_aw);

    test_heap_reset();
    ain_free(ain);
    ain = NULL;

    // 3. Export decryption of legacy encrypted RSM saves (with ain == NULL!)
    char *enc_v14 = savedir_path("old_enc_v14.asd");
    struct rsave *rs_enc14 = make_minimal_rsave(14, 0);
    FILE *fp = fopen(enc_v14, "wb");
    assert(fp && rsave_write(rs_enc14, fp, true, 1) == SAVEFILE_SUCCESS);
    fclose(fp);
    rsave_free(rs_enc14);
    assert(file_is_encrypted_gd(enc_v14));

    char *dec_v14 = savedir_path("exported_v14.asd");
    assert(savedata_export_pc_file_with_ain(enc_v14, dec_v14, NULL));
    assert(!file_is_encrypted_gd(dec_v14));
    // Verify decompressed payload is byte-identical!
    enum savefile_error err1, err2;
    struct savefile *sf1 = savefile_read(enc_v14, &err1);
    struct savefile *sf2 = savefile_read(dec_v14, &err2);
    assert(sf1 && sf2 && sf1->len == sf2->len && memcmp(sf1->buf, sf2->buf, sf1->len) == 0);
    savefile_free(sf1); savefile_free(sf2);

    // Encrypted RSM v7 with ain_path == NULL gets decrypted!
    char *enc_rsm7 = savedir_path("enc_rsm7.asd");
    struct rsave *rs_v7 = make_minimal_rsave(7, 0);
    fp = fopen(enc_rsm7, "wb");
    assert(fp && rsave_write(rs_v7, fp, true, 1) == SAVEFILE_SUCCESS);
    fclose(fp);
    rsave_free(rs_v7);
    char *exp_rsm7 = savedir_path("exported_rsm7.asd");
    assert(savedata_export_pc_file_with_ain(enc_rsm7, exp_rsm7, NULL));
    assert(!file_is_encrypted_gd(exp_rsm7));
    free(enc_rsm7); free(exp_rsm7);

    // Encrypted RSM v6 with ain_path == NULL stays encrypted (AIN v4 uses RSM v6!),
    // with AIN v4 .ain stays encrypted, and with AIN v6 .ain gets decrypted!
    char *exp_v6_noain = savedir_path("exported_v6_noain.asd");
    char *exp_v4_keep = savedir_path("exported_v4_keep.asd");
    char *exp_v6_dec  = savedir_path("exported_v6_dec.asd");
    assert(savedata_export_pc_file_with_ain(p_v4, exp_v6_noain, NULL));
    assert(file_is_encrypted_gd(exp_v6_noain));
    free(exp_v6_noain);
    assert(savedata_export_pc_file_with_ain(p_v4, exp_v4_keep, ain_v4_path));
    assert(file_is_encrypted_gd(exp_v4_keep));
    assert(savedata_export_pc_file_with_ain(p_v4, exp_v6_dec, ain_v6_path));
    assert(!file_is_encrypted_gd(exp_v6_dec));

    // Encrypted RSM v4 (no .ain) stays encrypted!
    char *enc_rsm4 = savedir_path("enc_rsm4.asd");
    struct rsave *rs_v4 = make_minimal_rsave(4, 0);
    fp = fopen(enc_rsm4, "wb");
    assert(fp && rsave_write(rs_v4, fp, true, 1) == SAVEFILE_SUCCESS);
    fclose(fp);
    rsave_free(rs_v4);
    char *exp_rsm4 = savedir_path("exported_rsm4.asd");
    assert(savedata_export_pc_file_with_ain(enc_rsm4, exp_rsm4, NULL));
    assert(file_is_encrypted_gd(exp_rsm4));

    // 4. savedata_file_has_xsystem4_only_state() tests!
    char *rsm_xpe = savedir_path("rsm_xpe.asd");
    char *rsm_xpp2 = savedir_path("rsm_xpp2.asd");
    char *rsm_packed = savedir_path("rsm_packed.asd");
    char *rsm_str = savedir_path("rsm_str.asd");

    struct rsave *rx1 = make_minimal_rsave(14, 1);
    fp = fopen(rsm_xpe, "wb"); assert(fp && rsave_write(rx1, fp, true, 1) == SAVEFILE_SUCCESS); fclose(fp); rsave_free(rx1);
    struct rsave *rx2 = make_minimal_rsave(14, 2);
    fp = fopen(rsm_xpp2, "wb"); assert(fp && rsave_write(rx2, fp, false, 1) == SAVEFILE_SUCCESS); fclose(fp); rsave_free(rx2);
    struct rsave *rx3 = make_minimal_rsave(14, 3);
    fp = fopen(rsm_packed, "wb"); assert(fp && rsave_write(rx3, fp, false, 1) == SAVEFILE_SUCCESS); fclose(fp); rsave_free(rx3);
    struct rsave *rx4 = make_minimal_rsave(14, 4);
    fp = fopen(rsm_str, "wb"); assert(fp && rsave_write(rx4, fp, false, 1) == SAVEFILE_SUCCESS); fclose(fp); rsave_free(rx4);

    // Must return TRUE on XPE / XPP2 / packed XPE RSM saves!
    assert(savedata_file_has_xsystem4_only_state(rsm_xpe) == true);
    assert(savedata_file_has_xsystem4_only_state(rsm_xpp2) == true);
    assert(savedata_file_has_xsystem4_only_state(rsm_packed) == true);

    // Must return FALSE on clean RSM, string-only "XPE!" RSM, gsave, GD SerializeStruct, PSR, CSD, QNT, MsgSkip, missing!
    assert(savedata_file_has_xsystem4_only_state(p_v14) == false);
    assert(savedata_file_has_xsystem4_only_state(rsm_str) == false);

    // gsave with key "RSM" (tricky edge case!) -> must return FALSE!
    char *gs_rsm_key = savedir_path("gs_rsm_key.asd");
    struct gsave *gs = gsave_create(8, "RSM", 0, "");
    gsave_add_globals_record(gs, 0);
    fp = fopen(gs_rsm_key, "wb"); assert(fp && gsave_write(gs, fp, false, 1) == SAVEFILE_SUCCESS); fclose(fp); gsave_free(gs);
    assert(savedata_file_has_xsystem4_only_state(gs_rsm_key) == false);

    char *p_psr = savedir_path("psr.asd");
    char *p_csd = savedir_path("csd.asd");
    char *p_qnt = savedir_path("thumb.qnt");
    char *p_msk = savedir_path("MsgSkip.asd");
    char *p_gdss = savedir_path("gd_ss.asd");
    char *p_x4ss = savedir_path("x4ss.asd");
    char *p_missing = savedir_path("does_not_exist.asd");
    assert(savedata_file_has_xsystem4_only_state(p_psr) == false);
    assert(savedata_file_has_xsystem4_only_state(p_csd) == false);
    assert(savedata_file_has_xsystem4_only_state(p_qnt) == false);
    assert(savedata_file_has_xsystem4_only_state(p_msk) == false);
    assert(savedata_file_has_xsystem4_only_state(p_gdss) == false);
    assert(savedata_file_has_xsystem4_only_state(p_x4ss) == false);
    assert(savedata_file_has_xsystem4_only_state(p_missing) == false);
    assert(savedata_file_has_xsystem4_only_state(NULL) == false);

    // 5. Passthrough copy for PSR, CSD, QNT, MsgSkip
    const char *pass_names[4] = { "psr.asd", "csd.asd", "thumb.qnt", "MsgSkip.asd" };
    for (int i = 0; i < 4; i++) {
        char *src = savedir_path(pass_names[i]);
        char dst[1024];
        snprintf(dst, sizeof(dst), "%s/copy_%s", dir, pass_names[i]);
        assert(savedata_export_pc_file_with_ain(src, dst, ain_v6_path));
        size_t l1 = 0, l2 = 0;
        uint8_t *b1 = file_read(src, &l1);
        uint8_t *b2 = file_read(dst, &l2);
        assert(b1 && b2 && l1 == l2 && memcmp(b1, b2, l1) == 0);
        free(b1); free(b2); free(src);
    }

    free(p_v14); free(p_v6); free(p_v4);
    free(enc_v14); free(dec_v14); free(exp_v4_keep); free(exp_v6_dec);
    free(enc_rsm4); free(exp_rsm4);
    free(rsm_xpe); free(rsm_xpp2); free(rsm_packed); free(rsm_str);
    free(gs_rsm_key); free(p_psr); free(p_csd); free(p_qnt); free(p_msk); free(p_gdss); free(p_x4ss); free(p_missing);
    puts("OK");
    return 0;
}
"""
            out = self.run_harness(
                harness,
                args=[str(tmp), str(ain_v4), str(ain_v6)],
            )
            self.assertEqual(out.strip(), "OK")


if __name__ == "__main__":
    unittest.main()
