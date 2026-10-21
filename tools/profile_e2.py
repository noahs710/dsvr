"""Reproducible retained-log evidence, not a controlled performance comparison.

python tools/profile_e2.py --overhead build/n2-overhead.csv --out build/no-hmd-e2-comparison.json
CPU log snapshots are rounded, inclusive and not frame distributions. Difference
only calls/total estimates, never cumulative maxima. Reject resets/nonmonotonicity.
"""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import math
import re
from collections import defaultdict
from pathlib import Path

CPU = re.compile(r"CpuPerformance: stage=(.*?) calls=(\d+) avg=([\d.]+)ms max=([\d.]+)ms")
FPS = re.compile(r"RateEvidence: game_fps=([\d.]+)")
DEFAULT_LOGS = ["build/e2-profile-native.log", "build/e2-memory-native.log",
                "build/e2-npc-desktop-native.log"]


def percentile(values, p):
    """Linearly interpolated percentile (offline only)."""
    values = sorted(values)
    if not values:
        return None
    at = (len(values) - 1) * p / 100
    lo, hi = math.floor(at), math.ceil(at)
    return values[lo] + (values[hi] - values[lo]) * (at - lo)


def summary(values):
    return {"n": len(values), "min": min(values) if values else None,
            "median": percentile(values, 50), "p95": percentile(values, 95),
            "max": max(values) if values else None}


def snapshot_delta(before, after):
    """Rounded cumulative call/mean snapshots; maxima cannot be differenced."""
    n0, avg0 = before
    n1, avg1 = after
    count = n1 - n0
    total = n1 * avg1 - n0 * avg0
    # avg printed to .001ms: subtraction amplifies uncertainty for small deltas.
    uncertainty = (n1 + n0) * 0.0005
    if count <= 0 or total < -uncertainty:
        return None
    return {"calls": count, "mean_ms": max(0.0, total) / count,
            "rounding_error_ms": uncertainty / count}


def analyze_log(path):
    raw = path.read_bytes()
    previous = {}
    deltas, maxima, errors = defaultdict(list), defaultdict(list), defaultdict(list)
    fps, rejected = [], 0
    for line in raw.decode("utf-8", errors="replace").splitlines():
        match = CPU.search(line)
        if match:
            stage, n, avg, maximum = match.groups()
            sample = (int(n), float(avg))
            if stage in previous:
                delta = snapshot_delta(previous[stage], sample)
                if delta:
                    deltas[stage].append(delta["mean_ms"])
                    errors[stage].append(delta["rounding_error_ms"])
                else:
                    rejected += 1
            previous[stage] = sample
            maxima[stage].append(float(maximum))
        match = FPS.search(line)
        if match:
            fps.append(float(match[1]))
    return {"path": str(path), "sha256": hashlib.sha256(raw).hexdigest(),
            "bytes": len(raw), "rate_window_fps": summary(fps),
            "rejected_nonmonotonic_or_reset_pairs": rejected,
            "stages": {stage: {"interval_mean_ms_NOT_frame_percentiles": summary(deltas[stage]),
                                "largest_rounding_error_ms": max(errors[stage], default=None),
                                "largest_cumulative_max_ms_NOT_interval_max": max(maxima[stage])}
                       for stage in sorted(maxima)}}


def overhead(path):
    groups = defaultdict(list)
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            groups[row["threads"]].append(float(row["delta_ns_per_scope"]))
    return {threads: {"paired_delta_ns_per_scope": summary(samples),
                      "eleven_serial_scopes_equivalent_us_NOT_game_frame":
                          {key: value * 11 / 1000 if value is not None else None
                           for key, value in summary(samples).items() if key != "n"}}
            for threads, samples in groups.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="*", default=DEFAULT_LOGS)
    parser.add_argument("--overhead", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    report = {"limits": ["Retained e2 runs are different uncontrolled scenes/configurations; no causal A/B claim.",
                         "CPU stages are inclusive and cannot be summed; old labels omit restore/poll semantics.",
                         "p95 of interval means/FPS windows is NOT p95 frame latency.",
                         "GPU queries cover staged copy/blits only, NOT native scene; no new GPU measurements.",
                         "Offline scope harness excludes native reads, hooks, render, XR and real cadence."],
              "logs": [analyze_log(Path(path)) for path in args.logs]}
    if args.overhead:
        report["offline_overhead"] = overhead(args.overhead)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(args.out)


if __name__ == "__main__":
    main()
