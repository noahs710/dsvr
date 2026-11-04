#!/usr/bin/env python3
"""State-driven autopilot for DSVR sim validation runs.

Drives the protected OpenXR-Simulator transaction on state oracles (mod log
lines, live trace records) instead of screenshot steering. See
docs/superpowers/specs/2026-11-04-autopilot-design.md.

Subcommands:
  plan                 print phases, no side effects
  analyze TRACE        offline verdict on a saved capture (seconds)
  run                  full transaction: check, launch, menu, walk, combat,
                       verdict, close, restore; writes verdict.json

Proof bar: PASS only when a paired distinct-actor contact shows the target
losing HP. Counters alone never pass.
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
DEFAULT_CANDIDATE = REPO / "build/verification/synthetic-hmd-game-20261104-r12/candidate-final-receipt.json"

VK = {"E": 0x45, "W": 0x57, "A": 0x41, "S": 0x53, "D": 0x44,
      "H": 0x48, "U": 0x55, "R": 0x52, "SPACE": 0x20}


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
    u = ctypes.windll.user32
    u.keybd_event(vk, 0, 0, 0)
    time.sleep(down_s)
    u.keybd_event(vk, 0, 2, 0)


def hold(vk, seconds):
    u = ctypes.windll.user32
    u.keybd_event(vk, 0, 0, 0)
    time.sleep(seconds)
    u.keybd_event(vk, 0, 2, 0)


def mouse_yaw(hwnd, dx):
    """Click the sim preview to capture the mouse, then yaw by dx pixels."""
    from ctypes import wintypes
    u = ctypes.windll.user32
    r = wintypes.RECT()
    u.GetWindowRect(hwnd, ctypes.byref(r))
    cx, cy = (r.left + r.right) // 2, (r.top + r.bottom) // 2
    u.SetCursorPos(cx, cy)
    time.sleep(0.2)
    u.mouse_event(0x0002, 0, 0, 0, 0)
    time.sleep(0.05)
    u.mouse_event(0x0004, 0, 0, 0, 0)
    time.sleep(0.3)
    u.mouse_event(0x0001, dx, 0, 0, 0)
    time.sleep(0.4)


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
        self.pos = path.stat().st_size if path.exists() else 0

    def wait_for(self, needle, deadline_s):
        def probe():
            if not self.path.exists():
                return None
            with open(self.path, "rb") as f:
                f.seek(self.pos)
                data = f.read()
                self.pos = f.tell()
            return needle.encode() in data or None

        return poll(probe, deadline_s, what=f"log line {needle!r}")


def request_shot(sim_dir: Path, tag: str, deadline_s=45) -> Path:
    bmp = sim_dir / "screenshot.bmp"
    old = bmp.stat().st_mtime if bmp.exists() else 0
    (sim_dir / "screenshot_request.json").write_text(
        json.dumps({"capture_id": tag, "eye": "both", "layer": "all", "after_sync": 0}))

    def fresh():
        return bmp.exists() and bmp.stat().st_mtime != old

    poll(fresh, deadline_s, interval=0.5, what="sim screenshot")
    return bmp


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
    ("menu", "E presses gated on the InputRoute gameplay flip"),
    ("wait-player", "poll DSVR.log for CombatHealth equipment=1"),
    ("walk", "scripted legs toward the gate (same seed/spawn)"),
    ("combat", "go-capture + firebomb volley (R) + H taps; watch footer"),
    ("verdict", "analyze the capture; PASS needs an enemy HP drop"),
    ("close", "Finish, WM_CLOSE, wait exit, Restore (always runs)"),
]


def cmd_plan():
    for i, (name, what) in enumerate(PHASES, 1):
        print(f"{i}. {name}: {what}")
    return 0


def cmd_analyze(path):
    print(json.dumps(verdict_for(Path(path)), indent=1))
    return 0


def timed(name, fn):
    t0 = time.time()
    out = fn()
    print(f"[{name}] {time.time() - t0:.1f}s", flush=True)
    return out


def cmd_run(candidate):
    stamp = time.strftime("%Y%m%d-%H%M")
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

    def phase_launch():
        # Fire-and-forget: the Launch invocation spawns the detached Run
        # child and exits; waiting on its pipes can hang on inherited
        # handles, so poll for the launch record instead.
        subprocess.Popen(
            ["pwsh", "-NoProfile", "-File", str(RUNNER), "-Mode", "Launch",
             *base, "-WindowMinutes", "30", "-MenuBudgetSeconds", "60",
             "-SimulatorSoakSeconds", "180"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=REPO)
        poll(lambda: (run_dir / "receipt.json.launch.json").exists()
             or None, 60, interval=0.5, what="launch record")
    timed("launch", phase_launch)

    pid = None

    def phase_wait_game():
        nonlocal pid
        pid = poll(game_pid, 120, what="game process")
        return pid
    timed("wait-game", phase_wait_game)
    sim_dir = run_dir / "simxr-data"
    log = LogTail()

    def phase_unhide():
        # The harness hides the game window while it consumes global input;
        # the user must be able to see it before any key is sent.
        u = ctypes.windll.user32
        shown = []
        for hwnd, title in enum_windows(pid):
            u.ShowWindow(hwnd, 5)
            shown.append(title or "(untitled)")
        print(f"unhidden game windows: {shown}", flush=True)
    timed("unhide", phase_unhide)

    def phase_menu():
        # E navigates dialog -> offline notice -> main menu (Continue loads
        # the save). Gate each press on the InputRoute gameplay flip; the
        # player cannot exist before this, so it comes before wait-player.
        for attempt in range(4):
            key(VK["E"])
            try:
                log.wait_for("gameplay=1", 12)
                return
            except Deadline:
                print(f"menu: E{attempt + 1} no gameplay yet", flush=True)
        raise Deadline("menu navigation never reached gameplay")
    timed("menu", phase_menu)

    def phase_player():
        # Fail fast if the equipment read never binds the local player:
        # combat evidence is meaningless without it.
        log.wait_for("CombatHealth: equipment=1", 90)
    timed("wait-player", phase_player)

    def phase_walk():
        # Scripted legs (same seed/spawn as runs 17-22) with log sanity gates.
        for vk, secs in ((VK["W"], 3.0), (VK["W"], 2.0), (VK["W"], 2.0)):
            hold(vk, secs)
            time.sleep(0.5)
    timed("walk", phase_walk)

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
            tail = fs[-1].read_bytes()[-2048:]
            return fs[-1] if b'"event":"footer"' in tail else None
        capture["path"] = poll(newest, 90, interval=1.0, what="capture footer")
    timed("combat", phase_combat)

    def phase_verdict():
        v = verdict_for(capture["path"])
        (run_dir / "verdict.json").write_text(json.dumps(v, indent=1))
        print(json.dumps(v, indent=1), flush=True)
        return v

    def phase_close():
        pwsh_runner("-Mode", "Finish", *base)
        if pid:
            for hwnd, title in enum_windows(pid):
                ctypes.windll.user32.PostMessageW(hwnd, 0x10, 0, 0)
        poll(lambda: game_pid() is None, 120, interval=1.0,
             what="game exit")
        t0 = time.time()
        r = pwsh_runner("-Mode", "Restore", *base, timeout=170)
        print(f"restore exit={r.returncode} in "
              f"{time.time() - t0:.1f}s", flush=True)
        if r.returncode != 0:
            raise SystemExit(f"RESTORE FAIL: {r.stdout} {r.stderr}")

    verdict = None
    try:
        timed("unhide", phase_unhide)
        timed("menu", phase_menu)
        timed("wait-player", phase_player)
        timed("walk", phase_walk)
        timed("combat", phase_combat)
        verdict = timed("verdict", phase_verdict)
    finally:
        timed("close", phase_close)
    print(f"DONE verdict={verdict['verdict']} run_dir={run_dir}")
    return 0 if verdict["verdict"] == "PASS" else 1


def main(argv):
    if len(argv) < 2 or argv[1] == "plan":
        return cmd_plan()
    if argv[1] == "analyze" and len(argv) == 3:
        return cmd_analyze(argv[2])
    if argv[1] == "run":
        cand = argv[2] if len(argv) > 2 else str(DEFAULT_CANDIDATE)
        return cmd_run(cand)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
