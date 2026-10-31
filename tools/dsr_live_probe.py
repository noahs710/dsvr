"""Read-only live probe of DSR's attack-object manager and Havok capsules.

Recovered statically (MODLOG, lane B): manager at image+0x1c7a050; +0x10 slot
array (0x230 stride, <=0x80 slots), +0x00 overflow list (next at record+0x88).
FUN_1403c5620 stores its shape-like param3 at record+0x08; +0x118 is not a
shape pointer. Records also carry phantom instances at +0x10/+0x18 and an
hkpCapsuleShape at +0x28 (vtable 0x14141bf58): radius +0x20, endpoints +0x30/+0x40.

Attack timing: start --watch immediately before a deliberate swing and attack
DURING its samples. Do not capture only after the swing; transient geometry may
have reverted. --once is for an idle/baseline snapshot. The tool only reads
process memory; it never writes game memory or injects input. Every run writes a
new Markdown receipt under build/native-live-probes by default.
"""

import argparse
import contextlib
import ctypes
import ctypes.wintypes as wt
from datetime import datetime, timezone
import hashlib
import os
from pathlib import Path
import struct
import sys
import time

EXE = r"C:\Program Files (x86)\Steam\steamapps\common\DARK SOULS REMASTERED\DarkSoulsRemastered.exe"
EXE_NAME = os.path.basename(EXE).lower()
GAME_PROCESS_NAMES = {EXE_NAME, "darksoulsremastered.exe", "darksoulsremastered_win64.exe"}
PINNED_SHA256 = "a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b"
IMAGE_BASE = 0x140000000
SINGLETON_OFF = 0x1C7A050
SLOT_STRIDE = 0x230
MAX_SLOTS = 0x80
PROCESS_VM_READ = 0x0010
PROCESS_QUERY = 0x1000

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.OpenProcess.restype = wt.HANDLE
kernel32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
kernel32.ReadProcessMemory.restype = wt.BOOL
kernel32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
                                       ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
kernel32.CloseHandle.argtypes = [wt.HANDLE]
kernel32.Process32FirstW.restype = wt.BOOL
kernel32.Process32NextW.restype = wt.BOOL
kernel32.Process32FirstW.argtypes = [wt.HANDLE, ctypes.c_void_p]
kernel32.Process32NextW.argtypes = [wt.HANDLE, ctypes.c_void_p]


class ModuleEntry(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("th32ModuleID", wt.DWORD),
                ("th32ProcessID", wt.DWORD), ("GlcntUsage", wt.DWORD),
                ("ProccntUsage", wt.DWORD), ("modBaseAddr", ctypes.c_void_p),
                ("modBaseSize", wt.DWORD), ("hModule", wt.HMODULE),
                ("szModule", ctypes.c_wchar * 256), ("szExePath", ctypes.c_wchar * 260)]


kernel32.CreateToolhelp32Snapshot.restype = wt.HANDLE
kernel32.CreateToolhelp32Snapshot.argtypes = [wt.DWORD, wt.DWORD]
kernel32.Module32FirstW.restype = wt.BOOL
kernel32.Module32FirstW.argtypes = [wt.HANDLE, ctypes.POINTER(ModuleEntry)]
kernel32.Module32NextW.restype = wt.BOOL
kernel32.Module32NextW.argtypes = [wt.HANDLE, ctypes.POINTER(ModuleEntry)]



def find_process():
    snap = kernel32.CreateToolhelp32Snapshot(0x2, 0)  # TH32CS_SNAPPROCESS
    if snap == -1 or not snap:
        return None

    class PROCESSENTRY32W(ctypes.Structure):
        _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD),
                    ("th32DefaultHeapID", ctypes.c_size_t), ("th32ModuleID", wt.DWORD),
                    ("cntThreads", wt.DWORD), ("th32ParentProcessID", wt.DWORD),
                    ("pcPriClassBase", ctypes.c_long), ("dwFlags", wt.DWORD),
                    ("szExeFile", ctypes.c_wchar * 260)]

    ent = PROCESSENTRY32W()
    ent.dwSize = ctypes.sizeof(ent)
    found = None
    ok = kernel32.Process32FirstW(snap, ctypes.byref(ent))
    while ok:
        if ent.szExeFile.lower() in GAME_PROCESS_NAMES:
            found = ent.th32ProcessID
            print(f"matched process {ent.szExeFile} pid={found}")
            break
        ok = kernel32.Process32NextW(snap, ctypes.byref(ent))
    kernel32.CloseHandle(snap)
    return found


def module_base(pid, want=EXE_NAME):
    snap = kernel32.CreateToolhelp32Snapshot(0x8, pid)  # TH32CS_SNAPMODULE
    if snap == -1 or not snap:
        return None, None
    ent = ModuleEntry()
    ent.dwSize = ctypes.sizeof(ModuleEntry)
    ok = kernel32.Module32FirstW(snap, ctypes.byref(ent))
    base, size = None, None
    while ok:
        if ent.szModule.lower() == want:
            base = ctypes.cast(ent.modBaseAddr, ctypes.c_void_p).value
            size = ent.modBaseSize
            break
        ok = kernel32.Module32NextW(snap, ctypes.byref(ent))
    kernel32.CloseHandle(snap)
    return base, size


class Reader:
    def __init__(self, pid):
        self.h = kernel32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY, False, pid)
        if not self.h:
            raise OSError(f"OpenProcess failed pid={pid} err={ctypes.get_last_error()} "
                          "(a protected/anti-cheat process or a 32-vs-64 bit mismatch)")

    def close(self):
        kernel32.CloseHandle(self.h)

    def read(self, addr, n):
        if not addr or addr >= 1 << 48:
            return None
        buf = ctypes.create_string_buffer(n)
        got = ctypes.c_size_t(0)
        if not kernel32.ReadProcessMemory(self.h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)):
            return None
        return buf.raw[:got.value]

    def u32(self, addr):
        raw = self.read(addr, 4)
        return struct.unpack_from("<I", raw)[0] if raw and len(raw) == 4 else None

    def u64(self, addr):
        raw = self.read(addr, 8)
        return struct.unpack_from("<Q", raw)[0] if raw and len(raw) == 8 else None


def describe_shape(r, label, shape):
    if not shape or shape >= 1 << 48:
        print(f"    {label}: {shape:#x} (not a readable pointer)")
        return
    vtable = r.u64(shape)
    owner = r.u64(shape + 0x10)
    if vtable is None or owner is None:
        print(f"    {label}: {shape:#x} object header unreadable")
        return
    print(f"    {label}: object={shape:#x} vtable={vtable:#018x} +0x10={owner:#018x}")
    if vtable == 0x14141BF58:
        raw = r.read(shape + 0x20, 0x30)
        if raw and len(raw) == 0x30:
            radius = struct.unpack_from("<f", raw, 0)[0]
            endpoints = struct.unpack_from("<6f", raw, 0x10)
            print(f"      hkpCapsuleShape radius={radius:.6g} endpoints={endpoints}")


def probe_once(r, base):
    mgr = r.u64(base + SINGLETON_OFF)
    print(f"module base {base:#x} slide {base - IMAGE_BASE:#x}")
    print(f"manager (image+{SINGLETON_OFF:x}) = {mgr and hex(mgr)}")
    if not mgr or mgr >= 1 << 48:
        print("  manager pointer not readable - is the game past the title screen?")
        return 0
    arr = r.u64(mgr + 0x10)
    head = r.u64(mgr + 0x00)
    print(f"  manager+0x10 slot array = {arr and hex(arr)}   manager+0x00 overflow head = {head and hex(head)}")
    live = 0
    if arr and arr < (1 << 48):
        for i in range(MAX_SLOTS):
            obj = arr + i * SLOT_STRIDE
            h = r.u32(obj)
            if h is None or h == 0xFFFFFFFF or (h & 0xFFFF) == 0:
                continue
            if (h >> 16) != i:
                continue  # stale slot
            live += 1
            print(f"  slot {i:#03x} handle={h:#010x} obj={obj:#x}")
            for off in (0x08, 0x10, 0x18, 0x28):
                describe_shape(r, f"obj+{off:#x}", r.u64(obj + off))
            print(f"    obj+0x118 raw={r.u64(obj + 0x118)!r}")
    n = 0
    cur = head
    seen = set()
    while cur and cur < (1 << 48) and n < 64 and cur not in seen:
        seen.add(cur)
        h = r.u32(cur)
        if h is not None and h != 0xFFFFFFFF and (h & 0xFFFF) != 0:
            print(f"  overflow[{n}] handle={h:#010x} obj={cur:#x}")
            for off in (0x08, 0x10, 0x18, 0x28):
                describe_shape(r, f"obj+{off:#x}", r.u64(cur + off))
            print(f"    obj+0x118 raw={r.u64(cur + 0x118)!r}")
            n += 1
        cur = r.u64(cur + 0x88)
    print(f"  live attack objects: {live} in slots, {n} in overflow")
    return live + n


class OutputTee:
    def __init__(self, *streams):
        self.streams = streams
        self.parts = []

    def write(self, text):
        self.parts.append(text)
        for stream in self.streams:
            stream.write(text)
        return len(text)

    def flush(self):
        for stream in self.streams:
            stream.flush()


def write_receipt(pid, base, digest, args, captured, completed, requested, interrupted):
    out_dir = Path(args.receipt_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc)
    stem = f"dsr-live-probe-{stamp.strftime('%Y%m%dT%H%M%S.%fZ')}-pid{pid}"
    path = out_dir / f"{stem}.md"
    suffix = 1
    while path.exists():
        path = out_dir / f"{stem}-{suffix}.md"
        suffix += 1
    lines = [
        "# DSR live shape-probe receipt", "",
        f"- Captured UTC: {stamp.isoformat()}",
        f"- Process: {EXE_NAME}; PID {pid}",
        f"- Module base: {base:#x}; image slide: {base - IMAGE_BASE:#x}",
        f"- Pinned EXE SHA-256: {digest}",
        f"- Samples completed/requested: {completed}/{requested}; interval: {args.interval:g} seconds",
        f"- Countdown: {args.countdown} seconds before GO",
        f"- Status: {'interrupted' if interrupted else 'complete'}",
        f"- Label: {args.label or 'unspecified'}",
        f"- Notes: {args.notes or 'none'}",
        "- Timing: countdown is BEFORE sampling. On GO, strike immediately while the capture window is live. A post-capture strike is not captured; --once is an idle/baseline snapshot only.",
        "- Access: read-only process memory; no writes or input injection.", "",
        "## Sample transcript", "", "~~~text", captured.rstrip(), "~~~", "",
        "## Interpretation", "",
        "Changing geometry alone does not prove a hit or identify the target. Use --label/--notes to record action, target, timing and observed outcome. Coordinates are native values; units are not asserted.", ""
    ]
    with path.open("x", encoding="utf-8", newline="\n") as receipt:
        receipt.write("\n".join(lines))
    return path
def main():
    ap = argparse.ArgumentParser(description="Read-only DSR shape capture. Countdown, then attack on GO while --watch is sampling.")
    ap.add_argument("--once", action="store_true", help="take one idle/baseline snapshot; not an attack capture")
    ap.add_argument("--watch", type=int, default=0, help="number of snapshots; perform the attack during this window")
    ap.add_argument("--interval", type=float, default=0.2, help="seconds between snapshots")
    ap.add_argument("--countdown", type=int, default=3, help="visible seconds before watch starts (default: 3)")
    ap.add_argument("--label", help="capture label, e.g. player-swing-at-hollow")
    ap.add_argument("--notes", help="target, when you swung, and observed result")
    ap.add_argument("--receipt-dir", default="build/native-live-probes", help="directory for unique Markdown receipts")
    ap.add_argument("--exe-name", help="override process/module executable name")
    args = ap.parse_args()
    if args.exe_name:
        global EXE_NAME
        EXE_NAME = args.exe_name.lower()
        GAME_PROCESS_NAMES.add(EXE_NAME)
    if args.watch < 0 or args.interval <= 0 or args.countdown < 0:
        ap.error("--watch must be >= 0, --interval > 0 and --countdown >= 0")
    if args.watch == 1:
        ap.error("--watch needs at least 2 samples to create a capture window")
    if args.once and args.watch:
        ap.error("choose --once for baseline or --watch N for attack capture, not both")
    raw = open(EXE, "rb").read()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != PINNED_SHA256:
        print(f"REFUSED: on-disk EXE sha256 {digest} != pinned {PINNED_SHA256}")
        return 2
    pid = find_process()
    if not pid:
        print("DarkSoulsRemastered.exe is not running. Start the game before capturing.")
        return 1
    base, size = module_base(pid)
    if not base:
        print(f"found game process pid={pid} but no matching EXE module; expected {EXE_NAME}")
        return 1
    count = 1 if args.once or not args.watch else args.watch
    if count > 1:
        print(f"Capture armed for {count} samples at {args.interval:g}s intervals. Attack on GO, during the capture window; do not wait until after it.")
    else:
        print("Baseline snapshot only. For a strike, use --watch; the countdown precedes sampling.")
    reader = Reader(pid)
    tee = OutputTee(sys.stdout)
    completed = 0
    interrupted = False
    try:
        if count > 1 and args.countdown:
            try:
                for remaining in range(args.countdown, 0, -1):
                    print(f"{remaining}...")
                    time.sleep(1)
            except KeyboardInterrupt:
                interrupted = True
        if not interrupted:
            print("GO - STRIKE NOW - capture window is live.", flush=True)
            with contextlib.redirect_stdout(tee):
                try:
                    for i in range(count):
                        print(f"--- sample {i + 1}/{count} ---")
                        probe_once(reader, base)
                        completed += 1
                        if i + 1 < count:
                            time.sleep(args.interval)
                except KeyboardInterrupt:
                    interrupted = True
    finally:
        reader.close()
    receipt = write_receipt(pid, base, digest, args, "".join(tee.parts), completed, count, interrupted)
    if interrupted:
        print(f"Capture interrupted after {completed}/{count} samples; partial receipt saved: {receipt}")
        return 130
    print(f"Receipt: {receipt}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
