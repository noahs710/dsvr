"""Unpack DSR's DCX+BND3/BND4 data archives (params, paramdefs, Lua bundles).

Read-only against the game install: nothing here writes to the game directory.
Output goes to a workspace directory (default build/dsr-data) so extracted data
is disposable and never mistaken for a verification receipt.

Usage:
  python tools/dsr_unpack.py --game "<game dir>" --out build/dsr-data [--list]
"""

import argparse
import os
import struct
import sys
import zlib

DCX_MAGIC = b"DCX\x00"
DCA_MAGIC = b"DCA\x00"
BND4_MAGIC = b"BND4"
BND3_MAGIC = b"BND3"


def dcx_decompress(blob: bytes) -> bytes:
    """Return the inflated payload of a DCX-wrapped file.

    DSR ships DCX v19: a 0x24-byte header, a DCS container, then one or more
    DCP packets whose DCA sub-chunk carries the actual zlib stream. Scanning
    for DCA is deliberate - the packet sizes in the DFLT header are not a
    reliable stream boundary.
    """
    if blob[:4] != DCX_MAGIC:
        raise ValueError("not a DCX file")
    chunks = []
    pos = 0
    while True:
        pos = blob.find(DCA_MAGIC, pos)
        if pos < 0:
            break
        # Consume exactly one zlib stream per DCA chunk: the next chunk's header
        # follows immediately, so a whole-buffer decompress would see trailing
        # bytes and fail on any multi-packet file.
        decomp = zlib.decompressobj()
        chunks.append(decomp.decompress(blob[pos + 8 :]))
        pos += 8 + (len(blob) - pos - 8 - len(decomp.unused_data))
    if not chunks:
        raise ValueError("DCX file has no DCA chunk")
    return b"".join(chunks)


def bnd3_read(blob: bytes):
    """Yield (name, data) for every entry of a BND3 archive.

    Layout (all little-endian, verified against paramdef.paramdefbnd on 29 Oct):
      0x00 magic 'BND3', 0x04 version string (8 bytes, '07D7R6\0'),
      0x0C u32 table entry count, 0x10 u32 file count, 0x14 u32 foffset.
    Entries are 24 bytes at 0x20: (flags, size, offset, id, name_offset, size).
    Offsets and name offsets are ABSOLUTE in the payload, not foffset-relative -
    the name strings live between the entry table and the file data, so a
    foffset-relative read (the BND4 habit) decodes garbage.
    """
    file_count = struct.unpack_from("<I", blob, 0x10)[0]
    for index in range(file_count):
        _flags, size, offset, _id, name_off, _size2 = struct.unpack_from(
            "<6I", blob, 0x20 + 24 * index
        )
        stop = blob.index(b"\0", name_off)
        name = blob[name_off:stop].decode("utf-8", "replace")
        # Names are author-side absolute paths (N:\FRPG\data\...); keep the leaf.
        name = name.replace("\\", "/").rstrip("/").split("/")[-1] or "file_%d" % index
        yield name, blob[offset : offset + size]


def bnd4_read(blob: bytes):
    """Yield (name, data) for every entry of a BND3 or BND4 archive."""
    if blob[:4] == BND3_MAGIC:
        yield from bnd3_read(blob)
        return
    if blob[:4] != BND4_MAGIC:
        raise ValueError("not a BND3/BND4 file: %r" % blob[:8])
    endian = struct.unpack_from("<H", blob, 0x04)[0]
    endian = "<" if endian == 0x2A2A else ">"
    count, foffset = struct.unpack_from(endian + "II", blob, 0x0C)
    is_big = blob[0x12]
    has_names = blob[0x13]
    entry_fmt = endian + ("QQQ" if is_big else "III")
    entry_size = 24 if is_big else 12
    for index in range(count):
        start, end, name_off = struct.unpack_from(
            entry_fmt, blob, 0x20 + entry_size * index
        )
        if has_names:
            stop = blob.index(b"\0", foffset + name_off)
            name = blob[foffset + name_off : stop].decode("utf-8", "replace")
        else:
            name = "file_%d" % index
        yield name, blob[foffset + start : foffset + end]


def unpack_tree(game_dir: str, out_dir: str, only=None, listing=False):
    """Unpack every *.dcx under the given subdirectories of the game install."""
    roots = only or ["param", "paramdef", "script"]
    written = []
    for root in roots:
        base = os.path.join(game_dir, root)
        if not os.path.isdir(base):
            print("missing archive root: %s" % base, file=sys.stderr)
            continue
        for dirpath, _dirs, files in os.walk(base):
            for fname in sorted(files):
                if not fname.endswith(".dcx"):
                    continue
                src = os.path.join(dirpath, fname)
                blob = open(src, "rb").read()
                payload = dcx_decompress(blob)
                rel_dir = os.path.relpath(dirpath, game_dir)
                for name, data in bnd4_read(payload):
                    dest = os.path.join(out_dir, rel_dir, fname[: -len(".dcx")], name)
                    if listing:
                        print("%-60s %8d" % (os.path.join(rel_dir, fname, name), len(data)))
                    else:
                        os.makedirs(os.path.dirname(dest), exist_ok=True)
                        with open(dest, "wb") as fh:
                            fh.write(data)
                    written.append(dest)
    return written


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", required=True, help="DSR install directory")
    ap.add_argument("--out", default="build/dsr-data", help="output directory")
    ap.add_argument("--only", nargs="*", help="subdirectories to unpack")
    ap.add_argument("--list", action="store_true", help="list entries, write nothing")
    args = ap.parse_args()
    files = unpack_tree(args.game, args.out, args.only, args.list)
    if not args.list:
        print("wrote %d files under %s" % (len(files), args.out))


if __name__ == "__main__":
    main()
