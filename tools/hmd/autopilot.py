#!/usr/bin/env python3
"""State-driven autopilot for DSVR sim validation runs.

Drives the protected OpenXR-Simulator transaction on state oracles (mod log
lines, live trace records) instead of screenshot steering.

Subcommands:
  plan                 print phases, no side effects
  analyze TRACE        offline verdict on a saved capture (seconds)
  run                  attempts check, launch, menu, walk, combat, verdict,
                       close and restore; writes verdict.json after analysis

The offline verdict is PASS only when a paired distinct-actor contact shows
target HP loss. A live acceptance also needs a complete trace and restored
receipt; the analyzer does not enforce those transaction requirements.
"""
import ctypes
import json
import os
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
GAME_DIR = Path(r"C:/Program Files (x86)/Steam/steamapps/common/DARK SOULS REMASTERED")
RUNNER = REPO / "tools" / "run_combat_capture.ps1"
DEFAULT_CANDIDATE = REPO / "build/verification/synthetic-hmd-game-20261104-r13/candidate-final-receipt.json"

VK = {"W": 0x57, "H": 0x48, "R": 0x52}
ACTIVE_GAME_PID = None


class Deadline(Exception):
    pass


def poll(fn, deadline_s, interval=0.2, what="condition"):
    """Poll fn until truthy or the deadline passes. No fixed long sleeps."""
    end = time.time() + deadline_s
    while time.time() < end:
        out = fn()
        if out:
            return out
        time.sleep(interval)
    raise Deadline(f"deadline {deadline_s}s exceeded waiting for {what}")


def key(vk, down_s=0.15):
    hold(vk, down_s)


def hold(vk, seconds):
    u = ctypes.windll.user32
    require_game_foreground(u)
    u.keybd_event(vk, 0, 0, 0)
    try:
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            require_game_foreground(u)
            time.sleep(min(0.05, max(until - time.monotonic(), 0)))
    finally:
        u.keybd_event(vk, 0, 2, 0)


def enum_windows(pid):
    from ctypes import wintypes
    u = ctypes.windll.user32
    CB = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    out = []

    @CB
    def cb(hwnd, _):
        p = wintypes.DWORD()
        u.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid:
            n = u.GetWindowTextLengthW(hwnd)
            buf = ctypes.create_unicode_buffer(n + 1)
            u.GetWindowTextW(hwnd, buf, n + 1)
            out.append((hwnd, buf.value))
        return True

    u.EnumWindows(cb, 0)
    return out


def require_game_foreground(user32):
    if not game_foreground(user32):
        raise RuntimeError("refusing global key input: owned game is not foreground")


def game_foreground(user32):
    from ctypes import wintypes
    current = wintypes.DWORD()
    user32.GetWindowThreadProcessId(user32.GetForegroundWindow(),
                                    ctypes.byref(current))
    return bool(ACTIVE_GAME_PID and current.value == ACTIVE_GAME_PID)


def read_receipt(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def game_pid():
    out = subprocess.run(
        ["powershell", "-NoProfile", "-Command",
         "(Get-Process -Name DarkSoulsRemastered -ErrorAction SilentlyContinue | Select-Object -First 1).Id"],
        capture_output=True, text=True).stdout.strip()
    return int(out) if out.isdigit() else None


def pwsh_runner(*args, timeout=170):
    cmd = ["pwsh", "-NoProfile", "-File", str(RUNNER), *args]
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, cwd=REPO)


class LogTail:
    """Incremental DSVR.log reader (the log oracle)."""

    def __init__(self, path=GAME_DIR / "DSVR.log"):
        self.path = path
        initial = path.stat() if path.exists() else None
        self.pos = initial.st_size if initial else 0
        self.created_ns = initial.st_ctime_ns if initial else None
        self.carry = b""

    def wait_for(self, needle, deadline_s):
        def probe():
            if not self.path.exists():
                return None
            state = self.path.stat()
            if state.st_ctime_ns != self.created_ns or state.st_size < self.pos:
                self.pos = 0
                self.carry = b""
                self.created_ns = state.st_ctime_ns
            with open(self.path, "rb") as f:
                f.seek(self.pos)
                data = f.read()
                self.pos = f.tell()
            window = self.carry + data
            target = needle.encode()
            self.carry = window[-max(len(target) - 1, 0):] if target else b""
            return target in window or None

        return poll(probe, deadline_s, what=f"log line {needle!r}")


# ---------------------------------------------------------------- analyze ---

# Trace event names and payload layout per src/core/vr_trace.cpp (ContactDetour)
# and the mirrored decoder in tools/vr_trace.py. Actor sides: 0=source,
# 1=target. Contact flags: bit<side> = before-read valid, bit<side+2> = stable
# through the native call. Contact values: [side*4..+3] actor address limbs,
# [8+side*4] hp_before, [9+side*4] hp_after, [10/11+side*4] stamina.
# Diagnostics flags pack four read-reason nibbles in that side order.
REASON_NAMES = (
    "valid", "invalid_address", "vtable_unreadable_or_outside_image",
    "unqualified_actor_type", "handle_unreadable", "stats_unreadable",
    "stats_out_of_range", "record_unreadable", "actor_changed",
    "not_admitted_before_call",
)
PLAYER_VTABLE_RVA = 0x13251F0
SCAN_TAGS = (b"native_combat", b'"event":"header"', b'"event":"footer"')
COMBAT_KINDS = (
    "native_combat_contact", "native_combat_identity",
    "native_combat_diagnostics", "native_combat_context",
    "native_combat_motion", "native_combat_health_write",
)


def limbs_of(values):
    return sum(int(v) << (16 * i) for i, v in enumerate(values))


def signed32(lo, hi):
    value = int(lo) | (int(hi) << 16)
    return value if value < (1 << 31) else value - (1 << 32)


def scan_combat(trace: Path):
    """Stream a trace once, keeping only combat-relevant records."""
    header = footer = None
    by_kind = {kind: {} for kind in COMBAT_KINDS}
    player_addrs = set()
    with open(trace, "rb") as f:
        for raw in f:
            if not any(tag in raw for tag in SCAN_TAGS):
                continue
            try:
                e = json.loads(raw)
            except ValueError:
                continue
            ev = e.get("event")
            if ev == "header":
                header = e
            elif ev == "footer":
                footer = e
            elif ev == "native_combat_self_excluded":
                vals = e.get("values") or []
                if len(vals) >= 4:
                    player_addrs.add(limbs_of(vals[0:4]))
            elif ev in by_kind:
                try:
                    key = (e.get("thread"), e.get("seq"),
                           int(e.get("object") or "0", 16))
                except (TypeError, ValueError):
                    continue
                by_kind[ev].setdefault(key, []).append(e)
    return header, footer, by_kind, player_addrs


def verdict_for(trace: Path) -> dict:
    t0 = time.time()
    header, footer, by_kind, player_addrs = scan_combat(trace)
    base = {"trace": str(trace)}
    # A missing footer means an incomplete capture, not unusable evidence: the
    # contact batches are self-contained. Counters stay null and the result is
    # flagged complete=false. MALFORMED is reserved for no parseable records.
    complete = footer is not None
    module_base = 0
    if header:
        try:
            module_base = int(header.get("combat_module_base") or "0", 16)
        except (TypeError, ValueError):
            module_base = 0

    def player_side(actors, ident):
        for side in (0, 1):
            if actors[side] in player_addrs:
                return side
        if ident is not None and module_base:
            vals = ident.get("values") or []
            if len(vals) >= 8:
                for side in (0, 1):
                    if limbs_of(vals[side * 4:side * 4 + 4]) == \
                            module_base + PLAYER_VTABLE_RVA:
                        return side
        return None

    contacts = by_kind["native_combat_contact"]
    identities = by_kind["native_combat_identity"]
    diagnostics = by_kind["native_combat_diagnostics"]
    health_writes = by_kind["native_combat_health_write"]

    ordered = sorted(contacts.items(),
                     key=lambda kv: (kv[1][0].get("qpc") or 0, kv[0][1] or 0))
    drops, player_damage, enemies = [], [], set()
    reason_hist = {}
    player_contacts = unattributed = pairing_errors = 0
    enemy_hp = {}

    def note_reasons(key, enemy_side):
        rows = diagnostics.get(key) or []
        if not rows:
            return
        flags = int(rows[0].get("flags") or 0)
        codes = [(flags >> shift) & 15 for shift in (0, 4, 8, 12)]
        for code in (codes[enemy_side], codes[2 + enemy_side]):
            name = REASON_NAMES[code] if code < len(REASON_NAMES) else f"code_{code}"
            if name != "valid":
                reason_hist[name] = reason_hist.get(name, 0) + 1

    for key, rows in ordered:
        e = rows[0]
        vals = e.get("values") or []
        if len(vals) < 16:
            pairing_errors += 1
            continue
        actors = [limbs_of(vals[0:4]), limbs_of(vals[4:8])]
        if actors[0] == actors[1]:
            continue  # self-pair; the footer counts these separately
        ident_rows = identities.get(key) or []
        paired = len(ident_rows) == 1 and len(rows) == 1
        if not paired:
            pairing_errors += 1
        side = player_side(actors, ident_rows[0] if ident_rows else None)
        if side is None:
            unattributed += 1
            continue
        player_contacts += 1
        enemy = 1 - side
        enemy_addr = actors[enemy]
        enemies.add(enemy_addr)
        note_reasons(key, enemy)
        if not paired:
            continue  # HP evidence needs the full identity-paired batch
        flags = int(e.get("flags") or 0)
        qpc = e.get("qpc")

        def side_hp(s):
            before = int(vals[8 + s * 4]) if flags & (1 << s) else None
            after = int(vals[9 + s * 4]) if flags & (1 << (s + 2)) else None
            return before, after

        hp_b, hp_a = side_hp(enemy)
        if hp_b is not None:
            prev = enemy_hp.get(enemy_addr)
            if prev is not None and hp_b < prev:
                drops.append({"actor": hex(enemy_addr), "qpc": qpc,
                              "via": "sequence", "hp_before": prev,
                              "hp_after": hp_b})
            enemy_hp[enemy_addr] = hp_b
        if hp_a is not None:
            if hp_b is not None and hp_a < hp_b:
                drops.append({"actor": hex(enemy_addr), "qpc": qpc,
                              "via": "contact", "hp_before": hp_b,
                              "hp_after": hp_a})
            enemy_hp[enemy_addr] = hp_a
        for w in health_writes.get(key) or []:
            wvals = w.get("values") or []
            if len(wvals) < 10 or limbs_of(wvals[0:4]) != enemy_addr:
                continue
            requested = signed32(wvals[8], wvals[9])
            prev = enemy_hp.get(enemy_addr)
            if prev is not None and requested < prev:
                drops.append({"actor": hex(enemy_addr), "qpc": qpc,
                              "via": "health_write", "hp_before": prev,
                              "hp_after": requested})
            enemy_hp[enemy_addr] = requested
        p_b, p_a = side_hp(side)
        if p_b is not None and p_a is not None and p_a < p_b:
            player_damage.append({"qpc": qpc, "hp_before": p_b,
                                   "hp_after": p_a})

    if header is None and footer is None and not contacts:
        return {**base, "verdict": "MALFORMED", "detail": "no parseable records",
                "elapsed_s": round(time.time() - t0, 1)}
    if drops:
        verdict = "PASS"
    elif player_contacts or unattributed:
        verdict = "UNVERIFIED"
    else:
        verdict = "NO_CONTACTS"
    if unattributed and not player_contacts and not drops:
        reason_hist["player_side_unknown"] = unattributed
    counters = footer or {}
    return {
        **base,
        "verdict": verdict,
        "complete": complete,
        "combat_total": counters.get("combat_total"),
        "combat_self": counters.get("combat_self"),
        "combat_non_player": counters.get("combat_non_player"),
        "combat_detail": counters.get("combat_detail"),
        "contacts": len(contacts),
        "player_contacts": player_contacts,
        "unattributed_contacts": unattributed,
        "pairing_errors": pairing_errors,
        "distinct_targets": len(enemies),
        "hp_drops": drops,
        "player_damage_taken": player_damage,
        "read_reasons": reason_hist,
        "elapsed_s": round(time.time() - t0, 1),
    }


# -------------------------------------------------------------------- run ---

PHASES = [
    ("check", "pwsh -Mode Check against the candidate (read-only)"),
    ("launch", "pwsh -Mode Launch (detached), short driver budgets"),
    ("wait-game", "poll for the game process"),
    ("unhide", "show the game windows before any key is sent"),
    ("menu", "simulator driver navigates; wait for gameplay log"),
    ("wait-player", "poll DSVR.log for CombatHealth equipment=1"),
    ("walk", "scripted legs toward the gate (same seed/spawn)"),
    ("combat", "go-capture + firebomb volley (R) + H taps; watch footer"),
    ("verdict", "analyze the capture; PASS needs an enemy HP drop"),
    ("close", "request Finish; the protected runner closes and restores"),
]


def cmd_plan():
    for i, (name, what) in enumerate(PHASES, 1):
        print(f"{i}. {name}: {what}")
    return 0


def newest_trace(root=GAME_DIR):
    fs = sorted(root.glob("DSVR-trace-*.jsonl"), key=lambda p: p.stat().st_mtime)
    return fs[-1] if fs else None


def cmd_analyze(path=None):
    trace = Path(path) if path else newest_trace()
    if trace is None or not trace.exists():
        print(f"no DSVR-trace-*.jsonl under {GAME_DIR}")
        return 2
    print(json.dumps(verdict_for(trace), indent=1))
    return 0


def timed(name, fn):
    t0 = time.time()
    out = fn()
    print(f"[{name}] {time.time() - t0:.1f}s", flush=True)
    return out


def cmd_run(candidate):
    global ACTIVE_GAME_PID
    stamp = time.strftime("%Y%m%d-%H%M%S")
    run_dir = REPO / "build" / "verification" / f"autopilot-{stamp}"
    run_dir.mkdir(parents=True, exist_ok=True)
    receipt = run_dir / "receipt.json"
    base = ["-Receipt", str(receipt), "-CandidateReceipt", str(candidate),
            "-InputMode", "OpenXRSimulator"]

    def phase_check():
        r = pwsh_runner("-Mode", "Check", *base)
        if r.returncode != 0:
            raise SystemExit(f"CHECK FAIL: {r.stdout} {r.stderr}")
    timed("check", phase_check)

    launched = False

    def phase_launch():
        nonlocal launched
        # Fire-and-forget: the Launch invocation spawns the detached Run
        # child and exits; waiting on its pipes can hang on inherited
        # handles, so poll for the launch record instead.
        subprocess.Popen(
            ["pwsh", "-NoProfile", "-File", str(RUNNER), "-Mode", "Launch",
             *base, "-WindowMinutes", "30", "-MenuBudgetSeconds", "180",
             "-SimulatorSoakSeconds", "180"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=REPO)
        launched = True
        poll(lambda: (run_dir / "receipt.json.launch.json").exists()
             or None, 60, interval=0.5, what="launch record")

    pid = None

    def phase_wait_game():
        global ACTIVE_GAME_PID
        nonlocal pid
        def owned_game():
            state = read_receipt(receipt)
            if not state:
                return None
            if state.get("state") == "RecoveryBlocked":
                raise RuntimeError("protected runner blocked: inspect receipt and stderr")
            identities = state.get("pids") or []
            if len(identities) != 1:
                return None
            candidate_pid = int(identities[0]["id"])
            return candidate_pid if candidate_pid == game_pid() else None

        pid = poll(owned_game, 120, what="receipt-owned game process")
        ACTIVE_GAME_PID = pid
        return pid
    log = LogTail()

    def phase_unhide():
        # The harness hides the game window while it consumes global input;
        # the user must be able to see it before any key is sent. The window
        # can lag the process, so wait for one to exist.
        u = ctypes.windll.user32
        wins = poll(lambda: enum_windows(pid) or None, 30, interval=0.5,
                    what="game window")
        shown = []
        for hwnd, title in wins:
            u.ShowWindow(hwnd, 5)
            shown.append(title or "(untitled)")
        u.SetForegroundWindow(wins[0][0])
        poll(lambda: game_foreground(u), 5,
             what="owned game foreground")
        print(f"unhidden game windows: {shown}", flush=True)

    def phase_menu():
        # The simulator driver owns controller-ray menu navigation. Historical
        # E presses were coincident with its clicks, not proven causal.
        log.wait_for("gameplay=1", 180)

    def phase_player():
        # Fail fast if the equipment read never binds the local player:
        # combat evidence is meaningless without it.
        log.wait_for("CombatHealth: equipment=1", 90)

    def phase_walk():
        # Scripted legs (same seed/spawn as runs 17-22) with log sanity gates.
        for vk, secs in ((VK["W"], 3.0), (VK["W"], 2.0), (VK["W"], 2.0)):
            hold(vk, secs)
            time.sleep(0.5)

    capture = {}

    def phase_combat():
        go = GAME_DIR / f"DSVR-trace-go-{pid}.request"
        go.write_text("")
        time.sleep(2)
        for _ in range(4):  # firebomb volley: R is Use Item (proven)
            key(VK["R"], 0.15)
            time.sleep(2.0)
        for _ in range(6):
            key(VK["H"], 0.35)
            time.sleep(0.65)
        # Footer lands ~20 s after the drop; find the newest capture for pid.
        def newest():
            fs = sorted(GAME_DIR.glob(f"DSVR-trace-{pid}-*.jsonl"),
                        key=lambda p: p.stat().st_mtime)
            if not fs:
                return None
            with fs[-1].open("rb") as stream:
                stream.seek(max(stream.seek(0, os.SEEK_END) - 2048, 0))
                tail = stream.read()
            return fs[-1] if b'"event":"footer"' in tail else None
        capture["path"] = poll(newest, 90, interval=1.0, what="capture footer")

    def phase_verdict():
        v = verdict_for(capture["path"])
        (run_dir / "verdict.json").write_text(json.dumps(v, indent=1))
        print(json.dumps(v, indent=1), flush=True)
        return v

    def phase_close():
        state = poll(lambda: read_receipt(receipt), 20, interval=0.5,
                     what="protected runner receipt")
        if not state:
            raise RuntimeError("no receipt; inspect launch logs before recovery")
        if state.get("state") != "Restored":
            r = pwsh_runner("-Mode", "Finish", *base)
            if r.returncode != 0:
                raise RuntimeError(f"Finish failed: {r.stdout} {r.stderr}")

        def restored():
            current = read_receipt(receipt)
            if current and current.get("state") == "RecoveryBlocked":
                raise RuntimeError("recovery blocked; inspect receipt and runner stderr")
            return current if current and current.get("state") == "Restored" else None

        poll(restored, 240, interval=1.0, what="protected runner restore")

    verdict = None
    try:
        timed("launch", phase_launch)
        timed("wait-game", phase_wait_game)
        timed("unhide", phase_unhide)
        timed("menu", phase_menu)
        timed("wait-player", phase_player)
        timed("walk", phase_walk)
        timed("combat", phase_combat)
        verdict = timed("verdict", phase_verdict)
    finally:
        try:
            if launched:
                timed("close", phase_close)
        finally:
            ACTIVE_GAME_PID = None
    if not verdict["complete"]:
        raise RuntimeError("capture lacks footer despite restored transaction")
    print(f"DONE verdict={verdict['verdict']} run_dir={run_dir}")
    return 0 if verdict["verdict"] == "PASS" else 1


def main(argv):
    if len(argv) < 2 or argv[1] == "plan":
        return cmd_plan()
    if argv[1] == "analyze" and len(argv) in (2, 3):
        return cmd_analyze(argv[2] if len(argv) == 3 else None)
    if argv[1] == "run":
        cand = argv[2] if len(argv) > 2 else str(DEFAULT_CANDIDATE)
        return cmd_run(cand)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
