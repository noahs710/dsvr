#!/usr/bin/env python3
"""Dump DSR .paramdef field tables from already-extracted paramdef bundles.

Read-only. Run tools/dsr_unpack.py first; this tool only reads the extracted
*.paramdef files under the output directory.

The record layout below was recovered by byte-probing
paramdef/paramdef.paramdefbnd/AtkParam.paramdef, not copied from a wiki:

  header  u32 fileSize | u16 48 | u16 version | u16 fieldCount | u16 rowSize
          char[32] struct name at 0x0C
  field   stride 0xB0 from 0x2C:
            +0x00 u32 field id
            +0x04 char[0x20] Shift-JIS display label
            +0x44 char[8]  type name (f32/s32/s16/u16/u8/s8/dummy8)
            +0x48 char[0x20] printf format
            +0x58 f64 max, +0x60 f64 min
            +0x64 u32 element count, +0x68 u32 element size
            +0x90 char[0x20] field name (":k" suffix = k-bit bitfield)

The u16 at 0x0A reads 0xB0 for all 51 defs, so it is the field RECORD STRIDE,
not a row size - the real row size lives in the paired .param row table
(tools/dsr_param.py measures it). The JSON key is still named row_size for
compatibility; read it as record_stride.

Three rules reconcile the field walk with the measured row. (1) A declared
element count of 0 means ONE element - CoolTimeParam declares count 0 / size 4
for all 8 f32 members and its row is exactly 32 bytes. (2) A contiguous
bitfield run occupies ceil(total bits / 8) bytes rather than one byte per
member. packed_bytes applies both. (3) A declared count can exceed the row's
real array length (AtkParam's hti0..3_Priority declare x5, 20 bytes, where the
128-byte row has 4) - that rule is NOT applied automatically because no
independent evidence pins which counts are inflated. With rules 1 and 2, 22 of
22 paired .paramdef/.param files reconcile exactly; AtkParam is the only
overrun and is isolated to those four records. A def whose packed_bytes still
exceeds the measured stride has an inflated count - investigate it, do not
publish offsets from it.
"""

import argparse
import json
import os
import struct
import sys

HEADER = struct.Struct("<4H")
FIELD_ID = 0x00
LABEL = 0x04
TYPE = 0x44
FMT = 0x48
FMAX = 0x58
FMIN = 0x60
COUNT = 0x64
ESIZE = 0x68
NAME = 0x90
STRIDE = 0xB0
BODY = 0x2C


def fixed(buf, off, size):
    raw = buf[off:off + size]
    nul = raw.find(b"\x00")
    if nul >= 0:
        raw = raw[:nul]
    return raw.rstrip(b" \x00")


def label(buf, off, size):
    raw = fixed(buf, off, size)
    for codec in ("shift_jis", "cp932", "latin-1"):
        try:
            return raw.decode(codec).strip()
        except UnicodeDecodeError:
            continue
    return raw.decode("latin-1").strip()


def parse_bytes(name, buf):
    if len(buf) < BODY + 32:
        raise ValueError("truncated paramdef: " + name)
    _, version, field_count, row_size = HEADER.unpack_from(buf, 4)
    fields = []
    for index in range(field_count):
        base = BODY + index * STRIDE
        if base + STRIDE > len(buf):
            fields.append({"index": index, "error": "record past end of file"})
            continue
        field_name = fixed(buf, base + NAME, 0x20).decode("latin-1")
        fields.append({
            "index": index,
            "id": struct.unpack_from("<I", buf, base + FIELD_ID)[0],
            "type": fixed(buf, base + TYPE, 8).decode("latin-1"),
            "format": fixed(buf, base + FMT, 0x20).decode("latin-1"),
            # A declared count of 0 means one element: CoolTimeParam declares
            # count 0 / size 4 for all 8 f32 members and its measured row is
            # exactly 8 * 4 = 32 bytes, so 0 is the scalar encoding, not "no
            # elements".
            "count": struct.unpack_from("<I", buf, base + COUNT)[0] or 1,
            "element_size": struct.unpack_from("<I", buf, base + ESIZE)[0],
            "max": struct.unpack_from("<d", buf, base + FMAX)[0],
            "min": struct.unpack_from("<d", buf, base + FMIN)[0],
            "name": field_name,
            "label": label(buf, base + LABEL, 0x20),
            "bits": int(field_name.rsplit(":", 1)[1]) if ":" in field_name else None,
        })
    declared = sum(f["count"] * f["element_size"] for f in fields if "error" not in f)
    # A bitfield run is declared as one u8 per member but occupies ceil(total
    # bits / 8) bytes in the row. Collapse each maximal contiguous run so the
    # byte budget can be compared against the stride measured from the .param.
    packed = 0
    index = 0
    while index < len(fields):
        field = fields[index]
        if "error" in field:
            index += 1
            continue
        if field["bits"] is None:
            packed += field["count"] * field["element_size"]
            index += 1
            continue
        bits = 0
        while index < len(fields) and "error" not in fields[index] and fields[index]["bits"] is not None:
            bits += fields[index]["bits"]
            index += 1
        packed += (bits + 7) // 8
    return {
        "file": name,
        "struct": fixed(buf, 0x0C, 32).decode("latin-1"),
        "version": version,
        "field_count": field_count,
        "row_size": row_size,
        "declared_bytes": declared,
        "packed_bytes": packed,
        "unaccounted_bytes": row_size - declared,
        "fields": fields,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=os.path.join("build", "dsr-data", "paramdef"),
                    help="directory containing extracted *.paramdef files (recursed)")
    ap.add_argument("--def", dest="defname", help="only this leaf, e.g. AtkParam.paramdef")
    ap.add_argument("--json", help="write all tables as JSON here")
    ap.add_argument("--summary", action="store_true", help="one line per def")
    args = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    if not os.path.isdir(args.dir):
        sys.exit("no such directory: %s (run tools/dsr_unpack.py first)" % args.dir)
    paths = []
    for root, _, files in os.walk(args.dir):
        for leaf in sorted(files):
            if leaf.endswith(".paramdef") and (not args.defname or leaf == args.defname):
                paths.append(os.path.join(root, leaf))
    if not paths:
        sys.exit("no .paramdef files under " + args.dir)

    tables = [parse_bytes(os.path.basename(p), open(p, "rb").read()) for p in paths]

    if args.json:
        os.makedirs(os.path.dirname(os.path.abspath(args.json)), exist_ok=True)
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump(tables, handle, ensure_ascii=False, indent=1)

    gaps = 0
    for t in tables:
        if t["unaccounted_bytes"]:
            gaps += 1
        if args.summary:
            print("%-30s struct=%-24s fields=%3d recStride=%4d declared=%6d packed=%6d"
                  % (t["file"], t["struct"], t["field_count"], t["row_size"],
                     t["declared_bytes"], t["packed_bytes"]))
            continue
        print("%s  struct=%s fields=%d recStride=%d declared=%d packed=%d"
              % (t["file"], t["struct"], t["field_count"], t["row_size"],
                 t["declared_bytes"], t["packed_bytes"]))
        for f in t["fields"]:
            if "error" in f:
                print("  %3d  %s" % (f["index"], f["error"]))
                continue
            print("  %3d id=%-6d %-8s x%-2d %-30s %s"
                  % (f["index"], f["id"], f["type"], f["count"], f["name"], f["label"]))
    print("%d defs parsed, %d with an unaccounted stride gap" % (len(tables), gaps))
    return 0


if __name__ == "__main__":
    sys.exit(main())
