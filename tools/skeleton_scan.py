#!/usr/bin/env python3
"""DSVR skeleton-RE log scanner (offline, read-only).

Parses DSVR.log for:
  - ArmIK weapon/offhand target lines (1 Hz, log-only phase)
  - Diagnostics player-pos snapshots (F9 markers)
  - Presence model pointers (PlayerIns+0x60 / +0x850)

Outputs a calibration report used to verify the IK rig before any bone
write is allowed:
  - wrist/elbow reach plausibility (upper 0.28 + forearm 0.30 = 0.58 max)
  - tip-speed distribution (contact_speed_min sanity)
  - model-pointer stability (do +0x60/+0x850 stay stable per session?)

Usage:
  python tools/skeleton_scan.py --log DSVR.log [--self-test]
Exit 0 = parsed OK, 2 = no IK lines found (still OK, just nothing to do).
"""
import argparse
import math
import re
import sys

ARM_RE = re.compile(
    r"ArmIK:\s+(weapon|offhand)\s+wrist=([-\d.]+),([-\d.]+),([-\d.]+)\s+"
    r"elbow=([-\d.]+),([-\d.]+),([-\d.]+)\s+reached=(\d)"
    r"(?:\s+tipSpeed=([-\d.]+)\s+kind=(\d+))?"
)
POS_RE = re.compile(
    r"Diagnostics:\s+player physics pos\s+=\s+([-\d.]+),\s*([-\d.]+),\s*([-\d.]+)"
)
MODEL_RE = re.compile(
    r"Presence:\s+primary.*model\s+([0-9a-fA-Fx]+).*second.*model\s+([0-9a-fA-Fx]+)"
)
SKELETON_RE = re.compile(
    r"Skeleton:\s+player=([0-9a-fA-Fx]+)\s+model60=([0-9a-fA-Fx]+)\s+"
    r"model850=([0-9a-fA-Fx]+)\s+yaw=([-\d.]+)"
)

UPPER = 0.28
FOREARM = 0.30
MAX_REACH = UPPER + FOREARM + 1e-3


def parse(path):
    arms = []
    positions = []
    models = []
    skeletons = []
    with open(path, "r", errors="replace") as f:
        for line in f:
            m = ARM_RE.search(line)
            if m:
                side = m.group(1)
                wrist = tuple(map(float, m.group(2, 3, 4)))
                elbow = tuple(map(float, m.group(5, 6, 7)))
                reached = int(m.group(8))
                tip = float(m.group(9)) if m.group(9) is not None else None
                arms.append(
                    {"side": side, "wrist": wrist, "elbow": elbow,
                     "reached": reached, "tip": tip}
                )
                continue
            m = POS_RE.search(line)
            if m:
                positions.append(tuple(map(float, m.group(1, 2, 3))))
                continue
            m = MODEL_RE.search(line)
            if m:
                models.append((m.group(1), m.group(2)))
                continue
            m = SKELETON_RE.search(line)
            if m:
                skeletons.append(
                    {"player": m.group(1), "m60": m.group(2),
                     "m850": m.group(3), "yaw": float(m.group(4))}
                )
    return arms, positions, models, skeletons


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def report(arms, positions, models, skeletons):
    lines = []
    lines.append(f"arms={len(arms)} positions={len(positions)} "
                 f"models={len(models)} skeletons={len(skeletons)}")
    if not arms:
        lines.append("NOTE: no ArmIK lines (IK disabled or no tracking).")
        return lines, 2
    unreached = sum(1 for a in arms if not a["reached"])
    lines.append(f"unreached={unreached}/{len(arms)} "
                 f"({100.0*unreached/max(1,len(arms)):.1f}% beyond 0.58m)")
    tips = [a["tip"] for a in arms if a["tip"] is not None]
    if tips:
        lines.append(f"tipSpeed min={min(tips):.2f} max={max(tips):.2f} "
                     f"mean={sum(tips)/len(tips):.2f} "
                     f"(contact_min=0.8; resting should read ~0)")
    # Elbow-from-wrist sanity: elbow must be within upper length of a
    # plausible shoulder. Without shoulder in the log we check wrist-elbow
    # distance <= forearm + slack (catches NaN/teleport garbage).
    bad = 0
    for a in arms:
        d = dist(a["wrist"], a["elbow"])
        if not (math.isfinite(d) and d <= FOREARM + 0.05):
            bad += 1
    lines.append(f"wrist-elbow>{FOREARM+0.05:.2f}m: {bad} "
                 + ("FAIL: teleport/NaN leak" if bad else "OK"))
    if models:
        uniq = set(models)
        lines.append(f"model pointers: {len(uniq)} unique "
                     + ("STABLE" if len(uniq) == 1 else "CHANGED (re-load?)"))
    if skeletons:
        yaws = [s["yaw"] for s in skeletons]
        lines.append(f"skeleton probes={len(skeletons)} "
                     f"yaw range {min(yaws):.2f}..{max(yaws):.2f}")
    else:
        lines.append("NOTE: no Skeleton: probes (need v0.15.1+ live run).")
    code = 1 if bad else 0
    return lines, code


def self_test():
    sample = (
        "ArmIK: weapon wrist=0.20,1.30,0.40 elbow=0.10,1.35,0.20 "
        "reached=1 tipSpeed=2.50 kind=1 (log-only=1; bone writes pending)\n"
        "ArmIK: offhand wrist=-0.20,1.30,0.40 elbow=-0.10,1.35,0.20 reached=1\n"
        "Diagnostics: player physics pos = 4.214, -10.020, -63.781\n"
        "Presence: primary (weapon-side) model 000001234 kept visible; "
        "second (body) model 000005678 hidden (hide_second_model=1; flip)\n"
        "Skeleton: player=00000AAA model60=000001234 model850=000005678 yaw=1.57\n"
    )
    import tempfile, os
    fd, p = tempfile.mkstemp(suffix=".log")
    try:
        with os.fdopen(fd, "w") as f:
            f.write(sample)
        arms, pos, mods, skel = parse(p)
        assert len(arms) == 2, arms
        assert len(pos) == 1, pos
        assert len(mods) == 1, mods
        assert len(skel) == 1, skel
        lines, code = report(arms, pos, mods, skel)
        assert code == 0, lines
        print("skeleton_scan self-test PASS")
        print("\n".join(lines))
        return 0
    finally:
        os.unlink(p)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", default="DSVR.log")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    try:
        arms, pos, mods, skel = parse(args.log)
    except FileNotFoundError:
        print(f"FAIL: log not found: {args.log}")
        return 1
    lines, code = report(arms, pos, mods, skel)
    print("\n".join(lines))
    # Exit 2 = no IK data (not a failure, just nothing to calibrate).
    return code


if __name__ == "__main__":
    sys.exit(main())
