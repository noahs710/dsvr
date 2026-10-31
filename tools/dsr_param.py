#!/usr/bin/env python3
"""Read DSR .param row tables and join them with the .paramdef field layout.

Read-only over the tree produced by tools/dsr_unpack.py.

The .param header and row table were recovered by byte-probing
param/GameParam/GameParam.parambnd/AtkParam_Pc.param on 29 Oct:

  header  u32 fileSize | u32 dataStart | u32 version/flags | char[32] struct name
          (name at 0x0C, not 0x08)
          u32 rowCountHint @0x2C | u32 @0x30 | u32 firstRowOffset @0x34
          u32 dataEnd @0x38 | u32 @0x3C
  rows    12-byte entries from 0x34, terminated by a zero rowId:
            (u32 rowOffset, u32 labelStringOffset, u32 rowId)
          rowOffset steps by a CONSTANT stride, so the stride is measured, never
          assumed, and closed by: lastRowOffset + stride == dataEnd exactly.
          dataStart is NOT the first rowOffset (EquipParamWeapon: rows start at
          14988, dataStart reads 80524) - do not anchor the table on it.

AtkParam_Pc measured 1396 rows at stride 128 (0x41AC + 1396*128 = 0x2FC2C =
dataEnd). The u32 at 0x2C (512) is NOT the row count - do not use it.

Why the offsets are gated: walking the paramdef field records for AtkParam
declares 144 bytes but the measured row stride is 128, so the field walk
overruns. The first 0x30 bytes are byte-confirmed against real rows (four f32
hit radii, knockbackDist, hitStopTime, five s32 spEffectId defaults of -1), the
tail is not. This tool therefore only decodes fields whose end offset is below
the measured stride, and reports the rest as unvalidated instead of guessing.
"""

import argparse
import json
import os
import struct
import sys

ROW_ENTRY = struct.Struct("<3I")


def param_header(buf):
    name = buf[0x0C:0x2C].split(b"\x00")[0].rstrip(b" ").decode("latin-1")
    data_start = struct.unpack_from("<I", buf, 0x04)[0]
    data_end = struct.unpack_from("<I", buf, 0x38)[0]
    # The row table starts at 0x34 and runs to the first zero rowId. Its first
    # rowOffset is NOT always dataStart: EquipParamWeapon rows begin at 14988
    # while dataStart reads 80524, so anchoring on dataStart silently rejected
    # most tables. The stride is measured from constant rowOffset deltas and
    # closed by the last row ending exactly at dataEnd.
    rows = []
    off = 0x34
    while off + 12 <= len(buf):
        row_off, label_off, row_id = ROW_ENTRY.unpack_from(buf, off)
        if not (0x40 <= row_off < len(buf)):
            break
        rows.append((row_id, row_off, label_off))
        off += 12
    # A trailing entry with rowId 0 still occupies a row slot (AtkParam_Pc's last
    # entry is (0, 195500, 225261) and 195500 + 128 == dataEnd), so it is kept as
    # a real row rather than treated as a terminator. The constant-stride and
    # dataEnd checks below are what reject a genuine sentinel.
    if len(rows) < 2:
        raise ValueError("no row table found")
    strides = {rows[i + 1][1] - rows[i][1] for i in range(len(rows) - 1)}
    if len(strides) != 1:
        raise ValueError("row offsets are not constant-stride: %r" % sorted(strides)[:6])
    stride = strides.pop()
    if rows[-1][1] + stride != data_end:
        raise ValueError(
            "last row at %d + stride %d does not reach dataEnd %d (%d rows)"
            % (rows[-1][1], stride, data_end, len(rows))
        )
    return {
        "struct": name,
        "data_start": data_start,
        "data_end": data_end,
        "row_count": len(rows),
        "row_stride": stride,
        "rows": rows,
    }


def validated_fields(table, stride):
    """Fields whose cumulative end offset fits inside the measured row stride."""
    ok, cum, bitrun = [], 0, 0
    for f in table["fields"]:
        if "error" in f:
            continue
        if f["bits"]:
            bitrun += f["bits"]
            if bitrun % 8 == 0:
                cum += bitrun // 8
                bitrun = 0
            if cum + (bitrun + 7) // 8 > stride:
                break
            continue
        if bitrun:
            cum += (bitrun + 7) // 8
            bitrun = 0
        if cum + f["count"] * f["element_size"] > stride:
            break
        ok.append((cum, f))
        cum += f["count"] * f["element_size"]
    return ok, cum


def decode(rows, fields, stride, limit=12):
    out = []
    for row_id, raw in rows[:limit]:
        rec = {"rowId": row_id}
        for off, f in fields:
            if f["type"] == "f32":
                rec[f["name"]] = round(struct.unpack_from("<f", raw, off)[0], 4)
            elif f["type"] in ("s32",):
                rec[f["name"]] = struct.unpack_from("<i", raw, off)[0]
            elif f["type"] == "s16":
                rec[f["name"]] = struct.unpack_from("<h", raw, off)[0]
            elif f["type"] == "u16":
                rec[f["name"]] = struct.unpack_from("<H", raw, off)[0]
            elif f["type"] in ("u8", "dummy8"):
                rec[f["name"]] = raw[off]
        out.append(rec)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=os.path.join("build", "dsr-data", "param"))
    ap.add_argument("--param", help="leaf name, e.g. AtkParam_Pc.param")
    ap.add_argument("--defdir", default=os.path.join("build", "dsr-data", "paramdef"))
    ap.add_argument("--json", help="write joined tables here")
    ap.add_argument("--rows", type=int, default=0, help="decode this many rows")
    args = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import dsr_paramdef

    defs = {}
    for root, _, files in os.walk(args.defdir):
        for leaf in files:
            if leaf.endswith(".paramdef"):
                t = dsr_paramdef.parse_bytes(leaf, open(os.path.join(root, leaf), "rb").read())
                defs[t["struct"]] = t

    joined = []
    for root, _, files in os.walk(args.dir):
        for leaf in sorted(files):
            if not leaf.endswith(".param") or (args.param and leaf != args.param):
                continue
            buf = open(os.path.join(root, leaf), "rb").read()
            try:
                head = param_header(buf)
            except ValueError as exc:
                print("%-28s refused: %s" % (leaf, exc))
                continue
            table = defs.get(head["struct"])
            entry = {
                "file": leaf,
                "struct": head["struct"],
                "row_count": head["row_count"],
                "row_stride": head["row_stride"],
                "has_paramdef": table is not None,
            }
            if table:
                fields, used = validated_fields(table, head["row_stride"])
                entry["field_count"] = table["field_count"]
                entry["validated_prefix_bytes"] = used
                entry["declared_bytes"] = table["declared_bytes"]
                entry["validated_fields"] = [
                    {"offset": o, "name": f["name"], "type": f["type"], "count": f["count"]}
                    for o, f in fields
                ]
                if args.rows:
                    entry["rows"] = decode(
                        [(i, buf[o : o + head["row_stride"]]) for i, o, _ in head["rows"]],
                        fields,
                        head["row_stride"],
                        args.rows,
                    )
            joined.append(entry)
            print(
                "%-28s struct=%-24s rows=%5d stride=%4d def=%s validated=%s/%s"
                % (leaf, head["struct"], head["row_count"], head["row_stride"],
                   "yes" if table else "NO",
                   entry.get("validated_prefix_bytes", "-"),
                   entry.get("declared_bytes", "-")),
            )

    if args.json:
        os.makedirs(os.path.dirname(os.path.abspath(args.json)), exist_ok=True)
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(joined, fh, ensure_ascii=False, indent=1)
    print("%d params read" % len(joined))


if __name__ == "__main__":
    main()
